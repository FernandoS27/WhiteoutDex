// MDLXExporter — Wc3Collision extractor implementation
//
// Extracts Wdx_CollisionSphere and Wdx_CollisionBox helper nodes into the IR
// model. Coordinate swizzle to MDX space is applied by the Model Builder's
// mdx_transform::position() on every vertex written, so we hand it Max-space
// coordinates directly.
//
// Sphere convention (Max):
//   - Helper placed at the sphere center.
//   - radius = scripted-plugin `radius` float.
//   - Export: v1 = node.pos (Max space), radius = radius.
//
// Box convention (Max):
//   - Helper placed at (X-center, Y-center, Z-bottom) of the box.
//   - width = X extent, length = Y extent, height = Z extent.
//   - Export: compute Max-space AABB, hand both corners as-is to the builder
//     which swizzles them; downstream writer takes component-wise min/max
//     to ensure v1 ≤ v2 in MDX space.
//
// Note on flags: collision nodes must have bit 0x2000 (CollisionShape) set
// in the node flags. This is applied by buildNode() in the model builder,
// not here — we just set ir::CollisionShape fields.

#include "wc3_collision_extractor.h"
#include "../mdx_class_ids.h"
#include <scene/paramblock_reader.h>

#include <max.h>
#include <object.h>
#include <inode.h>
#include <algorithm>
#include <cstdio>
#include <cstdarg>

// Debug logging — writes to the same file as other extractors
namespace {

void cl_log(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    char tempPath[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tempPath);
    if (n == 0 || n > MAX_PATH) return;

    char logPath[MAX_PATH];
    snprintf(logPath, sizeof(logPath), "%smdlx_material_debug.log", tempPath);

    FILE* f = nullptr;
    if (fopen_s(&f, logPath, "a") == 0 && f) {
        fputs(buf, f);
        fclose(f);
    }
}

} // anon

namespace mdx_extract {

void extractCollisions(const std::vector<core::SceneNode>& nodes,
                       ir::IRModel& model,
                       core::ExportErrorReporter& reporter)
{
    using PBR = core::ParamBlockReader;

    cl_log("\n==== Collision Extractor ====\n");

    int sphereCount = 0, boxCount = 0;

    for (auto& sn : nodes) {
        // Detect by customTag (set by scene classifier from Max class name).
        // The plugin class is "Wdx_Wc3CollisionSphere" / "Wdx_Wc3CollisionBox"
        // but the customTag typically strips the "Wdx_" prefix.
        bool isSphere = (sn.customTag == "Wc3CollisionSphere" ||
                         sn.customTag == "Wdx_Wc3CollisionSphere" ||
                         sn.customTag == "CollisionSphere");
        bool isBox    = (sn.customTag == "Wc3CollisionBox" ||
                         sn.customTag == "Wdx_Wc3CollisionBox" ||
                         sn.customTag == "CollisionBox");

        if (!isSphere && !isBox) continue;
        if (!sn.maxNode) continue;

        auto* obj = sn.maxNode->GetObjectRef();
        auto* ref = dynamic_cast<ReferenceTarget*>(obj);
        if (!ref) {
            cl_log("  skip: no ReferenceTarget on '%ls'\n",
                   sn.maxNode->GetName() ? sn.maxNode->GetName() : L"?");
            continue;
        }

        ir::CollisionShape cs;
        cs.nodeIndex = sn.nodeIndex;

        TimeValue t = 0;

        // Get node position in world space (helper nodes aren't parented to
        // bones via TM inheritance in a way that requires local-space conversion;
        // the importer placed them at the correct absolute location).
        Matrix3 tm = sn.maxNode->GetNodeTM(t);
        Point3 pos = tm.GetTrans();

        const MCHAR* nm = sn.maxNode->GetName();
        cl_log("  %s '%ls' pos=(%.2f, %.2f, %.2f)\n",
               isSphere ? "Sphere" : "Box",
               nm ? nm : L"?", pos.x, pos.y, pos.z);

        if (isSphere) {
            cs.shape = ir::CollisionShape::Shape::Sphere;

            // Radius from the scripted plugin parameter
            PBR::readFloatByName(ref, L"radius", t, cs.radius);

            // Sphere center = node position in Max space.
            // Builder's mdx_transform::position swizzles to MDX space.
            cs.vertices.push_back(pos);

            cl_log("    radius=%.3f  vertex=(%.3f, %.3f, %.3f)\n",
                   cs.radius, pos.x, pos.y, pos.z);
            sphereCount++;
        }
        else {
            cs.shape = ir::CollisionShape::Shape::Box;

            float width = 0, length = 0, height = 0;
            PBR::readFloatByName(ref, L"width",  t, width);
            PBR::readFloatByName(ref, L"length", t, length);
            PBR::readFloatByName(ref, L"height", t, height);

            // Max-space AABB per spec:
            //   min = (pos.x - w/2, pos.y - l/2, pos.z)
            //   max = (pos.x + w/2, pos.y + l/2, pos.z + h)
            Point3 bboxMin(pos.x - width  * 0.5f,
                           pos.y - length * 0.5f,
                           pos.z);
            Point3 bboxMax(pos.x + width  * 0.5f,
                           pos.y + length * 0.5f,
                           pos.z + height);

            // Hand both corners to the builder. mdx_transform::position
            // swizzles each to MDX space; downstream we take component-wise
            // min/max so v1 ≤ v2 in MDX space (swizzle flips Y sign, so
            // Max-min can become MDX-max on Y).
            cs.vertices.push_back(bboxMin);
            cs.vertices.push_back(bboxMax);

            cl_log("    wlh=(%.2f, %.2f, %.2f)  maxBBox min=(%.2f,%.2f,%.2f) max=(%.2f,%.2f,%.2f)\n",
                   width, length, height,
                   bboxMin.x, bboxMin.y, bboxMin.z,
                   bboxMax.x, bboxMax.y, bboxMax.z);
            boxCount++;
        }

        model.collisionShapes.push_back(std::move(cs));
    }

    cl_log("Total collision shapes: %d (spheres=%d, boxes=%d)\n",
           sphereCount + boxCount, sphereCount, boxCount);
}

} // namespace mdx_extract
