#pragma once
// The pure IK Rig stack: an .ocikrig bound to a skeleton, then solved on a pose each tick -- body (pelvis)
// shift, chains in priority order with the claim rule, and foot placement over a ground-query callback.
// No scene, no physics, no time source of its own: the scene layer (IkRigSystem) feeds it goals, a ground
// function and dt, which is also what lets the unit tests drive it with a plane.
// Design: docs/plans/wave5/IK_RETARGETING.md 3.3 and 4.3.
#include "aver/anim/IkRigAsset.hpp"

#include <string>
#include <vector>

namespace aver::anim {

// CIkRig::flags: each bit turns a part of the rig off.
inline constexpr u32 kIkRigFeetOff = 0x1, kIkRigGoalsOff = 0x2, kIkRigBodyOff = 0x4;

inline constexpr u32 kIkNone = 0xFFFFFFFFu;

struct IkBoundChain {
    std::string name;
    u64 nameHash = 0;
    IkChain chain;                    // limits baked (the asset's list, ragdoll axis rule where it gives none)
    IkParams params;
    bool tipRotation = false;
    i32 goalSlot = -1;                // into IkRigBound::goalHashes, -1 = no external goal
    i32 footIndex = -1;               // into IkRigBound::feet
    IkPoleMode poleMode = IkPoleMode::Animated;
    u32 poleBone = kIkNone;
    Vec3 poleDir{0, 0, 1};
    i32 priority = 0;
};

struct IkBoundFoot {
    u32 chain = 0;                    // index into IkRigBound::chains
    u64 nameHash = 0;                 // hash of the leg chain's name: how a script names the foot
    u32 foot = 0, toe = kIkNone;
    f32 soleOffset = 0.0f;            // rest ankle height above the assumed ground
    f32 groundZ = 0.0f;               // model z of the ground plane the animation was authored on
    u64 curveHash = 0;                // 0 = automatic plant
    f32 plantLow = 2.0f, plantHigh = 10.0f, probeUp = 50.0f, probeDown = 60.0f;
    bool align = true;
    f32 maxAlignDeg = 30.0f;
};

struct IkRigBound {
    bool ok = false;
    u32 boneCount = 0;
    std::vector<std::string> issues;  // unresolved bones, fallbacks: logged once by the caller
    u32 bodyBone = kIkNone;
    i32 bodyGoalSlot = -1;
    f32 bodyMaxOffsetCm = 40.0f;
    std::vector<std::string> goalNames;
    std::vector<u64> goalHashes;
    std::vector<IkBoundChain> chains; // solve order: ascending priority, ties in file order
    std::vector<IkBoundFoot> feet;
    IkRigFootSettings foot;

    i32 findGoal(u64 hash) const;
    i32 findFoot(u64 footHash) const;
};

// Resolves names against `skel`. Never fails outright: what does not resolve disables its chain (listed in
// `issues`). `ok` is false only for a skeleton with no bones.
bool bindIkRig(const IkRigAsset& asset, const fmt::OcSkeleton& skel, IkRigBound& out);

// Smoothing state per entity. Transient: a rebind or a fresh attach starts with init == false, which snaps.
struct IkFootState {
    f32 offset = 0.0f;                // smoothed model-space z offset of the foot goal, cm
    f32 plant = 1.0f;
    Vec3 normal{0, 0, 1};
    bool init = false;
    bool hadHit = false;
};
struct IkRigState {
    std::vector<IkFootState> feet;
    f32 pelvis = 0.0f;                // smoothed pelvis z offset (<= 0), cm
    bool pelvisInit = false;
};

// One goal for the tick, already in model space. Untouched or released goals have alpha 0.
struct IkGoalValue {
    bool active = false;
    Vec3 pos{0, 0, 0};
    bool hasRot = false;
    Quat rot = Quat::identity();
    f32 alpha = 0.0f;
};

// Casts DOWN from `originModel` for `distance` (model units) and reports the first ground hit, point and
// unit normal in model space. False = nothing within reach. The host converts to world and back.
using IkGroundFn = bool (*)(const Vec3& originModel, f32 distance, u32 layerMask, Vec3& pointModel,
                            Vec3& normalModel, void* user);
// A float curve on the playing clip, by name hash.
using IkCurveFn = bool (*)(u64 nameHash, f32& out, void* user);

struct IkRigInputs {
    f32 weight = 1.0f;                // CIkRig::weight
    u32 flags = 0;                    // kIkRig*
    f32 dt = 0.0f;
    const IkGoalValue* goals = nullptr;   // one per IkRigBound::goalHashes
    usize goalCount = 0;
    const f32* footAlpha = nullptr;   // one per IkRigBound::feet; null = all 1
    IkGroundFn ground = nullptr;      // null = every foot sees "no hit": offsets ease to 0
    void* groundUser = nullptr;
    IkCurveFn curve = nullptr;
    void* curveUser = nullptr;
    bool probe = true;                // false = over the host's probe budget: feet hold their last offsets
};

struct IkChainReport {
    bool driven = false;              // had a goal or a foot target this tick
    bool solved = false;
    bool claimedOut = false;          // nothing unclaimed was left (a higher priority chain took its bones)
    bool reached = false;
    f32 errorCm = 0.0f;
};
struct IkRigReport {
    std::vector<IkChainReport> chains;    // parallel to IkRigBound::chains
    std::vector<f32> goalError;           // parallel to goalHashes
    std::vector<u8> goalReached;
    f32 pelvisOffset = 0.0f;              // the total model-space z shift applied to the body bone
    u32 probes = 0;
    bool changed = false;                 // anything in the pose was edited
};

// Edits `pose` in place. Does nothing for weight <= 0, an unbound rig or a pose that does not match.
void solveIkRig(const fmt::OcSkeleton& skel, const IkRigBound& bound, IkRigState& state, const IkRigInputs& in,
                Pose& pose, IkRigReport& report, IkScratch& scratch);

} // namespace aver::anim
