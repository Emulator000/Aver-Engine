#pragma once
// .ocretarget -- a retargeter asset: which chains of a SOURCE skeleton drive which chains of a TARGET
// skeleton, and how. Design: docs/plans/wave5/IK_RETARGETING.md 3.4. The math is in Retarget.hpp.
//
// Text JSON, written and parsed like .ocikrig / .ocmontage: nine significant digits, unknown keys ignored,
// a newer version refused, parse does not call valid(). Bones are named; skeletons are content-relative
// paths whose hash is their asset id.
#include "aver/anim/Pose.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::anim {

inline constexpr int kRetargetVersion = 1;

// "auto" or a number.
struct RtgScale {
    bool autoScale = true;
    f32 value = 1.0f;
};

enum class RtgMode : u8 { Fk = 0, Ik = 1, None = 2 };
enum class RtgRotation : u8 { Interpolate = 0, OneToOne = 1 };
enum class RtgTranslation : u8 { None = 0, Scaled = 1 };
enum class RtgAnchor : u8 { Auto = 0, Root = 1, Start = 2 };
enum class RtgPole : u8 { Fk = 0, None = 1 };
enum class RtgUnmapped : u8 { Rest = 0, Inherit = 1 };

struct RetargetRoot {
    std::string srcBone, dstBone;     // the pelvis on each side; its translation is scaled
    RtgScale scaleH, scaleV;
    Vec3 offsetCm{0, 0, 0};
};

struct RetargetRootMotion {
    std::string srcBone, dstBone;     // a ground "root" above the pelvis; empty = none
    RtgScale scale;                   // auto = scaleH
};

struct RetargetChainDef {
    std::string name;
    std::string srcStart, srcEnd;     // a parent path, inclusive; a single bone has start == end
    std::string dstStart, dstEnd;
    RtgMode mode = RtgMode::Fk;
    RtgRotation rotation = RtgRotation::Interpolate;
    RtgTranslation translation = RtgTranslation::None;
    RtgAnchor anchor = RtgAnchor::Auto;       // ik chains
    RtgScale scale;                           // ik chains: overrides the anchor's factor
    RtgPole pole = RtgPole::Fk;
    bool tipRotation = false;
};

struct RetargetAsset {
    std::string name;
    std::string source, target;       // content-relative .ocskel paths
    RetargetRoot root;
    RetargetRootMotion rootMotion;
    RtgUnmapped unmapped = RtgUnmapped::Rest;
    std::vector<RetargetChainDef> chains;

    // Names present and chain names unique; no skeleton needed.
    bool valid(std::string* why = nullptr) const;
    // Against two skeletons: every name exists, every path is parent-linked, no bone is in two chains on one
    // side, oneToOne chains have equal counts, and root.dstBone is an ancestor-or-self of every target chain.
    bool valid(const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst, std::string* why = nullptr) const;
};

std::string writeRetarget(const RetargetAsset& a);
bool parseRetarget(std::string_view json, RetargetAsset& out, std::string* why = nullptr);
bool saveRetarget(const std::string& path, const RetargetAsset& a, std::string* why = nullptr);
bool loadRetarget(const std::string& path, RetargetAsset& out, std::string* why = nullptr);

// How sure the auto-mapper is of one proposed chain: identical normalised names, a synonym-table match, or a
// purely structural pairing.
enum class RtgConfidence : u8 { Exact = 0, Synonym = 1, Structural = 2 };

struct AutoMapResult {
    RetargetAsset asset;
    std::vector<RtgConfidence> confidence;    // parallel to asset.chains
};

// Proposes chains between two skeletons by bone-name logic (prefix stripping, side and role extraction with a
// synonym table), falling back to structural chains (hierarchy split at branches and leaves, paired by side and
// order) for bones the table does not understand. A DRAFT: callers show the confidence and accept it explicitly.
AutoMapResult autoMapChains(const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst);

} // namespace aver::anim
