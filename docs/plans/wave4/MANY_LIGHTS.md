# Wave 4, feature #4: stochastic many-light direct lighting in Voxi (design contract)

Status: DESIGN, frozen for the implementing agent. 2026-10-11. Scope: thousands of shadowed lights lit by Voxi's unified light table with a bounded ray budget, on the staged ray-driven visible surface and at GI, reflection and path hits, denoised by NRD2. Not in scope: shadow maps per light (feature #3), light functions, volumetric-fog light shafts, emissive triangles as lights (blocked by a live patent, section 2), the render.pt reference tracer (stays at 32 lights), and any change to `AverLightRec` or `CLight` layout.

## 0. Decisions in one screen

1. **Upgrade the unified light table; no parallel light system.** The 80-byte `AverLightRec` list in t18 (sun as its directional entry, then the light grid) stays the single source. A **light tree** (BVH over the non-directional lights) is appended after the grid inside the same buffer, exactly as the grid was ("no new binding", `VoxiRenderer.cpp:4591-4596`).
2. **Candidate selection is a per-pixel tree descent, not a per-cell light set.** Each pixel draws M candidates from the tree with probability proportional to a conservative importance bound, resamples them by an exact-ish target (RIS), and traces S shadow rays. Rays per pixel are `1 + S` (+ at most 2 extra directional lights). This replaces the grid's top-24-per-cell cut for the tail, which silently drops every light past 24 (a bias, and it gets worse the more lights there are).
3. **No light, reservoir or visibility is ever carried from another pixel or another frame.** This is a patent decision, not a taste one: NVIDIA US 11,948,246 (in force to 2042) claims, in effect, ReSTIR DI's reuse (section 2). All noise reduction is in radiance space after shading: NRD2 (already patent-reviewed) plus TAA. That costs noise against ReSTIR DI at equal rays, and the budget table (section 8) is sized for it.
4. **The exact light stays.** Per pixel, `CSRdShadow` still picks the strongest light and gives it the sun's full shadow kernel (disc, history keyed by light, probe tiles). The tree samples the tail with that light excluded. By day the exact light is the sun; at night it is the dominant lamp, which is what removed the lamp spots.
5. **Hybrid by count.** Below `kFlatLightList` (48) lights, or wherever few lights reach a pixel, the shipped exhaustive path is kept untouched (it is exact and cheap). The stochastic path engages automatically above `voxi.manyLightsMin` lights (default 48). A scene with 48 or fewer lights renders byte-for-byte as today.
6. **NRD2 frames only in v1.** The stochastic tail is noisy by design and FidelityFX denoises AO, GI and reflections only (`UNIFIED_LIGHTS.md` line 100). FidelityFX/None frames keep the grid + ratio-estimator tail (the "legacy" path). A single-frame fallback filter for them is phase 6, optional.
7. **New code goes in compute passes plus two reads in Stage B.** The ray-traced `PSMainVoxi` variants and the single-pass kernel are at their register limit (three hangs, `UNIFIED_LIGHTS.md` "Constraints"). One new entry `CSRdManyLights` is authored **SM 6.5-clean** (no `ddx`, no SM 6.6 features) so the same source is the Vulkan implementation when the staged family lands there (section 3.9).
8. **Two new UAV slots, so table 0 must widen** (section 3.11): `kMaxBindingSlots` 25 to 28 is a prerequisite commit shared with texture streaming, which needs a slot too.
9. **One shipped exposure is found and fixed first** (section 3.10): the exact light's frame-to-frame hysteresis (`voxi.hlsl:2641-2654`) reads on the same patent. Step S0 removes it before any new code lands.

## 1. What exists today, and what is missing

| Need | Exists (file:line) | Missing |
|---|---|---|
| Light record | `AverLightRec`, 80 bytes, five `float4`, kinds point/spot/rect/directional (`aver_lights.hlsli:52-58`, `:34-39`); host mirror `PackedLight` and `packSceneLight` (`SceneLight.hpp:32-39`, `:67`); sun built as a directional entry after the lamps (`VoxiRenderer.cpp:4571-4588`) | Nothing for the record |
| Light list and cap | Importance-ordered from the camera, cut at `kMaxListLights = 4000` (`VoxiRenderer.hpp:663`, cut `VoxiRenderer.cpp:4554-4567`); emissive lamp draws become sphere lights (`:4489-4543`); scene `CLight`s via `appendSceneLightCandidates` (`:4235`) | A spatial acceleration over the lights that can be sampled by importance |
| Light grid | World cells, each the 24 most important lights at its centre (`kMaxLightsPerCell`, `:4351`, `:4717-4721`), up to 32 per axis, 50 cm cells, 200 m around the camera (`VoxiRenderer.hpp:666-667`); header record + cell table + index pool packed as floats in t18 (`:4733-4752`); GPU walk `rdLightsAt`/`rdLightIndex` (`voxi_rt.hlsli:826-861`); a list of at most 48 is walked whole (`:4625`, `:836`); canonical-order cache so a camera move reorders cheaply (`LightGridCanon`, `:4300-4464`) | An unbiased way to reach a light past a cell's 24; any cap-free candidate source |
| Visible surface, exact light | `CSRdShadow` picks the strongest light by `rdLightWeight` over the cell list, sticks to last frame's pick within 80% via `prevKey` (`voxi.hlsl:2641-2655`), runs the sun's full kernel (`:2688-2695`), writes `gRdLocalOut = (e0, key, -1, -1)` (`:2702`) | Hysteresis without frame-to-frame state (section 3.10) |
| Visible surface, tail | `CSRdTailVis` at quarter resolution: per 2x2 block the top `lightRaysPerBlock` (6) tail lights by cheap weight get one ray each, the rest share an irradiance-weighted fraction (`voxi.hlsl:2720-2798`); `CSRdTailFilter` 5x5 into `.z` (`:2804-2811`); Stage B shades every cell light exactly times that fraction (`rdTailLights`/`rdTailLightsSplit`, `:710-766`; call sites `:2121-2130`) | A tail estimator that covers all lights and is unbiased |
| Hit sites | `averDirectLights` (`voxi_pt.hlsli:178-268`): top 4 by cheap weight from the cell list, the strongest `lightRaysPerHit` (1) traced, the rest share their visibility; must stay ONE shading call in a loop (compile time) | The same unbiased tail at GI, reflection and path hits |
| Cheap ranking | `rdLightIrradianceCheap`/`rdLightWeight` (`voxi_rt.hlsli:1366-1397`); `rdLightKey` stable key (`:1321`) | A BRDF-aware target density |
| Shadow ray | `rdLocalShadow`: opaque, accept-first-hit, disc/rect sample (`voxi_rt.hlsli:914-952`) | Nothing |
| NRD2 inputs | Stage B writes demodulated D (u3) and S (u23); lamps and the exact light go through the filter (`NRD2.md` "Stage B"; `voxi.hlsl:2296-2313`, `:2121-2123`) | A way to add a stochastic tail to D and S |
| Settings | `localLights`, `lightRaysPerBlock` (6), `lightRaysPerHit` (1) (`Voxi.hpp:239-243`); console `EditorConsole.hpp:978-999`; tier-derived settings via `ladder::` and `Renderer::...ForQuality` (`Voxi.cpp:95-96`, `:204`, `Voxi.hpp:391`) | Many-light settings and ladder rungs |
| Reservoir code | `voxi_reservoir.hlsli` is per-pixel GI only and forbids "a world-space grid of stochastic light reservoirs" (`:20-22`) | Nothing: this design builds none (section 2) |
| Binding slots | Table 0 is full: `kVoxiSrvCount == kVoxiUavCount == 25`, `kMaxBindingSlots = 25` (`RHIResources.hpp:218`), u24 = froxel volume (`VoxiRenderer.cpp:147-229`) | Two UAV slots (section 3.11) |
| Vulkan | `computeInScenePass` and `gBuffer` false (`VulkanDevice.cpp:1084-1086`); caps report shader model 6.5 at most (`:1114-1116`); the staged compute entries compile at profile 66 (`VoxiRenderer.cpp:8201`); `buildLocalLights` only runs when `computeInScenePass` (`:4482-4483`, `:4573`), so **Vulkan has no light list at all today**; the bindless table and inline ray queries exist (commit 449140a2) | Everything staged; see section 3.9 |
| Raster | PSMainVoxi lights its fragment from a 32-lamp working set (`kMaxLocalLights`, `VoxiRenderer.hpp:662`, `rdLocalLightsShade` `voxi.hlsl:614-628`); the G-buffer holds viewZ, normal+roughness, velocity, no albedo (`RHI.hpp:737-753`, `voxi.hlsl:1907-1913`) | Anything beyond 32 lights in raster (section 3.8) |

Also noted: the repository root carries an untracked stray `voxi_combined.hlsl`, and `voxi_rt.hlsli` has uncommitted edits from another task at the time of writing; the implementer re-reads both files before editing.

## 2. Patent check (LIVE, 2026-10-11)

Method: Google Patents pages and USPTO print PDFs (claims OCR'd from the page images where the text layer was absent), Justia (blocked), web search. **This is an engineering mapping of claim elements to this design, not an infringement or clearance opinion**; counsel decides. Maintenance-fee status could not be read from any source used; "in force" below means granted and not shown as lapsed. Applications published after about 2025-04 are invisible to any search (18-month rule). Not swept: AMD, Intel, Qualcomm, Sony, Activision, Unity filings on light culling or light trees.

| Number | Owner, status | What the independent claim requires (plain words) | Effect on this design |
|---|---|---|---|
| **US 11,948,246 B2** "Spatiotemporal resampling with decoupled shading and reuse" | NVIDIA, granted 2024-04-02, expiry about 2042-03-24; continuation 18/619,804 pending | Claim 1: select first lights from the scene's lights; select second lights from lights associated with pixels in this frame or a **prior frame**; select from the first and second sets a light to use for a subsequent frame; render the pixel from both sets. Claims 8 and 16 are the system and medium forms (claim 8 adds "spatially proximate pixels"). Dependents: reuse a light's visibility from the prior frame or from neighbouring pixels (2, 3, 9, 10, 17, 18), store visibility (5, 12, 20), resampling (6, 7, 14, 15, 21, 22) | **This is ReSTIR DI.** Element "second lights from other pixels or the prior frame" is what the design removes: candidates come only from the pixel's own tree descent over the current light table; nothing selected for any pixel (this frame or last) is read by any other pixel or frame. See also section 3.10, where the shipped exact-light hysteresis is mapped to this claim |
| **US 12,361,635 B1** "Initial candidates in spatiotemporal resampling" | NVIDIA, granted 2025-07-15, expiry about 2043 | Per pixel, generate initial candidate lights; add at least one light identified as important for a **prior image** to the initial candidates; weights and counts for the current image with the prior weights and counts **discarded**; importance resampling over the set; shade | No prior-image light is ever a candidate. This also rules out Epic-style "visible light lists from last frame guide the next frame" ray guiding |
| **US 12,299,801 B2** "Grid-based light sampling for ray tracing applications" (ReGIR) | NVIDIA, granted 2025-05-13, expiry about 2040-09 | Sample a first set of lights **in each of a plurality of subdivisions of the virtual area, based on their contribution to that subdivision**; store them in a cell of a data structure per subdivision; **sample a second set** from a subset of the first set in each cell; use it to render a pixel. Dependents: uniform grid (2, 3, 18), proportional probability (4, 11, 19), RIS (13), nearer cells give more lights (8, 16) | The tree is a hierarchy over the lights, not a record of sampled lights per spatial subdivision, and nothing is sampled "for a cell". The shipped grid is a deterministic cull + importance cut, not a sample, and is not used as the stochastic source. **Rule: no stochastic per-cell or per-tile light selection is ever stored** (matches `voxi_reservoir.hlsli:20-22`). Counsel to confirm a light BVH is outside "subdivisions of a virtual area" |
| **US 11,663,773 B2** and **US 12,361,633 B2** "Using importance resampling to reduce the memory incoherence of light sampling" | NVIDIA, granted 2023-05-30 / 2025-07-15 | Select a set of lights by random factors, then a (proper) subset of it sized to fit a predicted processor cache, render pixels from the subset (per tile) | No pre-randomised light subsets and no cache-size-driven subset selection |
| **US 11,315,310 B2** (+ US 2022/0198746, US 2025/0182387 pending) "Reservoir-based spatiotemporal importance resampling utilizing a global illumination data structure" | NVIDIA, granted 2022-04-26 (3.5-year fee status unverified; its grace period ends 2026-10-26) | Claim 1: reservoir importance resampling to compute global illumination on visible surfaces **using a world-space data structure** (RTXGI probes). US 2025/0182387 claim 1: in a **single step** sample one point from lights AND geometry together with probability proportional to its light value, then cast the shadow ray | Direct lights only; no world-space GI structure is consulted for selection; emissive geometry is never pooled with lights in one draw. Emissive triangle lights are therefore **not** designed here |
| **US 2026/0162355 A1** (parent 18/464,818) | NVIDIA, published 2026-06-11, pending | Segment the environment into regions (claim: non-cubic voxels); from values determined for two or more lights by a first set of rays decide a second set of rays to sample; sample them; render | Ray counts are fixed per pixel (`S`), never allocated from measured per-region light values. No spatial-hash light importance cache |
| US 10,740,954, US 11,600,036, US 12,482,168 (shadow denoising) | NVIDIA | Anisotropic kernels from light and occluder geometry; self-guided spatiotemporal clamps from sample distributions | Already designed around in `UNIFIED_LIGHTS.md` and `NEURAA_NRD.md` rules 1, 7, 9; this design adds no denoiser |
| Epic MegaLights | Epic Games | No patent or application found by title, inventor (Narkowicz, Costa) or technique. Public sources: SIGGRAPH 2025 talk, UE 5.8 docs | Only publicly described ideas are used, and not the ones that overlap the NVIDIA claims above (visibility-fed ray guiding is excluded by US 12,361,635) |
| Light BVH sampling (Conty Estevez and Kulla 2018; Lightcuts, Walter et al. 2005; Moreau et al. 2019) | Sony Pictures Imageworks / Cornell / academic | No patent found by title or technique; published papers | Importance bound and descent follow the papers; implementation is written from the papers' formulas (no code copied; pbrt-v4/Cycles/Arnold's trees are public references only) |
| ReSTIR DI paper (Bitterli et al. 2020) | academic | No patent whose claim 1 matches the paper's own title was found; the family is claimed through the rows above | Cited for RIS maths only (`voxi_reservoir.hlsli:3-14` already cites it) |

**Cost of the design-around, stated plainly.** Without cross-pixel and cross-frame reuse, the stochastic tail carries more variance than ReSTIR DI at the same ray count. The mitigations are all legal here: (a) candidate quality (a BRDF-aware target, M = 8 to 16 candidates that need no ray), (b) NRD2 over the composed lighting, (c) TAA on the final colour, (d) deterministic exact handling of the dominant light, (e) an exhaustive path when few lights reach a pixel. If the owner or counsel clears US 11,948,246 (licence, invalidity, or a claim construction that excludes it), temporal and spatial reservoir reuse is the first thing to add on top of section 3.3; the pass is structured so it slots in (section 9, Q1).

## 3. Design

### 3.1 The light tree (CPU)

New `modules/render.voxi/include/aver/voxi/LightTree.hpp` + `src/LightTree.cpp`, depending on Aver.Core only, so the whole structure is unit-testable without a device (the `SceneLight.hpp` precedent).

- **Input:** the frame's non-directional lights (`PackedLight`), i.e. the list `buildLocalLights` already sorted and cut, before the header/grid are appended. At most `kMaxTreeLights = kMaxListLights` (4000). Directional lights are not in the tree (the sun reaches every point; section 3.3 handles them).
- **Leaf quantities** (per light): position `p`; emitter radius `r` (`posRadius.w`; 0 for rect, whose half extents widen the bound instead); range `R` (`radianceRange.w`); power `Phi` = luminance of `radianceRange.rgb` in **irradiance-at-1-m units** (for a rect, times its area in m^2, because `I = L*A` and `E1m = I/1 m^2`, `SceneLight.hpp:61-64`); orientation cone: axis `a` (`axisKind.xyz`), `cosThetaO = 1` for a spot/rect axis, `-1` (omni) for point/sphere; emission spread `cosThetaE` = spot outer cosine, 0 for rect (the front hemisphere, `thetaE = pi/2`), `-1` for omni. IES/cookie lights are bounded as their kind (the IES table only dims).
- **Build:** Morton-sort the light positions (30-bit, scene bounds), split each range at the highest differing Morton bit (median when all equal), one light per leaf, depth capped at 40. Bottom-up merge: AABB of positions; `Phi` sum; `R` max; emitter radius max; the orientation cone merged by the standard cone union (`theta_d = angle(a1, a2)`; if `min(theta_d + thetaO2, pi) <= thetaO1` keep cone 1, symmetric for 2, else `thetaO' = (thetaO1 + theta_d + thetaO2)/2` with the axis rotated from `a1` toward `a2` by `thetaO' - thetaO1`, omni if `>= pi`); `thetaE = max`. Cost target for 4000 lights: 0.3 ms single thread. NaN or negative power refuses the tree for the frame (log once; legacy path).
- **Canonical order and caching.** The list reorders every frame the camera moves (importance from the camera). Like `LightGridCanon`, build the tree once over the lights in canonical (byte) order and keep a `fill()` that rewrites only the leaves' list indices when the same set reappears in another order (`VoxiRenderer.cpp:4404-4464`). Key = the same set key the grid uses (`setKey`, `:4642-4649`). A changed set (a moving or flickering light) rebuilds; a **refit** (same topology, new bounds) is phase 7.
- **Packing: one `AverLightRec`-shaped (80-byte, 20-float) record per node**, appended after the grid pool in t18; `TreeNodeRec` (host) with a `static_assert(sizeof == 80)`:

| floats | field |
|---|---|
| 0-2, 3 | AABB min of light positions (cm), `Phi` |
| 4-6, 7 | AABB max, max range `R` (cm) |
| 8-10, 11 | cone axis (unit; zero length = omni), `cosThetaO` |
| 12 | `cosThetaE` |
| 13, 14 | left child record index, right child record index. **Leaf:** 13 = -1, 14 = the light's **list index** |
| 15 | parent record index (root = -1); needed to walk a leaf's path (section 3.3) |
| 16 | max emitter radius in the node (cm) |
| 17 | flags: bit 0 leaf, bit 1 every light in the node is no-shadow |
| 18, 19 | spare (zero) |

  The **tree header** is one more record placed first (`treeHdr`): `posRadius.x` = root record index, `.y` = node count, `radianceRange.x` = default S, `.y` = default M (hit M in `.z`), `.w` = weight clamp (0 = off), `axisKind.x` = 1 when valid. Its index is stored in the existing grid header's unused `right.x` (`VoxiRenderer.cpp:4733-4741` leaves `right` zero), so **no constant-buffer field changes** (VoxiFrame has three mirrors; this avoids them). `right.x == 0` means no tree.
- **Upload:** part of the grid tail, so the existing ring-slot caching (`rdGridSlotKey_`, `:4781-4792`) rewrites it only when the tree key changes. Size 2N x 80 B = 640 KB at 4000 lights. `rdLocalLightCapacity_` growth already handles a larger tail.
- **When built:** `manyLights != 0`, `lightCount > manyLightsMin` (and not flat), and ray tracing on. Otherwise `right.x = 0`.

### 3.2 Importance and descent (HLSL)

New `modules/render.voxi/shaders/voxi_lighttree.hlsli`, included from `voxi_rt.hlsli` directly after `rdLightIndex` (`:861`) so `voxi_pt.hlsli` sees it. It declares no resource (it reads t18 through `rdListFloat`-style single-`float4` loads, the register-pressure rule at `voxi_rt.hlsli:810`). `rdTreeNode(i)` returns two `float4`s per field group, loaded one `float4` at a time.

- `float rdTreeImportance(node, float3 p, float3 n)`: the conservative bound of Conty Estevez and Kulla 2018 adapted to this record's units:
  - `c` = box centre, `rb` = half-diagonal + max emitter radius, `d2 = |c - p|^2`. Zero if `sqrt(d2) - rb >= R` (outside every light's range window; this zero is exact, so a zero never hides a lit light).
  - Inside the box (`d <= rb`): geometric cosines 1, `d2 := rb^2`. Else the box subtends `sinThetaU = rb/d`.
  - Receiver cosine: `cos(max(0, thetaI - thetaU))` where `thetaI` is the angle between `n` and `c - p`, evaluated without trig as `cosI*cosU + sinI*sinU` when `cosI < cosU`, else 1. A node that may contain a rect keeps a floor of 0.1 as `rdLightIrradianceCheap` does (`voxi_rt.hlsli:1387-1390`).
  - Emitter cosine: `thetaW` = angle between the axis and `p - c`; `thetap = max(0, thetaW - thetaO - thetaU)`; factor `cos(thetap)` if `thetap < thetaE` else 0; omni = 1.
  - Window upper bound: `win(max(d - rb, 0)/R)` with `win(x) = (1 - x^4)^2`, the same window `aversLightEval` applies (`aver_lights.hlsli:153-155`).
  - `I = Phi * 1e4 * cosI' * cosE' * win / max(d2, rb^2)`. The `1e4` and the `d` in cm match `rdLightIrradianceCheap` (`:1396`).
  - Property (tested on the CPU twin): for any point and normal, `I(node) >= sum of the leaves' rdLightIrradianceCheap luminance` (within 1e-4 relative).
- `bool rdTreeSample(p, n, float u, uint exclude, out uint lightIdx, out float pdf)`: from the root, at each inner node compute both children's `I`; probability `pL = IL/(IL+IR)`; descend by `u < pL` and rescale `u` (`u/pL` or `(u-pL)/(1-pL)`), `pdf *= p_child`; a node whose children are both 0 returns false; depth loop bound 40 (`[loop]`, no recursion, no stack array). At the leaf `lightIdx = f14` and `pdf` is `q(x)`.
- `float rdTreePdfOf(leafRecord, p, n)`: the same probability product along the leaf's path, walked **upward** by the parent pointers (so `q(x)` of a given light is computable for the excluded light); one walk costs one traversal.
- Both are SM 6.0 arithmetic and loads only (no derivatives, no wave ops, no groupshared).

### 3.3 The estimator

Per shading point `x` (visible pixel or a hit), normal `N`, view `V`, with `e0` the exact light (or none):

1. **Directional lights other than `e0`** (a moon beside a sun; at most 2): each is shaded exactly with one ray, added to the result. They are not in the tree.
2. **M candidates.** For `j < M`: draw `u_j`; `rdTreeSample` with `exclude = e0`: if it returns `e0`, redraw (at most 3 tries); a candidate that fails all tries has weight 0. With `q0 = rdTreePdfOf(e0)` (computed once per point, only if `e0` is a local light), the candidate density is `q_eff(y) = q(y) * (1 + q0 + q0^2)` (a sub-density; the failed mass `q0^3` is a candidate of weight 0). This keeps the estimator **unbiased** and avoids the spike a dominant `e0` would cause (95% of draws wasted, the rest weighted 20x).
3. **Target.** `phat(y) = lum(rdLightIrradianceCheap(y, x, N)) * (kdLum/pi + F0lum * D_GGX(NoH, a) / (4 * max(NoV, 0.05) * max(NoL, 0.05)))`, with `NoL`, `NoH` to the light's centre, `a = rough^2` widened by the emitter size as `rdLocalLightAt` does (`voxi.hlsl:605`). It is only a density: any positive function gives an unbiased result, so the surface terms come from the cheap factor-level inputs (`kdAlbedo`, `F0`, roughness) and need no texture fetch for the selection. Skip (weight 0) where it is 0.
4. **Resample into S slots, streaming.** One pass over the M candidates updates `S <= 4` statically unrolled slots (selected index, selected `phat`) and one shared `wsum = sum phat_j / q_eff_j`; slot `s` accepts candidate `j` with probability `w_j / wsum_j` using its own random. **No runtime-indexed arrays** (the scratch-memory hang, `aver_lights.hlsli:103-108`). Contribution weight of slot `s`: `W_s = wsum / (M * phat_s)`.
5. **Roulette.** A slot whose `phat_s * W_s` luminance is under `tau` survives with probability `p = that/tau` and then has weight `W_s / p` (unbiased). `tau = (e0 irradiance luminance) / 256`, or `1e-4` with no `e0`. This replaces the biased 1/128 drop in `rdTailFloor` (`voxi.hlsl:704`) and removes most rays from far, dim lights.
6. **Shade and trace, one call site in a loop** over the slots (an inlined shading call per light multiplied compile time, `UNIFIED_LIGHTS.md` "Hits"): `aversLightEval`, `rdLocalLightAt`-style roughness widening, `averDirectTerms`; if the light casts shadows, `rdLocalShadow` (`voxi_rt.hlsli:914`) with `u2` from the pixel's stream; `contrib = BRDF * radiance * cos * V * W_s / S`. Diffuse+subsurface and specular accumulate separately (the D and S buckets).
7. **Clamp** (`voxi.lightWeightClamp`, default 0 = off): `w_j` capped at `clamp * (wsum / M)` before streaming. NRD2's own despeckle (cap at 2x the 5th brightest of 5x5) handles what remains; the clamp is a bias knob for scenes that still sparkle.
8. **Random numbers.** The PCG stream of `voxi_reservoir.hlsli` (`giInitRng(pixel, frame, salt)`, engine-free) for `u_j` and the shadow samples; the first dimension is Cranley-Patterson rotated per 4x4 tile by interleaved gradient noise so neighbouring pixels draw different candidates (the filter then averages them). The frame index is mixed in: **there is no accumulation across frames in this pass**.
9. **Glass and filter fraction.** The pass also writes `gRdLocalOut.a` = (sum of shadowed tail luminance)/(sum of unshadowed tail luminance) over its S slots (1 if none, -1 where it did not run), exactly the semantics `CSRdTailVis` writes, so `CSRdTailFilter` (kept, `:2804`) and the glass replay's `rdLocalVisFiltered` (`voxi.hlsl:1745`, `:381-384`) work unchanged.

**Unbiasedness argument** (tested numerically, section 5): conditioned on `e0`, each slot's expectation is `sum over lights y != e0 of f(y)V(y)`, because candidates are iid from a known (sub-)density, RIS divides by it, and the roulette and the zero-weight failures are accounted. Where `phat = 0` but `f > 0` the estimator is biased low; with the cheap factor above that happens only for a rect's back-lobe or a cookie's edge and is documented, as the shipped ranking is.

**Hits (GI, reflection, path).** `averDirectLights` (`voxi_pt.hlsli:178`) gets the same estimator with `M = lightHitCandidates` (4), `S = lightRaysPerHit` (1; clamp 4), no roulette floor from `e0`, and the shipped exhaustive loop kept for `anyComplex`-free short lists. The new branch is `if (rdTreeLive())` inside the existing `#elif AVER_RD_LAMPS` arm; still one shading call in a loop. The "lite" arm (ray-traced raster, glass, single pass) is untouched. The directional lights at a hit stay as today (shadow map where allowed, else one ray). `rdLocalEmitterCarried` (`voxi_rt.hlsli:893`) is unchanged: a listed lamp still drops its own glow at hits, and with the tree every listed lamp is reachable, so the "past the cell's 24" exception shrinks to lights beyond `kMaxListLights`.

### 3.4 `CSRdManyLights` (the visible surface)

- **Entry:** `[numthreads(8, 8, 1)] void CSRdManyLights(uint3 tid : SV_DispatchThreadID)` in `voxi.hlsl`, beside `CSRdTailVis` (`:2720`). Compiled **cs_6_5 / SM 6.5** (D3D12 and Vulkan), with `rasterDefs(csDefs)` like the other staged entries; `compile("CSRdManyLights", Compute, 65, ...)` at `VoxiRenderer.cpp` next to `:8201`. No `ddx`/`ddy`; surface footprint comes from the neighbouring primary rays exactly as `CSRdRefl` reconstructs it (`rdPrimaryRayDir`, `rdHitSurface`, `voxi.hlsl:867-880`).
- **Per pixel:** read `gRdVisBuf` (u11); sky pixel or `e0 < 0` or `s.model == UNLIT`: write zeros and `.a = -1`, return. Else `rdSurfaceFromRecord`, `rdHitSurface(AVER_RT_HIT_FULL)` (the same textured surface Stage B shades; `gAverFbOn` stays false so no texture-streaming feedback is recorded, see `TEXTURE_STREAMING.md`), `averRtCutoutPolicy(AVER_RD_CUTOUTS_SHADOW, false)`. Skip the whole tree descent when the root importance is under 1e-6. Then section 3.3.
- **Outputs (this pass is the only writer):** `gRdManyDiff` (RGBA16F, rgb = diffuse + subsurface radiance of the tail incl. albedo, a = 0) and `gRdManySpec` (RGBA16F, rgb = specular radiance, a = 0), full scene viewport, **written for every pixel** so Stage B reads unconditionally; `gRdLocalOut.a` (section 3.3 item 9). Radiance, not irradiance: Stage B demodulates by its own `kdAlbedo`/`FssEss` as it already does for `nrdD`/`nrdS` (`voxi.hlsl:2307-2310`).
- **Resources:** table-0 UAVs `kManyLightDiffUavSlot`, `kManyLightSpecUavSlot` (section 3.11), declared `RWTexture2D<float4>` in `voxi.hlsl` beside `gRdSunVisTex` (`:260`) with `register(uN)` equal to the constants (`static_assert` in `giTableKinds`, as for the froxel slot). Created in `ensureRdStagedResources` when `rdManyLightsWanted()`, 1x1 placeholders otherwise, released with the others (`VoxiRenderer.cpp:6420-6500` pattern). VRAM: 2 x W x H x 8 B = 14.7 MB at 1280x720 render size, 59 MB at 2560x1440 (only while active; phase 7 packs spec only when a glossy surface exists).
- **Order and barriers** (`recordStagedRayDriven`, `VoxiRenderer.cpp:5577-5727`): replaces `recordTailVis` when `manyLightsLive`. It reads `gRdLocalOut.x` (CSRdShadow) and `gRdSunVisTex.a`: `uavBarrierTexture(rdLocalOutThisFrame_)` and `(rdSunVisTex_)` before it, as `:5718-5719`; it joins the barrier-free group with GI, sky occlusion and reflections (none reads its outputs); then `uavBarrierTexture(rdLocalOutThisFrame_)`, `CSRdTailFilter`, as `:5721-5722`; `uavBarrierTexture` on both new textures before the Stage B draw. GPU span `"Voxi RD many lights"` via `ScopedGpuStat`; the per-stage timing mode keeps its extra barriers (`:5516-5529`).
- **Stage B** (NRD2 variant only; one branch, two loads): at `voxi.hlsl:2121-2124`
  - `if (rtManyLights()) { nrdD += gRdManyDiff[px].rgb; nrdS += gRdManySpec[px].rgb; } else { rdTailLightsSplit(...); }` still inside `lightSlots.x >= 0` and `!ptRef`.
  - `rtManyLights()` = `(rtGiShadowBits() & 2048u) != 0u`, set with the other NRD2 bits (`VoxiRenderer.cpp:7034-7037`) when the pass ran this frame. Non-NRD2 Stage B (`:2127-2129`) is untouched.
  - In Path Tracing Reference mode (`ptRef`) the pass is not dispatched (Stage B's path does its own NEE through `averDirectLights`).

### 3.5 Engage rules and fallbacks

`manyLightsLive` this frame = `settings.manyLights != 0` && D3D12 staged ray-driven frame && `nrd2Frame_` && tree valid && `rdManyLightsCsPso_` compiled && both textures present && (`manyLights == 2` or `lightCount > manyLightsMin`). Any failure logs once (`AVER_WARN` for compile or allocation, `AVER_INFO` for an expected skip) and the frame runs the legacy tail; nothing is half-applied. `manyLights == 3` (exhaustive, ground truth, at most 512 lights): `averDirectLights` walks every list light with one ray each and the visible-surface pass is not used; this is the reference the unbiasedness gate compares to.

### 3.6 Settings, console, ladder, project, UI

- `Voxi.hpp` Settings, one banner-commented block after `lightRaysPerHit` (`:243`): `u32 manyLights = 1` (0 legacy, 1 auto, 2 force, 3 exhaustive), `u32 manyLightsMin = 48`, `u32 lightSamples = 1` (S, 1 to 4), `u32 lightCandidates = 8` (M, 2 to 16), `u32 lightHitCandidates = 4` (1 to 8), `f32 lightWeightClamp = 0`, `u32 debugStressLights = 0`, `u32 debugStressSeed = 1`.
- **Console** (`EditorConsole.hpp`, after `voxi.lightRaysPerHit` `:995`): `voxi.manyLights`, `voxi.manyLightsMin`, `voxi.lightSamples`, `voxi.lightCandidates`, `voxi.lightHitCandidates`, `voxi.lightWeightClamp`, `voxi.debugStressLights`, `voxi.debugStressSeed`, each with the one-line description the file requires. `--set voxi.x v` already works from the CLI (`SandboxMain.cpp:242`), so no new flag.
- **Quality ladder** (`QualityLadder.hpp`, with `Renderer::...ForQuality` forwards in `Voxi.cpp` beside `:204` and the derivation at `:95-96` keyed on `rayTracing`): tier-derived, struct defaults = Medium's rung.

| Rung | `lightSamples` S | `lightCandidates` M | `lightHitCandidates` |
|---|---|---|---|
| Off, Low | 1 | 4 | 2 |
| Medium (default) | 1 | 8 | 4 |
| High | 2 | 8 | 4 |
| Epic | 4 | 16 | 8 |

- **Project key** `RENDER.MANYLIGHTS` (0/1/2; 3 is console only): `OcProject.hpp/.cpp` (+ `FORMAT_SPECS.md` key list), `ProjectRenderApply.hpp` (manifest apply), `SandboxSettings.cpp` (Rendering page: "Many lights" combo Auto/Off/On under the Lights group, tooltip stating the NRD2 requirement and that a 48-light scene is unchanged).
- **Debug views** (`VoxiRenderer::ViewDebug`, `VoxiRenderer.hpp:65`): `LightCount = 7` (heat map of lights reaching the pixel = tree leaves with nonzero importance, capped at 255; counts via a bounded traversal in the debug build of Stage B) and `ManyLightsRaw = 8` (the pass's diffuse+spec before NRD2). Implemented in `PSRayDriven` like views 2 to 6.
- **Stress** (`debugStressLights`): in `appendSceneLightCandidates` (`VoxiRenderer.cpp:4235`), inject N synthetic point lights as ordinary `SceneLight`s (shadowed, radius 5 cm, log-uniform 20 to 400 cd, hue from the seed) on a seeded jittered 3-D grid, +-30 m horizontally and -1 to +5 m vertically around the camera. Pure function of `(seed, N, eye snapped to 1 m)` so captures repeat. Costs nothing at 0.
- No graph nodes (section 4).

### 3.7 Raster

v1 behaviour, stated plainly: **raster keeps its 32-lamp working set** (`PSMainVoxi`, `rdLocalLightsShade`, `kMaxLocalLights`), now chosen from the same importance order. Nothing in the register-limited fragment shader changes. The reason is the missing albedo: a deferred many-light pass over the raster G-buffer cannot colour its result (the G-buffer has viewZ, normal+roughness and velocity only, `RHI.hpp:737-753`).

Phase 8 (optional, needs the owner's yes because it changes the RHI G-buffer, which geometry and materials also read): add an albedo+F0 target (RGBA8) to the G-buffer pipelines, then run `CSManyLightsRaster` (the same estimator, surface from the G-buffer, position from viewZ, tail added additively to the scene colour before NRD2/tonemap), with PSMainVoxi's working set reduced to the 8 strongest so the two do not double count (`rdLocalEmitterCarried` rule, `voxi_rt.hlsli:893`). Not specified further here.

### 3.8 Light ranking in the list (unchanged but noted)

`kMaxListLights` stays 4000 in v1. Lights past the cut (far, dim) are dropped as today. Raising the cap to 16384 is a one-constant change once the tree build time (target 1.2 ms for 16k) and VRAM (2.6 MB) are measured: Q5.

### 3.9 Vulkan

State today: no light list, no staged passes, no SM 6.6 (`VulkanDevice.cpp:1084-1086`, `:1114-1129`; `VoxiRenderer.cpp:4482-4483`, `:7344`). "Parity" for this feature therefore means: **nothing in this design is D3D12-specific, and every Vulkan blocker is named**.

| Item | Design rule | Blocker owner |
|---|---|---|
| `CSRdManyLights`, the tree includes | SM 6.5-clean HLSL (no `ddx`, no SM 6.6 intrinsics, no `ResourceDescriptorHeap`, no wave intrinsics, group size 8x8); added to `VoxiSpirvCompileTest` at 6.5 so the SPIR-V compile is a gate from phase 2 | this feature |
| Host code | Only RHI calls (`setPipeline`, `dispatch`, `uavBarrier*`, `setUav`); no D3D12 types | this feature |
| Table widening | `kVkUavBindingBase = kMaxBindingSlots` (`VulkanCommon.hpp:622`) renumbers every UAV binding; shaders recompile with `-fvk-u-shift`, nothing hand-written changes | P0 (section 3.11) |
| `computeInScenePass` | Must become true on Vulkan for `buildLocalLights` to publish a list | the Vulkan staged-passes task (plan item V) |
| Staged entries at profile 66 | `CSRdShadow` and others use `rtShadowTemporal` derivatives; the Vulkan task must give them an SM 6.5 form (explicit footprints, as `CSRdRefl` does) or enable `VK_KHR_compute_shader_derivatives` | the Vulkan staged-passes task |
| Ray queries, bindless table | Present (commit 449140a2); `RayQuery` with `ACCEPT_FIRST_HIT` and the set-5 table are what `rdLocalShadow` and `rdHitSurface` use | none |
| Mesh-shader variants refused | Irrelevant to compute | none |

When the Vulkan staged task lands, enabling many-lights there is: the pass's pipeline compiles (already proven by the SPIR-V test), the two textures bind, the barriers record. Verification on Vulkan is the A/B in section 5 against D3D12 at the same pose (mean per-pixel difference, the metric of commit 449140a2). Until then `manyLights` is a no-op on Vulkan and says so once in the log.

### 3.10 S0: the shipped exact-light hysteresis (a finding, fixed first)

`CSRdShadow` keeps last frame's exact light for a pixel while it delivers at least 80% of the strongest: `prevKey = gRdLocalHist.Load(pixel).y` then `if (... rdLightKey(light) == prevKey) { prevE0 = j; ... }` then `if (prevE0 ... wPrev >= 0.8 * w0) e0 = prevE0` (`voxi.hlsl:2641-2654`). Mapped to US 11,948,246 claim 1: first lights = the scene's lights evaluated this frame; second lights = the light associated with this pixel in a prior frame; select between them for the pixel; shade from both. The claim has no "resampling" or "reservoir" element. This is a mapping, not an opinion, but it is a live exposure in code that already ships, independent of this feature.

Design-around (small, first commit, behind no flag): make the choice a pure function of the current frame. Among lights with `w >= 0.8 * w0`, take the one with the smallest `rdLightKey` (the stable position hash, `voxi_rt.hlsl:1321`), not the prior pick. `gRdLocalOut.y` keeps carrying the key for the exact light's own shadow history (`rtShadowTemporalEx` is keyed by it). Effect: near-equal candles no longer trade places because of noise, only where the geometry (weights) really moves; a swap along a contour is static in world space rather than frame-to-frame. Accept if exact-light swaps per pixel per frame in the fly-by capture (`--cam-wobble 30 16 --frames 200`) are within 2x of the shipped value and the spots metric does not move (section 5). If it fails, the fallback is a coarser band (0.6) on the same rule, not the prior pick.

### 3.11 Binding slots (prerequisite P0, shared)

Table 0 is full (25 SRV, 25 UAV, `kMaxBindingSlots = 25`, `RHIResources.hpp:218`). Two UAVs are needed here; `TEXTURE_STREAMING.md` needs one (its symbolic `kTexFeedbackUavSlot`, "today u25"). That doc widens `kVoxiUavCount` but does not mention that `kMaxBindingSlots` must grow too (every per-set array and the Vulkan binding base are sized from it, and the variant key packs counts in 5 bits, so the ceiling is 31).

- **P0 (one commit, whoever needs a slot first):** `kMaxBindingSlots` 25 to **28**; `kVoxiUavCount` to 25 plus the number of new slots; extend `giTableKinds` (`VoxiRenderer.cpp:147-229`) and rewrite its `static_assert` to name every trailing slot; D3D12 root signature and Vulkan layouts follow from the constants (check `D3D12Device.cpp:7887-7898`, `VulkanResourceFactory.cpp:218-257`, `VulkanRegisterMap.hpp:37-41`).
- **Allocation by order of landing**, appended after the highest existing slot; symbolic constants (`kTexFeedbackUavSlot`, `kManyLightDiffUavSlot`, `kManyLightSpecUavSlot`) next to `VoxiFroxelFog::kVolumeUavSlot`, shader `register(uN)` hand-matched and `static_assert`ed. This feature must not take a number streaming left symbolic, and vice versa.
- If the owner refuses the widening: fallback is one packed texture (RGBA16F diffuse rgb + spec luminance in alpha, spec colour rebuilt in Stage B as `specLum * F0-tinted`), at the cost of coloured tail speculars on metals (Q2).

## 4. Graph nodes

None. Lights are `CLight` components (`LightDetails`, `SceneLightFeed`) that scripts already reach through the entity/component nodes; the feature adds no component field, no C# ABI and no event. Authoring surface is the Rendering settings page, the `RENDER.MANYLIGHTS` key and console variables only. A per-light "keep exact" flag (the analogue of a per-light opt-out) is not added (Q6).

## 5. Tests and verification

**A. CPU, `tests/render.voxi/` (new `src/LightTreeTest.cpp`, linked to Aver.Core, `AVER_REPO_ROOT` like `VoxiRtSeqTest`, registered in `tests/render.voxi/CMakeLists.txt`).**

| Case | Asserts |
|---|---|
| Structure | One leaf per light; node count 2N-1; every inner node's AABB contains its children's; `Phi` sums; cone contains children's cones; parent pointers consistent; depth <= 40; N = 0, 1, 2, 4000, 4000 coincident lights, all-no-shadow |
| Canonical order | Same lights in any order: identical nodes after `fill()` modulo leaf indices; a changed light changes the key |
| Packing | `sizeof(TreeNodeRec) == 80`; field offsets equal the table in 3.1; the HLSL constants in `voxi_lighttree.hlsli` (read through `AVER_REPO_ROOT`) match the header |
| Bound | For 1e5 random (point, normal, node): `I(node) >= sum of leaf cheap luminance` (1e-4 relative); `I = 0` only where every leaf is out of range |
| PDF | At random points, `sum over leaves of q(leaf) = 1` (1e-5); `rdTreePdfOf` (path walk) equals the descent's pdf |
| Unbiasedness | CPU twin of 3.3 on a 64-light scene with a stub visibility: mean of 2e6 estimates equals the brute-force sum within 0.5% at M = 1, 4, 16, S = 1, 4, with and without `e0` excluded (incl. the 3-try multiplier) and with roulette on |
| Degenerate | NaN power refuses the tree; a point inside a node's box is finite; all-zero importance returns false, no NaN |

**B. Shader compile (existing gates):** `VoxiShaderCompileTest` gains `CSRdManyLights` (D3D12 DXC, 6.6 and 6.5) and the new includes in every variant `voxi.hlsl` already builds; `VoxiSpirvCompileTest` gains `CSRdManyLights` at 6.5 (the Vulkan gate). The 59-variant matrix must still compile (`UNIFIED_LIGHTS.md` compile-time note: keep one shading call in a loop).

**C. Headed GPU verification (implementer reports numbers, does not claim).** Owner rules: headed, visible window, 1440p screenshots when the owner is remote, GPU idle check first (`aver-perf-measuring-traps`), restore the owner's `editor.ini`/render scale after any crash (`aver-crash-cookie-resets-owner-scale`).
1. **TDR safety first** (a hang resets the driver): NewSponza_Night default view, `--denoiser 0`, `1`, `2` and `--pt 3`, with `voxi.manyLights 2`, in that order, on a 60-light scene before any stress.
2. **Unbiasedness on the GPU:** scene with 128 stress lights (`--set voxi.debugStressLights 128`), reference = `voxi.manyLights 3` (exhaustive) at `--denoiser 1 --set voxi.ptMode 1`, 1500 frames; compare `voxi.manyLights 2` (`--denoiser 1`, raw D+S recomposed with `voxi.nrd2Bypass 1`): mean luminance ratio within 1% of 1.0, per-16x16-block error reported.
3. **Image quality with NRD2** (`--denoiser 2`), against the same reference, still and `--cam-wobble 4 60`: blob error and bright-spot area (the `UNIFIED_LIGHTS.md` metrics, mask the toolbar rows) at 48 (must equal legacy bit-for-bit: `manyLights 1` falls back), 256, 1000, 2000, 4000 lights; acceptance: no worse than legacy +0.3% absolute at 256, and at 2000 lights blob error under 1.5% still / 2.5% moving, spot area under 0.5%.
4. **Flicker:** frame-to-frame MAD at rest with TAA on and off, and the exact-light swap rate for S0.
5. **Cross-API (when Vulkan staged exists):** mean per-pixel difference D3D12 vs Vulkan at the same pose under 3.
6. **Rig:** `scripts/many-lights-ab.ps1` + `scripts/many_lights_metrics.py` (new; model on `scripts/pt-compare.ps1`, which refuses unconverged references and wrong-renderer captures). The old session scripts (`abrun.py`, `abspot.py`) are not in the repository and must not be assumed.

**D. Native/other:** no ABI change, no managed test. `docs-check.py`/`claim-audit.py` must pass on the new doc text.

## 6. Files to create or modify, and ownership

**Created (many-light owns outright)**
- `modules/render.voxi/include/aver/voxi/LightTree.hpp`, `modules/render.voxi/src/LightTree.cpp` (add to `modules/render.voxi/CMakeLists.txt`)
- `modules/render.voxi/shaders/voxi_lighttree.hlsli`
- `tests/render.voxi/src/LightTreeTest.cpp`
- `scripts/many-lights-ab.ps1`, `scripts/many_lights_metrics.py`
- `docs/rendering/MANY_LIGHTS.md` (user + as-built guide, written last)

**Modified (many-light-owned edits; the named function or block only)**
- `modules/render.voxi/src/VoxiRenderer.cpp`: `giTableKinds` + counts (P0); `appendSceneLightCandidates` (stress); `buildLocalLights` tail (tree build, header `right.x`, `:4733-4752`); `ensureRdStagedResources` and the release paths (two textures); `recordStagedRayDriven` tail block (`:5577-5727`); NRD2 bit (`:7034-7037`); pipeline creation next to `:8201`; `reportVramUsage` category.
- `modules/render.voxi/include/aver/voxi/VoxiRenderer.hpp`: constants, `rdManyLightsCsPso_`, textures, `LightTree` member, `ViewDebug` values, `rdManyLightsWanted()`.
- `modules/render.voxi/shaders/voxi.hlsl`: S0 block (`:2641-2655`); `CSRdManyLights`; the two UAV declarations; Stage B branch (`:2121-2124`); debug views. Nothing else.
- `modules/render.voxi/shaders/voxi_rt.hlsli`: one include after `:861`; `rtManyLights()` accessor beside `rtNrd2SunClean` (`:594`).
- `modules/render.voxi/shaders/voxi_pt.hlsli`: `averDirectLights` only.
- `modules/render.voxi/include/aver/voxi/Voxi.hpp`, `src/Voxi.cpp`, `QualityLadder.hpp`, `ProjectRenderApply.hpp`, `RenderSettingsResolver.hpp` (if a field is resolved there)
- `modules/rhi/include/aver/rhi/RHIResources.hpp` (`kMaxBindingSlots`, P0 only), with the one-line consequences in `rhi.d3d12`/`rhi.vulkan` if the grep in 3.11 finds a literal 25
- `sandbox/src/EditorConsole.hpp` (append block), `sandbox/src/SandboxSettings.cpp` + `SandboxApp.hpp` (combo + state), `modules/formats/include/aver/formats/OcProject.hpp`, `src/OcProject.cpp`, `tests/formats/src/FormatTest.cpp` (key round trip)
- `tests/render.voxi/CMakeLists.txt`, `VoxiShaderCompileTest.cpp`, `VoxiSpirvCompileTest.cpp`
- Docs, append-only: `UNIFIED_LIGHTS.md` ("Many lights" section + the S0 note), `LIGHTS.md` (limits section), `NRD2.md` (inputs), `FORMAT_SPECS.md` (key), `docs/rendering/NEURAA_NRD.md` rule list (new rule: "no light selection is carried between pixels or frames"), `docs/MODULES.md` only if `module.json` changes (it should not)

**Not touched by many-light**
- `aver_lights.hlsli`, `SceneLight.hpp`, `CLight`, `LightGather.hpp`, `render.pt` (all stay; the tree is Voxi-only)
- `modules/trifactor`, the cluster draw and culling paths, `submit()`/`Draw`, `ClusterMaterialShader`, `SandboxAssets` cluster code (geometry's)
- `render.pbr` material system, `MaterialConstants`, `material_prelude.hlsl`, graphs, `.ocmat` (materials' feature; b2 has no room and this design needs none)
- `modules/assets` texture loading, BC7 cache, bindless table ownership, `averRtSampleSlot` (texture streaming's)
- `PSMainVoxi` and the single-pass kernel bodies

**Shared-file rules (merge-adjacent, additive; whoever merges second keeps both)**
- `VoxiRenderer.cpp/.hpp`, `voxi.hlsl`, `voxi_rt.hlsli`: geometry, materials and streaming also edit them in disjoint functions (their docs list them). Many-light edits are confined to the functions above. Each block is banner-commented `// MANY LIGHTS (docs/plans/wave4/MANY_LIGHTS.md)`. No reformatting, no moving code.
- `giTableKinds`/`kMaxBindingSlots`: section 3.11; the second lander rewrites the `static_assert`.
- `giLayout()` is not changed in shape, so the cluster pipelines that reuse it follow automatically (`VIRTUALIZED_GEOMETRY.md`).
- `Voxi.hpp` Settings, `EditorConsole.hpp`, `QualityLadder.hpp`, `SandboxSettings.cpp`, `OcProject.*`: append-only blocks.
- Texture streaming: `rdHitSurface` is called with `gAverFbOn == false` in `CSRdManyLights`, so its extra visit records no footprint (their doc: only the primary-hit callers set it).
- Material instances: `RtMaterial` growth is transparent here (the pass only calls `rtHitSurface`).

## 7. Implementation order (each step builds, tests, ends with a headed run and a local commit; no push, no release)

| Step | Content | Gate |
|---|---|---|
| S0 | Exact-light selection as a pure function of the current frame (3.10) | Swap-rate and spots metric (5C.4); 48-light scene unchanged |
| P0 | Widen table 0 (3.11), stress lights (3.6), console + settings plumbing, ladder, key | Builds on both backends; no image change; `FormatTest` round trip |
| 1 | `LightTree` CPU + tests; pack and upload behind `manyLights` (tree present, nothing reads it) | `LightTreeTest` green; buffer size and upload cost logged |
| 2 | `voxi_lighttree.hlsli` + `CSRdManyLights` + textures + Stage B branch, NRD2 frames, `manyLights 2` only | Shader + SPIR-V compile gates; TDR protocol 5C.1; unbiasedness 5C.2 |
| 3 | Auto engage (`manyLightsMin`), `.a` ratio + filter + glass check, descriptor/VRAM release paths, debug views | 5C.3 at 256 and 1000 lights; a glass pane over a lamp-lit floor looks as before |
| 4 | Hits: `averDirectLights` tree branch; exhaustive mode 3 | GI/reflection/PT agree with mode 3 within 1% (mean) on 128 lights |
| 5 | Ladder tuning, budgets measured (section 8), docs | Numbers in `MANY_LIGHTS.md`; budget table filled |
| 6 (optional) | Single-frame fallback filter for FidelityFX/None frames | Only with the owner's yes (Q3) |
| 7 (optional) | Tree refit for moving lights; 16k cap; spec texture only when needed | Measured wins |
| 8 (optional) | Raster deferred pass (3.7) | Owner's yes on the G-buffer change (Q4) |

## 8. Performance budget and how to measure it

Hardware of record: RX 7800 XT, NewSponza_Night default view, render scale 0.5 (1280x720 internal), NRD2, the table in `UNIFIED_LIGHTS.md` (total 17.7 ms; shadow stage 1.4 + tail 2.7 ms). **All figures below are targets, none measured.**

| Item | Target | Why it should hold |
|---|---|---|
| Scenes with <= 48 lights | +0.00 ms, identical image | Legacy path, no tree, no textures |
| Tree CPU build, 4000 lights | <= 0.3 ms, cached on a still set | Morton sort + merge; `fill()` on reorder |
| `CSRdManyLights` Medium (S=1, M=8), 2000 lights, 720p | <= 1.5 ms | 8 descents x ~12 levels x 2 child bounds, 1 shadow ray (about 2 ms/ray-pixel in the old numbers, but short and mostly occluded-early) |
| Whole-frame delta at 2000 vs the 48-light baseline | <= +2.0 ms | replaces `CSRdTailVis` (2.7 ms measured) |
| Hit sites | <= +0.5 ms total at M = 4 | the shipped top-4 loop is replaced, not added to |
| Texture VRAM | 14.7 MB at 720p, 59 MB at 1440p | two RGBA16F |
| Tree upload | 640 KB per set change | cached per ring slot |

Measure with `--gpu-timing` (span `"Voxi RD many lights"`, plus the existing `"Voxi RD tail lights"` for the filter) and, for the stage in isolation, `voxi.rayDrivenStageTiming 1` (adds a barrier after each stage: confirm any win on the total without it). Sweep `lightSamples` 1/2/4, `lightCandidates` 4/8/16, lights 48/256/1000/2000/4000, still and `--cam-wobble`. Two traps from `UNIFIED_LIGHTS.md`: incoherent per-pixel rays cost more than their count suggests (the old per-pixel interleave was 3 ms slower than a quarter-resolution pass), and half-rate checkerboards need a wave-coherent thread mapping to pay. If Medium misses 1.5 ms, the order of levers is: M 8 to 4, roulette `tau` up, then a quarter-resolution trace with the pass's own bilateral fill (phase 7, using `rdHrThread`, `voxi.hlsl:779`) before touching S.

## 9. Risks and open questions for the owner

1. **Q1: US 11,948,246 changes the product.** ReSTIR-style reuse (the real quality win) is what it claims, to 2042, with a pending continuation. This design ships without it and pays in noise. Options: accept (this doc); get counsel's read of the "second lights from other pixels or a prior frame" element; license. Separately, the shipped hysteresis (S0) is fixed regardless. Decision needed on S0 landing now.
2. **Q2: widening table 0 to 28 slots** touches both RHI backends and the Vulkan binding base. Alternative (one packed texture) costs coloured tail speculars. Streaming needs the same widening.
3. **Q3: NRD2-only.** Users on FidelityFX/None keep the grid + ratio tail (capped at 24 lights per cell) and see no gain from thousands of lights. A single-frame filter fallback is phase 6; NRD2 needs retraining on stochastic tails regardless (its network saw the shipped lamp noise model; default parameters apply until retrained, `NRD2.md` "Training").
4. **Q4: raster gets nothing beyond 32 lights in v1.** The fix needs a G-buffer albedo target, an RHI change shared with geometry and materials.
5. **Q5: list cap 4000.** Far lights are dropped by the camera-importance cut; a far-field aggregate (one cluster light for the rest) or a 16k cap is possible.
6. **Q6: per-light "keep exact" flag** (hero lights always in the exact set) would need a `CLight` flag bit; not added.
7. **Vulkan has no light list today.** This feature cannot show on Vulkan until the staged-passes task lands (3.9); the code is ready for it, the image is not.
8. **Variance in motion.** No temporal reuse means NRD2 is single-frame and TAA carries the temporal averaging; fast pans with few lights per pixel and S = 1 may shimmer. Mitigation order: M, S, the clamp, then the quarter-resolution/fill option. Measured in 5C.4.
9. **Register pressure and TDR.** The pass is a fresh compute entry, but it includes `rdHitSurface`, a shadow ray and a BRDF in one kernel. Split into two passes (select, then shade+trace through a small per-pixel record) if the first headed run shows occupancy or watchdog trouble; the rule is no code growth in `PSMainVoxi`.
10. **Patent search limits.** Claims were read from OCR'd page images for 12,299,801, 11,663,773, 12,361,633, 12,361,635, 11,948,246 and the published applications; maintenance-fee status and anything filed after about 2025-04 are unknown; the light BVH has no patent found, not a clearance. Counsel should review sections 2 and 3.10 before the feature ships.
11. **Emissive triangle lights** (UE's emissive-mesh lighting) are intentionally absent: US 2025/0182387 claims sampling lights and geometry in one step. Emissive meshes stay GI-only or become sphere lamps as today.
12. **Unbiasedness has a documented hole** where the cheap target is zero but the exact contribution is not (a rect's back-lobe, a cookie edge); the shipped ranking has the same one.
