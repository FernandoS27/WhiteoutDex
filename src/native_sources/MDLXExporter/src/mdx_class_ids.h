// MDLXExporter — MDX ClassID definitions for custom node types
#pragma once

#include <maxtypes.h>

namespace mdx_ids {
    // Scripted plugins (Wdx_* family from WhiteoutDex MaxScript plugins).
    // All IDs taken verbatim from the corresponding .ms files — do not change
    // without coordinating with the plugin scripts.
    inline const Class_ID WC3_MATERIAL       (0x4b8e20a3, 0x1f6c3d57);
    inline const Class_ID WC3_ATTACH_POINT   (0x7A1B2C01, 0x3D4E5F01);
    inline const Class_ID WC3_COLLISION_SPH  (0x7A1B2C02, 0x3D4E5F02);
    inline const Class_ID WC3_COLLISION_BOX  (0x7A1B2C03, 0x3D4E5F03);
    inline const Class_ID WC3_LIGHT          (0x7A1B2C04, 0x3D4E5F04);
    inline const Class_ID WC3_EVENT_V2021    (0x7A1B2C05, 0x3D4E5F05);
    inline const Class_ID WC3_EVENT_V2020    (0x7A1B2C06, 0x3D4E5F06);
    inline const Class_ID WC3_VERTEX_MOD     (0x7A1B2C07, 0x3D4E5F07);
    inline const Class_ID WC3_FACEFX         (0x7A1B2C08, 0x3D4E5F08);
    inline const Class_ID WC3_POPCORN        (0x7A1B2C09, 0x3D4E5F09);

    // Legacy: Wc3Bitmap was superseded by the native Bitmaptexture in the
    // v2+ plugin generation. The importer was updated accordingly and
    // Wc3Material now stores replaceableId on the material itself.
    // Kept here for backward compatibility with scenes authored before
    // the migration — the material extractor detects this classID and
    // falls back to reading replaceableId / wrapU / wrapV / sphereEnvMap
    // from the legacy plugin's paramblock.
    inline const Class_ID WC3_BITMAP         (0x3a7c10f1, 0x5e2d4b08);

    // Native C++ plugins
    inline const Class_ID WC3_PARTICLES1     (0x12E4F5A6, 0x3B7C8D9E);
    inline const Class_ID WC3_PARTICLES2     (0xD9F33BC9, 0x7A0DA37A);
    inline const Class_ID WC3_RIBBON         (0x937AA064, 0x9EFFA3DA);
} // namespace mdx_ids
