// The montage C ABI (framework_anim_abi.h) end to end through the Aver.Framework DLL: with no provider
// every call answers 0 and writes nothing; with a provider every call forwards its exact arguments
// under the right op code and relays the return and the out values; clearing the provider restores the
// zeros. The provider here is a fake, so no animation module is involved. Exits 0 or 1.
#include "aver/core/Log.hpp"
#include "aver/framework/framework_anim_abi.h"

#include <cmath>
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
    int64_t h0 = 0, h1 = 0, h2 = 0;
    float f0 = 0, f1 = 0;
    bool hasOutF = false, hasOutI = false;
    void* user = nullptr;
    int calls = 0;
};
static Call g_call;
static int32_t g_return = 0;

static int32_t AVER_FW_CALL fake(int32_t op, int32_t entity, int64_t h0, int64_t h1, int64_t h2, float f0, float f1, float* outF,
                    int64_t* outI, void* user) {
    g_call = Call{op, entity, h0, h1, h2, f0, f1, outF != nullptr, outI != nullptr, user, g_call.calls + 1};
    if (op == AVER_FW_ANIM_OP_MONTAGE_STATE && outF && outI) {
        outF[0] = 1.5f; outF[1] = 2.5f; outF[2] = 0.75f; outF[3] = 0.5f;
        outI[0] = 0x1234567890ABCDEFll;
    }
    return g_return;
}

static bool is(const Call& c, int32_t op, int32_t entity, int64_t h0, int64_t h1, int64_t h2, float f0, float f1) {
    return c.op == op && c.entity == entity && c.h0 == h0 && c.h1 == h1 && c.h2 == h2 && c.f0 == f0 && c.f1 == f1;
}

int main() {
    AVER_INFO("FrameworkAnimAbiTest");
    check(aver_fw_anim_abi_version() == 1 && AVER_FW_ANIM_ABI_VERSION == 1, "the anim ABI is version 1");

    // The op codes are the wire format between this DLL and a provider in another module.
    check(AVER_FW_ANIM_OP_MONTAGE_PLAY == 1 && AVER_FW_ANIM_OP_MONTAGE_STOP == 2 && AVER_FW_ANIM_OP_MONTAGE_STOP_SLOT == 3 &&
              AVER_FW_ANIM_OP_MONTAGE_JUMP == 4 && AVER_FW_ANIM_OP_MONTAGE_NEXT == 5 && AVER_FW_ANIM_OP_MONTAGE_RATE == 6 &&
              AVER_FW_ANIM_OP_MONTAGE_PLAYING == 7 && AVER_FW_ANIM_OP_MONTAGE_STATE == 8,
          "the op codes are pinned");

    AVER_INFO("=== no provider ===");
    {
        float out[4] = {-1, -1, -1, -1};
        int64_t sec = -7;
        check(aver_fw_montage_play(5, 1, 2, 3, 1.0f, 0.1f) == 0, "play answers 0");
        check(aver_fw_montage_stop(5, 1, 0.2f) == 0, "stop answers 0");
        check(aver_fw_montage_stop_slot(5, 2, 0.2f) == 0, "stop_slot answers 0");
        check(aver_fw_montage_jump(5, 1, 3) == 0, "jump answers 0");
        check(aver_fw_montage_set_next_section(5, 1, 3, 4) == 0, "set_next_section answers 0");
        check(aver_fw_montage_set_rate(5, 1, 2.0f) == 0, "set_rate answers 0");
        check(aver_fw_montage_is_playing(5, 1) == 0, "is_playing answers 0");
        check(aver_fw_montage_state(5, 1, out, &sec) == 0, "state answers 0");
        check(out[0] == -1 && out[1] == -1 && out[2] == -1 && out[3] == -1 && sec == -7, "and writes nothing");
    }

    AVER_INFO("=== a provider receives exactly what was sent ===");
    {
        int token = 0;
        check(aver_fw_set_anim_provider(&fake, &token) == 1, "installing returns 1");
        g_return = 1;

        check(aver_fw_montage_play(7, 11, 12, 13, 1.5f, 0.25f) == 1, "play relays the return");
        check(is(g_call, AVER_FW_ANIM_OP_MONTAGE_PLAY, 7, 11, 12, 13, 1.5f, 0.25f), "play: op, entity, montage, slot, section, rate, blendIn");
        check(g_call.user == &token, "and the user pointer comes back");

        g_return = 3;
        check(aver_fw_montage_stop(7, 11, 0.3f) == 3, "stop relays the count");
        check(is(g_call, AVER_FW_ANIM_OP_MONTAGE_STOP, 7, 11, 0, 0, 0.3f, 0.0f), "stop: montage and blendOut");

        check(aver_fw_montage_stop_slot(7, 21, 0.4f) == 3, "stop_slot relays the count");
        check(is(g_call, AVER_FW_ANIM_OP_MONTAGE_STOP_SLOT, 7, 21, 0, 0, 0.4f, 0.0f), "stop_slot: slot and blendOut");

        g_return = 1;
        check(aver_fw_montage_jump(7, 11, 31) == 1, "jump relays");
        check(is(g_call, AVER_FW_ANIM_OP_MONTAGE_JUMP, 7, 11, 31, 0, 0.0f, 0.0f), "jump: montage and section");

        check(aver_fw_montage_set_next_section(7, 11, 31, 32) == 1, "set_next_section relays");
        check(is(g_call, AVER_FW_ANIM_OP_MONTAGE_NEXT, 7, 11, 31, 32, 0.0f, 0.0f), "next: montage, from, to");
        aver_fw_montage_set_next_section(7, 11, 31, 0);
        check(g_call.h2 == 0, "a 0 target (restore) is sent as 0");

        check(aver_fw_montage_set_rate(7, 11, 0.5f) == 1, "set_rate relays");
        check(is(g_call, AVER_FW_ANIM_OP_MONTAGE_RATE, 7, 11, 0, 0, 0.5f, 0.0f), "rate: montage and rate");

        check(aver_fw_montage_is_playing(7, 0) == 1, "is_playing relays");
        check(is(g_call, AVER_FW_ANIM_OP_MONTAGE_PLAYING, 7, 0, 0, 0, 0.0f, 0.0f), "playing: montage 0 (any) is sent as 0");

        float out[4] = {};
        int64_t sec = 0;
        check(aver_fw_montage_state(7, 11, out, &sec) == 1, "state relays");
        check(is(g_call, AVER_FW_ANIM_OP_MONTAGE_STATE, 7, 11, 0, 0, 0.0f, 0.0f) && g_call.hasOutF && g_call.hasOutI,
              "state: montage and both output pointers");
        check(out[0] == 1.5f && out[1] == 2.5f && out[2] == 0.75f && out[3] == 0.5f && sec == 0x1234567890ABCDEFll,
              "and the provider's out values arrive");

        g_return = 0;
        check(aver_fw_montage_play(7, 11, 0, 0, 1.0f, -1.0f) == 0, "a refusal is relayed as 0");

        AVER_INFO("=== clearing the provider ===");
        const int before = g_call.calls;
        check(aver_fw_set_anim_provider(nullptr, nullptr) == 1, "clearing returns 1");
        float zero[4] = {-1, -1, -1, -1};
        int64_t zs = -1;
        g_return = 1;
        check(aver_fw_montage_play(7, 11, 0, 0, 1.0f, -1.0f) == 0 && aver_fw_montage_state(7, 11, zero, &zs) == 0,
              "every call is 0 again");
        check(zero[0] == -1 && zs == -1, "and writes nothing");
        check(g_call.calls == before, "the old provider was not called");
    }

    AVER_INFO(g_failures == 0 ? "FrameworkAnimAbiTest: PASS" : "FrameworkAnimAbiTest: FAIL");
    return g_failures == 0 ? 0 : 1;
}
