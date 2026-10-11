// LEVELINST / LIOVERRIDE / EXTENT records of .ocworld: round trips, ids, unknown-record behaviour, and that a
// level with no instances and no extent writes exactly what it wrote before.
#include "aver/core/Log.hpp"
#include "aver/formats/OcLevelInst.hpp"
#include "aver/formats/OcWorld.hpp"

#include <string>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static fmt::OcPrefabOverride ovVec3(const char* path, const char* field, f32 x, f32 y, f32 z) {
    fmt::OcPrefabOverride o;
    o.op = fmt::OcOverrideOp::Set; o.path = path; o.component = fmt::kOcLevelInstPlacementComponent;
    o.value.name = field; o.value.kind = fmt::kOcPrefabKindVec3; o.value.f = {x, y, z};
    return o;
}
static fmt::OcPrefabOverride ovStr(const char* path, const char* field, const char* s) {
    fmt::OcPrefabOverride o;
    o.op = fmt::OcOverrideOp::Set; o.path = path; o.component = fmt::kOcLevelInstPlacementComponent;
    o.value.name = field; o.value.kind = fmt::kOcPrefabKindString; o.value.s = s;
    return o;
}
static fmt::OcPrefabOverride ovBool(const char* path, const char* field, bool b) {
    fmt::OcPrefabOverride o;
    o.op = fmt::OcOverrideOp::Set; o.path = path; o.component = fmt::kOcLevelInstPlacementComponent;
    o.value.name = field; o.value.kind = fmt::kOcPrefabKindBool; o.value.i = b ? 1 : 0;
    return o;
}

static fmt::OcLevelInstance sampleInstance() {
    fmt::OcLevelInstance in;
    in.asset = "Levels/Room # A.ocworld";
    in.name = "Room A;1";
    in.x = 1200; in.y = -0.125; in.z = 3.0000001;
    in.yaw = 90; in.pitch = 0; in.roll = 1.5;
    in.sx = 1; in.sy = 2; in.sz = 1;
    in.id = 0xa1;
    in.packed = true;
    in.stream = 1;
    in.overrides.push_back(ovStr("00000000000000c3", fmt::kLiFieldMaterial, "M_Red Dark"));
    in.overrides.push_back(ovBool("00000000000000c4", fmt::kLiFieldRemoved, true));
    in.overrides.push_back(ovVec3("00000000000000b2/00000000000000c9", fmt::kLiFieldPosition, 0, 0, 40));
    return in;
}

static bool sameInstance(const fmt::OcLevelInstance& a, const fmt::OcLevelInstance& b) {
    if (a.asset != b.asset || a.name != b.name || a.x != b.x || a.y != b.y || a.z != b.z || a.yaw != b.yaw ||
        a.pitch != b.pitch || a.roll != b.roll || a.sx != b.sx || a.sy != b.sy || a.sz != b.sz || a.id != b.id ||
        a.packed != b.packed || a.stream != b.stream || a.overrides.size() != b.overrides.size())
        return false;
    for (size_t i = 0; i < a.overrides.size(); ++i) {
        const auto& x = a.overrides[i]; const auto& y = b.overrides[i];
        if (x.op != y.op || x.path != y.path || x.component != y.component || x.value.name != y.value.name ||
            !fmt::ocPrefabFieldEqual(x.value, y.value))
            return false;
    }
    return true;
}

static const char* kBase =
    "OCWORLD 1\n"
    "NAME T\n"
    "PLACE Meshes/a.ocmesh 1 2 3 0 0 0 1\n"
    "PLACE Meshes/b.ocmesh 4 5 6 0 0 0 2\n";

static void testRoundTrip() {
    fmt::OcWorldData w;
    std::string err;
    check(fmt::parseOcworld(kBase, w, &err), "base level parses");
    w.levelInstances.push_back(sampleInstance());
    fmt::OcLevelInstance second;
    second.asset = "Levels/B.ocworld";
    second.id = 0x1;
    w.levelInstances.push_back(second);
    w.hasExtent = true;
    w.extentMin[0] = -500; w.extentMin[1] = -500; w.extentMin[2] = 0;
    w.extentMax[0] = 500;  w.extentMax[1] = 500;  w.extentMax[2] = 400.5;

    const std::string text = fmt::writeOcworld(w);
    fmt::OcWorldData r;
    check(fmt::parseOcworld(text, r, &err), "level with instances parses back");
    check(r.levelInstances.size() == 2, "both instances survive");
    check(r.levelInstances.size() == 2 && sameInstance(r.levelInstances[0], w.levelInstances[0]),
          "every token and the three override kinds round trip, nested path included");
    check(r.levelInstances.size() == 2 && sameInstance(r.levelInstances[1], w.levelInstances[1]),
          "a bare instance round trips with default scale and unset stream");
    check(r.levelInstances.size() == 2 && r.levelInstances[1].stream == -1 && !r.levelInstances[1].packed,
          "stream is -1 and packed false when absent");
    check(r.hasExtent && r.extentMin[0] == -500 && r.extentMax[2] == 400.5, "EXTENT round trips");
    check(r.placements.size() == 2, "placements are untouched");
    check(fmt::writeOcworld(r) == text, "write(parse(write(x))) is stable");
    check(text.find("id=00000000000000a1") != std::string::npos, "id is always written as 16 digits");
    check(text.find("name Room%20A%3B1") != std::string::npos, "name is percent-encoded");
    check(text.find("Levels/Room%20%23%20A.ocworld") != std::string::npos, "asset is percent-encoded");
}

static void testIds() {
    fmt::OcWorldData r;
    const std::string text =
        "OCWORLD 1\n"
        "LEVELINST Levels/A.ocworld pos 0 0 0 id=a1\n"
        "ENDLEVELINST\n"
        "LEVELINST Levels/B.ocworld pos 0 0 0 id=000000000000ABCD\n"
        "ENDLEVELINST\n";
    check(fmt::parseOcworld(text, r), "ids spelled with and without leading zeros parse");
    check(r.levelInstances.size() == 2 && r.levelInstances[0].id == 0xa1 && r.levelInstances[1].id == 0xabcd,
          "leading zeros optional, case-insensitive");
    check(fmt::ocLevelInstIdText(0xa1) == "00000000000000a1", "ocLevelInstIdText is 16 lowercase digits");
    check(fmt::ocLevelInstIdParse("zz") == 0 && fmt::ocLevelInstIdParse("") == 0, "a bad id token is 0");
    std::vector<u64> ids;
    check(fmt::ocLevelInstSplitIds("b2/c9", ids) && ids.size() == 2 && ids[0] == 0xb2 && ids[1] == 0xc9,
          "a nested path splits into ids");
    check(fmt::ocLevelInstJoinIds(ids) == "00000000000000b2/00000000000000c9", "ids join back at 16 digits");
    check(!fmt::ocLevelInstSplitIds("b2//c9", ids), "an empty segment is refused");
}

static void testUntouchedLevel() {
    fmt::OcWorldData a, b;
    check(fmt::parseOcworld(kBase, a), "plain level parses");
    const std::string once = fmt::writeOcworld(a);
    check(once.find("LEVELINST") == std::string::npos && once.find("EXTENT") == std::string::npos &&
          once.find("LIOVERRIDE") == std::string::npos,
          "a level with no instances and no extent writes none of the new records");
    check(fmt::parseOcworld(once, b) && fmt::writeOcworld(b) == once, "plain level is byte-stable");
    check(!a.hasExtent && a.levelInstances.empty(), "defaults are empty");
}

static void testStrayAndUnknown() {
    fmt::OcWorldData r;
    const std::string text =
        "OCWORLD 1\n"
        "LIOVERRIDE set 00000000000000c3 @Placement material string ~M_Red\n"
        "ENDLEVELINST\n"
        "LEVELINST Levels/A.ocworld pos 1 2 3 futuretoken 7 id=5\n"
        "  LIOVERRIDE addcomp 00000000000000c3 CTags\n"
        "  LIOVERRIDE rmcomp 00000000000000c3 CTags\n"
        "  LIOVERRIDE set 00000000000000c3 @Placement visible bool 0\n"
        "ENDLEVELINST\n"
        "LIOVERRIDE set 00000000000000c5 @Placement visible bool 0\n"
        "PLACE Meshes/a.ocmesh 0 0 0 0 0 0 1\n";
    check(fmt::parseOcworld(text, r), "stray records do not fail the parse");
    check(r.levelInstances.size() == 1, "one instance");
    if (r.levelInstances.size() == 1) {
        const auto& in = r.levelInstances[0];
        check(in.x == 1 && in.y == 2 && in.z == 3 && in.id == 5, "tokens around an unknown token are read");
        check(in.overrides.size() == 1 && in.overrides[0].value.name == "visible",
              "addcomp and rmcomp are parsed and ignored; the set stays");
    }
    check(r.placements.size() == 1, "the placement after the block is read");
}

static void testCoexistWithPrefabs() {
    fmt::OcWorldData w;
    fmt::parseOcworld(kBase, w);
    fmt::OcPrefabInstance pi; pi.prefab = "Prefabs/Crate.ocprefab"; pi.x = 10;
    w.prefabInstances.push_back(pi);
    w.levelInstances.push_back(sampleInstance());
    fmt::OcWorldData r;
    check(fmt::parseOcworld(fmt::writeOcworld(w), r), "prefab + level instance + placements parse");
    check(r.prefabInstances.size() == 1 && r.prefabInstances[0].prefab == "Prefabs/Crate.ocprefab" &&
          r.prefabInstances[0].x == 10, "the prefab list is intact");
    check(r.levelInstances.size() == 1 && sameInstance(r.levelInstances[0], w.levelInstances[0]),
          "the level instance list is intact");
    check(r.placements.size() == 2, "the placements are intact");
}

// An older build has no LEVELINST in its record chain, so each line is skipped as an unknown record. Model that by
// stripping the new records from the text and checking everything else reads exactly as before.
static void testOlderReaderModel() {
    fmt::OcWorldData w;
    fmt::parseOcworld(kBase, w);
    w.levelInstances.push_back(sampleInstance());
    w.hasExtent = true;
    const std::string text = fmt::writeOcworld(w);
    std::string stripped;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const bool isNew = line.find("LEVELINST") != std::string::npos || line.find("LIOVERRIDE") != std::string::npos ||
                           line.find("EXTENT") != std::string::npos;
        if (!isNew) { stripped += line; stripped += '\n'; }
    }
    fmt::OcWorldData r;
    check(fmt::parseOcworld(stripped, r), "the level reads with the new records removed");
    check(r.placements.size() == 2 && r.placements[0].x == 1 && r.placements[1].sx == 2,
          "placements are untouched (an older build loses the instance, never misplaces anything)");
}

int main() {
    testRoundTrip();
    testIds();
    testUntouchedLevel();
    testStrayAndUnknown();
    testCoexistWithPrefabs();
    testOlderReaderModel();
    if (g_failures) { AVER_ERROR("OcLevelInstTest: {} failure(s)", g_failures); return 1; }
    AVER_INFO("OcLevelInstTest: all passed");
    return 0;
}
