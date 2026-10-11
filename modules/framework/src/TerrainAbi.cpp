// Terrain and water queries C ABI: a thin relay to the providers the host installs.
// See framework_terrain_abi.h.

#include "aver/framework/framework_terrain_abi.h"

namespace {

aver_fw_terrain_sample_fn g_sample = nullptr;
aver_fw_terrain_layer_count_fn g_count = nullptr;
aver_fw_terrain_layer_fn g_layer = nullptr;
void* g_terrainUser = nullptr;

aver_fw_water_sample_fn g_water = nullptr;
void* g_waterUser = nullptr;

} // namespace

int32_t aver_fw_terrain_abi_version(void) { return AVER_FW_TERRAIN_ABI_VERSION; }

int32_t aver_fw_set_terrain_provider(aver_fw_terrain_sample_fn s, aver_fw_terrain_layer_count_fn c,
                                     aver_fw_terrain_layer_fn l, void* user) {
    g_sample = s;
    g_count = c;
    g_layer = l;
    g_terrainUser = user;
    return 1;
}

int32_t aver_fw_terrain_sample(float x, float y, float* outZ, float* outNormal3, int32_t* outDominant,
                               float* outWeights) {
    return g_sample ? g_sample(x, y, outZ, outNormal3, outDominant, outWeights, g_terrainUser) : 0;
}

int32_t aver_fw_terrain_layer_count(void) {
    return g_count ? g_count(g_terrainUser) : 0;
}

int32_t aver_fw_terrain_layer(int32_t layer, char* nameBuf, int32_t nameCap, char* surfaceBuf,
                              int32_t surfaceCap, float* outFriction, float* outRestitution) {
    return g_layer ? g_layer(layer, nameBuf, nameCap, surfaceBuf, surfaceCap, outFriction, outRestitution,
                             g_terrainUser)
                   : 0;
}

int32_t aver_fw_set_water_provider(aver_fw_water_sample_fn fn, void* user) {
    g_water = fn;
    g_waterUser = user;
    return 1;
}

int32_t aver_fw_water_sample(float x, float y, float z, float* outSurfaceZ, float* outNormal3,
                             float* outCurrent3, float* outDepth, int32_t* outKind) {
    return g_water ? g_water(x, y, z, outSurfaceZ, outNormal3, outCurrent3, outDepth, outKind, g_waterUser) : 0;
}
