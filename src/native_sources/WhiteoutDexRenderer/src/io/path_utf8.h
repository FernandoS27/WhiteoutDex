// ============================================================================
// path_utf8.h — UTF-8 ↔ std::filesystem::path helpers.
//
// The convention across this codebase is that every `std::string` carrying a
// path holds UTF-8 bytes. `std::filesystem::path`'s narrow constructors and
// accessors interpret `std::string` in the platform's NARROW encoding, which
// on Windows is the ANSI code page (CP_ACP) — that strips non-ASCII (e.g.
// CJK paths like `C:\…\月真女版夏侯渊正式版\…\XHYPT.mdx` become garbage). This
// header provides explicit UTF-8-safe conversions:
//
//   FsPathFromUtf8(utf8)  — std::string_view → fs::path, preserving UTF-8
//   PathToUtf8(p)         — fs::path → std::string, UTF-8 bytes
//
// Use FsPathFromUtf8 anywhere a UTF-8 `std::string` would otherwise be passed
// to `fs::path`'s narrow constructor, and PathToUtf8 anywhere a `fs::path`
// needs to round-trip back through a `std::string`.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace WhiteoutDex {

inline std::filesystem::path FsPathFromUtf8(std::string_view utf8) {
#ifdef _WIN32
    if (utf8.empty()) return {};
    const int wlen = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) return std::filesystem::path(utf8);  // fallback
    std::wstring wide(static_cast<size_t>(wlen), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                          wide.data(), wlen);
    return std::filesystem::path(std::move(wide));
#else
    return std::filesystem::path(utf8);
#endif
}

inline std::string PathToUtf8(const std::filesystem::path& p) {
    auto u8 = p.u8string();   // std::u8string in C++20
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

} // namespace WhiteoutDex
