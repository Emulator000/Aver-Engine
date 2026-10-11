// Blackboard C ABI: a thin relay to the provider a composition root installs.
// See framework_blackboard_abi.h and docs/BLACKBOARD_BT.md.

#include "aver/framework/framework_blackboard_abi.h"
#include "aver/framework/framework_brain_abi.h"   // AI: wave 3 #21

namespace {

aver_fw_blackboard_fn g_provider = nullptr;
void* g_providerUser = nullptr;

int32_t call(int32_t op, int32_t e, const char* key, int32_t type, int64_t* i, float* f, char* text,
             int32_t textCap) {
    if (!g_provider || !key) return 0;
    return g_provider(op, e, key, type, i, f, text, textCap, g_providerUser);
}

int32_t getNumeric(int32_t e, const char* key, int32_t type, int64_t* i, float* f) {
    return call(AVER_FW_BB_OP_GET, e, key, type, i, f, nullptr, 0);
}

int32_t setValue(int32_t e, const char* key, int32_t type, int64_t* i, float* f, char* text) {
    return call(AVER_FW_BB_OP_SET, e, key, type, i, f, text, 0);
}

} // namespace

int32_t aver_fw_blackboard_abi_version(void) { return AVER_FW_BLACKBOARD_ABI_VERSION; }

int32_t aver_fw_set_blackboard_provider(aver_fw_blackboard_fn fn, void* user) {
    g_provider = fn;
    g_providerUser = user;
    return 1;
}

int32_t aver_fw_bb_type(int32_t e, const char* key) {
    return call(AVER_FW_BB_OP_TYPE, e, key, 0, nullptr, nullptr, nullptr, 0);
}

int32_t aver_fw_bb_get_bool(int32_t e, const char* key, int32_t* out) {
    int64_t v = 0;
    if (!out || !getNumeric(e, key, AVER_FW_BB_BOOL, &v, nullptr)) return 0;
    *out = v != 0 ? 1 : 0;
    return 1;
}

int32_t aver_fw_bb_get_int(int32_t e, const char* key, int64_t* out) {
    int64_t v = 0;
    if (!out || !getNumeric(e, key, AVER_FW_BB_INT, &v, nullptr)) return 0;
    *out = v;
    return 1;
}

int32_t aver_fw_bb_get_float(int32_t e, const char* key, float* out) {
    float v[3] = {0.0f, 0.0f, 0.0f};
    if (!out || !getNumeric(e, key, AVER_FW_BB_FLOAT, nullptr, v)) return 0;
    *out = v[0];
    return 1;
}

int32_t aver_fw_bb_get_vec3(int32_t e, const char* key, float* out3) {
    float v[3] = {0.0f, 0.0f, 0.0f};
    if (!out3 || !getNumeric(e, key, AVER_FW_BB_VEC3, nullptr, v)) return 0;
    out3[0] = v[0]; out3[1] = v[1]; out3[2] = v[2];
    return 1;
}

int32_t aver_fw_bb_get_string(int32_t e, const char* key, char* buf, int32_t cap) {
    if (!buf || cap <= 0) return 0;
    buf[0] = '\0';
    return call(AVER_FW_BB_OP_GET, e, key, AVER_FW_BB_STRING, nullptr, nullptr, buf, cap);
}

int32_t aver_fw_bb_get_entity(int32_t e, const char* key, int32_t* out) {
    int64_t v = 0;
    if (!out || !getNumeric(e, key, AVER_FW_BB_ENTITY, &v, nullptr)) return 0;
    *out = static_cast<int32_t>(v);
    return 1;
}

int32_t aver_fw_bb_set_bool(int32_t e, const char* key, int32_t value) {
    int64_t v = value != 0 ? 1 : 0;
    return setValue(e, key, AVER_FW_BB_BOOL, &v, nullptr, nullptr);
}

int32_t aver_fw_bb_set_int(int32_t e, const char* key, int64_t value) {
    return setValue(e, key, AVER_FW_BB_INT, &value, nullptr, nullptr);
}

int32_t aver_fw_bb_set_float(int32_t e, const char* key, float value) {
    float v[3] = {value, 0.0f, 0.0f};
    return setValue(e, key, AVER_FW_BB_FLOAT, nullptr, v, nullptr);
}

int32_t aver_fw_bb_set_vec3(int32_t e, const char* key, float x, float y, float z) {
    float v[3] = {x, y, z};
    return setValue(e, key, AVER_FW_BB_VEC3, nullptr, v, nullptr);
}

int32_t aver_fw_bb_set_string(int32_t e, const char* key, const char* value) {
    if (!value) value = "";
    // The provider reads `text` and never writes it on a set; the cast only adapts the shared signature.
    return setValue(e, key, AVER_FW_BB_STRING, nullptr, nullptr, const_cast<char*>(value));
}

int32_t aver_fw_bb_set_entity(int32_t e, const char* key, int32_t value) {
    int64_t v = value < 0 ? 0 : value;
    return setValue(e, key, AVER_FW_BB_ENTITY, &v, nullptr, nullptr);
}

int32_t aver_fw_bb_reset(int32_t e, const char* key) {
    return call(AVER_FW_BB_OP_RESET, e, key, 0, nullptr, nullptr, nullptr, 0);
}

int32_t aver_fw_bb_define(int32_t e, const char* key, int32_t type, int32_t scope) {
    int64_t s = scope == AVER_FW_BB_SCOPE_SHARED ? 1 : 0;
    return call(AVER_FW_BB_OP_DEFINE, e, key, type, &s, nullptr, nullptr, 0);
}

int32_t aver_fw_bb_set_team(int32_t e, const char* team) {
    return call(AVER_FW_BB_OP_SETTEAM, e, team ? team : "", 0, nullptr, nullptr, nullptr, 0);
}

// ---- brains (AI: wave 3 #21) ------------------------------------------------------------------------
// The same provider; ops 6..13 (framework_blackboard_abi.h). Declared in framework_brain_abi.h.

int32_t aver_fw_brain_abi_version(void) { return AVER_FW_BRAIN_ABI_VERSION; }

int32_t aver_fw_brain_set_asset(int32_t entity, const char* path) {
    if (!path || !*path) return 0;
    return call(AVER_FW_BB_OP_BRAIN_SET, entity, path, 0, nullptr, nullptr, nullptr, 0);
}

int32_t aver_fw_brain_kind(int32_t entity) {
    int64_t v = 0;
    return call(AVER_FW_BB_OP_BRAIN_KIND, entity, "", 0, &v, nullptr, nullptr, 0) ? static_cast<int32_t>(v) : AVER_FW_BRAIN_NONE;
}

int32_t aver_fw_brain_status(int32_t entity) {
    int64_t v = 0;
    return call(AVER_FW_BB_OP_BRAIN_STATUS, entity, "", 0, &v, nullptr, nullptr, 0) ? static_cast<int32_t>(v) : AVER_FW_BRAIN_IDLE;
}

int32_t aver_fw_brain_send_event(int32_t entity, const char* name, int32_t payloadInt, float payloadFloat) {
    if (!name || !*name) return 0;
    int64_t i = payloadInt;
    float f[3] = {payloadFloat, 0.0f, 0.0f};
    return call(AVER_FW_BB_OP_BRAIN_EVENT, entity, name, 0, &i, f, nullptr, 0);
}

int32_t aver_fw_brain_state(int32_t entity, char* buf, int32_t cap) {
    if (!buf || cap <= 0) return 0;
    buf[0] = '\0';
    return call(AVER_FW_BB_OP_BRAIN_STATE, entity, "", 0, nullptr, nullptr, buf, cap);
}

int32_t aver_fw_brain_in_state(int32_t entity, const char* path) {
    if (!path || !*path) return 0;
    return call(AVER_FW_BB_OP_BRAIN_INSTATE, entity, path, 0, nullptr, nullptr, nullptr, 0);
}

int32_t aver_fw_brain_restart(int32_t entity) { return call(AVER_FW_BB_OP_BRAIN_RESTART, entity, "", 0, nullptr, nullptr, nullptr, 0); }

int32_t aver_fw_brain_stop(int32_t entity) { return call(AVER_FW_BB_OP_BRAIN_STOP, entity, "", 0, nullptr, nullptr, nullptr, 0); }
