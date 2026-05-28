// MDLXExporter — Geoset animation extractor implementation
//
// For each mesh in the IR model, emits one ir::IRModel::GeosetAnim entry.
// Handles:
//   * Visibility tracks on the mesh INode (KGAO) — via extractVisibilityTrack
//   * Wc3VertexMod modifier (KGAC + Color flag + DropShadow flag)
//
// Wc3VertexMod is a scripted-plugin modifier (class="Wc3VertexMod") with
// parameters UsesColor (bool), UsesDropShadow (bool), VertexColor (color).
// The VertexColor parameter can have a HYBRIDINTERP_COLOR animation
// controller attached; if so it carries the KGAC data.
//
// ClassID check is unreliable for scripted plugins (MSPlugin wraps
// them with runtime-assigned IDs) — we detect by class-name string
// instead, with a property-probe fallback.
//
// CHANGES 2026-04-18 (Patch B):
//   * Wc3VertexMod modifier detection (name-based, scripted-plugin-safe)
//   * Static UsesColor / UsesDropShadow / VertexColor extraction
//   * Animated VertexColor → ir::ColorTrack (KGAC) with GlobalSeq tagging
//   * RGB→BGR swap on KGAC keys (animated only); static color stays RGB
//   * Sparse emit: only push a GeosetAnim if there's something to say

#include "geoset_anim_extractor.h"
#include "visibility_track_helper.h"
#include <animation/global_sequence_helper.h>

#include <inode.h>
#include <max.h>
#include <iparamb2.h>
#include <modstack.h>
#include <istdplug.h>
#include <control.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>

// Debug logging to a file shared with other extractors.
// The first call within an export truncates the log; subsequent calls
// append. We rely on the fact that each export is a fresh invocation
// of the plugin, so the static flag resets implicitly per DLL load.
// To force reset mid-run, call ga_log_reset() first.
static bool g_logFirstWrite = true;

static void ga_log_reset() {
    g_logFirstWrite = true;
}

static void ga_log(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    char tempPath[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tempPath);
    if (n == 0 || n > MAX_PATH) return;

    char logPath[MAX_PATH];
    snprintf(logPath, sizeof(logPath), "%smdlx_material_debug.log", tempPath);

    // First write of this export session: truncate. Otherwise append.
    const char* mode = g_logFirstWrite ? "w" : "a";
    g_logFirstWrite = false;

    FILE* f = nullptr;
    if (fopen_s(&f, logPath, mode) == 0 && f) {
        fputs(buf, f);
        fflush(f);  // critical: survive crash
        fclose(f);
    }
}

namespace mdx_extract {

namespace {

// ─── Wc3VertexMod detection ──────────────────────────────────────────
//
// Scripted plugins get wrapped by MSPlugin which assigns a runtime ClassID
// that differs from the declared one (see KGAC_EXPORTER_HANDOFF.md §1).
// They ALSO have multiple names:
//   * Modifier::GetClassName(s)            → Track-View instance name,
//                                            often "<Name>[instance]"
//   * ClassDesc::ClassName()               → MaxScript-visible class name
//                                            (may be localized in newer Max)
//   * ClassDesc::InternalName()            → fixed parsable name
//   * The plugin script's "name:" attr     → Another name entirely
//                                            ("Wc3 Vertex Color")
//   * MaxScript `classOf` string           → Depends on Max version and
//                                            UseOnlyInternalNameForMAXScriptExposure
//
// We match against a list of all known variants. On a detection miss,
// we log everything we could find so we can extend the list.

static const wchar_t* kWc3VertexModNames[] = {
    L"Wc3VertexMod",          // MaxScript classOf observed
    L"Wdx_Wc3VertexMod",      // raw plugin internal name
    L"Wc3 Vertex Color",      // plugin "name:" attribute
    nullptr
};

bool matchesVertexModName(const wchar_t* str) {
    if (!str) return false;
    for (int i = 0; kWc3VertexModNames[i]; i++) {
        if (wcsstr(str, kWc3VertexModNames[i]) != nullptr) return true;
    }
    return false;
}

bool isWc3VertexMod(Modifier* mod) {
    if (!mod) return false;
    MSTR cname;
    mod->GetClassName(cname);
    return matchesVertexModName(cname.data());
}

// Verbose log of what a modifier looks like — called during stack walk
// so we can see every modifier's class name and decide if our matching
// needs to be extended.
void logModifierDebug(Modifier* mod, int stackPos, int modIdx, const char* nodeName) {
    if (!mod) {
        ga_log("    [stack=%d mod=%d] <null>\n", stackPos, modIdx);
        return;
    }
    MSTR cname;
    mod->GetClassName(cname);
    const MCHAR* cstr = cname.data();
    Class_ID cid = mod->ClassID();
    SClass_ID sid = mod->SuperClassID();
    ga_log("    [stack=%d mod=%d] '%s' name='%ls' ClassID=(0x%x,0x%x) Super=0x%x\n",
           stackPos, modIdx, nodeName,
           cstr ? cstr : L"?",
           (unsigned)cid.PartA(), (unsigned)cid.PartB(),
           (unsigned)sid);
}

// Walk the modifier stack (IDerivedObject chain) and return the first
// Wc3VertexMod modifier, or nullptr.
//
// NOTE: We do not use try/catch — under /EHsc (Max's default build flag)
// these do NOT catch access violations. Safety comes from NULL checks
// and sanity bounds, not exception handling.
Modifier* findVertexMod(INode* node) {
    if (!node) return nullptr;

    // Get node name for log readability
    const MCHAR* nodeNameW = node->GetName();
    char nodeName[128] = "?";
    if (nodeNameW) {
        WideCharToMultiByte(CP_UTF8, 0, nodeNameW, -1,
                            nodeName, sizeof(nodeName), nullptr, nullptr);
    }

    Object* obj = node->GetObjectRef();
    int safetyLimit = 32;
    int stackPos = 0;
    while (obj && safetyLimit-- > 0) {
        if (obj->SuperClassID() != GEN_DERIVOB_CLASS_ID) {
            ga_log("    obj @ stack %d superClassID=0x%x (not GEN_DERIVOB) — stop\n",
                   stackPos, (unsigned)obj->SuperClassID());
            break;
        }
        IDerivedObject* dobj = static_cast<IDerivedObject*>(obj);
        int n = dobj->NumModifiers();
        if (n < 0 || n > 256) break;
        ga_log("    IDerivedObject @ stack %d has %d modifier(s)\n", stackPos, n);
        for (int i = 0; i < n; i++) {
            Modifier* m = dobj->GetModifier(i);
            logModifierDebug(m, stackPos, i, nodeName);
            if (m && isWc3VertexMod(m)) {
                ga_log("    → MATCH on Wc3VertexMod at stack %d modifier %d\n",
                       stackPos, i);
                return m;
            }
        }
        Object* next = dobj->GetObjRef();
        if (next == obj) break;  // self-reference guard
        obj = next;
        stackPos++;
    }
    return nullptr;
}

// ─── Static property reads ───────────────────────────────────────────

bool readBoolParam(Modifier* mod, const wchar_t* name, bool defaultVal = false) {
    IParamBlock2* pb = nullptr;
    ParamID pid = 0;
    if (!core::anim::findAnimParam(mod, name, pb, pid)) return defaultVal;
    // IParamBlock2::GetValue has overloads for int/float/Point3&/Color& but
    // NOT for BOOL& — using BOOL (typedef int) here worked by coincidence
    // until it didn't. Use int explicitly.
    int val = defaultVal ? 1 : 0;
    Interval iv = FOREVER;
    pb->GetValue(pid, 0, val, iv);
    return val != 0;
}

Color readColorParam(Modifier* mod, const wchar_t* name) {
    IParamBlock2* pb = nullptr;
    ParamID pid = 0;
    if (!core::anim::findAnimParam(mod, name, pb, pid)) return Color(1, 1, 1);
    Color val(1, 1, 1);
    Interval iv = FOREVER;
    pb->GetValue(pid, 0, val, iv);
    // MaxScript stores color channels as 0..255 but C++ Color stores 0..1
    // Quick sanity: if any channel > 1.1, assume 0..255 and normalize.
    if (val.r > 1.1f || val.g > 1.1f || val.b > 1.1f) {
        val.r /= 255.0f;
        val.g /= 255.0f;
        val.b /= 255.0f;
    }
    return val;
}

// ─── Color key read (HYBRIDINTERP_COLOR_CLASS_ID) ────────────────────
//
// Bezier color controllers store keys as IBezPoint3Key (time, val, intan,
// outtan) where val/intan/outtan are Point3 with 0..1 RGB channels.
// Linear/TCB color don't exist natively — bezier_color is the only
// keyed color controller in Max.
//
// All keys arrive in the track with RGB→BGR swap applied so the
// MDX writer can just emit them directly.

ir::InterpolationType detectColorInterp(Control* ctrl) {
    if (!ctrl) return ir::InterpolationType::None;
    ULONG cidA = ctrl->ClassID().PartA();
    // Color controllers in Max: HYBRIDINTERP_COLOR is the standard bezier_color.
    // Point3-based variants also exist (used by some texmap properties).
    // There is no LININTERP_POINT3_CLASS_ID in the Max SDK — linear has
    // only Float/Position/Rotation/Scale, no Point3/Color variants.
    if (cidA == HYBRIDINTERP_COLOR_CLASS_ID)  return ir::InterpolationType::Bezier;
    if (cidA == HYBRIDINTERP_POINT3_CLASS_ID) return ir::InterpolationType::Bezier;
    if (cidA == TCBINTERP_POINT3_CLASS_ID)    return ir::InterpolationType::Hermite;
    // Unknown color controller type — default to bezier (safest for MDX readers)
    return ir::InterpolationType::Bezier;
}

// Extract a KGAC track from the Wc3VertexMod's VertexColor controller.
// Returns index into model.colorTracks, or -1 if no animation / not keyed.
//
// This implementation deliberately avoids IKeyControl / typed GetKey:
//   - try/catch is useless under /EHsc (won't catch access violations)
//   - IKeyControl can return NULL or an invalid wrapper for scripted
//     plugin color controllers
//   - IBezPoint3Key has layout assumptions that can differ across Max
//     versions and for MSPlugin-wrapped controllers
//
// Instead we use the Autodesk-documented "procedural sampling" path:
//   * ctrl->NumKeys()          — safe on all keyable controllers
//   * ctrl->GetKeyTime(i)      — returns the tick time of the i-th key
//   * ctrl->GetValue(t, &v, iv) — samples the controller at time t
//
// Trade-off: we lose exact Bezier tangent values (read as zero). Since
// Max interpolates bezier colors smoothly between keys at authoring
// time, the sampled values at key times are exact, and zero tangents
// produce a visually identical result for most MDX readers.
int32_t extractColorTrack(Modifier* mod, ir::IRModel& model,
                          int meshIdx, const char* meshName)
{
    ga_log("    [extractColorTrack] entered mesh[%d] '%s'\n", meshIdx, meshName);

    if (!mod) {
        ga_log("    [extractColorTrack] mod is NULL — abort\n");
        return -1;
    }

    Control* ctrl = core::anim::getParamControllerDirect(mod, L"VertexColor");
    ga_log("    [extractColorTrack] getParamControllerDirect → %p\n", (void*)ctrl);
    if (!ctrl) {
        ga_log("    mesh[%d] '%s' color: no controller\n", meshIdx, meshName);
        return -1;
    }

    // Read ClassID via a helper that's explicit about NULL
    Class_ID cid = ctrl->ClassID();
    ga_log("    [extractColorTrack] ctrl ClassID=(0x%x,0x%x)\n",
           (unsigned)cid.PartA(), (unsigned)cid.PartB());

    // NumKeys() is safe on all keyable controllers; returns 0 for procedural
    int numKeys = ctrl->NumKeys();
    ga_log("    [extractColorTrack] NumKeys=%d\n", numKeys);
    if (numKeys <= 0) {
        ga_log("    mesh[%d] '%s' color: controller but 0 keys\n", meshIdx, meshName);
        return -1;
    }

    ir::Track<Color> track;
    track.interpolation = detectColorInterp(ctrl);

    // Sample each key via GetKeyTime + GetValue — the Autodesk-recommended
    // "procedural controller" path. Works on any controller type, crash-free.
    for (int i = 0; i < numKeys; i++) {
        TimeValue t = ctrl->GetKeyTime(i);
        Point3 val(1.0f, 1.0f, 1.0f);
        Interval iv = FOREVER;
        ctrl->GetValue(t, &val, iv);

        ir::Keyframe<Color> key;
        key.time = t;
        // Max stores RGB, MDX stores BGR — swap now so writer emits directly
        key.value = Color(val.z, val.y, val.x);
        // Tangents left at default (zero) — see function comment
        track.keys.push_back(key);

        ga_log("        key[%d] t=%d RGB=(%.3f,%.3f,%.3f)\n",
               i, (int)t, val.x, val.y, val.z);
    }

    ga_log("    [extractColorTrack] %d keys sampled\n", (int)track.keys.size());

    // ── Global Sequence detection (inline, Point3-safe) ────────────────
    //
    // We do NOT use core::anim::detectAndRegisterGlobalSeq here — that
    // helper reads keys as IBezFloatKey, which is smaller than
    // IBezPoint3Key. On a Point3/Color controller this overruns the
    // struct and produces an access violation.
    //
    // Since we've already sampled the key times via ctrl->GetKeyTime()
    // into track.keys[], we can compute the GS duration from those
    // directly — no further SDK calls needed.
    //
    // A Global Sequence qualifies iff after-ORT is CYCLE or LOOP.
    int afterORT = ctrl->GetORT(ORT_AFTER);
    bool isGS = (afterORT == ORT_CYCLE || afterORT == ORT_LOOP);
    ga_log("    [extractColorTrack] afterORT=%d isGS=%d\n", afterORT, (int)isGS);

    if (isGS && !track.keys.empty()) {
        TimeValue duration = track.keys.back().time;
        int32_t gsIdx = core::anim::registerGlobalSequence(model, duration);
        if (gsIdx >= 0) {
            track.globalSequenceIndex = gsIdx;
            ga_log("    mesh[%d] '%s' color: %d keys, interp=%d, globalSeq=%d (dur=%d ticks)\n",
                   meshIdx, meshName, (int)track.keys.size(),
                   (int)track.interpolation, (int)gsIdx, (int)duration);
        }
    } else {
        ga_log("    mesh[%d] '%s' color: %d keys, interp=%d, no globalSeq\n",
               meshIdx, meshName, (int)track.keys.size(),
               (int)track.interpolation);
    }

    int32_t idx = static_cast<int32_t>(model.colorTracks.size());
    model.colorTracks.push_back(std::move(track));
    return idx;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────

void extractGeosetAnims(const std::vector<core::SceneNode>& nodes,
                        ir::IRModel& model,
                        core::ExportErrorReporter& /*reporter*/)
{
    ga_log_reset();  // truncate log — each export writes a fresh file
    ga_log("==== GeosetAnim Extractor (Patch B) ====\n");
    ga_log("IR has %d meshes, %d scene nodes\n",
           (int)model.meshes.size(), (int)nodes.size());

    // Build nodeIndex → INode* lookup from SceneNodes
    std::vector<INode*> nodeIdxToMax(model.nodes.size(), nullptr);
    for (auto& sn : nodes) {
        if (sn.nodeIndex >= 0 &&
            sn.nodeIndex < static_cast<int32_t>(nodeIdxToMax.size()) &&
            sn.maxNode)
        {
            nodeIdxToMax[sn.nodeIndex] = sn.maxNode;
        }
    }

    int animatedAlpha = 0;
    int animatedColor = 0;
    int staticColor = 0;
    int dropShadowCount = 0;
    int skipped = 0;
    int emitted = 0;

    for (size_t meshIdx = 0; meshIdx < model.meshes.size(); meshIdx++) {
        auto& mesh = model.meshes[meshIdx];

        INode* maxNode = nullptr;
        if (mesh.nodeIndex >= 0 &&
            mesh.nodeIndex < static_cast<int32_t>(nodeIdxToMax.size()))
        {
            maxNode = nodeIdxToMax[mesh.nodeIndex];
        }

        ir::IRModel::GeosetAnim ga;
        ga.meshIndex = static_cast<int32_t>(meshIdx);
        ga.alpha = 1.0f;
        ga.color = Color(1.0f, 1.0f, 1.0f);
        ga.usesColor = false;
        ga.dropShadow = mesh.hasDropShadow;
        ga.alphaTrackIndex = -1;
        ga.colorTrackIndex = -1;

        // ── Visibility / alpha (KGAO) — Patch A, unchanged ─────
        if (maxNode) {
            ga.alpha = maxNode->GetVisibility(0);
            int32_t trackIdx = extractVisibilityTrack(maxNode, model);
            if (trackIdx >= 0) {
                ga.alphaTrackIndex = trackIdx;
                int kc = (int)model.floatTracks[trackIdx].keys.size();
                animatedAlpha++;
                ga_log("  mesh[%d] '%s' alpha: animated trackIdx=%d keys=%d\n",
                       (int)meshIdx, mesh.name.c_str(), trackIdx, kc);
            }
        }

        // ── Wc3VertexMod / color (KGAC + flags) — Patch B ──────
        if (maxNode) {
            ga_log("  mesh[%d] '%s' checking for Wc3VertexMod...\n",
                   (int)meshIdx, mesh.name.c_str());
            Modifier* vmod = findVertexMod(maxNode);
            if (vmod) {
                MSTR cname;
                vmod->GetClassName(cname);
                ga_log("  mesh[%d] '%s' Wc3VertexMod found (class=%ls) — reading props\n",
                       (int)meshIdx, mesh.name.c_str(),
                       cname.data() ? cname.data() : L"?");

                ga_log("    reading UsesColor...\n");
                ga.usesColor = readBoolParam(vmod, L"UsesColor", false);
                ga_log("    UsesColor=%d\n", (int)ga.usesColor);

                ga_log("    reading UsesDropShadow...\n");
                bool modDropShadow = readBoolParam(vmod, L"UsesDropShadow", false);
                ga_log("    UsesDropShadow=%d\n", (int)modDropShadow);
                ga.dropShadow = ga.dropShadow || modDropShadow;

                ga_log("    reading VertexColor...\n");
                Color c = readColorParam(vmod, L"VertexColor");
                ga_log("    VertexColor(RGB)=(%.3f, %.3f, %.3f)\n", c.r, c.g, c.b);

                // ─── MDX GeosetAnim color convention is ASYMMETRIC ───
                // Static color (this path)        : stored as RGB in MDX
                // Animated color keys (KGAC track): stored as BGR in MDX
                // Confirmed empirically against game/Magos rendering: the
                // animated path needs the swap, the static path does NOT.
                // The NeoDex MaxScript tool has the same split (IOFixColor
                // static = RGB, animated = BGR).
                ga.color = c;
                ga_log("    stored(RGB)=(%.3f, %.3f, %.3f)\n",
                       ga.color.r, ga.color.g, ga.color.b);

                if (modDropShadow) dropShadowCount++;

                // Animated VertexColor controller → KGAC track
                ga_log("    calling extractColorTrack...\n");
                int32_t colorTrackIdx = extractColorTrack(vmod, model,
                                                          (int)meshIdx,
                                                          mesh.name.c_str());
                ga_log("    extractColorTrack returned %d\n", (int)colorTrackIdx);
                if (colorTrackIdx >= 0) {
                    ga.colorTrackIndex = colorTrackIdx;
                    animatedColor++;
                } else if (ga.usesColor) {
                    staticColor++;
                }
            } else {
                ga_log("  mesh[%d] '%s' no Wc3VertexMod\n",
                       (int)meshIdx, mesh.name.c_str());
            }
        }

        // ── Sparse emit: only push if there's actual data to carry ──
        // Skip trivial entries (fully default: white, alpha=1, no anim,
        // no flags) to match ORIG MDX behaviour which has ~13 entries
        // for 28+ meshes. Non-trivial = any of:
        //   * alpha animation
        //   * color track
        //   * UsesColor flag (even with static color)
        //   * DropShadow flag
        //   * Non-default static alpha (< 1.0)
        //   * Non-default static color (not white)
        bool hasAlphaAnim = (ga.alphaTrackIndex >= 0);
        bool hasColorAnim = (ga.colorTrackIndex >= 0);
        bool hasFlags     = (ga.usesColor || ga.dropShadow);
        bool hasNonDefaultAlpha = (ga.alpha < 0.999f);
        bool hasNonWhiteColor = (ga.color.r < 0.999f || ga.color.g < 0.999f ||
                                 ga.color.b < 0.999f);

        bool keep = hasAlphaAnim || hasColorAnim || hasFlags ||
                    hasNonDefaultAlpha || hasNonWhiteColor;

        if (keep) {
            model.geosetAnims.push_back(std::move(ga));
            emitted++;
        } else {
            skipped++;
            ga_log("  mesh[%d] '%s' trivial — skipped\n",
                   (int)meshIdx, mesh.name.c_str());
        }
    }

    ga_log("==== GeosetAnim done: %d emitted, %d skipped (trivial) ====\n",
           emitted, skipped);
    ga_log("     %d with alpha anim, %d with color anim, %d static color only, %d drop shadow\n\n",
           animatedAlpha, animatedColor, staticColor, dropShadowCount);
}

} // namespace mdx_extract
