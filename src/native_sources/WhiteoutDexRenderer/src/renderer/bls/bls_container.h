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
// (reflection tool is built out-of-tree; no hard dependency)

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

// v1.8 per-permute header (preceding each DXBC blob). IDA's 24-byte struct is
// for a newer BLS variant; the shipped Warcraft III Previewd files use an
// 80-byte header where only these fields are populated.
//   +0x14 totalSize : dxbc + 56-byte trailer
//   +0x18 shaderType: 3 for VS, 1 for PS, etc.
//   +0x40 numResources
//   +0x48 codeSize  : DXBC byte count (exact)
//   +0x4c stageFlag : shader stage/profile tag (observed 4 for SM5)
// Everything else in the 80-byte block is zero-padded; resource masks
// (cbMask/srvMask/uavMask/samplerMask) are not populated -- use D3DReflect
// on the DXBC if you need them.
struct PermuteHeader {
    uint32_t unk0[5];      // 0x00..0x13
    uint32_t totalSize;    // 0x14
    uint32_t shaderType;   // 0x18
    uint32_t unk1[9];      // 0x1c..0x3f
    uint32_t numResources; // 0x40
    uint32_t unk2;         // 0x44
    uint32_t codeSize;     // 0x48
    uint32_t stageFlag;    // 0x4c
};
static_assert(sizeof(PermuteHeader) == 80);
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
