#ifndef AVER_FRAMEWORK_IK_ABI_H
#define AVER_FRAMEWORK_IK_ABI_H

/* IK and control-rig C ABI (Aver.Framework): named rig inputs, IK goals and foot-IK weights, relayed to the control
 * rig system.
 *
 * Same conventions as framework_anim_abi.h (int32_t/int64_t/float only, 1 on success and 0 on refusal).
 * Aver.Framework does not link the animation module, so a composition root installs ONE provider function and every
 * call below forwards to it; with no provider every call returns 0 and nothing is written. The provider is
 * anim::ikProvider (modules/anim.scene/include/aver/anim/IkRigSystem.hpp), with the control rig system as `user`.
 * Own version, separate from AVER_FW_ABI_VERSION_MINOR. All hashes are fnv1a64 of the name (Assets.ObjectIdOf in C#).
 * Goal and foot calls return 0 when the entity has no CIkRig or the rig has no such goal / foot: a typo is refused,
 * not stored. Semantics: docs/ANIM_IK_RETARGET.md. */

#include "aver/framework/framework_abi.h"   /* AVER_FW_ABI, AVER_FW_CALL */

#ifdef __cplusplus
extern "C" {
#endif

#define AVER_FW_IK_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_ik_abi_version(void);

/* Provider operations; the argument usage of each is listed with it. */
#define AVER_FW_IK_OP_RIG_INPUT_FLOAT 1   /* h0 input name hash, in[0] */
#define AVER_FW_IK_OP_RIG_INPUT_VEC3  2   /* h0 input, in[0..2] */
#define AVER_FW_IK_OP_RIG_INPUT_QUAT  3   /* h0 input, in[0..3] xyzw */
#define AVER_FW_IK_OP_RIG_INPUT_CLEAR 4   /* h0 input (0 = all) */
#define AVER_FW_IK_OP_GOAL_SET        5   /* h0 goal, h1 space (0 world, 1 entity model), in[0..2] position, in[3..6] rotation when inCount == 7 */
#define AVER_FW_IK_OP_GOAL_BIND       6   /* h0 goal, h1 target entity, h2 socket name hash (0 = target origin), in[0..2] offset, in[3..6] rotation offset when inCount == 7 */
#define AVER_FW_IK_OP_GOAL_WEIGHT     7   /* h0 goal, in[0] weight, in[1] blend seconds (<0 = 0.15) */
#define AVER_FW_IK_OP_GOAL_CLEAR      8   /* h0 goal (0 = all): script releases it, alpha blends to 0 */
#define AVER_FW_IK_OP_GOAL_STATE      9   /* h0 goal; outF[0] alpha, outF[1] error cm, outF[2] reached 0/1 */
#define AVER_FW_IK_OP_FOOT_WEIGHT    10   /* h0 foot chain name hash (0 = every foot), in[0] weight, in[1] blend seconds */
#define AVER_FW_IK_OP_FOOT_STATE     11   /* h0 foot chain hash (0 = pelvis); outF[0] offset cm, outF[1] plant, outF[2..4] ground normal */

typedef int32_t (AVER_FW_CALL* aver_fw_ik_fn)(int32_t op, int32_t entity, int64_t h0, int64_t h1, int64_t h2,
                                              const float* in, int32_t inCount, float* outF, int64_t* outI, void* user);
/* Installs the provider. Passing null clears it. Always returns 1. */
AVER_FW_ABI int32_t aver_fw_set_ik_provider(aver_fw_ik_fn fn, void* user);

/* Named inputs of an OCRIG 2 control rig. A ninth distinct name on one entity is refused. */
AVER_FW_ABI int32_t aver_fw_rig_set_input_float(int32_t entity, int64_t name, float value);
AVER_FW_ABI int32_t aver_fw_rig_set_input_vec3(int32_t entity, int64_t name, float x, float y, float z);
AVER_FW_ABI int32_t aver_fw_rig_set_input_quat(int32_t entity, int64_t name, float x, float y, float z, float w);
AVER_FW_ABI int32_t aver_fw_rig_clear_input(int32_t entity, int64_t name);

/* IK Rig goals. A goal set with its weight never raised engages by itself over 0.15 s; hasRotation != 0 passes
 * the 4 rotation floats (otherwise they are ignored). */
AVER_FW_ABI int32_t aver_fw_ik_set_goal(int32_t entity, int64_t goal, int32_t space, float px, float py, float pz,
                                        int32_t hasRotation, float qx, float qy, float qz, float qw);
/* Follows `targetEntity`'s socket (0 = its origin) each tick, with a local offset. */
AVER_FW_ABI int32_t aver_fw_ik_bind_goal(int32_t entity, int64_t goal, int32_t targetEntity, int64_t socket,
                                         float ox, float oy, float oz);
AVER_FW_ABI int32_t aver_fw_ik_set_goal_weight(int32_t entity, int64_t goal, float weight, float blendSeconds);
AVER_FW_ABI int32_t aver_fw_ik_clear_goal(int32_t entity, int64_t goal);
AVER_FW_ABI int32_t aver_fw_ik_goal_state(int32_t entity, int64_t goal, float* outAlphaErrorReached /* 3 floats */);

/* Foot IK weight (foot 0 = every foot) and state (foot 0 = the pelvis). */
AVER_FW_ABI int32_t aver_fw_ik_set_foot_weight(int32_t entity, int64_t foot, float weight, float blendSeconds);
AVER_FW_ABI int32_t aver_fw_ik_foot_state(int32_t entity, int64_t foot, float* outOffsetPlantNormal /* 5 floats */);

#ifdef __cplusplus
}
#endif

#endif /* AVER_FRAMEWORK_IK_ABI_H */
