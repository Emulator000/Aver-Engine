#ifndef AVER_FRAMEWORK_TERRAIN_ABI_H
#define AVER_FRAMEWORK_TERRAIN_ABI_H

/* Terrain and water queries C ABI (Aver.Framework): lets C# and graphs ask the landscape which layer is
 * under a point, what it is made of, and where the water is.
 *
 * The landscape and the water live in the game runtime, which the framework cannot link, so each host
 * installs a PROVIDER of function pointers and the framework relays the calls (the same shape as
 * framework_state_abi.h). Coordinates are world centimetres, +Z up. With no provider every query answers 0
 * and leaves its outputs untouched. Strings are UTF-8, NUL-terminated, truncated to the caller's buffer.
 * Own version, bumped on additive change. */

#include "aver/framework/framework_abi.h"   /* AVER_FW_ABI, AVER_FW_CALL */

#ifdef __cplusplus
extern "C" {
#endif

#define AVER_FW_TERRAIN_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_terrain_abi_version(void);

#define AVER_FW_TERRAIN_MAX_LAYERS 8
#define AVER_FW_WATER_KIND_NONE  0
#define AVER_FW_WATER_KIND_OCEAN 1
#define AVER_FW_WATER_KIND_LAKE  2
#define AVER_FW_WATER_KIND_RIVER 3

/* 1 = inside the section. outWeights has AVER_FW_TERRAIN_MAX_LAYERS floats (material layer order, sum 1).
 * Any pointer may be null. */
typedef int32_t (AVER_FW_CALL* aver_fw_terrain_sample_fn)(float x, float y, float* outZ, float* outNormal3,
        int32_t* outDominant, float* outWeights, void* user);
typedef int32_t (AVER_FW_CALL* aver_fw_terrain_layer_count_fn)(void* user);
/* Copies NUL-terminated UTF-8 into the buffers (truncated); 1 if the layer exists. */
typedef int32_t (AVER_FW_CALL* aver_fw_terrain_layer_fn)(int32_t layer, char* nameBuf, int32_t nameCap,
        char* surfaceBuf, int32_t surfaceCap, float* outFriction, float* outRestitution, void* user);

AVER_FW_ABI int32_t aver_fw_set_terrain_provider(aver_fw_terrain_sample_fn s, aver_fw_terrain_layer_count_fn c,
                                                 aver_fw_terrain_layer_fn l, void* user);   /* always 1 */
AVER_FW_ABI int32_t aver_fw_terrain_sample(float x, float y, float* outZ, float* outNormal3, int32_t* outDominant,
                                           float* outWeights);                                /* 0 with no provider */
AVER_FW_ABI int32_t aver_fw_terrain_layer_count(void);                                        /* 0 with no provider */
AVER_FW_ABI int32_t aver_fw_terrain_layer(int32_t layer, char* nameBuf, int32_t nameCap, char* surfaceBuf,
                                          int32_t surfaceCap, float* outFriction, float* outRestitution);

/* outDepth = surface minus ground (cm); outCurrent3 cm/s. 1 = water covers (x, y). */
typedef int32_t (AVER_FW_CALL* aver_fw_water_sample_fn)(float x, float y, float z, float* outSurfaceZ,
        float* outNormal3, float* outCurrent3, float* outDepth, int32_t* outKind, void* user);
AVER_FW_ABI int32_t aver_fw_set_water_provider(aver_fw_water_sample_fn fn, void* user);       /* always 1 */
AVER_FW_ABI int32_t aver_fw_water_sample(float x, float y, float z, float* outSurfaceZ, float* outNormal3,
                                         float* outCurrent3, float* outDepth, int32_t* outKind); /* 0 with no provider */

#ifdef __cplusplus
}
#endif

#endif /* AVER_FRAMEWORK_TERRAIN_ABI_H */
