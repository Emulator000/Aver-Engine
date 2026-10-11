// Shared fixtures for the motion-matching tests: a four-bone rig, synthetic locomotion clips whose root
// travel is known in closed form, and a clip table. Header-only; every test is its own executable.
#pragma once

#include "aver/anim/AnimSampler.hpp"
#include "aver/anim/MotionAsset.hpp"
#include "aver/anim/PoseDatabase.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace mt {

using namespace aver;

inline int g_failures = 0;

inline void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

inline void checkNear(f32 got, f32 want, f32 eps, const std::string& what) {
    if (std::fabs(got - want) <= eps) { AVER_INFO("  ok    {} ({:.5f})", what, got); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {} (got {:.6f}, want {:.6f})", what, got, want);
}

inline Quat rotZ(f32 r) { return Quat::fromAxisAngle(Vec3{0, 0, 1}, r); }

// root -> hips (0,0,100) -> foot_l (0,-15,-100), foot_r (0,15,-100). Z up, X forward, Y right.
inline fmt::OcSkeleton rig() {
    fmt::OcSkeleton s;
    fmt::OcBone root, hips, fl, fr;
    root.name = "root";
    hips.name = "hips";
    hips.parent = 0;
    hips.translation = Vec3{0, 0, 100};
    fl.name = "foot_l";
    fl.parent = 1;
    fl.translation = Vec3{0, -15, -100};
    fr.name = "foot_r";
    fr.parent = 1;
    fr.translation = Vec3{0, 15, -100};
    s.bones = {root, hips, fl, fr};
    s.rootBone = 0;
    return s;
}

struct RootKey { f32 t; Vec3 p; f32 yaw; };

// A clip with a keyed root (translation + heading) and, when swing > 0, feet that swing +-swing cm along
// the heading in a triangle wave of period `dur` (foot_l first forward). NO kOcAnimRootMotion flag: a
// motion database must read the travel anyway.
inline fmt::OcAnimation makeClip(f32 dur, bool loop, const std::vector<RootKey>& keys, f32 swing = 0.0f) {
    fmt::OcAnimation c;
    c.duration = dur;
    c.flags = loop ? fmt::kOcAnimLoop : 0;
    fmt::OcTrack r;
    r.boneIndex = 0;
    r.channels = fmt::kOcChannelTranslation | fmt::kOcChannelRotation;
    for (const RootKey& k : keys) {
        r.times.push_back(k.t);
        const Quat q = rotZ(k.yaw);
        r.values.insert(r.values.end(), {k.p.x, k.p.y, k.p.z, q.x, q.y, q.z, q.w});
    }
    c.tracks.push_back(r);
    if (swing > 0.0f) {
        for (int side = 0; side < 2; ++side) {
            fmt::OcTrack t;
            t.boneIndex = static_cast<u16>(2 + side);
            t.channels = fmt::kOcChannelTranslation;
            const f32 y = side == 0 ? -15.0f : 15.0f;
            const f32 a = side == 0 ? -swing : swing;
            t.times = {0.0f, dur * 0.5f, dur};
            t.values = {a, y, -100.0f, -a, y, -100.0f, a, y, -100.0f};
            c.tracks.push_back(t);
        }
    }
    return c;
}

// Straight travel along the heading at `speed` cm/s.
inline fmt::OcAnimation walkClip(f32 dur, f32 speed, bool loop = true, f32 swing = 10.0f, f32 yaw = 0.0f) {
    const Vec3 dir{std::cos(yaw), std::sin(yaw), 0.0f};
    return makeClip(dur, loop, {{0.0f, {0, 0, 0}, yaw}, {dur, dir * (speed * dur), yaw}}, swing);
}

inline fmt::OcAnimation idleClip(f32 dur = 2.0f) { return makeClip(dur, true, {{0.0f, {0, 0, 0}, 0.0f}, {dur, {0, 0, 0}, 0.0f}}, 0.0f); }

// A closed circle of radius `radius` driven at `omega` rad/s, keyed every 1/30 s so linear interpolation
// stays within a fraction of a centimetre of the true arc.
inline fmt::OcAnimation circleClip(f32 radius, f32 omega, bool loop = true) {
    const f32 dur = kTwoPi / omega;
    const int n = static_cast<int>(std::lround(dur * 30.0f));
    std::vector<RootKey> keys;
    for (int i = 0; i <= n; ++i) {
        const f32 th = omega * dur * static_cast<f32>(i) / static_cast<f32>(n);
        keys.push_back({dur * static_cast<f32>(i) / static_cast<f32>(n),
                        {radius * std::sin(th), radius * (1.0f - std::cos(th)), 0.0f}, th});
    }
    return makeClip(dur, loop, keys, 0.0f);
}

class ClipTable {
public:
    void add(const std::string& name, fmt::OcAnimation c) { clips_[name] = std::move(c); }
    const fmt::OcAnimation* find(const std::string& n) const {
        const auto it = clips_.find(n);
        return it == clips_.end() ? nullptr : &it->second;
    }
    anim::MotionClipFn fn() const { return [this](const std::string& n) { return find(n); }; }
    std::map<std::string, fmt::OcAnimation>& all() { return clips_; }

private:
    std::map<std::string, fmt::OcAnimation> clips_;
};

inline anim::MotionClipEntry entry(const std::string& clip, std::vector<std::string> tags = {}, f32 bias = 0.0f) {
    anim::MotionClipEntry e;
    e.clip = clip;
    e.tags = std::move(tags);
    e.bias = bias;
    return e;
}

// feet pos + vel, root velocity, trajectory: the default feature layout.
inline anim::MotionAsset baseAsset() {
    anim::MotionAsset a;
    a.name = "Test";
    a.skeleton = "rig.ocskel";
    a.sampleRate = 30;
    a.tags = {"idle", "walk", "run", "stop"};
    a.bones = {{"foot_l", 1.0f, 1.0f}, {"foot_r", 1.0f, 1.0f}};
    return a;
}

// idle / walk 150 / run 400, each a loop with root travel, one tag each.
inline anim::MotionAsset locomotionAsset(ClipTable& t) {
    t.add("idle", idleClip());
    t.add("walk", walkClip(1.0f, 150.0f, true, 10.0f));
    t.add("run", walkClip(0.8f, 400.0f, true, 20.0f));
    anim::MotionAsset a = baseAsset();
    a.clips = {entry("idle", {"idle"}), entry("walk", {"walk"}), entry("run", {"run"})};
    return a;
}

// The raw (un-normalised) feature value `dim` of a database row.
inline std::vector<f32> rawRow(const anim::PoseDatabase& db, u32 row) {
    std::vector<f32> out(db.dimPadded());
    db.denormalise(db.row(row), out.data());
    return out;
}

inline const anim::FeatureGroup* groupOf(const anim::PoseDatabase& db, anim::FeatureKind k, u32 bone = 0xFFFFFFFFu) {
    for (const anim::FeatureGroup& g : db.groups())
        if (g.kind == k && g.bone == bone) return &g;
    return nullptr;
}

} // namespace mt
