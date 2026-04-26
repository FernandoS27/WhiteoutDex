// ============================================================================
// WhiteoutDex Renderer — ModelTemplateManager (impl)
// ============================================================================

#include "model_template_manager.h"

#include "../gfx/gfx.h"
#include "../io/content_provider.h"
#include "../io/mdx_model_adapter.h"
#include "animation.h"             // SkinningData
#include <whiteout/models/mdx/parser.h>

#include <chrono>
#include <span>
#include <utility>

namespace WhiteoutDex {

ModelTemplateManager::ModelTemplateManager() {
    StartLoader();
}

ModelTemplateManager::~ModelTemplateManager() {
    StopLoader();
}

void ModelTemplateManager::SetContentProvider(IContentProvider* provider) {
    contentProvider_ = provider;
}

void ModelTemplateManager::SetBasePath(std::filesystem::path basePath) {
    basePath_ = std::move(basePath);
}

void ModelTemplateManager::SetTextureCacheQuery(TextureCacheQuery q) {
    textureCacheQuery_ = std::move(q);
}

// ---- Lookup / load --------------------------------------------------------

std::shared_ptr<ModelTemplate>
ModelTemplateManager::Lookup(const std::string& mdxPath) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(mdxPath);
    return (it != cache_.end()) ? it->second : nullptr;
}

std::shared_ptr<ModelTemplate>
ModelTemplateManager::GetOrLoadAsync(const std::string& mdxPath) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(mdxPath);
        if (it != cache_.end()) return it->second;  // hit (or cached failure)
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (loadPending_.count(mdxPath)) return nullptr;   // already queued
        loadPending_.insert(mdxPath);
        loadQueue_.push_back(mdxPath);
    }
    queueCV_.notify_one();
    return nullptr;
}

std::shared_ptr<ModelTemplate>
ModelTemplateManager::GetOrLoadSync(const std::string& mdxPath) {
    // 1. Cache hit (or cached failure).
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = cache_.find(mdxPath);
        if (it != cache_.end()) return it->second;
    }
    // 2. Synchronous parse. The async worker may have this queued; we just
    // produce the result ourselves and let DrainTemplateResults find a cache
    // hit on its next pass.
    auto tmpl = ParseAndBuild(mdxPath);
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_[mdxPath] = tmpl;     // cache failure (nullptr) too
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        loadPending_.erase(mdxPath);
    }
    return tmpl;
}

std::shared_ptr<ModelTemplate>
ModelTemplateManager::Adopt(const std::string& key,
                            std::shared_ptr<ModelTemplate> tmpl) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_[key] = tmpl;
    return tmpl;
}

// ---- Tick ----------------------------------------------------------------

void ModelTemplateManager::Tick() {
    std::vector<std::pair<std::string, std::shared_ptr<ModelTemplate>>> results;
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        results.swap(loadResults_);
    }
    if (results.empty()) return;
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        for (auto& [path, tmpl] : results) cache_[path] = tmpl;
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (auto& [path, _] : results) loadPending_.erase(path);
    }
}

// ---- Shutdown ------------------------------------------------------------

void ModelTemplateManager::ReleaseAllGPU(gfx::IGFXDevice& gfx) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (auto& [path, tmpl] : cache_) {
        if (tmpl) tmpl->ReleaseGPU(gfx);
    }
}

void ModelTemplateManager::Clear() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
}

// ---- Async loader --------------------------------------------------------

void ModelTemplateManager::StartLoader() {
    loaderRunning_ = true;
    loaderThread_  = std::thread(&ModelTemplateManager::LoaderFunc, this);
}

void ModelTemplateManager::StopLoader() {
    loaderRunning_ = false;
    queueCV_.notify_one();
    if (loaderThread_.joinable()) loaderThread_.join();
}

void ModelTemplateManager::LoaderFunc() {
    while (loaderRunning_) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCV_.wait_for(lock, std::chrono::milliseconds(50),
                [this] { return !loadQueue_.empty() || !loaderRunning_; });
            if (!loaderRunning_) break;
            if (loadQueue_.empty()) continue;
            path = std::move(loadQueue_.front());
            loadQueue_.pop_front();
        }

        auto tmpl = ParseAndBuild(path);

        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            loadResults_.emplace_back(std::move(path), std::move(tmpl));
        }
    }
}

// ---- Parse + build (the meat of the old loadTemplateSync) ---------------

std::shared_ptr<ModelTemplate>
ModelTemplateManager::ParseAndBuild(const std::string& mdxPath) {
    if (!contentProvider_) return nullptr;
    auto fileData = contentProvider_->ReadFile(mdxPath);
    if (!fileData || fileData->empty()) return nullptr;

    whiteout::mdx::Parser mdxParser;
    whiteout::mdx::Model model;
    try {
        model = mdxParser.parse(
            std::span<const whiteout::u8>(fileData->data(), fileData->size()));
    } catch (...) {
        return nullptr;
    }

    namespace fs = std::filesystem;
    fs::path texBasePath =
        basePath_.empty() ? fs::path(mdxPath).parent_path() : basePath_;

    auto tmpl    = std::make_shared<ModelTemplate>();
    auto adapter = std::make_shared<MdxModelAdapter>(
        std::move(model), texBasePath, contentProvider_);
    if (textureCacheQuery_) adapter->SetTextureCacheQuery(textureCacheQuery_);

    tmpl->adapter           = adapter;
    tmpl->meshes            = adapter->GetMeshes();
    tmpl->textures          = adapter->GetTextures();
    tmpl->materials         = adapter->GetMaterials();
    tmpl->skeleton          = adapter->GetSkeleton();
    tmpl->skinWeights       = adapter->GetSkinWeights();
    tmpl->pe2Configs        = adapter->GetParticleConfigs();
    tmpl->ribbonConfigs     = adapter->GetRibbonConfigs();
    tmpl->collisionConfigs  = adapter->GetCollisionShapes();
    tmpl->pe1Configs        = adapter->GetPE1Configs();
    tmpl->attachmentConfigs = adapter->GetAttachmentConfigs();
    tmpl->cameraPresets     = adapter->GetCameraPresets();

    // Build the immutable shared SkinningData once. Every instance that
    // adopts this template calls SetSharedData(skinningData) instead of
    // re-running the per-vertex influence copy.
    auto skinningData = std::make_shared<SkinningData>();
    skinningData->nodeCount           = tmpl->skeleton.nodeCount;
    skinningData->inverseBindMatrices = tmpl->skeleton.inverseBindMatrices;
    for (auto& sw : tmpl->skinWeights) {
        const int vc = (int)sw.influences.size();
        auto& info = skinningData->geosetWeights[sw.geosetId];
        info.vertices.resize(vc);
        for (int v = 0; v < vc; v++) {
            for (int j = 0; j < 4; j++) {
                info.vertices[v].boneIdx[j] = sw.influences[v].boneIdx[j];
                info.vertices[v].weight[j]  = sw.influences[v].weight[j];
            }
        }
        GeosetPaletteLayout layout;
        layout.subsetNodeIndices = sw.subsetNodeIndices;
        layout.groupAverages     = sw.groupAverages;
        skinningData->geosetLayouts[sw.geosetId] = std::move(layout);
    }
    tmpl->skinningData = std::move(skinningData);

    return tmpl;
}

} // namespace WhiteoutDex
