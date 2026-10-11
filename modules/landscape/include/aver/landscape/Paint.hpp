#pragma once
// Weightmap paint on a landscape section (OcLandData::layerNames / weights): brush Paint / Erase /
// Smooth, fill, slope auto-fill, name-based remap to a material's layers and the RGBA8 weight textures
// the terrain material samples. Pure CPU like the rest of Aver.Landscape; never touches heights.
//
// Weights are 8-bit, layer 0 implicit (255 - sum), so every edit re-quantises the touched sample with
// largest remainder and the stored sum is always <= 255 (the writer enforces it).
#include "aver/landscape/Sculpt.hpp"

#include <string_view>
#include <vector>

namespace aver::landscape {

using aver::i32;
using aver::u8;

enum class PaintMode { Paint, Erase, Smooth };

struct PaintParams {
    f32 centerCm[2] = {0.0f, 0.0f};
    f32 radiusCm = 500.0f;
    f32 strength = 1.0f;     // 0..1: share of the gap closed per full-amount tick
    f32 falloff = 1.0f;      // as BrushParams::falloff
    PaintMode mode = PaintMode::Paint;
    u32 layer = 0;           // index into data.layerNames
    f32 target = 1.0f;       // Paint stops at this weight (opacity cap)
};

// The rect a paint tick touches (same geometry and clamp as brushRect).
BrushRect paintRect(const fmt::OcLandData& d, const PaintParams& p);

// Applies one tick to the weights in place. Returns paintRect(); nothing changes when the brush
// misses the grid, the layer does not exist or amount <= 0.
BrushRect applyPaint(fmt::OcLandData& d, const PaintParams& p, f32 amount = 1.0f);

// Finds or appends a layer by name; false at 8 layers or for a bad name. The first layer added to an
// unpainted section becomes the base (layer 0, no plane); later ones append a zero plane.
bool ensureLayer(fmt::OcLandData& d, std::string_view name, u32& outIndex);
// Drops layer `index` (>= 1); its weight folds into the implicit base layer.
bool removeLayer(fmt::OcLandData& d, u32 index);
// Whole section -> that layer at weight 1.
void fillLayer(fmt::OcLandData& d, u32 layer);

// Paints `layer` where the slope (degrees, central difference) is in [minSlopeDeg, maxSlopeDeg] and
// the height in [minZCm, maxZCm]; featherDeg softens the slope band's edges. amount 0..1.
BrushRect autoFillBySlope(fmt::OcLandData& d, u32 layer, f32 minSlopeDeg, f32 maxSlopeDeg,
                          f32 minZCm, f32 maxZCm, f32 featherDeg, f32 amount);

// Bilinear weights at a world XY, normalised to sum 1 (slots past the layer count are 0). An unpainted
// section answers layer 0 = 1. False outside the footprint (out left zeroed).
bool layerWeightsAt(const fmt::OcLandData& d, f32 worldX, f32 worldY, f32 out[8]);
// Arg max over the first `count` weights; ties go to the lower index.
u32 dominantLayer(const f32 w[8], u32 count);

// Packs the section's weights into two N*N RGBA8 images for the terrain material: A = material layers
// 1..4 (rgba), B = 5..7 (rgb, a = 0). `materialChannelOfLayer[i]` is the material layer a section
// layer i maps to (-1 or >= materialLayerCount: folds into the base). Layer 0 is implicit in the
// shader (1 - sum), so nothing is stored for it.
void packWeightTextures(const fmt::OcLandData& d, const i32 materialChannelOfLayer[8], u32 materialLayerCount,
                        std::vector<u8>& rgbaA, std::vector<u8>& rgbaB);

// Box-filters an n x n RGBA8 image down to 1x1. levels[0] = level0; sizes max(1, s/2), edges clamped.
void buildMipChainRGBA8(u32 n, const std::vector<u8>& level0, std::vector<std::vector<u8>>& levels);

} // namespace aver::landscape
