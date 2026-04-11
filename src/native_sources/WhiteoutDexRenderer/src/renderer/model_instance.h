#pragma once
// ============================================================================
// WhiteoutDex Renderer — ModelInstance
// Encapsulates all per-model state: GPU resources, staged data, subsystems.
// The Renderer owns one or more ModelInstance objects.
// ============================================================================

#include "types.h"
#include "animation.h"
#include "particle.h"
#include "ribbon.h"
#include <unordered_map>
#include <vector>

namespace WhiteoutDex {

// ============================================================================
// Staged data (CPU side — written by API thread, read by render thread)
// ============================================================================

struct StagedTexture {
    std::vector<uint8_t> pixels;
    int width  = 0;
    int height = 0;
    int replaceableId = 0;
};

struct StagedMaterialLayer {
    int filterMode  = 0;
    int textureId   = -1;
    float alpha     = 1.0f;
    int flags       = 0;
};

struct StagedMaterial {
    std::vector<StagedMaterialLayer> layers;
    int priorityPlane = 0;
    int sortOrder     = 0;
};

struct StagedGeoset {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    int materialId = -1;
};

// ============================================================================
// GPU resources (render thread only)
// ============================================================================

struct GPUGeoset {
    int geosetId       = -1;
    ID3D11Buffer* vb   = nullptr;
    ID3D11Buffer* ib   = nullptr;
    int indexCount      = 0;
    int vertexCount     = 0;
    int materialId      = -1;

    std::vector<Vertex> baseVertices;
    bool hasSkinning    = false;
    float geosetAlpha   = 1.0f;
    XMFLOAT3 geosetColor = {1,1,1};
    XMMATRIX worldMatrix = XMMatrixIdentity();
    int priorityPlane   = 0;

    void Release() {
        SafeRelease(vb); SafeRelease(ib);
        indexCount = 0; vertexCount = 0;
        baseVertices.clear(); baseVertices.shrink_to_fit();
    }
};

struct GPUTexture {
    ID3D11Texture2D*          tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;

    void Release() { SafeRelease(srv); SafeRelease(tex); }
};

struct GPUMaterial {
    StagedMaterial cpu;
};

// ============================================================================
// Collision shape (per-model, rendered as wireframe)
// ============================================================================
struct CollisionShape {
    int type = 0;            // 0=box, 1=sphere
    XMFLOAT3 vmin = {0,0,0};
    XMFLOAT3 vmax = {0,0,0};
    float radius = 0;
    XMMATRIX transform = XMMatrixIdentity();
};

// ============================================================================
// Per-layer texture animation data
// ============================================================================
struct TexAnimData {
    float uOff=0, vOff=0, uTile=1, vTile=1, rotation=0;
};

// ============================================================================
// ModelInstance — all state for a single renderable model
// ============================================================================
struct ModelInstance {
    uint32_t handle = 0;
    bool isFocus = false;

    // World transform for the entire model instance
    // (identity for focus model, per-particle transform for PE1 children)
    XMMATRIX worldTransform = XMMatrixIdentity();

    // ---- Staged data (CPU side, written by API thread under dataMutex_) ----
    std::unordered_map<int, StagedGeoset>   stagedGeosets;
    std::unordered_map<int, StagedMaterial> stagedMaterials;
    std::unordered_map<int, StagedTexture>  stagedTextures;
    bool stagedDirty = false;
    bool stagedClear = false;

    // ---- GPU resources (render thread only) ----
    std::vector<GPUGeoset>              gpuGeosets;
    std::unordered_map<int, GPUTexture> gpuTextures;
    std::vector<GPUMaterial>            gpuMaterials;

    // ---- Skinning ----
    SkinningSystem skinning;
    bool skinDirty = false;

    // ---- Particle system ----
    ParticleSystem particles;
    ID3D11Buffer*  particleVB     = nullptr;
    int            particleVBSize = 0;

    // ---- Ribbon system ----
    RibbonSystem ribbons;
    ID3D11Buffer*  ribbonVB     = nullptr;
    int            ribbonVBSize = 0;

    // ---- Collision shapes ----
    std::vector<CollisionShape> collisionShapes;

    // ---- Per-layer texture animation (updated per frame) ----
    // Key: materialId * 1000 + layerIndex
    std::unordered_map<int, TexAnimData> matTexAnim;

    // ---- Replaceable texture map (for team color) ----
    // textureId → replaceableId (1=TeamColor, 2=TeamGlow)
    std::unordered_map<int, int> replaceableTexMap;

    // Release all GPU resources
    void ReleaseGPU() {
        for (auto& g : gpuGeosets) g.Release();
        gpuGeosets.clear();
        for (auto& [id, t] : gpuTextures) t.Release();
        gpuTextures.clear();
        gpuMaterials.clear();
        SafeRelease(particleVB); particleVBSize = 0;
        SafeRelease(ribbonVB); ribbonVBSize = 0;
    }
};

} // namespace WhiteoutDex
