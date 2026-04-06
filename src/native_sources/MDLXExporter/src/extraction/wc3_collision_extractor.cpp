// MDLXExporter — Wc3Collision extractor implementation
#include "wc3_collision_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

namespace mdx_extract {

void extractCollisions(const std::vector<core::SceneNode>& nodes,
                       ir::IRModel& model,
                       core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    for (auto& sn : nodes) {
        bool isSphere = (sn.customTag == "Wc3CollisionSphere");
        bool isBox = (sn.customTag == "Wc3CollisionBox");
        if (!isSphere && !isBox) continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) continue;

        ir::CollisionShape cs;
        cs.nodeIndex = sn.nodeIndex;

        TimeValue t = 0;

        if (isSphere) {
            cs.shape = ir::CollisionShape::Shape::Sphere;
            PBR::readFloatByName(ref, L"radius", t, cs.radius);
        } else {
            cs.shape = ir::CollisionShape::Shape::Box;
            // Box dimensions from custom params or from node bounding box
            float width = 0, length = 0, height = 0;
            PBR::readFloatByName(ref, L"width", t, width);
            PBR::readFloatByName(ref, L"length", t, length);
            PBR::readFloatByName(ref, L"height", t, height);

            // Box defined by two corner vertices (min, max)
            Point3 halfExt(width * 0.5f, length * 0.5f, height * 0.5f);
            cs.vertices.push_back(-halfExt);
            cs.vertices.push_back(halfExt);
        }

        model.collisionShapes.push_back(std::move(cs));
    }
}

} // namespace mdx_extract
