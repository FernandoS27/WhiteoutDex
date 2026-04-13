// ============================================================================
// FileResolver — Resolves external file references (textures, models) by
// searching relative to a base path with extension fallback.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <span>

namespace WhiteoutDex {

class FileResolver {
public:
    explicit FileResolver(const std::filesystem::path& basePath = {});

    void SetBasePath(const std::filesystem::path& basePath) { basePath_ = basePath; }
    const std::filesystem::path& BasePath() const { return basePath_; }

    // Resolve a file given a relative path and a list of candidate extensions.
    // Search order for each extension set:
    //   1. basePath / relativePath  (original ext, then each alt ext)
    //   2. basePath / filename      (original ext, then each alt ext)
    // Returns empty path if not found.
    std::filesystem::path Resolve(const std::string& relativePath,
                                  std::span<const char* const> extensions) const;

    // Convenience: resolve a texture file (.blp, .dds, .tga, .png)
    std::filesystem::path ResolveTexture(const std::string& relativePath) const;

    // Convenience: resolve a model file (.mdx, .mdl)
    std::filesystem::path ResolveModel(const std::string& relativePath) const;

    // Normalize path separators (backslash → forward slash)
    static std::string NormalizeSeparators(const std::string& path);

private:
    std::filesystem::path basePath_;
};

} // namespace WhiteoutDex
