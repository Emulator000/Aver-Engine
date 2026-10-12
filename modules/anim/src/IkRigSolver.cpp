// The IK Rig stack: bind, then body shift, chains by priority with claims, and foot placement.
// See IkRigSolver.hpp; the foot algorithm is IK_RETARGETING.md 4.3.
#include "aver/anim/IkRigSolver.hpp"

#include "IkMath.hpp"
#include "aver/core/Hash.hpp"

#include <algorithm>
#include <cmath>

namespace aver::anim {

using namespace ikm;

namespace {

u32 boneNamed(const fmt::OcSkeleton& s, const std::string& name) {
    if (name.empty()) return kIkNone;
    for (usize i = 0; i < s.bones.size(); ++i)
        if (s.bones[i].name == name) return static_cast<u32>(i);
    return kIkNone;
}

f32 clamp01f(f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

f32 smoothstep(f32 a, f32 b, f32 x) {
    if (b <= a) return x >= b ? 1.0f : 0.0f;
    const f32 t = clamp01f((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

i32 slotFor(IkRigBound& b, const std::string& goal) {
    if (goal.empty()) return -1;
    const u64 h = fnv1a64(goal);
    for (usize i = 0; i < b.goalHashes.size(); ++i)
        if (b.goalHashes[i] == h) return static_cast<i32>(i);
    b.goalNames.push_back(goal);
    b.goalHashes.push_back(h);
    return static_cast<i32>(b.goalHashes.size() - 1);
}

} // namespace

i32 IkRigBound::findGoal(u64 hash) const {
    for (usize i = 0; i < goalHashes.size(); ++i)
        if (goalHashes[i] == hash) return static_cast<i32>(i);
    return -1;
}

i32 IkRigBound::findFoot(u64 footHash) const {
    for (usize i = 0; i < feet.size(); ++i)
        if (feet[i].nameHash == footHash) return static_cast<i32>(i);
    return -1;
}

bool bindIkRig(const IkRigAsset& asset, const fmt::OcSkeleton& skel, IkRigBound& out) {
    out = IkRigBound{};
    out.boneCount = static_cast<u32>(skel.bones.size());
    out.foot = asset.foot;
    if (skel.bones.empty()) { out.issues.push_back("the skeleton has no bones"); return false; }

    if (!asset.body.bone.empty()) {
        out.bodyBone = boneNamed(skel, asset.body.bone);
        if (out.bodyBone == kIkNone) out.issues.push_back("body bone '" + asset.body.bone + "' not found");
        out.bodyMaxOffsetCm = asset.body.maxOffsetCm;
        if (out.bodyBone != kIkNone) out.bodyGoalSlot = slotFor(out, asset.body.goal);
    }

    // Declared goals exist whether or not their chain binds, so a script's name is never refused for a
    // bone problem (the problem is in `issues`, once).
    std::vector<IkBoundChain> bound;
    for (const IkRigChainDef& def : asset.chains) {
        const i32 slot = slotFor(out, def.goal);
        const u32 s = boneNamed(skel, def.start), e = boneNamed(skel, def.end);
        if (s == kIkNone || e == kIkNone) {
            out.issues.push_back("chain '" + def.name + "' disabled: bone '" +
                                 (s == kIkNone ? def.start : def.end) + "' not found");
            continue;
        }
        IkBoundChain bc;
        if (!buildChain(skel, s, e, bc.chain)) {
            out.issues.push_back("chain '" + def.name + "' disabled: '" + def.start + "' is not an ancestor of '" +
                                 def.end + "'");
            continue;
        }
        bc.name = def.name;
        bc.nameHash = fnv1a64(def.name);
        bc.goalSlot = slot;
        bc.priority = def.priority;
        bc.tipRotation = def.tipRotation;
        bc.params.kind = def.solver;
        bc.params.maxIterations = def.iterations;
        bc.params.toleranceCm = def.toleranceCm;
        bc.params.maxStepDeg = def.maxStepDeg;
        if (def.solver == IkSolverKind::TwoBone && bc.chain.bones.size() != 3) {
            bc.params.kind = IkSolverKind::Fabrik;
            out.issues.push_back("chain '" + def.name + "': twobone needs exactly 3 bones, using fabrik");
        }
        if (def.limits) {
            bc.chain.limits.resize(bc.chain.bones.size());
            for (usize i = 0; i < bc.chain.bones.size(); ++i) {
                IkJointLimit& l = bc.chain.limits[i];
                defaultLimitAxis(skel, bc.chain.bones[i], l.axis);
                for (const IkRigLimitDef& ld : asset.limits) {
                    if (ld.bone != skel.bones[bc.chain.bones[i]].name) continue;
                    l.enabled = true;
                    l.swing1Deg = ld.swing1;
                    l.swing2Deg = ld.swing2;
                    l.twistDeg = ld.twist;
                    if (ld.hasAxis) l.axis = ld.axis;
                    break;
                }
            }
        }
        bc.poleMode = def.pole.mode;
        bc.poleDir = def.pole.dirModel;
        if (def.pole.mode == IkPoleMode::Bone) {
            bc.poleBone = boneNamed(skel, def.pole.bone);
            if (bc.poleBone == kIkNone) {
                bc.poleMode = IkPoleMode::Animated;
                out.issues.push_back("chain '" + def.name + "': pole bone '" + def.pole.bone + "' not found");
            }
        }
        bound.push_back(std::move(bc));
    }
    std::stable_sort(bound.begin(), bound.end(),
                     [](const IkBoundChain& a, const IkBoundChain& b) { return a.priority < b.priority; });
    out.chains = std::move(bound);

    // The ground the animation assumes: the lowest rest z among foot and toe bones, but never above the
    // model origin (a character stands on z = 0), so flat ground at the origin reads zero offset.
    Pose rest;
    restPose(skel, rest);
    std::vector<Mat4> restModel;
    poseToModel(skel, rest, restModel);
    f32 lowest = 0.0f;
    for (const IkRigFootDef& fd : asset.feet) {
        for (const IkBoundChain& bc : out.chains) {
            if (bc.name != fd.chain) continue;
            lowest = std::min(lowest, originOf(restModel[bc.chain.bones.back()]).z);
            const u32 toe = boneNamed(skel, fd.toe);
            if (toe != kIkNone) lowest = std::min(lowest, originOf(restModel[toe]).z);
        }
    }
    for (const IkRigFootDef& fd : asset.feet) {
        i32 ci = -1;
        for (usize i = 0; i < out.chains.size(); ++i)
            if (out.chains[i].name == fd.chain) ci = static_cast<i32>(i);
        if (ci < 0) { out.issues.push_back("foot on chain '" + fd.chain + "' dropped: the chain is not bound"); continue; }
        IkBoundChain& bc = out.chains[static_cast<usize>(ci)];
        IkBoundFoot f;
        f.chain = static_cast<u32>(ci);
        f.nameHash = bc.nameHash;
        f.foot = bc.chain.bones.back();
        f.toe = boneNamed(skel, fd.toe);
        if (!fd.toe.empty() && f.toe == kIkNone) out.issues.push_back("toe bone '" + fd.toe + "' not found");
        f.groundZ = std::min(0.0f, lowest);
        f.soleOffset = fd.soleOffsetCm >= 0.0f ? fd.soleOffsetCm : originOf(restModel[f.foot]).z - f.groundZ;
        f.curveHash = fd.plantCurve.empty() ? 0 : fnv1a64(fd.plantCurve);
        f.plantLow = fd.plantLowCm;
        f.plantHigh = fd.plantHighCm;
        f.probeUp = fd.probeUpCm;
        f.probeDown = fd.probeDownCm;
        f.align = fd.alignToNormal;
        f.maxAlignDeg = fd.maxAlignDeg;
        bc.footIndex = static_cast<i32>(out.feet.size());
        out.feet.push_back(f);
    }
    out.ok = true;
    return true;
}

void solveIkRig(const fmt::OcSkeleton& skel, const IkRigBound& bound, IkRigState& state, const IkRigInputs& in,
                Pose& pose, IkRigReport& rep, IkScratch& scratch) {
    rep.chains.assign(bound.chains.size(), IkChainReport{});
    rep.goalError.assign(bound.goalHashes.size(), 0.0f);
    rep.goalReached.assign(bound.goalHashes.size(), 0);
    rep.pelvisOffset = 0.0f;
    rep.probes = 0;
    rep.changed = false;
    if (!bound.ok || in.weight <= 0.0f || !pose.matches(skel) || bound.boneCount != skel.bones.size()) return;

    const f32 weight = clamp01f(in.weight);
    const bool feetOn = !(in.flags & kIkRigFeetOff) && !bound.feet.empty();
    const bool goalsOn = !(in.flags & kIkRigGoalsOff);
    const bool bodyOn = !(in.flags & kIkRigBodyOff) && bound.bodyBone != kIkNone;

    std::vector<Mat4>& model = scratch.rigModel;
    poseToModel(skel, pose, model);
    state.feet.resize(bound.feet.size());

    auto goalAt = [&](i32 slot) -> const IkGoalValue* {
        if (!goalsOn || slot < 0 || !in.goals || static_cast<usize>(slot) >= in.goalCount) return nullptr;
        const IkGoalValue& g = in.goals[slot];
        return (g.active && g.alpha > 0.0f) ? &g : nullptr;
    };
    auto footAlphaOf = [&](usize f) { return in.footAlpha ? clamp01f(in.footAlpha[f]) : 1.0f; };
    const f32 k = in.dt > 0.0f ? 1.0f - std::exp(-bound.foot.smoothing * in.dt) : 0.0f;
    const f32 follow = bound.foot.smoothing > 0.0f ? k : 1.0f;

    // ---- 1. foot placement: probe, asymmetric target, smoothing
    std::vector<Vec3>& animFoot = scratch.aux;
    animFoot.assign(bound.feet.size(), Vec3{0, 0, 0});
    if (feetOn) {
        for (usize f = 0; f < bound.feet.size(); ++f) {
            const IkBoundFoot& bf = bound.feet[f];
            IkFootState& fs = state.feet[f];
            const Vec3 pos = originOf(model[bf.foot]);
            animFoot[f] = pos;
            const f32 hAnim = pos.z - bf.groundZ - bf.soleOffset;

            f32 plant = 1.0f - smoothstep(bf.plantLow, bf.plantHigh, hAnim);
            f32 curve = 0.0f;
            if (bf.curveHash != 0 && in.curve && in.curve(bf.curveHash, curve, in.curveUser)) plant = clamp01f(curve);

            f32 target = fs.offset;
            Vec3 normal = fs.normal;
            if (in.probe) {
                f32 delta = 0.0f;
                normal = Vec3{0, 0, 1};
                if (in.ground) {
                    Vec3 hit{0, 0, 0}, n{0, 0, 1};
                    if (in.ground(Vec3{pos.x, pos.y, bf.groundZ + bf.probeUp}, bf.probeUp + bf.probeDown,
                                  bound.foot.layerMask, hit, n, in.groundUser)) {
                        delta = hit.z - bf.groundZ;
                        normal = n.sizeSquared() > 0.25f ? n.getSafeNormal() : Vec3{0, 0, 1};
                    }
                }
                ++rep.probes;
                // Asymmetric: ground above the assumption always lifts the foot (a swinging foot must not
                // clip a step), ground below only pulls a planted one.
                target = delta > 0.0f ? delta : delta * plant;
                target = std::clamp(target, -bound.foot.maxFootOffsetCm, bound.foot.maxFootOffsetCm) * footAlphaOf(f);
            }
            if (!fs.init) {
                fs.offset = target;
                fs.normal = normal;
                fs.init = true;
            } else {
                fs.offset += (target - fs.offset) * follow;
                fs.normal = lerp(fs.normal, normal, follow).getSafeNormal();
                if (fs.normal.sizeSquared() < 0.25f) fs.normal = Vec3{0, 0, 1};
            }
            fs.plant = plant;
        }
    }

    // ---- 2. pelvis: drops by the lowest foot, capped, smoothed
    f32 pelvisTarget = 0.0f;
    if (feetOn && bound.foot.pelvisMode == IkPelvisMode::Lowest) {
        for (const IkFootState& fs : state.feet) pelvisTarget = std::min(pelvisTarget, fs.offset);
        pelvisTarget = std::max(pelvisTarget, -bound.foot.maxPelvisDropCm);
    }
    if (!state.pelvisInit) { state.pelvis = pelvisTarget; state.pelvisInit = true; }
    else state.pelvis += (pelvisTarget - state.pelvis) * follow;

    // ---- 3. body
    if (bodyOn) {
        Vec3 off{0, 0, state.pelvis * weight};
        const u32 bb = bound.bodyBone;
        if (const IkGoalValue* g = goalAt(bound.bodyGoalSlot))
            off += (g->pos - originOf(model[bb])) * (g->alpha * weight);
        const f32 mag = off.size();
        if (mag > bound.bodyMaxOffsetCm && mag > 0.0f) off = off * (bound.bodyMaxOffsetCm / mag);
        if (off.size() > 1e-3f) {
            const i32 par = skel.bones[bb].parent;
            const Vec3 dl = par >= 0 ? transformDir(off, model[static_cast<usize>(par)].inverse()) : off;
            pose.local[bb].position += dl;
            poseToModel(skel, pose, model);
            rep.pelvisOffset = off.z;
            rep.changed = true;
        }
    }

    // ---- 4. chains in priority order, with claims
    scratch.claimed.assign(skel.bones.size(), 0);
    IkChain sub;
    for (usize ci = 0; ci < bound.chains.size(); ++ci) {
        const IkBoundChain& bc = bound.chains[ci];
        IkChainReport& cr = rep.chains[ci];
        const IkGoalValue* gv = goalAt(bc.goalSlot);
        const bool hasFoot = feetOn && bc.footIndex >= 0;
        if (!gv && !hasFoot) continue;
        cr.driven = true;
        const usize fi = hasFoot ? static_cast<usize>(bc.footIndex) : 0;
        const f32 fAlpha = hasFoot ? footAlphaOf(fi) : 0.0f;

        // Where the tip should be, and how strongly. A goal wins over the foot offset; the foot only
        // supplies the ground height for the part of the weight the goal leaves free.
        Vec3 target{0, 0, 0};
        f32 w = 0.0f;
        const Vec3 footTarget = hasFoot ? animFoot[fi] + Vec3{0, 0, state.feet[fi].offset} : Vec3{0, 0, 0};
        if (gv && hasFoot) { target = lerp(footTarget, gv->pos, gv->alpha); w = fAlpha + (1.0f - fAlpha) * gv->alpha; }
        else if (gv)       { target = gv->pos; w = gv->alpha; }
        else               { target = footTarget; w = fAlpha; }
        w = clamp01f(w * weight);

        // A foot already on its target is left alone, so flat ground leaves the pose bit-identical. (Not
        // "offset is zero": a pelvis that dropped has moved the ankle off a zero-offset target.)
        const bool idleFoot = !gv && hasFoot && lengthOf(originOf(model[bc.chain.bones.back()]) - target) < 1e-3f;
        if (w > 0.0f && !idleFoot) {
            usize start = 0;
            for (usize i = 0; i < bc.chain.bones.size(); ++i)
                if (scratch.claimed[bc.chain.bones[i]]) start = i + 1;
            if (bc.chain.bones.size() - start < 2) {
                cr.claimedOut = true;
            } else {
                sub.bones.assign(bc.chain.bones.begin() + static_cast<std::ptrdiff_t>(start), bc.chain.bones.end());
                sub.limits.clear();
                if (!bc.chain.limits.empty())
                    sub.limits.assign(bc.chain.limits.begin() + static_cast<std::ptrdiff_t>(start), bc.chain.limits.end());
                IkParams p = bc.params;
                if (p.kind == IkSolverKind::TwoBone && sub.bones.size() != 3) p.kind = IkSolverKind::Fabrik;

                IkGoal g;
                g.position = target;
                g.weight = w;
                const bool useRot = gv && bc.tipRotation && gv->hasRot;
                if (useRot) { g.hasRotation = true; g.rotation = gv->rot; }
                if (bc.poleMode == IkPoleMode::Bone && bc.poleBone != kIkNone) {
                    g.hasPole = true;
                    g.pole = originOf(model[bc.poleBone]);
                } else if (bc.poleMode == IkPoleMode::Vector) {
                    g.hasPole = true;
                    g.pole = originOf(model[sub.bones[sub.bones.size() / 2]]) + bc.poleDir.getSafeNormal() * 50.0f;
                }
                const IkResult r = solveChain(skel, pose, sub, g, p, scratch);
                cr.solved = r.solved;
                cr.reached = r.reached;
                cr.errorCm = r.errorCm;
                if (r.solved) {
                    rep.changed = true;
                    const usize claimTo = useRot ? sub.bones.size() : sub.bones.size() - 1;
                    for (usize i = 0; i < claimTo; ++i) scratch.claimed[sub.bones[i]] = 1;
                    if (gv && bc.goalSlot >= 0) {
                        rep.goalError[static_cast<usize>(bc.goalSlot)] = r.errorCm;
                        rep.goalReached[static_cast<usize>(bc.goalSlot)] = r.reached ? 1 : 0;
                    }
                    poseToModel(skel, pose, model);
                }
            }
        }

        // Foot align: tilt the foot by the ground's tilt (planted weight), capped. The tilt is applied as a
        // delta on the animated foot, so toe-off and swing pitch survive; flat ground is the identity.
        if (hasFoot && bound.feet[fi].align && fAlpha > 0.0f) {
            const IkBoundFoot& bf = bound.feet[fi];
            const IkFootState& fs = state.feet[fi];
            const Vec3 up{0, 0, 1};
            const Vec3 wn = lerp(up, fs.normal, fs.plant).getSafeNormal();
            Vec3 axis{0, 0, 1};
            f32 angle = 0.0f;
            if (wn.sizeSquared() > 0.25f && rotationBetween(up, wn, axis, angle) && angle > 1e-4f) {
                angle = std::min(angle, radians(bf.maxAlignDeg)) * fAlpha * weight;
                if (angle > 1e-5f) {
                    const Quat tilt = Quat::fromAxisAngle(axis, angle);
                    setModelRotation(skel, pose, model, bf.foot, (tilt * modelRotation(model[bf.foot])).normalized());
                    poseToModel(skel, pose, model);
                    rep.changed = true;
                }
            }
        }
    }
}

} // namespace aver::anim
