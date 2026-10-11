// The IK / control-rig C ABI (framework_ik_abi.h) end to end through the Aver.Framework DLL: with no provider every
// call answers 0 and writes nothing; with a provider every call forwards its exact arguments under the right op
// code (including inCount 3 against 7 for a goal with and without a rotation) and relays the return and the out
// values; clearing the provider restores the zeros. The provider is a fake, so no animation module is involved.
// Exits 0 or 1.
#include "aver/core/Log.hpp"
#include "aver/framework/framework_ik_abi.h"

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
    float in[8] = {};
    int32_t inCount = -1;
    bool hasOutF = false;
    void* user = nullptr;
    int calls = 0;
};
static Call g_call;
static int32_t g_return = 0;

static int32_t AVER_FW_CALL fake(int32_t op, int32_t entity, int64_t h0, int64_t h1, int64_t h2, const float* in, int32_t inCount,
                                 float* outF, int64_t*, void* user) {
    Call c;
    c.op = op; c.entity = entity; c.h0 = h0; c.h1 = h1; c.h2 = h2; c.inCount = inCount; c.hasOutF = outF != nullptr;
    c.user = user; c.calls = g_call.calls + 1;
    for (int i = 0; i < inCount && i < 8 && in; ++i) c.in[i] = in[i];
    g_call = c;
    if (op == AVER_FW_IK_OP_GOAL_STATE && outF) { outF[0] = 0.5f; outF[1] = 2.25f; outF[2] = 1.0f; }
    if (op == AVER_FW_IK_OP_FOOT_STATE && outF) { outF[0] = -4.0f; outF[1] = 0.75f; outF[2] = 0.0f; outF[3] = 0.6f; outF[4] = 0.8f; }
    return g_return;
}

int main() {
    AVER_INFO("FrameworkIkAbiTest");
    check(aver_fw_ik_abi_version() == 1 && AVER_FW_IK_ABI_VERSION == 1, "the IK ABI is version 1");
    check(AVER_FW_IK_OP_RIG_INPUT_FLOAT == 1 && AVER_FW_IK_OP_RIG_INPUT_VEC3 == 2 && AVER_FW_IK_OP_RIG_INPUT_QUAT == 3 &&
              AVER_FW_IK_OP_RIG_INPUT_CLEAR == 4 && AVER_FW_IK_OP_GOAL_SET == 5 && AVER_FW_IK_OP_GOAL_BIND == 6 &&
              AVER_FW_IK_OP_GOAL_WEIGHT == 7 && AVER_FW_IK_OP_GOAL_CLEAR == 8 && AVER_FW_IK_OP_GOAL_STATE == 9 &&
              AVER_FW_IK_OP_FOOT_WEIGHT == 10 && AVER_FW_IK_OP_FOOT_STATE == 11,
          "the op codes are pinned");

    AVER_INFO("=== no provider ===");
    {
        float g3[3] = {-1, -1, -1}, f5[5] = {-1, -1, -1, -1, -1};
        check(aver_fw_rig_set_input_float(1, 2, 3.0f) == 0 && aver_fw_rig_set_input_vec3(1, 2, 1, 2, 3) == 0 &&
                  aver_fw_rig_set_input_quat(1, 2, 0, 0, 0, 1) == 0 && aver_fw_rig_clear_input(1, 2) == 0,
              "the input calls answer 0");
        check(aver_fw_ik_set_goal(1, 2, 0, 1, 2, 3, 0, 0, 0, 0, 1) == 0 && aver_fw_ik_bind_goal(1, 2, 3, 4, 0, 0, 0) == 0 &&
                  aver_fw_ik_set_goal_weight(1, 2, 1, 0.1f) == 0 && aver_fw_ik_clear_goal(1, 2) == 0 &&
                  aver_fw_ik_set_foot_weight(1, 2, 1, 0.1f) == 0,
              "the goal and foot calls answer 0");
        check(aver_fw_ik_goal_state(1, 2, g3) == 0 && g3[0] == -1 && g3[1] == -1 && g3[2] == -1, "GOAL_STATE leaves its output untouched");
        check(aver_fw_ik_foot_state(1, 2, f5) == 0 && f5[0] == -1 && f5[4] == -1, "FOOT_STATE leaves its output untouched");
    }

    int token = 0;
    check(aver_fw_set_ik_provider(&fake, &token) == 1, "installing a provider returns 1");
    g_return = 1;

    AVER_INFO("=== inputs ===");
    {
        check(aver_fw_rig_set_input_float(7, 0x1111, 0.25f) == 1 && g_call.op == AVER_FW_IK_OP_RIG_INPUT_FLOAT && g_call.entity == 7 &&
                  g_call.h0 == 0x1111 && g_call.inCount == 1 && g_call.in[0] == 0.25f && g_call.user == &token,
              "set_input_float: op, entity, hash, one float, the user pointer");
        aver_fw_rig_set_input_vec3(7, 0x2222, 1.0f, 2.0f, 3.0f);
        check(g_call.op == AVER_FW_IK_OP_RIG_INPUT_VEC3 && g_call.h0 == 0x2222 && g_call.inCount == 3 && g_call.in[0] == 1.0f &&
                  g_call.in[1] == 2.0f && g_call.in[2] == 3.0f,
              "set_input_vec3: three floats in order");
        aver_fw_rig_set_input_quat(7, 0x3333, 0.1f, 0.2f, 0.3f, 0.9f);
        check(g_call.op == AVER_FW_IK_OP_RIG_INPUT_QUAT && g_call.inCount == 4 && g_call.in[3] == 0.9f, "set_input_quat: four floats, xyzw");
        aver_fw_rig_clear_input(7, 0);
        check(g_call.op == AVER_FW_IK_OP_RIG_INPUT_CLEAR && g_call.h0 == 0 && g_call.inCount == 0, "clear_input: hash 0 means all");
    }

    AVER_INFO("=== goals ===");
    {
        aver_fw_ik_set_goal(3, 0xAAAA, 1, 10, 20, 30, 0, 9, 9, 9, 9);
        check(g_call.op == AVER_FW_IK_OP_GOAL_SET && g_call.h0 == 0xAAAA && g_call.h1 == 1 && g_call.inCount == 3 && g_call.in[2] == 30.0f,
              "set_goal without a rotation passes inCount 3");
        aver_fw_ik_set_goal(3, 0xAAAA, 0, 10, 20, 30, 1, 0.0f, 0.0f, 0.5f, 0.5f);
        check(g_call.h1 == 0 && g_call.inCount == 7 && g_call.in[5] == 0.5f && g_call.in[6] == 0.5f && g_call.in[3] == 0.0f,
              "and with a rotation inCount 7, quaternion after the position");
        aver_fw_ik_bind_goal(3, 0xBBBB, 12, 0xCCCC, 1, 2, 3);
        check(g_call.op == AVER_FW_IK_OP_GOAL_BIND && g_call.h0 == 0xBBBB && g_call.h1 == 12 && g_call.h2 == 0xCCCC && g_call.inCount == 3 &&
                  g_call.in[1] == 2.0f,
              "bind_goal: goal, target entity, socket, offset");
        aver_fw_ik_set_goal_weight(3, 0xAAAA, 0.5f, 0.3f);
        check(g_call.op == AVER_FW_IK_OP_GOAL_WEIGHT && g_call.inCount == 2 && g_call.in[0] == 0.5f && g_call.in[1] == 0.3f, "set_goal_weight: weight, blend");
        aver_fw_ik_clear_goal(3, 0);
        check(g_call.op == AVER_FW_IK_OP_GOAL_CLEAR && g_call.h0 == 0, "clear_goal: 0 is every goal");
        float st[3] = {};
        check(aver_fw_ik_goal_state(3, 0xAAAA, st) == 1 && g_call.op == AVER_FW_IK_OP_GOAL_STATE && g_call.hasOutF && st[0] == 0.5f &&
                  st[1] == 2.25f && st[2] == 1.0f,
              "goal_state relays alpha, error and reached");
    }

    AVER_INFO("=== feet ===");
    {
        aver_fw_ik_set_foot_weight(3, 0xDDDD, 0.0f, 0.2f);
        check(g_call.op == AVER_FW_IK_OP_FOOT_WEIGHT && g_call.h0 == 0xDDDD && g_call.inCount == 2 && g_call.in[1] == 0.2f, "set_foot_weight");
        float st[5] = {};
        check(aver_fw_ik_foot_state(3, 0, st) == 1 && g_call.op == AVER_FW_IK_OP_FOOT_STATE && g_call.h0 == 0 && st[0] == -4.0f &&
                  st[1] == 0.75f && st[3] == 0.6f && st[4] == 0.8f,
              "foot_state relays offset, plant and the ground normal");
    }

    AVER_INFO("=== a refusing provider, then clearing it ===");
    {
        g_return = 0;
        check(aver_fw_ik_clear_goal(1, 2) == 0, "the provider's 0 is relayed");
        const int calls = g_call.calls;
        check(aver_fw_set_ik_provider(nullptr, nullptr) == 1, "clearing the provider returns 1");
        check(aver_fw_ik_set_goal_weight(1, 2, 1, 0.1f) == 0 && g_call.calls == calls, "and the calls go back to answering 0 without reaching it");
    }

    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
