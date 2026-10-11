// game::spawnLevelLights and the pure light helpers that moved to Runtime (aver/game/LevelLights.hpp): the packaged game
// spawns LIGHT records now, so what a record means must be pinned. Needs a real scene::World; no device.
#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcWorld.hpp"
#include "aver/game/LevelLights.hpp"
#include "aver/scene/LightGather.hpp"
#include "aver/scene/World.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}
static bool near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }

int main() {
    // ---- ids: pinned literals, so a change of hash or of slash handling cannot pass silently ----
    check(game::lightAssetId("Lights/downlight.ies") == 0x3c157dc32e132d12ull, "lightAssetId of an .ies path is the pinned fnv1a64");
    check(game::lightAssetId("Textures\\gobo.png") == 0x5f9bdf28f1129836ull, "backslashes hash as forward slashes");
    check(game::lightAssetId("Lights/a_down.ies") == 0x10a609a5c88c0b84ull, "a second pinned id");

    // ---- cones ----
    check(near(game::lightConeCos(0.0f), 1.0f) && near(game::lightConeCos(60.0f), 0.5f) && near(game::lightConeDegrees(0.5f), 60.0f, 1e-2f),
          "lightConeCos / lightConeDegrees are inverse and pinned (60 degrees = 0.5)");
    check(near(game::lightConeCos(200.0f), std::cos(89.9f * kDegToRad)), "a cone angle clamps to 89.9 degrees");
    check(near(game::lightConeDegrees(2.0f), 0.0f), "a cosine above 1 reads 0 degrees");

    // ---- extensions ----
    check(game::lightHasExt("a.IES", true) && !game::lightHasExt("a.png", true), "lightHasExt: .ies is case-insensitive");
    check(game::lightHasExt("a.PNG", false) && game::lightHasExt("a.jpeg", false) && !game::lightHasExt("a.ies", false), "lightHasExt: cookie images");

    // ---- records -> entities -> gathered lights ----
    fmt::OcLight point;
    point.name = "Point";
    point.kind = fmt::OcLightKind::Point;
    point.x = 100; point.y = 0; point.z = 250;
    point.colour[0] = 1.0; point.colour[1] = 0.5; point.colour[2] = 0.25;
    point.intensityCd = 4000;
    point.rangeCm = 1500;
    point.radiusCm = 3;
    fmt::OcLight spot = point;
    spot.name = "Spot";
    spot.kind = fmt::OcLightKind::Spot;
    spot.innerDeg = 15; spot.outerDeg = 35;
    spot.castShadows = false;
    spot.ies = "Lights/downlight.ies";
    fmt::OcLight rect = point;
    rect.name = "Panel";
    rect.kind = fmt::OcLightKind::Rect;
    rect.widthCm = 120; rect.heightCm = 60;
    rect.outerDeg = 60;
    rect.cookie = "Textures/gobo.png";

    scene::World& world = scene::World::instance();
    const u32 before = static_cast<u32>(world.count());
    const std::vector<scene::Entity> made = game::spawnLevelLights(world, {point, spot, rect});
    check(made.size() == 3 && world.count() == before + 3, "three records make three entities");
    world.flush();

    std::vector<scene::WorldLight> lights;
    scene::gatherLights(world, lights);
    size_t ours = 0;
    const scene::WorldLight* gp = nullptr; const scene::WorldLight* gs = nullptr; const scene::WorldLight* gr = nullptr;
    for (const scene::WorldLight& l : lights) {
        if (l.entity == made[0]) { gp = &l; ++ours; }
        if (l.entity == made[1]) { gs = &l; ++ours; }
        if (l.entity == made[2]) { gr = &l; ++ours; }
    }
    check(ours == 3 && gp && gs && gr, "gatherLights returns all three");
    if (gp && gs && gr) {
        check(gp->kind == scene::kLightPoint && gs->kind == scene::kLightSpot && gr->kind == scene::kLightRect, "kinds map");
        check(near(gp->pos[0], 100.0f) && near(gp->pos[2], 250.0f), "the position is the record's");
        check(near(gp->colour[1], 0.5f) && near(gp->intensityCd, 4000.0f) && near(gp->rangeCm, 1500.0f) && near(gp->sourceRadiusCm, 3.0f),
              "colour, intensity, range and radius map");
        check(near(gs->innerCos, game::lightConeCos(15.0f)) && near(gs->outerCos, game::lightConeCos(35.0f)), "spot cone angles map to cosines");
        check(!gs->castShadows && gp->castShadows, "castShadows maps");
        check(gs->iesProfile == static_cast<i64>(0x3c157dc32e132d12ull), "the IES path becomes its pinned id");
        check(near(gr->widthCm, 120.0f) && near(gr->heightCm, 60.0f) && gr->cookie == static_cast<i64>(0x5f9bdf28f1129836ull), "rect size and cookie map");
    }

    // ---- the record -> component direction round trips through the component's fields ----
    const scene::CLight c = game::lightFromRecord(spot);
    check(c.kind == scene::kLightSpot && near(c.outerCos, game::lightConeCos(35.0f)) && (c.flags & scene::kLightNoShadows) != 0,
          "lightFromRecord fills the component");
    const Transform xf = game::transformFromLightRecord(point);
    check(near(xf.position.x, 100.0f) && near(xf.position.z, 250.0f), "transformFromLightRecord is the record's pose");

    for (const scene::Entity e : made) world.destroy(e);
    world.flush();
    if (g_failures) { AVER_ERROR("LevelLightsTest: {} failure(s)", g_failures); return 1; }
    AVER_INFO("LevelLightsTest: all passed");
    return 0;
}
