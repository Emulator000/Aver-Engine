#pragma once
// Packs up to 8 tileable textures into one atlas for a layered terrain material (docs/MATERIALS.md,
// "Terrain layers"). The RHI has no array textures and a material has 8 SRVs, so layers share one
// texture per map kind.
//
// Layout: `cols x rows` cells, each S = tile + 2 * pad texels square with the tile in its middle and the
// pad holding the WRAPPED neighbouring texels, so bilinear sampling at a tile edge continues the tile.
// Mips are built per cell and then assembled, which stays aligned while S is divisible by 2^m (528
// with the default tile 512 / pad 8 is divisible by 16); deeper levels continue from the assembled level
// and are only ever used for the flat-average blend. The pure CPU part (buildAtlasImage) needs no device.
#include "aver/assets/TextureUpload.hpp"

#include <string>
#include <vector>

namespace aver::assets {

struct AtlasTile {
    std::string colorPath;       // empty = flat fill
    std::string alphaPath;       // optional: its R channel becomes the tile's alpha (a layer height map)
    bool alphaFromLuma = false;  // no alphaPath: alpha = luminance of the tile
    f32 fill[4] = {0.5f, 0.5f, 0.5f, 0.5f};   // the flat colour (0..1 of the stored bytes) when colorPath is empty
};

struct AtlasSpec {
    u32 cols = 4, rows = 2;
    u32 tileTexels = 512, padTexels = 8;
    TextureUsage usage = TextureUsage::Colour;
    std::vector<AtlasTile> tiles;             // <= cols * rows; missing cells are filled flat
};

struct AtlasImage {
    u32 width = 0, height = 0;
    std::vector<std::vector<u8>> levels;      // RGBA8 per mip, level 0 first
    // Mean of each tile (the tile only, not its pad): rgb in LINEAR light for Colour usage and raw 0..1
    // otherwise, alpha raw.
    f32 tileAverage[8][4] = {};
};

// How many mip levels stay cell-aligned for a cell of `stride` texels (the count of trailing zero bits).
u32 atlasAlignedLevels(u32 stride);

// Builds the atlas on the CPU. False with `err` for a bad spec; a tile whose file cannot be read is
// logged and filled flat rather than failing the atlas.
bool buildAtlasImage(const AtlasSpec& spec, AtlasImage& out, std::string* err);

// Uploads a built atlas. `srgb` picks the view format (true for the albedo atlas).
rhi::TextureHandle uploadAtlas(rhi::IResourceFactory& res, const AtlasImage& image, bool srgb,
                               std::string* err, TextureUploadInfo* info = nullptr);

} // namespace aver::assets
