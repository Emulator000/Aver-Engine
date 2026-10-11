// Root motion: the planar travel baked into a clip's root bone, pulled out as a delta the entity
// follows instead of the mesh. Pure arithmetic over a clip and a skeleton, so it is decidable with
// no scene. Design: docs/ANIM_MONTAGES.md.
//
// ASSUMES A Z-UP ENGINE-SPACE RIG whose heading is a rotation about +Z. A root bone authored with a
// different up axis extracts no yaw (fix it at import).
#pragma once

#include "aver/anim/Pose.hpp"

namespace aver::anim {

inline constexpr u32 kRootAxisX = 1, kRootAxisY = 2, kRootAxisZ = 4, kRootAxisYaw = 8;
inline constexpr u32 kRootAxesDefault = kRootAxisX | kRootAxisY | kRootAxisYaw;
inline constexpr u32 kNoBone = 0xFFFFFFFFu;

// Built once per entity per tick by the host. `rest` is the lock reference: the extracted components
// are replaced by the skeleton's rest values (Unreal's default root lock), so the mesh stays on the
// entity while the entity moves by the deltas.
struct RootSpec {
    u32 bone = kNoBone;
    u32 axes = kRootAxesDefault;
    Transform rest;
    bool valid() const { return bone != kNoBone; }
};

// Planar rigid motion in the root's own frame at the START of the step.
struct RootDelta {
    Vec3 move{0, 0, 0};       // cm
    f32  yaw = 0.0f;          // radians about +Z
    bool any() const { return move.x != 0.0f || move.y != 0.0f || move.z != 0.0f || yaw != 0.0f; }
};

// OcSkeleton::rootBone when in range, else the first bone with no parent, else kNoBone.
u32 rootBoneOf(const fmt::OcSkeleton& s);
// Twist of q about +Z in radians, (-pi, pi].
f32 yawOf(const Quat& q);
// The clip carries kOcAnimRootMotion, is not additive or an object clip, and has a track on s.bone.
bool clipHasRootMotion(const fmt::OcAnimation& c, const RootSpec& s);
// Motion between two clip times, already wrapped into the clip. wrap = true reads t1 < t0 as forward
// through the loop seam (t0 -> duration, then 0 -> t1). move = Rz(-yaw0) * (p1 - p0), masked by s.axes.
RootDelta rootDeltaBetween(const fmt::OcAnimation& c, const RootSpec& s, f32 t0, f32 t1, bool wrap);
// acc += w * d
void addWeighted(RootDelta& acc, const RootDelta& d, f32 w);
// a toward b by t, componentwise.
RootDelta lerpDelta(const RootDelta& a, const RootDelta& b, f32 t);
// acc = acc o d: acc.move += Rz(acc.yaw) * d.move; acc.yaw += d.yaw.
void composeDelta(RootDelta& acc, const RootDelta& d);
// Replaces the extracted components of the root bone with the skeleton's rest values: positions per
// s.axes; yaw as q = Rz(yaw) * swing -> Rz(yawRest) * swing, so pitch and roll survive. No-op when !s.valid().
void lockRoot(Pose& p, const RootSpec& s);

} // namespace aver::anim
