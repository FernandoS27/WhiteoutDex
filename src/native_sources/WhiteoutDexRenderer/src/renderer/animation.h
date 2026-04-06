#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Skinning System
// Phase 4: Matrix-based CPU vertex skinning
//
// Max evaluates bone controllers → sends world matrices per frame → DLL skins.
// Standard skinning: V' = sum(w_i * offset_i * V)
// where offset_i = currentMatrix_i * inverseBindMatrix_i
// ============================================================================

#include "types.h"
#include <unordered_map>

namespace WhiteoutDex {

// ============================================================================
// Per-vertex bone influences (max 4, standard limit)
// ============================================================================
struct VertexInfluence {
    int   boneIdx[4] = {0, 0, 0, 0};
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
        boneCount_ = 0;
        matricesDirty_ = false;
        bonesReady_ = false;
    }

    // ---- Setup (called once from API thread) ----

    void SetSkeleton(int boneCount, const float* inverseBindData) {
        boneCount_ = boneCount;
        inverseBindMatrices_.resize(boneCount);
        currentMatrices_.resize(boneCount);
        offsetMatrices_.resize(boneCount);
        bonesReady_ = false;  // Don't allow skinning until UpdateBoneMatrices is called

        for (int i = 0; i < boneCount; i++) {
            // Load 16 floats as row-major 4x4 matrix
            const float* m = inverseBindData + i * 16;
            inverseBindMatrices_[i] = XMMATRIX(
                m[0], m[1], m[2],  m[3],
                m[4], m[5], m[6],  m[7],
                m[8], m[9], m[10], m[11],
                m[12],m[13],m[14], m[15]
            );
            currentMatrices_[i]  = XMMatrixIdentity();
            offsetMatrices_[i]   = XMMatrixIdentity(); // Safe default until bones arrive
        }
    }

    void SetGeosetWeights(int geosetId, int vertCount,
                          const int* boneIndices, const float* weights) {
        GeosetSkinInfo& info = geosetWeights_[geosetId];
        info.vertices.resize(vertCount);
        for (int v = 0; v < vertCount; v++) {
            for (int j = 0; j < 4; j++) {
                info.vertices[v].boneIdx[j] = boneIndices[v * 4 + j];
                info.vertices[v].weight[j]  = weights[v * 4 + j];
            }
        }
    }

    // ---- Per-frame update (called from API thread via SetTime path) ----

    void UpdateBoneMatrices(int boneCount, const float* worldData) {
        if (boneCount != boneCount_) return;
        for (int i = 0; i < boneCount; i++) {
            const float* m = worldData + i * 16;
            currentMatrices_[i] = XMMATRIX(
                m[0], m[1], m[2],  m[3],
                m[4], m[5], m[6],  m[7],
                m[8], m[9], m[10], m[11],
                m[12],m[13],m[14], m[15]
            );
        }
        matricesDirty_ = true;
        bonesReady_ = true;  // Safe to start skinning now
    }

    // ---- Query ----

    bool HasSkeleton()              const { return boneCount_ > 0; }
    bool IsReady()                  const { return bonesReady_; }
    int  BoneCount()                const { return boneCount_; }
    bool HasWeights(int geosetId)   const { return geosetWeights_.count(geosetId) > 0; }
    bool NeedsUpdate()              const { return matricesDirty_; }

    // ---- Compute offset matrices (render thread, call once per frame) ----

    void ComputeOffsetMatrices() {
        if (!matricesDirty_) return;
        for (int i = 0; i < boneCount_; i++) {
            // offset = inverseBind * current
            // This transforms: bindPoseWorld → boneLocal → currentWorld
            offsetMatrices_[i] = inverseBindMatrices_[i] * currentMatrices_[i];
        }
        matricesDirty_ = false;
    }

    // ---- CPU vertex skinning ----

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

            XMVECTOR posSum = XMVectorZero();
            XMVECTOR nrmSum = XMVectorZero();
            float totalWeight = 0.0f;

            XMVECTOR basePos = XMLoadFloat3(&baseVerts[i].position);
            XMVECTOR baseNrm = XMLoadFloat3(&baseVerts[i].normal);

            for (int j = 0; j < 4; j++) {
                float w = inf.weight[j];
                if (w < 0.0001f) continue;

                int bIdx = inf.boneIdx[j];
                if (bIdx < 0 || bIdx >= boneCount_) continue;

                const XMMATRIX& offset = offsetMatrices_[bIdx];

                // Transform position (full 4x4)
                posSum = XMVectorAdd(posSum,
                    XMVectorScale(XMVector3Transform(basePos, offset), w));

                // Transform normal (3x3 rotation only, no translation)
                nrmSum = XMVectorAdd(nrmSum,
                    XMVectorScale(XMVector3TransformNormal(baseNrm, offset), w));

                totalWeight += w;
            }

            if (totalWeight < 0.0001f) {
                // No valid weights — keep original
                outVerts[i] = baseVerts[i];
                continue;
            }

            XMStoreFloat3(&outVerts[i].position, posSum);
            XMStoreFloat3(&outVerts[i].normal, XMVector3Normalize(nrmSum));
            outVerts[i].uv    = baseVerts[i].uv;
            outVerts[i].color = baseVerts[i].color;
        }

        return true;
    }

private:
    int boneCount_ = 0;
    std::vector<XMMATRIX> inverseBindMatrices_;  // set once at setup
    std::vector<XMMATRIX> currentMatrices_;      // updated per frame from Max
    std::vector<XMMATRIX> offsetMatrices_;       // = invBind * current (precomputed)
    std::unordered_map<int, GeosetSkinInfo> geosetWeights_;
    bool matricesDirty_ = false;
    bool bonesReady_ = false;   // true after first UpdateBoneMatrices call
};

} // namespace WhiteoutDex
