// Nrd2ResolveReference -- CPU twin of NRD2's fixed maths and of the phase 3 oracle fit: the pyramid
// (CSNrd2Pyramid), the resolve forward and backward (nrd2_resolve.hlsli), the temporal stage's per-pixel
// maths (anchor, prefilter weight, blend) and the per-tile fit (nrd2_capture.hlsl). The spec the shaders follow and what
// Nrd2ResolveTest checks.
//
// KEEP IN SYNC WITH nrd2.hlsl / nrd2_resolve.hlsli / nrd2_capture.hlsl.
//
// Frames here are one viewport at origin 0 (render target == viewport). Patent rule 4: the oracle
// scores only free per-tile parameters through the filter; no model is involved.
#pragma once

#include <aver/core/Types.hpp>

#include <array>
#include <vector>

namespace aver::render::denoise {

// What Stage B writes for one frame (texel layout of the GPU targets, RGBA floats per pixel).
struct Nrd2Frame {
    u32 width = 0, height = 0;
    std::vector<f32> d;        // rgb demodulated diffuse, a = albedo usable (0/1)
    std::vector<f32> s;        // rgb demodulated specular, a = hit distance (cm; 0 none, -1 unlit)
    std::vector<f32> viewZ;    // cm, one per pixel; outside (0, 1e6) = no surface
    std::vector<f32> normal;   // averPackNormalRoughness: xy octahedral, z roughness, w unused
};

// Levels 1/2, 1/4, 1/8 (index 0..2), RGBA texels. guide: xyz averaged normal, w view Z (m);
// value[signal]: rgb, a validity fraction.
struct Nrd2Pyramid {
    u32 width[3] = {}, height[3] = {};
    std::vector<f32> guide[3];
    std::vector<f32> value[2][3];
};

// halfStorage rounds every stored texel through fp16 (the GPU levels are RGBA16F); the in-group
// chain stays fp32 like the shader's groupshared.
void nrd2BuildPyramid(const Nrd2Frame& f, Nrd2Pyramid& out, bool halfStorage = true);

// Defaults (Nrd2Params) for signal 0 (D) or 1 (S).
std::array<f32, 6> nrd2DefaultTheta(u32 signal);

// The resolve of one pixel and signal with tile parameters theta (sanitised like the shader, defaults
// as the fallback). dOut, when given, receives d(output)/d(theta_k) for k = 0..5.
std::array<f32, 3> nrd2ResolvePixel(const Nrd2Frame& f, const Nrd2Pyramid& p, u32 x, u32 y, u32 signal,
                                    const f32 theta[6], f32 (*dOut)[3] = nullptr);

// ---- temporal stage (nrd2_resolve.hlsli: nrd2StabMaxFrames ... nrd2TemporalBlend) ----

// Frames of history allowed at this screen speed (px per frame): nStill at rest, min(nFast, nStill) from
// 8 px, none from 128 px (or for a non-finite speed).
f32 nrd2StabMaxFrames(f32 speed, f32 nStill, f32 nFast);
// Specular's cap: min(nMax, max(8, nMax * (1 - exp(-100 roughness)))); 0 stays 0.
f32 nrd2StabSpecMaxFrames(f32 nMax, f32 roughness);
// The sample count after this frame: min(nMax, prevCount + 1), or 1 when there is no accepted history.
f32 nrd2StabSamples(f32 prevCount, f32 nMax);
// Weight of the history after n samples: 1 - 1/n (0 at n = 1).
f32 nrd2StabBlend(f32 n);
// Firefly clamp (nrd2StabFirefly): c scaled down to 2x the brightest neighbour's luminance when above it.
void nrd2StabFirefly(f32 c[3], f32 neighbourMaxLum);
// Squared relative luminance change ((a - b) / max(a, b, scale / 2))^2, the per-pixel noise estimate.
f32 nrd2StabRelVar(f32 lumA, f32 lumB, f32 scale);
// Per-channel min/max over the given rgb values (this frame's neighbourhood box).
void nrd2StabBox(const std::vector<std::array<f32, 3>>& values, f32 lo[3], f32 hi[3]);

// A pixel's weight in its 8x8 tile's anchor: max(exp(-0.3 lum / tileScale), 0.01).
f32 nrd2AnchorWeight(f32 lum, f32 tileScale);
// The tile's anchor of the given (valid) pixel values, as CSNrd2Reproject reduces it: weights from each value's
// luminance against the tile's plain mean luminance. out = the weighted mean rgb.
void nrd2TileAnchor(const std::vector<std::array<f32, 3>>& values, f32 out[3]);

// The prefilter's weight of one neighbour: pow(max(cosN, 0), 512) * exp(-30 dzRel) * max(exp(-(0.6 + 0.1 var)
// radDiffRel), 0.01) * max(0.1, 1 - exp(-4.4 var)). dzRel = depth error relative to the pixel's view depth,
// radDiffRel = |anchor - value| relative to the pixel's luminance scale.
f32 nrd2PrefilterWeight(f32 cosN, f32 dzRel, f32 radDiffRel, f32 variance);
f32 nrd2PrefilterCentreWeight(f32 radDiffRel, f32 variance);

struct Nrd2TemporalOut {
    f32 out[3] = {};    // the blended value
    f32 cur[3] = {};    // the clipped current value
    f32 hist[3] = {};   // the clipped history
};
// nrd2TemporalBlend: cur pulled toward the anchor by 1/(n + 1), clipped to [min(lo, anchor), max(hi, anchor)];
// the history clipped to the same box; out = lerp(cur, history, 1 - 1/n). lo/hi are this frame's 5x5 min/max.
Nrd2TemporalOut nrd2TemporalBlend(const f32 cur[3], const f32 anchor[3], const f32 lo[3], const f32 hi[3],
                                  const f32 hist[3], f32 n);

// ---- FidelityFX history handling (nrd2_resolve.hlsli: kNrd2Ffx* ... nrd2GuidedAnchor) ----
// DESIGN.md (docs/plans/neurac_nrd2) section 2. The pass-5 pieces with no HLSL helper (moments, search, rebuild,
// the history fetch) mirror the inline code of CSNrd2Reproject / CSNrd2Temporal. The constants below are checked
// against the shader's literals by Nrd2ResolveTest.

inline constexpr f32 kNrd2FfxThreshold = 0.9f;   // a history texel is kept at or above this factor
inline constexpr f32 kNrd2FfxNormalW   = 1.4f;
inline constexpr f32 kNrd2FfxDepthW    = 1.0f;
inline constexpr f32 kNrd2FfxRejectW   = 1.5f;
inline constexpr f32 kNrd2FfxClipK     = 1.4f;
inline constexpr f32 kNrd2FfxMeanMix   = 0.2f;
inline constexpr f32 kNrd2FfxGaussK    = 3.0f;
inline constexpr i32 kNrd2FfxMomentRadius = 4;   // the moments' window is 9x9

// FFX GetDisocclusionFactor: exp(-|1 - max(0, cosN)| normalW) * exp(-|zHist - zExp| / max(zExp, 1e-4) depthW).
f32 nrd2FfxFactor(f32 cosN, f32 zHist, f32 zExp, f32 normalW, f32 depthW);
// exp(-3 i^2 / 25), the moment window's 1D Gaussian weight at offset i (|i| <= 4).
f32 nrd2MomentWeight(i32 i);
// Catmull-Rom weights of the taps at -1, 0, 1, 2 for fraction f in [0, 1].
void nrd2CatmullRom(f32 f, f32 w[4]);
// FFX ClipAABB: v pulled toward the box centre along the line to it (not per channel) when outside the box.
std::array<f32, 3> nrd2ClipAabb(const f32 lo[3], const f32 hi[3], const f32 v[3]);
// True = discard the history: !(|hist - mean|^2 < w |var|). A NaN rejects.
bool nrd2FfxReject(const f32 hist[3], const f32 mean[3], const f32 var[3], f32 w);

// nrd2TemporalBlendFfx: the mean +- sigma clip. avg = the tile anchor (guided or not), mean/var = this frame's
// 9x9 moments.
Nrd2TemporalOut nrd2TemporalBlendFfx(const f32 cur[3], const f32 avg[3], const f32 mean[3], const f32 var[3],
                                     const f32 hist[3], f32 n, f32 clipW);

// The guided anchor (DESIGN.md section 3.1): clamp(A + c k s (g - gbar), A / 4, 4 A) with
// s = min(1, lum(A) / lum(gbar)). Returns A bit-for-bit when c <= 0, gbarValid <= 0, strength <= 0, A has no
// luminance or g/gbar is NaN.
std::array<f32, 3> nrd2GuidedAnchor(const f32 A[3], const f32 g[3], f32 c, const f32 gbar[3], f32 gbarValid,
                                    f32 strength);

// Weighted mean and variance over a 9x9 patch (row-major, 81 entries), the window being the separable
// Gaussian w(i) w(j). valid[k] weights each pixel (0 = no surface, 1 = usable). Same order as the shader:
// rows first (sum w(i) v x, sum w(i) v, sum w(i) v x^2), then the column pass. var = |E[x^2] - mean^2|.
// weight is the total window weight, 0 (and mean = var = 0) when nothing is valid.
struct Nrd2Moments {
    f32 mean[3] = {};
    f32 var[3] = {};
    f32 weight = 0.0f;
};
Nrd2Moments nrd2Moments(const std::vector<std::array<f32, 3>>& values, const std::vector<f32>& valid);

// The 3x3 closest-history search (row-major, index 4 = the nearest texel). Invalid texels carry factor 0.
// Returns the index of the highest factor; a strictly greater factor must beat the running best, so the
// centre wins ties and the first of equal neighbours wins among them.
u32 nrd2FfxSearch(const f32 factors[9]);

// One of the 2x2 core history texels around the fetch position, in the order of nrd2LoadTaps (index
// i: x = i & 1, y = i >> 1).
struct Nrd2HistTap {
    bool ok = false;         // inside the viewport with a history surface
    f32 factor = 0.0f;       // nrd2FfxFactor at this texel
    f32 nrm[3] = {};         // its decoded normal
    f32 z = 0.0f;            // its view depth (m)
    f32 colour[3] = {};      // the signal being fetched
};
struct Nrd2Rebuild {
    f32 factor = 0.0f;       // the factor of the blended texel, 0 when it fails the threshold
    f32 w[4] = {};           // the normalised weights (hard validity x bilinear), 0 when no tap is valid
    f32 colour[3] = {};      // sum w_i colour_i
};
// The 2x2 validity-weighted rebuild (a disoccluded pixel's chance to find history at its edge): tap i counts when
// ok && factor > thr / 2, with weight b_i from the bilinear fraction (fx, fy); weights renormalise by
// max(sum, 1e-3). The normal and depth blend unnormalised (literal FFX), the factor is recomputed from them
// against n / zExp, and below thr it becomes 0.
Nrd2Rebuild nrd2FfxRebuild(const Nrd2HistTap taps[4], f32 fx, f32 fy, const f32 n[3], f32 zExp, f32 normalW,
                           f32 depthW, f32 thr);

struct Nrd2HistFetch {
    bool ok = false;         // false: no valid core tap, so no history
    bool catmull = false;    // the Catmull-Rom path ran (all four core taps valid and sharp > 0)
    f32 colour[3] = {};
};
// The history colour at texel fraction (fx, fy) from a 4x4 patch (taps[j * 4 + i], i/j = 0..3, the core 2x2 at
// i, j = 1..2) when the nearest texel passed the threshold. validMask bit (j * 4 + i) = that tap's factor
// exceeds thr / 2. Core taps all valid and sharp > 0: the 12-tap Catmull-Rom (the four corners dropped, the
// weights renormalised over the valid taps, clamped to their min/max, floored at 0) mixed with the
// validity-weighted bilinear by sharp. Otherwise the validity-weighted bilinear alone.
Nrd2HistFetch nrd2HistFetch(const f32 taps[16][3], u32 validMask, f32 fx, f32 fy, f32 sharp);

// ---- oracle fit (one 8x8 tile, one signal) ----

enum class Nrd2OracleMode : u32 { Grad = 0, Grid = 1 };

struct Nrd2OracleDesc {
    Nrd2OracleMode mode = Nrd2OracleMode::Grad;
    u32 adamIters = 150;      // Grad: Adam steps after the grid
    u32 patternIters = 24;    // Grid: derivative-free pattern-search rounds after the grid
    f32 lr = 0.05f, beta1 = 0.9f, beta2 = 0.99f, adamEps = 1e-8f;
    f32 lambda = 1e-3f;       // lambda * ||theta - theta0||^2
};

inline constexpr u32 kNrd2GridStarts        = 27;   // 3 logit offsets x 3 depth x 3 luminance
inline constexpr u32 kNrd2GridDefault       = 13;   // the all-zero offset: theta0 itself
inline constexpr u32 kNrd2PatternCandidates = 12;   // +-step on each of the six parameters
inline constexpr f32 kNrd2PatternStep0      = 1.0f;
inline constexpr f32 kNrd2LossEps           = 1e-8f;
inline constexpr f32 kNrd2SplitHalfR0       = 0.1f;

void nrd2ClampTheta(f32 t[6]);
void nrd2GridCandidate(const f32 theta0[6], u32 c, f32 out[6]);
void nrd2PatternCandidate(const f32 theta[6], f32 step, u32 c, f32 out[6]);
// validity x split-half confidence; r = rms(lum(even mean) - lum(odd mean)) / (mean lum + 1e-6).
f32 nrd2TileWeight(f32 validFraction, f32 r);

// K frames of one pose and the target per frame (rgb per pixel). The capture uses one converged mean
// for every frame; tests may give each frame its own target.
struct Nrd2TileProblem {
    const std::vector<Nrd2Frame>* frames = nullptr;
    const std::vector<Nrd2Pyramid>* pyramids = nullptr;
    const std::vector<std::vector<f32>>* targets = nullptr;
    u32 tileX = 0, tileY = 0, signal = 0;
};

// Data loss: sum over frames, tile pixels and rgb of (resolve - target)^2 / (mean lum(target)^2 + eps),
// over K * count. Pixels count when they have a surface (and for D an albedo in frame 0). grad, when
// given, is d(loss)/d(theta). count 0 returns 0.
f32 nrd2TileLoss(const Nrd2TileProblem& p, const f32 theta[6], f32 grad[6] = nullptr, u32* count = nullptr);

struct Nrd2TileFit {
    f32 theta[6] = {};
    f32 loss = 0.0f, defaultLoss = 0.0f;   // data loss at theta and at theta0
    u32 count = 0;
};
// Grid of 27 starts around theta0, then Adam (Grad) or pattern search (Grid) on loss + lambda * reg;
// keeps the best objective seen. The GPU fit runs the same sequence.
Nrd2TileFit nrd2FitTile(const Nrd2TileProblem& p, const f32 theta0[6], const Nrd2OracleDesc& d);

}  // namespace aver::render::denoise
