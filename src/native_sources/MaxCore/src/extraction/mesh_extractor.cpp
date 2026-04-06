// MaxCore — Mesh geometry extractor implementation
// Based on the extraction pattern from WhiteoutDexRenderer/src/extract.cpp
#include "mesh_extractor.h"
#include "modifier_reader.h"
#include "../util/max_helpers.h"
#include "../util/class_ids.h"

#include <triobj.h>
#include <MeshNormalSpec.h>
#include <iskin.h>
#include <unordered_map>

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

    // Find Skin modifier — disable it to get bind-pose vertices
    Modifier* skinMod = core::findModifierByClassID(node, core_ids::SKIN_CLASS_ID);
    if (skinMod) skinMod->DisableMod();

    // Evaluate node to TriObject
    ObjectState os = node->EvalWorldState(t);
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
        extractSkinWeights(node, result, faceVertMap, reporter);
    }

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
