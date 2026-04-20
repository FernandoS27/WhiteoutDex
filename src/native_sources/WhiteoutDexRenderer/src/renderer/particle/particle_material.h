#pragma once
// ============================================================================
// ParticleMaterialDesc — renderer-native port of the fields PE2 reads from
// the engine's CWar3Mat / CGxMatParams. We do NOT mirror the full 272-byte
// layout; only what's actually consumed by the simulation and geometry paths.
//
// See docs/PARTICLEEMITTERS2.md §3.9 for the blend-mode table.
// ============================================================================

#include <cstdint>

namespace WhiteoutDex::particle {

// MDX FilterMode byte values. Matches docs/PARTICLEEMITTERS2.md §3.9 table.
enum class FilterMode : uint8_t {
    Blend       = 0,    // src=SrcAlpha,  dst=InvSrcAlpha,  depthW=off, depthT=on
    Additive    = 1,    // src=SrcAlpha,  dst=One,          depthW=off, depthT=off
    Modulate    = 2,    // src=DstColor,  dst=Zero,         depthW=off, depthT=off
    Modulate2X  = 3,    // src=DstColor,  dst=SrcColor,     depthW=off, depthT=off
    AlphaKey    = 4     // src=One,       dst=Zero,         depthW=on,  depthT=on, alphaTest=on
};

struct ParticleMaterialDesc {
    int         textureId     = -1;    // slot 0 / GxTexSem_Diffuse
    FilterMode  filterMode    = FilterMode::Blend;
    bool        unshaded      = false; // material-layer shading bypass
    bool        unfogged      = false; // equivalent of m_disables bit 2
    int         replaceableId = 0;     // 0=none, 1=team-colour, N=palette slot
};

} // namespace WhiteoutDex::particle
