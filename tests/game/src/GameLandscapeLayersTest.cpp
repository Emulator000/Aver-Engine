// game::GameLandscape's paint layers, headless: the layer table and its by-name remap, ground samples (weights,
// dominant layer, friction, surface), the weight stroke and its undo/redo rects bit-exact, save and reload, and
// the derived section / ring materials. No device, no window, no physics.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcLand.hpp"
#include "aver/game/GameLandscape.hpp"

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#if AVER_MODULE_PBR
#  include "aver/pbr/Material.hpp"
#endif

using namespace aver;
using namespace aver::game;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static GameLandscape::LayerInfo layer(const char* name, const char* surface, f32 friction, f32 restitution) {
    GameLandscape::LayerInfo l;
    l.name = name; l.surface = surface; l.friction = friction; l.restitution = restitution;
    return l;
}

int main() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "aver_landscape_layers_test";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "t.ocland").string();
    {
        // 65 x 65 (64k+1 for the default 64-quad node), 100 cm apart, origin (0,0): a plane rising 20 cm per sample along +X.
        fmt::OcLandData d;
        d.sampleCount = 65;
        d.spacingCm = 100.0f;
        d.heights.resize(65u * 65u);
        for (u32 y = 0; y < 65; ++y)
            for (u32 x = 0; x < 65; ++x) d.heights[y * 65 + x] = static_cast<f32>(x) * 20.0f;
        std::string why;
        check(fmt::saveOcLand(path, d, &why), "wrote the test section (" + why + ")");
    }

    GameLandscape land;
    check(land.loadSection(nullptr, path), "loaded the section");

    AVER_INFO("=== no terrain material ===");
    GameLandscape::GroundInfo g;
    check(land.sampleGround(1600.0f, 1600.0f, g), "sampleGround works headless");
    // .ocland stores heights as u16 over the authored range (0..1280 cm here: ~0.02 cm a step).
    check(near(g.z, 320.0f, 0.05f) && near(g.normal[0], -std::sin(std::atan(0.2f)), 1e-3f) && near(g.normal[1], 0.0f) && g.normal[2] > 0.9f,
          "height and slope normal of the ramp (z " + std::to_string(g.z) + ", n " + std::to_string(g.normal[0]) + " " +
              std::to_string(g.normal[1]) + " " + std::to_string(g.normal[2]) + ")");
    check(g.surface.empty() && near(g.friction, 0.6f) && g.dominant == 0, "no layer table: defaults");
    check(!land.sampleGround(-500.0f, 0.0f, g), "false outside the section");
    u32 idx = 0;
    check(!land.ensureLayer("Grass", idx), "ensureLayer needs a layer table");
    f32 fr = 0, re = 0;
    check(!land.surfaceFrictionAt(1600.0f, 1600.0f, fr, re), "no friction answer without layers");

    AVER_INFO("=== layers and the by-name remap ===");
    land.setLayerTable({layer("Grass", "Turf", 0.8f, 0.05f), layer("Rock", "Stone", 0.3f, 0.2f), layer("Sand", "", 0.5f, 0.0f)});
    check(land.materialLayerCount() == 3 && land.materialLayerName(1) == "Rock" && land.terrainMaterialActive(), "table installed");
    check(land.ensureLayer("Rock", idx) && idx == 1, "Rock is section layer 1 (the base Grass was added first)");
    check(land.data().layerNames.size() == 2 && land.data().layerNames[0] == "Grass", "layer 0 is the material's base");
    check(land.dirty(), "adding a layer dirties the section");

    check(land.sampleGround(1600.0f, 1600.0f, g) && g.dominant == 0 && g.surface == "Turf" && near(g.friction, 0.8f) && near(g.weights[0], 1.0f),
          "unpainted is all base layer");

    landscape::PaintParams pp;
    pp.centerCm[0] = 1600.0f; pp.centerCm[1] = 1600.0f; pp.radiusCm = 600.0f; pp.layer = idx;
    land.beginWeightStroke();
    check(land.weightStrokeActive(), "stroke began");
    check(!land.ensureLayer("Sand", idx), "no new layer during a stroke");
    idx = 1;
    const landscape::BrushRect r = land.paint(nullptr, pp, 1.0f);
    check(!r.empty && land.weightsDirty(), "paint touched samples and marked the weights dirty");
    std::vector<u8> before, after;
    u32 x0, y0, x1, y1, planes;
    check(land.endWeightStroke(before, after, x0, y0, x1, y1, planes) && planes == 1, "the stroke made an undo entry");
    check(before.size() == after.size() && before != after && x0 <= x1 && y0 <= y1, "before / after differ over the rect");

    check(land.sampleGround(1600.0f, 1600.0f, g) && g.dominant == 1 && g.surface == "Stone" && near(g.friction, 0.3f, 1e-2f) &&
              near(g.restitution, 0.2f, 1e-2f) && g.weights[1] > 0.99f,
          "the painted centre is Rock: surface, friction, restitution, weights");
    f32 total = 0; for (f32 w : g.weights) total += w;
    check(near(total, 1.0f), "weights sum to 1");
    check(land.sampleGround(200.0f, 200.0f, g) && g.dominant == 0 && g.surface == "Turf", "far from the brush it is still Grass");
    check(land.surfaceFrictionAt(1600.0f, 1600.0f, fr, re) && near(fr, 0.3f, 1e-2f), "the physics-side friction query agrees");

    AVER_INFO("=== undo / redo restore bit for bit ===");
    const std::vector<u8> painted = land.data().weights;
    check(land.applyWeightRect(x0, y0, x1, y1, planes, before), "undo applied");
    bool allZero = true; for (u8 v : land.data().weights) if (v) allZero = false;
    check(allZero, "undo restored the unpainted plane exactly");
    check(land.applyWeightRect(x0, y0, x1, y1, planes, after), "redo applied");
    check(land.data().weights == painted, "redo restored the painted plane exactly");
    check(!land.applyWeightRect(x0, y0, x1, y1, planes + 3, after), "more planes than exist is refused");
    check(!land.applyWeightRect(0, 0, 99, 99, planes, after), "an out-of-range rect is refused");

    land.beginWeightStroke();
    landscape::PaintParams erase = pp;
    erase.mode = landscape::PaintMode::Erase;
    erase.centerCm[0] = 100.0f; erase.centerCm[1] = 100.0f;   // nothing painted there
    land.paint(nullptr, erase, 1.0f);
    check(!land.endWeightStroke(before, after, x0, y0, x1, y1, planes), "a stroke that changed nothing makes no entry");

    AVER_INFO("=== the remap follows names, not positions ===");
    land.setLayerTable({layer("Rock", "Stone", 0.3f, 0.2f), layer("Grass", "Turf", 0.8f, 0.05f), layer("Sand", "", 0.5f, 0.0f)});
    check(land.sampleGround(1600.0f, 1600.0f, g) && g.dominant == 0 && g.surface == "Stone" && g.weights[0] > 0.99f,
          "reordering the material moves the painted Rock to material layer 0");
    check(land.sampleGround(200.0f, 200.0f, g) && g.dominant == 1 && g.surface == "Turf", "and the base Grass to layer 1");
    land.setLayerTable({layer("Grass", "Turf", 0.8f, 0.05f), layer("Sand", "", 0.5f, 0.0f)});
    check(land.sampleGround(1600.0f, 1600.0f, g) && g.dominant == 0 && g.surface == "Turf", "a section layer the material lacks folds into the base");
    land.setLayerTable({layer("Grass", "Turf", 0.8f, 0.05f), layer("Rock", "Stone", 0.3f, 0.2f), layer("Sand", "", 0.5f, 0.0f)});

    AVER_INFO("=== whole-section edits, remove ===");
    land.beginWeightStroke();
    land.fillLayer(1);
    check(land.endWeightStroke(before, after, x0, y0, x1, y1, planes) && x0 == 0 && y1 == 64, "fill is one undo entry over the section");
    check(land.sampleGround(200.0f, 200.0f, g) && g.dominant == 1, "everything is Rock now");
    land.applyWeightRect(x0, y0, x1, y1, planes, before);
    land.autoFillBySlope(1, 5.0f, 60.0f, -1e9f, 1e9f, 0.0f, 1.0f);   // the ramp is ~11 degrees
    check(land.sampleGround(200.0f, 200.0f, g) && g.dominant == 1, "slope auto-fill selects the ramp");
    check(land.removeLayer(1) && land.data().layerNames.size() == 1, "removeLayer drops it");
    check(land.sampleGround(200.0f, 200.0f, g) && g.dominant == 0, "its paint folded into the base");

    AVER_INFO("=== save and reload ===");
    u32 i2 = 0;
    land.ensureLayer("Rock", i2);
    pp.layer = i2;
    land.paint(nullptr, pp, 1.0f);
    std::string why;
    check(land.save(&why), "saved (" + why + ")");
    check(!land.dirty(), "saving clears dirty");
    GameLandscape again;
    check(again.loadSection(nullptr, path), "reloaded");
    again.setLayerTable({layer("Grass", "Turf", 0.8f, 0.05f), layer("Rock", "Stone", 0.3f, 0.2f)});
    check(again.data().layerNames == land.data().layerNames && again.data().weights == land.data().weights, "names and weights survived the round trip");
    check(again.sampleGround(1600.0f, 1600.0f, g) && g.surface == "Stone", "and still sample as Rock");

    AVER_INFO("=== carve ===");
    {
        const f32 beforeZ = [&] { GameLandscape::GroundInfo q; again.sampleGround(2000.0f, 1600.0f, q); return q.z; }();
        const landscape::CarvePoint pts[2] = {{1000.0f, 1600.0f, 300.0f, 100.0f}, {3000.0f, 1600.0f, 300.0f, 100.0f}};
        landscape::CarveParams cp;
        cp.depthCm = 150.0f; cp.smooth = 0.0f;
        again.beginStroke();
        const landscape::BrushRect cr = again.carveRiver(nullptr, pts, 2, cp);
        std::vector<f32> hb, ha;
        u32 cx0, cy0, cx1, cy1;
        check(!cr.empty && again.endStroke(hb, ha, cx0, cy0, cx1, cy1), "carve is a height stroke with an undo entry");
        GameLandscape::GroundInfo q;
        again.sampleGround(2000.0f, 1600.0f, q);
        check(q.z < beforeZ - 100.0f, "the channel is lower (" + std::to_string(beforeZ) + " -> " + std::to_string(q.z) + ")");
    }

#if AVER_MODULE_PBR
    AVER_INFO("=== derived materials ===");
    {
        pbr::MaterialDesc base;
        base.name = "M_Terrain";
        base.terrain.layers.resize(2);
        base.terrain.layers[0].name = "Grass";
        base.terrain.layers[1].name = "Rock";
        base.textures[static_cast<u32>(pbr::TextureSlot::Occlusion)].path = "should_be_replaced.png";
        pbr::MaterialDesc sec, ring;
        GameLandscape::makeTerrainSectionDescs(base, sec, ring);
        check(sec.name == "M_Terrain#sec" && ring.name == "M_Terrain#ring", "derived names");
        check(sec.textures[static_cast<u32>(pbr::TextureSlot::Occlusion)].path == "@rt:land.w0" &&
                  sec.textures[static_cast<u32>(pbr::TextureSlot::Emissive)].path == "@rt:land.w1" &&
                  sec.textures[static_cast<u32>(pbr::TextureSlot::Layer1BaseColor)].path == "@rt:land.params",
              "the section's copy names the runtime weightmaps and parameters");
        bool noRt = true;
        for (const pbr::TextureRef& t : ring.textures) if (t.path.rfind("@rt:", 0) == 0) noRt = false;
        check(noRt && ring.textures[static_cast<u32>(pbr::TextureSlot::Occlusion)].empty(), "ring materials have no @rt: refs");
        check(sec.terrain.layers.size() == 2 && ring.terrain.layers.size() == 2, "both keep the layers");
    }
#endif

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    AVER_INFO("GameLandscapeLayers tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
