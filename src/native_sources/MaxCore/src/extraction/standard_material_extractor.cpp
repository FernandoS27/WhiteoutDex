// MaxCore — Standard material extractor implementation
#include "standard_material_extractor.h"

#include <stdmat.h>
#include <bitmap.h>
#include <bmmlib.h>

namespace core {

ir::Material StandardMaterialExtractor::extract(Mtl* mtl, TimeValue t,
                                                 ExportErrorReporter& reporter) {
    ir::Material result;
    if (!mtl) return result;

    const MCHAR* mname = mtl->GetName().data();
    if (mname) {
        std::wstring wname(mname);
        result.name.assign(wname.begin(), wname.end());
    }

    // Check if it's a StdMat2
    StdMat2* stdMat = dynamic_cast<StdMat2*>(mtl);
    if (!stdMat) {
        // Generic fallback: just get the diffuse color via Mtl interface
        ir::MaterialLayer layer;
        layer.blendMode = ir::BlendMode::Opaque;

        // Try to find a diffuse texture
        Texmap* diffTex = mtl->GetSubTexmap(0);
        if (diffTex && diffTex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0x00)) {
            BitmapTex* bmTex = static_cast<BitmapTex*>(diffTex);
            const MCHAR* mapName = bmTex->GetMapName();
            if (mapName && mapName[0]) {
                // The texture path will be resolved by the backend
                // For now, store as-is
            }
        }
        result.layers.push_back(std::move(layer));
        return result;
    }

    // StdMat2: extract basic properties
    ir::MaterialLayer layer;

    // Transparency
    float opacity = stdMat->GetOpacity(t);
    layer.alpha = opacity;
    if (opacity < 1.0f)
        layer.blendMode = ir::BlendMode::Alpha;
    else
        layer.blendMode = ir::BlendMode::Opaque;

    // Two-sided
    layer.twoSided = stdMat->GetTwoSided() ? true : false;

    // Diffuse texmap
    Texmap* diffTex = stdMat->GetSubTexmap(ID_DI);
    if (diffTex && stdMat->SubTexmapOn(ID_DI)) {
        if (diffTex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0x00)) {
            BitmapTex* bmTex = static_cast<BitmapTex*>(diffTex);
            const MCHAR* mapName = bmTex->GetMapName();
            if (mapName && mapName[0]) {
                // Texture reference will be added by the IR model builder
                ir::TextureRef ref;
                ref.slot = ir::TextureSlot::Diffuse;
                ref.textureIndex = -1; // To be resolved
                layer.textureRefs.push_back(ref);
            }
        }
    }

    result.layers.push_back(std::move(layer));
    return result;
}

} // namespace core
