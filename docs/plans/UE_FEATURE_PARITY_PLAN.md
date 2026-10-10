# Unreal Engine feature parity: plan (APPROVED 2026-10-11)

2026-10-11. The source was Epic's UE 5.4 to 5.8 documentation (feature pages and release notes). Each feature was checked against Aver's CODE, not its docs, by three survey agents. The STATUS.md file says it is stale, and the "written without a build" notes in GAME_UI.md and AI_CROWDS_HEARING_COVER.md mean some PARTIAL items are unverified at runtime.

Rules for all of it:
- General purpose only.
- Module boundaries hold.
- D3D12 and Vulkan parity.
- Clean-room designs where Epic or NVIDIA ideas are patented (Nanite, VSM and MegaLights get a patent check before any design).
- Sonnet workflows with distinct file ownership.
- Main loop builds, tests, runs the engine headed, and commits locally. No push, no release.

## The 30 features: what Aver has, and the v1 to build

Sizes are S (hours), M (a day) and L (several days, one workflow per phase).

| # | UE feature | Aver today | v1 to build | Size |
|---|---|---|---|---|
| 1 | Nanite | PARTIAL: Trifactor clusters and LOD DAG, but the mesh-shader path is behind `--lod-mesh-shader` | Default it on. Add HiZ two-pass occlusion culling of clusters, plus Vulkan. | L |
| 2 | Lumen | PARTIAL (strong): voxel GI, ReSTIR, ray-driven, NRD2 | Fallback for non-DXR GPUs: voxel-traced rough reflections and AO | M |
| 3 | Virtual Shadow Maps | PARTIAL: cascades plus RT shadows; Vulkan never writes cascades | Fix the Vulkan cascades. High-resolution clipmapped shadow map for non-RT GPUs (patent check first). | L |
| 4 | MegaLights | PARTIAL: per-light shadow rays | Stochastic many-light sampling: ReSTIR DI over the scene-light table. Clean room, patent check. | M |
| 5 | TSR | PARTIAL: AverSR TAAU, FSR1, NeuRAA | Packaged runtime uses TAAU; temporal path on Vulkan | S |
| 6 | Substrate / Material Editor | PARTIAL: 58-node graphs, one layer | Material instances (parameter overrides) and material functions. A graph is created from the editor, not by hand. | M |
| 7 | Virtual Texturing / texture streaming | MISSING (BC7 cache only) | Runtime mip streaming with a residency budget (feedback-driven) | L |
| 8 | Sky, Volumetric Clouds, Height Fog | PARTIAL (strong): physical sky and one cloud slab | Froxel volumetric fog with light shafts, multi-layer clouds with cloud shadows | M |
| 9 | Path Tracer + Movie Render Queue | PARTIAL: PT and a PNG-sequence player | Render queue: per-shot settings, N-sample accumulation per frame, EXR, MP4 via Media Foundation | M |
| 10 | Post-process volumes + lens effects | PARTIAL: one global PostSettings | Post volumes with blending; LUT grading, DOF, motion blur, vignette, grain, chromatic aberration, lens flare | M |
| 11 | World Partition (HLOD, Data Layers) | PARTIAL: distance-cell streaming | HLOD proxies per cell (merged and simplified with Trifactor), data layers, per-cell editor load UI | L |
| 12 | Level Instances / Packed Level Actors | PARTIAL: prefabs only | Level instance (`.ocworld` as an instance) and packed, merged static batches | M |
| 13 | Landscape + Water | PARTIAL: sculpt, Gerstner and the new water material | Landscape paint layers with a layered material; spline rivers and lakes; buoyancy from the real wave height | L |
| 14 | PCG framework | PARTIAL: F# PCG, GPU density, chunk generator | Node-graph PCG editor (points, filters, spawners) on the existing samplers | L |
| 15 | Modeling Mode / Geometry Script | MISSING | Basic modeling: box/cylinder primitives, extrude, bevel, booleans (CSG), with a C# geometry API | L |
| 16 | Hair / Groom | MISSING | Later wave: strand asset from Alembic or curves, card generation, hair shading | L |
| 17 | Blueprints | PARTIAL: 240 node types, C# Roslyn | Hot reload keeps state (field migration); graph debugger with breakpoints | M |
| 18 | Gameplay Ability System | MISSING | C# framework: gameplay tags, attribute sets, gameplay effects (instant, duration, periodic), abilities with cooldown and cost, plus graph nodes | L |
| 19 | Enhanced Input | PARTIAL: `.ocinput`, gamepad, rebind UI | Triggers (hold, tap, chord) and modifiers (dead zone, swizzle, negate); verify and wire the `.ocinput` C# loader | M |
| 20 | UMG / Common UI | PARTIAL, unverified | Verify end to end. Add widget animation, styles/themes, gamepad focus routing (activatable stacks). | M |
| 21 | StateTree + EQS + Smart Objects | PARTIAL: BT, blackboard, navmesh, crowds | EQS (generators, tests, scoring), Smart Objects (claimable slots), StateTree asset and editor | L |
| 22 | Mass Entity | PARTIAL: GPU ORCA crowds | Later: data-oriented fragment/processor layer over crowds | L |
| 23 | Networking (Iris) + sessions | MISSING (README stubs) | UDP transport, entity replication (property deltas, relevancy), RPCs, client prediction for the pawn, LAN sessions | L |
| 24 | Control Rig + IK Rig + Retargeter | PARTIAL: two IK ops | Full-body IK (FABRIK and CCD chains), rig graph ops, IK retargeting between skeletons (chain mapping) | L |
| 25 | Motion Matching | MISSING | Pose database, trajectory features, KD-tree search, blending into the anim graph | L |
| 26 | Anim Blueprint extras | PARTIAL: state machines, blend spaces | Montages with slots, layered and additive blends, runtime root motion | M |
| 27 | Sequencer | PARTIAL: camera tracks | Actor transform, property, event and audio tracks; sub-sequences | M |
| 28 | Chaos Destruction / ragdoll / cloth | PARTIAL: Jolt vehicles, soft body | Voronoi pre-fracture into geometry collections with Jolt break thresholds; ragdoll from a skeleton; cloth on skinned meshes | L |
| 29 | Niagara | PARTIAL: CPU billboards | GPU compute particles, ribbons, mesh particles, module stack in the editor | L |
| 30 | MetaSounds + spatial audio | PARTIAL: offline synth graph, reverb zones, occlusion | Real-time sound graph, HRTF panning, Ogg/Vorbis decode | M |
| + | Tools: Insights, data tables, localization, source control | PARTIAL or MISSING | Timeline profiler capture/view; CSV data tables; string tables and culture switch; git commit/checkout from the editor | M each |

## Order (waves; each wave is one or two workflows, finished and verified before the next)

0. **Finish the approved plan in flight:** Vulkan blended replay, USD importer fixes, the instance-set editor, and the Caldera re-import with sea, roads and catenary.
1. **Rendering polish, high visibility, contained:** #10 post volumes and lens, #8 volumetric fog, #5 TSR in the runtime, #9 render queue and video.
2. **Gameplay foundations:** #18 GAS, #19 Enhanced Input triggers, data tables and localization, #26 montages and root motion.
3. **AI and world:** #21 EQS, Smart Objects and StateTree; #12 level instances; #13 landscape paint layers and rivers.
4. **Big rendering:** #1 Nanite default with occlusion culling; #4 many-light sampling; #6 material instances; #7 texture streaming.
5. **Animation:** #24 IK and retargeting; #25 motion matching; #27 sequencer tracks.
6. **Physics and VFX:** #28 destruction, ragdoll and cloth; #29 GPU particles.
7. **Networking:** #23. This is the largest single item and its own milestone.
8. **Tools and the rest:** #14 modeling, #11 HLOD and data layers, #14 PCG editor, #30 audio, #17 hot-reload state, Insights-like profiler, source control, #16 hair, #22 Mass, #2 and #3 non-RT fallbacks.

Tonight: wave 0, then wave 1 as far as it gets verified.

## Added by the owner (2026-10-11, after approval)
- **J. More of Jolt** (wave 1b), exposed through the existing physics ABI and C# layer:
  - ragdolls built from a skeleton (also covers #28's ragdoll);
  - constraint motors and limits;
  - sensors and triggers;
  - shape casts and collide-shape queries;
  - collision groups and filters;
  - character virtual improvements;
  - Jolt's Hair module evaluated for #16.
- **M. Module cherry-picking** (wave 1c):
  - One manifest per module (name, dependencies, optional or required, description) is read by CMake (`cmake/AvModule.cmake`, `aver_check_module_dag`) and by the Aver Launcher.
  - The launcher shows module checkboxes. A module can be disabled only when no enabled module depends on it, and enabling one enables its dependencies.
  - The selection becomes CMake cache options / a preset.
  - The `scripts/module-guard-audit.py` and `module-matrix.ps1` checks are kept green.

## Owner decisions (2026-10-11)
- Scope and order approved. **Upgrade existing systems, do not add parallel ones**: virtualized geometry on Trifactor, GI and lighting in Voxi, particles in modules/particles, and so on.
- Hair (#16) and Mass (#22) are done only if everything else finishes before the owner is back; otherwise the owner decides.
- Third-party code was not answered, so the defaults are: Media Foundation (part of the OS) for MP4; our own UDP layer for networking (no new dependency); Ogg waits for a yes on stb_vorbis.

## Decisions originally asked
1. Is the order and scope above right, or should anything move?
2. Third-party code (vendored only after a yes):
   - stb_vorbis (public domain) for Ogg (#30).
   - Media Foundation (part of Windows, nothing vendored) for MP4 (#9).
   - Networking (#23): our own UDP layer versus ENet (MIT) or GameNetworkingSockets (BSD).
3. Hair (#16) and Mass (#22) are at the end. Keep them, or drop them from the list?
