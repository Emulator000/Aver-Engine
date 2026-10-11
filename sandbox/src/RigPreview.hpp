#pragma once
// RigPreview -- the skeleton view the IK Rig, Retarget and Control Rig editors share: an orbit camera, bones drawn as
// lines and joints as dots through the ImGui draw list, chain highlighting, and goal handles you drag on a
// camera-facing plane. No GPU and no skinned mesh: the AnimEditor's skinned preview is private to that file and is not
// worth extracting for this. Design: docs/plans/wave5/IK_RETARGETING.md section 8.
//
// The camera, projection and picking math is ImGui-free (and tested by RigPreviewTest); only drawRigPreview needs
// AVER_WITH_IMGUI.
#include "aver/anim/Pose.hpp"
#include "aver/core/Math.hpp"

#include <vector>

namespace aver::editor {

// An orbit camera around a target point. Engine space: centimetres, +Z up, +X forward, +Y right, left-handed.
struct RigView {
    Vec3 target{0, 0, 100};
    f32 yawDeg = 35.0f;          // azimuth of the eye around +Z, degrees
    f32 pitchDeg = 12.0f;        // elevation, clamped to +-85
    f32 distance = 320.0f;       // eye to target, cm
    f32 fovDeg = 45.0f;          // vertical
};

Vec3 rigEye(const RigView& v);
Vec3 rigForward(const RigView& v);
Vec3 rigRight(const RigView& v);
Vec3 rigCamUp(const RigView& v);

// World point -> pixel in a w x h rectangle (origin top-left). False when the point is behind the camera.
bool rigProject(const RigView& v, f32 w, f32 h, const Vec3& p, f32& sx, f32& sy, f32* depth = nullptr);

// The world ray through a pixel (dir is unit length).
void rigRay(const RigView& v, f32 w, f32 h, f32 px, f32 py, Vec3& origin, Vec3& dir);

// The point under a pixel on the camera-facing plane through `through` (how a goal handle is dragged).
bool rigPlanePoint(const RigView& v, f32 w, f32 h, f32 px, f32 py, const Vec3& through, Vec3& out);

void rigOrbit(RigView& v, f32 dxPixels, f32 dyPixels);
void rigPan(RigView& v, f32 w, f32 h, f32 dxPixels, f32 dyPixels);
void rigZoom(RigView& v, f32 wheelTicks);           // positive = in
// Centres the view on a sphere and backs off to fit it.
void rigFrame(RigView& v, const Vec3& center, f32 radius);
// Frames a pose's model-space bounds.
void rigFramePose(RigView& v, const std::vector<Mat4>& model);

// The index of the nearest point whose projection is within `radiusPx` of the pixel, or -1. Behind-camera points never hit.
i32 rigPick(const RigView& v, f32 w, f32 h, const std::vector<Vec3>& points, f32 px, f32 py, f32 radiusPx);

#if AVER_WITH_IMGUI

struct RigHandle {
    Vec3 pos{0, 0, 0};
    unsigned color = 0xFF50C8FFu;   // ABGR, as ImU32
    const char* label = "";
    bool enabled = true;
};

struct RigPreviewIn {
    const fmt::OcSkeleton* skel = nullptr;
    const anim::Pose* pose = nullptr;
    // One colour per bone (ABGR); 0 draws the default. Empty = all default.
    std::vector<unsigned> boneColors;
    std::vector<RigHandle> handles;
    i32 selectedBone = -1;
    bool drawGround = false;
    f32 groundZ = 0.0f;
    Vec3 groundNormal{0, 0, 1};
    const char* caption = nullptr;
};

struct RigPreviewOut {
    i32 clickedBone = -1;       // a click (no drag) on a joint
    i32 draggingHandle = -1;    // a handle being dragged this frame
    Vec3 handlePos{0, 0, 0};    // where it was dragged to
    bool handleReleased = false;
};

// Draws the view into a size x size region at the cursor and handles its input: left-drag orbits (or drags a
// handle it starts on), right/middle-drag pans, the wheel zooms.
RigPreviewOut drawRigPreview(const char* id, float width, float height, RigView& view, const RigPreviewIn& in);
#endif

} // namespace aver::editor
