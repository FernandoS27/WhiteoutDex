#include "bls_container.h"

#include <cstring>

namespace WhiteoutDex::bls {

namespace {

void SetError(std::string* error, const char* msg) {
    if (error) *error = msg;
}

} // namespace

bool BlsContainer::Load(std::span<const uint8_t> fileBytes, std::string* error) {
    loaded_ = false;
    permutes_.clear();
    bytes_.clear();

    if (fileBytes.size() < sizeof(BlsHeader)) {
        SetError(error, "BLS file too small for header");
        return false;
    }

    BlsHeader h{};
    std::memcpy(&h, fileBytes.data(), sizeof(BlsHeader));

    if (h.magic != kHsxgMagic) {
        SetError(error, "Bad magic (expected 'HSXG')");
        return false;
    }
    if (h.version != kHsxgVersion) {
        SetError(error, "Unsupported BLS version (expected 1.8)");
        return false;
    }
    if (h.permutationCount == 0) {
        SetError(error, "Permutation count is zero");
        return false;
    }

    const uint64_t permTableEnd =
        static_cast<uint64_t>(h.permutationOffset) + uint64_t{h.permutationCount} * sizeof(uint32_t);
    if (permTableEnd > fileBytes.size()) {
        SetError(error, "Permutation table out of bounds");
        return false;
    }
    if (h.dataOffset > fileBytes.size()) {
        SetError(error, "Data offset out of bounds");
        return false;
    }

    bytes_.assign(fileBytes.begin(), fileBytes.end());
    header_ = h;

    const auto* permTable = reinterpret_cast<const uint32_t*>(bytes_.data() + h.permutationOffset);
    const uint8_t* permuteData    = bytes_.data() + h.dataOffset;
    const size_t   permuteDataLen = bytes_.size() - h.dataOffset;

    permutes_.reserve(h.permutationCount);
    for (uint32_t i = 0; i < h.permutationCount; ++i) {
        const uint32_t off = permTable[i];
        if (static_cast<uint64_t>(off) + sizeof(PermuteHeader) > permuteDataLen) {
            SetError(error, "Permute header out of bounds");
            permutes_.clear();
            bytes_.clear();
            return false;
        }

        PermuteHeader ph{};
        std::memcpy(&ph, permuteData + off, sizeof(PermuteHeader));

        const uint64_t blobStart = static_cast<uint64_t>(off) + sizeof(PermuteHeader);
        const uint64_t blobEnd   = blobStart + ph.codeSize;
        if (blobEnd > permuteDataLen || ph.codeSize < sizeof(uint32_t) * 2) {
            SetError(error, "Permute DXBC blob out of bounds");
            permutes_.clear();
            bytes_.clear();
            return false;
        }

        const uint8_t* blob = permuteData + blobStart;
        uint32_t dxbcMagic = 0;
        std::memcpy(&dxbcMagic, blob, sizeof(uint32_t));
        if (dxbcMagic != kDxbcMagic) {
            SetError(error, "Permute blob is not a DXBC container");
            permutes_.clear();
            bytes_.clear();
            return false;
        }

        permutes_.push_back({ ph, std::span<const uint8_t>(blob, ph.codeSize) });
    }

    loaded_ = true;
    return true;
}

} // namespace WhiteoutDex::bls
