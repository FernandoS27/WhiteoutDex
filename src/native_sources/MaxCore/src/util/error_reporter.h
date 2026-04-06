// MaxCore — Structured error/warning log for export operations
#pragma once

#include <string>
#include <vector>
#include <Windows.h>

namespace core {

enum class Severity { Info, Warning, Error };

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

    void showSummaryDialog(HWND parent) const;

private:
    std::vector<Entry> entries_;
};

} // namespace core
