// world::bakeLevelPack: which placements pack, the rows, KEEP, determinism and staleness. No scene.
#include "aver/core/Log.hpp"
#include "aver/formats/OcInstances.hpp"
#include "aver/formats/OcLevelInst.hpp"
#include "aver/formats/OcPack.hpp"
#include "aver/formats/OcWorld.hpp"
#include "aver/platform/FileSystem.hpp"
#include "aver/world/LevelPack.hpp"

#include <cmath>
#include <filesystem>
#include <map>
#include <string>

using namespace aver;
using aver::world::LevelAssetLibrary;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }

struct Store {
    std::map<std::string, std::string> files;
    LevelAssetLibrary::Loader loader() {
        return [this](const std::string& path, fmt::OcWorldData& out, std::string* why) {
            const auto it = files.find(LevelAssetLibrary::normalise(path));
            if (it == files.end()) { if (why) *why = "missing " + path; return false; }
            return fmt::parseOcworld(it->second, out, why);
        };
    }
    void set(const std::string& path, const std::string& text) { files[LevelAssetLibrary::normalise(path)] = text; }
};

static const char* kLevel =
    "OCWORLD 1\n"
    "PLACE crate 100 0 0 0 0 0 1 M_Wood id=1\n"
    "PLACE crate 200 0 0 0 0 0 1 M_Wood id=2\n"
    "PLACE crate 300 0 0 0 0 0 1 M_Wood nocollide id=3\n"
    "PLACE rock 0 0 0 0 0 0 1 class Foo id=4\n"
    "PLACE car 0 0 0 0 0 0 1 vehicle car id=5\n"
    "PLACE fan 0 0 0 0 0 0 1 anim clip.ocanim id=6\n"
    "BEGIN\n"
    "  CHILD blade 5 0 0 0 0 0 1 id=7\n"
    "END\n"
    "PLACE ghost 0 0 0 0 0 0 1 hidden id=8\n"
    "PLACE hill 0 0 0 0 0 0 1 snap id=9\n"
    "PLACE glass 0 0 0 0 0 0 1 id=a\n"
    "PLACE tower 0 0 0 0 0 0 1 id=b\n"
    "BEGIN\n"
    "  CHILD floor 0 0 10 0 0 0 1 id=c\n"
    "  BEGIN\n"
    "    CHILD brick 3 0 0 0 0 0 1 id=d\n"
    "  END\n"
    "END\n"
    "LEVELINST Inner pos 1000 0 0 id=70\n"
    "ENDLEVELINST\n";

static const char* kInner =
    "OCWORLD 1\n"
    "PLACE stone 1 2 3 0 0 0 1 id=9\n"
    "PLACE glass 0 0 0 0 0 0 1 id=e\n";

static std::string readAll(const std::string& p) {
    std::string t;
    readFileText(p, t);
    return t;
}

int main() {
    const std::filesystem::path proj = std::filesystem::temp_directory_path() / "aver_levelpack_test";
    std::error_code ec;
    std::filesystem::remove_all(proj, ec);
    std::filesystem::create_directories(proj);

    Store st;
    st.set("Levels/Room.ocworld", kLevel);
    st.set("Inner", kInner);
    LevelAssetLibrary lib;
    lib.setLoader(st.loader());

    world::PackBakeOptions opt;
    opt.meshPackable = [](const std::string& asset, const std::string&, std::string* whyNot) {
        if (asset == "glass") { if (whyNot) *whyNot = "translucent"; return false; }
        return true;
    };
    world::PackBakeReport rep;
    std::string why;
    check(world::bakeLevelPack(lib, "Levels/Room.ocworld", "", proj.string(), opt, rep, &why), "bake succeeds: " + why);

    check(world::levelPackFlatName("Levels/Room.ocworld") == "Levels_Room" && world::levelPackFlatName("A\\B.x.ocworld") == "A_B.x",
          "the flat name drops the extension and maps both slashes");
    check(rep.rows == 7, "seven rows: crates x3, tower, floor, brick and the nested stone");
    check(rep.groups == 6, "six groups keyed by (asset, material, collide)");
    check(rep.kept == 8, "eight placements stay entities");
    check(rep.notPackable == 4, "hidden, snap, translucent (twice) are counted as not packable; class, vehicle, animated and its child are not");

    world::LoadedLevelPack lp;
    bool stale = true;
    check(world::loadLevelPack(proj.string(), "Levels/Room.ocworld", lib.closureHash("Levels/Room.ocworld"), lp, &stale, &why),
          "the pack reads back: " + why);
    check(!stale, "SOURCE equals the closure hash");
    check(lp.table.valid(), ".ocinst passes valid()");
    check(lp.table.transforms.size() == 7 * 12, "seven 12-float rows");

    // Groups: crate/M_Wood/collide (2 rows), crate/M_Wood/nocollide, then the rest.
    if (lp.table.groups.size() == 6 && lp.pack.groups.size() == 6) {
        check(lp.table.groups[0].asset == "crate" && lp.table.groups[0].count == 2 && lp.pack.groups[0].collide &&
                  lp.pack.groups[0].material == "M_Wood", "the first group is the colliding wooden crates");
        check(lp.table.groups[1].asset == "crate" && lp.table.groups[1].count == 1 && !lp.pack.groups[1].collide,
              "a nocollide crate is its own group");
        check(lp.table.groups[0].flags == fmt::kOcInstanceFlagCastShadow, "groups cast shadows");
        const auto rowOf = [&](const char* asset, f32 out[12]) {
            for (const auto& g : lp.table.groups)
                if (g.asset == asset && g.count) {
                    for (int i = 0; i < 12; ++i) out[i] = lp.table.transforms[g.first * 12 + i];
                    return true;
                }
            return false;
        };
        f32 r[12];
        check(rowOf("brick", r) && near(r[9], 3) && near(r[10], 0) && near(r[11], 10),
              "a two-deep parent chain composes: brick sits at (3, 0, 10)");
        check(rowOf("stone", r) && near(r[9], 1001) && near(r[10], 2) && near(r[11], 3),
              "a nested instance's rows go through its record transform");
        check(rowOf("tower", r) && near(r[0], 1) && near(r[4], 1) && near(r[8], 1), "an identity placement has the identity basis");
    }

    // KEEP: every placement not in the table, nested ones as path ids.
    check(lp.pack.keep.size() == 8, "KEEP lists the eight kept placements");
    for (const char* id : {"4", "5", "6", "7", "8", "9", "a", "70/e"})
        check(lp.pack.keeps(id), std::string("KEEP has ") + id);
    for (const char* id : {"1", "2", "3", "b", "c", "d", "70/9"})
        check(!lp.pack.keeps(id), std::string("KEEP does not have the packed ") + id);

    // Baking twice is byte-identical.
    const std::string t1 = readAll((proj / world::levelPackTablePath("Levels/Room.ocworld")).string());
    const std::string p1 = readAll((proj / world::levelPackSidecarPath("Levels/Room.ocworld")).string());
    world::PackBakeReport rep2;
    check(world::bakeLevelPack(lib, "Levels/Room.ocworld", "", proj.string(), opt, rep2, &why), "second bake succeeds");
    check(readAll((proj / world::levelPackTablePath("Levels/Room.ocworld")).string()) == t1 &&
              readAll((proj / world::levelPackSidecarPath("Levels/Room.ocworld")).string()) == p1,
          "baking twice is byte-identical");

    // Staleness after a child edit.
    st.set("Inner", std::string(kInner) + "PLACE extra 5 5 5 0 0 0 1 id=f\n");
    lib.invalidate("Inner");
    check(world::loadLevelPack(proj.string(), "Levels/Room.ocworld", lib.closureHash("Levels/Room.ocworld"), lp, &stale, &why) && stale,
          "a nested child edit makes the pack stale");
    st.set("Inner", kInner);
    lib.invalidate("Inner");

    // The row cap.
    world::PackBakeOptions capped = opt;
    capped.maxRows = 2;
    world::PackBakeReport rep3;
    check(world::bakeLevelPack(lib, "Levels/Room.ocworld", "", proj.string(), capped, rep3, &why), "capped bake succeeds");
    check(rep3.rows == 2 && rep3.capped, "maxRows caps the table and says so");
    check(rep3.kept == 8 + 5, "everything past the cap stays an entity");

    // A kept child keeps its ancestors live; the packed sibling beside it still packs.
    st.set("Levels/Anc.ocworld",
           "OCWORLD 1\n"
           "PLACE tower 0 0 0 0 0 0 1 id=1\n"
           "BEGIN\n"
           "  CHILD glass 0 0 5 0 0 0 1 id=2\n"
           "  CHILD brick 1 0 0 0 0 0 1 id=3\n"
           "END\n");
    world::PackBakeReport rep5;
    check(world::bakeLevelPack(lib, "Levels/Anc.ocworld", "", proj.string(), opt, rep5, &why), "ancestor bake succeeds: " + why);
    world::LoadedLevelPack lpa;
    check(world::loadLevelPack(proj.string(), "Levels/Anc.ocworld", lib.closureHash("Levels/Anc.ocworld"), lpa, &stale, &why),
          "the ancestor pack reads back");
    check(rep5.rows == 1 && lpa.pack.keeps("1") && lpa.pack.keeps("2") && !lpa.pack.keeps("3"),
          "the parent of a kept child is kept; the packed sibling stays a row");

    // A cycle refuses to bake.
    st.set("CycA", "OCWORLD 1\nLEVELINST CycB pos 0 0 0 id=1\nENDLEVELINST\n");
    st.set("CycB", "OCWORLD 1\nLEVELINST CycA pos 0 0 0 id=2\nENDLEVELINST\n");
    world::PackBakeReport rep4;
    check(!world::bakeLevelPack(lib, "CycA", "", proj.string(), opt, rep4, &why) && why.find("CycA -> CycB -> CycA") != std::string::npos,
          "a cyclic level refuses to bake and names the chain");

    std::filesystem::remove_all(proj, ec);
    if (g_failures) { AVER_ERROR("LevelPackTest: {} failure(s)", g_failures); return 1; }
    AVER_INFO("LevelPackTest: all passed");
    return 0;
}
