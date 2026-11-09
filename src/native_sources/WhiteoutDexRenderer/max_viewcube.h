#pragma once

// ============================================================================
// A view cube that behaves like 3ds Max's (Max Help, "ViewCube"):
//  - 26 click targets: 6 faces, 12 edges, 8 corners; the one under the mouse
//    lights up, and a click turns the view to it in a short animation;
//  - dragging the cube orbits the view;
//  - a compass ring under it (N/E/S/W, N = world +Y) turns the view about
//    the vertical when dragged;
//  - a Home button (house) top left resets the camera;
//  - looking straight at a face, four arrows lead to the neighbouring faces
//    and two roll arrows turn the view 90 degrees about its own axis;
//  - the compass letters are clickable (the view turns to look from there);
//  - right-click: Home, set the current view as Home, orthographic;
//  - drawn half transparent until the mouse is over it.
// Drawn with ImDrawList only; the renderer knows nothing about it. The axis
// conventions are those of WhiteoutFlakes' tools/common/imgui_viewcube.h,
// which this replaces in the Max plug-in: cube space is the renderer's native
// space, and the camera looks from (-d.x, d.y, d.z) at a cube direction d.
// ============================================================================

#include "localization.h"
#include "renderer/camera.h"
#include "whiteout/flakes/util/coordinate_system.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace whiteout::flakes::wdxui {

using whiteout::Vector3f;

namespace vc {

inline f32 Dot(const Vector3f& a, const Vector3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline ImU32 WithAlpha(ImU32 c, f32 a) {
    const u32 alpha = static_cast<u32>(((c >> IM_COL32_A_SHIFT) & 0xFF) * a);
    return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
}

inline bool PointInQuad(const ImVec2& p, const ImVec2 q[4]) {
    bool pos = false, neg = false;
    for (i32 i = 0; i < 4; ++i) {
        const ImVec2& a = q[i];
        const ImVec2& b = q[(i + 1) & 3];
        const f32 s = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        pos |= (s > 0.0f);
        neg |= (s < 0.0f);
    }
    return !(pos && neg);
}

inline bool PointInTri(const ImVec2& p, const ImVec2& a, const ImVec2& b, const ImVec2& c) {
    auto s = [](const ImVec2& p0, const ImVec2& p1, const ImVec2& p2) {
        return (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
    };
    const f32 d1 = s(p, a, b), d2 = s(p, b, c), d3 = s(p, c, a);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

inline f32 WrapAngle(f32 a) {
    constexpr f32 kPi = 3.14159265358979f;
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

/// A cube direction with components in {-1, 0, 1}: a face, edge or corner.
struct Zone {
    i32 x = 0, y = 0, z = 0;
    bool valid() const { return x || y || z; }
    bool operator==(const Zone& o) const { return x == o.x && y == o.y && z == o.z; }
};

inline Zone ToZone(const Vector3f& v) {
    auto c = [](f32 f) { return f > 0.5f ? 1 : (f < -0.5f ? -1 : 0); };
    return {c(v.x), c(v.y), c(v.z)};
}

/// Camera yaw/pitch that look at the cube from direction `d` (cube space).
inline void ViewAngles(const Vector3f& d, f32 frontYaw, f32& yaw, f32& pitch) {
    const f32 len = std::sqrt(Dot(d, d));
    const Vector3f e{-d.x / len, d.y / len, d.z / len};
    if (std::abs(e.z) > 0.999f) {
        // Straight down or up: Max keeps the front at the bottom of the view.
        yaw = frontYaw;
        pitch = e.z > 0 ? 1.55f : -1.55f;
        return;
    }
    yaw = std::atan2(e.y, e.x);
    pitch = std::asin(std::clamp(e.z, -1.0f, 1.0f));
}

/// One code point from UTF-8 text; returns the bytes it used (1 for a broken
/// sequence, which then reads as U+FFFD).
inline i32 Utf8Next(const char* s, unsigned int& cp) {
    const unsigned char c = static_cast<unsigned char>(s[0]);
    i32 n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (n == 0) { cp = 0xFFFD; return 1; }
    cp = n == 1 ? c : c & (0xFF >> (n + 1));
    for (i32 i = 1; i < n; ++i) {
        const unsigned char d = static_cast<unsigned char>(s[i]);
        if ((d & 0xC0) != 0x80) { cp = 0xFFFD; return i; }
        cp = (cp << 6) | (d & 0x3F);
    }
    return n;
}

/// Draws one word mapped onto the cube's plane through `project`, so it turns
/// and skews with the face. UTF-8 text; glyphs the font lacks are skipped.
template <class Project>
void FaceLabel(ImDrawList* dl, const char* text, const Vector3f& faceC, const Vector3f& rt,
               const Vector3f& up, Project project, ImU32 col) {
    ImFontBaked* baked = ImGui::GetFontBaked();
    if (!baked || !text || !*text)
        return;
    const ImTextureRef tex = ImGui::GetIO().Fonts->TexRef;
    f32 w = 0.0f, minY = 1e9f, maxY = -1e9f;
    for (const char* s = text; *s;) {
        unsigned int cp = 0;
        s += Utf8Next(s, cp);
        const ImFontGlyph* g = baked->FindGlyph(static_cast<ImWchar>(cp));
        if (!g)
            continue;
        w += g->AdvanceX;
        minY = std::min(minY, g->Y0);
        maxY = std::max(maxY, g->Y1);
    }
    if (w <= 0.0f || maxY <= minY)
        return;
    const f32 pxToUnit = std::min(0.66f / w, 0.30f / (maxY - minY));
    const f32 midY = (minY + maxY) * 0.5f;
    f32 penX = -w * 0.5f;
    for (const char* s = text; *s;) {
        unsigned int cp = 0;
        s += Utf8Next(s, cp);
        const ImFontGlyph* g = baked->FindGlyph(static_cast<ImWchar>(cp));
        if (!g)
            continue;
        auto corner = [&](f32 px, f32 py) {
            const f32 u = (penX + px) * pxToUnit;
            const f32 v = (midY - py) * pxToUnit;
            return project(faceC + rt * u + up * v);
        };
        dl->AddImageQuad(tex, corner(g->X0, g->Y0), corner(g->X1, g->Y0), corner(g->X1, g->Y1),
                         corner(g->X0, g->Y1), ImVec2(g->U0, g->V0), ImVec2(g->U1, g->V0),
                         ImVec2(g->U1, g->V1), ImVec2(g->U0, g->V1), col);
        penX += g->AdvanceX;
    }
}

/// What the cube remembers between frames: the turn animation and a drag.
struct State {
    bool animating = false;
    f32 fromYaw = 0, fromPitch = 0, toYaw = 0, toPitch = 0, fromRoll = 0, toRoll = 0;
    // Home also moves the camera: distance and orbit target go along, and the
    // default Home ends in Camera::Reset() for everything a reset sets.
    bool moveCamera = false, resetAtEnd = false;
    f32 fromDistance = 0, toDistance = 0;
    Vector3f fromTarget{0, 0, 0}, toTarget{0, 0, 0};
    f64 duration = 0.28;
    bool hasHome = false;          // "Set Current View as Home"
    f32 homeYaw = 0, homePitch = 0, homeRoll = 0, homeDistance = 0;
    Vector3f homeTarget{0, 0, 0};
    f64 startTime = 0;
    enum class Drag { None, Cube, Ring } drag = Drag::None;
    bool dragMoved = false;
    ImVec2 pressPos{0, 0};
    // What was under the mouse when the button went down: a click acts on
    // that, as Max's cube does, so a hand that twitches still hits it.
    enum class Target { None, Home, Roll, Letter, Arrow, Zone } target = Target::None;
    i32 targetIndex = -1;
    Vector3f targetDir{0, 0, 0};
    f32 ringAngle = 0;          // mouse angle about the ring centre at the last frame
};

inline State& GetState() {
    static State s;
    return s;
}

} // namespace vc

/// Draws the cube in the top-right corner of the 3D view `areaMin..areaMax`.
inline void DrawMaxViewCube(renderer::Camera& camera, const ImVec2& areaMin, const ImVec2& areaMax) {
    using renderer::CoordinateSystem;
    using renderer::CoordSpace;
    using vc::Dot;
    vc::State& st = vc::GetState();

    auto toNative = [](const Vector3f& m) {
        return CoordinateSystem::ConvertDirection(CoordSpace::Max, CoordinateSystem::Default(), m);
    };
    // Face normals in Max space, in imgui_viewcube.h's order and convention
    // (Front, Back, Left, Right, Top, Bottom).
    static const std::array<Vector3f, 6> kFaceN = [&] {
        const Vector3f m[6] = {{0, 1, 0}, {0, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
        std::array<Vector3f, 6> a{};
        for (i32 i = 0; i < 6; ++i) a[i] = toNative(m[i]);
        return a;
    }();
    static const std::array<Vector3f, 6> kLabelUp = [&] {
        const Vector3f m[6] = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}};
        std::array<Vector3f, 6> a{};
        for (i32 i = 0; i < 6; ++i) a[i] = toNative(m[i]);
        return a;
    }();
    static constexpr const char* kFaceKey[6] = {"wdx.viewcube.front", "wdx.viewcube.back",
                                                "wdx.viewcube.left",  "wdx.viewcube.right",
                                                "wdx.viewcube.top",   "wdx.viewcube.bottom"};

    // ---- Layout ----
    constexpr f32 kCube = 78.0f;  // projected cube size
    constexpr f32 kPad = 26.0f;   // room for the arrows and the ring
    constexpr f32 kHomeSz = 18.0f;
    const f32 boxW = kCube + 2.0f * kPad;
    const f32 boxH = kCube + 2.0f * kPad + 10.0f;
    const ImVec2 boxPos(areaMax.x - boxW - 6.0f, areaMin.y + 6.0f);

    ImGui::SetNextWindowPos(boxPos);
    ImGui::SetNextWindowSize(ImVec2(boxW, boxH));
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (!ImGui::Begin("##wdx_viewcube", nullptr, kFlags)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }
    ImGui::InvisibleButton("##wdx_viewcube_hit", ImVec2(boxW, boxH));
    const bool hovered = ImGui::IsItemHovered();
    const bool pressed = ImGui::IsItemActivated();
    const bool held = ImGui::IsItemActive();
    const bool released = ImGui::IsItemDeactivated();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- Animation: turn (and for Home, move) towards the chosen view ----
    if (st.animating) {
        const f32 t = static_cast<f32>(std::min(1.0, (ImGui::GetTime() - st.startTime) / st.duration));
        const f32 e = t * t * (3.0f - 2.0f * t);
        camera.SetYaw(st.fromYaw + vc::WrapAngle(st.toYaw - st.fromYaw) * e);
        camera.SetPitch(st.fromPitch + (st.toPitch - st.fromPitch) * e);
        camera.SetRoll(st.fromRoll + vc::WrapAngle(st.toRoll - st.fromRoll) * e);
        if (st.moveCamera) {
            // Zoom at a steady rate (geometric), as a dolly reads to the eye.
            const f32 d = (st.fromDistance > 0.0f && st.toDistance > 0.0f)
                              ? st.fromDistance * std::pow(st.toDistance / st.fromDistance, e)
                              : st.fromDistance + (st.toDistance - st.fromDistance) * e;
            camera.SetDistance(d);
            camera.SetTarget(st.fromTarget + (st.toTarget - st.fromTarget) * e);
        }
        if (t >= 1.0f) {
            st.animating = false;
            if (st.resetAtEnd)
                camera.Reset();
            st.moveCamera = st.resetAtEnd = false;
        }
    }
    // A preset view is upright: turning to one also takes the roll back to 0.
    auto turnTo = [&](f32 yaw, f32 pitch, f32 roll = 0.0f) {
        st.animating = true;
        st.moveCamera = st.resetAtEnd = false;
        st.duration = 0.28;
        st.fromYaw = camera.GetYaw();
        st.fromPitch = camera.GetPitch();
        st.fromRoll = camera.GetRoll();
        st.toYaw = yaw;
        st.toPitch = pitch;
        st.toRoll = roll;
        st.startTime = ImGui::GetTime();
    };
    // Home flies there, as in 3ds Max: angles, distance and target together.
    // Without a Home of the user's the end is the camera's reset view.
    auto goHome = [&] {
        if (camera.GetMode() != renderer::Camera::Mode::Orbital) {
            st.animating = false;
            if (!st.hasHome) camera.Reset();
            return;
        }
        const f32 yaw = st.hasHome ? st.homeYaw : renderer::Camera::kDefaultYaw;
        const f32 pitch = st.hasHome ? st.homePitch : renderer::Camera::kDefaultPitch;
        const f32 roll = st.hasHome ? st.homeRoll : 0.0f;
        turnTo(yaw, pitch, roll);
        st.moveCamera = true;
        st.resetAtEnd = !st.hasHome;
        st.duration = 0.5;
        st.fromDistance = camera.GetDistance();
        st.toDistance = st.hasHome ? st.homeDistance : renderer::Camera::kDefaultDistance;
        st.fromTarget = camera.GetTarget();
        st.toTarget = st.hasHome ? st.homeTarget : Vector3f{0.0f, 0.0f, 50.0f};  // Camera::Reset's target
    };
    f32 frontYaw = 0, frontPitch = 0;
    vc::ViewAngles(kFaceN[0], 0.0f, frontYaw, frontPitch);

    const f32 alpha = (hovered || held || st.animating) ? 1.0f : 0.55f;

    // ---- Projection (orthographic, as imgui_viewcube.h) ----
    const ImVec2 center(boxPos.x + boxW * 0.5f, boxPos.y + kPad + kCube * 0.5f);
    const f32 scale = kCube * 0.5f / 0.92f;
    const f32 cp = std::cos(camera.GetPitch()), sp = std::sin(camera.GetPitch());
    const f32 cy = -std::cos(camera.GetYaw()), sy = std::sin(camera.GetYaw());
    const Vector3f eye{cp * cy, cp * sy, sp};
    const Vector3f fwd{-eye.x, -eye.y, -eye.z};
    Vector3f right = cross(Vector3f{0, 0, 1}, fwd);
    if (right.length_squared() < 1e-5f) right = Vector3f{1, 0, 0};
    right = right.normalized();
    Vector3f vUp = cross(fwd, right).normalized();
    {
        // the cube turns with the camera's roll (Camera::ComputeUpFromAngles
        // turns the up towards the right by the roll)
        const f32 r = camera.GetRoll(), c = std::cos(r), sn = std::sin(r);
        const Vector3f rr = right * c - vUp * sn, uu = vUp * c + right * sn;
        right = rr;
        vUp = uu;
    }
    auto project = [&](const Vector3f& p) {
        return ImVec2(center.x + Dot(p, right) * scale, center.y - Dot(p, vUp) * scale);
    };

    // ---- Home (house) ----
    const ImVec2 hMin(boxPos.x + 3.0f, boxPos.y + 3.0f), hMax(hMin.x + kHomeSz, hMin.y + kHomeSz);
    const bool homeHover = hovered && mouse.x >= hMin.x && mouse.x <= hMax.x && mouse.y >= hMin.y &&
                           mouse.y <= hMax.y;
    {
        const ImU32 c = vc::WithAlpha(homeHover ? IM_COL32(120, 170, 235, 255) : IM_COL32(205, 210, 218, 255), alpha);
        const f32 x0 = hMin.x + 2, x1 = hMax.x - 2, y0 = hMin.y + 2, y1 = hMax.y - 2;
        const f32 xm = (x0 + x1) * 0.5f, ym = y0 + (y1 - y0) * 0.45f;
        dl->AddTriangleFilled(ImVec2(x0 - 1, ym), ImVec2(xm, y0), ImVec2(x1 + 1, ym), c);
        dl->AddRectFilled(ImVec2(x0 + 2, ym), ImVec2(x1 - 2, y1), c);
        dl->AddRectFilled(ImVec2(xm - 2, y1 - 6), ImVec2(xm + 2, y1), vc::WithAlpha(IM_COL32(40, 44, 52, 255), alpha));
    }

    // ---- Compass ring in the plane under the cube ----
    constexpr i32 kRingN = 72;
    constexpr f32 kRingR = 0.98f, kRingZ = -0.62f;
    const Vector3f upN = toNative({0, 0, 1});
    // Cube space is Max space with Y mirrored (the face normals above store
    // Front as +Y): north, Max +Y, is -Y here, so it lies behind the Front face.
    const Vector3f northN = toNative({0, -1, 0}), eastN = toNative({1, 0, 0});
    std::array<ImVec2, kRingN> ring{};
    std::array<bool, kRingN> ringNear{};  // segment i (point i to i+1) is in front of the cube
    for (i32 i = 0; i < kRingN; ++i) {
        const f32 a = 6.2831853f * i / kRingN;
        const Vector3f p = (northN * std::cos(a) + eastN * std::sin(a)) * kRingR + upN * kRingZ;
        ring[i] = project(p);
        const f32 am = 6.2831853f * (i + 0.5f) / kRingN;
        const Vector3f pm = (northN * std::cos(am) + eastN * std::sin(am)) * kRingR + upN * kRingZ;
        ringNear[i] = Dot(pm, eye) > 0.0f;
    }
    const ImVec2 ringC = project(upN * kRingZ);
    // Distance of the mouse to the ring, separately for the half in front of
    // the cube and the half behind it (which the cube covers).
    f32 ringDistNear = 1e9f, ringDistFar = 1e9f;
    for (i32 i = 0; i < kRingN; ++i) {
        const ImVec2 a = ring[i], b = ring[(i + 1) % kRingN];
        const ImVec2 ab(b.x - a.x, b.y - a.y), am(mouse.x - a.x, mouse.y - a.y);
        const f32 l2 = ab.x * ab.x + ab.y * ab.y;
        const f32 t = l2 > 0 ? std::clamp((am.x * ab.x + am.y * ab.y) / l2, 0.0f, 1.0f) : 0.0f;
        const f32 dx = a.x + ab.x * t - mouse.x, dy = a.y + ab.y * t - mouse.y;
        f32& d = ringNear[i] ? ringDistNear : ringDistFar;
        d = std::min(d, std::sqrt(dx * dx + dy * dy));
    }

    // ---- Visible faces, painter-sorted ----
    struct Vis { i32 face; f32 depth; };
    std::array<Vis, 6> vis{};
    i32 nVis = 0;
    for (i32 i = 0; i < 6; ++i) {
        const f32 d = Dot(kFaceN[i], eye);
        if (d > 1e-3f) vis[nVis++] = {i, d};
    }
    std::sort(vis.begin(), vis.begin() + nVis, [](const Vis& a, const Vis& b) { return a.depth < b.depth; });

    // A face split 3 x 3: the centre is the face, the strips its edges, the
    // squares its corners. Each part is a quad in cube space plus its zone.
    constexpr f32 kBorder = 0.22f;
    struct Part { ImVec2 q[4]; vc::Zone zone; };
    auto faceParts = [&](i32 face, std::array<Part, 9>& parts) {
        const Vector3f n = kFaceN[face];
        Vector3f a{n.y, n.z, n.x};
        a = (a - n * Dot(a, n)).normalized();
        const Vector3f b = cross(n, a);
        const f32 cuts[4] = {-0.5f, -0.5f + kBorder, 0.5f - kBorder, 0.5f};
        i32 k = 0;
        for (i32 iu = 0; iu < 3; ++iu)
            for (i32 iv = 0; iv < 3; ++iv) {
                const f32 u0 = cuts[iu], u1 = cuts[iu + 1], v0 = cuts[iv], v1 = cuts[iv + 1];
                const Vector3f c = n * 0.5f;
                Part& p = parts[k++];
                p.q[0] = project(c + a * u0 + b * v0);
                p.q[1] = project(c + a * u1 + b * v0);
                p.q[2] = project(c + a * u1 + b * v1);
                p.q[3] = project(c + a * u0 + b * v1);
                const Vector3f dir = n + a * static_cast<f32>(iu - 1) + b * static_cast<f32>(iv - 1);
                p.zone = vc::ToZone(dir);
            }
    };

    // What the mouse is over, front to back, as Max picks it: the ring and
    // letters in front of the cube, then the cube, then what lies behind it
    // (only where the cube does not cover it).
    vc::Zone cubeZone;
    if (hovered && !homeHover) {
        for (i32 v = nVis - 1; v >= 0 && !cubeZone.valid(); --v) {
            std::array<Part, 9> parts{};
            faceParts(vis[v].face, parts);
            for (const Part& p : parts)
                if (vc::PointInQuad(mouse, p.q)) { cubeZone = p.zone; break; }
        }
    }
    const bool overCube = cubeZone.valid();
    const bool ringHover = hovered && !homeHover &&
                           (ringDistNear < 6.0f || (!overCube && ringDistFar < 6.0f));

    // ---- Face-on: arrows to the neighbouring faces ----
    i32 faceOn = -1;
    for (i32 i = 0; i < 6; ++i)
        if (Dot(kFaceN[i], eye) > 0.9995f) faceOn = i;
    struct Arrow { ImVec2 a, b, c; Vector3f to; };
    std::array<Arrow, 4> arrows{};
    i32 nArrows = 0, hoverArrow = -1;
    // Face-on, a face fills a square of half size 0.5 * scale: the arrows sit
    // just outside it, as in Max.
    const f32 faceHalf = 0.5f * scale;
    if (faceOn >= 0) {
        const f32 r = faceHalf + 9.0f, w = 7.0f, h = 8.0f;
        const ImVec2 dirs[4] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        const Vector3f to[4] = {vUp, vUp * -1.0f, right * -1.0f, right};
        for (i32 i = 0; i < 4; ++i) {
            const ImVec2 d = dirs[i], o(-d.y, d.x);
            const ImVec2 tip(center.x + d.x * (r + h), center.y + d.y * (r + h));
            const ImVec2 b0(center.x + d.x * r + o.x * w, center.y + d.y * r + o.y * w);
            const ImVec2 b1(center.x + d.x * r - o.x * w, center.y + d.y * r - o.y * w);
            arrows[nArrows] = {tip, b0, b1, to[i]};
            if (hovered && vc::PointInTri(mouse, tip, b0, b1)) hoverArrow = nArrows;
            ++nArrows;
        }
    }

    // Roll arrows (face view): two curved arrows over the top corners.
    struct RollArrow { ImVec2 c; f32 dir; };
    std::array<RollArrow, 2> rolls{};
    i32 nRolls = 0, hoverRoll = -1;
    if (faceOn >= 0) {
        const f32 o = faceHalf + 10.0f;
        rolls[0] = {ImVec2(center.x + o, center.y - o), -1.0f};
        rolls[1] = {ImVec2(center.x - o, center.y - o), 1.0f};
        nRolls = 2;
        for (i32 i = 0; i < nRolls; ++i) {
            const f32 dx = mouse.x - rolls[i].c.x, dy = mouse.y - rolls[i].c.y;
            if (hovered && dx * dx + dy * dy < 81.0f) hoverRoll = i;
        }
    }

    // Compass letters: click to look from that direction.
    const char* compass[4] = {i18n::tr("wdx.viewcube.n"), i18n::tr("wdx.viewcube.e"),
                              i18n::tr("wdx.viewcube.s"), i18n::tr("wdx.viewcube.w")};
    std::array<ImVec2, 4> letterPos{};
    std::array<bool, 4> letterNear{};
    i32 hoverLetter = -1;
    for (i32 i = 0; i < 4; ++i) {
        const f32 a = 1.5707963f * i;
        const Vector3f p = (northN * std::cos(a) + eastN * std::sin(a)) * (kRingR + 0.16f) + upN * kRingZ;
        letterPos[i] = project(p);
        letterNear[i] = Dot(p, eye) > 0.0f;
        const ImVec2 ts = ImGui::CalcTextSize(compass[i]);
        if (hovered && !homeHover && (letterNear[i] || !overCube) &&
            std::abs(mouse.x - letterPos[i].x) <= ts.x * 0.5f + 3 &&
            std::abs(mouse.y - letterPos[i].y) <= ts.y * 0.5f + 2)
            hoverLetter = i;
    }
    // The cube part under the mouse, unless something in front of it is.
    vc::Zone hoverZone;
    if (st.drag == vc::State::Drag::None && !(ringHover && ringDistNear < 6.0f) &&
        !(hoverLetter >= 0 && letterNear[hoverLetter]))
        hoverZone = cubeZone;

    // ---- Draw ----
    // The ring and its letters in two passes: what lies behind the cube before
    // it, so the cube covers it, what lies in front after it.
    auto drawCompass = [&](bool nearPass) {
        const ImU32 ringCol = vc::WithAlpha(ringHover || st.drag == vc::State::Drag::Ring
                                                ? IM_COL32(120, 170, 235, 255) : IM_COL32(150, 156, 166, 230), alpha);
        for (i32 i = 0; i < kRingN; ++i)
            if (ringNear[i] == nearPass)
                dl->AddLine(ring[i], ring[(i + 1) % kRingN], ringCol, ringHover ? 5.0f : 4.0f);
        for (i32 i = 0; i < 4; ++i) {
            if (letterNear[i] != nearPass) continue;
            const ImVec2 ts = ImGui::CalcTextSize(compass[i]);
            dl->AddText(ImVec2(letterPos[i].x - ts.x * 0.5f, letterPos[i].y - ts.y * 0.5f),
                        vc::WithAlpha(i == hoverLetter ? IM_COL32(120, 170, 235, 255) : IM_COL32(225, 228, 235, 255), alpha),
                        compass[i]);
        }
    };
    drawCompass(false);

    for (i32 v = 0; v < nVis; ++v) {
        const i32 face = vis[v].face;
        std::array<Part, 9> parts{};
        faceParts(face, parts);
        for (const Part& p : parts) {
            const bool lit = hoverZone.valid() && p.zone == hoverZone;
            const ImU32 col = lit ? IM_COL32(110, 168, 240, 255) : IM_COL32(214, 218, 224, 255);
            dl->AddConvexPolyFilled(p.q, 4, vc::WithAlpha(col, alpha));
        }
        // outline of the face
        const ImVec2 o[4] = {parts[0].q[0], parts[6].q[1], parts[8].q[2], parts[2].q[3]};
        dl->AddPolyline(o, 4, vc::WithAlpha(IM_COL32(70, 76, 86, 255), alpha), ImDrawFlags_Closed, 1.2f);
        vc::FaceLabel(dl, i18n::tr(kFaceKey[face]), kFaceN[face] * 0.5f,
                      cross(kFaceN[face], kLabelUp[face]).normalized(), kLabelUp[face], project,
                      vc::WithAlpha(IM_COL32(40, 44, 52, 255), alpha));
    }
    drawCompass(true);
    for (i32 i = 0; i < nArrows; ++i) {
        const Arrow& ar = arrows[i];
        dl->AddTriangleFilled(ar.a, ar.b, ar.c,
                              vc::WithAlpha(i == hoverArrow ? IM_COL32(120, 170, 235, 255) : IM_COL32(205, 210, 218, 235), alpha));
    }

    for (i32 i = 0; i < nRolls; ++i) {
        // a quarter arc with an arrowhead, turning the way it rolls
        const ImU32 c = vc::WithAlpha(i == hoverRoll ? IM_COL32(120, 170, 235, 255) : IM_COL32(205, 210, 218, 235), alpha);
        const f32 sgn = rolls[i].dir;
        const f32 a0 = sgn < 0 ? -2.6f : -0.55f, a1 = sgn < 0 ? -0.55f : -2.6f;
        dl->PathArcTo(rolls[i].c, 7.0f, std::min(a0, a1), std::max(a0, a1), 10);
        dl->PathStroke(c, 0, 2.0f);
        const ImVec2 tip(rolls[i].c.x + 7.0f * std::cos(a1), rolls[i].c.y + 7.0f * std::sin(a1));
        const f32 t = a1 + 1.5707963f * (sgn < 0 ? 1.0f : -1.0f);
        dl->AddTriangleFilled(ImVec2(tip.x + 4.0f * std::cos(t), tip.y + 4.0f * std::sin(t)),
                              ImVec2(tip.x + 3.0f * std::cos(a1), tip.y + 3.0f * std::sin(a1)),
                              ImVec2(tip.x - 3.0f * std::cos(a1), tip.y - 3.0f * std::sin(a1)), c);
    }

    // ---- Input ----
    if (pressed) {
        st.pressPos = mouse;
        st.dragMoved = false;
        st.drag = (ringHover && hoverLetter < 0) ? vc::State::Drag::Ring
                  : (homeHover || hoverArrow >= 0 || hoverRoll >= 0 || hoverLetter >= 0
                         ? vc::State::Drag::None : vc::State::Drag::Cube);
        st.ringAngle = std::atan2(mouse.y - ringC.y, mouse.x - ringC.x);
        using T = vc::State::Target;
        st.target = T::None;
        if (homeHover) st.target = T::Home;
        else if (hoverRoll >= 0) { st.target = T::Roll; st.targetIndex = hoverRoll; }
        else if (hoverLetter >= 0) { st.target = T::Letter; st.targetIndex = hoverLetter; }
        else if (hoverArrow >= 0) { st.target = T::Arrow; st.targetDir = arrows[hoverArrow].to; }
        else if (hoverZone.valid()) {
            st.target = T::Zone;
            st.targetDir = Vector3f{static_cast<f32>(hoverZone.x), static_cast<f32>(hoverZone.y),
                                    static_cast<f32>(hoverZone.z)};
        }
    }
    if (held && st.drag != vc::State::Drag::None) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        // A mouse that left the window has no position (-FLT_MAX): that is no drag.
        if (ImGui::IsMousePosValid(&mouse) &&
            std::abs(mouse.x - st.pressPos.x) + std::abs(mouse.y - st.pressPos.y) > 3.0f)
            st.dragMoved = true;
        if (st.dragMoved) {
            st.animating = false;
            if (st.drag == vc::State::Drag::Cube) {
                camera.Rotate(static_cast<i32>(d.x), static_cast<i32>(d.y));
            } else {
                const f32 ang = std::atan2(mouse.y - ringC.y, mouse.x - ringC.x);
                camera.SetYaw(camera.GetYaw() + vc::WrapAngle(ang - st.ringAngle));
                st.ringAngle = ang;
            }
        }
    }
    if (released) {
        if (!st.dragMoved) {
            using T = vc::State::Target;
            f32 y = 0, p = 0;
            switch (st.target) {
            case T::Home:
                goHome();
                break;
            case T::Roll:
                turnTo(camera.GetYaw(), camera.GetPitch(),
                       camera.GetRoll() + (st.targetIndex == 0 ? -1.0f : 1.0f) * 1.5707963f);
                break;
            case T::Letter: {
                const f32 a = 1.5707963f * st.targetIndex;
                vc::ViewAngles(northN * std::cos(a) + eastN * std::sin(a), frontYaw, y, p);
                turnTo(y, camera.GetPitch());
                break;
            }
            case T::Arrow:
            case T::Zone:
                vc::ViewAngles(st.targetDir, frontYaw, y, p);
                turnTo(y, p);
                break;
            default:
                break;
            }
        }
        st.target = vc::State::Target::None;
        st.drag = vc::State::Drag::None;
        st.dragMoved = false;
    }
    if (homeHover)
        ImGui::SetTooltip("%s", i18n::tr("wdx.viewcube.home"));

    // Right-click menu, as Max's.
    if (ImGui::BeginPopupContextWindow("##wdx_viewcube_menu", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem(i18n::tr("wdx.viewcube.home")))
            goHome();
        if (ImGui::MenuItem(i18n::tr("wdx.viewcube.set_home"))) {
            st.hasHome = true;
            st.homeYaw = camera.GetYaw();
            st.homePitch = camera.GetPitch();
            st.homeRoll = camera.GetRoll();
            st.homeDistance = camera.GetDistance();
            st.homeTarget = camera.GetTarget();
        }
        bool ortho = camera.IsOrthographic();
        if (ImGui::MenuItem(i18n::tr("wdx.viewcube.ortho"), nullptr, &ortho))
            camera.SetOrthographic(ortho);
        ImGui::EndPopup();
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}

/// For a host whose 3D view is the window: the main viewport's work rect.
inline void DrawMaxViewCube(renderer::Camera& camera) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    DrawMaxViewCube(camera, vp->WorkPos, ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y));
}

} // namespace whiteout::flakes::wdxui
