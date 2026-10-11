#pragma once
// Chain IK beyond the analytic two-bone solve: FABRIK and CCD with joint limits, a pole vector, tip
// rotation and a blend weight. Design: docs/plans/wave5/IK_RETARGETING.md section 3.2.
//
// Same contract as Ik.hpp: MODEL SPACE IN (goal, pole, tip rotation), LOCAL ROTATIONS OUT (Pose::local).
// Only rotations are written; the chain's root bone never translates, and segment lengths are held for
// the whole solve (a solver that reaches by stretching is the failure that looks most like success).
#include "aver/anim/Pose.hpp"
#include "aver/core/Math.hpp"

#include <vector>

namespace aver::anim {

enum class IkSolverKind : u8 { TwoBone = 0, Fabrik = 1, Ccd = 2 };

// Same meaning as ragdoll::RagdollDef::limitsDeg (3 per bone): symmetric half-angles in degrees about the
// segment, `axis` being that segment in the BONE's local space (the twist axis). The reference is the
// bone's REST local rotation. Aver.Anim cannot depend on the ragdoll module, so the numbers are copied,
// not shared; the editor fills them from ragdoll::buildRagdoll.
struct IkJointLimit {
    bool enabled = false;
    f32 swing1Deg = 180, swing2Deg = 180, twistDeg = 180;
    Vec3 axis{1, 0, 0};
};

// root-first, each bone the parent of the next, >= 2 bones.
struct IkChain {
    std::vector<u32> bones;
    std::vector<IkJointLimit> limits;   // empty, or one per bone (the tip's entry is ignored)
    bool valid(const fmt::OcSkeleton& s) const;
};

struct IkGoal {
    Vec3 position{0, 0, 0};             // model space, cm
    bool hasRotation = false;
    Quat rotation = Quat::identity();   // desired MODEL rotation of the tip bone
    f32  rotationWeight = 1.0f;
    bool hasPole = false;
    Vec3 pole{0, 0, 0};                 // model-space point the middle of the chain bends toward
    f32  poleWeight = 1.0f;
    f32  weight = 1.0f;                 // solved on a copy and slerped back per bone
};

struct IkParams {
    IkSolverKind kind = IkSolverKind::Fabrik;
    u32 maxIterations = 10;             // asset defaults: FABRIK 10, CCD 12
    f32 toleranceCm = 0.1f;
    f32 maxStepDeg = 30.0f;             // CCD: per-joint rotation cap per iteration (damping)
};

struct IkResult {
    bool solved = false;                // false = the pose was not touched
    bool reached = false;
    f32 errorCm = 0.0f;                 // |tip - goal| after the solve
    u32 iterations = 0;
};

// Reusable buffers, so a solve allocates nothing after the first.
struct IkScratch {
    std::vector<Mat4> model;
    std::vector<Vec3> pos;
    std::vector<f32> len;
    std::vector<Quat> frame;
    Pose copy;
    // For the rig stack above the solvers (IkRigSolver): its own model array and bookkeeping, so a chain
    // solve overwriting `model` cannot clobber them.
    std::vector<Mat4> rigModel;
    std::vector<u8> claimed;
    std::vector<Vec3> aux;
};

// Walks `end` up to `start` (an ancestor); false when it never meets it, when start == end, or an index
// is out of range.
bool buildChain(const fmt::OcSkeleton& skel, u32 start, u32 end, IkChain& out);

// The twist axis of a bone in its own local space: toward its longest child (the ragdoll builder's rule,
// RagdollBuilder.cpp), +X for a leaf.
void defaultLimitAxis(const fmt::OcSkeleton& skel, u32 bone, Vec3& axisLocal);

// Clamps `local` (a bone's local rotation) to the limit about its rest rotation. The swing is bounded by
// the ELLIPTIC cone of (swing1, swing2) around the axis and the twist by +-twist. True when it changed.
bool clampJointRotation(const IkJointLimit& lim, const Quat& restLocal, Quat& local);

// TwoBone needs exactly 3 bones and calls twoBoneIk (limits are not applied there). The pose is rewritten
// only on solved == true.
IkResult solveChain(const fmt::OcSkeleton& skel, Pose& pose, const IkChain& chain, const IkGoal& goal,
                    const IkParams& params, IkScratch& scratch);

// Sets a bone's MODEL-space position and/or rotation by rewriting its LOCAL transform (the parent's pose is
// read as it stands). Either pointer may be null. For control-rig copybone / setbone; false on a bad index
// or a mismatched pose.
bool setBoneModelTransform(const fmt::OcSkeleton& skel, Pose& pose, u32 bone, const Vec3* position, const Quat* rotation);

} // namespace aver::anim
