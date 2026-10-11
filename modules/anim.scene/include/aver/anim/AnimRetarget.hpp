#pragma once
// Runtime retargeting: a clip authored for skeleton A just plays on skeleton B when a .ocretarget in the
// project says how. The first time (clip, skeleton) is asked for, the clip is baked by retargetClip and kept
// until AnimSystem::clear(); everything after is one map lookup. Design: docs/plans/wave5/IK_RETARGETING.md 4.6.
//
// The match is by FILE STEM: a clip's skeletonRef is a stem hint, the retargeter's `source` names the stem it
// accepts, and the entity's skeleton file has a stem too. Two different skeletons that share a stem would be
// treated as the same (documented risk). No catalog entry, or a clip already authored for this skeleton, means
// the plain clip, untouched (the same pointer AnimSystem::clip returns).
#include "aver/anim/Retarget.hpp"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace aver::anim {

class AnimSystem;

class RetargetRuntime {
public:
    // Replaces the catalog with every .ocretarget the host lists (content id, absolute path). Each is read once
    // for its `source` / `target`: the key is (hash of the lowercase source stem, id of the target skeleton path).
    // Baked clips are dropped (the assets may have changed). Safe to call again when content is hot-added.
    void registerAssets(const std::vector<std::pair<u64, std::string>>& idAndPath);

    // The clip as it should play on skeleton `skelId`: the baked retarget when a catalog entry matches `plain`'s
    // skeleton stem and skelId, otherwise `plain` itself. A failed bake is remembered (one warning) and falls
    // back to `plain`.
    const fmt::OcAnimation* resolve(AnimSystem& anim, u64 clipId, const fmt::OcAnimation* plain, u64 skelId);

    // Drops baked clips and bound retargeters; the catalog stays.
    void dropBaked();
    // Drops everything, catalog included.
    void clear();

    bool empty() const { return catalog_.empty(); }
    u32 catalogSize() const { return static_cast<u32>(catalog_.size()); }
    u32 bakedCount() const { return static_cast<u32>(owned_.size()); }
    // Bumped whenever the catalog is replaced; holders of resolved clip pointers rebind when it moves.
    u32 epoch() const { return epoch_; }

    // Hash of the lowercase file stem of a path or stem ("Characters/Hero.ocskel" and "hero" agree).
    static u64 stemHash(const std::string& pathOrStem);

private:
    struct Entry { u64 assetId = 0; std::string path; };
    struct Binder {
        Retargeter rt;
        const fmt::OcSkeleton* src = nullptr;
        bool ok = false;
    };
    using Key = std::pair<u64, u64>;

    std::map<Key, Entry> catalog_;                              // (source stem hash, target skeleton id)
    std::map<Key, const fmt::OcAnimation*> resolved_;           // (clip id, skeleton id) -> what to play
    std::vector<std::unique_ptr<fmt::OcAnimation>> owned_;      // the baked clips
    std::map<Key, std::unique_ptr<Binder>> binders_;            // (retarget asset id, target skeleton id)
    u32 epoch_ = 0;
};

} // namespace aver::anim
