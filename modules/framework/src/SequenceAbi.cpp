// Level sequence C ABI: a thin relay to the provider a composition root installs.
// See framework_sequence_abi.h and docs/SEQUENCER.md.

#include "aver/framework/framework_sequence_abi.h"

namespace {

aver_fw_sequence_fn g_provider = nullptr;
void* g_providerUser = nullptr;

int32_t call(int32_t op, int32_t entity, int64_t h0, int64_t h1, float f0, float f1,
             float* outF = nullptr, int64_t* outI = nullptr) {
    return g_provider ? g_provider(op, entity, h0, h1, f0, f1, outF, outI, g_providerUser) : 0;
}

} // namespace

int32_t aver_fw_sequence_abi_version(void) { return AVER_FW_SEQUENCE_ABI_VERSION; }

int32_t aver_fw_set_sequence_provider(aver_fw_sequence_fn fn, void* user) {
    g_provider = fn;
    g_providerUser = user;
    return 1;
}

int32_t aver_fw_sequence_play(int64_t sequence, float startSeconds, float rate) {
    return call(AVER_FW_SEQ_OP_PLAY, 0, sequence, 0, startSeconds, rate);
}

int32_t aver_fw_sequence_pause(int64_t sequence) { return call(AVER_FW_SEQ_OP_PAUSE, 0, sequence, 0, 0.0f, 0.0f); }

int32_t aver_fw_sequence_stop(int64_t sequence) { return call(AVER_FW_SEQ_OP_STOP, 0, sequence, 0, 0.0f, 0.0f); }

int32_t aver_fw_sequence_set_time(int64_t sequence, float seconds) {
    return call(AVER_FW_SEQ_OP_SET_TIME, 0, sequence, 0, seconds, 0.0f);
}

int32_t aver_fw_sequence_set_rate(int64_t sequence, float rate) {
    return call(AVER_FW_SEQ_OP_SET_RATE, 0, sequence, 0, rate, 0.0f);
}

int32_t aver_fw_sequence_is_playing(int64_t sequence) { return call(AVER_FW_SEQ_OP_PLAYING, 0, sequence, 0, 0.0f, 0.0f); }

int32_t aver_fw_sequence_state(int64_t sequence, float* outTimeLengthRate, int32_t* outPlaying) {
    float f[3] = {0.0f, 0.0f, 0.0f};
    int64_t i[1] = {0};
    const int32_t r = call(AVER_FW_SEQ_OP_STATE, 0, sequence, 0, 0.0f, 0.0f, f, i);
    if (r == 0) return 0;   // an unknown sequence leaves the outputs alone
    if (outTimeLengthRate) { outTimeLengthRate[0] = f[0]; outTimeLengthRate[1] = f[1]; outTimeLengthRate[2] = f[2]; }
    if (outPlaying) *outPlaying = i[0] != 0 ? 1 : 0;
    return r;
}

int32_t aver_fw_sequence_bind(int64_t sequence, int64_t bindingName, int32_t entity) {
    return call(AVER_FW_SEQ_OP_BIND, entity, sequence, bindingName, 0.0f, 0.0f);
}
