// The parse cache behind level instances: .ocworld files by content path, their ids, their nested-instance
// closure (cycle and depth checks), a hash of what an instance of them depends on, and the "child with this
// instance's overrides applied" view the realiser builds entities from. docs/LEVEL_INSTANCES.md section 2.4.
//
// PURE: Core + Formats only, so it compiles in a tree with no scene. The host supplies the loader (content index to
// fmt::loadOcworld), which is how the editor prefers an unsaved in-memory level over the one on disk.
#pragma once

#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"
#include "aver/formats/OcWorld.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace aver::world {

struct LevelAsset {
    std::string key;                       // content-relative path as authored
    fmt::OcWorldData data;                 // ids filled in memory (a placement without one gets a stable derived id)
    u64 contentHash = 0;                   // FNV-1a over the instanced records: placements, lights, decals, prefabs, foliage names, nested records
    std::vector<std::string> childAssets;  // distinct LEVELINST assets, file order
    bool idsFromFile = true;               // false when any id above was derived in memory
};

// One instance's child with its overrides applied, in the instance's own frame.
struct ResolvedContents {
    fmt::OcWorldData world;                // placements/lights/decals/prefabs/foliage/levelInstances only (section 2.3); removed placements and their subtrees dropped
    std::vector<u64> placementIds;         // parallel to world.placements
    u32 applied = 0, unmatched = 0;        // unmatched overrides are counted, never fatal
};

// The local transform a placement / instance record describes (the narrowing instantiate() does).
Transform levelPlacementTransform(const fmt::OcWorldPlacement& p);
Transform levelInstanceTransform(const fmt::OcLevelInstance& r);

class LevelAssetLibrary {
public:
    static constexpr u32 kMaxDepth = 8;                         // PrefabLibrary::resolve's cap
    using Loader = std::function<bool(const std::string& path, fmt::OcWorldData& out, std::string* why)>;
    // Vetoes a rewrite of a child file (the editor refuses while that level is the open, dirty one).
    using WriteGuard = std::function<bool(const std::string& path, std::string* why)>;

    void setLoader(Loader l);                                   // host: content index -> loadOcworld
    void setWriteGuard(WriteGuard g) { guard_ = std::move(g); }
    // A loader that reads `<contentDir>/<path>` from disk.
    static Loader diskLoader(std::string contentDir);

    // Parses once. The pointer stays valid for the library's life (a put or invalidate refills the same object).
    const LevelAsset* get(const std::string& path, std::string* why = nullptr);
    void put(const std::string& path, fmt::OcWorldData data);   // a commit or an external reload
    void invalidate(const std::string& path);                   // next get re-parses
    void clear() { cache_.clear(); loads_ = 0; }                 // drops every asset: only with nothing realised from it
    // Assigns ids to the asset's placements and nested instances, and rewrites `absoluteFile` once if any were missing.
    bool ensureIds(const std::string& path, const std::string& absoluteFile, std::string* why);
    // Depth-first over LEVELINST. False with `chain` = "A -> B -> A" on a cycle, or "depth > 8".
    // `order` is every distinct asset reached, root first.
    bool closure(const std::string& root, std::vector<std::string>& order, std::string* chain);
    // True when instancing `child` inside `container` would create a cycle (child is, or transitively contains, container).
    bool wouldCycle(const std::string& container, const std::string& child, std::string* chain = nullptr);
    u64 closureHash(const std::string& root);                   // asset hash combined with every nested asset's; pack staleness key
    std::vector<std::string> usersOf(const std::string& path) const;   // cached assets that directly instance it
    bool resolve(const fmt::OcLevelInstance& rec, ResolvedContents& out, std::string* why = nullptr);
    // World AABB of the instance (EXTENT through the TRS; nested fallback). False = unknown.
    bool instanceBounds(const fmt::OcLevelInstance& rec, AABB& out);

    // The authored key of a cached asset whose forward-slash path hashes (fnv1a64) to `hash`; empty when none.
    std::string keyForHash(u64 hash) const;
    static std::string normalise(const std::string& path);      // case-folded, '\' mapped to '/'
    u32 loadCount() const { return loads_; }                    // loader calls made (tests)

private:
    struct Entry {
        std::unique_ptr<LevelAsset> asset;
        bool valid = false;
    };
    bool fill(Entry& e, const std::string& path, fmt::OcWorldData data);
    bool visit(const std::string& key, u32 depth, std::vector<std::string>& stack, std::vector<std::string>& order,
               std::vector<std::string>& done, std::string* chain);
    bool boundsRec(const std::string& path, u32 depth, AABB& out);

    Loader loader_;
    WriteGuard guard_;
    std::unordered_map<std::string, Entry> cache_;
    u32 loads_ = 0;
};

} // namespace aver::world
