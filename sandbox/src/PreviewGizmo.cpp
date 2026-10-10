// PreviewGizmo -- see the header. The maths half is ImGui-free; draw() sits behind AVER_WITH_IMGUI.
#include "PreviewGizmo.hpp"

#include <algorithm>
#include <cmath>

#if AVER_WITH_IMGUI
#  include "imgui.h"
#endif

namespace aver::editor::gizmo {

namespace {

constexpr f32 kPi = 3.14159265358979f;

f32 len3(const f32 v[3]) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); }

f32 snapTo(f32 v, f32 step) { return step > 0.0f ? std::round(v / step) * step : v; }

// The 3x3 of `a` (row-major, rows = images of the basis) and translation c - c*A so the affine keeps
// the pivot c fixed.
void aboutPivot(const f32 pivot[3], const f32 a[9], f32 out[12]) {
    for (int i = 0; i < 9; ++i) out[i] = a[i];
    for (int c = 0; c < 3; ++c)
        out[9 + c] = pivot[c] - (pivot[0]*a[0*3+c] + pivot[1]*a[1*3+c] + pivot[2]*a[2*3+c]);
}

} // namespace

// ---------------------------------------------------------------- frames

void frameAt(Mode mode, Space space, const f32 pivot[3], const f32* m12, Frame& out) {
    for (int c = 0; c < 3; ++c) out.pivot[c] = pivot[c];
    for (int a = 0; a < 3; ++a)
        for (int c = 0; c < 3; ++c) out.axes[a][c] = (a == c) ? 1.0f : 0.0f;
    // Scale is ALWAYS local: row i of the matrix scales along the object's own axis i.
    if (m12 && (space == Space::Local || mode == Mode::Scale)) {
        for (int a = 0; a < 3; ++a) {
            const f32 row[3] = {m12[a*3+0], m12[a*3+1], m12[a*3+2]};
            const f32 l = len3(row);
            if (l > 1e-8f) for (int c = 0; c < 3; ++c) out.axes[a][c] = row[c] / l;
        }
    }
}

void frameFromMatrix(Mode mode, Space space, const f32 m12[12], Frame& out) {
    frameAt(mode, space, m12 + 9, m12, out);
}

// ---------------------------------------------------------------- projection and picking

bool project(const View& v, const f32 p[3], f32 out[2]) {
    const f32* vp = v.viewProj;
    const f32 x = p[0]*vp[0] + p[1]*vp[4] + p[2]*vp[8]  + vp[12];
    const f32 y = p[0]*vp[1] + p[1]*vp[5] + p[2]*vp[9]  + vp[13];
    const f32 w = p[0]*vp[3] + p[1]*vp[7] + p[2]*vp[11] + vp[15];
    if (w <= 1e-4f) return false;   // behind the eye; there is no honest screen position
    out[0] = (x / w * 0.5f + 0.5f) * v.width;
    out[1] = (0.5f - y / w * 0.5f) * v.height;   // screen +Y is down
    return true;
}

namespace {
// Pivot and the unit-step projection along a world direction: origin px, unit direction px, px per cm.
bool axisScreen(const View& v, const f32 pivot[3], const f32 dir[3], f32 origin[2], f32 dirPx[2], f32& pxPerCm) {
    if (!project(v, pivot, origin)) return false;
    const f32 probe[3] = {pivot[0] + dir[0], pivot[1] + dir[1], pivot[2] + dir[2]};
    f32 unit[2];
    if (!project(v, probe, unit)) return false;
    const f32 dx = unit[0] - origin[0], dy = unit[1] - origin[1];
    const f32 l = std::sqrt(dx*dx + dy*dy);
    if (l < 1e-5f) return false;   // the axis points at the eye; there is no direction to draw
    dirPx[0] = dx / l;
    dirPx[1] = dy / l;
    pxPerCm = l;
    return true;
}
} // namespace

bool axisTip(const View& v, const Frame& f, int axis, f32 out[2], f32 handlePx) {
    if (axis < 0 || axis > 2) return false;
    f32 o[2], d[2], ppc;
    if (!axisScreen(v, f.pivot, f.axes[axis], o, d, ppc)) return false;
    out[0] = o[0] + d[0] * handlePx;
    out[1] = o[1] + d[1] * handlePx;
    return true;
}

int pickAxis(const View& v, const Frame& f, Mode mode, const f32 cursor[2], f32 grabPx, f32 handlePx) {
    f32 origin[2];
    if (!project(v, f.pivot, origin)) return kAxisNone;
    if (mode == Mode::Scale) {
        const f32 dx = cursor[0] - origin[0], dy = cursor[1] - origin[1];
        if (std::sqrt(dx*dx + dy*dy) <= kCenterPixels) return kAxisUniform;
    }
    int best = kAxisNone;
    f32 bestD = grabPx;
    for (int a = 0; a < 3; ++a) {
        f32 tip[2];
        if (!axisTip(v, f, a, tip, handlePx)) continue;
        const f32 dx = tip[0] - origin[0], dy = tip[1] - origin[1];
        const f32 len2 = dx*dx + dy*dy;
        f32 t = len2 > 1e-4f ? ((cursor[0] - origin[0]) * dx + (cursor[1] - origin[1]) * dy) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const f32 px = origin[0] + dx * t, py = origin[1] + dy * t;
        const f32 d = std::sqrt((cursor[0] - px) * (cursor[0] - px) + (cursor[1] - py) * (cursor[1] - py));
        if (d < bestD) { bestD = d; best = a; }
    }
    return best;
}

// ---------------------------------------------------------------- delta builders

void translationDelta(const f32 dir[3], f32 distCm, f32 out[12]) {
    xf::identity(out);
    for (int c = 0; c < 3; ++c) out[9 + c] = dir[c] * distCm;
}

void rotationDelta(const f32 pivot[3], const f32 axis[3], f32 degrees, f32 out[12]) {
    f32 d[3] = {axis[0], axis[1], axis[2]};
    const f32 l = len3(d);
    if (l < 1e-8f) { xf::identity(out); return; }
    for (f32& x : d) x /= l;
    const f32 a = degrees * kPi / 180.0f;
    const f32 c = std::cos(a), s = std::sin(a);
    // Row-vector Rodrigues: p' = c*p + s*(d x p) + (1-c)(d.p)d, i.e. R[i][j] = c*I + (1-c) d d^T - s*[d]x.
    const f32 skew[9] = {0, -d[2], d[1],   d[2], 0, -d[0],   -d[1], d[0], 0};
    f32 r[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r[i*3+j] = (i == j ? c : 0.0f) + (1.0f - c) * d[i] * d[j] - s * skew[i*3+j];
    aboutPivot(pivot, r, out);
}

void scaleAxisDelta(const f32 pivot[3], const f32 axis[3], f32 factor, f32 out[12]) {
    f32 d[3] = {axis[0], axis[1], axis[2]};
    const f32 l = len3(d);
    if (l < 1e-8f) { xf::identity(out); return; }
    for (f32& x : d) x /= l;
    f32 a[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) a[i*3+j] = (i == j ? 1.0f : 0.0f) + (factor - 1.0f) * d[i] * d[j];
    aboutPivot(pivot, a, out);
}

void scaleUniformDelta(const f32 pivot[3], f32 factor, f32 out[12]) {
    const f32 a[9] = {factor, 0, 0,   0, factor, 0,   0, 0, factor};
    aboutPivot(pivot, a, out);
}

// ---------------------------------------------------------------- dragging

bool beginDrag(Drag& d, const View& v, const Frame& f, Mode mode, int axis, const f32 cursor[2],
               const Snap& snap, f32 handlePx) {
    d = Drag{};
    if (axis < 0 || axis > (mode == Mode::Scale ? kAxisUniform : 2)) return false;
    d.mode = mode;
    d.axis = axis;
    d.view = v;
    d.frame = f;
    d.snap = snap;
    d.handlePx = handlePx > 1.0f ? handlePx : kHandlePixels;
    d.start[0] = cursor[0];
    d.start[1] = cursor[1];
    if (axis == kAxisUniform) {
        f32 o[2];
        if (!project(v, f.pivot, o)) return false;
    } else {
        f32 o[2];
        if (!axisScreen(v, f.pivot, f.axes[axis], o, d.dirPx, d.pxPerCm)) return false;
    }
    d.active = true;
    return true;
}

bool updateDrag(Drag& d, const f32 cursor[2], f32 delta12[12]) {
    if (!d.active) return false;
    const f32 dx = cursor[0] - d.start[0], dy = cursor[1] - d.start[1];
    const f32 pivot[3] = {d.frame.pivot[0], d.frame.pivot[1], d.frame.pivot[2]};

    if (d.axis == kAxisUniform) {
        // Right/up grows, left/down shrinks: a handle-length of net motion doubles it.
        f32 fct = std::clamp(1.0f + (dx - dy) / d.handlePx, 0.02f, 50.0f);
        fct = std::max(snapTo(fct, d.snap.scaleStep), 0.02f);
        d.amount = fct;
        scaleUniformDelta(pivot, fct, delta12);
        return true;
    }

    const f32* dir = d.frame.axes[d.axis];
    const f32 along = dx * d.dirPx[0] + dy * d.dirPx[1];   // pixels along the handle
    switch (d.mode) {
    case Mode::Translate: {
        const f32 dist = snapTo(along / d.pxPerCm, d.snap.translateCm);
        d.amount = dist;
        translationDelta(dir, dist, delta12);
        break;
    }
    case Mode::Rotate: {
        const f32 deg = snapTo(along / d.handlePx * 90.0f, d.snap.rotateDeg);   // a handle length = quarter turn
        d.amount = deg;
        rotationDelta(pivot, dir, deg, delta12);
        break;
    }
    case Mode::Scale: {
        f32 fct = std::clamp(1.0f + along / d.handlePx, 0.02f, 50.0f);   // a handle length doubles
        fct = std::max(snapTo(fct, d.snap.scaleStep), 0.02f);
        d.amount = fct;
        scaleAxisDelta(pivot, dir, fct, delta12);
        break;
    }
    }
    return true;
}

// ---------------------------------------------------------------- drawing

#if AVER_WITH_IMGUI
void draw(ImDrawList* dl, f32 tlx, f32 tly, const View& v, const Frame& f, Mode mode, int hotAxis, f32 handlePx) {
    f32 origin[2];
    if (!dl || !project(v, f.pivot, origin)) return;
    // +X red, +Y green, +Z blue (ActorEditor's colours).
    const ImU32 col[3] = {IM_COL32(230, 70, 70, 255), IM_COL32(90, 210, 90, 255), IM_COL32(80, 140, 245, 255)};
    const ImVec2 o(tlx + origin[0], tly + origin[1]);
    for (int a = 0; a < 3; ++a) {
        f32 tip[2];
        if (!axisTip(v, f, a, tip, handlePx)) continue;
        const ImVec2 t(tlx + tip[0], tly + tip[1]);
        const bool hot = hotAxis == a;
        dl->AddLine(o, t, col[a], hot ? 4.0f : 2.5f);
        const f32 r = hot ? 6.0f : 4.5f;
        if (mode == Mode::Scale)
            dl->AddRectFilled(ImVec2(t.x - r, t.y - r), ImVec2(t.x + r, t.y + r), col[a]);
        else if (mode == Mode::Rotate)
            dl->AddQuadFilled(ImVec2(t.x, t.y - r - 1), ImVec2(t.x + r + 1, t.y), ImVec2(t.x, t.y + r + 1),
                              ImVec2(t.x - r - 1, t.y), col[a]);
        else
            dl->AddCircleFilled(t, r, col[a]);
    }
    const bool hotC = hotAxis == kAxisUniform;
    dl->AddCircleFilled(o, hotC ? 5.0f : 3.0f, hotC ? IM_COL32(255, 220, 90, 255) : IM_COL32(240, 240, 240, 255));
}
#endif

} // namespace aver::editor::gizmo
