#include "aver/world/LevelPack.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcLevelInst.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <tuple>

namespace aver::world {

namespace {

struct GroupKey {
    std::string asset, material;
    bool collide = false;
    bool operator<(const GroupKey& o) const {
        return std::tie(asset, material, collide) < std::tie(o.asset, o.material, o.collide);
    }
};

struct Baker {
    LevelAssetLibrary& lib;
    const PackBakeOptions& opt;
    PackBakeReport& rep;
    std::map<GroupKey, u32> groupIndex;                 // first-appearance order, via `order`
    std::vector<GroupKey> order;
    std::vector<std::vector<f32>> rows;                 // per group
    std::vector<std::string> keep;
    std::map<std::pair<std::string, std::string>, bool> packableCache;
    std::string error;

    bool meshPackable(const std::string& asset, const std::string& material) {
        if (!opt.meshPackable) return true;
        const auto key = std::make_pair(asset, material);
        if (const auto it = packableCache.find(key); it != packableCache.end()) return it->second;
        std::string whyNot;
        const bool ok = opt.meshPackable(asset, material, &whyNot);
        if (!ok) AVER_INFO("[LevelPack] '{}' ({}) stays an entity: {}", asset, material.empty() ? "own material" : material, whyNot);
        packableCache.emplace(key, ok);
        return ok;
    }

    bool walk(const ResolvedContents& rc, const Mat4& base, const std::vector<u64>& prefix, u32 depth) {
        if (depth > LevelAssetLibrary::kMaxDepth) { error = "level instances nest deeper than 8"; return false; }
        const std::vector<fmt::OcWorldPlacement>& ps = rc.world.placements;
        std::vector<Mat4> m(ps.size());
        std::vector<char> blocked(ps.size(), 0);   // a class / animated / vehicle / component placement or something under one
        std::vector<char> pack(ps.size(), 0);
        u32 tentative = 0;
        for (usize i = 0; i < ps.size(); ++i) {
            const fmt::OcWorldPlacement& p = ps[i];
            const Mat4 local = levelPlacementTransform(p).toMatrix();
            const bool hasParent = p.parent >= 0 && static_cast<usize>(p.parent) < i;
            m[i] = hasParent ? local * m[static_cast<usize>(p.parent)] : local * base;
            const bool own = !p.className.empty() || !p.vehiclePreset.empty() || !p.animClip.empty() || !p.components.empty();
            blocked[i] = own || (hasParent && blocked[static_cast<usize>(p.parent)]);
            if (blocked[i] || !p.visible || p.snapToGround || p.asset.empty() || !meshPackable(p.asset, p.material)) continue;
            if (rep.rows + tentative >= opt.maxRows) { rep.capped = true; continue; }
            pack[i] = 1;
            ++tentative;
        }
        // A kept placement needs its parents live in the realised level, so its ancestors stay entities too (parents
        // precede children, so one reverse sweep carries it up the whole chain).
        for (usize i = ps.size(); i-- > 0;) {
            if (pack[i] || ps[i].parent < 0 || static_cast<usize>(ps[i].parent) >= i) continue;
            pack[static_cast<usize>(ps[i].parent)] = 0;
        }
        for (usize i = 0; i < ps.size(); ++i) {
            const fmt::OcWorldPlacement& p = ps[i];
            if (pack[i]) {
                const GroupKey gk{p.asset, p.material, p.collide};
                auto [it, fresh] = groupIndex.try_emplace(gk, static_cast<u32>(order.size()));
                if (fresh) { order.push_back(gk); rows.emplace_back(); }
                std::vector<f32>& r = rows[it->second];
                for (int a = 0; a < 4; ++a) for (int c = 0; c < 3; ++c) r.push_back(m[i].m[a][c]);
                ++rep.rows;
                continue;
            }
            if (!blocked[i]) ++rep.notPackable;
            std::vector<u64> path = prefix;
            path.push_back(rc.placementIds[i]);
            keep.push_back(fmt::ocLevelInstJoinIds(path));
            ++rep.kept;
        }
        for (const fmt::OcLevelInstance& rec : rc.world.levelInstances) {
            ResolvedContents sub;
            std::string why;
            if (!lib.resolve(rec, sub, &why)) { error = "nested level '" + rec.asset + "': " + why; return false; }
            std::vector<u64> childPrefix = prefix;
            childPrefix.push_back(rec.id);
            if (!walk(sub, levelInstanceTransform(rec).toMatrix() * base, childPrefix, depth + 1)) return false;
        }
        return true;
    }
};

} // namespace

std::string levelPackFlatName(const std::string& levelAsset) {
    std::string s = levelAsset;
    const usize dot = s.find_last_of('.');
    const usize slash = s.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) s.erase(dot);
    for (char& c : s) if (c == '/' || c == '\\') c = '_';
    return s;
}

std::string levelPackTablePath(const std::string& levelAsset) {
    return "Binaries/LevelPacks/" + levelPackFlatName(levelAsset) + ".ocinst";
}

std::string levelPackSidecarPath(const std::string& levelAsset) {
    return "Binaries/LevelPacks/" + levelPackFlatName(levelAsset) + ".ocpack";
}

bool bakeLevelPack(LevelAssetLibrary& lib, const std::string& levelAsset, const std::string& contentDir,
                   const std::string& projectDir, const PackBakeOptions& opt, PackBakeReport& report,
                   std::string* why) {
    (void)contentDir;   // child levels come through the library's loader
    report = PackBakeReport{};
    const auto fail = [&](const std::string& m) { if (why) *why = m; return false; };

    std::vector<std::string> order;
    std::string chain;
    if (!lib.closure(levelAsset, order, &chain)) return fail("cannot pack a level with a cycle or too deep a nest: " + chain);

    // The asset as an instance with NO overrides: an instance that has any is never packed (section 2.8.4).
    fmt::OcLevelInstance top;
    top.asset = levelAsset;
    ResolvedContents rc;
    std::string err;
    if (!lib.resolve(top, rc, &err)) return fail(err.empty() ? "could not load " + levelAsset : err);

    Baker b{lib, opt, report, {}, {}, {}, {}, {}, {}};
    if (!b.walk(rc, Mat4::identity(), {}, 0)) return fail(b.error);

    fmt::OcInstanceData table;
    fmt::OcPack pack;
    pack.source = lib.closureHash(levelAsset);
    pack.table = levelPackTablePath(levelAsset);
    for (usize g = 0; g < b.order.size(); ++g) {
        fmt::OcInstanceGroup grp;
        grp.asset = b.order[g].asset;
        grp.flags = fmt::kOcInstanceFlagCastShadow;
        grp.first = static_cast<u32>(table.transforms.size() / 12);
        grp.count = static_cast<u32>(b.rows[g].size() / 12);
        table.groups.push_back(std::move(grp));
        table.transforms.insert(table.transforms.end(), b.rows[g].begin(), b.rows[g].end());
        pack.groups.push_back(fmt::OcPackGroup{b.order[g].collide, b.order[g].material});
    }
    pack.keep = std::move(b.keep);
    report.groups = static_cast<u32>(table.groups.size());

    const std::filesystem::path root(projectDir);
    std::error_code mkec;
    std::filesystem::create_directories((root / levelPackTablePath(levelAsset)).parent_path(), mkec);
    std::string werr;
    if (!fmt::saveOcInstances((root / levelPackTablePath(levelAsset)).string(), table, &werr))
        return fail("could not write the instance table: " + werr);
    if (!fmt::saveOcPack((root / levelPackSidecarPath(levelAsset)).string(), pack, &werr))
        return fail("could not write the pack sidecar: " + werr);
    AVER_INFO("[LevelPack] {}: {} row(s) in {} group(s), {} placement(s) kept as entities ({} not packable){}",
              levelAsset, report.rows, report.groups, report.kept, report.notPackable,
              report.capped ? " -- row cap reached" : "");
    return true;
}

bool loadLevelPack(const std::string& projectDir, const std::string& levelAsset, u64 expectedSource,
                   LoadedLevelPack& out, bool* stale, std::string* why) {
    out = LoadedLevelPack{};
    const std::filesystem::path root(projectDir);
    std::string err;
    if (!fmt::loadOcPack((root / levelPackSidecarPath(levelAsset)).string(), out.pack, &err)) {
        if (why) *why = err;
        return false;
    }
    if (!fmt::loadOcInstances((root / out.pack.table).string(), out.table, &err)) {
        if (why) *why = err;
        return false;
    }
    if (!fmt::ocPackMatchesTable(out.pack, out.table, &err)) {
        if (why) *why = err;
        return false;
    }
    if (stale) *stale = out.pack.source != expectedSource;
    return true;
}

} // namespace aver::world
