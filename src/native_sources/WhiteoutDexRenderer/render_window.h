#pragma once

// ============================================================================
// RenderWindow — Win32-hosted preview window for the 3ds Max plugin.
//
// Wraps a single Win32 HWND owned by a render thread (so Max's UI thread
// stays free for WhiteoutFlakesStart / TimeChanged / material polling). The whole
// client area is the swap chain target; the toolbar / menus / settings
// panel are Dear ImGui widgets drawn by the engine's BLS-backed ImGui
// adapter, with input forwarded through imgui_impl_win32.
//
// Cross-thread access pattern matches the original Win32 build: Max-thread
// callers go through SetCameraPresets / SetSequences / SetFocusActor, all of
// which take hostMutex_ (or a relaxed atomic for focusActor_); the UI runs
// entirely on the render thread and snapshots host state under the same
// lock.
// ============================================================================

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "model/actor_manager.h"
#include "render_target.h"
#include "whiteout/flakes/gfx_types.h"
#include "whiteout/flakes/model_source.h"
#include "whiteout/flakes/model_types.h"
#include "whiteout/flakes/types.h"

#include <windows.h>

struct ImGuiContext;

namespace whiteout::flakes::renderer {
class RenderService;
}

namespace whiteout::flakes {

using namespace whiteout::flakes::renderer;
using namespace whiteout::flakes::renderer::model;

class MaxPluginUI;

class RenderWindow {
public:
    explicit RenderWindow(RenderService& service);
    ~RenderWindow();

    bool Open(i32 width, i32 height, gfx::GfxApi api = gfx::GfxApi::D3D12);
    void Close();
    bool IsOpen() const;

    bool Create(i32 width, i32 height);
    void Destroy();
    void Show();

    bool PumpMessages();

    // Host-side authorship — Max main thread pushes presets/sequences/focus
    // into the render thread. The UI on the render thread reads these under
    // hostMutex_ (or relaxed atomic for focusActor_).
    void SetCameraPresets(std::vector<CameraPreset> presets);
    void SetSequences(std::vector<std::string> names, std::vector<SequenceInfo> ranges);
    void SetFocusActor(ActorId h) {
        focusActor_.store(h, std::memory_order_relaxed);
    }
    ActorId FocusActor() const {
        return focusActor_.load(std::memory_order_relaxed);
    }

    HWND GetParentHWND() const {
        return hwnd_;
    }
    HWND GetRenderHWND() const {
        return hwnd_;
    }

    void SetTitle(const wchar_t* title);

    // ---- Parking the render thread ----
    //
    // SuspendRendering() blocks until the render thread has finished the frame
    // it was on and gone idle; ResumeRendering() lets it go again. Two callers,
    // two reasons:
    //
    //  * The asset picker (SuspendForModal): Dear ImGui's *current* context is
    //    one process-global pointer, so two windows can each own a context
    //    (they do) but only one may be in use at a time. The picker parks this
    //    window for its lifetime rather than making that pointer thread-local.
    //  * A Resync: the Max thread reaps the old actor and spawns a new one,
    //    both of which mutate the actor map RenderFrame walks every frame.
    //
    // Either way the preview freezes for the duration, which is what both
    // callers want.
    void SuspendRendering();
    void ResumeRendering();
    void SuspendForModal() {
        SuspendRendering();
    }
    void Resume() {
        ResumeRendering();
    }

    // ---- Active-viewport camera sync (Max thread → render thread) ----
    //
    // While enabled, the Max UI thread samples whichever viewport Max has
    // active and pushes its pose here; the render thread stamps it onto the
    // scene camera at the top of every frame. Mouse orbit goes inert for the
    // duration — the next frame would overwrite it anyway.
    //
    // SetSyncCamera runs on the render thread (it is a toolbar checkbox) and
    // touches the camera directly to hand control back cleanly on the way out.
    void SetSyncCamera(bool on);
    bool SyncCamera() const {
        return syncCamera_.load(std::memory_order_relaxed);
    }
    void SetExternalCameraPose(const Vector3f& position, const Vector3f& target, f32 roll,
                               f32 fovHorizontal);

    // ---- Full-rebuild request (render thread → Max thread) ----
    //
    // The toolbar's Resync button cannot re-walk the Max scene itself — that is
    // Max-UI-thread work — so it raises a flag the plugin's viewport-sync timer
    // picks up on the next tick.
    void RequestResync() {
        resyncRequested_.store(true, std::memory_order_relaxed);
    }
    bool ConsumeResyncRequest() {
        return resyncRequested_.exchange(false, std::memory_order_relaxed);
    }

    RenderService& Service() {
        return service_;
    }
    const RenderService& Service() const {
        return service_;
    }

    // ---- UI accessors (called by MaxPluginUI on the render thread) ----
    // Snapshots are taken under the lock to keep the UI's read consistent
    // even if the Max thread re-publishes mid-frame.
    std::vector<std::string> SequenceNamesSnapshot() const;
    std::vector<SequenceInfo> SequenceRangesSnapshot() const;
    std::vector<CameraPreset> CameraPresetsSnapshot() const;
    i32 ActiveCameraPresetIdx() const;
    bool CameraLocked() const;

    void ActivateCameraPreset(i32 idx);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void ThreadFunc(i32 width, i32 height, gfx::GfxApi api);

    void InitImGui();
    void ShutdownImGui();
    void UpdateCameraPresetAnimator();
    // Stamp the last pose the Max thread pushed onto the scene camera. Render
    // thread, once per frame; a no-op while camera sync is off.
    void ApplyExternalCamera();
    // Mouse orbit is inert whenever a live camera preset or the viewport sync
    // owns the pose.
    bool CameraInputBlocked() const;

    RenderService& service_;

    HWND hwnd_ = nullptr;

    HICON icon_ = nullptr;

    bool imguiInitialised_ = false;
    // This window's own ImGui context. Never assume it is the current one:
    // ImGui::CreateContext restores whatever was current before it, so a
    // second window created after this one leaves ITS context selected.
    ImGuiContext* imguiCtx_ = nullptr;
    std::unique_ptr<MaxPluginUI> ui_;

    // Set by the Max thread, observed by the render thread; `suspendedAck_`
    // goes true once the render thread is parked and has stopped touching
    // ImGui and scene state.
    //
    // A count, not a flag: the asset picker's modal loop pumps the plugin's
    // timers, so a queued Resync can start while the picker already holds a
    // park. The inner ResumeRendering must not unpark the render thread while
    // the picker still owns the ImGui context.
    std::atomic<i32> suspendCount_{0};
    std::atomic<bool> suspendedAck_{false};

    bool lmbDown_ = false, rmbDown_ = false, mmbDown_ = false;
    POINT lastMouse_ = {0, 0};

    // Cross-thread host state — Max main thread writes via the public
    // setters; render thread reads inside HandleMessage / MaxPluginUI.
    mutable std::mutex hostMutex_;
    std::vector<CameraPreset> cameraPresets_;
    i32 activeCameraPresetIdx_ = -1;
    bool cameraLocked_ = false;

    std::vector<std::string> sequenceNames_;
    std::vector<SequenceInfo> sequenceRanges_;

    // Latest active-viewport pose, written by the Max thread under hostMutex_
    // and consumed by ApplyExternalCamera on the render thread. `extCamValid_`
    // stays false until the first push, so enabling the checkbox never snaps
    // the camera to an all-zero pose for one frame.
    Vector3f extCamPos_{};
    Vector3f extCamTarget_{};
    f32 extCamRoll_ = 0.0f;
    f32 extCamFovH_ = 0.0f;
    bool extCamValid_ = false;
    std::atomic<bool> syncCamera_{false};

    // Raised by the toolbar on the render thread, drained by the plugin's
    // timer on Max's.
    std::atomic<bool> resyncRequested_{false};

    std::atomic<ActorId> focusActor_{0};

    std::thread renderThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    RenderTargetId targetId_ = 0;

    i32 lastFbW_ = 0;
    i32 lastFbH_ = 0;
};

} // namespace whiteout::flakes
