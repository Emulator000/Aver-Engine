// The terrain atlas builder (assets/TextureAtlas.hpp): cell origins, wrapped pad, per-level cell
// alignment, alpha sources, flat fill and tile averages. CPU only, no device. Exit code = failure count.
#include "aver/assets/TextureAtlas.hpp"
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::assets;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

// Writes an uncompressed 32-bit top-left TGA (stb_image reads it) from RGBA bytes.
static bool writeTga(const std::string& path, u32 w, u32 h, const std::vector<u8>& rgba) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    u8 hdr[18] = {};
    hdr[2] = 2;
    hdr[12] = static_cast<u8>(w & 255); hdr[13] = static_cast<u8>(w >> 8);
    hdr[14] = static_cast<u8>(h & 255); hdr[15] = static_cast<u8>(h >> 8);
    hdr[16] = 32;
    hdr[17] = 0x28;   // top-left origin, 8 alpha bits
    f.write(reinterpret_cast<const char*>(hdr), 18);
    for (usize i = 0; i < static_cast<usize>(w) * h; ++i) {
        const char bgra[4] = {static_cast<char>(rgba[i * 4 + 2]), static_cast<char>(rgba[i * 4 + 1]),
                              static_cast<char>(rgba[i * 4 + 0]), static_cast<char>(rgba[i * 4 + 3])};
        f.write(bgra, 4);
    }
    return static_cast<bool>(f);
}

static std::vector<u8> gradient(u32 n) {
    std::vector<u8> p(static_cast<usize>(n) * n * 4);
    for (u32 y = 0; y < n; ++y)
        for (u32 x = 0; x < n; ++x) {
            u8* d = &p[(static_cast<usize>(y) * n + x) * 4];
            d[0] = static_cast<u8>(x * 7);        // distinct per column
            d[1] = static_cast<u8>(y * 7);        // distinct per row
            d[2] = static_cast<u8>((x + y) * 3);
            d[3] = 255;
        }
    return p;
}

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "aver_atlas_test";
    std::filesystem::create_directories(dir);
    const std::string gradPath = (dir / "grad.tga").string();
    const std::string grayPath = (dir / "gray.tga").string();
    const std::string heightPath = (dir / "height.tga").string();
    const u32 T = 32, P = 4, S = T + 2 * P;
    const std::vector<u8> grad = gradient(T);
    check(writeTga(gradPath, T, T, grad), "wrote the gradient");
    check(writeTga(grayPath, T, T, std::vector<u8>(static_cast<usize>(T) * T * 4, 100)), "wrote the gray tile");
    {
        std::vector<u8> h(static_cast<usize>(T) * T * 4, 200);
        check(writeTga(heightPath, T, T, h), "wrote the height map");
    }

    AtlasSpec spec;
    spec.cols = 2; spec.rows = 2;
    spec.tileTexels = T; spec.padTexels = P;
    spec.usage = TextureUsage::Data;   // exact byte round trips
    spec.tiles.resize(3);
    spec.tiles[0].colorPath = gradPath;
    spec.tiles[1].colorPath = grayPath;
    spec.tiles[1].alphaFromLuma = true;
    spec.tiles[2].fill[0] = 1.0f; spec.tiles[2].fill[1] = 0.0f; spec.tiles[2].fill[2] = 0.0f; spec.tiles[2].fill[3] = 1.0f;   // missing file: flat red
    // tile 3 is absent: the default flat 0.5

    AtlasImage img;
    std::string err;
    check(buildAtlasImage(spec, img, &err), "builds (" + err + ")");
    check(img.width == 2 * S && img.height == 2 * S, "atlas is cols*S by rows*S");
    check(atlasAlignedLevels(S) == 3, "40 texels stay aligned for 3 halvings");
    // 80, 40, 20, 10 (cell aligned), then the whole atlas keeps halving: 5, 2, 1
    check(img.levels.size() == 7, "seven levels (got " + std::to_string(img.levels.size()) + ")");
    check(!img.levels.empty() && img.levels[0].size() == static_cast<usize>(80) * 80 * 4, "level 0 size");
    check(img.levels.size() > 3 && img.levels[3].size() == static_cast<usize>(10) * 10 * 4, "level 3 is 10 x 10");
    check(img.levels.size() == 7 && img.levels[6].size() == 4, "last level is 1 x 1");

    const auto px = [&](u32 level, u32 w, u32 x, u32 y, u32 c) {
        return img.levels[level][(static_cast<usize>(y) * w + x) * 4 + c];
    };
    // Cell origin and tile content: tile 0 is cell (0,0); its tile starts at (P, P).
    bool tileOk = true;
    for (u32 y = 0; y < T; ++y)
        for (u32 x = 0; x < T; ++x)
            for (u32 c = 0; c < 4; ++c)
                if (px(0, 80, P + x, P + y, c) != grad[(static_cast<usize>(y) * T + x) * 4 + c]) tileOk = false;
    check(tileOk, "tile 0 sits at cell origin + pad, byte for byte");
    // The pad is the WRAPPED opposite edge.
    bool padOk = true;
    for (u32 y = 0; y < S; ++y)
        for (u32 x = 0; x < S; ++x) {
            const u32 tx = (x + T - P) % T, ty = (y + T - P) % T;
            for (u32 c = 0; c < 3; ++c)
                if (px(0, 80, x, y, c) != grad[(static_cast<usize>(ty) * T + tx) * 4 + c]) padOk = false;
        }
    check(padOk, "the pad holds the wrapped opposite edge, corners included");
    // Tile 1 (gray, alpha from luma) is cell (1,0): origin x = S.
    check(px(0, 80, S + P + 3, P + 3, 0) == 100 && px(0, 80, S + P + 3, P + 3, 3) == 100, "gray tile with alpha = luminance");
    // Tile 2 (missing file -> flat fill) is cell (0,1).
    check(px(0, 80, P + 1, S + P + 1, 0) == 255 && px(0, 80, P + 1, S + P + 1, 1) == 0, "flat fill for a tile whose file is missing");
    // Tile 3 (absent) is cell (1,1): default 0.5 -> 128.
    check(px(0, 80, S + P, S + P, 0) == 128, "absent tile is the default flat fill");

    // Alignment through mip 3: constant cells stay constant (a misaligned cell would mix its neighbour in).
    bool aligned = true;
    for (u32 m = 1; m <= 3; ++m) {
        const u32 cs = S >> m, w = 2 * cs;
        for (u32 y = 0; y < cs; ++y)
            for (u32 x = 0; x < cs; ++x) {
                if (px(m, w, cs + x, y, 0) != 100) aligned = false;            // gray cell (1,0)
                if (px(m, w, x, cs + y, 0) != 255) aligned = false;            // red cell (0,1)
                if (px(m, w, cs + x, cs + y, 0) != 128) aligned = false;       // default cell (1,1)
            }
    }
    check(aligned, "cells stay aligned and uncontaminated through mip 3");

    // Averages: tile 0 is the gradient's mean; fills are their own value; Data usage is raw 0..1.
    f64 mean[3] = {0, 0, 0};
    for (usize i = 0; i < static_cast<usize>(T) * T; ++i)
        for (int c = 0; c < 3; ++c) mean[c] += grad[i * 4 + static_cast<usize>(c)] / 255.0;
    bool avgOk = true;
    for (int c = 0; c < 3; ++c) if (std::fabs(img.tileAverage[0][c] - mean[c] / (T * T)) > 1e-4) avgOk = false;
    check(avgOk, "tile average equals the mean of the tile");
    check(std::fabs(img.tileAverage[1][0] - 100.0f / 255.0f) < 1e-4f, "gray average");
    check(std::fabs(img.tileAverage[2][0] - 1.0f) < 1e-4f && std::fabs(img.tileAverage[2][1]) < 1e-4f, "flat fill average");

    // Height from a file: alpha comes from the file's R.
    {
        AtlasSpec h = spec;
        h.tiles.resize(1);
        h.tiles[0].colorPath = grayPath;
        h.tiles[0].alphaPath = heightPath;
        h.tiles[0].alphaFromLuma = true;   // an explicit alpha file wins
        AtlasImage a;
        check(buildAtlasImage(h, a, &err), "builds with an alpha file");
        check(a.levels[0][(static_cast<usize>(P + 5) * 80 + P + 5) * 4 + 3] == 200, "alpha comes from the alpha file's R, not the luma");
    }

    // Colour usage: the tile average is in linear light.
    {
        AtlasSpec c = spec;
        c.usage = TextureUsage::Colour;
        c.tiles.resize(1);
        c.tiles[0].colorPath = grayPath;
        AtlasImage a;
        check(buildAtlasImage(c, a, &err), "builds as Colour");
        const f32 lin = std::pow((100.0f / 255.0f + 0.055f) / 1.055f, 2.4f);
        check(std::fabs(a.tileAverage[0][0] - lin) < 2e-3f, "Colour average is decoded to linear light");
    }

    // A bad spec fails with a reason.
    {
        AtlasSpec bad = spec;
        bad.tiles.resize(5);
        AtlasImage a;
        check(!buildAtlasImage(bad, a, &err) && !err.empty(), "more tiles than cells is refused");
        bad = spec;
        bad.padTexels = 40;
        check(!buildAtlasImage(bad, a, &err), "a pad wider than the tile is refused");
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    AVER_INFO("TextureAtlas tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
