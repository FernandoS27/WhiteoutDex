// MDLXExporter — Wc3Material extractor implementation
#include "wc3_material_extractor.h"
#include "../mdx_class_ids.h"

#include <scene/paramblock_reader.h>
#include <max.h>
#include <stdmat.h>
#include <bitmap.h>
#include <inode.h>

#include <string>
#include <unordered_map>

namespace mdx_extract {

namespace {

std::string wstrToUtf8(const wchar_t* wstr) {
    if (!wstr || !wstr[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), len, nullptr, nullptr);
    return result;
}

std::string extractBitmapPath(Texmap* tex) {
    if (!tex) return {};
    // Standard Bitmaptexture
    if (tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
        auto* bmt = static_cast<BitmapTex*>(tex);
        return wstrToUtf8(bmt->GetMapName());
    }
    // Wc3Bitmap — find BitmapTex delegate in references
    if (tex->ClassID() == mdx_ids::WC3_BITMAP) {
        for (int i = 0; i < tex->NumRefs(); i++) {
            ReferenceTarget* ref = tex->GetReference(i);
            if (ref && ref->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)) {
                auto* bmt = static_cast<BitmapTex*>(ref);
                return wstrToUtf8(bmt->GetMapName());
            }
        }
    }
    return {};
}

int32_t findOrAddTexture(ir::IRModel& model, const std::string& path,
                         int32_t replaceableId, bool wrapU, bool wrapV)
{
    // Look for an existing match
    for (size_t i = 0; i < model.textures.size(); i++) {
        auto& t = model.textures[i];
        if (t.filePath == path && t.replaceableId == replaceableId &&
            t.wrapU == wrapU && t.wrapV == wrapV)
            return static_cast<int32_t>(i);
    }
    ir::Texture tex;
    tex.filePath = path;
    tex.replaceableId = replaceableId;
    tex.wrapU = wrapU;
    tex.wrapV = wrapV;
    model.textures.push_back(std::move(tex));
    return static_cast<int32_t>(model.textures.size() - 1);
}

// Extract properties from a Wc3Bitmap texture plugin
struct BitmapProperties {
    int replaceableId = 0;
    bool wrapU = false;
    bool wrapV = false;
    bool sphereEnvMap = false;
    std::string prefixPath;
};

BitmapProperties extractBitmapProperties(Texmap* tex) {
    BitmapProperties props;
    if (!tex || tex->ClassID() != mdx_ids::WC3_BITMAP) return props;

    auto* ref = dynamic_cast<ReferenceTarget*>(tex);
    if (!ref) return props;

    using PBR = core::ParamBlockReader;
    TimeValue t = 0;

    int replId = 1; // 1-based: 1 = Not Used
    PBR::readIntByName(ref, L"replaceableId", t, replId);
    props.replaceableId = std::max(0, replId - 1);

    BOOL flag = FALSE;
    if (PBR::readBoolByName(ref, L"wrapU", t, flag) && flag) props.wrapU = true;
    flag = FALSE;
    if (PBR::readBoolByName(ref, L"wrapV", t, flag) && flag) props.wrapV = true;
    flag = FALSE;
    if (PBR::readBoolByName(ref, L"sphereEnvMap", t, flag) && flag) props.sphereEnvMap = true;

    std::wstring prefix;
    if (PBR::readStringByName(ref, L"prefixPath", t, prefix) && !prefix.empty())
        props.prefixPath = wstrToUtf8(prefix.c_str());

    return props;
}

// Material-level properties extracted from a single Wc3Material sub-material.
// These get merged when building a composite material.
struct MaterialLevelProps {
    int priorityPlane = 0;
    uint32_t flags = 0;
    std::string shaderName;
};

ir::MaterialLayer extractWc3Layer(ReferenceTarget* mtlRef, ir::IRModel& model,
                                  MaterialLevelProps& matProps)
{
    using PBR = core::ParamBlockReader;
    TimeValue t = 0;

    ir::MaterialLayer layer;

    // Filter mode (1-based: 1=None..7=Modulate2x)
    int filterMode = 1;
    PBR::readIntByName(mtlRef, L"filterMode", t, filterMode);
    switch (filterMode) {
    case 1: layer.blendMode = ir::BlendMode::None; break;
    case 2: layer.blendMode = ir::BlendMode::Transparent; break;
    case 3: layer.blendMode = ir::BlendMode::Blend; break;
    case 4: layer.blendMode = ir::BlendMode::Additive; break;
    case 5: layer.blendMode = ir::BlendMode::AddAlpha; break;
    case 6: layer.blendMode = ir::BlendMode::Modulate; break;
    case 7: layer.blendMode = ir::BlendMode::Modulate2x; break;
    default: layer.blendMode = ir::BlendMode::None; break;
    }

    // Opacity (0–100 → 0.0–1.0)
    float opacity = 100.0f;
    PBR::readFloatByName(mtlRef, L"opacity", t, opacity);
    layer.alpha = opacity / 100.0f;

    // Flags
    BOOL flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"twoSided", t, flagVal) && flagVal)
        layer.twoSided = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"unshaded", t, flagVal) && flagVal)
        layer.unshaded = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"unfogged", t, flagVal) && flagVal)
        layer.unfogged = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"noDepthTest", t, flagVal) && flagVal)
        layer.noDepthTest = true;
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"noDepthSet", t, flagVal) && flagVal)
        layer.noDepthWrite = true;

    // Priority plane
    int priority = 0;
    PBR::readIntByName(mtlRef, L"priorityPlane", t, priority);
    matProps.priorityPlane = priority;

    // Coord ID (-1 = default → 0)
    int coordId = -1;
    PBR::readIntByName(mtlRef, L"coordId", t, coordId);
    layer.uvSetIndex = (coordId >= 0) ? coordId : 0;

    // Material flags — ConstantColor
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"constantColor", t, flagVal) && flagVal)
        matProps.flags |= 0x01;

    // Material flags — FullResolution
    flagVal = FALSE;
    if (PBR::readBoolByName(mtlRef, L"fullResolution", t, flagVal) && flagVal)
        matProps.flags |= 0x20;

    // Material flags — SortOrder (1=unused, 2=nearToFar, 3=farToNear)
    int sortOrder = 1;
    PBR::readIntByName(mtlRef, L"sortOrder", t, sortOrder);
    if (sortOrder == 2) matProps.flags |= 0x08;
    else if (sortOrder == 3) matProps.flags |= 0x10;

    // Shader path (Reforged)
    std::wstring shaderPath;
    if (PBR::readStringByName(mtlRef, L"shaderPath", t, shaderPath) && !shaderPath.empty())
        matProps.shaderName = wstrToUtf8(shaderPath.c_str());

    // --- Diffuse texture ---
    Texmap* texmap = nullptr;
    if (PBR::readTexmapByName(mtlRef, L"diffuseMap", texmap) && texmap) {
        std::string texPath = extractBitmapPath(texmap);
        BitmapProperties bmpProps = extractBitmapProperties(texmap);

        // Prepend prefix path
        if (!bmpProps.prefixPath.empty() && !texPath.empty())
            texPath = bmpProps.prefixPath + texPath;

        // SphereEnvMap lives on the bitmap
        if (bmpProps.sphereEnvMap) layer.sphereEnvMap = true;

        ir::TextureRef texRef;
        texRef.textureIndex = findOrAddTexture(model, texPath,
            bmpProps.replaceableId, bmpProps.wrapU, bmpProps.wrapV);
        texRef.slot = ir::TextureSlot::Diffuse;
        layer.textureRefs.push_back(texRef);
    }

    // --- Reforged PBR maps ---
    Texmap* normalMap = nullptr;
    if (PBR::readTexmapByName(mtlRef, L"normalMap", normalMap) && normalMap) {
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, extractBitmapPath(normalMap), 0, true, true);
        ref.slot = ir::TextureSlot::Normal;
        layer.textureRefs.push_back(ref);
    }

    Texmap* ormMap = nullptr;
    if (PBR::readTexmapByName(mtlRef, L"ormMap", ormMap) && ormMap) {
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, extractBitmapPath(ormMap), 0, true, true);
        ref.slot = ir::TextureSlot::ORM;
        layer.textureRefs.push_back(ref);
    }

    Texmap* emissiveMap = nullptr;
    if (PBR::readTexmapByName(mtlRef, L"emissiveMap", emissiveMap) && emissiveMap) {
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, extractBitmapPath(emissiveMap), 0, true, true);
        ref.slot = ir::TextureSlot::Emissive;
        layer.textureRefs.push_back(ref);
    }

    Texmap* envMap = nullptr;
    if (PBR::readTexmapByName(mtlRef, L"environmentMap", envMap) && envMap) {
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, extractBitmapPath(envMap), 0, true, true);
        ref.slot = ir::TextureSlot::Environment;
        layer.textureRefs.push_back(ref);
    }

    // Fresnel properties
    PBR::readFloatByName(mtlRef, L"emissiveGain", t, layer.emissiveGain);
    PBR::readFloatByName(mtlRef, L"fresnelOpacity", t, layer.fresnelOpacity);
    PBR::readFloatByName(mtlRef, L"fresnelTeamCol", t, layer.fresnelTeamColor);

    // Fresnel color
    float fR = 1.0f, fG = 1.0f, fB = 1.0f;
    PBR::readFloatByName(mtlRef, L"fresnelR", t, fR);
    PBR::readFloatByName(mtlRef, L"fresnelG", t, fG);
    PBR::readFloatByName(mtlRef, L"fresnelB", t, fB);
    layer.fresnelColor = Point3(fR, fG, fB);

    return layer;
}

void extractWc3Material(ReferenceTarget* mtlRef, ir::IRModel& model,
                        core::ExportErrorReporter& /*reporter*/)
{
    MaterialLevelProps matProps;
    ir::MaterialLayer layer = extractWc3Layer(mtlRef, model, matProps);

    ir::Material mat;
    mat.priorityPlane = matProps.priorityPlane;
    mat.flags = matProps.flags;
    mat.shaderName = std::move(matProps.shaderName);
    mat.layers.push_back(std::move(layer));
    model.materials.push_back(std::move(mat));
}

bool isWc3Composite(Mtl* mtl) {
    if (!mtl) return false;
    int n = mtl->NumSubMtls();
    if (n == 0) return false;
    bool hasWc3 = false;
    for (int i = 0; i < n; i++) {
        Mtl* sub = mtl->GetSubMtl(i);
        if (!sub) continue;
        if (sub->ClassID() != mdx_ids::WC3_MATERIAL)
            return false;
        hasWc3 = true;
    }
    return hasWc3;
}

void extractCompositeMaterial(Mtl* mtl, ir::IRModel& model,
                              core::ExportErrorReporter& /*reporter*/)
{
    ir::Material mat;
    MaterialLevelProps matProps;

    int n = mtl->NumSubMtls();
    for (int i = 0; i < n; i++) {
        Mtl* sub = mtl->GetSubMtl(i);
        if (!sub || sub->ClassID() != mdx_ids::WC3_MATERIAL) continue;
        auto* ref = dynamic_cast<ReferenceTarget*>(sub);
        if (!ref) continue;

        MaterialLevelProps layerProps;
        ir::MaterialLayer layer = extractWc3Layer(ref, model, layerProps);
        mat.layers.push_back(std::move(layer));

        // Use properties from the first layer for the material
        if (mat.layers.size() == 1) {
            matProps = std::move(layerProps);
        }
    }

    mat.priorityPlane = matProps.priorityPlane;
    mat.flags = matProps.flags;
    mat.shaderName = std::move(matProps.shaderName);
    model.materials.push_back(std::move(mat));
}

void extractStdMaterial(Mtl* mtl, ir::IRModel& model) {
    if (!mtl) return;
    auto* stdMat = dynamic_cast<StdMat2*>(mtl);
    if (!stdMat) return;

    ir::Material mat;
    ir::MaterialLayer layer;
    layer.blendMode = ir::BlendMode::None;
    layer.alpha = stdMat->GetOpacity(0);
    layer.twoSided = stdMat->GetTwoSided() != 0;

    Texmap* diffTex = stdMat->GetSubTexmap(ID_DI);
    if (diffTex) {
        std::string path = extractBitmapPath(diffTex);
        ir::TextureRef ref;
        ref.textureIndex = findOrAddTexture(model, path, 0, true, true);
        ref.slot = ir::TextureSlot::Diffuse;
        layer.textureRefs.push_back(ref);
    }

    mat.layers.push_back(std::move(layer));
    model.materials.push_back(std::move(mat));
}

} // anonymous namespace

MaterialMap extractMaterials(const std::vector<core::SceneNode>& nodes,
                             ir::IRModel& model,
                             core::ExportErrorReporter& reporter)
{
    std::unordered_map<Mtl*, int32_t> mtlToIndex;

    for (auto& sn : nodes) {
        if (sn.category != core::NodeCategory::Mesh) continue;
        if (!sn.maxNode) continue;

        Mtl* mtl = sn.maxNode->GetMtl();
        if (!mtl || mtlToIndex.count(mtl)) continue;

        int32_t idx = static_cast<int32_t>(model.materials.size());
        auto* ref = dynamic_cast<ReferenceTarget*>(mtl);
        if (ref && mtl->ClassID() == mdx_ids::WC3_MATERIAL) {
            extractWc3Material(ref, model, reporter);
        } else if (isWc3Composite(mtl)) {
            extractCompositeMaterial(mtl, model, reporter);
        } else {
            extractStdMaterial(mtl, model);
        }
        mtlToIndex[mtl] = idx;
    }

    // Assign material indices to meshes
    for (auto& mesh : model.meshes) {
        if (mesh.nodeIndex < 0 || mesh.nodeIndex >= static_cast<int32_t>(model.nodes.size()))
            continue;
        auto* maxNode = model.nodes[mesh.nodeIndex].maxNode;
        if (!maxNode) continue;
        Mtl* mtl = maxNode->GetMtl();
        if (!mtl) continue;

        auto it = mtlToIndex.find(mtl);
        if (it != mtlToIndex.end())
            mesh.materialIndex = it->second;
    }

    return mtlToIndex;
}

} // namespace mdx_extract
