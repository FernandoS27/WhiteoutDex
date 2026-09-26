// MDLXExporter — Export target path helpers
//
// Shared by DoExport (which writes the file) and the export dialog (which
// previews the name in its header), so both always agree on the file name an
// auto-incremented export ends up with.
#pragma once

#include <windows.h>
#include <string>

namespace mdx_export {

inline bool FileExistsW(const std::wstring& path) {
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// The path an auto-incremented export writes to.
//
// Strips an existing trailing _<digits> from the base name first, so running
// auto-increment twice doesn't yield base_1_1.mdx, then walks _1, _2, ... to
// the first name that does not exist. Callers only use this when |path|
// itself exists. |outCounter| (optional) receives the number that was used.
inline std::wstring AutoIncrementPath(const std::wstring& path, int* outCounter = nullptr) {
    const size_t sl = path.find_last_of(L"/\\");
    const std::wstring dir = (sl != std::wstring::npos) ? path.substr(0, sl + 1) : std::wstring();
    std::wstring base = (sl != std::wstring::npos) ? path.substr(sl + 1) : path;
    std::wstring ext;
    const size_t dt = base.rfind(L'.');
    if (dt != std::wstring::npos) {
        ext = base.substr(dt);
        base = base.substr(0, dt);
    }

    const size_t us = base.rfind(L'_');
    if (us != std::wstring::npos && us + 1 < base.size()) {
        bool allDigits = true;
        for (size_t i = us + 1; i < base.size(); ++i)
            if (base[i] < L'0' || base[i] > L'9') { allDigits = false; break; }
        if (allDigits) base = base.substr(0, us);
    }

    int counter = 1;
    std::wstring candidate = dir + base + L"_" + std::to_wstring(counter) + ext;
    while (FileExistsW(candidate)) {
        ++counter;
        candidate = dir + base + L"_" + std::to_wstring(counter) + ext;
    }
    if (outCounter) *outCounter = counter;
    return candidate;
}

} // namespace mdx_export
