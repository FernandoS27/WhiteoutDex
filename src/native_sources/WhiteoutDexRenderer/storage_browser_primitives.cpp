// ============================================================================
// WhiteoutDex — MaxScript bindings for WhiteoutFlakes' storage browser.
//
// The engine already knows how to walk a CASC install, a set of MPQ archives
// or a loose folder and present only the browsable files — models, particle
// effects, textures — as a folder tree (io::StorageBrowser). MaxScript had no
// way to reach it, so every place that wants a game asset path — the Popcorn
// emitter's .pkfx, a material's .blp, an attachment point's .mdx — fell back to
// getOpenFileName and produced a local disk path, which is not what MDX
// stores. These primitives hand MaxScript the same tree, and the
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
//                                      kind: -1 auto, 0 CASC, 1 one MPQ,
//                                      2 folder, 3 a directory of MPQs
//   WdxBrowserClose()                → releases the tree and the CASC handle
//   WdxBrowserIsOpen()               → true/false
//   WdxBrowserRoot()                 → the root it was opened with
//   WdxBrowserKind()                 → 0 Casc / 1 Mpq / 2 Folder / 3 MpqSet,
//                                      -1 closed
//   WdxBrowserProduct()              → "Warcraft III" / "World of Warcraft" / …
//   WdxBrowserAvailableTypes()       → bitmask the open storage actually holds
//   WdxBrowserSetTypes <mask>        → 1 Models | 2 Effects | 4 M2 | 8 M3 |
//                                      16 Actor | 32 Textures
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
// WhiteoutFlakes' StorageExplorer — the same grid/tree browser the standalone
// Model Explorer shows, with a live thumbnail per cell (a rendered scene for a
// model or effect, the decoded image for a texture) — and returns what was
// chosen. It is what both the Model Browser and the Texture Browser open;
// they differ only in the type mask they ask for.
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

#if WHITEOUT_HAS_MPQ
// The other generation. A pre-Reforged Warcraft III install has no CASC at
// all, so an extraction from one goes straight to the archives.
#include <whiteout/storages/mpq/storage.h>
#endif

#include <filesystem>

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
using whiteout::flakes::io::ClassifyStorage;
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

// An array of #(<label>, <path>) pairs -> the picker's File menu list. Anything
// that is not a two-element array of strings is skipped rather than raising:
// this argument is an offer of convenience, and a rollout that gets it wrong
// should still get its picker.
std::vector<whiteout::flakes::AssetPickerRoot> ArgToRoots(Value* v) {
    std::vector<whiteout::flakes::AssetPickerRoot> out;
    if (!v || v == &undefined || !is_array(v))
        return out;
    Array* outer = static_cast<Array*>(v);
    for (int i = 0; i < outer->size; ++i) {
        Value* row = (*outer)[i];
        if (!row || !is_array(row))
            continue;
        Array* pair = static_cast<Array*>(row);
        if (pair->size < 2)
            continue;
        whiteout::flakes::AssetPickerRoot entry;
        entry.label = ArgToUtf8((*pair)[0]);
        entry.root = ArgToUtf8((*pair)[1]);
        if (!entry.label.empty() && !entry.root.empty())
            out.push_back(std::move(entry));
    }
    return out;
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

#if WHITEOUT_HAS_MPQ

// Read one entry out of a pre-Reforged install's archive set.
//
// Search order is the reverse of the load order the browser inserted them in:
// War3Patch.mpq shipped to replace what War3.mpq holds, so the patch's copy of
// a path is the one the game reads and the one a browse of the merged tree was
// showing. Unknown archives are searched last — they were also inserted last,
// so an override in one still wins over the retail four.
//
// Nothing is cached: an extraction happens once per texture the user picks,
// and holding four archive handles open for the rest of the Max session to
// save a few milliseconds is the wrong trade.
bool ReadFromMpqSet(const std::string& root, const std::string& path,
                    std::vector<whiteout::u8>& out) {
    static const char* kSearchOrder[] = {"war3patch.mpq", "war3xlocal.mpq", "war3local.mpq",
                                         "war3x.mpq",     "war3.mpq",       "deprecated.mpq"};

    std::error_code ec;
    const std::filesystem::path base(root);
    if (!std::filesystem::is_directory(base, ec))
        return false;

    std::vector<std::filesystem::path> known(std::size(kSearchOrder));
    std::vector<std::filesystem::path> extra;
    for (const auto& entry : std::filesystem::directory_iterator(
             base, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec))
            continue;
        std::string name = entry.path().filename().string();
        for (char& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".mpq") != 0)
            continue;
        const auto it = std::find_if(std::begin(kSearchOrder), std::end(kSearchOrder),
                                     [&](const char* k) { return name == k; });
        if (it != std::end(kSearchOrder))
            known[static_cast<std::size_t>(it - std::begin(kSearchOrder))] = entry.path();
        else
            extra.push_back(entry.path());
    }
    std::sort(extra.rbegin(), extra.rend());

    auto tryOne = [&](const std::filesystem::path& p) {
        if (p.empty())
            return false;
        // The one-argument overload: passing a literal null would be ambiguous
        // between the WorkerPool and the error-string overloads.
        auto s = whiteout::storages::mpq::Storage::open(p.string());
        if (!s)
            return false;
        if (auto data = s->readFile(path)) {
            out = std::move(*data);
            return true;
        }
        return false;
    };
    for (const auto& p : known)
        if (tryOne(p))
            return true;
    for (const auto& p : extra)
        if (tryOne(p))
            return true;
    return false;
}

#endif // WHITEOUT_HAS_MPQ

} // namespace

// ============================================================================
// WdxBrowserOpen <root> <kind>
//
// kind: -1 auto-detect, 0 Casc, 1 Mpq (one archive file), 2 Folder, 3 MpqSet
// (every .mpq in a directory, merged - what a pre-Reforged Warcraft III install
// is). Returns "" on success and the failure message otherwise, so a rollout
// can put the reason straight into a label instead of inferring it from a bool.
//
// -1 is the one to pass when "the Warcraft III install" could be either
// generation: it tells CASC and MPQ-set installs apart by what is on disk.
// ============================================================================
def_visible_primitive(WdxBrowserOpen, "WdxBrowserOpen");
Value* WdxBrowserOpen_cf(Value** arg_list, int count) {
    check_arg_count(WdxBrowserOpen, 2, count);

    const std::string root = ArgToUtf8(arg_list[0]);
    if (root.empty())
        return MakeString("No archive path given. Set the Warcraft III path in "
                          "WhiteoutDex Settings first.");

    const int kindArg = ArgToInt(arg_list[1], -1);
    if (kindArg > 3)
        return MakeString("Unknown storage kind (expected -1 auto, 0 CASC, 1 MPQ, "
                          "2 folder, 3 MPQ set).");

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
// WdxPickAsset <cascRoot> <typeMask> <initialRelPath> [<initialFilter>] [<roots>]
//
// The picker proper: opens WhiteoutFlakes' StorageExplorer — the browser with
// live thumbnails, as a grid of icons or a tree beside one large preview —
// modally, and returns what the user chose.
//
// `typeMask` is what the picker browses FOR, and it narrows the manifest walk
// as well as the listing: 1 Models, 2 Effects, 32 Textures. A texture pick
// shows the decoded image in every cell rather than a rendered scene, and its
// preview pane adds the file's dimensions and source format.
//
// `cascRoot` may be either generation of install — a Reforged CASC directory
// or a 1.2x one holding War3.mpq and friends; the picker works out which.
//
// `initialFilter` is optional and seeds the panel's search box (substrings,
// `*`/`?` globs, comma-separated alternatives, `-` to exclude). It is how a
// caller that knows more than the type mask narrows the opening view — a
// material's normal-map button passes "_normal".
//
// `roots` is optional and lists the installs to offer BY NAME in the picker's
// File menu, as #( #(<label>, <path>), ... ) - the caller's own resolution of
// where the game is, which the picker cannot repeat: the two Warcraft III
// generations are configured in two different INIs and the game finder reports
// one path per product. `cascRoot` is still what opens; these are what the user
// can switch to without typing a path.
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
    // The filter and the root list are optional, so every existing 3-argument
    // call keeps working.
    if (count < 3 || count > 5)
        check_arg_count(WdxPickAsset, 3, count);

    const std::string root = ArgToUtf8(arg_list[0]);
    const int mask = ArgToInt(arg_list[1], static_cast<int>(BrowseType::Models));
    const std::string initial = ArgToUtf8(arg_list[2]);
    const std::string filter = count >= 4 ? ArgToUtf8(arg_list[3]) : std::string{};
    const std::vector<whiteout::flakes::AssetPickerRoot> roots =
        count >= 5 ? ArgToRoots(arg_list[4]) : std::vector<whiteout::flakes::AssetPickerRoot>{};

    std::wstring title = L"Select a Model";
    if (mask == static_cast<int>(BrowseType::Effects))
        title = L"Select a Particle Effect";
    else if (mask == static_cast<int>(BrowseType::Textures))
        title = L"Select a Texture";
    const whiteout::flakes::AssetPickResult picked =
        whiteout::flakes::RunAssetPicker(title, static_cast<BrowseType>(static_cast<unsigned>(mask)),
                                         root, initial, filter, roots);

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

#if !WHITEOUT_HAS_CASC && !WHITEOUT_HAS_MPQ
    return MakeString("This build of WhiteoutDex has no archive support.");
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

    // Ask the storage the install actually is first. io::ClassifyStorage is the
    // same rule the picker opened `root` with, so the two agree by
    // construction — which matters for a Battle.net-managed classic install,
    // where opening the CASC its .build.info advertises SUCCEEDS and then has
    // no file to give. Both are still tried: the other one answering is how a
    // misconfigured path stays usable, and the failure that reaches the user is
    // then about the file rather than the storage.
    [[maybe_unused]] const bool archivesFirst = ClassifyStorage(root) == StorageKind::MpqSet;

    std::vector<whiteout::u8> data;
    bool read = false;
    std::string cascError;

#if WHITEOUT_HAS_MPQ
    if (archivesFirst)
        read = ReadFromMpqSet(root, archivePath, data);
#endif
#if WHITEOUT_HAS_CASC
    if (!read) {
        if (const SharedCasc* casc = AcquireForExtract(root, cascError))
            read = ReadArchiveFile(*casc, archivePath, data);
    }
#endif
#if WHITEOUT_HAS_MPQ
    if (!read && !archivesFirst)
        read = ReadFromMpqSet(root, archivePath, data);
#endif

    if (!read) {
        std::string error = "'" + archivePath + "' is not in this installation, or its content "
                                                "is encrypted with a key this build does not "
                                                "have.";
        // Only when nothing could be opened at all: with a storage open, the
        // miss is about the file and the open error is noise.
        if (!cascError.empty())
            mprintf(_M("WhiteoutDex Extract: CASC open - %hs\n"), cascError.c_str());
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
