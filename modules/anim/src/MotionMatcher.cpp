// The motion matcher. See MotionMatcher.hpp for the patent rules this file lives under.
#include "aver/anim/MotionMatcher.hpp"

#include "aver/anim/AnimSampler.hpp"
#include "aver/core/Hash.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace aver::anim {

void motionQueryVector(const PoseDatabase& db, const MotionQuery& q, const f32* poseRow, std::vector<f32>& out) {
    const u32 dp = db.dimPadded();
    std::vector<f32> base(dp, 0.0f), raw(dp, 0.0f);
    if (poseRow) std::copy(poseRow, poseRow + dp, base.begin());
    db.denormalise(base.data(), raw.data());
    for (const FeatureGroup& g : db.groups()) {
        f32* o = raw.data() + g.firstDim;
        switch (g.kind) {
            case FeatureKind::RootVel:
                o[0] = q.rootVel[0]; o[1] = q.rootVel[1]; o[2] = q.rootVel[2];
                break;
            case FeatureKind::FuturePos:
            case FeatureKind::FutureDir:
            case FeatureKind::PastPos:
                for (u32 i = 0; i < g.count / 2 && i < kMaxTrajectorySamples; ++i) {
                    const f32(*src)[2] = g.kind == FeatureKind::FuturePos ? q.futurePos
                                       : g.kind == FeatureKind::FutureDir ? q.futureDir : q.pastPos;
                    o[2 * i] = src[i][0];
                    o[2 * i + 1] = src[i][1];
                }
                break;
            default: break;   // BonePos / BoneVel stay as the playing row had them
        }
    }
    out.assign(dp, 0.0f);
    db.normalise(raw.data(), out.data());
}

bool MotionMatcher::bind(const PoseDatabase* db, const fmt::OcSkeleton* skel, MotionClipFn clips) {
    unbind();
    if (!db || !skel || db->ranges().empty()) return false;
    const std::vector<std::string>& names = db->boneNames();
    if (names.size() != skel->bones.size()) return false;
    for (usize i = 0; i < names.size(); ++i)
        if (names[i] != skel->bones[i].name) return false;
    if (db->rootBone() >= skel->bones.size()) return false;

    db_ = db;
    skel_ = skel;
    const fmt::OcBone& rb = skel->bones[db->rootBone()];
    rootRest_ = Transform{rb.translation, rb.rotation, rb.scale};
    for (const MotionRange& r : db->ranges()) {
        rangeClips_.push_back(clips ? clips(r.clip) : nullptr);
        rangeHash_.push_back(fnv1a64(std::string_view(r.clip)));
    }
    restPose(*skel, pose_);
    prevPose_ = pose_;
    info_ = MotionInfo{};
    return true;
}

void MotionMatcher::unbind() {
    db_ = nullptr;
    skel_ = nullptr;
    rangeClips_.clear();
    rangeHash_.clear();
    range_ = kNoRow;
    clipTime_ = 0.0f;
    rowValid_ = false;
    havePose_ = false;
    pose_ = prevPose_ = toPose_ = toPrev_ = scratch_ = Pose{};
    inert_.reset();
    sinceSearch_ = sinceSwitch_ = lastDt_ = 0.0f;
    rootAcc_ = RootDelta{};
    info_ = MotionInfo{};
    candidates_.clear();
    lastCurRow_ = lastBestRow_ = kNoRow;
}

void MotionMatcher::setRootMotion(bool on, u32 axes) {
    rootOn_ = on;
    rootAxes_ = axes ? axes : kRootAxesDefault;
    if (!on) rootAcc_ = RootDelta{};
}

RootDelta MotionMatcher::consumeRootDelta() {
    const RootDelta d = rootAcc_;
    rootAcc_ = RootDelta{};
    return d;
}

u64 MotionMatcher::clipHash() const {
    return (db_ && range_ < rangeHash_.size()) ? rangeHash_[range_] : 0;
}

void MotionMatcher::evaluate(const fmt::OcSkeleton& skel, Pose& out) const {
    if (havePose_ && pose_.matches(skel)) out = pose_;
    else restPose(skel, out);
}

void MotionMatcher::samplePose(u32 range, f32 t, Pose& out) const {
    restPose(*skel_, out);
    if (range < rangeClips_.size() && rangeClips_[range]) sampleAnimation(*rangeClips_[range], t, out);
}

RootDelta MotionMatcher::deltaOver(u32 range, f32 t0, f32 t1, bool wrapped) const {
    if (range >= rangeClips_.size() || !rangeClips_[range]) return {};
    const fmt::OcAnimation& c = *rangeClips_[range];
    RootSpec s;
    s.bone = db_->rootBone();
    s.axes = rootAxes_;
    s.rest = rootRest_;
    if (!wrapped) return rootDeltaUnflagged(c, s, t0, t1, false);
    // The loop seam of a range is its end, not necessarily the clip's: finish the lap, then start the next.
    const MotionRange& r = db_->ranges()[range];
    RootDelta d = rootDeltaUnflagged(c, s, t0, r.end, false);
    composeDelta(d, rootDeltaUnflagged(c, s, r.start, t1, false));
    return d;
}

void MotionMatcher::lastGroupCosts(std::vector<f32>& current, std::vector<f32>& best) const {
    const usize n = db_ ? db_->groups().size() : 0;
    current.assign(n, 0.0f);
    best.assign(n, 0.0f);
    if (!db_ || query_.empty()) return;
    for (u32 g = 0; g < n; ++g) {
        if (lastCurRow_ != kNoRow) current[g] = db_->groupCost(query_.data(), lastCurRow_, g);
        if (lastBestRow_ != kNoRow) best[g] = db_->groupCost(query_.data(), lastBestRow_, g);
    }
}

void MotionMatcher::tick(f32 dt, const MotionQuery& q, const MotionParams& p) {
    if (!db_) return;
    dt = std::max(dt, 0.0f);
    const MotionAsset& defs = db_->defaults();
    const f32 rate = std::max(p.playRate, 0.0f);
    const f32 interval = p.searchInterval > 0.0f ? p.searchInterval : defs.search.interval;
    inert_.setHalfLife(p.inertHalfLife > 0.0f ? p.inertHalfLife : defs.inertHalfLife);
    info_.searched = info_.switched = false;
    inert_.advance(dt);
    sinceSearch_ += dt;
    sinceSwitch_ += dt;

    // Clock, and the root travel of the interval just played.
    if (rowValid_ && !p.paused && range_ != kNoRow) {
        const MotionRange& r = db_->ranges()[range_];
        const f32 t0 = clipTime_;
        f32 t1 = t0 + dt * rate;
        bool wrapped = false, ended = false;
        if (r.loop) {
            if (t1 >= r.end) { t1 = r.start + std::fmod(t1 - r.start, r.end - r.start); wrapped = true; }
        } else if (t1 >= r.end) {
            t1 = r.end;
            ended = true;
        }
        if (rootOn_) composeDelta(rootAcc_, deltaOver(range_, t0, t1, wrapped));
        clipTime_ = t1;
        if (ended) rowValid_ = false;   // the clip ran out: the next search must find a continuation
    }

    const bool search = !p.paused && q.valid && (p.forceSearch || !rowValid_ || sinceSearch_ >= interval);
    if (search) {
        // The query: pose groups from the stored row being played (patent rule 4: never from the output
        // pose), the rest from the world. Without a playing row the pose groups are the database mean.
        const u32 curRow = rowValid_ ? db_->rowOf(range_, clipTime_) : kNoRow;
        motionQueryVector(*db_, q, curRow != kNoRow ? db_->row(curRow) : nullptr, query_);

        const SearchFilter filter{q.require, q.exclude};
        const SearchHit hit = db_->search(query_.data(), filter, defs.search.accel);
        sinceSearch_ = 0.0f;
        info_.searched = true;
        lastCurRow_ = curRow;
        lastBestRow_ = hit.row;
        candidates_.clear();
        if (hit.row != kNoRow) candidates_.push_back({hit.row, hit.cost});

        // A playing row that breaks the tag filter has to be left whatever it costs.
        const bool violates = curRow != kNoRow &&
            ((db_->meta(curRow).tagMask & q.require) != q.require || (db_->meta(curRow).tagMask & q.exclude) != 0);
        const f32 currentCost = curRow == kNoRow ? std::numeric_limits<f32>::infinity()
                              : violates ? std::numeric_limits<f32>::infinity() : db_->cost(query_.data(), curRow);
        info_.cost = hit.row == kNoRow ? 0.0f : hit.cost;
        info_.currentCost = curRow == kNoRow ? 0.0f : db_->cost(query_.data(), curRow);
        info_.playingCost = info_.currentCost;

        if (hit.row != kNoRow) {
            const u32 hr = db_->meta(hit.row).range;
            const f32 ht = db_->timeOf(hit.row);
            bool take = false;
            if (!rowValid_) {
                take = true;
            } else {
                const MotionRange& cr = db_->ranges()[range_];
                f32 gap = std::fabs(ht - clipTime_);
                if (cr.loop) gap = std::min(gap, (cr.end - cr.start) - gap);
                const bool sameMotion = !violates && hr == range_ && gap <= defs.search.continuityWindow;
                if (!sameMotion)
                    take = violates || (hit.cost + defs.search.switchMargin < currentCost &&
                                        (defs.search.acceptCost < 0.0f || hit.cost <= defs.search.acceptCost));
            }
            if (take) {
                const f32 dtPrev = std::max(lastDt_ > 0.0f ? lastDt_ : dt, 1.0f / 240.0f);
                if (havePose_) {
                    const MotionRange& nr = db_->ranges()[hr];
                    f32 tp = ht - dtPrev * std::max(rate, 1e-3f);
                    if (nr.loop && tp < nr.start) tp += nr.end - nr.start;
                    tp = std::min(std::max(tp, nr.start), nr.end);
                    samplePose(hr, ht, toPose_);
                    samplePose(hr, tp, toPrev_);
                    inert_.begin(pose_, prevPose_, toPose_, toPrev_, dtPrev, db_->rootBone());
                }
                range_ = hr;
                clipTime_ = ht;
                rowValid_ = true;
                sinceSwitch_ = 0.0f;
                ++info_.switches;
                info_.switched = true;
                info_.playingCost = hit.cost;
            }
        }
    }

    // The output pose: the playing clip, the inertialization offsets, the root locked to the entity.
    if (range_ != kNoRow) {
        samplePose(range_, clipTime_, scratch_);
        inert_.apply(scratch_);
        RootSpec lock;
        lock.bone = db_->rootBone();
        lock.axes = kRootAxisX | kRootAxisY | kRootAxisYaw | (rootAxes_ & kRootAxisZ);
        lock.rest = rootRest_;
        lockRoot(scratch_, lock);
        prevPose_ = havePose_ ? pose_ : scratch_;
        pose_ = scratch_;
        havePose_ = true;
    }
    lastDt_ = dt;

    info_.range = range_;
    info_.row = (rowValid_ && range_ != kNoRow) ? db_->rowOf(range_, clipTime_) : kNoRow;
    info_.clipTime = clipTime_;
    info_.sinceSwitch = sinceSwitch_;
}

} // namespace aver::anim
