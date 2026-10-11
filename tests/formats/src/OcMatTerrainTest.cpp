// Layered terrain materials in .ocmat (TERRAIN + LAYER records): parse, defaults, byte-stable round
// trip, refusals, and what packMaterial does with the flag. CPU only. Exit code = failure count.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcMat.hpp"
#include "aver/pbr/MaterialGpu.hpp"

#include <cmath>
#include <string>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("   PASS  {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

static const char* kTerrain =
    "OCMAT 1\nNAME M_Terrain\nSHADER standard\n"
    "PARAM uvTiling 1000\n"
    "TERRAIN tile 512 pad 8 blend 0.2\n"
    "LAYER Grass tiling 400 baseColor {path:Textures/T/grass diff.png} normal {path:Textures/T/grass_nor.png} "
    "metalRough {path:Textures/T/grass_orm.png} height {path:Textures/T/grass_h.png} surface Turf friction 0.8 "
    "restitution 0.05 foliage Foliage/Grass.ocfoliage density 8 minweight 0.6\n"
    "LAYER Rock tiling 600 heightbias 0.25 baseColor {guid:0x00000000000000AB}\n"
    "LAYER Sand\n";

int main() {
    AVER_INFO("=== parse ===");
    pbr::MaterialDesc d;
    std::string err;
    check(fmt::parseOcmat(kTerrain, d, nullptr, &err), "parses (" + err + ")");
    check(d.terrain.enabled() && d.terrain.layers.size() == 3, "three layers, file order kept");
    check(d.terrain.tileTexels == 512 && d.terrain.padTexels == 8 && std::fabs(d.terrain.blendDepth - 0.2f) < 1e-6f, "TERRAIN tokens");
    if (d.terrain.layers.size() == 3) {
        const pbr::TerrainLayerDesc& g = d.terrain.layers[0];
        check(g.name == "Grass" && g.tilingCm == 400.0f, "layer name and tiling");
        check(g.baseColor.path == "Textures/T/grass diff.png", "a braced reference keeps its space");
        check(g.normal.path == "Textures/T/grass_nor.png" && g.metalRough.path == "Textures/T/grass_orm.png" && g.height.path == "Textures/T/grass_h.png",
              "the other three references");
        check(g.surface == "Turf" && g.surfaceName() == "Turf" && g.friction == 0.8f && g.restitution == 0.05f, "surface, friction, restitution");
        check(g.foliage == "Foliage/Grass.ocfoliage" && g.foliageDensity == 8.0f && g.foliageMinWeight == 0.6f, "foliage tokens");
        const pbr::TerrainLayerDesc& r = d.terrain.layers[1];
        check(r.heightBias == 0.25f && r.baseColor.id == 0xAB && r.baseColor.path.empty(), "guid reference and heightbias");
        const pbr::TerrainLayerDesc& s = d.terrain.layers[2];
        check(s.tilingCm == 400.0f && s.friction == 0.6f && s.restitution == 0.0f && s.foliageMinWeight == 0.5f &&
                  s.surface.empty() && s.surfaceName() == "Sand" && s.foliage.empty(), "defaults for a bare LAYER");
    }

    AVER_INFO("=== round trip ===");
    const std::string w1 = fmt::writeOcmat(d);
    pbr::MaterialDesc d2;
    check(fmt::parseOcmat(w1, d2, nullptr, &err), "written text parses");
    const std::string w2 = fmt::writeOcmat(d2);
    check(w1 == w2, "round trip is byte-stable");
    check(has(w1, "TERRAIN tile 512 pad 8 blend 0.2\n"), "TERRAIN line");
    check(has(w1, "LAYER Sand tiling 400\n"), "defaults omitted");
    check(has(w1, "heightbias 0.25"), "non-default heightbias written");
    pbr::MaterialDesc plain;
    check(!has(fmt::writeOcmat(plain), "TERRAIN") && !has(fmt::writeOcmat(plain), "LAYER"), "a plain material writes neither record");

    AVER_INFO("=== refusals ===");
    {
        std::string nine = "OCMAT 1\nNAME Nine\nTERRAIN tile 512 pad 8 blend 0.1\n";
        for (int i = 0; i < 9; ++i) nine += "LAYER L" + std::to_string(i) + "\n";
        pbr::MaterialDesc n;
        check(fmt::parseOcmat(nine, n, nullptr, &err) && !n.terrain.enabled(), "nine layers refuse the block and the material still loads");
        pbr::MaterialDesc dup;
        check(fmt::parseOcmat("OCMAT 1\nTERRAIN\nLAYER A\nLAYER B\nLAYER A\n", dup, nullptr, &err) && !dup.terrain.enabled(), "a duplicate name refuses the block");
        pbr::MaterialDesc bad;
        check(fmt::parseOcmat("OCMAT 1\nTERRAIN\nLAYER A\nLAYER bad-name\n", bad, nullptr, &err) && !bad.terrain.enabled(), "an invalid name refuses the block");
        pbr::MaterialDesc orphan;
        check(fmt::parseOcmat("OCMAT 1\nLAYER A\nLAYER B\n", orphan, nullptr, &err) && !orphan.terrain.enabled(), "LAYER without TERRAIN is ignored");
        pbr::MaterialDesc unk;
        check(fmt::parseOcmat("OCMAT 1\nTERRAIN tile 1024 pad 5 blend 3 colour red\nLAYER A tiling 100 sparkle 7 friction 0.3\nLAYER B\n", unk, nullptr, &err) &&
                  unk.terrain.enabled(),
              "unknown tokens are skipped");
        check(unk.terrain.tileTexels == 1024 && unk.terrain.padTexels == 4 && unk.terrain.blendDepth == 1.0f, "tile kept, pad made even and clamped, blend clamped");
        check(unk.terrain.layers[0].friction == 0.3f, "known tokens after an unknown one still parse");
        pbr::MaterialDesc badTile;
        fmt::parseOcmat("OCMAT 1\nTERRAIN tile 300\nLAYER A\n", badTile, nullptr, &err);
        check(badTile.terrain.tileTexels == 512, "an unsupported tile size falls back to 512");
    }

    AVER_INFO("=== packMaterial ===");
    {
        pbr::MaterialConstants c = pbr::packMaterial(d);
        check((c.flags & pbr::MaterialFlag_TerrainLayers) != 0u && pbr::MaterialFlag_TerrainLayers == (1u << 17), "bit 17 set with layers");
        check((c.flags & pbr::MaterialFlag_NormalMap) != 0u && (c.flags & pbr::MaterialFlag_OcclusionMap) == 0u &&
                  (c.flags & pbr::MaterialFlag_EmissiveMap) == 0u,
              "atlas maps flagged, weightmap slots are not occlusion / emissive");
        pbr::MaterialDesc slope = d;
        slope.slopeBlend = true;
        slope.textures[static_cast<u32>(pbr::TextureSlot::Layer1BaseColor)].path = "x.png";
        c = pbr::packMaterial(slope);
        check((c.flags & pbr::MaterialFlag_SlopeBlend) == 0u, "terrain clears the slope blend");
        pbr::MaterialDesc none;
        none.slopeBlend = true;
        none.textures[static_cast<u32>(pbr::TextureSlot::Layer1BaseColor)].path = "x.png";
        c = pbr::packMaterial(none);
        check((c.flags & pbr::MaterialFlag_TerrainLayers) == 0u && (c.flags & pbr::MaterialFlag_SlopeBlend) != 0u, "a plain material is untouched");
        d2.terrain.layers.clear();
        check((pbr::packMaterial(d2).flags & pbr::MaterialFlag_TerrainLayers) == 0u, "no layers, no bit");
        check(sizeof(pbr::MaterialConstants) == 192, "the constant block is still 192 bytes");
    }

    AVER_INFO("OcMatTerrain tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
