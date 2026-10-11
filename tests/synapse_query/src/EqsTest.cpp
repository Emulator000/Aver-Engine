// The EQS evaluator (EqsRun) against a fake EqsEnv: generator counts, filters, the score formula against
// hand-computed numbers, tie order, cost ordering (a counting fake proves expensive tests only see
// survivors), RESULT modes, RUN firstvalid / all, failure cases and time slicing. Pure: no world.
#include "aver/synapse/Eqs.hpp"

#include "aver/core/Log.hpp"
#include "aver/synapse/Nav.hpp"

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::synapse;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool near(f32 a, f32 b, f32 eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// ---- the fake world ---------------------------------------------------------------------------------

struct Fake {
    std::map<std::string, EqsContext> ctx;
    fmt::OcNavData nav;
    bool useNav = false;
    int traceCalls = 0, customCalls = 0;
    bool blockAll = false;
    bool customKnown = true;
    std::vector<std::pair<u32, Vec3>> actors;
    std::map<u32, u32> tagBits;
};
static Fake F;

static void resetFake() {
    F = Fake{};
    F.ctx["querier"] = EqsContext{Vec3{0, 0, 0}, 1, true};
    F.ctx["target"] = EqsContext{Vec3{500, 0, 0}, 2, true};
}

static EqsContext fCtx(void*, std::string_view name, const EqsRunParams&) {
    const auto it = F.ctx.find(std::string(name));
    return it == F.ctx.end() ? EqsContext{} : it->second;
}
static bool fForward(void*, u32, Vec3& out) { out = Vec3{1, 0, 0}; return true; }
static u32 fActors(void*, const EqsGenerator&, const Vec3&, u32, u32* out, u32 cap) {
    u32 n = 0;
    for (const auto& a : F.actors)
        if (n < cap) out[n++] = a.first;
    return n;
}
static bool fEntityPos(void*, u32 e, Vec3& out) {
    for (const auto& a : F.actors)
        if (a.first == e) { out = a.second; return true; }
    return false;
}
static u32 fTagBits(void*, u32 e) { const auto it = F.tagBits.find(e); return it == F.tagBits.end() ? 0 : it->second; }
static bool fTrace(void*, const Vec3&, const Vec3&, f32, u32, u32) { ++F.traceCalls; return F.blockAll; }
static bool fGround(void*, const Vec3& p, f32, f32, f32& z) {
    if (p.x >= 150.0f) return false;   // a hole
    z = 10.0f;
    return true;
}
static bool fCustom(void*, const EqsTest&, const EqsRunParams&, const EqsItem& item, const EqsContext&, f32& value) {
    ++F.customCalls;
    if (!F.customKnown) return false;
    value = item.pos.x * 2.0f;
    return true;
}

static EqsEnv makeEnv() {
    EqsEnv env;
    env.nav = F.useNav ? &F.nav : nullptr;
    env.context = &fCtx;
    env.forward = &fForward;
    env.actors = &fActors;
    env.entityPos = &fEntityPos;
    env.entityTagBits = &fTagBits;
    env.trace = &fTrace;
    env.ground = &fGround;
    env.custom = &fCustom;
    return env;
}

static EqsQuery parse(const std::string& text) {
    EqsQuery q;
    std::string why;
    if (!parseEqsQuery(text, "t.oceqs", q, &why)) {
        ++g_failures;
        AVER_ERROR("  FAIL  test query did not parse: {}", why);
    }
    return q;
}

static EqsResult run(const std::string& text, u32 runId = 1) {
    const EqsQuery q = parse(text);
    EqsRunParams p;
    p.querier = 1;
    p.target = 2;
    p.runId = runId;
    EqsRun r(q, p, makeEnv());
    r.step(0);
    return r.result();
}

// Four points on the X axis at 100, 200, 300, 400 from the querier.
static const std::string kPoints4 = "OPTION\n  GENERATOR points list=\"100,0,0;200,0,0;300,0,0;400,0,0\"\n";

// A 10x10 grid, 50 cm cells, a wall down column 5 that splits two regions.
static void buildNav() {
    F.nav = fmt::OcNavData{};
    F.nav.cellSizeCm = 50.0f;
    F.nav.widthCells = F.nav.heightCells = 10;
    F.nav.cells.resize(100);
    for (u32 y = 0; y < 10; ++y)
        for (u32 x = 0; x < 10; ++x)
            F.nav.cells[y * 10 + x].flags = (x == 5) ? 0 : fmt::kOcNavWalkable;
    buildRegions(F.nav);
    F.useNav = true;
}

// ---- generators --------------------------------------------------------------------------------------

static void generators() {
    resetFake();
    check(run("OCEQS 1\nOPTION\n  GENERATOR grid halfx=200 halfy=200 step=100\n").generated == 25, "grid halfx=200 step=100 makes 25 points");
    EqsResult r = run("OCEQS 1\nOPTION\n  GENERATOR ring inner=100 outer=500 rings=3 count=8\n");
    check(r.generated == 24 && r.items.size() == 24, "ring 3 x 8 makes 24 points");
    // Ring radii: 100, 300, 500 about the querier; angle 0 along +X.
    bool radii = true;
    for (const EqsItem& it : r.items) {
        const f32 d = std::sqrt(it.pos.x * it.pos.x + it.pos.y * it.pos.y);
        radii = radii && (near(d, 100, 0.01f) || near(d, 300, 0.01f) || near(d, 500, 0.01f));
    }
    check(radii && near(r.items[0].pos.x, 100, 0.01f) && near(r.items[0].pos.y, 0, 0.01f), "ring points lie on their circles, the first along +X");
    r = run("OCEQS 1\nOPTION\n  GENERATOR around radius=250 count=5\n");
    check(r.generated == 5, "around makes one circle");
    r = run("OCEQS 1\nOPTION\n  GENERATOR ring outer=100 count=5 arcstart=0 arcend=90\n");
    check(r.generated == 5 && near(r.items.back().pos.x, 0, 0.01f) && near(r.items.back().pos.y, 100, 0.01f), "a partial arc includes both ends");
    r = run("OCEQS 1\nOPTION\n  GENERATOR points list=\"1,2,3;4,5,6;7,8,9\" zoffset=10\n");
    check(r.generated == 3 && r.items[1].pos.z == 16, "points with a z offset");
    r = run("OCEQS 1\nOPTION\n  GENERATOR ring inner=100 outer=500 rings=3 count=8 max=10\n");
    check(r.generated == 10 && r.truncated, "max truncates and sets the truncated flag");
    r = run("OCEQS 1\nOPTION\n  GENERATOR grid halfx=100 halfy=0 step=100\n");
    check(r.generated == 3 && !r.truncated, "a flat grid row");

    // actors by entity list; radius and excludequerier are applied by the run
    F.actors = {{1, Vec3{0, 0, 0}}, {5, Vec3{100, 0, 0}}, {6, Vec3{900, 0, 0}}};
    r = run("OCEQS 1\nOPTION\n  GENERATOR actors named=Guard\n");
    check(r.generated == 2 && r.items[0].entity == 5, "actors: the querier is excluded");
    r = run("OCEQS 1\nOPTION\n  GENERATOR actors named=Guard radius=500 excludequerier=0\n");
    check(r.generated == 2, "actors: radius drops far ones, excludequerier=0 keeps the querier");
}

static void navGenerators() {
    resetFake();
    buildNav();
    F.ctx["querier"].pos = Vec3{125, 125, 0};   // cell (2, 2), left of the wall
    EqsResult r = run("OCEQS 1\nOPTION\n  GENERATOR nav radius=10000 stride=1\n");
    check(r.status == EqsResult::Status::Finished && r.generated == 50, "nav region=same: only the querier's region (50 cells)");
    bool left = true;
    for (const EqsItem& it : r.items) left = left && it.pos.x < 250.0f;
    check(left, "...and every item is left of the wall");
    r = run("OCEQS 1\nOPTION\n  GENERATOR nav radius=10000 stride=1 region=any\n");
    check(r.generated == 90, "nav region=any: both regions (90 walkable cells)");
    r = run("OCEQS 1\nOPTION\n  GENERATOR nav radius=10000 stride=2\n");
    check(r.generated == 15, "nav stride=2 samples a lattice anchored on the centre cell");
    r = run("OCEQS 1\nOPTION\n  GENERATOR nav radius=60 stride=1\n");
    bool near60 = !r.items.empty();
    for (const EqsItem& it : r.items) near60 = near60 && std::hypot(it.pos.x - 125, it.pos.y - 125) <= 60.0f + 0.01f;
    check(near60 && r.generated < 15, "nav radius limits the cells");

    // project=nav snaps, drops what has no walkable cell within four, and drops duplicates of a cell
    r = run("OCEQS 1\nOPTION\n  GENERATOR points list=\"60,60,0;70,70,0;260,25,0;5000,5000,0\" project=nav\n");
    check(r.generated == 2, "project=nav: a duplicate cell and an off-grid point are dropped");
    check(near(r.items[0].pos.x, 75) && near(r.items[0].pos.y, 75), "project=nav snaps to the cell centre");
    check(r.items[1].pos.x < 250.0f || r.items[1].pos.x > 300.0f, "project=nav moves a point out of a wall cell");

    r = run("OCEQS 1\nOPTION\n  GENERATOR points list=\"100,0,0;200,0,0\" project=ground\n");
    check(r.generated == 1 && r.items[0].pos.z == 10, "project=ground drops a miss and takes the hit height");
}

// ---- tests, filters, scores -------------------------------------------------------------------------

static void scoring() {
    resetFake();
    EqsResult r = run("OCEQS 1\n" + kPoints4 + "  TEST distance\n");
    check(r.items.size() == 4 && near(r.items[0].score, 1) && near(r.items[3].score, 0), "linear distance over the observed range 0..1");
    check(near(r.items[0].pos.x, 400) && near(r.items[1].score, 2.0f / 3.0f), "highest distance first, n = (v - lo) / (hi - lo)");

    r = run("OCEQS 1\n" + kPoints4 + "  TEST distance curve=inverse\n");
    check(near(r.items[0].pos.x, 100) && near(r.items[0].score, 1), "curve=inverse prefers the nearest");

    r = run("OCEQS 1\n" + kPoints4 + "  TEST distance lo=0 hi=500\n");
    check(near(r.items[0].score, 0.8f) && near(r.items[3].score, 0.2f), "explicit lo / hi");

    const struct { const char* curve; f32 expect[4]; } curves[] = {
        {"linear", {0.25f, 0.5f, 0.75f, 1.0f}},
        {"inverse", {0.75f, 0.5f, 0.25f, 0.0f}},
        {"square", {0.0625f, 0.25f, 0.5625f, 1.0f}},
        {"invsquare", {0.5625f, 0.25f, 0.0625f, 0.0f}},
        {"sqrt", {0.5f, std::sqrt(0.5f), std::sqrt(0.75f), 1.0f}},
        {"step", {0.0f, 1.0f, 1.0f, 1.0f}},
    };
    for (const auto& c : curves) {
        r = run(std::string("OCEQS 1\n") + kPoints4 + "  TEST distance lo=0 hi=400 curve=" + c.curve + "\n");
        // r is sorted by score; find each item by its x
        bool ok = r.items.size() == 4;
        for (const EqsItem& it : r.items) ok = ok && near(it.score, c.expect[static_cast<int>(it.pos.x / 100) - 1]);
        check(ok, std::string("curve ") + c.curve + " against hand-computed values");
    }

    // A negative weight: score = (2 * nA - nB) / 3 with nA = distance from the querier, nB from the target.
    r = run("OCEQS 1\n" + kPoints4 + "  TEST distance weight=2\n  TEST distance to=target weight=-1\n");
    check(r.items.size() == 4 && near(r.items[0].score, 2.0f / 3.0f) && near(r.items[0].pos.x, 400), "two tests, one with a negative weight: best");
    check(near(r.items[1].score, 1.0f / 3.0f) && near(r.items[2].score, 0.0f) && near(r.items[3].score, -1.0f / 3.0f), "...and the rest, in [-1, 1]");

    // Filter ranges.
    r = run("OCEQS 1\n" + kPoints4 + "  TEST distance max=250 purpose=filter\n");
    check(r.items.size() == 2 && r.passed == 2, "a numeric filter keeps min <= v <= max");
    check(near(r.items[0].score, 1) && near(r.items[1].score, 1), "with no scoring test every item scores 1");
    check(r.items[0].genIndex == 0 && r.items[1].genIndex == 1, "...and ties keep generation order");
    r = run("OCEQS 1\n" + kPoints4 + "  TEST distance min=150 max=350 purpose=filter\n");
    check(r.items.size() == 2 && r.items[0].pos.x == 200, "min and max together");
    r = run("OCEQS 1\n" + kPoints4 + "  TEST distance max=250 purpose=score\n");
    check(r.items.size() == 4, "purpose=score never filters");

    // Ties: equal scores keep generation order.
    r = run("OCEQS 1\nOPTION\n  GENERATOR points list=\"100,0,0;0,100,0;-100,0,0\"\n  TEST distance\n");
    check(r.items.size() == 3 && r.items[0].genIndex == 0 && r.items[1].genIndex == 1 && r.items[2].genIndex == 2, "equal scores tie-break by generation index");

    // dot
    r = run("OCEQS 1\nOPTION\n  GENERATOR points list=\"100,0,0;-100,0,0;0,100,0\"\n  TEST dot\n");
    bool dots = r.testValues.size() == 3;
    for (usize i = 0; i < r.items.size(); ++i) {
        const f32 expect = r.items[i].pos.x > 0 ? 1.0f : r.items[i].pos.x < 0 ? -1.0f : 0.0f;
        dots = dots && near(r.testValues[i], expect);
    }
    check(dots, "dot against the querier's forward: 1, -1, 0");
    r = run("OCEQS 1\nOPTION\n  GENERATOR points list=\"0,0,5;100,0,0\"\n  TEST dot ref=up\n");
    check(r.items.size() == 2 && near(r.testValues[0], 1.0f), "dot ref=up: straight up reads 1");

    // random: deterministic, in [0, 1)
    r = run("OCEQS 1\n" + kPoints4 + "  TEST random seed=7\n");
    const EqsResult r2 = run("OCEQS 1\n" + kPoints4 + "  TEST random seed=7\n");
    bool same = r.items.size() == 4 && r2.items.size() == 4;
    for (usize i = 0; same && i < 4; ++i) same = r.items[i].genIndex == r2.items[i].genIndex && r.testValues[i] == r2.testValues[i] && r.testValues[i] >= 0 && r.testValues[i] < 1;
    check(same, "random is deterministic for a seed and lies in [0, 1)");

    // tag bits on actors
    F.actors = {{5, Vec3{100, 0, 0}}, {6, Vec3{200, 0, 0}}, {7, Vec3{300, 0, 0}}};
    F.tagBits = {{5, 1}, {6, 3}, {7, 4}};
    r = run("OCEQS 1\nOPTION\n  GENERATOR actors tagbits=3\n  TEST tag bits=1 match=any purpose=filter\n");
    check(r.items.size() == 2, "tag bits any: entities 5 and 6 hold bit 1");
    r = run("OCEQS 1\nOPTION\n  GENERATOR actors tagbits=7\n  TEST tag bits=3 match=all purpose=filter\n");
    check(r.items.size() == 1 && r.items[0].entity == 6, "tag bits all");
    r = run("OCEQS 1\nOPTION\n  GENERATOR actors tagbits=7\n  TEST tag bits=3 match=none purpose=filter\n");
    check(r.items.size() == 1 && r.items[0].entity == 7, "tag bits none");

    // custom
    r = run("OCEQS 1\n" + kPoints4 + "  TEST custom name=Double\n");
    check(r.items.size() == 4 && near(r.testValues[0], 800) && near(r.items[0].score, 1), "a custom test supplies the raw value");
    F.customKnown = false;
    r = run("OCEQS 1\n" + kPoints4 + "  TEST custom name=Unknown\n");
    check(r.status == EqsResult::Status::Finished && r.items.empty(), "an unregistered custom test rejects every item");
}

static void costOrdering() {
    resetFake();
    // trace is declared FIRST but costs more: the distance filter runs before it and thins its input.
    const EqsResult r = run("OCEQS 1\n" + kPoints4 + "  TEST trace\n  TEST distance max=250 purpose=filter\n");
    check(F.traceCalls == 2, "trace ran for the 2 survivors of the distance filter, not all 4");
    check(r.evaluations == 6, "evaluations = 4 distance + 2 trace");
    check(r.items.size() == 2, "both survivors are visible (not blocked)");
    resetFake();
    F.blockAll = true;
    const EqsResult b = run("OCEQS 1\n" + kPoints4 + "  TEST trace purpose=filter\n");
    check(b.items.empty() && F.traceCalls == 4, "a blocked trace rejects the item (want=1 keeps visible)");
    resetFake();
    F.blockAll = true;
    const EqsResult w = run("OCEQS 1\n" + kPoints4 + "  TEST trace want=0 purpose=filter\n");
    check(w.items.size() == 4, "want=0 keeps the blocked ones");
    // A custom test (class 1) runs after dot / distance (class 0) and before trace (class 2).
    resetFake();
    const EqsResult c = run("OCEQS 1\n" + kPoints4 + "  TEST custom name=X\n  TEST distance max=150 purpose=filter\n");
    check(F.customCalls == 1 && c.items.size() == 1, "a custom test sees only what the cheaper filter left");
}

static void resultModes() {
    resetFake();
    std::string ten = "OPTION\n  GENERATOR points list=\"";
    for (int i = 1; i <= 10; ++i) ten += std::to_string(i * 10) + ",0,0" + (i < 10 ? ";" : "");
    ten += "\"\n  TEST distance\n";
    check(run("OCEQS 1\nRESULT best\n" + ten).items.size() == 1, "RESULT best keeps one");
    check(run("OCEQS 1\nRESULT top 3\n" + ten).items.size() == 3, "RESULT top 3");
    check(run("OCEQS 1\nRESULT percent 25\n" + ten).items.size() == 3, "RESULT percent 25 of 10 keeps ceil(2.5) = 3");
    const EqsResult all = run("OCEQS 1\n" + ten);
    check(all.items.size() == 10 && all.passed == 10, "RESULT all keeps everything");
    const EqsResult a = run("OCEQS 1\nRESULT randompercent 30\n" + ten, 5);
    const EqsResult b = run("OCEQS 1\nRESULT randompercent 30\n" + ten, 5);
    check(a.items.size() == 1 && b.items.size() == 1 && a.items[0].genIndex == b.items[0].genIndex, "randompercent is deterministic for a run id");
    check(a.items[0].pos.x >= 80, "...and picks from the top slice (3 of 10)");
}

static void options() {
    resetFake();
    const std::string none = "OPTION\n  GENERATOR points list=\"100,0,0;200,0,0\"\n  TEST distance max=0 purpose=filter\n";
    const std::string some = "OPTION\n  GENERATOR points list=\"300,0,0;400,0,0;500,0,0\"\n  TEST distance\n";
    const std::string third = "OPTION\n  GENERATOR points list=\"9,9,9\"\n";
    EqsResult r = run("OCEQS 1\n" + none + some + third);
    check(r.items.size() == 3 && r.items[0].option == 1, "RUN firstvalid: the first option with survivors wins");
    check(r.generated == 5, "...and later options never run (2 + 3 generated)");
    r = run("OCEQS 1\nRUN all\n" + some + third);
    check(r.items.size() == 4 && r.generated == 4, "RUN all merges every option");
    check(r.items[0].option <= 1, "...sorted together");
    r = run("OCEQS 1\n" + none);
    check(r.status == EqsResult::Status::Finished && r.items.empty(), "no option yields an item: Finished with zero items, not Failed");
    r = run("OCEQS 1\nRUN all\nRESULT top 2\n" + some + third);
    check(r.items.size() == 2 && r.testCount == 1 && r.testValues.size() == 2, "test values pad to the widest option");
}

static void failures() {
    resetFake();
    EqsResult r = run("OCEQS 1\nOPTION\n  GENERATOR nav\n");
    check(r.status == EqsResult::Status::Failed && !r.reason.empty(), "a nav generator with no grid fails with a reason");
    r = run("OCEQS 1\nOPTION\n  GENERATOR grid project=nav\n");
    check(r.status == EqsResult::Status::Failed, "project=nav with no grid fails");
    r = run("OCEQS 1\nCONTEXT foe key=Foe\nOPTION\n  GENERATOR grid centre=foe\n");
    check(r.status == EqsResult::Status::Failed && r.reason.find("foe") != std::string::npos, "an unresolved generator centre fails the run");

    // An unresolved test context: a filter rejects everything, a score-only test contributes 0.
    r = run("OCEQS 1\nCONTEXT foe key=Foe\n" + kPoints4 + "  TEST distance to=foe purpose=filter\n");
    check(r.status == EqsResult::Status::Finished && r.items.empty(), "an unresolved context rejects every item for a filter test");
    r = run("OCEQS 1\nCONTEXT foe key=Foe\n" + kPoints4 + "  TEST distance to=foe purpose=score\n");
    check(r.items.size() == 4 && near(r.items[0].score, 0), "...and contributes 0 for a score-only test");

    // path with no nav rejects
    r = run("OCEQS 1\n" + kPoints4 + "  TEST path\n");
    check(r.items.empty(), "a path test with no grid rejects every item");
}

static void paths() {
    resetFake();
    buildNav();
    F.ctx["querier"].pos = Vec3{125, 125, 0};
    EqsResult r = run("OCEQS 1\nOPTION\n  GENERATOR points list=\"75,75,0;375,75,0\"\n  TEST path metric=reachable purpose=filter\n");
    check(r.items.size() == 1 && r.items[0].pos.x == 75, "path reachable: the other region is unreachable and rejected");
    r = run("OCEQS 1\nOPTION\n  GENERATOR points list=\"225,125,0;75,75,0\"\n  TEST path\n");
    check(r.items.size() == 2 && r.testValues[0] > 40.0f && r.testValues[0] < 220.0f, "path length is the route length in cm");
    r = run("OCEQS 1\nOPTION\n  GENERATOR points list=\"225,125,0;375,75,0\"\n  TEST path\n");
    check(r.items.size() == 1, "an unreachable item is rejected whatever the purpose");
}

static void slicing() {
    resetFake();
    const EqsQuery q = parse("OCEQS 1\nOPTION\n  GENERATOR grid halfx=300 halfy=300 step=100\n  TEST trace\n  TEST distance curve=inverse weight=2\n  TEST dot weight=0.5\n");
    EqsRunParams p;
    p.querier = 1;
    p.target = 2;
    EqsRun whole(q, p, makeEnv());
    check(whole.step(0) && whole.done(), "an unlimited step finishes in one call");

    EqsRun sliced(q, p, makeEnv());
    int calls = 0;
    while (!sliced.step(1) && calls < 100000) ++calls;
    check(calls > 1 && sliced.done(), "step(1) needs many calls");
    const EqsResult& a = whole.result();
    const EqsResult& b = sliced.result();
    bool same = a.items.size() == b.items.size() && a.testValues.size() == b.testValues.size() && a.evaluations == b.evaluations;
    for (usize i = 0; same && i < a.items.size(); ++i)
        same = a.items[i].genIndex == b.items[i].genIndex && a.items[i].score == b.items[i].score && a.items[i].pos.x == b.items[i].pos.x;
    check(same, "time slicing never changes a result: item for item identical");
    check(whole.unitsSpent() == sliced.unitsSpent(), "...and costs the same units");

    EqsRun cut(q, p, makeEnv());
    cut.step(10);
    check(!cut.done(), "a small budget leaves the run unfinished");
    cut.cancel();
    check(cut.done() && cut.cancelled() && cut.result().status == EqsResult::Status::Failed, "cancel mid-run ends it");
    check(cut.step(0), "stepping a finished run is harmless");
}

int main() {
    generators();
    navGenerators();
    scoring();
    costOrdering();
    resultModes();
    options();
    failures();
    paths();
    slicing();
    if (g_failures == 0) AVER_INFO("EqsTest: all passed");
    return g_failures;
}
