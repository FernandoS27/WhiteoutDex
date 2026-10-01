// MDLXImporter — MdlxImporterPlugin implementation
#include "mdlx_importer_plugin.h"
#include <wdx_text.h>
#include "mdlx_import_options.h"
#include "mdlx_class_ids.h"
#include "import_dialog.h"

// Disassembly (MDX → IR)
#include "disassembly/mdx_model_disassembler.h"

// Scene builders (IR → Wc3 scripted plugin nodes)
#include "scene/wc3_material_builder.h"
#include "scene/wc3_scene_builders.h"
#include "scene/texture_resolver.h"

// Core (MaxCore)
#include <core/intermediate_types.h>
#include <util/error_reporter.h>
#include <util/class_ids.h>
#include <optimization/vertex_optimizer.h>
#include <optimization/bone_optimizer.h>
#include <optimization/keyframe_optimizer.h>

// WhiteoutLib
#include <whiteout/models/mdx/parser.h>

// Max SDK
#include <MaxDirectories.h>
#include <triobj.h>
#include <MeshNormalSpec.h>
#include <modstack.h>
#include <iskin.h>
#include <istdplug.h>
#include <stdmat.h>
#include <ilayer.h>
#include <ilayermanager.h>
#include <maxscript/maxscript.h>
#include <wdx_localization.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <optional>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <fstream>

// Narrow-string helper for Max's wchar_t names
static std::string narrow(const MCHAR* ws) {
    if (!ws) return "";
    std::string s;
    while (*ws) { s += static_cast<char>(*ws > 127 ? '?' : *ws); ++ws; }
    return s;
}

static std::ofstream& importLog() {
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
#define ILOG importLog()

// ── Bone Debug Dump ─────────────────────────────────────────
//
// Dumps bone state to the import log so we can diagnose skinning
// explosions.  Compares NodeTM vs ObjectTM (a mismatch reveals stretchTM
// or ObjectOffset contamination — the #1 cause of mid-frame explosions
// when using BoneGeometry).
//
// label: short string shown before the dump ("after-create",
//        "after-skin", "after-animation", ...)
// sampleFrames: additional frames to sample (in addition to t=0).
static void dumpBoneState(
    const std::vector<INode*>& boneNodes,
    const char* label,
    const std::vector<TimeValue>& sampleFrames = {})
{
    ILOG << "\n==== BONE DEBUG DUMP [" << label << "] ====\n";
    int boneIdx = 0;
    for (INode* node : boneNodes) {
        if (!node) { ++boneIdx; continue; }

        // Narrow name for logging
        std::string nm;
        const MCHAR* wname = node->GetName();
        while (wname && *wname) {
            nm += static_cast<char>(*wname > 127 ? '?' : *wname);
            ++wname;
        }

        // Bone-specific flags
        BOOL isBone   = node->GetBoneNodeOnOff();
        BOOL autoAln  = node->GetBoneAutoAlign();
        BOOL freezeLn = node->GetBoneFreezeLen();

        ILOG << "  bone[" << boneIdx << "] '" << nm << "'"
             << " isBone=" << isBone
             << " autoAlign=" << autoAln
             << " freezeLen=" << freezeLn << "\n";

        // stretchTM: non-identity ⇒ bone has accumulated stretch
        Matrix3 stretchTM = node->GetStretchTM(0);
        {
            Point3 r0 = stretchTM.GetRow(0);
            Point3 r1 = stretchTM.GetRow(1);
            Point3 r2 = stretchTM.GetRow(2);
            Point3 tr = stretchTM.GetTrans();
            bool isId = (std::abs(r0.x - 1.0f) < 1e-5f && std::abs(r0.y) < 1e-5f && std::abs(r0.z) < 1e-5f &&
                         std::abs(r1.x) < 1e-5f && std::abs(r1.y - 1.0f) < 1e-5f && std::abs(r1.z) < 1e-5f &&
                         std::abs(r2.x) < 1e-5f && std::abs(r2.y) < 1e-5f && std::abs(r2.z - 1.0f) < 1e-5f &&
                         std::abs(tr.x) < 1e-5f && std::abs(tr.y) < 1e-5f && std::abs(tr.z) < 1e-5f);
            ILOG << "    stretchTM=" << (isId ? "IDENTITY" : "NON-IDENTITY!");
            if (!isId) {
                ILOG << "  r0=(" << r0.x << "," << r0.y << "," << r0.z << ")"
                     << " r1=(" << r1.x << "," << r1.y << "," << r1.z << ")"
                     << " r2=(" << r2.x << "," << r2.y << "," << r2.z << ")"
                     << " t=(" << tr.x << "," << tr.y << "," << tr.z << ")";
            }
            ILOG << "\n";
        }

        // Sample NodeTM vs ObjectTM at t=0 and at each sample frame
        std::vector<TimeValue> frames = {0};
        for (auto t : sampleFrames) frames.push_back(t);

        Point3 prevNodePos(0, 0, 0);
        bool havePrev = false;
        for (TimeValue t : frames) {
            Matrix3 nodeTM = node->GetNodeTM(t);
            Matrix3 objTM  = node->GetObjectTM(t);
            Point3 nPos = nodeTM.GetTrans();
            Point3 oPos = objTM.GetTrans();
            Point3 delta = oPos - nPos;
            bool mismatch = (delta.LengthSquared() > 1e-6f);

            ILOG << "    t=" << t
                 << " nodeTM.pos=(" << nPos.x << "," << nPos.y << "," << nPos.z << ")"
                 << " objTM.pos=(" << oPos.x << "," << oPos.y << "," << oPos.z << ")";
            if (mismatch) {
                ILOG << " *** MISMATCH delta=(" << delta.x << "," << delta.y << "," << delta.z << ")";
            }
            // Jump detection: compare to previous sampled frame
            if (havePrev) {
                Point3 jump = nPos - prevNodePos;
                float jumpLen = jump.Length();
                if (jumpLen > 1000.0f) {
                    ILOG << " *** JUMP jumpLen=" << jumpLen;
                }
            }
            ILOG << "\n";
            prevNodePos = nPos;
            havePrev = true;
        }
        ++boneIdx;
    }
    ILOG << "==== end BONE DEBUG DUMP ====\n";
    ILOG.flush();
}

// ── Layer Organization ──────────────────────────────────────
//
// Traverses the scene graph and places each imported node into a
// layer based on its Class_ID.  Mirrors the categories used by the
// WdxNodeManager UI so users see the same grouping in the layer
// manager and in the node manager.
//
// Layer names (matching NodeManager.ms labels):
//   Geometry, Bones, Helpers / Dummies, Attachments, Events, Lights,
//   Cameras, Particle Emitters 1, Particle Emitters 2,
//   Ribbon Emitters, Popcorn FX, FaceFX, Collision Shapes
//
// Notes:
//   - Cameras are also placed into "Cameras" by Wc3CameraBuilder;
//     re-adding is idempotent (AddToLayer no-ops if already present).
//   - Root node is skipped.
//   - Unclassified nodes are left on whatever layer Max assigned
//     (usually the default "0" layer).
//
static void organizeNodesIntoLayers(Interface* gi) {
    ILayerManager* lm = GetCOREInterface13()->GetLayerManager();
    if (!lm) {
        ILOG << "[layers] no layer manager available — skipping\n";
        return;
    }

    // Layer name resolver — creates the layer lazily on first use
    // and caches it so we don't hammer GetLayer/CreateLayer per node.
    std::map<std::wstring, ILayer*> layerCache;
    auto getOrCreateLayer = [&](const TCHAR* name) -> ILayer* {
        std::wstring key(name);
        auto it = layerCache.find(key);
        if (it != layerCache.end()) return it->second;
        MSTR layerName(name);
        ILayer* layer = lm->GetLayer(layerName);
        if (!layer) layer = lm->CreateLayer(layerName);
        layerCache[key] = layer;
        return layer;
    };

    // Classify one node — returns layer name or nullptr if unclassified.
    // For geometry (meshes), the Wc3GeosetLod UserProp (written by
    // createMeshNode) is consulted to route LOD>0 geosets into their own
    // layers, matching NeoDex convention:
    //   LOD 0      → "Geosets"        (visible)
    //   LOD N > 0  → "Geosets_LOD<N>" (hidden)
    // Non-mesh nodes still go to their respective type-specific layers.
    auto classifyNode = [](INode* n, std::wstring& outLayerName) -> bool {
        outLayerName.clear();
        if (!n || n->IsRootNode()) return false;
        Object* obj = n->GetObjectRef();
        if (!obj) return false;
        // Walk down derived-object / WSM stack to the base object
        while (obj && (obj->SuperClassID() == GEN_DERIVOB_CLASS_ID ||
                       obj->SuperClassID() == WSM_DERIVOB_CLASS_ID)) {
            IDerivedObject* dobj = static_cast<IDerivedObject*>(obj);
            obj = dobj->GetObjRef();
        }
        if (!obj) return false;

        Class_ID  cid  = obj->ClassID();
        SClass_ID scid = obj->SuperClassID();

        // Wc3-specific types first (most specific)
        if (cid == mdx_ids::WC3_ATTACH_POINT)   { outLayerName = L"Attachments"; return true; }
        if (cid == mdx_ids::WC3_LIGHT)          { outLayerName = L"Lights"; return true; }
        if (cid == mdx_ids::WC3_EVENT_V2021 ||
            cid == mdx_ids::WC3_EVENT_V2020)    { outLayerName = L"Events"; return true; }
        if (cid == mdx_ids::WC3_COLLISION_SPH ||
            cid == mdx_ids::WC3_COLLISION_BOX)  { outLayerName = L"Collision Shapes"; return true; }
        if (cid == mdx_ids::WC3_PARTICLES1)     { outLayerName = L"Particle Emitters 1"; return true; }
        if (cid == mdx_ids::WC3_PARTICLES2)     { outLayerName = L"Particle Emitters 2"; return true; }
        if (cid == mdx_ids::WC3_RIBBON)         { outLayerName = L"Ribbon Emitters"; return true; }
        if (cid == mdx_ids::BLIZZ_POPCORN)      { outLayerName = L"Popcorn FX"; return true; }
        if (cid == mdx_ids::BLIZZ_FACEFX)       { outLayerName = L"FaceFX"; return true; }

        // Bones before generic geometry (BoneGeometry's superclass is GEOMOBJECT)
        if (cid == BONE_OBJ_CLASSID) { outLayerName = L"Bones"; return true; }

        // Cameras
        if (scid == CAMERA_CLASS_ID) { outLayerName = L"Cameras"; return true; }

        // Camera / light targets — must be checked BEFORE generic GEOMOBJECT_CLASS_ID
        if (n->IsTarget()) { outLayerName = L"Cameras"; return true; }

        // Dummies / Helpers — only reach here if not one of the Wc3 helper types above
        if (cid == Class_ID(DUMMY_CLASS_ID, 0)) { outLayerName = L"Helpers / Dummies"; return true; }
        if (scid == HELPER_CLASS_ID)            { outLayerName = L"Helpers / Dummies"; return true; }

        // Generic geometry (meshes) — catch-all last
        if (scid == GEOMOBJECT_CLASS_ID) {
            int lodLevel = 0;
            n->GetUserPropInt(_T("Wc3GeosetLod"), lodLevel);
            if (lodLevel > 0) {
                outLayerName = L"Geosets_LOD" + std::to_wstring(lodLevel);
            } else {
                outLayerName = L"Geosets";
            }
            return true;
        }

        return false;
    };

    // Recursive traversal. Uses std::function to allow the lambda to
    // reference itself.
    std::function<void(INode*)> walk = [&](INode* n) {
        if (!n) return;
        std::wstring layerName;
        if (classifyNode(n, layerName)) {
            if (ILayer* layer = getOrCreateLayer(layerName.c_str()))
                layer->AddToLayer(n);
        }
        for (int i = 0; i < n->NumberOfChildren(); ++i)
            walk(n->GetChildNode(i));
    };

    INode* root = gi->GetRootNode();
    if (root) {
        for (int i = 0; i < root->NumberOfChildren(); ++i)
            walk(root->GetChildNode(i));
    }

    ILOG << "[layers] organized into " << layerCache.size() << " layer(s)";
    for (auto& kv : layerCache) {
        ILOG << "  [" << narrow(kv.first.c_str()) << "]";
    }
    ILOG << "\n";

    // Hide all "Geosets_LOD<N>" layers (N > 0), matching NeoDex convention
    // (NeoDexSceneRebuilder.ms fn recreateGeosets lines 2072-2073).
    // The main "Geosets" layer stays visible.
    const std::wstring lodPrefix = L"Geosets_LOD";
    for (auto& kv : layerCache) {
        if (kv.first.rfind(lodPrefix, 0) == 0 && kv.second) {
            kv.second->Hide(TRUE);
            ILOG << "[layers] hiding " << narrow(kv.first.c_str()) << " (LOD>0)\n";
        }
    }

    ILOG.flush();
}

// Forward declarations for INI loading
void loadImportOptionsFromINI(Interface* gi, MdlxImportOptions& opts);
void applyFastPreset(MdlxImportOptions& opts);

// Module handle from dllmain.cpp
extern HINSTANCE hInstance;

int MdlxImporterPlugin::ExtCount() { return 2; }

const TCHAR* MdlxImporterPlugin::Ext(int n) {
    switch (n) {
    case 0: return _T("mdx");
    case 1: return _T("mdl");
    default: return _T("");
    }
}

const TCHAR* MdlxImporterPlugin::LongDesc() { return _T("Warcraft III MDX/MDL Model"); }
const TCHAR* MdlxImporterPlugin::ShortDesc() { return _T("MDX/MDL Import"); }
const TCHAR* MdlxImporterPlugin::AuthorName() { return _T("WhiteoutDex"); }
const TCHAR* MdlxImporterPlugin::CopyrightMessage() { return _T(""); }
const TCHAR* MdlxImporterPlugin::OtherMessage1() { return _T(""); }
const TCHAR* MdlxImporterPlugin::OtherMessage2() { return _T(""); }
unsigned int MdlxImporterPlugin::Version() { return 100; }
void MdlxImporterPlugin::ShowAbout(HWND /*hWnd*/) {}

namespace {

std::string wcharToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

// ── Bone/Helper creation ────────────────────────────────────

// Real MDX bones (isHelper=false)        → Max BoneGeometry (GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID).
//   Matches MaxScript `bone pos:pivot` — a proper deformable bone visible as a
//   diamond in the viewport, accepted by Skin, picked up by FBX/glTF as a Bone.
// MDX helpers (isHelper=true) with       → Point helper (non-deforming transform node:
// importHelpersAsPointHelpers              fake bones, attachment sockets, model root).
//
// Note: BoneGeometry draws its length along local X toward the child's pivot,
// but that's purely visual — the node's TM controller is untouched, so
// SetNodeTM works exactly as it did with Point helpers.
INode* createBoneNode(const ir::Bone& bone, Interface* gi, bool asPointHelper) {
    Object* obj = nullptr;

    if (asPointHelper) {
        obj = static_cast<Object*>(
            gi->CreateInstance(HELPER_CLASS_ID, core_ids::POINT_HELPER_ID));
    } else {
        obj = static_cast<Object*>(
            gi->CreateInstance(GEOMOBJECT_CLASS_ID, BONE_OBJ_CLASSID));
    }

    if (!obj) return nullptr;

    INode* node = gi->CreateObjectNode(obj);
    if (!node) return nullptr;

    MSTR name;
    name = wdx::text::mdxToWide(bone.name).c_str();
    node->SetName(name);

    // Point-helper styling — match NeoDex's appearance:
    //   p = point name:obj.name pos:obj.pivot box:true size:pSize
    //   p.wirecolor = green
    //
    // Without this our point helpers default to a tiny axis tripod which
    // is hard to see at typical model scale. NeoDex uses box-style helpers
    // sized 20 with green wirecolor; replicating that here makes attachment
    // points / fake bones / model roots immediately visible and visually
    // distinguishable from real bones.
    //
    // Implementation: the PointHelpObject paramblock layout is not part of
    // the public SDK so we drive it through MaxScript instead — that's
    // exactly what NeoDex does and the API surface is documented.
    if (asPointHelper) {
        // Wirecolor we can set directly via the C++ API.
        node->SetWireColor(RGB(0, 255, 0));

        // Display flags via MaxScript. Resolve the node by HANDLE, not by
        // name — node names are not unique (community models routinely have
        // several nodes named "1"), and getNodeByName returns the first
        // match, silently configuring the wrong node.
        std::wstringstream ss;
        ss << L"(local n = maxOps.getNodeByHandle " << node->GetHandle() << L";"
           << L"if n != undefined and (classOf n) == Point do ("
           << L"n.size = 20;"
           << L"n.box = true;"
           << L"n.cross = false;"
           << L"n.axistripod = false;"
           << L"n.centermarker = false))";
        std::wstring scriptStr = ss.str();
        ExecuteMAXScriptScript(
            const_cast<wchar_t*>(scriptStr.c_str()),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            MAXScript::ScriptSource::NonEmbedded,
#endif
            TRUE, nullptr);
    }

    // Flag BoneGeometry nodes as bones so Max's tools & exporters treat them
    // correctly (equivalent to MaxScript's `.boneEnable = true`).
    if (!asPointHelper) {
        node->ShowBone(1);              // display as bone shape (line + triangle)
        node->SetBoneNodeOnOff(TRUE, 0);

        // CRITICAL: Disable every bone behavior that modifies the effective TM.
        //
        // Why: MDX animations are authoritative per-frame TM keys.  Max's
        // default BoneGeometry behavior silently adds rotation & stretch via a
        // hidden stretchTM that leaks into ObjectTM (and Skin's AddBoneEx uses
        // ObjectTM to snapshot the bind pose).
        //
        //   Auto-Align = OFF     → parent doesn't rotate to face child
        //   FreezeLen  = OFF     → bone length isn't locked
        //   ScaleType  = NONE    → NO STRETCH AT ALL (the critical one).
        //                          Without this, even with AutoAlign off, child
        //                          animation keys can introduce stretch scale
        //                          up to 55000× on specific bones where the
        //                          axis math degenerates (fengmo04 in test data).
        //
        // NOTE: Do NOT call ResetBoneStretch() — despite its name it does
        // NOT reset stretch to identity.  From SDK docs: "Specifies the time
        // at which to calculate and store INITIAL child position" — i.e. it
        // freezes the current (possibly already-stretched) state as the new
        // baseline.  That's why earlier attempts actually made things worse.
        node->SetBoneAutoAlign(FALSE);
        node->SetBoneFreezeLen(FALSE);
        node->SetBoneScaleType(BONE_SCALETYPE_NONE);
    }

    return node;
}

// ── Mesh creation ───────────────────────────────────────────

INode* createMeshNode(const ir::Mesh& irMesh, Interface* gi) {
    TriObject* triObj = CreateNewTriObject();
    if (!triObj) return nullptr;

    Mesh& mesh = triObj->GetMesh();

    int vertCount = static_cast<int>(irMesh.vertices.size());
    if (vertCount == 0) {
        triObj->DeleteMe();
        return nullptr;
    }

    // Truncate to whole triangles
    int faceCount = static_cast<int>(irMesh.indices.size() / 3);

    mesh.setNumVerts(vertCount);
    mesh.setNumFaces(faceCount);

    // Vertices
    for (int i = 0; i < vertCount; ++i) {
        mesh.setVert(i, irMesh.vertices[i].position);
    }

    // Faces — validate each index is in range
    int validFaces = 0;
    for (int f = 0; f < faceCount; ++f) {
        int base = f * 3;
        uint32_t i0 = irMesh.indices[base];
        uint32_t i1 = irMesh.indices[base + 1];
        uint32_t i2 = irMesh.indices[base + 2];
        if (i0 >= static_cast<uint32_t>(vertCount) ||
            i1 >= static_cast<uint32_t>(vertCount) ||
            i2 >= static_cast<uint32_t>(vertCount))
            continue;  // skip degenerate face

        mesh.faces[validFaces].setVerts(
            static_cast<int>(i0), static_cast<int>(i1), static_cast<int>(i2));
        mesh.faces[validFaces].setEdgeVisFlags(1, 1, 1);
        mesh.faces[validFaces].setSmGroup(1);
        if (irMesh.materialIndex >= 0)
            mesh.faces[validFaces].setMatID(0);
        ++validFaces;
    }
    if (validFaces < faceCount)
        mesh.setNumFaces(validFaces);
    faceCount = validFaces;

    if (faceCount == 0) {
        triObj->DeleteMe();
        return nullptr;
    }

    // UV mapping (channel 1)
    if (irMesh.vertices[0].uvSetCount > 0) {
        mesh.setMapSupport(1, TRUE);
        mesh.setNumMapVerts(1, vertCount);
        mesh.setNumMapFaces(1, faceCount);

        for (int i = 0; i < vertCount; ++i) {
            const auto& uv = irMesh.vertices[i].uvSets[0];
            mesh.setMapVert(1, i, Point3(uv.x, 1.0f - uv.y, 0.0f));
        }

        TVFace* mapFaces1 = mesh.mapFaces(1);
        if (mapFaces1) {
            // Rebuild face list with only the validated faces
            int mf = 0;
            for (int f = 0; f < static_cast<int>(irMesh.indices.size() / 3); ++f) {
                int base = f * 3;
                uint32_t i0 = irMesh.indices[base];
                uint32_t i1 = irMesh.indices[base + 1];
                uint32_t i2 = irMesh.indices[base + 2];
                if (i0 >= static_cast<uint32_t>(vertCount) ||
                    i1 >= static_cast<uint32_t>(vertCount) ||
                    i2 >= static_cast<uint32_t>(vertCount))
                    continue;
                mapFaces1[mf].setTVerts(
                    static_cast<int>(i0), static_cast<int>(i1), static_cast<int>(i2));
                ++mf;
            }
        }
    }

    // Additional UV sets (channels 2-4)
    for (int uvSet = 1; uvSet < irMesh.vertices[0].uvSetCount && uvSet < 4; ++uvSet) {
        int channel = uvSet + 1;
        mesh.setMapSupport(channel, TRUE);
        mesh.setNumMapVerts(channel, vertCount);
        mesh.setNumMapFaces(channel, faceCount);

        for (int i = 0; i < vertCount; ++i) {
            const auto& uv = irMesh.vertices[i].uvSets[uvSet];
            mesh.setMapVert(channel, i, Point3(uv.x, 1.0f - uv.y, 0.0f));
        }

        TVFace* mapFacesCh = mesh.mapFaces(channel);
        if (mapFacesCh) {
            int mf = 0;
            for (int f = 0; f < static_cast<int>(irMesh.indices.size() / 3); ++f) {
                int base = f * 3;
                uint32_t i0 = irMesh.indices[base];
                uint32_t i1 = irMesh.indices[base + 1];
                uint32_t i2 = irMesh.indices[base + 2];
                if (i0 >= static_cast<uint32_t>(vertCount) ||
                    i1 >= static_cast<uint32_t>(vertCount) ||
                    i2 >= static_cast<uint32_t>(vertCount))
                    continue;
                mapFacesCh[mf].setTVerts(
                    static_cast<int>(i0), static_cast<int>(i1), static_cast<int>(i2));
                ++mf;
            }
        }
    }

    // Per-vertex normals — write the MDX file's authoritative normals directly
    // into the mesh's MeshNormalSpec so Max doesn't recompute them from the
    // face geometry + a single smoothing group. Without this step, models
    // like Arthas show wrong shading on rounded surfaces (nose, chin, etc.):
    // SmGroup=1 makes everything one giant smooth surface, but with welded
    // hard-edge vertices (Arthas's mesh has a single vertex shared between
    // smooth-shaded and hard-shaded regions) Max ends up averaging across
    // semantic boundaries and the lighting collapses to nonsense.
    //
    // MDX stores ONE normal per vertex (no per-corner normals), so we use
    // each face's three vertex indices to look up the corresponding normal.
    // Using SetAllExplicit(true) tells Max these normals are authoritative
    // and must NOT be recomputed when smoothing groups change.
    bool haveNormals = false;
    for (int i = 0; i < vertCount; ++i) {
        const Point3& nv = irMesh.vertices[i].normal;
        if (nv.LengthSquared() > 1e-6f) { haveNormals = true; break; }
    }
    if (haveNormals) {
        mesh.SpecifyNormals();
        MeshNormalSpec* specNorms = mesh.GetSpecifiedNormals();
        if (specNorms) {
            specNorms->ClearAndFree();
            specNorms->SetParent(&mesh);
            specNorms->SetNumNormals(vertCount);
            specNorms->SetNumFaces(faceCount);

            // Populate the normal pool — one per IR vertex.
            for (int i = 0; i < vertCount; ++i) {
                Point3 n = irMesh.vertices[i].normal;
                float L = n.Length();
                if (L > 1e-6f) n /= L; else n = Point3(0.0f, 0.0f, 1.0f);
                specNorms->Normal(i) = n;
            }
            specNorms->SetAllExplicit(true);

            // Attach normal IDs to each face corner. faceCount has been
            // truncated above to validFaces, so the face indices remain
            // in lockstep with the original triangulation we walked.
            int mf = 0;
            for (int f = 0; f < static_cast<int>(irMesh.indices.size() / 3); ++f) {
                int base = f * 3;
                uint32_t i0 = irMesh.indices[base];
                uint32_t i1 = irMesh.indices[base + 1];
                uint32_t i2 = irMesh.indices[base + 2];
                if (i0 >= static_cast<uint32_t>(vertCount) ||
                    i1 >= static_cast<uint32_t>(vertCount) ||
                    i2 >= static_cast<uint32_t>(vertCount))
                    continue;
                if (mf >= faceCount) break;
                MeshNormalFace& nf = specNorms->Face(mf);
                nf.SpecifyAll(true);
                nf.SetNormalID(0, static_cast<int>(i0));
                nf.SetNormalID(1, static_cast<int>(i1));
                nf.SetNormalID(2, static_cast<int>(i2));
                ++mf;
            }
            specNorms->CheckNormals();
        }
    }

    // Build smoothing-group derived normals as a fallback for any face where
    // we didn't specify one (none expected, but the API requires this call).
    mesh.buildNormals();
    mesh.InvalidateGeomCache();
    mesh.InvalidateTopologyCache();

    INode* node = gi->CreateObjectNode(triObj);

    // Mesh naming + LOD handling. The disassembler encodes LOD level as a
    // "#LOD<n>" suffix on irMesh.name (or no suffix for LOD 0 / v800). We:
    //   1. Strip the suffix for the visible node name
    //   2. Store the LOD level on a UserProp so the layer classifier can
    //      sort meshes into "Geosets" / "Geosets_LOD1" / "Geosets_LOD2" ...
    //   3. Store the original lodName (everything before the trailing "_<id>")
    //      so it's available for future Wc3LodName queries / round-trip export.
    // Matches NeoDex convention (NeoDexSceneRebuilder.ms fn recreateGeosets).
    std::string fullName = irMesh.name;
    int lodLevel = 0;
    size_t lodPos = fullName.rfind("#LOD");
    if (lodPos != std::string::npos) {
        try {
            lodLevel = std::stoi(fullName.substr(lodPos + 4));
        } catch (...) { lodLevel = 0; }
        fullName = fullName.substr(0, lodPos);
    }

    MSTR name;
    name = wdx::text::mdxToWide(fullName).c_str();
    node->SetName(name);

    node->SetUserPropInt(_T("Wc3GeosetLod"), lodLevel);

    // Recover the lodName (NeoDex-compatible) by stripping the trailing "_<id>".
    // The disassembler wrote either "<lodName>_<id>" or "Geoset_<id>".
    // If the prefix is literally "Geoset" it's a v800 fallback — no lodName
    // to preserve.
    size_t lastUnderscore = fullName.rfind('_');
    if (lastUnderscore != std::string::npos) {
        std::string prefix = fullName.substr(0, lastUnderscore);
        if (prefix != "Geoset" && !prefix.empty()) {
            MSTR lodNameStr;
            lodNameStr = wdx::text::mdxToWide(prefix).c_str();
            node->SetUserPropString(_T("Wc3LodName"), lodNameStr);
        }
    }

    // Only written when set, so plain meshes keep a clean User Defined box.
    if (irMesh.selectionGroup != 0)
        node->SetUserPropInt(_T("Wc3SelectionGroup"), static_cast<int>(irMesh.selectionGroup));
    // Object Settings' "Unselectable" checkbox reads and writes this (0 / 1).
    if (irMesh.unselectable)
        node->SetUserPropInt(_T("Unselectable"), 1);

    return node;
}

// ── Skin modifier application ───────────────────────────────

// Max's Skin modifier ships with "Bone Affect Limit" (bone_Limit) = 5 and
// silently discards every influence past it — AddWeights accepts an 8-bone
// list and GetNumAssignedBones later reports 5. Classic MDX matrix groups do
// reach 8 bones (Undead3D_Exp, the kroxigor family), so raise the limit to
// whatever this mesh actually needs before any weights go in, or those bones
// are gone before the exporter ever sees them.
static void raiseSkinBoneLimit(Modifier* skinMod, int needed) {
    if (!skinMod || needed <= 0) return;

    for (int i = 0; i < skinMod->NumParamBlocks(); ++i) {
        // Through Animatable: the 2016 SDK's BaseObject::GetParamBlock()
        // (no argument, IParamArray*) hides the indexed overload.
        IParamBlock2* pb = static_cast<Animatable*>(skinMod)->GetParamBlock(i);
        if (!pb) continue;
        ParamBlockDesc2* desc = pb->GetDesc();
        if (!desc) continue;

        for (int p = 0; p < desc->Count(); ++p) {
            const ParamDef& pd = desc->paramdefs[p];
            if (!pd.int_name || _tcsicmp(pd.int_name, _T("bone_Limit")) != 0)
                continue;

            int current = 0;
            Interval valid = FOREVER;
            pb->GetValue(pd.ID, 0, current, valid);
            // 0 means "no limit" in Max — leave that alone.
            if (current > 0 && current < needed)
                pb->SetValue(pd.ID, 0, needed);
            return;
        }
    }
}

bool applySkinModifier(INode* meshNode, const ir::Mesh& irMesh,
                       const std::vector<INode*>& boneNodes, Interface* gi)
{
    // Check if any vertex has skin influences
    bool hasSkin = false;
    for (const auto& v : irMesh.vertices) {
        if (!v.skinInfluences.empty()) { hasSkin = true; break; }
    }
    if (!hasSkin) return false;

    // Create Skin modifier
    Modifier* skinMod = static_cast<Modifier*>(
        gi->CreateInstance(OSM_CLASS_ID, core_ids::SKIN_CLASS_ID));
    if (!skinMod) return false;

    // Get IDerivedObject
    IDerivedObject* derivObj = nullptr;
    Object* obj = meshNode->GetObjectRef();
    if (obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
        derivObj = static_cast<IDerivedObject*>(obj);
    } else {
        derivObj = CreateDerivedObject(obj);
        meshNode->SetObjectRef(derivObj);
    }
    derivObj->AddModifier(skinMod);

    // Need to evaluate the pipeline to initialize ISkin
    gi->ForceCompleteRedraw(FALSE);
    meshNode->EvalWorldState(0);

    // Get ISkinImportData
    ISkinImportData* skinImport = static_cast<ISkinImportData*>(
        skinMod->GetInterface(I_SKINIMPORTDATA));
    if (!skinImport) return false;

    // Widen the bone-affect limit before any AddWeights call, so wide matrix
    // groups survive the import.
    {
        size_t maxInfluences = 0;
        for (const auto& v : irMesh.vertices)
            maxInfluences = std::max(maxInfluences, v.skinInfluences.size());
        raiseSkinBoneLimit(skinMod, static_cast<int>(maxInfluences));
    }

    // Collect all unique bones used
    std::set<int32_t> usedBones;
    for (const auto& v : irMesh.vertices)
        for (const auto& inf : v.skinInfluences)
            if (inf.boneIndex >= 0 && inf.boneIndex < static_cast<int32_t>(boneNodes.size()))
                usedBones.insert(inf.boneIndex);

    // Add bones to skin
    for (int32_t bIdx : usedBones) {
        if (boneNodes[bIdx])
            skinImport->AddBoneEx(boneNodes[bIdx], TRUE);
    }

    // Assign weights per vertex
    int vertCount = static_cast<int>(irMesh.vertices.size());
    for (int v = 0; v < vertCount; ++v) {
        const auto& influences = irMesh.vertices[v].skinInfluences;
        if (influences.empty()) continue;

        Tab<INode*> bones;
        Tab<float> weights;
        bones.SetCount(static_cast<int>(influences.size()));
        weights.SetCount(static_cast<int>(influences.size()));

        for (int j = 0; j < static_cast<int>(influences.size()); ++j) {
            int32_t bIdx = influences[j].boneIndex;
            bones[j] = (bIdx >= 0 && bIdx < static_cast<int32_t>(boneNodes.size()))
                ? boneNodes[bIdx] : nullptr;
            weights[j] = influences[j].weight;
        }

        skinImport->AddWeights(meshNode, v, bones, weights);
    }

    return true;
}

// ── Animation key insertion ─────────────────────────────────
// Matches WhiteoutDexSceneRebuilder.ms animation import logic.

// Tangents of key i of an MDX Bezier or Hermite track on a Max bezier key
// (float, Point3 or scale).
//
// The MDX Bezier segment from key a to key b is the cubic with control points
// a.value, a.outTangent, b.inTangent, b.value, evenly spaced in time (what
// the game and mdx-m3-viewer interpolate). A Hermite segment is the same
// cubic with control points a.value + a.outTangent/3 and b.value -
// b.inTangent/3: its tangents are derivatives. A Max bezier key stores each
// tangent as a slope in value per tick, and its handle reaches the given
// fraction of the way to the neighbouring key (istdplug.h, IBezPoint3Key;
// MAXScript Help, "Bezier Controller Keys"). Both handles sit at
// value + tangent * length * dt, the in handle too (measured in Max 2027: a
// curve sampled per frame fits these control points to 1e-8). With handles a
// third long, the slopes below put them exactly on those control points. The
// in and out handles are unlocked (BEZKEY_*BROKEN): an MDX key's two
// tangents differ.
//
// Keys without tangents are the sequence boundary keys the importer adds;
// the game holds the value there, so they and the sides facing them are flat.
// "Flat" is a custom tangent of 0: BEZKEY_FLAT (6) is what Max calls Auto
// (MAXScript Help, "Bezier Controller Keys": #smooth #linear #step #fast #slow
// #custom #auto), a tangent computed from the neighbouring keys. After a key
// the curve arrives at from another value it keeps that slope - over a long
// sequence the Teen Abomination's root sank 570 units in "Decay Bone", where
// the file holds it still.
// Some old MDX files hold NaN keys (a rotation of nan,nan,nan,nan). Max's
// controllers do not agree on them: Max 2016 drops most, Max 2027 carries the
// NaN on into more channels and exports more of them than the file had.
// A key without a finite value is dropped before any controller sees it.
static bool finiteValue(float v) { return std::isfinite(v); }
static bool finiteValue(int32_t) { return true; }
static bool finiteValue(const Point3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
static bool finiteValue(const Point4& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.w); }
static bool finiteValue(const Quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }
static bool finiteValue(const Color& c) { return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b); }

template <class T>
static size_t dropNonFiniteKeys(ir::Track<T>& track)
{
    const size_t before = track.keys.size();
    track.keys.erase(std::remove_if(track.keys.begin(), track.keys.end(), [](const ir::Keyframe<T>& k) {
        return !finiteValue(k.value) || (k.hasTangents && (!finiteValue(k.inTangent) || !finiteValue(k.outTangent)));
    }), track.keys.end());
    return before - track.keys.size();
}

static void prepareBezierHandles(IBezFloatKey& key)
{
    SetTangentLock(key.flags, 0, FALSE);
    key.inLength = key.outLength = 1.0f / 3.0f;
}

template <class Key>   // IBezPoint3Key, IBezScaleKey
static void prepareBezierHandles(Key& key)
{
    for (int axis = 0; axis < 3; ++axis) SetTangentLock(key.flags, axis, FALSE);
    key.inLength = key.outLength = Point3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);
}

template <class Key, class T>
static void setMdxBezierTangents(Key& key, const std::vector<ir::Keyframe<T>>& keys,
                                 int i, bool hermite)
{
    const auto& kf = keys[i];
    const int n = static_cast<int>(keys.size());
    prepareBezierHandles(key);

    // Tangent types go through a variable: SetInTanType/SetOutTanType shift
    // their argument unparenthesized, which would split a ?: expression.
    const bool inCurve = kf.hasTangents && i > 0 && keys[i - 1].hasTangents;
    SetInTanType(key.flags, BEZKEY_USER);   // tangent stays 0 (the key is zeroed) unless curved
    if (inCurve) {
        const float dt = static_cast<float>(kf.time - keys[i - 1].time);
        if (dt > 0.0f) key.intan = hermite ? kf.inTangent * (-1.0f / dt)
                                           : (kf.inTangent - kf.value) * (3.0f / dt);
    }

    const bool outCurve = kf.hasTangents && i + 1 < n && keys[i + 1].hasTangents;
    SetOutTanType(key.flags, BEZKEY_USER);
    if (outCurve) {
        const float dt = static_cast<float>(keys[i + 1].time - kf.time);
        if (dt > 0.0f) key.outtan = hermite ? kf.outTangent * (1.0f / dt)
                                             : (kf.outTangent - kf.value) * (3.0f / dt);
    }
}

// A flat key (sequence stub): holds its value, like the game does there.
// Custom tangents of 0 - the key is zeroed - not BEZKEY_FLAT, which is Auto.
template <class Key>
static void setFlatBezierKey(Key& key)
{
    prepareBezierHandles(key);
    SetInTanType(key.flags, BEZKEY_USER);
    SetOutTanType(key.flags, BEZKEY_USER);
}

// Adds a rest value to a track's keys. Bezier tangents are control points and
// move with the value; Hermite tangents are derivatives and do not.
static void offsetTrack(ir::Vec3Track& track, const Point3& delta)
{
    const bool bezier = (track.interpolation == ir::InterpolationType::Bezier);
    for (auto& k : track.keys) {
        k.value += delta;
        if (bezier && k.hasTangents) {
            k.inTangent += delta;
            k.outTangent += delta;
        }
    }
}

// ── Sequence boundary key insertion ─────────────────────────
// Matches MaxScript applyTranslationAnimations / applyRotationAnimations /
// applyScaleAnimations boundary logic. For each sequence [startTime, endTime],
// if the track has no key at endTime, insert one with the value of the LAST
// key inside the sequence; if no key at startTime, insert one with the value
// of the FIRST key inside the sequence. Skipped for global-sequence tracks
// (those don't honor sequence boundaries).
//
// Without these synthetic boundary keys, the last key of one sequence
// "leaks" into the start of the next sequence — bones drift / float across
// animation transitions because the controller interpolates from sequence
// N's terminal pose toward sequence N+1's first key, instead of resetting.
template <typename T>
void insertSequenceBoundaryKeys(ir::Track<T>& track,
                                const std::vector<ir::Sequence>& sequences)
{
    if (track.empty()) return;
    if (track.globalSequenceIndex >= 0) return;
    if (sequences.empty()) return;

    std::set<TimeValue> existing;
    for (const auto& k : track.keys) existing.insert(k.time);

    std::vector<ir::Keyframe<T>> additions;

    for (const auto& seq : sequences) {
        // End boundary: sample LAST key in [startTime, endTime]
        if (existing.find(seq.endTime) == existing.end()) {
            const ir::Keyframe<T>* last = nullptr;
            for (const auto& k : track.keys) {
                if (k.time > seq.endTime) break;
                if (k.time >= seq.startTime) last = &k;
            }
            if (last) {
                ir::Keyframe<T> nk;
                nk.time = seq.endTime;
                nk.value = last->value;
                nk.hasTangents = false;
                additions.push_back(nk);
                existing.insert(seq.endTime);
            }
        }

        // Start boundary: sample FIRST key in [startTime, endTime]
        if (existing.find(seq.startTime) == existing.end()) {
            const ir::Keyframe<T>* first = nullptr;
            for (const auto& k : track.keys) {
                if (k.time > seq.endTime) break;
                if (k.time >= seq.startTime) { first = &k; break; }
            }
            if (first) {
                ir::Keyframe<T> nk;
                nk.time = seq.startTime;
                nk.value = first->value;
                nk.hasTangents = false;
                additions.push_back(nk);
                existing.insert(seq.startTime);
            }
        }
    }

    if (additions.empty()) return;

    track.keys.insert(track.keys.end(), additions.begin(), additions.end());
    std::sort(track.keys.begin(), track.keys.end(),
        [](const ir::Keyframe<T>& a, const ir::Keyframe<T>& b) {
            return a.time < b.time;
        });
}

// ── Curved rotations ────────────────────────────────────────
// Max has no rotation controller that interpolates like the game: the
// importer uses linear_rotation (slerp), while Hermite and Bezier rotation
// tracks are drawn by the game as squad between the keys and their tangent
// quaternions (slerp(slerp(a, b, s), slerp(aOut, bIn, s), 2s(1-s)), as in
// mdx-m3-viewer's sqlerp). Keeping only the file's keys bends limbs by
// tens of units between them (Teen Abomination "Attack": Chain_2 30 units).
// So the curve is sampled once per frame between two keys of the same
// sequence (or of a global-sequence track) and stored as extra linear keys.
// A step track ("None") holds its value until one frame before the next key.
static Quat quatSlerpGame(Quat a, Quat b, float t)
{
    float cosom = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (cosom < 0.0f) { cosom = -cosom; b = Quat(-b.x, -b.y, -b.z, -b.w); }
    float s0 = 1.0f - t, s1 = t;
    if (1.0f - cosom > 1e-6f) {
        const float omega = std::acos(std::min(1.0f, cosom));
        const float sinom = std::sin(omega);
        s0 = std::sin((1.0f - t) * omega) / sinom;
        s1 = std::sin(t * omega) / sinom;
    }
    return Quat(s0 * a.x + s1 * b.x, s0 * a.y + s1 * b.y,
                s0 * a.z + s1 * b.z, s0 * a.w + s1 * b.w);
}

static void bakeCurvedRotation(ir::QuatTrack& track,
                               const std::vector<ir::Sequence>& sequences)
{
    const auto interp = track.interpolation;
    if (track.keys.size() < 2 || interp == ir::InterpolationType::Linear) return;
    const TimeValue tpf = GetTicksPerFrame();
    const bool gseq = track.globalSequenceIndex >= 0;
    auto sameSequence = [&](TimeValue a, TimeValue b) {
        if (gseq) return true;
        for (const auto& s : sequences)
            if (s.startTime <= a && b <= s.endTime) return true;
        return false;
    };
    std::vector<ir::Keyframe<Quat>> extra;
    for (size_t i = 0; i + 1 < track.keys.size(); ++i) {
        const auto& a = track.keys[i];
        const auto& b = track.keys[i + 1];
        if (b.time - a.time <= tpf || !sameSequence(a.time, b.time)) continue;
        if (interp == ir::InterpolationType::None) {
            ir::Keyframe<Quat> k;
            k.time = b.time - tpf;
            k.value = a.value;
            extra.push_back(k);
            continue;
        }
        // Boundary stubs carry no tangents: the game holds there anyway.
        if (!a.hasTangents || !b.hasTangents) continue;
        const TimeValue first = (a.time / tpf + 1) * tpf;
        for (TimeValue t = first; t < b.time; t += tpf) {
            const float s = static_cast<float>(t - a.time) / static_cast<float>(b.time - a.time);
            Quat q = quatSlerpGame(quatSlerpGame(a.value, b.value, s),
                                   quatSlerpGame(a.outTangent, b.inTangent, s),
                                   2.0f * s * (1.0f - s));
            const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            if (len > 1e-8f) q = Quat(q.x / len, q.y / len, q.z / len, q.w / len);
            ir::Keyframe<Quat> k;
            k.time = t;
            k.value = q;
            extra.push_back(k);
        }
    }
    if (extra.empty()) return;
    track.keys.insert(track.keys.end(), extra.begin(), extra.end());
    std::stable_sort(track.keys.begin(), track.keys.end(),
        [](const ir::Keyframe<Quat>& x, const ir::Keyframe<Quat>& y) { return x.time < y.time; });
}

void insertTranslationKeys(INode* node, const ir::Vec3Track& track,
                           const Point3& stubVal)
{
    if (track.empty()) return;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return;

    // Replace the default Position XYZ controller with a typed Position
    // controller that matches the MDX interpolation. Position XYZ wraps
    // 3 float sub-controllers and routes SetValue per-channel, which
    // discards the source interpolation type and tangents. Hermite goes on
    // Bezier Position as well: TCB keys have no free tangents, bezier
    // handles hold the file's curve exactly (setMdxBezierTangents).
    const Class_ID cid = (track.interpolation == ir::InterpolationType::Linear)
        ? Class_ID(LININTERP_POSITION_CLASS_ID, 0)
        : Class_ID(HYBRIDINTERP_POSITION_CLASS_ID, 0);

    Control* posCtrl = static_cast<Control*>(
        CreateInstance(CTRL_POSITION_CLASS_ID, cid));
    if (!posCtrl) return;
    tmCtrl->SetPositionController(posCtrl);

    IKeyControl* ikc = GetKeyControlInterface(posCtrl);
    if (!ikc) return;

    ILOG << "    TRANS '" << narrow(node->GetName()) << "'"
         << " stub=(" << stubVal.x << "," << stubVal.y << "," << stubVal.z
         << ") keys=" << track.keys.size()
         << " interp=" << static_cast<int>(track.interpolation) << "\n";
    if (track.keys.size() > 0) {
        auto& k0 = track.keys[0];
        ILOG << "      key[0] time=" << k0.time
             << " val=(" << k0.value.x << "," << k0.value.y << "," << k0.value.z << ")\n";
    }
    ILOG.flush();

    int numKeys = static_cast<int>(track.keys.size());
    // Stub rule: sequence-driven tracks get a Frame-0 stub to anchor the
    // rest pose; global-sequence tracks DO NOT (NeoDex-compatible behavior).
    // See insertRotationKeys for the full rationale.
    bool isGlobalSeq = (track.globalSequenceIndex >= 0);
    bool needStub = !isGlobalSeq && (numKeys == 0 || track.keys[0].time > 0);

    if (track.interpolation == ir::InterpolationType::Linear) {
        if (needStub) {
            ILinPoint3Key k;
            memset(&k, 0, sizeof(k));
            k.time = 0;
            k.val = stubVal;
            ikc->AppendKey(&k);
        }
        for (int i = 0; i < numKeys; ++i) {
            ILinPoint3Key k;
            memset(&k, 0, sizeof(k));
            k.time = track.keys[i].time;
            k.val = track.keys[i].value;
            ikc->AppendKey(&k);
        }
    } else { // Bezier, Hermite, None (step)
        const bool step = (track.interpolation == ir::InterpolationType::None);
        const bool hermite = (track.interpolation == ir::InterpolationType::Hermite);
        if (needStub) {
            IBezPoint3Key k;
            memset(&k, 0, sizeof(k));
            k.time = 0;
            k.val = stubVal;
            if (step) {
                SetInTanType(k.flags, BEZKEY_STEP);
                SetOutTanType(k.flags, BEZKEY_STEP);
            } else {
                setFlatBezierKey(k);
            }
            ikc->AppendKey(&k);
        }
        for (int i = 0; i < numKeys; ++i) {
            const auto& kf = track.keys[i];
            IBezPoint3Key k;
            memset(&k, 0, sizeof(k));
            k.time = kf.time;
            k.val = kf.value;
            if (step) {
                SetInTanType(k.flags, BEZKEY_STEP);
                SetOutTanType(k.flags, BEZKEY_STEP);
            } else {
                setMdxBezierTangents(k, track.keys, i, hermite);
            }
            ikc->AppendKey(&k);
        }
    }

    ikc->SortKeys();

    // ORT for global sequences: constant before (hold first key), cycle after.
    // Matches NeoDex setup. Without the before=constant, Max would extrapolate
    // backwards from the first key, giving wrong values at Frame 0 for models
    // whose global-seq keys live at high tick values (e.g., Undead3D_Exp
    // Bone_Main's gseq key at Frame 1970).
    if (isGlobalSeq) {
        posCtrl->SetORT(ORT_CONSTANT, ORT_BEFORE);
        posCtrl->SetORT(ORT_CYCLE,    ORT_AFTER);
        posCtrl->EnableORTs(TRUE);
    }
}

void insertRotationKeys(INode* node, const ir::QuatTrack& track,
                        const Quat& stubVal) {
    if (track.empty()) return;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return;

    // ── DEBUG: log for Bone_Main ──
    bool debugThis = false;
    {
        std::wstring wn = node->GetName();
        std::string nn(wn.begin(), wn.end());
        if (nn.find("Bone_Main") != std::string::npos) {
            debugThis = true;
            ILOG << "  *** insertRotationKeys for '" << nn << "' ***\n";
            ILOG << "    track.keys=" << track.keys.size()
                 << " gseq=" << track.globalSequenceIndex << "\n";
            ILOG << "    stubVal=(" << stubVal.x << "," << stubVal.y
                 << "," << stubVal.z << "," << stubVal.w << ")\n";
            for (size_t i = 0; i < track.keys.size() && i < 5; ++i) {
                const auto& k = track.keys[i];
                ILOG << "    in key[" << i << "] time=" << k.time
                     << " (frame=" << (k.time / GetTicksPerFrame()) << ")"
                     << " quat=(" << k.value.x << "," << k.value.y
                     << "," << k.value.z << "," << k.value.w << ")\n";
            }
        }
    }

    // Replace rotation controller with linear_rotation for quaternion SLERP.
    // Unlike position, we MUST replace because Euler XYZ can't do quaternion interpolation.
    Control* rotCtrl = static_cast<Control*>(
        CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    if (!rotCtrl) return;
    tmCtrl->SetRotationController(rotCtrl);

    // Clear any pre-existing keys inherited from SetNodeTM(0, identity+pivot).
    // When SetRotationController replaces the controller, Max may seed the new
    // linear_rotation with an identity key at Frame 0. We always clear to
    // guarantee a clean slate — writing our own keys on top of unknown
    // pre-existing keys risks "undefined behavior" per SDK docs
    // ("setting two keys to the same time value is undefined, and should be
    // avoided"). For gseq tracks this removes the phantom that was causing
    // the Undead3D Bone_Main bug; for non-gseq tracks it just ensures our
    // Frame-0 stub lands on an empty track as intended.
    bool isGlobalSeq = (track.globalSequenceIndex >= 0);
    {
        IKeyControl* ikcPre = GetKeyControlInterface(rotCtrl);
        if (ikcPre && ikcPre->GetNumKeys() > 0) {
            int preExisting = ikcPre->GetNumKeys();
            ikcPre->SetNumKeys(0);
            if (debugThis) {
                ILOG << "    cleared " << preExisting
                     << " pre-existing keys on new linear_rotation controller\n";
            }
        }
    }

    int numRotKeys = static_cast<int>(track.keys.size());

    // MaxScript fixQuaternions: ensure consecutive keys are in the same
    // hemisphere so SLERP takes the shortest arc.
    Quat prevQ = stubVal;
    std::vector<Quat> fixedKeys(numRotKeys);
    for (int i = 0; i < numRotKeys; ++i) {
        Quat q = track.keys[i].value;
        if ((q.x * prevQ.x + q.y * prevQ.y + q.z * prevQ.z + q.w * prevQ.w) < 0.0f)
            q = -q;
        fixedKeys[i] = q;
        prevQ = q;
    }

    // Use SetValue for key creation — SDK-recommended approach
    // SetValue for rotation expects CONJUGATE quaternion compared to ILinRotKey.val
    // Conjugate: negate x,y,z; keep w
    //
    // Stub rule:
    //   * Sequence-driven tracks (globalSequenceIndex < 0):
    //       Insert identity stub at Frame 0 if first real key isn't at 0.
    //       This anchors the rest pose for Linear/Bezier interpolation from
    //       "before the animation starts" to the first key.
    //   * Global-sequence tracks (globalSequenceIndex >= 0):
    //       NO stub. NeoDex writes exactly the track's keys as-is, then relies
    //       on ORT_BEFORE=constant to hold the first real key's value for all
    //       frames before it. Adding an identity stub would cause Max to SLERP
    //       from identity to the actual rotation between Frame 0 and the first
    //       key — which is wrong (see Undead3D_Exp Bone_Main: 65.67s key at
    //       Frame 1970 was being SLERPed against an identity stub at Frame 0,
    //       so Arthas appeared nearly unrotated in the sequence window).
    // (isGlobalSeq already declared above, after SetRotationController.)
    bool needRotStub = !isGlobalSeq && (numRotKeys == 0 || track.keys[0].time > 0);

    // Use IKeyControl::AppendKey instead of SetValue — bypasses the
    // Animate-button gate (SetValue with Animate off applies to frame 0
    // per Autodesk docs, not to the specified time; that's the Undead3D
    // Bone_Main bug). Position already uses this approach.
    //
    // Quaternion convention:
    //   SetValue(CTRL_ABSOLUTE) expects left-hand (SDK) convention — we
    //   had to pass conjugate(q) to match Max's internal storage.
    //   IKeyControl stores keys in the SAME internal storage, so the
    //   same conjugate applies to ILinRotKey.val.
    IKeyControl* ikc = GetKeyControlInterface(rotCtrl);
    if (!ikc) return;

    if (needRotStub) {
        ILinRotKey k;
        memset(&k, 0, sizeof(k));
        k.time = 0;
        k.val = Quat(-stubVal.x, -stubVal.y, -stubVal.z, stubVal.w);
        ikc->AppendKey(&k);
        if (debugThis) {
            ILOG << "    STUB at time=0 value=(" << k.val.x << "," << k.val.y
                 << "," << k.val.z << "," << k.val.w << ")\n";
        }
    } else if (debugThis && isGlobalSeq) {
        ILOG << "    STUB skipped (globalSequence track)\n";
    }

    for (int i = 0; i < numRotKeys; ++i) {
        ILinRotKey k;
        memset(&k, 0, sizeof(k));
        k.time = track.keys[i].time;
        k.val = Quat(-fixedKeys[i].x, -fixedKeys[i].y, -fixedKeys[i].z, fixedKeys[i].w);
        ikc->AppendKey(&k);
        if (debugThis) {
            ILOG << "    WROTE time=" << track.keys[i].time
                 << " (frame=" << (track.keys[i].time / GetTicksPerFrame()) << ")"
                 << " value=(" << k.val.x << "," << k.val.y
                 << "," << k.val.z << "," << k.val.w << ")\n";
        }
    }

    // Sort keys by time. AppendKey doesn't enforce ordering, and SDK docs
    // warn that out-of-order keys produce undefined behavior. Matches the
    // pattern used by insertTranslationKeys.
    ikc->SortKeys();

    if (isGlobalSeq) {
        // Before = constant (hold first key's value for frames before it).
        // After  = cycle (loop the sequence).  Matches NeoDex setup.
        rotCtrl->SetORT(ORT_CONSTANT, ORT_BEFORE);
        rotCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
        rotCtrl->EnableORTs(TRUE);
        if (debugThis) {
            ILOG << "    ORT: before=CONSTANT, after=CYCLE (gseq)\n";
            // Verify ORT was actually set
            int gotBefore = rotCtrl->GetORT(ORT_BEFORE);
            int gotAfter = rotCtrl->GetORT(ORT_AFTER);
            ILOG << "    GetORT readback: before=" << gotBefore
                 << " after=" << gotAfter
                 << " (ORT_CONSTANT=" << ORT_CONSTANT
                 << " ORT_CYCLE=" << ORT_CYCLE << ")\n";
        }
    }

    if (debugThis) {
        // Read back what Max currently has at time 0
        Quat readBack;
        Interval iv = FOREVER;
        rotCtrl->GetValue(0, &readBack, iv, CTRL_ABSOLUTE);
        ILOG << "    READBACK at time=0: (" << readBack.x << "," << readBack.y
             << "," << readBack.z << "," << readBack.w << ")\n";
        // Also readback at key time to confirm key is stored correctly
        Quat readBackKey;
        Interval iv2 = FOREVER;
        rotCtrl->GetValue(track.keys[0].time, &readBackKey, iv2, CTRL_ABSOLUTE);
        ILOG << "    READBACK at keytime=" << track.keys[0].time
             << ": (" << readBackKey.x << "," << readBackKey.y
             << "," << readBackKey.z << "," << readBackKey.w << ")\n";
    }
}

void insertScaleKeys(INode* node, const ir::Vec3Track& track,
                     const Point3& stubVal)
{
    if (track.empty()) return;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return;

    // A typed scale controller written through IKeyControl, like position:
    // SetValue on the default Bezier Scale let Max pick its own tangents for
    // every key, so Linear, DontInterp and the file's Bezier/Hermite curves
    // all played as auto-smooth curves. Hermite sits on bezier handles.
    const bool linear = (track.interpolation == ir::InterpolationType::Linear);
    const Class_ID cid = linear ? Class_ID(LININTERP_SCALE_CLASS_ID, 0)
                                : Class_ID(HYBRIDINTERP_SCALE_CLASS_ID, 0);
    Control* scaleCtrl = static_cast<Control*>(CreateInstance(CTRL_SCALE_CLASS_ID, cid));
    if (!scaleCtrl) return;
    tmCtrl->SetScaleController(scaleCtrl);

    IKeyControl* ikc = GetKeyControlInterface(scaleCtrl);
    if (!ikc) return;

    int numKeys = static_cast<int>(track.keys.size());

    // Stub rule: sequence-driven tracks get a Frame-0 stub (bind-pose scale);
    // global-sequence tracks DO NOT (matches NeoDex behavior, prevents
    // unwanted interpolation from stub scale to first real key).
    bool isGlobalSeq = (track.globalSequenceIndex >= 0);
    bool needStub = !isGlobalSeq && (numKeys == 0 || track.keys[0].time > 0);

    if (linear) {
        if (needStub) {
            ILinScaleKey k;
            memset(&k, 0, sizeof(k));
            k.time = 0;
            k.val = ScaleValue(stubVal);
            ikc->AppendKey(&k);
        }
        for (int i = 0; i < numKeys; ++i) {
            ILinScaleKey k;
            memset(&k, 0, sizeof(k));
            k.time = track.keys[i].time;
            k.val = ScaleValue(track.keys[i].value);
            ikc->AppendKey(&k);
        }
    } else { // Bezier, Hermite, None (step)
        const bool step = (track.interpolation == ir::InterpolationType::None);
        const bool hermite = (track.interpolation == ir::InterpolationType::Hermite);
        if (needStub) {
            IBezScaleKey k;
            memset(&k, 0, sizeof(k));
            k.time = 0;
            k.val = ScaleValue(stubVal);
            if (step) {
                SetInTanType(k.flags, BEZKEY_STEP);
                SetOutTanType(k.flags, BEZKEY_STEP);
            } else {
                setFlatBezierKey(k);
            }
            ikc->AppendKey(&k);
        }
        for (int i = 0; i < numKeys; ++i) {
            IBezScaleKey k;
            memset(&k, 0, sizeof(k));
            k.time = track.keys[i].time;
            k.val = ScaleValue(track.keys[i].value);
            if (step) {
                SetInTanType(k.flags, BEZKEY_STEP);
                SetOutTanType(k.flags, BEZKEY_STEP);
            } else {
                setMdxBezierTangents(k, track.keys, i, hermite);
            }
            ikc->AppendKey(&k);
        }
    }
    ikc->SortKeys();

    // A lone global-sequence key is constant by definition, and its key range
    // is zero-length — cycling over it is degenerate. Max's default (constant
    // both ways) is already what the engine does.
    if (isGlobalSeq && numKeys > 1) {
        scaleCtrl->SetORT(ORT_CONSTANT, ORT_BEFORE);
        scaleCtrl->SetORT(ORT_CYCLE,    ORT_AFTER);
        scaleCtrl->EnableORTs(TRUE);
    }
}

// ── Parameter animation helpers ─────────────────────────────
// Matches MaxScript applyFloatParamAnimations / applyColorParamAnimations.
// Creates a controller of the appropriate type, writes keys via IKeyControl,
// then assigns the controller to a paramblock param via SetController.

// Paramblock param locator (same as scene builders)
struct PBParam {
    IParamBlock2* pb = nullptr;
    ParamID id = -1;
    explicit operator bool() const { return pb != nullptr; }
};

PBParam findPBParam(ReferenceTarget* target, const wchar_t* name) {
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

// Write float keys onto a controller and return it.  Caller assigns to PB.
Control* createFloatController(const ir::FloatTrack& track) {
    if (track.empty()) return nullptr;

    // Hermite goes on a bezier controller too: a TCB key has no free
    // tangents, so only bezier handles can hold the file's curve exactly.
    const Class_ID cid = (track.interpolation == ir::InterpolationType::Linear)
        ? Class_ID(LININTERP_FLOAT_CLASS_ID, 0)
        : Class_ID(HYBRIDINTERP_FLOAT_CLASS_ID, 0);

    Control* ctrl = static_cast<Control*>(CreateInstance(CTRL_FLOAT_CLASS_ID, cid));
    if (!ctrl) return nullptr;

    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) { ctrl->DeleteThis(); return nullptr; }

    int numKeys = static_cast<int>(track.keys.size());

    for (int i = 0; i < numKeys; ++i) {
        const auto& kf = track.keys[i];
        switch (track.interpolation) {
        case ir::InterpolationType::Linear: {
            ILinFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = kf.value;
            ikc->AppendKey(&key);
            break;
        }
        default: { // Bezier and Hermite; step-keyed for None (DontInterp)
            IBezFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = kf.value;

            if (track.interpolation == ir::InterpolationType::None) {
                SetInTanType(key.flags, BEZKEY_STEP);
                SetOutTanType(key.flags, BEZKEY_STEP);
            } else {
                setMdxBezierTangents(key, track.keys, i,
                                     track.interpolation == ir::InterpolationType::Hermite);
            }
            ikc->AppendKey(&key);
            break;
        }
        }
    }
    ikc->SortKeys();

    if (track.globalSequenceIndex >= 0) {
        ctrl->SetORT(ORT_CYCLE, ORT_AFTER);
        ctrl->EnableORTs(TRUE);
    }
    return ctrl;
}

// Write color keys onto a Bezier Color controller and return it.
Control* createColorController(const ir::ColorTrack& track) {
    if (track.empty()) return nullptr;

    // All color interpolation types use Bezier Color (HYBRIDINTERP_COLOR_CLASS_ID):
    // Max has no Linear color controller, and TCB keys have no free tangents.
    Control* ctrl = static_cast<Control*>(
        CreateInstance(CTRL_POINT3_CLASS_ID, Class_ID(HYBRIDINTERP_COLOR_CLASS_ID, 0)));
    if (!ctrl) return nullptr;

    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) { ctrl->DeleteThis(); return nullptr; }

    // Colour keys as Point3 keyframes, so they share the bezier conversion.
    std::vector<ir::Keyframe<Point3>> keys;
    keys.reserve(track.keys.size());
    for (const auto& kf : track.keys) {
        ir::Keyframe<Point3> k;
        k.time = kf.time;
        k.value = Point3(kf.value.r, kf.value.g, kf.value.b);
        k.inTangent = Point3(kf.inTangent.r, kf.inTangent.g, kf.inTangent.b);
        k.outTangent = Point3(kf.outTangent.r, kf.outTangent.g, kf.outTangent.b);
        k.hasTangents = kf.hasTangents;
        keys.push_back(k);
    }
    const bool hermite = (track.interpolation == ir::InterpolationType::Hermite);

    for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
        IBezPoint3Key key;
        memset(&key, 0, sizeof(key));
        key.time = keys[i].time;
        key.val = keys[i].value;

        if (track.interpolation == ir::InterpolationType::None) {
            SetInTanType(key.flags, BEZKEY_STEP);
            SetOutTanType(key.flags, BEZKEY_STEP);
        } else if (track.interpolation == ir::InterpolationType::Linear) {
            // No linear colour controller exists; LINEAR tangents play the
            // same and tell the exporter the track was Linear.
            SetInTanType(key.flags, BEZKEY_LINEAR);
            SetOutTanType(key.flags, BEZKEY_LINEAR);
        } else { // Bezier, Hermite: the file's curve on bezier handles
            setMdxBezierTangents(key, keys, i, hermite);
        }
        ikc->AppendKey(&key);
    }
    ikc->SortKeys();

    if (track.globalSequenceIndex >= 0) {
        ctrl->SetORT(ORT_CYCLE, ORT_AFTER);
        ctrl->EnableORTs(TRUE);
    }
    return ctrl;
}

// Animate a float paramblock param from an IR float track index.
void animateFloatPB(IParamBlock2* pb, ParamID pid,
                    int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!pb || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.floatTracks.size()))
        return;
    const auto& track = irModel.floatTracks[trackIndex];
    Control* ctrl = createFloatController(track);
    if (ctrl)
        pb->SetControllerByID(pid, 0, ctrl, FALSE);
}

// Animate a float param by name on a scripted plugin (by track index).
void animateFloatNamed(ReferenceTarget* ref, const wchar_t* name,
                       int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!ref || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.floatTracks.size()))
        return;
    auto p = findPBParam(ref, name);
    if (!p) return;
    const auto& track = irModel.floatTracks[trackIndex];
    Control* ctrl = createFloatController(track);
    if (ctrl)
        p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
}

// Animate a float param by name on a scripted plugin (by track directly).
void animateFloatNamed(ReferenceTarget* ref, const wchar_t* name,
                       const ir::FloatTrack& track)
{
    if (!ref || track.empty()) return;
    auto p = findPBParam(ref, name);
    if (!p) return;
    Control* ctrl = createFloatController(track);
    if (ctrl)
        p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
}

// Animate a color param by name on a scripted plugin.
void animateColorNamed(ReferenceTarget* ref, const wchar_t* name,
                       int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!ref || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.colorTracks.size()))
        return;
    auto p = findPBParam(ref, name);
    if (!p) return;
    const auto& track = irModel.colorTracks[trackIndex];
    Control* ctrl = createColorController(track);
    if (ctrl)
        p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
}

// ── MaxScript-based animation helpers for scripted simpleManipulator plugins ──
//
// Scripted `simpleManipulator` plugins (Wdx_Wc3Light, Wdx_Wc3Event, etc.)
// wrap their parameters in a runtime MSPlugin proxy with dynamic ParamBlock
// and ParamIDs. `findPBParam + SetControllerByID` locates the parameter but
// fails to attach controllers reliably — the static value from the last
// keyframe leaks into the plugin's static parameter, but the Control* is
// not connected, so animation is dead on playback.
//
// The reliable route (proven for KGAC) is via MaxScript:
//   selected[1].<param>.controller = bezier_float()   -- oder bezier_color()
//   local c = selected[1].<param>.controller
//   addNewKey c <ticks>
//   k.value = ...
//
// MaxScript sees the parameter via the plugin's published interface directly
// and the resulting controller is attached through the plugin's own
// published API.
//
// These helpers take a node (to resolve via `getNodeByName`) and a parameter
// name. Used only for scripted simpleManipulator plugins.

void animateFloatNamedScript(INode* node, const wchar_t* paramName,
                             int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!node || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.floatTracks.size()))
        return;
    const auto& track = irModel.floatTracks[trackIndex];
    if (track.empty()) return;

    const wchar_t* tanType = L"#smooth";
    switch (track.interpolation) {
        case ir::InterpolationType::None:   tanType = L"#step";   break;
        case ir::InterpolationType::Linear: tanType = L"#linear"; break;
        default: break;
    }

    // Resolve by handle: node names are not unique, getNodeByName would
    // animate the first same-named node instead of this one.
    std::wstringstream ss;
    ss << L"(local n = maxOps.getNodeByHandle " << node->GetHandle() << L";"
       << L"if n != undefined do ("
       << L"n." << paramName << L".controller = bezier_float();"
       << L"local c = n." << paramName << L".controller;";

    // V3 strategy for Bezier: the sides towards a same-value neighbour are flat
    // (custom tangent 0 - "#flat" is Auto in Max) to avoid overshoot.
    // Linear/Step/None bypass this and use their own tangent type.
    constexpr float kEpsilon = 1e-4f;
    const bool isBezier = (track.interpolation == ir::InterpolationType::Bezier);
    const int n = static_cast<int>(track.keys.size());
    for (int i = 0; i < n; ++i) {
        const auto& kf = track.keys[i];
        const wchar_t* inTan = tanType;
        const wchar_t* outTan = tanType;
        if (isBezier) {
            bool prevSame = (i > 0) && (fabsf(track.keys[i-1].value - kf.value) < kEpsilon);
            bool nextSame = (i < n - 1) && (fabsf(track.keys[i+1].value - kf.value) < kEpsilon);
            inTan  = prevSame ? L"#custom" : L"#smooth";
            outTan = nextSame ? L"#custom" : L"#smooth";
        }
        ss << L"local k = addNewKey c " << kf.time << L"t;"
           << L"k.value = " << kf.value << L";"
           << L"k.inTangentType = " << inTan << L";"
           << L"k.outTangentType = " << outTan << L";";
        if (inTan[1] == L'c') ss << L"k.inTangent = 0.0;";
        if (outTan[1] == L'c') ss << L"k.outTangent = 0.0;";
    }

    if (track.globalSequenceIndex >= 0) {
        ss << L"setBeforeORT c #constant;"
           << L"setAfterORT c #cycle;"
           << L"enableORTs c true;";
    }

    ss << L"))";

    std::wstring scriptStr = ss.str();
    ExecuteMAXScriptScript(
        const_cast<wchar_t*>(scriptStr.c_str()),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
        MAXScript::ScriptSource::NonEmbedded,
#endif
        TRUE, nullptr);
}

void animateColorNamedScript(INode* node, const wchar_t* paramName,
                             int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!node || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.colorTracks.size()))
        return;
    const auto& track = irModel.colorTracks[trackIndex];
    if (track.empty()) return;

    const wchar_t* tanType = L"#smooth";
    switch (track.interpolation) {
        case ir::InterpolationType::None:   tanType = L"#step";   break;
        case ir::InterpolationType::Linear: tanType = L"#linear"; break;
        default: break;
    }

    // Resolve by handle — see animateFloatNamedScript.
    std::wstringstream ss;
    ss << L"(local n = maxOps.getNodeByHandle " << node->GetHandle() << L";"
       << L"if n != undefined do ("
       << L"n." << paramName << L".controller = bezier_color();"
       << L"local c = n." << paramName << L".controller;";

    constexpr float kEpsilon = 1e-4f;
    auto sameColor = [&](const Color& a, const Color& b) {
        return fabsf(a.r - b.r) < kEpsilon
            && fabsf(a.g - b.g) < kEpsilon
            && fabsf(a.b - b.b) < kEpsilon;
    };
    const bool isBezier = (track.interpolation == ir::InterpolationType::Bezier);
    const int n = static_cast<int>(track.keys.size());

    for (int i = 0; i < n; ++i) {
        const auto& kf = track.keys[i];
        // Max's color picker uses 0..255 range (confirmed by Wc3Light plugin
        // defaults: AmbColor=[255,230,142], ShadowColor=[122,172,255]).
        // Convert MDX 0..1 float to 0..255 byte. BGR->RGB swap is already
        // done in the disassembler.
        int r = static_cast<int>(kf.value.r * 255.0f + 0.5f);
        int g = static_cast<int>(kf.value.g * 255.0f + 0.5f);
        int b = static_cast<int>(kf.value.b * 255.0f + 0.5f);
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;

        // V3 strategy for Bezier: flat (custom tangent 0) towards a
        // same-value neighbour - "#flat" is Auto in Max.
        const wchar_t* inTan = tanType;
        const wchar_t* outTan = tanType;
        if (isBezier) {
            bool prevSame = (i > 0) && sameColor(track.keys[i-1].value, kf.value);
            bool nextSame = (i < n - 1) && sameColor(track.keys[i+1].value, kf.value);
            inTan  = prevSame ? L"#custom" : L"#smooth";
            outTan = nextSame ? L"#custom" : L"#smooth";
        }
        ss << L"local k = addNewKey c " << kf.time << L"t;"
           << L"k.value = color " << r << L" " << g << L" " << b << L";"
           << L"k.inTangentType = " << inTan << L";"
           << L"k.outTangentType = " << outTan << L";";
        if (inTan[1] == L'c') ss << L"k.inTangent = [0,0,0];";
        if (outTan[1] == L'c') ss << L"k.outTangent = [0,0,0];";
    }

    if (track.globalSequenceIndex >= 0) {
        ss << L"setBeforeORT c #constant;"
           << L"setAfterORT c #cycle;"
           << L"enableORTs c true;";
    }

    ss << L"))";

    std::wstring scriptStr = ss.str();
    ExecuteMAXScriptScript(
        const_cast<wchar_t*>(scriptStr.c_str()),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
        MAXScript::ScriptSource::NonEmbedded,
#endif
        TRUE, nullptr);
}


// Animate a color paramblock param from an IR color track index.
void animateColorPB(IParamBlock2* pb, ParamID pid,
                    int32_t trackIndex, const ir::IRModel& irModel)
{
    if (!pb || trackIndex < 0 ||
        trackIndex >= static_cast<int32_t>(irModel.colorTracks.size()))
        return;
    const auto& track = irModel.colorTracks[trackIndex];
    Control* ctrl = createColorController(track);
    if (ctrl)
        pb->SetControllerByID(pid, 0, ctrl, FALSE);
}

// ParamIDs for native plugins (mirrored from scene builders)

// ── Helper: get the diffuse texmap from a Wc3Material ────────
// Reads the "diffuseMap" paramblock param. With the TXAN-on-texture scheme
// this is normally a Wc3Bitmap wrapper; legacy scenes may hold a plain
// BitmapTex.
static Texmap* getDiffuseTexmap(ReferenceTarget* wc3MatRef) {
    if (!wc3MatRef) return nullptr;
    auto p = findPBParam(wc3MatRef, L"diffuseMap");
    if (!p) return nullptr;
    ParamType2 ptype = p.pb->GetParameterType(p.id);
    if (ptype != TYPE_TEXMAP) return nullptr;
    Texmap* tex = nullptr;
    Interval valid = FOREVER;
    p.pb->GetValue(p.id, 0, tex, valid);
    return tex;
}

// Unwrap a Texmap to its native BitmapTex: the texmap itself when it is one,
// the BitmapTex delegate when it's a Wc3Bitmap wrapper, else nullptr.
static BitmapTex* unwrapBitmapTex(Texmap* tex) {
    if (!tex) return nullptr;
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
        return static_cast<BitmapTex*>(tex);
    if (tex->ClassID() == mdx_ids::WC3_BITMAP) {
        for (int i = 0; i < tex->NumRefs(); i++) {
            ReferenceTarget* ref = tex->GetReference(i);
            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0))
                return static_cast<BitmapTex*>(ref);
        }
    }
    return nullptr;
}

// ── Helper: assign a float controller to a StdUVGen parameter ──
// Uses findPBParam on the StdUVGen's IParamBlock2 to find the param by name.
// If that fails (older Max versions), falls back to sub-anim assignment.
//
// Known StdUVGen internal param names (3ds Max 2020+):
//   "U_Offset", "V_Offset", "U_Tiling", "V_Tiling",
//   "U_Angle", "V_Angle", "W_Angle"
//
// Sub-anim indices (stable across all Max versions):
//   0=U_Offset, 1=V_Offset, 2=U_Tiling, 3=V_Tiling,
//   4=U_Angle, 5=V_Angle, 6=W_Angle
static bool assignControllerToUVGen(StdUVGen* uvGen, const wchar_t* paramName,
                                     int subAnimFallbackIndex, Control* ctrl)
{
    if (!uvGen || !ctrl) return false;

    // Strategy 1: IParamBlock2 by internal name (most reliable for modern Max)
    auto* uvRef = dynamic_cast<ReferenceTarget*>(uvGen);
    if (uvRef) {
        auto p = findPBParam(uvRef, paramName);
        if (p) {
            p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
            return true;
        }
    }

    // Strategy 2: Sub-anim assignment (works on all Max versions)
    if (subAnimFallbackIndex >= 0 && subAnimFallbackIndex < uvGen->NumSubs()) {
        BOOL result = uvGen->AssignController(ctrl, subAnimFallbackIndex);
        return result != FALSE;
    }

    return false;
}

// ParamIDs for native plugins (mirrored from scene builders)
enum P1Params : ParamID {
    P1_PB_COUNT = 0, P1_PB_SPEED = 1, P1_PB_EMISSION_RATE = 2,
    P1_PB_LIFE = 3, P1_PB_ACCELERATION = 4,
    P1_PB_LATITUDE = 5, P1_PB_LONGITUDE = 6,
};
enum P2Params : ParamID {
    P2_PB_COUNT = 0, P2_PB_SPEED = 1, P2_PB_VARIATION = 2,
    P2_PB_WIDTH = 4, P2_PB_HEIGHT = 5, P2_PB_INITVEL = 6,
    P2_PB_ANGLE_Y = 7,
    P2_PB_GRAVITY = 36,
};
enum RibbonParams : ParamID {
    RB_PB_HEIGHT_ABOVE = 0, RB_PB_HEIGHT_BELOW = 1,
    RB_PB_TEX_SLOT = 6, RB_PB_COLOR = 8, RB_PB_ALPHA = 9,
};

// ── Visibility key insertion ────────────────────────────────
// Matches MaxScript applyVisibilityAnimations:
//   None (DontInterp) → boolean_float (absolute 0/1 per key, via MaxScript)
//   Linear            → linear_float
//   Hermite           → tcb_float
//   Bezier            → bezier_float with custom tangents

void insertVisibilityKeys(INode* node, const ir::FloatTrack& track,
                          const std::vector<ir::Sequence>& sequences)
{
    if (track.empty()) return;

    int numKeys = static_cast<int>(track.keys.size());

    // Ensure frame 0 exists
    bool hasFrameZero = false;
    for (const auto& k : track.keys)
        if (k.time == 0) { hasFrameZero = true; break; }

    Control* visCtrl = nullptr;

    switch (track.interpolation) {
    case ir::InterpolationType::None: {
        // DontInterp → boolean_float controller (BoolController).
        //
        // Why boolean_float and NOT On_Off:
        //   * On_Off treats each key as a TOGGLE relative to current state.
        //     Consecutive 0-value keys would cancel each other out, and
        //     state-compression eliminates sequence-boundary stubs so that
        //     idle sequences (no MDX visibility keys) end up with NO keys
        //     in Max's Track View. That makes per-sequence editing
        //     impossible and breaks round-trip to MDX (Blizzard exporters
        //     expect explicit visibility keys at every sequence boundary).
        //   * boolean_float stores ABSOLUTE 1.0/0.0 per key. Every stub
        //     produced by remapVisibilityTrack survives as an explicit
        //     key, matching NeoDex output.
        //
        // Native SDK has no publicly exposed Class_ID for boolean_float
        // (BOOLCNTRL_CLASS_ID is not in maxsdk/control.h). We create the
        // controller via MaxScript where it's a first-class named type,
        // then walk the remapped keys and set each one explicitly.
        //
        // The sequence-boundary / per-sequence-reset semantics (NeoDex
        // preProcessVisibility: reset to 1.0 at every sequence start)
        // are handled upstream in remapVisibilityTrack. By the time we
        // get here, the key list already contains the proper per-sequence
        // boundary stubs with correct hold values.

        // Resolve by HANDLE, never by name: node names are not unique
        // (e.g. Madara_SusanooHum has five PE2 nodes all named "1"), and
        // getNodeByName returns the first match — every same-named node's
        // visibility track landed on that one node (last write wins) while
        // the rest got NO visibility at all, breaking the round-trip.
        std::wstringstream ss;
        ss << L"(local n = maxOps.getNodeByHandle " << node->GetHandle() << L";"
           << L"if n != undefined do ("
           << L"n.visibility = bezier_float();"    // create the visibility track
           << L"n.visibility.controller = boolean_float();"
           << L"local c = n.visibility.controller;";

        // Add one key per remapped key — boolean_float stores 0.0/1.0 absolutely
        for (const auto& kf : track.keys) {
            float v = (kf.value >= 0.5f) ? 1.0f : 0.0f;
            ss << L"addNewKey c " << kf.time << L"t;";
            // The newly added key is the last one. Set its value.
            ss << L"c.keys[c.keys.count].value = " << v << L";";
        }

        if (track.globalSequenceIndex >= 0) {
            ss << L"setAfterORT c #cycle;"
               << L"enableORTs c true;";
        }

        ss << L"))";   // close: if-do, local-block

        std::wstring scriptStr = ss.str();
        ExecuteMAXScriptScript(
            const_cast<wchar_t*>(scriptStr.c_str()),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            MAXScript::ScriptSource::NonEmbedded,
#endif
            TRUE, nullptr);

        // visCtrl stays nullptr here — we don't touch it further, and
        // the ORT / frame-0 post-processing below is gated by visCtrl.
        return;
    }
    case ir::InterpolationType::Linear: {
        visCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(LININTERP_FLOAT_CLASS_ID, 0)));
        if (!visCtrl) return;
        node->SetVisController(visCtrl);

        IKeyControl* ikc = GetKeyControlInterface(visCtrl);
        if (!ikc) return;

        if (!hasFrameZero) {
            ILinFloatKey stubKey;
            memset(&stubKey, 0, sizeof(stubKey));
            stubKey.time = 0;
            stubKey.val = track.keys[0].value;
            ikc->AppendKey(&stubKey);
        }

        for (int i = 0; i < numKeys; ++i) {
            ILinFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = track.keys[i].time;
            key.val = track.keys[i].value;
            ikc->AppendKey(&key);
        }
        ikc->SortKeys();
        break;
    }
    case ir::InterpolationType::Hermite:
    case ir::InterpolationType::Bezier: {
        // The file's curve on bezier handles (setMdxBezierTangents); Hermite
        // too, since TCB keys have no free tangents.
        visCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(HYBRIDINTERP_FLOAT_CLASS_ID, 0)));
        if (!visCtrl) return;
        node->SetVisController(visCtrl);

        IKeyControl* ikc = GetKeyControlInterface(visCtrl);
        if (!ikc) return;

        if (!hasFrameZero) {
            IBezFloatKey stubKey;
            memset(&stubKey, 0, sizeof(stubKey));
            stubKey.time = 0;
            stubKey.val = track.keys[0].value;
            setFlatBezierKey(stubKey);
            ikc->AppendKey(&stubKey);
        }

        const bool hermite = (track.interpolation == ir::InterpolationType::Hermite);
        for (int i = 0; i < numKeys; ++i) {
            IBezFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = track.keys[i].time;
            key.val = track.keys[i].value;
            setMdxBezierTangents(key, track.keys, i, hermite);
            ikc->AppendKey(&key);
        }
        ikc->SortKeys();
        break;
    }
    }

    // ORT cycle for global sequences
    if (visCtrl && track.globalSequenceIndex >= 0) {
        visCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
        visCtrl->EnableORTs(TRUE);
    }
}

// ── Timeline remapping ──────────────────────────────────────
// Matches MaxScript: convertSequences + preProcessKeys + apply*Animations.
// Compacts sequences to start at frame 10 with 20-frame gaps,
// remaps all animation key times, and inserts boundary keys.

struct SeqRange {
    TimeValue oldStart, oldEnd;
    TimeValue newStart, newEnd;
    bool isLooping;
    size_t origIdx;
};

// ── Visibility-specific remap ───────────────────────────────────
// Matches NeoDex `preProcessVisibility` semantics:
//   * The WC3 engine RESETS visibility to 1.0 (visible) at the start of
//     every sequence. This is confirmed by Taylor_Mouse (NeoDex author):
//     "Blizzard resetted the visibility back to its original value at
//      the start of every sequence".
//   * Unlike `remapTrackKeys` which holds the last value across sequence
//     gaps, visibility must default to 1.0 in sequences that have no
//     keys, and re-establish 1.0 as the starting value of each new
//     sequence unless there's an explicit 0-value key at the start.
//
// Algorithm (mirrors NeoDex preProcessVisibility):
//   1. Insert frame-0 stub key at value 1.0
//   2. Per sequence:
//      - If sequence has NO keys in its [oldStart, oldEnd] window:
//        insert TWO stubs (newStart, newEnd) with value 1.0
//      - If sequence has keys:
//        * If no key lands exactly at newStart: insert stub at
//          newStart with the FIRST in-sequence key's value
//        * Remap each in-sequence key to newTime = oldTime + offset
//        * If last key doesn't land at newEnd: insert stub at newEnd
//          holding the last key's value
//   3. Sort by time, dedupe same-time keys (keep last)
void remapVisibilityTrack(ir::FloatTrack& track,
                          const std::vector<SeqRange>& ranges)
{
    if (track.keys.empty() || track.globalSequenceIndex >= 0) return;

    // Pre-sort + dedup (same as remapTrackKeys — handles ms-rounding
    // collisions at sequence boundaries).
    if (track.keys.size() > 1) {
        std::stable_sort(track.keys.begin(), track.keys.end(),
            [](const ir::Keyframe<float>& a, const ir::Keyframe<float>& b) {
                return a.time < b.time;
            });
        std::vector<ir::Keyframe<float>> deduped;
        deduped.reserve(track.keys.size());
        for (const auto& k : track.keys) {
            if (!deduped.empty() && deduped.back().time == k.time)
                deduped.back() = k;
            else
                deduped.push_back(k);
        }
        track.keys = std::move(deduped);
    }

    const float DEFAULT_VIS = 1.0f;
    std::vector<ir::Keyframe<float>> newKeys;

    // Frame 0 stub — always visible at the very start of the timeline.
    ir::Keyframe<float> zeroKey{};
    zeroKey.time = 0;
    zeroKey.value = DEFAULT_VIS;
    newKeys.push_back(zeroKey);

    for (size_t si = 0; si < ranges.size(); ++si) {
        const auto& r = ranges[si];

        // Collect in-sequence keys in [oldStart, oldEnd].
        // Back-to-back rule: a key at an exact boundary that is also
        // the oldStart of the next sequence belongs to the next one.
        std::vector<size_t> seqKeyIdx;
        for (size_t ki = 0; ki < track.keys.size(); ++ki) {
            TimeValue t = track.keys[ki].time;
            if (t < r.oldStart || t > r.oldEnd) continue;
            if (t == r.oldEnd && si + 1 < ranges.size()
                && ranges[si + 1].oldStart == r.oldEnd)
                continue;
            seqKeyIdx.push_back(ki);
        }

        TimeValue offset = r.newStart - r.oldStart;

        if (seqKeyIdx.empty()) {
            // Sequence has no keys → visible throughout (per NeoDex).
            ir::Keyframe<float> sk{}; sk.time = r.newStart; sk.value = DEFAULT_VIS;
            ir::Keyframe<float> ek{}; ek.time = r.newEnd;   ek.value = DEFAULT_VIS;
            newKeys.push_back(sk);
            newKeys.push_back(ek);
        } else {
            // Start boundary: if no key lands exactly at newStart,
            // insert a stub with the first in-sequence key's value.
            TimeValue firstRemapped = track.keys[seqKeyIdx[0]].time + offset;
            if (firstRemapped != r.newStart) {
                ir::Keyframe<float> sk{};
                sk.time = r.newStart;
                sk.value = track.keys[seqKeyIdx[0]].value;
                newKeys.push_back(sk);
            }

            // Remap sequence keys.
            for (size_t ki : seqKeyIdx) {
                ir::Keyframe<float> k = track.keys[ki];
                k.time += offset;
                newKeys.push_back(k);
            }

            // End boundary: hold last in-sequence value until end-of-seq.
            size_t lastKi = seqKeyIdx.back();
            TimeValue lastRemapped = track.keys[lastKi].time + offset;
            if (lastRemapped != r.newEnd) {
                ir::Keyframe<float> ek{};
                ek.time = r.newEnd;
                ek.value = track.keys[lastKi].value;
                newKeys.push_back(ek);
            }
        }
    }

    // Sort + dedup (keep last value at any duplicate tick)
    std::stable_sort(newKeys.begin(), newKeys.end(),
        [](const ir::Keyframe<float>& a, const ir::Keyframe<float>& b) {
            return a.time < b.time;
        });
    std::vector<ir::Keyframe<float>> cleanKeys;
    cleanKeys.reserve(newKeys.size());
    for (const auto& k : newKeys) {
        if (!cleanKeys.empty() && cleanKeys.back().time == k.time)
            cleanKeys.back() = k;
        else
            cleanKeys.push_back(k);
    }
    track.keys = std::move(cleanKeys);
}

template <typename T>
void remapTrackKeys(ir::Track<T>& track,
                    const std::vector<SeqRange>& ranges,
                    const T& defaultVal,
                    bool useDefaultForStartBoundary,
                    bool resetOnEmptySeq = false)
{
    if (track.keys.empty() || track.globalSequenceIndex >= 0) return;

    // De-duplicate keys at same tick time. This handles MDX models where
    // two adjacent keys at near-identical ms values (e.g., 2999ms + 3000ms
    // at a sequence boundary) both round to the same Max tick via
    // msToTicks. Without this, the remap picks the wrong key for loop
    // closure (e.g., Walk-loop-close uses Stand-end-pose, causing the
    // Stand pose to appear at the end of Walk).
    // Strategy: keep the LAST key at each tick — semantically the "new"
    // value that should take effect from that frame onward.
    if (track.keys.size() > 1) {
        std::stable_sort(track.keys.begin(), track.keys.end(),
            [](const ir::Keyframe<T>& a, const ir::Keyframe<T>& b) {
                return a.time < b.time;
            });
        std::vector<ir::Keyframe<T>> deduped;
        deduped.reserve(track.keys.size());
        for (const auto& k : track.keys) {
            if (!deduped.empty() && deduped.back().time == k.time)
                deduped.back() = k;
            else
                deduped.push_back(k);
        }
        track.keys = std::move(deduped);
    }

    std::vector<ir::Keyframe<T>> newKeys;

    // Pre-first-sequence keys: proportional remap into [0, firstSeq.newStart].
    // Without this, MDX keys at t=0ms (common for initial/baseline states)
    // are silently dropped because the per-sequence loop only collects keys
    // within [oldStart, oldEnd] ranges. The Campfire model's KLAC/KLBC
    // Key[0] at t=0ms landed nowhere until this block was added.
    if (!ranges.empty()) {
        const auto& r0 = ranges[0];
        TimeValue ogS = 0, ogE = r0.oldStart;
        TimeValue ngS = 0, ngE = r0.newStart;
        float oLen = static_cast<float>(ogE - ogS);
        float nLen = static_cast<float>(ngE - ngS);
        if (oLen > 0.0f && nLen > 0.0f) {
            for (const auto& k : track.keys) {
                if (k.time >= ogS && k.time < ogE) {
                    float ratio = static_cast<float>(k.time - ogS) / oLen;
                    ir::Keyframe<T> gk = k;
                    gk.time = ngS + static_cast<TimeValue>(ratio * nLen);
                    newKeys.push_back(gk);
                }
            }
        }
    }

    for (size_t si = 0; si < ranges.size(); ++si) {
        const auto& r = ranges[si];

        // Collect keys in [oldStart, oldEnd].
        // Back-to-back: key at boundary goes to next sequence.
        std::vector<size_t> seqKeyIdx;
        for (size_t ki = 0; ki < track.keys.size(); ++ki) {
            TimeValue t = track.keys[ki].time;
            if (t < r.oldStart || t > r.oldEnd) continue;
            if (t == r.oldEnd && si + 1 < ranges.size()
                && ranges[si + 1].oldStart == r.oldEnd)
                continue;
            seqKeyIdx.push_back(ki);
        }

        TimeValue offset = r.newStart - r.oldStart;

        if (seqKeyIdx.empty()) {
            // No keys in this sequence — hold a sensible neutral value.
            // Previously defaultVal (1.0f) was used, but that was only correct
            // for visibility tracks. For Speed/EmissionRate/Gravity/Width/
            // Height/Variation, 1.0 is not a meaningful neutral and produces
            // wrong stubs when the track's keys all cluster in one sequence
            // (e.g. saurus BLOOD_FX has Speed keys only within "Death" — all
            // other 20 sequences would get stubs at 1.0 otherwise).
            //
            // Priority for the "held" value:
            //   1. Last output key we already produced (track's current state
            //      through earlier remapped sequences).
            //   2. Last MDX key at or before r.oldStart (state before this seq).
            //   3. First MDX key (track's initial/anchor value). This is the
            //      critical fallback for sequences BEFORE the first keyed one.
            //   4. defaultVal (should never reach this — track has keys).
            //
            // EXCEPTION (resetOnEmptySeq == true): for bone Translation/Rotation/
            // Scale tracks, the Wc3 engine convention is that a sequence WITHOUT
            // keys for a bone evaluates to local identity (0,0,0 / identity quat /
            // 1,1,1) — NOT to the last value of the previous sequence. Without
            // this branch, e.g. Crystal Golem rt_wrist_jnt holds the Death-end
            // pose throughout the Stand sequence because Stand has no KGTR keys.
            // In NeoDex this works because no stubs are inserted at all, so
            // Max's bind-pose interpolation takes over; we explicitly write the
            // identity stub to match.
            T holdVal = defaultVal;
            if (resetOnEmptySeq) {
                // Use the bind-pose/identity default unconditionally.
                // holdVal stays = defaultVal.
            } else if (!newKeys.empty()) {
                holdVal = newKeys.back().value;
            } else {
                bool found = false;
                for (size_t ki = track.keys.size(); ki-- > 0;) {
                    if (track.keys[ki].time <= r.oldStart) {
                        holdVal = track.keys[ki].value;
                        found = true;
                        break;
                    }
                }
                if (!found && !track.keys.empty()) {
                    // All MDX keys are in a future sequence — use the first
                    // key as the anchor. This matches what an emitter with
                    // a keyed run (e.g. BLOOD_FX on saurus Death) would look
                    // like: before Death it sits at whatever value the first
                    // Death key specifies, not a synthesised 1.0.
                    holdVal = track.keys.front().value;
                }
            }
            ir::Keyframe<T> sk{}; sk.time = r.newStart; sk.value = holdVal;
            ir::Keyframe<T> ek{}; ek.time = r.newEnd;   ek.value = holdVal;
            newKeys.push_back(sk);
            newKeys.push_back(ek);
        } else {
            // Start boundary
            if (track.keys[seqKeyIdx[0]].time + offset != r.newStart) {
                ir::Keyframe<T> sk{};
                sk.time = r.newStart;
                // Previously: useDefaultForStartBoundary ? defaultVal : firstKey
                // That flag was set for float/int tracks and forced a jump to
                // 1.0 (or 0) between sequences, destroying the held value.
                // Now: always use the first in-sequence key's value — the
                // emitter "enters" each sequence at whatever the author wrote
                // as the first keyframe. This matches what NeoDex does.
                sk.value = track.keys[seqKeyIdx[0]].value;
                newKeys.push_back(sk);
            }

            // Remap sequence keys
            for (size_t ki : seqKeyIdx) {
                ir::Keyframe<T> k = track.keys[ki];
                k.time += offset;
                newKeys.push_back(k);
            }

            // End boundary
            size_t lastKi = seqKeyIdx.back();
            if (track.keys[lastKi].time + offset != r.newEnd) {
                ir::Keyframe<T> ek{};
                ek.time = r.newEnd;
                ek.value = r.isLooping
                    ? track.keys[seqKeyIdx[0]].value   // loop closure
                    : track.keys[lastKi].value;         // hold last
                newKeys.push_back(ek);
            }
        }

        // Gap keys: proportional remapping into (shorter) inter-sequence gap
        if (si + 1 < ranges.size()) {
            TimeValue ogS = r.oldEnd, ogE = ranges[si + 1].oldStart;
            TimeValue ngS = r.newEnd, ngE = ranges[si + 1].newStart;
            float oLen = static_cast<float>(ogE - ogS);
            float nLen = static_cast<float>(ngE - ngS);
            if (oLen > 0.0f && nLen > 0.0f) {
                for (const auto& k : track.keys) {
                    if (k.time > ogS && k.time < ogE) {
                        float ratio = static_cast<float>(k.time - ogS) / oLen;
                        ir::Keyframe<T> gk = k;
                        gk.time = ngS + static_cast<TimeValue>(ratio * nLen);
                        newKeys.push_back(gk);
                    }
                }
            }
        }
    }

    std::sort(newKeys.begin(), newKeys.end(),
        [](const ir::Keyframe<T>& a, const ir::Keyframe<T>& b) {
            return a.time < b.time;
        });

    track.keys = std::move(newKeys);
}

void remapTimeline(ir::IRModel& irModel) {
    if (irModel.sequences.empty()) return;

    const TimeValue tpf = GetTicksPerFrame(); // 160 at 30 fps

    // Build ranges sorted by old start time
    std::vector<SeqRange> ranges;
    ranges.reserve(irModel.sequences.size());
    for (size_t i = 0; i < irModel.sequences.size(); ++i) {
        SeqRange r{};
        r.oldStart  = irModel.sequences[i].startTime;
        r.oldEnd    = irModel.sequences[i].endTime;
        r.isLooping = irModel.sequences[i].isLooping;
        r.origIdx   = i;
        ranges.push_back(r);
    }
    std::sort(ranges.begin(), ranges.end(),
        [](const SeqRange& a, const SeqRange& b) { return a.oldStart < b.oldStart; });

    // Compact: frame 10 start, gap = 20 frames between sequences.
    // Previously this used ((endFrames / 10) * 10 + 20) which rounds
    // DOWN to the next multiple of 10, silently losing up to 9 frames
    // when a sequence's duration is not a multiple of 10 (e.g. Campfire
    // Stand=45 frames → end=85 → rounds to 80, Death starts at 100
    // instead of 105). The correct behavior is a consistent 20-frame gap.
    TimeValue nextStart = 10 * tpf;
    for (auto& r : ranges) {
        TimeValue dur = r.oldEnd - r.oldStart;
        r.newStart = nextStart;
        r.newEnd   = nextStart + dur;
        TimeValue endFrames = r.newEnd / tpf;
        nextStart = (endFrames + 20) * tpf;
    }

    // Update ir::Sequence times
    for (const auto& r : ranges) {
        irModel.sequences[r.origIdx].startTime = r.newStart;
        irModel.sequences[r.origIdx].endTime   = r.newEnd;
    }

    // Remap node animation tracks.
    //
    // For Translation/Rotation/Scale on bones, we pass resetOnEmptySeq=true
    // so that sequences WITHOUT keys for a bone collapse to local identity
    // (0,0,0 / identity quat / 1,1,1) instead of holding the last value
    // from a previous sequence. This matches the Wc3 engine semantics:
    // a sequence with no track entries for a bone returns the bone to its
    // bind pose, not whatever pose the previous sequence ended on.
    //
    // Concrete failure case without this flag: Crystal Golem rt_wrist_jnt
    // has KGTR keys only in Walk/Death/Birth/Sleep but NOT in Stand. With
    // hold-last semantics, Stand inherits the Death-end pose ("dead arm
    // dangling"). With resetOnEmptySeq, Stand correctly goes to bind pose.
    for (auto& na : irModel.nodeAnimations) {
        remapTrackKeys(na.translation, ranges, Point3(0.0f, 0.0f, 0.0f), false, true);
        remapTrackKeys(na.rotation,    ranges, Quat(0.0f, 0.0f, 0.0f, 1.0f), false, true);
        remapTrackKeys(na.scale,       ranges, Point3(1.0f, 1.0f, 1.0f), false, true);
    }

    // ── Remap floatTracks, routing visibility through dedicated path ─
    // Visibility has different semantics: per-sequence reset to 1.0
    // (visible) rather than "hold last value". Matches NeoDex.
    //
    // Identify which floatTrack indices are visibility tracks by
    // collecting the referenced indices from every object type that
    // has a visibility track field.
    std::set<int32_t> visTrackIndices;
    auto addIfValid = [&](int32_t idx) {
        if (idx >= 0 && idx < static_cast<int32_t>(irModel.floatTracks.size()))
            visTrackIndices.insert(idx);
    };
    for (const auto& light : irModel.lights)
        addIfValid(light.visibilityTrackIndex);
    for (const auto& att : irModel.attachments)
        addIfValid(att.visibilityTrackIndex);
    for (const auto& pe : irModel.particleEmitters)
        addIfValid(pe.visibilityTrackIndex);
    for (const auto& rib : irModel.ribbonEmitters)
        addIfValid(rib.visibilityTrackIndex);
    for (const auto& cam : irModel.cameras)
        addIfValid(cam.visibilityTrackIndex);
    // GeosetAnim alpha tracks drive mesh visibility — same NeoDex logic.
    for (const auto& ga : irModel.geosetAnims)
        addIfValid(ga.alphaTrackIndex);

    // Layer alpha (KMTA) and texture animation (KTAT/KTAR/KTAS) tracks: like
    // bone tracks, a sequence without keys plays the track's default, not
    // the previous sequence's last value (mdx-m3-viewer sd.ts: KMTA 1, KTAT
    // 0, KTAR identity, KTAS 1). Holding would show, say, the last frame of a
    // sprite sheet keyed only in Attack for the rest of the sequences.
    std::set<int32_t> layerAlphaTracks, uvTranslationTracks, uvRotationTracks, uvScaleTracks;
    for (const auto& mat : irModel.materials)
        for (const auto& layer : mat.layers)
            if (layer.alphaTrackIndex >= 0) layerAlphaTracks.insert(layer.alphaTrackIndex);
    for (const auto& ta : irModel.textureAnimations) {
        if (ta.translationTrackIndex >= 0) uvTranslationTracks.insert(ta.translationTrackIndex);
        if (ta.rotationTrackIndex >= 0)    uvRotationTracks.insert(ta.rotationTrackIndex);
        if (ta.scaleTrackIndex >= 0)       uvScaleTracks.insert(ta.scaleTrackIndex);
    }

    for (size_t i = 0; i < irModel.floatTracks.size(); ++i) {
        const int32_t idx = static_cast<int32_t>(i);
        if (visTrackIndices.count(idx)) {
            remapVisibilityTrack(irModel.floatTracks[i], ranges);
        } else {
            remapTrackKeys(irModel.floatTracks[i], ranges, 1.0f, true,
                           layerAlphaTracks.count(idx) > 0);
        }
    }
    for (size_t i = 0; i < irModel.vec3Tracks.size(); ++i) {
        const int32_t idx = static_cast<int32_t>(i);
        if (uvScaleTracks.count(idx))
            remapTrackKeys(irModel.vec3Tracks[i], ranges, Point3(1.0f, 1.0f, 1.0f), false, true);
        else
            remapTrackKeys(irModel.vec3Tracks[i], ranges, Point3(0.0f, 0.0f, 0.0f), false,
                           uvTranslationTracks.count(idx) > 0);
    }
    for (size_t i = 0; i < irModel.quatTracks.size(); ++i)
        remapTrackKeys(irModel.quatTracks[i], ranges, Quat(0.0f, 0.0f, 0.0f, 1.0f), false,
                       uvRotationTracks.count(static_cast<int32_t>(i)) > 0);
    for (auto& t : irModel.colorTracks)
        remapTrackKeys(t, ranges, Color(1.0f, 1.0f, 1.0f), false);
    for (auto& t : irModel.intTracks)
        remapTrackKeys(t, ranges, int32_t(0), true);
    for (auto& t : irModel.vec4Tracks)
        remapTrackKeys(t, ranges, Point4(0.0f, 0.0f, 0.0f, 0.0f), false);

    // Remap event object key times. Keys on a global sequence are in that
    // sequence's own clock, like every other global-sequence track, so they
    // stay where they are.
    for (auto& evt : irModel.eventObjects) {
        if (evt.globalSequenceIndex >= 0) continue;
        std::vector<TimeValue> newTimes;
        for (TimeValue kt : evt.keyTimes) {
            for (size_t si = 0; si < ranges.size(); ++si) {
                const auto& r = ranges[si];
                if (kt >= r.oldStart && kt <= r.oldEnd) {
                    newTimes.push_back(kt + (r.newStart - r.oldStart));
                    break;
                }
                if (si + 1 < ranges.size() && kt > r.oldEnd
                    && kt < ranges[si + 1].oldStart) {
                    float oLen = static_cast<float>(ranges[si + 1].oldStart - r.oldEnd);
                    float nLen = static_cast<float>(ranges[si + 1].newStart - r.newEnd);
                    if (oLen > 0.0f && nLen > 0.0f) {
                        float ratio = static_cast<float>(kt - r.oldEnd) / oLen;
                        newTimes.push_back(
                            r.newEnd + static_cast<TimeValue>(ratio * nLen));
                    }
                    break;
                }
            }
        }
        evt.keyTimes = std::move(newTimes);
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// DoImport — main import pipeline
// ═══════════════════════════════════════════════════════════════

int MdlxImporterPlugin::DoImport(const TCHAR* name, ImpInterface* ii,
                                  Interface* gi, BOOL suppressPrompts)
{
    core::ExportErrorReporter reporter;
    MdlxImportOptions opts;

    // 1. Load settings from INI
    loadImportOptionsFromINI(gi, opts);

    // 2. Parse MDX/MDL file via WhiteoutLib (fast — needed to detect version)
    std::string filePath = wcharToUtf8(name);

    // Read the TRUE MDX version directly from the file header BEFORE
    // calling parser.parse(), which auto-upgrades mdxModel.version to
    // CurrentVersion (1200) regardless of what the file actually was.
    // This matches NeoDex's peekMDXVersion approach and is needed to
    // pick the correct dialog (Classic MPQ vs Reforged CASC) and to
    // make per-version code paths reachable.
    //
    // MDX layout: bytes 0..3 = "MDLX", 4..7 = "VERS", 8..11 = chunk
    // size (always 4), 12..15 = u32 version. v800 / v1000 / v1100 / v1200.
    // For .mdl files (text format), this byte-peek will return 0 and we
    // fall back to whatever the parser reports — fine since .mdl is
    // mostly v800/Classic anyway.
    uint32_t trueMdxVersion = 0;
    {
        FILE* f = nullptr;
        if (fopen_s(&f, filePath.c_str(), "rb") == 0 && f) {
            uint8_t header[16] = {0};
            if (fread(header, 1, 16, f) == 16) {
                if (header[0]=='M' && header[1]=='D' && header[2]=='L' && header[3]=='X' &&
                    header[4]=='V' && header[5]=='E' && header[6]=='R' && header[7]=='S') {
                    trueMdxVersion = (uint32_t)header[12]
                                   | ((uint32_t)header[13] << 8)
                                   | ((uint32_t)header[14] << 16)
                                   | ((uint32_t)header[15] << 24);
                }
            }
            fclose(f);
        }
    }

    whiteout::mdx::Parser parser;
    whiteout::mdx::Model mdxModel;

    try {
        mdxModel = parser.parse(filePath);
    } catch (const std::exception& e) {
        // The catalog holds the sentence with a %1 where the parser's own
        // reason goes; an absent catalog falls back to the English wording.
        MSTR reason;
        reason.printf(_T("%hs"), e.what());
        std::wstring text = wdx::l10n::TrOr("imp_parse_failed_msg",
                                            L"Failed to parse the model file:\n%1");
        wdx::l10n::Substitute(text, L"%1", reason.data());

        const std::wstring caption =
            wdx::l10n::TrOr("report_import_ptitle", L"WhiteoutDex Import");

        if (gi->GetMAXHWnd())
            MessageBoxW(gi->GetMAXHWnd(), text.c_str(), caption.c_str(), MB_OK | MB_ICONERROR);
        return IMPEXP_FAIL;
    }

    // Use the true (pre-upgrade) version where available, otherwise
    // fall back to the parser's value (e.g. for .mdl text files).
    opts.detectedVersion = (trueMdxVersion != 0) ? trueMdxVersion : mdxModel.version;

    // 3. Show import options dialog (unless suppressed). After Import it stays
    // open and shows the progress of the steps below; without a dialog
    // (#noPrompt) the progress calls do nothing.
    ImportDialog dialog(hInstance, gi->GetMAXHWnd());
    if (!suppressPrompts) {
        // Use the TRUE MDX version (pre-upgrade) so v800/v1000/v1100
        // models get the Classic MPQ dialog and only true v1200+ models
        // get the Reforged CASC dialog. mdxModel.version cannot be used
        // here because the parser auto-upgrades it to 1200.
        // The dialog also shows what the file contains and disables every
        // category it does not.
        ImportFileInfo info;
        info.path = name;
        info.version = opts.detectedVersion;
        info.isReforged = (opts.detectedVersion >= 900);
        info.geosets = static_cast<int>(mdxModel.geosets.size());
        info.materials = static_cast<int>(mdxModel.materials.size());
        info.textures = static_cast<int>(mdxModel.textures.size());
        info.sequences = static_cast<int>(mdxModel.sequences.size());
        info.bones = static_cast<int>(mdxModel.bones.size());
        info.helpers = static_cast<int>(mdxModel.helpers.size());
        info.lights = static_cast<int>(mdxModel.lights.size());
        info.attachments = static_cast<int>(mdxModel.attachments.size());
        info.particleEmitters1 = static_cast<int>(mdxModel.particleEmitters.size());
        info.particleEmitters2 = static_cast<int>(mdxModel.particleEmitters2.size());
        info.ribbonEmitters = static_cast<int>(mdxModel.ribbonEmitters.size());
        info.collisionShapes = static_cast<int>(mdxModel.collisionShapes.size());
        info.eventObjects = static_cast<int>(mdxModel.eventObjects.size());
        info.cameras = static_cast<int>(mdxModel.cameras.size());
        info.cornEmitters = static_cast<int>(mdxModel.cornEmitters.size());
        info.faceEffects = static_cast<int>(mdxModel.faceEffects.size());
        if (!dialog.run(opts, info))
            return IMPEXP_CANCEL;
    } else {
        // Scripted imports (#noPrompt) have always run with the All preset:
        // FastSettings was never read from the INI before, so keep them there
        // now that the loader reads it for the dialog.
        opts.core.preset = ir::CoreImportOptions::Preset::All;
    }

    applyFastPreset(opts);

    // 4. Disassemble: mdx::Model → ir::IRModel
    dialog.step(ImportStep::Preparing);
    mdx_disasm::MdxModelDisassembler disassembler;
    ir::IRModel irModel = disassembler.disassemble(mdxModel, opts);

    // 4a. The text encoding of the model's names (wdx_text.h): every name and
    // path below is read in it, and the scene keeps it for the exporter.
    std::vector<std::string> modelTexts;
    for (const auto& n : irModel.nodes) modelTexts.push_back(n.name);
    for (const auto& q : irModel.sequences) modelTexts.push_back(q.name);
    for (const auto& t : irModel.textures) modelTexts.push_back(t.filePath);
    const UINT modelCodePage = wdx::text::detectCodePage(modelTexts);
    wdx::text::CodePageScope codePageScope(modelCodePage);

    // 4b. Compact timeline (MaxScript convertSequences + preProcessKeys)
    remapTimeline(irModel);

    // 4c. PIVT / BPOS reconciliation.
    // In some v1200 (Reforged) models PIVT is left zero for certain bones —
    // in that case BPOS.row3 is the authoritative world-space position.
    // BUT: in v1000/v1100 (and mixed v1200 rigs) PIVT is populated and
    // authoritative, while BPOS may encode a different pose (e.g. Maya bind
    // vs exported rest) — KulTirasMarine has Shield_Root / pauldrons with
    // massively divergent values. Fix: per-bone fallback — use BPOS only if
    // PIVT is (near-)zero AND BPOS has a meaningful value.
    // NOTE: the whiteout parser auto-upgrades mdx.version to CurrentVersion
    // (1200) after loading, so detectedVersion==1200 alone does NOT mean
    // the file was originally v1200.
    if (opts.detectedVersion >= 1200) {
        for (auto& bone : irModel.bones) {
            Point3 bposPos = bone.bindPose.GetTrans();
            const bool pivtIsZero = bone.pivotPoint.LengthSquared() < 0.0001f;
            const bool bposIsZero = bposPos.LengthSquared() < 0.0001f;
            if (pivtIsZero && !bposIsZero) {
                bone.pivotPoint = bposPos;
                if (bone.nodeIndex >= 0
                    && bone.nodeIndex < static_cast<int32_t>(irModel.nodes.size())) {
                    irModel.nodes[bone.nodeIndex].pivotPoint = bposPos;
                }
            }
        }
    }

    // 5. Handle import mode
    if (opts.core.mode == ir::CoreImportOptions::ImportMode::NewScene) {
        // NewScene asks "save your changes?" when the scene counts as
        // modified - in Max 2016 even a freshly reset one does. A caller that
        // suppresses prompts (importFile #noPrompt, batch) must not get it.
        if (suppressPrompts)
            SetSaveRequiredFlag(FALSE);
        ii->NewScene();
    }

    // Non-finite vertex data (old exporters wrote "-1.#IND00" normals)
    // would reach Max's mesh as NaN: neutral values instead.
    for (auto& mesh : irModel.meshes) {
        for (auto& v : mesh.vertices) {
            auto fin3 = [](const Point3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
            if (!fin3(v.position)) v.position = Point3(0.0f, 0.0f, 0.0f);
            if (!fin3(v.normal)) v.normal = Point3(0.0f, 0.0f, 1.0f);
            for (auto& uv : v.uvSets)
                if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) uv = Point2(0.0f, 0.0f);
        }
    }
    for (auto& na : irModel.nodeAnimations) {
        dropNonFiniteKeys(na.translation);
        dropNonFiniteKeys(na.rotation);
        dropNonFiniteKeys(na.scale);
    }
    for (auto& t : irModel.floatTracks) dropNonFiniteKeys(t);
    for (auto& t : irModel.vec3Tracks) dropNonFiniteKeys(t);
    for (auto& t : irModel.quatTracks) dropNonFiniteKeys(t);
    for (auto& t : irModel.colorTracks) dropNonFiniteKeys(t);
    for (auto& t : irModel.vec4Tracks) dropNonFiniteKeys(t);
    // A new scene always gets the model's value, 0 included: Max 2016 keeps
    // the root node's user properties through a reset, so the code page of
    // the model imported before would otherwise encode this one's names
    // (a 1252 "Fuß" exported as UTF-8).
    const bool newScene = opts.core.mode == ir::CoreImportOptions::ImportMode::NewScene;
    if ((modelCodePage != 0 || newScene) && gi && gi->GetRootNode())
        gi->GetRootNode()->SetUserPropInt(_T("Wc3CodePage"), static_cast<int>(modelCodePage));

    // 6. Optimize (optional)
    if (opts.core.optimizeGeometry) {
        core::VertexOptimizer vopt;
        for (auto& mesh : irModel.meshes)
            vopt.optimize(mesh, 0.001f);
    }
    if (opts.core.optimizeBonesAndHelpers) {
        core::BoneOptimizer bopt;
        bopt.optimize(irModel);
    }

    // 7. Build skeleton (bones and helpers)
    dialog.step(ImportStep::Skeleton);
    // SuspendAnimate prevents auto-keying during SetNodeTM + AttachChild
    SuspendAnimate();
    AnimateOff();

    std::vector<INode*> nodeMap(irModel.nodes.size(), nullptr);
    std::vector<INode*> boneNodes;

    if (opts.core.importBones || opts.core.importHelpers) {
        boneNodes.reserve(irModel.bones.size());

        // Determine which bones are directly wanted by import settings
        // and which must be force-imported because they are ancestors of wanted bones.
        std::vector<bool> mustImport(irModel.nodes.size(), false);
        for (const auto& bone : irModel.bones) {
            if (bone.isHelper && !opts.core.importHelpers) continue;
            if (!bone.isHelper && !opts.core.importBones) continue;
            if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int32_t>(mustImport.size()))
                mustImport[bone.nodeIndex] = true;
        }
        // Walk ancestors: any node with an imported descendant must also be imported
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& bone : irModel.bones) {
                if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(mustImport.size()))
                    continue;
                if (!mustImport[bone.nodeIndex]) continue;
                // bone.parentIndex is a bone-array-pos (Index into irModel.bones),
                // not a nodeIndex. Translate via irModel.bones[parentIdx].nodeIndex
                // before indexing mustImport[] which is sized by nodeIndex space.
                if (bone.parentIndex >= 0
                    && bone.parentIndex < static_cast<int32_t>(irModel.bones.size())) {
                    int32_t parentNodeIdx = irModel.bones[bone.parentIndex].nodeIndex;
                    if (parentNodeIdx >= 0
                        && parentNodeIdx < static_cast<int32_t>(mustImport.size())
                        && !mustImport[parentNodeIdx]) {
                        mustImport[parentNodeIdx] = true;
                        changed = true;
                    }
                }
            }
        }

        size_t bonesDone = 0;
        for (const auto& bone : irModel.bones) {
            dialog.items(bonesDone++, irModel.bones.size());
            if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(mustImport.size()))
                continue;
            if (!mustImport[bone.nodeIndex]) continue;

            INode* boneNode = createBoneNode(bone, gi,
                bone.isHelper && opts.core.importHelpersAsPointHelpers);
            if (!boneNode) continue;

            // Replace rotation controller immediately with linear_rotation
            // before SetNodeTM.  The default Euler XYZ controller decomposes
            // rotations imprecisely (gimbal lock); linear_rotation stores the
            // quaternion directly and matches MaxScript's convention.
            {
                Control* tmCtrl = boneNode->GetTMController();
                if (tmCtrl) {
                    Control* linRot = static_cast<Control*>(
                        CreateInstance(CTRL_ROTATION_CLASS_ID,
                                       Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
                    if (linRot) tmCtrl->SetRotationController(linRot);
                }
            }

            // Position bone at pivot with identity rotation — matching MaxScript.
            // The full BPOS bind pose matrix (v1200) is metadata for round-trip
            // export, NOT for skeleton transforms.  Using it here would give bones
            // non-identity rest rotations, breaking the animation key math which
            // assumes local position = pivotDiff and rest rotation = identity.
            Matrix3 tm;
            tm.IdentityMatrix();
            tm.SetTrans(bone.pivotPoint);
            boneNode->SetNodeTM(0, tm);

            // MaxScript setupNode: apply DontInherit flags
            // MDX flags: 0x1 = DontInheritTranslation, 0x2 = DontInheritRotation, 0x4 = DontInheritScaling
            //
            // 0x2 (DontInheritRotation) is deliberately NOT translated to Max's
            // INHERIT_ROT_*. The Wc3 engine effectively ignores this flag in
            // its bone-TM composition: setting it on/off produces identical
            // rendering in-game, and the war3-model TypeScript renderer
            // explicitly skips it without any visual regression.
            //
            // In Max however, INHERIT_ROT_off does not reset the bone's own
            // rotation but suppresses rotation pass-through to CHILDREN.
            // Empirical measurement on Naruto_Hokage_2026.mdx (3ds Max Biped
            // export with the flag set on every biped bone), frame 10 of Stand:
            //
            //   Bone           with flag honored          flag stripped              Δ pos    Δ rot
            //   R Thigh (d2)   pos=(-0.15,+10.28,+80.50)  pos=(-0.15,+10.28,+80.50)  0.0      0°
            //   R Calf  (d3)   pos=(-20.13, +2.47,+51.18) pos=(-21.57,+18.43,+52.32) 16.1     53°
            //   R Foot  (d4)   pos=(-26.65,+32.79,+15.99) pos=(-19.92,+38.35, +9.88) 10.7     73°
            //
            // Grandchildren-and-deeper drift visibly away from the bind pose.
            // Stripping the flag matches the engine and produces correct anim.
            //
            // The original flag value is preserved as a user property so the
            // exporter can round-trip the original MDX bit pattern.
            boneNode->SetUserPropInt(_T("DontInheritRotation"),
                                     (bone.nodeFlags & 0x2) ? 1 : 0);

            if (bone.nodeFlags & 0x5) {  // 0x1 (translation) and 0x4 (scaling) only
                DWORD inheritFlags = INHERIT_ALL;
                if (bone.nodeFlags & 0x1)
                    inheritFlags &= ~(INHERIT_POS_X | INHERIT_POS_Y | INHERIT_POS_Z);
                if (bone.nodeFlags & 0x4)
                    inheritFlags &= ~(INHERIT_SCL_X | INHERIT_SCL_Y | INHERIT_SCL_Z);
                Control* tmCtrl = boneNode->GetTMController();
                if (tmCtrl)
                    tmCtrl->SetInheritanceFlags(inheritFlags, TRUE);
            }

            // MaxScript setupNode: billboard flags → user properties
            boneNode->SetUserPropInt(_T("Billboarded"),      (bone.nodeFlags & 0x8)  ? 1 : 0);
            boneNode->SetUserPropInt(_T("BillboardedLockX"), (bone.nodeFlags & 0x10) ? 1 : 0);
            boneNode->SetUserPropInt(_T("BillboardedLockY"), (bone.nodeFlags & 0x20) ? 1 : 0);
            boneNode->SetUserPropInt(_T("BillboardedLockZ"), (bone.nodeFlags & 0x40) ? 1 : 0);

            // MaxScript setupNode: CameraAnchored → user property
            boneNode->SetUserPropInt(_T("CameraAnchored"),   (bone.nodeFlags & 0x80) ? 1 : 0);

            // MaxScript: p.showLinks = true
            boneNode->ShowBone(2);

            if (bone.nodeIndex >= 0 && bone.nodeIndex < static_cast<int32_t>(nodeMap.size()))
                nodeMap[bone.nodeIndex] = boneNode;

            boneNodes.push_back(boneNode);
        }

        // Set parent relationships (second pass — all nodes must exist first)
        // MaxScript relinkObjects walks up the parent chain when a parent wasn't
        // created (e.g. helpers disabled).  We replicate that here.
        //
        // CRITICAL: bone.parentIndex is a bone-array-pos (Index into
        // irModel.bones), NOT a nodeIndex. To look up the parent's Max
        // node we translate via irModel.bones[parentIdx].nodeIndex
        // before indexing nodeMap[]. This used to "happen to work"
        // without BoneOptimizer because bone-array-pos coincidentally
        // matched nodeIndex on most models, but the moment the optimizer
        // compacts the bones array (or a model has non-bone nodes
        // interleaved among bones) the two index spaces diverge.
        for (const auto& bone : irModel.bones) {
            if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                continue;
            INode* boneNode = nodeMap[bone.nodeIndex];
            if (!boneNode) continue;

            // Walk the bone-array parent chain until we find a parent whose
            // Max node was actually created.
            int32_t parentBoneArrayPos = bone.parentIndex;
            INode* parent = nullptr;
            while (parentBoneArrayPos >= 0
                   && parentBoneArrayPos < static_cast<int32_t>(irModel.bones.size())) {
                int32_t parentNodeIdx = irModel.bones[parentBoneArrayPos].nodeIndex;
                if (parentNodeIdx >= 0
                    && parentNodeIdx < static_cast<int32_t>(nodeMap.size())
                    && nodeMap[parentNodeIdx]) {
                    parent = nodeMap[parentNodeIdx];
                    break;
                }
                // This bone's Max node wasn't created — walk up via the
                // bone-array parent chain (which is also bone-array-pos).
                parentBoneArrayPos = irModel.bones[parentBoneArrayPos].parentIndex;
            }

            if (parent)
                parent->AttachChild(boneNode);
        }
    }

    ResumeAnimate();  // ← skeleton setup done, restore animate state

    // DEBUG: dump bone state immediately after creation (before skin, before animation).
    // Expect: autoAlign=0, freezeLen=0, isBone=1, stretchTM=IDENTITY for all bones.
    // nodeTM.pos == objTM.pos at t=0.
    dumpBoneState(boneNodes, "after-create");

    // 8. Build meshes
    dialog.step(ImportStep::Meshes);
    std::vector<INode*> meshNodes;
    meshNodes.reserve(irModel.meshes.size());

    for (const auto& irMesh : irModel.meshes) {
        dialog.items(meshNodes.size(), irModel.meshes.size());
        INode* meshNode = createMeshNode(irMesh, gi);
        meshNodes.push_back(meshNode);  // may be nullptr for empty meshes
    }

    // Bone visibility gates. A gated bone (geosetAnimationId set) follows the
    // geoset animation of its geoset — in every shipped model the id is the
    // GEOA of the bone's geosetId. Blizzard's geosetId cannot be re-derived
    // from the skin (heropaladin's head bone names geoset 0 while it skins
    // several), so the gating geoset is kept as the handle of its mesh node
    // and the exporter resolves it back to the final geoset.
    for (const auto& bone : irModel.bones) {
        if (bone.geosetAnimationIndex < 0 ||
            bone.geosetAnimationIndex >= static_cast<int32_t>(irModel.geosetAnims.size()))
            continue;
        const int32_t meshIdx = irModel.geosetAnims[bone.geosetAnimationIndex].meshIndex;
        if (meshIdx < 0 || meshIdx >= static_cast<int32_t>(meshNodes.size()) || !meshNodes[meshIdx])
            continue;
        if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(nodeMap.size()) ||
            !nodeMap[bone.nodeIndex])
            continue;
        nodeMap[bone.nodeIndex]->SetUserPropInt(_T("Wc3VisibilityGeoset"),
                                                static_cast<int>(meshNodes[meshIdx]->GetHandle()));
    }

    // NOTE: Skin modifier application is deferred to AFTER animation key
    // writing (step 12b) so that AddBoneEx captures the post-controller
    // bone TMs.  In MaxScript this happens automatically because the Skin
    // modifier evaluates lazily on the first viewport refresh, which occurs
    // after all controllers have been replaced.

    // Extract model directory for texture path resolution
    std::wstring modelDir;
    {
        std::wstring fullPath(name);
        auto lastSep = fullPath.find_last_of(L"\\/");
        if (lastSep != std::wstring::npos)
            modelDir = fullPath.substr(0, lastSep + 1);
    }

    // ── Scene art tier ──────────────────────────────────────────────────
    // Which of 3.0.0's CASC overlays this model's bare paths resolve through.
    // The overlay the model browser pulled it out of is certain; failing
    // that, the MDX version bounds the tier and the shaders pick inside it
    // (wdx_scene_art_tier.h). Stored on the scene at step 14b so the preview
    // and the texture browser read the chain the textures were resolved with.
    wdx::scene::ArtTier artTier =
        wdx::scene::ArtTierFromOverlay(mdx_scene::detectModChain(modelDir));
    if (artTier == wdx::scene::ArtTier::Auto) {
        bool hasHdLayers = false;
        for (const auto& mat : irModel.materials) {
            for (const auto& layer : mat.layers)
                hasHdLayers = hasHdLayers || mdx_scene::isHdLayer(mat, layer);
        }
        artTier = wdx::scene::InferArtTier(opts.detectedVersion, hasHdLayers);
    }

    // ── TextureResolver ─────────────────────────────────────────────────
    // Single resolver instance owns the CASC + MPQ archive handles for the
    // import session and centralises all alias/path logic (see
    // texture_resolver.cpp). Builders just call resolver->Resolve(relPath);
    // the resolver decides whether to read from disk or extract from an
    // archive, picks the right alias set (textures vs models vs pkb/pkfx),
    // and writes archive bytes into modelDir with the actual extension.
    std::optional<mdx_scene::TextureResolver> resolverOpt;
    if (opts.core.importTextures) {
        resolverOpt.emplace(modelDir, opts.cascDirectory, opts.mpqDirectory,
                            opts.mpqArchives, artTier);
    }
    mdx_scene::TextureResolver* resolver = resolverOpt ? &*resolverOpt : nullptr;

    // Pre-resolve all textures and PE1 model files (including CASC/MPQ
    // extraction) so every builder can find them on disk without needing
    // the archive handles. PE1 model files are parsed recursively to
    // extract their textures and any nested PE1 model references (with
    // cycle detection).
    dialog.step(ImportStep::Textures);
    if (resolver) {
        // Resolve all textures from the main model.
        size_t texDone = 0;
        for (const auto& irTex : irModel.textures) {
            dialog.items(texDone++, irModel.textures.size());
            if (!irTex.filePath.empty()) {
                std::wstring wpath(irTex.filePath.begin(), irTex.filePath.end());
                resolver->Resolve(wpath);
            }
        }

        // Recursively resolve PE1 model files and their textures. PE1
        // particles spawn sub-models which may have their own textures
        // and their own PE1 emitters (forming a tree). The resolver's
        // built-in `.mdx`/`.mdl` alias set means we don't need an
        // ad-hoc extension swap dance here.
        {
            namespace fs = std::filesystem;

            auto resolveModelFull = [&](const std::wstring& relPath) -> std::wstring {
                std::wstring r = resolver->Resolve(relPath);
                std::error_code ec;
                return (!r.empty() && fs::exists(r, ec)) ? r : std::wstring{};
            };

            std::set<std::string> visited;
            std::vector<std::string> pendingModels;

            // Seed with PE1 model paths
            for (const auto& pe : irModel.particleEmitters) {
                if (pe.variant == 1 && !pe.modelPath.empty())
                    pendingModels.push_back(pe.modelPath);
            }
            // Seed with attachment model paths
            for (const auto& att : irModel.attachments) {
                if (!att.path.empty())
                    pendingModels.push_back(att.path);
            }

            while (!pendingModels.empty()) {
                std::string mdxRelPath = pendingModels.back();
                pendingModels.pop_back();

                // Normalize for cycle detection
                std::string normalized = mdxRelPath;
                for (char& c : normalized)
                    if (c == '/') c = '\\';
                std::transform(normalized.begin(), normalized.end(),
                               normalized.begin(), ::tolower);
                // Strip extension for cycle check (Foo.mdl == Foo.mdx)
                auto dotPos = normalized.rfind('.');
                if (dotPos != std::string::npos) normalized.resize(dotPos);
                if (visited.count(normalized)) continue;
                visited.insert(normalized);

                std::wstring wRelPath(mdxRelPath.begin(), mdxRelPath.end());
                std::wstring resolvedModel = resolveModelFull(wRelPath);
                if (resolvedModel.empty()) continue;

                try {
                    std::ifstream ifs(resolvedModel, std::ios::binary | std::ios::ate);
                    if (!ifs) continue;
                    auto sz = ifs.tellg();
                    ifs.seekg(0);
                    std::vector<uint8_t> buf(static_cast<size_t>(sz));
                    ifs.read(reinterpret_cast<char*>(buf.data()), sz);
                    ifs.close();

                    auto fmt = whiteout::mdx::MDLXFormat::MDX;
                    {
                        std::wstring ext = fs::path(resolvedModel).extension().wstring();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                        if (ext == L".mdl") fmt = whiteout::mdx::MDLXFormat::MDL;
                    }

                    // Child model's directory for resolving its own textures
                    std::wstring childDir = fs::path(resolvedModel).parent_path().wstring();
                    if (!childDir.empty() && childDir.back() != L'\\' && childDir.back() != L'/')
                        childDir += L'\\';

                    whiteout::mdx::Parser subParser;
                    auto subModel = subParser.parse(
                        std::span<const uint8_t>(buf.data(), buf.size()), fmt);

                    for (const auto& tex : subModel.textures) {
                        if (!tex.fileName.empty()) {
                            std::wstring wTexPath(tex.fileName.begin(), tex.fileName.end());
                            // The resolver was built with `modelDir` as
                            // its base; for a child-model texture we
                            // probe under the child's directory first
                            // (matches PE1 lookup convention), then let
                            // the resolver retry with its configured
                            // base. The free-function fallback handles
                            // the child-dir local probe without needing
                            // a second resolver instance.
                            std::error_code ec;
                            auto childHit = mdx_scene::resolveTexturePath(childDir, wTexPath);
                            if (childHit.empty() || !fs::exists(childHit, ec))
                                resolver->Resolve(wTexPath);
                        }
                    }
                    for (const auto& subPE : subModel.particleEmitters) {
                        if (!subPE.spawnModelFileName.empty())
                            pendingModels.push_back(subPE.spawnModelFileName);
                    }
                } catch (...) {}
            }
        }

        // NOTE: Storages stay open here — closed at the very end of DoImport
        // so material/particle builders can still hit the archives.
    }

    // 10. Build materials and assign to meshes
    dialog.step(ImportStep::Materials);
    std::vector<Mtl*> materials;
    if (opts.core.importMaterials) {
        mdx_scene::Wc3MaterialBuilder matBuilder;
        materials = matBuilder.buildMaterials(
            irModel, opts.core.importTextures, modelDir, resolver, gi, reporter);

        for (size_t mi = 0; mi < irModel.meshes.size(); ++mi) {
            int32_t matIdx = irModel.meshes[mi].materialIndex;
            if (meshNodes[mi] && matIdx >= 0 && matIdx < static_cast<int32_t>(materials.size()))
                meshNodes[mi]->SetMtl(materials[matIdx]);
        }

        // 10b. Trigger Wc3Material delegate viewport update
        // C++ paramblock SetValue does NOT fire scripted plugin "on X set" handlers.
    }

    // 11. Build format-specific scene objects (scripted plugins)
    dialog.step(ImportStep::Objects);
    std::vector<mdx_scene::Wc3CameraBuilder::CameraNodePair> cameraPairs;
    if (opts.core.importObjects) {
        if (opts.core.importLights) {
            mdx_scene::Wc3LightBuilder lightBuilder;
            lightBuilder.buildLights(irModel, nodeMap, gi, reporter);
        }
        if (opts.core.importAttachments) {
            mdx_scene::Wc3AttachmentBuilder attBuilder;
            attBuilder.buildAttachments(irModel, nodeMap, modelDir, gi, reporter);
        }
        if (opts.core.importParticleEmitters1) {
            mdx_scene::Wc3Particle1Builder pe1Builder;
            pe1Builder.buildParticles(irModel, nodeMap, modelDir, gi, reporter);
        }
        if (opts.core.importParticleEmitters2) {
            mdx_scene::Wc3Particle2Builder pe2Builder;
            pe2Builder.buildParticles(irModel, nodeMap, modelDir, resolver, gi, reporter);
        }
        if (opts.core.importRibbonEmitters) {
            mdx_scene::Wc3RibbonBuilder ribBuilder;
            ribBuilder.buildRibbons(irModel, nodeMap, materials, gi, reporter);
        }
        if (opts.core.importEventObjects) {
            mdx_scene::Wc3EventBuilder evtBuilder;
            evtBuilder.buildEvents(irModel, nodeMap, gi, reporter);
        }
        if (opts.core.importCollisionShapes) {
            mdx_scene::Wc3CollisionBuilder colBuilder;
            colBuilder.buildCollisions(irModel, nodeMap, gi, reporter);
        }

        // Corn / FaceFX. The MDX format started shipping CORN / FAFX chunks in
        // v1200 (Reforged), but in practice some Reforged exporters keep the
        // version field at 1000 / 1100 while still writing the chunks — so
        // don't gate on detectedVersion. Run the builder whenever the chunks
        // were actually parsed; it's a no-op when nothing matches the variant.
        {
            std::ostringstream ss;
            ss << "[Popcorn-gate] importObjects=" << (opts.core.importObjects ? 1 : 0)
               << " importCornEmitters=" << (opts.importCornEmitters ? 1 : 0)
               << " detectedVersion=" << opts.detectedVersion
               << " mdxModel.cornEmitters=" << mdxModel.cornEmitters.size()
               << " irModel.particleEmitters=" << irModel.particleEmitters.size();
            mdx_scene::PopcornDiagLog(ss.str());
        }
        if (opts.importCornEmitters && !mdxModel.cornEmitters.empty()) {
            mdx_scene::Wc3PopcornBuilder cornBuilder;
            cornBuilder.buildPopcorn(irModel, nodeMap, modelDir, resolver, gi, reporter);
        } else {
            mdx_scene::PopcornDiagLog("[Popcorn-gate] skipped (gate false)");
        }
        if (opts.importFaceFX && opts.detectedVersion >= 1200) {
            mdx_scene::Wc3FaceFxBuilder ffxBuilder;
            ffxBuilder.buildFaceFX(irModel, nodeMap, gi, reporter);
        }

        // Cameras
        if (opts.core.importCameras) {
            mdx_scene::Wc3CameraBuilder camBuilder;
            cameraPairs = camBuilder.buildCameras(irModel, nodeMap, gi, reporter);
        }

    }

    // 12. Apply skin modifiers BEFORE animation (MaxScript order)
    dialog.step(ImportStep::Skin);
    ILOG << "\n==== Skinning (before animation) ====\n";

    // DEBUG: dump bones RIGHT BEFORE skin is applied — these are the transforms
    // AddBoneEx will snapshot as bind pose.  If any bone's ObjectTM differs from
    // its NodeTM here, Skin will store a wrong bind pose and every frame will
    // look wrong relative to it.
    dumpBoneState(boneNodes, "pre-skin");

    if (opts.core.importSkinning) {
        for (size_t mi = 0; mi < irModel.meshes.size(); ++mi) {
            dialog.items(mi, irModel.meshes.size());
            if (meshNodes[mi])
                applySkinModifier(meshNodes[mi], irModel.meshes[mi], nodeMap, gi);
        }
    }
    ILOG << "==== end skinning ====\n";

    // 12b. Vertex color modifiers (AFTER skinning, so they sit above Skin in the stack)
    {
        mdx_scene::Wc3VertexColorBuilder vcBuilder;
        vcBuilder.applyVertexColors(irModel, meshNodes, gi, reporter);
    }

    // 13. Insert animation keyframes (AFTER skinning)
    // 
    // SuspendAnimate()/AnimateOn() is required for SetValue() to create keys.
    // SDK docs: "the animate button should be turned on"
    //
    dialog.step(ImportStep::Animations);
    ILOG << "\n==== Animation Keys ====\n";
    if (opts.core.importAnimations) {
        SuspendAnimate();
        AnimateOn();  // ← CRITICAL: SetValue needs this to create keys

        const Quat stubRot(0.0f, 0.0f, 0.0f, 1.0f);

        // Read local positions from actual Max node transforms after hierarchy
        // setup.  This matches MaxScript's "in coordsys parent subPos = obj.pos"
        // and correctly handles v1200 where PIVT may differ from BPOS.
        std::vector<Point3> nodeLocalPos(irModel.nodes.size(), Point3(0,0,0));
        for (size_t i = 0; i < irModel.nodes.size(); ++i) {
            INode* nd = (i < nodeMap.size()) ? nodeMap[i] : nullptr;
            if (!nd) continue;

            Matrix3 worldTM = nd->GetNodeTM(0);
            INode* par = nd->GetParentNode();
            if (par && !par->IsRootNode()) {
                Matrix3 parentTM = par->GetNodeTM(0);
                Matrix3 localTM = worldTM * Inverse(parentTM);
                nodeLocalPos[i] = localTM.GetTrans();
            } else {
                nodeLocalPos[i] = worldTM.GetTrans();
            }
        }

        size_t animsDone = 0;
        for (const auto& na : irModel.nodeAnimations) {
            dialog.items(animsDone++, irModel.nodeAnimations.size());
            if (na.nodeIndex < 0 || na.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                continue;
            INode* node = nodeMap[na.nodeIndex];
            if (!node) continue;

            Point3 localPos =(na.nodeIndex < static_cast<int32_t>(nodeLocalPos.size()))
                ? nodeLocalPos[na.nodeIndex] : Point3(0,0,0);

            ILOG << "  ANIM node[" << na.nodeIndex << "]"
                 << " localPos=(" << localPos.x << "," << localPos.y << "," << localPos.z
                 << ") KGTR=" << na.translation.keys.size()
                 << " KGRT=" << na.rotation.keys.size()
                 << " KGSC=" << na.scale.keys.size() << "\n";

            // ── DEBUG: Bone_Main rotation track inspection ──
            // Logs the raw rotation keys before any remapping so we can
            // verify against MDX values. Triggered for any node whose name
            // contains "Bone_Main" to keep logs focused.
            {
                std::string nodeName;
                if (na.nodeIndex >= 0 && na.nodeIndex < static_cast<int32_t>(irModel.nodes.size()))
                    nodeName = irModel.nodes[na.nodeIndex].name;
                if (nodeName.find("Bone_Main") != std::string::npos) {
                    ILOG << "  *** DEBUG Bone_Main ***\n";
                    ILOG << "    node name: '" << nodeName << "'\n";
                    ILOG << "    rot track: keys=" << na.rotation.keys.size()
                         << " gseq=" << na.rotation.globalSequenceIndex << "\n";
                    for (size_t i = 0; i < na.rotation.keys.size() && i < 5; ++i) {
                        const auto& k = na.rotation.keys[i];
                        ILOG << "    rot key[" << i << "] time=" << k.time
                             << " quat=(" << k.value.x << "," << k.value.y
                             << "," << k.value.z << "," << k.value.w << ")\n";
                    }
                    ILOG << "    pos track: keys=" << na.translation.keys.size()
                         << " gseq=" << na.translation.globalSequenceIndex << "\n";
                    for (size_t i = 0; i < na.translation.keys.size() && i < 5; ++i) {
                        const auto& k = na.translation.keys[i];
                        ILOG << "    pos key[" << i << "] time=" << k.time
                             << " vec=(" << k.value.x << "," << k.value.y << "," << k.value.z << ")\n";
                    }
                    ILOG << "    localPos=(" << localPos.x << "," << localPos.y << "," << localPos.z << ")\n";
                    ILOG << "    stubRot=(" << stubRot.x << "," << stubRot.y
                         << "," << stubRot.z << "," << stubRot.w << ")\n";
                    if (!irModel.globalSequenceDurations.empty()
                        && na.rotation.globalSequenceIndex >= 0
                        && na.rotation.globalSequenceIndex
                             < static_cast<int32_t>(irModel.globalSequenceDurations.size()))
                    {
                        ILOG << "    gseq[" << na.rotation.globalSequenceIndex
                             << "] duration="
                             << irModel.globalSequenceDurations[na.rotation.globalSequenceIndex]
                             << "\n";
                    }
                }
            }

            // Rotation: write raw keys (no offset needed)
            if (opts.core.importRotation && !na.rotation.empty()) {
                ir::QuatTrack rotTrack = na.rotation;
                insertSequenceBoundaryKeys(rotTrack, irModel.sequences);
                bakeCurvedRotation(rotTrack, irModel.sequences);
                insertRotationKeys(node, rotTrack, stubRot);
            }

            // Translation: add localPos to every key value + stub
            if (opts.core.importTranslation && !na.translation.empty()) {
                ir::Vec3Track adjustedTrack = na.translation;
                offsetTrack(adjustedTrack, localPos);
                insertSequenceBoundaryKeys(adjustedTrack, irModel.sequences);
                insertTranslationKeys(node, adjustedTrack, localPos);
            }

            // Scale: write raw keys
            if (opts.core.importScale && !na.scale.empty()) {
                ir::Vec3Track scaleTrack = na.scale;
                insertSequenceBoundaryKeys(scaleTrack, irModel.sequences);
                insertScaleKeys(node, scaleTrack, Point3(1.0f, 1.0f, 1.0f));
            }
        }

        // Camera animations: KCTR (camera pos), KTTR (target pos), KCRL (roll),
        // KCVS (visibility), IDUF / ELAF / PTSF (depth of field)
        if (opts.core.importCameras && !cameraPairs.empty()) {
            ILOG << "\n==== Camera Animations ====\n";
            for (size_t ci = 0; ci < irModel.cameras.size() && ci < cameraPairs.size(); ++ci) {
                const auto& irCam = irModel.cameras[ci];
                const auto& pair = cameraPairs[ci];
                if (!pair.cameraNode) continue;

                // KCTR: camera position animation
                // MDX camera Translation tracks are offsets from rest Position
                // (same convention as node Translation tracks relative to pivot).
                // Convert to absolute positions for Max by adding the rest position.
                if (opts.core.importTranslation && irCam.positionTrackIndex >= 0 &&
                    irCam.positionTrackIndex < static_cast<int32_t>(irModel.vec3Tracks.size())) {
                    const auto& track = irModel.vec3Tracks[irCam.positionTrackIndex];
                    if (!track.empty()) {
                        ir::Vec3Track adjustedTrack = track;
                        Point3 startPos = irCam.position;
                        offsetTrack(adjustedTrack, startPos);
                        insertSequenceBoundaryKeys(adjustedTrack, irModel.sequences);
                        insertTranslationKeys(pair.cameraNode, adjustedTrack, startPos);
                        ILOG << "  cam[" << ci << "] KCTR keys=" << track.keys.size() << "\n";
                    }
                }

                // KTTR: target position animation (same offset convention)
                if (opts.core.importTranslation && pair.targetNode &&
                    irCam.targetPositionTrackIndex >= 0 &&
                    irCam.targetPositionTrackIndex < static_cast<int32_t>(irModel.vec3Tracks.size())) {
                    const auto& track = irModel.vec3Tracks[irCam.targetPositionTrackIndex];
                    if (!track.empty()) {
                        ir::Vec3Track adjustedTrack = track;
                        Point3 startPos = irCam.targetPosition;
                        offsetTrack(adjustedTrack, startPos);
                        insertSequenceBoundaryKeys(adjustedTrack, irModel.sequences);
                        insertTranslationKeys(pair.targetNode, adjustedTrack, startPos);
                        ILOG << "  cam[" << ci << "] KTTR keys=" << track.keys.size() << "\n";
                    }
                }

                // KCRL: roll animation (applied to camera object's roll_angle sub-anim)
                if (opts.core.importRotation && irCam.rotationTrackIndex >= 0 &&
                    irCam.rotationTrackIndex < static_cast<int32_t>(irModel.floatTracks.size())) {
                    const auto& track = irModel.floatTracks[irCam.rotationTrackIndex];
                    if (!track.empty()) {
                        Control* rollCtrl = createFloatController(track);
                        if (rollCtrl) {
                            // Roll lives on the LookAt TM controller, not on
                            // the camera object (a Targetcamera has no
                            // `.rotation` at all). The controller is set on
                            // the LookAt itself: AssignController goes on the
                            // PARENT with the sub-anim index, and calling it on
                            // the roll controller (as this used to) was a
                            // silent no-op, so no KCRL ever reached Max.
                            Control* tmCtrl = pair.cameraNode->GetTMController();
                            const BOOL assigned = tmCtrl && tmCtrl->SetRollController(rollCtrl);
                            ILOG << "  cam[" << ci << "] KCRL keys=" << track.keys.size()
                                 << (assigned ? "" : " (not assigned: no LookAt controller)") << "\n";
                            if (!assigned)
                                rollCtrl->DeleteThis();
                        }
                    }
                }

                // KCVS: the camera node's visibility. The 3.0.0 client skips
                // a camera while this is 0.
                if (opts.core.importVisibility && irCam.visibilityTrackIndex >= 0 &&
                    irCam.visibilityTrackIndex < static_cast<int32_t>(irModel.floatTracks.size())) {
                    const auto& track = irModel.floatTracks[irCam.visibilityTrackIndex];
                    insertVisibilityKeys(pair.cameraNode, track, irModel.sequences);
                    ILOG << "  cam[" << ci << "] KCVS keys=" << track.keys.size() << "\n";
                }

                // IDUF / ELAF / PTSF onto the Physical Camera the builder made
                // (no-op on a Target Camera, which has none of these params).
                Object* camObj = opts.core.importParameterAnimations
                                     ? pair.cameraNode->GetObjectRef() : nullptr;
                if (camObj) {
                    auto animateCamParam = [&](const wchar_t* name, int32_t idx, const char* tag) {
                        if (idx < 0 || idx >= static_cast<int32_t>(irModel.floatTracks.size()))
                            return;
                        auto p = findPBParam(camObj, name);
                        if (!p) return;
                        const auto& track = irModel.floatTracks[idx];
                        if (Control* ctrl = createFloatController(track)) {
                            p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
                            ILOG << "  cam[" << ci << "] " << tag << " keys=" << track.keys.size() << "\n";
                        }
                    };
                    animateCamParam(L"focus_distance",  irCam.focusDistanceTrackIndex, "IDUF");
                    animateCamParam(L"focal_length_mm", irCam.focalLengthTrackIndex,   "ELAF");
                    animateCamParam(L"f_number",        irCam.fStopTrackIndex,         "PTSF");
                }
            }
            ILOG << "==== end camera animations ====\n";
            ILOG.flush();
        }

        // Material animations: KMTA (layer alpha) + texture animation (KTAT/KTAS)
        if (opts.core.importMaterials && !materials.empty()) {

            // ── Disable unused composite slots ──
            // (Bitmap sharing is now done in buildMaterials per (texIdx, taIdx) —
            // assigning diffuseMap here via MaxScript would trigger on-set
            // handlers that wipe animation controllers on the shared bitmap.)
            ExecuteMAXScriptScript(
                _M("for m in sceneMaterials do ("
                     "local isComp = try(m.materialList != undefined)catch(false);"
                     "if isComp do ("
                       "local sc = try(m.materialList.count)catch(0);"
                       "local usedCount = 0;"
                       "for i = 1 to sc do ("
                         "if try(m.materialList[i] != undefined)catch(false) then usedCount = i"
                       ");"
                       "for i = 1 to sc do ("
                         "if i > usedCount then try(m.mapEnables[i] = false)catch()"
                       ")"
                     ")"
                   ")"),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                MAXScript::ScriptSource::NonEmbedded,
#endif
                TRUE, nullptr);

            ILOG << "\n==== Material Animations ====\n";

            std::set<BitmapTex*> processedBitmaps;  // track shared bitmaps

            for (size_t mi = 0; mi < irModel.materials.size() && mi < materials.size(); ++mi) {
                Mtl* mtl = materials[mi];
                if (!mtl) continue;
                const auto& irMat = irModel.materials[mi];

                // Get the Wc3Material for a given layer index.
                // Multi-layer materials use CompositeMaterial with Wc3Material children.
                // Single-layer: the material IS the Wc3Material directly.
                auto getMtlForLayer = [&](int li) -> Mtl* {
                    if (irMat.layers.size() <= 1)
                        return (li == 0) ? mtl : nullptr;
                    // CompositeMaterial: sub-materials accessed via GetSubMtl
                    if (li < mtl->NumSubMtls())
                        return mtl->GetSubMtl(li);
                    return nullptr;
                };

                ILOG << "  mat[" << mi << "] layers=" << irMat.layers.size()
                     << " mtl=" << (mtl ? "yes" : "null")
                     << " isMulti=" << (mtl && mtl->IsMultiMtl() ? "yes" : "no") << "\n";

                for (size_t li = 0; li < irMat.layers.size(); ++li) {
                    const auto& layer = irMat.layers[li];
                    Mtl* layerMtl = getMtlForLayer((int)li);

                    ILOG << "    layer[" << li << "] alphaTrackIdx=" << layer.alphaTrackIndex
                         << " texAnimIdx=" << layer.textureAnimationIndex
                         << " staticAlpha=" << layer.alpha
                         << " layerMtl=" << (layerMtl ? "yes" : "null") << "\n";

                    if (!layerMtl) continue;
                    auto* ref = dynamic_cast<ReferenceTarget*>(layerMtl);
                    if (!ref) continue;

                    // KMTA: animate "opacity" on Wc3Material (0-100 range)
                    // The delegate.opacity sync is done in the late MaxScript block
                    // (after all animations are set up, via sub.opacity.controller).
                    if (layer.alphaTrackIndex >= 0 &&
                        layer.alphaTrackIndex < (int32_t)irModel.floatTracks.size()) {
                        const auto& srcTrack = irModel.floatTracks[layer.alphaTrackIndex];
                        if (!srcTrack.empty()) {
                            // Wc3Material opacity is 0-100 range
                            ir::FloatTrack scaled100 = srcTrack;
                            for (auto& k : scaled100.keys) {
                                k.value *= 100.0f;
                                if (k.hasTangents) {
                                    k.inTangent *= 100.0f;
                                    k.outTangent *= 100.0f;
                                }
                            }
                            Control* ctrl100 = createFloatController(scaled100);
                            if (ctrl100) {
                                // Set on Wc3Material "opacity" paramblock param
                                auto p = findPBParam(ref, L"opacity");
                                if (p) p.pb->SetControllerByID(p.id, 0, ctrl100, FALSE);
                            }

                            // Set opacityCtrl dropdown to match interpolation type
                            // Dropdown: 1=None, 2=Linear, 3=Bezier, 4=Hermite
                            {
                                int ctrlType = 1; // None (DontInterp)
                                switch (srcTrack.interpolation) {
                                case ir::InterpolationType::None:    ctrlType = 1; break;
                                case ir::InterpolationType::Linear:  ctrlType = 2; break;
                                case ir::InterpolationType::Hermite: ctrlType = 3; break; // held on bezier handles
                                default:                             ctrlType = 3; break; // Bezier
                                }
                                auto pCtrl = findPBParam(ref, L"opacityCtrl");
                                if (pCtrl) pCtrl.pb->SetValue(pCtrl.id, 0, ctrlType);
                            }

                            ILOG << "  mat[" << mi << "] layer[" << li << "] KMTA keys="
                                 << srcTrack.keys.size() << "\n";
                        }
                    }

                    // Exact KMTF next to the IFL preview the material builder
                    // made: the textures the flipbook shows, in first-use
                    // order, and the animated index into them. Written as
                    // "replaceableId|flags|MDX path" (see Wc3Material.ms).
                    if (layer.textureIdTrackIndex >= 0 &&
                        layer.textureIdTrackIndex < static_cast<int32_t>(irModel.intTracks.size())) {
                        const auto& texTrack = irModel.intTracks[layer.textureIdTrackIndex];
                        std::vector<int32_t> order;
                        ir::FloatTrack frames;
                        frames.interpolation = texTrack.interpolation;
                        frames.globalSequenceIndex = texTrack.globalSequenceIndex;
                        for (const auto& k : texTrack.keys) {
                            auto it = std::find(order.begin(), order.end(), k.value);
                            if (it == order.end()) { order.push_back(k.value); it = order.end() - 1; }
                            ir::Keyframe<float> fk;
                            fk.time = k.time;
                            fk.value = static_cast<float>(it - order.begin());
                            frames.keys.push_back(fk);
                        }
                        auto texList = findPBParam(ref, L"flipbookTextures");
                        auto frameParam = findPBParam(ref, L"flipbookFrame");
                        if (texList && frameParam && !frames.keys.empty()) {
                            texList.pb->SetCount(texList.id, static_cast<int>(order.size()));
                            for (size_t oi = 0; oi < order.size(); ++oi) {
                                std::string entry = "0|0|";
                                if (order[oi] >= 0 && order[oi] < static_cast<int32_t>(irModel.textures.size())) {
                                    const auto& tex = irModel.textures[order[oi]];
                                    entry = std::to_string(tex.replaceableId) + "|" +
                                            std::to_string((tex.wrapU ? 1 : 0) | (tex.wrapV ? 2 : 0)) + "|" +
                                            tex.filePath;
                                }
                                std::wstring wentry(
                                    MultiByteToWideChar(CP_UTF8, 0, entry.c_str(), -1, nullptr, 0), L'\0');
                                MultiByteToWideChar(CP_UTF8, 0, entry.c_str(), -1, wentry.data(),
                                                    static_cast<int>(wentry.size()));
                                if (!wentry.empty()) wentry.pop_back();  // the terminator
                                texList.pb->SetValue(texList.id, 0, wentry.c_str(), static_cast<int>(oi));
                            }
                            // Integer steps: anything but a hold reads as Linear.
                            if (frames.interpolation != ir::InterpolationType::None)
                                frames.interpolation = ir::InterpolationType::Linear;
                            if (Control* ctrl = createFloatController(frames))
                                frameParam.pb->SetControllerByID(frameParam.id, 0, ctrl, FALSE);
                            ILOG << "  mat[" << mi << "] layer[" << li << "] KMTF exact: "
                                 << order.size() << " textures, " << frames.keys.size() << " keys\n";
                        }
                    }

                    // KMTE / KFCA / KFTC / KFC3 onto the Reforged params. The
                    // Wc3Material keeps the fresnel colour as three floats, so
                    // KFC3 is split per channel.
                    {
                        auto animateMtlParam = [&](const wchar_t* name, const ir::FloatTrack& track) {
                            if (track.empty()) return;
                            auto p = findPBParam(ref, name);
                            if (!p) return;
                            if (Control* ctrl = createFloatController(track))
                                p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
                        };
                        auto floatTrackAt = [&](int32_t idx) -> const ir::FloatTrack* {
                            return (idx >= 0 && idx < static_cast<int32_t>(irModel.floatTracks.size()))
                                ? &irModel.floatTracks[idx] : nullptr;
                        };
                        if (auto* t = floatTrackAt(layer.emissiveGainTrackIndex))
                            animateMtlParam(L"emissiveGain", *t);
                        if (auto* t = floatTrackAt(layer.fresnelAlphaTrackIndex))
                            animateMtlParam(L"fresnelOpacity", *t);
                        if (auto* t = floatTrackAt(layer.fresnelTeamColorTrackIndex))
                            animateMtlParam(L"fresnelTeamCol", *t);
                        if (layer.fresnelColorTrackIndex >= 0 &&
                            layer.fresnelColorTrackIndex < static_cast<int32_t>(irModel.colorTracks.size())) {
                            const auto& color = irModel.colorTracks[layer.fresnelColorTrackIndex];
                            static const wchar_t* const kChannel[3] = { L"fresnelR", L"fresnelG", L"fresnelB" };
                            for (int c = 0; c < 3; ++c) {
                                ir::FloatTrack channel;
                                channel.interpolation = color.interpolation;
                                channel.globalSequenceIndex = color.globalSequenceIndex;
                                for (const auto& k : color.keys) {
                                    ir::Keyframe<float> fk;
                                    fk.time = k.time;
                                    fk.value = k.value[c];
                                    fk.inTangent = k.inTangent[c];
                                    fk.outTangent = k.outTangent[c];
                                    fk.hasTangents = k.hasTangents;
                                    channel.keys.push_back(fk);
                                }
                                animateMtlParam(kChannel[c], channel);
                            }
                        }
                        if (layer.emissiveGainTrackIndex >= 0 || layer.fresnelColorTrackIndex >= 0 ||
                            layer.fresnelAlphaTrackIndex >= 0 || layer.fresnelTeamColorTrackIndex >= 0)
                            ILOG << "  mat[" << mi << "] layer[" << li << "] KMTE=" << layer.emissiveGainTrackIndex
                                 << " KFC3=" << layer.fresnelColorTrackIndex
                                 << " KFCA=" << layer.fresnelAlphaTrackIndex
                                 << " KFTC=" << layer.fresnelTeamColorTrackIndex << "\n";
                    }

                    // Texture animation (KTAT translation, KTAR rotation, KTAS scale)
                    // TXAN lives on the TEXTURE now, not on the material.
                    // Controllers are written to BOTH texture-side targets:
                    //   1. BitmapTex StdUVGen         (direct viewport animation)
                    //   2. Wc3Bitmap anim_* params    (exporter round-trip; the
                    //      scripted plugin shares these controllers with its
                    //      delegate's coords)
                    // Layers that share the same bitmap instance share the
                    // animation by construction — the builder pre-clones a
                    // bitmap when two layers use DIFFERENT texture animations
                    // on the same texture — so each bitmap is processed once.
                    if (layer.textureAnimationIndex >= 0 &&
                        layer.textureAnimationIndex < (int32_t)irModel.textureAnimations.size()) {
                        const auto& ta = irModel.textureAnimations[layer.textureAnimationIndex];

                        // Get the diffuse texmap: Wc3Bitmap wrapper (preferred)
                        // or plain BitmapTex (fallback when the scripted plugin
                        // is unavailable — UVGen animation only, W offset lost).
                        Texmap* diffuseTex = getDiffuseTexmap(ref);
                        BitmapTex* diffuseBmp = unwrapBitmapTex(diffuseTex);
                        StdUVGen* uvGen = diffuseBmp ? diffuseBmp->GetUVGen() : nullptr;
                        ReferenceTarget* texAnimRef =
                            (diffuseTex && diffuseTex->ClassID() == mdx_ids::WC3_BITMAP)
                                ? dynamic_cast<ReferenceTarget*>(diffuseTex)
                                : nullptr;

                        // Check if this bitmap was already processed (shared texture)
                        bool bitmapAlreadyProcessed = false;
                        if (diffuseBmp) {
                            if (processedBitmaps.count(diffuseBmp))
                                bitmapAlreadyProcessed = true;
                            else
                                processedBitmaps.insert(diffuseBmp);
                        }

                        // Split a Vec3 track into a per-channel float track
                        auto splitVec3Channel = [](const ir::Vec3Track& v3, int ch) -> ir::FloatTrack {
                            ir::FloatTrack ft;
                            ft.interpolation = v3.interpolation;
                            ft.globalSequenceIndex = v3.globalSequenceIndex;
                            ft.keys.reserve(v3.keys.size());
                            for (const auto& k : v3.keys) {
                                ir::Keyframe<float> fk;
                                fk.time = k.time;
                                fk.value = (ch == 0) ? k.value.x : (ch == 1) ? k.value.y : k.value.z;
                                fk.hasTangents = k.hasTangents;
                                if (k.hasTangents) {
                                    fk.inTangent = (ch == 0) ? k.inTangent.x : (ch == 1) ? k.inTangent.y : k.inTangent.z;
                                    fk.outTangent = (ch == 0) ? k.outTangent.x : (ch == 1) ? k.outTangent.y : k.outTangent.z;
                                }
                                ft.keys.push_back(fk);
                            }
                            return ft;
                        };

                        // Assign a controller to a named anim_* param on the
                        // Wc3Bitmap wrapper (no-op when there is no wrapper).
                        auto setTexAnimParam = [&](const wchar_t* name, Control* ctrl) {
                            if (!texAnimRef || !ctrl) return;
                            auto p = findPBParam(texAnimRef, name);
                            if (p) p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
                        };

                        // KTAT: UV offset — after swizzle: ch1=-X→U_Offset, ch0=Y→V_Offset
                        // NeoDex applies the same transform during read (readMDXPosition).
                        // Result: U_Offset = -X_original, V_Offset = Y_original.
                        //
                        // Z channel: StdUVGen has NO W_Offset property (only W_Angle).
                        // For 2D bitmaps the W component is not renderable in Max — but we
                        // still preserve it on the Wc3Bitmap's anim_WOffset param so the
                        // exporter can round-trip the original MDX KTAT untouched.
                        if (!bitmapAlreadyProcessed &&
                            ta.translationTrackIndex >= 0 &&
                            ta.translationTrackIndex < (int32_t)irModel.vec3Tracks.size()) {
                            const auto& v3track = irModel.vec3Tracks[ta.translationTrackIndex];
                            if (!v3track.empty()) {
                                auto uTrack = splitVec3Channel(v3track, 1);  // Y → U_Offset
                                auto vTrack = splitVec3Channel(v3track, 0);  // X → V_Offset
                                auto wTrack = splitVec3Channel(v3track, 2);  // Z → anim_WOffset (round-trip only)
                                Control* uCtrl = createFloatController(uTrack);
                                Control* vCtrl = createFloatController(vTrack);
                                Control* wCtrl = createFloatController(wTrack);

                                // Write directly to BitmapTex StdUVGen (U/V only — no W_Offset exists)
                                if (uvGen && uCtrl)
                                    assignControllerToUVGen(uvGen, L"U_Offset", 0, uCtrl);
                                if (uvGen && vCtrl)
                                    assignControllerToUVGen(uvGen, L"V_Offset", 1, vCtrl);

                                // Also set on the Wc3Bitmap params (includes W for round-trip)
                                setTexAnimParam(L"anim_UOffset", uCtrl);
                                setTexAnimParam(L"anim_VOffset", vCtrl);
                                setTexAnimParam(L"anim_WOffset", wCtrl);

                                ILOG << "  mat[" << mi << "] layer[" << li << "] KTAT keys="
                                     << v3track.keys.size()
                                     << " (uvgen: " << (uvGen ? "yes" : "no")
                                     << ", wc3bitmap: " << (texAnimRef ? "yes" : "no") << ")\n";
                            }
                        }

                        // KTAR: UV rotation — quaternion → Z-euler angle → W_Angle (degrees)
                        if (!bitmapAlreadyProcessed &&
                            ta.rotationTrackIndex >= 0 &&
                            ta.rotationTrackIndex < (int32_t)irModel.quatTracks.size()) {
                            const auto& qtrack = irModel.quatTracks[ta.rotationTrackIndex];
                            if (!qtrack.empty()) {
                                auto quatToZAngle = [](const Quat& q) -> float {
                                    float ang[3];
                                    Quat mq = q;
                                    QuatToEuler(mq, ang);
                                    return -ang[2] * (180.0f / 3.14159265f);
                                };

                                ir::FloatTrack rotFloat;
                                rotFloat.interpolation = qtrack.interpolation;
                                rotFloat.globalSequenceIndex = qtrack.globalSequenceIndex;
                                rotFloat.keys.reserve(qtrack.keys.size());
                                for (const auto& k : qtrack.keys) {
                                    ir::Keyframe<float> fk;
                                    fk.time = k.time;
                                    fk.value = quatToZAngle(k.value);
                                    fk.hasTangents = k.hasTangents;
                                    if (k.hasTangents) {
                                        fk.inTangent = quatToZAngle(k.inTangent);
                                        fk.outTangent = quatToZAngle(k.outTangent);
                                    }
                                    rotFloat.keys.push_back(fk);
                                }

                                Control* wCtrl = createFloatController(rotFloat);

                                // Write directly to BitmapTex StdUVGen
                                if (uvGen && wCtrl)
                                    assignControllerToUVGen(uvGen, L"W_Angle", 6, wCtrl);

                                // Also set on the Wc3Bitmap param
                                setTexAnimParam(L"anim_WAngle", wCtrl);

                                ILOG << "  mat[" << mi << "] layer[" << li << "] KTAR keys="
                                     << qtrack.keys.size()
                                     << " (uvgen: " << (uvGen ? "yes" : "no")
                                     << ", wc3bitmap: " << (texAnimRef ? "yes" : "no") << ")\n";
                            }
                        }

                        // KTAS: UV scale — X → U_Tiling, Y → V_Tiling
                        if (!bitmapAlreadyProcessed &&
                            ta.scaleTrackIndex >= 0 &&
                            ta.scaleTrackIndex < (int32_t)irModel.vec3Tracks.size()) {
                            const auto& v3track = irModel.vec3Tracks[ta.scaleTrackIndex];
                            if (!v3track.empty()) {
                                auto uTrack = splitVec3Channel(v3track, 0);
                                auto vTrack = splitVec3Channel(v3track, 1);
                                Control* uCtrl = createFloatController(uTrack);
                                Control* vCtrl = createFloatController(vTrack);

                                // Write directly to BitmapTex StdUVGen
                                if (uvGen && uCtrl)
                                    assignControllerToUVGen(uvGen, L"U_Tiling", 2, uCtrl);
                                if (uvGen && vCtrl)
                                    assignControllerToUVGen(uvGen, L"V_Tiling", 3, vCtrl);

                                // Also set on the Wc3Bitmap params
                                setTexAnimParam(L"anim_UTiling", uCtrl);
                                setTexAnimParam(L"anim_VTiling", vCtrl);

                                ILOG << "  mat[" << mi << "] layer[" << li << "] KTAS keys="
                                     << v3track.keys.size()
                                     << " (uvgen: " << (uvGen ? "yes" : "no")
                                     << ", wc3bitmap: " << (texAnimRef ? "yes" : "no") << ")\n";
                            }
                        }

                        // Set uvCtrlType dropdown (on the Wc3Bitmap) to match
                        // interpolation type. 1=None, 2=Linear, 3=Bezier, 4=Hermite
                        // Determine from KTAT, KTAR, or KTAS (first one found)
                        if (!bitmapAlreadyProcessed && texAnimRef) {
                            ir::InterpolationType interpType = ir::InterpolationType::None;
                            bool found = false;
                            if (!found && ta.translationTrackIndex >= 0 &&
                                ta.translationTrackIndex < (int32_t)irModel.vec3Tracks.size()) {
                                const auto& t = irModel.vec3Tracks[ta.translationTrackIndex];
                                if (!t.empty()) { interpType = t.interpolation; found = true; }
                            }
                            if (!found && ta.rotationTrackIndex >= 0 &&
                                ta.rotationTrackIndex < (int32_t)irModel.quatTracks.size()) {
                                const auto& t = irModel.quatTracks[ta.rotationTrackIndex];
                                if (!t.empty()) { interpType = t.interpolation; found = true; }
                            }
                            if (!found && ta.scaleTrackIndex >= 0 &&
                                ta.scaleTrackIndex < (int32_t)irModel.vec3Tracks.size()) {
                                const auto& t = irModel.vec3Tracks[ta.scaleTrackIndex];
                                if (!t.empty()) { interpType = t.interpolation; found = true; }
                            }
                            if (found) {
                                // None must stay 1: Wc3Bitmap's uvCtrlType handler
                                // turns every U/V key #smooth for 3 (Bezier), which
                                // would undo the STEP keys of a DontInterp track.
                                int ctrlType = 1; // None
                                switch (interpType) {
                                case ir::InterpolationType::None:    ctrlType = 1; break;
                                case ir::InterpolationType::Linear:  ctrlType = 2; break;
                                case ir::InterpolationType::Hermite: ctrlType = 3; break; // held on bezier handles
                                default:                             ctrlType = 3; break; // Bezier
                                }
                                auto pUV = findPBParam(texAnimRef, L"uvCtrlType");
                                if (pUV) pUV.pb->SetValue(pUV.id, 0, ctrlType);
                            }
                        }
                    }
                }
            }
            // After animation setup, only touch what's safe to touch:
            //   - showInViewport: per-sub-material visibility
            //   - delegate.opacity sync: needed for composite sub-materials
            //     (on-set handlers don't fire reliably for nested Wc3Materials)
            //
            // We do NOT call applyFilterMode() / generateOpacity() here — those
            // trigger coords refreshes (opMap.coords.U_Tile = true) that wipe
            // animation controllers we just set up. The filter mode and opacity
            // map were already configured in C++ during material build.
            ExecuteMAXScriptScript(
                // Sync each Wc3Material's opacity to its StandardMaterial delegate so the
                // viewport renderer (which reads the delegate, not the wrapper) honors the
                // animation. Empirically verified against box_export_2.mdx with KMTA Bezier
                // alpha tracks: without this sync, the box stays at 100% opacity even when
                // sub.opacity drops to 0, because Max renders from delg.opacity which still
                // has its initial value.
                //
                // Subtlety: MaxScript's get/set on Wc3Material.opacity and Standardmaterial.
                // opacity both use the 0..100 UI scale, so the static value assignment
                // (sub.delegate.opacity = sub.opacity) is correct as-is. But when assigning
                // a CONTROLLER, Max treats the controller's stored values as the internal
                // 0..1 scale and re-multiplies by 100 for display, producing 10000.0 from
                // a 100.0 key. We therefore copy the controller and divide its values and
                // tangents by 100 before attaching to the delegate.
                _M("for m in sceneMaterials do ("
                     "local isComp = try(m.materialList != undefined)catch(false);"
                     "fn syncOpacity sub = ("
                       "if sub == undefined or (try(sub.delegate)catch(undefined)) == undefined then return false;"
                       "try(sub.delegate.opacity = sub.opacity)catch();"
                       "local aCtrl = try(sub.opacity.controller)catch(undefined);"
                       "if aCtrl != undefined and (try((classOf aCtrl) == bezier_float or (classOf aCtrl) == linear_float or (classOf aCtrl) == on_off)catch(false)) do ("
                         "try("
                           "local cc = copy aCtrl;"
                           "for i = 1 to cc.keys.count do ("
                             "cc.keys[i].value = cc.keys[i].value / 100.0;"
                             "try(cc.keys[i].inTangent  = cc.keys[i].inTangent  / 100.0)catch();"
                             "try(cc.keys[i].outTangent = cc.keys[i].outTangent / 100.0)catch()"
                           ");"
                           "setPropertyController sub.delegate \"opacity\" cc"
                         ")catch()"
                       ")"
                     ");"
                     "if isComp do ("
                       "local hasAdditive = false;"
                       "for i = 1 to m.materialList.count do ("
                         "local sub = try(m.materialList[i])catch(undefined);"
                         "if sub != undefined do ("
                           "local fm = try(sub.filterMode)catch(1);"
                           "if fm >= 4 and fm <= 5 do hasAdditive = true"
                         ")"
                       ");"
                       "for i = 1 to m.materialList.count do ("
                         "local sub = try(m.materialList[i])catch(undefined);"
                         "if sub == undefined do continue;"
                         "local fm = try(sub.filterMode)catch(1);"
                         "local isAdd = (fm >= 4 and fm <= 5);"
                         "if hasAdditive then ("
                           "try(sub.showInViewport = isAdd)catch()"
                         ") else ("
                           "try(sub.showInViewport = true)catch()"
                         ");"
                         // NOTE: deliberately NO showTextureMap on the composite
                         // here. "Show map in viewport" is EXCLUSIVE within one
                         // material tree — activating each sub's texture on the
                         // composite in turn left only the LAST layer displayed,
                         // so a 2-layer form-switch composite rendered its
                         // alternate-form texture at full opacity, ignoring the
                         // animated per-layer opacity. Per-sub showInViewport
                         // (re-asserted in the final viewport pass below) is
                         // what makes Nitrous blend the sub materials.
                         "syncOpacity sub"
                       ")"
                     ");"
                     "if not isComp do ("
                       "try(m.showInViewport = true)catch();"
                       "syncOpacity m"
                     ")"
                   ")"),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                MAXScript::ScriptSource::NonEmbedded,
#endif
                TRUE, nullptr);

            // ── Fix BitmapTex tiling — LAST operation in material setup ──
            // Setting bm.coords.U_Tile = X can trigger a paramblock refresh on
            // the UVGen. To protect animation controllers on U_Offset/V_Offset/
            // W_Angle set during Phase 2, we SAVE them before the tiling fix
            // runs and RESTORE them afterwards. Same for the Wc3Bitmap anim_*
            // params (which carry the TXAN tracks now) and the Wc3Material
            // opacity.
            {
                std::wstring tilingScript =
                    L"( "
                    // Phase A1: save bitmap coords controllers (getClassInstances
                    // Bitmaptexture also finds the Wc3Bitmap delegates)
                    L"local savedBmpCtrls = #();"
                    L"for bm in getClassInstances Bitmaptexture do ("
                      L"local uC = try(bm.coords.U_Offset.controller)catch(undefined);"
                      L"local vC = try(bm.coords.V_Offset.controller)catch(undefined);"
                      L"local wC = try(bm.coords.W_Angle.controller)catch(undefined);"
                      L"if uC != undefined or vC != undefined or wC != undefined do "
                        L"append savedBmpCtrls #(bm, uC, vC, wC)"
                    L");"
                    // Phase A2a: save Wc3Bitmap anim_* controllers (TXAN home)
                    L"local savedTexCtrls = #();"
                    L"for tx in (try(getClassInstances Wc3Bitmap)catch(#())) do ("
                      L"local uC  = try(tx.anim_UOffset.controller)catch(undefined);"
                      L"local vC  = try(tx.anim_VOffset.controller)catch(undefined);"
                      L"local woC = try(tx.anim_WOffset.controller)catch(undefined);"
                      L"local wC  = try(tx.anim_WAngle.controller)catch(undefined);"
                      L"local utC = try(tx.anim_UTiling.controller)catch(undefined);"
                      L"local vtC = try(tx.anim_VTiling.controller)catch(undefined);"
                      L"if uC != undefined or vC != undefined or woC != undefined or "
                         L"wC != undefined or utC != undefined or vtC != undefined do "
                        L"append savedTexCtrls #(tx, uC, vC, woC, wC, utC, vtC)"
                    L");"
                    // Phase A2b: save Wc3Material opacity controllers.
                    // We also save delegate.opacity.controller because the opacity sync
                    // performed earlier (syncOpacity in the showInViewport pass) attaches
                    // a scaled controller copy to the StandardMaterial delegate, and the
                    // tiling fix below can trigger a paramblock refresh that wipes it.
                    L"local savedMatCtrls = #();"
                    L"fn saveMatCtrl m = ("
                      L"local oC = try(m.opacity.controller)catch(undefined);"
                      L"local dC = try(m.delegate.opacity.controller)catch(undefined);"
                      L"if oC != undefined or dC != undefined do "
                        L"append savedMatCtrls #(m, oC, dC)"
                    L");"
                    L"for m in sceneMaterials do ("
                      L"if classof m == Wdx_Wc3Material do saveMatCtrl m;"
                      L"if try(m.materialList != undefined)catch(false) do ("
                        L"for sub in m.materialList do ("
                          L"if sub != undefined and classof sub == Wdx_Wc3Material do saveMatCtrl sub"
                        L")"
                      L")"
                    L");"

                    L"local wrapLookup = #(); ";
                for (const auto& irTex : irModel.textures) {
                    if (irTex.filePath.empty()) continue;
                    std::string stem = irTex.filePath;
                    auto slashPos = stem.find_last_of("\\/");
                    if (slashPos != std::string::npos) stem = stem.substr(slashPos + 1);
                    auto dotPos = stem.find_last_of('.');
                    if (dotPos != std::string::npos) stem = stem.substr(0, dotPos);
                    std::transform(stem.begin(), stem.end(), stem.begin(),
                                   [](unsigned char c) { return (char)std::tolower(c); });

                    std::wstring wstem;
                    wstem.reserve(stem.size());
                    for (char c : stem) {
                        if (c == L'"' || c == L'\\') continue;
                        wstem.push_back((wchar_t)(unsigned char)c);
                    }

                    tilingScript += L"append wrapLookup #(\"";
                    tilingScript += wstem;
                    tilingScript += L"\", ";
                    tilingScript += (irTex.wrapU ? L"true" : L"false");
                    tilingScript += L", ";
                    tilingScript += (irTex.wrapV ? L"true" : L"false");
                    tilingScript += L"); ";
                }
                tilingScript +=
                    // Phase B: apply tiling booleans
                    L"local fixed = 0;"
                    L"for bm in getClassInstances Bitmaptexture do ("
                      L"local fn2 = \"\";"
                      L"try(fn2 = getFilenameFile bm.fileName)catch();"
                      L"if fn2 != \"\" do ("
                        L"local entry = undefined;"
                        L"for e in wrapLookup while entry == undefined do ("
                          L"if stricmp e[1] fn2 == 0 do entry = e"
                        L");"
                        L"if entry != undefined do ("
                          L"try(bm.coords.U_Tile = entry[2])catch();"
                          L"try(bm.coords.V_Tile = entry[3])catch();"
                          L"fixed += 1"
                        L")"
                      L")"
                    L");"
                    // Phase B2: a Wc3Bitmap's own wrap params are the file's
                    // flags. Replaceable textures (Team Glow) have no file name
                    // for the lookup above, and Wc3Material's filter-mode
                    // handler switched tiling on for additive opacity maps.
                    L"for tx in (try(getClassInstances Wc3Bitmap)catch(#())) do ("
                      L"try(tx.delegate.coords.U_Tile = tx.wrapU)catch();"
                      L"try(tx.delegate.coords.V_Tile = tx.wrapV)catch()"
                    L");"
                    // Phase C1: restore bitmap coords controllers
                    L"for entry in savedBmpCtrls do ("
                      L"local bm = entry[1];"
                      L"if entry[2] != undefined do "
                        L"try(bm.coords.U_Offset.controller = entry[2])catch();"
                      L"if entry[3] != undefined do "
                        L"try(bm.coords.V_Offset.controller = entry[3])catch();"
                      L"if entry[4] != undefined do "
                        L"try(bm.coords.W_Angle.controller = entry[4])catch()"
                    L");"
                    // Phase C2a: restore Wc3Bitmap anim_* controllers
                    L"for entry in savedTexCtrls do ("
                      L"local tx = entry[1];"
                      L"if entry[2] != undefined do "
                        L"try(tx.anim_UOffset.controller = entry[2])catch();"
                      L"if entry[3] != undefined do "
                        L"try(tx.anim_VOffset.controller = entry[3])catch();"
                      L"if entry[4] != undefined do "
                        L"try(tx.anim_WOffset.controller = entry[4])catch();"
                      L"if entry[5] != undefined do "
                        L"try(tx.anim_WAngle.controller = entry[5])catch();"
                      L"if entry[6] != undefined do "
                        L"try(tx.anim_UTiling.controller = entry[6])catch();"
                      L"if entry[7] != undefined do "
                        L"try(tx.anim_VTiling.controller = entry[7])catch()"
                    L");"
                    // Phase C2b: restore Wc3Material opacity controllers
                    // Including delegate.opacity (entry[3]) so the opacity sync set up
                    // earlier survives the tiling fix's paramblock refresh.
                    L"for entry in savedMatCtrls do ("
                      L"local m = entry[1];"
                      L"if entry[2] != undefined do "
                        L"try(m.opacity.controller = entry[2])catch();"
                      L"if entry[3] != undefined do "
                        L"try(m.delegate.opacity.controller = entry[3])catch()"
                    L");"
                    // Phase C3: push Wc3Bitmap anim_* controllers down to the
                    // delegate's coords. Needed because the delegate's own
                    // controllers may have been wiped by a paramblock refresh
                    // while the Wc3Bitmap's params survived. Only push when the
                    // param controller actually has keys (numKeys > 0) —
                    // otherwise an empty default controller would overwrite a
                    // valid one. NOTE: scripted plugins do NOT forward unknown
                    // property accesses, so the coords must be reached through
                    // tx.delegate explicitly.
                    L"local pushed = 0;"
                    L"for entry in savedTexCtrls do ("
                      L"local tx = entry[1];"
                      L"local uC = entry[2];"
                      L"if uC != undefined and (try(numKeys uC)catch(0)) > 0 do ("
                        L"try(tx.delegate.coords.U_Offset.controller = uC)catch(); pushed += 1"
                      L");"
                      L"local vC = entry[3];"
                      L"if vC != undefined and (try(numKeys vC)catch(0)) > 0 do ("
                        L"try(tx.delegate.coords.V_Offset.controller = vC)catch(); pushed += 1"
                      L");"
                      L"local wC = entry[5];"
                      L"if wC != undefined and (try(numKeys wC)catch(0)) > 0 do ("
                        L"try(tx.delegate.coords.W_Angle.controller = wC)catch(); pushed += 1"
                      L");"
                      L"local utC = entry[6];"
                      L"if utC != undefined and (try(numKeys utC)catch(0)) > 0 do ("
                        L"try(tx.delegate.coords.U_Tiling.controller = utC)catch(); pushed += 1"
                      L");"
                      L"local vtC = entry[7];"
                      L"if vtC != undefined and (try(numKeys vtC)catch(0)) > 0 do ("
                        L"try(tx.delegate.coords.V_Tiling.controller = vtC)catch(); pushed += 1"
                      L")"
                    L");"
                    L"format \"[MDLX] Tiling fix: % bitmaps, bmp ctrls=%, tex ctrls=%, mat ctrls=%, pushed=%\\n\" fixed savedBmpCtrls.count savedTexCtrls.count savedMatCtrls.count pushed"
                    L" )";

                ExecuteMAXScriptScript(tilingScript.c_str(),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                    MAXScript::ScriptSource::NonEmbedded,
#endif
                    TRUE, nullptr);
            }

            // Force viewport to Nitrous "Shaded Materials with Maps" mode so
            // transparent/additive MDX materials render correctly. Without this,
            // composite materials with additive layers show a black halo around
            // transparent areas. This mirrors what NeoDex users typically have
            // configured as their default. If the user prefers a different mode,
            // they can change it via the viewport label menu.
            //
            // Reference: NitrousGraphicsManager.GetActiveViewportSetting() exposes
            // the viewport shading options. "Shaded Materials with Maps" is the
            // default in 3ds Max 2020.2+, but older defaults or user prefs may
            // leave it off, which breaks transparent composite materials.
            ExecuteMAXScriptScript(
                // Re-assert per-material viewport display flags as the LAST
                // material operation. The tiling fix's paramblock refreshes
                // (and scripted-plugin on-set handlers) can clear the
                // showInViewport flags set during the sync pass — verified on
                // Mr.War3: both composite subs read showInViewport=false at
                // the end of import, so Nitrous displayed a single (last)
                // texture instead of blending the opacity-animated layers.
                _M("for m in sceneMaterials do ("
                     "local isComp = try(m.materialList != undefined)catch(false);"
                     "if isComp then ("
                       "local hasAdd = false;"
                       "for i = 1 to m.materialList.count do ("
                         "local sub = try(m.materialList[i])catch(undefined);"
                         "if sub != undefined do ("
                           "local fm = try(sub.filterMode)catch(1);"
                           "if fm >= 4 and fm <= 5 do hasAdd = true"
                         ")"
                       ");"
                       "for i = 1 to m.materialList.count do ("
                         "local sub = try(m.materialList[i])catch(undefined);"
                         "if sub != undefined do ("
                           "local fm = try(sub.filterMode)catch(1);"
                           "local isAdd = (fm >= 4 and fm <= 5);"
                           "try(sub.showInViewport = (if hasAdd then isAdd else true))catch()"
                         ")"
                       ")"
                     ") else ("
                       "try(m.showInViewport = true)catch()"
                     ")"
                   ");"
                   "try ("
                     "local vs = NitrousGraphicsManager.GetActiveViewportSetting();"
                     "if vs != undefined do ("
                       "vs.UseTextureEnabled = true;"
                       "vs.TransparencyEnabled = true"
                     ")"
                   ") catch();"
                   "try(completeRedraw())catch(try(redrawViews())catch())"),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                MAXScript::ScriptSource::NonEmbedded,
#endif
                TRUE, nullptr);

            // The layer display above rewrites the composite layers'
            // showInViewport flags, so switch on the layer each composite
            // shows in the viewport once more, now that they are final.
            for (size_t mi = 0; mi < irModel.materials.size() && mi < materials.size(); ++mi) {
                Mtl* mtl = materials[mi];
                if (mtl && mtl->ClassID() == COMPOSITE_MATERIAL_CLASS_ID)
                    mdx_scene::activateCompositeViewportLayer(
                        mtl, irModel.materials[mi], irModel, gi);
            }
            gi->RedrawViews(gi->GetTime());

            ILOG << "==== end material animations ====\n";
            ILOG.flush();
        }

        ResumeAnimate();  // ← restore animate state

        // Visibility animations (uses IKeyControl directly, not SetValue)
        if (opts.core.importVisibility) {
            ILOG << "\n==== Visibility Animations ====\n";

            // Geoset animations: alpha track → mesh node visibility,
            //                    color track → Wc3VertexMod VertexColor
            for (const auto& ga : irModel.geosetAnims) {
                if (ga.meshIndex < 0 ||
                    ga.meshIndex >= static_cast<int32_t>(meshNodes.size()))
                    continue;
                INode* meshNode = meshNodes[ga.meshIndex];
                if (!meshNode) continue;

                // Alpha → visibility
                if (ga.alphaTrackIndex >= 0 &&
                    ga.alphaTrackIndex < static_cast<int32_t>(irModel.floatTracks.size())) {
                    const auto& track = irModel.floatTracks[ga.alphaTrackIndex];
                    if (!track.empty()) {
                        ILOG << "  geosetAnim mesh[" << ga.meshIndex << "] '"
                             << narrow(meshNode->GetName()) << "' vis keys=" << track.keys.size() << "\n";
                        insertVisibilityKeys(meshNode, track, irModel.sequences);
                    }
                }

                // Color → animate Wc3VertexMod.VertexColor on the mesh node's modifier
                //
                // The old C++-path used mod->ClassID() == mdx_ids::WC3_VERTEX_MOD to
                // locate the modifier, but scripted-extend plugins (Wdx_Wc3VertexMod
                // extends VertexPaint) get wrapped by Max at runtime in an MSPlugin
                // class with its own ClassID that differs from the declared one.
                // The check always returned false → animation never applied.
                //
                // Route via MaxScript — same pattern Wc3VertexColorBuilder already
                // uses for modifier creation, and proven in probe_vertexmod.ms:
                // m.VertexColor.controller = bezier_color() works reliably.
                if (ga.usesColor && ga.colorTrackIndex >= 0 &&
                    ga.colorTrackIndex < static_cast<int32_t>(irModel.colorTracks.size())) {
                    const auto& track = irModel.colorTracks[ga.colorTrackIndex];
                    // Preferred: the modifier's VertexColor param found by name
                    // (the wrapped ClassID does not match, a param name does),
                    // keyed by createColorController: exact values and the
                    // file's Bezier/Hermite curve. The MaxScript path below
                    // stays as the fallback.
                    bool colorDone = false;
                    if (!track.empty()) {
                        Object* obj = meshNode->GetObjectRef();
                        if (obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID) {
                            auto* dobj = static_cast<IDerivedObject*>(obj);
                            for (int mi = 0; mi < dobj->NumModifiers() && !colorDone; ++mi) {
                                auto p = findPBParam(dobj->GetModifier(mi), L"VertexColor");
                                if (!p) continue;
                                if (Control* ctrl = createColorController(track)) {
                                    p.pb->SetControllerByID(p.id, 0, ctrl, FALSE);
                                    colorDone = true;
                                }
                            }
                        }
                        if (colorDone)
                            ILOG << "  geosetAnim mesh[" << ga.meshIndex << "] '"
                                 << narrow(meshNode->GetName()) << "' color keys="
                                 << track.keys.size() << "\n";
                    }
                    if (!colorDone && !track.empty()) {
                        // Tangent type per MaxScript key: maps IR interpolation
                        //   None    → #step   (hard toggles, no interp)
                        //   Linear  → #linear (straight lines between keys)
                        //   Hermite → #smooth (auto-smoothed, Max computes tangents)
                        //   Bezier  → #smooth (same; explicit tangent values are
                        //                     rare on KGAC and not worth the
                        //                     extra scripting complexity)
                        const wchar_t* tanType = L"#smooth";
                        switch (track.interpolation) {
                            case ir::InterpolationType::None:   tanType = L"#step";   break;
                            case ir::InterpolationType::Linear: tanType = L"#linear"; break;
                            default: break;
                        }

                        // Resolve by handle — names are not unique.
                        std::wstringstream ss;
                        ss << L"(local n = maxOps.getNodeByHandle "
                           << meshNode->GetHandle() << L";"
                           << L"if n != undefined do ("
                           << L"for m in n.modifiers do ("
                           // Match the Wc3VertexMod modifier robustly. Depending
                           // on Max version (classOf m) as string returns either
                           // the plugin classname (Wdx_Wc3VertexMod) or the UI
                           // `name:` field ("Wc3 Vertex Color"). Accept both,
                           // plus the historical "Wc3VertexMod" string for safety.
                           << L"local cn = (classOf m) as string;"
                           << L"if (cn == \"Wdx_Wc3VertexMod\" or "
                           <<     L"cn == \"Wc3 Vertex Color\" or "
                           <<     L"cn == \"Wc3VertexMod\") do ("
                           << L"m.VertexColor.controller = bezier_color();"
                           << L"local c = m.VertexColor.controller;";

                        // Add each key via addNewKey — returns the MAXKey directly
                        // (NOT an index), so we can set .value/.inTangentType/
                        // .outTangentType on the returned key.
                        for (const auto& kf : track.keys) {
                            int r = static_cast<int>(kf.value.r * 255.0f + 0.5f);
                            int g = static_cast<int>(kf.value.g * 255.0f + 0.5f);
                            int b = static_cast<int>(kf.value.b * 255.0f + 0.5f);
                            if (r < 0) r = 0; if (r > 255) r = 255;
                            if (g < 0) g = 0; if (g > 255) g = 255;
                            if (b < 0) b = 0; if (b > 255) b = 255;
                            ss << L"local k = addNewKey c " << kf.time << L"t;"
                               << L"k.value = color " << r << L" " << g << L" " << b << L";"
                               << L"k.inTangentType = " << tanType << L";"
                               << L"k.outTangentType = " << tanType << L";";
                        }

                        // Global sequence → ORT=cycle so the track loops over its
                        // [0, globalSeqDuration] window.
                        if (track.globalSequenceIndex >= 0) {
                            ss << L"setBeforeORT c #constant;"
                               << L"setAfterORT c #cycle;"
                               << L"enableORTs c true;";
                        }

                        ss << L"exit))))";   // close: if-do, for-do, if-do, (local

                        std::wstring scriptStr = ss.str();
                        ExecuteMAXScriptScript(
                            const_cast<wchar_t*>(scriptStr.c_str()),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                            MAXScript::ScriptSource::NonEmbedded,
#endif
                            TRUE, nullptr);

                        ILOG << "  geosetAnim mesh[" << ga.meshIndex << "] '"
                             << narrow(meshNode->GetName()) << "' color keys="
                             << track.keys.size()
                             << (track.globalSequenceIndex >= 0 ? " (globalSeq)" : "")
                             << " (via MaxScript)\n";
                    }
                }
            }

            // Object visibility: lights, attachments, PE1, PE2, Corn, ribbons
            // Matches NeoDex applyVisibilityAnimations logic per interpolation
            // type, with the upgrade: NonInterp (MDX DontInterp) uses On_Off
            // controller instead of bezier_float+step tangents for true instant
            // on/off behavior matching the WC3 engine.
            auto applyObjVis = [&](int32_t nodeIndex, int32_t trackIndex, const char* type) {
                if (trackIndex < 0 ||
                    trackIndex >= static_cast<int32_t>(irModel.floatTracks.size()))
                    return;
                const auto& track = irModel.floatTracks[trackIndex];
                if (track.empty()) return;
                if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    return;
                INode* node = nodeMap[nodeIndex];
                if (!node) return;

                const char* ctrlName = "?";
                switch (track.interpolation) {
                    case ir::InterpolationType::None:    ctrlName = "boolean_float"; break;
                    case ir::InterpolationType::Linear:  ctrlName = "linear_float";  break;
                    case ir::InterpolationType::Hermite: ctrlName = "tcb_float";     break;
                    case ir::InterpolationType::Bezier:  ctrlName = "bezier_float";  break;
                }

                ILOG << "  " << type << " node[" << nodeIndex << "] '"
                     << narrow(node->GetName()) << "' keys=" << track.keys.size()
                     << " interp=" << static_cast<int>(track.interpolation)
                     << " ctrl=" << ctrlName << "\n";
                insertVisibilityKeys(node, track, irModel.sequences);
            };

            for (const auto& light : irModel.lights)
                applyObjVis(light.nodeIndex, light.visibilityTrackIndex, "light");
            for (const auto& att : irModel.attachments)
                applyObjVis(att.nodeIndex, att.visibilityTrackIndex, "attachment");
            for (const auto& pe : irModel.particleEmitters) {
                const char* label =
                    (pe.variant == 2) ? "pe2" :
                    (pe.variant == 3) ? "corn" : "pe1";
                applyObjVis(pe.nodeIndex, pe.visibilityTrackIndex, label);
            }
            for (const auto& rib : irModel.ribbonEmitters)
                applyObjVis(rib.nodeIndex, rib.visibilityTrackIndex, "ribbon");

            ILOG << "==== end visibility ====\n";
            ILOG.flush();
        }

        // Parameter animations on plugin objects
        if (opts.core.importParameterAnimations) {
            ILOG << "\n==== Parameter Animations ====\n";

            // Helper: get the first IParamBlock2 on a node's base object
            auto getNodePB = [](INode* node) -> IParamBlock2* {
                if (!node) return nullptr;
                Object* obj = node->GetObjectRef();
                if (!obj) return nullptr;
                auto* ref = dynamic_cast<ReferenceTarget*>(obj);
                if (!ref) return nullptr;
                for (int i = 0; i < ref->NumRefs(); i++) {
                    auto* pb = dynamic_cast<IParamBlock2*>(ref->GetReference(i));
                    if (pb) return pb;
                }
                return nullptr;
            };

            auto getNodeRef = [](INode* node) -> ReferenceTarget* {
                if (!node) return nullptr;
                Object* obj = node->GetObjectRef();
                return obj ? dynamic_cast<ReferenceTarget*>(obj) : nullptr;
            };

            // PE1: speed, emissionRate, lifespan, gravity, latitude, longitude
            for (const auto& pe : irModel.particleEmitters) {
                if (pe.variant != 1) continue;
                if (pe.nodeIndex < 0 || pe.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    continue;
                IParamBlock2* pb = getNodePB(nodeMap[pe.nodeIndex]);
                if (!pb) continue;
                ILOG << "  PE1 node[" << pe.nodeIndex << "]\n";
                animateFloatPB(pb, P1_PB_SPEED,          pe.speedTrackIndex, irModel);
                animateFloatPB(pb, P1_PB_EMISSION_RATE,  pe.emissionRateTrackIndex, irModel);
                animateFloatPB(pb, P1_PB_LIFE,           pe.lifespanTrackIndex, irModel);
                animateFloatPB(pb, P1_PB_ACCELERATION,   pe.gravityTrackIndex, irModel);
                // Latitude/longitude: MDX stores radians, plugin expects degrees
                auto animateRadToDeg = [&](ParamID pid, int32_t trackIdx) {
                    if (trackIdx < 0 || trackIdx >= (int32_t)irModel.floatTracks.size()) return;
                    ir::FloatTrack degTrack = irModel.floatTracks[trackIdx];
                    constexpr float kRadToDeg = 180.0f / 3.14159265f;
                    for (auto& k : degTrack.keys) {
                        k.value *= kRadToDeg;
                        if (k.hasTangents) {
                            k.inTangent *= kRadToDeg;
                            k.outTangent *= kRadToDeg;
                        }
                    }
                    Control* ctrl = createFloatController(degTrack);
                    if (ctrl) pb->SetControllerByID(pid, 0, ctrl, FALSE);
                };
                animateRadToDeg(P1_PB_LATITUDE,  pe.latitudeTrackIndex);
                animateRadToDeg(P1_PB_LONGITUDE, pe.longitudeTrackIndex);
            }

            // PE2: speed, variation, latitude, gravity, emissionRate, width, length
            for (const auto& pe : irModel.particleEmitters) {
                if (pe.variant != 2) continue;
                if (pe.nodeIndex < 0 || pe.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    continue;
                IParamBlock2* pb = getNodePB(nodeMap[pe.nodeIndex]);
                if (!pb) continue;
                ILOG << "  PE2 node[" << pe.nodeIndex << "]\n";
                animateFloatPB(pb, P2_PB_SPEED,      pe.speedTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_VARIATION,  pe.variationTrackIndex, irModel);
                // Latitude (ConeAngle): PE2 stores latitude in DEGREES
                // (WC3 engine converts internally, unlike PE1 which is radians).
                // No unit conversion needed — pass the track straight through.
                animateFloatPB(pb, P2_PB_ANGLE_Y,    pe.latitudeTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_GRAVITY,    pe.gravityTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_INITVEL,    pe.emissionRateTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_WIDTH,      pe.widthTrackIndex, irModel);
                animateFloatPB(pb, P2_PB_HEIGHT,     pe.lengthTrackIndex, irModel);
            }

            // Corn / Popcorn FX (variant 3): KPPE, KPPS, KPPL, KPPA, KPPC.
            //
            // Wc3Popcorn is a scripted simpleManipulator, so paramblock
            // controller assignment is as unreliable as it is for Wc3Light —
            // use the same MaxScript *NamedScript route. Param names must
            // match Popcorn.ms (and max_scene_adapter, which samples all five
            // at time t, so the renderer picks the animation up for free).
            //
            // KPPV is deliberately absent: the visibility pass above already
            // materialises it as the node's own visibility track.
            for (const auto& pe : irModel.particleEmitters) {
                if (pe.variant != 3) continue;
                if (pe.nodeIndex < 0 || pe.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    continue;
                INode* cornNode = nodeMap[pe.nodeIndex];
                if (!cornNode) continue;
                ILOG << "  Corn node[" << pe.nodeIndex << "] '"
                     << narrow(cornNode->GetName()) << "' (via MaxScript)"
                     << " KPPE=" << pe.emissionRateTrackIndex
                     << " KPPS=" << pe.speedTrackIndex
                     << " KPPL=" << pe.lifespanTrackIndex
                     << " KPPA=" << pe.alphaTrackIndex
                     << " KPPC=" << pe.colorTrackIndex << "\n";
                animateFloatNamedScript(cornNode, L"emissionRate", pe.emissionRateTrackIndex, irModel);
                animateFloatNamedScript(cornNode, L"speed",        pe.speedTrackIndex, irModel);
                animateFloatNamedScript(cornNode, L"lifeSpan",     pe.lifespanTrackIndex, irModel);
                animateFloatNamedScript(cornNode, L"alpha",        pe.alphaTrackIndex, irModel);
                animateColorNamedScript(cornNode, L"baseColor",    pe.colorTrackIndex, irModel);
            }

            // Ribbons: heightAbove, heightBelow, alpha, color, textureSlot
            for (const auto& rib : irModel.ribbonEmitters) {
                if (rib.nodeIndex < 0 || rib.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    continue;
                IParamBlock2* pb = getNodePB(nodeMap[rib.nodeIndex]);
                if (!pb) continue;
                ILOG << "  Ribbon node[" << rib.nodeIndex << "]\n";
                animateFloatPB(pb, RB_PB_HEIGHT_ABOVE, rib.heightAboveTrackIndex, irModel);
                animateFloatPB(pb, RB_PB_HEIGHT_BELOW, rib.heightBelowTrackIndex, irModel);
                animateFloatPB(pb, RB_PB_ALPHA,        rib.alphaTrackIndex, irModel);
                animateColorPB(pb, RB_PB_COLOR,        rib.colorTrackIndex, irModel);
                // KRTX is an integer track (intTracks), animated through the
                // int parameter's float controller.
                if (rib.textureSlotTrackIndex >= 0 &&
                    rib.textureSlotTrackIndex < static_cast<int32_t>(irModel.intTracks.size())) {
                    const auto& slots = irModel.intTracks[rib.textureSlotTrackIndex];
                    ir::FloatTrack slotTrack;
                    // An integer track carries no tangents, so Bezier/Hermite
                    // come in as Linear; the exporter reads them back the same.
                    slotTrack.interpolation = (slots.interpolation == ir::InterpolationType::None)
                        ? ir::InterpolationType::None : ir::InterpolationType::Linear;
                    slotTrack.globalSequenceIndex = slots.globalSequenceIndex;
                    for (const auto& k : slots.keys) {
                        ir::Keyframe<float> fk;
                        fk.time = k.time;
                        fk.value = static_cast<float>(k.value);
                        slotTrack.keys.push_back(fk);
                    }
                    if (Control* ctrl = createFloatController(slotTrack))
                        pb->SetControllerByID(RB_PB_TEX_SLOT, 0, ctrl, FALSE);
                }
            }

            // Lights: attStart, attEnd, color, intensity, ambColor, ambIntensity,
            // and the 3.0 shadow-casting range and falloff (KLSS/KLSE/KLQF/KLLF/KLDA)
            //
            // Wc3Light is a scripted simpleManipulator, so assigning a
            // ParamBlock controller directly is unreliable. Same solution as for
            // KGAC: go through MaxScript via the *NamedScript helpers. Those set
            // `n.<param>.controller = bezier_float/bezier_color` and add the keys
            // with addNewKey.
            for (const auto& light : irModel.lights) {
                if (light.nodeIndex < 0 || light.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                    continue;
                INode* lightNode = nodeMap[light.nodeIndex];
                if (!lightNode) continue;
                ILOG << "  Light node[" << light.nodeIndex << "] '"
                     << narrow(lightNode->GetName()) << "' (via MaxScript)\n";
                animateFloatNamedScript(lightNode, L"DecayStart",   light.attStartTrackIndex, irModel);
                animateFloatNamedScript(lightNode, L"DecayEnd",     light.attEndTrackIndex, irModel);
                animateColorNamedScript(lightNode, L"ShadowColor",  light.colorTrackIndex, irModel);
                animateFloatNamedScript(lightNode, L"ShadowValue",  light.intensityTrackIndex, irModel);
                animateColorNamedScript(lightNode, L"AmbColor",     light.ambColorTrackIndex, irModel);
                animateFloatNamedScript(lightNode, L"AmbValue",     light.ambIntensityTrackIndex, irModel);
                animateFloatNamedScript(lightNode, L"ShadowCastingStart", light.shadowCastStartTrackIndex, irModel);
                animateFloatNamedScript(lightNode, L"ShadowCastingEnd",   light.shadowCastEndTrackIndex, irModel);
                animateFloatNamedScript(lightNode, L"QuadraticFalloff",   light.quadFalloffTrackIndex, irModel);
                animateFloatNamedScript(lightNode, L"LinearFalloff",      light.linearFalloffTrackIndex, irModel);
                animateFloatNamedScript(lightNode, L"Damping",            light.dampingTrackIndex, irModel);
            }

            ILOG << "==== end parameter animations ====\n";
            ILOG.flush();
        }

        // POST-ANIMATION VERIFICATION: Check actual bone positions AND rotations at frame 0
        ILOG << "\n==== Post-Animation Verify (frame 0) ====\n";
        for (const auto& bone : irModel.bones) {
            if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                continue;
            INode* boneNode = nodeMap[bone.nodeIndex];
            if (!boneNode) continue;

            Matrix3 wTM = boneNode->GetNodeTM(0);
            Point3 wp = wTM.GetTrans();
            Point3 expectedWP = bone.pivotPoint;

            // Extract rotation from world TM
            Matrix3 rotOnly = wTM;
            rotOnly.NoTrans();  // remove translation, keep rotation+scale
            Point3 r0 = rotOnly.GetRow(0);
            Point3 r1 = rotOnly.GetRow(1);
            Point3 r2 = rotOnly.GetRow(2);

            // Also get local TM rotation
            Matrix3 localTM;
            localTM.IdentityMatrix();
            INode* par = boneNode->GetParentNode();
            if (par && !par->IsRootNode()) {
                Matrix3 pTM = par->GetNodeTM(0);
                localTM = wTM * Inverse(pTM);
            } else {
                localTM = wTM;
            }
            Matrix3 localRot = localTM;
            localRot.NoTrans();
            Point3 lr0 = localRot.GetRow(0);
            Point3 lr1 = localRot.GetRow(1);
            Point3 lr2 = localRot.GetRow(2);

            // Check if rotation is identity
            bool rotIsIdentity = (fabs(lr0.x-1)<0.01f && fabs(lr0.y)<0.01f && fabs(lr0.z)<0.01f
                               && fabs(lr1.x)<0.01f && fabs(lr1.y-1)<0.01f && fabs(lr1.z)<0.01f
                               && fabs(lr2.x)<0.01f && fabs(lr2.y)<0.01f && fabs(lr2.z-1)<0.01f);

            Point3 diff = wp - expectedWP;
            float err = Length(diff);

            ILOG << "  bone[" << bone.nodeIndex << "] '" << bone.name
                 << "' pos=(" << wp.x << "," << wp.y << "," << wp.z
                 << ") exp=(" << expectedWP.x << "," << expectedWP.y << "," << expectedWP.z
                 << ") err=" << err;

            if (!rotIsIdentity) {
                ILOG << " ROT=[(" << lr0.x << "," << lr0.y << "," << lr0.z
                     << ")(" << lr1.x << "," << lr1.y << "," << lr1.z
                     << ")(" << lr2.x << "," << lr2.y << "," << lr2.z << ")]";
            }
            ILOG << (err > 0.1f ? " *** BAD ***" : " OK") << "\n";
        }
        ILOG.flush();
    }
    ILOG << "==== end animation ====\n";
    ILOG.flush();

    // DEBUG: sample bone state at multiple frames after animation keys are in place.
    // This reveals:
    //   - Whether NodeTM and ObjectTM diverge at any frame (stretchTM leaks)
    //   - Whether any bone "jumps" > 1000 units between samples (explosion origin)
    //   - Whether autoAlign/freezeLen have been silently re-enabled
    {
        std::vector<TimeValue> sampleFrames;
        int tpf = GetTicksPerFrame();
        // Sample every 20 frames across the animation range
        Interval range = gi->GetAnimRange();
        for (TimeValue t = range.Start(); t <= range.End(); t += 20 * tpf) {
            sampleFrames.push_back(t);
        }
        dumpBoneState(boneNodes, "after-animation", sampleFrames);
    }

    // 14. Build sequences (Note Track entries)
    dialog.step(ImportStep::Finishing);
    {
        mdx_scene::Wc3SequenceBuilder seqBuilder;
        seqBuilder.buildSequences(irModel, gi, reporter);
    }

    // 14b. Scene art tier, through the same MaxScript writer the Settings
    // dialog and the preview's View menu use. No resync: the import is still
    // running, and a preview only shows the imported model after its next
    // rebuild anyway, which is where it reads the tier.
    {
        wchar_t script[96];
        swprintf_s(script, L"::WdxSceneData.setArtTier %d resync:false",
                   static_cast<int>(artTier));
        ExecuteMAXScriptScript(script,
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            MAXScript::ScriptSource::NonEmbedded,
#endif
            TRUE, nullptr);
    }

    // 15. Organize imported nodes into type-based layers
    // (Geometry, Bones, Attachments, Events, Lights, Cameras,
    //  Particle Emitters 1/2, Ribbon Emitters, Popcorn FX, FaceFX,
    //  Collision Shapes, Helpers / Dummies) — matches WdxNodeManager.
    organizeNodesIntoLayers(gi);

    // 16. The report moved to the end of DoImport: it shows once the import
    // dialog, which stays open for the progress, has closed.

    // 17. Populate Material Editor with imported materials
    // Done at the very end so all materials, textures, and animation
    // controllers are fully set up before they appear in the editor.
    // Uses meditMaterials[i] = sceneMaterials[i] (same approach as NeoDex)
    // via ExecuteMAXScriptScript (C++ SDK call into MaxScript).
    ILOG << "\n==== Material Editor Population ====\n";
    ILOG << "  opts.importMaterials=" << opts.core.importMaterials
         << "  materials.size()=" << materials.size() << "\n";

    if (opts.core.importMaterials && !materials.empty()) {
        // Debug: log all materials we created in C++
        int nonNull = 0;
        for (size_t i = 0; i < materials.size(); ++i) {
            Mtl* m = materials[i];
            if (m) {
                ++nonNull;
                ILOG << "  materials[" << i << "] = " << narrow(m->GetName())
                     << "  classID=(" << std::hex
                     << m->ClassID().PartA() << ", "
                     << m->ClassID().PartB() << std::dec << ")\n";
            } else {
                ILOG << "  materials[" << i << "] = nullptr\n";
            }
        }
        ILOG << "  non-null materials: " << nonNull << "\n";

        // Debug: log which meshes have materials assigned
        int assignedCount = 0;
        for (size_t mi = 0; mi < meshNodes.size(); ++mi) {
            if (meshNodes[mi] && meshNodes[mi]->GetMtl()) {
                ++assignedCount;
            }
        }
        ILOG << "  mesh nodes with materials assigned: " << assignedCount
             << " / " << meshNodes.size() << "\n";
        ILOG.flush();

        // Step A: Query sceneMaterials count
        FPValue result;
        BOOL ok = ExecuteMAXScriptScript(
            _M("format \"[MDLX DEBUG] sceneMaterials.count = %\\n\" sceneMaterials.count;"
               "for i = 1 to sceneMaterials.count do "
               "  format \"[MDLX DEBUG]   sceneMat[%] = '%' class=%\\n\" i sceneMaterials[i].name (classOf sceneMaterials[i]);"
               "sceneMaterials.count"),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            MAXScript::ScriptSource::NonEmbedded,
#endif
            TRUE, &result);

        ILOG << "  Step A (query sceneMaterials): ok=" << ok << "\n";
        ILOG.flush();

        // Step B: Populate Compact Material Editor
        // Uses direct meditMaterials[i] assignment (same as NeoDex MaxScript importer)
        ok = ExecuteMAXScriptScript(
            _M("try ("
                 "local count = 0;"
                 "local n = if sceneMaterials.count > 24 then 24 else sceneMaterials.count;"
                 "format \"[MDLX DEBUG] Populating % medit slots...\\n\" n;"
                 "for i = 1 to n do ("
                   "try ("
                     "meditMaterials[i] = sceneMaterials[i];"
                     "setMTLMeditObjType sceneMaterials[i] 3;"
                     "count += 1;"
                     "format \"[MDLX DEBUG]   slot[%] = '%' OK\\n\" i sceneMaterials[i].name"
                   ") catch ("
                     "format \"[MDLX DEBUG]   slot[%] FAILED: %\\n\" i (getCurrentException())"
                   ")"
                 ");"
                 "if count > 0 do activeMeditSlot = 1;"
                 "format \"[MDLX DEBUG] Compact Editor: % slots populated\\n\" count"
               ") catch ("
                 "format \"[MDLX DEBUG] medit populate OUTER ERROR: %\\n\" (getCurrentException())"
               ")"),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            MAXScript::ScriptSource::NonEmbedded,
#endif
            TRUE, nullptr);

        ILOG << "  Step B (medit populate): ok=" << ok << "\n";
        ILOG.flush();

        // Step C: Populate Slate Material Editor (if open)
        ok = ExecuteMAXScriptScript(
            _M("try ("
                 "local viewIdx = sme.CreateView \"Imported Materials\";"
                 "format \"[MDLX DEBUG] SME CreateView returned index: %\\n\" viewIdx;"
                 "if viewIdx != undefined and viewIdx > 0 do ("
                   "local v = sme.GetView viewIdx;"
                   "format \"[MDLX DEBUG] SME GetView returned: %\\n\" (classOf v);"
                   "if v != undefined do ("
                     "for i = 1 to sceneMaterials.count do "
                       "v.CreateNode sceneMaterials[i] [((mod (i-1) 5) * 250), (((i-1) / 5) * 300)];"
                     "actionMan.executeAction 369891408 \"40060\";"
                     "format \"[MDLX DEBUG] SME: % nodes created\\n\" sceneMaterials.count"
                   ")"
                 ")"
               ") catch ("
                 "format \"[MDLX DEBUG] SME failed (OK if editor not open): %\\n\" (getCurrentException())"
               ")"),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
            MAXScript::ScriptSource::NonEmbedded,
#endif
            TRUE, nullptr);

        ILOG << "  Step C (SME populate): ok=" << ok << "\n";
    } else {
        ILOG << "  SKIPPED: importMaterials="
             << opts.core.importMaterials
             << " materials.empty()=" << materials.empty() << "\n";
    }
    ILOG << "==== end Material Editor ====\n";
    ILOG.flush();

    // Set animation range to cover all sequences
    if (!irModel.sequences.empty()) {
        TimeValue maxEnd = 0;
        for (const auto& seq : irModel.sequences)
            if (seq.endTime > maxEnd) maxEnd = seq.endTime;
        if (maxEnd > 0) {
            // at least one frame (see buildSequences)
            Interval range(0, std::max<TimeValue>(maxEnd, GetTicksPerFrame()));
            gi->SetAnimRange(range);
            ILOG << "  Animation range set to 0 - " << maxEnd
                 << " ticks (" << (maxEnd / GetTicksPerFrame()) << " frames)\n";
        }
    } else if (!irModel.globalSequenceDurations.empty()) {
        // No named sequences but has global sequences — use longest
        TimeValue maxGS = 0;
        for (const auto& dur : irModel.globalSequenceDurations) {
            TimeValue t = static_cast<TimeValue>(static_cast<int64_t>(dur) * 4800 / 1000);
            if (t > maxGS) maxGS = t;
        }
        if (maxGS > 0) {
            Interval range(0, std::max<TimeValue>(maxGS, GetTicksPerFrame()));
            gi->SetAnimRange(range);
            ILOG << "  Animation range (global seq) set to 0 - " << maxGS
                 << " ticks (" << (maxGS / GetTicksPerFrame()) << " frames)\n";
        }
    }

    // Tell an open Sequence Manager to re-read the scene. We wrote the sequence
    // Custom Attributes ourselves (Wc3SequenceBuilder::buildSequences), which the
    // dialog knows nothing about: it would keep showing the previous model's list
    // and, because closing it saves, write that stale list back over ours -
    // leaving the freshly imported model with no sequences at all.
    // No-op when the manager was never instanced.
    ExecuteMAXScriptScript(
        _M("try(WdxSequenceManagerRefresh())catch()"),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
        MAXScript::ScriptSource::NonEmbedded,
#endif
        TRUE, nullptr);

    // Close the progress window before Max redraws the scene and before the
    // report, which would otherwise open behind it.
    dialog.close();
    gi->ForceCompleteRedraw();

    // 16. Report
    if (reporter.hasWarnings() || reporter.hasErrors()) {
        reporter.showSummaryDialog(gi->GetMAXHWnd(), core::Operation::Import);
    }

    // TextureResolver (in resolverOpt) closes its CASC + MPQ handles
    // when it goes out of scope at the end of DoImport — nothing to do
    // here.

    ILOG << "\n==== Import Complete ====\n";
    ILOG.flush();

    return IMPEXP_SUCCESS;
}
