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
#include "model_instance.h"
#include "file_content_provider.h"
#include <unordered_map>
#include <memory>

namespace WhiteoutDex {

// Line vertex for grid/bone rendering
struct LineVertex {
    XMFLOAT3 position;
    XMFLOAT4 color;
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
    XMFLOAT3 GetCameraPosition() const { return camera_.GetSource(); }

    // Model data (thread-safe — called from API/MaxScript thread)
    void ClearModel();

    // Multi-model API — returns handle for the loaded model
    uint32_t AddModel(const std::vector<MeshData>& meshes,
                      const std::vector<TextureData>& textures,
                      const std::vector<MaterialData>& materials,
                      const SkeletonData& skeleton,
                      const std::vector<SkinWeightData>& skinWeights,
                      const std::vector<ParticleEmitterConfig>& particles,
                      const std::vector<RibbonEmitterConfig>& ribbons,
                      const std::vector<CollisionShapeData>& collisions);
    void RemoveModel(uint32_t handle);
    void SetAttachmentConfigs(uint32_t handle, const std::vector<AttachmentConfig>& configs);
    void SetPE1Configs(uint32_t handle, const std::vector<PE1EmitterConfig>& configs);
    void SetPE1ChildCoordSpace(CoordSpace space);
    void SetPE1BasePath(const std::string& basePath);
    uint32_t GetFocusModelHandle() const { return focusModelHandle_; }

    // Access the unified file content provider (disk + CASC + MPQ)
    FileContentProvider& GetContentProvider() { return contentProvider_; }

    // Backward-compatible single-model API (operates on focus model)
    void LoadModel(const std::vector<MeshData>& meshes,
                   const std::vector<TextureData>& textures,
                   const std::vector<MaterialData>& materials,
                   const SkeletonData& skeleton,
                   const std::vector<SkinWeightData>& skinWeights,
                   const std::vector<ParticleEmitterConfig>& particles,
                   const std::vector<RibbonEmitterConfig>& ribbons,
                   const std::vector<CollisionShapeData>& collisions);

    // Apply pre-computed per-frame state
    void ApplyFrameState(uint32_t handle, const FrameState& state, int timeMs);
    void ApplyFrameState(const FrameState& state, int timeMs); // focus model

    // Update materials and textures without full model reload
    void UpdateMaterials(uint32_t handle, const std::vector<MaterialData>& materials,
                         const std::vector<TextureData>& textures);
    void UpdateMaterials(const std::vector<MaterialData>& materials,
                         const std::vector<TextureData>& textures); // focus model

    // Team color (RGB 0-255)
    void SetTeamColor(uint8_t r, uint8_t g, uint8_t b);

    // Camera presets (index 0 is always "Free Camera")
    void SetCameraPresets(const std::vector<CameraPreset>& presets);
    int  GetActiveCameraIndex() const;
    bool IsCameraLocked() const { return cameraLocked_; }

    // Sequence picker (used by standalone viewer; safe to ignore from Max plugin)
    void SetSequences(const std::vector<std::string>& names);
    int  GetActiveSequenceIndex() const;

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
    void UploadStagedTextures(ModelInstance& mi);
    void UploadStagedGeosets(ModelInstance& mi);
    void CreateNodePalette(ModelInstance& mi);
    void ReleaseModelGPU();

    // Phase 4: Animation update
    void UpdateAnimation();

    // Phase 5: Particle simulation + rendering
    void UpdateParticles(float dt);
    void RenderParticles();

    // Attachment model lifecycle
    void UpdateAttachments();

    // PE1: Model particle lifecycle
    void UpdatePE1(float dt);
    void EvaluatePE1Children();

    // Phase 5b: Ribbon rendering
    void UpdateRibbons(float dt);
    void RenderRibbons();

    // Collision shape wireframes
    void RenderCollisions();

    // ApplyFrameState helpers
    void ApplyBoneMatrices(ModelInstance& mi, const FrameState& state);
    void ApplyGeosetStates(ModelInstance& mi, const FrameState& state);
    void ApplyLayerStates(ModelInstance& mi, const FrameState& state);
    void ApplyParticleFrameStates(ModelInstance& mi, const FrameState& state);
    void ApplyRibbonFrameStates(ModelInstance& mi, const FrameState& state);
    void ApplyPE1FrameStates(ModelInstance& mi, const FrameState& state);
    void ApplyAttachmentStates(ModelInstance& mi, const FrameState& state, int timeMs);

    // Team color + camera presets
    void UpdateTeamColorTextures();
    void ProcessCameraPresets();

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

    // Toolbar controls
    HWND chkGrid_ = nullptr, chkParticles_ = nullptr;
    HWND chkRibbons_ = nullptr, chkCollisions_ = nullptr;
    HWND btnTeamColor_ = nullptr;
    HWND cmbCamera_ = nullptr;
    HWND lblSequence_ = nullptr;
    HWND cmbSequence_ = nullptr;
    enum { IDC_GRID=1001, IDC_PARTICLES, IDC_RIBBONS, IDC_COLLISIONS,
           IDC_TEAMCOLOR, IDC_CAMERA, IDC_SEQUENCE };

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

    // ---- Model instances ----
    uint32_t nextModelHandle_ = 1;
    std::unordered_map<uint32_t, std::unique_ptr<ModelInstance>> models_;
    uint32_t focusModelHandle_ = 0;

    // Helper: get focus model (may be null)
    ModelInstance* focusModel() const {
        auto it = models_.find(focusModelHandle_);
        return (it != models_.end()) ? it->second.get() : nullptr;
    }
    ModelInstance* getModel(uint32_t h) const {
        auto it = models_.find(h);
        return (it != models_.end()) ? it->second.get() : nullptr;
    }

    // ---- PE1 model template cache (PE1ModelTemplate defined in renderer.cpp) ----
    struct PE1ModelTemplate;
    std::unordered_map<std::string, std::shared_ptr<PE1ModelTemplate>> pe1TemplateCache_;
    static constexpr int kMaxPE1Depth = 3;
    static constexpr int kMaxPE1Instances = 256;
    int pe1InstanceCount_ = 0;
    CoordSpace pe1ChildCoordSpace_ = CoordSpace::MDX;

    std::string pe1BasePath_;  // root directory for resolving PE1 model + texture paths
    FileContentProvider contentProvider_; // unified file resolution (disk + CASC + MPQ)
    std::shared_ptr<PE1ModelTemplate> getOrLoadTemplate(const std::string& modelPath);
    void stageModelFromTemplate(ModelInstance* mi, const PE1ModelTemplate& tmpl);

    // Team color (shared across all models)
    COLORREF teamColor_ = RGB(255, 0, 0);

    // Camera presets
    std::vector<CameraPreset> cameraPresets_;
    std::vector<CameraPreset> pendingCameraPresets_;
    bool cameraDirty_ = false;
    bool cameraLocked_ = false;

    // Sequence picker (standalone viewer)
    std::vector<std::string> pendingSequenceNames_;
    bool sequencesDirty_ = false;
    void ProcessSequences();
    std::atomic<int> activeSequence_{0};

    // Window icon
    HICON icon_ = nullptr;

    // ---- DX11 core ----
    ID3D11Device*           device_       = nullptr;
    ID3D11DeviceContext*    context_      = nullptr;
    IDXGISwapChain*         swapChain_    = nullptr;
    ID3D11RenderTargetView* rtv_          = nullptr;
    ID3D11DepthStencilView* dsv_          = nullptr;
    ID3D11Texture2D*        depthBuffer_  = nullptr;

    // Shaders
    ID3D11VertexShader*     vertexShader_       = nullptr;
    ID3D11PixelShader*      pixelShader_        = nullptr;
    ID3D11InputLayout*      inputLayout_        = nullptr;
    ID3D11VertexShader*     lineVertexShader_   = nullptr;
    ID3D11PixelShader*      linePixelShader_    = nullptr;
    ID3D11InputLayout*      lineInputLayout_    = nullptr;
    ID3D11ComputeShader*    skinComputeShader_  = nullptr;

    // Constant buffers
    ID3D11Buffer*           cbPerFrame_ = nullptr;

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
    ID3D11DepthStencilState*  dsDisabled_   = nullptr;
    ID3D11BlendState*         bsOpaque_     = nullptr;
    ID3D11BlendState*         bsAlphaTest_  = nullptr;
    ID3D11BlendState*         bsAlphaBlend_ = nullptr;
    ID3D11BlendState*         bsAdditive_   = nullptr;
    ID3D11BlendState*         bsAddAlpha_   = nullptr;
    ID3D11BlendState*         bsModulate_   = nullptr;
    ID3D11BlendState*         bsModulate2x_ = nullptr;
    ID3D11SamplerState*       samplerLinear_ = nullptr;
    // Per-texture wrap mode samplers: index = wrapFlags (0x0..0x3)
    // bit 0 = WrapWidth (U repeat), bit 1 = WrapHeight (V repeat)
    ID3D11SamplerState*       samplerWrap_[4] = {};

    // 1x1 white default texture
    ID3D11ShaderResourceView* defaultTexSRV_ = nullptr;
    ID3D11Texture2D*          defaultTex_    = nullptr;

    // Animation time
    std::atomic<int>        currentTimeMs_{0};
};

} // namespace WhiteoutDex
