// Animation montage C ABI: a thin relay to the provider a composition root installs.
// See framework_anim_abi.h and docs/ANIM_MONTAGES.md.

#include "aver/framework/framework_anim_abi.h"

namespace {

aver_fw_anim_fn g_provider = nullptr;
void* g_providerUser = nullptr;

int32_t call(int32_t op, int32_t entity, int64_t h0, int64_t h1, int64_t h2, float f0, float f1,
             float* outF = nullptr, int64_t* outI = nullptr) {
    return g_provider ? g_provider(op, entity, h0, h1, h2, f0, f1, outF, outI, g_providerUser) : 0;
}

} // namespace

int32_t aver_fw_anim_abi_version(void) { return AVER_FW_ANIM_ABI_VERSION; }

int32_t aver_fw_set_anim_provider(aver_fw_anim_fn fn, void* user) {
    g_provider = fn;
    g_providerUser = user;
    return 1;
}

int32_t aver_fw_montage_play(int32_t entity, int64_t montage, int64_t slot, int64_t section, float rate,
                             float blendIn) {
    return call(AVER_FW_ANIM_OP_MONTAGE_PLAY, entity, montage, slot, section, rate, blendIn);
}

int32_t aver_fw_montage_stop(int32_t entity, int64_t montage, float blendOut) {
    return call(AVER_FW_ANIM_OP_MONTAGE_STOP, entity, montage, 0, 0, blendOut, 0.0f);
}

int32_t aver_fw_montage_stop_slot(int32_t entity, int64_t slot, float blendOut) {
    return call(AVER_FW_ANIM_OP_MONTAGE_STOP_SLOT, entity, slot, 0, 0, blendOut, 0.0f);
}

int32_t aver_fw_montage_jump(int32_t entity, int64_t montage, int64_t section) {
    return call(AVER_FW_ANIM_OP_MONTAGE_JUMP, entity, montage, section, 0, 0.0f, 0.0f);
}

int32_t aver_fw_montage_set_next_section(int32_t entity, int64_t montage, int64_t fromSection, int64_t toSection) {
    return call(AVER_FW_ANIM_OP_MONTAGE_NEXT, entity, montage, fromSection, toSection, 0.0f, 0.0f);
}

int32_t aver_fw_montage_set_rate(int32_t entity, int64_t montage, float rate) {
    return call(AVER_FW_ANIM_OP_MONTAGE_RATE, entity, montage, 0, 0, rate, 0.0f);
}

int32_t aver_fw_montage_is_playing(int32_t entity, int64_t montage) {
    return call(AVER_FW_ANIM_OP_MONTAGE_PLAYING, entity, montage, 0, 0, 0.0f, 0.0f);
}

int32_t aver_fw_montage_state(int32_t entity, int64_t montage, float* outPosLenWeightRate,
                              int64_t* outSectionHash) {
    return call(AVER_FW_ANIM_OP_MONTAGE_STATE, entity, montage, 0, 0, 0.0f, 0.0f, outPosLenWeightRate,
                outSectionHash);
}
