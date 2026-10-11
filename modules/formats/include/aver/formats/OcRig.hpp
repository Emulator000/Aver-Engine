#pragma once
// .ocrig -- a control rig: a short list of operations that edit a posed skeleton every frame.
//
// A rig is what makes an animation react. The clip says where the arm swings; the rig says the hand
// stays on the grip while it does, or the foot sits on the slope actually under it. It runs in
// AnimSystem's pose-modifier seam, between sampling the clip and computing skinning matrices.
//
// TEXT, NOT AN AVR1 CONTAINER, and that is a deliberate break from .ocbt next door. A behaviour tree
// is BAKED by a tool; a rig is short, hand-authorable and read far more often than it is written --
// four ops is a whole rig, where a .ocmesh is a megabyte of vertices. .ocgraph made the same call for
// the same reason, and it is the one that lets the demo rig in a project be opened and understood in
// a text editor rather than requiring a tool to exist first.
//
// BONES ARE NAMED HERE AND RESOLVED TO INDICES AT LOAD, exactly as OcSocket::bone is. A rig that
// stored indices would break the moment a skeleton gained a bone, and skeletons gain bones.
//
// ---------------------------------------------------------------------------------------------
// OCRIG 1
// NAME ArmReach
// # everything after a # is a comment
// OP twobone root=shoulder mid=elbow tip=wrist goal=80,0,0 pole=0,-100,20 weight=1
// OP aim bone=head at=0,200,150 axis=0,0,1 weight=0.5
// ---------------------------------------------------------------------------------------------
//
// OCRIG 2 (docs/plans/wave5/IK_RETARGETING.md 3.5) adds named runtime INPUTS a script sets, references
// in place of literals (`@name` an input, `bone:name+x,y,z` a bone's position in the pose as it stands),
// and the ops fabrik, ccd, copybone and setbone:
//
//   INPUT GunTarget vec3 space=world default=0,0,0
//   OP fabrik root=upperarm_r tip=hand_r goal=@GunTarget pole=bone:elbow_r+0,-30,0 iter=10 tol=0.1 limits=1 weight=@Aim
//   OP copybone dst=ik_hand_l src=hand_r space=model pos=1 rot=1 weight=1
//   OP setbone bone=weapon pos=0,0,0 rot=0,0,0,1 space=local weight=1
//
// The writer emits OCRIG 1 (byte-identical to what it always wrote) unless the data needs 2.
//
// EVERY OP CARRIES ITS OWN WEIGHT, and this is the format's one genuinely open question. The scout
// (docs/recon/control-rig.md) declined to decide whether a rig LAYERS on the sampled pose or
// OVERWRITES it, on the grounds that the answer wants a real rig in hand: `addPose` argues for
// layering, a planted foot argues for overwriting. Per-op weight is how this file avoids choosing --
// weight 1 overwrites that op's bones, weight 0 leaves the clip alone, and between them is a blend.
// **Decided without a real rig to test it against**, and cheap to revisit while nothing has authored
// one.
#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::fmt {

enum class OcRigOpKind : u32 {
    // Bends a three-joint chain so `tip` reaches `goal`. `pole` picks which way it bends -- see
    // anim::twoBoneIk, which this is the authored form of.
    TwoBoneIk = 0,
    // Turns one bone so its `axis` points at `target`. A head that watches something, a turret.
    AimAt = 1,
    // v2: chain IK over the parent path root..tip (anim::solveChain). Fabrik suits arms and tails, Ccd spines.
    Fabrik = 2,
    Ccd = 3,
    // v2: copies a bone's position and/or rotation from another bone, as the pose stands.
    CopyBone = 4,
    // v2: sets a bone's position and/or rotation outright.
    SetBone = 5,
};

// A value that is a literal, a named runtime input, or a bone's position in the pose when the op runs.
// A literal keeps living in OcRigOp::target / hint / rotation, so v1 data is untouched.
struct OcRigRef {
    enum class Kind : u8 { Literal = 0, Input = 1, Bone = 2 } kind = Kind::Literal;
    std::string name;            // the input, or the bone
    Vec3 offset{0, 0, 0};        // Bone: a model-space offset added to the bone's position
};

enum class OcRigInputType : u8 { Float = 0, Vec3 = 1, Quat = 2 };

// A named value a script sets at run time (ControlRigSystem::setInput); `default*` applies until it does.
struct OcRigInput {
    std::string name;
    OcRigInputType type = OcRigInputType::Vec3;
    bool world = true;           // Vec3 / Quat: world space (converted to model space at apply) or model space
    Vec3 defVec{0, 0, 0};
    Quat defQuat{0, 0, 0, 1};
    f32 defFloat = 0.0f;
};

// One operation. Which of the fields matter depends on `kind`, and `valid()` below is what says so
// rather than a reader guessing -- an op naming bones it does not use is a rig that will confuse the
// next person to open it.
struct OcRigOp {
    OcRigOpKind kind = OcRigOpKind::TwoBoneIk;

    // TwoBoneIk uses all three; AimAt uses `root` only, as the bone it turns. Fabrik / Ccd: root..tip is
    // the chain. CopyBone: `root` is the SOURCE bone. SetBone: `root` is the bone set.
    std::string root, mid, tip;

    // TwoBoneIk / Fabrik / Ccd: the goal, in the skeleton's own model space, centimetres.
    // AimAt:     the point to look at, same space.
    // SetBone:   the position to set.
    Vec3 target{0, 0, 0};

    // TwoBoneIk / Fabrik / Ccd: the pole that picks the bend direction.
    // AimAt:     the bone-local axis that should end up pointing at the target.
    Vec3 hint{0, 0, 1};

    // 0 leaves the sampled pose alone, 1 applies the op fully. See the header note on why this is
    // per-op rather than a property of the rig.
    f32 weight = 1.0f;

    // ---- v2. Every field below is inert for a v1 op; the writer emits OCRIG 2 only if one is set.
    OcRigRef targetRef, hintRef, rotRef;   // `target`/`hint`/`rotation` as a reference when kind != Literal
    std::string weightInput;               // weight=@name (a float input); empty = the literal `weight`
    u32 iterations = 0;                    // Fabrik / Ccd; 0 = the op's default (10 / 12)
    f32 tolerance = 0.0f;                  // cm; 0 = default (0.1 / 0.5)
    f32 maxStepDeg = 0.0f;                 // Ccd per-joint step; 0 = default (30)
    f32 rotWeight = 1.0f;                  // tiprotw=
    bool limits = false;                   // Fabrik / Ccd: apply joint limits
    bool hasPole = false;                  // Fabrik / Ccd: pole= given (none = keep the bend the pose had)
    bool hasTipRot = false;                // TwoBoneIk / Fabrik / Ccd: `rotation` / `rotRef` is a tip rotation
    Quat rotation{0, 0, 0, 1};             // literal tip rotation (tiprot=) or SetBone's rot=
    bool copyPos = true, copyRot = true;   // CopyBone: what to copy. SetBone: whether pos= / rot= were given
    bool modelSpace = true;                // CopyBone / SetBone: space=model (default) or local
    std::string dst;                       // CopyBone: the bone written
};

struct OcRigData {
    std::string name;
    std::vector<OcRigInput> inputs;        // v2
    std::vector<OcRigOp> ops;

    // Every op names the bones its kind needs, no name is empty, and every weight is in [0,1].
    // v2: input names unique and non-empty, every @name names an input of the matching type, fabrik/ccd
    // name distinct root and tip, iterations 1..64 when given, bone: offsets finite.
    // Does NOT check that the bones exist -- a rig is validated against a FILE here and against a
    // SKELETON at load, and conflating the two would make a rig unopenable without its skeleton.
    bool valid(std::string* why = nullptr) const;

    // True when the data cannot be expressed in OCRIG 1 (any INPUT, new op kind, reference or v2 field).
    bool needsV2() const;
    const OcRigInput* input(const std::string& name) const;
};

// Parses `text`. False on a malformed line, with `err` set to what and where.
bool parseOcRig(std::string_view text, OcRigData& out, std::string* err = nullptr);

// Reads `path`. False if it cannot be opened or does not parse.
bool loadOcRig(const std::string& path, OcRigData& out, std::string* err = nullptr);

// Serialises to the text form above. Round-trips through parseOcRig.
std::string writeOcRig(const OcRigData& rig);

// Writes `path`. False on an I/O failure.
bool saveOcRig(const std::string& path, const OcRigData& rig, std::string* err = nullptr);

} // namespace aver::fmt
