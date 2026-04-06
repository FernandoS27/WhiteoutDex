// MDLXExporter — Register MDX-specific node types with the classifier
#pragma once

#include <scene/node_classifier.h>
#include "mdx_class_ids.h"

inline void registerMdxNodeTypes(core::NodeClassifier& c) {
    c.registerClassID(mdx_ids::WC3_MATERIAL,       "Wc3Material");
    c.registerClassID(mdx_ids::WC3_ATTACH_POINT,   "Wc3AttachPoint");
    c.registerClassID(mdx_ids::WC3_LIGHT,          "Wc3Light");
    c.registerClassID(mdx_ids::WC3_EVENT_V2021,    "Wc3Event");
    c.registerClassID(mdx_ids::WC3_EVENT_V2020,    "Wc3Event");
    c.registerClassID(mdx_ids::WC3_COLLISION_SPH,  "Wc3CollisionSphere");
    c.registerClassID(mdx_ids::WC3_COLLISION_BOX,  "Wc3CollisionBox");
    c.registerClassID(mdx_ids::WC3_VERTEX_MOD,     "Wc3VertexMod");
    c.registerClassID(mdx_ids::BLIZZ_POPCORN,      "BlizzPopcorn");
    c.registerClassID(mdx_ids::BLIZZ_FACEFX,       "BlizzFaceFX");
    c.registerClassID(mdx_ids::WC3_PARTICLES1,     "Wc3Particles1");
    c.registerClassID(mdx_ids::WC3_PARTICLES2,     "Wc3Particles2");
    c.registerClassID(mdx_ids::WC3_RIBBON,         "Wc3Ribbon");
}
