// Two-bone IK. See Ik.hpp for what it is for; this file is the arithmetic and the conventions.
#include "aver/anim/Ik.hpp"

#include "IkMath.hpp"
#include "aver/formats/OcAnim.hpp"

#include <cmath>

namespace aver::anim {

using namespace ikm;   // the shared helpers (IkMath.hpp), moved out of this file unchanged

bool aimAt(const fmt::OcSkeleton& skel, Pose& pose, u32 bone,
           const Vec3& targetModel, const Vec3& localAxis) {
    if (bone >= skel.bones.size() || pose.local.size() != skel.bones.size()) return false;
    if (lengthOf(localAxis) < kEps) return false;

    std::vector<Mat4> model;
    poseToModel(skel, pose, model);

    const Vec3 origin = originOf(model[bone]);
    const Vec3 toTarget = targetModel - origin;
    if (lengthOf(toTarget) < kEps) return false;

    // The authored axis is in the BONE's space, so it has to be carried into model space through the
    // bone's own model matrix before it can be compared with a direction to a point out in the world.
    const Vec3 curDir = transformDir(localAxis, model[bone]).getSafeNormal();
    if (lengthOf(curDir) < kEps) return false;

    Vec3 axis{0, 0, 1};
    f32 angle = 0.0f;
    if (rotationBetween(curDir, toTarget.getSafeNormal(), axis, angle))
        applyModelDelta(skel, pose, model, bone, axis, angle);
    return true;
}

bool bonePositionModel(const fmt::OcSkeleton& skel, const Pose& pose, u32 bone, Vec3& out) {
    if (bone >= skel.bones.size() || pose.local.size() != skel.bones.size()) return false;
    std::vector<Mat4> model;
    poseToModel(skel, pose, model);
    if (bone >= model.size()) return false;
    out = originOf(model[bone]);
    return true;
}

bool twoBoneIk(const fmt::OcSkeleton& skel, Pose& pose,
               u32 root, u32 mid, u32 tip,
               const Vec3& goalModel, const Vec3& poleModel) {
    if (pose.local.size() != skel.bones.size()) return false;
    if (!linked(skel, root, mid) || !linked(skel, mid, tip)) return false;

    std::vector<Mat4> model;
    poseToModel(skel, pose, model);

    const Vec3 p0 = originOf(model[root]);
    const Vec3 p1 = originOf(model[mid]);
    const Vec3 p2 = originOf(model[tip]);

    const f32 a = lengthOf(p1 - p0);          // root -> mid
    const f32 b = lengthOf(p2 - p1);          // mid  -> tip
    if (a < kEps || b < kEps) return false;

    const Vec3 toGoal = goalModel - p0;
    const f32 rawDist = lengthOf(toGoal);
    if (rawDist < kEps) return false;
    const Vec3 dirGoal = toGoal * (1.0f / rawDist);

    // CLAMPED INTO THE TRIANGLE INEQUALITY, which is what makes an out-of-range goal straighten the
    // chain instead of producing a NaN. Outside [|a-b|, a+b] the law of cosines below asks acos for
    // a value past +-1; the epsilons keep it strictly inside so a fully extended chain still has a
    // defined bend direction rather than a degenerate zero-area triangle.
    const f32 dMin = std::fabs(a - b) + kEps;
    const f32 dMax = a + b - kEps;
    const f32 d = rawDist < dMin ? dMin : (rawDist > dMax ? dMax : rawDist);

    // The interior angle at the root between the goal direction and the first segment.
    f32 cosRoot = (a * a + d * d - b * b) / (2.0f * a * d);
    cosRoot = cosRoot < -1.0f ? -1.0f : (cosRoot > 1.0f ? 1.0f : cosRoot);
    const f32 angleRoot = std::acos(cosRoot);

    // THE BEND PLANE, and the pole is what picks it. Two mirrored solutions satisfy any reachable
    // goal; the pole names which side the middle joint goes. A pole ON the root-goal line leaves no
    // plane to define, so a perpendicular is chosen -- arbitrary, but arbitrary and stable beats
    // refusing to solve because a caller pointed the pole straight down the arm.
    Vec3 bendAxis = cross(dirGoal, (poleModel - p0));
    if (lengthOf(bendAxis) < 1e-6f) {
        const Vec3 pick = std::fabs(dirGoal.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        bendAxis = cross(dirGoal, pick);
    }
    bendAxis = bendAxis.getSafeNormal();

    // Where the middle joint wants to be: the goal direction, swung up out of the line by the root
    // angle, times the first segment's length.
    //
    // THE SIGN IS +angleRoot AND IT WAS MEASURED, NOT DERIVED. Written first as -angleRoot, which
    // solved perfectly -- wrist exactly on the goal, bone lengths held, unreachable goals
    // straightening properly -- while putting the elbow on the side AWAY from the pole every time.
    // Every distance check passed; only the pole test in IkTest caught it. That is the whole reason
    // that test exists, because a mirrored solution is still a solution.
    const Quat swing = Quat::fromAxisAngle(bendAxis, angleRoot);
    const Vec3 desiredMid = p0 + swing.rotate(dirGoal) * a;

    // Two rotations, applied in order, each measured against the pose as it stands at that moment --
    // the second one has to see where the first put the middle joint, so the model array is rebuilt
    // between them rather than predicted.
    Vec3 axis{0, 0, 1};
    f32 angle = 0.0f;

    const Vec3 curDir1 = (p1 - p0).getSafeNormal();
    const Vec3 wantDir1 = (desiredMid - p0).getSafeNormal();
    if (rotationBetween(curDir1, wantDir1, axis, angle)) {
        applyModelDelta(skel, pose, model, root, axis, angle);
        poseToModel(skel, pose, model);
    }

    const Vec3 q1 = originOf(model[mid]);
    const Vec3 q2 = originOf(model[tip]);
    const Vec3 curDir2 = (q2 - q1).getSafeNormal();
    const Vec3 wantDir2 = (goalModel - q1).getSafeNormal();
    if (rotationBetween(curDir2, wantDir2, axis, angle)) {
        applyModelDelta(skel, pose, model, mid, axis, angle);
    }

    return true;
}

} // namespace aver::anim
