#pragma once
// ============================================================================
// BLS shader container parser — HSXG v1.8 (Warcraft III Previewd.exe layout).
//
// A .bls file contains N pre-compiled DXBC permutations of a single HLSL entry
// point ("_main") for a single shader stage. See docs/BLS_Implementation.md
// for the full format reverse-engineering.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace WhiteoutDex::bls {

inline constexpr uint32_t kHsxgMagic   = 0x47585348u; // bytes 'H','S','X','G'
inline constexpr uint32_t kHsxgVersion = 0x00010008u; // v1.8 (the only version shipped in Previewd)
inline constexpr uint32_t kDxbcMagic   = 0x43425844u; // bytes 'D','X','B','C'

#pragma pack(push, 1)
struct BlsHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t permutationOffset;
    uint32_t permutationCount;
    uint32_t dataOffset;
};
static_assert(sizeof(BlsHeader) == 20);

struct PermuteHeader {
    uint32_t inputSignature;
    uint32_t cbMask;
    uint32_t srvMask;
    uint32_t uavMask;
    uint16_t samplerMask;
    uint16_t pad;
    uint32_t codeSize;
};
static_assert(sizeof(PermuteHeader) == 24);
#pragma pack(pop)

struct PermuteView {
    PermuteHeader           header;
    std::span<const uint8_t> dxbc;
};

class BlsContainer {
public:
    // Parses a BLS file. Returns true on success; populates error on failure.
    // The container holds a copy of the file bytes so callers can free their buffer.
    bool Load(std::span<const uint8_t> fileBytes, std::string* error = nullptr);

    bool           IsLoaded()        const { return loaded_; }
    uint32_t       Version()         const { return header_.version; }
    size_t         PermuteCount()    const { return permutes_.size(); }
    PermuteView    Permute(size_t i) const { return permutes_[i]; }

private:
    bool                     loaded_ = false;
    BlsHeader                header_{};
    std::vector<uint8_t>     bytes_;
    std::vector<PermuteView> permutes_;
};

} // namespace WhiteoutDex::bls
