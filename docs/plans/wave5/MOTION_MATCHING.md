# Feature #25: Motion matching (pose database, trajectory prediction, inertialization)

Design contract for the implementing agent. Parity plan: `docs/plans/UE_FEATURE_PARITY_PLAN.md` row 25, wave 5. Nothing here is built. Format and depth follow `docs/plans/wave2/MONTAGES_ROOT_MOTION.md`, which is now implemented (`docs/ANIM_MONTAGES.md` describes what shipped). Rules in force: upgrade the existing systems (no parallel animation system: a matcher is one more pose source and one more state-machine state kind), module boundaries hold (`docs/MODULES.md`), every C# capability gets graph nodes, no new third-party code, lean comments, D3D12/Vulkan parity (CPU pose math that ends in `Pose`/`poseToSkinning`, which both backends already consume), patent safety (section 2 is binding on the implementer).

## 0. Decisions in one screen

1. **One global nearest-neighbour search over a normalised, weighted feature vector.** Features: selected-bone positions and velocities (in the root's heading frame), root velocity, future and past root-trajectory samples. No phase channels, no per-limb or per-contact matching, no learned components, no re-matching from a physics/ragdoll pose. Section 2 says why; those are the EA claims that are live.
2. **Authoring = `.ocmotion` (JSON)** naming a skeleton, the feature layout, search/spring/inertialization defaults and a list of `.ocanim` clips, each with tags, optional tagged sub-ranges and a cost bias. **Baked = `.ocmbake` sidecar** (AVR1 subtype `MMDB`, next to the asset, ships as ordinary Content like `.ocinst`), keyed by a hash of the asset text plus the skeleton and clip bytes. A missing or stale sidecar is rebuilt on a worker thread; the editor and runtime both write a fresh one best-effort.
3. **Search**: exact. SSE2 brute force with per-row filtering, bias and an early-out; above 40,000 searchable rows an exact branch-and-bound KD-tree over an 8-D PCA projection (the projection is non-expansive, so it is a true lower bound and the result equals the linear scan). Search runs every `interval` seconds (default 0.1) or when forced or when the current range ends; a switch happens only when `best + margin < currentCost`, so the matcher does not thrash.
4. **Switching uses inertialization** (one pose evaluated, an offset decays with a critically damped spring and velocity continuity), not a crossfade. Crossfade stays for state-machine transitions into and out of a motion-matching state.
5. **Trajectory prediction** is a closed-form critically damped spring on the desired velocity and on heading, fed by the script's desired velocity/facing and the entity's *measured* motion (history ring). Query pose features come from the stored database row of the playing frame, never from the output skeleton.
6. **Integration**: (a) `CMotionMatcher` component + `MotionMatchSystem`, chained behind `AnimGraphSystem` in `AnimSystem`'s pose-source seam (the ragdoll's chaining arrangement, applied to the source instead of the modifier); (b) a new `.ocasm` state kind `motionmatch`, so a state machine can mix locomotion matching with authored clip/blend-space/sub-machine states, montage slots and the layered pipeline.
7. **Root motion** reuses wave 2 end to end: the matcher always locks the planar root of its output pose (the mesh stays on the entity), and reports the extracted delta through `AnimSystem::addRootMotion` when the entity's `CRootMotion` asks for it (Apply/Accumulate; the physics-character External path is unchanged). Database clips do **not** need the `kOcAnimRootMotion` flag: the database declares them as root-carrying, via a new unflagged helper in `RootMotion.cpp`.
8. **No new C ABI.** Inputs and outputs travel as fields of a runtime-registered component over the generic scene field ABI, exactly as `CAnimGraph`/`CRootMotion` do. `framework_abi.h` and every `*_abi.h` are untouched, so no ABI-version collision with other wave-5 agents.
9. **Dynamic tags** (gameplay says "no stopping now", "only turn-in-place poses") travel as `fnv1a64` hashes in 4+4 component slots; the database owns the hash-to-bit table (up to 32 tags).

## 1. What exists today

| Piece | Where | State |
|---|---|---|
| Pose = flat `std::vector<Transform> local`; `restPose`, `poseToModel`, `blendPose`, `blendPoseMasked`, `addPose` | `modules/anim/include/aver/anim/Pose.hpp:17,26,30,53,57,77` | Complete. FK for features = `poseToModel` (Pose.hpp:30). No model-space pose type, no quaternion log/exp/conjugate anywhere in `Math.hpp:83-142` (the inertializer carries its own). |
| Clip sampling `sampleAnimation(clip,t,pose)`, `sampleTrack(track,t,Transform&)` | `AnimSampler.hpp:22,26` | Complete; `sampleTrack` is how root travel is read without sampling a whole pose. |
| Root motion: `RootSpec`, `RootDelta`, `rootBoneOf`, `yawOf`, `rootDeltaBetween`, `composeDelta`, `lockRoot` | `RootMotion.hpp:13-51`, `RootMotion.cpp:52-106` | Wave 2. `rootDeltaBetween` returns `{}` unless the clip carries `kOcAnimRootMotion` (`RootMotion.cpp:61-63`); the internal `stepDelta` (`:29-42`) is what a database needs, unflagged. |
| Clip flags `kOcAnimLoop` (bit0), `kOcAnimRootMotion` (bit2), notifies, `notifyDurations`, curves | `modules/formats/include/aver/formats/OcAnim.hpp:93-97,144-226` | Notify states exist but carry events on playback; tags live in the new asset instead (section 4.1). |
| AVR1 container (`parseAvr1/writeAvr1`, `AvrStringTable`, `avrHash64`) | `modules/formats/include/aver/formats/Avr1.hpp:72-125` | Reused for `.ocmbake`; `Aver.Anim` already links Formats. No compressed chunks (`FORMAT_SPECS` note, `docs/PACKAGING.md:104`), which suits an aligned float matrix. |
| JSON asset I/O pattern: `esc`/`num` (`%.9g`)/`header()` tag+version gate, `fmt::JsonValue` | `modules/anim/src/AnimGraphAsset.cpp:14-37,154`, `modules/formats/include/aver/formats/Json.hpp` | Private helpers; `.ocmotion` gets its own small header `AnimJson.hpp`, `AnimGraphAsset.cpp` is not refactored by this feature. |
| State machine: `AsmStateKind {Clip, BlendSpace, SubMachine}`, `Layer` with `clip`/`BlendSpacePlayer`, `pushLayer`, `tick`, `evaluate`, per-layer root deltas | `AnimStateMachine.hpp:27,141`, `.cpp:144-172` (pushLayer), `:287-350` (tick), `:368-391` (evaluate) | The mixing point. A layer is weighted by `layerWeights()` (cpp:352) and its root delta is summed with the same weights (cpp:323-327). |
| `.ocasm` state kind names and version gate | `AnimGraphAsset.cpp:59` (`kindName`), `:308-309` (parse), `:234` (write version), `AnimGraphAsset.hpp:17` (`kStateMachineVersion = 2`) | Extend with `motionmatch`; version 3 only when such a state exists. |
| Pose source seam (`PoseSourceFn`, single) and modifier seam (`PoseModifierFn`, chained by `RagdollSystem`) | `AnimSystem.hpp:53,59,113,117`; `RagdollSystem.hpp:5-10,25-26` | `AnimGraphSystem::install` takes the only source (`AnimGraphSystem.cpp:62-66`). There is no source getter, so a second source cannot chain. |
| `AnimSystem::tick` order: prune, montages tick, per entity `restPose`, `poseSrc_` (`AnimSystem.cpp:417`), CAnimator clip fallback (`:421`), DefaultSlot montages (`:451-470`), root mix/accumulate/apply (`:472-513`), `poseMod_` (`:522`), `poseToSkinning` (`:524`) | `modules/anim.scene/src/AnimSystem.cpp` | MM sits at `:417`; montages and root motion already compose with a sourced pose. |
| `AnimSystem::addRootMotion` / `rootSpec` / `evaluateSlot`; `pendingBase_` consumed only when `sourced && rootMode` | `AnimSystem.hpp:124-130`, `AnimSystem.cpp:120-148,418-420` | The root-delta channel MM uses. |
| `AnimGraphSystem`: auto-adds `CAnimator` (`AnimGraphSystem.cpp:135`), builds `AnimStateMachine`, ticks, `sourceThunk` (`:200`) | `AnimGraphSystem.cpp:121-220` | Gains the MM resolver and the MM query hand-off. |
| `AnimSystem::clear()` drops every clip (`AnimSystem.cpp:173-214`), called at project open (`Runtime/src/GameContent.cpp:88`) | | Anything holding `const OcAnimation*` must drop on it. Today the state machines rely on project-open ordering; the matcher needs an explicit epoch (section 6.3). |
| Per-frame host order: `game::tickAnimGraphs(dt)` -> `ragdoll.tick` -> `animSystem().tick` | `Runtime/src/GameApp.cpp:2092-2096`, `sandbox/src/SandboxApp.cpp:2222-2226`; `tickAnimGraphs` body `GameSystemsWiring.hpp:162-169` | The motion-match tick joins inside `tickAnimGraphs`: no host edit. |
| Component registration pattern for runtime components (`CAnimGraph`, `CRootMotion`): `registerComponent<T>`, `.field(...)`, `verify(sizeof)` | `AnimGraphSystem.cpp:24-51`, `AnimSystem.hpp:68-79` | Pattern for `CMotionMatcher`. |
| C# surface without P/Invoke: params as hash/value slots over the field ABI | `scripting/csharp/Aver.Framework/AnimGraph.cs:19-77`, `RootMotion.cs:53-60` | Pattern for `MotionMatching.cs`. |
| Physics character: `wish` computed then `aver_phys_character_set_velocity`; wave 2 `ApplyRootMotion` | `Character.cs:237-250,261-296` | Where the matcher's input is fed and (optionally) the body is eased. |
| Graph node tables | `scripting/csharp/Aver.Graph/GraphGameSystemNodeTable.cs:44,109-120`, `sandbox/src/GraphNodeDefsGameSystems.hpp:82,208-240` | Pattern for the nodes in section 11. |
| Debug overlay pattern: pure feed -> `rhi::LineVertex` -> `createLineMesh/drawLines`, labels on the ImGui foreground | `sandbox/src/AiDebugOverlay.hpp:1-60`, `SandboxRender.cpp:1961-1972,2008`, `SandboxShell.cpp:2150-2163` | Pattern for the trajectory overlay. |
| Asset-editor registry and create entries | `SandboxApp.cpp:445` (factory), `SandboxContentBrowser.cpp` (`cbCreate*`, type labels), `sandbox/src/MontageEditor.hpp` | Pattern for the database editor. |
| SIMD in engine code | only `Jolt/Core/Core.h`; no `/arch:` flag in any CMake file | x86-64 SSE2 is baseline; AVX2 would be a build-flag decision (open question). |

**Missing**: any pose database, feature extraction, nearest-neighbour search, trajectory prediction, inertialization, a second pose source, a state kind that is not a clip, and any editor/debug view for them. Declared-but-unread precedent to avoid: every field added here has a reader in the same change (section 12 lists which test reads it).

## 2. Patent check (live, 2026-10-11) and the design-around rules

Method: web search plus Google Patents page fetches. Claim text could not be read in full for the EA documents (the pages truncate before the claims), so findings rest on the abstract, summaries and the claim paraphrases the fetch returned. A negative web search is not clearance; counsel confirmation is an open question (section 14).

| Item | Finding | Consequence |
|---|---|---|
| **EA US 11,562,523 B1** (filed/priority 2021-08-02; continuation **US 11,995,754 B2**, granted 2024-05-28) "motion matching using local bone phases" | Active, est. expiry 2041-08-02. Independent claims 1 and 11 (method, system): access first animation control information including a pose and **local bone phases** (phase information tied to contacts of rigid bodies with the environment); run a **local motion matching process for each local bone phase** (matching criteria, select a local pose); generate the second pose from the matched local poses; render. Dependents: 2-D phase vectors, nearest-neighbour search, motion-type filter, global-pose blend, bone-phase interpolation. | **Never** add contact/phase features, never run separate matches per bone or limb and combine them. |
| **EA US 12,403,400 B2** (priority 2022-03-31; pub. US 2023/0310998 A1) "Learning character motion alignment with periodic autoencoders" | Active (granted 2025-09-02), est. expiry 2042-10-25. Motion matching over a **motion phase manifold** of **local motion phase channels** (2-D vectors: phase, frequency, amplitude), per-channel candidate scoring, nearest-neighbour selection. | No phase manifold, no periodic autoencoder, no learned phase. |
| **EA US 9,827,496 B1** (Zinno; priority 2015-03-27, est. expiry 2036) "example-based motion synthesis" | Active. Claim 1 (the only independent claim fully visible): a **collision** is detected, a **ragdoll model** is updated to a final pose, **reference features are extracted from that final pose** and the best motion-capture frame is displayed. The abstract also covers choosing a transition start frame by comparing reference features (those claims were not visible). | **The matcher never extracts features from a physics/ragdoll/IK-modified pose** and never re-selects a frame because of a collision or force. Query pose features come from the stored database row (decision 5). The wave-1b ragdoll chain runs *after* the matcher and is never read back. A future "get-up from ragdoll" feature needs its own patent review. |
| Ubisoft (Clavet, "Motion Matching and the Road to Next-Gen Animation", GDC 2016; Holden et al., "Learned Motion Matching", SIGGRAPH 2020) | Searches (web, patent-adjacent) found the talks, paper and blog posts and **no Ubisoft patent or application** on motion matching, learned motion matching or the decompressor. These are public disclosures, i.e. prior art for the basic technique (feature database, trajectory query, periodic search, short blend). | The plain algorithm is free to build. **Learned** motion matching (decompressor/stepper/projector networks) is out of scope: no need and not cleared. |
| Microsoft (Bollo, "Inertialization", SIGGRAPH 2017 talk / GDC 2018) | No patent found. Public technique; Unreal ships equivalent dead-blending/inertialization nodes and Holden's "Dead Blending" write-up is public. | Use the textbook critically damped decay with velocity continuity (section 5.4). Flagged for counsel anyway. |
| Others surfaced | US 11,282,214 B2 (fitness pose comparison), Empire Technology US 9,925,461 (assembling mocap segments from start/target pose by multi-path search; expired) | Not applicable. |

**Binding implementation rules** (also in the `MotionMatcher.hpp` banner): (1) one feature vector, one search; (2) no phase, contact-phase, frequency or amplitude features; (3) no body-part-partitioned search (upper/lower body search is a future item that needs a patent re-check, not a quiet option); (4) pose-part of the query is read from the database row, never from `Pose` after any modifier/ragdoll/montage; (5) no neural components.

## 3. Frame order and data flow (contract)

```
host frame (unchanged hosts):
  tickGameplay                 C# Drive: feeds SetMotionInput (desired velocity/facing), eases the body
  game::tickAnimGraphs(dt):
    motionMatchSystem().tick   per CMotionMatcher: read fields, observe entity motion (history), build the MotionQuery;
                               standalone entities: matcher.tick (search / switch / advance / pose / root delta);
                               write outputs; poll/start database loads
    animGraphSystem().tick     machines with a motionmatch state: setMotionContext(skel, query) then sm.tick (MM layers tick inside)
  ragdoll.tick(dt)
  AnimSystem::tick(dt):
    montages tick                                           (unchanged)
    per animator entity: restPose
       sourced = poseSrc_(...)   // AnimGraphSystem's thunk first; if it declines, MotionMatchSystem's thunk
       DefaultSlot montages, root mix/apply                (unchanged: sees MM's addRootMotion delta)
       poseMod_ (control rig, ragdoll)                     (unchanged; never read back by the matcher)
       poseToSkinning
```

Both hosts already call `tickAnimGraphs` right before `AnimSystem::tick` (`GameApp.cpp:2092`, `SandboxApp.cpp:2222`), so the motion-match tick needs no host edit.

## 4. Assets

### 4.1 `.ocmotion` (JSON, UTF-8; `"format":"ocmotion"`, `"version":1`)

Written/parsed in `modules/anim/src/MotionAsset.cpp` with the conventions of `AnimGraphAsset.cpp`: floats `%.9g` (lossless f32), parse does not call `valid()` (a half-authored asset round-trips), unknown keys ignored, a newer version refused, write is deterministic (byte-identical re-save).

```json
{
  "format": "ocmotion",
  "version": 1,
  "name": "Locomotion",
  "skeleton": "Skeletons/Character.ocskel",
  "rootBone": "",
  "sampleRate": 30,
  "tags": ["idle", "walk", "run", "turn", "stop"],
  "features": {
    "bones": [
      {"bone": "foot_l", "pos": 1.0, "vel": 0.8},
      {"bone": "foot_r", "pos": 1.0, "vel": 0.8},
      {"bone": "hips",   "pos": 0.5, "vel": 1.0}
    ],
    "rootVelocity": 1.0,
    "future": {"times": [0.2, 0.4, 0.7, 1.0], "pos": 1.0, "dir": 0.8},
    "past":   {"times": [-0.2, -0.5], "pos": 0.5}
  },
  "search": {"interval": 0.1, "switchMargin": 0.25, "acceptCost": -1, "continuityWindow": 0.25,
             "accel": "auto", "kdMinRows": 40000},
  "trajectory": {"velocityHalfLife": 0.2, "turnHalfLife": 0.15, "historySeconds": 1.0, "faceSpeedMin": 5},
  "inertialization": {"halfLife": 0.1},
  "clips": [
    {"clip": "Anims/walk_fwd.ocanim", "start": 0, "end": -1, "loop": "auto", "bias": 0, "noSearch": false,
     "tags": ["walk"], "ranges": []},
    {"clip": "Anims/stop_from_run.ocanim", "start": 0, "end": -1, "loop": false, "bias": 0,
     "tags": ["stop"], "ranges": [{"start": 0.0, "end": 0.15, "tags": [], "noSearch": true}]}
  ]
}
```

Semantics (normative):
- **`skeleton`** is a content path (id = `fnv1a64(path)` through the existing resolver). Every `bone`/`rootBone` names a bone of it. `rootBone` empty = `rootBoneOf(skeleton)`. The entity's skeleton must be the same asset, or have the same bone count and names; otherwise the matcher refuses to bind (warn once). Cross-skeleton use is the retarget feature's job later (open question 6).
- **Sampling.** Each clip entry plays clip seconds `[start, end]` (`end < 0` = clip duration). Rows are taken at `sampleRate` (default 30 Hz): `t_k = start + k/sampleRate`. `loop`: `true`, `false` or `"auto"` (= the clip's `kOcAnimLoop` flag). A looping range wraps; a non-loop range ends at `end`. `bias` is added to the cost of every row of the entry (a clip the author wants to prefer gets a negative bias). `noSearch` keeps the entry playable (continuation, `@end` flow) but never a candidate.
- **Tags**: `tags` (entry-wide) and `ranges[].tags` (sub-range, union) name tags declared in the top-level `tags` (<= 32). `ranges[].noSearch` removes rows in that sub-range from candidacy (e.g. the first frames of a stop clip). `ranges` times are clip seconds.
- **Features** (each group is omitted when its weights are 0; layout order is fixed): per `features.bones[i]` in file order `pos` (3 floats) then `vel` (3); `rootVelocity` (3: `vx, vy` cm/s, `yawRate` rad/s); `future.pos` (2 per time), `future.dir` (2 per time), `past.pos` (2 per time). `times` are seconds relative to the row (future > 0, past < 0), ascending within their list, at most 8 each. All positions/velocities are in the **root heading frame at the row**: origin the root's planar position, x forward, y right (matching `Character.cs` `WalkForward`/`WalkRight`), z unchanged (model height). Velocities are central differences with step `1/sampleRate` of the *world-space* bone position (the clip's root travel included), rotated by `-yaw`.
- **Trajectory features** for row at `t`: with `R(t)` the root bone's model transform sampled from the clip (planar position `p(t)`, heading `yaw(t) = yawOf(R.rotation)`): `future.pos = rotZ(-yaw(t)) * (p(t+tau) - p(t))` (x, y); `future.dir = (cos, sin)` of `yaw(t+tau) - yaw(t)`; `past.pos` likewise with negative `tau`. Looping ranges wrap. In a non-loop range, **past samples before `start` clamp to `start`** (a start clip reads "was standing still"), and **rows whose future horizon (`max(future.times)`) passes `end` are flagged `Tail`** and are not candidates (they still play).
- **Search defaults** (an entity's `CMotionMatcher` may override `interval`): `interval` s between searches; `switchMargin` (normalised cost units) a candidate must beat the current row by; `acceptCost` (< 0 = off) above which a valid current pose is kept regardless; `continuityWindow` s: a best hit in the same range within this distance of the playhead is "the current motion", not a switch; `accel` `auto|linear|kdtree`; `kdMinRows` threshold for `auto`.
- **Trajectory defaults**: `velocityHalfLife`, `turnHalfLife` (the spring half-lives of section 5.3), `historySeconds` (ring length), `faceSpeedMin` cm/s below which facing follows no velocity direction.
- **Inertialization**: `halfLife` seconds (section 5.4).
- `valid()`: non-empty `skeleton` and >= 1 clip; `sampleRate` in [5, 120]; every `bone` non-empty; `pos`/`vel` weights finite and >= 0 and at least one group enabled; time lists ascending, finite, future > 0, past < 0, <= 8 each; clip `start >= 0`, `end < 0 || end > start`; tags unique, non-empty, <= 32; every tag referenced is declared; `interval > 0`; half-lives > 0; `kdMinRows >= 1024`. Existence of bones/clips is checked at build against real assets, not in `valid()`.

### 4.2 `.ocmbake` (AVR1, subtype `MMDB`, content version 1)

Sidecar next to the asset (`Locomotion.ocmotion` -> `Locomotion.ocmbake`); derived data that is also shipped Content (no `DerivedDataCache` dependency: `Content/` is copied minus `Scripts/`, `*.pdb`, `Source/`, `docs/PACKAGING.md:116`). Deleting it costs one rebuild. `MinReaderVersion` 1. Chunks (all uncompressed, little-endian, base alignment 32 bytes so rows are SSE-aligned):

| Chunk | Contents |
|---|---|
| `MHDR` | `u32 builderVersion` (`kMotionBuildVersion = 1`), `u32 dim`, `u32 dimPadded` (multiple of 8), `u32 rows`, `u32 ranges`, `u32 tags`, `u32 groups`, `u32 flags`, `f32 sampleRate`, `u64 sourceKey`, `f32 minBias`, `u32 kdNodes`, `u32 kdDims`, `u32 reserved[3]` |
| `STRT` | clip paths, tag names, bone names |
| `GRPS` | per group: `u32 kind` (0 BonePos, 1 BoneVel, 2 RootVel, 3 FuturePos, 4 FutureDir, 5 PastPos), `u32 bone` (skeleton index or `0xFFFFFFFF`), `u32 firstDim`, `u32 count`, `f32 weight`, `f32 scale` |
| `NORM` | `f32 mean[dimPadded]` (padding 0) |
| `TRAJ` | `u32 nFuture, nPast`, `f32 futureTimes[], pastTimes[]`; search/spring/inertialization defaults (so the runtime needs no JSON beyond the key check) |
| `RNGE` | per range: `StringRef clip`, `f32 start, end`, `u32 firstRow, rowCount`, `u32 flags` (bit0 loop), `u32 tagMask`, `f32 bias`, `u32 clipBone` (root bone index) |
| `ROWM` | per row, 8 bytes: `u32 tagMask`, `u16 range`, `u8 flags` (bit0 NoSearch, bit1 Tail), `u8 pad` |
| `FEAT` | `f32 rows * dimPadded`, row-major, already `(f - mean) * weight / scale` |
| `PCA8` | optional: `f32 proj[8][dimPadded]` (orthonormal rows) |
| `KDTR` | optional: nodes `{f32 lo[8], hi[8]; u32 left, right, rowBegin, rowEnd; u32 tagOr; u32 flagsAny}` (80 bytes) and `u32 order[rows]` (leaf order) |

`sourceKey = avrHash64` over: `kMotionBuildVersion`, the canonical `writeMotionAsset` text, the skeleton file bytes, then each distinct clip file's bytes in entry order. Load rules: parse failure, builder-version mismatch or key mismatch is a **miss, not an error** (rebuild); sizes are checked against `rows * dimPadded` rather than trusted. The format section for `FORMAT_SPECS.md` (a new 11j) is written at integration by the main loop; the schema above is the source until then.

## 5. `modules/anim` (pure; Core + Formats only)

New files (declared in `modules/anim/CMakeLists.txt`, 6 sources): `MotionAsset`, `PoseDatabase`, `PoseSearch`, `Trajectory`, `Inertializer`, `MotionMatcher` (`.hpp` under `include/aver/anim/`, `.cpp` under `src/`), plus the internal `src/AnimJson.hpp`. No scene, no threads (the worker lives one tier up), no GPU.

### 5.1 `MotionAsset.hpp` / `MotionAsset.cpp`

```cpp
inline constexpr int kMotionAssetVersion = 1;
inline constexpr u32 kMaxMotionTags = 32;
inline constexpr u32 kMaxTrajectorySamples = 8;

struct MotionBoneFeature { std::string bone; f32 pos = 1.0f, vel = 1.0f; };
struct MotionTagRange   { f32 start = 0.0f, end = -1.0f; std::vector<std::string> tags; bool noSearch = false; };
struct MotionClipEntry  {
    std::string clip; f32 start = 0.0f, end = -1.0f;
    i32 loop = -1;                       // -1 auto (clip flag), 0 no, 1 yes
    f32 bias = 0.0f; bool noSearch = false;
    std::vector<std::string> tags; std::vector<MotionTagRange> ranges;
};
enum class SearchAccel : u8 { Auto = 0, Linear = 1, KdTree = 2 };
struct MotionSearchDefaults { f32 interval = 0.1f, switchMargin = 0.25f, acceptCost = -1.0f, continuityWindow = 0.25f;
                              SearchAccel accel = SearchAccel::Auto; u32 kdMinRows = 40000; };
struct MotionSpringDefaults { f32 velocityHalfLife = 0.2f, turnHalfLife = 0.15f, historySeconds = 1.0f, faceSpeedMin = 5.0f; };

struct MotionAsset {
    std::string name, skeleton, rootBone;
    u32 sampleRate = 30;
    std::vector<std::string> tags;
    std::vector<MotionBoneFeature> bones;
    f32 rootVelocity = 1.0f;
    std::vector<f32> futureTimes{0.2f, 0.4f, 0.7f, 1.0f}, pastTimes{-0.2f, -0.5f};
    f32 futurePos = 1.0f, futureDir = 0.8f, pastPos = 0.5f;
    MotionSearchDefaults search; MotionSpringDefaults spring; f32 inertHalfLife = 0.1f;
    std::vector<MotionClipEntry> clips;
    bool valid(std::string* why = nullptr) const;       // rules of 4.1
    i32  tagIndex(std::string_view name) const;         // -1 if absent
};
std::string writeMotionAsset(const MotionAsset&);
bool parseMotionAsset(std::string_view json, MotionAsset& out, std::string* why = nullptr);   // never calls valid()
bool saveMotionAsset(const std::string& path, const MotionAsset&, std::string* why = nullptr);
bool loadMotionAsset(const std::string& path, MotionAsset& out, std::string* why = nullptr);
```

### 5.2 `PoseDatabase.hpp` / `PoseDatabase.cpp` / `PoseSearch.cpp`

```cpp
inline constexpr u32 kMotionBuildVersion = 1;
inline constexpr u32 kNoRow = 0xFFFFFFFFu;
inline constexpr u8  kRowNoSearch = 1, kRowTail = 2;

enum class FeatureKind : u8 { BonePos = 0, BoneVel = 1, RootVel = 2, FuturePos = 3, FutureDir = 4, PastPos = 5 };
struct FeatureGroup { FeatureKind kind; u32 bone = 0xFFFFFFFFu; u32 firstDim = 0, count = 0; f32 weight = 1.0f, scale = 1.0f; };
struct MotionRange { std::string clip; f32 start = 0, end = 0; u32 firstRow = 0, rowCount = 0; bool loop = false; u32 tagMask = 0; f32 bias = 0; u32 rootBone = 0; };
struct RowMeta { u32 tagMask; u16 range; u8 flags; u8 pad; };

struct SearchFilter { u32 require = 0, exclude = 0; };        // row passes when (tags & require) == require && !(tags & exclude)
struct SearchHit    { u32 row = kNoRow; f32 cost = std::numeric_limits<f32>::infinity(); };

class PoseDatabase {                                          // immutable after build/load
public:
    u32 rows() const; u32 dim() const; u32 dimPadded() const;            // dimPadded: multiple of 8
    const f32* row(u32 r) const;                                          // dimPadded floats, 32-byte aligned
    const RowMeta& meta(u32 r) const;
    const std::vector<MotionRange>& ranges() const;
    const std::vector<FeatureGroup>& groups() const;
    const std::vector<f32>& futureTimes() const; const std::vector<f32>& pastTimes() const;
    const MotionAsset& defaults() const;                                  // search / spring / inertialization (the asset minus clips)
    u32  tagBit(u64 nameHash) const;                                      // 0 when unknown; otherwise 1u << index
    u32  tagMaskOf(const u64* hashes, u32 n) const;
    u32  rowOf(u32 range, f32 clipSeconds) const;                         // nearest row at/before the time, clamped to the range
    f32  timeOf(u32 row) const;                                           // clip seconds
    // Query helpers. `out` has dimPadded floats.
    void normalise(const f32* rawFeatureVector, f32* out) const;          // (f - mean) * weight / scale
    void denormalise(const f32* rowOrQuery, f32* out) const;              // inverse, for the debug view
    f32  groupCost(const f32* q, u32 row, u32 group) const;
    // Exact nearest searchable row. Linear (SSE2 + scalar fallback) or branch-and-bound KD per `accel`.
    SearchHit search(const f32* q, const SearchFilter&, SearchAccel accel = SearchAccel::Auto) const;
    bool hasKd() const;
    u64  memoryBytes() const;
};

struct MotionBuildReport { u32 rows = 0, ranges = 0, skippedClips = 0; double seconds = 0; u64 bytes = 0; std::vector<std::string> warnings; };
using MotionClipFn = std::function<const fmt::OcAnimation*(const std::string&)>;

// Builds from an asset, its skeleton and resolved clips. Pure and const over its inputs, so a worker thread may run it.
bool buildPoseDatabase(const MotionAsset&, const fmt::OcSkeleton&, const MotionClipFn&, PoseDatabase& out,
                       MotionBuildReport* report = nullptr, std::string* why = nullptr);
u64  motionSourceKey(const MotionAsset&, const std::vector<u8>& skeletonBytes, const std::vector<std::vector<u8>>& clipBytes);
bool savePoseDatabase(const std::string& path, const PoseDatabase&, u64 sourceKey, std::string* why = nullptr);
bool loadPoseDatabase(const std::string& path, PoseDatabase& out, u64* sourceKey, std::string* why = nullptr);
```

**Build algorithm** (per range, in entry order; tag-range splits and `noSearch` ranges only set row flags/masks, they do not create new ranges):
1. `n = floor((end - start) * sampleRate) + 1` rows for a non-loop range, `n = round((end - start) * sampleRate)` for a loop. For each distinct needed time (rows plus `+-1/sampleRate` for velocities plus each trajectory offset) sample the pose (`restPose` + `sampleAnimation`), `poseToModel`, and keep the *world-space* positions of the selected bones plus the root's planar position and `yawOf` heading. Loop ranges wrap times; non-loop ranges clamp (rule of 4.1). (Full FK per sample; optimising to the selected bones' parent chains is an allowed refinement, not part of the contract.)
2. Fill the raw vector in layout order (4.1). Heading wraps use `wrapPi`.
3. Flags: `Tail` per 4.1; `NoSearch` from the entry or a tag range; `tagMask` = entry tags | covering range tags.
4. After all ranges: `mean[j]` over **all** rows (including NoSearch/Tail, they are still real data). Per group `s_g = sqrt(sum_{j in g} var_j)` (the group's *total* standard deviation, so every group has unit total variance before weighting regardless of its component count); `s_g < 1e-4` -> 1. Stored row = `(f - mean) * weight / s_g`, padding 0.
5. Warnings, never failures: a clip whose root bone track is missing or whose planar travel over the whole range is < 1 cm while `future.pos` is enabled ("root carries no motion: trajectory features are zero"); a requested bone missing from the skeleton (that group is dropped); a clip that does not resolve (entry skipped); `rows == 0` after skipping is a failure.
6. PCA + KD (only when `rows >= kdMinRows` or `accel == KdTree`): covariance of the stored rows over searchable rows, top 8 eigenvectors by orthogonal subspace iteration (fixed 40 iterations, deterministic seed from the identity-leading basis; no external library), projection of every searchable row, median-split KD-tree on the widest dimension, leaf size 16, each node keeping its 8-D AABB, `tagOr` (OR of leaf tag masks) and `flagsAny`.

**Search** (`PoseSearch.cpp`). `cost(row) = sum_j (q_j - r_j)^2 + bias(range(row))`.
- *Linear*: iterate rows; skip `NoSearch`/`Tail` and rows failing the tag filter before touching `FEAT`; SSE2 (`_mm_loadu_ps` x 4 lanes; the scalar loop is compiled instead when `_M_X64`/`__SSE2__` is absent) accumulates 16 floats at a time and abandons the row when the partial sum already exceeds the best (bias included). Ties go to the lower row index.
- *KD*: project `q` (8 x dimPadded mat-vec), best-first descent with box lower bound `|P q - clamp(P q, lo, hi)|^2 + minBias`, pruning a node when the bound >= best, when `(tagOr & require) != require`, or when `flagsAny == 0`; leaves run the full-dimension distance. Because `P` has orthonormal rows `|P(q - r)| <= |q - r|`, so the bound is valid and **the result equals the linear scan** (tested, section 12).
- `Auto` = KD when `hasKd()` and the filter leaves the tree useful (always true here), else linear.
- Budget (informative, to be measured by `PoseSearchTest --bench`, not asserted): 100k rows x 56 floats = 22 MB; linear < 1.5 ms single thread; KD typically < 0.2 ms on locomotion data. Searches are staggered by the system (6.2) so a crowd never searches in one tick.

### 5.3 `Trajectory.hpp` / `Trajectory.cpp`

```cpp
struct MotionQuery {                       // everything the matcher needs from the world, in the heading frame at "now"
    bool valid = false;
    f32 futurePos[kMaxTrajectorySamples][2] = {}, futureDir[kMaxTrajectorySamples][2] = {}, pastPos[kMaxTrajectorySamples][2] = {};
    f32 rootVel[3] = {};                   // vx, vy cm/s (heading frame), yaw rate rad/s
    u32 require = 0, exclude = 0;          // tag masks
};
struct SpringParams { f32 velocityHalfLife = 0.2f, turnHalfLife = 0.15f; };

inline constexpr f32 kSpringHalfLifeK = 1.67835f;   // root of (1 + z) e^{-z} = 0.5: gamma = K / halfLife

class TrajectoryPredictor {
public:
    void reset();
    void observe(const Vec3& worldPos, f32 yaw, f32 dt, f32 historySeconds);   // once per tick; ring of (t, pos, yaw), v / omega estimate
    void setIntent(f32 vx, f32 vy, bool faceYaw, f32 yawTarget, f32 faceSpeedMin);   // desired world-space planar velocity (cm/s), facing
    // Fills futurePos/futureDir at the database's times and pastPos from the measured history.
    void fill(const SpringParams&, const std::vector<f32>& futureTimes, const std::vector<f32>& pastTimes, MotionQuery& out) const;
    // World-space polyline for the debug view.
    void predictedWorld(const SpringParams&, const std::vector<f32>& times, std::vector<Vec3>& pts, std::vector<f32>& yaws) const;
    void historyWorld(std::vector<Vec3>& pts) const;
    Vec3 velocity() const; f32 yawRate() const; Vec3 position() const; f32 yaw() const;
};
f32 springVelocity(f32 v0, f32 vd, f32 halfLife, f32 t);                    // velocity at t, initial acceleration 0
f32 springDisplacement(f32 v0, f32 vd, f32 halfLife, f32 t);                // integral of the above from 0 to t
f32 springAngle(f32 a0, f32 w0, f32 aTarget, f32 halfLife, f32 t);          // shortest-arc target, initial angular velocity w0
```

**Math** (derived here; no code taken from anywhere). `gamma = K / halfLife`. Velocity toward `vd` from `v0` (initial acceleration 0): `v(t) = vd + A (1 + gamma t) e^{-gamma t}`, `A = v0 - vd`. Position: `p(t) = p0 + vd t + A (2 - (2 + gamma t) e^{-gamma t}) / gamma`. Heading toward `psi_d` from `psi_0` with measured `omega_0`: `psi(t) = psi_d + (A + B t) e^{-gamma_psi t}`, `A = wrapPi(psi_0 - psi_d)`, `B = omega_0 + gamma_psi A`. `K` makes `(1 + gamma t) e^{-gamma t}` equal 0.5 at `t = halfLife` exactly, so the half-life parameter means what it says (a test pins it). 2-D velocity is the same scalar formula per axis in world space; the results are rotated into the heading frame at "now" (`rotZ(-yaw0)`) for `futurePos`; `futureDir = (cos, sin)(psi(tau) - psi_0)`.

**Measured state** (`observe`): ring buffer of (time, world position, yaw) over `historySeconds`; `velocity` = smoothed finite difference (one-pole, half-life 0.05 s), `yawRate` likewise from unwrapped yaw differences. **Past samples** are linearly interpolated from the ring (yaw by shortest arc) and expressed `rotZ(-yaw_now) * (p(tau) - p_now)`; before the ring's oldest entry the oldest value holds. **Facing target**: `faceYaw` -> `yawTarget`; otherwise the direction of `vd` when `|vd| > faceSpeedMin`, else the current heading (no turn). The planar position/yaw are the entity's (`CLocal`, section 6.2).

### 5.4 `Inertializer.hpp` / `Inertializer.cpp`

```cpp
class Inertializer {
public:
    void setHalfLife(f32 seconds);                         // default 0.1
    // Start a transition. from/fromPrev: the last two OUTPUT poses (dtPrev apart). to/toPrev: the NEW source sampled at the
    // switch time and dtPrev earlier. Replaces any running offsets (their current value is already in `from`).
    void begin(const Pose& from, const Pose& fromPrev, const Pose& to, const Pose& toPrev, f32 dtPrev, u32 skipBone);
    void advance(f32 dt);                                  // decays every offset
    void apply(Pose& p) const;                             // adds the offsets to a pose of the new source
    bool active() const;
    void reset();
};
```
Per bone and component the state is `(x, v)`: translation offset (3), rotation offset as a rotation vector (3, from `log(q_from * conj(q_to))`, shortest arc), scale offset (3). Initial `x = from - to`; initial `v` = (velocity of `from`) - (velocity of `to`), each by finite difference over `dtPrev`. Decay is the critically damped solution `x(t) = (a + b t) e^{-gamma t}`, `a = x0`, `b = v0 + gamma x0`, advanced exactly over `dt`: `x' = (a + b dt) e`, `v' = (b - gamma (a + b dt)) e`, `e = e^{-gamma dt}` (`gamma` from `K / halfLife`). **No-overshoot rule**: if `x0 * b < 0` (the spring would cross zero), set `v0 = -gamma x0` so `b = 0` and the decay is monotone. Apply: translation/scale add, rotation `exp(r) * q_to`. The state goes inactive when every `|x|` < 1e-4 and `|v|` < 1e-3. `skipBone` (the locked root) is not offset: the matcher locks the planar root after `apply`.

### 5.5 `MotionMatcher.hpp` / `MotionMatcher.cpp`

Banner (binding): the five rules of section 2.

```cpp
struct MotionParams {                      // per tick, from the component / state; <= 0 means "database default"
    f32 searchInterval = 0.0f, playRate = 1.0f, inertHalfLife = 0.0f;
    bool forceSearch = false, paused = false;
};
struct MotionInfo { u32 range = kNoRow, row = kNoRow; f32 clipTime = 0, cost = 0, currentCost = 0, sinceSwitch = 0;
                    u32 switches = 0; bool searched = false, switched = false; };
struct MotionCandidate { u32 row; f32 cost; };

class MotionMatcher {
public:
    // Resolves each range's clip. False (and unbound) when the skeleton's bone count/names disagree with the database.
    bool bind(const PoseDatabase* db, const fmt::OcSkeleton* skel, MotionClipFn clips);
    bool bound() const;
    void unbind();
    void setRootMotion(bool on, u32 axes);                 // report root deltas (axes as RootSpec::axes); the lock is always on
    void tick(f32 dt, const MotionQuery& q, const MotionParams& p);   // advance clock, maybe search + switch, update pose, root delta
    const Pose& pose() const;                              // locked-root, inertialized
    void evaluate(const fmt::OcSkeleton& skel, Pose& out) const;     // copies pose(), restPose for an unbound matcher
    RootDelta consumeRootDelta();
    const MotionInfo& info() const;
    // Debug: the cost of every group against the last query for the current row and for the best hit, and the best few hits.
    const std::vector<MotionCandidate>& lastCandidates() const;
    void lastGroupCosts(std::vector<f32>& current, std::vector<f32>& best) const;
    u64  clipHash() const;                                 // fnv1a64 of the playing range's clip path (0 when none)
};
```
`tick` (order is normative): (1) unbound -> no-op. (2) `inertializer.advance(dt)`; unless `paused`, advance `clipTime += dt * playRate`: loop ranges wrap inside `[start, end)`, a non-loop range reaching `end` holds there and marks the current row invalid (forces a search). (3) Root delta for the travelled interval with `rootDeltaUnflagged` (5.6) when `setRootMotion(true, ...)`, wrap handled for loops, scaled by nothing (the system scales). (4) Decide a search: `forceSearch`, no valid current row, or `sinceSearch >= interval`. (5) If searching and `q.valid`: build the raw query vector: pose groups (BonePos/BoneVel) copied from the **current row's stored vector** (when no valid row they are 0 in normalised space, the database mean); `RootVel` from `q.rootVel`; trajectory groups from `q`; `normalise`; `db.search(q~, {q.require, q.exclude})`. (6) Switch rule: no valid current -> take the hit; else the current row's cost is `cost(currentRow)` with its own bias; a hit in the **same range within `continuityWindow` of the playhead** is the same motion (no switch, the playhead is not moved); otherwise switch only if `hit.cost + switchMargin < currentCost` and (`acceptCost < 0` or `hit.cost <= acceptCost`). A hit with no valid current pose is always taken. (7) On a switch: sample the new range at the hit's clip time, `Inertializer::begin(pose_, prevPose_, sample(new, t), sample(new, t - dtPrev), dtPrev, rootBone)` (first bind: no inertialization, a hard start), `switches++`, `sinceSwitch = 0`. (8) Pose: `restPose` + `sampleAnimation(clip, clipTime)`; `inertializer.apply`; `lockRoot` with a spec of the database root bone (`axes` = X|Y|Yaw, plus Z if the root-motion axes include it) so the mesh stays on the entity; keep `prevPose_`. (9) Fill `MotionInfo`.

Staggering is the system's job; the matcher is deterministic given (state, query, params).

### 5.6 Changes to existing anim files

- **`RootMotion.hpp/.cpp`**: add `RootDelta rootDeltaUnflagged(const fmt::OcAnimation& c, const RootSpec& s, f32 t0, f32 t1, bool wrap);` (the body of `rootDeltaBetween` without the `clipHasRootMotion` flag/additive/object gate, but still requiring a track on `s.bone`); `rootDeltaBetween` becomes `clipHasRootMotion(c, s) ? rootDeltaUnflagged(...) : RootDelta{}`. No behaviour change for wave-2 callers.
- **`AnimStateMachine.hpp/.cpp`**: `AsmStateKind::MotionMatch = 3`; `AsmState` gains `std::vector<std::string> tagsRequired, tagsExcluded;`. `AnimStateMachine` gains
  ```cpp
  using AsmMotionFn = std::function<const PoseDatabase*(const std::string& assetRef)>;
  void setMotionResolver(AsmMotionFn fn);                         // before the first tick
  void setMotionContext(const fmt::OcSkeleton* skel, const MotionQuery* query);   // per tick; null query = MM layers idle
  ```
  `Layer` gains `std::unique_ptr<MotionMatcher> mm; bool isMotionMatch`. `pushLayer` (cpp:144) creates the matcher for a `MotionMatch` state (resolver may return null while the database loads: the layer holds rest and `tick` retries the resolver each tick until it binds). `tick` (cpp:287): an MM layer calls `mm->tick(step, query + state tag masks, params{playRate = state.speed * speedParam})` and, when `hasRoot_`, `deltas[li] = mm->consumeRootDelta()` (consumed regardless so nothing accumulates). `evaluate` (cpp:368): `mm->evaluate(skel, tmp)`. `normalizedTime()` of an MM leaf reads 0 and `exitTimeReached` is never true for it (`valid()` refuses `hasExitTime` on a transition leaving an MM state: "motion-matching states do not end"). Crossfades into/out of the layer are the existing `layerWeights()` mix, with the MM layer's inertialization only for its internal switches.
- **`AnimGraphAsset.hpp/.cpp`**: `kStateMachineVersion = 3`; `kindName`/parse (`:59`, `:308`) gain `"motionmatch"`; write emits `tagsRequired`/`tagsExcluded` arrays only for that kind; written version = 3 only when a state of that kind exists, else 2 with a pipeline, else 1 (all existing files stay byte-identical: pinned by `AnimGraphAssetTest`). `AnimStateMachineAsset::valid` (cpp:23): an MM state needs a non-empty `asset`; tag lists <= 32 and non-empty names. A new `bool AnimStateMachineAsset::usesMotionMatch() const`.

## 6. `modules/anim.scene`

### 6.1 `CMotionMatcher` (runtime-registered, like `CAnimGraph`; no `Components.hpp`/scene-ABI change)

In `MotionMatchSystem.hpp`:
```cpp
inline constexpr u32 kMotionTagSlots = 4;
inline constexpr u32 kMotionPaused = 0x1, kMotionForceSearch = 0x2, kMotionFaceYaw = 0x4;
struct CMotionMatcher {
    u64 database = 0;          // .ocmotion ObjectId; 0 = none (a state-machine motionmatch state names its own)
    u32 flags = 0;             // kMotionPaused; kMotionForceSearch (script sets, system clears); kMotionFaceYaw (else face the velocity)
    f32 playRate = 0.0f;       // 0 reads as 1
    f32 searchInterval = 0.0f; // <= 0 = database default
    f32 desiredVelX = 0.0f, desiredVelY = 0.0f;   // world-space planar velocity the character is trying to reach, cm/s
    f32 desiredYaw = 0.0f;     // radians about +Z, world; used with kMotionFaceYaw
    f32 velHalfLife = 0.0f, turnHalfLife = 0.0f;  // <= 0 = database default
    u64 requireTag[kMotionTagSlots] = {}, excludeTag[kMotionTagSlots] = {};   // fnv1a64(tag name); 0 = unused
    // OUT (written every tick by the system)
    u64 clip = 0;              // fnv1a64 of the playing clip path
    f32 clipTime = 0.0f, cost = 0.0f;
    u32 switches = 0, row = 0;
    f32 sinceSwitch = 0.0f;
    u32 reserved = 0;
};
static_assert(sizeof(CMotionMatcher) == 136, "CMotionMatcher must be padding-free");
```
Zero-filled storage is a valid, enabled component (rate 0 reads 1, face-the-velocity, database defaults). Registered fields: `database` (I64), `flags` (I32), `playRate, searchInterval, desiredVelX, desiredVelY, desiredYaw, velHalfLife, turnHalfLife` (F32), `requireTag0..3`, `excludeTag0..3` (I64), and read-only `clip` (I64), `clipTime, cost, sinceSwitch` (F32), `switches, row, reserved` (I32). Names live in static arrays like `CAnimGraph`'s (`AnimGraphSystem.cpp:26-33`). `static CMotionMatcher* attach(world, e, u64 database)` assigns a fresh value over the zero-filled bytes.

### 6.2 `MotionMatchSystem` (`MotionMatchSystem.hpp` / `MotionMatchSystem.cpp`)

```cpp
class MotionMatchSystem {
public:
    static u32 registerComponents(scene::World&);
    static CMotionMatcher* attach(scene::World&, scene::Entity, u64 databaseAsset);
    void install(AnimSystem&, scene::World&);          // chains AnimSystem::poseSource() (the graph) as `prev_`, then installs itself
    static void uninstall(AnimSystem&);
    void tick(scene::World&, f32 dt);                  // section 3
    const MotionQuery* query(scene::Entity) const;     // null until the entity has a runtime this tick
    const PoseDatabase* database(u64 assetId);         // null while loading or failed; starts the load
    u32  pendingBuilds() const;                        // loading/baking in the background (a loading screen may wait on it)
    u32  buildCount() const;                           // full builds since clear() (tests)
    void setMaxSearchesPerTick(u32 n);                 // default 4
    void setBakeSidecars(bool on);                     // default true; a write failure is logged once at info and ignored
    struct Debug { Vec3 origin; f32 yaw = 0; std::vector<Vec3> history, predicted, matched; std::vector<f32> predictedYaw;
                   Vec3 desiredVel; std::string clip; f32 clipTime = 0, cost = 0, currentCost = 0; u32 switches = 0;
                   std::vector<std::string> groupNames; std::vector<f32> groupCurrent, groupBest; };
    bool debug(scene::Entity, Debug& out) const;       // world-space points for MotionDebugOverlay
    void clear();                                      // joins any worker first, then drops everything
};
MotionMatchSystem& motionMatchSystem();
```
**Per entity** (`Runtime { TrajectoryPredictor traj; MotionQuery q; MotionMatcher mm; u64 dbId; u32 stamp; u32 lastSearchTick; }`, keyed by the full `Entity`, pruned by stamp like `AnimGraphSystem::instances_`, `AnimGraphSystem.cpp:194-197`):
1. Read `CMotionMatcher`; `traj.observe(world position, yaw of CLocal, dt, historySeconds)` (the entity's local transform; a parented character moves in its parent's frame: documented limit); `traj.setIntent(desiredVel, flags & kMotionFaceYaw, desiredYaw, faceSpeedMin)`; `traj.fill(spring, db times, q)`; `q.rootVel` = measured velocity in the heading frame and yaw rate; `q.require/exclude` from the slot hashes through `db.tagMaskOf` (unknown hashes are ignored) OR-ed with any state-level masks (added in the graph path).
2. Standalone matcher (`CMotionMatcher.database != 0` and the entity has no `CAnimGraph` instance): ensure `CAnimator` (as `AnimGraphSystem.cpp:135`), bind lazily when the database is ready and the entity's skeleton resolves, `mm.setRootMotion(anim.rootSpec(world, e, kRootSrcGraph, spec), spec.axes)`, `mm.tick(dt, q, params)` unless paused, then `anim.addRootMotion(e, mm.consumeRootDelta())` and write the outputs. A cleared `kMotionForceSearch` after one tick.
3. **Stagger**: a tick may start at most `maxSearchesPerTick` searches; entities beyond the budget search next tick (ordered by oldest `lastSearchTick`), so a crowd spreads over the interval. Forced searches and `invalid current` searches bypass the budget.
4. The source thunk: `if (prev_ && prev_(e, skel, pose, prevUser_)) return true;` then, for an entity with a bound standalone matcher, `mm.evaluate(skel, pose)` and `return true`. A database that is still loading returns false (the CAnimator clip/rest pose shows).

**Database loading** (`database(id)`): main thread: `anim.assetPath(id)`, `loadMotionAsset`, `valid()`, resolve the skeleton and **every clip** through `AnimSystem::skeleton()/clip()` (their caches are not thread-safe, which is why clip loading stays on the main thread). Then a worker (`std::async(std::launch::async)`) reads the sidecar and compares `sourceKey` (hashing the raw skeleton/clip file bytes), loading `loadPoseDatabase` on a match, else `buildPoseDatabase`, then writing the sidecar when `bakeSidecars`. `tick` polls the future with `wait_for(0)`; a result is installed on the main thread. A failed load is cached as null (one warning), like `AnimGraphSystem::machines_`.

### 6.3 Changes to `AnimSystem` / `AnimGraphSystem` / wiring

- **`AnimSystem.hpp/.cpp`** (two small additions, nothing else): `PoseSourceFn poseSource() const; void* poseSourceUser() const;` (beside `poseModifier()`, `AnimSystem.hpp:117`) so a second source can chain; and `u32 contentEpoch() const` incremented in `clear()` only (`AnimSystem.cpp:173`). `MotionMatchSystem::tick` compares the epoch and drops every runtime and loaded database when it changed (clip pointers into `clips_` are dead).
- **`AnimGraphSystem.hpp/.cpp`**: in `tick` (cpp:121), when `loaded->asset.usesMotionMatch()`: ensure the entity has a `CMotionMatcher` (`attach` with database 0) so scripts can drive it; after `bind`, `inst.sm->setMotionResolver([](ref){ return motionMatchSystem().database(fnv1a64(ref)); })`; before `sm.tick`, `sm.setMotionContext(skel, motionMatchSystem().query(e))` (skeleton from `anim_->skeleton(CSkeletalMesh.skeleton)`, the same lookup as the pipeline bind at cpp:152-161). `sourceThunk` is unchanged (`sm.evaluate` now covers MM layers). The graph path never also runs the standalone matcher for the entity (decision 6).
- **`GameSystemsWiring.hpp`** (shared, append-only): `registerGameSystems()` (:118-126) adds `anim::MotionMatchSystem::registerComponents(world); anim::motionMatchSystem().install(anim::animSystem(), world);` **after** the `animGraphSystem().install` line (install order = chain order); `tickAnimGraphs` (:162) calls `anim::motionMatchSystem().tick(world, dt)` first; `resetGameSystems()` (:172) adds `anim::motionMatchSystem().clear()`.

### 6.4 Root motion interplay (all wave 2, restated as contract)

- The pose is root-locked **always** (X, Y, yaw; Z when the entity's `CRootMotion.axes` has Z). With no `CRootMotion` (or mode None/Ignore) the entity is moved by gameplay (**simulation-driven**: the physics character, a script, a vehicle) and the matcher only picks poses that fit the motion it observes.
- `CRootMotion` mode **Apply** (or Accumulate) makes it **animation-driven**: `mm` reports `rootDeltaUnflagged` deltas through `addRootMotion`, which `AnimSystem::tick` composes with montage deltas and writes to `CLocal` (`AnimSystem.cpp:472-513`) or leaves for a physics character (`kRootMotionExternal`, `Character.cs ApplyRootMotion`, one frame of latency as accepted in wave 2). The convenience `Entity.SetMotionMatching(..., drive: MotionDrive.Animation)` sets Apply for you.
- The trajectory fed to the matcher is always the **measured** entity motion plus the desired-velocity spring, so both drive modes use the same code.
- A montage over a matched character composes unchanged: DefaultSlot montages are blended over the sourced pose (`AnimSystem.cpp:451-470`) and their root delta overrides by coverage; the matcher keeps ticking underneath (searches are cheap) and corrects within one interval after the blend-out.

## 7. C ABI

**None.** The whole surface is `CMotionMatcher` fields (6.1) over the generic scene field ABI plus the existing component add/has calls, so no header, version constant or export is added and `framework_abi.h`, `framework_anim_abi.h`, `scene_abi.h` and `Components.hpp` are untouched. Rationale: nothing here needs a call that cannot be a field write (inputs are numbers and hashes; outputs are numbers), and the field route is what keeps this feature from colliding with the wave-5 agents' ABI edits. The field table of 6.1 is the binary contract; `static_assert(sizeof(CMotionMatcher) == 136)` and `b.verify(sizeof)` guard it. Everything that would have been a function (force a search, set tags) is a flag or slot write (`kMotionForceSearch`, `requireTag*`).

## 8. C# API (`scripting/csharp/Aver.Framework/MotionMatching.cs`, new; no edits to `Native.cs`, `Entity.cs`, `AnimGraph.cs`)

```csharp
public enum MotionFacing { Velocity = 0, Yaw = 1 }
public enum MotionDrive  { Simulation = 0, Animation = 1 }   // Animation = CRootMotion Apply on top

public readonly partial struct Entity
{
    // Adds CMotionMatcher (+ CAnimator) if absent. database "" = leave to a state-machine motionmatch state.
    // Animation also sets RootMotion Apply (planar), Simulation leaves root motion alone.
    public bool SetMotionMatching(string database, MotionFacing facing = MotionFacing.Velocity,
                                  MotionDrive drive = MotionDrive.Simulation, float playRate = 1f);
    // World-space desired velocity (cm/s) and, for MotionFacing.Yaw, the facing in degrees (the AverCharacter.Yaw convention).
    public bool SetMotionInput(Vec3 worldVelocity, float facingYawDegrees = 0f);
    public bool SetMotionInput(float velocityX, float velocityY, float facingYawDegrees = 0f);
    public bool SetMotionTags(string[]? required, string[]? excluded);        // <= 4 each; null clears; false when over the slot count
    public bool RequestMotionSearch();                                         // sets kMotionForceSearch
    public bool SetMotionTuning(float searchInterval = 0f, float velocityHalfLife = 0f, float turnHalfLife = 0f);   // 0 = database default
    public bool PauseMotionMatching(bool paused);
    public bool TryGetMotionState(out string? clipHashHex, out float clipTime, out float cost, out int switches);   // hex of the clip id
    public bool IsMotionClip(string clipPath);                                 // compares CMotionMatcher.clip with Assets.ObjectIdOf
    public float MotionCost { get; }
    public int MotionSwitches { get; }
}
internal static class MotionMatchGraphInterop    // int-entity / primitive signatures for GraphCompiler (AnimGraphInterop pattern, AnimGraph.cs:98-118)
{
    internal static bool SetMotionMatchingForGraph(int entity, string database, int facing, int drive, float playRate);
    internal static bool SetMotionInputForGraph(int entity, float velocityX, float velocityY, float facingYawDegrees);
    internal static bool SetMotionTagsForGraph(int entity, string required, string excluded);   // comma-separated names; empty = none
    internal static bool RequestMotionSearchForGraph(int entity);
    internal static bool GetMotionStateForGraph(int entity, out float clipTime, out float cost, out int switches);
    internal static bool IsMotionClipForGraph(int entity, string clipPath);
}
```
Radians in the component, degrees in C# (as `CRootMotion` yaw and `AverCharacter.Yaw` already differ). Writes are ordered like `SetAnimParam` (`AnimGraph.cs:44-49`): values first, the enabling field (`database`) last, so the system never sees a half-written configuration.

**`Character.cs` (edit)**: `public float MotionHalfLife = 0f;` (body acceleration half-life; 0 keeps today's instantaneous `Velocity` write, so existing games are unchanged). In `Drive`, after `wish` is computed (:237-238) and before the capsule velocity is set (:248-250): if the entity has `CMotionMatcher` (checked once per second, not per tick), write `SetMotionInput(wish (the raw target), _yaw)`; when `MotionHalfLife > 0` replace the capsule's horizontal velocity by a critically damped approach to `wish` with that half-life (the same `K = 1.67835` form as the matcher's, kept in C#), so the body and the predicted trajectory agree and feet do not slide. `AverCharacter` defaults to `MotionFacing.Yaw` (its body always faces the mouse-look yaw); velocity-facing needs a script that turns the entity (open question 7). Root-motion `ApplyRootMotion` (:261-296) already handles `drive: Animation`.

## 9. Runtime and packaged-game wiring, save/load

- **Hosts: no edit.** The system installs in `registerGameSystems()` (both hosts call it) and ticks inside `tickAnimGraphs` (6.3).
- **Packaged**: `.ocmotion` and `.ocmbake` are ordinary Content (ids are `fnv1a64(relative path)`; `Runtime/src/GameContent.cpp:78-82` indexes everything). The shipped sidecar normally hits; a stale/missing one rebuilds on a worker and is written best-effort (a read-only install ignores the failure). Hashing the clip bytes at load is the price of never trusting a stale bake; those clips are loaded for playback anyway.
- **Save/load**: `CMotionMatcher` fields (inputs and outputs) are saved by the generic component capture like `CAnimGraph`. Matcher runtime state (current range/time, offsets, history) is transient: after a load the system re-searches on the first tick (a one-pop blend at worst, no stale pointers). Montages are likewise transient (wave 2).
- **Editor outside Play**: nothing ticks matchers unless an entity has the component; `AnimSystem::tick` already runs unconditionally for previews, so a placed matcher entity poses in the editor viewport from its input fields (default zero velocity: idle).
- **Threads**: one worker per loading database; `clear()` joins before dropping anything. No other thread touches `AnimSystem` caches.
- D3D12/Vulkan parity: n/a, CPU pose math ending in `poseToSkinning`.

## 10. Editor

Pattern: `MontageEditor.hpp/.cpp` (free functions + class + factory, `AVER_WITH_IMGUI` draw guard) and `SnapshotUndo`.

1. **`MotionDbEditor.hpp/.cpp` (new, `.ocmotion` tab)**: `editor::makeMotionDbEditor(path)`, registered with one line next to `SandboxApp.cpp:445` and the file added to `sandbox/CMakeLists.txt`. Free functions for headless tests: `motionStarter()`, `mdbAddClip/mdbRemoveClip/mdbMoveClip`, `mdbAddTag/mdbRemoveTag/mdbRenameTag` (rename repairs every entry/range; remove strips it), `mdbAddRange/mdbRemoveRange/mdbMoveRange`, `mdbAddBone/mdbRemoveBone`, `mdbSetTrajectoryTimes` (sorted, clamped, <= 8), `mdbClipRefFromPath/mdbResolveClipPath` (the montage editor's rules), `mdbBuildPreview(asset, resolver) -> MotionBuildReport + PoseDatabase`. UI: top row skeleton / sample rate / default search+spring+inertialization; a features panel (bones list with pos/vel weights, root velocity, future/past times and weights); a clips list (drop a `.ocanim` from the Content Browser to append) with per-entry loop/bias/noSearch/tags and a tag-range lane drawn on the clip's duration (drag handles, 1/30 s snap, like the montage notify lane); a **Bake** button writing the sidecar and showing the `MotionBuildReport` (rows, ranges, MB, build ms, warnings such as "root carries no motion"); and a **Query lab**: sliders for desired speed/heading, tag checkboxes, a `TrajectoryPredictor` with zero history producing a `MotionQuery`, a Search button listing the top 8 rows (`clip @ time`, cost, per-group cost bars) with linear-vs-KD timing. No skinned preview in v1 (the Anim tab previews single clips; PIE shows the result).
2. **`AnimStateMachineEditor`** (edit): the state-kind combo gains "Motion matching"; for such a state the details panel shows the `.ocmotion` asset picker, speed, tags required/excluded (comma list with a combo of the database's declared tags when it loads), and the `onEnter/onExit` fields as for other kinds; the graph view tints MM states. Free functions `asmSetStateMotionMatch(asset, machine, state, assetRef)`; `asmProblems` gains "MM state has an exit-time transition".
3. **`MotionDebugOverlay.hpp/.cpp` (new)**: `MotionDebugOptions {trajectory, matched, labels, costs}`; `motionDebugCollect(opts)` walks entities with `CMotionMatcher` (`MotionMatchSystem::debug`), emits `rhi::LineVertex`s: history polyline (grey), predicted trajectory (green polyline, a short heading tick at each database time), matched row's trajectory re-expressed in world (cyan) so predicted-vs-matched shows at a glance, desired-velocity arrow (yellow); labels (clip@time, cost/current cost, switches, top two group costs) via an ImGui foreground draw like `aiDebugDrawLabels`. Hooks (append-only): a `Show > Motion Matching` submenu (`SandboxShell.cpp` beside `:2150`), the member and include in `SandboxApp.hpp` (beside `aiDebug_`, `:3120`), and a collect/draw block in `SandboxRender.cpp` after the AI block (`:1961-1972`, `:2008`), under `!noEditorChrome_`.
4. **Content Browser** (`SandboxContentBrowser.cpp`, `SandboxApp.hpp` decl): `cbCreateMotionDb()` (copy of `cbCreateMontage`, `saveMotionAsset(path, motionStarter())`), a menu entry, and a type label `{".ocmotion", {ICON_..., "Motion Matching Database"}}`; `.ocmbake` shows as "Motion Matching Bake" and is hidden behind its asset in the list by the existing sidecar rule if one exists (otherwise listed; no new rule invented).

## 11. Graph nodes (for the graph-node agent)

Category `"Animation"`, in `GraphNodeDefsGameSystems.hpp` + `GraphGameSystemNodeTable.cs`, pattern of rows `playmontage`/`setrootmotion` (`GraphGameSystemNodeTable.cs:109,118`). Exec nodes have pins `exec`, `entity:int`, outputs `then`, `success:bool`; strings are NODE-line attributes (`attr`). Wrapped methods are in `MotionMatchGraphInterop` (section 8).

| Node | Pins (besides exec/entity/then/success) | Attrs | Wraps |
|---|---|---|---|
| `SetMotionMatching` | in `facing:int=0` (0 velocity, 1 yaw), `drive:int=0` (0 simulation, 1 animation), `playRate:float=1` | `database` (empty = from a state-machine state) | `SetMotionMatchingForGraph` |
| `SetMotionInput` | in `velocityX:float`, `velocityY:float`, `facingYaw:float=0` (degrees) | | `SetMotionInputForGraph` |
| `SetMotionTags` | | `required`, `excluded` (comma-separated tag names, empty = none) | `SetMotionTagsForGraph` |
| `RequestMotionSearch` | | | `RequestMotionSearchForGraph` |
| `GetMotionState` (pure) | `entity` -> out `found:bool`, `clipTime:float`, `cost:float`, `switches:int` | | `GetMotionStateForGraph` |
| `IsMotionClip` (pure) | `entity` -> out `result:bool` | `clip` | `IsMotionClipForGraph` |

A motion-matching *state* needs no node: it is a state in the `.ocasm`, driven by the existing `SetAnimParamFloat`/`TriggerAnim` nodes for transitions and by `SetMotionInput` for the trajectory.

## 12. Tests (all auto-registered: any `*Test` executable under `tests/` is picked up by the walk at the bottom of the root `CMakeLists.txt`; add only the `add_executable`)

**`tests/anim` (link `Aver.Anim Aver.Formats Aver.Core Aver.Platform`)**
- `MotionAssetTest`: `.ocmotion` write -> parse -> write byte-identical; every `valid()` refusal of 4.1; a newer version refused; unknown keys ignored; `.ocasm` with a `motionmatch` state writes version 3 and round-trips, one without still writes 1 or 2 byte-identically to the existing golden (reuse the `AnimGraphAssetTest` fixture); `valid()` refuses an exit-time transition out of an MM state.
- `PoseDatabaseTest` (synthetic skeleton and in-memory clips: idle, a straight walk at 150 cm/s, a circle, a loop, a short non-loop): row counts for loop and non-loop; feature values of the straight walk hand-computed (future pos at 0.2 s = (30, 0), dir (1, 0), root velocity (150, 0, 0), foot velocity from a known swing); a yawed root puts motion on the heading axis not world +X; loop wrap across the seam; head clamp for past samples; `Tail` flagging and that tail rows are never returned by `search`; tag masks from entry and range; `noSearch`; normalisation (each group's total variance 1 before weight, constant group gets scale 1, weight 0 drops the group); sidecar save -> load gives identical `FEAT`, ranges and tags; `sourceKey` changes when a clip byte, the asset or the skeleton changes; truncated/corrupt/old-builder sidecars are misses not crashes; missing bone/clip produce warnings, zero rows is a failure.
- `PoseSearchTest`: the SSE2 path equals the scalar path (relative 1e-5) and the brute-force reference on 5,000 random queries over 20,000 random rows; the KD-tree result equals the linear result row for row (including bias, `require`, `exclude`, `NoSearch`, `Tail`) on 2,000 queries, forced `KdTree` on a database below `kdMinRows`; ties pick the lower row; empty filter result gives `kNoRow`; `--bench` prints linear and KD microseconds for 100k rows (not asserted).
- `TrajectoryTest`: the half-life constant (`(1+z)e^{-z}` = 0.5 at `t = halfLife` for velocity from rest); `springDisplacement` equals RK4 integration of `springVelocity` within 1e-3 cm; velocity never overshoots `vd` (a0 = 0); heading takes the shortest arc across +-pi and honours measured `omega_0`; zero intent and zero motion give an all-zero trajectory; `fill` rotates into the heading frame for a yawed entity; ring interpolation for past samples, holding the oldest value before it; facing: velocity mode below `faceSpeedMin` holds heading, yaw mode follows the target.
- `InertializerTest`: offset at `begin` equals `from - to` (translation, rotation vector, scale) and output equals `from` at t = 0; output velocity continuity (finite difference across the switch equals the source's); with v0 = 0 the offset halves at exactly `halfLife`; the no-overshoot rule (b < 0 case) is monotone; rotation takes the shortest arc (q and -q); inactive after settling and `apply` is then the identity; `skipBone` is untouched; `reset`.
- `MotionMatcherTest` (synthetic database of idle / walk / run with root travel, unflagged clips): picks idle, walk, run for desired velocity 0 / 150 / 400 cm/s once the interval elapses; `searchInterval` is honoured (count searches over simulated ticks); `switchMargin` and `continuityWindow` stop thrash on two near-equal clips (switch count stays 0); `acceptCost` keeps a valid pose; `forceSearch`; end of a non-loop range forces a search and a switch; `require`/`exclude` tags constrain the pick; loop wrap keeps the clock in range; the root delta over a second equals the clip's root travel even though the clip lacks `kOcAnimRootMotion`, and `playRate` scales it; the output pose's planar root equals the skeleton rest (lock) while other bones follow the clip; the pose is continuous across a switch (max per-bone delta well below a hard cut's) and converges to the clip pose; unbound/paused are safe; a skeleton mismatch refuses to bind; **a regression guard for patent rule 4**: the pose-part of the query is identical whether or not the output pose was modified after `evaluate` (the matcher never reads it back).

**`tests/anim.scene` (add to `tests/anim.scene/CMakeLists.txt`, same link set as `AnimGraphSystemTest`)**
- `MotionMatchSystemTest`: component registration, size 136, zero-filled defaults, `attach`; database written to a temp content tree with a resolver: the entity shows rest/clip while `Loading`, then the matcher's pose (skinning differs from rest), outputs (`clip`, `row`, `cost`) update; desired-velocity input changes the chosen clip; history is measured from a script-moved entity (200 cm/s) and the predicted trajectory matches the closed form; sidecar written on first load, a second system instance loads it with `buildCount() == 0`, touching a clip makes it rebuild; `AnimSystem::clear()` during a load joins and drops (no dangling clip pointers, checked under the sanitizer build if available); entity destroyed mid-load; stagger: 10 entities with `maxSearchesPerTick = 2` never exceed 2 searches in a tick and every entity searches within `interval + 2 ticks`; precedence: an entity with an active `CAnimGraph` instance is not matched by the standalone path; a state machine with `Idle(clip) <-> Move(motionmatch)` transitions by parameter, the layer crossfades and then the matcher drives the pose, tags OR with the component's; **root motion**: `drive: Simulation` keeps the entity put and the pose root locked, `Apply` moves the entity along its heading by the clip's travel, `Accumulate` grows the accumulator, the External flag leaves `CLocal` alone; a DefaultSlot montage over a matched entity overrides the pose while the matcher keeps ticking (reuse the montage fixtures); the control rig and ragdoll modifiers still run after the matcher (existing `ControlRigTest`/ragdoll tests unchanged and green); `debug()` returns consistent world points.
- Extend `AnimGraphSystemTest` only for the `usesMotionMatch` auto-attach of `CMotionMatcher`.

**`tests/editor`**: `MotionDbEditorTest` (new, compiles `sandbox/src/MotionDbEditor.cpp` with `AVER_WITH_IMGUI` undefined, like `MontageEditorTest`): the `mdb*` edits and rename/remove repairs, time-list sorting and caps, load/save/dirty/undo bookkeeping, `mdbBuildPreview` on synthetic clip files in a temp tree (report numbers, warnings), a corrupt file reports `loadError`. Extend `AnimGraphEditorTest.cpp` for `asmSetStateMotionMatch` and the MM exit-time problem.

**C# (`scripting/csharp/Aver.Graph.Tests`, for the graph-node agent)**: `MotionMatchNodeTests.cs` in the `MontageNodeTests` mould (every table row names a real `MotionMatchGraphInterop` method matching its pins, defaults, emitted IL valid, exec nodes refused by the pure compiler, pure reads work, missing attribute fails by name), registered in `Program.cs`. `Aver.Framework.Tests` is not used (it needs a native scene).

Verification the main loop runs (not the implementer's tests): headed PIE with an `AverCharacter` on the new database (idle, walk, run, turn, stop clips) at `MotionHalfLife` 0.2: stop/start/reverse/strafe from keyboard, `Show > Motion Matching` overlay on, no visible pop at switches, sliding below an eyeballed threshold, `searchInterval` 0.1 with 30 characters staying inside the animation budget (measure with the existing `--gpu-timing`-free CPU frame timers; the GPU must be idle, the owner plays other games), a montage attack over locomotion, and a state-machine variant (Idle clip -> Move motionmatch -> a jump clip).

## 13. Files, partitioned

**Create**
- `modules/anim/include/aver/anim/{MotionAsset,PoseDatabase,Trajectory,Inertializer,MotionMatcher}.hpp`, `modules/anim/src/{MotionAsset,PoseDatabase,PoseSearch,Trajectory,Inertializer,MotionMatcher}.cpp`, `modules/anim/src/AnimJson.hpp`
- `modules/anim.scene/include/aver/anim/MotionMatchSystem.hpp`, `modules/anim.scene/src/MotionMatchSystem.cpp`
- `scripting/csharp/Aver.Framework/MotionMatching.cs`
- `sandbox/src/{MotionDbEditor,MotionDebugOverlay}.{hpp,cpp}`
- `docs/ANIM_MOTION_MATCHING.md` (user-facing summary of sections 4-6, 8 and the patent rules; a pointer line in `docs/ANIM_BLEND_STATE.md` section on state kinds)
- `tests/anim/src/{MotionAssetTest,PoseDatabaseTest,PoseSearchTest,TrajectoryTest,InertializerTest,MotionMatcherTest}.cpp`, `tests/anim.scene/src/MotionMatchSystemTest.cpp`, `tests/editor/src/MotionDbEditorTest.cpp`, `scripting/csharp/Aver.Graph.Tests/MotionMatchNodeTests.cs`

**Modify (this feature owns these)**
- `modules/anim/`: `CMakeLists.txt` (6 sources, one commented block), `RootMotion.hpp/.cpp`, `AnimStateMachine.hpp/.cpp`, `AnimGraphAsset.hpp/.cpp` (the kind name, parse/write, version gate, `valid`, nothing else)
- `modules/anim.scene/`: `CMakeLists.txt` (1 source), `AnimSystem.hpp/.cpp` (the two additions of 6.3 only), `AnimGraphSystem.hpp/.cpp`
- `Runtime/include/aver/game/GameSystemsWiring.hpp` (three small insertions), `scripting/csharp/Aver.Framework/Character.cs`
- `sandbox/src/AnimStateMachineEditor.hpp/.cpp`, `sandbox/src/SandboxApp.cpp` (include + one factory line), `sandbox/src/SandboxApp.hpp` (`cbCreateMotionDb` decl, `motionDebug_` member), `sandbox/src/SandboxContentBrowser.cpp`, `sandbox/src/SandboxShell.cpp` (menu), `sandbox/src/SandboxRender.cpp` (overlay block), `sandbox/CMakeLists.txt` (2 sources)
- `tests/anim/CMakeLists.txt`, `tests/anim.scene/CMakeLists.txt`, `tests/editor/CMakeLists.txt`
- `modules/anim/module.json` and `modules/anim.scene/module.json`: **no change** (no new `Aver.*` edge; `std::async` needs no module). Run `scripts/module-guard-audit.py` after.

**Shared-file rules with the other wave-5 features**
- IK/retarget owns `Ik.*`, `ControlRig.*`, a retarget asset and `AnimEditor.cpp` panels: this feature touches none of them. It does not read bones' IK results (patent rule 4). If retargeting later feeds a database from a different skeleton it enters through `MotionClipFn`.
- Sequencer owns OcWorld SEQUENCE records, `SequenceEditor`, the Runtime `LevelSequence` player: no overlap.
- Append-only, whoever lands second keeps both blocks: `modules/anim/CMakeLists.txt`, `modules/anim.scene/CMakeLists.txt`, `tests/*/CMakeLists.txt`, `sandbox/CMakeLists.txt`, `SandboxApp.cpp/.hpp`, `SandboxContentBrowser.cpp`, `SandboxShell.cpp`, `SandboxRender.cpp`, `GameSystemsWiring.hpp`, `GraphNodeDefsGameSystems.hpp` and `GraphGameSystemNodeTable.cs` (graph-node agent, rows after the wave-2 montage block).
- `AnimSystem.hpp/.cpp`: the two small additions only; any other wave-5 change there merges around them.
- Owned by others (do not edit): `framework_abi.h`, `FrameworkAbi.cpp`, `framework_anim_abi.h`, `Native.cs`, `Entity.cs`, `AnimGraph.cs`, `FORMAT_SPECS.md` (the main loop adds section 11j at integration), `modules/formats`, `modules/ragdoll`, `modules/rhi.*`.

**Phasing** (size L, three workflows, each verified before the next): A = `modules/anim` pure layer + its six tests (asset, database build/bake, search, trajectory, inertializer, matcher; nothing else compiles against it yet). B = `modules/anim.scene` system + state kind + C# + graph rows + `MotionMatchSystemTest`. C = editor, debug overlay, Content Browser.

## 14. Risks and open questions for the owner

1. **Patents.** The EA family (US 11,562,523 / 11,995,754 / 12,403,400 / 9,827,496) is live; the design avoids their claimed elements (section 2) but I could not read full claim text, and an absent Ubisoft/Microsoft hit in web search is not clearance. Do you want counsel (or you) to read those claims before the implementer starts? Learned motion matching and any body-part-partitioned search are explicitly out until a re-check.
2. **Locomotion content.** The feature needs clips whose root bone carries the planar travel and heading, with idle/start/stop/turn coverage. Unity's LAFAN1 is non-commercial (CC BY-NC-ND), so it cannot ship; CMU mocap would need a BVH importer; Mixamo-style clips are usually in-place. Which source should the demo content come from? Until then the tests use synthetic clips.
3. **Mirroring** (halve capture cost by mirroring left/right) is not in v1: it needs a per-skeleton mirror table and correct local-rotation reflection. Wanted next?
4. **No time-warp / foot locking.** The pose plays at `playRate` (use 0.9-1.1); foot-slide correction belongs to the IK feature (planted-foot IK on top of the pose modifier chain), not here.
5. **SIMD.** SSE2 only (the x86-64 baseline; the build has no `/arch` flags). An AVX2 path needs a build flag or runtime dispatch; the linear search is memory-bound at this size, so I expect little gain. Say if you want it.
6. **One skeleton per database.** A database is built for one skeleton; using it on another needs the retarget asset from the IK feature (the matcher's `MotionClipFn` is where it would plug in). Not designed beyond that.
7. **`AverCharacter` faces the mouse-look yaw**, so it defaults to `MotionFacing.Yaw` (strafing poses). A third-person "turns toward where it runs" character needs a script (or a later `AverCharacter` option) that rotates the entity toward the velocity.
8. **Loading latency.** While a database builds (first run, ~1 s per 100k rows; instant with a good sidecar) the entity shows its CAnimator clip or rest pose. `pendingBuilds()` exists so a loading screen can wait; wiring it to either loading screen is not in scope.
9. **Parented characters** are matched in their parent's frame (local transform), and entities moved by a dynamic rigid body or Synapse steering are not specially handled (they overwrite `CLocal`, same limit as wave-2 root motion).
10. **Memory** is f32: 100k rows x 56 floats = 22 MB per database. 16-bit or 8-bit quantisation is possible later if a large library shows up.
11. **Montage/ragdoll under the matcher**: a long montage hides the pose while the matcher keeps ticking; after the blend-out the first search (<= 0.1 s) re-picks. A ragdoll or powered ragdoll sits after the matcher and is never fed back (patent rule 4); getting up from a ragdoll with matching is a separate, patent-reviewed item.
