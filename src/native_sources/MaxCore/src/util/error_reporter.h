// MaxCore — Structured error/warning log for export operations
#pragma once

#include <string>
#include <vector>
#include <Windows.h>

namespace core {

enum class Severity { Info, Warning, Error };

/// Which run produced the entries. Only affects the wording of the summary
/// dialog — the importer used to borrow the exporter's "Export completed"
/// heading, which read as the wrong operation entirely.
enum class Operation { Import, Export };

class ExportErrorReporter {
public:
    struct Entry {
        Severity severity;
        std::wstring message;
        std::wstring nodeName;
    };

    void info(const std::wstring& msg, const std::wstring& node = L"") {
        entries_.push_back({Severity::Info, msg, node});
    }

    void warning(const std::wstring& msg, const std::wstring& node = L"") {
        entries_.push_back({Severity::Warning, msg, node});
    }

    void error(const std::wstring& msg, const std::wstring& node = L"") {
        entries_.push_back({Severity::Error, msg, node});
    }

    bool hasWarnings() const {
        for (auto& e : entries_)
            if (e.severity == Severity::Warning) return true;
        return false;
    }

    bool hasErrors() const {
        for (auto& e : entries_)
            if (e.severity == Severity::Error) return true;
        return false;
    }

    const std::vector<Entry>& entries() const { return entries_; }

    /// Show the collected entries in a message box, with the heading, the
    /// per-row severity prefixes and the caption taken from the translation
    /// catalog. The entry text itself is whatever the caller passed in.
    void showSummaryDialog(HWND parent, Operation op = Operation::Export) const;

private:
    std::vector<Entry> entries_;
};

} // namespace core
