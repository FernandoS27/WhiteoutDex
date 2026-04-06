// MaxCore — Tangent computation utilities
#pragma once

#include <max.h>

namespace core {
namespace tangent {

    // TCB → Hermite tangent conversion for Point3
    void tcbToHermite(float tension, float continuity, float bias,
                      const Point3& prev, const Point3& curr, const Point3& next,
                      TimeValue prevTime, TimeValue currTime, TimeValue nextTime,
                      Point3& inTangent, Point3& outTangent);

    // TCB → Hermite tangent conversion for Quat (via log-map)
    void tcbToHermiteQuat(float tension, float continuity, float bias,
                          const Quat& prev, const Quat& curr, const Quat& next,
                          TimeValue prevTime, TimeValue currTime, TimeValue nextTime,
                          Quat& inTangent, Quat& outTangent);

    // Bezier → Hermite tangent conversion for Point3
    void bezierToHermite(const Point3& currInCtrl, const Point3& currVal,
                         const Point3& currOutCtrl,
                         TimeValue prevTime, TimeValue currTime, TimeValue nextTime,
                         Point3& hermiteIn, Point3& hermiteOut);

    // Quaternion shortest-path consistency
    Quat ensureShortestPath(const Quat& prev, const Quat& curr);

    // Compute central-difference tangent for Point3
    Point3 centralDifference(const Point3& prev, const Point3& next,
                              TimeValue prevTime, TimeValue nextTime);

} // namespace tangent
} // namespace core
