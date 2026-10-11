// Root motion extraction and lock. See RootMotion.hpp.
#include "aver/anim/RootMotion.hpp"
#include "aver/anim/AnimSampler.hpp"

#include <cmath>

namespace aver::anim {

namespace {

f32 wrapPi(f32 a) {
    while (a > kPi) a -= kTwoPi;
    while (a <= -kPi) a += kTwoPi;
    return a;
}

Quat rotZ(f32 yaw) { return Quat::fromAxisAngle(Vec3{0, 0, 1}, yaw); }

const fmt::OcTrack* trackOf(const fmt::OcAnimation& c, u32 bone) {
    for (const fmt::OcTrack& t : c.tracks)
        if (t.boneIndex == bone) return &t;
    return nullptr;
}

Transform rootAt(const fmt::OcTrack& t, const RootSpec& s, f32 time) {
    Transform x = s.rest;
    sampleTrack(t, time, x);
    return x;
}

// One straight (non-wrapping) step.
RootDelta stepDelta(const fmt::OcTrack& t, const RootSpec& s, f32 t0, f32 t1) {
    const Transform a = rootAt(t, s, t0), b = rootAt(t, s, t1);
    const f32 yaw0 = yawOf(a.rotation), yaw1 = yawOf(b.rotation);
    Vec3 m = rotZ(-yaw0).rotate(b.position - a.position);
    if (!(s.axes & kRootAxisX)) m.x = 0.0f;
    if (!(s.axes & kRootAxisY)) m.y = 0.0f;
    if (!(s.axes & kRootAxisZ)) m.z = 0.0f;
    RootDelta d;
    d.move = m;
    d.yaw = (s.axes & kRootAxisYaw) ? wrapPi(yaw1 - yaw0) : 0.0f;
    return d;
}

} // namespace

u32 rootBoneOf(const fmt::OcSkeleton& s) {
    if (s.rootBone < s.bones.size()) return s.rootBone;
    for (usize i = 0; i < s.bones.size(); ++i)
        if (s.bones[i].parent < 0) return static_cast<u32>(i);
    return kNoBone;
}

f32 yawOf(const Quat& q) {
    return wrapPi(2.0f * std::atan2(q.z, q.w));
}

bool clipHasRootMotion(const fmt::OcAnimation& c, const RootSpec& s) {
    if (!s.valid()) return false;
    if (!(c.flags & fmt::kOcAnimRootMotion)) return false;
    if (c.flags & (fmt::kOcAnimAdditiveBase | fmt::kOcAnimObject)) return false;
    return trackOf(c, s.bone) != nullptr;
}

RootDelta rootDeltaBetween(const fmt::OcAnimation& c, const RootSpec& s, f32 t0, f32 t1, bool wrap) {
    if (!clipHasRootMotion(c, s)) return {};
    const fmt::OcTrack* t = trackOf(c, s.bone);
    if (!t) return {};
    if (!(wrap && t1 < t0)) return stepDelta(*t, s, t0, t1);

    RootDelta d = stepDelta(*t, s, t0, c.duration);
    composeDelta(d, stepDelta(*t, s, 0.0f, t1));
    return d;
}

void addWeighted(RootDelta& acc, const RootDelta& d, f32 w) {
    acc.move += d.move * w;
    acc.yaw += d.yaw * w;
}

RootDelta lerpDelta(const RootDelta& a, const RootDelta& b, f32 t) {
    RootDelta r;
    r.move = a.move + (b.move - a.move) * t;
    r.yaw = a.yaw + (b.yaw - a.yaw) * t;
    return r;
}

void composeDelta(RootDelta& acc, const RootDelta& d) {
    acc.move += rotZ(acc.yaw).rotate(d.move);
    acc.yaw += d.yaw;
}

void lockRoot(Pose& p, const RootSpec& s) {
    if (!s.valid() || s.bone >= p.local.size()) return;
    Transform& x = p.local[s.bone];
    if (s.axes & kRootAxisX) x.position.x = s.rest.position.x;
    if (s.axes & kRootAxisY) x.position.y = s.rest.position.y;
    if (s.axes & kRootAxisZ) x.position.z = s.rest.position.z;
    if (s.axes & kRootAxisYaw) {
        const f32 yaw = yawOf(x.rotation);
        const Quat swing = (rotZ(-yaw) * x.rotation).normalized();
        x.rotation = (rotZ(yawOf(s.rest.rotation)) * swing).normalized();
    }
}

} // namespace aver::anim
