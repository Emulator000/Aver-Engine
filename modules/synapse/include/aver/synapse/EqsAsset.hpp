#pragma once
// The Environment Query asset (.oceqs): options of one generator plus ordered tests, with an exact
// text reader/writer. Pure. Evaluation lives in Eqs.hpp. Format: FORMAT_SPECS.md 11e.
#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"
#include "aver/synapse/Tags.hpp"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace aver::synapse {

enum class EqsGen : u8 { Grid, Ring, Around, Nav, Actors, Slots, Points };
enum class EqsTestKind : u8 { Distance, Dot, Trace, Path, Tag, Random, Custom };
enum class EqsPurpose : u8 { Filter, Score, Both };
enum class EqsCurve : u8 { Linear, Inverse, Square, InvSquare, Sqrt, Step };
enum class EqsProject : u8 { None, Nav, Ground };
enum class EqsResultMode : u8 { All, Best, Top, Percent, RandomPercent };

inline constexpr u32 kEqsMaxOptions = 8;
inline constexpr u32 kEqsMaxTests = 16;
inline constexpr u32 kEqsMaxContexts = 8;
inline constexpr u32 kEqsMaxItems = 16384;

struct EqsGenerator {
    EqsGen kind = EqsGen::Grid;
    std::string centre = "querier", facing;
    EqsProject project = EqsProject::None;
    f32 zOffset = 0;
    u32 maxItems = 2048;
    f32 halfX = 500, halfY = 500, step = 100, inner = 0, outer = 800, arcStart = 0, arcEnd = 360, radius = 800;
    u32 rings = 1, count = 12, stride = 2;
    bool sameRegion = true, derived = true, excludeQuerier = true, matchAll = false, freeOnly = true;
    std::string className, namePrefix;
    u32 tagBits = 0;
    TagList slotTags;
    std::vector<Vec3> points;
};

struct EqsTest {
    EqsTestKind kind = EqsTestKind::Distance;
    std::string name, from = "querier", to = "querier", ref = "forward", metric, custom, text, refCtx;
    Vec3 refDir{1, 0, 0};
    EqsPurpose purpose = EqsPurpose::Both;
    EqsCurve curve = EqsCurve::Linear;
    f32 weight = 1, lo = 0, hi = 0, min = -INFINITY, max = INFINITY, fromZ = 0, toZ = 0, radius = 0, gap = 10, p[4] = {};
    bool hasLo = false, hasHi = false, want = true, partialOk = false;
    u32 maxExp = 4000, seed = 0, bits = 0;
    TagList tags;
    int match = 0;   // tag test: 0 any, 1 all, 2 none
};

struct EqsOption {
    EqsGenerator gen;
    std::vector<EqsTest> tests;
};

struct EqsQuery {
    std::string name, desc;
    std::vector<EqsOption> options;
    bool runAll = false;
    EqsResultMode mode = EqsResultMode::All;
    u32 modeN = 0;
    f32 modeP = 0;
    std::vector<std::pair<std::string, std::string>> contexts;   // declared name -> board key
    struct Output { std::string pos, entity, score, count; } out;
};

// Defaults per kind (what a missing key means; the writer omits keys equal to these).
EqsGenerator eqsDefaultGenerator(EqsGen kind);
EqsTest eqsDefaultTest(EqsTestKind kind);
const char* eqsGenName(EqsGen k);
const char* eqsTestName(EqsTestKind k);
bool eqsTestIsBoolean(const EqsTest& t);

// "<name>:<line>: <message>" on the first problem; the output is empty after a failed parse.
bool parseEqsQuery(std::string_view text, const std::string& name, EqsQuery& out, std::string* why);
std::string writeEqsQuery(const EqsQuery& q);   // exact: a second write is byte-identical
bool loadEqsQuery(const std::string& path, EqsQuery& out, std::string* why);
bool saveEqsQuery(const std::string& path, const EqsQuery& in, std::string* why);
// Non-fatal warnings (unused context, an option with no tests, zero weights, ...).
std::vector<std::string> validateEqsQuery(const EqsQuery& q);

// A single option with a `ring` generator and a `distance` score test: what "New EQS Query" writes.
EqsQuery eqsStarter();

} // namespace aver::synapse
