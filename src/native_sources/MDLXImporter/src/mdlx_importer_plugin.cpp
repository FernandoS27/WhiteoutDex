// MDLXImporter — MdlxImporterPlugin implementation
#include "mdlx_importer_plugin.h"
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
#include <modstack.h>
#include <iskin.h>
#include <istdplug.h>
#include <stdmat.h>
#include <ilayer.h>
#include <ilayermanager.h>
#include <maxscript/maxscript.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
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
    name.printf(_T("%hs"), bone.name.c_str());
    node->SetName(name);

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

    // Normals via smoothing groups (explicit normals handled later if needed)
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
    name.printf(_T("%hs"), fullName.c_str());
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
            lodNameStr.printf(_T("%hs"), prefix.c_str());
            node->SetUserPropString(_T("Wc3LodName"), lodNameStr);
        }
    }

    return node;
}

// ── Skin modifier application ───────────────────────────────

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

// Bezier constant: (3 * frameRate) / ticksPerFrame
// At 30fps: 3*30/160 = 0.5625.  Converts raw MDX tangent offsets
// into the slope form that Max's bezier controllers expect.
float getBezierConstant() {
    int tpf = GetTicksPerFrame();
    float fps = 4800.0f / static_cast<float>(tpf);
    return (3.0f * fps) / static_cast<float>(tpf);
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

void insertTranslationKeys(INode* node, const ir::Vec3Track& track,
                           const Point3& stubVal,
                           const std::set<TimeValue>& boundaryTimes)
{
    if (track.empty()) return;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return;

    // Replace the default Position XYZ controller with a typed Position
    // controller that matches the MDX interpolation. Position XYZ wraps
    // 3 float sub-controllers and routes SetValue per-channel, which
    // discards the source interpolation type and tangents.
    Class_ID cid;
    switch (track.interpolation) {
    case ir::InterpolationType::Linear:
        cid = Class_ID(LININTERP_POSITION_CLASS_ID, 0);
        break;
    case ir::InterpolationType::Hermite:
        cid = Class_ID(TCBINTERP_POSITION_CLASS_ID, 0);
        break;
    default: // Bezier and None both use Bezier Position (None = step tangents)
        cid = Class_ID(HYBRIDINTERP_POSITION_CLASS_ID, 0);
        break;
    }

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
    bool needStub = (numKeys == 0 || track.keys[0].time > 0);
    float bezConst = getBezierConstant();

    switch (track.interpolation) {
    case ir::InterpolationType::Linear: {
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
        break;
    }
    case ir::InterpolationType::Hermite: {
        // Reconstruct TCB tens/cont/bias from MDX Hermite tangent vectors.
        //
        // Max TCB tangent formulas (from SDK docs):
        //   DS_i = K1·(P_i - P_{i-1}) + K2·(P_{i+1} - P_i)   (outgoing)
        //   DD_i = K3·(P_i - P_{i-1}) + K4·(P_{i+1} - P_i)   (incoming)
        // where K1..K4 are functions of t/c/b. Catmull-Rom (t=c=b=0) gives
        // K1=K2=K3=K4=½, so DS_def = DD_def = (chord_in + chord_out)/2.
        //
        // 3D tangents → scalar t/c/b is underdetermined (6 equations, 3
        // unknowns). Best-effort:
        //   tension     ← average tangent magnitude vs Catmull-Rom default
        //   continuity  ← signed split between incoming/outgoing magnitudes
        //   bias        ← projection asymmetry along chord direction
        // This recovers the most salient curve shape (stiffness, in/out
        // skew) without inventing values that the data can't justify.
        auto reconstructTCB = [&](int i, float& outT, float& outC, float& outB) {
            outT = outC = outB = 0.0f;
            const auto& kf = track.keys[i];
            if (!kf.hasTangents) return;

            Point3 chordIn  = (i > 0)
                ? (kf.value - track.keys[i-1].value)
                : (kf.value - stubVal);
            Point3 chordOut = (i < numKeys - 1)
                ? (track.keys[i+1].value - kf.value)
                : Point3(0.0f, 0.0f, 0.0f);

            Point3 catmull = (chordIn + chordOut) * 0.5f;
            float catmullLen = catmull.Length();
            if (catmullLen < 1e-6f) return;

            // MDX hermite stores in/out tangents as control-point positions
            // adjacent to the key value (same convention as the bezier path
            // above). Take magnitudes — sign convention doesn't affect the
            // ratio used for tension reconstruction.
            Point3 inRel  = kf.inTangent  - kf.value;
            Point3 outRel = kf.outTangent - kf.value;
            float inLen   = inRel.Length();
            float outLen  = outRel.Length();
            float avgLen  = 0.5f * (inLen + outLen);

            // tension: 1 - (actual / Catmull-Rom default), clamped.
            // |tangent| > default → t < 0 (looser/longer handles)
            // |tangent| < default → t > 0 (tighter handles)
            float scale = avgLen / catmullLen;
            outT = std::max(-1.0f, std::min(1.0f, 1.0f - scale));

            // continuity: signed asymmetry of incoming vs outgoing magnitude.
            // |in| == |out| → c = 0 (smooth).  |in| ≠ |out| → kink.
            float magSum = inLen + outLen;
            if (magSum > 1e-6f) {
                float c = (outLen - inLen) / magSum;
                outC = std::max(-1.0f, std::min(1.0f, c));
            }

            // bias: projection asymmetry along the chord direction.
            // Average tangent leans toward chordOut → b > 0; toward chordIn → b < 0.
            Point3 catmullN = catmull / catmullLen;
            // Flip inRel so both vectors point "forward in time" (toward the
            // outgoing chord direction); MDX inTan handle sits behind the key.
            float dotIn  = DotProd(-inRel, catmullN);
            float dotOut = DotProd( outRel, catmullN);
            float dotSum = std::abs(dotIn) + std::abs(dotOut);
            if (dotSum > 1e-6f) {
                float b = (dotOut - dotIn) / dotSum;
                outB = std::max(-1.0f, std::min(1.0f, b));
            }
        };

        if (needStub) {
            ITCBPoint3Key k;
            memset(&k, 0, sizeof(k));
            k.time = 0;
            k.val = stubVal;
            ikc->AppendKey(&k);
        }
        for (int i = 0; i < numKeys; ++i) {
            ITCBPoint3Key k;
            memset(&k, 0, sizeof(k));
            k.time = track.keys[i].time;
            k.val = track.keys[i].value;
            reconstructTCB(i, k.tens, k.cont, k.bias);

            // Lock TCB tangents at sequence boundaries so the curve does
            // not bleed across sequence transitions. Matches MaxScript
            // fixRotationTCB tension=50 (= SDK tens=1.0). Continuity stays
            // at 0 (smooth), bias stays at 0 (no skew).
            if (boundaryTimes.count(track.keys[i].time) > 0) {
                k.tens = 1.0f;
                k.cont = 0.0f;
                k.bias = 0.0f;
            }
            ikc->AppendKey(&k);
        }
        break;
    }
    default: { // Bezier and None
        if (needStub) {
            IBezPoint3Key k;
            memset(&k, 0, sizeof(k));
            k.time = 0;
            k.val = stubVal;
            if (track.interpolation == ir::InterpolationType::None) {
                SetInTanType(k.flags, BEZKEY_STEP);
                SetOutTanType(k.flags, BEZKEY_STEP);
            }
            ikc->AppendKey(&k);
        }
        for (int i = 0; i < numKeys; ++i) {
            const auto& kf = track.keys[i];
            IBezPoint3Key k;
            memset(&k, 0, sizeof(k));
            k.time = kf.time;
            k.val = kf.value;

            if (track.interpolation == ir::InterpolationType::None) {
                SetInTanType(k.flags, BEZKEY_STEP);
                SetOutTanType(k.flags, BEZKEY_STEP);
            } else if (kf.hasTangents) {
                SetInTanType(k.flags, BEZKEY_USER);
                SetOutTanType(k.flags, BEZKEY_USER);
                if (i > 0) {
                    float dt = static_cast<float>(kf.time - track.keys[i-1].time);
                    if (dt > 0.0f) k.intan = bezConst * (kf.inTangent - kf.value) / dt;
                }
                if (i < numKeys - 1) {
                    float dt = static_cast<float>(track.keys[i+1].time - kf.time);
                    if (dt > 0.0f) k.outtan = bezConst * (kf.outTangent - kf.value) / dt;
                }
            }
            ikc->AppendKey(&k);
        }
        break;
    }
    }

    ikc->SortKeys();

    // ORT cycle for global sequences
    if (track.globalSequenceIndex >= 0) {
        posCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
        posCtrl->EnableORTs(TRUE);
    }
}

void insertRotationKeys(INode* node, const ir::QuatTrack& track,
                        const Quat& stubVal) {
    if (track.empty()) return;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return;

    // Replace rotation controller with linear_rotation for quaternion SLERP.
    // Unlike position, we MUST replace because Euler XYZ can't do quaternion interpolation.
    Control* rotCtrl = static_cast<Control*>(
        CreateInstance(CTRL_ROTATION_CLASS_ID, Class_ID(LININTERP_ROTATION_CLASS_ID, 0)));
    if (!rotCtrl) return;
    tmCtrl->SetRotationController(rotCtrl);

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
    bool needRotStub = (numRotKeys == 0 || track.keys[0].time > 0);
    if (needRotStub) {
        Quat sv(-stubVal.x, -stubVal.y, -stubVal.z, stubVal.w);
        rotCtrl->SetValue(0, &sv, 1, CTRL_ABSOLUTE);
    }

    for (int i = 0; i < numRotKeys; ++i) {
        Quat q(-fixedKeys[i].x, -fixedKeys[i].y, -fixedKeys[i].z, fixedKeys[i].w);
        rotCtrl->SetValue(track.keys[i].time, &q, 1, CTRL_ABSOLUTE);
    }

    if (track.globalSequenceIndex >= 0) {
        rotCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
        rotCtrl->EnableORTs(TRUE);
    }
}

void insertScaleKeys(INode* node, const ir::Vec3Track& track,
                     const Point3& stubVal)
{
    if (track.empty()) return;

    Control* tmCtrl = node->GetTMController();
    if (!tmCtrl) return;

    // Use existing scale controller + SetValue (same approach as position)
    Control* scaleCtrl = tmCtrl->GetScaleController();
    if (!scaleCtrl) return;

    int numKeys = static_cast<int>(track.keys.size());

    // Set bind-pose scale at frame 0
    bool needStub = (numKeys == 0 || track.keys[0].time > 0);
    if (needStub) {
        ScaleValue sv(stubVal);
        scaleCtrl->SetValue(0, &sv, 1, CTRL_ABSOLUTE);
    }

    // Set animation keys
    for (int i = 0; i < numKeys; ++i) {
        ScaleValue sv(track.keys[i].value);
        scaleCtrl->SetValue(track.keys[i].time, &sv, 1, CTRL_ABSOLUTE);
    }

    if (track.globalSequenceIndex >= 0) {
        scaleCtrl->SetORT(ORT_CYCLE, ORT_AFTER);
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

    Class_ID cid;
    switch (track.interpolation) {
    case ir::InterpolationType::Linear:  cid = Class_ID(LININTERP_FLOAT_CLASS_ID, 0); break;
    case ir::InterpolationType::Hermite: cid = Class_ID(TCBINTERP_FLOAT_CLASS_ID, 0); break;
    default:                             cid = Class_ID(HYBRIDINTERP_FLOAT_CLASS_ID, 0); break;
    }

    Control* ctrl = static_cast<Control*>(CreateInstance(CTRL_FLOAT_CLASS_ID, cid));
    if (!ctrl) return nullptr;

    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) { ctrl->DeleteThis(); return nullptr; }

    float bezConst = getBezierConstant();
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
        case ir::InterpolationType::Hermite: {
            ITCBFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = kf.value;
            ikc->AppendKey(&key);
            break;
        }
        default: { // Bezier (also used for None/DontInterp on float params)
            IBezFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = kf.value;

            if (track.interpolation == ir::InterpolationType::None) {
                SetInTanType(key.flags, BEZKEY_STEP);
                SetOutTanType(key.flags, BEZKEY_STEP);
            } else if (kf.hasTangents) {
                SetInTanType(key.flags, BEZKEY_USER);
                SetOutTanType(key.flags, BEZKEY_USER);
                if (i > 0) {
                    float dt = static_cast<float>(kf.time - track.keys[i-1].time);
                    if (dt > 0.0f) key.intan = bezConst * (kf.inTangent - kf.value) / dt;
                }
                if (i < numKeys - 1) {
                    float dt = static_cast<float>(track.keys[i+1].time - kf.time);
                    if (dt > 0.0f) key.outtan = bezConst * (kf.outTangent - kf.value) / dt;
                }
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

    // All color interpolation types use Bezier Color (HYBRIDINTERP_COLOR_CLASS_ID).
    // Max has no separate Linear or TCB color controller classes.
    Control* ctrl = static_cast<Control*>(
        CreateInstance(CTRL_POINT3_CLASS_ID, Class_ID(HYBRIDINTERP_COLOR_CLASS_ID, 0)));
    if (!ctrl) return nullptr;

    IKeyControl* ikc = GetKeyControlInterface(ctrl);
    if (!ikc) { ctrl->DeleteThis(); return nullptr; }

    float bezConst = getBezierConstant();
    int numKeys = static_cast<int>(track.keys.size());

    for (int i = 0; i < numKeys; ++i) {
        const auto& kf = track.keys[i];
        Point3 val(kf.value.r, kf.value.g, kf.value.b);

        if (track.interpolation == ir::InterpolationType::Hermite) {
            ITCBPoint3Key key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = val;
            ikc->AppendKey(&key);
        } else {
            // Bezier, Linear, None all use IBezPoint3Key
            IBezPoint3Key key;
            memset(&key, 0, sizeof(key));
            key.time = kf.time;
            key.val = val;

            if (track.interpolation == ir::InterpolationType::None) {
                SetInTanType(key.flags, BEZKEY_STEP);
                SetOutTanType(key.flags, BEZKEY_STEP);
            } else if (kf.hasTangents && track.interpolation == ir::InterpolationType::Bezier) {
                SetInTanType(key.flags, BEZKEY_USER);
                SetOutTanType(key.flags, BEZKEY_USER);
                Point3 inT(kf.inTangent.r, kf.inTangent.g, kf.inTangent.b);
                Point3 outT(kf.outTangent.r, kf.outTangent.g, kf.outTangent.b);
                if (i > 0) {
                    float dt = static_cast<float>(kf.time - track.keys[i-1].time);
                    if (dt > 0.0f) key.intan = bezConst * (inT - val) / dt;
                }
                if (i < numKeys - 1) {
                    float dt = static_cast<float>(track.keys[i+1].time - kf.time);
                    if (dt > 0.0f) key.outtan = bezConst * (outT - val) / dt;
                }
            }
            ikc->AppendKey(&key);
        }
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

    std::wstring nodeName(node->GetName());
    std::wstringstream ss;
    ss << L"(local n = getNodeByName \"" << nodeName << L"\";"
       << L"if n != undefined do ("
       << L"n." << paramName << L".controller = bezier_float();"
       << L"local c = n." << paramName << L".controller;";

    for (const auto& kf : track.keys) {
        ss << L"local k = addNewKey c " << kf.time << L"t;"
           << L"k.value = " << kf.value << L";"
           << L"k.inTangentType = " << tanType << L";"
           << L"k.outTangentType = " << tanType << L";";
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

    std::wstring nodeName(node->GetName());
    std::wstringstream ss;
    ss << L"(local n = getNodeByName \"" << nodeName << L"\";"
       << L"if n != undefined do ("
       << L"n." << paramName << L".controller = bezier_color();"
       << L"local c = n." << paramName << L".controller;";

    for (const auto& kf : track.keys) {
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
        ss << L"local k = addNewKey c " << kf.time << L"t;"
           << L"k.value = color " << r << L" " << g << L" " << b << L";"
           << L"k.inTangentType = " << tanType << L";"
           << L"k.outTangentType = " << tanType << L";";
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

// ── Helper: get diffuse BitmapTex from a Wc3Material ────────
// Reads the "diffuseMap" paramblock param and returns the BitmapTex pointer
// (or nullptr if not a native BitmapTex).
static BitmapTex* getDiffuseBitmapTex(ReferenceTarget* wc3MatRef) {
    if (!wc3MatRef) return nullptr;
    auto p = findPBParam(wc3MatRef, L"diffuseMap");
    if (!p) return nullptr;
    ParamType2 ptype = p.pb->GetParameterType(p.id);
    if (ptype != TYPE_TEXMAP) return nullptr;
    Texmap* tex = nullptr;
    Interval valid = FOREVER;
    p.pb->GetValue(p.id, 0, tex, valid);
    if (!tex) return nullptr;
    if (tex->ClassID() != Class_ID(BMTEX_CLASS_ID, 0)) return nullptr;
    return static_cast<BitmapTex*>(tex);
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
//   None (DontInterp) → On_Off (boolean toggle) controller
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
        // DontInterp → On/Off toggle controller (BOOL_CONTROL_CLASS_ID).
        // On/Off default state is ON (visible=1.0). Each key toggles state.
        // Output is 1.0 (visible) or -1.0 (hidden).
        //
        // Critical: MDX sequences are independent — visibility resets to ON
        // at the start of every sequence UNLESS there's an explicit 0 key
        // at that time. Since On/Off accumulates toggles across the whole
        // timeline, we must insert correction toggles at sequence boundaries.
        static const Class_ID ON_OFF_CLASS_ID(0x984b8d27, 0x938f3e43);
        visCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, ON_OFF_CLASS_ID));
        if (!visCtrl) return;
        node->SetVisController(visCtrl);

        // Build ordered lookup: time → MDX value
        std::map<TimeValue, float> keyMap;
        for (const auto& k : track.keys)
            keyMap[k.time] = k.value;

        // On/Off starts ON. Track accumulated state.
        bool state = true;

        // AddNewKey inserts a toggle point at time t.
        auto emitToggle = [&](TimeValue t) {
            visCtrl->AddNewKey(t, 0);
            state = !state;
        };

        // ── GLOBAL SEQUENCE PATH ────────────────────────────────
        // Global-sequence tracks live on [0, globalSeqDuration] and don't
        // honor named-sequence boundaries (remapTrackKeys skips them, so
        // track.keys are still in their original MDX-local times).
        // Named-sequence boundary toggling would drop keys outside those
        // windows and misplace the ones that fall inside — for a globalSeq
        // track the authoritative toggle points ARE track.keys in order.
        if (track.globalSequenceIndex >= 0) {
            for (const auto& [t, v] : keyMap) {
                bool desired = (v >= 0.5f);
                if (state != desired)
                    emitToggle(t);
            }
            break;
        }

        if (!sequences.empty()) {
            // Process sequence by sequence.
            for (const auto& seq : sequences) {
                // Expected state at sequence start: ON unless explicit 0 key
                bool startVis = true;
                auto it = keyMap.find(seq.startTime);
                if (it != keyMap.end() && it->second < 0.5f)
                    startVis = false;

                // Correct accumulated state if it doesn't match
                if (state != startVis)
                    emitToggle(seq.startTime);

                // Process keys within (startTime, endTime]
                // NOTE: endTime is INCLUSIVE. A key at t == seq.endTime is
                // the final frame of the sequence and represents a real
                // alpha transition (e.g. a geoset hidden at the last frame
                // of a death animation). Skipping it loses toggles — bug
                // visible on Madara Susanoo geosets 14/16, which drop from
                // 4 real transitions down to 2 when endTime is exclusive.
                auto kit = keyMap.upper_bound(seq.startTime);
                for (; kit != keyMap.end() && kit->first <= seq.endTime; ++kit) {
                    bool desired = (kit->second >= 0.5f);
                    if (state != desired)
                        emitToggle(kit->first);
                }
            }
        } else {
            // No sequences — process all keys linearly
            for (const auto& [t, v] : keyMap) {
                bool desired = (v >= 0.5f);
                if (state != desired)
                    emitToggle(t);
            }
        }
        break;
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
    case ir::InterpolationType::Hermite: {
        visCtrl = static_cast<Control*>(
            CreateInstance(CTRL_FLOAT_CLASS_ID, Class_ID(TCBINTERP_FLOAT_CLASS_ID, 0)));
        if (!visCtrl) return;
        node->SetVisController(visCtrl);

        IKeyControl* ikc = GetKeyControlInterface(visCtrl);
        if (!ikc) return;

        if (!hasFrameZero) {
            ITCBFloatKey stubKey;
            memset(&stubKey, 0, sizeof(stubKey));
            stubKey.time = 0;
            stubKey.val = track.keys[0].value;
            stubKey.tens = 0.0f;
            stubKey.cont = 0.0f;
            stubKey.bias = 0.0f;
            stubKey.easeIn = 0.0f;
            stubKey.easeOut = 0.0f;
            ikc->AppendKey(&stubKey);
        }

        for (int i = 0; i < numKeys; ++i) {
            ITCBFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = track.keys[i].time;
            key.val = track.keys[i].value;
            key.tens = 0.0f;
            key.cont = 0.0f;
            key.bias = 0.0f;
            key.easeIn = 0.0f;
            key.easeOut = 0.0f;
            ikc->AppendKey(&key);
        }
        ikc->SortKeys();
        break;
    }
    case ir::InterpolationType::Bezier: {
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
            stubKey.intan = 0.0f;
            stubKey.outtan = 0.0f;
            ikc->AppendKey(&stubKey);
        }

        float bezConst = getBezierConstant();

        for (int i = 0; i < numKeys; ++i) {
            IBezFloatKey key;
            memset(&key, 0, sizeof(key));
            key.time = track.keys[i].time;
            key.val = track.keys[i].value;

            // Custom tangents from MDX data
            SetInTanType(key.flags, BEZKEY_USER);
            SetOutTanType(key.flags, BEZKEY_USER);

            if (track.keys[i].hasTangents) {
                // In tangent: relative to previous key time
                if (i > 0) {
                    float dt = static_cast<float>(track.keys[i].time - track.keys[i-1].time);
                    if (dt > 0.0f)
                        key.intan = bezConst * (track.keys[i].inTangent - key.val) / dt;
                }
                // Out tangent: relative to next key time
                if (i < numKeys - 1) {
                    float dt = static_cast<float>(track.keys[i+1].time - track.keys[i].time);
                    if (dt > 0.0f)
                        key.outtan = bezConst * (track.keys[i].outTangent - key.val) / dt;
                }
            }

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

template <typename T>
void remapTrackKeys(ir::Track<T>& track,
                    const std::vector<SeqRange>& ranges,
                    const T& defaultVal,
                    bool useDefaultForStartBoundary)
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
            T holdVal = defaultVal;
            if (!newKeys.empty()) {
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

    // Remap node animation tracks
    for (auto& na : irModel.nodeAnimations) {
        remapTrackKeys(na.translation, ranges, Point3(0.0f, 0.0f, 0.0f), false);
        remapTrackKeys(na.rotation,    ranges, Quat(0.0f, 0.0f, 0.0f, 1.0f), false);
        remapTrackKeys(na.scale,       ranges, Point3(1.0f, 1.0f, 1.0f), false);
    }

    // Remap indexed tracks (global-sequence tracks skipped inside remapTrackKeys)
    for (auto& t : irModel.floatTracks)
        remapTrackKeys(t, ranges, 1.0f, true);
    for (auto& t : irModel.vec3Tracks)
        remapTrackKeys(t, ranges, Point3(0.0f, 0.0f, 0.0f), false);
    for (auto& t : irModel.quatTracks)
        remapTrackKeys(t, ranges, Quat(0.0f, 0.0f, 0.0f, 1.0f), false);
    for (auto& t : irModel.colorTracks)
        remapTrackKeys(t, ranges, Color(1.0f, 1.0f, 1.0f), false);
    for (auto& t : irModel.intTracks)
        remapTrackKeys(t, ranges, int32_t(0), true);
    for (auto& t : irModel.vec4Tracks)
        remapTrackKeys(t, ranges, Point4(0.0f, 0.0f, 0.0f, 0.0f), false);

    // Remap event object key times
    for (auto& evt : irModel.eventObjects) {
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
    whiteout::mdx::Parser parser;
    whiteout::mdx::Model mdxModel;

    try {
        mdxModel = parser.parse(filePath);
    } catch (const std::exception& e) {
        MSTR msg;
        msg.printf(_T("Failed to parse model file:\n%hs"), e.what());
        if (gi->GetMAXHWnd())
            MessageBoxW(gi->GetMAXHWnd(), msg.data(), _T("MDLXImporter"), MB_OK | MB_ICONERROR);
        return IMPEXP_FAIL;
    }

    opts.detectedVersion = mdxModel.version;

    // 3. Show import options dialog (unless suppressed)
    if (!suppressPrompts) {
        bool isReforged = (mdxModel.version >= 1200);
        if (!showImportDialog(hInstance, gi->GetMAXHWnd(), opts, isReforged))
            return IMPEXP_CANCEL;
    }

    applyFastPreset(opts);

    // 4. Disassemble: mdx::Model → ir::IRModel
    mdx_disasm::MdxModelDisassembler disassembler;
    ir::IRModel irModel = disassembler.disassemble(mdxModel, opts);

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
        ii->NewScene();
    }

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
                if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int32_t>(mustImport.size())
                    && !mustImport[bone.parentIndex]) {
                    mustImport[bone.parentIndex] = true;
                    changed = true;
                }
            }
        }

        for (const auto& bone : irModel.bones) {
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
            if (bone.nodeFlags & 0x7) {
                DWORD inheritFlags = INHERIT_ALL;
                if (bone.nodeFlags & 0x1)
                    inheritFlags &= ~(INHERIT_POS_X | INHERIT_POS_Y | INHERIT_POS_Z);
                if (bone.nodeFlags & 0x2)
                    inheritFlags &= ~(INHERIT_ROT_X | INHERIT_ROT_Y | INHERIT_ROT_Z);
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
        for (const auto& bone : irModel.bones) {
            if (bone.nodeIndex < 0 || bone.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                continue;
            INode* boneNode = nodeMap[bone.nodeIndex];
            if (!boneNode) continue;

            // Walk up the parent chain until we find one that exists in nodeMap
            int32_t parentIdx = bone.parentIndex;
            while (parentIdx >= 0 && parentIdx < static_cast<int32_t>(nodeMap.size())
                   && !nodeMap[parentIdx]) {
                parentIdx = irModel.nodes[parentIdx].parentIndex;
            }

            if (parentIdx >= 0 && parentIdx < static_cast<int32_t>(nodeMap.size())) {
                INode* parent = nodeMap[parentIdx];
                if (parent)
                    parent->AttachChild(boneNode);
            }
        }
    }

    ResumeAnimate();  // ← skeleton setup done, restore animate state

    // DEBUG: dump bone state immediately after creation (before skin, before animation).
    // Expect: autoAlign=0, freezeLen=0, isBone=1, stretchTM=IDENTITY for all bones.
    // nodeTM.pos == objTM.pos at t=0.
    dumpBoneState(boneNodes, "after-create");

    // 8. Build meshes
    std::vector<INode*> meshNodes;
    meshNodes.reserve(irModel.meshes.size());

    for (const auto& irMesh : irModel.meshes) {
        INode* meshNode = createMeshNode(irMesh, gi);
        meshNodes.push_back(meshNode);  // may be nullptr for empty meshes
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

    // Pre-resolve all textures and PE1 model files (including CASC extraction)
    // so every builder can find them on disk without needing the CASC handle.
    // PE1 model files are parsed recursively to extract their textures and
    // any nested PE1 model references (with cycle detection).
    if (opts.core.importTextures) {
        void* cascPtr = nullptr;
        if (opts.searchCASC)
            cascPtr = mdx_scene::openCascStorage(opts.cascDirectory);

        // Resolve all textures from the main model
        for (const auto& irTex : irModel.textures) {
            if (!irTex.filePath.empty()) {
                std::wstring wpath(irTex.filePath.begin(), irTex.filePath.end());
                mdx_scene::resolveTexturePathFull(modelDir, wpath, cascPtr);
            }
        }

        // Recursively resolve PE1 model files and their textures.
        // PE1 particles spawn sub-models which may have their own textures
        // and their own PE1 emitters (forming a tree).
        {
            namespace fs = std::filesystem;

            // Resolve a model file on disk: try modelDir + relPath directly,
            // then try swapping .mdx↔.mdl extension.
            auto resolveModelOnDisk = [&](const std::wstring& relPath) -> std::wstring {
                std::error_code ec;
                // Try exact path
                fs::path full = fs::path(modelDir) / relPath;
                if (fs::exists(full, ec)) return full.wstring();

                // Try alternate extension (.mdx ↔ .mdl)
                fs::path stem = full;
                stem.replace_extension();
                std::wstring ext = full.extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                fs::path alt;
                if (ext == L".mdl") { alt = stem; alt += L".mdx"; }
                else                { alt = stem; alt += L".mdl"; }
                if (fs::exists(alt, ec)) return alt.wstring();
                return {};
            };

            // Resolve a model file, falling back to CASC extraction.
            auto resolveModelFull = [&](const std::wstring& relPath) -> std::wstring {
                // Check disk first (both extensions)
                auto onDisk = resolveModelOnDisk(relPath);
                if (!onDisk.empty()) return onDisk;

                // Try CASC extraction with original path
                auto extracted = mdx_scene::resolveTexturePathFull(modelDir, relPath, cascPtr);
                std::error_code ec;
                if (!extracted.empty() && fs::exists(extracted, ec)) return extracted;

                // Try CASC with alternate extension
                std::wstring altPath = relPath;
                if (altPath.size() > 4) {
                    std::wstring ext = altPath.substr(altPath.size() - 4);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    if (ext == L".mdx")
                        altPath = altPath.substr(0, altPath.size() - 4) + L".mdl";
                    else if (ext == L".mdl")
                        altPath = altPath.substr(0, altPath.size() - 4) + L".mdx";
                    else
                        return {};
                    extracted = mdx_scene::resolveTexturePathFull(modelDir, altPath, cascPtr);
                    if (!extracted.empty() && fs::exists(extracted, ec)) return extracted;
                }
                return {};
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
                            // Try relative to child model first, then parent model dir
                            mdx_scene::resolveTexturePathFull(childDir, wTexPath, cascPtr);
                            mdx_scene::resolveTexturePathFull(modelDir, wTexPath, cascPtr);
                        }
                    }
                    for (const auto& subPE : subModel.particleEmitters) {
                        if (!subPE.spawnModelFileName.empty())
                            pendingModels.push_back(subPE.spawnModelFileName);
                    }
                } catch (...) {}
            }
        }

        if (cascPtr) {
            mdx_scene::closeCascStorage(cascPtr);
            cascPtr = nullptr;
        }
    }

    // 10. Build materials and assign to meshes
    std::vector<Mtl*> materials;
    if (opts.core.importMaterials) {
        mdx_scene::Wc3MaterialBuilder matBuilder;
        materials = matBuilder.buildMaterials(
            irModel, opts.core.importTextures, modelDir, nullptr, gi, reporter);

        for (size_t mi = 0; mi < irModel.meshes.size(); ++mi) {
            int32_t matIdx = irModel.meshes[mi].materialIndex;
            if (meshNodes[mi] && matIdx >= 0 && matIdx < static_cast<int32_t>(materials.size()))
                meshNodes[mi]->SetMtl(materials[matIdx]);
        }

        // 10b. Trigger Wc3Material delegate viewport update
        // C++ paramblock SetValue does NOT fire scripted plugin "on X set" handlers.
    }

    // 11. Build format-specific scene objects (scripted plugins)
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
            pe2Builder.buildParticles(irModel, nodeMap, modelDir, nullptr, gi, reporter);
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

        // v1200-specific
        if (opts.detectedVersion >= 1200) {
            if (opts.importCornEmitters) {
                mdx_scene::Wc3PopcornBuilder cornBuilder;
                cornBuilder.buildPopcorn(irModel, nodeMap, gi, reporter);
            }
            if (opts.importFaceFX) {
                mdx_scene::Wc3FaceFxBuilder ffxBuilder;
                ffxBuilder.buildFaceFX(irModel, nodeMap, gi, reporter);
            }
        }

        // Cameras
        if (opts.core.importCameras) {
            mdx_scene::Wc3CameraBuilder camBuilder;
            cameraPairs = camBuilder.buildCameras(irModel, nodeMap, gi, reporter);
        }

    }

    // 12. Apply skin modifiers BEFORE animation (MaxScript order)
    ILOG << "\n==== Skinning (before animation) ====\n";

    // DEBUG: dump bones RIGHT BEFORE skin is applied — these are the transforms
    // AddBoneEx will snapshot as bind pose.  If any bone's ObjectTM differs from
    // its NodeTM here, Skin will store a wrong bind pose and every frame will
    // look wrong relative to it.
    dumpBoneState(boneNodes, "pre-skin");

    if (opts.core.importSkinning) {
        for (size_t mi = 0; mi < irModel.meshes.size(); ++mi) {
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
    ILOG << "\n==== Animation Keys ====\n";
    if (opts.core.importAnimations) {
        SuspendAnimate();
        AnimateOn();  // ← CRITICAL: SetValue needs this to create keys

        const Quat stubRot(0.0f, 0.0f, 0.0f, 1.0f);

        // Sequence boundary times — used for synthetic boundary key insertion
        // and TCB tension lock at sequence borders. See insertSequenceBoundaryKeys.
        std::set<TimeValue> boundaryTimes;
        for (const auto& seq : irModel.sequences) {
            boundaryTimes.insert(seq.startTime);
            boundaryTimes.insert(seq.endTime);
        }

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

        for (const auto& na : irModel.nodeAnimations) {
            if (na.nodeIndex < 0 || na.nodeIndex >= static_cast<int32_t>(nodeMap.size()))
                continue;
            INode* node = nodeMap[na.nodeIndex];
            if (!node) continue;

            Point3 localPos = (na.nodeIndex < static_cast<int32_t>(nodeLocalPos.size()))
                ? nodeLocalPos[na.nodeIndex] : Point3(0,0,0);

            ILOG << "  ANIM node[" << na.nodeIndex << "]"
                 << " localPos=(" << localPos.x << "," << localPos.y << "," << localPos.z
                 << ") KGTR=" << na.translation.keys.size()
                 << " KGRT=" << na.rotation.keys.size()
                 << " KGSC=" << na.scale.keys.size() << "\n";

            // Rotation: write raw keys (no offset needed)
            if (opts.core.importRotation && !na.rotation.empty()) {
                ir::QuatTrack rotTrack = na.rotation;
                insertSequenceBoundaryKeys(rotTrack, irModel.sequences);
                insertRotationKeys(node, rotTrack, stubRot);
            }

            // Translation: add localPos to every key value + stub
            if (opts.core.importTranslation && !na.translation.empty()) {
                ir::Vec3Track adjustedTrack = na.translation;
                for (auto& k : adjustedTrack.keys) {
                    k.value += localPos;
                    if (k.hasTangents) {
                        k.inTangent += localPos;
                        k.outTangent += localPos;
                    }
                }
                insertSequenceBoundaryKeys(adjustedTrack, irModel.sequences);
                insertTranslationKeys(node, adjustedTrack, localPos, boundaryTimes);
            }

            // Scale: write raw keys
            if (opts.core.importScale && !na.scale.empty()) {
                ir::Vec3Track scaleTrack = na.scale;
                insertSequenceBoundaryKeys(scaleTrack, irModel.sequences);
                insertScaleKeys(node, scaleTrack, Point3(1.0f, 1.0f, 1.0f));
            }
        }

        // Camera animations: KCTR (camera pos), KTTR (target pos), KCRL (roll)
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
                // Tangents are derivatives — they must NOT be offset.
                if (opts.core.importTranslation && irCam.positionTrackIndex >= 0 &&
                    irCam.positionTrackIndex < static_cast<int32_t>(irModel.vec3Tracks.size())) {
                    const auto& track = irModel.vec3Tracks[irCam.positionTrackIndex];
                    if (!track.empty()) {
                        ir::Vec3Track adjustedTrack = track;
                        Point3 startPos = irCam.position;
                        for (auto& k : adjustedTrack.keys)
                            k.value += startPos;
                        insertSequenceBoundaryKeys(adjustedTrack, irModel.sequences);
                        insertTranslationKeys(pair.cameraNode, adjustedTrack, startPos, boundaryTimes);
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
                        for (auto& k : adjustedTrack.keys)
                            k.value += startPos;
                        insertSequenceBoundaryKeys(adjustedTrack, irModel.sequences);
                        insertTranslationKeys(pair.targetNode, adjustedTrack, startPos, boundaryTimes);
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
                            // Camera sub-anim layout: [0]=Node, [1]=Material, [2]=CameraObject
                            // CameraObject sub-anims: [0]=FOV, [1]=Roll Angle
                            Animatable* camSubAnim = pair.cameraNode->SubAnim(2); // Camera object
                            if (camSubAnim) {
                                Animatable* rollSubAnim = camSubAnim->SubAnim(1); // Roll angle
                                if (rollSubAnim) {
                                    rollSubAnim->AssignController(rollCtrl, 0);
                                    ILOG << "  cam[" << ci << "] KCRL keys=" << track.keys.size() << "\n";
                                }
                            }
                        }
                    }
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
                                int ctrlType = 1; // None
                                switch (srcTrack.interpolation) {
                                case ir::InterpolationType::Linear:  ctrlType = 2; break;
                                case ir::InterpolationType::Hermite: ctrlType = 4; break;
                                default:                             ctrlType = 3; break; // Bezier
                                }
                                auto pCtrl = findPBParam(ref, L"opacityCtrl");
                                if (pCtrl) pCtrl.pb->SetValue(pCtrl.id, 0, ctrlType);
                            }

                            ILOG << "  mat[" << mi << "] layer[" << li << "] KMTA keys="
                                 << srcTrack.keys.size() << "\n";
                        }
                    }

                    // Texture animation (KTAT translation, KTAR rotation, KTAS scale)
                    // Controllers are written to BOTH targets:
                    //   1. BitmapTex StdUVGen  (direct viewport animation)
                    //   2. Wc3Material params  (exporter round-trip)
                    // For composites with shared BitmapTex instances, write to the
                    // bitmap only ONCE (first layer). Use materialControllersUpdate()
                    // on subsequent layers to sync from the shared bitmap.
                    if (layer.textureAnimationIndex >= 0 &&
                        layer.textureAnimationIndex < (int32_t)irModel.textureAnimations.size()) {
                        const auto& ta = irModel.textureAnimations[layer.textureAnimationIndex];

                        // Get the diffuse BitmapTex for direct StdUVGen access
                        BitmapTex* diffuseBmp = getDiffuseBitmapTex(ref);
                        StdUVGen* uvGen = diffuseBmp ? diffuseBmp->GetUVGen() : nullptr;

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

                        // KTAT: UV offset — after swizzle: ch1=-X→U_Offset, ch0=Y→V_Offset
                        // NeoDex applies the same transform during read (readMDXPosition).
                        // Result: U_Offset = -X_original, V_Offset = Y_original.
                        //
                        // Z channel: StdUVGen has NO W_Offset property (only W_Angle).
                        // For 2D bitmaps the W component is not renderable in Max — but we
                        // still preserve it on the Wc3Material.anim_WOffset custom attribute
                        // so the exporter can round-trip the original MDX KTAT untouched.
                        if (ta.translationTrackIndex >= 0 &&
                            ta.translationTrackIndex < (int32_t)irModel.vec3Tracks.size()) {
                            const auto& v3track = irModel.vec3Tracks[ta.translationTrackIndex];
                            if (!v3track.empty()) {
                                if (!bitmapAlreadyProcessed) {
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

                                    // Also set on Wc3Material params (includes W for round-trip)
                                    if (uCtrl) {
                                        auto p = findPBParam(ref, L"anim_UOffset");
                                        if (p) p.pb->SetControllerByID(p.id, 0, uCtrl, FALSE);
                                    }
                                    if (vCtrl) {
                                        auto p = findPBParam(ref, L"anim_VOffset");
                                        if (p) p.pb->SetControllerByID(p.id, 0, vCtrl, FALSE);
                                    }
                                    if (wCtrl) {
                                        auto p = findPBParam(ref, L"anim_WOffset");
                                        if (p) p.pb->SetControllerByID(p.id, 0, wCtrl, FALSE);
                                    }
                                } else {
                                    // Shared bitmap already has controllers from first layer.
                                    // Create same controllers for this sub-material's params only.
                                    auto uTrack = splitVec3Channel(v3track, 1);
                                    auto vTrack = splitVec3Channel(v3track, 0);
                                    auto wTrack = splitVec3Channel(v3track, 2);
                                    Control* uCtrl = createFloatController(uTrack);
                                    Control* vCtrl = createFloatController(vTrack);
                                    Control* wCtrl = createFloatController(wTrack);
                                    if (uCtrl) {
                                        auto p = findPBParam(ref, L"anim_UOffset");
                                        if (p) p.pb->SetControllerByID(p.id, 0, uCtrl, FALSE);
                                    }
                                    if (vCtrl) {
                                        auto p = findPBParam(ref, L"anim_VOffset");
                                        if (p) p.pb->SetControllerByID(p.id, 0, vCtrl, FALSE);
                                    }
                                    if (wCtrl) {
                                        auto p = findPBParam(ref, L"anim_WOffset");
                                        if (p) p.pb->SetControllerByID(p.id, 0, wCtrl, FALSE);
                                    }
                                }

                                ILOG << "  mat[" << mi << "] layer[" << li << "] KTAT keys="
                                     << v3track.keys.size()
                                     << " (direct-to-bitmap: " << (uvGen ? "yes" : "no")
                                     << ", shared: " << (bitmapAlreadyProcessed ? "yes" : "no") << ")\n";
                            }
                        }

                        // KTAR: UV rotation — quaternion → Z-euler angle → W_Angle (degrees)
                        if (ta.rotationTrackIndex >= 0 &&
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

                                if (!bitmapAlreadyProcessed) {
                                    // Write directly to BitmapTex StdUVGen
                                    if (uvGen && wCtrl)
                                        assignControllerToUVGen(uvGen, L"W_Angle", 6, wCtrl);
                                }

                                // Always set on Wc3Material param
                                if (wCtrl) {
                                    auto p = findPBParam(ref, L"anim_WAngle");
                                    if (p) p.pb->SetControllerByID(p.id, 0, wCtrl, FALSE);
                                }

                                ILOG << "  mat[" << mi << "] layer[" << li << "] KTAR keys="
                                     << qtrack.keys.size()
                                     << " (direct-to-bitmap: " << (uvGen ? "yes" : "no") << ")\n";
                            }
                        }

                        // KTAS: UV scale — X → U_Tiling, Y → V_Tiling
                        if (ta.scaleTrackIndex >= 0 &&
                            ta.scaleTrackIndex < (int32_t)irModel.vec3Tracks.size()) {
                            const auto& v3track = irModel.vec3Tracks[ta.scaleTrackIndex];
                            if (!v3track.empty()) {
                                auto uTrack = splitVec3Channel(v3track, 0);
                                auto vTrack = splitVec3Channel(v3track, 1);
                                Control* uCtrl = createFloatController(uTrack);
                                Control* vCtrl = createFloatController(vTrack);

                                if (!bitmapAlreadyProcessed) {
                                    // Write directly to BitmapTex StdUVGen
                                    if (uvGen && uCtrl)
                                        assignControllerToUVGen(uvGen, L"U_Tiling", 2, uCtrl);
                                    if (uvGen && vCtrl)
                                        assignControllerToUVGen(uvGen, L"V_Tiling", 3, vCtrl);
                                }

                                // Always set on Wc3Material params
                                if (uCtrl) {
                                    auto p = findPBParam(ref, L"anim_UTiling");
                                    if (p) p.pb->SetControllerByID(p.id, 0, uCtrl, FALSE);
                                }
                                if (vCtrl) {
                                    auto p = findPBParam(ref, L"anim_VTiling");
                                    if (p) p.pb->SetControllerByID(p.id, 0, vCtrl, FALSE);
                                }

                                ILOG << "  mat[" << mi << "] layer[" << li << "] KTAS keys="
                                     << v3track.keys.size()
                                     << " (direct-to-bitmap: " << (uvGen ? "yes" : "no") << ")\n";
                            }
                        }

                        // Set uvCtrlType dropdown to match interpolation type
                        // Dropdown: 1=None, 2=Linear, 3=Bezier, 4=Hermite
                        // Determine from KTAT, KTAR, or KTAS (first one found)
                        {
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
                                int ctrlType = 1; // None
                                switch (interpType) {
                                case ir::InterpolationType::Linear:  ctrlType = 2; break;
                                case ir::InterpolationType::Hermite: ctrlType = 4; break;
                                default:                             ctrlType = 3; break; // Bezier
                                }
                                auto pUV = findPBParam(ref, L"uvCtrlType");
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
            // We do NOT call applyFilterMode() / generateOpacity() / materialControllersUpdate()
            // here — those trigger coords refreshes (opMap.coords.U_Tile = true) that
            // wipe animation controllers we just set up. The filter mode and opacity
            // map were already configured in C++ during material build.
            ExecuteMAXScriptScript(
                _M("for m in sceneMaterials do ("
                     "local isComp = try(m.materialList != undefined)catch(false);"
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
                         "local tex = try(sub.diffuseMap)catch(undefined);"
                         "if tex != undefined do try(showTextureMap m tex true)catch();"
                         "try(sub.delegate.opacity = sub.opacity)catch();"
                         "local aCtrl = try(sub.opacity.controller)catch(undefined);"
                         "if aCtrl != undefined do try(sub.delegate.opacity.controller = aCtrl)catch()"
                       ")"
                     ");"
                     "if not isComp do ("
                       "try(m.showInViewport = true)catch()"
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
            // runs and RESTORE them afterwards. Same for Wc3Material params
            // (anim_UOffset/VOffset/WAngle + opacity).
            {
                std::wstring tilingScript =
                    L"( "
                    // Phase A1: save bitmap coords controllers
                    L"local savedBmpCtrls = #();"
                    L"for bm in getClassInstances Bitmaptexture do ("
                      L"local uC = try(bm.coords.U_Offset.controller)catch(undefined);"
                      L"local vC = try(bm.coords.V_Offset.controller)catch(undefined);"
                      L"local wC = try(bm.coords.W_Angle.controller)catch(undefined);"
                      L"if uC != undefined or vC != undefined or wC != undefined do "
                        L"append savedBmpCtrls #(bm, uC, vC, wC)"
                    L");"
                    // Phase A2: save Wc3Material anim_* + opacity controllers
                    L"local savedMatCtrls = #();"
                    L"fn saveMatCtrl m = ("
                      L"local uC = try(m.anim_UOffset.controller)catch(undefined);"
                      L"local vC = try(m.anim_VOffset.controller)catch(undefined);"
                      L"local wC = try(m.anim_WAngle.controller)catch(undefined);"
                      L"local oC = try(m.opacity.controller)catch(undefined);"
                      L"if uC != undefined or vC != undefined or wC != undefined or oC != undefined do "
                        L"append savedMatCtrls #(m, uC, vC, wC, oC)"
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
                    // Phase C2: restore Wc3Material anim_* + opacity controllers
                    L"for entry in savedMatCtrls do ("
                      L"local m = entry[1];"
                      L"if entry[2] != undefined do "
                        L"try(m.anim_UOffset.controller = entry[2])catch();"
                      L"if entry[3] != undefined do "
                        L"try(m.anim_VOffset.controller = entry[3])catch();"
                      L"if entry[4] != undefined do "
                        L"try(m.anim_WAngle.controller = entry[4])catch();"
                      L"if entry[5] != undefined do "
                        L"try(m.opacity.controller = entry[5])catch()"
                    L");"
                    // Phase C3: push material UV controllers down to diffuseMap.coords.
                    // This is needed because the bitmap's own controllers may have
                    // been wiped by a paramblock refresh, while the material's
                    // controllers survived. Only push when the material controller
                    // actually has keys (numKeys > 0) — otherwise an empty default
                    // controller would overwrite a valid one on a shared bitmap.
                    L"local pushed = 0;"
                    L"for entry in savedMatCtrls do ("
                      L"local m = entry[1];"
                      L"local dm = try(m.diffuseMap)catch(undefined);"
                      L"if dm == undefined do continue;"
                      L"local uC = entry[2];"
                      L"if uC != undefined and (try(numKeys uC)catch(0)) > 0 do ("
                        L"try(dm.coords.U_Offset.controller = uC)catch(); pushed += 1"
                      L");"
                      L"local vC = entry[3];"
                      L"if vC != undefined and (try(numKeys vC)catch(0)) > 0 do ("
                        L"try(dm.coords.V_Offset.controller = vC)catch(); pushed += 1"
                      L");"
                      L"local wC = entry[4];"
                      L"if wC != undefined and (try(numKeys wC)catch(0)) > 0 do ("
                        L"try(dm.coords.W_Angle.controller = wC)catch(); pushed += 1"
                      L")"
                    L");"
                    L"format \"[MDLX] Tiling fix: % bitmaps, bmp ctrls=%, mat ctrls=%, pushed=%\\n\" fixed savedBmpCtrls.count savedMatCtrls.count pushed"
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
                _M("try ("
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
                    if (!track.empty()) {
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

                        std::wstring nodeName(meshNode->GetName());
                        std::wstringstream ss;
                        ss << L"(local n = getNodeByName \"" << nodeName << L"\";"
                           << L"if n != undefined do ("
                           << L"for m in n.modifiers do ("
                           << L"if (classOf m) as string == \"Wc3VertexMod\" do ("
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

            // Object visibility: lights, attachments, PE1, PE2, ribbons
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

                ILOG << "  " << type << " node[" << nodeIndex << "] '"
                     << narrow(node->GetName()) << "' keys=" << track.keys.size()
                     << " interp=" << static_cast<int>(track.interpolation) << "\n";
                insertVisibilityKeys(node, track, irModel.sequences);
            };

            for (const auto& light : irModel.lights)
                applyObjVis(light.nodeIndex, light.visibilityTrackIndex, "light");
            for (const auto& att : irModel.attachments)
                applyObjVis(att.nodeIndex, att.visibilityTrackIndex, "attachment");
            for (const auto& pe : irModel.particleEmitters)
                applyObjVis(pe.nodeIndex, pe.visibilityTrackIndex,
                            pe.variant == 2 ? "pe2" : "pe1");
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
                animateFloatPB(pb, RB_PB_TEX_SLOT,     rib.textureSlotTrackIndex, irModel);
            }

            // Lights: attStart, attEnd, color, intensity, ambColor, ambIntensity
            //
            // Wc3Light ist ein scripted simpleManipulator → ParamBlock-Controller-
            // Assignment ist unzuverlässig. Gleiche Lösung wie bei KGAC: MaxScript-
            // Route über *NamedScript-Helpers. Die setzen `n.<param>.controller =
            // bezier_float/bezier_color` und fügen Keys via addNewKey hinzu.
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
    {
        mdx_scene::Wc3SequenceBuilder seqBuilder;
        seqBuilder.buildSequences(irModel, gi, reporter);
    }

    // 15. Organize imported nodes into type-based layers
    // (Geometry, Bones, Attachments, Events, Lights, Cameras,
    //  Particle Emitters 1/2, Ribbon Emitters, Popcorn FX, FaceFX,
    //  Collision Shapes, Helpers / Dummies) — matches WdxNodeManager.
    organizeNodesIntoLayers(gi);

    // 16. Report
    if (reporter.hasWarnings() || reporter.hasErrors()) {
        reporter.showSummaryDialog(gi->GetMAXHWnd());
    }

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
            Interval range(0, maxEnd);
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
            Interval range(0, maxGS);
            gi->SetAnimRange(range);
            ILOG << "  Animation range (global seq) set to 0 - " << maxGS
                 << " ticks (" << (maxGS / GetTicksPerFrame()) << " frames)\n";
        }
    }

    gi->ForceCompleteRedraw();

    ILOG << "\n==== Import Complete ====\n";
    ILOG.flush();

    return IMPEXP_SUCCESS;
}
