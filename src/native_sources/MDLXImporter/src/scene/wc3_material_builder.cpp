// MDLXImporter — Wc3MaterialBuilder implementation
#include "wc3_material_builder.h"
#include "../mdlx_class_ids.h"

#include <scene/paramblock_reader.h>
#include <iparamb2.h>
#include <stdmat.h>
#include <bitmap.h>

#include <algorithm>
#include <filesystem>

namespace {

using PBR = core::ParamBlockReader;

// ── Param write helper (same as scene builders) ─────────────

struct PBParam {
    IParamBlock2* pb = nullptr;
    ParamID id = -1;
    explicit operator bool() const { return pb != nullptr; }
};

PBParam findParam(ReferenceTarget* target, const wchar_t* name) {
    if (!target) return {};
    for (int i = 0; i < target->NumRefs(); i++) {
        auto* ref = target->GetReference(i);
        auto* pb = dynamic_cast<IParamBlock2*>(ref);
        if (!pb) continue;
        auto* desc = pb->GetDesc();
        if (!desc) continue;
        for (int j = 0; j < desc->Count(); j++) {
            ParamID pid = desc->IndextoID(j);
            const ParamDef& pd = desc->GetParamDef(pid);
            if (pd.int_name && _wcsicmp(pd.int_name, name) == 0)
                return { pb, pid };
        }
    }
    return {};
}

void pbSetInt(ReferenceTarget* t, const wchar_t* n, int v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetFloat(ReferenceTarget* t, const wchar_t* n, float v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetBool(ReferenceTarget* t, const wchar_t* n, BOOL v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetString(ReferenceTarget* t, const wchar_t* n, const MCHAR* v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}
void pbSetTexmap(ReferenceTarget* t, const wchar_t* n, Texmap* v) {
    auto p = findParam(t, n); if (p) p.pb->SetValue(p.id, 0, v);
}

std::wstring toWstr(const std::string& u8) {
    if (u8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring r(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, r.data(), len);
    return r;
}

// ── Texture path resolution ──────────────────────────────────

/// Resolve an MDX-relative texture path to an actual file on disk.
/// Tries the original path, then alternative extensions (.dds, .tga, .png, .tif).
/// Returns empty string if no file is found.
std::wstring resolveTexturePath(const std::wstring& modelDir, const std::wstring& relPath) {
    namespace fs = std::filesystem;
    if (relPath.empty()) return {};

    // Normalize separators in the relative path
    std::wstring normRel = relPath;
    std::replace(normRel.begin(), normRel.end(), L'/', L'\\');

    // Build the absolute candidate from modelDir + relative path
    fs::path basePath = fs::path(modelDir) / normRel;
    fs::path stem = basePath.parent_path() / basePath.stem();
    std::wstring origExt = basePath.extension().wstring();
    std::transform(origExt.begin(), origExt.end(), origExt.begin(), ::towlower);

    // Extensions to try (in order of preference)
    static const std::wstring altExts[] = { L".dds", L".tga", L".png", L".tif" };

    // 1) Try original path (skip .blp — Max can't load it directly)
    if (origExt != L".blp") {
        std::error_code ec;
        if (fs::exists(basePath, ec))
            return basePath.wstring();
    }

    // 2) Try alternative extensions
    for (const auto& ext : altExts) {
        if (ext == origExt) continue; // already tried
        fs::path candidate = stem;
        candidate += ext;
        std::error_code ec;
        if (fs::exists(candidate, ec))
            return candidate.wstring();
    }

    // 3) Fallback: try just the filename (no subdirectory) in the model directory
    fs::path filenameOnly = basePath.filename();
    fs::path flatStem = fs::path(modelDir) / filenameOnly.stem();

    if (origExt != L".blp") {
        fs::path flatOrig = flatStem;
        flatOrig += origExt;
        std::error_code ec;
        if (fs::exists(flatOrig, ec))
            return flatOrig.wstring();
    }
    for (const auto& ext : altExts) {
        if (ext == origExt) continue;
        fs::path candidate = flatStem;
        candidate += ext;
        std::error_code ec;
        if (fs::exists(candidate, ec))
            return candidate.wstring();
    }

    // Nothing found — return the best-guess absolute path (modelDir + relative)
    // Replace .blp with .dds as the default fallback
    if (origExt == L".blp") {
        fs::path fallback = stem;
        fallback += L".dds";
        return fallback.wstring();
    }
    return basePath.wstring();
}

// ── Wc3Bitmap texture map creation ──────────────────────────

Texmap* createWc3Bitmap(const ir::Texture& irTex, const std::wstring& modelDir, Interface* gi) {
    Texmap* tex = static_cast<Texmap*>(
        gi->CreateInstance(TEXMAP_CLASS_ID, mdx_ids::WC3_BITMAP));

    if (!tex) {
        // Fallback: standard BitmapTex
        BitmapTex* bmpTex = NewDefaultBitmapTex();
        if (!irTex.filePath.empty()) {
            auto wpath = resolveTexturePath(modelDir, toWstr(irTex.filePath));
            bmpTex->SetMapName(wpath.c_str());
        }
        return bmpTex;
    }

    // Set file path on the BitmapTex delegate (found in references)
    if (!irTex.filePath.empty()) {
        auto wpath = resolveTexturePath(modelDir, toWstr(irTex.filePath));
        for (int i = 0; i < tex->NumRefs(); i++) {
            ReferenceTarget* ref = tex->GetReference(i);
            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                static_cast<BitmapTex*>(ref)->SetMapName(wpath.c_str());
                break;
            }
        }
    }

    // Set custom properties
    auto* ref = dynamic_cast<ReferenceTarget*>(tex);
    if (ref) {
        // replaceableId is 1-based in the plugin: 1 = Not Used, 2+ = replaceable types
        pbSetInt(ref, L"replaceableId", irTex.replaceableId + 1);
        pbSetBool(ref, L"wrapU", irTex.wrapU ? TRUE : FALSE);
        pbSetBool(ref, L"wrapV", irTex.wrapV ? TRUE : FALSE);
    }

    return tex;
}

// Map ir::BlendMode to filterMode int (1-based)
int blendModeToFilterMode(ir::BlendMode bm) {
    switch (bm) {
    case ir::BlendMode::Opaque:     return 1;
    case ir::BlendMode::AlphaKey:   return 2;
    case ir::BlendMode::Alpha:      return 3;
    case ir::BlendMode::Additive:   return 4;
    case ir::BlendMode::AddAlpha:   return 5;
    case ir::BlendMode::Modulate:   return 6;
    case ir::BlendMode::Modulate2x: return 7;
    default: return 1;
    }
}

// Map ir::TextureSlot to Wc3Material param name
const wchar_t* textureSlotParamName(ir::TextureSlot slot) {
    switch (slot) {
    case ir::TextureSlot::Diffuse:     return L"diffuseMap";
    case ir::TextureSlot::Normal:      return L"normalMap";
    case ir::TextureSlot::ORM:         return L"ormMap";
    case ir::TextureSlot::Emissive:    return L"emissiveMap";
    case ir::TextureSlot::Environment: return L"environmentMap";
    default: return nullptr;
    }
}

} // anonymous namespace

namespace mdx_scene {

std::vector<Mtl*> Wc3MaterialBuilder::buildMaterials(
    const ir::IRModel& irModel, bool importTextures, const std::wstring& modelDir,
    Interface* gi, core::ExportErrorReporter& reporter)
{
    std::vector<Mtl*> materials;
    materials.reserve(irModel.materials.size());

    for (const auto& irMat : irModel.materials) {
        materials.push_back(buildWc3Material(irMat, irModel, importTextures, modelDir, gi, reporter));
    }

    return materials;
}

Mtl* Wc3MaterialBuilder::buildWc3Material(
    const ir::Material& irMat, const ir::IRModel& irModel, bool importTextures,
    const std::wstring& modelDir, Interface* gi, core::ExportErrorReporter& reporter)
{
    // Try to create Wc3Material via ClassID; fall back to StdMat if not available.
    Mtl* mtl = static_cast<Mtl*>(
        gi->CreateInstance(MATERIAL_CLASS_ID, mdx_ids::WC3_MATERIAL));

    if (mtl) {
        if (!irMat.name.empty()) {
            MSTR name;
            name.printf(_T("%hs"), irMat.name.c_str());
            mtl->SetName(name);
        }

        auto* ref = dynamic_cast<ReferenceTarget*>(mtl);
        if (ref && !irMat.layers.empty()) {
            const auto& layer = irMat.layers[0];

            // Filter mode (1-based)
            pbSetInt(ref, L"filterMode", blendModeToFilterMode(layer.blendMode));

            // Opacity (0-100)
            pbSetFloat(ref, L"opacity", layer.alpha * 100.0f);

            // Shading flags
            pbSetBool(ref, L"twoSided", layer.twoSided ? TRUE : FALSE);
            pbSetBool(ref, L"unshaded", layer.unshaded ? TRUE : FALSE);
            pbSetBool(ref, L"unfogged", layer.unfogged ? TRUE : FALSE);
            pbSetBool(ref, L"noDepthTest", layer.noDepthTest ? TRUE : FALSE);
            pbSetBool(ref, L"noDepthSet", layer.noDepthWrite ? TRUE : FALSE);

            // Priority plane
            pbSetInt(ref, L"priorityPlane", irMat.priorityPlane);

            // Coord ID
            pbSetInt(ref, L"coordId", layer.uvSetIndex);

            // Material flags
            pbSetBool(ref, L"constantColor", (irMat.flags & 0x01) ? TRUE : FALSE);
            pbSetBool(ref, L"fullResolution", (irMat.flags & 0x20) ? TRUE : FALSE);

            // Sort order: 0x08 = nearToFar(2), 0x10 = farToNear(3), else 1
            int sortOrder = 1;
            if (irMat.flags & 0x08) sortOrder = 2;
            else if (irMat.flags & 0x10) sortOrder = 3;
            pbSetInt(ref, L"sortOrder", sortOrder);

            // Shader path (Reforged)
            if (!irMat.shaderName.empty()) {
                auto wshader = toWstr(irMat.shaderName);
                pbSetString(ref, L"shaderPath", wshader.c_str());
            }

            // Texture maps
            if (importTextures) {
                Texmap* diffuseTexmap = nullptr;

                for (const auto& texRef : layer.textureRefs) {
                    if (texRef.textureIndex < 0 ||
                        texRef.textureIndex >= static_cast<int32_t>(irModel.textures.size()))
                        continue;

                    const wchar_t* paramName = textureSlotParamName(texRef.slot);
                    if (!paramName) continue;

                    Texmap* texmap = createWc3Bitmap(irModel.textures[texRef.textureIndex], modelDir, gi);
                    if (texmap) {
                        // Set sphere env map flag on the texture
                        if (layer.sphereEnvMap && texRef.slot == ir::TextureSlot::Environment) {
                            auto* texRef2 = dynamic_cast<ReferenceTarget*>(texmap);
                            if (texRef2) pbSetBool(texRef2, L"sphereEnvMap", TRUE);
                        }
                        pbSetTexmap(ref, paramName, texmap);

                        if (texRef.slot == ir::TextureSlot::Diffuse)
                            diffuseTexmap = texmap;
                    }
                }

                // Enable viewport display of the diffuse texture
                if (diffuseTexmap)
                    diffuseTexmap->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED);
            }

            // Reforged PBR parameters
            pbSetFloat(ref, L"emissiveGain", layer.emissiveGain);
            pbSetFloat(ref, L"fresnelOpacity", layer.fresnelOpacity);
            pbSetFloat(ref, L"fresnelTeamCol", layer.fresnelTeamColor);
            pbSetFloat(ref, L"fresnelR", layer.fresnelColor.x);
            pbSetFloat(ref, L"fresnelG", layer.fresnelColor.y);
            pbSetFloat(ref, L"fresnelB", layer.fresnelColor.z);
        }

        return mtl;
    }

    // Fallback: create StdMat2
    auto* stdMtl = NewDefaultStdMat();
    if (!irMat.name.empty()) {
        MSTR name;
        name.printf(_T("%hs"), irMat.name.c_str());
        stdMtl->SetName(name);
    }

    if (!irMat.layers.empty()) {
        const auto& layer = irMat.layers[0];
        stdMtl->SetTwoSided(layer.twoSided ? TRUE : FALSE);
        stdMtl->SetOpacity(layer.alpha * 100.0f, 0);

        if (importTextures && !layer.textureRefs.empty()) {
            int32_t texIdx = layer.textureRefs[0].textureIndex;
            if (texIdx >= 0 && texIdx < static_cast<int32_t>(irModel.textures.size())) {
                const auto& irTex = irModel.textures[texIdx];
                if (!irTex.filePath.empty()) {
                    BitmapTex* bmpTex = NewDefaultBitmapTex();
                    auto wpath = resolveTexturePath(modelDir, toWstr(irTex.filePath));
                    bmpTex->SetMapName(wpath.c_str());
                    stdMtl->SetSubTexmap(ID_DI, bmpTex);
                    stdMtl->EnableMap(ID_DI, TRUE);
                    bmpTex->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED);
                }
            }
        }
    }

    return stdMtl;
}

} // namespace mdx_scene
