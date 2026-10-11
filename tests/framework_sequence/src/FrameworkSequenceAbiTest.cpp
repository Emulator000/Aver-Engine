// The sequencer C ABI (framework_sequence_abi.h) end to end through the Aver.Framework DLL: with no
// provider every call answers 0 and writes nothing; with a provider every call forwards its exact
// arguments under the right op code and relays the return and the out values; clearing the provider
// restores the zeros. The provider here is a fake, so no runtime is involved. Exits 0 or 1.
#include "aver/core/Log.hpp"
#include "aver/framework/framework_sequence_abi.h"

#include <string>

using namespace aver;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) return;
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

struct Call {
    int32_t op = 0, entity = 0;
    int64_t h0 = 0, h1 = 0;
    float f0 = 0, f1 = 0;
    bool hasOutF = false, hasOutI = false;
    void* user = nullptr;
    int calls = 0;
};
static Call g_call;
static int32_t g_return = 0;

static int32_t AVER_FW_CALL fake(int32_t op, int32_t entity, int64_t h0, int64_t h1, float f0, float f1, float* outF,
                                 int64_t* outI, void* user) {
    g_call = Call{op, entity, h0, h1, f0, f1, outF != nullptr, outI != nullptr, user, g_call.calls + 1};
    if (op == AVER_FW_SEQ_OP_STATE && outF && outI) {
        outF[0] = 1.5f; outF[1] = 20.0f; outF[2] = 2.0f;
        outI[0] = 1;
    }
    return g_return;
}

static bool is(const Call& c, int32_t op, int32_t entity, int64_t h0, int64_t h1, float f0, float f1) {
    return c.op == op && c.entity == entity && c.h0 == h0 && c.h1 == h1 && c.f0 == f0 && c.f1 == f1;
}

int main() {
    AVER_INFO("FrameworkSequenceAbiTest");
    check(aver_fw_sequence_abi_version() == 1 && AVER_FW_SEQUENCE_ABI_VERSION == 1, "the sequence ABI is version 1");

    // The op codes are the wire format between this DLL and a provider in another module.
    check(AVER_FW_SEQ_OP_PLAY == 1 && AVER_FW_SEQ_OP_PAUSE == 2 && AVER_FW_SEQ_OP_STOP == 3 && AVER_FW_SEQ_OP_SET_TIME == 4 &&
              AVER_FW_SEQ_OP_SET_RATE == 5 && AVER_FW_SEQ_OP_PLAYING == 6 && AVER_FW_SEQ_OP_STATE == 7 && AVER_FW_SEQ_OP_BIND == 8,
          "the op codes are pinned");

    AVER_INFO("=== no provider ===");
    {
        float out[3] = {-1, -1, -1};
        int32_t playing = -1;
        check(aver_fw_sequence_play(11, 0.0f, 1.0f) == 0 && aver_fw_sequence_pause(11) == 0 && aver_fw_sequence_stop(11) == 0 &&
                  aver_fw_sequence_set_time(11, 2.0f) == 0 && aver_fw_sequence_set_rate(11, 2.0f) == 0 &&
                  aver_fw_sequence_is_playing(11) == 0 && aver_fw_sequence_bind(11, 22, 5) == 0,
              "every call answers 0");
        check(aver_fw_sequence_state(11, out, &playing) == 0, "state answers 0");
        check(out[0] == -1 && out[1] == -1 && out[2] == -1 && playing == -1, "and writes nothing");
    }

    AVER_INFO("=== a provider ===");
    {
        int token = 0;
        check(aver_fw_set_sequence_provider(&fake, &token) == 1, "installing returns 1");
        g_return = 1;
        const int64_t seq = 0x1122334455667788ll, bind = 0x0102030405060708ll;

        check(aver_fw_sequence_play(seq, 2.5f, 1.5f) == 1 && is(g_call, AVER_FW_SEQ_OP_PLAY, 0, seq, 0, 2.5f, 1.5f) &&
                  g_call.user == &token,
              "play forwards the sequence, start and rate");
        check(aver_fw_sequence_pause(seq) == 1 && is(g_call, AVER_FW_SEQ_OP_PAUSE, 0, seq, 0, 0.0f, 0.0f), "pause");
        check(aver_fw_sequence_stop(seq) == 1 && is(g_call, AVER_FW_SEQ_OP_STOP, 0, seq, 0, 0.0f, 0.0f), "stop");
        check(aver_fw_sequence_stop(0) == 1 && is(g_call, AVER_FW_SEQ_OP_STOP, 0, 0, 0, 0.0f, 0.0f), "stop 0 means every sequence");
        check(aver_fw_sequence_set_time(seq, 7.25f) == 1 && is(g_call, AVER_FW_SEQ_OP_SET_TIME, 0, seq, 0, 7.25f, 0.0f), "set time");
        check(aver_fw_sequence_set_rate(seq, 0.5f) == 1 && is(g_call, AVER_FW_SEQ_OP_SET_RATE, 0, seq, 0, 0.5f, 0.0f), "set rate");
        check(aver_fw_sequence_is_playing(seq) == 1 && is(g_call, AVER_FW_SEQ_OP_PLAYING, 0, seq, 0, 0.0f, 0.0f), "is playing");
        check(aver_fw_sequence_bind(seq, bind, 42) == 1 && is(g_call, AVER_FW_SEQ_OP_BIND, 42, seq, bind, 0.0f, 0.0f),
              "bind forwards the entity and the binding name hash");

        float out[3] = {0, 0, 0};
        int32_t playing = 0;
        check(aver_fw_sequence_state(seq, out, &playing) == 1 && g_call.op == AVER_FW_SEQ_OP_STATE && g_call.hasOutF && g_call.hasOutI,
              "state passes both output buffers");
        check(out[0] == 1.5f && out[1] == 20.0f && out[2] == 2.0f && playing == 1, "and the provider's out values arrive");

        g_return = 0;
        check(aver_fw_sequence_play(seq, -1.0f, 0.0f) == 0, "a refusal is relayed as 0");
        out[0] = out[1] = out[2] = -1;
        playing = -1;
        check(aver_fw_sequence_state(seq, out, &playing) == 0 && out[0] == -1 && playing == -1, "a refused state leaves the outputs alone");

        AVER_INFO("=== clearing the provider ===");
        const int before = g_call.calls;
        check(aver_fw_set_sequence_provider(nullptr, nullptr) == 1, "clearing returns 1");
        float zero[3] = {-1, -1, -1};
        int32_t zp = -1;
        g_return = 1;
        check(aver_fw_sequence_play(seq, 0.0f, 1.0f) == 0 && aver_fw_sequence_state(seq, zero, &zp) == 0, "every call is 0 again");
        check(zero[0] == -1 && zp == -1, "and writes nothing");
        check(g_call.calls == before, "the old provider was not called");
    }

    AVER_INFO(g_failures == 0 ? "FrameworkSequenceAbiTest: PASS" : "FrameworkSequenceAbiTest: FAIL");
    return g_failures == 0 ? 0 : 1;
}
