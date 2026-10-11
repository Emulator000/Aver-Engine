// .ocmotion: the authoring asset of a motion-matching pose database. A skeleton, the feature layout
// (which bones, which trajectory samples, how much each counts), search / spring / inertialization
// defaults, and a list of .ocanim clips with tags and cost biases.
//
// Text JSON with a tag and a version, like .ocasm. Parsing does NOT call valid(): a half-authored
// asset must still round-trip. Writes are deterministic (floats %.9g, fixed key order), so a re-save
// is byte-identical. Design: docs/ANIM_MOTION_MATCHING.md.
#pragma once

#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::anim {

inline constexpr int kMotionAssetVersion = 1;
inline constexpr u32 kMaxMotionTags = 32;
inline constexpr u32 kMaxTrajectorySamples = 8;

struct MotionBoneFeature { std::string bone; f32 pos = 1.0f, vel = 1.0f; };
struct MotionTagRange   { f32 start = 0.0f, end = -1.0f; std::vector<std::string> tags; bool noSearch = false; };
struct MotionClipEntry {
    std::string clip;
    f32 start = 0.0f, end = -1.0f;       // clip seconds; end < 0 = the clip's duration
    i32 loop = -1;                       // -1 auto (the clip's loop flag), 0 no, 1 yes
    f32 bias = 0.0f;                     // added to the cost of every row of the entry
    bool noSearch = false;               // playable (continuation) but never a candidate
    std::vector<std::string> tags;       // entry-wide
    std::vector<MotionTagRange> ranges;  // sub-ranges: extra tags and/or no-search
};

enum class SearchAccel : u8 { Auto = 0, Linear = 1, KdTree = 2 };

struct MotionSearchDefaults {
    f32 interval = 0.1f, switchMargin = 0.25f, acceptCost = -1.0f, continuityWindow = 0.25f;
    SearchAccel accel = SearchAccel::Auto;
    u32 kdMinRows = 40000;
};
struct MotionSpringDefaults { f32 velocityHalfLife = 0.2f, turnHalfLife = 0.15f, historySeconds = 1.0f, faceSpeedMin = 5.0f; };

struct MotionAsset {
    std::string name, skeleton, rootBone;
    u32 sampleRate = 30;
    std::vector<std::string> tags;
    std::vector<MotionBoneFeature> bones;
    f32 rootVelocity = 1.0f;
    // Seconds relative to a row: future > 0, past < 0, each list ascending in magnitude.
    std::vector<f32> futureTimes{0.2f, 0.4f, 0.7f, 1.0f}, pastTimes{-0.2f, -0.5f};
    f32 futurePos = 1.0f, futureDir = 0.8f, pastPos = 0.5f;
    MotionSearchDefaults search;
    MotionSpringDefaults spring;
    f32 inertHalfLife = 0.1f;
    std::vector<MotionClipEntry> clips;

    // The rules of docs/ANIM_MOTION_MATCHING.md (existence of bones and clips is checked at build).
    bool valid(std::string* why = nullptr) const;
    // Index of a declared tag, -1 if absent.
    i32 tagIndex(std::string_view name) const;
};

std::string writeMotionAsset(const MotionAsset&);
bool parseMotionAsset(std::string_view json, MotionAsset& out, std::string* why = nullptr);
bool saveMotionAsset(const std::string& path, const MotionAsset&, std::string* why = nullptr);
bool loadMotionAsset(const std::string& path, MotionAsset& out, std::string* why = nullptr);

} // namespace aver::anim
