// MaxCore — Mesh geometry extractor implementation
// Based on the extraction pattern from WhiteoutDexRenderer/src/extract.cpp
#include "mesh_extractor.h"
#include "modifier_reader.h"
#include "../util/max_helpers.h"
#include "../util/class_ids.h"
#include "../animation/controller_reader.h"

#include <triobj.h>
#include <MeshNormalSpec.h>
#include <iskin.h>
#include <unordered_map>
#include <fstream>
#include <cmath>
#include <windows.h>

// Debug logging for mesh extraction
static std::ofstream& meshLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        std::string path = std::string(tmp) + "mdlx_mesh_debug.log";
        log.open(path, std::ios::trunc);
    }
    return log;
}
#define MLOG meshLog()
#define MFLUSH meshLog().flush()

namespace core {

ir::Mesh MeshExtractor::extract(INode* node, int nodeIndex, TimeValue t,
                                ExportErrorReporter& reporter) {
    ir::Mesh result;
    result.nodeIndex = nodeIndex;

    // Use node name
    const MCHAR* nodeName = node->GetName();
    if (nodeName) {
        std::wstring wname(nodeName);
        result.name.assign(wname.begin(), wname.end());
    }

    // LOD round-trip (v1000+ only, safe no-op on v800):
    // Read UserProps written by the importer. Defaults (0 / empty string) are
    // the correct values for newly-created meshes and for v800 round-trips.
    // Matches the NeoDex behavior (writes lod=0) but additionally preserves
    // non-zero LOD levels that our importer persisted.
    {
        int lodLevel = 0;
        BOOL hasLodLevel = node->GetUserPropInt(_T("Wc3GeosetLod"), lodLevel);
        result.lod = lodLevel;

        MSTR lodNameStr;
        BOOL hasLodName = node->GetUserPropString(_T("Wc3LodName"), lodNameStr);
        if (hasLodName) {
            // MSTR -> std::string (ASCII-safe for lodName labels like "head")
            std::wstring w(lodNameStr.data());
            result.lodName.assign(w.begin(), w.end());
        }

        // DEDICATED LOD DEBUG LOG — writes to %TEMP%\mdlx_lod_debug.log so we
        // can trace exactly which UserProps were found on each mesh.
        {
            char tempPath[MAX_PATH];
            GetTempPathA(MAX_PATH, tempPath);
            std::string lodLogPath = std::string(tempPath) + "mdlx_lod_debug.log";
            std::ofstream lodLog(lodLogPath, std::ios::app);
            if (lodLog.is_open()) {
                char meshName[256] = {};
                if (nodeName) WideCharToMultiByte(CP_UTF8, 0, nodeName, -1, meshName, 255, nullptr, nullptr);
                lodLog << "[MESH EXTRACT] '" << meshName << "'"
                       << " nodeIdx=" << nodeIndex
                       << " hasLodProp=" << (hasLodLevel ? "yes" : "NO")
                       << " hasLodNameProp=" << (hasLodName ? "yes" : "NO")
                       << " lod=" << result.lod
                       << " lodName='" << result.lodName << "'"
                       << "\n";
            }
        }
    }

    // Debug: log mesh info
    char nameBuf[256] = {};
    if (nodeName) WideCharToMultiByte(CP_UTF8, 0, nodeName, -1, nameBuf, 255, nullptr, nullptr);
    MLOG << "\n=== MESH '" << nameBuf << "' nodeIdx=" << nodeIndex << " ===\n";
    MLOG << "  LOD=" << result.lod << " lodName='" << result.lodName << "'\n";

    // Log transforms
    {
        Matrix3 nodeTM = node->GetNodeTM(t);
        Matrix3 objTM = node->GetObjectTM(t);
        float nodeRow0 = Length(nodeTM.GetRow(0));
        float objRow0 = Length(objTM.GetRow(0));
        Point3 nodePos = nodeTM.GetRow(3);
        Point3 objPos = objTM.GetRow(3);
        // Check for negative scale (mirroring)
        float nodeDet = DotProd(nodeTM.GetRow(0), CrossProd(nodeTM.GetRow(1), nodeTM.GetRow(2)));
        float objDet = DotProd(objTM.GetRow(0), CrossProd(objTM.GetRow(1), objTM.GetRow(2)));
        MLOG << "  NodeTM  row0Len=" << nodeRow0 << " pos=(" << nodePos.x << "," << nodePos.y << "," << nodePos.z << ") det=" << nodeDet << "\n";
        MLOG << "  ObjTM   row0Len=" << objRow0 << " pos=(" << objPos.x << "," << objPos.y << "," << objPos.z << ") det=" << objDet << "\n";
        if (nodeDet < 0) MLOG << "  ** NEGATIVE DET (mirrored) in NodeTM **\n";
        if (objDet < 0) MLOG << "  ** NEGATIVE DET (mirrored) in ObjTM **\n";
    }
    MFLUSH;

    // Find Skin modifier — disable it to get bind-pose vertices
    Modifier* skinMod = core::findModifierByClassID(node, core_ids::SKIN_CLASS_ID);
    MLOG << "  Skin modifier: " << (skinMod ? "FOUND" : "NOT FOUND") << "\n";

    // Check if any skin bone has a Link Constraint controller.
    // Link Constraint bones change parent during animation, so the raw mesh
    // position (skin disabled) doesn't match the frame-0 bone position.
    // In that case, we read the SKINNED mesh at frame 0 instead.
    bool useSkinned = false;
    if (skinMod) {
        ISkin* skinCheck = static_cast<ISkin*>(skinMod->GetInterface(I_SKIN));
        if (skinCheck) {
            int numBones = skinCheck->GetNumBones();
            MLOG << "  Checking " << numBones << " skin bones for Link Constraint...\n";
            for (int b = 0; b < numBones; ++b) {
                INode* boneNode = skinCheck->GetBone(b);
                if (boneNode) {
                    Control* boneTmCtrl = boneNode->GetTMController();
                    char bnBuf[256] = {};
                    const MCHAR* bn = boneNode->GetName();
                    if (bn) WideCharToMultiByte(CP_UTF8, 0, bn, -1, bnBuf, 255, nullptr, nullptr);

                    // Check both ControllerReader and direct ClassID match
                    // Link Constraint ClassID = (2269112164, 2864612865) = (0x87366764, 0xAAD50281)
                    bool isLC = false;
                    if (boneTmCtrl) {
                        isLC = core::ControllerReader::isLinkConstraint(boneTmCtrl);
                        if (!isLC) {
                            Class_ID lcID(2269112164u, 2864612865u);
                            isLC = (boneTmCtrl->ClassID() == lcID);
                        }
                    }
                    Class_ID cid = boneTmCtrl ? boneTmCtrl->ClassID() : Class_ID(0,0);
                    MLOG << "    bone[" << b << "] '" << bnBuf << "'"
                         << " tmCtrl=" << (boneTmCtrl ? "yes" : "null")
                         << " classID=(" << cid.PartA() << "," << cid.PartB() << ")"
                         << " isLink=" << (isLC ? "YES" : "no") << "\n";

                    if (isLC) {
                        useSkinned = true;
                        MLOG << "  ** Link Constraint detected → using skinned bind pose **\n";
                        break;
                    }
                }
            }
        } else {
            MLOG << "  ISkin interface not available\n";
        }
    }
    MFLUSH;

    if (!useSkinned && skinMod) {
        skinMod->DisableMod();
    }

    // Evaluate node to TriObject
    // For skinned bind pose: evaluate at frame 0 to get the correct deformed positions
    TimeValue evalTime = useSkinned ? 0 : t;
    ObjectState os = node->EvalWorldState(evalTime);
    Object* obj = os.obj;

    if (!obj || !obj->CanConvertToType(triObjectClassID)) {
        if (skinMod) skinMod->EnableMod();
        reporter.warning(L"Node '" + std::wstring(nodeName ? nodeName : L"<unnamed>") +
                         L"' cannot be converted to triangle mesh.");
        return result;
    }

    TriObject* triObj = static_cast<TriObject*>(obj->ConvertToType(t, triObjectClassID));
    if (!triObj) {
        if (skinMod) skinMod->EnableMod();
        return result;
    }

    ::Mesh& mesh = triObj->GetMesh();
    mesh.buildNormals();

    // Check for Edit_Normals modifier
    bool hasEditNormals = (ModifierReader::findEditNormals(node) != nullptr);

    // Extract geometry (positions, normals, indices), expanding per-face vertices
    std::vector<int> faceVertMap;
    int numFaces = mesh.getNumFaces();
    int numVerts = numFaces * 3;
    faceVertMap.resize(numVerts);

    // Get face vertex normal spec for edit normals
    MeshNormalSpec* specN = mesh.GetSpecifiedNormals();
    bool hasSpecN = specN && specN->GetNumNormals() > 0;

    result.vertices.resize(numVerts);
    result.indices.resize(numVerts);

    for (int f = 0; f < numFaces; ++f) {
        Face& face = mesh.faces[f];

        // Check for degenerate face
        Point3 v0 = mesh.verts[face.v[0]];
        Point3 v1 = mesh.verts[face.v[1]];
        Point3 v2 = mesh.verts[face.v[2]];
        Point3 edge1 = v1 - v0;
        Point3 edge2 = v2 - v0;
        Point3 cross = CrossProd(edge1, edge2);
        if (Length(cross) < 1e-8f) {
            reporter.warning(L"Degenerate triangle at face " + std::to_wstring(f) +
                             L" on node '" + std::wstring(nodeName ? nodeName : L"") + L"'.");
        }

        for (int v = 0; v < 3; ++v) {
            int outIdx = f * 3 + v;
            int origV = face.v[v];
            faceVertMap[outIdx] = origV;

            ir::Vertex& vert = result.vertices[outIdx];
            vert.position = mesh.verts[origV];

            // Normal: prefer specified normals (Edit Normals), else smoothing group
            if (hasSpecN) {
                vert.normal = Normalize(specN->GetNormal(f, v));
            } else {
                vert.normal = Normalize(getVNormal(mesh, f, origV));
            }

            result.indices[outIdx] = outIdx;
        }
    }

    // Transform vertices & normals from object-space to world-space.
    // For unskinned extraction: vertices are in object space → apply GetObjectTM.
    // For skinned extraction (Link Constraint): Skin deforms into node-local space,
    // so apply GetNodeTM (NOT GetObjectTM which would double-apply object offset).
    if (!useSkinned) {
        Matrix3 worldTM = node->GetObjectTM(t);
        Matrix3 normalTM = worldTM;
        normalTM.NoTrans();
        for (auto& vert : result.vertices) {
            vert.position = vert.position * worldTM;
            vert.normal = Normalize(vert.normal * normalTM);
        }
        MLOG << "  Transform: GetObjectTM applied (unskinned path)\n";
    } else {
        Matrix3 nodeTM = node->GetNodeTM(0);
        Matrix3 normalTM = nodeTM;
        normalTM.NoTrans();
        for (auto& vert : result.vertices) {
            vert.position = vert.position * nodeTM;
            vert.normal = Normalize(vert.normal * normalTM);
        }
        MLOG << "  Transform: GetNodeTM(0) applied (skinned bind pose path)\n";
    }

    // Debug: log vertex bounds after world transform
    {
        Point3 minP(1e30f, 1e30f, 1e30f), maxP(-1e30f, -1e30f, -1e30f);
        for (const auto& vert : result.vertices) {
            if (vert.position.x < minP.x) minP.x = vert.position.x;
            if (vert.position.y < minP.y) minP.y = vert.position.y;
            if (vert.position.z < minP.z) minP.z = vert.position.z;
            if (vert.position.x > maxP.x) maxP.x = vert.position.x;
            if (vert.position.y > maxP.y) maxP.y = vert.position.y;
            if (vert.position.z > maxP.z) maxP.z = vert.position.z;
        }
        Point3 size = maxP - minP;
        MLOG << "  Faces=" << numFaces << " Verts=" << numVerts << "\n";
        MLOG << "  World bounds: min=(" << minP.x << "," << minP.y << "," << minP.z
             << ") max=(" << maxP.x << "," << maxP.y << "," << maxP.z << ")\n";
        MLOG << "  Size=(" << size.x << "," << size.y << "," << size.z << ")\n";
        if (numVerts > 0) {
            MLOG << "  Sample v[0]=(" << result.vertices[0].position.x << ","
                 << result.vertices[0].position.y << "," << result.vertices[0].position.z << ")\n";
        }
        MFLUSH;
    }

    // Extract UV sets (channels 1-4)
    int uvSetCount = 0;
    for (int ch = 1; ch <= 4; ++ch) {
        if (mesh.getNumMapVerts(ch) > 0) {
            extractUVSet(mesh, ch, uvSetCount, result);
            uvSetCount++;
        }
    }
    for (auto& vert : result.vertices) {
        vert.uvSetCount = uvSetCount;
    }

    // Clean up converted TriObject
    if (triObj != os.obj) triObj->DeleteThis();

    // Re-enable Skin AFTER all mesh data is extracted
    if (skinMod) skinMod->EnableMod();

    // Extract skin weights (uses original vertex indices via faceVertMap)
    if (skinMod) {
        MLOG << "  Extracting skin weights...\n";
        MFLUSH;
        extractSkinWeights(node, result, faceVertMap, reporter);
        // Count bones used
        int maxBoneIdx = -1;
        for (const auto& vert : result.vertices) {
            for (const auto& inf : vert.skinInfluences) {
                if (inf.weight > 0 && inf.boneIndex > maxBoneIdx)
                    maxBoneIdx = inf.boneIndex;
            }
        }
        MLOG << "  Skin: maxBoneIndex=" << maxBoneIdx << "\n";
    } else {
        MLOG << "  No skin — unskinned mesh\n";
    }

    MLOG << "  DONE mesh '" << nameBuf << "'\n";
    MFLUSH;

    return result;
}

void MeshExtractor::extractUVSet(::Mesh& mesh, int channel, int setIndex, ir::Mesh& out) {
    int numFaces = mesh.getNumFaces();
    for (int f = 0; f < numFaces; ++f) {
        TVFace& tvf = mesh.mapFaces(channel)[f];
        for (int v = 0; v < 3; ++v) {
            int outIdx = f * 3 + v;
            UVVert uv = mesh.mapVerts(channel)[tvf.t[v]];
            out.vertices[outIdx].uvSets[setIndex] = Point2(uv.x, 1.0f - uv.y);
        }
    }
}

void MeshExtractor::extractSkinWeights(INode* node, ir::Mesh& out,
                                        const std::vector<int>& faceVertMap,
                                        ExportErrorReporter& reporter) {
    ISkin* skin = ModifierReader::findSkin(node);
    if (!skin) return;

    ISkinContextData* ctx = skin->GetContextInterface(node);
    if (!ctx) {
        const MCHAR* name = node->GetName();
        reporter.warning(L"Failed to get ISkin context for '" +
                         std::wstring(name ? name : L"<unnamed>") + L"'.");
        return;
    }

    int origVertCount = ctx->GetNumPoints();
    int numBones = skin->GetNumBones();

    MLOG << "    SkinWeights: origVerts=" << origVertCount << " numBones=" << numBones << "\n";
    for (int b = 0; b < numBones && b < 5; ++b) {
        INode* boneNode = skin->GetBone(b);
        if (boneNode) {
            char bn[256] = {};
            const MCHAR* bname = boneNode->GetName();
            if (bname) WideCharToMultiByte(CP_UTF8, 0, bname, -1, bn, 255, nullptr, nullptr);
            MLOG << "      bone[" << b << "] '" << bn << "'\n";
        }
    }
    if (numBones > 5) MLOG << "      ... +" << (numBones - 5) << " more\n";
    MFLUSH;

    // Build original vertex weights (up to 4 influences per vert)
    struct OrigWeights {
        int32_t boneIndices[4] = {0, 0, 0, 0};
        float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    };
    std::vector<OrigWeights> origWeights(origVertCount);

    for (int v = 0; v < origVertCount; ++v) {
        int nw = ctx->GetNumAssignedBones(v);
        float totalW = 0.0f;
        int maxInf = std::min(nw, 4);
        for (int b = 0; b < maxInf; ++b) {
            int boneIdx = ctx->GetAssignedBone(v, b);
            origWeights[v].boneIndices[b] = boneIdx;
            origWeights[v].weights[b] = ctx->GetBoneWeight(v, b);
            totalW += origWeights[v].weights[b];
        }

        // Note: boneIndices here are ISkin bone indices (0..numBones-1)
        // They will be remapped to ir::Bone indices in a later pass by the backend.

        if (totalW < 0.001f) {
            const MCHAR* name = node->GetName();
            reporter.warning(L"Vertex " + std::to_wstring(v) +
                             L" on '" + std::wstring(name ? name : L"") +
                             L"' has zero skin weights.");
        }
    }

    // Map expanded vertices back to original vertex weights
    for (size_t i = 0; i < faceVertMap.size(); ++i) {
        int origV = faceVertMap[i];
        if (origV < 0 || origV >= origVertCount) continue;

        auto& ow = origWeights[origV];
        auto& vert = out.vertices[i];
        for (int b = 0; b < 4; ++b) {
            if (ow.weights[b] > 0.0f) {
                ir::SkinInfluence inf;
                inf.boneIndex = ow.boneIndices[b];
                inf.weight = ow.weights[b];
                vert.skinInfluences.push_back(inf);
            }
        }
    }

    // Store the ISkin bone node pointers for later resolution
    // The backend will need to map ISkin bone indices → ir::Bone indices
    // This is done by BoneExtractor which collects all skin bone INode*s
}

Point3 MeshExtractor::getVNormal(::Mesh& mesh, int faceIdx, int vertIdx) {
    DWORD smGroup = mesh.faces[faceIdx].getSmGroup();
    if (smGroup == 0) return mesh.getFaceNormal(faceIdx);

    Point3 n(0, 0, 0);
    int numFaces = mesh.getNumFaces();
    for (int f = 0; f < numFaces; ++f) {
        if (mesh.faces[f].getSmGroup() & smGroup) {
            for (int v = 0; v < 3; ++v) {
                if (mesh.faces[f].v[v] == static_cast<DWORD>(vertIdx)) {
                    n += mesh.getFaceNormal(f);
                    break;
                }
            }
        }
    }
    return Normalize(n);
}

} // namespace core
