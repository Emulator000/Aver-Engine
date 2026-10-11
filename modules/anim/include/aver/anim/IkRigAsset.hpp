#pragma once
// .ocikrig -- an IK Rig asset: chains with solvers, priorities and goals, joint limits, a body (pelvis)
// solve and foot placement, authored against one skeleton. Design: docs/plans/wave5/IK_RETARGETING.md 3.3.
//
// Text JSON with a format tag and a version, written and parsed like .ocmontage: floats at nine
// significant digits (lossless for f32), unknown keys ignored, a newer version refused, and parse does NOT
// call valid() so a half-authored rig still round-trips. Bones are named, never indexed: bound to a
// skeleton by the runtime (IkRigSolver.hpp).
#include "aver/anim/IkSolvers.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::anim {

inline constexpr int kIkRigVersion = 1;

enum class IkPoleMode : u8 { Animated = 0, Bone = 1, Vector = 2 };

struct IkRigPole {
    IkPoleMode mode = IkPoleMode::Animated;   // Animated keeps the bend side the input pose had
    std::string bone;                         // Bone: that bone's model position
    Vec3 dirModel{0, 0, 1};                   // Vector: direction from the mid joint, 50 cm out
};

struct IkRigBody {
    std::string bone;                         // empty = no body solve
    std::string goal;                         // script name that offsets the pelvis (empty = none)
    f32 maxOffsetCm = 40.0f;
};

struct IkRigChainDef {
    std::string name, start, end;
    IkSolverKind solver = IkSolverKind::Fabrik;
    i32 priority = 0;                         // ascending = more important
    std::string goal;                         // script name (SetIkGoal); empty = no external goal
    u32 iterations = 10;
    f32 toleranceCm = 0.1f;
    f32 maxStepDeg = 30.0f;
    bool limits = false;                      // apply the limits list to this chain's bones
    bool tipRotation = false;                 // also follow the goal's rotation
    IkRigPole pole;
};

struct IkRigLimitDef {
    std::string bone;
    f32 swing1 = 180, swing2 = 180, twist = 180;   // degrees, ragdoll meaning
    bool hasAxis = false;
    Vec3 axis{1, 0, 0};                       // bone-local twist axis; default is IkSolvers' defaultLimitAxis
};

struct IkRigFootDef {
    std::string chain;                        // the leg chain; its end is the ankle
    std::string toe;
    f32 soleOffsetCm = -1.0f;                 // ankle height above the sole; -1 = derive at bind
    std::string plantCurve;                   // float curve on the playing clip (1 planted); empty = automatic
    f32 plantLowCm = 2.0f, plantHighCm = 10.0f;
    f32 probeUpCm = 50.0f, probeDownCm = 60.0f;
    bool alignToNormal = true;
    f32 maxAlignDeg = 30.0f;
};

enum class IkPelvisMode : u8 { Lowest = 0, None = 1 };

struct IkRigFootSettings {
    IkPelvisMode pelvisMode = IkPelvisMode::Lowest;
    f32 smoothing = 14.0f;                    // 1/s of the critically damped follow
    f32 maxPelvisDropCm = 40.0f;
    f32 maxFootOffsetCm = 45.0f;
    u32 layerMask = 0xFFFFFFFFu;              // physics layers the probe may hit
};

struct IkRigAsset {
    std::string name;
    std::string skeleton;                     // content-relative path of the .ocskel
    IkRigBody body;
    std::vector<IkRigChainDef> chains;
    std::vector<IkRigLimitDef> limits;
    std::vector<IkRigFootDef> feet;
    IkRigFootSettings foot;

    // Unique chain and goal names, priority >= 0, iterations 1..64, finite non-negative tolerances and
    // angles, limits in [0,180], feet naming real chains, probe distances > 0. Does not check bones
    // against a skeleton (that is bind time).
    bool valid(std::string* why = nullptr) const;
};

std::string writeIkRig(const IkRigAsset& a);
bool parseIkRig(std::string_view json, IkRigAsset& out, std::string* why = nullptr);
bool saveIkRig(const std::string& path, const IkRigAsset& a, std::string* why = nullptr);
bool loadIkRig(const std::string& path, IkRigAsset& out, std::string* why = nullptr);

} // namespace aver::anim
