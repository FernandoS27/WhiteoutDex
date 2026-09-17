#pragma once

// Reading the scene's art tier off rootNode. Split from wdx_scene_art_tier.h so
// code that only orders CASC prefixes (the importer's texture resolver) does
// not pull the Max SDK in.

#include "wdx_scene_art_tier.h"

#include <custattrib.h>
#include <icustattribcontainer.h>
#include <inode.h>
#include <iparamb2.h>

namespace wdx::scene {

// The scene's stored tier, found by parameter name rather than by block index
// or shape so the order other CAs were attached in does not matter. Max UI
// thread only.
inline ArtTier ReadSceneArtTier(INode* root) {
    ICustAttribContainer* cac = root ? root->GetCustAttribContainer() : nullptr;
    if (!cac)
        return ArtTier::Auto;
    for (int i = 0; i < cac->GetNumCustAttribs(); ++i) {
        CustAttrib* ca = cac->GetCustAttrib(i);
        if (!ca)
            continue;
        for (int b = 0; b < ca->NumParamBlocks(); ++b) {
            IParamBlock2* pb = ca->GetParamBlock(b);
            ParamBlockDesc2* desc = pb ? pb->GetDesc() : nullptr;
            if (!desc)
                continue;
            const int index = desc->NameToIndex(kArtTierParam);
            if (index < 0)
                continue;
            const ParamID id = desc->IndextoID(index);
            if (desc->GetParamDef(id).type != TYPE_INT)
                continue;
            return ArtTierFromStored(pb->GetInt(id, 0));
        }
    }
    return ArtTier::Auto;
}

} // namespace wdx::scene
