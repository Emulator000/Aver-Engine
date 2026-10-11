// Tests NeuRaCNetReference.hpp -- the CPU twin of voxi_neurac_net.hlsl and the spec NeuRaCNet follows
// (docs/plans/neurac_nrd2/DESIGN.md section 5). No GPU: NeuRaCGpuTest holds the kernels to the same functions.
//   1. the RCN_* #defines in the shader against the header's constants, and the shader/C++ structure
//   2. the network: shape, weight count, identity init (trust of exactly 1), the batch against safeBatchLimit
//   3. hash thinning: rate, keep fraction, salts, the expected training count under the cap
//   4. label, eligibility and the trust mapping, with edge cases
//   5. the feature builder on hand-built cells (each input against its definition)
//   6. a MlpReference-trained net separates leaky from clean synthetic cells
#include "aver/core/Log.hpp"
#include "aver/voxi/NeuRaCLayout.hpp"
#include "aver/voxi/NeuRaCNetReference.hpp"

#include <aver/render/neural/MlpReference.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#ifndef AVER_REPO_ROOT
#error "AVER_REPO_ROOT must name the repository root"
#endif

using namespace aver;
namespace rc  = aver::voxi::neurac;
namespace net = aver::voxi::neurac::net;
namespace nn  = aver::render::neural;

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        // An unreadable file must FAIL its assertions, not skip them: an empty string would pass vacuously.
        AVER_ERROR("[NeuRaCNet] could not read {} -- every source assertion against it is a failure", path);
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Value of `#define NAME value` in `text`, comments and suffixes stripped; NaN when absent.
f64 defineValue(const std::string& text, const std::string& name) {
    size_t at = 0;
    while ((at = text.find("#define", at)) != std::string::npos) {
        size_t p = at + 7;
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (text.compare(p, name.size(), name) == 0) {
            const size_t after = p + name.size();
            if (after < text.size() && (text[after] == ' ' || text[after] == '\t')) {
                size_t e = text.find('\n', after);
                std::string v = text.substr(after, e == std::string::npos ? std::string::npos : e - after);
                const size_t c = v.find("//");
                if (c != std::string::npos) v.resize(c);
                std::string clean;
                for (char ch : v) if (ch != '(' && ch != ')' && ch != '\r' && ch != ' ' && ch != '\t') clean += ch;
                while (!clean.empty() && (clean.back() == 'f' || clean.back() == 'F' || clean.back() == 'u' || clean.back() == 'U'))
                    clean.pop_back();
                char* end = nullptr;
                const f64 d = std::strtod(clean.c_str(), &end);
                if (end != clean.c_str() && *end == '\0') return d;
            }
        }
        at = p;
    }
    return std::numeric_limits<f64>::quiet_NaN();
}

bool has(const std::string& t, const char* what) { return t.find(what) != std::string::npos; }
bool approx(f64 a, f64 b, f64 tol) { return std::fabs(a - b) <= tol; }

// Deterministic uniform generator (LCG), so every machine builds the same scenes.
struct Rng {
    u32 s = 2463534242u;
    explicit Rng(u32 seed = 1) : s(2463534242u ^ (seed * 2654435761u)) {}
    f32 next() {   // [0, 1)
        s = s * 1664525u + 1013904223u;
        return static_cast<f32>(s >> 8) * (1.0f / 16777216.0f);
    }
    f32 uni(f32 a, f32 b) { return a + (b - a) * next(); }
    u32 below(u32 n) { return static_cast<u32>(next() * static_cast<f32>(n)) % n; }
};

void normalize3(f32 v[3]) {
    const f32 l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; ++i) v[i] /= l;
}
void randomUnit(Rng& r, f32 v[3]) {
    do { for (int i = 0; i < 3; ++i) v[i] = r.uni(-1.0f, 1.0f); } while (v[0] * v[0] + v[1] * v[1] + v[2] * v[2] < 0.05f);
    normalize3(v);
}

// ------------------------------------------------------------------------------- a synthetic cache
rc::RcInfo makeInfo(u32 frame) {
    rc::RcInfo i{};
    i.frameIndex = frame;
    i.ageStepFrames = 16;
    i.sampleCap = 64;
    i.alphaMin = 0.0625f;
    i.coarseCaps = rc::kDefaultCoarseCap1 | (rc::kDefaultCoarseCap2 << 8);
    for (u32 c = 0; c < rc::kCascades; ++c) {
        for (int a = 0; a < 3; ++a) i.cas[c].originCell[a] = 0;   // world cell == texel in [0, 64)
        i.cas[c].cellSizeCm = rc::kCellSizeCm[c];
    }
    return i;
}

struct Scene {
    std::vector<rc::RcCell> cells;
    std::vector<i32> accum;
    rc::RcInfo info;
    Scene() : cells(rc::kCells), accum(rc::kAccumInts, 0), info(makeInfo(5)) {}
};

// SH of a radiance L (per channel) with L1 anisotropy a (c1..c3 as a fraction of c0), scaled.
void makeSh(const f32 L[3], const f32 aniso[3], f32 scale, f32 out[rc::kShFloats]) {
    for (int ch = 0; ch < 3; ++ch) out[ch] = 3.5449077f * L[ch] * scale;   // integral of L Y0 over the sphere
    for (int j = 1; j < 4; ++j) for (int ch = 0; ch < 3; ++ch) out[3 * j + ch] = aniso[j - 1] * out[ch];
}

void writeCell(rc::RcCell& c, const f32 sh[rc::kShFloats], const f32 nu[3], f32 len, u32 nEff, u32 age, u32 trust6, u32 tag) {
    u32 w[6];
    rc::packSh(sh, w);
    c.a[0] = w[0]; c.a[1] = w[1]; c.a[2] = w[2]; c.a[3] = w[3];
    c.b[0] = w[4]; c.b[1] = w[5];
    const f32 v[3] = {nu[0] * len, nu[1] * len, nu[2] * len};
    c.b[2] = rc::packNormal(v, trust6);
    c.b[3] = rc::metaPack(tag, nEff, age);
}

struct NbParams {
    bool present = false, badTag = false;
    f32 scale = 1.0f, len = 1.0f;
    f32 dir[3] = {0.0f, 1.0f, 0.0f};
    u32 nEff = 10, age = 0;
};
struct CaseParams {
    u32 cascade = 0;
    i32 world[3] = {10, 10, 10};
    f32 len = 1.0f;
    f32 nu[3] = {0.3f, 0.8f, 0.52f};
    u32 nEff = 10, age = 0, nFresh = 16, trust6 = rc::kTrustMax, counts = 0;   // counts overrides nFresh's word when set
    bool badTag = false;
    f32 L[3] = {1.0f, 0.8f, 0.6f};
    f32 aniso[3] = {0.1f, 0.0f, 0.05f};
    f32 freshScale = 1.0f;
    NbParams nb[6];
    CaseParams() { normalize3(nu); }
    void allNeighbours(f32 scale) {
        for (NbParams& n : nb) { n.present = true; n.scale = scale; for (int a = 0; a < 3; ++a) n.dir[a] = nu[a]; }
    }
};

// Writes the cell, its fresh accumulator and its neighbours; returns the cell index.
u32 writeCase(Scene& S, const CaseParams& p) {
    const u32 cell = rc::cellIndex(p.cascade, p.world[0], p.world[1], p.world[2]);
    const u32 tag = rc::tagPack(p.world[0], p.world[1], p.world[2]);
    f32 sh[rc::kShFloats];
    makeSh(p.L, p.aniso, 1.0f, sh);
    writeCell(S.cells[cell], sh, p.nu, p.len, p.nEff, p.age, p.trust6, p.badTag ? (tag ^ 1u) : tag);

    const u32 n = p.counts ? p.counts : p.nFresh;
    S.accum[cell] = static_cast<i32>(n | (3u << 16));
    if (n) {
        const u32 nUsed = std::min(n & 0xFFFFu, rc::capForCascade(S.info, p.cascade));   // what the resolve will divide by
        f32 fresh[rc::kShFloats];
        makeSh(p.L, p.aniso, p.freshScale, fresh);
        const u32 pb = rc::kPayloadBase + cell * rc::kPayloadInts;
        for (u32 k = 0; k < rc::kShFloats; ++k)
            S.accum[pb + k] = static_cast<i32>(std::llround(static_cast<f64>(fresh[k]) * nUsed * rc::kShScale));
    }
    for (int k = 0; k < 6; ++k) {
        const NbParams& nb = p.nb[k];
        if (!nb.present) continue;
        const i32 nw[3] = {p.world[0] + net::kNeighbour[k][0], p.world[1] + net::kNeighbour[k][1], p.world[2] + net::kNeighbour[k][2]};
        f32 nsh[rc::kShFloats];
        makeSh(p.L, p.aniso, nb.scale, nsh);
        const u32 ntag = rc::tagPack(nw[0], nw[1], nw[2]);
        writeCell(S.cells[rc::cellIndex(p.cascade, nw[0], nw[1], nw[2])], nsh, nb.dir, nb.len, nb.nEff, nb.age, rc::kTrustMax,
                  nb.badTag ? (ntag ^ 1u) : ntag);
    }
    return cell;
}

net::Features featuresOf(const Scene& S, u32 cell) { return net::buildForCell(S.cells.data(), S.accum.data(), S.info, cell); }

}  // namespace

int main() {
    const std::string root = AVER_REPO_ROOT;
    const std::string shaders = root + "/modules/render.voxi/shaders/";

    // ---- 1. the shader and the C++ agree ------------------------------------------------------------
    AVER_INFO("=== shader constants and structure ===");
    {
        const std::string hlsl = readFile(shaders + "voxi_neurac_net.hlsl");
        const std::string cpp = readFile(root + "/modules/render.voxi/src/NeuRaCNet.cpp");
        const std::string cmake = readFile(root + "/modules/render.voxi/CMakeLists.txt");
        struct Lit { const char* name; f64 value; };
        const Lit lits[] = {
            {"RCN_INPUTS", net::kInputs}, {"RCN_MAX_INFER", net::kMaxInfer}, {"RCN_MAX_TRAIN", net::kMaxTrain},
            {"RCN_INFER_TARGET", net::kInferTarget}, {"RCN_TRAIN_TARGET", net::kTrainTarget}, {"RCN_MIN_FRESH", net::kMinFresh},
            {"RCN_MIN_NEFF", net::kMinNEff}, {"RCN_EPS", net::kEps}, {"RCN_TRUST_DIV", net::kTrustDivisor},
            {"RCN_STAT_SCALE", net::kStatScale}, {"RCN_STATS", net::kStatCount},
        };
        for (const Lit& l : lits) {
            const f64 v = defineValue(hlsl, l.name);
            check(!std::isnan(v) && approx(v, l.value, 1e-6),
                  std::string("voxi_neurac_net.hlsl #define ") + l.name + " equals the header's constant");
        }
        for (const char* entry : {"CSRcNetReset", "CSRcNetFeatures", "CSRcNetFinish", "CSRcNetApply"}) {
            check(has(hlsl, (std::string("void ") + entry + "(").c_str()) && has(cpp, (std::string("\"") + entry + "\"").c_str()),
                  std::string("entry point ") + entry + " exists in the shader and NeuRaCNet.cpp asks for it");
        }
        check(has(hlsl, "register(t0)"), "the shader declares RcInfo at t0");
        bool allUav = true;
        for (u32 i = 0; i < 12; ++i) allUav = allUav && has(hlsl, ("register(u" + std::to_string(i) + ")").c_str());
        check(allUav && !has(hlsl, "register(u12)"), "the shader declares u0..u11, the 12 UAVs NeuRaCNet binds");
        check(has(hlsl, "#include \"voxi_neurac.hlsli\"") && !has(hlsl, "voxi_neurac_io.hlsli"),
              "the shader includes the pure-maths header, not the twin's resource-bound one");
        check(has(hlsl, "[numthreads(64, 1, 1)]") && has(hlsl, "void CSRcNetFeatures(") && rc::kResolveGroups * 64u == rc::kCells,
              "the features kernel is one thread per cell in groups of 64 (the 12,288-group dispatch)");
        check(has(cpp, "kMaxInfer / 64u"), "the apply dispatch covers every inference record in groups of 64");
        check(has(cmake, "src/NeuRaCNet.cpp"), "modules/render.voxi/CMakeLists.txt builds NeuRaCNet.cpp");
        check(has(hlsl, "saturate(pred / RCN_TRUST_DIV)") && has(hlsl, "0x03FFFFFFu") && has(hlsl, "t6 << 26"),
              "the apply bakes round(63 saturate(pred / 0.85)) into bits 26..31 and keeps the other 26");
        check(has(hlsl, "1.0 - min(1.0, 2.0 * rho)") && has(hlsl, "abs(Ef - Eo) / (Ef + Eo + RCN_EPS)"),
              "the label is 1 - min(1, 2 rho), rho = |Ef - Eo| / (Ef + Eo + eps)");
        check(has(hlsl, "f[3] = 1.0;") && has(hlsl, "f[3] = log2(1.0 + (float)n) / 6.0;"),
              "input 3 is 1 at inference and log2(1 + n_fresh) / 6 in a training record");
    }

    // ---- 2. the network ------------------------------------------------------------------------------
    AVER_INFO("=== network shape and identity init ===");
    {
        const nn::MlpDesc d = net::netDesc();
        const nn::OptimiserDesc o = net::netOptimiser();
        check(nn::validate(d) && nn::validate(o), "the network and optimiser descriptions are inside the Mlp's limits");
        check(d.inputs == 10 && d.outputs == 1 && d.hiddenWidth == 8 && d.hiddenLayers == 2 && d.hidden == nn::Activation::ReLU &&
                  d.output == nn::Activation::Sigmoid && d.bias,
              "10-8-8-1, ReLU hidden, Sigmoid out, with biases");
        check(approx(o.learningRate, 2e-3, 1e-9) && approx(o.weightEma, 0.99, 1e-6) && approx(o.gradClamp, 2.0, 1e-9) && o.loss == nn::Loss::L2,
              "Adam lr 2e-3, weightEma 0.99, gradClamp 2, L2 loss");
        const nn::MlpLayout lay = nn::MlpLayout::make(d);
        check(lay.total == 80 + 8 + 64 + 8 + 8 + 1, "169 weights: 80 + 64 + 8 weights and 8 + 8 + 1 biases");
        check(net::kMaxTrain <= nn::safeBatchLimit(o) && nn::safeBatchLimit(o) == 16383 && net::kMaxTrain == 16383,
              "the training batch (16383) is within the Mlp's safeBatchLimit at gradClamp 2");
        check(net::kInferTarget < net::kMaxInfer && net::kTrainTarget < net::kMaxTrain,
              "the thinning targets leave headroom under both list caps");

        const std::vector<f32> w = net::identityWeights(d);
        const u32 last = lay.layers - 1;
        bool zeroed = true;
        for (u32 i = 0; i < lay.inDim[last] * lay.outDim[last]; ++i) zeroed = zeroed && w[lay.wOffset[last] + i] == 0.0f;
        check(w.size() == lay.total && zeroed && approx(w[lay.bOffset[last]], std::log(9.0), 1e-6),
              "identity init: the last layer's W is 0 and its bias is logit(0.9) = ln 9");
        bool hiddenKept = false;
        for (u32 i = 0; i < lay.inDim[0] * lay.outDim[0]; ++i) hiddenKept = hiddenKept || w[lay.wOffset[0] + i] != 0.0f;
        check(hiddenKept, "the hidden layers keep their He-uniform weights");

        nn::MlpReference m(d, o);
        check(m.setWeights(w), "MlpReference accepts the identity weights");
        Rng r(7);
        bool exact = true, constant = true;
        f32 first = -1.0f;
        for (int i = 0; i < 2000; ++i) {
            f32 in[net::kInputs];
            for (f32& x : in) x = r.uni(-1.0f, 1.0f);
            f32 out[1];
            m.forward(in, out, i & 1);
            if (first < 0.0f) first = out[0];
            constant = constant && approx(out[0], first, 1e-7) && approx(out[0], 0.9, 1e-6);
            exact = exact && net::trustOf(out[0]) == 1.0f && net::trustBits(out[0]) == rc::kTrustMax;
        }
        check(constant, "the identity-init net outputs 0.9 for any input");
        check(exact, "its trust is exactly 1.0 and bakes to 63, so stage 2 starts as stage 1");
    }

    // ---- 3. thinning ---------------------------------------------------------------------------------
    AVER_INFO("=== hash thinning ===");
    {
        check(net::thinRate(0, 100) == 1.0f && net::thinRate(100, 100) == 1.0f && net::thinRate(50, 100) == 1.0f,
              "no thinning at or under the target");
        check(approx(net::thinRate(200, 100), 0.5, 1e-7) && approx(net::thinRate(400, 100), 0.25, 1e-7), "twice the target keeps half");
        bool all = true;
        for (u32 c = 0; c < 5000; ++c) all = all && net::thinKeep(1.0f, c, 9, net::kSaltInfer);
        check(all, "rate 1 keeps every cell");
        const u32 N = 100000;
        u32 kept = 0, keptBoth = 0;
        for (u32 c = 0; c < N; ++c) {
            const bool a = net::thinKeep(0.25f, c, 7, net::kSaltInfer), b = net::thinKeep(0.25f, c, 7, net::kSaltTrain);
            kept += a;
            keptBoth += a && b;
        }
        check(approx(static_cast<f64>(kept) / N, 0.25, 0.01), "rate 0.25 keeps about a quarter of 100,000 cells (" + std::to_string(kept) + ")");
        check(approx(static_cast<f64>(keptBoth) / N, 0.0625, 0.01), "the two salts decorrelate: both keep about rate^2 (" + std::to_string(keptBoth) + ")");
        u32 changed = 0;
        for (u32 c = 0; c < 20000; ++c) changed += net::thinKeep(0.5f, c, 7, 0) != net::thinKeep(0.5f, c, 8, 0);
        check(changed > 8000 && changed < 12000, "the kept set changes frame to frame (about half the cells flip at rate 0.5)");
        bool det = true;
        for (u32 c = 0; c < 1000; ++c) det = det && net::thinKeep(0.3f, c, 3, 1) == net::thinKeep(0.3f, c, 3, 1);
        check(det, "the decision is a pure function of (cell, frame, salt)");
        // 300k eligible cells last frame: the rate holds this frame's expected training count at the target.
        const f32 rate = net::thinRate(300000, net::kTrainTarget);
        u32 train = 0;
        for (u32 c = 0; c < 300000; ++c) train += net::thinKeep(rate, c * 3u + 1u, 11, net::kSaltTrain);
        check(std::abs(static_cast<i64>(train) - static_cast<i64>(net::kTrainTarget)) < 600 && train < net::kMaxTrain,
              "300,000 eligible cells thin to about 12,288 training records, under 16,383 (" + std::to_string(train) + ")");
    }

    // ---- 4. label, eligibility, trust ----------------------------------------------------------------
    AVER_INFO("=== label, eligibility, trust mapping ===");
    {
        check(net::label(1.0f, 1.0f) == 1.0f, "label: an unmoved cell is 1");
        check(approx(net::label(1.0f, 1.5f), 0.6, 2e-3), "label: Ef = 1.5 Eo gives rho = 0.2, label 0.6");
        check(approx(net::label(1.0f, 0.5f), 1.0 - 2.0 * (0.5 / 1.501), 2e-3), "label: Ef = Eo / 2 gives rho = 1/3");
        check(net::label(1.0f, 3.0f) >= 0.0f && net::label(1.0f, 3.0f) < 1e-3f && net::label(3.0f, 1.0f) < 1e-3f &&
                  net::label(1.0f, 100.0f) == 0.0f,
              "label: a cell that changed by a factor of 3 or more is ~0, never negative");
        check(approx(net::label(2.0f, 1.0f), net::label(1.0f, 2.0f), 1e-6), "label: symmetric in Eo and Ef");

        check(net::eligible(8, 2, 1.0f, 1.0f), "eligible: 8 fresh samples, n_eff 2");
        check(!net::eligible(7, 2, 1.0f, 1.0f), "not eligible: 7 fresh samples");
        check(!net::eligible(16, 1, 1.0f, 1.0f), "not eligible: n_eff 1");
        check(!net::eligible(16, 5, 0.0004f, 0.0004f) && net::eligible(16, 5, 0.01f, 0.0f), "not eligible when Eo + Ef <= 1e-3");

        check(net::trustBits(0.0f) == 0 && net::trustBits(-3.0f) == 0, "trust: prediction 0 (or below) bakes to 0");
        check(net::trustBits(0.85f) == 63 && net::trustBits(0.9f) == 63 && net::trustBits(1.0f) == 63 && net::trustBits(7.0f) == 63,
              "trust: 0.85 and above bake to 63");
        bool sweep = true, monotone = true;
        u32 prev = 0;
        for (int i = 0; i <= 120; ++i) {
            const f32 p = static_cast<f32>(i) * 0.01f;
            const f64 want = std::floor(63.0 * std::min(1.0, p / 0.85) + 0.5);
            const u32 got = net::trustBits(p);
            sweep = sweep && std::fabs(static_cast<f64>(got) - want) <= 1.0;
            monotone = monotone && got >= prev;
            prev = got;
        }
        check(sweep && monotone, "trust: round(63 saturate(p / 0.85)) over 0..1.2, monotone");
        check(net::trustBits(0.2f) == 15, "trust: 0.2 -> 0.235 -> 15");
        check(!net::trustUsable(std::numeric_limits<f32>::quiet_NaN()) && !net::trustUsable(std::numeric_limits<f32>::infinity()) &&
                  net::trustUsable(0.5f),
              "a NaN or infinite prediction is not applied (the old trust stays)");

        const f32 v[3] = {0.3f, -0.5f, 0.7f};
        const u32 word = rc::packNormal(v, 63);
        const u32 w2 = net::withTrust(word, 17);
        f32 d0[3], d1[3], l0, l1;
        rc::unpackNormal(word, d0, l0);
        rc::unpackNormal(w2, d1, l1);
        check(rc::normalTrust6(w2) == 17 && (w2 & 0x03FFFFFFu) == (word & 0x03FFFFFFu) && d0[0] == d1[0] && l0 == l1,
              "withTrust replaces bits 26..31 and keeps the direction and length bits");
        check(rc::normalTrust6(net::withTrust(word, 200)) == rc::kTrustMax, "withTrust clamps to 63");
    }

    // ---- 5. the feature builder ---------------------------------------------------------------------
    AVER_INFO("=== features on hand-built cells ===");
    {
        auto features = [&](const CaseParams& p, Scene& S) { return featuresOf(S, writeCase(S, p)); };
        Scene S;

        // An isolated, steady cell.
        {
            CaseParams p;
            p.len = 0.9f; p.nEff = 12; p.age = 3; p.nFresh = 12; p.cascade = 1;
            const u32 cell = writeCase(S, p);
            const net::Features f = featuresOf(S, cell);
            f32 nu[3], len;
            rc::unpackNormal(S.cells[cell].b[2], nu, len);
            check(f.touched && f.eligible && f.nFresh == 12, "an isolated cell with 12 fresh samples is touched and eligible");
            check(f.in[0] == len && approx(len, 0.9, 1.0 / 63.0), "input 0 is the cell's planarity as stored (0.9, resolution 1/63)");
            check(approx(f.in[1], 12.0 / 15.0, 1e-6) && approx(f.in[2], 3.0 / 15.0, 1e-6), "inputs 1 and 2 are n_eff / 15 and age / 15");
            check(f.in[3] == 1.0f && approx(f.trainIn3, std::log2(13.0) / 6.0, 1e-6), "input 3 is 1 at inference and log2(1 + 12) / 6 in training");
            check(f.in[4] == 0.0f && f.in[5] == 0.0f && f.in[6] == 0.0f, "no neighbours: support, disagreement and the neighbour ratio are 0");
            check(approx(f.in[7], std::sqrt(0.01 + 0.0 + 0.0025) * 0.5, 3e-3), "input 7 is the old SH anisotropy |c1,c2,c3| / c0, halved");
            check(f.in[8] == nu[1] && approx(f.in[9], 0.5, 1e-7), "inputs 8 and 9 are nu.y and cascade / 2");
            check(f.label > 0.995f && approx(f.Ef / f.Eo, 1.0, 3e-3), "an unmoved cell has Ef = Eo and label ~1 (" + std::to_string(f.label) + ")");
        }

        // Neighbours.
        {
            CaseParams p;
            p.world[0] = 20;
            p.allNeighbours(1.0f);
            const net::Features f = features(p, S);
            check(approx(f.in[4], 1.0, 2e-2) && approx(f.in[5], 0.0, 1e-4) && approx(f.in[6], 0.0, 1e-6),
                  "six identical neighbours: support ~1, no disagreement, ratio 0 (support " + std::to_string(f.in[4]) + ")");

            CaseParams q;
            q.world[0] = 24;
            q.allNeighbours(4.0f);
            const net::Features g = features(q, S);
            check(approx(g.in[6], 0.5, 2e-3) && approx(g.in[5], 0.0, 1e-4),
                  "six neighbours four times brighter: ratio log2(4) / 4 = 0.5, no disagreement among them (" + std::to_string(g.in[6]) + ")");

            CaseParams h;
            h.world[0] = 28;
            h.nb[0].present = h.nb[1].present = true;
            for (int a = 0; a < 3; ++a) h.nb[0].dir[a] = h.nb[1].dir[a] = h.nu[a];
            h.nb[0].scale = 1.0f;
            h.nb[1].scale = 3.0f;
            const net::Features k = features(h, S);
            check(approx(k.in[5], 0.25, 0.01) && approx(k.in[4], 2.0 / 6.0, 1e-2),
                  "two neighbours at 1x and 3x: disagreement 0.5 -> 0.25, support 2/6 (" + std::to_string(k.in[5]) + ")");
            check(approx(k.in[6], std::log2((2.0 + 0.001) / (1.0 + 0.001)) * 0.25, 5e-3), "their weighted mean is 2x: ratio log2(2) / 4");

            // Neighbours that must not count: wrong tag, empty, aged out, normal at right angles or opposite, absent.
            CaseParams m;
            m.world[0] = 32;
            for (NbParams& n : m.nb) { n.present = true; for (int a = 0; a < 3; ++a) n.dir[a] = m.nu[a]; }
            m.nb[1].badTag = true;
            m.nb[2].nEff = 0;
            m.nb[3].age = 15;
            m.nb[4].dir[0] = -m.nu[0]; m.nb[4].dir[1] = -m.nu[1]; m.nb[4].dir[2] = -m.nu[2];
            m.nb[5].present = false;
            const net::Features n = features(m, S);
            check(approx(n.in[4], 1.0 / 6.0, 1e-2), "only the one valid, agreeing neighbour counts (support " + std::to_string(n.in[4]) + ")");
        }

        // The anisotropy clamp and the dead-neighbour weights.
        {
            CaseParams p;
            p.world[0] = 36;
            p.aniso[0] = p.aniso[1] = p.aniso[2] = 1.5f;
            const net::Features f = features(p, S);
            check(f.touched && f.in[7] == 1.0f, "anisotropy above 2 clamps, so input 7 stays <= 1");
        }

        // Label against the fresh scale.
        {
            struct Row { f32 scale; f64 label; };
            const Row rows[] = {{1.5f, 0.6}, {0.5f, 1.0 - 2.0 / 3.0}, {3.0f, 0.0}, {0.0f, 0.0}};
            bool ok = true;
            for (const Row& r : rows) {
                CaseParams p;
                p.world[0] = 40;
                p.freshScale = r.scale;
                const net::Features f = features(p, S);
                ok = ok && f.eligible && approx(f.label, r.label, 4e-3);
            }
            check(ok, "label from the real cell and accumulator: fresh at 1.5x, 0.5x, 3x and 0 gives 0.6, 0.333, 0, 0");
        }

        // Eligibility and "touched".
        {
            CaseParams p;
            p.world[0] = 44;
            p.nFresh = 7;
            check(features(p, S).touched && !features(p, S).eligible, "7 fresh samples: touched, not eligible");
            p.nFresh = 8;
            check(features(p, S).eligible, "8 fresh samples: eligible");
            p.nEff = 1;
            check(features(p, S).touched && !features(p, S).eligible, "n_eff 1: touched, not eligible");
            p.nEff = 10; p.age = 15;
            check(!features(p, S).touched, "age 15 is not a valid old state: no record");
            p.age = 0; p.badTag = true;
            check(!features(p, S).touched, "a stale tag is not a valid old state: no record");
            p.badTag = false; p.nFresh = 0;
            check(!features(p, S).touched, "a cell with no fresh samples is not touched");
            p.nFresh = 16; p.L[0] = p.L[1] = p.L[2] = 1.0e-5f;
            check(features(p, S).touched && !features(p, S).eligible, "a nearly black cell is touched, not eligible");
            S.info.flags = rc::kFlagClearAll;
            check(!features(p, S).touched, "a cache-clear frame makes no records");
            S.info.flags = 0;
            p = CaseParams();
            p.world[0] = 44;
            p.counts = 200;
            check(features(p, S).nFresh == 64, "fresh samples are capped by the cascade's cap: 200 -> 64 (cascade 0)");
            p.cascade = 1;
            check(features(p, S).nFresh == 16, "... 16 (cascade 1)");
            p.cascade = 2;
            check(features(p, S).nFresh == 8, "... 8 (cascade 2)");
        }

        // Addressing: negative world cells wrap into the window and still find their neighbours.
        {
            CaseParams p;
            p.world[0] = -5; p.world[1] = -3; p.world[2] = -30;
            p.allNeighbours(1.0f);
            Scene T;
            T.info = makeInfo(5);
            for (int a = 0; a < 3; ++a) for (u32 c = 0; c < rc::kCascades; ++c) T.info.cas[c].originCell[a] = -32;
            const u32 cell = writeCase(T, p);
            u32 cascade;
            i32 world[3];
            net::cellWorld(cell, T.info, cascade, world);
            check(cascade == 0 && world[0] == -5 && world[1] == -3 && world[2] == -30,
                  "cellWorld inverts the toroidal index in a window whose origin is -32");
            const net::Features f = featuresOf(T, cell);
            check(f.touched && approx(f.in[4], 1.0, 2e-2), "negative world cells: the neighbours are found through the wrapped index");
        }
    }

    // ---- 6. a trained net separates leaky from clean ---------------------------------------------------
    AVER_INFO("=== a MlpReference-trained net separates leaky cells from clean ones ===");
    {
        Scene S;
        Rng rng(3);
        auto pos = [](u32 k, CaseParams& p) {   // spaced 4 cells apart so no two cases share a neighbour
            p.world[0] = static_cast<i32>(4 * (k % 15) + 1);
            p.world[1] = static_cast<i32>(4 * ((k / 15) % 15) + 1);
            p.world[2] = static_cast<i32>(4 * (k / 225) + 1);
            p.cascade = k % 3;
        };
        auto makeCase = [&](u32 k, bool leaky) {
            CaseParams p;
            pos(k, p);
            randomUnit(rng, p.nu);
            const f32 base = rng.uni(0.2f, 3.0f);
            p.L[0] = base; p.L[1] = base * rng.uni(0.7f, 1.0f); p.L[2] = base * rng.uni(0.6f, 1.0f);
            p.nFresh = 8 + rng.below(33);
            if (!leaky) {
                p.len = rng.uni(0.9f, 1.0f);
                p.nEff = 8 + rng.below(8);
                p.age = rng.below(3);
                for (f32& a : p.aniso) a = rng.uni(-0.15f, 0.15f);
                p.freshScale = rng.uni(0.95f, 1.05f);
                for (int i = 0; i < 6; ++i) {
                    NbParams& n = p.nb[i];
                    n.present = true;
                    n.scale = rng.uni(0.95f, 1.05f);
                    for (int a = 0; a < 3; ++a) n.dir[a] = p.nu[a] + 0.05f * rng.uni(-1.0f, 1.0f);
                    normalize3(n.dir);
                    n.len = rng.uni(0.9f, 1.0f);
                    n.nEff = 8 + rng.below(8);
                    n.age = rng.below(3);
                }
            } else {
                p.len = rng.uni(0.25f, 0.6f);
                p.nEff = 2 + rng.below(5);
                p.age = rng.below(7);
                for (f32& a : p.aniso) a = rng.uni(-0.3f, 0.3f);
                const f32 r = std::exp(rng.uni(std::log(0.2f), std::log(0.6f)));
                p.freshScale = rng.below(2) ? r : 1.0f / r;
                const u32 count = rng.below(5);
                for (u32 i = 0; i < count; ++i) {
                    NbParams& n = p.nb[rng.below(6)];
                    n.present = true;
                    n.scale = std::exp(rng.uni(std::log(0.3f), std::log(3.0f)));
                    randomUnit(rng, n.dir);
                    n.len = rng.uni(0.3f, 0.9f);
                    n.nEff = 2 + rng.below(7);
                    n.age = rng.below(4);
                }
            }
            return featuresOf(S, writeCase(S, p));
        };

        constexpr u32 kTrainCases = 1536, kTestCases = 512;
        std::vector<f32> trainIn, trainOut, testIn;
        std::vector<bool> testLeaky;
        u32 eligibleCount = 0;
        for (u32 k = 0; k < kTrainCases + kTestCases; ++k) {
            const bool leaky = (k & 1u) != 0;   // alternating, so every contiguous batch is half and half
            const net::Features f = makeCase(k, leaky);
            eligibleCount += f.touched && f.eligible;
            if (k < kTrainCases) {
                for (u32 i = 0; i < net::kInputs; ++i) trainIn.push_back(i == 3 ? f.trainIn3 : f.in[i]);
                trainOut.push_back(f.label);
            } else {
                for (u32 i = 0; i < net::kInputs; ++i) testIn.push_back(f.in[i]);
                testLeaky.push_back(leaky);
            }
        }
        check(eligibleCount == kTrainCases + kTestCases, "every synthetic case is touched and eligible (" + std::to_string(eligibleCount) + ")");
        f64 cleanLabel = 0.0, leakyLabel = 0.0;
        for (u32 k = 0; k < kTrainCases; ++k) (k & 1u ? leakyLabel : cleanLabel) += trainOut[k];
        cleanLabel /= kTrainCases / 2; leakyLabel /= kTrainCases / 2;
        check(cleanLabel > 0.9 && leakyLabel < 0.4, "the labels separate: clean " + std::to_string(cleanLabel) + ", leaky " + std::to_string(leakyLabel));

        const nn::MlpDesc d = net::netDesc();
        nn::MlpReference m(d, net::netOptimiser());
        m.setWeights(net::identityWeights(d));
        auto meanTrust = [&](bool leaky) {
            f64 sum = 0.0;
            u32 n = 0;
            for (u32 i = 0; i < kTestCases; ++i) {
                if (testLeaky[i] != leaky) continue;
                f32 out[1];
                m.forward(std::span<const f32>(testIn.data() + i * net::kInputs, net::kInputs), out, true);
                sum += net::trustOf(out[0]);
                ++n;
            }
            return sum / n;
        };
        const f64 cleanBefore = meanTrust(false), leakyBefore = meanTrust(true);
        check(approx(cleanBefore, 1.0, 1e-6) && approx(leakyBefore, 1.0, 1e-6), "before training every held-out cell has trust 1");

        constexpr u32 kBatch = 128, kSteps = 1500;
        static_assert(kBatch <= net::kMaxTrain, "a test batch is below the training cap");
        check(kBatch <= nn::safeBatchLimit(net::netOptimiser()), "the training batch is within safeBatchLimit");
        f32 firstLoss = 0.0f, lastLoss = 0.0f;
        for (u32 s = 0; s < kSteps; ++s) {
            const u32 off = (s * kBatch) % kTrainCases;
            const f32 loss = m.trainBatch(std::span<const f32>(trainIn.data() + off * net::kInputs, kBatch * net::kInputs),
                                          std::span<const f32>(trainOut.data() + off, kBatch), kBatch);
            if (s == 0) firstLoss = loss;
            lastLoss = loss;
        }
        const f64 cleanAfter = meanTrust(false), leakyAfter = meanTrust(true);
        AVER_INFO("  loss {:.4f} -> {:.4f}; held-out mean trust: clean {:.3f}, leaky {:.3f}", firstLoss, lastLoss, cleanAfter, leakyAfter);
        check(lastLoss < 0.25f * firstLoss, "the training loss falls to a quarter or less");
        check(cleanAfter > 0.95, "after training, clean held-out cells keep a trust near 1 (" + std::to_string(cleanAfter) + ")");
        check(leakyAfter < 0.5, "leaky held-out cells fall below 0.5 (" + std::to_string(leakyAfter) + ")");
        check(cleanAfter - leakyAfter > 0.4, "the margin between clean and leaky mean trust is above 0.4 (" + std::to_string(cleanAfter - leakyAfter) + ")");
    }

    if (g_failures == 0) {
        AVER_INFO("=== all {} NeuRaC net checks passed ===", g_checks);
        return 0;
    }
    AVER_ERROR("=== {} FAILED ===", g_failures);
    return 1;
}
