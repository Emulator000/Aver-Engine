// The .oceqs asset: the documented example parses with every field in place, each refusal names its
// line, and the writer is exact (a second write is byte-identical). Pure.
#include "aver/synapse/EqsAsset.hpp"

#include "aver/core/Log.hpp"

#include <string>

using namespace aver;
using namespace aver::synapse;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static const char* kExample =
    "OCEQS 1\n"
    "DESC \"Cover spots the player cannot see\"        # optional\n"
    "OUTPUT pos=CoverPos entity=CoverEntity score=CoverScore count=CoverCount\n"
    "RESULT best\n"
    "RUN firstvalid\n"
    "CONTEXT target key=Target\n"
    "OPTION\n"
    "  GENERATOR ring centre=querier inner=300 outer=1500 rings=3 count=16 project=nav\n"
    "  TEST trace from=target fromz=160 toz=60 want=0 purpose=filter\n"
    "  TEST distance to=querier curve=inverse weight=2\n"
    "  TEST path from=querier metric=length maxexp=4000 weight=1 curve=inverse\n"
    "OPTION\n"
    "  GENERATOR nav centre=querier radius=2000 stride=3\n"
    "  TEST random seed=7 weight=0.2\n";

static std::string refusal(const std::string& text) {
    EqsQuery q;
    std::string why;
    if (parseEqsQuery(text, "q.oceqs", q, &why)) return "";
    return why + (q.options.empty() ? "" : " [not empty]");
}

static bool refusedAt(const std::string& text, int line, const char* needle = nullptr) {
    const std::string why = refusal(text);
    if (why.rfind("q.oceqs:" + std::to_string(line) + ":", 0) != 0) {
        AVER_ERROR("    got '{}'", why);
        return false;
    }
    return !needle || why.find(needle) != std::string::npos;
}

static void example() {
    EqsQuery q;
    std::string why;
    check(parseEqsQuery(kExample, "ex.oceqs", q, &why), "the section 3.2 example parses: " + why);
    check(q.desc == "Cover spots the player cannot see", "DESC");
    check(q.out.pos == "CoverPos" && q.out.entity == "CoverEntity" && q.out.score == "CoverScore" && q.out.count == "CoverCount", "OUTPUT keys");
    check(q.mode == EqsResultMode::Best && !q.runAll, "RESULT best, RUN firstvalid");
    check(q.contexts.size() == 1 && q.contexts[0].first == "target" && q.contexts[0].second == "Target", "CONTEXT");
    check(q.options.size() == 2, "two options");
    const EqsGenerator& g = q.options[0].gen;
    check(g.kind == EqsGen::Ring && g.inner == 300 && g.outer == 1500 && g.rings == 3 && g.count == 16 && g.project == EqsProject::Nav, "ring generator fields");
    check(q.options[0].tests.size() == 3, "three tests in option 1");
    const EqsTest& t0 = q.options[0].tests[0];
    check(t0.kind == EqsTestKind::Trace && t0.from == "target" && t0.fromZ == 160 && t0.toZ == 60 && !t0.want && t0.purpose == EqsPurpose::Filter, "trace test fields");
    const EqsTest& t1 = q.options[0].tests[1];
    check(t1.kind == EqsTestKind::Distance && t1.curve == EqsCurve::Inverse && t1.weight == 2 && t1.to == "querier" && t1.metric == "3d", "distance test fields and defaults");
    const EqsTest& t2 = q.options[0].tests[2];
    check(t2.kind == EqsTestKind::Path && t2.maxExp == 4000 && t2.metric == "length" && !t2.partialOk, "path test fields");
    const EqsGenerator& n = q.options[1].gen;
    check(n.kind == EqsGen::Nav && n.radius == 2000 && n.stride == 3 && n.sameRegion, "nav generator fields");
    check(q.options[1].tests[0].kind == EqsTestKind::Random && q.options[1].tests[0].seed == 7 && q.options[1].tests[0].weight == 0.2f, "random test fields");
    check(q.name == "ex.oceqs", "the query carries its name");
}

static void defaultsAndKinds() {
    EqsQuery q;
    std::string why;
    check(parseEqsQuery("OCEQS 1\nOPTION\n GENERATOR around\n TEST dot ref=toward:ghost\n", "a", q, &why) == false, "toward:<ctx> needs a declared context");
    check(parseEqsQuery("OCEQS 1\nCONTEXT foe key=Foe\nOPTION\n GENERATOR around radius=300 count=6 arc=120 facing=foe\n TEST dot from=foe ref=toward:foe metric=2d\n TEST dot ref=dir:0,1,0\n", "a", q, &why), "around + dot variants parse: " + why);
    const EqsGenerator& g = q.options[0].gen;
    check(g.kind == EqsGen::Around && g.radius == 300 && g.count == 6 && g.arcStart == -60 && g.arcEnd == 60 && g.facing == "foe", "around: arc is centred on the facing");
    check(q.options[0].tests[0].ref == "toward" && q.options[0].tests[0].refCtx == "foe" && q.options[0].tests[0].metric == "2d", "dot toward:<ctx>");
    check(q.options[0].tests[1].ref == "dir" && q.options[0].tests[1].refDir.y == 1, "dot dir:x,y,z");
    check(eqsDefaultGenerator(EqsGen::Around).radius == 400 && eqsDefaultGenerator(EqsGen::Around).count == 8, "around has its own defaults");

    check(parseEqsQuery("OCEQS 1\nOPTION\n GENERATOR points list=\"1,2,3;4,5,6\"\n TEST tag names=\"A.B, C\" match=all\n TEST custom name=Safe p1=2 text=hello\n", "a", q, &why), "points + tag + custom parse: " + why);
    check(q.options[0].gen.points.size() == 2 && q.options[0].gen.points[1].z == 6, "points list");
    check(q.options[0].tests[0].tags.v.size() == 2 && q.options[0].tests[0].match == 1, "tag names and match");
    check(q.options[0].tests[1].custom == "Safe" && q.options[0].tests[1].p[1] == 2 && q.options[0].tests[1].text == "hello", "custom test fields");

    check(parseEqsQuery("OCEQS 1\nRESULT top 3\nRUN all\nOPTION\n GENERATOR actors class=Guard radius=900 derived=0\n GENERATOR_X\n", "a", q, &why) == false, "an unknown record is refused");
    check(parseEqsQuery("OCEQS 1\nRESULT percent 25\nOPTION\n GENERATOR slots tags=Furniture.Seat radius=900 free=0\n", "a", q, &why) &&
              q.mode == EqsResultMode::Percent && q.modeP == 25 && !q.options[0].gen.freeOnly && q.options[0].gen.slotTags.v.size() == 1,
          "RESULT percent and the slots generator");
}

static void refusals() {
    check(refusedAt("OCEQS 2\n", 1, "unsupported version"), "a wrong version");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR blob\n", 3, "unknown generator"), "an unknown generator kind");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR grid zoom=3\n", 3, "unknown key 'zoom'"), "an unknown key");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR grid\n  GENERATOR ring\n", 4, "second GENERATOR"), "two generators in one option");
    check(refusedAt("OCEQS 1\nOPTION\n  TEST distance\n", 3), "a test before its generator");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR grid centre=ghost\n", 3, "not declared"), "an undeclared context");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR grid halfx=5000 halfy=5000 step=10\n", 3, "over max"), "a grid over max");
    check(refusedAt("OCEQS 1\nRESULT several\nOPTION\n  GENERATOR grid\n", 2, "unknown RESULT"), "a bad RESULT");
    check(refusedAt("OCEQS 1\nRESULT top 0\nOPTION\n  GENERATOR grid\n", 2), "RESULT top 0");
    check(refusedAt("OCEQS 1\nRUN sometimes\nOPTION\n  GENERATOR grid\n", 2, "RUN must be"), "a bad RUN");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR ring rings=40\n", 3, "out of range"), "a number outside its range");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR grid\n  TEST distance curve=wobbly\n", 4, "unknown curve"), "an unknown curve");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR grid\n  TEST distance want=0\n", 4, "boolean"), "'want' on a numeric test");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR actors\n", 3, "exactly one of"), "actors without a filter");
    check(refusedAt("OCEQS 1\nDESC \"a\"\nDESC \"b\"\nOPTION\n  GENERATOR grid\n", 3, "duplicate DESC"), "a duplicate DESC");
    check(refusedAt("OCEQS 1\nOPTION\n  GENERATOR grid\nDESC \"late\"\n", 4, "before the first OPTION"), "a top-level record after OPTION");
    check(refusedAt("OCEQS 1\nCONTEXT target key=A\nCONTEXT target key=B\nOPTION\n  GENERATOR grid\n", 3, "duplicate CONTEXT"), "a duplicate CONTEXT");
    check(refusedAt("OCEQS 1\nOPTION\n", 2, "no GENERATOR"), "an option without a generator");
    check(refusedAt("OCEQS 1\n", 1, "no OPTION"), "no option at all");
    check(refusedAt("OCEQS 1\nRESULT best\nRUN all\nOPTION\n  GENERATOR grid\nOPTION\n  GENERATOR ring\n", 6, "RUN all"), "RUN all with RESULT best on two options");
    check(refusal("OCEQS 1\nOPTION\n  GENERATOR grid\n  TEST random bogus\n").find("q.oceqs:4:") == 0, "a stray argument on a test");
    check(refusal("OCEQS 1\nOPTION\n  GENERATOR grid\n  TEST tag\n").find("q.oceqs:4:") == 0, "tag without names or bits");
    EqsQuery q;
    std::string why;
    check(!parseEqsQuery("OCEQS 1\nOPTION\n  GENERATOR grid\n  TEST distance curve=bad\n", "q", q, &why) && q.options.empty(), "a failed parse leaves the output empty");
    // Sixteen tests are fine, seventeen are not.
    std::string many = "OCEQS 1\nOPTION\n  GENERATOR grid\n";
    for (int i = 0; i < 16; ++i) many += "  TEST random seed=" + std::to_string(i) + "\n";
    check(refusal(many).empty(), "16 tests per option are allowed");
    many += "  TEST random\n";
    check(refusal(many).find("q.oceqs:20:") == 0, "the 17th test is refused");
}

static void writer() {
    EqsQuery a;
    std::string why;
    check(parseEqsQuery(kExample, "ex", a, &why), "parse the example");
    const std::string w1 = writeEqsQuery(a);
    EqsQuery b;
    check(parseEqsQuery(w1, "ex", b, &why), "the written text parses: " + why);
    check(writeEqsQuery(b) == w1, "a second write is byte-identical");
    check(w1.rfind("OCEQS 1\n", 0) == 0 && w1.find("DESC \"Cover spots the player cannot see\"") != std::string::npos, "the writer starts with the header and quotes DESC");

    // A query with unusual values survives the round trip.
    const char* odd =
        "OCEQS 1\nRESULT randompercent 12.5\nRUN all\nCONTEXT spot key=Spot\nOPTION\n"
        "  GENERATOR grid halfx=250 halfy=100 step=50 centre=spot project=ground zoffset=-5 max=300\n"
        "  TEST distance to=spot metric=absz min=10 max=400 lo=0 hi=500 curve=sqrt weight=-1.25 purpose=score name=Height\n"
        "  TEST path metric=reachable partial=ok maxexp=99\n"
        "  TEST trace radius=12.5 gap=0 want=0\n"
        "  TEST tag bits=0x3 match=none\n"
        "OPTION\n  GENERATOR points list=\"-1.5,2,3\"\n";
    EqsQuery c;
    check(parseEqsQuery(odd, "odd", c, &why), "the odd query parses: " + why);
    const std::string o1 = writeEqsQuery(c);
    EqsQuery d;
    check(parseEqsQuery(o1, "odd", d, &why), "the odd query's write parses: " + why);
    check(writeEqsQuery(d) == o1, "the odd query's second write is byte-identical");
    check(d.options[0].tests[0].min == 10 && d.options[0].tests[0].hasHi && d.options[0].tests[0].name == "Height" && d.options[0].tests[0].weight == -1.25f, "numeric test fields survive");
    check(d.options[0].gen.zOffset == -5 && d.options[0].gen.maxItems == 300 && d.options[0].gen.project == EqsProject::Ground, "generator common keys survive");

    // Defaults are omitted, so the starter writes small.
    const std::string starter = writeEqsQuery(eqsStarter());
    EqsQuery s;
    check(parseEqsQuery(starter, "s", s, &why) && writeEqsQuery(s) == starter, "the starter round-trips exactly");
}

static void validation() {
    EqsQuery q;
    std::string why;
    check(parseEqsQuery("OCEQS 1\nCONTEXT unused key=K\nOPTION\n  GENERATOR grid\nOPTION\n  GENERATOR ring\n  TEST distance weight=0 purpose=score\n", "v", q, &why), "parse " + why);
    const auto warnings = validateEqsQuery(q);
    bool unusedCtx = false, noTests = false, zeroWeight = false;
    for (const std::string& w : warnings) {
        unusedCtx = unusedCtx || w.find("'unused'") != std::string::npos;
        noTests = noTests || w.find("no tests") != std::string::npos;
        zeroWeight = zeroWeight || w.find("weight 0") != std::string::npos;
    }
    check(unusedCtx && noTests && zeroWeight, "warnings: unused context, option without tests, zero weight");
    check(validateEqsQuery(eqsStarter()).empty(), "the starter is clean");
}

int main() {
    example();
    defaultsAndKinds();
    refusals();
    writer();
    validation();
    if (g_failures == 0) AVER_INFO("EqsAssetTest: all passed");
    return g_failures;
}
