// world::LevelAssetLibrary: parse cache, closure and cycle checks, ids, override resolution, bounds. No scene.
#include "aver/core/Log.hpp"
#include "aver/formats/OcLevelInst.hpp"
#include "aver/formats/OcWorld.hpp"
#include "aver/platform/FileSystem.hpp"
#include "aver/world/LevelAssetLibrary.hpp"

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
    std::map<std::string, std::string> files;   // normalised key -> .ocworld text
    int loads = 0;
    LevelAssetLibrary::Loader loader() {
        return [this](const std::string& path, fmt::OcWorldData& out, std::string* why) {
            ++loads;
            const auto it = files.find(LevelAssetLibrary::normalise(path));
            if (it == files.end()) { if (why) *why = "missing " + path; return false; }
            return fmt::parseOcworld(it->second, out, why);
        };
    }
    void set(const std::string& path, const std::string& text) { files[LevelAssetLibrary::normalise(path)] = text; }
};

static std::string inst(const char* asset, const char* tokens = "") {
    return std::string("LEVELINST ") + asset + " pos 0 0 0 " + tokens + "\nENDLEVELINST\n";
}

static void testParseOnce() {
    Store st;
    st.set("Levels/Room.ocworld", "OCWORLD 1\nPLACE Meshes/a.ocmesh 1 2 3 0 0 0 1 id=1\n");
    st.set("Levels/UserA.ocworld", "OCWORLD 1\n" + inst("Levels/Room.ocworld", "id=11"));
    st.set("Levels/UserB.ocworld", "OCWORLD 1\n" + inst("levels\\room.ocworld", "id=12"));
    LevelAssetLibrary lib;
    lib.setLoader(st.loader());
    const auto* a = lib.get("Levels/Room.ocworld");
    const auto* b = lib.get("levels\\ROOM.ocworld");
    check(a && a == b, "case and backslash variants of one path are one key");
    check(st.loads == 1, "parsed once for two users");
    lib.get("Levels/UserA.ocworld");
    lib.get("Levels/UserB.ocworld");
    check(lib.usersOf("Levels/Room.ocworld").size() == 2, "usersOf lists both cached users");
    check(lib.get("Levels/Nope.ocworld") == nullptr, "a missing level is a null, not a crash");
    lib.invalidate("Levels/Room.ocworld");
    const int before = st.loads;
    const auto* c = lib.get("Levels/Room.ocworld");
    check(c == a && st.loads == before + 1, "invalidate re-parses into the same object");
}

static void testClosure() {
    Store st;
    st.set("A", "OCWORLD 1\n" + inst("B"));
    st.set("B", "OCWORLD 1\n" + inst("A"));
    st.set("Self", "OCWORLD 1\n" + inst("Self"));
    st.set("Leaf", "OCWORLD 1\nPLACE m 0 0 0 0 0 0 1\n");
    st.set("Top", "OCWORLD 1\n" + inst("Leaf") + inst("Leaf"));
    LevelAssetLibrary lib;
    lib.setLoader(st.loader());
    std::vector<std::string> order;
    std::string chain;
    check(!lib.closure("A", order, &chain) && chain == "A -> B -> A", "A -> B -> A is found: " + chain);
    check(!lib.closure("Self", order, &chain) && chain == "Self -> Self", "A -> A is found: " + chain);
    check(lib.closure("Top", order, &chain) && order.size() == 2 && order[0] == "Top" && order[1] == "Leaf",
          "a clean closure lists each asset once, root first");

    // A straight chain: depth 8 is accepted, depth 9 is refused.
    for (int i = 0; i < 12; ++i)
        st.set("C" + std::to_string(i), "OCWORLD 1\n" + inst(("C" + std::to_string(i + 1)).c_str()));
    st.set("C12", "OCWORLD 1\nPLACE m 0 0 0 0 0 0 1\n");
    LevelAssetLibrary chainLib;
    chainLib.setLoader(st.loader());
    // C4 reaches C12: eight nested hops.
    check(chainLib.closure("C4", order, &chain), "a chain of depth 8 is accepted");
    check(!chainLib.closure("C3", order, &chain) && chain == "depth > 8", "a chain of depth 9 is refused: " + chain);

    check(lib.wouldCycle("A", "B"), "wouldCycle(A, B) when B contains A");
    check(lib.wouldCycle("A", "A"), "wouldCycle(A, A)");
    check(!lib.wouldCycle("Top", "Leaf"), "wouldCycle(Top, Leaf) is false");
    check(lib.wouldCycle("Leaf", "Top"), "wouldCycle(Leaf, Top) is true: Top already contains Leaf");
}

static void testHash() {
    Store st;
    st.set("Top", "OCWORLD 1\nSUN dir 0 0 1\n" + inst("Mid"));
    st.set("Mid", "OCWORLD 1\nSUN dir 0 0 1\nPLACE m 0 0 0 0 0 0 1 id=5\n");
    LevelAssetLibrary lib;
    lib.setLoader(st.loader());
    const u64 h0 = lib.closureHash("Top");
    check(h0 == lib.closureHash("Top"), "closureHash is stable");

    st.set("Mid", "OCWORLD 1\nSUN dir 1 0 0\nPLACE m 0 0 0 0 0 0 1 id=5\n");
    lib.invalidate("Mid");
    check(lib.closureHash("Top") == h0, "an ignored record (SUN) does not change the hash");

    st.set("Mid", "OCWORLD 1\nPLACE m 1 0 0 0 0 0 1 id=5\n");
    lib.invalidate("Mid");
    check(lib.closureHash("Top") != h0, "a nested asset's placement change does");
}

static void testEnsureIds() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "aver_levelinst_ids_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "Child.ocworld").string();
    writeFileTextAtomic(file, "OCWORLD 1\nPLACE m 0 0 0 0 0 0 1\nPLACE n 1 0 0 0 0 0 1\n" + inst("Other"));

    LevelAssetLibrary lib;
    lib.setLoader(LevelAssetLibrary::diskLoader(dir.string()));
    const auto* before = lib.get("Child.ocworld");
    check(before && !before->idsFromFile, "ids derived in memory when the file has none");
    check(before && before->data.placements[0].placementId != 0 && before->data.placements[0].placementId !=
              before->data.placements[1].placementId, "derived ids are unique and non-zero");

    std::string why;
    check(lib.ensureIds("Child.ocworld", file, &why), "ensureIds succeeds: " + why);
    fmt::OcWorldData raw;
    check(fmt::loadOcworld(file, raw) && raw.placements.size() == 2 && raw.placements[0].placementId != 0 &&
              raw.placements[1].placementId != 0 && raw.placements[0].placementId != raw.placements[1].placementId &&
              raw.levelInstances.size() == 1 && raw.levelInstances[0].id != 0,
          "the file now carries unique non-zero ids on placements and the nested instance");
    std::string textBefore;
    readFileText(file, textBefore);
    check(lib.ensureIds("Child.ocworld", file, &why), "ensureIds is idempotent");
    std::string textAfter;
    readFileText(file, textAfter);
    check(textBefore == textAfter, "a second call rewrites nothing");
    const auto* after = lib.get("Child.ocworld");
    check(after && after->idsFromFile, "the reloaded asset reports file ids");

    bool guardCalled = false;
    writeFileTextAtomic(file, "OCWORLD 1\nPLACE m 0 0 0 0 0 0 1\n");
    lib.invalidate("Child.ocworld");
    lib.setWriteGuard([&](const std::string&, std::string* w) { guardCalled = true; if (w) *w = "save it first"; return false; });
    check(!lib.ensureIds("Child.ocworld", file, &why) && guardCalled && why == "save it first",
          "a write guard can refuse the rewrite");
    std::filesystem::remove_all(dir, ec);
}

static fmt::OcPrefabOverride ov(const std::string& path, const char* field, fmt::OcSaveField v) {
    fmt::OcPrefabOverride o;
    o.op = fmt::OcOverrideOp::Set; o.path = path; o.component = fmt::kOcLevelInstPlacementComponent;
    v.name = field;
    o.value = std::move(v);
    return o;
}
static fmt::OcSaveField fv3(f32 x, f32 y, f32 z) { fmt::OcSaveField f; f.kind = fmt::kOcPrefabKindVec3; f.f = {x, y, z}; return f; }
static fmt::OcSaveField fs(const char* s) { fmt::OcSaveField f; f.kind = fmt::kOcPrefabKindString; f.s = s; return f; }
static fmt::OcSaveField fb(bool b) { fmt::OcSaveField f; f.kind = fmt::kOcPrefabKindBool; f.i = b; return f; }

static void testResolve() {
    Store st;
    st.set("Child",
           "OCWORLD 1\n"
           "PLACE a 0 0 0 0 0 0 1 M_Old id=a\n"
           "BEGIN\n"
           "  CHILD b 10 0 0 0 0 0 1 id=b\n"
           "  BEGIN\n"
           "    CHILD c 5 0 0 0 0 0 1 id=c\n"
           "  END\n"
           "END\n"
           "PLACE d 100 0 0 0 0 0 1 id=d\n"
           "PLACE e 200 0 0 0 0 0 1 id=e\n" + inst("Inner", "id=70"));
    st.set("Inner", "OCWORLD 1\nPLACE z 1 2 3 0 0 0 1 id=9\n");
    LevelAssetLibrary lib;
    lib.setLoader(st.loader());

    fmt::OcLevelInstance rec;
    rec.asset = "Child";
    rec.overrides.push_back(ov("a", fmt::kLiFieldMaterial, fs("M_Red")));
    rec.overrides.push_back(ov("b", fmt::kLiFieldRemoved, fb(true)));
    rec.overrides.push_back(ov("d", fmt::kLiFieldPosition, fv3(0, 0, 40)));
    rec.overrides.push_back(ov("e", fmt::kLiFieldVisible, fb(false)));
    rec.overrides.push_back(ov("e", fmt::kLiFieldCollide, fb(false)));
    rec.overrides.push_back(ov("e", fmt::kLiFieldName, fs("Renamed")));
    rec.overrides.push_back(ov("70/9", fmt::kLiFieldPosition, fv3(7, 7, 7)));
    rec.overrides.push_back(ov("ffff", fmt::kLiFieldMaterial, fs("X")));

    world::ResolvedContents rc;
    std::string why;
    check(lib.resolve(rec, rc, &why), "resolve succeeds: " + why);
    check(rc.world.placements.size() == 3, "a removed placement takes its children with it (a, d, e remain)");
    check(rc.placementIds.size() == 3 && rc.placementIds[0] == 0xa && rc.placementIds[1] == 0xd && rc.placementIds[2] == 0xe,
          "placementIds are parallel to the placements");
    if (rc.world.placements.size() == 3) {
        check(rc.world.placements[0].material == "M_Red", "material override");
        check(rc.world.placements[1].z == 40 && rc.world.placements[1].x == 0, "position override replaces, not adds");
        check(!rc.world.placements[2].visible && !rc.world.placements[2].collide && rc.world.placements[2].name == "Renamed",
              "visible, collide and name overrides");
        check(rc.world.placements[0].parent == -1 && rc.world.placements[1].parent == -1, "parents renumbered");
    }
    check(rc.unmatched == 1, "an override matching nothing is counted");
    check(rc.world.levelInstances.size() == 1 && rc.world.levelInstances[0].overrides.size() == 1 &&
              rc.world.levelInstances[0].overrides[0].path == "0000000000000009",
          "a path with a slash lands on the nested record with the head stripped");

    // Removal of a middle node renumbers the surviving child's parent.
    st.set("Tree", "OCWORLD 1\nPLACE r 0 0 0 0 0 0 1 id=1\nBEGIN\n CHILD x 0 0 0 0 0 0 1 id=2\n CHILD y 0 0 0 0 0 0 1 id=3\nEND\n");
    fmt::OcLevelInstance rec2;
    rec2.asset = "Tree";
    rec2.overrides.push_back(ov("2", fmt::kLiFieldRemoved, fb(true)));
    world::ResolvedContents rc2;
    check(lib.resolve(rec2, rc2) && rc2.world.placements.size() == 2 && rc2.world.placements[1].parent == 0 &&
              rc2.world.placements[1].asset == "y", "a surviving child keeps a valid parent index after a sibling is removed");
}

static void testBounds() {
    Store st;
    st.set("Box", "OCWORLD 1\nEXTENT -100 -100 0 100 100 200\n");
    st.set("NoExtent", "OCWORLD 1\nPLACE m 0 0 0 0 0 0 1\n");
    st.set("Wrapper", "OCWORLD 1\nLEVELINST Box pos 1000 0 0 rot 0 0 0 scale 1 1 1 id=1\nENDLEVELINST\n");
    LevelAssetLibrary lib;
    lib.setLoader(st.loader());

    fmt::OcLevelInstance rec;
    rec.asset = "Box";
    rec.x = 1000;
    rec.yaw = 90;
    rec.sx = rec.sy = rec.sz = 2;
    AABB b;
    check(lib.instanceBounds(rec, b), "bounds known from EXTENT");
    // Yaw 90 maps the box's X extent onto world Y (both +-100 here), so the XY half-size is 200 either way; Z is 0..400.
    check(near(b.min.x, 800) && near(b.max.x, 1200) && near(b.min.y, -200) && near(b.max.y, 200) &&
              near(b.min.z, 0) && near(b.max.z, 400), "the AABB equals the hand-computed one");

    fmt::OcLevelInstance rec2;
    rec2.asset = "NoExtent";
    check(!lib.instanceBounds(rec2, b), "unknown extent returns false");

    fmt::OcLevelInstance rec3;
    rec3.asset = "Wrapper";
    check(lib.instanceBounds(rec3, b) && near(b.min.x, 900) && near(b.max.x, 1100),
          "a child with no EXTENT of its own takes the union of its nested instances");
}

int main() {
    testParseOnce();
    testClosure();
    testHash();
    testEnsureIds();
    testResolve();
    testBounds();
    if (g_failures) { AVER_ERROR("LevelAssetLibraryTest: {} failure(s)", g_failures); return 1; }
    AVER_INFO("LevelAssetLibraryTest: all passed");
    return 0;
}
