// MDLXExporter — Wc3Popcorn (CornEmitter) extractor implementation
#include "wc3_popcorn_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

namespace mdx_extract {

// Popcorn data is stored in ir::ParticleEmitter with variant=3.
// The model builder converts variant=3 to mdx::CornEmitter.

void extractPopcorn(const std::vector<core::SceneNode>& nodes,
                    ir::IRModel& model,
                    core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        if (sn.customTag != "Wc3Popcorn") continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        TimeValue t = 0;
        ir::ParticleEmitter pe;
        pe.nodeIndex = sn.nodeIndex;
        pe.variant = 3; // CornEmitter marker

        PBR::readFloatByName(ref, L"LifeSpan", t, pe.lifespan);
        PBR::readFloatByName(ref, L"EmissionRate", t, pe.emissionRate);
        PBR::readFloatByName(ref, L"Speed", t, pe.speed);
        // ReplaceableId / team color used to be on the Popcorn plugin but
        // the engine ignores both — the plugin no longer exposes them, and
        // we don't read them on export.

        // Render flags: pack the Wc3Popcorn bool params into pe.flags using
        // the same bit positions the disassembler uses. The model builder
        // OR-merges these into corn.node.flags on the way back out to MDX.
        //   0x8000  Unshaded
        //   0x20000 PopcornUnfogged
        //   0x40000 PopcornScaling
        BOOL b = FALSE;
        if (PBR::readBoolByName(ref, L"flagUnshaded", t, b) && b) pe.flags |= 0x8000u;
        if (PBR::readBoolByName(ref, L"flagUnfogged", t, b) && b) pe.flags |= 0x20000u;
        if (PBR::readBoolByName(ref, L"flagScaling",  t, b) && b) pe.flags |= 0x40000u;

        // Helper: wstring → UTF-8.
        auto wideToUtf8 = [](const std::wstring& w) -> std::string {
            if (w.empty()) return {};
            int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
            if (len <= 1) return {};
            std::string u8(static_cast<size_t>(len - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u8.data(), len,
                                nullptr, nullptr);
            return u8;
        };

        // Effect file path
        std::wstring pathW;
        if (PBR::readStringByName(ref, L"popcornPath", t, pathW) && !pathW.empty())
            pe.modelPath = wideToUtf8(pathW);

        // PopcornFX anim-visibility gate. The scripted plugin's `rawFlags`
        // carries the raw comma-separated guide string (e.g. "Stand=on,
        // Death=off"); round-trip it verbatim so the renderer can drive the
        // emitter the same way the engine does.
        std::wstring guideW;
        if (PBR::readStringByName(ref, L"rawFlags", t, guideW) && !guideW.empty())
            pe.animVisibilityGuide = wideToUtf8(guideW);

        // Base color (stored in segmentColors[0] + segmentAlpha[0] for variant==3)
        Color baseColor(1, 1, 1);
        PBR::readColorByName(ref, L"baseColor", t, baseColor);
        pe.segmentColors[0] = baseColor;
        float alpha = 1.0f;
        PBR::readFloatByName(ref, L"alpha", t, alpha);
        pe.segmentAlpha[0] = alpha;

        model.particleEmitters.push_back(std::move(pe));
    }
}

} // namespace mdx_extract
