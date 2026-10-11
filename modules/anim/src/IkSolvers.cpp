// FABRIK and CCD chain solvers with joint limits. See IkSolvers.hpp for the contract; the conventions
// (Hamilton quaternions, row-vector matrices, setModelRotation) live in IkMath.hpp.
#include "aver/anim/IkSolvers.hpp"

#include "IkMath.hpp"
#include "aver/anim/Ik.hpp"
#include "aver/formats/OcAnim.hpp"

#include <algorithm>
#include <cmath>

namespace aver::anim {

using namespace ikm;

namespace {

f32 clamp01f(f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

f32 wrapPi(f32 a) {
    while (a > kPi) a -= kTwoPi;
    while (a <= -kPi) a += kTwoPi;
    return a;
}

// The swing frame of a limit: `a` the twist axis, e1 bone-local +Z (+Y when the axis is within 5 degrees
// of Z), e2 = a x e1. Same choice as physics_ragdoll_abi.h, so a limit means the same in IK and ragdoll.
struct LimitBasis { Vec3 a, e1, e2; };

LimitBasis basisOf(const Vec3& axisIn) {
    LimitBasis b;
    b.a = axisIn.getSafeNormal();
    if (b.a.sizeSquared() < 0.5f) b.a = Vec3{1, 0, 0};
    const Vec3 ref = std::fabs(b.a.z) > 0.99619f ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    b.e1 = (ref - b.a * dot(ref, b.a)).getSafeNormal();
    b.e2 = cross(b.a, b.e1);
    return b;
}

// Bounds unit direction `d` to the elliptic cone (half-angles s1 toward e1, s2 toward e2, radians) about
// b.a. thetaMax(phi) = 1 / sqrt((cos phi / s1)^2 + (sin phi / s2)^2). True when it moved.
bool clampCone(const LimitBasis& b, f32 s1, f32 s2, Vec3& d) {
    const f32 c = std::clamp(dot(b.a, d), -1.0f, 1.0f);
    const f32 theta = std::acos(c);
    if (theta < 1e-5f) return false;
    const f32 phi = std::atan2(dot(d, b.e2), dot(d, b.e1));
    const f32 cp = std::cos(phi), sp = std::sin(phi);
    const f32 a1 = std::max(s1, 1e-4f), a2 = std::max(s2, 1e-4f);
    const f32 inv = (cp / a1) * (cp / a1) + (sp / a2) * (sp / a2);
    const f32 thetaMax = std::min(1.0f / std::sqrt(inv), kPi);
    if (theta <= thetaMax + 1e-4f) return false;
    d = b.a * std::cos(thetaMax) + (b.e1 * cp + b.e2 * sp) * std::sin(thetaMax);
    return true;
}

bool unlimited(const IkJointLimit& l) {
    return !l.enabled || (l.swing1Deg >= 179.99f && l.swing2Deg >= 179.99f && l.twistDeg >= 179.99f);
}

void refresh(const fmt::OcSkeleton& skel, const Pose& pose, std::vector<Mat4>& model,
             const std::vector<u32>& bones, usize from) {
    for (usize k = from; k < bones.size(); ++k) {
        const u32 b = bones[k];
        const i32 p = skel.bones[b].parent;
        const Mat4 lm = pose.local[b].toMatrix();
        model[b] = p >= 0 ? lm * model[static_cast<usize>(p)] : lm;
    }
}

void clampBone(const fmt::OcSkeleton& skel, Pose& pose, const IkChain& chain, usize i) {
    if (i >= chain.limits.size() || unlimited(chain.limits[i])) return;
    const u32 b = chain.bones[i];
    Quat l = pose.local[b].rotation;
    if (clampJointRotation(chain.limits[i], skel.bones[b].rotation, l)) pose.local[b].rotation = l;
}

// Rotates the chain bones, root to tip, so each segment points at the next TARGET position. Swing only:
// the animated pose's twist survives. The direction is measured from the bone's actual joint (not the
// target's), so a joint that a limit held back is compensated by the bones below it.
void writeBack(const fmt::OcSkeleton& skel, Pose& pose, const IkChain& chain,
               std::vector<Mat4>& model, const std::vector<Vec3>& target) {
    const usize n = chain.bones.size();
    for (usize i = 0; i + 1 < n; ++i) {
        const u32 b = chain.bones[i];
        const Vec3 pi = originOf(model[b]);
        const Vec3 pn = originOf(model[chain.bones[i + 1]]);
        const Vec3 cur = (pn - pi).getSafeNormal();
        const Vec3 want = (target[i + 1] - pi).getSafeNormal();
        if (cur.sizeSquared() > 0.5f && want.sizeSquared() > 0.5f) {
            const Quat delta = arc(cur, want);
            setModelRotation(skel, pose, model, b, (delta * modelRotation(model[b])).normalized());
        }
        clampBone(skel, pose, chain, i);
        refresh(skel, pose, model, chain.bones, i);
    }
}

// Rolls the intermediate joints about the root-tip axis so the chain's bend faces the pole.
void applyPole(std::vector<Vec3>& pos, const Vec3& pole, f32 weight) {
    const usize n = pos.size();
    if (n < 3 || weight <= 0.0f) return;
    Vec3 ax = pos[n - 1] - pos[0];
    const f32 L = lengthOf(ax);
    if (L < kEps) return;
    ax = ax * (1.0f / L);
    Vec3 m{0, 0, 0};
    for (usize i = 1; i + 1 < n; ++i) m += pos[i] - pos[0];
    m = m * (1.0f / static_cast<f32>(n - 2));
    const Vec3 mp = m - ax * dot(m, ax);
    Vec3 pp = pole - pos[0];
    pp = pp - ax * dot(pp, ax);
    if (lengthOf(mp) < 1e-4f || lengthOf(pp) < 1e-4f) return;
    const f32 angle = std::atan2(dot(ax, cross(mp, pp)), dot(mp, pp)) * weight;
    const Quat q = Quat::fromAxisAngle(ax, angle);
    for (usize i = 1; i + 1 < n; ++i) pos[i] = pos[0] + q.rotate(pos[i] - pos[0]);
}

void applyTipRotation(const fmt::OcSkeleton& skel, Pose& pose, const IkChain& chain,
                      std::vector<Mat4>& model, const IkGoal& goal) {
    if (!goal.hasRotation || goal.rotationWeight <= 0.0f) return;
    const u32 t = chain.bones.back();
    const Quat cur = modelRotation(model[t]);
    const Quat want = Quat::slerp(cur, goal.rotation.normalized(), clamp01f(goal.rotationWeight));
    setModelRotation(skel, pose, model, t, want.normalized());
    refresh(skel, pose, model, chain.bones, chain.bones.size() - 1);
}

IkResult finish(const IkChain& chain, const std::vector<Mat4>& model, const Vec3& goal, const IkParams& p,
                u32 iterations) {
    IkResult r;
    r.solved = true;
    r.iterations = iterations;
    r.errorCm = lengthOf(originOf(model[chain.bones.back()]) - goal);
    r.reached = r.errorCm <= std::max(p.toleranceCm, 0.1f);
    return r;
}

IkResult solveFabrik(const fmt::OcSkeleton& skel, Pose& pose, const IkChain& chain, const IkGoal& goal,
                     const IkParams& params, IkScratch& s) {
    const auto& bones = chain.bones;
    const usize n = bones.size();
    poseToModel(skel, pose, s.model);
    s.pos.resize(n);
    s.len.resize(n - 1);
    for (usize i = 0; i < n; ++i) s.pos[i] = originOf(s.model[bones[i]]);
    f32 total = 0.0f;
    for (usize i = 0; i + 1 < n; ++i) {
        s.len[i] = lengthOf(s.pos[i + 1] - s.pos[i]);
        if (s.len[i] < kEps) return {};
        total += s.len[i];
    }
    const Vec3 root = s.pos[0];
    const Vec3 toGoal = goal.position - root;
    const f32 dist = lengthOf(toGoal);
    if (dist < kEps) return {};

    bool limited = false;
    for (usize i = 0; i + 1 < n && i < chain.limits.size(); ++i)
        if (!unlimited(chain.limits[i])) limited = true;

    u32 iterations = 0;
    if (dist >= total) {
        // Out of reach: straighten at the goal. Never stretch.
        const Vec3 dir = toGoal * (1.0f / dist);
        for (usize i = 0; i + 1 < n; ++i) s.pos[i + 1] = s.pos[i] + dir * s.len[i];
    } else {
        Quat frame0 = Quat::identity();
        if (limited) {
            const i32 par = skel.bones[bones[0]].parent;
            if (par >= 0) frame0 = modelRotation(s.model[static_cast<usize>(par)]);
        }
        s.frame.resize(n);
        for (u32 it = 0; it < params.maxIterations; ++it) {
            if (lengthOf(s.pos[n - 1] - goal.position) <= params.toleranceCm) break;
            // Backward: the tip on the goal, every joint pulled to its bone length from its successor.
            s.pos[n - 1] = goal.position;
            for (usize i = n - 1; i-- > 0;) {
                const Vec3 d = (s.pos[i] - s.pos[i + 1]).getSafeNormal();
                s.pos[i] = s.pos[i + 1] + d * s.len[i];
            }
            // Forward: the root restored, every joint pushed out; limited joints clamp their direction to
            // the elliptic cone about the frame the swing of the bones above left them in.
            s.pos[0] = root;
            s.frame[0] = frame0;
            for (usize i = 0; i + 1 < n; ++i) {
                Vec3 d = (s.pos[i + 1] - s.pos[i]).getSafeNormal();
                if (d.sizeSquared() < 0.5f) d = Vec3{0, 0, 1};
                if (limited) {
                    const u32 b = bones[i];
                    const Quat frameI = (s.frame[i] * skel.bones[b].rotation).normalized();
                    const Vec3 segW = frameI.rotate(pose.local[bones[i + 1]].position.getSafeNormal());
                    Quat rel = arc(segW, d);
                    if (i < chain.limits.size() && !unlimited(chain.limits[i])) {
                        Quat relLocal = (conj(frameI) * rel * frameI).normalized();
                        if (clampJointRotation(chain.limits[i], Quat::identity(), relLocal)) {
                            rel = (frameI * relLocal * conj(frameI)).normalized();
                            d = rel.rotate(segW);
                        }
                    }
                    s.frame[i + 1] = (rel * frameI).normalized();
                }
                s.pos[i + 1] = s.pos[i] + d * s.len[i];
            }
            ++iterations;
        }
    }
    if (goal.hasPole) applyPole(s.pos, goal.pole, goal.poleWeight);

    writeBack(skel, pose, chain, s.model, s.pos);
    applyTipRotation(skel, pose, chain, s.model, goal);
    return finish(chain, s.model, goal.position, params, iterations);
}

IkResult solveCcd(const fmt::OcSkeleton& skel, Pose& pose, const IkChain& chain, const IkGoal& goal,
                  const IkParams& params, IkScratch& s) {
    const auto& bones = chain.bones;
    const usize n = bones.size();
    poseToModel(skel, pose, s.model);
    s.pos.resize(n);
    for (usize i = 0; i < n; ++i) s.pos[i] = originOf(s.model[bones[i]]);
    for (usize i = 0; i + 1 < n; ++i)
        if (lengthOf(s.pos[i + 1] - s.pos[i]) < kEps) return {};
    if (lengthOf(goal.position - s.pos[0]) < kEps) return {};

    const f32 maxStep = std::max(radians(params.maxStepDeg), 1e-3f);
    const u32 tipBone = bones[n - 1];
    u32 iterations = 0;
    for (u32 it = 0; it < params.maxIterations; ++it) {
        if (lengthOf(originOf(s.model[tipBone]) - goal.position) <= params.toleranceCm) break;
        f32 moved = 0.0f;
        for (usize j = n - 1; j-- > 0;) {
            const u32 b = bones[j];
            const Vec3 jp = originOf(s.model[b]);
            const Vec3 a = (originOf(s.model[tipBone]) - jp).getSafeNormal();
            const Vec3 g = (goal.position - jp).getSafeNormal();
            if (a.sizeSquared() < 0.5f || g.sizeSquared() < 0.5f) continue;
            Vec3 axis{0, 0, 1};
            f32 angle = 0.0f;
            if (!rotationBetween(a, g, axis, angle)) continue;
            angle = std::min(angle, maxStep);
            const Quat delta = Quat::fromAxisAngle(axis, angle);
            setModelRotation(skel, pose, s.model, b, (delta * modelRotation(s.model[b])).normalized());
            clampBone(skel, pose, chain, j);
            refresh(skel, pose, s.model, bones, j);
            moved = std::max(moved, angle);
        }
        ++iterations;
        if (moved < 1e-5f) break;      // every joint is aligned or held by a limit
    }
    if (goal.hasPole && n >= 3) {
        for (usize i = 0; i < n; ++i) s.pos[i] = originOf(s.model[bones[i]]);
        applyPole(s.pos, goal.pole, goal.poleWeight);
        writeBack(skel, pose, chain, s.model, s.pos);
    }
    applyTipRotation(skel, pose, chain, s.model, goal);
    return finish(chain, s.model, goal.position, params, iterations);
}

IkResult solveTwoBone(const fmt::OcSkeleton& skel, Pose& pose, const IkChain& chain, const IkGoal& goal,
                      const IkParams& params, IkScratch& s) {
    const u32 b0 = chain.bones[0], b1 = chain.bones[1], b2 = chain.bones[2];
    poseToModel(skel, pose, s.model);
    const Vec3 p0 = originOf(s.model[b0]), p1 = originOf(s.model[b1]), p2 = originOf(s.model[b2]);

    // No pole: keep the bend side the input pose had. The pole is the bend direction of the input pose
    // (the mid joint's offset off the root-tip line), pushed out so twoBoneIk sees a point on that side.
    Vec3 pole = p1;
    const Vec3 ax = (p2 - p0).getSafeNormal();
    const Vec3 bend = (p1 - p0) - ax * dot(p1 - p0, ax);
    if (lengthOf(bend) > 1e-3f) pole = p0 + bend.getSafeNormal() * 50.0f;
    if (goal.hasPole) pole = lerp(pole, goal.pole, clamp01f(goal.poleWeight));

    if (!twoBoneIk(skel, pose, b0, b1, b2, goal.position, pole)) return {};
    poseToModel(skel, pose, s.model);
    applyTipRotation(skel, pose, chain, s.model, goal);
    return finish(chain, s.model, goal.position, params, 1);
}

IkResult solveFull(const fmt::OcSkeleton& skel, Pose& pose, const IkChain& chain, const IkGoal& goal,
                   const IkParams& params, IkScratch& s) {
    switch (params.kind) {
        case IkSolverKind::TwoBone:
            return chain.bones.size() == 3 ? solveTwoBone(skel, pose, chain, goal, params, s) : IkResult{};
        case IkSolverKind::Ccd:    return solveCcd(skel, pose, chain, goal, params, s);
        case IkSolverKind::Fabrik: return solveFabrik(skel, pose, chain, goal, params, s);
    }
    return {};
}

} // namespace

bool IkChain::valid(const fmt::OcSkeleton& s) const {
    if (bones.size() < 2) return false;
    if (!limits.empty() && limits.size() != bones.size()) return false;
    for (usize i = 0; i < bones.size(); ++i) {
        if (bones[i] >= s.bones.size()) return false;
        if (i && s.bones[bones[i]].parent != static_cast<i32>(bones[i - 1])) return false;
    }
    return true;
}

bool buildChain(const fmt::OcSkeleton& skel, u32 start, u32 end, IkChain& out) {
    out = IkChain{};
    if (start >= skel.bones.size() || end >= skel.bones.size() || start == end) return false;
    std::vector<u32> rev;
    u32 cur = end;
    for (usize guard = 0; guard <= skel.bones.size(); ++guard) {
        rev.push_back(cur);
        if (cur == start) {
            out.bones.assign(rev.rbegin(), rev.rend());
            return true;
        }
        const i32 p = skel.bones[cur].parent;
        if (p < 0) return false;
        cur = static_cast<u32>(p);
    }
    return false;
}

void defaultLimitAxis(const fmt::OcSkeleton& skel, u32 bone, Vec3& axisLocal) {
    axisLocal = Vec3{1, 0, 0};
    if (bone >= skel.bones.size()) return;
    f32 best = -1.0f;
    for (usize c = 0; c < skel.bones.size(); ++c) {
        if (skel.bones[c].parent != static_cast<i32>(bone)) continue;
        const f32 len = skel.bones[c].translation.size();
        if (len > best) { best = len; axisLocal = skel.bones[c].translation.getSafeNormal(); }
    }
    if (axisLocal.sizeSquared() < 0.5f) axisLocal = Vec3{1, 0, 0};
}

bool clampJointRotation(const IkJointLimit& lim, const Quat& restLocal, Quat& local) {
    if (unlimited(lim)) return false;
    const LimitBasis b = basisOf(lim.axis);
    const Quat q = (conj(restLocal) * local).normalized();

    // q = swing * twist about the axis: the twist is the vector part's projection on the axis.
    const Vec3 proj = b.a * dot(Vec3{q.x, q.y, q.z}, b.a);
    Quat twist{proj.x, proj.y, proj.z, q.w};
    const f32 tn = std::sqrt(twist.x * twist.x + twist.y * twist.y + twist.z * twist.z + twist.w * twist.w);
    twist = tn < 1e-6f ? Quat::identity() : Quat{twist.x / tn, twist.y / tn, twist.z / tn, twist.w / tn};
    const Quat swing = (q * conj(twist)).normalized();

    const f32 tw = wrapPi(2.0f * std::atan2(dot(Vec3{twist.x, twist.y, twist.z}, b.a), twist.w));
    const f32 twMax = radians(std::clamp(lim.twistDeg, 0.0f, 180.0f));
    const f32 twClamped = std::clamp(tw, -twMax, twMax);
    const bool twistChanged = std::fabs(twClamped - tw) > 1e-4f;

    Vec3 dir = swing.rotate(b.a);
    const bool swingChanged = clampCone(b, radians(std::clamp(lim.swing1Deg, 0.0f, 180.0f)),
                                        radians(std::clamp(lim.swing2Deg, 0.0f, 180.0f)), dir);
    if (!swingChanged && !twistChanged) return false;

    const Quat swing2 = swingChanged ? arc(b.a, dir) : swing;
    const Quat twist2 = twistChanged ? Quat::fromAxisAngle(b.a, twClamped) : twist;
    local = (restLocal * swing2 * twist2).normalized();
    return true;
}

bool setBoneModelTransform(const fmt::OcSkeleton& skel, Pose& pose, u32 bone, const Vec3* position, const Quat* rotation) {
    if (bone >= skel.bones.size() || !pose.matches(skel)) return false;
    std::vector<Mat4> model;
    poseToModel(skel, pose, model);
    const i32 par = skel.bones[bone].parent;
    if (rotation) setModelRotation(skel, pose, model, bone, rotation->normalized());
    if (position) {
        const Vec3 p = *position;
        if (par < 0) {
            pose.local[bone].position = p;
        } else {
            const Mat4 inv = model[static_cast<usize>(par)].inverse();
            pose.local[bone].position = transformDir(p, inv) + originOf(inv);
        }
    }
    return true;
}

IkResult solveChain(const fmt::OcSkeleton& skel, Pose& pose, const IkChain& chain, const IkGoal& goal,
                    const IkParams& params, IkScratch& scratch) {
    if (!pose.matches(skel) || !chain.valid(skel)) return {};
    const f32 w = clamp01f(goal.weight);
    if (w <= 0.0f) return {};
    if (w >= 0.9999f) return solveFull(skel, pose, chain, goal, params, scratch);

    // Solved on a copy and slerped back per bone: IK is not linear in its goal, so a scaled goal would be
    // nobody's answer (the rule applyRigOp uses).
    scratch.copy = pose;
    IkResult r = solveFull(skel, scratch.copy, chain, goal, params, scratch);
    if (!r.solved) return r;
    for (const u32 b : chain.bones)
        pose.local[b].rotation = Quat::slerp(pose.local[b].rotation, scratch.copy.local[b].rotation, w).normalized();
    poseToModel(skel, pose, scratch.model);
    r.errorCm = lengthOf(originOf(scratch.model[chain.bones.back()]) - goal.position);
    r.reached = r.errorCm <= std::max(params.toleranceCm, 0.1f);
    return r;
}

} // namespace aver::anim
