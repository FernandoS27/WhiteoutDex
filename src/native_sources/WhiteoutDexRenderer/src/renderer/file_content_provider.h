// ============================================================================
// FileContentProvider — Reads file contents from disk, CASC, or MPQ archives.
//
// Auto-discovers Warcraft III installation via the Blizzard game finder.
// Tries CASC storage first (Reforged), then falls back to MPQ chain
// (War3Patch.mpq → War3x.mpq → war3.mpq).
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace WhiteoutDex {

class FileContentProvider {
public:
    FileContentProvider();
    ~FileContentProvider();

    FileContentProvider(FileContentProvider&&) noexcept;
    FileContentProvider& operator=(FileContentProvider&&) noexcept;

    FileContentProvider(const FileContentProvider&) = delete;
    FileContentProvider& operator=(const FileContentProvider&) = delete;

    /// Set the base path used for local disk resolution (FileResolver).
    void SetBasePath(const std::filesystem::path& basePath);

    /// Try to read a file by its relative path (e.g. "Textures\\Brick.blp").
    /// Search order: disk (via FileResolver) → CASC → MPQ chain.
    /// If actualExt is non-null, it receives the extension of the file that
    /// was actually found (e.g. ".dds" when the request was for ".blp").
    std::optional<std::vector<uint8_t>> ReadFile(const std::string& path,
                                                  std::string* actualExt = nullptr) const;

    /// Whether a CASC storage was successfully opened.
    bool HasCasc() const;

    /// Whether at least one MPQ archive was successfully opened.
    bool HasMpq() const;

    /// The discovered Warcraft III installation path (empty if not found).
    const std::string& Wc3Path() const;

private:
    std::optional<std::vector<uint8_t>> ReadFromCasc(const std::string& path,
                                                      std::string* actualExt) const;
    std::optional<std::vector<uint8_t>> ReadFromMpq(const std::string& path,
                                                     std::string* actualExt) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace WhiteoutDex
