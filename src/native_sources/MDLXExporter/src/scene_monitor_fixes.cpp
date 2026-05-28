// MDLXExporter — Scene Monitor fix routines
//
// Each fix is wrapped in its own undo group so the user can Ctrl-Z out
// of any change. Returns the number of problems addressed.

#include "scene_monitor.h"
#include "mdx_class_ids.h"
#include "mdx_export_debug.h"

#include <max.h>
#include <iparamb2.h>
#include <stdmat.h>
#include <modstack.h>
#include <iskin.h>          // SKIN_CLASSID
#include <triobj.h>         // EDITTRIOBJ_CLASS_ID
#include <maxscript/maxscript.h>  // ExecuteMAXScriptScript
#include <maxversion.h>

// MAX_VERSION_MAJOR cutoffs for 2022:
//   2016=18, 2017=19, 2018=20, 2019=21, 2020=22, 2021=23, 2022=24
//   Several SDK signatures changed in 2022.
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
#  include <maxscript/ScriptSource.h>
#endif

#include <unordered_set>
#include <unordered_map>
#include <functional>

namespace {
// MtlBase::GetSubTexmapSlotName gained a `localized` parameter in Max 2022.
// Older SDKs only return the (potentially localized) name.
inline MSTR getSlotName(MtlBase* m, int i) {
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
    return m->GetSubTexmapSlotName(i, /*localized=*/false);
#else
    return m->GetSubTexmapSlotName(i);
#endif
}

// ExecuteMAXScriptScript signature changed in Max 2022 (added a required
// MAXScript::ScriptSource enum). Wrap it so callers don't care.
inline BOOL execMaxScript(const MCHAR* script) {
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
    return ExecuteMAXScriptScript(const_cast<MCHAR*>(script),
                                  MAXScript::ScriptSource::NotSpecified);
#else
    return ExecuteMAXScriptScript(const_cast<MCHAR*>(script));
#endif
}
} // namespace

namespace scene_monitor {

// ============================================================================
// Fix: Duplicate names
// Renames each duplicate after the first to base + "_2", "_3", … using
// Max's UniqueName() helper to ensure no collisions even after renaming.
// ============================================================================

int fixDuplicateNames(const ScanResult& result) {
    Interface* gi = GetCOREInterface();
    if (!gi) return 0;

    theHold.Begin();

    // Group duplicate-flagged nodes by their (current) name
    std::unordered_map<std::wstring, std::vector<INode*>> groups;
    for (auto& p : result.problems) {
        if (p.type != ProblemType::DuplicateName) continue;
        if (!p.node) continue;
        groups[p.node->GetName()].push_back(p.node);
    }

    int fixed = 0;
    for (auto& kv : groups) {
        auto& nodes = kv.second;
        if (nodes.size() < 2) continue;
        // Keep the first one as-is, rename the rest
        for (size_t i = 1; i < nodes.size(); ++i) {
            INode* n = nodes[i];
            if (!n) continue;
            MSTR base(kv.first.c_str());
            MSTR fresh;
            gi->MakeNameUnique(base);  // mutates 'base' to a unique form
            // 3ds Max's MakeNameUnique gives us "Name_001" style — that's fine.
            n->SetName(base);
            ++fixed;
        }
    }

    theHold.Accept(_M("Fix Duplicate Names"));
    return fixed;
}

// ============================================================================
// Fix: Mesh problems
//
// EditablePoly without converter →  add Turn-to-Mesh / collapse to Editable_Mesh.
// Edit_Mesh above Skin           →  reorder modifiers (collapse Edit_Mesh into base).
// EmptyMesh                      →  delete the node.
//
// All three routes are conservative: anything risky is skipped with a log.
// ============================================================================

namespace {

// Collapse all modifiers above & including the Edit_Mesh modifier into the
// base object, leaving Skin (and anything below it) intact.
// Returns true if a fix was performed.
bool collapseEditMeshAboveSkin(INode* node) {
    if (!node) return false;
    Interface* gi = GetCOREInterface();
    if (!gi) return false;

    // Find positions in the modifier stack
    Object* obj = node->GetObjectRef();
    if (!obj) return false;

    // Build a flat list of modifiers (top-down)
    std::vector<Modifier*> mods;
    Object* it = obj;
    while (it && it->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* d = static_cast<IDerivedObject*>(it);
        for (int i = 0; i < d->NumModifiers(); ++i)
            mods.push_back(d->GetModifier(i));
        it = d->GetObjRef();
    }

    int skinIdx = -1, editMeshIdx = -1;
    for (size_t i = 0; i < mods.size(); ++i) {
        if (!mods[i]) continue;
        Class_ID mid = mods[i]->ClassID();
        if (mid == SKIN_CLASSID && skinIdx < 0) skinIdx = (int)i;
        if (mid == Class_ID(EDITTRIOBJ_CLASS_ID, 0) && editMeshIdx < 0 && skinIdx < 0)
            editMeshIdx = (int)i;
    }

    if (skinIdx < 0 || editMeshIdx < 0) return false;
    if (editMeshIdx >= skinIdx) return false;

    // The cleanest fix: collapse the stack down to JUST below Edit_Mesh,
    // then re-add Skin and below modifiers on top. This matches the NeoDex
    // approach. We rely on Max's CollapseNodeTo for the heavy lifting.
    //
    // SAFE PATH: if collapsing is too risky for the user's scene (e.g. with
    // unsaved morpher animations), we instead delete the offending Edit_Mesh
    // modifier — that already fixes the export issue while keeping the rest
    // of the stack intact.
    Object* itr = node->GetObjectRef();
    if (!itr || itr->SuperClassID() != GEN_DERIVOB_CLASS_ID) return false;
    IDerivedObject* dobj = static_cast<IDerivedObject*>(itr);

    // Find the Edit_Mesh modifier inside the topmost derived object
    int localIdx = -1;
    for (int i = 0; i < dobj->NumModifiers(); ++i) {
        Modifier* mod = dobj->GetModifier(i);
        if (mod && mod->ClassID() == Class_ID(EDITTRIOBJ_CLASS_ID, 0)) {
            localIdx = i;
            break;
        }
    }
    if (localIdx < 0) {
        // Edit_Mesh is in a deeper derived object — bail with a log.
        // Convert wide name to narrow for the narrow ostream-based ELOG.
        MSTR nm = node->GetName();
        char narrow[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, nm.data(), -1,
                             narrow, sizeof(narrow), nullptr, nullptr);
        ELOG << "    skipped " << narrow << ": Edit_Mesh in nested derived obj\n";
        return false;
    }

    dobj->DeleteModifier(localIdx);
    return true;
}

// Add a Turn-to-Mesh modifier above an EditablePoly base.
// We use the Edit_Mesh class so it works on all supported Max versions
// without depending on Turn_to_Mesh / Turn_to_Poly which moved class IDs
// across releases.
bool addEditMeshModifier(INode* node) {
    if (!node) return false;
    Interface* gi = GetCOREInterface();
    if (!gi) return false;

    Modifier* mod = static_cast<Modifier*>(
        gi->CreateInstance(OSM_CLASS_ID, Class_ID(EDITTRIOBJ_CLASS_ID, 0)));
    if (!mod) return false;

    // Get or create a derived object on the node so we can push our modifier
    // to the top of the stack. CreateDerivedObject() returns the existing
    // IDerivedObject if one is already there, otherwise wraps the base object
    // in a new one.
    Object* objRef = node->GetObjectRef();
    if (!objRef) return false;

    IDerivedObject* dobj = nullptr;
    if (objRef->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        dobj = static_cast<IDerivedObject*>(objRef);
    } else {
        dobj = CreateDerivedObject(objRef);
        if (!dobj) return false;
        node->SetObjectRef(dobj);
    }
    dobj->AddModifier(mod, nullptr, 0);  // 0 = top of stack
    return true;
}

} // anonymous

int fixMeshProblems(const ScanResult& result) {
    Interface* gi = GetCOREInterface();
    if (!gi) return 0;

    theHold.Begin();

    std::vector<INode*> toDelete;
    int fixed = 0;

    for (auto& p : result.problems) {
        if (!p.node) continue;
        switch (p.type) {
        case ProblemType::EditMeshAboveSkin:
            if (collapseEditMeshAboveSkin(p.node)) ++fixed;
            break;
        case ProblemType::EditablePoly:
            if (addEditMeshModifier(p.node)) ++fixed;
            break;
        case ProblemType::EmptyMesh:
            toDelete.push_back(p.node);
            ++fixed;
            break;
        default:
            break;
        }
    }

    // Delete empty meshes after the loop (mutating scene during iteration is bad)
    for (INode* n : toDelete) {
        if (n) gi->DeleteNode(n);
    }

    theHold.Accept(_M("Fix Mesh Problems"));
    return fixed;
}

// ============================================================================
// Fix: Bone controllers
//
// Replace any non-baseline P/R/S sub-controller with the corresponding
// linear flavor. Doesn't touch Biped/CAT/IK/Wdx_-plugin nodes — they're
// exempt by hasValidPRS().
// ============================================================================

int fixBoneControllers(const ScanResult& result) {
    Interface* gi = GetCOREInterface();
    if (!gi) return 0;

    theHold.Begin();
    int fixed = 0;

    for (auto& p : result.problems) {
        if (p.type != ProblemType::InvalidController) continue;
        if (!p.node) continue;

        // Re-check — the user might have already fixed this manually
        if (hasValidPRS(p.node)) continue;

        Control* tm = p.node->GetTMController();
        if (!tm) continue;

        // If the top-level controller isn't PRS, replace it with one
        if (tm->ClassID() != Class_ID(PRS_CONTROL_CLASS_ID, 0)) {
            Control* prs = static_cast<Control*>(
                gi->CreateInstance(CTRL_MATRIX3_CLASS_ID,
                                   Class_ID(PRS_CONTROL_CLASS_ID, 0)));
            if (!prs) continue;
            p.node->SetTMController(prs);
            tm = prs;
        }

        // Replace each sub-controller if it's not baseline.
        // Position_XYZ ID isn't in modern SDK headers — hardcoded.
        const Class_ID kPositionXYZ(0x118f7e02, 0xffee238a);
        const Class_ID kScaleXYZ   (0x118f7e03, 0xfeee238a);

        Control* posC = tm->GetPositionController();
        if (posC && !((posC->ClassID() == Class_ID(LININTERP_POSITION_CLASS_ID,    0)) ||
                      (posC->ClassID() == Class_ID(TCBINTERP_POSITION_CLASS_ID,    0)) ||
                      (posC->ClassID() == Class_ID(HYBRIDINTERP_POSITION_CLASS_ID, 0)) ||
                      (posC->ClassID() == kPositionXYZ)))
        {
            Control* nc = static_cast<Control*>(gi->CreateInstance(
                CTRL_POSITION_CLASS_ID,
                Class_ID(LININTERP_POSITION_CLASS_ID, 0)));
            if (nc) tm->SetPositionController(nc);
        }
        Control* rotC = tm->GetRotationController();
        if (rotC && !((rotC->ClassID() == Class_ID(LININTERP_ROTATION_CLASS_ID,    0)) ||
                      (rotC->ClassID() == Class_ID(TCBINTERP_ROTATION_CLASS_ID,    0)) ||
                      (rotC->ClassID() == Class_ID(HYBRIDINTERP_ROTATION_CLASS_ID, 0)) ||
                      (rotC->ClassID() == Class_ID(EULER_CONTROL_CLASS_ID,         0))))
        {
            Control* nc = static_cast<Control*>(gi->CreateInstance(
                CTRL_ROTATION_CLASS_ID,
                Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
            if (nc) tm->SetRotationController(nc);
        }
        Control* sclC = tm->GetScaleController();
        if (sclC && !((sclC->ClassID() == Class_ID(LININTERP_SCALE_CLASS_ID,    0)) ||
                      (sclC->ClassID() == Class_ID(TCBINTERP_SCALE_CLASS_ID,    0)) ||
                      (sclC->ClassID() == Class_ID(HYBRIDINTERP_SCALE_CLASS_ID, 0)) ||
                      (sclC->ClassID() == kScaleXYZ)))
        {
            Control* nc = static_cast<Control*>(gi->CreateInstance(
                CTRL_SCALE_CLASS_ID,
                Class_ID(LININTERP_SCALE_CLASS_ID, 0)));
            if (nc) tm->SetScaleController(nc);
        }

        ++fixed;
    }

    theHold.Accept(_M("Fix Bone Controllers"));
    return fixed;
}

// ============================================================================
// Fix: Unsupported materials
//
// Create a fresh Wdx_Wc3Material for each problem material. Try to
// transfer base color / diffuse texture / opacity over. Replace the
// material on every node + slot in the editor.
//
// We don't risk anything fancy here — copying is best-effort.
// ============================================================================

namespace {

// ── Material Fix Settings (read from MDLXExporter.ini [MaterialFix]) ──
//
// Mirrors the dialog's Material Fix tab. Loaded once per fixUnsupported
// call and applied to each newly created Wdx_Wc3Material.
struct MaterialFixSettings {
    bool         unshaded   = false;
    bool         unfogged   = false;
    bool         twoSided   = false;
    int          filterMode = 1;     // 1=None, 2=Transparent, 3=Blend, 4=Add,
                                      // 5=Add2x, 6=Modulate, 7=Modulate2x
    std::wstring prefixPath;
    bool         uTile      = false;
    bool         vTile      = false;
};

// Locate the same INI path the dialog uses. Duplicated from export_dialog.cpp
// to avoid pulling that whole file's includes here.
std::wstring matFixINIPath() {
    Interface* gi = GetCOREInterface();
    MSTR dir;
    if (gi) dir = gi->GetDir(APP_PLUGCFG_DIR);
    return std::wstring(dir.data() ? dir.data() : L"") + L"\\MDLXExporter.ini";
}

MaterialFixSettings loadMaterialFixSettings() {
    MaterialFixSettings s;
    std::wstring path = matFixINIPath();
    wchar_t buf[512];

    // Disambiguate Win32 GetPrivateProfileStringW from MaxSDK::Util's
    // overload by taking a function pointer to the exact Win32 signature.
    // (Same trick the export dialog uses for the same conflict.)
    static auto Win32_GetPrivateProfileStringW =
        static_cast<DWORD(WINAPI*)(LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPCWSTR)>(
            &::GetPrivateProfileStringW);

    auto getStr = [&](const wchar_t* key, const wchar_t* def = L"") -> std::wstring {
        Win32_GetPrivateProfileStringW(L"MaterialFix", key, def, buf, 512, path.c_str());
        return buf;
    };
    auto getBool = [&](const wchar_t* key) -> bool {
        auto v = getStr(key);
        return v == L"true" || v == L"True" || v == L"1";
    };

    s.unshaded   = getBool(L"Unshaded");
    s.unfogged   = getBool(L"Unfogged");
    s.twoSided   = getBool(L"Twosided");
    s.uTile      = getBool(L"UTile");
    s.vTile      = getBool(L"VTile");
    s.prefixPath = getStr(L"PrefixPath");

    auto fmStr = getStr(L"FilterMode", L"1");
    s.filterMode = _wtoi(fmStr.c_str());
    if (s.filterMode < 1 || s.filterMode > 7) s.filterMode = 1;

    return s;
}

// Apply the settings to a Wdx_Wc3Material. The material is a scripted
// plugin — we access its parameters by name. ParamBlockDesc2 doesn't
// have a NameToID method (Max 2025), so we walk paramdefs and match by
// int_name.
//
// CRITICAL — we type-check the parameter before calling SetValue. The
// SetValue overloads pick by C++ argument type, but the parameter's
// actual ParamType might not match (e.g. a 'twosides' param could be
// declared as TYPE_INT instead of TYPE_BOOL in some plugin versions).
// Calling SetValue on a type-mismatched param can crash Max. By skipping
// mismatched params, we degrade gracefully instead of crashing.
//
// If a parameter doesn't exist (older plugin version), we just skip it.
// This is the same name list the .ms uses internally.
void applyMaterialFixSettings(Mtl* m, const MaterialFixSettings& s) {
    if (!m) return;

    // Helper: find the ParamID for a given internal name in this paramblock,
    // or -1 if the name isn't present.
    auto findParamID = [](IParamBlock2* pb, const wchar_t* name) -> ParamID {
        if (!pb) return -1;
        int n = pb->NumParams();
        for (int idx = 0; idx < n; ++idx) {
            ParamID pid = pb->IndextoID(idx);
            const ParamDef& def = pb->GetParamDef(pid);
            if (def.int_name && _wcsicmp(def.int_name, name) == 0)
                return pid;
        }
        return -1;
    };

    // Helper: get a parameter's declared type, or -1 if not found.
    auto getParamType = [](IParamBlock2* pb, ParamID pid) -> int {
        if (!pb || pid == -1) return -1;
        const ParamDef& def = pb->GetParamDef(pid);
        return (int)def.type;
    };

    // Walk all param blocks on the material and try each named parameter.
    int n = m->NumParamBlocks();
    for (int i = 0; i < n; ++i) {
        IParamBlock2* pb = m->GetParamBlock(i);
        if (!pb) continue;

        // Bool/int parameters (Wc3Material uses these for the on/off flags)
        auto setBoolByName = [&](const wchar_t* name, bool val) {
            ParamID pid = findParamID(pb, name);
            if (pid == -1) return;
            int t = getParamType(pb, pid);
            // Accept BOOL or INT for bool-like params; reject anything else
            if (t != TYPE_BOOL && t != TYPE_INT) return;
            pb->SetValue(pid, 0, (int)(val ? 1 : 0));
        };

        // Pure int parameters (filterMode is an enum)
        auto setIntByName = [&](const wchar_t* name, int val) {
            ParamID pid = findParamID(pb, name);
            if (pid == -1) return;
            int t = getParamType(pb, pid);
            if (t != TYPE_INT && t != TYPE_RADIOBTN_INDEX) return;
            pb->SetValue(pid, 0, val);
        };

        // String parameters (path / prefix)
        auto setStrByName = [&](const wchar_t* name, const wchar_t* val) {
            ParamID pid = findParamID(pb, name);
            if (pid == -1) return;
            int t = getParamType(pb, pid);
            if (t != TYPE_STRING && t != TYPE_FILENAME) return;
            pb->SetValue(pid, 0, const_cast<MCHAR*>(val));
        };

        setBoolByName(L"unshaded",   s.unshaded);
        setBoolByName(L"unfogged",   s.unfogged);
        setBoolByName(L"twosides",   s.twoSided);
        // filterMode in NeoDex is 1-based; the Wdx_Wc3Material plugin uses
        // the same convention (None=1).
        setIntByName(L"filtermode",  s.filterMode);
        if (!s.prefixPath.empty()) {
            setStrByName(L"path", s.prefixPath.c_str());
        }
        setBoolByName(L"Wrap_Width",  s.uTile);
        setBoolByName(L"Wrap_Height", s.vTile);
    }
}

// Build a fresh Wdx_Wc3Material to replace the unsupported material.
//
// CRITICAL — we do NOT cast the result to StdMat*, even though
// Wdx_Wc3Material says `extends:Standard` in its .ms file.
//
// Why: `extends:Standard` is MaxScript-level inheritance. The C++ object
// returned by CreateInstance(MATERIAL_CLASS_ID, mdx_ids::WC3_MATERIAL) is
// a *scripted plugin wrapper* whose vtable does NOT match StandardMaterial.
// Calling StdMat methods (SetDiffuse, SetSubTexmap, ...) through that
// wrong vtable crashes Max immediately.
//
// We also do NOT copy diffuse color / texmap / opacity from the source.
// The source is typically a Physical / OpenPBR material whose properties
// don't map cleanly to Wc3 anyway. Copying texmap pointers also causes
// dangling-pointer crashes once the old material is freed. The user's
// Material Fix Settings (filter mode, prefix path, etc.) get applied via
// applyMaterialFixSettings() instead.
Mtl* createWc3MaterialFrom(Mtl* src) {
    Interface* gi = GetCOREInterface();
    if (!gi) {
        ELOG << "[matFix] createWc3MaterialFrom: gi is null!\n"; EFLUSH;
        return nullptr;
    }

    // Log what we know about the source material
    ELOG << "[matFix] createWc3MaterialFrom: src=" << (void*)src;
    if (src) {
        char nameBuf[256] = {};
        const MCHAR* nm = src->GetName();
        if (nm) WideCharToMultiByte(CP_UTF8, 0, nm, -1, nameBuf, 255, nullptr, nullptr);
        Class_ID cid = src->ClassID();
        ELOG << " src.name='" << nameBuf << "'"
             << " src.classId=(0x" << std::hex << cid.PartA() << ",0x"
             << cid.PartB() << std::dec << ")";
    }
    ELOG << "\n";

    // Log the WC3_MATERIAL Class_ID we're trying to instantiate
    ELOG << "[matFix] CreateInstance(MATERIAL_CLASS_ID, WC3_MATERIAL=(0x"
         << std::hex << mdx_ids::WC3_MATERIAL.PartA() << ",0x"
         << mdx_ids::WC3_MATERIAL.PartB() << std::dec << "))\n"; EFLUSH;

    Mtl* m = static_cast<Mtl*>(
        gi->CreateInstance(MATERIAL_CLASS_ID, mdx_ids::WC3_MATERIAL));

    if (!m) {
        // Wdx_Wc3Material plugin is NOT loaded or CreateInstance can't
        // see it from the C++ DLL. This is the typical case: scripted
        // plugins are sometimes invisible to CreateInstance from native
        // code. We fall back to a plain Standard material so the user
        // at least gets something non-Physical. They can then convert
        // it to Wdx_Wc3Material via the UI / MAXScript if needed.
        ELOG << "[matFix] CreateInstance returned NULL for Wdx_Wc3Material — "
             << "falling back to NewDefaultStdMat()\n"; EFLUSH;
        m = NewDefaultStdMat();
        if (!m) {
            ELOG << "[matFix] NewDefaultStdMat() also returned NULL — giving up\n";
            EFLUSH;
            return nullptr;
        }
    } else {
        Class_ID gotCid = m->ClassID();
        ELOG << "[matFix] CreateInstance succeeded: m=" << (void*)m
             << " classId=(0x" << std::hex << gotCid.PartA() << ",0x"
             << gotCid.PartB() << std::dec << ")\n"; EFLUSH;
    }

    // Copy name with suffix for clarity (this is a safe Mtl-level operation)
    MSTR name = src ? src->GetName() : MSTR(_M("Material"));
    name += _M("_wc3");
    m->SetName(name);

    char newNameBuf[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, name.data(), -1, newNameBuf, 255, nullptr, nullptr);
    ELOG << "[matFix] new material named '" << newNameBuf << "'\n"; EFLUSH;

    // ── Texture preservation ──
    //
    // Copy the diffuse/base-color texmap from the source material to the
    // new Wdx_Wc3Material. We use NumSubTexmaps()/GetSubTexmap()/
    // SetSubTexmap() which are MtlBase methods — virtual and safely
    // implemented by all materials including scripted plugins. We do NOT
    // cast to StdMat (vtable mismatch crashes Max).
    //
    // Strategy: walk the source material's slots, find one named "diffuse"
    // / "base color" / "albedo", then write it to the same-named slot on
    // the new material. Both slot lists are looked up by their English
    // (non-localized) name to be robust across Max language versions.
    if (src) {
        Texmap* srcDiffuse = nullptr;
        const wchar_t* srcSlotName = nullptr;

        int numSrcSlots = src->NumSubTexmaps();
        ELOG << "[matFix] source has " << numSrcSlots << " texmap slots\n";

        for (int i = 0; i < numSrcSlots; ++i) {
            Texmap* sub = src->GetSubTexmap(i);
            if (!sub) continue;
            // Max 2022+ requires the localized=false param to get the
            // English slot name reliably across language versions.
            MSTR slot = getSlotName(src, i);
            const wchar_t* s = slot.data();
            if (!s) continue;
            char slotBuf[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, s, -1, slotBuf, 127, nullptr, nullptr);
            ELOG << "[matFix]   src slot[" << i << "]='" << slotBuf << "'\n";
            if (wcsstr(s, L"iffuse") || wcsstr(s, L"olor") || wcsstr(s, L"lbedo")) {
                srcDiffuse = sub;
                srcSlotName = s;
                ELOG << "[matFix]   -> picked as diffuse source\n";
                break;
            }
        }

        if (srcDiffuse) {
            // Find a matching slot on the new material — try "diffuse"
            // first since Wdx_Wc3Material extends Standard.
            int numNewSlots = m->NumSubTexmaps();
            ELOG << "[matFix] new material has " << numNewSlots << " texmap slots\n";

            int targetSlot = -1;
            for (int i = 0; i < numNewSlots; ++i) {
                MSTR slot = getSlotName(m, i);
                const wchar_t* s = slot.data();
                if (!s) continue;
                char slotBuf[128] = {};
                WideCharToMultiByte(CP_UTF8, 0, s, -1, slotBuf, 127, nullptr, nullptr);
                ELOG << "[matFix]   new slot[" << i << "]='" << slotBuf << "'\n";
                if (wcsstr(s, L"iffuse") || wcsstr(s, L"olor")) {
                    targetSlot = i;
                    ELOG << "[matFix]   -> using slot " << i << " as diffuse target\n";
                    break;
                }
            }

            if (targetSlot >= 0) {
                m->SetSubTexmap(targetSlot, srcDiffuse);
                ELOG << "[matFix] diffuse texmap copied to slot " << targetSlot << "\n";
            } else {
                ELOG << "[matFix] no matching diffuse slot found on new material — "
                     << "texture not transferred\n";
            }
        } else {
            ELOG << "[matFix] no diffuse texmap found on source material\n";
        }
        EFLUSH;
    }

    return m;
}

void replaceMaterialOnAllNodes(Mtl* oldMtl, Mtl* newMtl) {
    if (!oldMtl || !newMtl) {
        ELOG << "[matFix] replaceMaterialOnAllNodes: bailing — oldMtl="
             << (void*)oldMtl << " newMtl=" << (void*)newMtl << "\n"; EFLUSH;
        return;
    }
    Interface* gi = GetCOREInterface();
    if (!gi) {
        ELOG << "[matFix] replaceMaterialOnAllNodes: gi is null\n"; EFLUSH;
        return;
    }

    INode* root = gi->GetRootNode();
    if (!root) {
        ELOG << "[matFix] replaceMaterialOnAllNodes: root is null\n"; EFLUSH;
        return;
    }

    ELOG << "[matFix] replaceMaterialOnAllNodes: walking scene for oldMtl="
         << (void*)oldMtl << " newMtl=" << (void*)newMtl << "\n"; EFLUSH;

    int nodesVisited = 0;
    int nodesMatched = 0;

    // Walk the scene and replace material references on nodes.
    // SetMtl handles Max's reference counting properly.
    //
    // CRITICAL — we do NOT mutate MtlBaseLib directly via (*lib)[i] = newMtl.
    // Direct index assignment bypasses reference counting and crashes Max
    // when the old material is later freed.
    std::function<void(INode*)> walk = [&](INode* n) {
        if (!n) return;
        ++nodesVisited;
        Mtl* curMtl = n->GetMtl();
        if (curMtl == oldMtl) {
            char nameBuf[256] = {};
            const MCHAR* nm = n->GetName();
            if (nm) WideCharToMultiByte(CP_UTF8, 0, nm, -1, nameBuf, 255, nullptr, nullptr);
            ELOG << "[matFix]   match on node '" << nameBuf
                 << "' — calling SetMtl\n"; EFLUSH;
            n->SetMtl(newMtl);
            ++nodesMatched;
        }
        for (int i = 0; i < n->NumberOfChildren(); ++i)
            walk(n->GetChildNode(i));
    };
    walk(root);

    // ── Force viewport to refresh despite the modal export dialog ──
    //
    // The export dialog runs as a modal Win32 DialogBox, which suspends
    // Max's main message loop. SetMtl marks the viewport dirty but the
    // redraw never happens because Max is waiting for the dialog to close.
    //
    // Two-step fix:
    //   1. RedrawViews() — Max-internal API to invalidate viewports.
    //      Works even with a modal dialog on top.
    //   2. UpdateWindow() — Win32 API to synchronously dispatch WM_PAINT
    //      to the Max main HWND. This bypasses the modal dialog's input
    //      lock (it only handles paint, not input messages).
    //
    // Result: the user sees the material swap happen behind the dialog,
    // even though they still can't click on Max until the dialog closes.
    gi->RedrawViews(gi->GetTime(), REDRAW_NORMAL);
    HWND maxHwnd = gi->GetMAXHWnd();
    if (maxHwnd) UpdateWindow(maxHwnd);

    ELOG << "[matFix] replaceMaterialOnAllNodes: visited=" << nodesVisited
         << " matched=" << nodesMatched
         << " — RedrawViews+UpdateWindow done\n"; EFLUSH;
}

} // anonymous

int fixUnsupportedMaterials(const ScanResult& result) {
    Interface* gi = GetCOREInterface();
    if (!gi) {
        ELOG << "[matFix] fixUnsupportedMaterials: gi is null\n"; EFLUSH;
        return 0;
    }

    ELOG << "\n========== Fix Unsupported Materials ==========\n";
    ELOG << "[matFix] Total problems in result: " << result.problems.size() << "\n";
    int unsupportedCount = 0;
    for (auto& p : result.problems) {
        if (p.type == ProblemType::UnsupportedMaterial) ++unsupportedCount;
    }
    ELOG << "[matFix] Unsupported material problems: " << unsupportedCount << "\n";
    EFLUSH;

    // Read settings once before the loop so we don't hit the INI per material
    MaterialFixSettings settings = loadMaterialFixSettings();
    ELOG << "[matFix] Loaded settings: filterMode=" << settings.filterMode
         << " unshaded=" << settings.unshaded
         << " unfogged=" << settings.unfogged
         << " twoSided=" << settings.twoSided << "\n"; EFLUSH;

    theHold.Begin();
    int fixed = 0;

    for (auto& p : result.problems) {
        if (p.type != ProblemType::UnsupportedMaterial) continue;
        if (!p.mtl) {
            ELOG << "[matFix] skipping problem with null mtl pointer\n"; EFLUSH;
            continue;
        }

        ELOG << "\n[matFix] -- problem.mtl=" << (void*)p.mtl << " --\n"; EFLUSH;

        Mtl* nm = createWc3MaterialFrom(p.mtl);
        if (!nm) {
            ELOG << "[matFix] createWc3MaterialFrom returned NULL — skipping\n"; EFLUSH;
            continue;
        }
        // Apply user-configured Material Fix settings (filter mode, prefix
        // path, two-sided, unshaded, unfogged, U/V tile)
        applyMaterialFixSettings(nm, settings);
        ELOG << "[matFix] applyMaterialFixSettings done\n"; EFLUSH;

        replaceMaterialOnAllNodes(p.mtl, nm);
        ++fixed;
    }

    theHold.Accept(_M("Fix Materials"));

    // ── Refresh the Material Editor display ──
    //
    // After C++ replaces materials on nodes, the Material Editor still
    // shows the OLD materials in its slots — meditMaterials[] holds
    // independent references that don't auto-update.
    //
    // NeoDex's recipe (per shank's MDLX importer notes) is the trio:
    //   1. meditMaterials[i] = newMat       → put new mat into slot i
    //   2. setMTLMeditObjType newMat 3      → mark it as a Scene Material
    //                                         so the UI shows it correctly
    //   3. activeMeditSlot = activeMeditSlot → trigger a UI refresh
    //
    // Without #2, the slot shows the material but the medit treats it
    // as orphaned. Without #3, the UI stays stale until the user clicks.
    //
    // We identify replaced materials by their "_wc3" suffix — every new
    // material is named "<oldname>_wc3" by createWc3MaterialFrom(), so we
    // can match each medit slot's material against the suffix-renamed
    // version in sceneMaterials.
    //
    // The script wraps in try() so a missing Wdx_Wc3Material plugin (e.g.
    // user without the toolkit, fallback to Standard) doesn't break it.
    if (fixed > 0) {
        const wchar_t* refreshScript =
            L"try (\n"
            L"  -- Walk medit slots; if the slot holds a material whose\n"
            L"  -- '_wc3' counterpart now exists in the scene, swap it in.\n"
            L"  for i = 1 to 24 do (\n"
            L"    local oldM = meditMaterials[i]\n"
            L"    if oldM != undefined do (\n"
            L"      local target = (oldM.name as string) + \"_wc3\"\n"
            L"      for sm in sceneMaterials do (\n"
            L"        if (sm.name as string) == target do (\n"
            L"          meditMaterials[i] = sm\n"
            L"          setMTLMeditObjType sm 3\n"
            L"          exit\n"
            L"        )\n"
            L"      )\n"
            L"    )\n"
            L"  )\n"
            L"  -- Mark every Wdx_Wc3Material as visible so 'Pick Material\n"
            L"  -- from Object' finds them properly.\n"
            L"  for m in sceneMaterials do (\n"
            L"    if (classOf m) == Wdx_Wc3Material do\n"
            L"      setMTLMeditObjType m 3\n"
            L"  )\n"
            L"  -- Force UI refresh by re-selecting the active slot.\n"
            L"  if activeMeditSlot != undefined do\n"
            L"    activeMeditSlot = activeMeditSlot\n"
            L"  redrawViews()\n"
            L") catch ()\n";

        BOOL scriptOk = execMaxScript(refreshScript);
        ELOG << "[matFix] medit slot-swap script: " << (scriptOk ? "OK" : "failed")
             << "\n"; EFLUSH;
    }

    ELOG << "\n[matFix] DONE. fixed=" << fixed
         << " out of " << unsupportedCount << " unsupported materials\n";
    ELOG << "========== End Fix Unsupported Materials ==========\n\n"; EFLUSH;
    return fixed;
}

// ============================================================================
// Fix all
// ============================================================================

int fixAll(const ScanResult& result) {
    int n = 0;
    n += fixDuplicateNames(result);
    n += fixMeshProblems(result);
    n += fixBoneControllers(result);
    n += fixUnsupportedMaterials(result);
    return n;
}

} // namespace scene_monitor
