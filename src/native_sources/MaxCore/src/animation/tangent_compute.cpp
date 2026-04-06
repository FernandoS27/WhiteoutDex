// MaxCore — Tangent computation utilities implementation
#include "tangent_compute.h"
#include "../util/max_helpers.h"
#include <cmath>

namespace core {
namespace tangent {

void tcbToHermite(float tension, float continuity, float bias,
                  const Point3& prev, const Point3& curr, const Point3& next,
                  TimeValue prevTime, TimeValue currTime, TimeValue nextTime,
                  Point3& inTangent, Point3& outTangent) {
    // TCB formula (Kochanek-Bartels):
    // d_in  = 0.5 * (1-T) * [ (1+C)(1-B)(curr-prev) + (1-C)(1+B)(next-curr) ]
    // d_out = 0.5 * (1-T) * [ (1-C)(1-B)(curr-prev) + (1+C)(1+B)(next-curr) ]
    // Adjusted by time intervals for non-uniform spacing.

    float dt1 = static_cast<float>(currTime - prevTime);
    float dt2 = static_cast<float>(nextTime - currTime);
    if (dt1 <= 0.0f) dt1 = 1.0f;
    if (dt2 <= 0.0f) dt2 = 1.0f;
    float dtSum = dt1 + dt2;

    Point3 d0 = (curr - prev);
    Point3 d1 = (next - curr);

    float adj1 = 2.0f * dt2 / dtSum;
    float adj2 = 2.0f * dt1 / dtSum;

    float halfT = 0.5f * (1.0f - tension);

    inTangent = halfT * ((1.0f + continuity) * (1.0f - bias) * d0 * adj1 +
                          (1.0f - continuity) * (1.0f + bias) * d1 * adj1);
    outTangent = halfT * ((1.0f - continuity) * (1.0f - bias) * d0 * adj2 +
                           (1.0f + continuity) * (1.0f + bias) * d1 * adj2);
}

void tcbToHermiteQuat(float tension, float continuity, float bias,
                      const Quat& prev, const Quat& curr, const Quat& next,
                      TimeValue prevTime, TimeValue currTime, TimeValue nextTime,
                      Quat& inTangent, Quat& outTangent) {
    // For quaternions, use a simplified approach:
    // Compute tangent quaternions via slerp-based differentiation
    // qIn  = slerp(slerp(prev, curr, 1+tin),  slerp(curr, next, tin),  0.5)
    // qOut = slerp(slerp(prev, curr, 1+tout), slerp(curr, next, tout), 0.5)

    float dt1 = static_cast<float>(currTime - prevTime);
    float dt2 = static_cast<float>(nextTime - currTime);
    if (dt1 <= 0.0f) dt1 = 1.0f;
    if (dt2 <= 0.0f) dt2 = 1.0f;

    float halfT = 0.5f * (1.0f - tension);
    float tin = halfT * (1.0f + continuity) * (1.0f + bias);
    float tout = halfT * (1.0f - continuity) * (1.0f - bias);

    // Ensure shortest path
    Quat p = ensureShortestPath(curr, prev);
    Quat n = ensureShortestPath(curr, next);

    Quat slerp_pc = Slerp(p, curr, 1.0f + tin);
    Quat slerp_cn = Slerp(curr, n, tin);
    inTangent = Slerp(slerp_pc, slerp_cn, 0.5f);

    slerp_pc = Slerp(p, curr, 1.0f + tout);
    slerp_cn = Slerp(curr, n, tout);
    outTangent = Slerp(slerp_pc, slerp_cn, 0.5f);
}

void bezierToHermite(const Point3& currInCtrl, const Point3& currVal,
                     const Point3& currOutCtrl,
                     TimeValue prevTime, TimeValue currTime, TimeValue nextTime,
                     Point3& hermiteIn, Point3& hermiteOut) {
    // Bezier control points → Hermite tangents
    // Hermite tangent = 3 * (control_point - knot_point) / dt
    float dtIn = static_cast<float>(currTime - prevTime);
    float dtOut = static_cast<float>(nextTime - currTime);
    if (dtIn <= 0.0f) dtIn = 1.0f;
    if (dtOut <= 0.0f) dtOut = 1.0f;

    hermiteIn = 3.0f * (currVal - currInCtrl) / dtIn;
    hermiteOut = 3.0f * (currOutCtrl - currVal) / dtOut;
}

Quat ensureShortestPath(const Quat& prev, const Quat& curr) {
    if (core::quatDot(prev, curr) < 0.0f)
        return -curr;
    return curr;
}

Point3 centralDifference(const Point3& prev, const Point3& next,
                          TimeValue prevTime, TimeValue nextTime) {
    float dt = static_cast<float>(nextTime - prevTime);
    if (dt <= 0.0f) return Point3(0, 0, 0);
    return (next - prev) / dt;
}

} // namespace tangent
} // namespace core
