#pragma once
// The decode / resize / mip helpers behind uploadTexture, shared with the atlas builder
// (TextureAtlas.cpp) so both treat a usage the same way. Private to Aver.Assets.Gpu.
#include "aver/assets/TextureUpload.hpp"
#include "aver/formats/Texture.hpp"

namespace aver::assets::detail {

// Decodes `path` for `usage` and stops at level 0 (no mips). Colour is sRGB, NormalMap is linear and
// filtered as vectors, Data is linear.
bool decodeTexture(const std::string& path, TextureUsage usage, fmt::TextureData& out, std::string* err);

// Builds levels 1..N down to 1x1 with the usage-aware filter (linear light for Colour, vectors for a
// normal map). No coverage preservation.
void buildMips(fmt::TextureData& t, TextureUsage usage);

// Resamples `src` to w x h: an area average when shrinking (in linear light for sRGB Colour), bilinear
// with wrap when growing, so a tileable source stays tileable. `out` keeps src's srgb annotation.
void resizeBox(const ImageData& src, u32 w, u32 h, TextureUsage usage, ImageData& out);

} // namespace aver::assets::detail
