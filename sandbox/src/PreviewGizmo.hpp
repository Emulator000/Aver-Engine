#pragma once
// PreviewGizmo -- the translate / rotate / scale gizmo MATHS, ImGui-free, lifted from the maths half
// of ActorEditor's gizmo (projectToScreen, axisTip, pickGizmoAxis, dragAlong, dragRotateAxis,
// dragScaleAxis) so the instance editor and a headless test can use it. ActorEditor itself still has
// its own copy and may adopt this later.
//
// WHAT IS THE SAME as ActorEditor: three axis handles drawn as a fixed pixel length from the pivot
// (found by projecting a unit step along the axis), picked by the cursor's distance to the handle
// SEGMENT in image pixels, and dragged by projecting the cursor motion onto the handle's screen
// direction. Translate follows the cursor (pixels along the handle / pixels-per-cm), a full handle
// length is a quarter turn for rotate and a doubling for scale. Scale handles are always LOCAL
// (ActorEditor's comment explains why: row i of the matrix scales along the object's own axis i).
//
// WHAT IS DIFFERENT, and why:
//  * A drag is FROZEN at begin (view, pivot, axes, start cursor) and update() takes the cursor's
//    TOTAL motion, returning the TOTAL world-space delta. ActorEditor re-projected every frame and
//    applied an incremental delta; the total form is what snapping needs (snap the cumulative value,
//    not each step) and what InstanceSetModel::applyDelta wants (base * total delta, no compounding).
//  * The result is an affine DELTA in the .ocinst 12-float row-vector convention about the pivot, so
//    one selection of many instances rotates/scales about the pivot as a rigid group:
//    new_row = old_row * delta.
//  * A centre handle (axis kAxisUniform) for uniform scale.
//  * Snapping (translate cm, rotate degrees, scale step) and World/Local space.
//
// CONVENTIONS. viewProj is 16 floats row-major, row-vector (clip = [p,1] * VP) as in ActorEditor and
// the model; image pixels have +Y DOWN; the cursor is image-local pixels. Z-up world, centimetres.
#include "InstanceSetModel.hpp"   // xf:: 12-float affine helpers

#if AVER_WITH_IMGUI
struct ImDrawList;
#endif

namespace aver::editor::gizmo {

enum class Mode { Translate, Rotate, Scale };
enum class Space { World, Local };

inline constexpr int kAxisNone    = -1;
inline constexpr int kAxisUniform = 3;     // Scale only: the centre handle
inline constexpr f32 kHandlePixels = 64.0f;   // handle length on screen
inline constexpr f32 kGrabPixels   = 10.0f;   // pick radius around a handle
inline constexpr f32 kCenterPixels = 7.0f;    // pick radius of the uniform-scale centre

struct View {
    f32 viewProj[16] = {};
    f32 width = 1.0f, height = 1.0f;   // image size in pixels
};

// Where the gizmo sits and which way its three handles point (unit WORLD directions).
struct Frame {
    f32 pivot[3] = {0, 0, 0};
    f32 axes[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
};

struct Snap {   // 0 = off
    f32 translateCm = 0.0f;
    f32 rotateDeg   = 0.0f;
    f32 scaleStep   = 0.0f;   // snaps the scale FACTOR (e.g. 0.1)
};

// Pivot = the matrix's translation; axes = identity (World) or the matrix's normalised basis rows
// (Local). Scale is always Local, whatever `space` says. A degenerate row falls back to its world axis.
void frameFromMatrix(Mode mode, Space space, const f32 m12[12], Frame& out);
// Same with an explicit pivot (a multi-selection's centre) and an optional orientation matrix
// (nullptr = world axes).
void frameAt(Mode mode, Space space, const f32 pivot[3], const f32* orientationM12, Frame& out);

// World point to image pixels. False when behind the eye.
bool project(const View& v, const f32 world[3], f32 out[2]);
// Screen position of axis `axis`' handle tip (handlePx from the pivot). False when the pivot is behind
// the eye or the axis points at it.
bool axisTip(const View& v, const Frame& f, int axis, f32 out[2], f32 handlePx = kHandlePixels);
// The handle under an image-local cursor (0..2, or kAxisUniform in Scale mode), else kAxisNone.
int pickAxis(const View& v, const Frame& f, Mode mode, const f32 cursor[2],
             f32 grabPx = kGrabPixels, f32 handlePx = kHandlePixels);

// An active drag. Plain data: copy it, store it in the tool, drop it on release.
struct Drag {
    bool active = false;
    Mode mode = Mode::Translate;
    int axis = kAxisNone;
    View view;
    Frame frame;
    Snap snap;
    f32 handlePx = kHandlePixels;
    f32 start[2] = {0, 0};      // cursor at begin
    f32 dirPx[2] = {1, 0};      // unit screen direction of the handle
    f32 pxPerCm = 1.0f;         // screen length of a 1 cm step along the axis (translate)
    f32 amount = 0.0f;          // last update(): cm (translate), degrees (rotate), factor (scale)
};

// Starts a drag on `axis` (as returned by pickAxis). False when the handle is edge-on / off-screen.
bool beginDrag(Drag& d, const View& v, const Frame& f, Mode mode, int axis, const f32 cursor[2],
               const Snap& snap = {}, f32 handlePx = kHandlePixels);
// Total motion since begin -> the TOTAL world delta about the pivot (new_row = old_row * delta12).
// False (delta untouched) when the drag is not active.
bool updateDrag(Drag& d, const f32 cursor[2], f32 delta12[12]);
inline void endDrag(Drag& d) { d.active = false; }

// ---- the delta builders updateDrag uses (exposed for tests and for numeric entry fields) ----------
void translationDelta(const f32 dir[3], f32 distCm, f32 out[12]);
void rotationDelta(const f32 pivot[3], const f32 axis[3], f32 degrees, f32 out[12]);
void scaleAxisDelta(const f32 pivot[3], const f32 axis[3], f32 factor, f32 out[12]);
void scaleUniformDelta(const f32 pivot[3], f32 factor, f32 out[12]);

#if AVER_WITH_IMGUI
// Draws the handles over the image whose top-left is `topLeft` on screen. hotAxis highlights one
// (the hovered or dragged handle). Defined in PreviewGizmo.cpp; ImVec2 stays out of this header.
void draw(ImDrawList* dl, f32 topLeftX, f32 topLeftY, const View& v, const Frame& f, Mode mode,
          int hotAxis, f32 handlePx = kHandlePixels);
#endif

} // namespace aver::editor::gizmo
