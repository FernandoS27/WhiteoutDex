// MDLXImporter — MDX ClassID definitions for custom node types
// Mirrors the exporter's mdx_class_ids.h (same ClassIDs for the same scripted plugins)
#pragma once

#include <maxtypes.h>

namespace mdx_ids {
    // Scripted plugins
    inline const Class_ID WC3_MATERIAL       (0x4b8e20a3, 0x1f6c3d57);
    inline const Class_ID WC3_BITMAP         (0x3a7c10f1, 0x5e2d4b08);
    inline const Class_ID WC3_ATTACH_POINT   (0x7A1B2C01, 0x3D4E5F01);
    inline const Class_ID WC3_LIGHT          (0x7A1B2C04, 0x3D4E5F04);
    inline const Class_ID WC3_EVENT_V2021    (0x7A1B2C05, 0x3D4E5F05);
    inline const Class_ID WC3_EVENT_V2020    (0x7A1B2C06, 0x3D4E5F06);
    inline const Class_ID WC3_COLLISION_SPH  (0x7A1B2C02, 0x3D4E5F02);
    inline const Class_ID WC3_COLLISION_BOX  (0x7A1B2C03, 0x3D4E5F03);
    inline const Class_ID WC3_VERTEX_MOD     (0x7A1B2C07, 0x3D4E5F07);
    inline const Class_ID BLIZZ_POPCORN      (0x7A1B2C09, 0x3D4E5F09);
    inline const Class_ID BLIZZ_FACEFX       (0x7A1B2C08, 0x3D4E5F08);

    // Native C++ plugins
    inline const Class_ID WC3_PARTICLES1     (0x12E4F5A6, 0x3B7C8D9E);
    inline const Class_ID WC3_PARTICLES2     (0xD9F33BC9, 0x7A0DA37A);
    inline const Class_ID WC3_RIBBON         (0x937AA064, 0x9EFFA3DA);

    // Cross-DLL interface IDs
    constexpr ULONG WC3P1_MODEL_PATH_IID    = 0x7B3C8D01;
    constexpr ULONG WC3P1_MODEL_PREFIX_IID  = 0x7B3C8D02;
} // namespace mdx_ids
