// The baked pose database of a motion-matching asset: one feature row per sampled frame of every
// clip range, normalised and weighted so a plain squared distance is the match cost, plus exact
// nearest-neighbour search over it.
//
// Pure data and arithmetic (Core + Formats): no scene, no threads, no GPU. A database is immutable
// once built or loaded, so a worker thread may build one while the main thread keeps ticking.
//
// PATENT RULES apply to everything under docs/ANIM_MOTION_MATCHING.md section 2: one feature vector
// and one search; no phase / contact-phase features; no per-limb searches.
#pragma once

#include "aver/anim/MotionAsset.hpp"
#include "aver/formats/OcAnim.hpp"

#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace aver::anim {

inline constexpr u32 kMotionBuildVersion = 1;
inline constexpr u32 kNoRow = 0xFFFFFFFFu;
inline constexpr u8  kRowNoSearch = 1, kRowTail = 2;
inline constexpr u32 kKdDims = 8;

enum class FeatureKind : u8 { BonePos = 0, BoneVel = 1, RootVel = 2, FuturePos = 3, FutureDir = 4, PastPos = 5 };

struct FeatureGroup {
    FeatureKind kind = FeatureKind::BonePos;
    u32 bone = 0xFFFFFFFFu;              // skeleton index (BonePos / BoneVel), else none
    u32 firstDim = 0, count = 0;
    f32 weight = 1.0f, scale = 1.0f;
};

struct MotionRange {
    std::string clip;
    f32 start = 0, end = 0;              // clip seconds
    u32 firstRow = 0, rowCount = 0;
    bool loop = false;
    u32 tagMask = 0;                     // entry-wide tags
    f32 bias = 0;
    u32 rootBone = 0;
};

struct RowMeta { u32 tagMask = 0; u16 range = 0; u8 flags = 0; u8 pad = 0; };

// A row passes when (tags & require) == require && !(tags & exclude).
struct SearchFilter { u32 require = 0, exclude = 0; };
struct SearchHit { u32 row = kNoRow; f32 cost = std::numeric_limits<f32>::infinity(); };

// One node of the exact branch-and-bound tree over the 8-D PCA projection of the searchable rows.
struct KdNode {
    f32 lo[kKdDims], hi[kKdDims];
    u32 left, right;                     // child node indices; 0 on a leaf
    u32 rowBegin, rowEnd;                // leaf: a span of KdTree order
    u32 tagOr;                           // OR of the tag masks below
    u32 flagsAny;                        // 1 when any searchable row lies below
};
static_assert(sizeof(KdNode) == 88, "KdNode is stored as raw bytes in .ocmbake");

struct MotionBuildReport {
    u32 rows = 0, ranges = 0, skippedClips = 0;
    double seconds = 0;
    u64 bytes = 0;
    std::vector<std::string> warnings;
};
using MotionClipFn = std::function<const fmt::OcAnimation*(const std::string&)>;

class PoseDatabase {
public:
    PoseDatabase();
    ~PoseDatabase();
    PoseDatabase(PoseDatabase&&) noexcept;
    PoseDatabase& operator=(PoseDatabase&&) noexcept;
    PoseDatabase(const PoseDatabase&) = delete;
    PoseDatabase& operator=(const PoseDatabase&) = delete;

    u32 rows() const { return rows_; }
    u32 dim() const { return dim_; }
    u32 dimPadded() const { return dimPadded_; }            // a multiple of 8
    const f32* row(u32 r) const { return feat_.get() + static_cast<usize>(r) * dimPadded_; }   // 32-byte aligned
    const RowMeta& meta(u32 r) const { return meta_[r]; }
    const std::vector<MotionRange>& ranges() const { return ranges_; }
    const std::vector<FeatureGroup>& groups() const { return groups_; }
    const std::vector<f32>& futureTimes() const { return futureTimes_; }
    const std::vector<f32>& pastTimes() const { return pastTimes_; }
    // Search / spring / inertialization defaults: the asset minus its clips.
    const MotionAsset& defaults() const { return defaults_; }
    // The skeleton's bone names at build time: what bind() compares an entity's rig against.
    const std::vector<std::string>& boneNames() const { return boneNames_; }
    u32 rootBone() const { return rootBone_; }
    f32 sampleRate() const { return sampleRate_; }
    const std::vector<f32>& mean() const { return mean_; }

    // 0 when unknown; otherwise 1u << index of the tag whose name hashes to `nameHash` (fnv1a64).
    u32 tagBit(u64 nameHash) const;
    u32 tagMaskOf(const u64* hashes, u32 n) const;
    // The row at/before `clipSeconds` in a range, clamped to the range. kNoRow for a bad range.
    u32 rowOf(u32 range, f32 clipSeconds) const;
    // Clip seconds of a row.
    f32 timeOf(u32 row) const;

    // Query helpers; `out` has dimPadded floats.
    void normalise(const f32* rawFeatureVector, f32* out) const;     // (f - mean) * weight / scale
    void denormalise(const f32* rowOrQuery, f32* out) const;         // the inverse, for the debug view
    f32  groupCost(const f32* q, u32 row, u32 group) const;          // squared distance over one group
    f32  cost(const f32* q, u32 row) const;                          // squared distance + the range bias

    // Exact nearest searchable row. Auto uses the KD tree when the database has one, else the linear scan.
    SearchHit search(const f32* q, const SearchFilter&, SearchAccel accel = SearchAccel::Auto) const;
    SearchHit searchLinear(const f32* q, const SearchFilter&, bool scalarOnly = false) const;
    SearchHit searchKd(const f32* q, const SearchFilter&) const;     // linear when there is no tree
    // The k cheapest searchable rows, cheapest first (linear; for the editor's query lab and tests).
    void searchTopK(const f32* q, const SearchFilter&, u32 k, std::vector<SearchHit>& out) const;
    bool hasKd() const { return !kdNodes_.empty(); }
    u32  kdNodeCount() const { return static_cast<u32>(kdNodes_.size()); }
    u64  memoryBytes() const;

private:
    friend bool buildPoseDatabase(const MotionAsset&, const fmt::OcSkeleton&, const MotionClipFn&, PoseDatabase&,
                                  MotionBuildReport*, std::string*);
    friend bool savePoseDatabase(const std::string&, const PoseDatabase&, u64, std::string*);
    friend bool loadPoseDatabase(const std::string&, PoseDatabase&, u64*, std::string*);

    struct AlignedFree { void operator()(f32* p) const; };
    static f32* allocAligned(usize floats);
    void buildKd();                                                  // PoseSearch.cpp
    f32  distance(const f32* q, u32 row, f32 limit, bool scalarOnly) const;

    u32 rows_ = 0, dim_ = 0, dimPadded_ = 0;
    f32 sampleRate_ = 30.0f;
    u32 rootBone_ = 0;
    f32 minBias_ = 0.0f;
    std::unique_ptr<f32[], AlignedFree> feat_;
    std::vector<RowMeta> meta_;
    std::vector<MotionRange> ranges_;
    std::vector<FeatureGroup> groups_;
    std::vector<f32> mean_;
    std::vector<f32> futureTimes_, pastTimes_;
    std::vector<std::string> boneNames_, tagNames_;
    std::vector<u64> tagHashes_;
    std::vector<f32> rangeBias_;
    MotionAsset defaults_;
    // KD acceleration (optional): 8 orthonormal rows of dimPadded floats, nodes, leaf order.
    std::vector<f32> pca_;
    std::vector<KdNode> kdNodes_;
    std::vector<u32> kdOrder_;
};

// Builds from an asset, its skeleton and resolved clips. Pure and const over its inputs, so a worker
// thread may run it. Warnings (a missing bone, a clip that does not resolve, a clip whose root carries
// no motion) land in `report`; zero rows after skipping is a failure.
bool buildPoseDatabase(const MotionAsset&, const fmt::OcSkeleton&, const MotionClipFn&, PoseDatabase& out,
                       MotionBuildReport* report = nullptr, std::string* why = nullptr);

// avrHash64 over the builder version, the canonical asset text, the skeleton file bytes and each
// clip file's bytes in entry order. The sidecar is valid only for the exact key.
u64 motionSourceKey(const MotionAsset&, const std::vector<u8>& skeletonBytes, const std::vector<std::vector<u8>>& clipBytes);

// .ocmbake (AVR1, subtype MMDB). A load that fails for any reason is a MISS for the caller, not an error.
bool savePoseDatabase(const std::string& path, const PoseDatabase&, u64 sourceKey, std::string* why = nullptr);
bool loadPoseDatabase(const std::string& path, PoseDatabase& out, u64* sourceKey, std::string* why = nullptr);

} // namespace aver::anim
