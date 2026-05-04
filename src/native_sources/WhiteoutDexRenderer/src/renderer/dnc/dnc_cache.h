#pragma once
// ============================================================================
// DncCache — refcounted cache of parsed DNC MDL/MDX assets.
//
// Mirrors bls::BlsShaderCache structurally (Acquire/Release/refcount,
// keyed by lower-cased path). One DncAsset per unique path; identical
// paths share a parsed Model so swapping back and forth between two
// custom DNC MDLs doesn't re-parse on every toggle.
//
// Path convention follows preview.exe's hardcoded forward-slash form:
//     "Environment/DNC/DNCLordaeron/DNCLordaeronUnit/DNCLordaeronUnit.mdl"
//
// Format detection: extension-based.
//     ".mdl" → text path through whiteout::mdx::convertMdlToModel
//     ".mdx" → binary path through whiteout::mdx::Parser
// (DNC files ship as .mdl, but we accept .mdx for hosts that pre-bake.)
// ============================================================================

#include "dnc_asset.h"
#include "io/content_provider.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace WhiteoutDex::dnc {

class DncCache {
public:
    explicit DncCache(IContentProvider* contentProvider);
    ~DncCache();

    DncCache(const DncCache&) = delete;
    DncCache& operator=(const DncCache&) = delete;

    // Acquire / Release semantics mirror bls::BlsShaderCache:
    //   * Acquire returns a borrowed pointer; the cache owns the
    //     DncAsset's lifetime.
    //   * Two Acquire calls with the same key bump the same entry's
    //     refcount and return the same pointer.
    //   * Release drops one refcount; on transition to 0 the cache
    //     destroys the entry. The pointer becomes invalid.
    // Returns nullptr on any failure (file missing, parse error).
    // Failures log once and don't poison subsequent attempts on a
    // different path.
    DncAsset* Acquire(const std::string& path);
    void      Release(DncAsset* asset);
    void      ReleaseAll();

private:
    static std::string NormalizeKey(const std::string& path);
    static bool        IsTextPath(const std::string& key);

    IContentProvider* contentProvider_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<DncAsset>> entries_;
};

} // namespace WhiteoutDex::dnc
