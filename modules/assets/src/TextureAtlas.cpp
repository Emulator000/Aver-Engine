// Terrain atlas builder: see TextureAtlas.hpp.
#include "aver/assets/TextureAtlas.hpp"

#include "TextureUploadDetail.hpp"
#include "aver/core/Log.hpp"

#include <algorithm>
#include <cmath>

namespace aver::assets {

namespace {

f32 srgbDecode(u8 v) {
    const f32 c = static_cast<f32>(v) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// One tile at tile x tile texels, from its file(s) or the flat fill.
ImageData makeTile(const AtlasSpec& spec, const AtlasTile& t) {
    const u32 T = spec.tileTexels;
    ImageData tile;
    bool have = false;
    if (!t.colorPath.empty()) {
        fmt::TextureData td;
        std::string why;
        if (detail::decodeTexture(t.colorPath, spec.usage, td, &why) && td.valid()) {
            detail::resizeBox(td.levels.front(), T, T, spec.usage, tile);
            have = true;
        } else {
            AVER_WARN("[Atlas] {} could not be read ({}); the layer is filled flat", t.colorPath, why);
        }
    }
    if (!have) {
        tile.width = tile.height = T;
        tile.channels = 4;
        tile.srgb = spec.usage == TextureUsage::Colour;
        tile.pixels.resize(static_cast<usize>(T) * T * 4);
        u8 px[4];
        for (int c = 0; c < 4; ++c) px[c] = static_cast<u8>(std::clamp(t.fill[c], 0.0f, 1.0f) * 255.0f + 0.5f);
        for (usize i = 0; i < static_cast<usize>(T) * T; ++i)
            for (int c = 0; c < 4; ++c) tile.pixels[i * 4 + static_cast<usize>(c)] = px[c];
    }
    if (!t.alphaPath.empty()) {
        fmt::TextureData hd;
        std::string why;
        ImageData h;
        if (detail::decodeTexture(t.alphaPath, TextureUsage::Data, hd, &why) && hd.valid()) {
            detail::resizeBox(hd.levels.front(), T, T, TextureUsage::Data, h);
            for (usize i = 0; i < static_cast<usize>(T) * T; ++i) tile.pixels[i * 4 + 3] = h.pixels[i * 4];
        } else {
            AVER_WARN("[Atlas] {} could not be read ({}); the layer keeps its own alpha", t.alphaPath, why);
        }
    } else if (t.alphaFromLuma) {
        for (usize i = 0; i < static_cast<usize>(T) * T; ++i) {
            const u8* p = &tile.pixels[i * 4];
            const f32 l = 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
            tile.pixels[i * 4 + 3] = static_cast<u8>(std::clamp(l, 0.0f, 255.0f) + 0.5f);
        }
    }
    return tile;
}

} // namespace

u32 atlasAlignedLevels(u32 stride) {
    u32 n = 0;
    while (stride > 0 && (stride & 1u) == 0u) { stride >>= 1u; ++n; }
    return n;
}

bool buildAtlasImage(const AtlasSpec& spec, AtlasImage& out, std::string* err) {
    out = AtlasImage{};
    const auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (spec.cols == 0 || spec.rows == 0 || spec.cols * spec.rows > 8) return fail("atlas needs 1..8 cells");
    if (spec.tiles.size() > spec.cols * spec.rows) return fail("more tiles than cells");
    if (spec.tileTexels < 16 || spec.tileTexels > 2048) return fail("tile size out of range");
    if (spec.padTexels < 1 || spec.padTexels * 2 >= spec.tileTexels) return fail("pad out of range");

    const u32 T = spec.tileTexels, P = spec.padTexels, S = T + 2 * P;
    const u32 aligned = std::min(atlasAlignedLevels(S), 6u);
    const bool srgbColour = spec.usage == TextureUsage::Colour;
    out.width = spec.cols * S;
    out.height = spec.rows * S;

    // level m of the atlas for m <= aligned: cols * (S >> m) square cells
    std::vector<std::vector<u8>> lv(aligned + 1);
    for (u32 m = 0; m <= aligned; ++m)
        lv[m].assign(static_cast<usize>(spec.cols * (S >> m)) * (spec.rows * (S >> m)) * 4, 0);

    const AtlasTile flat;
    for (u32 cell = 0; cell < spec.cols * spec.rows; ++cell) {
        const AtlasTile& t = cell < spec.tiles.size() ? spec.tiles[cell] : flat;
        const ImageData tile = makeTile(spec, t);

        // The tile's mean, before the pad: what the shader falls back to at a distance.
        if (cell < 8) {
            f64 acc[4] = {0, 0, 0, 0};
            for (usize i = 0; i < static_cast<usize>(T) * T; ++i) {
                for (int c = 0; c < 3; ++c)
                    acc[c] += srgbColour ? srgbDecode(tile.pixels[i * 4 + static_cast<usize>(c)])
                                         : tile.pixels[i * 4 + static_cast<usize>(c)] * (1.0f / 255.0f);
                acc[3] += tile.pixels[i * 4 + 3] * (1.0f / 255.0f);
            }
            for (int c = 0; c < 4; ++c) out.tileAverage[cell][c] = static_cast<f32>(acc[c] / (static_cast<f64>(T) * T));
        }

        // The padded cell: the tile wrapped around itself.
        fmt::TextureData cellTex;
        cellTex.width = cellTex.height = S;
        cellTex.srgb = srgbColour;
        ImageData ci;
        ci.width = ci.height = S;
        ci.channels = 4;
        ci.srgb = srgbColour;
        ci.pixels.resize(static_cast<usize>(S) * S * 4);
        for (u32 y = 0; y < S; ++y) {
            const u32 ty = (y + T - P % T) % T;
            for (u32 x = 0; x < S; ++x) {
                const u32 tx = (x + T - P % T) % T;
                const u8* s = &tile.pixels[(static_cast<usize>(ty) * T + tx) * 4];
                u8* d = &ci.pixels[(static_cast<usize>(y) * S + x) * 4];
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
        }
        cellTex.levels.push_back(std::move(ci));
        detail::buildMips(cellTex, spec.usage);

        const u32 cx = cell % spec.cols, cy = cell / spec.cols;
        for (u32 m = 0; m <= aligned && m < cellTex.levels.size(); ++m) {
            const ImageData& src = cellTex.levels[m];
            const u32 cs = S >> m, aw = spec.cols * cs;
            for (u32 y = 0; y < cs; ++y) {
                const u8* srow = &src.pixels[static_cast<usize>(y) * src.width * 4];
                u8* drow = &lv[m][(static_cast<usize>(cy * cs + y) * aw + static_cast<usize>(cx) * cs) * 4];
                std::copy(srow, srow + static_cast<usize>(cs) * 4, drow);
            }
        }
    }

    // Past the aligned levels the atlas just keeps halving (flat-average territory only).
    fmt::TextureData tail;
    tail.width = spec.cols * (S >> aligned);
    tail.height = spec.rows * (S >> aligned);
    tail.srgb = srgbColour;
    ImageData top;
    top.width = tail.width; top.height = tail.height; top.channels = 4; top.srgb = srgbColour;
    top.pixels = lv[aligned];
    tail.levels.push_back(std::move(top));
    detail::buildMips(tail, spec.usage);

    for (u32 m = 0; m < aligned; ++m) out.levels.push_back(std::move(lv[m]));
    for (ImageData& l : tail.levels) out.levels.push_back(std::move(l.pixels));
    return true;
}

rhi::TextureHandle uploadAtlas(rhi::IResourceFactory& res, const AtlasImage& image, bool srgb, std::string* err,
                               TextureUploadInfo* info) {
    if (image.levels.empty() || image.width == 0 || image.height == 0) {
        if (err) *err = "empty atlas";
        return 0;
    }
    std::vector<const void*> levels;
    usize bytes = 0;
    for (const std::vector<u8>& l : image.levels) { levels.push_back(l.data()); bytes += l.size(); }
    rhi::TextureDesc d;
    d.width = image.width;
    d.height = image.height;
    d.mips = static_cast<u32>(levels.size());
    d.format = srgb ? rhi::Format::RGBA8UnormSrgb : rhi::Format::RGBA8Unorm;
    d.bind = rhi::ResourceBind::ShaderResource;
    d.initialState = rhi::ResourceState::ShaderResource;
    d.initialData = levels.data();
    d.initialDataCount = static_cast<u32>(levels.size());
    d.initialRowPitch = image.width * 4;
    d.debugName = "terrain atlas";
    const rhi::TextureHandle h = res.createTexture(d);
    if (!h) {
        if (err) *err = "the GPU atlas texture could not be created";
        return 0;
    }
    if (info) {
        *info = TextureUploadInfo{image.width, image.height, d.mips, bytes};
        for (int c = 0; c < 3; ++c) info->averageLinear[c] = image.tileAverage[0][c];
    }
    return h;
}

} // namespace aver::assets
