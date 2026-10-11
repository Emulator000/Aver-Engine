// Terrain layer parameter texels and atlas hash: see TerrainLayers.hpp.
#include "aver/pbr/TerrainLayers.hpp"

#include <algorithm>

namespace aver::pbr {

void terrainParamTexels(const MaterialDesc& d, const TerrainTextures& tex, const TerrainSection* section,
                        f32 uvTilesPerCm, f32 out[kTerrainParamFloats]) {
    for (u32 i = 0; i < kTerrainParamFloats; ++i) out[i] = 0.0f;
    const auto at = [&](u32 row, u32 x) -> f32& { return out[row * kTerrainParamWidth + x]; };

    const u32 count = std::min<u32>(static_cast<u32>(d.terrain.layers.size()), kMaxTerrainLayers);
    for (u32 i = 0; i < count; ++i) {
        const TerrainLayerDesc& l = d.terrain.layers[i];
        const f32 perCm = l.tilingCm > 0.0f ? 1.0f / l.tilingCm : 0.0f;
        at(kTerrainRowUvScale, i) = uvTilesPerCm > 0.0f ? perCm / uvTilesPerCm : 0.0f;
        at(kTerrainRowHeightBias, i) = l.heightBias;
        at(kTerrainRowAvgR, i) = tex.avg[i][0];
        at(kTerrainRowAvgG, i) = tex.avg[i][1];
        at(kTerrainRowAvgB, i) = tex.avg[i][2];
        at(kTerrainRowAvgRough, i) = tex.avg[i][3];
        at(kTerrainRowAvgMetal, i) = tex.avg[i][4];
    }
    at(kTerrainRowLayout, 0) = static_cast<f32>(tex.cols);
    at(kTerrainRowLayout, 1) = static_cast<f32>(tex.rows);
    at(kTerrainRowLayout, 2) = static_cast<f32>(tex.stride);
    at(kTerrainRowLayout, 3) = static_cast<f32>(tex.tile);
    at(kTerrainRowLayout, 4) = static_cast<f32>(tex.pad);
    at(kTerrainRowLayout, 5) = static_cast<f32>(count);
    at(kTerrainRowLayout, 6) = d.terrain.blendDepth;
    at(kTerrainRowLayout, 7) = static_cast<f32>(tex.atlasW);
    at(kTerrainRowSection, 4) = static_cast<f32>(tex.atlasH);
    if (section && section->n >= 2 && section->spacingCm > 0.0f) {
        at(kTerrainRowSection, 0) = section->originXCm;
        at(kTerrainRowSection, 1) = section->originYCm;
        at(kTerrainRowSection, 2) = 1.0f / (static_cast<f32>(section->n) * section->spacingCm);
        at(kTerrainRowSection, 3) = 0.5f / static_cast<f32>(section->n);
    }
}

u64 terrainAtlasHash(const MaterialDesc& d) {
    u64 h = 1469598103934665603ull;   // FNV-1a
    const auto mix = [&h](const void* p, usize n) {
        const u8* b = static_cast<const u8*>(p);
        for (usize i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    const auto mixStr = [&](const std::string& s) { mix(s.data(), s.size()); const u8 z = 0; mix(&z, 1); };
    const auto mixRef = [&](const TextureRef& r) { mixStr(r.path); mix(&r.id, sizeof r.id); };
    mix(&d.terrain.tileTexels, sizeof d.terrain.tileTexels);
    mix(&d.terrain.padTexels, sizeof d.terrain.padTexels);
    for (const TerrainLayerDesc& l : d.terrain.layers) {
        mixRef(l.baseColor); mixRef(l.normal); mixRef(l.metalRough); mixRef(l.height);
    }
    return h;
}

} // namespace aver::pbr
