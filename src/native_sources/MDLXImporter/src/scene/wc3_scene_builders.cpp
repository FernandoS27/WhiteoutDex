// MDLXImporter — Wc3 scene builders implementation
//
// Phase 4: Full paramblock setup for all Wc3/Blizz plugin types.
// Each builder creates plugin instances and configures their IParamBlock2
// parameters from IR data. Animation key insertion on paramblock params
// is deferred to Phase 5 (parameter animation pipeline).

#include "wc3_scene_builders.h"
#include "texture_resolver.h"
#include "../mdlx_class_ids.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cctype>
#include <utility>
#include <algorithm>

#include <scene/paramblock_reader.h>
#include <notetrck.h>
#include <bitmap.h>
#include <gencam.h>
#include <ilayer.h>
#include <ilayermanager.h>
#include <modstack.h>
#include <maxscript/maxscript.h>

// ── Crash-safe diagnostic log (appended to %TEMP%\mdlx_import_debug.log) ──
// Mirrors the MLOG helper in wc3_material_builder.cpp. Used by Wc3PopcornBuilder
// (and the import-side gate logging) so a crash mid-import still leaves a
// readable trail. Every line is flushed immediately.
static std::ofstream& popcornLog() {
    static std::ofstream s_log;
    if (!s_log.is_open()) {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring p(tmp);
        p += L"mdlx_import_debug.log";
        s_log.open(p, std::ios::app);
    }
    return s_log;
}
// One-shot helper: writes the message to the persistent log AND to the
// MAXScript Listener (when it survives). Pass narrow UTF-8 / ASCII text;
// caller is responsible for any pre-formatting.
namespace mdx_scene {
void PopcornDiagLog(const std::string& msg) {
    popcornLog() << msg << std::endl; // std::endl flushes
    // Best-effort Listener echo — won't survive a crash but helps when the
    // import succeeds.
    std::wstring w(msg.begin(), msg.end());
    mprintf(_M("%s\n"), w.c_str());
}
} // namespace mdx_scene
#define PopLog(msg) ::mdx_scene::PopcornDiagLog(msg)

// ── Helpers ────────────────────────────────────────────────

namespace {

using PBR = core::ParamBlockReader;

// Paramblock write helpers for scripted plugins (name-based lookup)

struct PBParam {
    IParamBlock2* pb = nullptr;
    ParamID id = -1;
    explicit operator bool() const { return pb != nullptr; }
};

PBParam findParam(ReferenceTarget* target, const wchar_t* name) {
    if (!target) return {};
    for (int i = 0; i < target->NumRefs(); i++) {
        auto* ref = target->GetReference(i);
        auto* pb = dynamic_cast<IParamBlock2*>(ref);
        if (!pb) continue;
        auto* desc = pb->GetDesc();
        if (!desc) continue;
        for (int j = 0; j < desc->Count(); j++) {
            ParamID pid = desc->IndextoID(j);
            const ParamDef& pd = desc->GetParamDef(pid);
            if (pd.int_name && _wcsicmp(pd.int_name, name) == 0)
                return { pb, pid };
        }
    }
    return {};
}

void pbSetInt(ReferenceTarget* t, const wchar_t* n, int v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetFloat(ReferenceTarget* t, const wchar_t* n, float v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetBool(ReferenceTarget* t, const wchar_t* n, BOOL v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetColor(ReferenceTarget* t, const wchar_t* n, Color v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetString(ReferenceTarget* t, const wchar_t* n, const MCHAR* v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}

std::wstring toWstr(const std::string& u8) {
    if (u8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring r(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, r.data(), len);
    return r;
}

// Position a node at its pivot point before parenting.
// MaxScript: pos:obj.pivot — every scene object must be placed
// at its MDX pivot point so AttachChild(keepPos=1) computes correct local offset.
void positionAtPivot(INode* node, int32_t nodeIndex,
                     const ir::IRModel& irModel) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(irModel.nodes.size()))
        return;
    Matrix3 tm;
    tm.IdentityMatrix();
    tm.SetTrans(irModel.nodes[nodeIndex].pivotPoint);
    node->SetNodeTM(0, tm);
}

// Split an MDX resource path (texture or model) into its directory prefix
// and its filename. Used by particle emitter builders to populate the
// plugin's m_*Prefix / m_*Path fields in the same shape the plugins'
// own CASC-browser callbacks produce. Without this split, re-exported
// MDX files end up with either an absolute Windows path or an empty
// directory prefix, neither of which the game engine can load.
//
// Examples:
//   "Textures\\Fireball.blp"             → prefix="Textures\\", file="Fireball.blp"
//   "Units/Undead/Necromancer.mdx"       → prefix="Units\\Undead\\", file="Necromancer.mdx"
//   "x.blp"                              → prefix="",              file="x.blp"
//   ""                                   → prefix="",              file=""
//
// Forward slashes are normalized to backslashes so both MDX and MDL
// variants (which sometimes use / in quoted paths) split correctly.
static void splitMdxPath(const std::string& src,
                         std::wstring& outPrefix,
                         std::wstring& outFilename)
{
    outPrefix.clear();
    outFilename.clear();
    if (src.empty()) return;
    std::wstring w(src.begin(), src.end());
    std::replace(w.begin(), w.end(), L'/', L'\\');
    auto lastSep = w.find_last_of(L'\\');
    if (lastSep == std::wstring::npos) {
        outFilename = std::move(w);
    } else {
        outPrefix   = w.substr(0, lastSep + 1);  // includes trailing '\'
        outFilename = w.substr(lastSep + 1);
    }
}

// Walk up the parent chain until we find one that exists in nodeMap,
// matching MaxScript relinkObjects behavior.
void attachToParent(INode* node, int32_t nodeIndex,
                    const ir::IRModel& irModel,
                    const std::vector<INode*>& nodeMap) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(irModel.nodes.size()))
        return;
    int32_t parentIdx = irModel.nodes[nodeIndex].parentIndex;
    while (parentIdx >= 0 && parentIdx < static_cast<int32_t>(nodeMap.size())
           && !nodeMap[parentIdx]) {
        parentIdx = irModel.nodes[parentIdx].parentIndex;
    }
    if (parentIdx >= 0 && parentIdx < static_cast<int32_t>(nodeMap.size()) && nodeMap[parentIdx])
        nodeMap[parentIdx]->AttachChild(node);
}

// Apply MDX node properties: billboard flags, CameraAnchored (user properties),
// and DontInherit flags (Max inheritance flags).
// Matches MaxScript setupNode() behavior.
void setupNodeProperties(INode* node, int32_t nodeIndex,
                         const ir::IRModel& irModel) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(irModel.nodes.size()))
        return;
    uint32_t flags = irModel.nodes[nodeIndex].nodeFlags;

    // Billboard flags → user properties (matching MaxScript setBillboardFlags)
    node->SetUserPropInt(_T("Billboarded"),      (flags & 0x8)  ? 1 : 0);
    node->SetUserPropInt(_T("BillboardedLockX"), (flags & 0x10) ? 1 : 0);
    node->SetUserPropInt(_T("BillboardedLockY"), (flags & 0x20) ? 1 : 0);
    node->SetUserPropInt(_T("BillboardedLockZ"), (flags & 0x40) ? 1 : 0);

    // CameraAnchored → user property
    node->SetUserPropInt(_T("CameraAnchored"),   (flags & 0x80) ? 1 : 0);

    // DontInherit flags → Max inheritance flags
    if (flags & 0x7) {
        DWORD inheritFlags = INHERIT_ALL;
        if (flags & 0x1)
            inheritFlags &= ~(INHERIT_POS_X | INHERIT_POS_Y | INHERIT_POS_Z);
        if (flags & 0x2)
            inheritFlags &= ~(INHERIT_ROT_X | INHERIT_ROT_Y | INHERIT_ROT_Z);
        if (flags & 0x4)
            inheritFlags &= ~(INHERIT_SCL_X | INHERIT_SCL_Y | INHERIT_SCL_Z);
        Control* tmCtrl = node->GetTMController();
        if (tmCtrl)
            tmCtrl->SetInheritanceFlags(inheritFlags, TRUE);
    }
}

// ParamIDs for native plugins (mirrored from plugin headers)

enum P1Params : ParamID {
    P1_PB_COUNT = 0, P1_PB_SPEED = 1, P1_PB_EMISSION_RATE = 2,
    P1_PB_LIFE = 3, P1_PB_ACCELERATION = 4,
    P1_PB_LATITUDE = 5, P1_PB_LONGITUDE = 6, P1_PB_SCALE = 7,
};

enum P2Params : ParamID {
    PB_COUNT = 0, PB_SPEED = 1, PB_VARIATION = 2, PB_LIFE = 3,
    PB_WIDTH = 4, PB_HEIGHT = 5, PB_INITVEL = 6, PB_ANGLE_Y = 7,
    PB_MIDTIME = 8,
    PB_COLOR_START = 9, PB_COLOR_MID = 10, PB_COLOR_END = 11,
    PB_ALPHA_START = 12, PB_ALPHA_MID = 13, PB_ALPHA_END = 14,
    PB_SCALE_START = 15, PB_SCALE_MID = 16, PB_SCALE_END = 17,
    PB_HEAD_LIFE_START = 18, PB_HEAD_LIFE_REPEAT = 19, PB_HEAD_LIFE_END = 20,
    PB_HEAD_DECAY_START = 21, PB_HEAD_DECAY_REPEAT = 22, PB_HEAD_DECAY_END = 23,
    PB_TAIL_LEN = 24, PB_TYPE = 25, PB_ROWS = 26, PB_COLS = 27,
    PB_TAIL_LIFE_START = 28, PB_TAIL_LIFE_REPEAT = 29, PB_TAIL_LIFE_END = 30,
    PB_TAIL_DECAY_START = 31, PB_TAIL_DECAY_REPEAT = 32, PB_TAIL_DECAY_END = 33,
    PB_SQUIRT = 34, PB_BLEND = 35, PB_GRAVITY = 36,
    PB_SORT = 37, PB_LINE_EMIT = 38, PB_UNSHADED = 39,
    PB_PRIORITY = 41, PB_UNFOGGED = 42,
    PB_MODELSPACE = 43, PB_XYQUAD = 44, PB_REPLACEABLE_ID = 45,
    PB_LONGITUDE = 46,
};

enum RibbonParams : ParamID {
    pb_height_above = 0, pb_height_below = 1,
    pb_edges_per_second = 2, pb_edge_lifetime = 3,
    pb_tex_rows = 4, pb_tex_cols = 5, pb_tex_slot = 6,
    pb_material = 7, pb_color = 8, pb_alpha = 9, pb_gravity = 10,
};

constexpr ULONG WC3P1_MODEL_PATH_IID   = 0x7B3C8D01;
constexpr ULONG WC3P1_MODEL_PREFIX_IID = 0x7B3C8D02;
constexpr ULONG WC3P2_TEXTURE_PATH_IID   = 0x7B3C8D10;
constexpr ULONG WC3P2_TEXTURE_PREFIX_IID = 0x7B3C8D11;

} // anonymous namespace

namespace mdx_scene {

// ── Wc3LightBuilder ────────────────────────────────────────

void Wc3LightBuilder::buildLights(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irLight : irModel.lights) {
        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::WC3_LIGHT));
        if (!obj) {
            reporter.warning(L"Wc3_Light scripted plugin not found — skipping light");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irLight.nodeIndex >= 0 && irLight.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irLight.nodeIndex].name.c_str());
        else
            name = _T("Wc3Light");
        node->SetName(name);

        // Set parent (walk chain like MaxScript relinkObjects)
        positionAtPivot(node, irLight.nodeIndex, irModel);
        setupNodeProperties(node, irLight.nodeIndex, irModel);
        attachToParent(node, irLight.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (ref) {
            int lightType = 1; // Default Omni
            switch (irLight.type) {
            case ir::Light::Type::Omni:        lightType = 1; break;
            case ir::Light::Type::Directional: lightType = 2; break;
            case ir::Light::Type::Ambient:     lightType = 3; break;
            }
            pbSetInt(ref, L"LightType", lightType);
            pbSetFloat(ref, L"DecayStart", irLight.attenuationStart);
            pbSetFloat(ref, L"DecayEnd", irLight.attenuationEnd);

            // Max's color picker uses 0..255 range (confirmed by Wc3Light
            // plugin defaults: AmbColor=[255,230,142], ShadowColor=
            // [122,172,255]). Max SDK Color struct stores 0..1 floats which
            // the picker renders as 0..255. MDX color 0.604 → picker 154.
            // BGR->RGB swap is done in the disassembler.
            // Wc3_Light uses ShadowColor/ShadowValue for the primary light
            // (MDX "color") and AmbColor/AmbValue for the secondary (MDX
            // "ambientColor").
            pbSetColor(ref, L"ShadowColor", irLight.color);
            pbSetFloat(ref, L"ShadowValue", irLight.intensity);
            pbSetColor(ref, L"AmbColor", irLight.ambientColor);
            pbSetFloat(ref, L"AmbValue", irLight.ambientIntensity);

            // Reforged / 3.0 parameters, named after their MDL keywords.
            // `ShadowIntensity` is NOT the plug-in's `ShadowValue` above: that
            // one is the primary intensity, this one the IBL/shadow term.
            pbSetFloat(ref, L"ShadowIntensity", irLight.shadowIntensity);
            pbSetBool(ref, L"ShadowCasting", irLight.shadowCasting ? TRUE : FALSE);
            pbSetFloat(ref, L"ShadowCastingStart", irLight.shadowCastingStart);
            pbSetFloat(ref, L"ShadowCastingEnd", irLight.shadowCastingEnd);
            pbSetFloat(ref, L"QuadraticFalloff", irLight.quadraticFalloff);
            pbSetFloat(ref, L"LinearFalloff", irLight.linearFalloff);
            pbSetFloat(ref, L"Damping", irLight.damping);
        }

        // Store MDX-static values as UserProps so the exporter can
        // round-trip them bit-identically. Reason: when an animation
        // track (KLAC/KLBC/KLAI/etc.) exists, the scripted plugin's
        // parameter is fully controlled by the Controller — the "static"
        // value from the original MDX is no longer independently readable
        // from Max. These UserProps preserve it for the exporter.
        {
            wchar_t buf[64];

            swprintf_s(buf, 64, L"%.6f,%.6f,%.6f",
                irLight.color.r, irLight.color.g, irLight.color.b);
            node->SetUserPropString(MSTR(L"mdx_static_color"), MSTR(buf));

            swprintf_s(buf, 64, L"%.6f,%.6f,%.6f",
                irLight.ambientColor.r, irLight.ambientColor.g, irLight.ambientColor.b);
            node->SetUserPropString(MSTR(L"mdx_static_ambColor"), MSTR(buf));

            swprintf_s(buf, 64, L"%.6f", irLight.intensity);
            node->SetUserPropString(MSTR(L"mdx_static_intensity"), MSTR(buf));

            swprintf_s(buf, 64, L"%.6f", irLight.ambientIntensity);
            node->SetUserPropString(MSTR(L"mdx_static_ambIntensity"), MSTR(buf));

            swprintf_s(buf, 64, L"%.6f", irLight.attenuationStart);
            node->SetUserPropString(MSTR(L"mdx_static_attStart"), MSTR(buf));

            swprintf_s(buf, 64, L"%.6f", irLight.attenuationEnd);
            node->SetUserPropString(MSTR(L"mdx_static_attEnd"), MSTR(buf));
        }

        // Register for animation key insertion
        if (irLight.nodeIndex >= 0 && irLight.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irLight.nodeIndex] = node;
    }
}

// ── Wc3AttachmentBuilder ────────────────────────────────────

// Parse an MDX attachment name (e.g. "Hand Right Ref") into the three
// 1-based dropdown indices of the Wc3AttachPoint plug-in.
//
// Plug-in lists (from Wc3AttachPoint.ms):
//   attachmentList     = #("Head", "Overhead", "Origin", "Foot", "Chest",
//                          "Hand", "Weapon", "Sprite")
//   attachmentAdd1List = #("None", "Right", "Left", "Mount Left", "Mount Right")
//   attachmentAdd2List = #("None", "RallyPoint", "EatTree", "Gold", "Mount",
//                          "Mount Rear", "Rear", "Smart", "Alternate",
//                          "First", "Second", "Third", "Fourth", "Fifth", "Sixth")
//
// Every index is 1-based (the MaxScript convention). Each one defaults to 1
// when nothing matches ("Head" / "None" / "None").
struct AttachNameIndices {
    int nameFirst = 1;   // "Head"
    int nameAdd1  = 1;   // "None"
    int nameAdd2  = 1;   // "None"
};

static AttachNameIndices parseAttachmentName(const std::string& name) {
    AttachNameIndices out;

    // Type tokens (matched as a separate word; "Head" must NOT also match "Overhead").
    // Order matters: match the longer tokens first so "Overhead" is not misread as "Head".
    static const std::pair<const char*, int> kTypeTable[] = {
        {"Overhead", 2},
        {"Origin",   3},
        {"Sprite",   8},
        {"Weapon",   7},
        {"Chest",    5},
        {"Head",     1},   // check after "Overhead"!
        {"Foot",     4},
        {"Hand",     6},
    };
    for (const auto& [tok, idx] : kTypeTable) {
        size_t pos = name.find(tok);
        if (pos != std::string::npos) {
            // Check as a whole word: boundary on the left and on the right
            bool leftOk  = (pos == 0)                || !isalpha(static_cast<unsigned char>(name[pos - 1]));
            bool rightOk = (pos + strlen(tok) >= name.size()) || !isalpha(static_cast<unsigned char>(name[pos + strlen(tok)]));
            if (leftOk && rightOk) {
                out.nameFirst = idx;
                break;
            }
        }
    }

    // Add1 (Direction) — check "Mount Left" / "Mount Right" before "Left"/"Right"!
    // The matching token is erased from `remaining` so that it cannot accidentally
    // match as an Add2 token too (e.g. "Mount" inside "Mount Left").
    static const std::pair<const char*, int> kAdd1Table[] = {
        {"Mount Left",  4},
        {"Mount Right", 5},
        {"Right",       2},   // check after "Mount Right"
        {"Left",        3},   // check after "Mount Left"
    };
    std::string remaining = name;
    for (const auto& [tok, idx] : kAdd1Table) {
        size_t pos = remaining.find(tok);
        if (pos != std::string::npos) {
            out.nameAdd1 = idx;
            remaining.erase(pos, strlen(tok));
            break;
        }
    }

    // Add2 (Additional) — searches `remaining` (without the consumed Add1 token)
    static const std::pair<const char*, int> kAdd2Table[] = {
        {"Mount Rear",  6},
        {"RallyPoint",  2},
        {"EatTree",     3},
        {"Alternate",   9},
        {"Second",     11},
        {"Fourth",     13},
        {"Third",      12},
        {"Fifth",      14},
        {"Sixth",      15},
        {"First",      10},
        {"Smart",       8},
        {"Mount",       5},   // check after "Mount Rear"
        {"Rear",        7},
        {"Gold",        4},
    };
    for (const auto& [tok, idx] : kAdd2Table) {
        if (remaining.find(tok) != std::string::npos) {
            out.nameAdd2 = idx;
            break;
        }
    }

    return out;
}

void Wc3AttachmentBuilder::buildAttachments(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    const std::wstring& modelDir,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irAtt : irModel.attachments) {
        if (irAtt.attachmentId == -1) continue; // Skip FaceFX placeholders

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::WC3_ATTACH_POINT));
        if (!obj) {
            reporter.warning(L"Wc3_AttachPoint scripted plugin not found — skipping attachment");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        name.printf(_T("%hs"), irAtt.name.c_str());
        node->SetName(name);

        positionAtPivot(node, irAtt.nodeIndex, irModel);
        setupNodeProperties(node, irAtt.nodeIndex, irModel);
        attachToParent(node, irAtt.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (ref) {
            // Parse the Type/Direction/Additional dropdowns out of the node name
            AttachNameIndices idx = parseAttachmentName(irAtt.name);
            pbSetInt(ref, L"nameFirst", idx.nameFirst);
            pbSetInt(ref, L"nameAdd1",  idx.nameAdd1);
            pbSetInt(ref, L"nameAdd2",  idx.nameAdd2);

            pbSetInt(ref, L"attachmentId", irAtt.attachmentId);
            if (!irAtt.path.empty()) {
                // Resolve model path to absolute (pre-resolve extracted from CASC)
                namespace fs = std::filesystem;
                std::wstring wRelPath = toWstr(irAtt.path);
                std::wstring resolved;

                auto tryPath = [&](const std::wstring& rel) -> bool {
                    fs::path full = fs::path(modelDir) / rel;
                    std::error_code ec;
                    if (fs::exists(full, ec)) { resolved = full.wstring(); return true; }
                    return false;
                };
                if (!tryPath(wRelPath)) {
                    fs::path p(wRelPath);
                    std::wstring ext = p.extension().wstring();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    if (ext == L".mdl") { p.replace_extension(L".mdx"); tryPath(p.wstring()); }
                    else if (ext == L".mdx") { p.replace_extension(L".mdl"); tryPath(p.wstring()); }
                }
                if (resolved.empty()) resolved = wRelPath;

                pbSetBool(ref, L"usesExternalModel", TRUE);
                pbSetString(ref, L"externalModelPath", resolved.c_str());
            }
            if (irAtt.attachmentId >= 0) {
                pbSetBool(ref, L"usesAttachmentId", TRUE);
            }
        }

        // Register for animation key insertion
        if (irAtt.nodeIndex >= 0 && irAtt.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irAtt.nodeIndex] = node;
    }
}

// ── Wc3Particle1Builder ─────────────────────────────────────

void Wc3Particle1Builder::buildParticles(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    const std::wstring& modelDir,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irPE : irModel.particleEmitters) {
        if (irPE.variant != 1) continue;

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(GEOMOBJECT_CLASS_ID, mdx_ids::WC3_PARTICLES1));
        if (!obj) {
            reporter.warning(L"Wc3Particles1 plugin not found — skipping PE1");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irPE.nodeIndex].name.c_str());
        else
            name = _T("Wc3PE1");
        node->SetName(name);

        positionAtPivot(node, irPE.nodeIndex, irModel);
        setupNodeProperties(node, irPE.nodeIndex, irModel);
        attachToParent(node, irPE.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        IParamBlock2* pb = ref ? PBR::findParamBlock(ref, 0) : nullptr;
        if (pb) {
            pb->SetValue(P1_PB_SPEED, 0, irPE.speed);
            pb->SetValue(P1_PB_EMISSION_RATE, 0, irPE.emissionRate);
            pb->SetValue(P1_PB_LIFE, 0, irPE.lifespan);
            pb->SetValue(P1_PB_ACCELERATION, 0, irPE.gravity);
            // MDX stores latitude/longitude in radians; plugin expects degrees
            constexpr float kRadToDeg = 180.0f / 3.14159265f;
            pb->SetValue(P1_PB_LATITUDE, 0, irPE.latitude * kRadToDeg);
            pb->SetValue(P1_PB_LONGITUDE, 0, irPE.longitude * kRadToDeg);
        }

        // Model path — same split semantics as the PE2 texture path.
        //
        // The Wc3Particles1 plugin concatenates m_modelPrefix + m_modelPath
        // when the MDX exporter reads them back. If we leave the resolved
        // absolute Windows path in m_modelPath and empty prefix, the
        // exported MDX ends up with "C:\Users\...\foo.mdx" which breaks
        // in-game.
        //
        // Split the original MDX-relative model path into prefix + filename
        // so the exporter rebuilds the original path verbatim (or the user
        // can edit the prefix in the plugin UI). CASC extraction is a
        // separate side effect; its return value isn't stored here.
        if (!irPE.modelPath.empty()) {
            std::wstring prefix, filename;
            splitMdxPath(irPE.modelPath, prefix, filename);

            auto* pathPtr = static_cast<MSTR*>(obj->GetInterface(WC3P1_MODEL_PATH_IID));
            if (pathPtr) *pathPtr = filename.c_str();
            auto* prefixPtr = static_cast<MSTR*>(obj->GetInterface(WC3P1_MODEL_PREFIX_IID));
            if (prefixPtr) *prefixPtr = prefix.c_str();

            // Side effect: make sure the model file exists on disk so the
            // renderer / viewport can locate it via <modelDir>\<relPath>.
            namespace fs = std::filesystem;
            std::wstring wRelPath = toWstr(irPE.modelPath);
            auto exists = [&](const std::wstring& rel) -> bool {
                fs::path full = fs::path(modelDir) / rel;
                std::error_code ec;
                return fs::exists(full, ec);
            };
            if (!exists(wRelPath)) {
                // Try swapping .mdx↔.mdl (CASC models are often named
                // differently than what's referenced in MDX)
                fs::path p(wRelPath);
                std::wstring ext = p.extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                if (ext == L".mdl") (void)exists(p.replace_extension(L".mdx").wstring());
                else if (ext == L".mdx") (void)exists(p.replace_extension(L".mdl").wstring());
            }
        }

        // Register for animation key insertion
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irPE.nodeIndex] = node;
    }
}

// ── Wc3Particle2Builder ─────────────────────────────

void Wc3Particle2Builder::buildParticles(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    const std::wstring& modelDir, TextureResolver* resolver,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irPE : irModel.particleEmitters) {
        if (irPE.variant != 2) continue;

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(GEOMOBJECT_CLASS_ID, mdx_ids::WC3_PARTICLES2));
        if (!obj) {
            reporter.warning(L"Wc3Particles2 plugin not found — skipping PE2");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irPE.nodeIndex].name.c_str());
        else
            name = _T("Wc3PE2");
        node->SetName(name);

        positionAtPivot(node, irPE.nodeIndex, irModel);
        setupNodeProperties(node, irPE.nodeIndex, irModel);
        attachToParent(node, irPE.nodeIndex, irModel, nodeMap);

        // Paramblock setup — all 47 PE2 parameters
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        IParamBlock2* pb = ref ? PBR::findParamBlock(ref, 0) : nullptr;
        if (pb) {
            pb->SetValue(PB_SPEED, 0, irPE.speed);
            pb->SetValue(PB_VARIATION, 0, irPE.variation);
            pb->SetValue(PB_INITVEL, 0, irPE.emissionRate);
            pb->SetValue(PB_LIFE, 0, irPE.lifespan);
            pb->SetValue(PB_GRAVITY, 0, irPE.gravity);
            // PE2 latitude is stored in DEGREES in the MDX binary — the WC3
            // engine performs no conversion for PE2 (unlike PE1 which uses
            // radians). So we pass it through unchanged to ConeAngle.
            // Reference: NeoDexSceneRebuilder.ms applies `p.coneangle = sub.latitude`
            // directly without any conversion.
            pb->SetValue(PB_ANGLE_Y, 0, irPE.latitude);
            // PE2 has no longitude in MDX — plugin derives it from LineEmitter flag
            pb->SetValue(PB_WIDTH, 0, irPE.width);
            pb->SetValue(PB_HEIGHT, 0, irPE.length);
            pb->SetValue(PB_TAIL_LEN, 0, irPE.tailLength);
            pb->SetValue(PB_MIDTIME, 0, irPE.midTime);

            pb->SetValue(PB_BLEND, 0, irPE.filterMode);
            pb->SetValue(PB_ROWS, 0, irPE.rows);
            pb->SetValue(PB_COLS, 0, irPE.columns);
            pb->SetValue(PB_TYPE, 0, irPE.headOrTail);
            pb->SetValue(PB_REPLACEABLE_ID, 0, irPE.replaceableId);
            pb->SetValue(PB_PRIORITY, 0, irPE.priorityPlane);

            // Segment colors (stored as Point3 in PE2)
            Point3 cStart(irPE.segmentColors[0].r, irPE.segmentColors[0].g, irPE.segmentColors[0].b);
            Point3 cMid(irPE.segmentColors[1].r, irPE.segmentColors[1].g, irPE.segmentColors[1].b);
            Point3 cEnd(irPE.segmentColors[2].r, irPE.segmentColors[2].g, irPE.segmentColors[2].b);
            pb->SetValue(PB_COLOR_START, 0, cStart);
            pb->SetValue(PB_COLOR_MID, 0, cMid);
            pb->SetValue(PB_COLOR_END, 0, cEnd);

            // Segment alpha (0-1 float → 0-255 int)
            pb->SetValue(PB_ALPHA_START, 0, static_cast<int>(irPE.segmentAlpha[0] * 255.0f));
            pb->SetValue(PB_ALPHA_MID, 0, static_cast<int>(irPE.segmentAlpha[1] * 255.0f));
            pb->SetValue(PB_ALPHA_END, 0, static_cast<int>(irPE.segmentAlpha[2] * 255.0f));

            // Segment scale
            pb->SetValue(PB_SCALE_START, 0, irPE.segmentScale[0]);
            pb->SetValue(PB_SCALE_MID, 0, irPE.segmentScale[1]);
            pb->SetValue(PB_SCALE_END, 0, irPE.segmentScale[2]);

            // Head/tail UV animation intervals
            pb->SetValue(PB_HEAD_LIFE_START, 0, irPE.headInterval[0]);
            pb->SetValue(PB_HEAD_LIFE_REPEAT, 0, irPE.headInterval[1]);
            pb->SetValue(PB_HEAD_LIFE_END, 0, irPE.headInterval[2]);
            pb->SetValue(PB_HEAD_DECAY_START, 0, irPE.headDecayInterval[0]);
            pb->SetValue(PB_HEAD_DECAY_REPEAT, 0, irPE.headDecayInterval[1]);
            pb->SetValue(PB_HEAD_DECAY_END, 0, irPE.headDecayInterval[2]);
            pb->SetValue(PB_TAIL_LIFE_START, 0, irPE.tailInterval[0]);
            pb->SetValue(PB_TAIL_LIFE_REPEAT, 0, irPE.tailInterval[1]);
            pb->SetValue(PB_TAIL_LIFE_END, 0, irPE.tailInterval[2]);
            pb->SetValue(PB_TAIL_DECAY_START, 0, irPE.tailDecayInterval[0]);
            pb->SetValue(PB_TAIL_DECAY_REPEAT, 0, irPE.tailDecayInterval[1]);
            pb->SetValue(PB_TAIL_DECAY_END, 0, irPE.tailDecayInterval[2]);

            // Flags → individual bools
            uint32_t f = irPE.flags;
            pb->SetValue(PB_SORT, 0, (f & 0x10000) ? 1 : 0);
            pb->SetValue(PB_UNSHADED, 0, (f & 0x8000) ? 1 : 0);
            pb->SetValue(PB_LINE_EMIT, 0, (f & 0x20000) ? 1 : 0);
            pb->SetValue(PB_UNFOGGED, 0, (f & 0x40000) ? 1 : 0);
            pb->SetValue(PB_MODELSPACE, 0, (f & 0x80000) ? 1 : 0);
            pb->SetValue(PB_XYQUAD, 0, (f & 0x100000) ? 1 : 0);
            pb->SetValue(PB_SQUIRT, 0, (f & 1) ? 1 : 0);
        }

        // Texture path via custom interface.
        //
        // The Wc3Particles2 plugin stores two separate fields:
        //   m_texturePrefix : directory portion (e.g. "Textures\")
        //   m_particlePath  : filename only     (e.g. "Fireball.blp")
        //
        // On export the plugin concatenates them back into an MDX texture
        // path. The plugin's own CASC browser callback (see Particles.cpp
        // lines ~894-924) enforces this split — if we leave m_particlePath
        // holding a full absolute Windows path and m_texturePrefix empty,
        // the re-exported MDX ends up with a garbage texture path that
        // the game can't load.
        //
        // History:
        //   v1 — put absolute disk path in m_particlePath, empty prefix.
        //        Round-trip produced "" + "C:\Users\...\foo.blp" in MDX
        //        → broken in-game.
        //   v2 — extract prefix + filename from the original MDX texture
        //        path (e.g. "Textures\Fireball.blp" → prefix="Textures\",
        //        filename="Fireball.blp"). Matches what the CASC browser
        //        does when the user picks a texture manually.
        if (irPE.textureIndex >= 0 &&
            irPE.textureIndex < static_cast<int32_t>(irModel.textures.size()))
        {
            const auto& irTex = irModel.textures[irPE.textureIndex];

            // Pick the actual MDX path to use:
            //   1. Explicit filePath (covers most cases).
            //   2. ReplaceableID-derived canonical path when filePath is
            //      empty (e.g. v800 TeamColor / TeamGlow / Cliff). Mirrors
            //      the same fallback used in Wc3MaterialBuilder so that
            //      particle textures driven by an ID instead of a path
            //      still get extracted from CASC/MPQ.
            std::string mdxPath = irTex.filePath;
            if (mdxPath.empty() && irTex.replaceableId > 0) {
                static constexpr struct { int id; const char* path; } kMap[] = {
                    {  1, "ReplaceableTextures\\TeamColor\\TeamColor00.blp" },
                    {  2, "ReplaceableTextures\\TeamGlow\\TeamGlow00.blp" },
                    { 11, "ReplaceableTextures\\Cliff\\Cliff0.blp" },
                    { 21, "ReplaceableTextures\\LordaeronTree\\LordaeronSummerTree.blp" },
                    { 22, "ReplaceableTextures\\AshenvaleTree\\AshenTree.blp" },
                    { 23, "ReplaceableTextures\\BarrensTree\\BarrensTree.blp" },
                    { 24, "ReplaceableTextures\\NorthrendTree\\NorthTree.blp" },
                    { 25, "ReplaceableTextures\\Mushroom\\MushroomTree.blp" },
                    { 31, "ReplaceableTextures\\RuinsTree\\RuinsTree.blp" },
                    { 32, "ReplaceableTextures\\OutlandMushroomTree\\MushroomTree.blp" },
                };
                for (const auto& m : kMap) {
                    if (m.id == irTex.replaceableId) { mdxPath = m.path; break; }
                }
            }

            if (!mdxPath.empty()) {
                // Extract the texture to disk first so we can write the full
                // absolute path back into m_particlePath. The Wc3Particles2
                // plugin's manual file-browser also stores absolute disk
                // paths in m_particlePath, so this keeps the imported state
                // consistent with what the plugin produces when the user
                // picks a texture by hand. Re-export (if/when implemented)
                // will need to convert this back to a relative MDX path.
                std::wstring relW = toWstr(mdxPath);
                std::wstring fullDiskPath = resolver
                    ? resolver->Resolve(relW)
                    : mdx_scene::resolveTexturePath(modelDir, relW);

                auto* pathPtr   = static_cast<MSTR*>(obj->GetInterface(WC3P2_TEXTURE_PATH_IID));
                auto* prefixPtr = static_cast<MSTR*>(obj->GetInterface(WC3P2_TEXTURE_PREFIX_IID));

                // Prefer the resolved absolute disk path (mirrors what
                // BrowseForMdlFile does in the plugin). Fall back to the
                // MDX-relative filename only if extraction failed and the
                // file isn't on disk anywhere — gives the user something
                // visible in the rollout instead of an empty field.
                std::wstring filename, prefix;
                splitMdxPath(mdxPath, prefix, filename);

                std::error_code ec;
                if (!fullDiskPath.empty() && std::filesystem::exists(fullDiskPath, ec)) {
                    if (pathPtr)   *pathPtr   = fullDiskPath.c_str();
                    if (prefixPtr) *prefixPtr = prefix.c_str();   // keeps re-export prefix info
                } else {
                    // Texture not extractable — fall back to MDX-relative split
                    if (pathPtr)   *pathPtr   = filename.c_str();
                    if (prefixPtr) *prefixPtr = prefix.c_str();
                }
            }
        }

        // Register for animation key insertion
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irPE.nodeIndex] = node;
    }
}

// ── Wc3RibbonBuilder ───────────────────────────────

void Wc3RibbonBuilder::buildRibbons(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    const std::vector<Mtl*>& materials, Interface* gi,
    core::ExportErrorReporter& reporter)
{
    for (const auto& irRib : irModel.ribbonEmitters) {
        Object* obj = static_cast<Object*>(
            gi->CreateInstance(GEOMOBJECT_CLASS_ID, mdx_ids::WC3_RIBBON));
        if (!obj) {
            reporter.warning(L"Wc3Ribbon plugin not found — skipping ribbon");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irRib.nodeIndex >= 0 && irRib.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irRib.nodeIndex].name.c_str());
        else
            name = _T("Wc3Ribbon");
        node->SetName(name);

        // Assign material
        if (irRib.materialIndex >= 0 && irRib.materialIndex < static_cast<int32_t>(materials.size()))
            node->SetMtl(materials[irRib.materialIndex]);

        positionAtPivot(node, irRib.nodeIndex, irModel);
        setupNodeProperties(node, irRib.nodeIndex, irModel);
        attachToParent(node, irRib.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        IParamBlock2* pb = ref ? PBR::findParamBlock(ref, 0) : nullptr;
        if (pb) {
            pb->SetValue(pb_height_above, 0, irRib.heightAbove);
            pb->SetValue(pb_height_below, 0, irRib.heightBelow);
            pb->SetValue(pb_edge_lifetime, 0, irRib.lifespan);
            pb->SetValue(pb_gravity, 0, irRib.gravity);
            pb->SetValue(pb_edges_per_second, 0, irRib.emissionRate);
            pb->SetValue(pb_tex_rows, 0, irRib.rows);
            pb->SetValue(pb_tex_cols, 0, irRib.columns);
            pb->SetValue(pb_tex_slot, 0, irRib.textureSlot);
            pb->SetValue(pb_alpha, 0, irRib.alpha);

            Point3 col(irRib.color.r, irRib.color.g, irRib.color.b);
            pb->SetValue(pb_color, 0, col);

            // Material reference
            if (irRib.materialIndex >= 0 && irRib.materialIndex < static_cast<int32_t>(materials.size()))
                pb->SetValue(pb_material, 0, materials[irRib.materialIndex]);
        }

        // Register for animation key insertion
        if (irRib.nodeIndex >= 0 && irRib.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irRib.nodeIndex] = node;
    }
}

// ── Wc3EventBuilder ────────────────────────────────────────

void Wc3EventBuilder::buildEvents(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irEvt : irModel.eventObjects) {
        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::WC3_EVENT_V2021));
        if (!obj) {
            // Try older version
            obj = static_cast<Object*>(
                gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::WC3_EVENT_V2020));
        }
        if (!obj) {
            reporter.warning(L"Wc3_Event scripted plugin not found — skipping event");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        // The MDX name (e.g. "SNDxAHEA") goes to the plug-in's `eventName`
        // parameter, which the exporter writes back and the UI parses to
        // preselect its dropdowns. The node takes the same name only as a
        // readable default — it no longer has to be unique per event, nor
        // carry the old "Obj:" prefix.
        const std::wstring evtName =
            (irEvt.nodeIndex >= 0 && irEvt.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
                ? toWstr(irModel.nodes[irEvt.nodeIndex].name)
                : toWstr(irEvt.eventCode + irEvt.eventData);
        node->SetName(evtName.c_str());
        pbSetString(obj, L"eventName", evtName.c_str());

        positionAtPivot(node, irEvt.nodeIndex, irModel);
        setupNodeProperties(node, irEvt.nodeIndex, irModel);
        attachToParent(node, irEvt.nodeIndex, irModel, nodeMap);

        // Events on a global sequence keep its duration (ms) in a UserProp —
        // Wc3RefEvent has no parameter for it (see the exporter's event
        // extractor).
        if (irEvt.globalSequenceIndex >= 0 &&
            irEvt.globalSequenceIndex < static_cast<int32_t>(irModel.globalSequenceDurations.size()))
            node->SetUserPropInt(_T("Wc3GlobalSequence"),
                static_cast<int>(irModel.globalSequenceDurations[irEvt.globalSequenceIndex]));

        // Set event key times via MaxScript — the same approach as the
        // KGAC/Wc3VertexMod fix. Scripted simpleManipulator plug-ins (such as
        // Wdx_Wc3Event, which extends simpleManipulator) do not keep their
        // ParamBlocks on a fixed BlockID 0, and Max assigns the ParamIDs
        // dynamically. The old code
        // `PBR::findParamBlock(ref, 0) + pb->Append(0, 1, ...)` did not find the
        // IntTab parameter `keyList`, so event notes came out empty.
        //
        // MaxScript sees the plug-in parameter `keyList` directly (as an IntTab
        // array with `tabSizeVariable:true`) and can assign it a plain array.
        //
        // IMPORTANT: the plug-in expects frame numbers, NOT ticks!
        // The btnAddNote handler does `sliderTime / ticksperframe`, i.e. the
        // ListView presentation assumes frames. Storing ticks instead leaves the
        // keys "invisible" outside the timeline.
        if (irEvt.keyTimes.empty()) {
            // Diagnostic: the IR side no longer has any keys mapped to this
            // event (a remap loss, for instance). Surface it in the log:
            std::wstring warn = L"Event '" + std::wstring(node->GetName()) +
                L"' has no keyTimes after the remap — its notes will stay empty!";
            reporter.warning(warn.c_str());
        } else {
            std::wstring nodeName(node->GetName());
            const int tpf = GetTicksPerFrame();

            std::wstringstream kl;
            kl << L"#(";
            bool first = true;
            for (auto t : irEvt.keyTimes) {
                if (!first) kl << L",";
                // Ticks -> frames (the UI expects frame numbers)
                int frame = static_cast<int>(t) / tpf;
                kl << frame;
                first = false;
            }
            kl << L")";

            // Resolve by handle — event node names can collide, and
            // getNodeByName would set the keyList on the wrong node.
            std::wstringstream ss;
            ss << L"(local n = maxOps.getNodeByHandle " << node->GetHandle() << L";"
               << L"if n != undefined do ("
               << L"n.keyList = " << kl.str()
               << L"))";

            std::wstring scriptStr = ss.str();
            BOOL execOk = ExecuteMAXScriptScript(
                const_cast<wchar_t*>(scriptStr.c_str()),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                MAXScript::ScriptSource::NonEmbedded,
#endif
                TRUE, nullptr);

            if (!execOk) {
                std::wstring warn = L"ExecuteMAXScriptScript failed for event '" +
                    nodeName + L"'. Script: " + scriptStr;
                reporter.warning(warn.c_str());
            }
        }

        // Register for animation key insertion
        if (irEvt.nodeIndex >= 0 && irEvt.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irEvt.nodeIndex] = node;
    }
}

// ── Wc3CollisionBuilder ─────────────────────────────────────

void Wc3CollisionBuilder::buildCollisions(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irCol : irModel.collisionShapes) {
        Class_ID clsId = (irCol.shape == ir::CollisionShape::Shape::Sphere)
            ? mdx_ids::WC3_COLLISION_SPH : mdx_ids::WC3_COLLISION_BOX;

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, clsId));
        if (!obj) {
            reporter.warning(L"Wc3Collision scripted plugin not found — skipping collision shape");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        if (irCol.nodeIndex >= 0 && irCol.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irCol.nodeIndex].name.c_str());
        else
            name = _T("Wc3Collision");
        node->SetName(name);

        // Position collision shape — follows NeoDex (NeoDexSceneRebuilder.ms
        // fn createCollisionShapes lines 2723-2759).
        //
        // MDX semantics splits by animation, not by version:
        //   Static (no KGTR track): CLID vertices are ABSOLUTE world positions.
        //                           Node goes at shape center/bottom.
        //                           Classic v800 behavior.
        //   Animated (KGTR track):  CLID vertices are LOCAL extents relative
        //                           to the node's pivot. Node goes at
        //                           (pivot + shape offset). Reforged v1000+
        //                           Hitbox system (per-bone animated boxes).
        //
        // CRITICAL: Max pivot must equal MDX pivot so KGRT rotation keys
        // rotate the box around the correct anchor (the bone being tracked),
        // not around the box's visual bottom. NeoDex does `p.pivot = obj.pivot`
        // unconditionally — same here.
        //
        // Helper-plugin draw conventions (scripted Wdx_CollisionSphere /
        // Wdx_CollisionBox):
        //   Sphere — radius drawn symmetrically around node origin.
        //   Box    — Max-native: X/Y symmetric, Z upward (origin=bottom-center).
        //
        // History:
        //   v1 — positionAtPivot() → all shapes landed at (0,0,0).
        //   v2 — pivot + shapeCenter → OK spheres, double-offset boxes.
        //   v3 — shapeCenter alone → spheres OK, boxes floated h/2 in Z.
        //   v4 — shape-aware (sphere=v1, box=(xy-center, z-min)). v800 OK,
        //        v1000+ animated hitboxes landed under the ground.
        //   v5 — pivot-aware (pivot magnitude test): v800 & static v1000 OK,
        //        but animated v1000+ had KGRT rotating around wrong anchor
        //        and Weapon/Shield_BB with large KGTR offsets mis-placed.
        //   v6 — NeoDex-aligned: hasTransAnim detection + SetObjectOffset
        //        so Max pivot = MDX pivot.
        Point3 pivot(0.0f, 0.0f, 0.0f);
        if (irCol.nodeIndex >= 0 && irCol.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            pivot = irModel.nodes[irCol.nodeIndex].pivotPoint;

        // hasTransAnim detection: does this node have KGTR keys?
        bool hasTransAnim = false;
        for (const auto& na : irModel.nodeAnimations) {
            if (na.nodeIndex == irCol.nodeIndex && !na.translation.empty()) {
                hasTransAnim = true;
                break;
            }
        }

        Point3 translation(0.0f, 0.0f, 0.0f);
        if (irCol.shape == ir::CollisionShape::Shape::Sphere) {
            if (!irCol.vertices.empty())
                translation = irCol.vertices[0];          // sphere center
        } else if (irCol.shape == ir::CollisionShape::Shape::Box) {
            if (irCol.vertices.size() >= 2) {
                const Point3& a = irCol.vertices[0];
                const Point3& b = irCol.vertices[1];
                translation.x = (a.x + b.x) * 0.5f;       // X center
                translation.y = (a.y + b.y) * 0.5f;       // Y center
                translation.z = std::min(a.z, b.z);       // Z bottom
            } else if (!irCol.vertices.empty()) {
                translation = irCol.vertices[0];
            }
        }
        // Only add pivot for animated shapes (NeoDex convention)
        if (hasTransAnim)
            translation += pivot;

        Matrix3 tm;
        tm.IdentityMatrix();
        tm.SetTrans(translation);
        node->SetNodeTM(0, tm);

        // Set the Max pivot to match the MDX pivot — so KGTR adds to pivot
        // (not to shape-bottom) and KGRT rotates around the MDX anchor point.
        // Equivalent to MaxScript `p.pivot = obj.pivot`.
        // In Max SDK: move TM to pivot, then offset the object so geometry
        // stays visually in place. The resulting node has:
        //   GetNodeTM(0).GetTrans() == pivot        (animation anchor)
        //   GetObjOffsetPos()       == translation - pivot  (visual offset)
        if (hasTransAnim) {
            Matrix3 pivotTM;
            pivotTM.IdentityMatrix();
            pivotTM.SetTrans(pivot);
            node->SetNodeTM(0, pivotTM);
            node->SetObjOffsetPos(translation - pivot);
        }

        setupNodeProperties(node, irCol.nodeIndex, irModel);
        attachToParent(node, irCol.nodeIndex, irModel, nodeMap);

        // Paramblock setup: dimensions from collision vertices
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (ref) {
            if (irCol.shape == ir::CollisionShape::Shape::Sphere) {
                pbSetFloat(ref, L"radius", irCol.radius);
            } else if (irCol.shape == ir::CollisionShape::Shape::Box && irCol.vertices.size() >= 2) {
                Point3 ext = irCol.vertices[1] - irCol.vertices[0];
                pbSetFloat(ref, L"width", fabsf(ext.x));
                pbSetFloat(ref, L"length", fabsf(ext.y));
                pbSetFloat(ref, L"height", fabsf(ext.z));
            }
        }

        // Register for animation key insertion
        if (irCol.nodeIndex >= 0 && irCol.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irCol.nodeIndex] = node;
    }
}

// ── Wc3VertexColorBuilder ───────────────────────────────────

void Wc3VertexColorBuilder::applyVertexColors(
    const ir::IRModel& irModel, const std::vector<INode*>& meshNodes,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& ga : irModel.geosetAnims) {
        if (!ga.usesColor && !ga.dropShadow) continue;
        if (ga.meshIndex < 0 || ga.meshIndex >= static_cast<int32_t>(meshNodes.size()))
            continue;
        INode* meshNode = meshNodes[ga.meshIndex];
        if (!meshNode) continue;

        // Create and apply Wc3VertexMod via MaxScript — scripted modifier
        // plugins can't be safely cast to Modifier* from CreateInstance.
        // Plugin classname is Wdx_Wc3VertexMod (see Wc3VertexColor.ms line 1:
        // `plugin modifier Wdx_Wc3VertexMod`). Older code used the bare
        // "Wc3VertexMod" which is undefined in MaxScript.
        // Resolve by handle — mesh node names are not guaranteed unique.
        wchar_t script[512];
        swprintf_s(script, 512,
            L"(local n = maxOps.getNodeByHandle %u;"
            L"if n != undefined do ("
            L"local m = Wdx_Wc3VertexMod();"
            L"m.UsesDropShadow = %s;"
            L"m.UsesColor = %s;"
            L"m.VertexColor = color %d %d %d;"
            L"addModifier n m))",
            (unsigned)meshNode->GetHandle(),
            ga.dropShadow ? L"true" : L"false",
            ga.usesColor ? L"true" : L"false",
            (int)(ga.color.r * 255.0f + 0.5f),
            (int)(ga.color.g * 255.0f + 0.5f),
            (int)(ga.color.b * 255.0f + 0.5f));

        ExecuteMAXScriptScript(script,
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            MAXScript::ScriptSource::NonEmbedded,
#endif
            TRUE, nullptr);
    }
}

// ── Wc3PopcornBuilder (v1200) ───────────────────────────────

namespace {

// PopcornFX runtime paths in MDX are sometimes authored without an extension
// (engine appends one at load time). The TextureResolver only triggers its
// .pkb ↔ .pkfx alias fallback when the input ends in one of those two
// extensions, so we have to canonicalize first. Default to .pkb (the Reforged
// runtime payload) when no extension is present — the resolver's alias
// fallback then picks up the .pkfx sibling if that's what's actually shipped.
std::wstring ensurePopcornExtension(const std::wstring& path) {
    if (path.empty()) return path;

    auto lastSlash = path.find_last_of(L"/\\");
    auto lastDot = path.find_last_of(L'.');
    const bool hasExt = (lastDot != std::wstring::npos) &&
                        (lastSlash == std::wstring::npos || lastDot > lastSlash);
    if (!hasExt)
        return path + L".pkb";

    std::wstring ext = path.substr(lastDot);
    std::wstring extLower = ext;
    for (auto& c : extLower) c = (wchar_t)::towlower(c);
    if (extLower == L".pkb" || extLower == L".pkfx")
        return path;

    // Some pipelines store a totally unrelated extension on the runtime path
    // (e.g. ".xml"). Treat as missing: swap to .pkb so the resolver runs its
    // alias probe.
    return path.substr(0, lastDot) + L".pkb";
}

} // anonymous

void Wc3PopcornBuilder::buildPopcorn(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    const std::wstring& modelDir, TextureResolver* resolver,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    int cornCount = 0;
    for (const auto& pe : irModel.particleEmitters)
        if (pe.variant == 3) ++cornCount;
    {
        std::ostringstream ss;
        ss << "[Popcorn] buildPopcorn: " << cornCount << " corn variants of "
           << irModel.particleEmitters.size() << " particle emitters";
        PopLog(ss.str());
    }

    int created = 0, failed = 0;
    for (const auto& irPE : irModel.particleEmitters) {
        if (irPE.variant != 3) continue;

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::BLIZZ_POPCORN));
        if (!obj) {
            ++failed;
            reporter.warning(L"BlizzPopcorn scripted plugin not found — skipping corn emitter");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        if (!node) {
            ++failed;
            obj->DeleteThis();
            continue;
        }

        MSTR name;
        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
            name.printf(_T("%hs"), irModel.nodes[irPE.nodeIndex].name.c_str());
        else
            name = _T("BlizzPopcorn");
        node->SetName(name);

        positionAtPivot(node, irPE.nodeIndex, irModel);
        setupNodeProperties(node, irPE.nodeIndex, irModel);
        attachToParent(node, irPE.nodeIndex, irModel, nodeMap);

        // Paramblock setup
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (ref) {
            if (!irPE.modelPath.empty()) {
                // Same split as the Wc3Particles2 texture above: the MDX
                // directory goes to popcornPrefix, and popcornPath gets the
                // resolved file on disk (disk → CASC → MPQ, extension
                // canonicalized first) so 3ds Max users can see / edit it,
                // or the MDX file name when nothing resolved. The exporter
                // writes prefix + file name back. Storing the disk path with
                // no prefix is what used to leak
                // "...\WhiteoutDexCASC\_de.w3mod\...\Hero_Glow.pkb" into the
                // re-exported MDX.
                std::wstring prefix, filename;
                splitMdxPath(irPE.modelPath, prefix, filename);
                pbSetString(ref, L"popcornPrefix", prefix.c_str());

                const std::wstring rel = ensurePopcornExtension(toWstr(irPE.modelPath));
                std::wstring resolved;
                try {
                    resolved = resolver
                        ? resolver->Resolve(rel)
                        : mdx_scene::resolveTexturePath(modelDir, rel);
                } catch (const std::exception& e) {
                    PopLog(std::string("[Popcorn] resolver threw: ") + e.what());
                } catch (...) {
                    PopLog("[Popcorn] resolver threw (non-std)");
                }
                std::error_code ec;
                const bool resolvedExists =
                    !resolved.empty() && std::filesystem::exists(resolved, ec);
                const std::wstring& finalPath = resolvedExists ? resolved : filename;
                pbSetString(ref, L"popcornPath", finalPath.c_str());
            }
            pbSetFloat(ref, L"LifeSpan", irPE.lifespan);
            pbSetFloat(ref, L"EmissionRate", irPE.emissionRate);
            pbSetFloat(ref, L"Speed", irPE.speed);
            // Base color / alpha — CORN carries both as static fields next to
            // their KPPC / KPPA tracks. The disassembler parks them in
            // segmentColors[0] / segmentAlpha[0]; alpha used to be hardcoded
            // to 1.0 here, which silently discarded the file's value.
            pbSetFloat(ref, L"alpha", irPE.segmentAlpha[0]);
            pbSetColor(ref, L"baseColor", irPE.segmentColors[0]);
            // YS_baseColor is the plugin's hidden mirror of baseColor — the
            // rollout keeps the two in lockstep, so seed it the same way.
            pbSetColor(ref, L"YS_baseColor", irPE.segmentColors[0]);
            // Replaceable texture and team color are not used by Popcorn
            // emitters at runtime — intentionally not written here.
            // Render flags: NodeFlag bits 0x8000 Unshaded / 0x20000
            // PopcornUnfogged / 0x40000 PopcornScaling. The disassembler
            // pre-masked them onto irPE.flags, so we just project to bools.
            pbSetBool(ref, L"flagUnshaded", (irPE.flags & 0x8000u)  ? TRUE : FALSE);
            pbSetBool(ref, L"flagUnfogged", (irPE.flags & 0x20000u) ? TRUE : FALSE);
            pbSetBool(ref, L"flagScaling",  (irPE.flags & 0x40000u) ? TRUE : FALSE);
            // PopcornFX anim-visibility gate ("Stand=on,Death=off" etc.). The
            // scripted plugin keeps the raw string in `rawFlags`; flagAlways /
            // flagBirth / ... booleans are derived from it inside MaxScript.
            // The renderer reads rawFlags directly so the engine convention
            // ("listed names enable against implicit default-off") survives
            // the round trip without us having to parse the string here.
            if (!irPE.animVisibilityGuide.empty()) {
                const std::wstring guideW = toWstr(irPE.animVisibilityGuide);
                pbSetString(ref, L"rawFlags", guideW.c_str());
            }
        }

        if (irPE.nodeIndex >= 0 && irPE.nodeIndex < static_cast<int32_t>(nodeMap.size()))
            nodeMap[irPE.nodeIndex] = node;
        ++created;
    }
    {
        std::ostringstream ss;
        ss << "[Popcorn] buildPopcorn done: created=" << created << " failed=" << failed;
        PopLog(ss.str());
    }
}

// ── Wc3FaceFxBuilder (v1200) ────────────────────────────────

void Wc3FaceFxBuilder::buildFaceFX(
    const ir::IRModel& irModel, const std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    for (const auto& irAtt : irModel.attachments) {
        if (irAtt.attachmentId != -1) continue; // Only FaceFX placeholders (marker = -1)

        Object* obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, mdx_ids::BLIZZ_FACEFX));
        if (!obj) {
            reporter.warning(L"BlizzFaceFX scripted plugin not found — skipping face effect");
            continue;
        }

        INode* node = gi->CreateObjectNode(obj);
        MSTR name;
        name.printf(_T("%hs"), irAtt.name.c_str());
        node->SetName(name);

        // By name: the plug-in's first parameter is "adsorption" (General
        // rollout), so fixed ParamIDs 0/1 wrote the name into it and the
        // path into facefxName.
        auto wname = toWstr(irAtt.name);
        auto wpath = toWstr(irAtt.path);
        pbSetString(obj, L"facefxName", wname.c_str());
        pbSetString(obj, L"facefxPath", wpath.c_str());
    }
}

// ── Wc3CameraBuilder ──────────────────────────────────────

std::vector<Wc3CameraBuilder::CameraNodePair> Wc3CameraBuilder::buildCameras(
    const ir::IRModel& irModel, std::vector<INode*>& nodeMap,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    std::vector<CameraNodePair> result;
    if (irModel.cameras.empty()) return result;

    // Collect all created nodes for layer assignment
    std::vector<INode*> allCamNodes;

    for (const auto& irCam : irModel.cameras) {
        // Camera + target are built straight through the SDK. An earlier
        // revision drove MaxScript (`Targetcamera ... target:(Targetobject ...)`,
        // the NeoDex idiom) and then fished the nodes back out of the scene
        // with GetINodeByName — that lookup returned null during import and
        // every camera was silently dropped. CreateCameraObject +
        // CreateTargetObject + BindToTarget is the documented SDK sequence
        // (maxsdk/howto/import_export/asciiimp) and hands us the INode*
        // directly, so there is nothing left to look up.
        std::wstring wname = toWstr(irCam.name);
        if (wname.empty()) wname = L"Camera";

        GenCamera* camObj = gi->CreateCameraObject(TARGETED_CAMERA);
        if (!camObj) {
            reporter.warning(L"Failed to create camera object '" + wname + L"'");
            PopLog("[Camera] CreateCameraObject failed");
            result.push_back({});
            continue;
        }

        INode* camNode = gi->CreateObjectNode(camObj);
        if (!camNode) {
            reporter.warning(L"Failed to create camera node '" + wname + L"'");
            PopLog("[Camera] CreateObjectNode failed");
            camObj->DeleteThis();
            result.push_back({});
            continue;
        }
        camNode->SetName(wname.c_str());

        // Target object + LookAt controller. BindToTarget is what actually
        // wires the controller — INode::SetTarget only stores the pointer.
        INode* targetNode = nullptr;
        if (Object* targObj = gi->CreateTargetObject()) {
            targetNode = gi->CreateObjectNode(targObj);
            if (targetNode) {
                gi->BindToTarget(camNode, targetNode);
                targetNode->SetName((wname + L"_Target").c_str());
                Matrix3 targetTM;
                targetTM.IdentityMatrix();
                targetTM.SetTrans(irCam.targetPosition);
                targetNode->SetNodeTM(0, targetTM);
            } else {
                targObj->DeleteThis();
            }
        }

        Matrix3 camTM;
        camTM.IdentityMatrix();
        camTM.SetTrans(irCam.position);
        camNode->SetNodeTM(0, camTM);

        // GenCamera works in radians, the same unit ir::Camera carries and
        // the same unit CameraExtractor::extract reads back on export.
        // Clip distances are stored but manual clipping stays off, matching
        // what the MaxScript `nearclip:`/`farclip:` creation params did.
        camObj->SetFOV(0, irCam.fov);
        camObj->SetClipDist(0, CAM_HITHER_CLIP, irCam.nearClip);
        camObj->SetClipDist(0, CAM_YON_CLIP, irCam.farClip);
        camObj->Enable(TRUE);

        // Warcraft III 3.0 depth of field (IDUF / ELAF / PTSF) needs a focus
        // distance, a lens focal length and an f-number, which only the
        // Physical Camera has. It replaces the Target Camera object on the
        // same node, so the target, LookAt controller and roll built above
        // stay. The FOV is pinned ("Specify FOV") because MDX keeps it
        // static while the focal length animates. If PhysicalCamera.dlo is
        // missing, CreateInstance fails and the camera stays a Target Camera
        // without its DoF tracks.
        bool physical = false;
        if (irCam.focusDistanceTrackIndex >= 0 || irCam.focalLengthTrackIndex >= 0 ||
            irCam.fStopTrackIndex >= 0) {
            if (auto* phys = static_cast<Object*>(
                    gi->CreateInstance(CAMERA_CLASS_ID, mdx_ids::PHYSICAL_CAMERA))) {
                // A new Physical Camera is already "targeted"; the node's
                // LookAt binding supplies the target itself.
                camNode->SetObjectRef(phys);
                pbSetBool(phys, L"specify_fov", TRUE);
                // The Physical Camera's fov parameter is a plain float in
                // degrees (the MDX value is radians).
                pbSetFloat(phys, L"fov", irCam.fov * 180.0f / 3.14159265358979f);
                pbSetFloat(phys, L"clip_near", irCam.nearClip);
                pbSetFloat(phys, L"clip_far", irCam.farClip);
                // Focus on the IDUF distance rather than on the target.
                pbSetInt(phys, L"specify_focus", irCam.focusDistanceTrackIndex >= 0 ? 1 : 0);
                // The game applies DoF only with all three tracks.
                pbSetBool(phys, L"use_dof",
                          (irCam.focusDistanceTrackIndex >= 0 && irCam.focalLengthTrackIndex >= 0 &&
                           irCam.fStopTrackIndex >= 0) ? TRUE : FALSE);
                physical = true;
            } else {
                reporter.warning(L"Camera '" + wname + L"' has depth-of-field tracks, but this "
                                 L"3ds Max has no Physical Camera to hold them; they are not imported.");
            }
        }

        {
            std::ostringstream ss;
            ss << "[Camera] created '" << irCam.name << "' fov=" << irCam.fov
               << " near=" << irCam.nearClip << " far=" << irCam.farClip
               << " target=" << (targetNode ? "yes" : "no")
               << " physical=" << (physical ? "yes" : "no");
            PopLog(ss.str());
        }

        result.push_back({ camNode, targetNode });
        allCamNodes.push_back(camNode);
        if (targetNode) allCamNodes.push_back(targetNode);
    }

    // Place all camera nodes in the "Cameras" layer (matching MaxScript behavior)
    if (!allCamNodes.empty()) {
        ILayerManager* lm = GetCOREInterface13()->GetLayerManager();
        if (lm) {
            MSTR layerName(_T("Cameras"));
            ILayer* layer = lm->GetLayer(layerName);
            if (!layer)
                layer = lm->CreateLayer(layerName);
            if (layer) {
                for (INode* n : allCamNodes)
                    layer->AddToLayer(n);
            }
        }
    }

    return result;
}

// ── Wc3SequenceBuilder ─────────────────────────────────────

void Wc3SequenceBuilder::buildSequences(
    const ir::IRModel& irModel, Interface* gi,
    core::ExportErrorReporter& reporter)
{
    INode* rootNode = gi->GetRootNode();
    if (!rootNode) return;

    // If the MDX has no sequences, inject a default "Stand" sequence
    // (frame 10 → 60, looping) so the artist always has a working
    // animation range to start with. Without this the timeline collapses
    // to a single frame and the WdxSequenceStorage CA stays empty, which
    // breaks downstream tooling that assumes at least one sequence.
    //
    // We work from a local copy of the sequence list so the source
    // irModel reference stays untouched (it is const).
    std::vector<ir::Sequence> sequencesLocal;
    if (irModel.sequences.empty()) {
        ir::Sequence stand;
        stand.name = "Stand";
        stand.startTime = 10 * GetTicksPerFrame();
        stand.endTime   = 60 * GetTicksPerFrame();
        stand.isLooping = true;
        stand.rarity    = 0.0f;
        stand.moveSpeed = 0.0f;
        stand.flags     = 0;
        stand.extentMin = Point3(0.0f, 0.0f, 0.0f);
        stand.extentMax = Point3(0.0f, 0.0f, 0.0f);
        stand.extentRadius = 0.0f;
        sequencesLocal.push_back(std::move(stand));
    }
    const std::vector<ir::Sequence>& sequences =
        irModel.sequences.empty() ? sequencesLocal : irModel.sequences;

    // Remove legacy note tracks
    while (rootNode->HasNoteTracks())
        rootNode->DeleteNoteTrack(rootNode->GetNoteTrack(0), TRUE);

    // Setup WhiteoutDexSequenceData custom attributes and populate via MaxScript.
    // This matches the MaxScript rebuilder's recreateSequences().
    // Build the script: remove old CA, add fresh one, populate arrays, create FrameTags.
    std::wstring script;
    script += L"::WdxSequenceStorage.removeCA()\n";
    script += L"::WdxSequenceStorage.ensureCA()\n";

    for (const auto& seq : sequences) {
        // Convert name to wide string, escape backslashes and quotes
        std::wstring wname;
        for (char c : seq.name) {
            if (c == '\\') wname += L"\\\\";
            else if (c == '"') wname += L"\\\"";
            else wname += static_cast<wchar_t>(c);
        }

        TimeValue endTime = seq.endTime;
        if (seq.startTime == endTime) endTime += 160;  // 1 frame at 30fps

        int startF = seq.startTime / GetTicksPerFrame();
        int endF   = endTime / GetTicksPerFrame();

        // Extent string: "radius minX minY minZ maxX maxY maxZ"
        wchar_t extBuf[256];
        swprintf_s(extBuf, 256, L"%g %g %g %g %g %g %g",
            seq.extentRadius,
            seq.extentMin.x, seq.extentMin.y, seq.extentMin.z,
            seq.extentMax.x, seq.extentMax.y, seq.extentMax.z);

        wchar_t line[1024];
        swprintf_s(line, 1024,
            L"append rootNode.seqNames \"%s\"\n"
            L"append rootNode.startFrames %d\n"
            L"append rootNode.endFrames %d\n"
            L"append rootNode.nonLooping %s\n"
            L"append rootNode.rarity %g\n"
            L"append rootNode.moveSpeed %g\n"
            L"append rootNode.seqExtents \"%s\"\n"
            L"append rootNode.sharedGroup \"\"\n",
            wname.c_str(),
            startF, endF,
            seq.isLooping ? L"false" : L"true",  // nonLooping = !isLooping
            seq.rarity,
            seq.moveSpeed,
            extBuf);
        script += line;
    }

    // Create FrameTagManager entries (paired Start/End tags)
    for (const auto& seq : sequences) {
        std::wstring wname;
        for (char c : seq.name) {
            if (c == '\\') wname += L"\\\\";
            else if (c == '"') wname += L"\\\"";
            else wname += static_cast<wchar_t>(c);
        }
        TimeValue endTime = seq.endTime;
        if (seq.startTime == endTime) endTime += 160;

        wchar_t line[512];
        swprintf_s(line, 512,
            L"(local t1 = FrameTagManager.CreateNewTag \"%s Start\" %df\n"
            L"local id = FrameTagManager.GetTagID t1\n"
            L"FrameTagManager.CreateNewTag \"%s End\" %df lockID:id)\n",
            wname.c_str(), seq.startTime / GetTicksPerFrame(),
            wname.c_str(), endTime / GetTicksPerFrame());
        script += line;
    }

    ExecuteMAXScriptScript(script.c_str(),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
        MAXScript::ScriptSource::NonEmbedded,
#endif
        TRUE, nullptr);

    // Set animation range
    TimeValue minTime = sequences[0].startTime;
    TimeValue maxTime = sequences[0].endTime;
    for (const auto& seq : sequences) {
        if (seq.startTime < minTime) minTime = seq.startTime;
        if (seq.endTime > maxTime) maxTime = seq.endTime;
    }
    gi->SetAnimRange(Interval(minTime, maxTime));
}

} // namespace mdx_scene
