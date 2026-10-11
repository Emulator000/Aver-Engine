// Animation montages: a timeline of clip segments with named sections, blend in/out, notifies and
// root motion, played imperatively on one slot of an entity. Pure over asset structs and clip
// pointers, so timing, section chains and events are decidable with no scene and no GPU.
// The asset is .ocmontage (AnimGraphAsset.hpp); design and semantics: docs/ANIM_MONTAGES.md.
#pragma once

#include "aver/anim/Pose.hpp"
#include "aver/anim/RootMotion.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace aver::anim {

enum class BlendCurve : u8 { Linear = 0, Smooth = 1 };

// Plays clip seconds [start, end] (end < 0 = the clip's duration) at `rate`, so it occupies
// (end - start) / rate montage seconds. The clip's own loop flag is ignored.
struct MontageSegment { std::string clip; f32 start = 0.0f; f32 end = -1.0f; f32 rate = 1.0f; };
// next: "" = the following section in time order, "@end" = the montage ends with this section,
// a section's own name = loop.
struct MontageSection { std::string name; f32 start = 0.0f; std::string next; };
// duration > 0 makes a notify STATE: "<Name>_Begin" / "<Name>_End".
struct MontageNotify  { f32 time = 0.0f; std::string name; f32 duration = 0.0f; };

inline constexpr const char* kDefaultSlot = "DefaultSlot";
inline constexpr const char* kMontageEnd = "@end";

struct MontageAsset {
    std::string name;
    std::string slot = kDefaultSlot;
    f32 blendIn = 0.15f, blendOut = 0.2f, blendOutTrigger = -1.0f, rate = 1.0f;
    BlendCurve inCurve = BlendCurve::Smooth, outCurve = BlendCurve::Smooth;
    i32 additive = -1;                           // -1 auto (from the first clip's flag), 0 no, 1 yes
    std::vector<MontageSegment> segments;
    std::vector<MontageSection> sections;        // kept sorted by start
    std::vector<MontageNotify>  notifies;

    bool valid(std::string* why = nullptr) const;
    i32  sectionIndex(std::string_view name) const;     // -1 if absent
};

struct MontageEvent {
    enum class Kind : u8 { Started, Notify, NotifyBegin, NotifyEnd, SectionStarted, BlendOut, Interrupted, Ended };
    Kind kind = Kind::Started;
    std::string name;                            // notify or section name; empty for the lifecycle kinds
};
using MontageClipFn = std::function<const fmt::OcAnimation*(const std::string&)>;

class MontagePlayer {
public:
    // Resolves clips and the timeline. False when no segment resolves to a clip (nothing to play).
    // Closes any notify state a previous binding left open.
    bool bind(const MontageAsset* asset, MontageClipFn clips);
    // section < 0 = the first. blendInOverride < 0 = the asset's. Restarts a running player, ending
    // the old play first (Interrupted, Ended).
    void start(i32 section, f32 rate, f32 blendInOverride);
    // blendOutOverride < 0 = the asset's, 0 = cut. An early stop raises Interrupted (once) before
    // BlendOut, closes open notify states and silences further notifies; the position keeps running.
    void stop(f32 blendOutOverride);
    // Ends the play now (entity death, level change): closes open notify states and raises Ended once.
    void finish();
    // Immediate; weight kept; raises SectionStarted; no notifies for the skipped span.
    bool jumpToSection(i32 section);
    // to < 0 restores the authored link. False when an index is out of range.
    bool setNextSection(i32 from, i32 to);
    void setRate(f32 r);                                     // >= 0, 0 pauses
    void setRootMotion(const RootSpec* spec);                // null = off; copied
    // Clock, section chain, weight, notifies, root delta.
    void advance(f32 dt);
    // Samples ONTO a copy of `base`, so bones the montage does not animate keep the base. An additive
    // montage does addPose(base, sample, firstFrameRef, 1). The weight is not applied here: the slot
    // blends by coverage.
    void evaluate(const fmt::OcSkeleton& skel, const Pose& base, Pose& out) const;
    void drainEvents(std::vector<MontageEvent>& out);
    RootDelta consumeRootDelta();                            // unweighted
    bool  hasRootMotion() const { return rootMotion_; }      // a segment clip passes clipHasRootMotion
    f32   weight() const;                                    // 0..1 after the curve
    f32   position() const { return pos_; }                  // montage seconds
    f32   length() const { return length_; }
    f32   rate() const { return rate_; }                     // instance rate (asset rate not included)
    i32   section() const { return cur_; }
    bool  finished() const { return state_ == State::Done; } // Ended already raised
    bool  additive() const { return additive_; }
    const MontageAsset* asset() const { return asset_; }
    // Name of section `i` as the player sees it (a synthesized "Default" when the asset has none).
    const std::string& sectionName(i32 i) const;
    i32  sectionCount() const { return static_cast<i32>(secs_.size()); }

private:
    enum class State : u8 { Idle, In, Hold, Out, Done };
    struct Seg {
        const fmt::OcAnimation* clip = nullptr;
        f32 cs = 0.0f, ce = 0.0f;       // clip seconds actually played
        f32 rate = 1.0f;
        f32 ms = 0.0f, len = 0.0f;      // montage start and length
    };
    struct Sec {
        std::string name;
        f32 start = 0.0f, end = 0.0f;
        i32 link = -1;                  // kFollow, kEnd, or a section index
    };
    static constexpr i32 kFollow = -1, kEnd = -2, kNoOverride = -3;

    void raise(MontageEvent::Kind k, const std::string& name = {});
    void closeStates();
    void finishEnded();
    void beginBlendOut(f32 seconds);
    void travel(f32 amount);
    void moveTo(f32 np);
    void enterSection(i32 idx);
    void closeOutside();
    void checkAutoBlendOut();
    i32  nextOf(i32 cur) const;
    void refreshRootFlag();

    const MontageAsset* asset_ = nullptr;
    std::vector<Seg> segs_;
    std::vector<Sec> secs_;
    std::vector<i32> override_;
    std::vector<f32> noteTimes_;
    std::vector<u8> open_;
    std::vector<MontageEvent> events_;
    std::vector<u32> crossed_;
    f32 length_ = 0.0f;
    f32 pos_ = 0.0f;
    f32 rate_ = 1.0f;
    f32 a_ = 0.0f;                     // linear blend alpha
    f32 inTime_ = 0.0f, outTime_ = 0.0f;
    i32 cur_ = 0;
    State state_ = State::Idle;
    bool additive_ = false;
    bool inclusive_ = false;           // the next travelled interval includes its start point
    bool atEnd_ = false;               // the section chain ended; position holds
    bool notifying_ = false;
    bool rootMotion_ = false;
    bool hasSpec_ = false;
    RootSpec spec_;
    RootDelta rootAcc_;
    mutable Pose sampled_, ref_;
    mutable bool refReady_ = false;
};

} // namespace aver::anim
