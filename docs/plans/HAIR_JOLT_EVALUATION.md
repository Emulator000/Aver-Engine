# Hair / Groom (#16): should it build on Jolt's Hair module?

2026-10-11. Input to `UE_FEATURE_PARITY_PLAN.md` item 16 and the "J. More of Jolt" line. Read-only evaluation: no engine code was changed.

**Verdict: port Jolt Hair's approach into an Aver-owned GPU compute pass. Do not call Jolt's `JPH::Hair`.** The simulation is about 700 lines of readable, MIT, rod-based HLSL. The groom feature is mostly everything around it (asset, skinning, render, shading, LOD), and Jolt supplies none of that.

Read for this: all of `modules/physics.jolt/Jolt/Physics/Hair/` (2611 lines), all 15 `Jolt/Shaders/Hair*.hlsl` plus their binding headers, `Jolt/Compute/**` interfaces, `Jolt.cmake` and our `modules/physics.jolt/CMakeLists.txt`. Aver side: `render.skin`, `gpudeform`, `trifactor` (README), `physics` (README and ABI headers), `rhi` (`RHI.hpp`, `RHIResources.hpp`). The vendored tree is only `Jolt/` plus `LICENSE` (`modules/physics.jolt/README.md`, "What is here"), so there are no upstream release notes, samples, tests or perf numbers to read.

## 1. What Jolt Hair actually is

**Status, by its own header.** `Hair.h:21-33`: "currently still in development", and lists as missing: level of detail, wind, grid advection, collision against anything but `ConvexHullShape`, a real gradient class, a working gravity preload, memory frugality ("stores everything both on CPU and GPU"), more than one neutral pose, and optimisation.

**In our build it is linked but inert.** `Hair.cpp`, `HairSettings.cpp` and `HairShaders.cpp` are compiled into the `Jolt` target (`Jolt.cmake:372-379`). We set `JPH_USE_DX12`, `JPH_USE_VK` and `JPH_USE_MTL` OFF (`modules/physics.jolt/CMakeLists.txt:63-65`) and never set `JPH_USE_CPU_COMPUTE`. So no shader is built, and `CreateComputeSystem()` returns "Not implemented" (`Compute/ComputeSystem.h`, fallback branch). No Aver code uses Hair (word-grep over modules, tests, sandbox, scripting and Runtime finds only the English idiom "a hair" in comments and one stray "hair-card material slot" comment in `Runtime/src/GameRender.cpp:925`).

| Aspect | What the code does | Where |
|---|---|---|
| Strand model | Cosserat rod per strand: a vertex carries a position and an orientation quaternion (Bishop frame). Max 255 vertices per strand, 255 materials, strand fraction stored in 8 bits. | `HairSettings.h:58-72`; asserts `HairSettings.cpp:666,676`; 8-bit fraction `:709`; `HairStructs.h:46-49` |
| Guide vs render hair | Only `mSimulationStrandsFraction` (default 10%) of strands are simulated ("guides"). The rest are render strands that follow their single nearest guide by a rigid offset in the guide's Bishop frame. Three influence slots exist (`HairStructs.h:21`) but only slot 0 is ever filled, so there is no blending between guides. | `HairSettings.h:226`; `HairSettings.cpp:178,256`; `HairCalculateRenderPositions.h:3-14` |
| Constraints | Position-and-orientation-based rods (Kugelstadt and Schoemer, SIGGRAPH 2016): stretch/shear and bend/twist, solved Gauss-Seidel down each strand. "Stitched" three-vertex scheme per Pedersen, SIGGRAPH 2022. Plus long-range attachment (LRA) to stop stretching, and a pull toward a skinned rest pose ("global pose"). | `HairUpdateStrands.hlsl:8-70` (LRA, stretch, bend), `:72-154` (loop); `HairApplyGlobalPose.h:5-19` |
| Integrator | Substepped XPBD-style: 360 iterations per second by default, dt clamped to 1/30 s (so at most 12 iterations per step). Per-iteration: integrate, constraints, velocity update. Gravity, exponential damping, velocity caps. | `HairSettings.h:330,344-345`; `Hair.cpp:83-90`; `HairIntegrate.h:70-88` |
| Hair-hair interaction | No pairwise collision. A 32x32x32 (default) velocity-and-density grid: vertices splat in fixed point with `InterlockedAdd`, each vertex is nudged toward its cell's mean velocity, and an optional density-gradient force (default off, "can result in artifacts"). Grid is not advected. | `HairGridAccumulate.hlsl:8-50`; `HairCommon.h:12-13`; `HairIntegrate.h:10-68`; `HairSettings.h:223,346` |
| Collision with the world | CPU gathers bodies from the `PhysicsSystem` broadphase inside the hair's AABB (`CollideAABox`), expands compounds to leaf shapes, and keeps only `ConvexHull` leaves. Hulls are uploaded as face planes plus vertex/index tables. A GPU pass finds the deepest-penetrating hull per vertex and stores a contact plane. The velocity pass then pushes out, applies Coulomb friction, and zero restitution, using the body's linear and angular velocity. Spheres, capsules, boxes, meshes and heightfields are silently ignored. | `Hair.cpp:111-181` (gather), `:211,268` (hull-only), `:254-346` (upload); `HairCalculateCollisionPlanes.hlsl:8-114`; `HairUpdateVelocity.h:7-51` |
| Skinning | Optional. The scalp mesh is linear-blend skinned on the GPU (N weights per vertex), strand roots are pinned to scalp triangles by barycentrics (`SkinPoint`), and each root carries a frame. The scalp vertex and triangle buffers can instead be supplied externally, skipping Jolt's skinning. | `HairSkinVertices.hlsl:8-26`; `HairSkinRoots.hlsl:8-50`; `Hair.h:55-62`; `HairSettings.cpp:370-413,563-584` |
| Compute API | Jolt's own thin `ComputeSystem` / `ComputeQueue` / `ComputeBuffer` abstraction: shaders are created by name, buffers are bound by name, `Dispatch`, `ExecuteAndWait`. Backends: DX12, Vulkan, Metal, and a CPU fallback. It is a virtual interface, so a user backend is possible. | `Jolt/Compute/ComputeSystem.h`, `ComputeQueue.h`; `Hair.cpp:455-661` |
| Shader language | HLSL, `cs_6_0`. The same `.hlsl` is compiled as C++ for the CPU backend through macro shims (`ShaderCore.h`, `Compute/CPU/HLSLToCPP.h`) and by DXC to DXIL and to SPIR-V (`-spirv`). | `Jolt.cmake:625,689`; `Shaders/HairWrapper.cpp` |
| Data in | `HairSettings` (serialisable via Jolt's object stream, which we build OFF): sim and render vertices and strands, scalp mesh and skin weights, materials with per-strand-fraction gradients. `Init` derives lengths, Bishop frames, skin points, bounds and the neutral density grid. `sResample` makes strands a fixed vertex count. | `HairSettings.h:332-373`; `HairSettings.cpp:267-319,370-642` |
| Data out | `GetRenderPositionsCB()`: one `float3` per render vertex, in hair-local space, left on the GPU. Position, velocity and grid buffers are exposed too. CPU readback exists for debugging only and stalls (`ExecuteAndWait`). | `Hair.h:83-96`; `Hair.cpp:665-726` |
| Cost claims | None anywhere in the vendored tree. Unknown. Structure only: per update, `9 + 3N` dispatches with collision and grid on (N = iterations, so 27 at 60 fps and 45 at the 30 fps clamp), each with a UAV barrier. The constraint pass is one thread per guide strand walking the strand serially, so its parallelism is the guide count. Memory is roughly 100 B per guide vertex of live state (two position buffers, velocity, contact plane), plus rest-pose buffers. These are read from the code, not measured. | `Hair.cpp:441-661` |

What is good in it: a rod model that keeps hair shape with few guides, a clean separation of "guide sim" from "render interpolation", GPU-friendly vertex-major (transposed) layout (`HairSettings.cpp:701`), fixed-point atomics that make the grid sum order-independent, a skinned-rest-pose pull that stops sag, and a gather-compute-scatter split that fits a per-frame pass.

What is missing for us is the long list in `Hair.h:23-33` plus: any rendering, any asset import, any LOD, any non-hull collider.

## 2. What an Aver groom feature needs, and which existing module each piece extends

Owner rule: upgrade existing systems, no parallel ones. "New" below means a new capability inside an existing module's pattern, or a thin new join module where a dependency edge forces it (the same reason `render.softbody` is not folded into `render.skin`).

| Piece | Needs | Extends | Notes |
|---|---|---|---|
| Asset: strands | `.ochair` container (strands as point lists, per-strand width/random, root UV or scalp barycentrics, guide/clump hints), plus guide generation and `sResample`-style fixed counts. Importers: USD `BasisCurves` through the existing USD path, glTF `LINE_STRIP`, Alembic `.abc` curves (the UE route). | `Aver.Formats` (new `OcHair.hpp` beside `OcMesh.hpp`) | Today `GltfImport.cpp:316-318` skips every non-triangle primitive, and `Usd*.cpp` has no curve handling (grep for "curve" finds nothing). Alembic needs a reader: the real library is a new dependency and needs an owner yes; a hand-written Ogawa reader is the no-dependency option. |
| Asset: cards | Mesh strips with a hair-strand alpha texture, generated from strand clumps. | Existing triangle meshes: `.ocmesh`, `Aver.Render.Skin`, `Aver.Trifactor` | Cards are ordinary skinned triangle meshes, so skinning and LOD already work. Only generation is new (offline, in the importer tool). Trifactor's cluster/LOD DAG is triangle-only (`trifactor/README.md`) and cannot hold strands. |
| Component and scene join | A `CGroom` component (asset id, scalp mesh entity, LOD bias, shading parameters) and a join that owns per-entity GPU residency. | `Aver.Scene` (`Components.hpp`, beside `CSkeletalMesh` at `:152`) and the `SkinnedScene` pattern (`render.skin/.../SkinnedScene.hpp`) | Same shape: residency keyed by the full entity handle, per-instance ring buffers, retired-record reuse. |
| Skinning to a skeletal mesh | Roots and rest pose follow the character. | `Aver.Render.Skin` (`SkinningPass`, `SkinnedMeshGpu`) and `Aver.Anim` | Jolt's own design point: take the scalp as an external buffer (`Hair.h:55-62`). Here the scalp is the posed output buffer `SkinnedMeshGpu::out`, already a `rhi::MeshVertex` stream (32 B, `kSkinVertexStride`). Roots store (triangle, u, v) into that mesh's index buffer, readable via `IDevice::meshGeometry` (`RHI.hpp:537`). No second skinning pass for the head. |
| Simulation | Rod sim on the GPU, with a CPU reference. | New passes in the groom module, built like `SkinningPass` (own shader, own pipeline, inert when `caps().computeShaders` is false) | Parity discipline matches `Aver.Anim` `skinVertices` vs `skin.hlsl` and `Aver.Deform` vs GpuDeform: a small Core-only C++ reference solver, checked against the GPU on a real device. Jolt's trick of compiling one source as both HLSL and C++ (`HLSLToCPP.h`) is worth copying for that. |
| Collision with the character | Head, neck, shoulder and torso proxies; props. | `Aver.Physics` shapes and queries (capsule/sphere ABI in `physics_shapes_abi.h`, convex hull `physics_abi.h:312`, and the J-wave queries) and the ragdoll work | Prefer analytic sphere and capsule tests in the shader over Jolt's hull-plane loop (`HairCalculateCollisionPlanes.hlsl`). The character is a `CharacterVirtual`, "not a rigid body" (`physics_character_abi.h:138`), so Jolt's broadphase gather would never see it; the plan's "inner body" item (J, character improvements) or bone-attached proxies are required either way. |
| Rendering: strands | Camera-facing ribbons expanded on the GPU from render strand points. | The compute-written-mesh seam: `createSkinTargetMesh` (`RHI.hpp:518`), `drawMeshDepthOnly` (accepts compute-written meshes, `:638`), `createBlasUpdatable` / `refitBlas` | Written as `rhi::MeshVertex` (position, normal, uv), the ribbon is drawn by the existing path with no draw-path change, exactly as skinned meshes are. Cost: view-dependent geometry also feeds shadows and the BLAS (acceptable at v1). Strand tangent goes in the normal slot, strand parameter and random id in uv, since the vertex is fixed at 32 B. |
| Rendering: shading | Hair lobes (Kajiya-Kay-style anisotropic specular with two shifted lobes, or a Marschner-style model), self-shadowing/occlusion approximation, translucency to light, strand-count-aware coverage. | The shared material evaluator (`render.pbr/shaders/material_prelude.hlsl`) and Voxi (`voxi.hlsl`, the GI/RT includes) | The evaluator is shared by raster and rays, so a hair lobe goes there once. This is the riskiest piece: it is a new BSDF in a unified evaluator, and raytraced hair shadows cost scales with ribbon count. |
| LOD | Fewer render strands with wider ribbons by screen size; fewer guides; card fallback at distance. | `Aver.Trifactor` for the card stage only; strand LOD is new | Jolt has none (`Hair.h:25`). Strand LOD is naturally a per-instance render-strand fraction, which also bounds ribbon memory. |
| Editor and script | Groom asset slot, material and sim parameters, a viewport debug draw. | Existing editor panels, `Aver.Scene` components, C# component surface, graph nodes | Parameters mirror `HairSettings::Material` (`HairSettings.h:187-228`): the gradient-along-strand idea is worth keeping. |

Rough memory check (arithmetic, not measured): ribbons are 2 vertices per strand point at 32 B, plus 6 indices per segment. 20k render strands of 12 points is about 15 MB of vertices and 5 MB of indices; 100k strands of 12 points is about 77 MB and 26 MB. Strand-count LOD is therefore required, not optional, given how tight the engine's VRAM budget already is.

## 3. Module boundaries

The rule in `modules/physics/README.md` and `physics.jolt/README.md`: `Aver.Physics` links Jolt PRIVATE; `Aver.Physics` depends on Core only; `tests/physics` is the one permitted second consumer, and "a second consumer appearing there would mean the rule had stopped holding".

Using `JPH::Hair` directly breaks that on two edges at once:

1. **It needs `PhysicsSystem&` and a `ComputeSystem*` in the same call** (`Hair.h:81`). A `JPH::ComputeSystem` over our RHI must include Jolt headers and RHI headers. That module is either `Aver.Physics` (which would gain an RHI dependency and stop being Core-only, and a headless server build would carry it) or a new second Jolt consumer. Both defeat the link-line enforcement.
2. **Unit and basis mismatch.** Jolt is metres, right-handed, Y-up; Aver is centimetres, left-handed, Z-up (`physics.jolt/README.md`, conventions table). The conversion is done once on the CPU in `Convert.hpp`. Hair state is GPU buffers the CPU never touches, so the conversion would have to be baked into the shaders (gravity, radii, collider tables, root frames) and applied to the render positions. A mirrored basis also flips quaternion handedness for the rod frames. That is real work that a port does once in its own units.

Recommended boundary:

- **Strand state never crosses the ABI.** It lives in GPU buffers owned by the groom module (Render tier, generic RHI only, inert without compute, like `Aver.Render.Skin`).
- **The only physics-to-groom data is a collider feed.** One new flat-struct ABI query (in the style of the J-wave queries), e.g. "colliders in this AABB": kind (sphere, capsule, box, hull handle), transform, size, linear and angular velocity, plus hull points on request. The groom module also needs gravity, which the ABI already exposes. No `JPH::` type crosses.
- **Skeleton-attached proxies are a second source** that needs no physics at all: spheres/capsules parented to bones, authored with the groom, transformed on the CPU from the pose that `Aver.Anim.Scene` already keeps. This keeps hair working in builds with `AVER_MODULE_PHYSICS` off.
- **Dependency edges** for the new module: Core, RHI, Anim, Formats, Scene, and Render.Skin for the scalp buffer. It must not depend on Physics (feed by function-pointer/seam, the idiom `SkinPathFn`/`SkinMeshFn` already use).
- The `modules/gpudeform` skeleton is not wired and has nothing to extend.

RHI constraints to design around (`RHI.hpp:74-100`, `RHIResources.hpp`): compute exists on both D3D12 and Vulkan (`computeShaders`); scene-pass compute recording is D3D12-only today (`computeInScenePass`, `D3D12Device.cpp:2663`, Vulkan `VulkanDevice.cpp:1073` comment), so the groom passes must record in `prePass` as `SkinningPass` does; there is no indirect dispatch or indirect draw in either RHI header (so LOD means CPU-chosen counts); BLAS builds only from a `MeshHandle` (`RHIResources.hpp:463`), so strands reach ray tracing only as ribbon meshes. Jolt compiles these same shaders for SPIR-V with DXC (`Jolt.cmake:689`), and the repo already vendors a SPIR-V-capable DXC (`docs/VULKAN.md`), so D3D12/Vulkan parity of a ported sim looks feasible; not tested here.

## 4. Licensing and patents (only what I can state)

| Item | Statement | Confidence |
|---|---|---|
| Jolt licence | MIT. Every Hair file carries `SPDX-License-Identifier: MIT`, copyright 2026 Jorrit Rouwe (the vendored `LICENSE` says 2021; same grant). | Verified in the tree |
| What MIT requires | Keep the copyright and permission notice in copies of "substantial portions". If we port shaders closely, put the notice at the top of the ported files. The existing `THIRD-PARTY-NOTICES.txt` flow (`scripts/stage-payload.ps1`) already ships Jolt's `LICENSE` for the binary. | Licence text |
| Patent grant | The MIT text has no patent clause either way (unlike Apache-2.0). Jolt makes no patent statement in the Hair files (grep for "patent" over `Physics/Hair`, `Shaders`, `LICENSE`: zero hits). | Verified |
| Algorithms cited in source | Kugelstadt and Schoemer, "Position and Orientation Based Cosserat Rods", SIGGRAPH 2016 (`HairUpdateStrands.hlsl:21,51`); Pedersen, "Strand-based Hair System", SIGGRAPH 2022 (`:126`); Petrovic, Henne, Anderson, "Volumetric Methods for Simulation and Rendering of Hair" (`HairIntegrate.h:41`); Muller et al., extended PBD friction and restitution (`HairUpdateVelocity.h:28-46`). | Citations verified |
| Patent status of those methods, Kajiya-Kay / Marschner shading, strand ribbon expansion | **Unknown.** No patent search was run in this evaluation. The parity plan's rule (clean room plus a live patent check before design for anything Epic- or NVIDIA-shaped) applies; hair is both, and NVIDIA's hair work (HairWorks) is proprietary. | Unknown |
| Epic / Unreal code | Not read and must not be copied; the plan's clean-room rule stands. UE groom behaviour in this document comes from the parity plan's own description, not from engine source. | Policy |
| Alembic (`.abc`) | Licence and transitive dependencies (Imath, HDF5 for the legacy backend) not checked here. Needs a licence read before any vendoring and an owner yes (no new dependency without one). | Unknown |

## 5. Options

| | A. Use `JPH::Hair` | B. Port the approach into an Aver compute pass (recommended) | C. Skip #16 |
|---|---|---|---|
| Sim code we write | About 0, but a `JPH::ComputeSystem` adapter over the RHI (M) | Roughly 12 short HLSL shaders and a CPU reference (L); the source is MIT and ~700 lines | None |
| Boundary | Breaks "Jolt private to physics" or "physics is Core-only" | Holds; only a flat collider feed crosses | Holds |
| Collision | Convex hulls only; character invisible to the broadphase | Spheres/capsules first, hulls optional, bone proxies | n/a |
| Units/handedness | Baked into shaders anyway | Native cm / left-handed / Z-up | n/a |
| Vulkan parity | Must still rebuild shaders through our DXC path | Same, once, in our binding model | n/a |
| Upstream risk | Module self-described as in development; we are pinned to 5.6.0 and may not edit vendored files | None beyond attribution | None |
| Groom value delivered | Sim only; assets, render, shading, LOD all still ours | Same pieces, sim owned | Hair stays MISSING |

A saves the least: the sim is the smallest part of the feature, and the glue to host it costs about what the port costs while adding permanent coupling.

## 6. Recommendation and phased plan

Port (B). Sizes follow the parity plan (S hours, M a day, L several days, one workflow per phase). All are estimates, none measured. Hair stays at the end of the plan, per the owner's note that it is done only if everything else finishes first.

| Phase | Work | Size |
|---|---|---|
| 0. Gates | Live patent check on the methods above (owner's rule). Owner answers: Alembic as a dependency, or USD/glTF/own format only. | S |
| 1. Asset | `OcHair` container and round-trip tests; USD `BasisCurves` and glTF line import; guide selection and fixed-count resample; procedural strand generation from a scalp mesh (clump/length/curl) so a groom exists without Alembic. Alembic reader if approved. | M (L with Alembic) |
| 2. Sim | New groom module: GPU rod sim (integrate, stretch/shear, bend/twist, LRA, global pose, root follow, sphere/capsule collision, friction), per-instance residency, CPU reference solver and a GPU-vs-CPU parity self-test, teleport handling. Collider feed ABI in `Aver.Physics` plus bone-proxy source. Defer the velocity/density grid. | L |
| 3. Render | Ribbon expansion into a compute-target `MeshVertex` stream, `CGroom` and the scene join beside `SkinnedScene`, scalp roots from `SkinnedMeshGpu::out`, depth prepass and BLAS refit; first hair shading lobe in the shared evaluator; view-dependent ribbon width and coverage. D3D12 first, Vulkan second (the plan's parity rule). | L |
| 4. LOD and cards | Render-strand fraction by screen size, guide-count reduction, offline card generation and card fallback through existing skinned-mesh and Trifactor paths. | M |
| 5. Authoring and extras | Editor panel and viewport debug draw, C# surface and graph nodes, then the grid forces, wind, and a second rest pose (the items Jolt itself lists as missing). | M |

Total: roughly three to four L-sized workflows' worth. Phase 2 is the only place Jolt Hair is consulted line by line; phases 1 and 3 are where the feature succeeds or fails and have no Jolt content at all.

Open risks: the hair lobe inside the unified evaluator; ray-traced hair shadow cost and BLAS refit of a view-dependent ribbon mesh; ribbon VRAM at high strand counts; no indirect draw/dispatch in the RHI; Vulkan scene-pass compute not available (mitigated by recording in `prePass`).
