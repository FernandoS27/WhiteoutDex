// MaxCore — ExportErrorReporter implementation
#include "error_reporter.h"

#include <sstream>

#include <wdx_localization.h>

namespace core {

using wdx::l10n::Substitute;
using wdx::l10n::TrOr;

void ExportErrorReporter::showSummaryDialog(HWND parent, Operation op) const {
    int errors = 0, warnings = 0;
    for (auto& e : entries_) {
        if (e.severity == Severity::Error) ++errors;
        else if (e.severity == Severity::Warning) ++warnings;
    }

    const bool isImport = (op == Operation::Import);

    std::wstring heading = TrOr(
        isImport ? "report_import_summary_msg" : "report_export_summary_msg",
        isImport ? L"Import completed with %1 error(s) and %2 warning(s)."
                 : L"Export completed with %1 error(s) and %2 warning(s).");
    Substitute(heading, L"%1", std::to_wstring(errors));
    Substitute(heading, L"%2", std::to_wstring(warnings));

    const std::wstring errorPrefix = TrOr("report_error_prefix", L"ERROR");
    const std::wstring warningPrefix = TrOr("report_warning_prefix", L"WARNING");

    std::wostringstream ss;
    ss << heading << L"\n\n";

    int shown = 0;
    for (auto& e : entries_) {
        if (e.severity == Severity::Info) continue;
        if (shown >= 50) {
            ss << L"\n"
               << TrOr("report_more_entries_msg",
                       L"... and more (see the log for the full details)");
            break;
        }
        ss << (e.severity == Severity::Error ? errorPrefix : warningPrefix) << L": ";
        if (!e.nodeName.empty())
            ss << L"[" << e.nodeName << L"] ";
        ss << e.message << L"\n";
        ++shown;
    }

    const std::wstring caption = TrOr(
        isImport ? "report_import_ptitle" : "report_export_ptitle",
        isImport ? L"WhiteoutDex Import" : L"WhiteoutDex Export");

    UINT icon = errors > 0 ? MB_ICONERROR : MB_ICONWARNING;
    MessageBoxW(parent, ss.str().c_str(), caption.c_str(), MB_OK | icon);
}

} // namespace core
