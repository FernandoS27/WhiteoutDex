#pragma once
// ============================================================================
// WhiteoutDex Renderer — ModelTemplateManager
//
// Owns the cross-instance ModelTemplate cache, the async loader thread, and
// the drain step that publishes async results into the cache. Extracted from
// RenderService in Phase 2.
//
// Identity (Phase 2 form): paths are the cache key. The (filename, content
// hash) keying called for in the design lands in a follow-up — keeping the
// extraction itself surgical.
//
// Threading:
//   - The cache map is protected by `cacheMutex_`.
//   - The pending-set + queue are protected by `queueMutex_` (signalled by
//     `queueCV_`).
//   - The result staging vector is protected by `resultMutex_`.
//
// Tick discipline (F7 from the architecture review):
//   - Tick() is the host's responsibility to call. RenderService::Tick still
//     forwards to it for now; once SceneManager exists in Phase 5, the host
//     loop calls templates.Tick() explicitly before scene.Tick().
// ============================================================================

#include "model_template.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace WhiteoutDex { class IContentProvider; }
namespace WhiteoutDex::gfx { class IGFXDevice; }

namespace WhiteoutDex {

class ModelTemplateManager {
public:
    using TextureCacheQuery = std::function<bool(std::string_view)>;

    ModelTemplateManager();
    ~ModelTemplateManager();
    ModelTemplateManager(const ModelTemplateManager&)            = delete;
    ModelTemplateManager& operator=(const ModelTemplateManager&) = delete;

    // ---- Configuration (called from RenderService init / SetContentProvider) ----
    void SetContentProvider(IContentProvider* provider);
    void SetBasePath(std::filesystem::path basePath);
    void SetTextureCacheQuery(TextureCacheQuery q);

    // ---- Lookup / load ----
    // Cache only — never blocks, never queues. Returns nullptr on miss.
    std::shared_ptr<ModelTemplate> Lookup(const std::string& mdxPath);

    // Cache check + queue an async load on miss. Returns nullptr until the
    // worker drains (caller polls via Lookup or via the manager's Tick).
    std::shared_ptr<ModelTemplate> GetOrLoadAsync(const std::string& mdxPath);

    // Cache check + synchronous parse on miss. Used by AddModelByPath /
    // LoadModelByPath where the caller wants the model alive on return.
    // Cancels any pending async request for the same path (the worker will
    // see a cache hit on its next pass).
    std::shared_ptr<ModelTemplate> GetOrLoadSync(const std::string& mdxPath);

    // Inject a pre-built template (used by the Max path in Phase 5 — adopts
    // the live-source snapshot into the cache). Returns the same pointer.
    std::shared_ptr<ModelTemplate> Adopt(const std::string& key,
                                         std::shared_ptr<ModelTemplate>);

    // ---- Tick (host-pumped — see header note) ----
    void Tick();

    // ---- Shutdown ----
    void ReleaseAllGPU(gfx::IGFXDevice& gfx);
    void Clear();

private:
    void StartLoader();
    void StopLoader();
    void LoaderFunc();

    // Synchronous parse — runs on the caller's thread (loader OR API thread).
    // Reads the file, parses, builds a ModelTemplate. Doesn't touch the cache.
    std::shared_ptr<ModelTemplate> ParseAndBuild(const std::string& mdxPath);

    // ---- Configured dependencies ----
    IContentProvider*     contentProvider_ = nullptr;
    std::filesystem::path basePath_;
    TextureCacheQuery     textureCacheQuery_;

    // ---- Cache ----
    mutable std::mutex                                              cacheMutex_;
    std::unordered_map<std::string, std::shared_ptr<ModelTemplate>> cache_;

    // ---- Async loader ----
    std::thread                     loaderThread_;
    std::atomic<bool>               loaderRunning_{false};
    std::mutex                      queueMutex_;
    std::condition_variable         queueCV_;
    std::deque<std::string>         loadQueue_;
    std::unordered_set<std::string> loadPending_;
    std::mutex                      resultMutex_;
    std::vector<std::pair<std::string, std::shared_ptr<ModelTemplate>>> loadResults_;
};

} // namespace WhiteoutDex
