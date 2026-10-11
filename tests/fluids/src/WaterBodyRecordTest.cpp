// RIVER / RIVERPT / LAKE / LAKEPT records in .ocworld: parse, order, unknown names, byte-stable round
// trip, and that a level without bodies is written exactly as before. CPU only. Exit code = failure count.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcWorld.hpp"

#include <cmath>
#include <string>

using namespace aver;
using namespace aver::fmt;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool has(const std::string& s, const char* needle) { return s.find(needle) != std::string::npos; }

int main() {
    const std::string text =
        "OCWORLD 1\nNAME T\n"
        "WATER name Sea level 0 infinite\n"
        "RIVER name Thames material M_River depth 250 bank 400 slow 0.5 flowscale 2\n"
        "RIVERPT river Thames at 0 0 100 width 900\n"
        "RIVERPT river Nope at 5 5 5\n"
        "RIVERPT river Thames at 1000 -2000 150 flow 180\n"
        "RIVER name My%20River\n"
        "RIVERPT river My%20River at 1 2 3\n"
        "LAKE name Mere level 150 material M_Lake depth 350 bank 450 edge 2000\n"
        "LAKEPT lake Mere at 4000 4000\n"
        "LAKEPT lake Missing at 0 0\n"
        "LAKEPT lake Mere at 5000 4000\n"
        "LAKEPT lake Mere at 5000 5000\n";
    OcWorldData w;
    std::string err;
    check(parseOcworld(text, w, &err), "parses (" + err + ")");
    check(w.rivers.size() == 2 && w.lakes.size() == 1, "two rivers, one lake");
    if (w.rivers.size() == 2 && w.lakes.size() == 1) {
        const OcRiver& r = w.rivers[0];
        check(r.name == "Thames" && r.material == "M_River", "river name and material");
        check(r.depthCm == 250 && r.bankCm == 400 && r.bankSlow == 0.5 && r.flowScale == 2, "river tokens");
        check(r.points.size() == 2, "unknown river name skipped, order kept");
        check(r.points[0].x == 0 && r.points[0].z == 100 && r.points[0].widthCm == 900 && r.points[0].flowCmS == 150, "first point, default flow");
        check(r.points[1].y == -2000 && r.points[1].flowCmS == 180 && r.points[1].widthCm == 800, "second point, default width");
        check(w.rivers[1].name == "My River" && w.rivers[1].points.size() == 1, "percent-encoded name");
        check(w.rivers[1].depthCm == 200 && w.rivers[1].bankSlow == 0.35, "defaults for missing tokens");
        const OcLake& l = w.lakes[0];
        check(l.name == "Mere" && l.levelCm == 150 && l.depthCm == 350 && l.bankCm == 450 && l.maxEdgeCm == 2000, "lake tokens");
        check(l.points.size() == 3 && l.points[2].y == 5000, "lake points in order, unknown lake skipped");
    }

    const std::string s1 = writeOcworld(w);
    OcWorldData w2;
    check(parseOcworld(s1, w2, &err), "written text parses");
    const std::string s2 = writeOcworld(w2);
    check(s1 == s2, "round trip is byte-identical");
    check(has(s1, "RIVER name Thames material M_River depth 250 bank 400 slow 0.5 flowscale 2\n"), "river line");
    check(has(s1, "RIVERPT river Thames at 0 0 100 width 900\n"), "default flow omitted");
    check(has(s1, "RIVERPT river Thames at 1000 -2000 150 flow 180\n"), "default width omitted");
    check(has(s1, "RIVER name My%20River\n"), "defaults omitted, name encoded");
    check(has(s1, "LAKE name Mere level 150 material M_Lake depth 350 bank 450 edge 2000\n"), "lake line");

    // A level without bodies gains nothing.
    OcWorldData plain;
    check(parseOcworld("OCWORLD 1\nNAME T\nWATER name Sea level 12 infinite material M_Sea\n", plain, &err), "WATER-only level parses");
    const std::string p1 = writeOcworld(plain);
    check(plain.rivers.empty() && plain.lakes.empty(), "no bodies");
    check(!has(p1, "RIVER") && !has(p1, "LAKE"), "nothing new is written");
    OcWorldData plain2;
    parseOcworld(p1, plain2, &err);
    check(writeOcworld(plain2) == p1, "WATER-only level round trips byte for byte");

    // An unnamed body is written as "unnamed" and its points keep following it.
    OcWorldData un;
    OcRiver r;
    r.points.push_back({});
    un.rivers.push_back(r);
    const std::string u1 = writeOcworld(un);
    OcWorldData un2;
    parseOcworld(u1, un2, &err);
    check(has(u1, "RIVER name unnamed") && un2.rivers.size() == 1 && un2.rivers[0].points.size() == 1, "unnamed body survives");

    AVER_INFO("WaterBodyRecord tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
