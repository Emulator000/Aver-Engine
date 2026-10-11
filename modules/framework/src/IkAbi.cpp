// IK and control-rig C ABI: a thin relay to the provider a composition root installs.
// See framework_ik_abi.h and docs/ANIM_IK_RETARGET.md.

#include "aver/framework/framework_ik_abi.h"

namespace {

aver_fw_ik_fn g_provider = nullptr;
void* g_providerUser = nullptr;

int32_t call(int32_t op, int32_t entity, int64_t h0, int64_t h1, int64_t h2, const float* in, int32_t inCount,
             float* outF = nullptr) {
    return g_provider ? g_provider(op, entity, h0, h1, h2, in, inCount, outF, nullptr, g_providerUser) : 0;
}

} // namespace

int32_t aver_fw_ik_abi_version(void) { return AVER_FW_IK_ABI_VERSION; }

int32_t aver_fw_set_ik_provider(aver_fw_ik_fn fn, void* user) {
    g_provider = fn;
    g_providerUser = user;
    return 1;
}

int32_t aver_fw_rig_set_input_float(int32_t entity, int64_t name, float value) {
    return call(AVER_FW_IK_OP_RIG_INPUT_FLOAT, entity, name, 0, 0, &value, 1);
}

int32_t aver_fw_rig_set_input_vec3(int32_t entity, int64_t name, float x, float y, float z) {
    const float v[3] = {x, y, z};
    return call(AVER_FW_IK_OP_RIG_INPUT_VEC3, entity, name, 0, 0, v, 3);
}

int32_t aver_fw_rig_set_input_quat(int32_t entity, int64_t name, float x, float y, float z, float w) {
    const float v[4] = {x, y, z, w};
    return call(AVER_FW_IK_OP_RIG_INPUT_QUAT, entity, name, 0, 0, v, 4);
}

int32_t aver_fw_rig_clear_input(int32_t entity, int64_t name) {
    return call(AVER_FW_IK_OP_RIG_INPUT_CLEAR, entity, name, 0, 0, nullptr, 0);
}

int32_t aver_fw_ik_set_goal(int32_t entity, int64_t goal, int32_t space, float px, float py, float pz,
                            int32_t hasRotation, float qx, float qy, float qz, float qw) {
    const float v[7] = {px, py, pz, qx, qy, qz, qw};
    return call(AVER_FW_IK_OP_GOAL_SET, entity, goal, space, 0, v, hasRotation ? 7 : 3);
}

int32_t aver_fw_ik_bind_goal(int32_t entity, int64_t goal, int32_t targetEntity, int64_t socket, float ox, float oy,
                             float oz) {
    const float v[3] = {ox, oy, oz};
    return call(AVER_FW_IK_OP_GOAL_BIND, entity, goal, static_cast<int64_t>(static_cast<uint32_t>(targetEntity)), socket, v, 3);
}

int32_t aver_fw_ik_set_goal_weight(int32_t entity, int64_t goal, float weight, float blendSeconds) {
    const float v[2] = {weight, blendSeconds};
    return call(AVER_FW_IK_OP_GOAL_WEIGHT, entity, goal, 0, 0, v, 2);
}

int32_t aver_fw_ik_clear_goal(int32_t entity, int64_t goal) {
    return call(AVER_FW_IK_OP_GOAL_CLEAR, entity, goal, 0, 0, nullptr, 0);
}

int32_t aver_fw_ik_goal_state(int32_t entity, int64_t goal, float* outAlphaErrorReached) {
    return call(AVER_FW_IK_OP_GOAL_STATE, entity, goal, 0, 0, nullptr, 0, outAlphaErrorReached);
}

int32_t aver_fw_ik_set_foot_weight(int32_t entity, int64_t foot, float weight, float blendSeconds) {
    const float v[2] = {weight, blendSeconds};
    return call(AVER_FW_IK_OP_FOOT_WEIGHT, entity, foot, 0, 0, v, 2);
}

int32_t aver_fw_ik_foot_state(int32_t entity, int64_t foot, float* outOffsetPlantNormal) {
    return call(AVER_FW_IK_OP_FOOT_STATE, entity, foot, 0, 0, nullptr, 0, outOffsetPlantNormal);
}
