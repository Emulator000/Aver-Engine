// The layered terrain material's CPU/GPU contract: material_prelude.hlsl's AVER_MAT_TERRAIN and
// AVER_TERRAIN_ROW_* defines against TerrainLayers.hpp, and the parameter texels for a hand-computed
// 3-layer material. CPU only, reads the shader text. Exit code = failure count.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/pbr/MaterialGpu.hpp"
#include "aver/pbr/TerrainLayers.hpp"

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

using namespace aver;
using namespace aver::pbr;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool near(f32 a, f32 b, f32 eps = 1e-6f) { return std::fabs(a - b) <= eps * std::fmax(1.0f, std::fabs(b)); }

// Reads the numeric value of `#define NAME <number or (1u << n)>` from the shader text, or -1.
static i64 defineValue(const std::string& text, const std::string& name) {
    const std::regex shift("#define\\s+" + name + "\\s+\\(1u\\s*<<\\s*(\\d+)\\)");
    const std::regex plain("#define\\s+" + name + "\\s+(\\d+)");
    std::smatch m;
    if (std::regex_search(text, m, shift)) return static_cast<i64>(1) << std::stoi(m[1].str());
    if (std::regex_search(text, m, plain)) return std::stoll(m[1].str());
    return -1;
}

int main() {
    std::ifstream f(std::string(AVER_REPO_ROOT) + "/modules/render.pbr/shaders/material_prelude.hlsl", std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string hlsl = ss.str();
    check(!hlsl.empty(), "read material_prelude.hlsl");

    check(defineValue(hlsl, "AVER_MAT_TERRAIN") == static_cast<i64>(MaterialFlag_TerrainLayers), "AVER_MAT_TERRAIN == MaterialFlag_TerrainLayers");
    check(defineValue(hlsl, "AVER_TERRAIN_ROW_UVSCALE") == kTerrainRowUvScale, "row UVSCALE");
    check(defineValue(hlsl, "AVER_TERRAIN_ROW_HEIGHTBIAS") == kTerrainRowHeightBias, "row HEIGHTBIAS");
    check(defineValue(hlsl, "AVER_TERRAIN_ROW_AVG_R") == kTerrainRowAvgR, "row AVG_R");
    check(defineValue(hlsl, "AVER_TERRAIN_ROW_AVG_G") == kTerrainRowAvgG, "row AVG_G");
    check(defineValue(hlsl, "AVER_TERRAIN_ROW_AVG_B") == kTerrainRowAvgB, "row AVG_B");
    check(defineValue(hlsl, "AVER_TERRAIN_ROW_AVG_ROUGH") == kTerrainRowAvgRough, "row AVG_ROUGH");
    check(defineValue(hlsl, "AVER_TERRAIN_ROW_AVG_METAL") == kTerrainRowAvgMetal, "row AVG_METAL");
    check(defineValue(hlsl, "AVER_TERRAIN_ROW_LAYOUT") == kTerrainRowLayout, "row LAYOUT");
    check(defineValue(hlsl, "AVER_TERRAIN_ROW_SECTION") == kTerrainRowSection, "row SECTION");
    check(kTerrainParamHeight == kTerrainRowSection + 1 && kTerrainParamWidth == kMaxTerrainLayers, "8 x 9 texture");
    check(hlsl.find("averTerrainMaps") != std::string::npos && hlsl.find("averStockAuthoredAt") != std::string::npos, "the shared body and the raster entry exist");

    MaterialDesc d;
    d.uvTiling = 200.0f;
    d.terrain.blendDepth = 0.3f;
    d.terrain.layers.resize(3);
    d.terrain.layers[0].tilingCm = 400.0f;
    d.terrain.layers[1].tilingCm = 800.0f; d.terrain.layers[1].heightBias = 0.1f;
    d.terrain.layers[2].tilingCm = 100.0f; d.terrain.layers[2].heightBias = -0.2f;

    TerrainTextures tex;
    tex.cols = 4; tex.rows = 2; tex.stride = 528; tex.tile = 512; tex.pad = 8; tex.atlasW = 2112; tex.atlasH = 1056;
    for (u32 i = 0; i < 3; ++i) {
        tex.avg[i][0] = 0.1f * (i + 1); tex.avg[i][1] = 0.2f * (i + 1); tex.avg[i][2] = 0.05f * (i + 1);
        tex.avg[i][3] = 0.5f + 0.1f * i; tex.avg[i][4] = 0.0f + 0.25f * i;
    }

    f32 out[kTerrainParamFloats];
    terrainParamTexels(d, tex, nullptr, 1.0f / 200.0f, out);
    const auto at = [&](u32 row, u32 x) { return out[row * kTerrainParamWidth + x]; };
    check(near(at(kTerrainRowUvScale, 0), 0.5f) && near(at(kTerrainRowUvScale, 1), 0.25f) && near(at(kTerrainRowUvScale, 2), 2.0f), "uv scales are (1/tiling) / uvTilesPerCm");
    check(near(at(kTerrainRowUvScale, 3), 0.0f), "unused layer columns are zero");
    check(near(at(kTerrainRowHeightBias, 1), 0.1f) && near(at(kTerrainRowHeightBias, 2), -0.2f), "height biases");
    check(near(at(kTerrainRowAvgR, 2), 0.3f) && near(at(kTerrainRowAvgG, 1), 0.4f) && near(at(kTerrainRowAvgB, 2), 0.15f), "average albedo");
    check(near(at(kTerrainRowAvgRough, 1), 0.6f) && near(at(kTerrainRowAvgMetal, 2), 0.5f), "average roughness and metallic");
    check(at(kTerrainRowLayout, 0) == 4 && at(kTerrainRowLayout, 1) == 2 && at(kTerrainRowLayout, 2) == 528 && at(kTerrainRowLayout, 3) == 512 &&
              at(kTerrainRowLayout, 4) == 8 && at(kTerrainRowLayout, 5) == 3 && near(at(kTerrainRowLayout, 6), 0.3f) && at(kTerrainRowLayout, 7) == 2112,
          "layout row: cols rows stride tile pad count blend atlasWidth");
    check(at(kTerrainRowSection, 2) == 0.0f && at(kTerrainRowSection, 3) == 0.0f && at(kTerrainRowSection, 4) == 1056, "no section: invExtent 0, atlas height kept");

    TerrainSection sec;
    sec.originXCm = 500.0f; sec.originYCm = -300.0f; sec.spacingCm = 100.0f; sec.n = 1025;
    terrainParamTexels(d, tex, &sec, 1.0f / 200.0f, out);
    check(at(kTerrainRowSection, 0) == 500.0f && at(kTerrainRowSection, 1) == -300.0f, "section origin");
    check(near(at(kTerrainRowSection, 2), 1.0f / (1025.0f * 100.0f)) && near(at(kTerrainRowSection, 3), 0.5f / 1025.0f), "invExtent and half texel");

    // The weightmap lookup the shader does, replayed: sample (ix, iy) must land at the centre of texel (ix, iy).
    const f32 wx = 500.0f + 17.0f * 100.0f;
    const f32 u = (wx - at(kTerrainRowSection, 0)) * at(kTerrainRowSection, 2) + at(kTerrainRowSection, 3);
    check(std::fabs(u * 1025.0f - 17.5f) < 1e-3f, "world position of sample 17 maps to texel 17's centre");

    TerrainDesc empty;
    check(!empty.enabled(), "no layers, not enabled");
    MaterialDesc a, b = a;
    check(terrainAtlasHash(a) == terrainAtlasHash(b), "equal descs hash equal");
    b.terrain.layers.resize(1);
    b.terrain.layers[0].baseColor.path = "x.png";
    check(terrainAtlasHash(a) != terrainAtlasHash(b), "a layer reference moves the atlas hash");
    MaterialDesc c = b;
    c.terrain.layers[0].tilingCm = 123.0f;
    check(terrainAtlasHash(b) == terrainAtlasHash(c), "tiling does not");

    AVER_INFO("TerrainLayout tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
