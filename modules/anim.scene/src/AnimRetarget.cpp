// Runtime retarget catalog and bake cache. See AnimRetarget.hpp.
#include "aver/anim/AnimRetarget.hpp"

#include "aver/anim/AnimSystem.hpp"
#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"

#include <algorithm>
#include <cctype>

namespace aver::anim {

u64 RetargetRuntime::stemHash(const std::string& pathOrStem) {
    std::string s = pathStem(pathOrStem);
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return fnv1a64(std::string_view(s));
}

void RetargetRuntime::dropBaked() {
    resolved_.clear();
    binders_.clear();
    owned_.clear();
}

void RetargetRuntime::clear() {
    dropBaked();
    catalog_.clear();
    ++epoch_;
}

void RetargetRuntime::registerAssets(const std::vector<std::pair<u64, std::string>>& idAndPath) {
    dropBaked();
    catalog_.clear();
    for (const auto& [id, path] : idAndPath) {
        RetargetAsset a;
        std::string why;
        if (!loadRetarget(path, a, &why)) {
            AVER_WARN("[Retarget] could not read {}: {}", path, why);
            continue;
        }
        if (a.source.empty() || a.target.empty()) {
            AVER_WARN("[Retarget] {} names no source or target skeleton; ignored", path);
            continue;
        }
        std::string target = a.target;
        std::replace(target.begin(), target.end(), '\\', '/');
        const Key key{stemHash(a.source), fnv1a64(std::string_view(target))};
        if (catalog_.count(key)) AVER_WARN("[Retarget] {} duplicates another retargeter for the same skeletons; the later one wins", path);
        catalog_[key] = Entry{id, path};
    }
    ++epoch_;
}

const fmt::OcAnimation* RetargetRuntime::resolve(AnimSystem& anim, u64 clipId, const fmt::OcAnimation* plain, u64 skelId) {
    if (catalog_.empty() || !plain || skelId == 0) return plain;
    const Key rk{clipId, skelId};
    if (const auto it = resolved_.find(rk); it != resolved_.end()) return it->second;

    const fmt::OcAnimation* result = plain;
    do {
        if (plain->skeletonRef.empty() || (plain->flags & (fmt::kOcAnimObject | fmt::kOcAnimAdditiveBase))) break;
        const std::string dstPath = anim.assetPath(skelId);
        if (dstPath.empty()) break;                                     // not resolvable yet; remembered below, see note
        const u64 clipStem = stemHash(plain->skeletonRef);
        if (clipStem == stemHash(dstPath)) break;                       // authored for this skeleton
        const auto cat = catalog_.find(Key{clipStem, skelId});
        if (cat == catalog_.end()) break;

        const Key bk{cat->second.assetId, skelId};
        auto b = binders_.find(bk);
        if (b == binders_.end()) {
            auto binder = std::make_unique<Binder>();
            RetargetAsset asset;
            std::string why;
            if (!loadRetarget(cat->second.path, asset, &why)) {
                AVER_WARN("[Retarget] could not read {}: {}", cat->second.path, why);
            } else {
                std::string srcPath = asset.source;
                std::replace(srcPath.begin(), srcPath.end(), '\\', '/');
                binder->src = anim.skeleton(fnv1a64(std::string_view(srcPath)));
                const fmt::OcSkeleton* dst = anim.skeleton(skelId);
                std::vector<RetargetIssue> issues;
                if (!binder->src || !dst) {
                    AVER_WARN("[Retarget] {}: the source or target skeleton does not load", cat->second.path);
                } else if (!binder->rt.bind(asset, *binder->src, *dst, &issues)) {
                    for (const RetargetIssue& i : issues)
                        if (i.kind == RetargetIssue::Kind::Error) AVER_WARN("[Retarget] {}: {}", cat->second.path, i.text);
                } else {
                    binder->ok = true;
                }
            }
            b = binders_.emplace(bk, std::move(binder)).first;
        }
        if (!b->second->ok) break;

        auto baked = std::make_unique<fmt::OcAnimation>();
        std::vector<RetargetIssue> issues;
        if (!retargetClip(b->second->rt, *plain, *b->second->src, *baked, RetargetOptions{}, &issues)) {
            AVER_WARN("[Retarget] clip 0x{:016X} could not be retargeted onto skeleton 0x{:016X}: {}", clipId, skelId,
                      issues.empty() ? std::string("unknown") : issues.back().text);
            break;
        }
        owned_.push_back(std::move(baked));
        result = owned_.back().get();
    } while (false);
    resolved_[rk] = result;
    return result;
}

} // namespace aver::anim
