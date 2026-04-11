// MDLXExporter — Debug log (writes to %TEMP%\mdlx_export_debug.log)
#pragma once

#include <fstream>
#include <string>
#include <windows.h>

#ifndef MDX_EXPORT_DEBUG
#define MDX_EXPORT_DEBUG 1
#endif

#if MDX_EXPORT_DEBUG

inline std::ofstream& mdxExportLog() {
    static std::ofstream log;
    if (!log.is_open()) {
        wchar_t buf[MAX_PATH];
        GetTempPathW(MAX_PATH, buf);
        std::wstring path(buf);
        path += L"mdlx_export_debug.log";
        log.open(path, std::ios::trunc);
        log << "=== MDLX Exporter Debug Log ===\n\n";
        log.flush();
    }
    return log;
}

#define ELOG mdxExportLog()
#define EFLUSH mdxExportLog().flush()

#else

// No-op stream that discards everything
struct NullStream {
    template<typename T> NullStream& operator<<(const T&) { return *this; }
    NullStream& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }
};
inline NullStream& mdxExportLog() { static NullStream ns; return ns; }
#define ELOG mdxExportLog()
#define EFLUSH ((void)0)

#endif
