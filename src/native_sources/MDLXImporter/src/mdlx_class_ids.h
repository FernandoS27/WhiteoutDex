// MDLXImporter — MDX ClassID definitions for custom node types
// Mirrors the exporter's mdx_class_ids.h (same ClassIDs for the same scripted plugins)
#pragma once

#include <maxtypes.h>

namespace mdx_ids {
    // Scripted plugins
    inline const Class_ID WC3_MATERIAL       (0x4b8e20a3, 0x1f6c3d57);
    inline const Class_ID WC3_BITMAP         (0x3a7c10f1, 0x5e2d4b08);
    inline const Class_ID WC3_ATTACH_POINT   (0x1136ac20, 0x6f9cfeb7);
    inline const Class_ID WC3_LIGHT          (0x456E2573, 0x2A456757);
    inline const Class_ID WC3_EVENT_V2021    (0x189dc89e, 0x2e9f652d);
    inline const Class_ID WC3_EVENT_V2020    (0x956e6a9b, 0x87f39a9e);
    inline const Class_ID WC3_COLLISION_SPH  (0x0b945647, 0x61c1bb70);
    inline const Class_ID WC3_COLLISION_BOX  (0x51e76dc0, 0x471f81a5);
    inline const Class_ID WC3_VERTEX_MOD     (0x234d68a2, 0x7204a141);
    inline const Class_ID BLIZZ_POPCORN      (0x6a17b48c, 0x291672fe);
    inline const Class_ID BLIZZ_FACEFX       (0x1f2f6643, 0x253d2ca1);

    // Native C++ plugins
    inline const Class_ID WC3_PARTICLES1     (0x12E4F5A6, 0x3B7C8D9E);
    inline const Class_ID WC3_PARTICLES2     (0xD9F33BC9, 0x7A0DA37A);
    inline const Class_ID WC3_RIBBON         (0x937AA064, 0x9EFFA3DA);

    // Cross-DLL interface IDs
    constexpr ULONG WC3P1_MODEL_PATH_IID    = 0x7B3C8D01;
    constexpr ULONG WC3P1_MODEL_PREFIX_IID  = 0x7B3C8D02;
} // namespace mdx_ids
