// .ocpack (the level-pack sidecar) and the instance-row matrix helper.
#include "aver/core/Log.hpp"
#include "aver/core/Math.hpp"
#include "aver/formats/OcPack.hpp"

#include <algorithm>
#include <cmath>
#include <string>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static fmt::OcPack sample() {
    fmt::OcPack p;
    p.source = 0x3f9a1c0e2b7d4455ull;
    p.table = "Binaries/LevelPacks/Levels_Room.ocinst";
    p.groups.push_back({true, "M_Wood"});
    p.groups.push_back({false, ""});
    p.keep.push_back("00000000000000c3");
    p.keep.push_back("00000000000000b2/00000000000000c9");
    return p;
}

static void testRoundTrip() {
    const fmt::OcPack p = sample();
    const std::string text = fmt::writeOcPack(p);
    fmt::OcPack r;
    std::string why;
    check(fmt::parseOcPack(text, r, &why), "a pack parses back: " + why);
    check(r.source == p.source && r.table == p.table, "source and table survive");
    check(r.groups.size() == 2 && r.groups[0].collide && r.groups[0].material == "M_Wood" &&
          !r.groups[1].collide && r.groups[1].material.empty(), "groups keep collide and material ('-' = the mesh's own)");
    check(r.keep == p.keep, "KEEP path ids survive, nested ones included");
    check(r.keeps("C3") && r.keeps("b2/c9") && !r.keeps("c4") && !r.keeps("zz"), "keeps() accepts any leading-zero spelling");
    check(fmt::writeOcPack(r) == text, "write(parse(write(x))) is stable");
}

static void testRefusals() {
    fmt::OcPack r;
    std::string why;
    check(!fmt::parseOcPack("SOURCE 1\nTABLE a.ocinst\n", r, &why), "no header is refused");
    check(!fmt::parseOcPack("OCPACK 1\nTABLE a.ocinst\n", r, &why), "no SOURCE is refused");
    check(!fmt::parseOcPack("OCPACK 1\nSOURCE 1\n", r, &why), "no TABLE is refused");
    check(!fmt::parseOcPack("OCPACK 1\nSOURCE 1\nTABLE a.ocinst\nGROUP 1 collide 0 material -\n", r, &why),
          "a GROUP index out of order is refused");
    check(fmt::parseOcPack("OCPACK 1\nSOURCE 1\nTABLE a.ocinst\nFUTURE x\nKEEP nothex\n", r, &why) && r.keep.empty(),
          "unknown records and bad KEEP ids are skipped");

    fmt::OcInstanceData table;
    table.groups.resize(2);
    fmt::OcPack p = sample();
    check(fmt::ocPackMatchesTable(p, table), "a pack whose group count equals its table's matches");
    table.groups.resize(3);
    check(!fmt::ocPackMatchesTable(p, table, &why), "a group-count mismatch is refused");
}

static void testRows() {
    // One row from a TRS, then the same row through an instance matrix, versus composing the matrices.
    Transform child;
    child.position = {100, 20, 5};
    child.rotation = Quat::fromAxisAngle({0, 0, 1}, 0.5f);
    child.scale = {1, 1.5f, 1};
    Transform inst;
    inst.position = {1000, 0, 0};
    inst.rotation = Quat::fromAxisAngle({0, 0, 1}, 1.5707963f);
    inst.scale = {2, 2, 2};

    const Mat4 cm = child.toMatrix();
    f32 row[12];
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c) row[r * 3 + c] = cm.m[r][c];
    f32 out[12];
    fmt::transformOcInstanceRows(row, 1, inst.toMatrix(), out);
    const Mat4 want = cm * inst.toMatrix();
    bool ok = true;
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c)
        if (std::fabs(out[r * 3 + c] - want.m[r][c]) > 1e-4f * std::max(1.0f, std::fabs(want.m[r][c]))) ok = false;
    check(ok, "rows * instance matrix equals child * instance (yaw 90, scale 2, translation)");

    f32 id[12];
    fmt::transformOcInstanceRows(row, 1, Mat4::identity(), id);
    bool same = true;
    for (int i = 0; i < 12; ++i) if (id[i] != row[i]) same = false;
    check(same, "the identity matrix is the identity");

    f32 two[24];
    for (int i = 0; i < 12; ++i) { two[i] = row[i]; two[12 + i] = row[i]; }
    fmt::transformOcInstanceRows(two, 2, inst.toMatrix(), two);   // in place
    bool both = true;
    for (int i = 0; i < 12; ++i) if (std::fabs(two[i] - out[i]) > 1e-4f || std::fabs(two[12 + i] - out[i]) > 1e-4f) both = false;
    check(both, "in-place transform of several rows");
}

int main() {
    testRoundTrip();
    testRefusals();
    testRows();
    if (g_failures) { AVER_ERROR("OcPackTest: {} failure(s)", g_failures); return 1; }
    AVER_INFO("OcPackTest: all passed");
    return 0;
}
