# NeuRaC — Neural Radiance Cache (Aver) — design

NeuRaC is the engine's world-space radiance cache for ReSTIR's second bounce (`--restir-visibility
cached`; code `NeuRaC.hpp/.cpp`, `NeuRaCLayout.hpp`, `voxi_neurac*.hlsl*`). Stage 1 is procedural (SH
cells). Stage 2a adds a tiny network that learns a per-cell trust; the neural parts live in a shared
module, `Aver.Render.Neural`.

**Status (2026-10-12):**

- **Stage 1** is built, committed (`6057ff50`) and was run on 2026-10-03. *Measured* on NewSponza: the
  cache fills (cascade 0: ~18.8k valid cells, n_eff ~14 after 150 frames), the resolve costs 0.02-0.03 ms
  and the whole mode ~0.23 ms over HalfResolution; it supplies about half of the term it replaces, the
  HalfResolution fallback the rest.
- **Stage-1 fixes and the NRD2 guide** (section 5.11) are built and committed (`f1439403`), covered by CPU
  twin tests and a WARP GPU test, and **not yet run in the engine**: whether the walls are no longer red
  in the Coverage view, and what the guide does to NRD2 in motion, is for the owner to judge.
- **Stage 2a**, the trust network (section 5.12), is built in wave 2 of the same plan
  (`docs/plans/neurac_nrd2/DESIGN.md`), **not yet run or measured**. Its benefit is unverified.
- **Stages 2b and 3** are design only.

Where the implementation departs from the design below, sections 5.10-5.12 list the deviation and the
reason; the older text in 5.1-5.9 is kept as the design of record and carries a pointer where it no longer
matches. Every millisecond and cell count below is an estimate unless it says *measured*.
Written 2026-10-03 from three rounds of read-only scouting (algorithm, hardware, prior art, and the
Aver source); sections 5.11-5.12 added 2026-10-12 from the code as built.

**One-line summary:** a camera-centred cascade of cells around the viewer, each holding first-order
spherical harmonics (SH) of incident light, trained live from the rays ReSTIR GI already traces, and
read in place of the second-bounce ray on the pixels that do not trace one. It also hands NRD2 a
per-pixel estimate of the cached light as the guide of its temporal anchor (5.11). Stage 2 neural pieces
sit on top of it through `Aver.Render.Neural`.

---

## 1. Goals and non-goals

**Goals**

- A world-space radiance cache that runs on **any D3D12 GPU** (AMD, NVIDIA, Intel), with no vendor
  SDK, no CUDA, no Agility SDK, at the engine's current shader-model ceiling (SM 6.5/6.6).
- Built **in-house from scratch**, so it can ship under the engine's LGPL-2.1 licence.
- A **shared foundation** (`Aver.Render.Neural`) for later neural features — upscaling and frame
  interpolation reuse its optimiser, kernels and sample plumbing.
- First use: make ReSTIR GI's second bounce cheaper and better than today's options.
- Off means off: with the cache disabled, existing entry points compile from the same shader text and
  defines, and no new resource or pipeline is created.

**Non-goals (for now)**

- Matching NVIDIA NRC's per-pixel network. On the dev GPU (RX 7800 XT, RDNA3) there is no D3D12 route
  to matrix hardware, and the paper's 5×64 network is estimated at 8–18 ms there in plain HLSL.
- Glossy/specular indirect. Today's second bounce is diffuse-only (`giTraceInitialCandidate` in
  `voxi_restir.hlsli`: `radiance += s.kdAlbedo * indY`); glossy queries are a new capability, planned for stage 3.
- Cache-guided ReSTIR sampling. Parked: it sits in patent territory (see §9).
- Vulkan. D3D12 first, Vulkan parity afterwards, as for every other feature.

---

## 2. Constraints that shaped the design

| Constraint | Consequence |
|---|---|
| RX 7800 XT has no D3D12 matrix path. Cooperative Vectors never covered RDNA3 and are deprecated; LinAlg (SM 6.10) is preview and RDNA4-only; WaveMMA was removed from DXC; AGS exposes no WMMA. RDNA3 WMMA is reachable only through Vulkan `VK_KHR_cooperative_matrix`. | Portable HLSL is the baseline backend for every vendor. Matrix backends (Vulkan coop-matrix, later D3D12 LinAlg) are optional extras behind one interface. |
| Network maths is cheap at cache scale. About 100k decodes of a 2×32 MLP is ~0.02 ms of ideal FMA time (derived). | fp32 first. Packed fp16 later, gated on a new caps query. **No int8**: it needs per-step requantisation, HLSL has no mixed-sign `dot4`, and it is no faster than packed fp16 on AMD or NVIDIA. |
| The real cost is memory traffic: cell reads/writes, scattered atomics, weight loads. | Keep payloads small (24 B/cell), keep work per *cell* rather than per *pixel*, and accumulate with integer atomics. |
| D3D12 has no fp16 atomic add. Only u32 add/CAS is portable; fp32 atomics are optional. | Training accumulates in **u32 fixed point**, like the existing voxel volume (`AVER_VOX_FIXED` in `voxi.hlsl`). Integer sums are also order-independent, so training is deterministic. |
| Activision's *Neural Light Grid* (SIGGRAPH 2024) found that a learned latent per cell plus one shared small decoder **failed**; it needed 512–1024-wide decoders (3–10 ms per brick). What shipped was ordinary probes plus tiny 2×8 networks that learn the blend weights. | No "latent grid + shared tiny decoder" on the main path. Neural work starts with learned blend weights and spatially local networks. |
| Aver's voxel GI is one **world-fixed** cube over the level (`VoxiRenderer::setVolume`). On NeonDistrict that is ~8.6 m cells even at 512³. | The cache needs its own camera-local cascades and its own geometry source; it cannot reuse the voxel volume's cells or rebuild gate. |
| Voxi's binding table 0 was full: 22 SRV and 20 UAV, hard-asserted (the `static_assert` on `kVoxiSrvCount` / `kVoxiUavCount` in `VoxiRenderer.cpp`). | The cache gets its own small binding set for the resolve pass. Voxi's table was widened deliberately, with the static_assert, for the cache's slots (t22, u20, u21; see 5.10 item 4); other features have widened it since, to 25 SRV / 25 UAV. |

---

## 3. Architecture: the layers

```
 L0  analytic base        sun + sky + existing voxel GI            (exists)
 L1  cell cache           camera-centred cascades of SH cells      (stage 1, no network)
 L2  learned corrections  tiny blend-weight nets, local residual    (stage 2, neural)
     nets
 L3  per-pixel read       SH(N) x albedo in place of the 2nd bounce (stage 1)
     glossy query         direct network query for smooth/metal     (stage 3, neural)
```

The stack works because each layer only carries what the one below cannot:

- L0 is free and always available: it is the cold-start answer and the fallback.
- L1 stores what L0 gets wrong at the camera's scale. L0's cells are metres wide; L1's are 0.25 m near
  the camera.
- L2 learns only the residual error of L1 (leaks, detail SH cannot hold), so its networks stay tiny.
  An untrained L2 outputs ~0, which leaves L1 unchanged.

---

## 4. Stage plan

| Stage | Deliverable | Neural | Exit criterion |
|---|---|---|---|
| 0 | Measurements: occupied-cell counter; GPU cost of today's F2 trace and voxel tap (`--gpu-timing`). The owner runs these. | — | Numbers in this doc replace the estimates. |
| 1 | L1 cell cache + L3 read as a new ReSTIR visibility mode. Infrastructure: cascades, sample-driven occupancy (no voxeliser, see 5.10), sample stream, fixed-point scatter, resolve, bindings, settings. **Built and run; fixes in 5.11.** | No | Beats `HalfResolution`'s sky-ratio reconstruction against the path tracer at ≤ its cost, on PTTest and NeonDistrict. |
| 2a | `Aver.Render.Neural` MLP kernels + Adam optimiser + CPU reference (built); first client: a learned per-cell trust that scales the lookup weights (Activision-style tiny net, 5.12). **Built, unrun.** | Yes | Visible leak reduction at thin walls; no regression elsewhere. |
| 2b | KiloNeRF-style local residual networks per region, correcting L1's SH. | Yes | Measurable error drop vs the path tracer for ≤ 0.5 ms. |
| 3 | Glossy/metal direct query (fixes the metallic "black hole": `kdAlbedo = 0` hits get no indirect today). | Yes | Metals and roughness < ~0.45 receive indirect light. |
| — | Shared decoder over learned latents (old idea). | Yes | Research only: has a published failure (§2). |

---

## 5. Stage 1 in detail — the cell cache

### 5.1 Cascades

- **Layout (open decision D2):** 3 cascades of 64³ cells, at 0.25 m / 1 m / 4 m cell size. They cover
  16 m / 64 m / 256 m cubes centred on the camera. Beyond 256 m the read falls back to L0.
- **Scrolling (toroidal) addressing:** a cell's texel is `worldCell mod 64` per axis. When the camera
  moves, only the newly exposed slabs are cleared and re-seeded; nothing is copied.
- **Cell count:** 3 × 64³ = 786,432 cells. A surface-area estimate puts ~5–14% occupied, about
  37k–111k occupied cells in total (*derived; stage 0 measures it*).

### 5.2 Geometry source (occupancy)

> **As implemented:** there is no voxeliser. Occupancy is *sample-driven*: a cell exists because a
> traced F2 ray started inside it (section 5.10, deviation 1). The text below is the original design.

The world-fixed voxel volume is too coarse at cascade scale, so the cache builds its own occupancy:

- a cheap conservative voxelise of the static and movable draws into each cascade's **occupancy
  bitmask** (1 bit per cell, 32 KB per cascade). Only exposed slabs are re-voxelised when the camera
  scrolls; dirty regions are re-voxelised when the existing scene-change gate fires.
- an optional **distance-to-surface byte** per cell (a few jump-flood steps over the bitmask), used
  for leak control (§5.6).

Movers and skinned meshes are absent from the existing voxel volume. The cache's voxeliser should
include movers from the start, so lighting near them is not frozen.

### 5.3 Cell payload

> **As implemented:** cells are 32-byte structured-buffer elements, not `RGBA16F` 3D textures, and
> the accumulator is 15 ints per cell plus a separate counts region (section 5.10, deviations 4-5).
> The text below is the original design.

- **Incident radiance as first-order SH, RGB:** 4 coefficients × 3 channels = 12 values, stored as
  fp16 in three `RGBA16F` 3D textures per cascade (24 B/cell; ~6 MB per cascade, ~19 MB total).
- Hardware trilinear filtering is valid on SH coefficients, which is why SH was chosen over spherical
  Gaussians (not linearly filterable) or per-cell octahedral maps (too large for dense cells).
- At lookup, irradiance is the cosine-convolved SH, with ZH3-style sharpening (Roughton 2024) as a
  cheap upgrade on the diffuse term.
- An **accumulator** per cascade: 12 signed fixed-point sums + 1 sample count = 13 × u32 per cell. It is
  written only during the frame and cleared by the resolve. (As built: 17 ints per cell, 5.10 item 5 and 5.11.)

### 5.4 Training samples — where they come from

ReSTIR GI already traces a second-bounce ray on a subset of pixels: F2 path 3 (the traced branch of the `indY` block in `giTraceInitialCandidate`, `voxi_restir.hlsli`).

- That ray starts at the GI candidate's hit point `hitPos` with normal `s.N`, goes in direction `dir2`,
  and returns `indY`: incident radiance from `dir2` (sky on a miss, the voxel volume on a hit).
- Each such ray is **one Monte Carlo sample of incident radiance at `hitPos`**. It is projected onto
  the 4 SH basis functions in direction `dir2`, weighted by its sampling pdf, and scattered into the
  accumulator cell containing `hitPos` (fixed-point `InterlockedAdd`; as built, 17 atomics per accepted sample, see 5.11).
- Sample volume: F2 traces on every traced pixel under `Full` and on about ¼ under `HalfResolution`,
  i.e. ~0.35–1.4M samples per frame at 1440p (*derived*). That is ~10–40 samples per occupied cell per
  frame.

Two properties matter here:

- **No new rays.** Training rides on rays the frame already pays for.
- **No self-reference in stage 1.** A sample's value comes from the sky or the voxel volume, never from
  the cache itself. That keeps the cache single-bounce-over-L0 for now. Feeding the cache back into its
  own samples (multi-bounce) is possible later, but it is a deliberate design and patent decision (§9),
  not a default.

**Open decision D1:** the sample does not need the hit's albedo, because it is *incident* radiance at a
point. Widening `RdGiCand` (`voxi_restir.hlsli`, 48 B) to carry albedo and roughness is needed only
for stage 3's glossy queries. Recommendation: defer the widening until stage 3.

### 5.5 Resolve (once per frame per cascade)

For each cell with samples this frame:

- the new SH is the sample mean (sums ÷ count);
- it is blended into the stored SH with a temporal factor `α = max(1/(n_eff+1), α_min)`, where
  `n_eff` counts frames the cell has been observed. That gives fast convergence on a fresh cell and a
  floor for responsiveness when lighting changes;
- the accumulator is cleared.

Cells with no samples keep their value and age. Cells older than a threshold fade towards L0.

Stage 1 needs no gradient optimiser: SH projection is linear, so the running mean is already the
least-squares fit. Adam arrives with stage 2's networks, in the shared module.

### 5.6 Read (L3) and leak control

The cache replaces the second bounce on **pixels that do not trace one**. Today those pixels use a
voxel-cone march (`Reconstructed`, f2Path 1) or a sky-luminance ratio (`HalfResolution`'s untraced
pixels, f2Path 2). A new f2Path reads:

```
indY = irradianceFromSH(cacheSample(hitPos + s.N * offset), s.N)
```

- **Cascade selection:** the finest cascade containing `hitPos` with ≥ 1 voxel of margin, cross-faded
  over the last 4 cells to hide seams.
- **Normal offset:** look up half a cell along `s.N`, the same idea as the existing shell-straddle fix
  ("Voxel shell straddle" in `giTraceInitialCandidate`).
- **Occupancy-weighted trilinear:** manual 8-tap with weights zeroed where the occupancy bitmask says
  the corner cell is inside solid geometry or behind the surface. This is what stops light leaking
  through thin walls, which plain hardware trilinear cannot do.
- **Confidence fallback:** a cell's age and sample count give a confidence that blends towards L0 (the
  existing voxel/sky answer) while the cache is cold.

Integration is a small, local edit: one more branch in the `indY` block at
`giTraceInitialCandidate`, plus a new `RestirVisibility` value (`Voxi.hpp`). A working name is
`Cached = 4`: traced pixels train the cache, untraced pixels read it.

### 5.7 Frame placement

> **As implemented:** there is no occupancy-voxelise step in the frame, and the scatter and the read
> run only inside the four staged-compute *twin* pipelines (section 5.10, deviations 3 and 7).

```
voxelize -> filterMips -> airVis                      (existing L0)
cache: scroll/clear slabs -> voxelise occupancy        (new, small)
CSRdVisibility -> CSRdGiTrace (F2 samples -> accumulate) -> CSRdShadow -> CSRdGi -> ...
cache: resolve (accumulator -> SH)                     (new; used by NEXT frame's reads)
```

The resolve sits after the trace, so reads in frame N use the cache resolved in frame N−1. One frame
of latency on a cache that is already temporally smoothed is acceptable, and it avoids a mid-frame
dependency between trace and read.

### 5.8 Bindings, settings, determinism

> **As implemented:** Voxi's table 0 gains three slots, not one SRV (t22, u20, u21), and the wire
> encoding is mode 2 plus bit 128, not a fifth mode value (section 5.10, deviations 2-3). The "off
> means off" contract holds in the narrow form stated below. No cascade-size or cell-size settings
> keys exist yet: cascade count, resolution and cell sizes are compile-time constants.

- **Bindings:** the cache's own compute layout for its passes (accumulator UAVs, SH UAVs, occupancy),
  modelled on the voxel mip/resolve sets. Voxi's table 0 gains one SRV for the resolved SH, widening
  `kVoxiSrvCount` and the static_assert together.
- **Settings:** a new `RestirVisibility::Cached` value plus cascade size and cell-size keys, resolved in
  `RenderSettingsResolver` with a `DisableReason` when ray queries or the GI path are unavailable.
  Default stays `HalfResolution`; it is not on the quality ladder until measured.
- **Determinism:** integer accumulation makes training order-independent. The "off" contract is
  stated narrowly: same shader text and defines for existing entry points, no new resources or
  pipelines. "On" is judged statistically against the path tracer, not by bit equality, because
  ray-traced gate configs already do not reproduce run to run.

### 5.9 Cost estimate (stage 1, 1440p, RX 7800 XT) — all estimates

| Part | Estimate |
|---|---|
| Sample scatter: 0.35–1.4M samples × 13 u32 atomics (as built: up to 17, and up to three cascades, 5.11) | 0.1–0.4 ms (contention-dependent) |
| Resolve: ~100k touched cells | < 0.1 ms |
| Occupancy voxelise (scrolled slabs only) | < 0.1 ms typical; spikes on teleports |
| Read: 8-tap occupancy-weighted SH lookup per untraced pixel | 0.1–0.3 ms |
| **Saved:** cone march or sky-ratio replaced; with `Cached` at the HalfResolution trace rate, quality should approach `Full` without its extra rays | to be measured in stage 0 |

### 5.10 As implemented: deviations from the design

Everything in 5.1-5.9 stands unless listed here. Each item names why the design could not be built as
worded; none was a free choice. The list was written when stage 1 was first built (2026-10-03); the stage has
since been run and measured (Status above), and items 4, 5 and 8 were changed again by 5.11.

1. **Sample-driven occupancy replaces the camera-local voxeliser (5.2, D4).** A cell is created by the
   F2 samples that land in it, and the lookup trusts only cells whose tag, `n_eff` and age say a
   sample put them there. Consequences: no occupancy bitmask, no distance-to-surface byte, no
   exposed-slab re-voxelise on scroll or teleport (a window that shifts makes every tag mismatch, which
   is the reset), and **movers and skinned meshes need no special case** because the samples come
   from the same rays that already see them. The cost is that leak control is the mean-normal weighting
   described under item 6 rather than an occupancy test; thin walls can still leak where one cell holds
   two opposed surfaces.
2. **Wire encoding: mode 2 plus bit 128, not a new mode value (5.8, D3).** The shader decodes the mode
   as `(uint)gAmbientParams.w & 3u` (and `givis::packAmbientW` masks the same way), so a 4 would
   silently become 0 (NoRay), the over-bright pre-fix look. The CPU keeps `RestirVisibility::Cached = 4`
   in `Settings` and in the renderer, and packs mode 2 (`HalfResolution`) plus the new `neurac`
   bit, value 128 (`packAmbientW`'s ninth argument, default false). The existing
   `halfBound`/`tracedPx`/`rec.valid` logic then makes the tracing decisions exactly as for
   `HalfResolution`, and the half-resolution history pair is still allocated (`giVisHistWanted` accepts
   2 and 4). 128 is the last free bit below 2^23 inside the already-exact low byte; the word is a float
   and must stay under 2^24. Typos above 4 clamp to 3 (Full), never to Cached.
3. **Four lazily compiled twin pipelines, not runtime gating (5.8 "off means off").** Adding scatter and
   lookup code to `voxi_restir.hlsli` unconditionally would change the text of every variant that
   includes it, including single-pass `PSRayDriven`, which is documented at the AMD register limit. All
   new HLSL therefore sits under `#if AVER_NEURAC` (default 0, with a hard `#error` against
   `AVER_RD_SINGLE_PASS`), and the define is set only on four staged compute twins built on first use:
   `CSRdGi`, `CSRdGi` + checkerboard, `CSRdGiTrace`, `CSRdGiTrace` + checkerboard
   (`createNeuRaCTwins`). Path Tracing has six of its own, built by `createPathTraceTwins` with
   `AVER_PT_PATHS=1;AVER_NEURAC=1`: the same four GI entry points plus `CSRdRefl` plain and split (see 5.11,
   "Path Tracing"). Each frame the renderer picks a twin only when bit 128 is set and the twin exists. With the cache off, the existing
   variants compile from identical preprocessed text; the DXIL cache still invalidates once, because
   its key hashes the whole shader corpus.
4. **Cells are read through a UAV, not an SRV (5.8).** Slots: `t22` cascade info (CPU-written Upload
   ring of 3, fixed GENERIC_READ), `u20` accumulator, `u21` cells. An SRV for the cells would need
   UAV-to-SRV buffer barriers around two different reader states inside the deliberately barrier-free
   staged lighting group, and would have taken the last free table slot. UAV-only buffers need no state
   transitions at all. Table 0 went from 22 SRV / 20 UAV to 23 SRV / 22 UAV for these slots, and the static_assert in
   `VoxiRenderer.cpp` (and the text `GiVisibilityTest` greps for) said so; later features have widened it
   to **25 SRV / 25 UAV**, with the cache's slots still t22, u20 and u21. Nothing is bound at init:
   the new slots are null-filled until the cache is first needed, and a tiny placeholder buffer is
   created lazily at teardown only, because a UAV slot cannot be cleared once set.
5. **32-byte cell packing and a 15-int payload (5.3).** 12 SH values as fp16 are 24 bytes, the
   normal word is 4, which leaves 4 for the rest: a **24-bit tag** (3 x 8 bits of
   `(worldCell >> 6) & 255` per axis), 4-bit `n_eff` and 4-bit age. The normal word is octU 10 | octV 10 |
   length 6 | trust 6 (the 12 bits above the two coordinates held a 10-bit length until 5.11 added the
   trust). The accumulator payload is 12 signed fixed-point SH sums plus 3 summed-normal sums per cell
   (scales `2^15` and `2^16`; the headroom is a `static_assert` in `NeuRaCLayout.hpp`), with the per-cell
   counts in their own contiguous region so the resolve can early-out by streaming 3 MB instead of touching
   786k sparse cache lines. 5.11 adds an arrivals region beside the counts.
   The tag aliases every 256 x 64 cells (about 4 km at the finest cascade) and is bounded by age expiry;
   a 48-byte cell is the fallback if it ever matters.
6. **Lookup details (5.6).** Per cascade: normal-offset by half a cell, 8-corner manual trilinear
   with each corner weighted by its trilinear weight, `saturate(dot(cellDir, N) * 2)`, the mean-normal
   length (planarity), `min(n_eff / 4, 1)`, `1 - age / 15` and, with stage 2a, `lerp(1, trust, netStrength)`; cascades blend finest-first with a
   4-cell edge fade and a 1-cell margin; the remainder falls back to the f2Path 2 formula (the L0
   answer). The SH is L1 with the Ramamoorthi-Hanrahan cosine convolution; ZH3 sharpening is still
   a TODO.
7. **Staged ray-driven on D3D12 only.** The scatter and the read live in `giTraceInitialCandidate`'s
   F2 block, reachable from the staged `CSRdGi` and `CSRdGiTrace` twins. Single-pass `PSRayDriven`
   (register limit) and the raster `PSMainVoxi` (not worth a multiplicative variant set) never compile
   the cache, and the staged passes themselves are D3D12-only (Vulkan cannot record compute inside the
   scene pass). **Everywhere else `Cached` behaves exactly as `HalfResolution`**: the resolver reports a
   soft `DisableReason::RequiresStagedRayDriven` (it warns in the UI, never greys the control, and
   `effective` still equals `requested`), and the renderer logs the fallback once. The split `CSRdGi`
   variant (`AVER_GI_SPLIT`) only reads the candidate buffer and never traces, so it needs no twin.
8. **VRAM is about 79 MB (75 MiB), not 19 MB (5.3, 5.1).** Accumulator 53.5 MB (13,369,344 ints: 17 per
   cell, the 15 payload ints plus the counts and arrivals words) plus cells 25.2 MB (786,432 x 32 B), about
   four times the original estimate, because 15 ints per cell are accumulated rather than 13 and no packed
   texture form is used for the sums. The network's lists add about 8 MB (5.12). The owner has accepted
   this. If it proves too much: 48^3 cascades, or a 16-bit accumulator; neither is free.
9. **Zero-init and reset are one mechanism.** The RHI has no buffer clear, so the resolve shader has a
   `CLEAR_ALL` flag (bit 0 of the info word) that zeroes every cell and the whole accumulator; creation
   and `requestReset()` record one such dispatch. Teleports need no reset (item 1).
10. **Settings surface.** `RestirVisibility::Cached = 4` is selectable in the Project Settings combo,
    the `voxi.giRestirVisibility` console variable (0-4), `--restir-visibility cached` and the
    `RENDER.RESTIRVISIBILITY` project key (4). No quality-ladder rung returns it: it is opt-in until
    measured. The design's "cascade size and cell-size keys" are deferred; the occupied-cell counter of
    stage 0 is deferred too (a readback needs fence-synchronised `readBuffer`).
11. **Timing and debug.** GPU stats: `"Voxi NeuRaC resolve"` and `"Voxi NeuRaC clear"`, later also
    `"Voxi NeuRaC guide"` (5.11) and `"Voxi NeuRaC net"` (5.12).
    Scatter and lookup cost lives inside the existing RD lighting spans, so cache versus plain is an
    A/B of mode 2 against mode 4 on those. The F2 path view (`voxi.giVisPathView`) paints f2Path 4 magenta,
    brightness = cache confidence (not in the split `CSRdGi`, which has no twin).
12. **Neural Visualiser (Window > Neural Visualiser, 2026-10-03).** Shows the cache at the visible
    surfaces in place of the lit image (`rcDebugColour`, `voxi_neurac_io.hlsli`). The twin
    `giRestirIndirect` returns the colour after the denoiser-input write (so it never enters history),
    and Stage B shows it unshaded. Views:
    - **Cached light**: `rcLookup` alone; black = nothing cached.
    - **Coverage**: red = fallback, green = cache.
    - **Cascade**: cyan 25, yellow 100, orange 400 cm cells.
    - **Cell state**: green = n_eff, red = age, violet = empty or stale.
    - **Trust** (mode 5, 2026-10-12): red = low, green = full trust, violet = empty or stale.

    An optional **cell grid** darkens cell edges along the surface. The mode travels in `gAmbientParams.w`
    bits 8-10 and the grid in bit 11 (`givis::packAmbientW`'s `neuracView`), set only beside the live-cache bit.

    While a view is up the GI split stands down for the frame, because only the unsplit `CSRdGi` has a
    cache twin. CLI flags: `--neurac-view 0-5`, `--neurac-grid`.

    *Seen* on NewSponza (200 frames, Cached): the cascades are correct (cyan near, yellow beyond, grid
    aligned). Coverage after 200 frames is green on floors and red on most vertical surfaces (curtains,
    pillars, walls), so the cache mostly serves upward-facing surfaces so far. Worth a look against the
    warm-up stats (cascade 0 holds 7% of cells valid at frame 150).

**What is still unverified from stage 1:** image quality against the path tracer at scale (NeonDistrict),
Vulkan (untouched; the cache tears down to HalfResolution there and needs its own parity pass), and the
twins' register fit under Path Tracing. The stage ran on the RX 7800 XT on NewSponza (Status above).
Everything in 5.11 and 5.12 is newer than that run.

### 5.11 Stage-1 fixes and the NRD2 guide (2026-10-12, commit `f1439403`)

The warm-up log (cascade 0 held 7% valid cells at frame 150) and the Coverage view (floors green, walls
red) said stage 1 trained little besides upward-facing surfaces. An audit of the code found seven defects;
the fixes below are built, tested on the CPU and on WARP, and not yet judged in the editor. The plan is
`docs/plans/neurac_nrd2/DESIGN.md` (sections 3 and 4). Each item gives the cause.

1. **Reset and relax hooks.** Nothing told the cache that its light had gone stale (a sun-time change, a
   GI rebuild, a level load): old cells sat at `n_eff` 15 and decayed at the 1/16 floor. `NeuRaC` gains
   `requestRelax(frames, alphaFloor)` beside `requestReset()`, and the resolve blends at
   `alpha = max(1/(n_eff+1), max(alphaMin, relaxAlpha))`. A relax decays old light; a reset zeroes it.

   | Event | Action |
   |---|---|
   | Sun jump (`rtHistSunJumped`) | relax 8 frames, floor 0.5, in `updateNeuRaC` before `beginFrame` |
   | GI rebuild (`takeGiSnapshot`) | relax 4 frames, floor 0.25: it fires on any sky or sun byte change, so a clear would blank the cache |
   | Level load | `setVolume` (called every frame) resets when the volume's centre or extent moves by more than 1 cm |
   | `resetgihistory` | `resetGiHistory(quiet=false)` resets; the every-frame debug path passes `quiet` and does not |
   | Visibility mode away from Cached | the cache is torn down and cleared on re-entry; no hook needed |
   | Teleport | none: the window shifts, tags mismatch, cells read stale-invalid (item 1 of 5.10) |

2. **One frame counter for the checkerboard and the visibility phase.** F2 traces on one pixel
   checkerboard half, and the traced pixel inside a 2x2 block follows `kGiVisPhase`. The table cycled
   `{(0,0),(1,1),(1,0),(0,1)}`, so on half the frames the traced pixel lay outside the checkerboard class
   and F2 trained on only some frames and pixels. The checkerboard parity also came from `denoiseFrame_`
   while the shader used `rtFrameIndex_` (whether the two agreed in parity was never confirmed). Now the table is `{(0,0),(1,0),(1,1),(0,1)}`, so
   `(x ^ y) & 1 == frame & 1`, and `giCbParityWritten_` comes from `rtFrameIndex_`: one counter drives
   both (`GiVisibility.hpp::tracedPixel` mirrors the table). **Cost:** the steady-state F2 trace and
   scatter work roughly doubles (the intended quarter-rate trace now happens every frame); read it in
   "Voxi RD GI stage" with `--gpu-timing`.
3. **Scatter bins at the lookup's own cell.** The lookup offsets by `N * size / 2` and interpolates
   trilinearly over cell centres, but the scatter binned the unoffset hit. The trained cell then got
   weight `1 - u` for positive normals and `u` for negative ones, `u` being the surface's fractional
   position in its cell: floors sitting on a cell face got full weight, walls half on average and often
   nearly none (the red verticals). Now the scatter bins at `floor((hit + N * size/2) / size)` and trains
   only interior cells `[1, 62]`, so the cell containing the lookup point always carries at least half of
   its 1D trilinear weight, and the two faces of a thin wall fall in different cells.
4. **All cascades are trained.** The scatter loop tries every cascade whose interior holds the cell,
   finest first, with per-cell caps 64 / 16 / 8 (`RcInfo.hdr1.z`, `coarseCaps = cap1 | cap2 << 8`,
   defaults 16 and 8; cascade 0 uses `hdr0.w`). Before, only the finest cascade holding the sample trained, so the
   4-cell fade into a coarser cascade, and a surface leaving the fine window, landed on
   cells nothing had trained.
5. **Hashed, load-adaptive accept replaces first-come caps.** A per-cell per-frame cap of 64 kept the first
   64 arrivals, so which samples counted depended on arrival order (the dispatch's pixel order). Now every arrival does
   one atomic on an arrivals array; last frame's arrival count sits in the high half of the counts word.
   The accept rate is `prev <= cap ? 1 : max(cap/prev, 1/32)`, and a sample is accepted when
   `hash16(hitPos, dir, cascade) < rate * 65536`, so the accepted set is a random subset and the mean stays
   unbiased. The old count atomic stays as the hard bound. The resolve writes
   `counts = min(arrivals, 65535) << 16` and clears the arrivals before its early-out, so a stale hint
   never lingers. Cost per sample: 1 atomic if rejected, 17 if accepted (arrival, accepted count, 12 SH,
   3 normal). The scatter loop stays `[loop]`, not unrolled: the twin is register-bound.
6. **Accumulator barrier.** The resolve zeroes the accumulator's counts and payload for the next frame,
   but only the cells had a barrier after it. `VoxiRenderer` now issues `uavBarrierBuffer(accum)` after
   `recordResolve`, before the next scatter.
7. **Trust field and accumulator layout (for stage 2a).** The normal word is
   `octU 10 | octV 10 | len 6 | trust 6` (63 = 1.0; length resolution 1/63, octahedral error under 0.15
   degrees). The accumulator is counts `[0, K)` as `accepted | prevArrivals << 16`, arrivals `[K, 2K)`,
   then 15 payload ints per cell from `2K`, `K = kCells`: `kAccumInts = 17 * kCells` (13,369,344, 53.5 MB;
   `info.accumInts` reaches table 0 through `Bindings`, no renderer change). `RcInfo` stays 80 bytes:
   `hdr1 = (alphaMin, relaxAlpha, asfloat(coarseCaps), netStrength)`. The resolve sets trust to 63 for a
   new or replaced cell and carries it for a valid one. The lookup weight gains `lerp(1, trust,
   netStrength)`; `netStrength` 0 reproduces the old formula exactly. `NeuRaC::logWarmupStats` reads the
   new length field.

The cascade lookup core now lives in `voxi_neurac_lookup.hlsli` (`rcLookupCascade`), included by
`voxi_neurac_io.hlsli` (the twins' `rcLookup`, scatter, visualiser) and by the guide pass; the including
shader declares `gRcInfo` and `gRcCells` by name (twin t22 / u21, guide t0 / u0). `NeuRaCLayout.hpp` carries
the CPU mirrors: `scatterCell`, `acceptRate`, `capForCascade`, `hash16`, `lookupCornerWeight`,
`packNormal` / `unpackNormal` with trust.

**Not fixed: primary-surface coverage.** Training the primary surface's cell from the F1 hit radiance
would mix lamp-lit radiance into a cache that holds lamp-free light, and on untraced pixels it would feed
cache reads back into training. Items 3 and 4 mitigate it; the guide's coverage counter measures what is
left.

**Path Tracing.** With Path Tracing on and the cache live, the PT twins (6 pipelines, 5.10 item 3) use
it differently from the F2 twins. In `CSRdGi` / `CSRdGiTrace` one pixel in four per frame (rotating with
`gRtHistParams.z`) is the *trainer*: it traces the whole path and scatters the path's first diffuse
segment, a cosine sample of the light arriving at the hit, which is what a cell holds. The other three
read the cell wherever it is at least 50% confident and the surface is diffuse enough (`ptCacheable`),
and skip the continuation. Path vertices and `CSRdRefl`'s `ptRadiance` also end in a confident cell. So
the cache is read inside the paths that train it: its samples are self-referential. That is why the
stage 2 network is off under Path Tracing (5.12).

**Tests.** `NeuRaCTest` (CPU): normal-word round trip with trust, the `AVER_RC_*` literals including the new
payload base, the wall case (the trained cell gets at least 0.5 of the 1D weight for both normal signs),
rate and cap tables, `kAccumInts == 17 * kCells` and `sizeof(RcInfo) == 80`. `NeuRaCGpuTest` (WARP, or `hw`
for the adapter; skips with 77 without a device) runs the real hash, scatter, resolve and lookup shaders
through a harness and compares them with the CPU mirrors: arrivals land in the predicted cells, caps and the
accept rate across frames, the resolve blend / relax / trust carry / aging, lookup value and `remaining`
with and without trust. `VoxiShaderCompileTest` builds the cache, the guide and the twins; the SPIR-V test
covers the changed NRD2 passes and the guide shader.

#### The guide for NRD2 (`NeuRaCGuide`)

NRD2's temporal anchor fills new pixels from their neighbourhood's current mean. Where the history is
new (disocclusion, first frames) that mean is the only thing the pixel has, so it is flat and carries no
shape. The cache holds a spatial prior for exactly those pixels, so a per-pixel lookup hands it to NRD2
(formula and settings in NRD2.md, "NeuRaC guide"; patent flag in NEURAA_NRD.md section 7, rule 17).

- **A standalone compute pass** (`NeuRaCGuide.hpp/.cpp`, `voxi_neurac_guide.hlsl`, entry `CSNeuracGuide`),
  with its own small layout: SRV t0 `RcInfo`, t1 view Z, t2 normal-roughness; UAV u0 cells (read only), u1
  the guide, u2 coverage counters; root CBV b3. Table 0 and the twins are untouched, and no monolith grows.
- **Placement:** after Stage B's draw, before `Nrd2::record`, because Stage B writes the G-buffer.
  `record()` barriers view Z and the normal from `gbufferState` to `NonPixelShaderResource` and back, as
  `Nrd2::record` does. GPU span "Voxi NeuRaC guide".
- **Maths:** the surface point is reconstructed as centimetres from the eye (`z * (forward + ndc.x * tanX *
  right + ndc.y * tanY * up)`) and added to a per-cascade `eye / size - originCell` computed in double on
  the CPU (about [32, 33)), so no absolute world position is formed in fp32. Then it is `rcLookupCascade`
  per cascade, finest first, blended exactly as `rcLookup` does. Output RGBA16F at the staged size (about
  29 MB at 1440p): rgb = cosine-convolved irradiance / PI, in D units; a = `1 - remaining`, the confidence.
  A non-finite value is no guide (rgb 0, a 0).
- **Coverage counter:** surface pixels and pixels with confidence at least 0.5, a fixed-order tree sum per
  group and two atomics; logged every 120 frames as `[Voxi] NeuRaC guide: X% covered`. This is the data
  for the defect left open above.
- **It runs only when** `voxi.nrd2NeuracGuide` is on, the cache is live and the guide pass built;
  otherwise NRD2 gets no guide and takes the exact no-guide path (bit-identical). Not on Vulkan, where
  NeuRaC does not run in-frame.
- **Cost:** estimate 0.1-0.3 ms at 1440p on the RX 7800 XT (each pixel touches up to 8 cells in each of up
  to 2 cascades; neighbours share cells). **UNVERIFIED.**
- **Unconfirmed:** which normal the twin's `s.N` is (shading or geometric) against the G-buffer normal the
  guide reads, and that the G-buffer is in `RenderTarget` state when the pass runs (NRD2 does the same).

### 5.12 Stage 2a: the trust network (2026-10-12; built, not run, benefit unverified)

**What it is.** A tiny MLP (`Aver.Render.Neural`'s `Mlp`, unmodified) learns a per-cell **trust** in
[0, 1], the "learned leak/blend weight" of stage 2a. Trust is baked into the cell's 6-bit trust field and
multiplies each corner's weight in `rcLookup`, so the twin pays one multiply. Training and inference run
per cell in standalone passes, in the new class `NeuRaCNet` (`NeuRaCNet.hpp/.cpp`, `voxi_neurac_net.hlsl`,
CPU reference `NeuRaCNetReference.hpp`).

**Why trust, not a per-query network.** A network in the twin cannot have the registers or a binding
(table 0 is full, the twin is a monolith at the register limit). A per-pixel pass would write and read
about 237 MB of features at 1440p. At cell level there is neither cost.

**Network.**

| Item | Value |
|---|---|
| Shape | 10 inputs, two hidden layers of 8 (ReLU), 1 output (Sigmoid): a couple of hundred weights |
| Loss, optimiser | L2; Adam, learning rate 2e-3, `weightEma` 0.99, `gradClamp` 2, which gives `safeBatchLimit` 16383, so the training batch is capped at 16383 records |
| Init | He-uniform, last layer's weights zeroed and its bias set to logit(0.9) = 2.197, uploaded with `uploadWeights`: trust starts at exactly 1 |
| Trust mapping | `trust = saturate(output / 0.85)`, baked as `round(63 * trust)` |

**Inputs** (all in [-1, 1] or [0, 1]). "Old" is the cell's resolved state before this frame's update,
"fresh" the SH of this frame's accumulator. Eo and Ef are the luminances of the cosine-convolved
irradiance / PI along the cell's own mean normal, from the old and fresh SH. The neighbours are the six face
neighbours in the same cascade; one counts if its tag matches, `n_eff > 0` and age < 15, weighted by
`saturate(2 * dot(cn_nb, nu)) * len_nb`.

| # | Input |
|---|---|
| 0 | `len` (planarity) |
| 1 | `n_eff / 15` |
| 2 | `age / 15` |
| 3 | `log2(1 + n_fresh) / 6` in training; **1.0 at inference**, so sampling noise is not read as unreliability |
| 4 | neighbour support, sum of weights / 6 |
| 5 | neighbour disagreement, clamped to 2 and halved |
| 6 | `clamp(log2((E_nbr + eps) / (Eo + eps)), -4, 4) / 4`, 0 with no support |
| 7 | anisotropy of the old SH: `norm(c1, c2, c3) / c0` in luminance, clamped |
| 8 | `nu.y` |
| 9 | `cascade / 2` |

**Label** (training only): `tau* = 1 - min(1, 2 rho)`, `rho = |Ef - Eo| / (Ef + Eo + 1e-3)`: a cell whose
fresh samples disagree with its old state is unreliable. The label cannot tell a leak from a real
lighting change; the relax hooks (5.11) and the warm-up bound the damage. Only **touched** cells get a
record: those that took samples this frame and hold a valid old state (tag matches, age < 15). **Eligible**
for training are touched cells with `n_fresh >= 8`, `n_eff >= 2` and `Eo + Ef > 1e-3`. Both lists are
thinned by a hash on (cell, frame), at a keep rate set from last frame's raw total, so the expected counts
sit at 98304 (inference, list size 131072) and 12288 (training, list size 16383).

**Passes, per frame**, inside the existing resolve block (`recordStagedRayDriven`):

```
twins scatter -> uavBarrier(accum)
NeuRaCNet::recordPre     reset the counters; features (one thread per cell: early-out on the counts word,
                         append the inference record and, if eligible, the training record); finish
                         (clamp counts to the list sizes); barriers; Mlp::recordTrain (training list,
                         at most 16383 records); Mlp::recordInfer (up to 131072 records)
rc_.recordResolve        old state + fresh accum -> new state; trust carried (new or replaced cell: 63)
uavBarrier(accum), uavBarrier(cells)
NeuRaCNet::recordApply   one thread per inference record: write the trust bits into the cell, only if
                         it is still valid after the resolve; accumulate loss and label / prediction stats
uavBarrier(cells)
```

The lists total about 8 MB (131072 inference records x 10 floats plus the training list, targets, outputs,
cell indices and counters). The network's buffers rest in `Common` and the passes bracket each write with
`bufferBarrier`, as `Nrd2::recordFeatures` does. `NeuRaCNet::invalidateBindings()` runs before any list is
freed. Whether `recordTrain` / `recordInfer` accept a live count above `maxCount` was not confirmed, hence
the finish pass.

**Warm-up and switches.** `netStrength = user * saturate(framesActive / 900)` (`warmStrength`) goes into
`RcInfo.hdr1.w`; `resetWarmup()` restarts it with a cache reset or when the net is re-enabled. The net is
off, `netStrength` 0, and stage-1 behaviour exact, when:

- **Path Tracing is wanted** (5.11, "Path Tracing": PT samples read the cache they train);
- `voxi.neuracNet 0`;
- the `Mlp` or the passes failed to build.

No weights are saved: the tiny net converges in seconds. Trust bits stay in the cells but have no effect at
strength 0.

**Settings and views.** `voxi.neuracNet` (bool, default 0 since 2026-10-12: on Caldera the label read Monte Carlo noise as error, mean trust settled near 0.5 and the per-cell trust became a 25 cm pattern in the lookup coverage; it stays off until the label is noise-calibrated), `voxi.neuracNetStrength` (0-1, default 1).
Visualiser mode 5 shows trust (`--neurac-view 5`). Every 120 frames the log prints
`[Voxi] NeuRaC net: step N, eligible N, mean label, mean pred, loss, mean trust` (the loss comes from the
apply kernel, the `Mlp` has no loss readout). GPU span "Voxi NeuRaC net".

**Cost.** Estimate 0.15-0.25 ms at 1440p on the RX 7800 XT (features 0.05-0.1, train about 0.05, infer
about 0.03, apply about 0.02); budget 0.5 ms. **UNVERIFIED.**

**Tests.** `NeuRaCNetTest` (CPU, against `MlpReference`): identity init gives trust exactly 1; on synthetic
cells, planar stable ones keep a high trust and leaky, fluctuating low-planarity ones get a clearly lower
one; label, eligibility and mapping bounds; training batch at most `safeBatchLimit`. `NeuRaCGpuTest` (WARP)
compares `CSRcNetFeatures` records with the CPU feature builder and checks the `CSRcNetApply` bit writes.

**How to judge it.** Its benefit is unverified. Look at the trust view for thin walls and corners, and the
Coverage view; compare against a `voxi.ptMode 1` render of the same pose with `voxi.neuracNet 0` and `1`
(net off under Path Tracing, so the comparison is ray-traced).

**Why 2a only, and 2b later.**

- 2b (KiloNeRF-style regional networks correcting the SH) needs `Mlp` with `instances > 1`, listed under
  "Not there yet" in `modules/render.neural/README.md`: a new kernel, a CPU twin, parity tests and a weight
  file version.
- A regional network's output also has to be baked per cell, because there is no query-time slot. A
  per-cell residual is 12 SH values, which does not fit the budget alongside training.
- It sits closer to claim 1 of US 11,610,360, because it outputs radiance. Counsel should see 2a first.
- 2a's measured benefit decides whether 2b is worth building.

**Counsel flag: US 11,610,360.** Claim 1 needs a neural radiance cache queried at a 3D position, an image
from its prediction, and weights updated from a second prediction at the end of an extended path. 2a's
construction: the cache is the non-neural SH grid; the network's only output is one scalar per cell, a trust,
never radiance; its target is derived from traced samples (the fresh accumulated SH against the cell's old
state); the cache enters as the baseline of that comparison and as an input feature, never as the label;
there is no second prediction at the end of a path and no network output is used as a label. A strict
reading of "targets from traced samples" is more cautious than this construction, so it is flagged. It is
off under Path Tracing for the same reason.

---

## 6. `Aver.Render.Neural` — the shared foundation (stage 2a)

An RHI-only static module like `Aver.Render.Denoise`: links `Aver.Core` + `Aver.RHI`, owns its shaders
(deployed with `aver_deploy_shaders`), records into a caller's `IRenderContext`, and never sees Voxi
types. The sketch below is the original design; the built API is `Mlp` / `MlpReference` / `ConvNet` (`modules/render.neural/README.md`), with the same kernel rules. Sketch:

```cpp
namespace aver::render::neural {

// What the device can run. Filled from DeviceCaps (+ a new OPTIONS4 native-16-bit query).
enum class Backend : u32 { PortableFp32 = 0, PortableFp16 = 1, VulkanCoopMatrix = 2, D3D12LinAlg = 3 };

struct MlpDesc {
    u32 inputs, outputs;
    u32 hiddenWidth, hiddenLayers;      // e.g. 8x2 (blend weights), 32x2 (local residual)
    Activation hidden = Activation::ReLU, output = Activation::None;
    u32 instances = 1;                  // >1 = many small networks (one per region, KiloNeRF-style)
};

struct OptimiserDesc {                  // Adam; fixed-point gradient accumulation, then a step pass
    f32 learningRate, beta1 = 0.9f, beta2 = 0.99f, epsilon = 1e-8f;
    f32 weightEma = 0.99f;              // queries read the smoothed copy (stability)
    f32 gradFixedScale;                 // fixed-point scale for u32 gradient atomics
};

class Network {
public:
    bool create(rhi::IDevice&, const MlpDesc&, const OptimiserDesc&);
    // Inference: records -> outputs. Records are a caller-owned structured buffer.
    void recordInfer(rhi::IRenderContext&, rhi::BufferHandle records, u32 count, rhi::BufferHandle out);
    // Training: records + targets -> gradient accumulation -> one optimiser step.
    void recordTrain(rhi::IRenderContext&, rhi::BufferHandle records, rhi::BufferHandle targets, u32 count);
    Backend backend() const;
};

// CPU reference of the same maths, for tests (no GPU needed).
std::vector<f32> referenceInfer(const MlpDesc&, std::span<const f32> weights, std::span<const f32> input);

}  // namespace aver::render::neural
```

**Kernel rules:**

- one thread per record;
- weights loaded cooperatively into groupshared per layer (layer-streaming, not one fused
  register-resident kernel; see §9);
- `[numthreads]` a multiple of 64, and no assumptions about wave size, so AMD wave32/64, NVIDIA 32 and
  Intel 8–32 all behave the same;
- fp32 by default, with an fp16 variant compiled only when the device reports native 16-bit ops.

**Engine changes it needs:**

- an `OPTIONS4` (`Native16BitShaderOpsSupported`) query in `queryCaps`;
- SM 6.4 and 6.2 added to the probed shader-model list (the `D3D_SHADER_MODEL` loop in `D3D12Device.cpp`), so a 6.4 device is not
  recorded as 6.1.

No Agility SDK.

---

## 7. Hardware backends

| Backend | Runs on | Status |
|---|---|---|
| Portable fp32 HLSL | every D3D12 GPU | stage 2a baseline |
| Portable fp16 HLSL | devices with native 16-bit ops | after the caps query lands |
| Vulkan cooperative matrix | AMD RDNA3+ (WMMA), NVIDIA (tensor cores) | after Vulkan parity; RDNA3 WMMA is ~1.5–3× plain fp16 at best |
| D3D12 LinAlg (SM 6.10) | RDNA4, NVIDIA, Intel | when retail; dev-only (Developer Mode) until then |

Weights are trained on the user's machine and never shipped, so each backend may use its own layout
and precision.

---

## 8. Measurements needed (stage 0, owner runs)

1. Occupied-cell fraction per cascade on PTTest and NeonDistrict (needs a small counter pass).
2. Cost of today's F2 trace (f2Path 3) and the cone march (f2Path 1) via `--gpu-timing`.
3. After stage 1: cache vs `HalfResolution` vs `Full` against the path tracer, on the same views.

---

## 9. Patents and licences (flags, not legal advice)

- **US 11,610,360** (NVIDIA, neural radiance caching). Independent claim 1 requires a *neural network*
  radiance cache queried at a 3D position, an image made from its prediction, and weights updated from
  a second prediction at the end of an extended path. Stage 1 has no network and no self-referential
  training. Stage 2 networks learn blend weights and residuals over a non-neural cache. Counsel should
  review stage 2 before it ships, and any later multi-bounce feedback. The exact stage 2a construction
  and the flag are in 5.12.
- **The guide (5.11):** a world-space radiance cache feeding a denoiser's disocclusion fill is not in any
  sweep entry; NEURAA_NRD.md section 7, rule 17 flags it for counsel.
- **US 11,631,210 / 11,935,179** (NVIDIA, fully fused execution): thread-block partitions, weights
  loaded once into the register file, intermediates in shared memory, layer by layer (and in '179,
  weight blocks sized to a hardware matrix unit). The module's kernels use per-layer groupshared
  streaming. Whether that is different enough is counsel's call.
- **US 11,315,310 / 12,299,801** (ReSTIR GI, ReGIR): why cache-guided ReSTIR sampling is parked, and why
  `voxi_reservoir.hlsli` (header comment) already forbids per-cell light reservoirs.
- **Code:** nothing is vendored. NVIDIA NRC/RTXGI/RTXNS (proprietary), instant-ngp (non-commercial)
  and VkNRC (no licence) are references only. tiny-cuda-nn (BSD-3, CUDA) and AMD MiniDXNN (MIT) are
  readable references.

---

## 10. Open decisions

| # | Decision | Recommendation |
|---|---|---|
| D1 | Widen `RdGiCand` with albedo/roughness now, or train on incident radiance only | Incident radiance now; widen in stage 3 |
| D2 | Cascade layout | 3 × 64³ at 0.25 / 1 / 4 m; fall back to L0 beyond 256 m. *Implemented as designed.* |
| D3 | First integration | New `RestirVisibility::Cached`: traced pixels train, untraced pixels read. *Implemented, with wire mode 2 + bit 128 (5.10).* |
| D4 | Movers in the cache voxeliser | Yes, from the start. *Moot: there is no voxeliser; sample-driven occupancy covers movers (5.10).* |
| D5 | Time budget on the RX 7800 XT at 1440p | Stage 1 ≤ ~0.6 ms; stage 2 ≤ ~0.5 ms more |

---

## 11. Sources

- Müller et al., *Real-time Neural Radiance Caching for Path Tracing*, 2021 — arXiv 2106.12372.
- Iwanicki, Sloan, Silvennoinen, Shirley (Activision), *Neural Light Grid*, SIGGRAPH 2024 Advances.
- Reiser et al., *KiloNeRF*, ICCV 2021. Fridovich-Keil et al., *Plenoxels*, CVPR 2022.
- Roughton, *ZH3: Quadratic Zonal Harmonics*, 2024. Ramamoorthi & Hanrahan, 2001 (irradiance and SH).
- Microsoft: Shader Model 6.4 (`dot2add`, `dot4add_*`), 6.9 retail notes, LinAlg (SM 6.10) preview.
- AMD GPUOpen: WMMA on RDNA3. Chips and Cheese: RDNA3 microbenchmarks.
- Google Patents: US 11,610,360; US 11,631,210; US 11,935,179.
- Aver source: `modules/render.voxi/shaders/voxi_restir.hlsli` (`giTraceInitialCandidate`),
  `voxi_neurac*.hls*`, `NeuRaC*.hpp/.cpp`; `voxi.hlsl` (`AVER_VOX_FIXED`); `VoxiRenderer.cpp`
  (`updateNeuRaC`, `recordStagedRayDriven`); `Voxi.hpp` (`RestirVisibility`); `D3D12Device.cpp` (`queryCaps`).
