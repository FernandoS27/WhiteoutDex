// ============================================================================
// WhiteoutDex — MaxScript bindings for WhiteoutFlakes' CASC storage browser.
//
// The engine already knows how to walk a CASC install, an MPQ archive or a
// loose folder and present only the model/effect files as a folder tree
// (io::StorageBrowser). MaxScript had no way to reach it, so every place that
// wants a game asset path — the Popcorn emitter's .pkfx, an attachment point's
// .mdx — fell back to getOpenFileName and produced a local disk path, which is
// not what MDX stores. These primitives hand MaxScript the same tree, and the
// paths they hand back are the CASC-relative display form
// ("units\nightelf\druid\druid.mdx") — exactly what the exporter writes and
// what the renderer's content provider resolves back through CASC at draw time.
//
// One browser lives per process. Re-opening the same root is a no-op, so the
// manifest walk — seconds on a Reforged install — is paid once per session
// however many times a rollout opens the dialog.
//
// MaxScript API (WhiteoutDexModelBrowser.ms builds the dialog on top of it):
//   WdxBrowserOpen <root> <kind>     → "" on success, else the error message
//                                      kind: -1 auto, 0 CASC, 1 MPQ, 2 folder
//   WdxBrowserClose()                → releases the tree and the CASC handle
//   WdxBrowserIsOpen()               → true/false
//   WdxBrowserRoot()                 → the root it was opened with
//   WdxBrowserKind()                 → 0 Casc / 1 Mpq / 2 Folder, -1 closed
//   WdxBrowserProduct()              → "Warcraft III" / "World of Warcraft" / …
//   WdxBrowserAvailableTypes()       → bitmask the open storage actually holds
//   WdxBrowserSetTypes <mask>        → 1 Models | 2 Effects | 4 M2 | 8 M3 | 16 Actor
//   WdxBrowserSetFilter <pattern>    → free-text filter, "" clears
//   WdxBrowserFolders <displayPath>  → #(names) of subfolders
//   WdxBrowserFiles <displayPath>    → #(names) of files directly inside it
//   WdxBrowserChildPath <path> <name>→ original archive path (for the loader)
//   WdxBrowserRelPath <path> <name>  → mod-chain-stripped path (what MDX stores)
//   WdxBrowserSearch <maxResults>    → #(display paths) matching the filter
//   WdxBrowserMatchCount()           → how many files the filter left standing
//
//   WdxExtractAsset <root> <path> <dest>
//                                    → copy one file out onto disk; "" on success
//
// The picker itself is WdxPickAsset at the bottom of this file: it runs
// WhiteoutFlakes' StorageExplorer — the same grid/tree browser with live model
// thumbnails the standalone Model Explorer shows — and returns what was chosen.
// The WdxBrowser* family above is the headless half, for a host that wants the
// listing without a window.
// ============================================================================

#include "asset_picker_window.h"
#include "io/storage_browser.h"

#if WHITEOUT_HAS_CASC
// WdxExtractAsset reads bytes, which the browser cannot do — it walks a
// manifest and hands back names. Going through the registry rather than
// opening a storage of its own is what lets the handle it caches be the same
// one the picker gets on its next open.
#include "io/storage/casc_registry.h"
#endif

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

// clang-format off
#include <max.h>
#include <maxscript/maxscript.h>
#include <maxscript/util/listener.h>
#include <maxscript/foundation/arrays.h>
#include <maxscript/foundation/numbers.h>
#include <maxscript/foundation/strings.h>
// MUST be the LAST maxscript header — the others pull in
// define_implementations.h, which turns def_visible_primitive into a no-op.
// See the same note in dllmain.cpp.
#include <maxscript/macros/define_instantiation_functions.h>
// clang-format on

namespace {

using whiteout::flakes::ProductId;
using whiteout::flakes::io::BrowseType;
using whiteout::flakes::io::StorageBrowser;
using whiteout::flakes::io::StorageKind;

// ── String marshalling ──────────────────────────────────────────────────────
// MaxScript is wide, the browser is UTF-8. Asset paths inside an archive are
// ASCII, but the install root the user typed need not be, so convert properly
// rather than truncating the way the W3Path read in dllmain.cpp does.

std::string ToUtf8(const MCHAR* w) {
    if (!w || !w[0])
        return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1)
        return {};
    std::string out(static_cast<size_t>(len - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string& s) {
    if (s.empty())
        return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 1)
        return {};
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

// to_string() throws on a value that is not string-like. A rollout that passes
// the wrong thing should get an empty argument back, not a MaxScript exception
// thrown out of the middle of a listbox refresh.
std::string ArgToUtf8(Value* v) {
    if (!v || v == &undefined)
        return {};
    try {
        return ToUtf8(v->to_string());
    } catch (...) {
        return {};
    }
}

int ArgToInt(Value* v, int fallback) {
    if (!v || v == &undefined)
        return fallback;
    try {
        return v->to_int();
    } catch (...) {
        return fallback;
    }
}

Value* MakeString(const std::string& s) {
    const std::wstring w = ToWide(s);
    return new String(w.empty() ? _M("") : w.c_str());
}

// ── The one browser ─────────────────────────────────────────────────────────
// Static rather than heap: it owns a tree of strings plus a shared_ptr into the
// CASC registry, and keeping it alive across dialog invocations is the whole
// point — the walk, not the open, is what costs.
StorageBrowser g_browser;
std::string g_openRoot; // as the caller spelled it, for the "already open" test

bool EqualsNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::towlower(static_cast<std::wint_t>(ca)) !=
            std::towlower(static_cast<std::wint_t>(cb)))
            return false;
    }
    return true;
}

const MCHAR* ProductName(ProductId id) {
    switch (id) {
    case ProductId::Wc3:
        return _M("Warcraft III");
    case ProductId::Wow:
        return _M("World of Warcraft");
    case ProductId::Sc2:
        return _M("StarCraft II");
    case ProductId::D3:
        return _M("Diablo III");
    case ProductId::Neutral:
    default:
        return _M("");
    }
}

// Depth-first walk of the filtered tree, collecting full display paths. Bounded
// by `limit` because an unfiltered search of a retail install runs to hundreds
// of thousands of rows and a MaxScript listbox will not survive being handed
// them. TreeChildren already prunes to the current filter, so this only ever
// descends into subtrees that hold a match.
void CollectMatches(const std::string& path, int limit, std::vector<std::string>& out) {
    if (static_cast<int>(out.size()) >= limit)
        return;
    const auto listing = g_browser.TreeChildren(path);
    for (const auto& f : listing.files) {
        if (static_cast<int>(out.size()) >= limit)
            return;
        out.push_back(path.empty() ? f : path + '\\' + f);
    }
    for (const auto& d : listing.folders) {
        if (static_cast<int>(out.size()) >= limit)
            return;
        CollectMatches(path.empty() ? d : path + '\\' + d, limit, out);
    }
}

// ── Extraction ──────────────────────────────────────────────────────────────

#if WHITEOUT_HAS_CASC

using whiteout::flakes::io::AcquireSharedCasc;
using whiteout::flakes::io::CascOpenKey;
using whiteout::flakes::io::SharedCasc;

// The install the last extraction used. Registry entries are weak and the
// picker drops its handle as its window closes, so without this every import
// would reopen the storage the picker had open a moment earlier. Holding one
// handle makes the second import free — and the picker's next open too, since
// it asks the same registry. Released by WdxBrowserClose with the browser tree.
std::shared_ptr<const SharedCasc> g_extractCasc;
std::string g_extractRoot;

const SharedCasc* AcquireForExtract(const std::string& root, std::string& error) {
    if (g_extractCasc && EqualsNoCase(g_extractRoot, root))
        return g_extractCasc.get();

    CascOpenKey key;
    key.root = root;
    auto shared = AcquireSharedCasc(key, error);
    if (!shared)
        return nullptr;

    g_extractCasc = std::move(shared);
    g_extractRoot = root;
    return g_extractCasc.get();
}

// mkdir -p for the directory holding `file`. CreateDirectoryW only makes the
// leaf, and the extraction mirrors the archive's folders into a temp tree that
// is several levels deep and does not exist yet.
bool EnsureParentDirs(const std::wstring& file) {
    const auto lastSep = file.find_last_of(L"\\/");
    if (lastSep == std::wstring::npos)
        return true;

    // Start past the root ("C:\" or "\\server\share\") so we never try to
    // create a drive or a UNC share.
    std::wstring::size_type at = 0;
    if (file.size() >= 2 && file[1] == L':')
        at = 2;
    while (at < lastSep && (file[at] == L'\\' || file[at] == L'/'))
        ++at;

    // Errors on the way up are ignored and only the result is checked: the
    // early components are a drive or a UNC share, which cannot be created and
    // report it in several different ways. What matters is whether the leaf
    // directory is there at the end.
    for (; at <= lastSep; ++at) {
        if (at != lastSep && file[at] != L'\\' && file[at] != L'/')
            continue;
        const std::wstring dir = file.substr(0, at);
        if (!dir.empty())
            ::CreateDirectoryW(dir.c_str(), nullptr);
    }

    const DWORD attrs = ::GetFileAttributesW(file.substr(0, lastSep).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool WriteWholeFile(const std::wstring& path, const std::vector<whiteout::u8>& data) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    bool okWrite = true;
    size_t written = 0;
    while (written < data.size()) {
        const DWORD chunk =
            static_cast<DWORD>((std::min)(data.size() - written, static_cast<size_t>(1u << 20)));
        DWORD got = 0;
        if (!::WriteFile(h, data.data() + written, chunk, &got, nullptr) || got == 0) {
            okWrite = false;
            break;
        }
        written += got;
    }
    ::CloseHandle(h);
    if (!okWrite)
        ::DeleteFileW(path.c_str());
    return okWrite;
}

// Read `archivePath` out of `casc`, accepting either spelling the rest of this
// file deals in: the original archive path the browser hands back (mod chain
// and all) or the stripped relative path a resource stores. The chain order
// mirrors CascSource's — HD overrides win — so a bare path resolves to the
// same file the renderer would draw.
bool ReadArchiveFile(const SharedCasc& casc, const std::string& archivePath,
                     std::vector<whiteout::u8>& out) {
    if (auto data = casc.Storage().readFile(archivePath)) {
        out = std::move(*data);
        return true;
    }
    if (archivePath.find(':') != std::string::npos)
        return false; // already fully qualified; a miss is a miss

    static const char* kPrefixes[] = {"war3.w3mod:_hd.w3mod:", "war3.w3mod:"};
    for (const char* prefix : kPrefixes) {
        if (auto data = casc.Storage().readFile(prefix + archivePath)) {
            out = std::move(*data);
            return true;
        }
    }
    return false;
}

#endif // WHITEOUT_HAS_CASC

} // namespace

// ============================================================================
// WdxBrowserOpen <root> <kind>
//
// kind: -1 auto-detect, 0 Casc, 1 Mpq, 2 Folder. Returns "" on success and the
// failure message otherwise, so a rollout can put the reason straight into a
// label instead of inferring it from a bool.
// ============================================================================
def_visible_primitive(WdxBrowserOpen, "WdxBrowserOpen");
Value* WdxBrowserOpen_cf(Value** arg_list, int count) {
    check_arg_count(WdxBrowserOpen, 2, count);

    const std::string root = ArgToUtf8(arg_list[0]);
    if (root.empty())
        return MakeString("No archive path given. Set the Warcraft III path in "
                          "WhiteoutDex Settings first.");

    const int kindArg = ArgToInt(arg_list[1], -1);
    if (kindArg > 2)
        return MakeString("Unknown storage kind (expected -1 auto, 0 CASC, 1 MPQ, 2 folder).");

    // Already walked this exact storage — hand back the live tree rather than
    // re-reading a manifest that has not changed under us.
    if (g_browser.IsOpen() && EqualsNoCase(g_openRoot, root) &&
        (kindArg < 0 || static_cast<StorageKind>(kindArg) == g_browser.Kind()))
        return MakeString("");

    std::string error;
    const bool opened = (kindArg < 0)
                            ? g_browser.OpenAuto(root, &error)
                            : g_browser.Open(root, static_cast<StorageKind>(kindArg), &error);
    if (!opened) {
        g_openRoot.clear();
        if (error.empty())
            error = "Could not open '" + root + "'.";
        mprintf(_M("WhiteoutDex Browser: open failed - %hs\n"), error.c_str());
        return MakeString(error);
    }

    g_openRoot = root;
    mprintf(_M("WhiteoutDex Browser: opened '%hs' (%s)\n"), root.c_str(),
            ProductName(g_browser.Product()));
    return MakeString("");
}

// ============================================================================
// WdxBrowserClose() — drop the tree and release the shared CASC handle.
// ============================================================================
def_visible_primitive(WdxBrowserClose, "WdxBrowserClose");
Value* WdxBrowserClose_cf(Value** /*arg_list*/, int count) {
    check_arg_count(WdxBrowserClose, 0, count);
    g_browser = StorageBrowser{};
    g_openRoot.clear();
#if WHITEOUT_HAS_CASC
    g_extractCasc.reset();
    g_extractRoot.clear();
#endif
    return &ok;
}

def_visible_primitive(WdxBrowserIsOpen, "WdxBrowserIsOpen");
Value* WdxBrowserIsOpen_cf(Value** /*arg_list*/, int count) {
    check_arg_count(WdxBrowserIsOpen, 0, count);
    return g_browser.IsOpen() ? &true_value : &false_value;
}

def_visible_primitive(WdxBrowserRoot, "WdxBrowserRoot");
Value* WdxBrowserRoot_cf(Value** /*arg_list*/, int count) {
    check_arg_count(WdxBrowserRoot, 0, count);
    return MakeString(g_browser.IsOpen() ? g_browser.Root() : std::string{});
}

def_visible_primitive(WdxBrowserKind, "WdxBrowserKind");
Value* WdxBrowserKind_cf(Value** /*arg_list*/, int count) {
    check_arg_count(WdxBrowserKind, 0, count);
    return Integer::intern(g_browser.IsOpen() ? static_cast<int>(g_browser.Kind()) : -1);
}

def_visible_primitive(WdxBrowserProduct, "WdxBrowserProduct");
Value* WdxBrowserProduct_cf(Value** /*arg_list*/, int count) {
    check_arg_count(WdxBrowserProduct, 0, count);
    return new String(
        ProductName(g_browser.IsOpen() ? g_browser.Product() : ProductId::Neutral));
}

// ============================================================================
// Type filter. AvailableTypes is what the open game actually ships, so a host
// showing "Models / Effects" checkboxes can grey out the ones not in it rather
// than offering an Effects filter on a World of Warcraft install.
// ============================================================================
def_visible_primitive(WdxBrowserAvailableTypes, "WdxBrowserAvailableTypes");
Value* WdxBrowserAvailableTypes_cf(Value** /*arg_list*/, int count) {
    check_arg_count(WdxBrowserAvailableTypes, 0, count);
    return Integer::intern(static_cast<int>(g_browser.AvailableTypes()));
}

def_visible_primitive(WdxBrowserSetTypes, "WdxBrowserSetTypes");
Value* WdxBrowserSetTypes_cf(Value** arg_list, int count) {
    check_arg_count(WdxBrowserSetTypes, 1, count);
    const int mask = ArgToInt(arg_list[0], 0);
    g_browser.SetEnabledTypes(static_cast<BrowseType>(static_cast<unsigned>(mask)));
    return &ok;
}

// ============================================================================
// WdxBrowserSetFilter <pattern>
//
// Substring by default; `*`/`?` make a term a glob, commas OR terms, a leading
// '-' excludes. Cheap enough to call on every keystroke — it re-lists only.
// ============================================================================
def_visible_primitive(WdxBrowserSetFilter, "WdxBrowserSetFilter");
Value* WdxBrowserSetFilter_cf(Value** arg_list, int count) {
    check_arg_count(WdxBrowserSetFilter, 1, count);
    g_browser.SetFilter(ArgToUtf8(arg_list[0]));
    return &ok;
}

// ============================================================================
// WdxBrowserFolders <displayPath> / WdxBrowserFiles <displayPath>
//
// Path-addressed rather than driven off a current directory: a rollout keeps
// its own notion of where the user is, and asking by path means a redraw never
// has to navigate the browser back there first.
// ============================================================================
def_visible_primitive(WdxBrowserFolders, "WdxBrowserFolders");
Value* WdxBrowserFolders_cf(Value** arg_list, int count) {
    check_arg_count(WdxBrowserFolders, 1, count);
    const auto listing = g_browser.TreeChildren(ArgToUtf8(arg_list[0]));

    two_typed_value_locals(Array* result, Value* entry);
    vl.result = new Array(0);
    for (const auto& f : listing.folders) {
        vl.entry = MakeString(f);
        vl.result->append(vl.entry);
    }
    return_value(vl.result);
}

def_visible_primitive(WdxBrowserFiles, "WdxBrowserFiles");
Value* WdxBrowserFiles_cf(Value** arg_list, int count) {
    check_arg_count(WdxBrowserFiles, 1, count);
    const auto listing = g_browser.TreeChildren(ArgToUtf8(arg_list[0]));

    two_typed_value_locals(Array* result, Value* entry);
    vl.result = new Array(0);
    for (const auto& f : listing.files) {
        vl.entry = MakeString(f);
        vl.result->append(vl.entry);
    }
    return_value(vl.result);
}

// ============================================================================
// WdxBrowserChildPath <displayPath> <fileName>
//
// The ORIGINAL archive path — "war3.w3mod:_hd.w3mod:units\...\druid.mdx" — which
// is what the renderer's content provider reads verbatim. The CASC-relative
// path a resource should STORE is the display path the caller already has, so
// this is only for hosts that want to hand a string straight to the loader.
// ============================================================================
def_visible_primitive(WdxBrowserChildPath, "WdxBrowserChildPath");
Value* WdxBrowserChildPath_cf(Value** arg_list, int count) {
    check_arg_count(WdxBrowserChildPath, 2, count);
    return MakeString(g_browser.ChildPathAt(ArgToUtf8(arg_list[0]), ArgToUtf8(arg_list[1])));
}

// ============================================================================
// WdxBrowserRelPath <displayPath> <fileName>
//
// The path a resource should STORE: the archive path with the whole Warcraft
// III mod chain dropped, so "war3.w3mod:_hd.w3mod:units\...\druid.mdx" comes
// back as "units\...\druid.mdx". That is what MDX holds and what the content
// provider expects to be handed — CascSource re-applies `war3.w3mod:` /
// `_hd.w3mod:` itself on every read, so a path that still carries them resolves
// to nothing.
//
// Not the same as the display path: the browser keeps `_hd.w3mod` as a visible
// top-level folder (so HD and SD are distinguishable while browsing) and only
// strips `war3.w3mod:`. An MPQ or folder storage has no mod chain, so there
// this is the display path verbatim.
// ============================================================================
def_visible_primitive(WdxBrowserRelPath, "WdxBrowserRelPath");
Value* WdxBrowserRelPath_cf(Value** arg_list, int count) {
    check_arg_count(WdxBrowserRelPath, 2, count);
    std::string p = g_browser.ChildPathAt(ArgToUtf8(arg_list[0]), ArgToUtf8(arg_list[1]));
    // Same rule as io::ToListingPath: the mod chain is everything through the
    // last ':'. Case and separators stay as MDX spells them ('\'), which the
    // storage layer lowercases for itself.
    const auto colon = p.rfind(':');
    if (colon != std::string::npos)
        p.erase(0, colon + 1);
    for (char& c : p)
        if (c == '/')
            c = '\\';
    while (!p.empty() && p.front() == '\\')
        p.erase(0, 1);
    return MakeString(p);
}

// ============================================================================
// WdxBrowserSearch <maxResults> — flat list of display paths matching the
// current filter, for the results view a filter box switches the dialog into.
// ============================================================================
def_visible_primitive(WdxBrowserSearch, "WdxBrowserSearch");
Value* WdxBrowserSearch_cf(Value** arg_list, int count) {
    check_arg_count(WdxBrowserSearch, 1, count);
    int limit = ArgToInt(arg_list[0], 500);
    if (limit <= 0)
        limit = 500;

    std::vector<std::string> matches;
    if (g_browser.IsOpen())
        CollectMatches(std::string{}, limit, matches);

    two_typed_value_locals(Array* result, Value* entry);
    vl.result = new Array(0);
    for (const auto& m : matches) {
        vl.entry = MakeString(m);
        vl.result->append(vl.entry);
    }
    return_value(vl.result);
}

// ============================================================================
// WdxBrowserMatchCount() — total files the filter left standing anywhere in the
// tree, so a dialog can say "showing 500 of 3120" when Search truncates. 0 when
// no filter is set: nothing was pruned, so there is nothing to count.
// ============================================================================
def_visible_primitive(WdxBrowserMatchCount, "WdxBrowserMatchCount");
Value* WdxBrowserMatchCount_cf(Value** /*arg_list*/, int count) {
    check_arg_count(WdxBrowserMatchCount, 0, count);
    return Integer::intern(static_cast<int>(g_browser.TreeMatchCount()));
}

// ============================================================================
// WdxPickAsset <cascRoot> <typeMask> <initialRelPath>
//
// The picker proper: opens WhiteoutFlakes' StorageExplorer — the CASC browser
// with live model thumbnails, as a grid of icons or a tree beside one large
// preview — modally, and returns what the user chose.
//
// Answers an array so a rollout can tell the outcomes apart without parsing a
// sentinel out of a string:
//
//   #(true,  <relative path>, <archive path>)        the user picked one
//   #(false, "", "")                                 the user cancelled
//   #(false, "", "", <message>)                      it could not run at all
//
// Only the third form has a fourth element, so `result.count == 4` is the test
// for "show the user why".
// ============================================================================
def_visible_primitive(WdxPickAsset, "WdxPickAsset");
Value* WdxPickAsset_cf(Value** arg_list, int count) {
    check_arg_count(WdxPickAsset, 3, count);

    const std::string root = ArgToUtf8(arg_list[0]);
    const int mask = ArgToInt(arg_list[1], static_cast<int>(BrowseType::Models));
    const std::string initial = ArgToUtf8(arg_list[2]);

    const std::wstring title = (mask == static_cast<int>(BrowseType::Effects))
                                   ? L"Select a Particle Effect"
                                   : L"Select a Model";
    const whiteout::flakes::AssetPickResult picked = whiteout::flakes::RunAssetPicker(
        title, static_cast<BrowseType>(static_cast<unsigned>(mask)), root, initial);

    two_typed_value_locals(Array* result, Value* entry);
    vl.result = new Array(0);
    vl.entry = picked.accepted ? &true_value : &false_value;
    vl.result->append(vl.entry);
    vl.entry = MakeString(picked.relPath);
    vl.result->append(vl.entry);
    vl.entry = MakeString(picked.archivePath);
    vl.result->append(vl.entry);
    if (!picked.error.empty()) {
        mprintf(_M("WhiteoutDex Picker: %hs\n"), picked.error.c_str());
        vl.entry = MakeString(picked.error);
        vl.result->append(vl.entry);
    }
    return_value(vl.result);
}

// ============================================================================
// WdxExtractAsset <cascRoot> <archivePath> <destFile>
//
// Copy one file out of the archive onto disk. Returns "" on success and the
// reason otherwise, the same shape as WdxBrowserOpen.
//
// This exists because importing is a file-path business: MDLXImporter.dle is
// handed a name by Max's importer machinery and reads it with the CRT, and the
// picker hands back an archive path, which is not a file. So Import from CASC
// extracts to a temp tree that mirrors the archive's folders and imports that.
// Textures are not extracted alongside — the importer resolves those through
// CASC itself, off the W3Path in the settings INI.
//
// `archivePath` may be either spelling: the original ("war3.w3mod:_hd.w3mod:
// units\...\druid.mdx") or the stripped relative path a resource stores. The
// original is exact; a bare path is resolved HD-first, the way the renderer
// would.
// ============================================================================
def_visible_primitive(WdxExtractAsset, "WdxExtractAsset");
Value* WdxExtractAsset_cf(Value** arg_list, int count) {
    check_arg_count(WdxExtractAsset, 3, count);

#if !WHITEOUT_HAS_CASC
    return MakeString("This build of WhiteoutDex has no CASC support.");
#else
    const std::string root = ArgToUtf8(arg_list[0]);
    const std::string archivePath = ArgToUtf8(arg_list[1]);
    const std::wstring dest = ToWide(ArgToUtf8(arg_list[2]));

    if (root.empty())
        return MakeString("No Warcraft III installation given. Set the CASC path in "
                          "WhiteoutDex Settings first.");
    if (archivePath.empty())
        return MakeString("No archive path given.");
    if (dest.empty())
        return MakeString("No destination file given.");

    std::string error;
    const SharedCasc* casc = AcquireForExtract(root, error);
    if (!casc) {
        if (error.empty())
            error = "Could not open the Warcraft III installation at '" + root + "'.";
        mprintf(_M("WhiteoutDex Extract: %hs\n"), error.c_str());
        return MakeString(error);
    }

    std::vector<whiteout::u8> data;
    if (!ReadArchiveFile(*casc, archivePath, data)) {
        error = "'" + archivePath + "' is not in this installation, or its content is "
                                    "encrypted with a key this build does not have.";
        mprintf(_M("WhiteoutDex Extract: %hs\n"), error.c_str());
        return MakeString(error);
    }

    if (!EnsureParentDirs(dest))
        return MakeString("Could not create the folder for the extracted file.");
    if (!WriteWholeFile(dest, data))
        return MakeString("Could not write the extracted file. Check that the temp folder "
                          "is writable and has room.");

    mprintf(_M("WhiteoutDex Extract: %hs (%d bytes)\n"), archivePath.c_str(),
            static_cast<int>(data.size()));
    return MakeString("");
#endif
}
