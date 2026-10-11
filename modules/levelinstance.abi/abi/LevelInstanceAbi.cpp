// Aver.LevelInstance.Abi: forwards the aver_levelinst_* calls to whatever host installed itself. No state of its
// own beyond the copied function table and the thread-local reason slot.
#include "aver/levelinstance/level_instance_abi.h"

#include "aver/core/ErrorCodes.hpp"

namespace {

AverLevelInstanceHost g_host{};
bool g_installed = false;

using aver::AbiError;

// Records `e` and returns 0, so each entry point reports its reason in one expression.
int32_t fail(AbiError e) { aver::setAbiError(e); return 0; }
int32_t ok(int32_t v) { aver::setAbiError(AbiError::Ok); return v; }

// A host call that returns 0 for a refusal. The host carries no reason, so `refused` is the best this DLL can say.
int32_t reported(int32_t hostResult, AbiError refused) {
    if (hostResult == 0) return fail(refused);
    return ok(hostResult);
}

// The not-installed and missing-function guards shared by every call.
template <typename Fn>
bool hostHas(Fn fn) {
    if (!g_installed) { aver::setAbiError(AbiError::NotInitialised); return false; }
    if (!fn)          { aver::setAbiError(AbiError::Unsupported);    return false; }
    return true;
}

} // namespace

extern "C" {

int32_t aver_levelinst_abi_version(void) { return AVER_LEVELINST_ABI_VERSION; }

int32_t aver_levelinst_last_error(void) { return static_cast<int32_t>(aver::lastAbiError()); }

void aver_levelinst_set_host(const AverLevelInstanceHost* host) {
    if (host) { g_host = *host; g_installed = true; }
    else      { g_host = AverLevelInstanceHost{}; g_installed = false; }
}

int32_t aver_levelinst_spawn(const char* path, int32_t parent, float x, float y, float z,
                             float yaw, float pitch, float roll, float scale, int32_t packed) {
    if (!hostHas(g_host.spawn)) return 0;
    if (!path || !*path) return fail(AbiError::InvalidArgument);
    const float xyz[3] = {x, y, z};
    const float ypr[3] = {yaw, pitch, roll};
    return reported(g_host.spawn(g_host.user, path, parent, xyz, ypr, scale, packed), AbiError::InvalidArgument);
}

int32_t aver_levelinst_destroy(int32_t root) {
    if (!hostHas(g_host.destroy)) return 0;
    return reported(g_host.destroy(g_host.user, root), AbiError::BadHandle);
}

int32_t aver_levelinst_root_of(int32_t entity) {
    if (!hostHas(g_host.root_of)) return 0;
    return ok(g_host.root_of(g_host.user, entity));   // 0 = not part of an instance: an answer
}

int32_t aver_levelinst_is_member(int32_t entity) {
    const int32_t root = aver_levelinst_root_of(entity);
    return root != 0 ? 1 : 0;
}

int32_t aver_levelinst_find(int32_t root, const char* idPath) {
    if (!hostHas(g_host.find)) return 0;
    return reported(g_host.find(g_host.user, root, idPath ? idPath : ""), AbiError::BadHandle);
}

int32_t aver_levelinst_set_loaded(int32_t root, int32_t loaded) {
    if (!hostHas(g_host.set_loaded)) return 0;
    return reported(g_host.set_loaded(g_host.user, root, loaded), AbiError::BadHandle);
}

int32_t aver_levelinst_is_loaded(int32_t root) {
    if (!hostHas(g_host.is_loaded)) return 0;
    const int32_t r = g_host.is_loaded(g_host.user, root);
    if (r < 0) { aver::setAbiError(AbiError::BadHandle); return r; }
    return ok(r);
}

int32_t aver_levelinst_reload(int32_t root) {
    if (!hostHas(g_host.reload)) return 0;
    return reported(g_host.reload(g_host.user, root), AbiError::BadHandle);
}

int32_t aver_levelinst_set_packed(int32_t root, int32_t packed) {
    if (!hostHas(g_host.set_packed)) return 0;
    return ok(g_host.set_packed(g_host.user, root, packed));   // 0 = the effective state differs from the request: an answer
}

int32_t aver_levelinst_bounds(int32_t root, float* minXyz, float* maxXyz) {
    if (!hostHas(g_host.bounds)) return 0;
    if (!minXyz || !maxXyz) return fail(AbiError::NullPointer);
    return ok(g_host.bounds(g_host.user, root, minXyz, maxXyz));   // 0 = bounds unknown: an answer
}

int32_t aver_levelinst_count(void) {
    if (!hostHas(g_host.count)) return 0;
    return ok(g_host.count(g_host.user));
}

int32_t aver_levelinst_at(int32_t index) {
    if (!hostHas(g_host.at)) return 0;
    return reported(g_host.at(g_host.user, index), AbiError::OutOfRange);
}

int32_t aver_levelinst_asset_id(int32_t root, int64_t* outId) {
    if (!hostHas(g_host.asset_id)) return 0;
    if (!outId) return fail(AbiError::NullPointer);
    return reported(g_host.asset_id(g_host.user, root, outId), AbiError::BadHandle);
}

} // extern "C"
