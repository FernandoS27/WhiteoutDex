// ============================================================================
// IContentProvider — Abstract interface for reading file contents.
//
// Decouples file resolution (disk, CASC, MPQ, etc.) from consumers like
// MdxModelAdapter and the renderer's PE1 template loader.
// ============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace WhiteoutDex {

class IContentProvider {
public:
    virtual ~IContentProvider() = default;

    /// Try to read a file by its relative path (e.g. "Textures\\Brick.blp").
    /// If actualExt is non-null, it receives the extension of the file that
    /// was actually found (e.g. ".dds" when the request was for ".blp").
    virtual std::optional<std::vector<uint8_t>> ReadFile(
        const std::string& path, std::string* actualExt = nullptr) const = 0;
};

} // namespace WhiteoutDex
