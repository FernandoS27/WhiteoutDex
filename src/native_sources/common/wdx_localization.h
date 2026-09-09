// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 WhiteoutDex Contributors

#pragma once

/**
 * @file wdx_localization.h
 * @brief Native access to the MaxScript translation catalog (`::WdxL`).
 *
 * The importer, exporter, particle and ribbon plug-ins draw Win32 dialogs whose
 * captions come out of the .rc file, which is compiled English. Their strings
 * live in the same catalog as the rest of the toolkit —
 * src/pre_startup_scripts/WhiteoutDexLocalization.ms — so rather than duplicate
 * every language into per-language string tables, WM_INITDIALOG asks MaxScript
 * for the translations and relabels the controls it just created.
 *
 * The renderer .dlx does NOT use this. Its chrome is WhiteoutFlakes' and reads
 * the submodule's own `lang/<code>.ini` catalogs; see
 * WhiteoutDexRenderer/wdx_ui_language.h.
 *
 * ## Why one call, not one per control
 *
 * ExecuteMAXScriptScript compiles and evaluates a script every time. The import
 * dialog has about fifty labelled controls, so a call per control would be
 * fifty compiles on every open. LocalizeDialog builds ONE script that appends
 * every requested key's translation into a single delimited string, and splits
 * the answer here.
 *
 * ## Failure is not an error
 *
 * If WdxL is undefined (scripts not installed, or the plug-in loaded into a Max
 * that never ran the pre-startup scripts), every lookup fails and the dialog
 * keeps the English captions the .rc gave it. That is the same fallback the
 * MaxScript UI uses, and it is why nothing here reports an error to the user.
 */

#include <string>
#include <vector>

#include <windows.h>

#include <maxscript/maxscript.h>
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
#include <maxscript/ScriptSource.h>
#endif

namespace wdx::l10n {

/// One control and the catalog key its caption comes from.
struct DialogString {
    int controlId;   ///< IDC_* from the plug-in's resource.h, or 0 for the dialog caption.
    const char* key; ///< Catalog key, e.g. "imp_skinned_chk".
};

namespace detail {

/// Separator between the translations in the batched reply. U+001F (unit
/// separator) rather than a printable character because a translation may
/// legitimately contain a newline, a pipe or a tab — the Chinese and Japanese
/// tooltips do.
constexpr wchar_t kSep = L'\x1F';

inline std::wstring Utf8ToWide(const char* s) {
    if (!s || !*s)
        return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 1)
        return {};
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    return out;
}

/// Run @p script and return its string result. Empty on any failure, including
/// a MaxScript exception — quietErrors keeps a broken lookup from throwing a
/// Listener window in the user's face mid-dialog.
inline std::wstring EvalString(const std::wstring& script) {
    FPValue result;
    result.type = TYPE_VOID;
    BOOL ok = FALSE;
    try {
        ok = ExecuteMAXScriptScript(const_cast<wchar_t*>(script.c_str()),
#if MAX_PRODUCT_YEAR_NUMBER >= 2022
                                    MAXScript::ScriptSource::NonEmbedded,
#endif
                                    TRUE, // quietErrors
                                    &result);
    } catch (...) {
        ok = FALSE;
    }
    if (!ok || result.type != TYPE_STRING || !result.s)
        return {};
    return std::wstring(result.s);
}

} // namespace detail

/// Translate one key. Returns an empty string when the catalog is unavailable,
/// which callers read as "keep what the resource file gave us".
///
/// For more than one or two strings, prefer LocalizeDialog / TranslateMany:
/// this compiles a script per call.
inline std::wstring Tr(const char* key) {
    if (!key || !*key)
        return {};
    std::wstring script = L"(if ::WdxL == undefined then \"\" else (::WdxL.t \"";
    script += detail::Utf8ToWide(key);
    script += L"\"))";
    return detail::EvalString(script);
}

/// Translate one key, falling back to @p fallback when there is no translation.
///
/// Two different failures mean "no translation": `Tr` gives back an empty
/// string when `::WdxL` itself is missing, and gives back the key when the
/// catalog has no such entry. Neither is worth showing a user — a message box
/// reading "imp_parse_failed_msg" is worse than one reading English — so both
/// land on @p fallback.
///
/// LocalizeDialog does not need this: a control it skips keeps the caption the
/// .rc already gave it. Use TrOr for text built at runtime, where there is no
/// such standing English default.
inline std::wstring TrOr(const char* key, const wchar_t* fallback) {
    const std::wstring v = Tr(key);
    if (v.empty() || v == detail::Utf8ToWide(key))
        return fallback;
    return v;
}

/// Replace the first occurrence of @p token in @p s with @p value.
///
/// The catalog marks substitution points as %1, %2 rather than printf
/// specifiers, so that a translator can reorder them — several languages need
/// the count before the noun.
inline void Substitute(std::wstring& s, const wchar_t* token, const std::wstring& value) {
    const size_t at = s.find(token);
    if (at != std::wstring::npos)
        s.replace(at, ::wcslen(token), value);
}

/// Translate @p keys in a single MaxScript evaluation. The result has exactly
/// one entry per key, in order; an entry is empty when that key is missing (or
/// when the whole catalog is).
inline std::vector<std::wstring> TranslateMany(const std::vector<const char*>& keys) {
    std::vector<std::wstring> out(keys.size());
    if (keys.empty())
        return out;

    // `s` accumulates <translation><sep> per key. The catalog check is hoisted
    // out of the loop so an absent WdxL costs one comparison, not one per key.
    std::wstring script = L"(local s = \"\"; if ::WdxL != undefined do (";
    for (const char* k : keys) {
        script += L"s += (::WdxL.t \"";
        script += detail::Utf8ToWide(k);
        script += L"\") + \"\\x1F\"; ";
    }
    script += L"); s)";

    const std::wstring joined = detail::EvalString(script);
    if (joined.empty())
        return out;

    size_t idx = 0;
    size_t start = 0;
    while (idx < out.size() && start <= joined.size()) {
        const size_t end = joined.find(detail::kSep, start);
        if (end == std::wstring::npos)
            break;
        out[idx++] = joined.substr(start, end - start);
        start = end + 1;
    }
    return out;
}

/// Relabel a dialog's controls from the catalog in one round trip.
///
/// A @p controlId of 0 sets the dialog's own caption. A control the dialog does
/// not have, or a key the catalog does not carry, is skipped — so one table can
/// serve a dialog whose Reforged-only controls are hidden on a Classic import.
inline void LocalizeDialog(HWND hDlg, const DialogString* table, size_t count) {
    if (!hDlg || !table || count == 0)
        return;

    std::vector<const char*> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; ++i)
        keys.push_back(table[i].key);

    const std::vector<std::wstring> values = TranslateMany(keys);
    for (size_t i = 0; i < count && i < values.size(); ++i) {
        if (values[i].empty())
            continue;
        // `t` returns the key itself for a missing entry, which would put
        // "imp_skinned_chk" on a checkbox. Leaving the English caption alone is
        // the better failure.
        if (values[i] == detail::Utf8ToWide(table[i].key))
            continue;
        if (table[i].controlId == 0)
            ::SetWindowTextW(hDlg, values[i].c_str());
        else
            ::SetDlgItemTextW(hDlg, table[i].controlId, values[i].c_str());
    }
}

/// Convenience for a table declared as a C array.
template <size_t N>
inline void LocalizeDialog(HWND hDlg, const DialogString (&table)[N]) {
    LocalizeDialog(hDlg, table, N);
}

/// Relabel a radio group from a single pipe-joined catalog entry.
///
/// The catalog stores a set of mutually exclusive choices as one `"_labels"`
/// key ("New Scene|Merge") because that is how the MaxScript side consumes it
/// — `WdxL.tList` splits the same string for a dropdownlist. Sharing the entry
/// keeps one translation per choice rather than one per UI that shows it.
///
/// Nothing is changed unless the entry splits into exactly @p count parts: a
/// translator who dropped or added a separator would otherwise shift every
/// label onto the wrong button, which is worse than leaving them English.
inline void LocalizeRadioGroup(HWND hDlg, const char* listKey, const int* ids, size_t count) {
    if (!hDlg || !listKey || !ids || count == 0)
        return;
    const std::wstring joined = Tr(listKey);
    if (joined.empty() || joined == detail::Utf8ToWide(listKey))
        return;

    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;) {
        const size_t bar = joined.find(L'|', start);
        parts.push_back(joined.substr(start, bar == std::wstring::npos ? bar : bar - start));
        if (bar == std::wstring::npos)
            break;
        start = bar + 1;
    }
    if (parts.size() != count)
        return;

    for (size_t i = 0; i < count; ++i)
        if (!parts[i].empty())
            ::SetDlgItemTextW(hDlg, ids[i], parts[i].c_str());
}

template <size_t N>
inline void LocalizeRadioGroup(HWND hDlg, const char* listKey, const int (&ids)[N]) {
    LocalizeRadioGroup(hDlg, listKey, ids, N);
}

} // namespace wdx::l10n
