// MaxCore — ExportErrorReporter implementation
#include "error_reporter.h"
#include <sstream>

namespace core {

void ExportErrorReporter::showSummaryDialog(HWND parent) const {
    int errors = 0, warnings = 0;
    for (auto& e : entries_) {
        if (e.severity == Severity::Error) ++errors;
        else if (e.severity == Severity::Warning) ++warnings;
    }

    std::wostringstream ss;
    ss << L"Export completed with " << errors << L" error(s) and "
       << warnings << L" warning(s).\n\n";

    int shown = 0;
    for (auto& e : entries_) {
        if (e.severity == Severity::Info) continue;
        if (shown >= 50) {
            ss << L"\n... and more (see log for full details)";
            break;
        }
        const wchar_t* prefix = (e.severity == Severity::Error) ? L"ERROR" : L"WARNING";
        ss << prefix << L": ";
        if (!e.nodeName.empty())
            ss << L"[" << e.nodeName << L"] ";
        ss << e.message << L"\n";
        ++shown;
    }

    UINT icon = errors > 0 ? MB_ICONERROR : MB_ICONWARNING;
    MessageBoxW(parent, ss.str().c_str(), L"WhiteoutDex Export", MB_OK | icon);
}

} // namespace core
