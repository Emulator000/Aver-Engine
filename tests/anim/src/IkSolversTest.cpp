// IkSolversTest -- FABRIK and CCD chain IK with limits, pole vectors, tip rotation and weights.
//
// THE STANDARD IS OBSERVED GEOMETRY, and the fixture is the one IkTest learned the hard way: a chain
// hanging off a ROTATED parent whose bones carry non-identity REST rotations. A parentless chain with
// identity rests cannot tell a right local<->model conversion from a wrong one (the quaternion product
// commutes and there is no parent frame to conjugate with), so every solver here is checked on the
// parented shape. Reach, bone lengths, bend side and limits are measured from poseToModel, never from
// return codes.
#include "aver/anim/IkSolvers.hpp"
#include "aver/anim/Pose.hpp"
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcAnim.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace aver;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static std::string f2s(f32 v) { char b[64]; std::snprintf(b, sizeof b, "%.3f", v); return b; }
static f32 len(const Vec3& v) { return std::sqrt(dot(v, v)); }
static f32 gap(const Vec3& a, const Vec3& b) { return len(a - b); }

// base (rotated, translated) -> n chain bones, 25 cm apart along the local Z, each with its own rest tilt.
static fmt::OcSkeleton chainSkeleton(int n) {
    fmt::OcSkeleton s;
    fmt::OcBone base; base.name = "base"; base.parent = -1;
    base.translation = Vec3{5, -3, 2};
    base.rotation = (Quat::fromAxisAngle(Vec3{0, 1, 0}, 0.7f) * Quat::fromAxisAngle(Vec3{0, 0, 1}, 0.5f)).normalized();
    s.bones.push_back(base);
    const Vec3 axes[] = {Vec3{1, 0, 0}, Vec3{0, 1, 1}, Vec3{1, 1, 0}, Vec3{0, 1, 0}, Vec3{1, 0, 1}, Vec3{1, 1, 1}, Vec3{0, 0, 1}};
    const f32 angles[] = {0.30f, -0.35f, 0.25f, -0.30f, 0.20f, -0.25f, 0.15f};
    for (int i = 0; i < n; ++i) {
        fmt::OcBone b;
        b.name = "b" + std::to_string(i);
        b.parent = i;
        b.translation = Vec3{0, 0, i == 0 ? 10.0f : 25.0f};
        b.rotation = Quat::fromAxisAngle(axes[i % 7], angles[i % 7]).normalized();
        s.bones.push_back(b);
    }
    s.rootBone = 0;
    return s;
}

static Vec3 posOf(const fmt::OcSkeleton& s, const anim::Pose& p, u32 bone) {
    std::vector<Mat4> m;
    anim::poseToModel(s, p, m);
    return Vec3{m[bone].m[3][0], m[bone].m[3][1], m[bone].m[3][2]};
}

static std::vector<f32> segmentLengths(const fmt::OcSkeleton& s, const anim::Pose& p, const anim::IkChain& c) {
    std::vector<f32> out;
    for (usize i = 0; i + 1 < c.bones.size(); ++i) out.push_back(gap(posOf(s, p, c.bones[i + 1]), posOf(s, p, c.bones[i])));
    return out;
}

static bool lengthsHeld(const std::vector<f32>& a, const std::vector<f32>& b) {
    if (a.size() != b.size()) return false;
    for (usize i = 0; i < a.size(); ++i)
        if (std::fabs(a[i] - b[i]) > 0.1f) return false;
    return true;
}

// Angle in degrees between two unit quaternions.
static f32 angleBetween(const Quat& a, const Quat& b) {
    const f32 d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.0f * std::acos(d > 1.0f ? 1.0f : d) * kRadToDeg;
}

static Quat modelRot(const fmt::OcSkeleton& s, const anim::Pose& p, u32 bone) {
    std::vector<Mat4> m;
    anim::poseToModel(s, p, m);
    return transformFromMatrix(m[bone]).rotation.normalized();
}

static bool samePose(const anim::Pose& a, const anim::Pose& b) {
    if (a.local.size() != b.local.size()) return false;
    for (usize i = 0; i < a.local.size(); ++i) {
        const Transform &x = a.local[i], &y = b.local[i];
        if (x.position.x != y.position.x || x.position.y != y.position.y || x.position.z != y.position.z ||
            x.rotation.x != y.rotation.x || x.rotation.y != y.rotation.y || x.rotation.z != y.rotation.z ||
            x.rotation.w != y.rotation.w)
            return false;
    }
    return true;
}

// Bend of the chain: the mean of the intermediate joints off the root-tip axis, as a unit vector.
static Vec3 bendOf(const fmt::OcSkeleton& s, const anim::Pose& p, const anim::IkChain& c) {
    const Vec3 r = posOf(s, p, c.bones.front());
    const Vec3 t = posOf(s, p, c.bones.back());
    const Vec3 ax = (t - r).getSafeNormal();
    Vec3 m{0, 0, 0};
    for (usize i = 1; i + 1 < c.bones.size(); ++i) m += posOf(s, p, c.bones[i]) - r;
    m = m * (1.0f / static_cast<f32>(c.bones.size() - 2));
    return (m - ax * dot(m, ax)).getSafeNormal();
}

// Swing angle (degrees) of a local rotation off the rest, about `axis`, and the twist angle.
static void swingTwistDeg(const Quat& rest, const Quat& local, const Vec3& axis, f32& swing, f32& twist) {
    const Quat restInv{-rest.x, -rest.y, -rest.z, rest.w};
    const Quat q = (restInv * local).normalized();
    const Vec3 a = axis.getSafeNormal();
    const Vec3 proj = a * dot(Vec3{q.x, q.y, q.z}, a);
    Quat t{proj.x, proj.y, proj.z, q.w};
    const f32 n = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z + t.w * t.w);
    t = Quat{t.x / n, t.y / n, t.z / n, t.w / n};
    const Quat tInv{-t.x, -t.y, -t.z, t.w};
    const Quat sw = (q * tInv).normalized();
    const Vec3 d = sw.rotate(a);
    swing = std::acos(std::fmax(-1.0f, std::fmin(1.0f, dot(a, d)))) * kRadToDeg;
    f32 tw = 2.0f * std::atan2(dot(Vec3{t.x, t.y, t.z}, a), t.w) * kRadToDeg;
    while (tw > 180.0f) tw -= 360.0f;
    while (tw < -180.0f) tw += 360.0f;
    twist = std::fabs(tw);
}

static anim::IkChain fullChain(const fmt::OcSkeleton& s) {
    anim::IkChain c;
    check(anim::buildChain(s, 1, static_cast<u32>(s.bones.size() - 1), c), "buildChain over the whole chain");
    return c;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO("chain IK solvers");

    const fmt::OcSkeleton s5 = chainSkeleton(5);     // base + 5 chain bones: four 25 cm segments, reach 100
    anim::IkScratch scratch;

    AVER_INFO("=== buildChain, valid ===");
    {
        anim::IkChain c;
        check(anim::buildChain(s5, 1, 5, c) && c.bones.size() == 5 && c.bones.front() == 1 && c.bones.back() == 5,
              "root-first, start and end inclusive");
        check(c.valid(s5), "and it is valid");
        check(!anim::buildChain(s5, 5, 1, c), "an end that is above the start is refused");
        check(!anim::buildChain(s5, 3, 3, c), "so is start == end");
        check(!anim::buildChain(s5, 1, 99, c), "so is an out-of-range index");
        anim::IkChain bad;
        bad.bones = {1, 3, 4};
        check(!bad.valid(s5), "a chain that skips a bone is not parent-linked");
    }

    AVER_INFO("=== defaultLimitAxis follows the ragdoll rule: toward the longest child, +X for a leaf ===");
    {
        fmt::OcSkeleton t;
        fmt::OcBone r; r.name = "r"; r.parent = -1;
        fmt::OcBone shortC; shortC.name = "s"; shortC.parent = 0; shortC.translation = Vec3{0, 5, 0};
        fmt::OcBone longC;  longC.name = "l";  longC.parent = 0;  longC.translation = Vec3{0, 0, 30};
        t.bones = {r, shortC, longC};
        Vec3 a{0, 0, 0};
        anim::defaultLimitAxis(t, 0, a);
        check(gap(a, Vec3{0, 0, 1}) < 1e-5f, "the longer child sets the axis");
        anim::defaultLimitAxis(t, 2, a);
        check(gap(a, Vec3{1, 0, 0}) < 1e-5f, "a leaf is +X");
    }

    for (const anim::IkSolverKind kind : {anim::IkSolverKind::Fabrik, anim::IkSolverKind::Ccd}) {
        const std::string name = kind == anim::IkSolverKind::Fabrik ? "FABRIK" : "CCD";
        AVER_INFO("=== {} ===", name);
        const anim::IkChain chain = fullChain(s5);
        anim::IkParams params;
        params.kind = kind;
        params.maxIterations = 80;
        params.toleranceCm = 0.05f;
        params.maxStepDeg = 30.0f;

        anim::Pose rest;
        anim::restPose(s5, rest);
        const std::vector<f32> lens0 = segmentLengths(s5, rest, chain);
        f32 total = 0.0f;
        for (const f32 l : lens0) total += l;
        const Vec3 p0 = posOf(s5, rest, chain.bones.front());

        const Vec3 dirA = Vec3{0.5f, 0.7f, 0.4f}.getSafeNormal();
        const Vec3 goal = p0 + dirA * (0.7f * total);

        {
            anim::Pose p = rest;
            anim::IkGoal g;
            g.position = goal;
            const anim::IkResult r = anim::solveChain(s5, p, chain, g, params, scratch);
            check(r.solved, name + ": it solves on a parented chain with rotated rests");
            const f32 miss = gap(posOf(s5, p, chain.bones.back()), goal);
            check(miss < 0.3f, name + ": the tip lands on the goal, missing by " + f2s(miss) + " cm");
            check(r.reached && std::fabs(r.errorCm - miss) < 0.05f, name + ": the result reports what was measured");
            check(lengthsHeld(lens0, segmentLengths(s5, p, chain)), name + ": every bone kept its length (0.1 cm)");
            check(gap(posOf(s5, p, chain.bones.front()), p0) < 1e-3f, name + ": the chain root did not translate");
        }

        {
            anim::Pose p = rest;
            anim::IkGoal g;
            g.position = p0 + dirA * (total * 1.8f);
            const anim::IkResult r = anim::solveChain(s5, p, chain, g, params, scratch);
            check(r.solved && !r.reached, name + ": an unreachable goal still solves, and says it was not reached");
            check(std::fabs(r.errorCm - (total * 0.8f)) < 0.6f,
                  name + ": the error is |goal| - reach, got " + f2s(r.errorCm) + " vs " + f2s(total * 0.8f));
            check(lengthsHeld(lens0, segmentLengths(s5, p, chain)), name + ": and nothing stretched to get there");
            const Vec3 tip = posOf(s5, p, chain.bones.back());
            check(gap(tip, p0 + dirA * total) < 1.0f, name + ": the chain is straight AT the goal, off by " + f2s(gap(tip, p0 + dirA * total)));
        }

        {
            // weight 0.5 is the per-bone slerp of the weight-1 solve (IK is not linear in its goal).
            anim::Pose full = rest, half = rest;
            anim::IkGoal g;
            g.position = goal;
            anim::solveChain(s5, full, chain, g, params, scratch);
            g.weight = 0.5f;
            const anim::IkResult r = anim::solveChain(s5, half, chain, g, params, scratch);
            check(r.solved, name + ": a half-weight solve runs");
            bool same = true;
            for (const u32 b : chain.bones) {
                const Quat want = Quat::slerp(rest.local[b].rotation, full.local[b].rotation, 0.5f).normalized();
                if (angleBetween(want, half.local[b].rotation) > 0.05f) same = false;
            }
            check(same, name + ": every chain bone is the slerp halfway between the input and the full solve");
            anim::Pose zero = rest;
            g.weight = 0.0f;
            check(!anim::solveChain(s5, zero, chain, g, params, scratch).solved && samePose(zero, rest),
                  name + ": weight 0 leaves the pose untouched");
        }

        {
            // The pole puts the bend on its side, and the mirrored pole mirrors it.
            const Vec3 mid = p0 + dirA * (0.35f * total);
            Vec3 perp = cross(dirA, Vec3{0, 0, 1});
            if (len(perp) < 0.1f) perp = cross(dirA, Vec3{1, 0, 0});
            perp = perp.getSafeNormal();
            anim::Pose a = rest, b = rest;
            anim::IkGoal g;
            g.position = goal;
            g.hasPole = true;
            g.pole = mid + perp * 80.0f;
            anim::solveChain(s5, a, chain, g, params, scratch);
            g.pole = mid - perp * 80.0f;
            anim::solveChain(s5, b, chain, g, params, scratch);
            const Vec3 ba = bendOf(s5, a, chain), bb = bendOf(s5, b, chain);
            check(dot(ba, perp) > 0.95f, name + ": the bend faces the pole, cos " + f2s(dot(ba, perp)));
            check(dot(bb, perp) < -0.95f, name + ": the mirrored pole mirrors it, cos " + f2s(dot(bb, perp)));
            check(gap(posOf(s5, a, chain.bones.back()), goal) < 0.4f && gap(posOf(s5, b, chain.bones.back()), goal) < 0.4f,
                  name + ": both still reach the goal");
            check(lengthsHeld(lens0, segmentLengths(s5, a, chain)), name + ": the roll preserved the lengths");
        }

        {
            // No pole: the bend the input pose already had survives a modest move of the goal.
            const Vec3 tip0 = posOf(s5, rest, chain.bones.back());
            const Vec3 before = bendOf(s5, rest, chain);
            anim::Pose p = rest;
            anim::IkGoal g;
            g.position = tip0 + Vec3{4, 3, -4};
            anim::solveChain(s5, p, chain, g, params, scratch);
            check(dot(bendOf(s5, p, chain), before) > 0.5f, name + ": without a pole the input bend side is kept");
        }

        {
            // Tip rotation reaches within a degree at weight 1.
            const Quat want = (Quat::fromAxisAngle(Vec3{0.3f, 0.8f, 0.5f}, 1.1f)).normalized();
            anim::Pose p = rest;
            anim::IkGoal g;
            g.position = goal;
            g.hasRotation = true;
            g.rotation = want;
            anim::solveChain(s5, p, chain, g, params, scratch);
            const f32 a = angleBetween(modelRot(s5, p, chain.bones.back()), want);
            check(a < 1.0f, name + ": the tip bone's model rotation is within " + f2s(a) + " degrees of the request");
            check(gap(posOf(s5, p, chain.bones.back()), goal) < 0.4f, name + ": and the position still lands");
        }

        {
            // Degenerate inputs leave the pose alone.
            anim::Pose p = rest;
            anim::IkGoal g;
            g.position = p0;
            check(!anim::solveChain(s5, p, chain, g, params, scratch).solved && samePose(p, rest), name + ": a goal on the root");
            anim::IkChain bad = chain;
            bad.bones = {1, 3, 4};
            g.position = goal;
            check(!anim::solveChain(s5, p, bad, g, params, scratch).solved && samePose(p, rest), name + ": a chain that is not linked");
            anim::Pose wrong;
            wrong.local.resize(2);
            check(!anim::solveChain(s5, wrong, chain, g, params, scratch).solved, name + ": a pose that does not match the skeleton");
            fmt::OcSkeleton zero = s5;
            zero.bones[3].translation = Vec3{0, 0, 0};
            anim::Pose zp;
            anim::restPose(zero, zp);
            anim::Pose zp0 = zp;
            check(!anim::solveChain(zero, zp, chain, g, params, scratch).solved && samePose(zp, zp0), name + ": a zero-length bone");
        }

        {
            // Limits: tight swing and twist on the middle bones, a goal that wants far more.
            anim::IkChain lim = chain;
            lim.limits.resize(lim.bones.size());
            for (usize i = 0; i < lim.bones.size(); ++i) {
                lim.limits[i].enabled = true;
                lim.limits[i].swing1Deg = 40.0f;
                lim.limits[i].swing2Deg = 40.0f;
                lim.limits[i].twistDeg = 30.0f;
                anim::defaultLimitAxis(s5, lim.bones[i], lim.limits[i].axis);
            }
            anim::Pose p = rest;
            anim::IkGoal g;
            g.position = p0 + Vec3{-0.3f, 0.2f, 0.9f}.getSafeNormal() * (0.55f * total);
            anim::Pose pFree = rest;
            const anim::IkResult rFree = anim::solveChain(s5, pFree, chain, g, params, scratch);
            const anim::IkResult rLim = anim::solveChain(s5, p, lim, g, params, scratch);
            check(rLim.solved, name + ": a limited solve runs");
            bool within = true;
            f32 worstSwing = 0.0f, worstTwist = 0.0f;
            for (usize i = 0; i + 1 < lim.bones.size(); ++i) {
                f32 sw = 0.0f, tw = 0.0f;
                swingTwistDeg(s5.bones[lim.bones[i]].rotation, p.local[lim.bones[i]].rotation, lim.limits[i].axis, sw, tw);
                worstSwing = std::fmax(worstSwing, sw);
                worstTwist = std::fmax(worstTwist, tw);
                if (sw > 40.5f || tw > 30.5f) within = false;
            }
            check(within, name + ": no bone exceeds its limit (worst swing " + f2s(worstSwing) + ", twist " + f2s(worstTwist) + ")");
            check(lengthsHeld(lens0, segmentLengths(s5, p, chain)), name + ": limited, and still rigid");
            check(rFree.errorCm <= rLim.errorCm + 0.05f, name + ": limits off is never worse than limits on at reaching");
        }
    }

    AVER_INFO("=== the elliptic cone bounds each direction ===");
    {
        anim::IkJointLimit l;
        l.enabled = true;
        l.swing1Deg = 60.0f;      // toward bone-local +Z
        l.swing2Deg = 15.0f;      // toward the other perpendicular (-Y for an X axis)
        l.twistDeg = 180.0f;
        l.axis = Vec3{1, 0, 0};
        const Quat rest = Quat::fromAxisAngle(Vec3{0, 1, 0}, 0.4f).normalized();
        auto swingAngle = [&](const Quat& q) {
            const Quat restInv{-rest.x, -rest.y, -rest.z, rest.w};
            return std::acos(std::fmax(-1.0f, std::fmin(1.0f, dot(Vec3{1, 0, 0}, (restInv * q).normalized().rotate(Vec3{1, 0, 0}))))) * kRadToDeg;
        };
        // Tilt the axis toward +Z by 80 degrees (a rotation about -Y takes +X toward +Z).
        Quat q1 = (rest * Quat::fromAxisAngle(Vec3{0, -1, 0}, 80.0f * kDegToRad)).normalized();
        check(anim::clampJointRotation(l, rest, q1), "80 degrees toward swing1 is clamped");
        check(std::fabs(swingAngle(q1) - 60.0f) < 0.2f, "to the swing1 half-angle, got " + f2s(swingAngle(q1)));
        // Toward the swing2 side by 40 degrees (a rotation about +Z takes +X toward +Y, which is -e2: same bound).
        Quat q2 = (rest * Quat::fromAxisAngle(Vec3{0, 0, 1}, 40.0f * kDegToRad)).normalized();
        check(anim::clampJointRotation(l, rest, q2), "40 degrees toward swing2 is clamped");
        check(std::fabs(swingAngle(q2) - 15.0f) < 0.2f, "to the swing2 half-angle, got " + f2s(swingAngle(q2)));
        // Halfway between: 45 degrees round, bound = 1/sqrt((cos/s1)^2 + (sin/s2)^2) = 20.58 degrees.
        const Vec3 dir = (Vec3{0, 0, 1} * 0.70710678f + Vec3{0, -1, 0} * 0.70710678f);
        const Quat tilt = Quat::fromAxisAngle(cross(Vec3{1, 0, 0}, dir), 50.0f * kDegToRad);
        Quat q3 = (rest * tilt).normalized();
        check(anim::clampJointRotation(l, rest, q3), "50 degrees along the diagonal is clamped");
        check(std::fabs(swingAngle(q3) - 20.58f) < 0.3f, "to the ellipse bound 20.58, got " + f2s(swingAngle(q3)));
        // Inside the cone nothing moves; idempotent after a clamp.
        Quat q4 = (rest * Quat::fromAxisAngle(Vec3{0, -1, 0}, 10.0f * kDegToRad)).normalized();
        const Quat q4Before = q4;
        check(!anim::clampJointRotation(l, rest, q4) && angleBetween(q4, q4Before) < 1e-3f, "a rotation inside the cone is not touched");
        Quat again = q1;
        const bool moved = anim::clampJointRotation(l, rest, again);
        check(!moved || angleBetween(again, q1) < 0.01f, "clamping a clamped rotation changes nothing (idempotent)");
        // Twist.
        anim::IkJointLimit t = l;
        t.swing1Deg = 180.0f; t.swing2Deg = 180.0f; t.twistDeg = 20.0f;
        Quat q5 = (rest * Quat::fromAxisAngle(Vec3{1, 0, 0}, 50.0f * kDegToRad)).normalized();
        check(anim::clampJointRotation(t, rest, q5), "50 degrees of twist against a 20 degree limit is clamped");
        f32 sw = 0.0f, tw = 0.0f;
        swingTwistDeg(rest, q5, Vec3{1, 0, 0}, sw, tw);
        check(std::fabs(tw - 20.0f) < 0.3f && sw < 0.3f, "to the twist limit with no swing, got " + f2s(tw) + " / " + f2s(sw));
        // Limits off, and a fully open limit, change nothing.
        anim::IkJointLimit off;
        Quat q6 = Quat::fromAxisAngle(Vec3{1, 1, 0}, 2.0f).normalized();
        check(!anim::clampJointRotation(off, rest, q6), "a disabled limit does not clamp");
        off.enabled = true;
        check(!anim::clampJointRotation(off, rest, q6), "and neither does a 180/180/180 one");
    }

    AVER_INFO("=== CCD maxStepDeg bounds the first iteration ===");
    {
        const anim::IkChain chain = fullChain(s5);
        anim::Pose rest;
        anim::restPose(s5, rest);
        anim::IkParams params;
        params.kind = anim::IkSolverKind::Ccd;
        params.maxIterations = 1;
        params.maxStepDeg = 5.0f;
        params.toleranceCm = 0.001f;
        anim::IkGoal g;
        g.position = posOf(s5, rest, chain.bones.front()) + Vec3{-60, 40, 30};
        anim::Pose p = rest;
        anim::solveChain(s5, p, chain, g, params, scratch);
        f32 worst = 0.0f;
        for (const u32 b : chain.bones) worst = std::fmax(worst, angleBetween(rest.local[b].rotation, p.local[b].rotation));
        check(worst <= 5.05f && worst > 0.5f, "no joint turned more than the cap in one sweep, worst " + f2s(worst));
    }

    AVER_INFO("=== two-bone through solveChain ===");
    {
        const fmt::OcSkeleton s3 = chainSkeleton(3);
        anim::IkChain c;
        anim::buildChain(s3, 1, 3, c);
        anim::Pose p;
        anim::restPose(s3, p);
        anim::IkParams params;
        params.kind = anim::IkSolverKind::TwoBone;
        anim::IkGoal g;
        const Vec3 p0 = posOf(s3, p, 1);
        g.position = p0 + Vec3{15, 20, 18};
        const anim::IkResult r = anim::solveChain(s3, p, c, g, params, scratch);
        check(r.solved && r.reached && gap(posOf(s3, p, 3), g.position) < 0.3f, "a 3-bone chain reaches through the analytic solver");
        anim::Pose q;
        anim::restPose(s5, q);
        check(!anim::solveChain(s5, q, fullChain(s5), g, params, scratch).solved, "twobone on 5 bones is refused");
    }

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
