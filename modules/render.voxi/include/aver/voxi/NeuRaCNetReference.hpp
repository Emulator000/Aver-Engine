// NeuRaCNetReference -- the CPU twin of voxi_neurac_net.hlsl and the spec NeuRaCNet follows (DESIGN.md section 5).
// Feature builder, label, eligibility, hash thinning, trust mapping and the network description, all RHI-free.
// Must stay synchronized with voxi_neurac_net.hlsl: NeuRaCNetTest holds the #defines to these constants and
// NeuRaCGpuTest holds the kernels to buildForCell on a synthetic cell buffer.
#pragma once

#include "aver/core/Types.hpp"
#include "aver/voxi/NeuRaCLayout.hpp"

#include <aver/render/neural/MlpReference.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace aver::voxi::neurac::net {

// ---------------------------------------------------------------- constants (voxi_neurac_net.hlsl RCN_*)
inline constexpr u32 kInputs      = 10;
inline constexpr u32 kMaxInfer    = 131072;   // RCN_MAX_INFER
inline constexpr u32 kMaxTrain    = 16383;    // RCN_MAX_TRAIN: the Mlp's safeBatchLimit at gradClamp 2
inline constexpr u32 kInferTarget = 98304;    // RCN_INFER_TARGET
inline constexpr u32 kTrainTarget = 12288;    // RCN_TRAIN_TARGET
inline constexpr u32 kMinFresh    = 8;        // RCN_MIN_FRESH
inline constexpr u32 kMinNEff     = 2;        // RCN_MIN_NEFF
inline constexpr f32 kEps         = 0.001f;   // RCN_EPS
inline constexpr f32 kRelSlack    = 0.05f;    // RCN_REL_SLACK
inline constexpr f32 kVarEps      = 0.000001f;   // RCN_VAR_EPS
inline constexpr f32 kZFree       = 2.0f;     // RCN_Z_FREE
inline constexpr f32 kZSpan       = 3.0f;     // RCN_Z_SPAN
inline constexpr f32 kTrustDivisor = 0.85f;   // RCN_TRUST_DIV
inline constexpr f32 kStatScale   = 16384.0f; // RCN_STAT_SCALE
inline constexpr u32 kStatCount   = 8;        // RCN_STATS
inline constexpr f32 kInitTrust   = 0.9f;     // the identity init's prediction
inline constexpr u32 kWarmupFrames = 900;     // warmStrength ramps over this many frames
inline constexpr u32 kLogEveryFrames = 120;

// Salts of the two hash thinnings.
inline constexpr u32 kSaltInfer = 0;
inline constexpr u32 kSaltTrain = 1;

// ---------------------------------------------------------------- the network
inline render::neural::MlpDesc netDesc() {
    render::neural::MlpDesc d;
    d.inputs = kInputs; d.outputs = 1; d.hiddenWidth = 8; d.hiddenLayers = 2;
    d.hidden = render::neural::Activation::ReLU;
    d.output = render::neural::Activation::Sigmoid;
    d.bias = true;
    d.seed = 0x4E52434Eu;
    return d;
}
inline render::neural::OptimiserDesc netOptimiser() {
    render::neural::OptimiserDesc o;
    o.learningRate = 2e-3f;
    o.weightEma = 0.99f;
    o.gradClamp = 2.0f;
    o.loss = render::neural::Loss::L2;
    return o;
}
// He-uniform hidden layers, then the last layer's W zeroed and its bias set to logit(kInitTrust): the net starts
// as the constant 0.9, so every trust is 1 and stage 2 begins as stage 1.
inline std::vector<f32> identityWeights(const render::neural::MlpDesc& d) {
    std::vector<f32> w = render::neural::initWeights(d);
    const render::neural::MlpLayout lay = render::neural::MlpLayout::make(d);
    const u32 last = lay.layers - 1;
    for (u32 i = 0; i < lay.inDim[last] * lay.outDim[last]; ++i) w[lay.wOffset[last] + i] = 0.0f;
    w[lay.bOffset[last]] = std::log(kInitTrust / (1.0f - kInitTrust));
    return w;
}

// ---------------------------------------------------------------- hash thinning (rcnHash16 / rcnRate / rcnKeep)
inline u32 hash16(u32 a, u32 b, u32 salt) {
    u32 h = 0x9E3779B9u * (salt + 1u);
    h = (h ^ a) * 0x85EBCA6Bu;
    h = (h ^ b) * 0xC2B2AE35u;
    h ^= h >> 16;  h *= 0x85EBCA6Bu;
    h ^= h >> 13;  h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h >> 16;
}
// Keep fraction that holds a frame's expected record count at `target` given last frame's raw total.
inline f32 thinRate(u32 prev, u32 target) {
    return prev <= target ? 1.0f : static_cast<f32>(target) / static_cast<f32>(prev);
}
inline bool thinKeep(f32 rate, u32 cell, u32 frame, u32 salt) {
    return rate >= 1.0f || hash16(cell, frame, salt) < static_cast<u32>(rate * 65536.0f);
}

// ---------------------------------------------------------------- label, eligibility, trust
inline f32 lum(f32 r, f32 g, f32 b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

// How far the frame's mean moved the cell's own light, in standard errors of that mean: 1 up to kZFree of them
// (a perfect cell scores 1 however noisy its fresh samples), 0 from kZFree + kZSpan. varMean = variance of Ef.
inline f32 label(f32 Eo, f32 Ef, f32 varMean) {
    const f32 z = std::fabs(Ef - Eo) / std::sqrt(varMean + (kRelSlack * Eo) * (kRelSlack * Eo) + kVarEps);
    const f32 l = 1.0f - std::max(z - kZFree, 0.0f) / kZSpan;
    return l < 0.0f ? 0.0f : (l > 1.0f ? 1.0f : l);
}
// Variance of the fresh mean from the mean of squares msq = sumSq / n and the mean Ef (unbiased, / (n - 1)).
inline f32 meanVariance(f32 msq, f32 Ef, u32 n) {
    return std::max(msq - Ef * Ef, 0.0f) / static_cast<f32>(std::max(n, 2u) - 1u);
}
// age < kAgeMax and the tag are the touched test (cellValid); this is the rest.
inline bool eligible(u32 nFresh, u32 nEff, f32 Eo, f32 Ef) {
    return nFresh >= kMinFresh && nEff >= kMinNEff && Eo + Ef > kEps;
}
// Prediction -> the 6-bit trust the cell stores. False: not a usable prediction (the old trust stays).
inline bool trustUsable(f32 pred) { return std::isfinite(pred); }
inline f32 trustOf(f32 pred) {
    const f32 t = pred / kTrustDivisor;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}
inline u32 trustBits(f32 pred) { return static_cast<u32>(trustOf(pred) * static_cast<f32>(kTrustMax) + 0.5f); }
// The normal word with its trust field replaced and the direction and length bits kept.
inline u32 withTrust(u32 normalWord, u32 trust6) { return (normalWord & 0x03FFFFFFu) | ((trust6 > kTrustMax ? kTrustMax : trust6) << 26); }

// ---------------------------------------------------------------- features
struct Features {
    bool touched  = false;   // took samples this frame and holds a valid old state: gets an inference record
    bool eligible = false;   // and may train
    u32  nFresh = 0;         // this frame's accepted samples, capped
    f32  in[kInputs] = {};   // the inference and training record (input 3 = 1)
    f32  Eo = 0.0f, Ef = 0.0f, label = 0.0f;   // Eo, Ef: DC irradiance / PI luminance of the old state and the fresh mean
    f32  varMean = 0.0f;     // sampling variance of Ef
};

// Face-neighbour order of the kernel: +x, -x, +y, -y, +z, -z.
inline constexpr i32 kNeighbour[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

// Cascade, and the world cell its texel holds in this frame's window.
inline void cellWorld(u32 cell, const RcInfo& info, u32& cascade, i32 world[3]) {
    cascade = cell / kCellsPerCascade;
    const u32 local = cell - cascade * kCellsPerCascade;
    const i32 texel[3] = {static_cast<i32>(local & 63u), static_cast<i32>((local >> 6) & 63u), static_cast<i32>(local >> 12)};
    for (int a = 0; a < 3; ++a) {
        const i32 o = info.cas[cascade].originCell[a];
        world[a] = o + ((texel[a] - o) & 63);
    }
}

// Cosine-convolved irradiance / PI of a 12-float SH (k = 3 j + ch) along n, as luminance.
inline f32 irradianceLum(const f32 sh[kShFloats], const f32 n[3]) {
    f32 c[4][3];
    for (int j = 0; j < 4; ++j) for (int ch = 0; ch < 3; ++ch) c[j][ch] = sh[3 * j + ch];
    f32 e[3];
    shIrradianceOverPi(c, n, e);
    return lum(e[0], e[1], e[2]);
}

// The kernel's work on one cell, from the cell images before this frame's resolve. `nbWorld`-addressed neighbours are
// read from `cells` through the toroidal index, so a neighbour outside the window fails its tag test, as on the GPU.
// `accum` is the accumulator image (counts at [0, kCells), payload from kPayloadBase, sums of squares from kSqBase).
// Thinning is not applied here.
inline Features buildForCell(const RcCell* cells, const i32* accum, const RcInfo& info, u32 cell) {
    Features out;
    if (info.flags & kFlagClearAll) return out;   // the resolve clears every cell this frame
    u32 cascade;
    i32 world[3];
    cellWorld(cell, info, cascade, world);
    const u32 n = std::min(static_cast<u32>(accum[cell]) & 0xFFFFu, capForCascade(info, cascade));
    if (n == 0) return out;
    const u32 tag = tagPack(world[0], world[1], world[2]);
    const RcCell& stored = cells[cell];
    if (!cellValid(stored.b[3], tag)) return out;
    out.touched = true;
    out.nFresh = n;

    const u32 nEff = metaNEff(stored.b[3]), age = metaAge(stored.b[3]);
    f32 nu[3], len;
    unpackNormal(stored.b[2], nu, len);
    f32 old[kShFloats];
    const u32 words[6] = {stored.a[0], stored.a[1], stored.a[2], stored.a[3], stored.b[0], stored.b[1]};
    unpackSh(words, old);

    const u32 pb = kPayloadBase + cell * kPayloadInts;
    const f32 shInv = 1.0f / (static_cast<f32>(n) * static_cast<f32>(kShScale));
    f32 fresh[kShFloats];
    for (u32 k = 0; k < kShFloats; ++k) fresh[k] = static_cast<f32>(accum[pb + k]) * shInv;

    // The label's scalar is the DC term of irradiance / PI: the mean of the scatter's per-sample scalar, so Ef and its
    // variance come from the same samples. The neighbour ratio below compares full irradiance.
    out.Eo = lum(0.282095f * old[0], 0.282095f * old[1], 0.282095f * old[2]);
    out.Ef = lum(0.282095f * fresh[0], 0.282095f * fresh[1], 0.282095f * fresh[2]);
    const f32 eoFull = irradianceLum(old, nu);
    const f32 msq = static_cast<f32>(accum[kSqBase + cell]) / (static_cast<f32>(n) * static_cast<f32>(kSqScale));
    out.varMean = meanVariance(msq, out.Ef, n);

    f32 sumW = 0.0f, sumWE = 0.0f, wk[6], ek[6];
    for (int k = 0; k < 6; ++k) {
        wk[k] = 0.0f; ek[k] = 0.0f;
        const i32 nw[3] = {world[0] + kNeighbour[k][0], world[1] + kNeighbour[k][1], world[2] + kNeighbour[k][2]};
        const RcCell& nb = cells[cellIndex(cascade, nw[0], nw[1], nw[2])];
        if (!cellValid(nb.b[3], tagPack(nw[0], nw[1], nw[2]))) continue;
        f32 nn[3], nl;
        unpackNormal(nb.b[2], nn, nl);
        const f32 d = 2.0f * (nn[0] * nu[0] + nn[1] * nu[1] + nn[2] * nu[2]);
        const f32 w = (d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d)) * nl;
        if (!(w > 0.0f)) continue;
        const u32 nbWords[6] = {nb.a[0], nb.a[1], nb.a[2], nb.a[3], nb.b[0], nb.b[1]};
        f32 nbSh[kShFloats];
        unpackSh(nbWords, nbSh);
        wk[k] = w;
        ek[k] = irradianceLum(nbSh, nu);
        sumW += w;
        sumWE += w * ek[k];
    }
    f32 disagree = 0.0f, ratio = 0.0f;
    if (sumW > 1.0e-6f) {
        const f32 ebar = sumWE / sumW;
        f32 dev = 0.0f;
        for (int k = 0; k < 6; ++k) dev += wk[k] * std::fabs(ek[k] - ebar);
        disagree = dev / (sumW * (ebar + kEps));
        ratio = std::clamp(std::log2((ebar + kEps) / (eoFull + kEps)), -4.0f, 4.0f) * 0.25f;
    }
    const f32 l1 = lum(old[3], old[4], old[5]), l2 = lum(old[6], old[7], old[8]), l3 = lum(old[9], old[10], old[11]);
    const f32 aniso = std::sqrt(l1 * l1 + l2 * l2 + l3 * l3) / std::max(lum(old[0], old[1], old[2]), kEps);

    out.in[0] = len;
    out.in[1] = static_cast<f32>(nEff) / 15.0f;
    out.in[2] = static_cast<f32>(age) / 15.0f;
    out.in[3] = 1.0f;
    out.in[4] = sumW / 6.0f;
    out.in[5] = std::min(disagree, 2.0f) * 0.5f;
    out.in[6] = ratio;
    out.in[7] = std::min(aniso, 2.0f) * 0.5f;
    out.in[8] = nu[1];
    out.in[9] = static_cast<f32>(cascade) * 0.5f;
    out.label = label(out.Eo, out.Ef, out.varMean);
    out.eligible = eligible(n, nEff, out.Eo, out.Ef);
    return out;
}

}  // namespace aver::voxi::neurac::net
