// Managed entity state C ABI: a thin relay to the provider the managed side installs.
// See framework_state_abi.h.

#include "aver/framework/framework_state_abi.h"

namespace {

aver_fw_state_count_fn g_count = nullptr;
aver_fw_state_at_fn g_at = nullptr;
aver_fw_state_set_fn g_set = nullptr;
void* g_user = nullptr;

} // namespace

int32_t aver_fw_state_abi_version(void) { return AVER_FW_STATE_ABI_VERSION; }

int32_t aver_fw_set_state_provider(aver_fw_state_count_fn count, aver_fw_state_at_fn at,
                                   aver_fw_state_set_fn setState, void* user) {
    g_count = count;
    g_at = at;
    g_set = setState;
    g_user = user;
    return 1;
}

int32_t aver_fw_state_count(int32_t entity) {
    return g_count ? g_count(entity, g_user) : 0;
}

int32_t aver_fw_state_at(int32_t entity, int32_t index, char* nameBuf, int32_t nameCap,
                         char* dataBuf, int32_t dataCap) {
    return g_at ? g_at(entity, index, nameBuf, nameCap, dataBuf, dataCap, g_user) : -1;
}

int32_t aver_fw_state_set(int32_t entity, const char* name, const char* data, int32_t dataLen) {
    if (!g_set || !name) return 0;
    return g_set(entity, name, data, dataLen, g_user);
}
