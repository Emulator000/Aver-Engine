// RigPreview: orbit-camera math (ImGui-free, tested) and the ImGui draw/interaction layer. See RigPreview.hpp.
#include "RigPreview.hpp"

#include <algorithm>
#include <cmath>

#if AVER_WITH_IMGUI
#  include "imgui.h"
#endif

namespace aver::editor {

namespace {
constexpr f32 kNear = 1.0f;

f32 focalPx(const RigView& v, f32 h) { return (h * 0.5f) / std::tan(radians(std::clamp(v.fovDeg, 10.0f, 120.0f)) * 0.5f); }
} // namespace

Vec3 rigEye(const RigView& v) {
    const f32 cy = std::cos(radians(v.yawDeg)), sy = std::sin(radians(v.yawDeg));
    const f32 cp = std::cos(radians(v.pitchDeg)), sp = std::sin(radians(v.pitchDeg));
    return v.target + Vec3{cp * cy, cp * sy, sp} * v.distance;
}

Vec3 rigForward(const RigView& v) { return (v.target - rigEye(v)).getSafeNormal(); }

Vec3 rigRight(const RigView& v) {
    const Vec3 r = cross(Vec3{0, 0, 1}, rigForward(v)).getSafeNormal();
    return r.sizeSquared() > 0.5f ? r : Vec3{0, 1, 0};
}

Vec3 rigCamUp(const RigView& v) { return cross(rigForward(v), rigRight(v)).getSafeNormal(); }

bool rigProject(const RigView& v, f32 w, f32 h, const Vec3& p, f32& sx, f32& sy, f32* depth) {
    const Vec3 eye = rigEye(v), fwd = rigForward(v), right = rigRight(v), up = rigCamUp(v);
    const Vec3 d = p - eye;
    const f32 z = dot(d, fwd);
    if (z <= kNear) return false;
    const f32 f = focalPx(v, h);
    sx = w * 0.5f + f * dot(d, right) / z;
    sy = h * 0.5f - f * dot(d, up) / z;
    if (depth) *depth = z;
    return true;
}

void rigRay(const RigView& v, f32 w, f32 h, f32 px, f32 py, Vec3& origin, Vec3& dir) {
    const f32 f = focalPx(v, h);
    origin = rigEye(v);
    dir = (rigForward(v) + rigRight(v) * ((px - w * 0.5f) / f) + rigCamUp(v) * (-(py - h * 0.5f) / f)).getSafeNormal();
}

bool rigPlanePoint(const RigView& v, f32 w, f32 h, f32 px, f32 py, const Vec3& through, Vec3& out) {
    Vec3 o, d;
    rigRay(v, w, h, px, py, o, d);
    const Vec3 fwd = rigForward(v);
    const f32 denom = dot(d, fwd);
    if (denom < 1e-5f) return false;
    const f32 t = dot(through - o, fwd) / denom;
    if (t <= 0.0f) return false;
    out = o + d * t;
    return true;
}

void rigOrbit(RigView& v, f32 dx, f32 dy) {
    v.yawDeg -= dx * 0.4f;
    v.pitchDeg = std::clamp(v.pitchDeg + dy * 0.4f, -85.0f, 85.0f);
    while (v.yawDeg > 180.0f) v.yawDeg -= 360.0f;
    while (v.yawDeg < -180.0f) v.yawDeg += 360.0f;
}

void rigPan(RigView& v, f32 w, f32 h, f32 dx, f32 dy) {
    (void)w;
    const f32 perPixel = v.distance / focalPx(v, h);
    v.target = v.target - rigRight(v) * (dx * perPixel) + rigCamUp(v) * (dy * perPixel);
}

void rigZoom(RigView& v, f32 ticks) {
    v.distance = std::clamp(v.distance * std::pow(0.88f, ticks), 20.0f, 4000.0f);
}

void rigFrame(RigView& v, const Vec3& center, f32 radius) {
    v.target = center;
    const f32 half = radians(std::clamp(v.fovDeg, 10.0f, 120.0f)) * 0.5f;
    v.distance = std::clamp(std::max(radius, 10.0f) / std::sin(half) * 1.1f, 20.0f, 4000.0f);
}

void rigFramePose(RigView& v, const std::vector<Mat4>& model) {
    if (model.empty()) return;
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const Mat4& m : model) {
        const Vec3 p{m.m[3][0], m.m[3][1], m.m[3][2]};
        lo = Vec3{std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = Vec3{std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    rigFrame(v, (lo + hi) * 0.5f, (hi - lo).size() * 0.5f);
}

i32 rigPick(const RigView& v, f32 w, f32 h, const std::vector<Vec3>& points, f32 px, f32 py, f32 radiusPx) {
    i32 best = -1;
    f32 bestD = radiusPx * radiusPx;
    for (usize i = 0; i < points.size(); ++i) {
        f32 sx, sy;
        if (!rigProject(v, w, h, points[i], sx, sy)) continue;
        const f32 d = (sx - px) * (sx - px) + (sy - py) * (sy - py);
        if (d <= bestD) { bestD = d; best = static_cast<i32>(i); }
    }
    return best;
}

#if AVER_WITH_IMGUI

RigPreviewOut drawRigPreview(const char* id, float width, float height, RigView& view, const RigPreviewIn& in) {
    RigPreviewOut out;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const f32 w = std::max(width, 40.0f), h = std::max(height, 40.0f);
    ImGui::InvisibleButton(id, ImVec2(w, h),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiIO& io = ImGui::GetIO();
    ImGuiStorage* st = ImGui::GetStateStorage();
    const ImGuiID dragKey = ImGui::GetID("##rigdrag"), pressKey = ImGui::GetID("##rigpress");

    // Joint positions in model space.
    std::vector<Mat4> model;
    std::vector<Vec3> joints;
    if (in.skel && in.pose && in.pose->matches(*in.skel)) {
        anim::poseToModel(*in.skel, *in.pose, model);
        joints.reserve(model.size());
        for (const Mat4& m : model) joints.push_back(Vec3{m.m[3][0], m.m[3][1], m.m[3][2]});
    }
    std::vector<Vec3> handlePts;
    for (const RigHandle& hd : in.handles) handlePts.push_back(hd.pos);

    const f32 mx = io.MousePos.x - p0.x, my = io.MousePos.y - p0.y;
    if (ImGui::IsItemActivated() && ImGui::IsMouseDown(0)) {
        // A press on a handle starts dragging it; anything else orbits.
        i32 hit = rigPick(view, w, h, handlePts, mx, my, 12.0f);
        if (hit >= 0 && !in.handles[static_cast<usize>(hit)].enabled) hit = -1;
        st->SetInt(dragKey, hit);
        st->SetFloat(pressKey, 0.0f);
    }
    const i32 dragging = st->GetInt(dragKey, -1);
    if (active && ImGui::IsMouseDown(0)) {
        if (dragging >= 0 && static_cast<usize>(dragging) < in.handles.size()) {
            Vec3 p;
            if (rigPlanePoint(view, w, h, mx, my, in.handles[static_cast<usize>(dragging)].pos, p)) {
                out.draggingHandle = dragging;
                out.handlePos = p;
            }
        } else {
            rigOrbit(view, io.MouseDelta.x, io.MouseDelta.y);
        }
        st->SetFloat(pressKey, st->GetFloat(pressKey, 0.0f) + std::fabs(io.MouseDelta.x) + std::fabs(io.MouseDelta.y));
    }
    if (active && (ImGui::IsMouseDown(1) || ImGui::IsMouseDown(2))) rigPan(view, w, h, io.MouseDelta.x, io.MouseDelta.y);
    if (hovered && io.MouseWheel != 0.0f) rigZoom(view, io.MouseWheel);
    if (ImGui::IsItemDeactivated()) {
        if (dragging >= 0) out.handleReleased = true;
        else if (st->GetFloat(pressKey, 0.0f) < 4.0f) out.clickedBone = rigPick(view, w, h, joints, mx, my, 10.0f);
        st->SetInt(dragKey, -1);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, ImVec2(p0.x + w, p0.y + h), true);
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), IM_COL32(24, 26, 30, 255));
    auto P = [&](const Vec3& q, ImVec2& s) {
        f32 sx, sy;
        if (!rigProject(view, w, h, q, sx, sy)) return false;
        s = ImVec2(p0.x + sx, p0.y + sy);
        return true;
    };
    auto line = [&](const Vec3& a, const Vec3& b, ImU32 col, f32 thick) {
        ImVec2 sa, sb;
        if (P(a, sa) && P(b, sb)) dl->AddLine(sa, sb, col, thick);
    };

    // The ground: a grid on the plane through (0,0,groundZ) with the given normal.
    if (in.drawGround) {
        const Vec3 n = in.groundNormal.getSafeNormal();
        Vec3 u = cross(n, Vec3{0, 1, 0});
        if (u.sizeSquared() < 1e-4f) u = cross(n, Vec3{1, 0, 0});
        u = u.getSafeNormal();
        const Vec3 v2 = cross(n, u).getSafeNormal();
        const Vec3 c{0, 0, in.groundZ};
        for (int i = -6; i <= 6; ++i) {
            const f32 t = static_cast<f32>(i) * 30.0f;
            line(c + u * t - v2 * 180.0f, c + u * t + v2 * 180.0f, IM_COL32(70, 76, 86, 255), 1.0f);
            line(c + v2 * t - u * 180.0f, c + v2 * t + u * 180.0f, IM_COL32(70, 76, 86, 255), 1.0f);
        }
    }
    line(Vec3{0, 0, 0}, Vec3{40, 0, 0}, IM_COL32(220, 70, 70, 255), 1.5f);
    line(Vec3{0, 0, 0}, Vec3{0, 40, 0}, IM_COL32(80, 200, 90, 255), 1.5f);
    line(Vec3{0, 0, 0}, Vec3{0, 0, 40}, IM_COL32(90, 130, 240, 255), 1.5f);

    if (in.skel && joints.size() == in.skel->bones.size()) {
        for (usize i = 0; i < joints.size(); ++i) {
            const i32 par = in.skel->bones[i].parent;
            if (par < 0) continue;
            const ImU32 col = (i < in.boneColors.size() && in.boneColors[i]) ? in.boneColors[i] : IM_COL32(190, 192, 205, 255);
            line(joints[static_cast<usize>(par)], joints[i], col, (i < in.boneColors.size() && in.boneColors[i]) ? 2.5f : 1.5f);
        }
        for (usize i = 0; i < joints.size(); ++i) {
            ImVec2 s;
            if (!P(joints[i], s)) continue;
            const bool sel = static_cast<i32>(i) == in.selectedBone;
            const ImU32 col = (i < in.boneColors.size() && in.boneColors[i]) ? in.boneColors[i] : IM_COL32(190, 192, 205, 255);
            dl->AddCircleFilled(s, sel ? 5.0f : 2.5f, sel ? IM_COL32(255, 225, 90, 255) : col);
        }
    }
    for (usize i = 0; i < in.handles.size(); ++i) {
        ImVec2 s;
        if (!P(in.handles[i].pos, s)) continue;
        const bool on = static_cast<i32>(i) == dragging;
        dl->AddCircleFilled(s, 7.0f, in.handles[i].enabled ? in.handles[i].color : IM_COL32(110, 110, 110, 255));
        dl->AddCircle(s, 8.5f, on ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 24, 255), 0, on ? 2.0f : 1.0f);
        if (in.handles[i].label && in.handles[i].label[0])
            dl->AddText(ImVec2(s.x + 11.0f, s.y - 7.0f), IM_COL32(235, 235, 240, 255), in.handles[i].label);
    }
    if (in.caption) dl->AddText(ImVec2(p0.x + 6.0f, p0.y + 4.0f), IM_COL32(150, 154, 168, 255), in.caption);
    dl->PopClipRect();
    return out;
}

#endif  // AVER_WITH_IMGUI

} // namespace aver::editor
