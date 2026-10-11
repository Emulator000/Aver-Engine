# NeuRaC + NRD2: frozen design (owner-approved 2026-10-11)

FROZEN CONTRACT for the implementer agents. Owner decisions 2026-10-11: NRD2 takes FidelityFX's history
handling including the literal mean +- sigma clip ON by default (owner accepts US 10,116,916); the FFX colour
reject is included, default ON; NeuRaC feeds NRD2 as the guided anchor ("fill new pixels"), flagged for counsel;
NeuRaC stage 1 is fixed and stage 2a (trust network) is built; 2b is a follow-up. Section 0 below was approved
as written. Function names, struct fields, slots and settings here are binding across tasks.

I read the code and did not edit, build or run anything. This is the plan as text. Anything I could not confirm in code is marked **UNVERIFIED** and collected in section 11.

All edits go to the main tree. `.claude/worktrees/opt-base060` is a stale copy and must not be touched.

## 0. Decisions approved by the owner (2026-10-11)

1. **Colour-distance history reject is its own setting, default ON.**
   - Setting: `voxi.nrd2FfxReject`.
   - It is FFX's `dot2(hist - mean) < 1.5 * length(variance)` test (reproject.h:180-204).
   - It is a mean/variance test inside history rejection, so it also touches the moment-based patents US 12,482,168 and US 11,600,036. These are named in rule 7, not only US 10,116,916.
   - It runs per signal, so D can be rejected while S keeps its history.
   - FFX itself rejects only one signal.
2. **The mean ± sigma clip is ON by default.**
   - Setting: `voxi.nrd2FfxClip`.
   - The clip weight defaults to 4, which is Aver's FidelityFX weight, so the box is ±5.6 sigma and loose.
   - Turning the setting off restores the existing min/max path.
3. **FFX depth weight 1.0 is loose.**
   - The disocclusion factor stays at the 0.9 threshold if relative depth error is under about 10%.
   - Today's gate is 2% + 1 cm + slope.
   - I use FFX's constants literally, but I measure depth against the camera-motion-predicted depth `zExp`, not the current depth. That is the one deviation from literal FFX.
   - I expose `voxi.nrd2FfxDepthWeight` so you can tighten it in the editor without a rebuild.
4. **No legacy toggle for the old reproject/edge handling.**
   - The FFX edge handling replaces it.
   - If you want an A/B switch for edge handling, say so. It costs a second code path in pass 5.
5. **Catmull-Rom history fetch** is Aver's addition on top of FFX, which uses plain bilinear. The setting is `voxi.nrd2HistSharp`, default 1.
6. **The F2 checkerboard fix doubles the steady-state F2 trace and scatter work.**
   - Defect (b) today leaves F2 training on 2 of 4 frames and only even-x pixels. The fix restores the intended quarter-rate on every frame.
   - It is the right fix, but it costs GPU time in the "Voxi RD GI stage". Measure it.
7. **Defect (g), primary-surface coverage, is mitigated, not fixed.**
   - Training the primary-surface cell from the F1 hit radiance would mix lamp-lit radiance into a cache that holds lamp-free L0 light.
   - On non-traced pixels it would also feed cache reads back into training.
   - Mitigations are the scatter offset (c), coarse-cascade training (d) and the new guide coverage counter.
8. **Stage 2 runs only when Path Tracing is off.**
   - PT paths read the cache during traced continuations, so their samples are self-referential.
   - This follows the US 11,610,360 flag.
9. **2b is a follow-up, not in this plan.**
   - Reasons are in section 5.8.
10. **Stage 2 defaults ON** (`voxi.neuracNet 1`) and warms in over about 900 frames. Its benefit is UNVERIFIED and can only be judged in the editor, using the visualiser trust view and `voxi.ptMode 1` as the reference.

---

## 1. Phases and dependencies

| Phase | Content | Depends on |
|---|---|---|
| A | NRD2 FFX history handling: edge handling, Catmull-Rom fetch, moments clip, colour reject, maths, CPU twin, tests, settings | none |
| B | NeuRaC stage 1 fixes (a)-(f), (i), cell repack with a trust field, accumulator re-layout, lookup/guide shared header | none |
| C | Guide producer pass and the NRD2 guide plumbing (the guided anchor itself is in phase A's passes) | B (shared lookup header, cell packing) |
| D | Stage 2: trust network, its passes, CPU reference, tests | B (cell packing, trust bits), C is not required |
| E | Docs, patent notes, final cleanup | A-D |

- Phases A and B are independent and run as wave 1 in parallel.
- Phase C is also built in wave 1, against contracts given in section 9.
- Phase D and the docs are wave 2.

## 2. Phase A: NRD2 temporal stage with FFX history handling

### 2.1 Settings

All settings are console-only: `Voxi.hpp` field, `Voxi.cpp` clamp, `EditorConsole.hpp` entry, `VoxiRenderer.cpp:5925` fill into `Nrd2Params`.

| Console name | Type | Default | Range | Meaning |
|---|---|---|---|---|
| `voxi.nrd2FfxClip` | bool | 1 | | 1: FFX mean ± sigma history clip. 0: existing 5x5 min/max box |
| `voxi.nrd2FfxClipWeight` | f32 | 4.0 | 0.1-8 | `history_clip_weight`, same default as Aver's FidelityFX |
| `voxi.nrd2FfxReject` | bool | 1 | | FFX colour-distance reject |
| `voxi.nrd2FfxRejectWeight` | f32 | 1.5 | 0.1-16 | FFX discard weight |
| `voxi.nrd2FfxDepthWeight` | f32 | 1.0 | 0-64 | Depth weight in the disocclusion factor |
| `voxi.nrd2HistSharp` | f32 | 1.0 | 0-1 | Catmull-Rom share of the fetch. 0 is bilinear |
| `voxi.nrd2NeuracGuide` | bool | 1 | | Guide in the anchor, only when NeuRaC is live |
| `voxi.nrd2NeuracGuideStrength` | f32 | 1.0 | 0-1 | Scales the guide's deviation term |

- The threshold 0.9 and the normal weight 1.4 are `static const` in the HLSL and travel in the constants block.
- `Nrd2Params` gains `ffxClip, ffxClipWeight, ffxReject, ffxRejectWeight, ffxDepthWeight, histSharp, guideStrength`.
- `Nrd2::Inputs` gains `rhi::TextureHandle guide = 0`. It is RGBA16F, rests in `NonPixelShaderResource`, rgb is cached irradiance/π in D units, a is confidence in [0,1].

### 2.2 Constants, flags, bindings (Nrd2.cpp / nrd2.hlsl)

- Constants grow 352 to 384 bytes. Append `float4 gNrd2Ffx` after `gNrd2Vis` in both the HLSL cbuffer and the C++ `Constants`, then `float4 gNrd2Hist`. Update the `static_assert`.
  - `gNrd2Ffx = {0.9, 1.4, depthW, rejectW}`
  - `gNrd2Hist = {sharp, clipW, guideStrength, 0}`
- `gNrd2Tiles.z` flags: 32 = guide active, 64 = FFX clip, 128 = FFX reject.
- Slot counts:

| Pass | SRV | UAV |
|---|---|---|
| Reproject | 9 to 10 (t9 = guide) | 5 to 6 (u5 = `anchG_`, guide tile mean) |
| Temporal | 10 to 12 (t10 = guide, t11 = `anchG_`) | 5 |

- New resources in the Nrd2 class:
  - `anchG_`: RGBA16F at tile resolution, allocated in `allocStab`, released in `releaseStab`, added to the reproject barrier list.
  - `guideDummy_`: a 1x1 RGBA16F texture in `kRead`, created in `create`, destroyed in `destroy`, following the `visDummy_` precedent. It is bound whenever `in.guide` is 0.
- With the guide absent the flag stays 0 and the shader takes the exact no-guide path. That gives bit-identical output.
- Table 0 is untouched.

### 2.3 HLSL helper contract (nrd2_resolve.hlsli, owned by task W1-A)

The pass owner (W1-B) calls these by name. The CPU twin mirrors each one.

```hlsl
static const float kNrd2FfxThreshold = 0.9, kNrd2FfxNormalW = 1.4, kNrd2FfxDepthW = 1.0,
                   kNrd2FfxRejectW = 1.5, kNrd2FfxClipK = 1.4, kNrd2FfxMeanMix = 0.2,
                   kNrd2FfxGaussK = 3.0;          // moment weight(i) = exp(-K i^2 / 25), |i| <= 4

// FFX GetDisocclusionFactor; depth is vs the camera-motion-predicted zExp, not the current depth.
float nrd2FfxFactor(float cosN, float zHist, float zExp, float normalW, float depthW) {
    return exp(-abs(1.0 - max(0.0, cosN)) * normalW)
         * exp(-abs(zHist - zExp) / max(zExp, 1e-4) * depthW);
}
float nrd2MomentWeight(int i) { return exp(-kNrd2FfxGaussK * float(i * i) / 25.0); }

void nrd2CatmullRom(float f, out float w[4]) {   // identical to sr_taa.hlsl w0..w3
    w[0] = f * (-0.5 + f * (1.0 - 0.5 * f));
    w[1] = 1.0 + f * f * (-2.5 + 1.5 * f);
    w[2] = f * (0.5 + f * (2.0 - 1.5 * f));
    w[3] = f * f * (-0.5 + 0.5 * f);             // f = 0.5 gives -1/16, 9/16, 9/16, -1/16
}

float3 nrd2ClipAabb(float3 lo, float3 hi, float3 v) {   // FFX ClipAABB, toward the centre
    float3 c = 0.5 * (hi + lo), e = 0.5 * (hi - lo) + 0.001, d = v - c;
    float m = max(abs(d.x / e.x), max(abs(d.y / e.y), abs(d.z / e.z)));
    return m > 1.0 ? c + d / m : v;
}
bool nrd2FfxReject(float3 hist, float3 mean, float3 var, float w) {
    float3 d = hist - mean;
    return !(dot(d, d) < w * length(var));              // NaN rejects
}
// FFX resolve_temporal 119-136. outCur/outHist feed the carried noise estimate.
float3 nrd2TemporalBlendFfx(float3 cur, float3 avg, float3 mean, float3 var, float3 hist,
                            float n, float clipW, out float3 outCur, out float3 outHist) {
    float3 sd = (sqrt(var) + length(mean - avg)) * clipW * kNrd2FfxClipK;
    float3 m2 = lerp(mean, avg, kNrd2FfxMeanMix);
    outHist = nrd2ClipAabb(m2 - sd, m2 + sd, hist);
    outCur  = nrd2ClipAabb(avg - sd, avg + sd, lerp(cur, avg, 1.0 / max(n + 1.0, 1.0)));
    return lerp(outCur, outHist, nrd2StabBlend(n));
}
// Guided anchor, section 3.1.
float3 nrd2GuidedAnchor(float3 A, float3 g, float c, float3 gbar, float gbarValid, float strength);
```

### 2.4 CPU twin (Nrd2ResolveReference.hpp/.cpp)

Mirror these, with the same names plus the `nrd2` prefix:

- `nrd2FfxFactor`
- `nrd2MomentWeight`
- `nrd2CatmullRom`
- `nrd2ClipAabb`
- `nrd2FfxReject`
- `nrd2TemporalBlendFfx`
- `nrd2GuidedAnchor`
- `nrd2Moments(values, valid, ...)`: weighted mean and variance with validity weights
- `nrd2FfxSearch(factors[9])`: index of the best, centre wins ties
- `nrd2FfxRebuild(...)`: the 2x2 rebuild
- `nrd2HistFetch(taps4x4, validMask, fx, fy, sharp)`: Catmull-Rom with per-tap validity, including the fallback

`nrd2TemporalBlend`, the existing min/max path, stays unchanged and its tests stay.

### 2.5 Pass 5, CSNrd2Reproject (replaces nrd2GatherHistory and the slope/zTol code)

Per surface pixel:

```hlsl
hp = float2(p) - mv;  hb = floor(hp);  hf = hp - hb;       // texel-corner space, as now
tn = hb + int2(hf >= 0.5);                                 // nearest texel

struct Tex { bool ok; float factor; float3 nrm; float z; };
Tex LoadHistTexel(int2 tc) {     // outside the viewport or histD.a <= 0 -> ok = false
    hd = HistD.Load(tc); hg = HistG.Load(tc);
    t.nrm = nrd2DecodeNormal(float4(hg.yz, 0, 0)); t.z = hg.x;
    t.factor = nrd2FfxFactor(dot(n, t.nrm), hg.x, zExp, gNrd2Ffx.y, gNrd2Ffx.z);
}

// (1) factor at the nearest texel
f = t0.ok ? t0.factor : 0;  best = tn;

// (2) colour reject, per signal, skipped when flag 128 is off.
//     Uses raw 4-tap bilinear of HistD/HistS at hp (no validity), like FFX's SampleRadianceHistory.
rejD = nrd2FfxReject(rawD, meanD, varD, gNrd2Ffx.w);   rejS likewise;
//     meanD/varD/meanS/varS: 9x9 Gaussian moments of this frame's D'/S' (2.7).

// (3) 3x3 closest search, only if f < thr; strict '>' so the centre never replaces itself
if (f < thr) for o in 3x3 \ {0}: t = LoadHistTexel(tn+o); if (t.ok && t.factor > f) { f = t.factor; best = tn+o; }
base = hb + (best - tn);

// (4) fetch
core[4] = LoadHistTexel(base + {0,1}x{0,1}); valid_i = ok && factor > 0.5*thr;  b_i = bilinear(hf)
if (f < thr) {          // 2x2 validity-weighted rebuild
    w_i = valid_i * b_i;  w /= max(sum w, 1e-3);
    nrm = sum w_i*nrm_i;  zH = sum w_i*z_i;               // unnormalised blend, literal FFX
    f   = nrd2FfxFactor(dot(n, nrm), zH, zExp, ...);
    if (f < thr) f = 0;                                   // no history
    hist = sum w_i * core colours;
} else if (all four valid && gNrd2Hist.x > 0) {
    // 12-tap Catmull-Rom: 4x4 minus the four corners, taps at base + (i-1, j-1).
    // Per-tap validity = valid_t (factor > thr/2). Corner weights are dropped.
    wx[], wy[] = nrd2CatmullRom(hf.x / hf.y);
    cr = sum(w_ij*valid_t*c_t) / sum(w_ij*valid_t);       // DC-preserving renormalisation
    cr = clamp(cr, min over valid taps, max over valid taps);   // ringing bound
    cr = max(cr, 0);                                      // no negatives
    hist = lerp(bilinearValid, cr, gNrd2Hist.x);          // sharpness share
} else {
    hist = bilinearValid;      // some core tap invalid: validity-weighted bilinear
}
nHist = youngest count among valid core taps with b_i > kNrd2StabAgeWeight;   // as today
vPrev = sum(valid_i * b_i * V_i) / sum(valid_i * b_i);
nD = (f >= thr && !rejD) ? nrd2StabSamples(nHistD * f, nMaxD) : 1;           // FFX: n = n_hist * factor + 1
```

- `okD` and `okS` are per signal. The existing anchor mix (`lerp(curD, hD, 0.3)`) and the noise estimate use the per-signal flag.
- `HistG` is unchanged: x is view Z in metres, yz is the packed normal.
- The tile reductions (`gsA/B/C`) stay.
- The guide tile mean is added when flag 32 is set:

```hlsl
c  = surf ? saturate(gNrd2Guide.Load(p).a) : 0;   g = guide.rgb;
wG = wD * c;                                      // wD: the existing D anchor weight
gsG[gi] = float4(wG * g, wG);                     // fixed-order tree sum, as gsB
if (gi == 0) gNrd2AnchG[gid.xy] = aG.w > 1e-6 ? float4(aG.rgb / aG.w, 1) : 0;
```

- Groupshared budget:
  - halo 16x16 float4 for D and S is 8 KB;
  - separable-moment temp for one signal is 4 KB (D, barrier, then S reuses it);
  - the anchor arrays are 4 KB;
  - the temp aliases the anchor arrays through one `float4 gsPool[]`, because it is dead before the anchors are written;
  - peak is 12 KB, under 16 KB;
  - keep the `#error` guard, as pass 6 has.

### 2.6 Pass 6 (prefilter)

Unchanged. Its radiance stop keeps the tile anchor, not the guided one.

### 2.7 Pass 7, CSNrd2Temporal

- The halo grows from 12x12 to 16x16, with prefD/prefS rgb and a (a < 0 means no surface).
- Moments are valid-weighted and separable, per signal:

```hlsl
// Stage H: 128 items (16 rows x 8 cols), 9 taps each:
//   H.xyz += w_i*v*x;  H.w += w_i*v;  H2 += w_i*v*x*x
// barrier
// Stage V: pixel sums H, H2 over 9 rows
// W = sum w_j*H.w;  mean = sum(w_j*H.xyz)/W;  var = abs(sum(w_j*H2)/W - mean*mean)
```

- D path:

```hlsl
aD0  = tile anchor (bilinear, as now)
aD   = (flag 32) ? nrd2GuidedAnchor(aD0, g, c, ĝ, ĝ.a, gNrd2Hist.z) : aD0;   // g, c from guide.Load(p); ĝ = bilinear anchG
if (flag 64) out = nrd2TemporalBlendFfx(curD, aD, meanD, varD, rpD.rgb, nD, gNrd2Hist.y, curD2, histD2);
else         out = nrd2TemporalBlend(curD, aD, loD, hiD, rpD.rgb, nD, curD2, histD2);   // 5x5 from the same halo
```

- S path is the same without the guide, using the tile anchor `aS`.
- The firefly clamp stays: it clamps the current value to 2x the brightest of its 8 neighbours.
- The noise estimate carry stays: `lerp(RelVar(lum curD2, lum histD2, scale), prefA, w)`.
- `scale` uses the unguided anchor luminance.

### 2.8 Tests (Nrd2ResolveTest, new `testTemporalFfx`)

- Catmull-Rom weights sum to 1. At f = 0 and f = 1 they interpolate. At 0.5 they are -1/16, 9/16, 9/16, -1/16. A linear ramp is reproduced exactly.
- A step edge: the clamp bounds the ringing and never goes below 0.
- An invalid core tap triggers the bilinear fallback. No valid taps means no history.
- FFX factor:
  - cos 1 and dz 0 give 1.
  - cos 0.9 gives exp(-0.14), about 0.869, below the threshold.
  - It is monotone in both inputs.
  - A relative depth error of 0.105 puts the factor right at 0.9.
- Search picks the highest factor and the centre wins ties. Rebuild weights are hard at thr/2 and renormalised. A factor below the threshold after rebuild gives 0.
- Moments: weights are validity-weighted, a constant patch has zero variance, and a known two-value patch gives the analytic mean and variance.
- ClipAABB: a point inside is unchanged. (4,3,0) in a box with centre 0 and half-extents (1,2,2) maps to (1,0.75,0), which differs from a per-channel clamp (1,2,0).
- Blend: n = 1 gives the current value pulled by 1/2 to the anchor. The history weight is 1 - 1/n.
- Guided anchor:
  - c = 0, invalid ĝ or strength 0 returns the anchor **exactly** (`==`). This is the bit-identical test.
  - g = ĝ returns the anchor.
  - A lamp-dominated tile (A > ĝ) behaves additively.
  - An indirect-dominated tile (A < ĝ) gives A·g/ĝ.
  - A = 0 returns 0.
  - The result stays within [0.25 A, 4 A].
- Constant-drift test: read nrd2_resolve.hlsli and compare each `kNrd2Ffx*` literal with the CPU constant, as NeuRaCTest does with `defineValue`.

## 3. Phase C: NeuRaC guide into NRD2

### 3.1 Combining the guide with the tile anchor

I chose a hybrid **additive-or-ratio form on the deviation from the guide's own tile mean**:

```
A_g = clamp( A + c * k * s * (g - ĝ),  0.25 A,  4 A )         s = min(1, lum(A)/lum(ĝ))
```

- `A` is the bilinear tile anchor, `g` the pixel's guide, `c` its confidence, `ĝ` the confidence-weighted tile mean of the guide, `k` the strength.
- The tile level comes from the current-frame anchor. Only the spatial shape comes from the cache.
- This is applied to **D only**, in the temporal pass only, so rules 5 and 10 are untouched. The network and level choice never see the guide.
- Why this form:
  - The cache holds only bounce light, but D also holds lamp direct diffuse.
  - Lamp-dominated tile (A greater than ĝ): s = 1, the term is purely additive. The lamp light stays in A and the guide adds only the indirect deviation.
  - Indirect-dominated tile (A at most ĝ): A + (g - ĝ)·A/ĝ = A·g/ĝ. This is the ratio form, and it cancels cache scale error, including the kD/kdAlbedo factor the guide cannot see.
- Edge cases:
  - Zero or low confidence: c = 0 gives exactly A. A partially confident pixel is scaled by c, and the tile mean weights pixels by D-anchor weight times c.
  - An empty guide tile makes `anchG.a = 0`, which gives A.
  - A = 0 gives s = 0, which gives A.
  - Cache hot spots are bounded by the 4x clamp.
- It acts where n is small automatically: the pull weight is 1/(n+1), 1/2 at n = 1.
- `k` is `voxi.nrd2NeuracGuideStrength`. Defaults are on, strength 1, and on only when NeuRaC is live and the guide pass built.
- With NeuRaC off, or on Vulkan, `in.guide = 0`, flag 32 is clear, and the result is bit-identical to the new no-guide path.

### 3.2 Guide producer (new class `NeuRaCGuide`, new `voxi_neurac_guide.hlsl`)

- It is a standalone compute pass with its own small layout. Table 0 is not involved, and no PS or monolith grows.
- Layout and binding:
  - SRV: t0 `RcInfo` (the current ring slot), t1 view Z, t2 normal-roughness.
  - UAV: u0 cells (read only), u1 the guide texture.
  - Constants: b3, root CBV, as Nrd2 does.
- Placement in the frame: after Stage B's draw and before `nrd2_.record`. Stage B writes the G-buffer, so the guide must read it after the draw, not from `rdVisBuf_`.
  - `record()` does `textureBarrier(viewZ/normal, gbufferState, NonPixelShaderResource)` first and restores `gbufferState` last, exactly as `Nrd2::record` does.
  - It also barriers the guide texture `kRead` to `kWrite`, dispatches, then barriers back.
- The guide texture is owned by `NeuRaCGuide`, RGBA16F at the staged size (`rdStagedW_` × `rdStagedH_`). It is resized lazily and costs about 29 MB at 1440p.

```hlsl
cbuffer GuideCB : register(b3) {
    uint4  gRect;          // viewport x y w h
    float4 gView[3];       // right, up, forward (xyz); w of right/up: tan(half FOV)    (same basis as Nrd2)
    float4 gEyeCell[3];    // per cascade: eye/size - originCell, computed in double on the CPU, in [32,33)
};
[numthreads(8,8,1)] void CSNeuracGuide(uint3 t : SV_DispatchThreadID) {
    if (any(t.xy >= gRect.zw)) return;
    p = gRect.xy + t.xy;  z = gViewZ.Load(p);                       // cm
    if (!(z > 0 && z < 1e6)) { gGuide[p] = 0; return; }
    N   = decode(gNormal.Load(p));                                  // 6-line octahedral decode
    off = z * (fwd + ndc.x*tanx*right + ndc.y*tany*up);             // cm from the eye
    float rem;  float3 e = rcLookupRel(off, N, gEyeCell, rem);      // same maths as rcLookup; q = (off + N*0.5*size)/size + eyeCell
    gGuide[p] = float4(e, 1.0 - rem);
}
```

- World position reconstruction duplicates the 25-line `viewBasis` of Nrd2.cpp inside `NeuRaCGuide.cpp`. It needs no new public denoise API.
- Cost: each lookup touches up to 8 corner cells in each of up to 2 cascades. Neighbouring pixels share cells, so cache hit rates should be high. My estimate is 0.1-0.3 ms at 1440p (**UNVERIFIED**).
- Optional follow-up: a half-resolution guide.
- The pass also counts the fraction of surface pixels with confidence at least 0.5, through a groupshared reduction and 2 atomics. It is logged every 120 frames as `[Voxi] NeuRaC guide: X% covered`. This is the data for defect (g).
- The guide runs only when `settings_.nrd2NeuracGuide && neuracLive_ && rc_.valid() && guide.valid()`.

## 4. Phase B: NeuRaC stage 1 fixes

### 4.1 Cell, layout and shared-header changes (owned by W1-C)

- **Normal word repack:** `octU 10 | octV 10 | len 6 | trust 6`. The trust field is for stage 2.
  - 63 means trust 1.0.
  - `rcPackNormal(v, trust6)` and `rcUnpackNormal(w, len, trust)` change in `voxi_neurac.hlsli` and `NeuRaCLayout.hpp`.
  - The resolve resets trust to 63 for a new or replaced cell and carries it for a valid one.
  - Meta (24-bit tag, 4 n_eff, 4 age) is unchanged.
  - Update `NeuRaC::logWarmupStats`, which reads `cell.b[2] >> 22`.
- **Accumulator layout:** counts `[0,K)` as `accepted | prevArrivals<<16`, arrivals `[K,2K)`, payload from `2K`, with `K = kCells`. `kAccumInts = 17 * kCells (18 * kCells since the 2026-10-12 noise-calibrated label adds a sum-of-squares block)`, about 53.5 MB total.
  - `info.accumInts` flows to the table-0 binding through `Bindings`. No renderer change is needed.
  - Replace the payload-base macro `AVER_RC_CELLS`.
- **RcInfo, same 80 bytes:** `hdr1 = (alphaMin, relaxAlpha, asfloat(coarseCaps), netStrength)`.
  - `coarseCaps = cap1 | cap2<<8`, defaults 16 and 8. Cascade 0 uses `hdr0.w`, 64.
  - The C++ struct replaces `reserved[3]` with `f32 relaxAlpha; u32 coarseCaps; f32 netStrength;`.
- **New shared header `voxi_neurac_lookup.hlsli`:**
  - It holds the cascade lookup core and helpers.
  - It references `gRcInfo` and `gRcCells` by name. The including shader declares them: the twin at t22/u21, the guide at t0/u0.
  - Core: `rcLookupCascade(c, q, N, out value, out a)`.
  - `rcLookup` calls the core with `q = (p + N*0.5*size)/size - org`.
  - The guide calls it with `q = (off + N*0.5*size)/size + eyeCell`.
  - `voxi_neurac_io.hlsli` includes it and keeps the scatter, the visualiser and `rcCacheOn`.
- **Lookup weight** gains the trust factor: `... * lerp(1, trust, netStrength)`. Network off means `netStrength` is 0, so behaviour is identical to the old formula.
- **Visualiser:** `rcDebugColour` gets mode 5, showing trust. The mode field is 3 bits, so mode 5 fits; the grep for the setter clamp is in W2-B.

### 4.2 Defects

**(a) Reset hooks.** `NeuRaC` gains `requestRelax(u32 frames, f32 alphaFloor)`; `requestReset()` stays. The resolve uses `alpha = max(1/(nEff+1), max(info.hdr1.x, info.hdr1.y))`. Hooks, owned by W1-F:

| Event | Action |
|---|---|
| Sun jump | In `updateNeuRaC`, before `rc_.beginFrame`: `if (rtHistSunJumped()) rc_.requestRelax(8, 0.5f)`. `rtHistSunJumped()` compares against the sun recorded at the end of last frame, so it is valid there |
| GI rebuild | After `takeGiSnapshot()` (VoxiRenderer.cpp:1322): `requestRelax(4, 0.25f)`. A decay, not a clear, because it fires on any sky or sun byte change |
| Level load | In `setVolume()` (called every frame from SandboxApp.cpp:2490 and GameApp.cpp:1759 with the fitted volume): if centre or extent changed by more than 1 cm, `requestReset()` |
| Explicit reset | `resetGiHistory(quiet=false)` calls `requestReset()`. Skip it when `quiet`, because the every-frame debug path uses `quiet=true` |
| Visibility mode | A change away from 4 already tears down (`neuracWanted()` false), and re-entry clears at create. No extra hook |
| Teleport | No hook. The window shifts, tags mismatch, and the cells go stale-invalid by design |

**(b) Checkerboard parity.**
- Reorder `kGiVisPhase` in `voxi_restir.hlsli` to `{(0,0),(1,0),(1,1),(0,1)}`. Then `(x^y)&1` equals `frame&1`, and the traced F2 pixel always lies in the checkerboard-traced class.
- Set `giCbParityWritten_ = rtFrameIndex_ & 1u` (VoxiRenderer.cpp:5610) so one counter drives both the shader's `gRtHistParams.z` and the parity.
- The PT trainer at `voxi_restir.hlsli:377` then needs no formula change.
- Mirror the table in `GiVisibility.hpp::tracedPixel` and the literal in `GiVisibilityTest`. That test already fails for unrelated reasons, so it only needs to stay no worse.
- The new schedule traces F2 on every frame at a quarter of the pixels, which is the cost noted in section 0 item 6.
- Whether `denoiseFrame_` and `rtFrameIndex_` currently have equal parity is **UNVERIFIED**. The fix removes the question.

**(c) Scatter and lookup consistency.**
- Cause: the lookup shifts by `N*0.5*size` and uses a trilinear over cell centres, so the trained cell receives weight 1-u for positive normals and u for negative ones. Here u is the surface's fractional position in its cell.
- Floors at y = 0 get u = 0 and full weight. Walls get an average of 0.5 and often nearly 0. That matches the red verticals.
- Fix: scatter bins at the lookup's own cell, `floor((hitPos + N*0.5*size)/size)`, and trains only interior cells `[MARGIN, RES-1-MARGIN]`. The cell containing q then always carries at least 0.5 of the 1D trilinear weight.
- This also separates the two faces of a thin wall into different cells.

**(d) Coarse cascades.** The scatter loop attempts every cascade whose interior contains the cell, finest first. Caps are 64, 16, 8, so the extra atomics stay bounded. The fade into coarser cascades now lands on trained cells, and a surface leaving the fine window is already known at the coarse level.

**(e) Arrival-order cap bias.**
- Hashed, load-adaptive accept. `prev = (gRcAccum[idx] >> 16)` is last frame's arrival count.
- `rate = prev <= cap ? 1 : max(cap/prev, 1/32)`.
- A sample is accepted if `hash16(asuint(hitPos), asuint(dir2), c) < rate*65536`.
- Every arrival does one atomic on the arrivals array, which the resolve reads and then shifts into `prev`.
- Accepted samples still go through the old atomic cap as a hard safety bound.
- The accepted set is a random subset of arrivals, so the mean stays unbiased.

```hlsl
void rcScatterCascade(uint c, float3 hitPos, float3 N, float3 dir2, float3 L, float wgt, RcInfo info) {
    const float size = asfloat(info.cas[c].w);
    const int3 cell = (int3)floor((hitPos + N * (0.5 * size)) / size);
    const int3 rel  = cell - info.cas[c].xyz;
    if (any(rel < AVER_RC_MARGIN_CELLS) || any(rel > AVER_RC_RES - 1 - AVER_RC_MARGIN_CELLS)) return;
    const uint idx = rcCellIndex(c, cell), cap = rcCapFor(info, c);
    if (cap == 0u) return;
    InterlockedAdd(gRcAccum[AVER_RC_CELLS + idx], 1);                      // arrival
    const uint prev = (uint)gRcAccum[idx] >> 16;
    const float rate = prev <= cap ? 1.0 : max((float)cap / prev, 1.0 / 32.0);
    if (rcHash16(hitPos, dir2, c) >= (uint)(rate * 65536.0)) return;
    int old; InterlockedAdd(gRcAccum[idx], 1, old);
    if ((uint)(old & 0xFFFF) >= cap) return;
    // ... the 15 payload atomics, unchanged
}
void rcScatter(...) { [loop] for (uint c = 0; c < AVER_RC_CASCADES; ++c) rcScatterCascade(c, ...); }
```

- Keep the cascade loop `[loop]`, not unrolled, to limit register growth in the twin.
- The resolve handles each cell as follows:
  - It reads the counts word and the arrivals word.
  - `n = min(acc, capFor(cascade))`.
  - It writes `counts = min(arr, 65535) << 16` and `arrivals = 0` before its early-out, so stale hints cannot linger.

**(f) Barrier.** In VoxiRenderer.cpp around line 5835, add `ctx.uavBarrierBuffer(rc_.accumBuffer())` after `rc_.recordResolve(ctx)`.

**(g) Primary-surface coverage.** Not fixed. Mitigated by (c) and (d), with the coverage counter in section 3.2 to measure. Reason: section 0 item 7.

**(h) Docs.** NEURAC.md is rewritten in wave 2 (section 9).

**(i) Tests.** Section 4.3.

### 4.3 Tests (owned by W1-E)

**CPU mirrors in `NeuRaCLayout.hpp`** (written by W1-C, tested by W1-E):
- `scatterCell(hitPos, N, cascade, origin, size, outCell)`: offset binning plus the interior test.
- `acceptRate(prev, cap)`.
- `capForCascade(RcInfo, c)`.
- `lookupCornerWeight(tri, dotNN, len, nEff, age, trust, netStrength)`.
- `packNormal` and `unpackNormal` with trust.

**Extend `NeuRaCTest`:**
- Normal word round-trip with trust. Oct error stays under 0.15 degrees. Len resolution is 1/63.
- Re-asserted `AVER_RC_*` literals, including the new payload base.
- The wall case: for N along +x and a surface at every u in a grid, the trained cell receives at least 0.5 of the 1D lookup weight, for both normal signs.
- Rate and cap tables.
- Layout arithmetic: `kAccumInts == 17 * kCells`, the headroom `static_assert`s still hold, `sizeof(RcInfo) == 80`.

**New `tests/render.voxi/src/NeuRaCGpuTest.cpp`:**
- Uses WARP through `IDevice::runStandaloneCompute`, like `NeuralGpuParityTest`.
- Embeds a harness `.hlsl` that declares `gRcInfo` (t0), `gRcAccum` (u0), `gRcCells` (u1) and a constant `gAmbientParams`, includes `voxi_neurac.hlsli` and `voxi_neurac_io.hlsli`, and exposes `CSScatter` and `CSLookup` kernels.
- It runs the real `CSRcResolve` and compares against the CPU mirrors:
  - the per-cell cap;
  - the cascade choice;
  - the normal offset;
  - the lookup weights and fallback `remaining`;
  - the resolve blend and aging;
  - the relax alpha.
- Skip with exit 77 if there is no device.
- The log format must use `  FAIL  ` and `FAILED ===` so ctest's regex sees failures.

**Compile coverage:**
- `VoxiShaderCompileTest` creates a standalone `NeuRaC` plus `NeuRaCGuide` on the device, and calls `createNeuRaCTwins` if it is not already part of `buildAllVariants` (**UNVERIFIED** whether it is). Wave 2 adds `NeuRaCNet`.
- `VoxiSpirvCompileTest` adds variants for `nrd2.hlsl` passes 5, 6 and 7 (`CSNrd2Reproject` etc., SM 6.0, `AVER_NRD2_PASS=5/6/7`) and the guide shader.
- Whether nrd2.hlsl compiles to SPIR-V today is **UNVERIFIED**. If a variant fails for a pre-existing reason, the agent reports it and does not fix unrelated code.

## 5. Phase D: Stage 2, the learned trust network (wave 2)

### 5.1 What it is

- A tiny MLP, `Mlp` unmodified, learns a per-cell **trust** in [0,1]. That is the "learned leak/blend weight" of 2a.
- The trust is baked into the cell's 6-bit field and multiplies each corner weight in `rcLookup`. The lookup in the twin therefore costs one extra multiply. Table 0 is full and the twin is already a monolith, so a network inside the twin is not an option.
- Inference and training run at cell level in standalone passes.

**Why trust and not the alternatives I considered:**
- A per-query network would have to run in the twin or in a per-pixel pass. The twin cannot take the registers or a new binding.
- A per-pixel pass would write and read about 237 MB of features at 1440p.
- Cell-level baking has none of these costs.

### 5.2 Network

| Item | Value |
|---|---|
| Shape | 10 inputs, 2 hidden layers of 8 (ReLU), 1 output (Sigmoid), 153 weights |
| Loss | L2 |
| Adam | lr 2e-3, `weightEma` 0.99, `gradClamp` 2 (this gives `safeBatchLimit` 16383, so cap the training batch at 16383 records) |
| Init | He-uniform, then the last layer's W zeroed and its bias set to logit(0.9) = 2.197, uploaded with `uploadWeights`. Trust therefore starts at 1 |
| Trust mapping | `trust = saturate(τ̂ / 0.85)`; baked as `round(63 * trust)` |

**Inputs φ (all in [-1,1] or [0,1]).** The old state means the cell's resolved state before this frame's update. Eo and Ef are the luminances of the cosine-convolved irradiance/π along the cell's own mean normal ν, from the old and fresh SH. The neighbours are the 6 face neighbours at the same cascade. They count if the tag matches, n_eff > 0 and age < 15, weighted by `saturate(2*dot(cn_nb, ν))*len_nb`.

| # | Input |
|---|---|
| 0 | `len` (planarity) |
| 1 | `n_eff/15` |
| 2 | `age/15` |
| 3 | `log2(1+n_fresh)/6` (training), **1.0 at inference** so sampling noise is not read as unreliability |
| 4 | neighbour support, Σw/6 |
| 5 | neighbour disagreement, clamped to 2 and halved |
| 6 | `clamp(log2((E_nbr+ε)/(Eo+ε)), -4, 4)/4`, 0 when there is no support |
| 7 | anisotropy of the old SH: `‖c1,c2,c3‖ / c0` in luminance, clamped |
| 8 | `ν.y` |
| 9 | `cascade/2` |

**Label** (training only): `τ* = 1 - min(1, 2ρ)`, `ρ = |Ef - Eo| / (Ef + Eo + 1e-3)`.

**Eligibility:** `n_fresh ≥ 8`, `n_eff ≥ 2`, age < 15, valid tag, `Eo + Ef > 1e-3`, plus a hash thinning so the expected eligible count stays under 16383.

**Counsel note (US 11,610,360):** the target is derived from traced samples. The cache enters as a baseline in the comparison and as an input feature, never as the label. There is no second prediction at path end and no network output used as a label. A strict reading of "targets from traced samples" is more cautious than this construction, so it is flagged in the docs for counsel.

### 5.3 Passes (new `NeuRaCNet` class, new `voxi_neurac_net.hlsl`, own layouts)

Per frame, in this order, all inside the existing resolve block of VoxiRenderer.cpp:

```
[twins scatter]
uavBarrier(accum)
NeuRaCNet::recordPre():
   CSRcNetReset      (1 thread: zero the four counters)
   CSRcNetFeatures   (one thread per cell; early-out on the counts word; appends the inference record
                      and, if eligible, the training record; atomic counter append)
   CSRcNetFinish     (1 thread: clamp counts to caps)
   bufferBarrier(UAV -> Common) on lists and counters
   Mlp::recordTrain(trainList, targets, trainCount, 16383)
   Mlp::recordInfer(infList, outputs, infCount, 131072)
rc_.recordResolve()                      // old state + fresh accum -> new state; trust carried
uavBarrier(accum); uavBarrier(cells)
NeuRaCNet::recordApply():
   CSRcNetApply      (one thread per inference record: write trust bits into cells[cell].b.z,
                      only if the cell is still valid after the resolve;
                      accumulate loss and label/pred stats in fixed point)
uavBarrier(cells)
```

- Lists:
  - Inference list: 131072 records × 10 floats, plus parallel `recCell` and output arrays.
  - Training list: 16383 records × 10 floats, plus targets.
  - Total about 7 MB.
  - Counters are `uint` buffers whose element 0 is the live count.
- Buffers rest in `Common`. Our passes use `bufferBarrier(Common <-> UnorderedAccess)` around each write, the same pattern as `Nrd2::recordFeatures`.
- GPU spans: "Voxi NeuRaC net".
- Stats (4 floats) go through a readback buffer every 120 frames, logged as `[Voxi] NeuRaC net: step N, eligible N, mean label, mean pred, loss`. The Mlp has no loss readout, so the apply kernel computes the loss.
- Strength warm-up: `netStrength = user * saturate(framesActive/900)`, written into `RcInfo.hdr1.w`.
- The net is off when Path Tracing is wanted, when `voxi.neuracNet 0`, or when the Mlp fails to build. In every case `netStrength` is 0, which gives stage-1 behaviour.
- No weights persistence in this plan. The tiny net converges in seconds.

### 5.4 CPU reference and tests

`NeuRaCNetReference.hpp` mirrors the feature builder, label, eligibility and trust mapping. `NeuRaCNetTest` (CPU only):
- Identity init gives trust of exactly 1.
- On synthetic cells, with clean planar cells that are stable and leaky low-planarity cells that fluctuate, a `MlpReference`-trained network gives leaky cells a clearly lower trust (margin in the test).
- Label and eligibility edge cases, and mapping bounds.
- Training batch ≤ `safeBatchLimit`.

`NeuRaCGpuTest` gains a section on WARP that compares `CSRcNetFeatures` records against the CPU feature builder on a tiny synthetic cell buffer, and `CSRcNetApply` bit writes.

### 5.5 Cost (estimate, **UNVERIFIED**, budget ≤ 0.5 ms)

| Part | Estimate |
|---|---|
| Features (counts stream, touched cells and neighbours) | 0.05-0.1 ms |
| Mlp train, 16k records | about 0.05 ms |
| Mlp infer, up to 131k records | about 0.03 ms |
| Apply | about 0.02 ms |
| **Total** | **about 0.15-0.25 ms** |

### 5.6 Why 2a only, 2b later

- 2b needs `Mlp` with `instances > 1`, which the module README lists under "Not there yet". That is a new kernel, a CPU twin, parity tests and a weight-file version.
- A regional network's output also has to be baked per cell, because there is no query-time slot. A residual per cell is 12 SH outputs, which does not fit the budget with training.
- It sits closer to claim 1 of US 11,610,360 because it outputs radiance, and counsel should see 2a first.
- 2a's measured benefit should guide whether 2b is worth it.

## 6. Binding and constants summary

- **Table 0:** no change. NeuRaC reuses t22/u20/u21. Only the accumulator buffer grows (50.3 to 53.5 MB).
- **New own-layout pipelines:** the guide (3 SRV, 2 UAV, b3) and the four net kernels, plus Mlp's own.
- **Nrd2:** Reproject 9/5 becomes 10/6, Temporal 10/5 becomes 12/5, constants 352 becomes 384 bytes.
- **VoxiFrame cbuffer:** untouched, so the three mirrors need no edit.
- **`packAmbientW` bits:** none used.
- **Vulkan:**
  - NRD2 does not run in-frame on Vulkan and NeuRaC tears down to HalfResolution there.
  - Behaviour is unchanged: no guide, no net, no cell layout in use.
  - SPIR-V compile coverage for the changed NRD2 passes is added as in section 4.3.

## 7. Cost summary at 1440p on the RX 7800 XT (all estimates, **UNVERIFIED**)

| Item | Estimate |
|---|---|
| NRD2 Reproject (FFX edge handling, 12-tap Catmull-Rom, moments) | +0.15-0.3 ms |
| NRD2 Temporal (16x16 halo, moments) | +0.1-0.2 ms |
| Guide producer | +0.1-0.3 ms |
| Stage 1 fixes (F2 rate restored, 3-cascade scatter) | owner-measured in "Voxi RD GI stage"; expect a real rise |
| Stage 2 | +0.15-0.25 ms |

NRD2 totals about +0.35-0.8 ms with the guide. The previous stage was about +0.6 ms. GPU spans to read: `NRD2.Reproject`, `NRD2.Temporal`, "Voxi NeuRaC guide", "Voxi NeuRaC net", "Voxi NeuRaC resolve".

## 8. Risks

- **TDR / register pressure.**
  - Nothing in PSMainVoxi, PSRayDriven, material_prelude or the raster `giRestirIndirect` is edited.
  - The only twin growth is `rcScatter` (3-cascade `[loop]`, hash) and the trust multiply, all under `#if AVER_NEURAC`.
  - The `kGiVisPhase` reorder is outside that guard but is only a table permutation, and invalidates the DXIL cache once.
  - If the twin does not fit on the RX 7800 XT, the fallback is a two-phase scatter: the twin appends compact records and a standalone pass scatters them.
  - Passes 5 and 7 and the guide are compute passes with bounded groupshared.
  - `voxi.hlsl` is not edited.
- **Patents** (not legal advice; every item is recorded in the docs for counsel):
  - The mean ± sigma clip and the mean/variance history reject versus US 10,116,916, US 12,482,168 and US 11,600,036. The owner has decided to accept the risk.
  - The guide, a world-space radiance cache feeding a denoiser's disocclusion fill, is new ground and flagged.
  - Stage 2 versus US 11,610,360, as section 5.2.
  - Rules 5, 6, 8 and 10 are unaffected: the guide never reaches the network or level choice; Catmull-Rom is a fixed tap pattern.
- **Behaviour.** Depth weight 1.0 may smear across small depth steps. The colour reject may be strict on noisy HDR content. Catmull-Rom ringing is bounded but may show on high contrast. The guide's cache units may be off, which the tile gain mostly cancels. All have console knobs.
- **Blind implementation.** Groupshared budgets (16 KB), cbuffer byte order between the HLSL and C++ (checked by the static_assert and the central build), and `Mlp` limits (`inputs ≤ 64`, `hiddenWidth` a multiple of 4, `safeBatchLimit`).

## 9. Implementer tasks (≤ 6 per wave, 2 waves, distinct file ownership)

**Rules for all implementers:** edit only the files they own; do not build or run; lean comments; no third-party code; keep FidelityFX, `aver_denoise.hlsl` and `Denoiser.cpp` stock. A function name used by another task must match the contracts below exactly.

### Wave 1

| Task | Owns |
|---|---|
| **W1-A NRD2 maths, CPU twin, tests** | `modules/render.denoise/shaders/nrd2_resolve.hlsli`, `modules/render.denoise/include/aver/render/denoise/Nrd2ResolveReference.hpp`, `modules/render.denoise/src/Nrd2ResolveReference.cpp`, `tests/render.denoise/src/Nrd2ResolveTest.cpp` |
| **W1-B NRD2 passes and host** | `modules/render.denoise/shaders/nrd2.hlsl`, `modules/render.denoise/src/Nrd2.cpp`, `modules/render.denoise/include/aver/render/denoise/Nrd2.hpp` |
| **W1-C NeuRaC stage 1** | `modules/render.voxi/shaders/voxi_neurac.hlsli`, `voxi_neurac_io.hlsli`, new `voxi_neurac_lookup.hlsli`, `voxi_neurac_resolve.hlsl`, `voxi_restir.hlsli` (only the phase table, the scatter call sites, and the warm-up trust); `modules/render.voxi/include/aver/voxi/NeuRaCLayout.hpp`, `NeuRaC.hpp`, `GiVisibility.hpp`; `modules/render.voxi/src/NeuRaC.cpp` |
| **W1-D Guide producer** | new `modules/render.voxi/shaders/voxi_neurac_guide.hlsl`, new `modules/render.voxi/include/aver/voxi/NeuRaCGuide.hpp`, new `modules/render.voxi/src/NeuRaCGuide.cpp`, `modules/render.voxi/CMakeLists.txt` (add the source and link `Aver.Render.Neural`), `modules/render.voxi/module.json` (add `Aver.Render.Neural` to `dependsOn`) |
| **W1-E Stage 1 tests and compile coverage** | `tests/render.voxi/src/NeuRaCTest.cpp`, new `tests/render.voxi/src/NeuRaCGpuTest.cpp`, `tests/render.voxi/CMakeLists.txt`, `tests/render.voxi/src/GiVisibilityTest.cpp`, `VoxiShaderCompileTest.cpp`, `VoxiSpirvCompileTest.cpp` |
| **W1-F Settings and wiring** | `modules/render.voxi/include/aver/voxi/Voxi.hpp`, `modules/render.voxi/src/Voxi.cpp`, `modules/render.voxi/include/aver/voxi/VoxiRenderer.hpp`, `modules/render.voxi/src/VoxiRenderer.cpp`, `sandbox/src/EditorConsole.hpp` |

**Contracts between wave-1 tasks.**

W1-B exposes (Nrd2.hpp):

```cpp
// Nrd2Params additions
bool ffxClip = true;  f32 ffxClipWeight = 4.0f;  bool ffxReject = true;  f32 ffxRejectWeight = 1.5f;
f32 ffxDepthWeight = 1.0f;  f32 histSharp = 1.0f;  f32 guideStrength = 1.0f;
// Inputs addition
rhi::TextureHandle guide = 0;      // RGBA16F, rests NonPixelShaderResource
```

W1-C exposes (NeuRaC.hpp):

```cpp
struct Params { u32 sampleCap = 64; f32 alphaMin = 0.0625f; u32 ageStepFrames = 16;
                u32 coarseCap1 = 16, coarseCap2 = 8; f32 netStrength = 0.0f; };
void requestRelax(u32 frames, f32 alphaFloor);   // requestReset() already exists
rhi::BufferHandle infoBuffer() const;            // this frame's RcInfo, after beginFrame
const f32* eyeCm() const;                        // the eye beginFrame was given
```

W1-D exposes (NeuRaCGuide.hpp):

```cpp
class NeuRaCGuide {
public:
    struct Frame { rhi::BufferHandle info, cells; const f32* eyeCm; rhi::TextureHandle viewZ, normalRoughness;
                   rhi::ResourceState gbufferState; u32 viewport[4]; u32 width, height; };
    bool create(rhi::IDevice&);   void destroy();   bool valid() const;
    bool record(rhi::IRenderContext&, const Frame&);   // false: no guide this frame
    rhi::TextureHandle guide() const;                  // RGBA16F, NonPixelShaderResource at rest
    void invalidateBindings();
};
```

W1-F calls these. It also owns the NRD2 `np.*` fill at VoxiRenderer.cpp:5925, `in.guide = guide.record(...) ? guide.guide() : 0`, `NeuRaC::Params` use in `updateNeuRaC`, the hooks of section 4.2 (a), the parity line, and the barrier. It records the guide just before `nrd2_.record`.

W1-E uses the CPU mirror names listed in section 4.3 and the final normal packing `octU 10 | octV 10 | len 6 | trust 6`.

**After wave 1, the main loop:**
- builds centrally;
- runs ctest;
- runs `python scripts/module-catalog.py --write`;
- fixes contract mismatches.

### Wave 2

| Task | Owns |
|---|---|
| **W2-A Stage-2 network** | new `modules/render.voxi/shaders/voxi_neurac_net.hlsl`, new `modules/render.voxi/include/aver/voxi/NeuRaCNet.hpp` and `NeuRaCNetReference.hpp`, new `modules/render.voxi/src/NeuRaCNet.cpp`, new `tests/render.voxi/src/NeuRaCNetTest.cpp`, `modules/render.voxi/CMakeLists.txt` (add the source) |
| **W2-B Stage-2 integration** | `Voxi.hpp`, `Voxi.cpp`, `VoxiRenderer.hpp`, `VoxiRenderer.cpp`, `EditorConsole.hpp`. Adds `voxi.neuracNet` (bool, default 1) and `voxi.neuracNetStrength` (f32 0-1, default 1), the net create and teardown, the net frame order, `netStrength` into `NeuRaC::Params`, and the NeuRaC viewer mode 5 (grep the `neuracView_` setter and the CLI `--neurac-view` clamp, update them) |
| **W2-C Docs** | `docs/rendering/NEURAC.md`, `docs/rendering/NRD2.md`, `docs/rendering/NEURAA_NRD.md`, `modules/render.neural/README.md` (a short client note; the API is not growing) |

W2-C also owns `NeuRaCGpuTest.cpp` additions? No. The NeuRaCGpuTest net section is added by W2-A, which therefore also owns `tests/render.voxi/CMakeLists.txt` in wave 2 (wave 1's edit is done by then).

**Docs content (W2-C).**
- NEURAC.md:
  - Rewrite the stale parts: line 7 versus 248 status, slot counts (25/25 not 23/22), atomics (16, not 13), line references, the twin list. Fix `NeuRaCLayout.hpp:2`, which cites a nonexistent .cpp.
  - Record the new cell packing and accumulator layout.
  - Add section 5.11 for the stage 1 fixes with reasons.
  - Add a stage 2 section, the 2b reasoning, and the US 11,610,360 flag with the exact construction (section 5.2).
  - Record that PT mode disables the net.
- NRD2.md:
  - Replace the "Temporal stabiliser" description of reproject and temporal with the FFX edge handling, Catmull-Rom, moments clip, reject, and the guide.
  - Record the owner decision of 2026-10-11 and the cost estimates.
  - Update "Differences from FidelityFX" (a), (d) and (e), which no longer hold.
- NEURAA_NRD.md section 7:
  - Rules 1, 7 and 13: add "Amended 2026-10-11, owner decision" and the exact patent exposure (US 10,116,916 in force to 2037; US 12,482,168 / 11,600,036 for moment-based ranges and rejection tests).
  - Rule 13 also gets the colour reject, noted as a mean/variance history-rejection test.
  - Add a new rule 17 for the guide and cache-into-denoiser, flagged for counsel.
  - State that the min/max path is selectable with the setting off.
  - Keep rules 5 and 10 intact.

**After wave 2, the main loop:** build, ctest, run `python scripts/docs-check.py` (any repo path a doc names in backticks must exist), then re-run the verification below.

## 10. Verification

### Central

- Build `build-release` targets: `Aver.Render.Denoise`, `Aver.Render.Voxi.Renderer`, `Sandbox`, `Nrd2ResolveTest`, `NeuRaCTest`, `NeuRaCGpuTest`, `NeuRaCNetTest`, `GiVisibilityTest`, `VoxiShaderCompileTest`, `VoxiSpirvCompileTest`, `NeuralGpuParityTest`.
- Run `ctest`. Any `*Test` executable under `tests/` is registered automatically. Known unrelated failures: `PlayMobilityTest`, `SkyOwnershipIdentityTest`, `GiVisibilityTest`, `ShapeTest`.
- `VoxiShaderCompileTest` and `VoxiSpirvCompileTest` must be green (exit 77 is a skip).
- Run `python scripts/module-catalog.py --write` after the `module.json` edit, then `--check`, which is the `ModuleCatalogCheck` ctest. Run `python scripts/docs-check.py`.

### Owner, in the editor

- **Setup:** `set voxi.denoiserMode 2`, `set voxi.giRestirVisibility 4`, `set voxi.rayDrivenStages 2`, TAA off so jitter is 0, move the camera.
- **A/B toggles:**
  - `set voxi.nrd2Stab 0` for no temporal stage.
  - `set voxi.nrd2FfxClip 0` for the min/max box.
  - `set voxi.nrd2FfxReject 0`.
  - `set voxi.nrd2HistSharp 0`.
  - `set voxi.nrd2NeuracGuide 0`.
- **Tuning:** `set voxi.nrd2FfxClipWeight 1`, `set voxi.nrd2FfxDepthWeight 8`.
- **What to look at:**
  - Disocclusions while strafing, with and without the guide.
  - Lamp-lit rooms (the lamp-dominated case).
  - A fast pan for lag.
  - Mirrors and wet roads.
  - Ghosting behind movers.
- **NeuRaC:**
  - Window > Neural Visualiser views (Cached light, Coverage, Cascade, Cell state, and Trust once added).
  - `--neurac-view`.
  - `set voxi.giVisPathView 1` for the F2 path colours.
  - The `[Voxi] NeuRaC warm-up` log at frame 150 (valid cells per cascade; walls should no longer be red).
  - The `[Voxi] NeuRaC guide: X% covered` and `[Voxi] NeuRaC net:` log lines.
- **Resets and hooks:** change the sun time (relax), reload the level (reset), run `resetgihistory` (reset), and toggle `set voxi.neuracNet 0/1`. For a full manual reset, toggle `set voxi.giRestirVisibility 2` then `4`.
- **Reference:** `set voxi.ptMode 1` for the path tracer comparison.
- **Costs:** `--gpu-timing` and the spans listed in section 7.

## 11. UNVERIFIED

- The `denoiseFrame_` versus `rtFrameIndex_` parity relationship today.
- That `gNrd2DRes.a` is 1 on surfaces (pass 5 would use it as validity; the shader comment says so).
- That the G-buffer is in `RenderTarget` state when the guide pass runs, and that read-back barriers inside the scene pass are accepted (Nrd2 does the same).
- That the engine world unit is centimetres (assumed from `camPosCm` and `kCellSizeCm`).
- Which normal (`s.N`, shading or geometric) the twin uses versus the G-buffer normal in the guide.
- Whether `buildAllVariants` builds the NeuRaC twins.
- That `setVolume()` is called with a stable value outside level load.
- That nrd2.hlsl compiles to SPIR-V today.
- That the twin, with the larger scatter, fits registers on the RX 7800 XT.
- That `Mlp::recordTrain` and `recordInfer` accept a counter larger than `maxCount` (hence `CSRcNetFinish`).
- All timings in sections 5.5 and 7.
- Stage 2's visible benefit.
- Where the editor panel parses the NeuRaC view mode (for mode 5).

### Critical Files for Implementation
- C:\Users\User\Documents\Aver Engine\modules\render.denoise\shaders\nrd2.hlsl
- C:\Users\User\Documents\Aver Engine\modules\render.denoise\shaders\nrd2_resolve.hlsli
- C:\Users\User\Documents\Aver Engine\modules\render.voxi\shaders\voxi_neurac_io.hlsli
- C:\Users\User\Documents\Aver Engine\modules\render.voxi\src\VoxiRenderer.cpp
- C:\Users\User\Documents\Aver Engine\modules\render.voxi\include\aver\voxi\NeuRaCLayout.hpp