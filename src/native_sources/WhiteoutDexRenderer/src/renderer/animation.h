#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Skinning System
// Phase 4: Matrix-based GPU vertex skinning
//
// Adapter evaluates hierarchy → sends node world matrices per frame → GPU skins.
// Standard skinning: V' = sum(w_i * offset_i * V)
// where offset_i = currentMatrix_i * inverseBindMatrix_i
// ============================================================================

#include "types.h"
#include <unordered_map>

namespace WhiteoutDex {

// ============================================================================
// Per-vertex node influences (max 4, standard limit)
// ============================================================================
struct VertexInfluence {
    int   boneIdx[4] = {0, 0, 0, 0};   // node indices into the hierarchy palette
    float weight[4]  = {0, 0, 0, 0};
};

// ============================================================================
// Per-geoset skin weight data
// ============================================================================
struct GeosetSkinInfo {
    std::vector<VertexInfluence> vertices;
};

// ============================================================================
// Skinning System
// ============================================================================
class SkinningSystem {
public:
    void Clear() {
        inverseBindMatrices_.clear();
        currentMatrices_.clear();
        offsetMatrices_.clear();
        geosetWeights_.clear();
        nodeCount_ = 0;
        matricesDirty_ = false;
        nodesReady_ = false;
    }

    // ---- Setup (called once from API thread) ----

    void SetSkeleton(int nodeCount, const float* inverseBindData) {
        nodeCount_ = nodeCount;
        inverseBindMatrices_.resize(nodeCount);
        currentMatrices_.resize(nodeCount);
        offsetMatrices_.resize(nodeCount);
        nodesReady_ = false;  // Don't allow skinning until UpdateNodeMatrices is called

        for (int i = 0; i < nodeCount; i++) {
            // Load 16 floats as row-major 4x4 matrix
            const float* m = inverseBindData + i * 16;
            Matrix44f& mat = inverseBindMatrices_[i];
            mat.data[0] = {m[0], m[1], m[2],  m[3]};
            mat.data[1] = {m[4], m[5], m[6],  m[7]};
            mat.data[2] = {m[8], m[9], m[10], m[11]};
            mat.data[3] = {m[12],m[13],m[14], m[15]};
            currentMatrices_[i]  = Matrix44f::identity();
            offsetMatrices_[i]   = Matrix44f::identity(); // Safe default until nodes arrive
        }
    }

    void SetGeosetWeights(int geosetId, int vertCount,
                          const int* nodeIndices, const float* weights) {
        GeosetSkinInfo& info = geosetWeights_[geosetId];
        info.vertices.resize(vertCount);
        for (int v = 0; v < vertCount; v++) {
            for (int j = 0; j < 4; j++) {
                info.vertices[v].boneIdx[j] = nodeIndices[v * 4 + j];
                info.vertices[v].weight[j]  = weights[v * 4 + j];
            }
        }
    }

    // ---- Per-frame update (called from API thread via SetTime path) ----

    void UpdateNodeMatrices(int nodeCount, const float* worldData) {
        if (nodeCount != nodeCount_) return;
        for (int i = 0; i < nodeCount; i++) {
            const float* m = worldData + i * 16;
            Matrix44f& mat = currentMatrices_[i];
            mat.data[0] = {m[0], m[1], m[2],  m[3]};
            mat.data[1] = {m[4], m[5], m[6],  m[7]};
            mat.data[2] = {m[8], m[9], m[10], m[11]};
            mat.data[3] = {m[12],m[13],m[14], m[15]};
        }
        matricesDirty_ = true;
        nodesReady_ = true;  // Safe to start skinning now
    }

    // ---- Query ----

    bool HasSkeleton()              const { return nodeCount_ > 0; }
    bool IsReady()                  const { return nodesReady_; }
    int  NodeCount()                const { return nodeCount_; }
    bool HasWeights(int geosetId)   const { return geosetWeights_.count(geosetId) > 0; }
    bool NeedsUpdate()              const { return matricesDirty_; }

    const GeosetSkinInfo* GetGeosetWeights(int geosetId) const {
        auto it = geosetWeights_.find(geosetId);
        return (it != geosetWeights_.end()) ? &it->second : nullptr;
    }

    const Matrix44f* OffsetMatrices() const { return offsetMatrices_.data(); }

    // ---- Compute offset matrices (render thread, call once per frame) ----

    void ComputeOffsetMatrices() {
        if (!matricesDirty_) return;
        for (int i = 0; i < nodeCount_; i++) {
            // offset = inverseBind * current
            // This transforms: bindPoseWorld → nodeLocal → currentWorld
            offsetMatrices_[i] = inverseBindMatrices_[i] * currentMatrices_[i];
        }
        matricesDirty_ = false;
    }

    // ---- CPU vertex skinning (legacy, kept for reference) ----

    bool SkinVertices(int geosetId,
                      const std::vector<Vertex>& baseVerts,
                      std::vector<Vertex>& outVerts) const
    {
        auto it = geosetWeights_.find(geosetId);
        if (it == geosetWeights_.end()) return false;
        const auto& skin = it->second;

        if (skin.vertices.size() != baseVerts.size()) return false;

        outVerts.resize(baseVerts.size());

        for (int i = 0; i < (int)baseVerts.size(); i++) {
            const auto& inf = skin.vertices[i];

            Vector3f posSum = {0, 0, 0};
            Vector3f nrmSum = {0, 0, 0};
            float totalWeight = 0.0f;

            const Vector3f& basePos = baseVerts[i].position;
            const Vector3f& baseNrm = baseVerts[i].normal;

            for (int j = 0; j < 4; j++) {
                float w = inf.weight[j];
                if (w < 0.0001f) continue;

                int nIdx = inf.boneIdx[j];
                if (nIdx < 0 || nIdx >= nodeCount_) continue;

                const Matrix44f& offset = offsetMatrices_[nIdx];

                // Transform position (full 4x4)
                posSum += whiteout::transform_point(basePos, offset) * w;

                // Transform normal (3x3 rotation only, no translation)
                nrmSum += whiteout::transform_normal(baseNrm, offset) * w;

                totalWeight += w;
            }

            if (totalWeight < 0.0001f) {
                // No valid weights — keep original
                outVerts[i] = baseVerts[i];
                continue;
            }

            outVerts[i].position = posSum;
            outVerts[i].normal = nrmSum.normalized();
            outVerts[i].uv    = baseVerts[i].uv;
            outVerts[i].color = baseVerts[i].color;
        }

        return true;
    }

private:
    int nodeCount_ = 0;
    std::vector<Matrix44f> inverseBindMatrices_;  // set once at setup
    std::vector<Matrix44f> currentMatrices_;      // updated per frame
    std::vector<Matrix44f> offsetMatrices_;       // = invBind * current (precomputed)
    std::unordered_map<int, GeosetSkinInfo> geosetWeights_;
    bool matricesDirty_ = false;
    bool nodesReady_ = false;   // true after first UpdateNodeMatrices call
};

} // namespace WhiteoutDex
