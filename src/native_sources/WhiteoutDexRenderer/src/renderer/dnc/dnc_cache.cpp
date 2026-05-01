// ============================================================================
// DncCache — implementation
// ============================================================================

#include "dnc_cache.h"

#include <whiteout/models/mdx/parser.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Forward-declare the MDL→Model converter — its header
// (src/whiteout/models/mdx/mdl_converter.h) is private to WhiteoutLib
// and we don't want to leak that internal path into the renderer's
// include search. The symbol is exported by whiteout_lib (used by
// WhiteoutLib's own test suite via the same forward-declared form).
namespace whiteout {
namespace mdx {
Model convertMdlToModel(std::string_view source, std::vector<std::string>& issues);
} // namespace mdx
} // namespace whiteout

namespace WhiteoutDex::dnc {

namespace {
using whiteout::mdx::Model;
} // namespace

DncCache::DncCache(IContentProvider* contentProvider)
    : contentProvider_(contentProvider) {}

DncCache::~DncCache() {
    ReleaseAll();
}

std::string DncCache::NormalizeKey(const std::string& path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '\\') c = '/';
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool DncCache::IsTextPath(const std::string& key) {
    if (key.size() < 4) return false;
    // Already lower-cased by NormalizeKey, so a literal ".mdl" suffix
    // is the right check.
    return key.compare(key.size() - 4, 4, ".mdl") == 0;
}

DncAsset* DncCache::Acquire(const std::string& path) {
    if (!contentProvider_) return nullptr;

    const std::string key = NormalizeKey(path);

    if (auto it = entries_.find(key); it != entries_.end()) {
        it->second->refs += 1;
        return it->second.get();
    }

    auto bytes = contentProvider_->ReadFile(path);
    if (!bytes || bytes->empty()) {
        std::fprintf(stderr, "[dnc] Acquire failed (file missing/empty): %s\n", path.c_str());
        return nullptr;
    }

    auto entry = std::make_unique<DncAsset>();
    entry->key = key;

    // Parse: detect text vs binary via the file's MAGIC, not the
    // extension. The engine asks for `*.mdl` paths but the content
    // chain can transparently substitute the `.mdx` (binary) sibling
    // — CASC's altExt fallback in particular returns the `.mdx` for a
    // `.mdl` request when only the binary form is shipped. Trusting
    // the extension would route a binary blob through the MDL parser
    // and fail at "expected '{' but got ''" on the first non-ASCII
    // byte. The MDX magic is the four-byte tag "MDLX" (FourCC); MDL
    // text always starts with the keyword "Version" or whitespace.
    const bool isBinaryMdx =
        bytes->size() >= 4 &&
        (*bytes)[0] == 'M' && (*bytes)[1] == 'D' &&
        (*bytes)[2] == 'L' && (*bytes)[3] == 'X';
    try {
        if (!isBinaryMdx) {
            std::string_view src(reinterpret_cast<const char*>(bytes->data()), bytes->size());
            std::vector<std::string> issues;
            entry->model = whiteout::mdx::convertMdlToModel(src, issues);
            if (!issues.empty()) {
                std::fprintf(stderr, "[dnc] '%s' parsed with %zu issue(s); first: %s\n",
                             path.c_str(), issues.size(), issues.front().c_str());
            }
        } else {
            whiteout::mdx::Parser parser;
            std::span<const whiteout::u8> view(
                reinterpret_cast<const whiteout::u8*>(bytes->data()), bytes->size());
            entry->model = parser.parse(view);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[dnc] '%s' parse threw: %s\n", path.c_str(), e.what());
        return nullptr;
    }

    // Sequence 0 is the day cycle (preview.exe DisplayInitialize @
    // 0x7ff609a7f93e calls SpriteSetAnimationByIndex(v6, 0, 0) on the
    // DNC sprite). Models with no sequences fall through with
    // animLength=0; Sample() then returns valid=false.
    if (!entry->model.sequences.empty()) {
        entry->seqStartMs = static_cast<int>(entry->model.sequences[0].intervalStart);
        entry->seqEndMs   = static_cast<int>(entry->model.sequences[0].intervalEnd);
    }

    entry->hierarchy.Build(entry->model);

    // Find the first MDX Light node in the hierarchy. Shipping DNC
    // MDLs author exactly one; if a custom asset ships more, only the
    // first contributes (mirrors the engine's "single ambient slot"
    // semantics — you can stack additional ones via the authored MDX
    // light pipeline if you want, that path runs separately).
    if (!entry->model.lights.empty()) {
        const auto& L = entry->model.lights[0];
        entry->lightNodeIdx = entry->hierarchy.ObjectIdToNodeIndex(static_cast<int>(L.node.objectId));
    }

    entry->refs = 1;
    DncAsset* raw = entry.get();
    entries_.emplace(key, std::move(entry));
    return raw;
}

void DncCache::Release(DncAsset* asset) {
    if (!asset) return;
    if (asset->refs == 0) return;
    asset->refs -= 1;
    if (asset->refs > 0) return;

    auto it = entries_.find(asset->key);
    if (it == entries_.end()) return;
    entries_.erase(it);
}

void DncCache::ReleaseAll() {
    entries_.clear();
}

} // namespace WhiteoutDex::dnc
