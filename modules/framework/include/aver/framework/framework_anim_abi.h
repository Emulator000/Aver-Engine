#ifndef AVER_FRAMEWORK_ANIM_ABI_H
#define AVER_FRAMEWORK_ANIM_ABI_H

/* Animation montage C ABI (Aver.Framework): play, stop, jump and query montages, relayed to AnimSystem.
 *
 * Same conventions as framework_abi.h (int32_t/int64_t/float only, 1 on success and 0 on refusal).
 * Aver.Framework does not link the animation module, so a composition root installs ONE provider
 * function and every call below forwards to it; with no provider every call returns 0 and nothing is
 * written, the answers an entity with nothing to play gives. The provider is anim::montageProvider
 * (modules/anim.scene/include/aver/anim/AnimMontages.hpp). Own version, separate from
 * AVER_FW_ABI_VERSION_MINOR. All hashes are fnv1a64 of the asset path or name (Assets.ObjectIdOf in C#).
 * Root motion needs no ABI: it is the CRootMotion component over the generic scene field ABI.
 * Semantics: docs/ANIM_MONTAGES.md. */

#include "aver/framework/framework_abi.h"   /* AVER_FW_ABI, AVER_FW_CALL */

#ifdef __cplusplus
extern "C" {
#endif

#define AVER_FW_ANIM_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_anim_abi_version(void);

/* Provider operations; the argument usage of each is listed with it. */
#define AVER_FW_ANIM_OP_MONTAGE_PLAY        1   /* h0 montage, h1 slot (0 authored), h2 start section (0 first), f0 rate, f1 blendIn (<0 authored) */
#define AVER_FW_ANIM_OP_MONTAGE_STOP        2   /* h0 montage (0 all), f0 blendOut (<0 authored); returns number stopped */
#define AVER_FW_ANIM_OP_MONTAGE_STOP_SLOT   3   /* h0 slot, f0 blendOut */
#define AVER_FW_ANIM_OP_MONTAGE_JUMP        4   /* h0 montage (0 newest), h1 section */
#define AVER_FW_ANIM_OP_MONTAGE_NEXT        5   /* h0 montage, h1 from section, h2 to section (0 restores authored) */
#define AVER_FW_ANIM_OP_MONTAGE_RATE        6   /* h0 montage, f0 rate */
#define AVER_FW_ANIM_OP_MONTAGE_PLAYING     7   /* h0 montage (0 any); returns 1/0 */
#define AVER_FW_ANIM_OP_MONTAGE_STATE       8   /* h0 montage (0 newest); outF[0..3] = position, length, weight, rate; outI[0] = section name hash */

typedef int32_t (AVER_FW_CALL* aver_fw_anim_fn)(int32_t op, int32_t entity, int64_t h0, int64_t h1, int64_t h2,
                                                float f0, float f1, float* outF, int64_t* outI, void* user);
/* Installs the provider. Passing null clears it. Always returns 1. */
AVER_FW_ABI int32_t aver_fw_set_anim_provider(aver_fw_anim_fn fn, void* user);

/* Starts the montage on the entity (adds a CAnimator if it has none; needs a skeleton). Playing a
 * running montage restarts it. 1 on success. */
AVER_FW_ABI int32_t aver_fw_montage_play(int32_t entity, int64_t montage, int64_t slot, int64_t section,
                                         float rate, float blendIn);
/* Blends the montage (0 = every montage on the entity) out over blendOut seconds (<0 = authored, 0 =
 * cut). Returns how many were stopped. */
AVER_FW_ABI int32_t aver_fw_montage_stop(int32_t entity, int64_t montage, float blendOut);
AVER_FW_ABI int32_t aver_fw_montage_stop_slot(int32_t entity, int64_t slot, float blendOut);
/* Jumps the montage (0 = the newest) to a section; weight is kept. */
AVER_FW_ABI int32_t aver_fw_montage_jump(int32_t entity, int64_t montage, int64_t section);
/* Overrides where `fromSection` goes next (toSection 0 restores the authored link). */
AVER_FW_ABI int32_t aver_fw_montage_set_next_section(int32_t entity, int64_t montage, int64_t fromSection,
                                                     int64_t toSection);
AVER_FW_ABI int32_t aver_fw_montage_set_rate(int32_t entity, int64_t montage, float rate);
/* 1 when the montage (0 = any) is playing or blending out. */
AVER_FW_ABI int32_t aver_fw_montage_is_playing(int32_t entity, int64_t montage);
/* Writes position, length, weight, rate (4 floats) and the current section's name hash; the outputs
 * are untouched and 0 returned when the montage (0 = newest) is not playing. */
AVER_FW_ABI int32_t aver_fw_montage_state(int32_t entity, int64_t montage, float* outPosLenWeightRate,
                                          int64_t* outSectionHash);

#ifdef __cplusplus
}
#endif

#endif /* AVER_FRAMEWORK_ANIM_ABI_H */
