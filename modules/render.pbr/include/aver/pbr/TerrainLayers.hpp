#pragma once
// Layered terrain material, the GPU-facing numbers: the 8 x 9 parameter texture the shader reads and the
// atlas description the host resolver hands back (docs/MATERIALS.md, "Terrain layers"). The HLSL side is
// material_prelude.hlsl's averTerrainMaps; TerrainLayoutTest compares its AVER_TERRAIN_ROW_* defines with
// the constants below, so the two cannot drift.
#include "aver/pbr/Material.hpp"
#include "aver/rhi/RHI.hpp"

namespace aver::pbr {

// The parameter texture (R32Float): kTerrainParamWidth columns (one per layer in the layer rows) by
// kTerrainParamHeight rows.
inline constexpr u32 kTerrainParamWidth  = 8;
inline constexpr u32 kTerrainParamHeight = 9;
inline constexpr u32 kTerrainParamFloats = kTerrainParamWidth * kTerrainParamHeight;

// Rows. y0..y6 hold one value per layer (column = layer); y7 and y8 hold scalars in columns 0..7.
inline constexpr u32 kTerrainRowUvScale   = 0;   // layer tiles relative to the mesh UV: (1/tilingCm) / uvTilesPerCm
inline constexpr u32 kTerrainRowHeightBias = 1;
inline constexpr u32 kTerrainRowAvgR      = 2;   // average linear albedo
inline constexpr u32 kTerrainRowAvgG      = 3;
inline constexpr u32 kTerrainRowAvgB      = 4;
inline constexpr u32 kTerrainRowAvgRough  = 5;
inline constexpr u32 kTerrainRowAvgMetal  = 6;
inline constexpr u32 kTerrainRowLayout    = 7;   // x0 cols, x1 rows, x2 stride S, x3 tile T, x4 pad, x5 layerCount, x6 blendDepth, x7 atlasWidth
inline constexpr u32 kTerrainRowSection   = 8;   // x0 originX cm, x1 originY cm, x2 invExtent (0 = no weightmap), x3 halfTexel, x4 atlasHeight

// What the host resolver returns for a terrain material: the three atlas textures and what each cell
// looks like on average. Owned by MaterialSystem once handed over (cached and refcounted like any
// resolved texture).
struct TerrainTextures {
    rhi::TextureHandle albedo = 0, normal = 0, orm = 0;
    f32 avg[8][5] = {};                       // per layer: linear albedo r, g, b, roughness, metallic
    u32 cols = 0, rows = 0, stride = 0, tile = 0, pad = 0, atlasW = 0, atlasH = 0;
    usize bytes = 0;                          // GPU upload size of the three, for the eviction log
};

// Where a section sits in the world, so the shader can find its weightmap texel for a world position.
struct TerrainSection {
    f32 originXCm = 0, originYCm = 0, spacingCm = 100;
    u32 n = 0;                                // samples per side; the weightmaps are n x n
};

// Fills out[kTerrainParamFloats] (row-major, y * kTerrainParamWidth + x) for the material's layers.
// `uvTilesPerCm` is the material's (1 / MaterialDesc::uvTiling); `section` null = no weightmap
// (invExtent 0: every pixel is the base layer).
void terrainParamTexels(const MaterialDesc& d, const TerrainTextures& tex, const TerrainSection* section,
                        f32 uvTilesPerCm, f32 out[kTerrainParamFloats]);

// A hash of everything that decides which atlas a material needs (layer texture references, tile and
// pad), so a cache can tell a re-resolve from a no-op. Weights, tiling and friction do not enter it.
u64 terrainAtlasHash(const MaterialDesc& d);

} // namespace aver::pbr
