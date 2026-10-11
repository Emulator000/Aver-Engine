// The motion matcher: plays a PoseDatabase by periodically searching it for the frame that best fits
// where the character is going, and switching to it with inertialization.
//
// PATENT RULES (binding, from docs/ANIM_MOTION_MATCHING.md section 2). Do not add anything that breaks
// them without a patent re-check:
//   1. one feature vector, one search over the whole database;
//   2. no phase, contact-phase, frequency or amplitude features;
//   3. no body-part-partitioned search (an upper/lower body split is a future item, not a quiet option);
//   4. the pose part of the query is read from the DATABASE ROW being played, never from a Pose after
//      any modifier / IK / ragdoll / montage touched it: nothing here reads the output skeleton back;
//   5. no neural components.
#pragma once

#include "aver/anim/Inertializer.hpp"
#include "aver/anim/PoseDatabase.hpp"
#include "aver/anim/RootMotion.hpp"
#include "aver/anim/Trajectory.hpp"

#include <vector>

namespace aver::anim {

// Per tick, from the component / state. A value <= 0 means "the database default".
struct MotionParams {
    f32 searchInterval = 0.0f, playRate = 1.0f, inertHalfLife = 0.0f;
    bool forceSearch = false, paused = false;
};

struct MotionInfo {
    u32 range = kNoRow, row = kNoRow;     // what is playing now (row = the database row at the playhead)
    f32 clipTime = 0, sinceSwitch = 0;
    f32 cost = 0;                         // best hit of the last search
    f32 currentCost = 0;                  // the playing row before that decision
    f32 playingCost = 0;                  // the playing row after it
    u32 switches = 0;
    bool searched = false, switched = false;   // this tick
};

struct MotionCandidate { u32 row; f32 cost; };

// The normalised search vector for a query: the pose groups come from `poseRow` (a stored database row of the
// motion being played, patent rule 4) or are the database mean when null; root velocity and the trajectory
// groups come from `q`. `out` gets dimPadded floats.
void motionQueryVector(const PoseDatabase& db, const MotionQuery& q, const f32* poseRow, std::vector<f32>& out);

class MotionMatcher {
public:
    // Resolves each range's clip. False (and unbound) when the skeleton's bone count / names disagree
    // with the database. The database, skeleton and the clips `clips` returns must outlive the matcher.
    bool bind(const PoseDatabase* db, const fmt::OcSkeleton* skel, MotionClipFn clips);
    bool bound() const { return db_ != nullptr; }
    void unbind();
    const PoseDatabase* database() const { return db_; }

    // Report root deltas (axes as RootSpec::axes). The root lock is always on, so the mesh stays on the
    // entity either way.
    void setRootMotion(bool on, u32 axes);
    // Advance the clock, maybe search and switch, update the pose and the root delta.
    void tick(f32 dt, const MotionQuery& q, const MotionParams& p);

    // The locked-root, inertialized pose.
    const Pose& pose() const { return pose_; }
    // Copies pose(); the rest pose when unbound or nothing has played yet.
    void evaluate(const fmt::OcSkeleton& skel, Pose& out) const;
    RootDelta consumeRootDelta();
    const MotionInfo& info() const { return info_; }
    f32 sinceSearch() const { return sinceSearch_; }

    // Debug: the best hit of the last search, and the cost of every group against the last query for the
    // row that was playing and for the best hit.
    const std::vector<MotionCandidate>& lastCandidates() const { return candidates_; }
    void lastGroupCosts(std::vector<f32>& current, std::vector<f32>& best) const;
    // fnv1a64 of the playing range's clip path (0 when none).
    u64 clipHash() const;

private:
    void samplePose(u32 range, f32 t, Pose& out) const;
    RootDelta deltaOver(u32 range, f32 t0, f32 t1, bool wrapped) const;

    const PoseDatabase* db_ = nullptr;
    const fmt::OcSkeleton* skel_ = nullptr;
    std::vector<const fmt::OcAnimation*> rangeClips_;
    std::vector<u64> rangeHash_;
    Transform rootRest_;

    u32 range_ = kNoRow;
    f32 clipTime_ = 0.0f;
    bool rowValid_ = false;          // a playing row exists and has not run off the end of a non-loop range
    bool havePose_ = false;
    Pose pose_, prevPose_, toPose_, toPrev_, scratch_;
    Inertializer inert_;
    f32 sinceSearch_ = 0.0f, sinceSwitch_ = 0.0f, lastDt_ = 0.0f;

    bool rootOn_ = false;
    u32 rootAxes_ = kRootAxesDefault;
    RootDelta rootAcc_;

    MotionInfo info_;
    std::vector<MotionCandidate> candidates_;
    std::vector<f32> query_;
    u32 lastCurRow_ = kNoRow, lastBestRow_ = kNoRow;
};

} // namespace aver::anim
