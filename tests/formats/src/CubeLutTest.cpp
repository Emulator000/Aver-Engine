// CubeLutTest -- Adobe .cube 3D LUT parsing and the .ocworld POSTVOLUME record.
//
// The LUT half checks what a colourist would otherwise see only as a wrong grade: the data order (red
// fastest), that a non-default DOMAIN is resampled onto [0,1], and the refusals (1D, wrong row count).
// The POSTVOLUME half is the usual round trip, byte stability, and "an old level without the record still
// loads; an unknown token does not fail the line". Exit code = failure count.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/CubeLut.hpp"
#include "aver/formats/OcWorld.hpp"

#include <cmath>
#include <cstring>
#include <string>

using namespace aver;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

// IEEE half -> float, for reading the uploaded array back.
static f32 halfToFloat(u16 h) {
    const u32 s = (h >> 15) & 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    f32 v;
    if (e == 0)       v = std::ldexp(static_cast<f32>(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else              v = std::ldexp(static_cast<f32>(m | 0x400), static_cast<int>(e) - 25);
    return s ? -v : v;
}

static f32 texel(const fmt::CubeLut& l, u32 r, u32 g, u32 b, int c) {
    return halfToFloat(l.rgba16f[(static_cast<usize>(b) * l.size * l.size + static_cast<usize>(g) * l.size + r) * 4 + c]);
}

static bool approx(f32 a, f32 b, f32 tol = 2e-3f) { return std::fabs(a - b) <= tol; }

int main() {
    AVER_INFO("=== CubeLutTest ===");
    std::string err;

    AVER_INFO("=== .cube: identity, order and format ===");
    {
        fmt::CubeLut l;
        check(fmt::parseCubeLut(fmt::writeIdentityCube(5, "grade"), l, &err), "a 5^3 identity parses: " + err);
        check(l.valid() && l.size == 5 && l.rgba16f.size() == 5u * 5 * 5 * 4, "5^3 RGBA16F array");
        check(l.title == "grade", "the quoted TITLE is kept");
        check(approx(texel(l, 1, 0, 0, 0), 0.25f) && approx(texel(l, 1, 0, 0, 1), 0.0f), "red varies fastest");
        check(approx(texel(l, 0, 1, 0, 1), 0.25f) && approx(texel(l, 0, 0, 1, 2), 0.25f), "then green, then blue");
        check(approx(texel(l, 4, 4, 4, 0), 1.0f) && approx(texel(l, 3, 2, 1, 0), 0.75f) && approx(texel(l, 3, 2, 1, 1), 0.5f) &&
              approx(texel(l, 3, 2, 1, 2), 0.25f), "identity: every texel equals its own coordinate");
        check(approx(texel(l, 2, 2, 2, 3), 1.0f), "alpha is 1");
    }
    {
        fmt::CubeLut l;
        std::string t = fmt::writeIdentityCube(2);
        std::string crlf;
        for (const char c : t) { if (c == '\n') crlf += '\r'; crlf += c; }
        check(fmt::parseCubeLut("# a comment\r\n" + crlf + "\r\n# trailing\r\n", l, &err) && l.size == 2,
              "CRLF line endings, comments and blank lines are tolerated: " + err);
        check(fmt::parseCubeLut("LUT_3D_SIZE 2\nVENDOR_KEY x y z\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n", l, &err),
              "an unknown keyword is skipped: " + err);
    }

    AVER_INFO("=== .cube: DOMAIN is resampled onto 0..1 ===");
    {
        // Two grid points spanning inputs 0..2: output 0 at input 0, output 1 at input 2. The array must cover
        // inputs 0..1, so its far corner reads halfway.
        const char* txt = "LUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\n"
                          "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
        fmt::CubeLut l;
        check(fmt::parseCubeLut(txt, l, &err), "parses: " + err);
        check(approx(texel(l, 0, 0, 0, 0), 0.0f) && approx(texel(l, 1, 1, 1, 0), 0.5f) && approx(texel(l, 1, 0, 0, 1), 0.0f),
              "input 1.0 reads the table at its middle (0.5)");
        check(l.domainMax[0] == 2.0f, "the stated domain is kept for information");
    }
    {
        // Resolve's spelling, and a negative domain.
        const char* txt = "LUT_3D_SIZE 2\nLUT_3D_INPUT_RANGE -1 1\n"
                          "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
        fmt::CubeLut l;
        check(fmt::parseCubeLut(txt, l, &err) && l.domainMin[0] == -1.0f && l.domainMax[2] == 1.0f,
              "LUT_3D_INPUT_RANGE sets the domain: " + err);
        check(approx(texel(l, 0, 0, 0, 0), 0.5f) && approx(texel(l, 1, 1, 1, 0), 1.0f),
              "input 0 maps to the table's middle, input 1 to its end");
    }

    AVER_INFO("=== .cube: refusals carry a reason ===");
    {
        fmt::CubeLut l;
        err.clear();
        check(!fmt::parseCubeLut("LUT_1D_SIZE 4\n0 0 0\n1 1 1\n0 0 0\n1 1 1\n", l, &err) && err.find("1D") != std::string::npos,
              "a 1D LUT is refused naming 1D: " + err);
        check(!l.valid(), "...and leaves no usable array");
        check(!fmt::parseCubeLut("0 0 0\n", l, &err) && err.find("LUT_3D_SIZE") != std::string::npos, "no size is refused: " + err);
        check(!fmt::parseCubeLut("LUT_3D_SIZE 1\n0 0 0\n", l, &err), "size 1 is refused");
        check(!fmt::parseCubeLut("LUT_3D_SIZE 4096\n0 0 0\n", l, &err), "an absurd size is refused");
        check(!fmt::parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n", l, &err) && err.find("expected 8") != std::string::npos,
              "too few rows are refused with the expected count: " + err);
        check(!fmt::parseCubeLut("LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 x\n", l, &err),
              "a non-numeric value is refused: " + err);
        check(!fmt::parseCubeLut("LUT_3D_SIZE 2\nDOMAIN_MIN 1 1 1\nDOMAIN_MAX 1 1 1\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n", l, &err),
              "an empty domain is refused: " + err);
        check(!fmt::loadCubeLut("Z:/definitely/not/here.cube", l, &err), "a missing file is refused: " + err);
    }

    AVER_INFO("=== POSTVOLUME: round trip ===");
    {
        fmt::OcWorldData w;
        w.name = "PV";
        fmt::OcPostVolume a;
        a.name = "Cave Mouth";
        a.bounds[0] = -100; a.bounds[1] = -200; a.bounds[2] = 0; a.bounds[3] = 1000; a.bounds[4] = 500; a.bounds[5] = 300.5;
        a.blendCm = 150;
        a.priority = 3;
        a.weight = 0.75;
        const f64 sat = 0.4, vig = 0.6, lift[3] = {0.01, -0.02, 0.03}, gain[3] = {1.1, 1.0, 0.9};
        a.set("saturation", &sat, 1);
        a.set("vignette", &vig, 1);
        a.set("lift", lift, 3);
        a.set("gain", gain, 3);
        a.lut = "Grades/teal orange.cube";
        fmt::OcPostVolume b;
        b.infinite = true;
        const f64 off = 0;
        b.set("autoexposure", &off, 1);
        w.postVolumes = {a, b};

        const std::string text = fmt::writeOcworld(w);
        fmt::OcWorldData back;
        check(fmt::parseOcworld(text, back, &err), "the written level parses: " + err);
        check(back.postVolumes.size() == 2, "both volumes survive, in order");
        if (back.postVolumes.size() == 2) {
            const fmt::OcPostVolume& x = back.postVolumes[0];
            check(x.name == "Cave Mouth" && !x.infinite, "name (with a space) and boxed");
            check(x.bounds[0] == -100 && x.bounds[1] == -200 && x.bounds[4] == 500 && x.bounds[5] == 300.5, "bounds");
            check(x.blendCm == 150 && x.priority == 3 && x.weight == 0.75, "blend, priority, weight");
            check(x.values.size() == 4, "only the four stated fields are carried");
            const fmt::OcPostValue* l = x.find("lift");
            check(l && l->v[0] == 0.01 && l->v[1] == -0.02 && l->v[2] == 0.03, "a 3-value field keeps all three");
            const fmt::OcPostValue* s = x.find("saturation");
            check(s && s->v[0] == 0.4, "a scalar field");
            check(x.find("grain") == nullptr, "an unstated field stays unstated");
            check(x.lut == "Grades/teal orange.cube", "a LUT path with a space survives");
            const fmt::OcPostVolume& y = back.postVolumes[1];
            check(y.infinite && y.name.empty() && y.priority == 0 && y.weight == 1.0 && y.blendCm == 0.0, "the infinite one keeps its defaults");
            const fmt::OcPostValue* ae = y.find("autoexposure");
            check(ae && ae->v[0] == 0.0, "an explicit 0 override is kept (off is a value)");
        }
        check(fmt::writeOcworld(back) == text, "write(parse(write)) is byte-identical");
    }

    AVER_INFO("=== POSTVOLUME: tolerance and defaults ===");
    {
        fmt::OcWorldData none;
        check(fmt::parseOcworld("OCWORLD 1\nNAME Old\n", none, &err) && none.postVolumes.empty(),
              "a level without the record loads with no volumes");
        check(fmt::writeOcworld(none).find("POSTVOLUME") == std::string::npos, "...and writes none");

        fmt::OcWorldData w;
        const char* junk = "OCWORLD 1\nPOSTVOLUME name A infinite colourtemperature 5 vignette 0.5 future_flag grain 0.2 lut\n"
                           "POSTVOLUME bounds 10 10 10 -10 -10 -10 saturation\n";
        check(fmt::parseOcworld(junk, w, &err) && w.postVolumes.size() == 2, "unknown tokens and a truncated line still give the records: " + err);
        if (w.postVolumes.size() == 2) {
            check(w.postVolumes[0].find("vignette") && w.postVolumes[0].find("grain"), "known fields around unknown tokens are read");
            check(w.postVolumes[0].lut.empty(), "a lut keyword with no path is ignored");
            check(w.postVolumes[1].bounds[0] == -10 && w.postVolumes[1].bounds[3] == 10, "a backwards box is the same box");
            check(w.postVolumes[1].find("saturation") == nullptr, "a field with no value is dropped, not zeroed");
        }

        fmt::OcWorldData d;
        d.postVolumes.emplace_back();
        const std::string text = fmt::writeOcworld(d);
        const usize at = text.find("POSTVOLUME");
        const std::string line = at == std::string::npos ? std::string() : text.substr(at, text.find('\n', at) - at);
        check(line == "POSTVOLUME bounds -500 -500 -500 500 500 500", "a default volume writes only its box: " + line);

        // The key table agrees with itself: every key is findable, lower-case, and 1 or 3 values wide.
        bool ok = true;
        for (const fmt::OcPostKeyInfo& k : fmt::kOcPostKeys) {
            ok = ok && fmt::ocPostKeyInfo(k.key) == &k && (k.count == 1 || k.count == 3) && k.hi >= k.lo;
            for (const char* p = k.key; *p; ++p) ok = ok && !(*p >= 'A' && *p <= 'Z');
        }
        check(ok, "the key table is lower-case, unique-lookup, 1 or 3 wide");
    }

    AVER_INFO("{}: {} checks, {} failures", g_failures ? "FAILED" : "passed", g_checks, g_failures);
    return g_failures;
}
