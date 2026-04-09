#pragma once
// ============================================================================
// WhiteoutDex Real-Time Renderer — Core Renderer
// Adapter-pattern API: LoadModel() + ApplyFrameState()
// ============================================================================

#include "types.h"
#include "camera.h"
#include "animation.h"
#include "particle.h"
#include "ribbon.h"
#include "model_types.h"
#include <unordered_map>

namespace WhiteoutDex {

// Line vertex for grid/bone rendering
struct LineVertex {
    XMFLOAT3 position;
    XMFLOAT4 color;
};

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
    int priorityPlane = 0;   // -20 to 20, render order within same filter pass
    int sortOrder     = 0;   // 0=unused, 1=near-to-far, 2=far-to-near
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
    ID3D11Buffer* vb   = nullptr;  // DYNAMIC for skinning updates
    ID3D11Buffer* ib   = nullptr;
    int indexCount      = 0;
    int vertexCount     = 0;
    int materialId      = -1;

    // Phase 4: CPU data for skinning
    std::vector<Vertex> baseVertices;
    bool hasSkinning    = false;
    float geosetAlpha   = 1.0f;      // visibility: 0=hidden, 1=visible
    XMFLOAT3 geosetColor = {1,1,1};  // color tint: RGB 0-1
    XMMATRIX worldMatrix = XMMatrixIdentity();  // node world transform (used for unskinned meshes)
    int priorityPlane   = 0;         // render order within same filter pass

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

// Per-material packed Texture2DArray (one slice per layer) for in-shader compositing.
struct GPUMaterial {
    StagedMaterial            cpu;
    ID3D11Texture2D*          texArray    = nullptr;
    ID3D11ShaderResourceView* texArraySRV = nullptr;
    int                       arrayW      = 0;
    int                       arrayH      = 0;

    void Release() {
        SafeRelease(texArraySRV);
        SafeRelease(texArray);
        arrayW = arrayH = 0;
    }
};

// ============================================================================
// Main Renderer Class
// ============================================================================

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Lifecycle
    bool Open(int width, int height);
    void Close();
    bool IsOpen() const;

    // Camera control (thread-safe)
    void SetCamera(float pitch, float yaw, float distance,
                   float targetX, float targetY, float targetZ);

    // Model data (thread-safe — called from API/MaxScript thread)
    void ClearModel();

    // Typed model loading API (adapter pattern)
    void LoadModel(const std::vector<MeshData>& meshes,
                   const std::vector<TextureData>& textures,
                   const std::vector<MaterialData>& materials,
                   const SkeletonData& skeleton,
                   const std::vector<SkinWeightData>& skinWeights,
                   const std::vector<ParticleEmitterConfig>& particles,
                   const std::vector<RibbonEmitterConfig>& ribbons,
                   const std::vector<CollisionShapeData>& collisions);

    // Apply pre-computed per-frame state (call from host thread each frame)
    void ApplyFrameState(const FrameState& state, int timeMs);

private:
    // Render thread
    void RenderThread(int width, int height);

    // Win32 window
    bool CreateRenderWindow(int width, int height);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK RenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // DirectX 11
    bool InitD3D();
    void CleanupD3D();
    bool CreateShaders();
    bool CreateDefaultResources();
    bool ResizeBuffers(int width, int height);

    // GPU resource management
    void ProcessStagedData();
    void ReleaseModelGPU();

    // Phase 4: Animation update
    void UpdateAnimation();

    // Phase 5: Particle simulation + rendering
    void UpdateParticles(float dt);
    void RenderParticles();

    // Phase 5b: Ribbon rendering
    void UpdateRibbons(float dt);
    void RenderRibbons();

    // Collision shape wireframes
    void RenderCollisions();

    // Rendering
    void RenderFrame();
    void RenderGrid();
    void RenderGeosets();
    void ApplyFilterMode(int filterMode, int matFlags);

    // ViewCube
    bool CreateViewCube();
    void RenderViewCube();
    int  HitTestViewCube(int mx, int my);
    void SnapCameraToFace(int faceIndex);
    RECT GetViewCubeRect() const;

    static int GetRenderOrder(int filterMode) {
        switch (filterMode) {
            case FILTER_NONE:        return 1;
            case FILTER_TRANSPARENT: return 2;
            case FILTER_BLEND:       return 3;
            default:                 return 4;
        }
    }

    // Thread & sync
    std::thread           renderThread_;
    std::atomic<bool>     running_{false};
    std::atomic<bool>     initialized_{false};
    std::mutex            dataMutex_;

    // Win32
    HWND                  hwnd_ = nullptr;      // parent window (toolbar)
    HWND                  hwndRender_ = nullptr; // child window (DX11 surface)
    int                   width_ = 800;
    int                   height_ = 600;
    static const int      kToolbarH = 28;

    // Toolbar checkboxes
    HWND chkGrid_ = nullptr, chkParticles_ = nullptr;
    HWND chkRibbons_ = nullptr, chkCollisions_ = nullptr;
    enum { IDC_GRID=1001, IDC_PARTICLES, IDC_RIBBONS, IDC_COLLISIONS };

    // Mouse
    bool                  lmbDown_ = false;
    bool                  rmbDown_ = false;
    bool                  mmbDown_ = false;
    POINT                 lastMouse_ = {0, 0};

    // Camera
    Camera                camera_;

    // ---- Display toggles (keyboard shortcuts) ----
    bool                  showGrid_       = true;
    bool                  showParticles_  = true;
    bool                  showRibbons_    = true;
    bool                  showCollisions_ = false;  // off by default

    // ---- Collision shapes ----
    struct CollisionShape {
        int type = 0;            // 0=box, 1=sphere
        XMFLOAT3 vmin = {0,0,0};
        XMFLOAT3 vmax = {0,0,0};
        float radius = 0;
        XMMATRIX transform = XMMatrixIdentity();
    };
    std::vector<CollisionShape> collisionShapes_;

    // ---- Staged model data (API thread writes, render thread reads) ----
    std::unordered_map<int, StagedGeoset>   stagedGeosets_;
    std::unordered_map<int, StagedMaterial> stagedMaterials_;
    std::unordered_map<int, StagedTexture>  stagedTextures_;
    bool                                    stagedDirty_ = false;
    bool                                    stagedClear_ = false;

    // Per-material texture animation params (updated per frame)
    struct TexAnimData { float uOff=0, vOff=0, uTile=1, vTile=1; };
    std::unordered_map<int, TexAnimData>    matTexAnim_;

    // ---- Skinning system (protected by dataMutex_) ----
    SkinningSystem                          skinning_;
    bool                                    skinDirty_ = false;

    // ---- Particle system (protected by dataMutex_) ----
    ParticleSystem                          particles_;
    ID3D11Buffer*                           particleVB_ = nullptr;
    int                                     particleVBSize_ = 0;

    // ---- Ribbon system (protected by dataMutex_) ----
    RibbonSystem                            ribbons_;
    ID3D11Buffer*                           ribbonVB_ = nullptr;
    int                                     ribbonVBSize_ = 0;  // current VB capacity in vertices

    // ---- GPU model data (render thread only) ----
    std::vector<GPUGeoset>                  gpuGeosets_;
    std::unordered_map<int, GPUTexture>     gpuTextures_;
    std::vector<GPUMaterial>                gpuMaterials_;

    // CPU-side cache of texture pixels (kept so per-material Texture2DArrays
    // can be rebuilt whenever materials change without re-uploading textures).
    std::unordered_map<int, StagedTexture>  texturePixels_;

    // Per-material texture-array rebuild helper (render thread only).
    bool BuildMaterialTextureArray(GPUMaterial& gm);

    // ---- DX11 core ----
    ID3D11Device*           device_       = nullptr;
    ID3D11DeviceContext*    context_      = nullptr;
    IDXGISwapChain*         swapChain_    = nullptr;
    ID3D11RenderTargetView* rtv_          = nullptr;
    ID3D11DepthStencilView* dsv_          = nullptr;
    ID3D11Texture2D*        depthBuffer_  = nullptr;

    // Shaders
    ID3D11VertexShader*     vertexShader_       = nullptr;
    ID3D11PixelShader*      pixelShader_        = nullptr;  // particles/ribbons/viewcube (Texture2D)
    ID3D11PixelShader*      geosetPixelShader_  = nullptr;  // geosets (Texture2DArray + CBLayers)
    ID3D11InputLayout*      inputLayout_        = nullptr;
    ID3D11VertexShader*     lineVertexShader_   = nullptr;
    ID3D11PixelShader*      linePixelShader_    = nullptr;
    ID3D11InputLayout*      lineInputLayout_    = nullptr;

    // Constant buffers
    ID3D11Buffer*           cbPerFrame_ = nullptr;
    ID3D11Buffer*           cbLayers_   = nullptr;  // b1 for geoset PS

    // Grid
    ID3D11Buffer*           gridVB_       = nullptr;
    int                     gridVertCount_ = 0;

    // ViewCube
    ID3D11Buffer*           vcCubeVB_     = nullptr;
    ID3D11Buffer*           vcCubeIB_     = nullptr;
    ID3D11Buffer*           vcOutlineVB_  = nullptr;
    ID3D11ShaderResourceView* vcFaceTexSRV_ = nullptr;
    ID3D11Texture2D*        vcFaceTex_    = nullptr;
    static constexpr int    kViewCubeSize = 120;
    bool                    vcHovered_    = false;

    // Render states
    ID3D11RasterizerState*    rsDefault_    = nullptr;
    ID3D11RasterizerState*    rsNoCull_     = nullptr;
    ID3D11DepthStencilState*  dsDefault_    = nullptr;
    ID3D11DepthStencilState*  dsNoWrite_    = nullptr;
    ID3D11DepthStencilState*  dsNoWriteLeq_ = nullptr;  // LESS_EQUAL + no write, for internal layers
    ID3D11DepthStencilState*  dsDisabled_   = nullptr;
    ID3D11BlendState*         bsOpaque_     = nullptr;
    ID3D11BlendState*         bsAlphaTest_  = nullptr;
    ID3D11BlendState*         bsAlphaBlend_ = nullptr;
    ID3D11BlendState*         bsAdditive_   = nullptr;
    ID3D11BlendState*         bsAddAlpha_   = nullptr;
    ID3D11BlendState*         bsModulate_   = nullptr;
    ID3D11BlendState*         bsModulate2x_ = nullptr;
    ID3D11SamplerState*       samplerLinear_ = nullptr;

    // 1x1 white default texture
    ID3D11ShaderResourceView* defaultTexSRV_ = nullptr;
    ID3D11Texture2D*          defaultTex_    = nullptr;

    // Animation time
    std::atomic<int>        currentTimeMs_{0};
};

} // namespace WhiteoutDex
