// Packed level instances: the rows reach the sink through the instance matrix, the kept placements stay entities, and
// every reason to fall back realises the instance unpacked. No GPU: the sink is a fake.
#include "LevelInstanceFixture.hpp"

#include "aver/world/LevelPack.hpp"

static const char* kRoom =
    "OCWORLD 1\n"
    "PLACE Meshes/crate.ocmesh 100 0 0 0 0 0 1 id=1\n"
    "PLACE Meshes/crate.ocmesh 200 0 0 0 0 0 1 id=2\n"
    "PLACE Meshes/crate.ocmesh 300 0 0 0 0 0 1 nocollide id=3\n"
    "PLACE Meshes/hid.ocmesh 0 50 0 0 0 0 1 hidden id=4\n";

static u64 theOwner(const Fixture& f) { return f.sink.empty() ? 0 : f.sink.begin()->first; }

int main() {
#if AVER_MODULE_PHYSICS
    aver_phys_init();
#endif
    const std::filesystem::path proj = std::filesystem::temp_directory_path() / "aver_levelinst_packed_test";
    std::error_code ec;
    std::filesystem::remove_all(proj, ec);
    std::filesystem::create_directories(proj);

    Fixture f;
    f.projectDir = proj.string();
    f.levels["Room"] = parse(kRoom);

    // Bake the pack the instances will show.
    world::PackBakeOptions bopt;
    world::PackBakeReport rep;
    std::string why;
    check(world::bakeLevelPack(f.lib, "Room", "", proj.string(), bopt, rep, &why), "the pack bakes: " + why);
    check(rep.rows == 3 && rep.kept == 1, "three rows, one kept placement");

    const fmt::OcWorldData host = parse("OCWORLD 1\nLEVELINST Room pos 1000 0 0 packed id=1\nENDLEVELINST\n");
    const i32 bodies0 = bodyCount();
    f.li.setPackedRenderOk(true);
    f.begin(host);
    const scene::Entity root = f.li.at(0);
    check(f.li.packState(root) == "packed", "the instance shows its pack");
    check(f.sink.size() == 1, "one table reached the sink");
    const u64 owner = theOwner(f);
    if (f.sink.size() == 1) {
        const auto& set = f.sink.begin()->second;
        check(set.data.transforms.size() == 36, "three 12-float rows");
        // Rows are the pack's child-local rows times the instance matrix: translation x + 1000.
        check(near(set.data.transforms[0 * 12 + 9], 1100.0f) && near(set.data.transforms[1 * 12 + 9], 1200.0f) &&
                  near(set.data.transforms[2 * 12 + 9], 1300.0f),
              "the rows went through the instance matrix");
        check(set.data.groups.size() == 2 && set.mats.size() == 2, "two groups and their material list");
    }
    check(f.li.find(root, "4") != scene::kInvalidEntity, "the KEEP placement is an entity");
    check(f.li.find(root, "1") == scene::kInvalidEntity && f.li.find(root, "3") == scene::kInvalidEntity, "the packed placements are not");
    check(f.li.stats().packedRows == 3, "stats count the packed rows");
#if AVER_MODULE_PHYSICS
    if (aver_phys_ready()) {
        // Two colliding rows plus the kept placement's own body; the nocollide row has none.
        check(bodyCount() == bodies0 + 3, "colliding rows make one body each, beside the kept entity's");
    }
#endif
    const int meshesHeld = f.acquireTotal() - f.releaseTotal();

    // The renderer cannot show packed rows: the same instance realises unpacked, with every placement an entity.
    f.li.setPackedRenderOk(false);
    f.li.tick({});
    check(f.li.packState(root) == "unpacked: renderer", "no ray-driven rendering: unpacked");
    check(f.sink.empty(), "the sink was cleared");
    check(f.li.find(root, "1") != scene::kInvalidEntity && f.li.find(root, "2") != scene::kInvalidEntity &&
              f.li.find(root, "3") != scene::kInvalidEntity && f.li.find(root, "4") != scene::kInvalidEntity,
          "all four placements are entities, as in the unpacked baseline");
    check(f.li.stats().entities == 5 && f.li.stats().packedRows == 0, "the entity count is the unpacked baseline (root + 4)");

    // And back, with nothing leaked.
    f.li.setPackedRenderOk(true);
    f.li.tick({});
    check(f.li.packState(root) == "packed" && f.sink.size() == 1, "ray-driven again: packed");
    check(f.acquireTotal() - f.releaseTotal() == meshesHeld, "toggling leaks no meshes");
#if AVER_MODULE_PHYSICS
    if (aver_phys_ready()) check(bodyCount() == bodies0 + 3, "toggling leaks no bodies");
#endif
    check(f.sink.size() == 1 && theOwner(f) == owner, "...and no rows");

    // Any override unpacks.
    check(f.li.setOverrides(root, std::vector<fmt::OcPrefabOverride>{ovStr("4", fmt::kLiFieldMaterial, "M_X")}), "set an override");
    check(f.li.packState(root) == "unpacked: overrides" && f.sink.empty(), "an instance with overrides realises unpacked");
    f.li.setOverrides(root, {});
    check(f.li.packState(root) == "packed", "clearing them packs it again");

    // Editing unpacks.
    check(f.li.beginEdit(root), "begin editing");
    check(f.li.packState(root) == "unpacked: editing" && f.li.find(root, "1") != scene::kInvalidEntity, "an instance being edited is unpacked");
    f.li.discardEdit(root);
    check(f.li.packState(root) == "packed", "discarding the edit packs it again");

    // A child edit makes the pack stale.
    fmt::OcWorldData grown = f.levels["Room"];
    grown.placements.push_back(parse("OCWORLD 1\nPLACE Meshes/extra.ocmesh 0 0 0 0 0 0 1 id=9\n").placements[0]);
    f.lib.put("Room", grown);
    f.li.reloadAsset("Room");
    check(f.li.packState(root) == "pack stale" && f.sink.empty(), "a stale pack is not used");
    check(f.li.find(root, "9") != scene::kInvalidEntity, "...and the instance shows the current child");

    // No pack at all.
    f.levels["Bare"] = parse("OCWORLD 1\nPLACE Meshes/crate.ocmesh 1 0 0 0 0 0 1 id=1\n");
    fmt::OcLevelInstance bare;
    bare.asset = "Bare";
    bare.packed = true;
    const scene::Entity bareRoot = f.li.spawn(bare);
    check(f.li.packState(bareRoot) == "no pack" && f.li.find(bareRoot, "1") != scene::kInvalidEntity, "a packed instance with no pack realises unpacked");

    f.li.end();
    flush();
    check(f.sink.empty(), "end() clears the sink");
    check(bodyCount() == bodies0, "every body is gone");
    check(f.acquireTotal() == f.releaseTotal(), "every mesh was released");
    std::filesystem::remove_all(proj, ec);
#if AVER_MODULE_PHYSICS
    aver_phys_shutdown();
#endif
    if (g_failures) { AVER_ERROR("LevelInstancesPackedTest: {} failure(s)", g_failures); return 1; }
    AVER_INFO("LevelInstancesPackedTest: all passed");
    return 0;
}
