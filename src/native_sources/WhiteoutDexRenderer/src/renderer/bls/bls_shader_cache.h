#pragma once
// ============================================================================
// BlsShaderCache — refcounted cache of BLS-backed gfx::ShaderHandle arrays.
//
// Mirrors GxShader::s_shaderList[stage] + GxShader::Create from Previewd.exe:
// one BlsShader entry per (stage, name) pair, each owning one gfx::ShaderHandle
// per permutation. Handles are created lazily on first load and refcounted.
// ============================================================================

#include "bls_container.h"
#include "content_provider.h"
#include "gfx/gfx.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace WhiteoutDex::bls {

struct BlsShader {
    std::string                    name;          // lowercase, e.g. "popcornfx"
    gfx::ShaderStage               stage;
    BlsContainer                   container;
    std::vector<gfx::ShaderHandle> permuteHandles;
    std::vector<PermuteHeader>     permuteHeaders;
    uint32_t                       refs = 0;

    size_t PermuteCount() const { return permuteHandles.size(); }
};

class BlsShaderCache {
public:
    BlsShaderCache(gfx::IGFXDevice* device, IContentProvider* contentProvider);
    ~BlsShaderCache();

    BlsShader* Acquire(gfx::ShaderStage stage, const std::string& name);
    void       Release(BlsShader* shader);
    void       ReleaseAll();

private:
    static constexpr size_t kStageCount = 3; // matches gfx::ShaderStage {Vertex, Pixel, Compute}

    gfx::IGFXDevice*  device_          = nullptr;
    IContentProvider* contentProvider_ = nullptr;

    std::array<std::unordered_map<std::string, std::unique_ptr<BlsShader>>, kStageCount> byStage_;

    static const char* StagePrefix(gfx::ShaderStage stage);
    static std::string Lowercase(const std::string& in);
};

} // namespace WhiteoutDex::bls
