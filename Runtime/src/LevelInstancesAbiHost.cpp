#include "aver/game/LevelInstancesAbiHost.hpp"

#if AVER_MODULE_SCENE && defined(AVER_WITH_LEVELINST_ABI)

namespace aver::game {
namespace {

LevelInstances& liOf(void* user) { return *static_cast<LevelInstances*>(user); }
scene::Entity ent(int32_t e) { return static_cast<scene::Entity>(e); }

int32_t abiSpawn(void* user, const char* path, int32_t parent, const float* xyz, const float* ypr, float scale,
                 int32_t packed) {
    LevelInstances& li = liOf(user);
    fmt::OcLevelInstance rec;
    rec.asset = path ? path : "";
    if (xyz) { rec.x = xyz[0]; rec.y = xyz[1]; rec.z = xyz[2]; }
    if (ypr) { rec.yaw = ypr[0]; rec.pitch = ypr[1]; rec.roll = ypr[2]; }
    const f64 k = scale == 0.0f ? 1.0 : static_cast<f64>(scale);
    rec.sx = rec.sy = rec.sz = k;
    rec.packed = packed != 0;
    const scene::Entity p = ent(parent);
    const bool parentOk = p != scene::kInvalidEntity && scene::World::instance().valid(p);
    return static_cast<int32_t>(li.spawn(rec, parentOk ? p : scene::kInvalidEntity, true));
}

int32_t abiDestroy(void* user, int32_t root) { return liOf(user).destroy(ent(root)) ? 1 : 0; }
int32_t abiRootOf(void* user, int32_t e) { return static_cast<int32_t>(liOf(user).rootOf(ent(e))); }
int32_t abiFind(void* user, int32_t root, const char* idPath) {
    return static_cast<int32_t>(liOf(user).find(ent(root), idPath ? idPath : ""));
}
int32_t abiSetLoaded(void* user, int32_t root, int32_t loaded) { return liOf(user).setLoaded(ent(root), loaded != 0) ? 1 : 0; }
int32_t abiIsLoaded(void* user, int32_t root) {
    LevelInstances& li = liOf(user);
    if (li.instanceIdOf(ent(root)) == 0) return -1;
    return li.loaded(ent(root)) ? 1 : 0;
}
int32_t abiReload(void* user, int32_t root) { return liOf(user).reload(ent(root)) ? 1 : 0; }
int32_t abiSetPacked(void* user, int32_t root, int32_t packed) { return liOf(user).setPacked(ent(root), packed != 0) ? 1 : 0; }
int32_t abiBounds(void* user, int32_t root, float* mn, float* mx) {
    AABB b;
    if (!liOf(user).bounds(ent(root), b)) return 0;
    mn[0] = b.min.x; mn[1] = b.min.y; mn[2] = b.min.z;
    mx[0] = b.max.x; mx[1] = b.max.y; mx[2] = b.max.z;
    return 1;
}
int32_t abiCount(void* user) { return static_cast<int32_t>(liOf(user).count()); }
int32_t abiAt(void* user, int32_t index) {
    return index < 0 ? 0 : static_cast<int32_t>(liOf(user).at(static_cast<u32>(index)));
}
int32_t abiAssetId(void* user, int32_t root, int64_t* out) {
    LevelInstances& li = liOf(user);
    if (li.instanceIdOf(ent(root)) == 0) return 0;
    *out = static_cast<int64_t>(li.assetIdOf(ent(root)));
    return 1;
}

} // namespace

AverLevelInstanceHost makeAbiHost(LevelInstances& li) {
    AverLevelInstanceHost h{};
    h.user = &li;
    h.spawn = &abiSpawn;
    h.destroy = &abiDestroy;
    h.root_of = &abiRootOf;
    h.find = &abiFind;
    h.set_loaded = &abiSetLoaded;
    h.is_loaded = &abiIsLoaded;
    h.reload = &abiReload;
    h.set_packed = &abiSetPacked;
    h.bounds = &abiBounds;
    h.count = &abiCount;
    h.at = &abiAt;
    h.asset_id = &abiAssetId;
    return h;
}

} // namespace aver::game

#endif
