// MDLXExporter — Scene Monitor implementation
#include "scene_monitor.h"
#include "mdx_class_ids.h"
#include "resource.h"
#include "mdx_export_debug.h"

#include <max.h>
#include <iparamb2.h>
#include <iparamm2.h>
#include <stdmat.h>
#include <modstack.h>
#include <iskin.h>          // SKIN_CLASSID
#include <triobj.h>         // EDITTRIOBJ_CLASS_ID
#include <polyobj.h>        // POLYOBJ_CLASS_ID
#include <CS/BIPEXP.h>      // BIPBODY_CONTROL_CLASS_ID, BIPDRIVEN_CONTROL_CLASS_ID
#include <icustattribcontainer.h>
#include <maxversion.h>

// Pre-Max 2022 SDKs only export BIPSLAVE_CONTROL_CLASS_ID (renamed to
// BIPDRIVEN_CONTROL_CLASS_ID in 2022 — same Class_ID(0x9154,0) value).
#ifndef BIPDRIVEN_CONTROL_CLASS_ID
#define BIPDRIVEN_CONTROL_CLASS_ID BIPSLAVE_CONTROL_CLASS_ID
#endif

#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <functional>
#include <commctrl.h>
#include <commdlg.h>

namespace scene_monitor {

// ============================================================================
// Class-ID classification helpers
// ============================================================================

namespace {

// Built-in 3ds Max controller class IDs.
// We only allow these on bone/helper transform tracks. Anything else is
// flagged because it can't be reproduced in the MDX/MDL format without
// per-frame baking (which only Biped/CAT/IK/Link/Wdx_-plugins get).
const Class_ID LINEAR_POSITION_ID  = Class_ID(LININTERP_POSITION_CLASS_ID, 0);
const Class_ID LINEAR_ROTATION_ID  = Class_ID(LININTERP_ROTATION_CLASS_ID, 0);
const Class_ID LINEAR_SCALE_ID     = Class_ID(LININTERP_SCALE_CLASS_ID,    0);
const Class_ID TCB_POSITION_ID     = Class_ID(TCBINTERP_POSITION_CLASS_ID, 0);
const Class_ID TCB_ROTATION_ID     = Class_ID(TCBINTERP_ROTATION_CLASS_ID, 0);
const Class_ID TCB_SCALE_ID        = Class_ID(TCBINTERP_SCALE_CLASS_ID,    0);
const Class_ID BEZIER_POSITION_ID  = Class_ID(HYBRIDINTERP_POSITION_CLASS_ID, 0);
const Class_ID BEZIER_ROTATION_ID  = Class_ID(HYBRIDINTERP_ROTATION_CLASS_ID, 0);
const Class_ID BEZIER_SCALE_ID     = Class_ID(HYBRIDINTERP_SCALE_CLASS_ID,    0);
const Class_ID EULER_XYZ_ID        = Class_ID(EULER_CONTROL_CLASS_ID, 0);
const Class_ID PRS_CONTROL_ID      = Class_ID(PRS_CONTROL_CLASS_ID, 0);

// Position_XYZ and Scale_XYZ — these IDs aren't exposed via the modern
// Max SDK headers (IPOS_CONTROL_CLASS_ID / ISCALE_CONTROL_CLASS_ID were
// removed in Max 2025). Hardcoded values from animtbl.h in older SDKs.
const Class_ID POSITION_XYZ_ID     = Class_ID(0x118f7e02, 0xffee238a);
const Class_ID SCALE_XYZ_ID        = Class_ID(0x118f7e03, 0xfeee238a);

// Link Constraint — hardcoded from scene debug output (not in SDK headers).
const Class_ID LINK_CONSTRAINT_ID  = Class_ID(0x873fe764, 0xaabe8601);

bool isBaselinePosCtrl(Control* c) {
    if (!c) return false;
    Class_ID id = c->ClassID();
    return id == LINEAR_POSITION_ID
        || id == TCB_POSITION_ID
        || id == BEZIER_POSITION_ID
        || id == POSITION_XYZ_ID;
}

bool isBaselineRotCtrl(Control* c) {
    if (!c) return false;
    Class_ID id = c->ClassID();
    return id == LINEAR_ROTATION_ID
        || id == TCB_ROTATION_ID
        || id == BEZIER_ROTATION_ID
        || id == EULER_XYZ_ID;
}

bool isBaselineSclCtrl(Control* c) {
    if (!c) return false;
    Class_ID id = c->ClassID();
    return id == LINEAR_SCALE_ID
        || id == TCB_SCALE_ID
        || id == BEZIER_SCALE_ID
        || id == SCALE_XYZ_ID;
}

// True if the node is a Biped or CAT bone (full-rig systems whose
// controllers are NEVER plain PRS but are nonetheless valid because the
// exporter resamples them to per-frame TM).
bool isBipedOrCATNode(INode* node) {
    if (!node) return false;

    // ── Biped: detected via TM controller class ID ──
    Control* tmCtrl = node->GetTMController();
    if (tmCtrl) {
        Class_ID ctrlID = tmCtrl->ClassID();
        if (ctrlID == BIPBODY_CONTROL_CLASS_ID ||
            ctrlID == BIPDRIVEN_CONTROL_CLASS_ID ||
            ctrlID == FOOTPRINT_CLASS_ID)
            return true;
    }

    // ── CAT / Hub: detected via base object class name (CAT IDs change
    // between Max versions, name sniffing is the reliable path).
    Object* obj = node->GetObjectRef();
    if (obj) {
        // Walk past modifier stack to base object
        Object* base = obj;
        while (base && base->SuperClassID() == GEN_DERIVOB_CLASS_ID)
            base = static_cast<IDerivedObject*>(base)->GetObjRef();

        if (base) {
            MSTR cn;
            base->GetClassName(cn);
            const wchar_t* s = cn.data();
            if (s) {
                if (wcsstr(s, L"CAT")  != nullptr) return true;
                if (wcsstr(s, L"Hub")  != nullptr) return true;
            }
        }
    }

    // Top-level controller class name as final fallback
    if (tmCtrl) {
        MSTR ctn;
        tmCtrl->GetClassName(ctn);
        const wchar_t* cs = ctn.data();
        if (cs) {
            if (wcsstr(cs, L"CAT") != nullptr) return true;
            if (wcsstr(cs, L"Xtra")!= nullptr) return true;
            if (wcsstr(cs, L"LayerMatrix") != nullptr) return true;
            if (wcsstr(cs, L"IKTarget") != nullptr) return true;
            if (wcsstr(cs, L"Vertical_Horizontal_Turn") != nullptr) return true;
        }
    }
    return false;
}

// True if the node uses an IK transform controller.
// We detect: HI/HD IKChainControl, IKControl, IK_Solver classes.
bool isIKNode(INode* node) {
    if (!node) return false;
    Control* c = node->GetTMController();
    if (!c) return false;

    // Sniff class name — IK controllers vary by Max version
    MSTR cn;
    c->GetClassName(cn);
    const wchar_t* s = cn.data();
    if (s && (wcsstr(s, L"IK") != nullptr)) return true;

    // Check sub-controllers
    Control* posC = c->GetPositionController();
    Control* rotC = c->GetRotationController();
    if (posC) {
        MSTR pcn; posC->GetClassName(pcn);
        const wchar_t* ps = pcn.data();
        if (ps && wcsstr(ps, L"IK") != nullptr) return true;
    }
    if (rotC) {
        MSTR rcn; rotC->GetClassName(rcn);
        const wchar_t* rs = rcn.data();
        if (rs && wcsstr(rs, L"IK") != nullptr) return true;
    }
    return false;
}

// All Wdx_-plugin Class_IDs from mdx_class_ids.h, plus the legacy WC3_BITMAP.
// Used by isWdxPluginNode + material check (WC3_MATERIAL).
bool classIdIsWdxNodePlugin(Class_ID id) {
    return id == mdx_ids::WC3_ATTACH_POINT
        || id == mdx_ids::WC3_COLLISION_SPH
        || id == mdx_ids::WC3_COLLISION_BOX
        || id == mdx_ids::WC3_LIGHT
        || id == mdx_ids::WC3_EVENT_V2021
        || id == mdx_ids::WC3_EVENT_V2020
        || id == mdx_ids::WC3_FACEFX
        || id == mdx_ids::WC3_POPCORN
        || id == mdx_ids::WC3_PARTICLES1
        || id == mdx_ids::WC3_PARTICLES2
        || id == mdx_ids::WC3_RIBBON
        // NeoDex equivalents
        || id == mdx_ids::NEODEX_ATTACH_POINT
        || id == mdx_ids::NEODEX_COLLISION_SPH
        || id == mdx_ids::NEODEX_COLLISION_BOX
        || id == mdx_ids::NEODEX_LIGHT
        || id == mdx_ids::NEODEX_EVENT
        || id == mdx_ids::NEODEX_EVENT_V2020
        || id == mdx_ids::NEODEX_FACEFX
        || id == mdx_ids::NEODEX_POPCORN
        || id == mdx_ids::NEODEX_PARTICLES1
        || id == mdx_ids::NEODEX_PARTICLES2
        || id == mdx_ids::NEODEX_RIBBON;
}

// True if the mesh node is an Editable_Mesh / Editable_Poly / TriMesh.
// We don't flag those — they're the desired base. We flag only the
// pre-base-conversion EditablePoly, and Empty meshes.
bool isMeshGeometryNode(INode* node) {
    if (!node) return false;
    ObjectState os = node->EvalWorldState(0);
    if (!os.obj) return false;
    if (os.obj->SuperClassID() != GEOMOBJECT_CLASS_ID) return false;
    return true;
}

// Recursively iterate scene nodes, calling |cb| for each.
template <class Callback>
void forEachNodeRec(INode* node, Callback cb) {
    if (!node) return;
    cb(node);
    for (int i = 0; i < node->NumberOfChildren(); ++i)
        forEachNodeRec(node->GetChildNode(i), cb);
}

// Returns true if Skin modifier sits below an Edit_Mesh modifier in the
// modifier stack (skin must be at-or-below for the editor's deformation
// to apply correctly to the exported mesh).
bool hasEditMeshAboveSkin(INode* node) {
    if (!node) return false;
    Object* obj = node->GetObjectRef();
    if (!obj) return false;

    int skinIdx = -1;
    int editMeshAbove = -1;

    // Walk the modifier stack: derivedObject is at the TOP, base at the BOTTOM
    int stackIndex = 0;
    Object* current = obj;
    while (current && current->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        IDerivedObject* dobj = static_cast<IDerivedObject*>(current);
        for (int i = 0; i < dobj->NumModifiers(); ++i) {
            Modifier* mod = dobj->GetModifier(i);
            if (!mod) continue;
            Class_ID mid = mod->ClassID();
            // Skin
            if (mid == SKIN_CLASSID) {
                if (skinIdx < 0) skinIdx = stackIndex;
            }
            // Edit_Mesh — wrap the ulong constant in Class_ID
            if (mid == Class_ID(EDITTRIOBJ_CLASS_ID, 0)) {
                if (skinIdx < 0) {
                    // Edit_Mesh appears ABOVE skin (still no skin seen yet)
                    editMeshAbove = stackIndex;
                }
            }
            ++stackIndex;
        }
        current = dobj->GetObjRef();
    }

    return (skinIdx >= 0) && (editMeshAbove >= 0) && (editMeshAbove < skinIdx);
}

bool isMaterialUnsupported(Mtl* m) {
    if (!m) return false;

    // Wdx_Wc3Material and NeoDex "Warcraft 3" material are both supported.
    if (m->ClassID() == mdx_ids::WC3_MATERIAL ||
        m->ClassID() == mdx_ids::NEODEX_MATERIAL) return false;

    // Wrapper materials (Multi/Sub, Composite, Blend, Shell, etc.): accept
    // the parent — children are checked individually by collectMaterialProblems.
    if (m->NumSubMtls() > 0) return false;

    // Anything else — Standard, Physical, OpenPBR, PBR, Arnold, V-Ray, etc.
    // — is not supported.
    return true;
}

} // anonymous namespace

// ============================================================================
// Public helpers
// ============================================================================

bool isWdxPluginNode(INode* node) {
    if (!node) return false;
    Object* obj = node->GetObjectRef();
    if (!obj) return false;
    // Walk past modifier stack to base object
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        obj = static_cast<IDerivedObject*>(obj)->GetObjRef();
    }
    if (!obj) return false;
    return classIdIsWdxNodePlugin(obj->ClassID());
}

bool hasValidPRS(INode* node) {
    if (!node) return true;
    if (isBipedOrCATNode(node)) return true;
    if (isIKNode(node))         return true;
    if (isWdxPluginNode(node))  return true;

    Control* tm = node->GetTMController();
    if (!tm) return false;

    // Link Constraint is baked at export — accept it.
    // Check both ClassID and class name string for robustness across
    // Max versions (the Link Constraint ClassID isn't in SDK headers).
    if (tm->ClassID() == LINK_CONSTRAINT_ID) return true;
    {
        MSTR className;
        tm->GetClassName(className);
        if (className.data()) {
            std::wstring cn(className.data());
            if (cn == L"Link Constraint" || cn == L"link_constraint" ||
                cn == L"Link_Constraint")
                return true;
        }
    }

    // Top-level must be PRS
    if (tm->ClassID() != PRS_CONTROL_ID) return false;

    return isBaselinePosCtrl(tm->GetPositionController())
        && isBaselineRotCtrl(tm->GetRotationController())
        && isBaselineSclCtrl(tm->GetScaleController());
}

// ============================================================================
// Detection
// ============================================================================

namespace {

// A node counts as "bone-like" for the purposes of controller validation if
// it's a BoneObject, dummy, helper, biped/CAT bone, or any Wdx_-plugin node.
// Geoms / lights / cameras are skipped — they have their own export paths.
bool isBoneLikeNode(INode* node) {
    if (!node || !node->GetObjectRef()) return false;

    Object* obj = node->GetObjectRef();
    while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID)
        obj = static_cast<IDerivedObject*>(obj)->GetObjRef();
    if (!obj) return false;

    SClass_ID sid = obj->SuperClassID();
    if (sid == HELPER_CLASS_ID)     return true;     // dummy, point, etc.
    Class_ID  cid = obj->ClassID();
    if (cid == BONE_OBJ_CLASSID)              return true;  // BoneGeometry (modern bone)
    if (cid.PartA() == BONE_CLASS_ID)         return true;  // legacy bone
    if (isBipedOrCATNode(node))     return true;
    if (isWdxPluginNode(node))      return true;
    return false;
}

void collectDuplicates(INode* root, std::vector<Problem>& out) {
    std::unordered_map<std::wstring, int> nameCount;
    std::unordered_set<std::wstring> reported;
    std::vector<INode*> all;

    forEachNodeRec(root, [&](INode* n) {
        if (n == root) return;  // skip scene root itself
        all.push_back(n);
        std::wstring nm = n->GetName();
        nameCount[nm]++;
    });

    // Emit one Problem entry per duplicated name (per node carrying it)
    for (INode* n : all) {
        std::wstring nm = n->GetName();
        if (nameCount[nm] > 1) {
            Problem p;
            p.type = ProblemType::DuplicateName;
            p.node = n;
            p.displayName = nm;
            out.push_back(p);
        }
    }
}

void collectMeshProblems(INode* root, std::vector<Problem>& out) {
    forEachNodeRec(root, [&](INode* n) {
        if (n == root) return;
        if (!isMeshGeometryNode(n)) return;

        // 0) Multi/Sub-Object material. The exporter writes one material per
        //    mesh (one geoset each), so the mesh has to be split per
        //    sub-material first. Other checks still run: the split pieces
        //    inherit whatever else is wrong and are re-scanned afterwards.
        if (Mtl* m = n->GetMtl(); m && m->ClassID() == MULTI_MATERIAL_CLASS_ID) {
            Problem p;
            p.type = ProblemType::MultiMaterialMesh;
            p.node = n;
            p.mtl  = m;
            p.displayName = std::wstring(n->GetName()) + L"  [Multi/Sub-Object: " +
                            std::to_wstring(m->NumSubMtls()) + L"]";
            out.push_back(p);
        }

        // 1) Edit_Mesh above Skin
        if (hasEditMeshAboveSkin(n)) {
            Problem p;
            p.type = ProblemType::EditMeshAboveSkin;
            p.node = n;
            p.displayName = std::wstring(n->GetName()) + L"  [Edit_Mesh above Skin]";
            out.push_back(p);
            // continue: the node may also be empty / EditablePoly. Don't.
            return;
        }

        // 2) Inspect base object class
        Object* base = n->GetObjectRef();
        while (base && base->SuperClassID() == GEN_DERIVOB_CLASS_ID)
            base = static_cast<IDerivedObject*>(base)->GetObjRef();
        if (!base) return;

        Class_ID baseCid = base->ClassID();

        // EditablePoly with no Edit_Mesh / Turn_to_Mesh modifier above it
        if (baseCid == Class_ID(POLYOBJ_CLASS_ID, 0)) {
            // Walk modifier stack — does it have a converter?
            bool hasConverter = false;
            Object* obj = n->GetObjectRef();
            while (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
                IDerivedObject* dobj = static_cast<IDerivedObject*>(obj);
                for (int i = 0; i < dobj->NumModifiers(); ++i) {
                    Modifier* mod = dobj->GetModifier(i);
                    if (!mod) continue;
                    Class_ID mid = mod->ClassID();
                    if (mid == Class_ID(EDITTRIOBJ_CLASS_ID, 0)) { hasConverter = true; break; }
                    // Turn-to-Mesh / Turn-to-Poly modifier IDs are Max-version-specific;
                    // sniff by class name.
                    MSTR mn; mod->GetClassName(mn);
                    const wchar_t* ms = mn.data();
                    if (ms && (wcsstr(ms, L"Turn to Mesh") != nullptr ||
                               wcsstr(ms, L"Turn_to_Mesh") != nullptr)) {
                        hasConverter = true; break;
                    }
                }
                if (hasConverter) break;
                obj = dobj->GetObjRef();
            }

            if (!hasConverter) {
                Problem p;
                p.type = ProblemType::EditablePoly;
                p.node = n;
                p.displayName = std::wstring(n->GetName()) + L"  [Editable Poly]";
                out.push_back(p);
                return;
            }
        }

        // 3) Empty mesh check (run last, only if not already flagged)
        // We use the ConvertToType API to get a TriObject — this avoids the
        // GetRenderMesh dependency on a View, which has a Max-version-specific
        // signature (ViewExp vs View&).
        ObjectState os = n->EvalWorldState(0);
        if (os.obj && os.obj->SuperClassID() == GEOMOBJECT_CLASS_ID) {
            int nf = -1;
            // ConvertToType(triObjectClassID) gives us a TriObject from any
            // GeomObject. The returned pointer might be the same as os.obj
            // (no conversion needed) or a temporary that we must delete.
            if (os.obj->CanConvertToType(triObjectClassID)) {
                TriObject* tri = static_cast<TriObject*>(
                    os.obj->ConvertToType(0, triObjectClassID));
                if (tri) {
                    nf = tri->GetMesh().getNumFaces();
                    if (tri != os.obj) tri->DeleteMe();
                }
            }
            if (nf == 0) {
                Problem p;
                p.type = ProblemType::EmptyMesh;
                p.node = n;
                p.displayName = std::wstring(n->GetName()) + L"  [Empty Mesh]";
                out.push_back(p);
            }
        }
    });
}

void collectBoneControllerProblems(INode* root, std::vector<Problem>& out) {
    forEachNodeRec(root, [&](INode* n) {
        if (n == root) return;
        if (!isBoneLikeNode(n)) return;
        if (hasValidPRS(n))     return;

        Problem p;
        p.type = ProblemType::InvalidController;
        p.node = n;
        p.displayName = std::wstring(n->GetName()) + L"  [Invalid Controller]";
        out.push_back(p);
    });
}

void collectMaterialProblems(std::vector<Problem>& out) {
    // Walk the scene material library — every unique Mtl* used by some node.
    Interface* gi = GetCOREInterface();
    if (!gi) return;

    std::unordered_set<Mtl*> seen;
    INode* root = gi->GetRootNode();
    if (!root) return;

    forEachNodeRec(root, [&](INode* n) {
        if (n == root) return;
        Mtl* m = n->GetMtl();
        if (!m) return;

        std::function<void(Mtl*)> walk = [&](Mtl* mm) {
            if (!mm) return;
            if (!seen.insert(mm).second) return;
            // Recurse into ANY wrapper material that has sub-materials
            // (Multi/Sub, Composite, Blend, Shell, etc.) — check children
            // individually instead of flagging the wrapper itself.
            if (mm->NumSubMtls() > 0) {
                for (int i = 0; i < mm->NumSubMtls(); ++i) walk(mm->GetSubMtl(i));
                return;
            }
            if (isMaterialUnsupported(mm)) {
                Problem p;
                p.type = ProblemType::UnsupportedMaterial;
                p.mtl  = mm;
                p.displayName = std::wstring(mm->GetName().data())
                              + L"  ["
                              + std::wstring([&]{
                                    MSTR cn; mm->GetClassName(cn);
                                    return cn.data() ? std::wstring(cn.data()) : std::wstring(L"?");
                                }())
                              + L"]";
                out.push_back(p);
            }
        };
        walk(m);
    });
}

} // anonymous namespace

ScanResult scanScene() {
    ScanResult result;
    Interface* gi = GetCOREInterface();
    if (!gi) return result;
    INode* root = gi->GetRootNode();
    if (!root) return result;

    collectDuplicates(root, result.problems);
    collectMeshProblems(root, result.problems);
    collectBoneControllerProblems(root, result.problems);
    collectMaterialProblems(result.problems);

    return result;
}

} // namespace scene_monitor
