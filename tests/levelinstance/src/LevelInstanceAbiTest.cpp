// The level-instance C ABI (level_instance_abi.h): no host, a fake host, null function pointers, bad arguments.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/levelinstance/level_instance_abi.h"

#include <cstring>
#include <string>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static int32_t err() { return aver_levelinst_last_error(); }
static constexpr int32_t kOk = 0, kBadHandle = -1, kNullPointer = -2, kNotInitialised = -3, kOutOfRange = -4,
                         kUnsupported = -5, kInvalidArgument = -6;

struct Recorded {
    std::string path;
    int32_t parent = -1;
    float xyz[3] = {0, 0, 0}, ypr[3] = {0, 0, 0}, scale = 0;
    int32_t packed = -1;
    int32_t destroyed = 0, loaded = -1, reloaded = 0, packedSet = -1;
    std::string idPath;
};

static int32_t hSpawn(void* u, const char* path, int32_t parent, const float* xyz, const float* ypr, float scale, int32_t packed) {
    auto* r = static_cast<Recorded*>(u);
    r->path = path;
    r->parent = parent;
    std::memcpy(r->xyz, xyz, sizeof r->xyz);
    std::memcpy(r->ypr, ypr, sizeof r->ypr);
    r->scale = scale;
    r->packed = packed;
    return path[0] == 'X' ? 0 : 42;
}
static int32_t hDestroy(void* u, int32_t root) { static_cast<Recorded*>(u)->destroyed = root; return root == 42 ? 1 : 0; }
static int32_t hRootOf(void*, int32_t e) { return e == 7 ? 42 : 0; }
static int32_t hFind(void* u, int32_t root, const char* id) { static_cast<Recorded*>(u)->idPath = id; return root == 42 ? 99 : 0; }
static int32_t hSetLoaded(void* u, int32_t root, int32_t l) { static_cast<Recorded*>(u)->loaded = l; return root == 42 ? 1 : 0; }
static int32_t hIsLoaded(void*, int32_t root) { return root == 42 ? 1 : (root == 43 ? 0 : -1); }
static int32_t hReload(void* u, int32_t root) { static_cast<Recorded*>(u)->reloaded = root; return root == 42 ? 1 : 0; }
static int32_t hSetPacked(void* u, int32_t root, int32_t p) { static_cast<Recorded*>(u)->packedSet = p; return root == 42 ? 1 : 0; }
static int32_t hBounds(void*, int32_t root, float* mn, float* mx) {
    if (root != 42) return 0;
    mn[0] = -1; mn[1] = -2; mn[2] = -3; mx[0] = 1; mx[1] = 2; mx[2] = 3;
    return 1;
}
static int32_t hCount(void*) { return 3; }
static int32_t hAt(void*, int32_t i) { return i < 3 ? 100 + i : 0; }
static int32_t hAssetId(void*, int32_t root, int64_t* out) { if (root != 42) return 0; *out = 0x1122334455667788ll; return 1; }

int main() {
    aver_levelinst_set_host(nullptr);
    // ---- no host ----
    check(aver_levelinst_abi_version() == AVER_LEVELINST_ABI_VERSION, "the version equals the macro");
    check(aver_levelinst_spawn("A.ocworld", 0, 0, 0, 0, 0, 0, 0, 1, 0) == 0 && err() == kNotInitialised, "spawn without a host is NotInitialised");
    check(aver_levelinst_destroy(1) == 0 && err() == kNotInitialised, "destroy without a host");
    check(aver_levelinst_root_of(1) == 0 && err() == kNotInitialised, "root_of without a host");
    check(aver_levelinst_is_member(1) == 0 && err() == kNotInitialised, "is_member without a host");
    check(aver_levelinst_find(1, "c3") == 0 && err() == kNotInitialised, "find without a host");
    check(aver_levelinst_set_loaded(1, 1) == 0 && err() == kNotInitialised, "set_loaded without a host");
    check(aver_levelinst_is_loaded(1) == 0 && err() == kNotInitialised, "is_loaded without a host");
    check(aver_levelinst_reload(1) == 0 && err() == kNotInitialised, "reload without a host");
    check(aver_levelinst_set_packed(1, 1) == 0 && err() == kNotInitialised, "set_packed without a host");
    float mn[3], mx[3];
    check(aver_levelinst_bounds(1, mn, mx) == 0 && err() == kNotInitialised, "bounds without a host");
    check(aver_levelinst_count() == 0 && err() == kNotInitialised, "count without a host");
    check(aver_levelinst_at(0) == 0 && err() == kNotInitialised, "at without a host");
    int64_t id = 0;
    check(aver_levelinst_asset_id(1, &id) == 0 && err() == kNotInitialised, "asset_id without a host");

    // ---- a fake host ----
    Recorded rec;
    AverLevelInstanceHost h{};
    h.user = &rec;
    h.spawn = &hSpawn; h.destroy = &hDestroy; h.root_of = &hRootOf; h.find = &hFind; h.set_loaded = &hSetLoaded;
    h.is_loaded = &hIsLoaded; h.reload = &hReload; h.set_packed = &hSetPacked; h.bounds = &hBounds; h.count = &hCount;
    h.at = &hAt; h.asset_id = &hAssetId;
    aver_levelinst_set_host(&h);

    check(aver_levelinst_spawn("Levels/Room.ocworld", 5, 1, 2, 3, 10, 20, 30, 2.5f, 1) == 42 && err() == kOk, "spawn reaches the host and succeeds");
    check(rec.path == "Levels/Room.ocworld" && rec.parent == 5 && rec.xyz[0] == 1 && rec.xyz[1] == 2 && rec.xyz[2] == 3 &&
              rec.ypr[0] == 10 && rec.ypr[1] == 20 && rec.ypr[2] == 30 && rec.scale == 2.5f && rec.packed == 1,
          "spawn passes the exact arguments");
    check(aver_levelinst_spawn("X.ocworld", 0, 0, 0, 0, 0, 0, 0, 1, 0) == 0 && err() == kInvalidArgument, "a refused spawn is InvalidArgument");
    check(aver_levelinst_spawn(nullptr, 0, 0, 0, 0, 0, 0, 0, 1, 0) == 0 && err() == kInvalidArgument, "a null path is InvalidArgument");
    check(aver_levelinst_spawn("", 0, 0, 0, 0, 0, 0, 0, 1, 0) == 0 && err() == kInvalidArgument, "an empty path is InvalidArgument");
    check(aver_levelinst_destroy(42) == 1 && err() == kOk && rec.destroyed == 42, "destroy succeeds");
    check(aver_levelinst_destroy(9) == 0 && err() == kBadHandle, "destroy of a non-root is BadHandle");
    check(aver_levelinst_root_of(7) == 42 && err() == kOk, "root_of answers");
    check(aver_levelinst_root_of(8) == 0 && err() == kOk, "root_of of a non-member is an answer, not an error");
    check(aver_levelinst_is_member(7) == 1 && aver_levelinst_is_member(8) == 0, "is_member");
    check(aver_levelinst_find(42, "b2/c9") == 99 && rec.idPath == "b2/c9", "find passes the id path");
    check(aver_levelinst_find(42, nullptr) == 99 && rec.idPath.empty(), "a null id path is the root");
    check(aver_levelinst_find(41, "c3") == 0 && err() == kBadHandle, "find on a bad root is BadHandle");
    check(aver_levelinst_set_loaded(42, 0) == 1 && rec.loaded == 0, "set_loaded");
    check(aver_levelinst_is_loaded(42) == 1 && aver_levelinst_is_loaded(43) == 0 && err() == kOk, "is_loaded 1 and 0");
    check(aver_levelinst_is_loaded(44) == -1 && err() == kBadHandle, "is_loaded of a bad root is -1 with BadHandle");
    check(aver_levelinst_reload(42) == 1 && rec.reloaded == 42, "reload");
    check(aver_levelinst_set_packed(42, 1) == 1 && rec.packedSet == 1 && aver_levelinst_set_packed(41, 1) == 0 && err() == kOk,
          "set_packed: 0 is an answer");
    check(aver_levelinst_bounds(42, mn, mx) == 1 && mn[0] == -1 && mx[2] == 3, "bounds");
    check(aver_levelinst_bounds(42, nullptr, mx) == 0 && err() == kNullPointer, "bounds with a null out pointer is NullPointer");
    check(aver_levelinst_bounds(41, mn, mx) == 0 && err() == kOk, "unknown bounds are an answer");
    check(aver_levelinst_count() == 3 && aver_levelinst_at(1) == 101, "count and at");
    check(aver_levelinst_at(5) == 0 && err() == kOutOfRange, "at past the end is OutOfRange");
    check(aver_levelinst_asset_id(42, &id) == 1 && id == 0x1122334455667788ll, "asset_id");
    check(aver_levelinst_asset_id(42, nullptr) == 0 && err() == kNullPointer, "asset_id with a null out pointer is NullPointer");

    // ---- a host missing a function ----
    AverLevelInstanceHost partial{};
    partial.user = &rec;
    partial.count = &hCount;
    aver_levelinst_set_host(&partial);
    check(aver_levelinst_count() == 3 && err() == kOk, "a provided function works");
    check(aver_levelinst_reload(1) == 0 && err() == kUnsupported, "a null function pointer is Unsupported");
    check(aver_levelinst_spawn("A.ocworld", 0, 0, 0, 0, 0, 0, 0, 1, 0) == 0 && err() == kUnsupported, "spawn with no function is Unsupported");

    aver_levelinst_set_host(nullptr);
    check(aver_levelinst_count() == 0 && err() == kNotInitialised, "removing the host returns to NotInitialised");

    if (g_failures) { AVER_ERROR("LevelInstanceAbiTest: {} failure(s)", g_failures); return 1; }
    AVER_INFO("LevelInstanceAbiTest: all passed");
    return 0;
}
