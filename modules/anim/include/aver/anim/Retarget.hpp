#pragma once
// The retargeter: poses of a SOURCE skeleton, carried onto a TARGET skeleton with different bones, proportions
// and rest pose, by chain. Design: docs/plans/wave5/IK_RETARGETING.md 3.4.
//
// THE METHOD, in one paragraph. Every source bone's model-space rotation DELTA from its rest pose
// (D = Q_pose * conj(Q_rest)) is a world-space rotation, so it does not depend on either skeleton's bone axes.
// Each target bone of a chain gets the delta of the source bone at the same fraction along the chain (resampled
// when the bone counts differ) applied to its own rest rotation, after a swing-only alignment of the two rests'
// bone directions (the automatic retarget pose: a T-pose source arm raised 90 degrees raises an A-pose target
// arm to the same direction). The pelvis translation is scaled by the rest heights, a root bone above it moves by
// its own scaled delta, and IK chains (legs, usually) are solved to goals taken from the source so feet land
// where scaled feet land, never stretching.
//
// One function, three users: the editor preview, the batch tool (retargetFolder -> new .ocanim files) and the
// runtime bake (AnimSystem::clipFor).
#include "aver/anim/IkSolvers.hpp"
#include "aver/anim/RetargetAsset.hpp"

#include <string>
#include <vector>

namespace aver::anim {

struct RetargetIssue {
    enum class Kind : u8 { Info, Warn, Error } kind = Kind::Info;
    std::string text;
};

struct RetargetOptions {
    f32 fps = 30.0f;
    f32 rotToleranceDeg = 0.05f;
    f32 posToleranceCm = 0.05f;
    bool reduceKeys = true;
};

class Retargeter {
public:
    // Resolves every name to indices on the two skeletons, builds the per-chain resampling tables and the auto
    // scales. False + issues when the asset does not fit (missing bone, overlap, bad path). The skeletons are
    // copied, so the Retargeter outlives them.
    bool bind(const RetargetAsset& a, const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst,
              std::vector<RetargetIssue>* issues = nullptr);
    bool bound() const { return ok_; }

    // srcPose (local, source bone order) -> dstPose (written whole: target rest, then retargeted).
    // Allocation-free after the first call. NOT thread-safe (it keeps scratch buffers).
    void retarget(const Pose& srcPose, Pose& dstPose) const;

    f32 scaleH() const { return scaleH_; }
    f32 scaleV() const { return scaleV_; }
    // Target bones that get a rotation track when a clip is baked, ascending.
    const std::vector<u32>& mappedDstBones() const { return mapped_; }
    // The subset that also gets a translation track: the pelvis, the root-motion bone and `scaled` chains.
    const std::vector<u32>& translatedDstBones() const { return translated_; }
    // The file stem of the asset's target skeleton: what a baked clip's skeletonRef says.
    const std::string& targetStem() const { return targetStem_; }
    const std::string& sourceStem() const { return sourceStem_; }

private:
    struct Chain {
        RtgMode mode = RtgMode::Fk;
        std::vector<u32> src, dst;
        // Per target bone: the bracketing source bones (indices into `src`) and the blend; a == b when direct.
        std::vector<u32> a, b;
        std::vector<f32> f;
        std::vector<Quat> align;              // swing-only rest alignment, model space, per target bone
        std::vector<f32> posRatio;            // translation:scaled, per target bone
        bool scaled = false;
        // IK
        RtgAnchor anchor = RtgAnchor::Root;
        RtgScale scale;
        bool poleFk = true;
        bool tipRotation = false;
        IkChain ik;
        f32 srcLen = 1.0f, dstLen = 1.0f;
        Vec3 srcRestTipRel{0, 0, 0}, dstRestTipRel{0, 0, 0};   // IK end bones at rest, in the root frame
        Quat startAlign = Quat::identity();                     // rest rotation of the source chain vector onto the target's
    };
    struct BoneRole { i32 chain = -1; u32 k = 0; bool pelvis = false; bool rootMotion = false; };

    bool ok_ = false;
    fmt::OcSkeleton src_, dst_;
    std::vector<Chain> chains_;
    std::vector<BoneRole> dstRole_;
    std::vector<Quat> srcRestQ_, dstRestQ_;           // rest model rotations
    std::vector<Vec3> srcRestPos_, dstRestPos_;       // rest model positions
    i32 srcPelvis_ = -1, dstPelvis_ = -1, srcRootBone_ = -1, dstRootBone_ = -1;
    bool frame_ = false;                              // a root bone above the pelvis defines the frame positions use
    Vec3 srcRestRel_{0, 0, 0}, dstRestRel_{0, 0, 0};  // pelvis rest positions (root-relative = model at rest)
    Mat4 srcRestRootModel_, dstRestRootInv_;        // the root bone: source rest model matrix, target inverse rest model matrix
    f32 scaleH_ = 1.0f, scaleV_ = 1.0f, rootScale_ = 1.0f;
    Vec3 offset_{0, 0, 0};
    std::vector<u32> mapped_, translated_;
    std::string targetStem_, sourceStem_;

    // scratch
    mutable std::vector<Mat4> sModel_, dModel_;
    mutable std::vector<Quat> sq_, dq_, delta_;
    mutable std::vector<Quat> fkTip_;
    mutable IkScratch ikScratch_;
};

// Output: Keyframed, Linear, sampled at opt.fps from 0 to in.duration inclusive (a final key at the duration),
// one track per mapped target bone (rotation always; translation for the pelvis, the root-motion bone and
// `scaled` chains; no scale). duration, flags (loop, root motion; NOT additive), curves, notifies and notify
// durations are copied verbatim; skeletonRef = the target skeleton's file stem. reduceKeys drops keys whose
// removal moves the value less than the tolerances (rotation as an angle, position in cm), per track, keeping
// the first and last. Additive and object clips are refused (false + Error issue).
bool retargetClip(const Retargeter& rt, const fmt::OcAnimation& in, const fmt::OcSkeleton& src, fmt::OcAnimation& out,
                  const RetargetOptions& opt, std::vector<RetargetIssue>* issues);

// Whole-folder driver for the editor panel and the CLI: clips whose skeleton does not match `source` are skipped
// with an issue; additive clips (kOcAnimAdditiveBase) and object clips (kOcAnimObject) are skipped with a
// warning. Output name = <stem><suffix>.ocanim in outDir; never overwrites a file that is not a previous
// retarget output (one whose skeletonRef is the target stem) unless `overwrite`.
struct RetargetBatchResult {
    u32 written = 0, skipped = 0, failed = 0;
    std::vector<std::string> files;
    std::vector<RetargetIssue> issues;
};
RetargetBatchResult retargetFolder(const RetargetAsset& asset, const std::string& srcSkelPath, const std::string& dstSkelPath,
                                   const std::vector<std::string>& clipPaths, const std::string& outDir,
                                   const std::string& suffix, const RetargetOptions& opt, bool overwrite);

// The file stem of a path, as a skeleton's clips name it in skeletonRef ("Characters/hero.ocskel" -> "hero").
std::string pathStem(const std::string& path);

} // namespace aver::anim
