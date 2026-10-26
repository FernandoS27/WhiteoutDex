// MDX strings (node, sequence, material names, texture paths) are bytes
// without a declared encoding. Blizzard's are ASCII. Models made on a Chinese
// Windows - NeoDex and the other Chinese tools write the ANSI code page -
// carry GBK (a two-character name as C3 B1 D7 D3), newer tools UTF-8.
//
// The importer used to widen each byte to a character and the exporter to
// cut each character back to a byte: the bytes survived, but Max showed
// four Latin-1 letters for two Chinese ones - on a Chinese Windows too - and a name typed in Max in
// Chinese came out as garbage. Sequence names were even written back as
// UTF-8 of those widened bytes, so a GBK sequence name changed its bytes.
//
//   mdxToWide: valid UTF-8 with non-ASCII bytes -> UTF-8, otherwise the
//              Windows ANSI code page (GBK on a Chinese Windows, 1252 on a
//              German one: the same bytes come back on export either way)
//   wideToMdx: the ANSI code page when every character has a byte form
//              there (what NeoDex writes on that system), otherwise UTF-8
#pragma once

#include <windows.h>
#include <string>

namespace wdx::text {

// The code page of the model being imported or exported: detected by the
// importer from the file's own names (detectCodePage), kept in the scene as
// the root node's user property Wc3CodePage, and read back by the exporter,
// so a GBK model shows its Chinese names in a German 3ds Max and exports the
// same bytes again. 0 = the Windows ANSI code page.
inline UINT& activeCodePage()
{
    static UINT cp = 0;
    return cp;
}

struct CodePageScope {
    explicit CodePageScope(UINT cp) { activeCodePage() = cp; }
    ~CodePageScope() { activeCodePage() = 0; }
    CodePageScope(const CodePageScope&) = delete;
    CodePageScope& operator=(const CodePageScope&) = delete;
};

// GBK: ASCII, or a lead byte 0x81-0xFE followed by 0x40-0xFE except 0x7F.
inline bool isValidGbk(const std::string& s)
{
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) continue;
        if (c == 0x80 || c == 0xFF || i + 1 >= s.size()) return false;
        const unsigned char t = static_cast<unsigned char>(s[++i]);
        if (t < 0x40 || t == 0x7F || t == 0xFF) return false;
    }
    return true;
}

inline bool isValidUtf8WithNonAscii(const std::string& s);

// One code page for all non-ASCII strings of a model: UTF-8 when they all
// are, GBK when they all are valid GBK (the Chinese tools' output), else the
// Windows code page. 0 when every string is ASCII.
template <typename Strings>
UINT detectCodePage(const Strings& texts)
{
    bool any = false, allUtf8 = true, allGbk = true;
    for (const std::string& s : texts) {
        bool nonAscii = false;
        for (unsigned char c : s) if (c >= 0x80) { nonAscii = true; break; }
        if (!nonAscii) continue;
        any = true;
        if (!isValidUtf8WithNonAscii(s)) allUtf8 = false;
        if (!isValidGbk(s)) allGbk = false;
    }
    if (!any) return 0;
    if (allUtf8) return CP_UTF8;
    if (allGbk) return 936;
    return 0;
}

inline bool isValidUtf8WithNonAscii(const std::string& s)
{
    bool nonAscii = false;
    for (unsigned char c : s) if (c >= 0x80) { nonAscii = true; break; }
    if (!nonAscii) return false;
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                 static_cast<int>(s.size()), nullptr, 0) > 0;
}

inline std::wstring mdxToWide(const std::string& s)
{
    if (s.empty()) return {};
    UINT cp = activeCodePage() ? activeCodePage() : CP_ACP;
    if (isValidUtf8WithNonAscii(s)) cp = CP_UTF8;
    else if (cp == CP_UTF8) cp = CP_ACP;
    const int n = ::MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(cp, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

inline std::string wideToMdx(const std::wstring& w)
{
    if (w.empty()) return {};
    bool ascii = true;
    for (wchar_t c : w) if (c >= 0x80) { ascii = false; break; }
    if (ascii) return std::string(w.begin(), w.end());
    // The model's own code page; a scene made in Max (none stored) tries the
    // Windows code page, then GBK - Chinese names come out as the Chinese
    // tools and NeoDex write them, whichever Windows exports - then UTF-8.
    const UINT active = activeCodePage();
    const UINT tries[2] = {active ? active : ::GetACP(), active ? 0u : 936u};
    UINT cp = CP_UTF8;
    int n = 0;
    for (UINT t : tries) {
        if (t == 0 || t == CP_UTF8) continue;
        BOOL lossy = FALSE;
        const int m = ::WideCharToMultiByte(t, WC_NO_BEST_FIT_CHARS, w.data(), static_cast<int>(w.size()),
                                            nullptr, 0, nullptr, &lossy);
        if (m > 0 && !lossy) { cp = t; n = m; break; }
    }
    if (cp == CP_UTF8) {
        cp = CP_UTF8;
        n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
    }
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(cp, cp != CP_UTF8 ? WC_NO_BEST_FIT_CHARS : 0, w.data(), static_cast<int>(w.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

inline std::string wideToMdx(const wchar_t* w) { return w ? wideToMdx(std::wstring(w)) : std::string(); }

} // namespace wdx::text
