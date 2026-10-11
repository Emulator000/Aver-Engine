// Root motion: extraction of a clip's planar root travel, the lock that keeps the mesh on the entity,
// and the plumbing through state machines and blend spaces, with no scene and no GPU.
//
// The failures worth catching are numeric and silent: a delta measured in the wrong frame (the clip's
// X instead of the root's own forward), a loop seam that loses a lap, a lock that eats the pitch of
// the root, or a layer weight applied twice. None shows in a screenshot of any one frame.
#include "aver/anim/AnimGraphAsset.hpp"
#include "aver/anim/AnimSampler.hpp"
#include "aver/anim/AnimStateMachine.hpp"
#include "aver/anim/BlendSpace.hpp"
#include "aver/anim/RootMotion.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static void checkNear(f32 got, f32 want, f32 eps, const std::string& what) {
    if (std::fabs(got - want) <= eps) { AVER_INFO("  ok    {} ({:.5f})", what, got); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {} (got {:.6f}, want {:.6f})", what, got, want);
}

static Quat rotZ(f32 r) { return Quat::fromAxisAngle(Vec3{0, 0, 1}, r); }
static Quat rotY(f32 r) { return Quat::fromAxisAngle(Vec3{0, 1, 0}, r); }

// root (rest at z = 5) -> spine -> hand.
static fmt::OcSkeleton rig() {
    fmt::OcSkeleton s;
    fmt::OcBone root, spine, hand;
    root.name = "root";
    root.translation = Vec3{0, 0, 5};
    spine.name = "spine";
    spine.parent = 0;
    hand.name = "hand";
    hand.parent = 1;
    s.bones = {root, spine, hand};
    s.rootBone = 0;
    return s;
}

static anim::RootSpec specOf(const fmt::OcSkeleton& s, u32 axes = anim::kRootAxesDefault) {
    anim::RootSpec r;
    r.bone = anim::rootBoneOf(s);
    r.axes = axes;
    r.rest = Transform{s.bones[r.bone].translation, s.bones[r.bone].rotation, s.bones[r.bone].scale};
    return r;
}

// A root track from `p0` to `p1` over `dur`, with the heading going from yaw0 to yaw1 when `rot`.
static fmt::OcAnimation rootClip(f32 dur, Vec3 p0, Vec3 p1, bool rot = false, f32 yaw0 = 0.0f, f32 yaw1 = 0.0f,
                                 u8 flags = fmt::kOcAnimLoop | fmt::kOcAnimRootMotion, u16 bone = 0) {
    fmt::OcAnimation c;
    c.duration = dur;
    c.flags = flags;
    fmt::OcTrack t;
    t.boneIndex = bone;
    t.channels = fmt::kOcChannelTranslation | (rot ? fmt::kOcChannelRotation : 0);
    t.times = {0.0f, dur};
    auto key = [&](Vec3 p, f32 yaw) {
        t.values.insert(t.values.end(), {p.x, p.y, p.z});
        if (rot) {
            const Quat q = rotZ(yaw);
            t.values.insert(t.values.end(), {q.x, q.y, q.z, q.w});
        }
    };
    key(p0, yaw0);
    key(p1, yaw1);
    c.tracks.push_back(t);
    return c;
}

static std::map<std::string, fmt::OcAnimation>& clipTable() {
    static std::map<std::string, fmt::OcAnimation> t;
    return t;
}
static const fmt::OcAnimation* findClip(const std::string& n) {
    const auto it = clipTable().find(n);
    return it == clipTable().end() ? nullptr : &it->second;
}

static anim::AsmState clipState(const char* name, const char* clip) {
    anim::AsmState s;
    s.name = name;
    s.asset = clip;
    s.loop = true;
    return s;
}

static anim::BlendSample bsample(const char* clip, f32 x) {
    anim::BlendSample s;
    s.clip = clip;
    s.x = x;
    return s;
}

int main() {
    const fmt::OcSkeleton skel = rig();
    const anim::RootSpec spec = specOf(skel);

    AVER_INFO("rootBoneOf and yawOf");
    {
        check(anim::rootBoneOf(skel) == 0, "the hinted root bone wins");
        fmt::OcSkeleton s2 = skel;
        s2.rootBone = 77;   // out of range: fall back to the first bone without a parent
        check(anim::rootBoneOf(s2) == 0, "an out-of-range hint falls back to the first parentless bone");
        checkNear(anim::yawOf(rotZ(0.5f)), 0.5f, 1e-5f, "yaw of a pure heading");
        checkNear(anim::yawOf(rotZ(-2.0f)), -2.0f, 1e-5f, "a negative heading reads back signed");
        checkNear(anim::yawOf(rotZ(0.7f) * rotY(0.3f)), 0.7f, 1e-5f, "a tilt under the heading does not change the yaw");
    }

    AVER_INFO("rootDeltaBetween: translation");
    {
        const fmt::OcAnimation c = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{100, 0, 5});
        check(anim::clipHasRootMotion(c, spec), "a flagged clip with a root track has root motion");
        const anim::RootDelta d = anim::rootDeltaBetween(c, spec, 0.25f, 0.75f, false);
        checkNear(d.move.x, 50.0f, 1e-3f, "half the slide in half the clip");
        checkNear(d.move.y, 0.0f, 1e-4f, "no sideways motion");
        checkNear(d.yaw, 0.0f, 1e-5f, "no turn");

        const fmt::OcAnimation up = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{100, 0, 105});
        const anim::RootDelta dz = anim::rootDeltaBetween(up, spec, 0.0f, 1.0f, false);
        checkNear(dz.move.x, 100.0f, 1e-3f, "X is extracted");
        checkNear(dz.move.z, 0.0f, 1e-4f, "Z is outside the default axes");
        const anim::RootSpec withZ = specOf(skel, anim::kRootAxesDefault | anim::kRootAxisZ);
        checkNear(anim::rootDeltaBetween(up, withZ, 0.0f, 1.0f, false).move.z, 100.0f, 1e-3f, "Z is extracted when asked for");
        const anim::RootSpec noX = specOf(skel, anim::kRootAxisY | anim::kRootAxisYaw);
        checkNear(anim::rootDeltaBetween(up, noX, 0.0f, 1.0f, false).move.x, 0.0f, 1e-4f, "X is dropped when masked");
    }

    AVER_INFO("rootDeltaBetween: the delta is in the root's own frame");
    {
        const f32 quarter = kPi * 0.5f;
        // Facing +Y the whole way, travelling along +Y: that is straight ahead for the root.
        const fmt::OcAnimation ahead = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{0, 100, 5}, true, quarter, quarter);
        const anim::RootDelta a = anim::rootDeltaBetween(ahead, spec, 0.0f, 1.0f, false);
        checkNear(a.move.x, 100.0f, 1e-2f, "travel along the heading is forward, not the model's +Y");
        checkNear(a.move.y, 0.0f, 1e-2f, "and not sideways");
        // Facing +Y, travelling along model +X: that is to the root's right-hand side (-Y of its frame).
        const fmt::OcAnimation side = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{100, 0, 5}, true, quarter, quarter);
        const anim::RootDelta s = anim::rootDeltaBetween(side, spec, 0.0f, 1.0f, false);
        checkNear(s.move.x, 0.0f, 1e-2f, "model +X is not forward for a root facing +Y");
        checkNear(s.move.y, -100.0f, 1e-2f, "it is -Y of the root's frame");

        const fmt::OcAnimation turn = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{0, 0, 5}, true, 0.0f, quarter);
        checkNear(anim::rootDeltaBetween(turn, spec, 0.0f, 1.0f, false).yaw, quarter, 1e-4f, "a quarter turn reads as a quarter turn");
        // 170 degrees to -170 degrees is a 20 degree turn the short way, not -340.
        const f32 d170 = 170.0f * kDegToRad;
        const fmt::OcAnimation seam = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{0, 0, 5}, true, d170, -d170);
        checkNear(anim::rootDeltaBetween(seam, spec, 0.0f, 1.0f, false).yaw, 20.0f * kDegToRad, 1e-3f,
                  "the yaw delta wraps into (-pi, pi]");
        const anim::RootSpec noYaw = specOf(skel, anim::kRootAxisX | anim::kRootAxisY);
        checkNear(anim::rootDeltaBetween(turn, noYaw, 0.0f, 1.0f, false).yaw, 0.0f, 1e-6f, "yaw is dropped when masked");
    }

    AVER_INFO("rootDeltaBetween: the loop seam");
    {
        const fmt::OcAnimation c = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{100, 0, 5});
        checkNear(anim::rootDeltaBetween(c, spec, 0.75f, 0.25f, true).move.x, 50.0f, 1e-3f,
                  "0.75 -> 0.25 through the seam is 25 + 25");
        checkNear(anim::rootDeltaBetween(c, spec, 0.75f, 0.25f, false).move.x, -50.0f, 1e-3f,
                  "without wrap the same step is a plain backwards step");
        checkNear(anim::rootDeltaBetween(c, spec, 0.25f, 0.75f, true).move.x, 50.0f, 1e-3f, "a forward step is unaffected by wrap");
    }

    AVER_INFO("clips that must not extract");
    {
        fmt::OcAnimation c = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{100, 0, 5}, false, 0, 0, fmt::kOcAnimLoop);
        check(!anim::clipHasRootMotion(c, spec), "the flag is unset");
        check(!anim::rootDeltaBetween(c, spec, 0.0f, 1.0f, false).any(), "an unflagged clip has an empty delta");
        c.flags = fmt::kOcAnimRootMotion | fmt::kOcAnimAdditiveBase;
        check(!anim::clipHasRootMotion(c, spec), "an additive clip is never extracted");
        c.flags = fmt::kOcAnimRootMotion | fmt::kOcAnimObject;
        check(!anim::clipHasRootMotion(c, spec), "an object clip is never extracted");
        c = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{100, 0, 5}, false, 0, 0, fmt::kOcAnimRootMotion, 2);
        check(!anim::clipHasRootMotion(c, spec), "a clip with no track on the root bone has no root motion");
        check(!anim::clipHasRootMotion(c, anim::RootSpec{}), "no spec, no root motion");
    }

    AVER_INFO("lockRoot");
    {
        anim::Pose p;
        anim::restPose(skel, p);
        p.local[0].position = Vec3{50, 60, 70};
        p.local[0].rotation = (rotZ(1.0f) * rotY(0.3f)).normalized();
        p.local[1].position = Vec3{9, 9, 9};
        anim::lockRoot(p, spec);
        checkNear(p.local[0].position.x, 0.0f, 1e-5f, "X is locked to rest");
        checkNear(p.local[0].position.y, 0.0f, 1e-5f, "Y is locked to rest");
        checkNear(p.local[0].position.z, 70.0f, 1e-5f, "Z is left alone when not extracted");
        checkNear(anim::yawOf(p.local[0].rotation), 0.0f, 1e-5f, "the heading is locked to rest");
        const Quat tilt = rotY(0.3f);
        const Quat q = p.local[0].rotation;
        check(std::fabs(q.x - tilt.x) < 1e-5f && std::fabs(q.y - tilt.y) < 1e-5f && std::fabs(q.z - tilt.z) < 1e-5f &&
                  std::fabs(q.w - tilt.w) < 1e-5f,
              "the tilt under the heading survives");
        check(p.local[1].position.x == 9.0f, "other bones are untouched");

        anim::restPose(skel, p);
        p.local[0].position = Vec3{50, 60, 70};
        anim::lockRoot(p, specOf(skel, anim::kRootAxesDefault | anim::kRootAxisZ));
        checkNear(p.local[0].position.z, 5.0f, 1e-5f, "Z is locked to rest when extracted");

        anim::restPose(skel, p);
        p.local[0].position = Vec3{50, 60, 70};
        anim::lockRoot(p, anim::RootSpec{});
        checkNear(p.local[0].position.x, 50.0f, 1e-6f, "an invalid spec is a no-op");
    }

    AVER_INFO("composeDelta, addWeighted, lerpDelta");
    {
        // 90 steps of 10 cm forward, each turning one degree: the closed form of a chord sum.
        anim::RootDelta acc;
        const f32 th = kDegToRad;
        anim::RootDelta step;
        step.move = Vec3{10, 0, 0};
        step.yaw = th;
        for (int i = 0; i < 90; ++i) anim::composeDelta(acc, step);
        const f32 mag = 10.0f * std::sin(90.0f * th * 0.5f) / std::sin(th * 0.5f);
        const f32 ang = 89.0f * th * 0.5f;
        checkNear(acc.move.x, mag * std::cos(ang), 0.05f, "a quarter circle: x matches the closed form");
        checkNear(acc.move.y, mag * std::sin(ang), 0.05f, "a quarter circle: y matches the closed form");
        checkNear(acc.yaw, 90.0f * th, 1e-4f, "and the heading is the sum of the turns");

        anim::RootDelta a, b, mix;
        a.move = Vec3{10, 0, 0};
        a.yaw = 0.4f;
        b.move = Vec3{30, 20, 0};
        b.yaw = -0.2f;
        anim::addWeighted(mix, a, 0.75f);
        anim::addWeighted(mix, b, 0.25f);
        checkNear(mix.move.x, 15.0f, 1e-4f, "weighted sum: x");
        checkNear(mix.move.y, 5.0f, 1e-4f, "weighted sum: y");
        checkNear(mix.yaw, 0.25f, 1e-5f, "weighted sum: yaw");
        const anim::RootDelta l = anim::lerpDelta(a, b, 0.5f);
        checkNear(l.move.x, 20.0f, 1e-4f, "lerp: x");
        checkNear(l.yaw, 0.1f, 1e-5f, "lerp: yaw");
    }

    AVER_INFO("a state machine sums the layer-weighted deltas");
    {
        clipTable()["ra"] = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{100, 0, 5});
        clipTable()["rb"] = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{300, 0, 5});
        anim::AnimStateMachineAsset a;
        a.name = "rm";
        a.params = {{"go", anim::AsmParamType::Float, 0.0f}};
        anim::AsmMachine m;
        m.name = "Root";
        m.states = {clipState("A", "ra"), clipState("B", "rb")};
        anim::AsmTransition t;
        t.from = 0;
        t.to = 1;
        t.blendTime = 1.0f;
        t.conditions = {{"go", anim::AsmOp::Greater, 0.5f}};
        m.transitions.push_back(t);
        a.machines = {m};

        anim::AnimStateMachine sm;
        sm.bind(&a, &findClip, [](const std::string&) -> const anim::BlendSpaceAsset* { return nullptr; });
        sm.setRootMotion(&spec);

        sm.tick(0.1f);
        checkNear(sm.consumeRootDelta().move.x, 10.0f, 1e-3f, "a single state moves at its clip's speed");
        check(!sm.consumeRootDelta().any(), "consuming resets the delta");

        anim::Pose p;
        sm.evaluate(skel, p);
        checkNear(p.local[0].position.x, 0.0f, 1e-5f, "the pose root is locked to rest while the delta travels");

        sm.setFloat("go", 1.0f);
        sm.tick(0.1f);
        checkNear(sm.consumeRootDelta().move.x, 10.0f, 1e-3f, "the tick a transition fires still belongs to the old state");
        check(sm.blending(), "and the blend has begun");
        sm.tick(0.25f);
        // A advanced 25 cm at weight 0.75, B 75 cm at weight 0.25.
        checkNear(sm.consumeRootDelta().move.x, 0.75f * 25.0f + 0.25f * 75.0f, 1e-2f, "mid-blend: the layer-weighted sum");

        anim::AnimStateMachine off;
        off.bind(&a, &findClip, [](const std::string&) -> const anim::BlendSpaceAsset* { return nullptr; });
        off.tick(0.1f);
        check(!off.consumeRootDelta().any(), "with no spec set nothing is extracted");
        off.evaluate(skel, p);
        checkNear(p.local[0].position.x, 10.0f, 1e-3f, "and the pose root is not locked");

        clipTable()["plain"] = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{100, 0, 5}, false, 0, 0, fmt::kOcAnimLoop);
        anim::AnimStateMachineAsset b = a;
        b.machines[0].states[0].asset = "plain";
        anim::AnimStateMachine un;
        un.bind(&b, &findClip, [](const std::string&) -> const anim::BlendSpaceAsset* { return nullptr; });
        un.setRootMotion(&spec);
        un.tick(0.1f);
        check(!un.consumeRootDelta().any(), "a clip without the flag contributes nothing");
        un.evaluate(skel, p);
        checkNear(p.local[0].position.x, 10.0f, 1e-3f, "and is not locked");
    }

    AVER_INFO("a blend space sums the weighted sample deltas");
    {
        const fmt::OcAnimation ca = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{100, 0, 5});
        const fmt::OcAnimation cb = rootClip(1.0f, Vec3{0, 0, 5}, Vec3{300, 0, 5});
        anim::BlendSpaceAsset s;
        s.dims = 1;
        s.axisX = {"x", 0, 1, 0};
        s.samples = {bsample("a", 0.0f), bsample("b", 1.0f)};
        anim::BlendSpacePlayer p;
        p.bind(&s, {&ca, &cb});
        p.setRootMotion(&spec);
        p.setInput(0.25f, 0.0f);
        p.snapInput();
        p.advance(0.1f);
        // 0.75 of 10 cm + 0.25 of 30 cm.
        checkNear(p.consumeRootDelta().move.x, 15.0f, 1e-2f, "weights apply to each sample's delta");
        check(!p.consumeRootDelta().any(), "consuming resets the delta");

        p.setPhase(0.95f);
        p.advance(0.1f);
        checkNear(p.consumeRootDelta().move.x, 15.0f, 1e-2f, "a step through the cycle seam loses no lap");

        anim::Pose pose;
        p.evaluate(skel, pose);
        checkNear(pose.local[0].position.x, 0.0f, 1e-4f, "the posed root is locked");

        anim::BlendSpacePlayer q;
        q.bind(&s, {&ca, &cb});
        q.setInput(0.25f, 0.0f);
        q.snapInput();
        q.advance(0.1f);
        check(!q.consumeRootDelta().any(), "off by default");
    }

    AVER_INFO(g_failures == 0 ? "RootMotionTest: PASS" : "RootMotionTest: FAIL");
    return g_failures == 0 ? 0 : 1;
}
