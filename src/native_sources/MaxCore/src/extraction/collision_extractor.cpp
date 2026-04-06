// MaxCore — Collision shape extractor implementation
#include "collision_extractor.h"
#include "../scene/paramblock_reader.h"

namespace core {

ir::CollisionShape CollisionExtractor::extract(INode* node,
                                                ir::CollisionShape::Shape shapeType,
                                                TimeValue t,
                                                ExportErrorReporter& reporter) {
    ir::CollisionShape shape;
    shape.shape = shapeType;

    // Node index will be set by the caller
    Matrix3 nodeTM = node->GetNodeTM(t);
    Point3 pos = nodeTM.GetTrans();

    if (shapeType == ir::CollisionShape::Shape::Box) {
        // Box collision: read width/height/depth from the object's param block
        // or compute from the node's bounding box
        ObjectState os = node->EvalWorldState(t);
        if (os.obj) {
            Box3 bbox;
            os.obj->GetDeformBBox(t, bbox);
            Point3 minP = bbox.pmin;
            Point3 maxP = bbox.pmax;
            // Store two corner vertices in world space
            shape.vertices.push_back(pos + minP);
            shape.vertices.push_back(pos + maxP);
        }
    } else if (shapeType == ir::CollisionShape::Shape::Sphere) {
        // Sphere collision: compute radius from bounding sphere
        ObjectState os = node->EvalWorldState(t);
        if (os.obj) {
            Box3 bbox;
            os.obj->GetDeformBBox(t, bbox);
            Point3 extent = bbox.pmax - bbox.pmin;
            shape.radius = Length(extent) * 0.5f;
        }
        shape.vertices.push_back(pos);
    } else if (shapeType == ir::CollisionShape::Shape::Cylinder) {
        ObjectState os = node->EvalWorldState(t);
        if (os.obj) {
            Box3 bbox;
            os.obj->GetDeformBBox(t, bbox);
            Point3 extent = bbox.pmax - bbox.pmin;
            shape.radius = std::max(extent.x, extent.y) * 0.5f;
            shape.vertices.push_back(pos + Point3(0.0f, 0.0f, bbox.pmin.z));
            shape.vertices.push_back(pos + Point3(0.0f, 0.0f, bbox.pmax.z));
        }
    } else if (shapeType == ir::CollisionShape::Shape::Plane) {
        shape.vertices.push_back(pos);
    }

    return shape;
}

} // namespace core
