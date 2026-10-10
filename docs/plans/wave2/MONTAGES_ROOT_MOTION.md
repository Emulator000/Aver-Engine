# Feature #26: Anim Blueprint extras (montages, slots, layered and additive blends, root motion)

Design contract for the implementing agent. Parity plan: `docs/plans/UE_FEATURE_PARITY_PLAN.md` row 26, wave 2. Nothing here is built. Rules in force: upgrade the existing systems (no parallel animation system), module boundaries hold (`docs/MODULES.md`), every C# capability gets graph nodes, no new third-party code, lean comments, D3D12/Vulkan parity (nothing here touches a GPU API: it ends in `Pose` and `poseToSkinning`, which both backends already consume).

## 0. Decisions in one screen

1. **Montage = a new JSON asset `.ocmontage`** (same family and loader style as `.ocblend`/`.ocasm`, in `AnimGraphAsset`). Played imperatively by script or graph node through a relayed ABI (the ragdoll pattern), not by a component.
2. **Slots are named strings.** A montage plays in one slot; per entity per slot the newest montage wins and older ones blend out. A **slot node** in the `.ocasm` pipeline says where in the pose chain a slot is applied. Slot `DefaultSlot` is applied full-body after the pipeline when no node names it.
3. **`.ocasm` gains an optional linear `pipeline`** (slot, layered, additive nodes) applied after the state machine's pose. File version 2 only when a pipeline is present; otherwise written as version 1, byte-identical to today.
4. **Layered blend per bone** = branch filter (bone + blend depth, nearest ancestor entry wins so a weight-0 entry carves a hole). **Additive** = existing `addPose` (Pose.cpp:75), now with a caller, reference pose = first frame, rest pose, or a named clip frame.
5. **Root motion**: a new dynamically registered component `CRootMotion` (mode None/Ignore/Apply/Accumulate) read by `AnimSystem`. Extraction is relative planar motion (X, Y, Z, yaw) of the skeleton's root bone from clips flagged `kOcAnimRootMotion`; the extracted components are locked to the skeleton rest value in the pose; the delta is accumulated per entity and either written to `CLocal` (Apply, no physics character), left for a physics character to consume (Apply + `External` flag, set by `AverCharacter`), or left for the script (Accumulate). Works for CAnimator clips, state machines, blend spaces and montages.
6. **No `scene_abi.h`, `framework_abi.h` or `Components.hpp` change.** The new C ABI lives in a new header `framework_anim_abi.h` with its own version (like `framework_blackboard_abi.h`), so it cannot collide with the input and GAS agents' bumps of `AVER_FW_ABI_VERSION_MINOR`.

## 1. What exists today

| Piece | Where | State |
|---|---|---|
| Pose = flat `std::vector<Transform> local` | `modules/anim/include/aver/anim/Pose.hpp:16` | Complete. No model-space pose type. |
| `blendPose` (lerp T/S, slerp R, whole pose, no mask) | Pose.hpp:52, Pose.cpp:64 | Used by AnimSystem weight blend (AnimSystem.cpp:330), state machine, blend space. No per-bone weights. |
| `addPose(base, additive, additiveRest, weight, out)` | Pose.hpp:58, Pose.cpp:75-103 | Correct and tested (`tests/anim/src/AnimTest.cpp:242`). **No runtime caller** (recorded in `docs/recon/control-rig.md:23`). |
| Clip flags `kOcAnimAdditiveBase` (bit1), `kOcAnimRootMotion` (bit2) | `modules/formats/include/aver/formats/OcAnim.hpp:96-97`, `docs/formats/FORMAT_SPECS.md:468` | Round-trip only. The one reader is the editor checkbox pair (`sandbox/src/AnimEditor.cpp:2470-2472`). Declared but unread. |
| `sampleAnimation(clip, t, pose)` samples every track | AnimSampler.cpp:136 | No per-track entry point; a root-only sample needs one. |
| `notifiesCrossed(const OcAnimation&, ClipStep, out)` | AnimSampler.hpp:178, AnimSampler.cpp:154 | Takes a whole clip; montage-level notifies have no clip. |
| `AnimStateMachine` (layers_ stack, `tick`, `evaluate`, `layerWeights`) | AnimStateMachine.hpp:89, .cpp:252/318/302 | Clip, blend-space and sub-machine states. Output is one `Pose`. No root motion, no post stage. |
| `BlendSpacePlayer` (`advance`, `sampleTime`, `evaluate`) | BlendSpace.hpp:254, .cpp:336/350/355 | As above. |
| `.ocblend`/`.ocasm` JSON io; version gate `header()` refuses newer | AnimGraphAsset.cpp:80, `kStateMachineVersion = 1` (AnimGraphAsset.hpp:17) | Pipeline/montage need an extension. |
| `AnimSystem::tick`: per CAnimator: `a->time += dt*speed` (AnimSystem.cpp:268), `restPose`, `poseSrc_` (318) else clip sample (319-333), `poseMod_` (341), `poseToSkinning` (343); early return when no animator pool (200) | `modules/anim.scene/src/AnimSystem.cpp` | The two seams: `PoseSourceFn` (AnimSystem.hpp:56) is taken by `AnimGraphSystem`, `PoseModifierFn` (hpp:50) is taken by the control rig and chained by the ragdoll (`RagdollSystem.hpp:34`, `RagdollSystem.cpp:85-107`). **Neither can host montages**: the modifier chain order is rig then ragdoll, and installing a montage modifier would run it after the rig and overwrite IK. So montage application is part of `AnimSystem` itself, before `poseMod_`. |
| `AnimGraphSystem` (CAnimGraph, instances_, `tick` before AnimSystem::tick, `sourceThunk`) | AnimGraphSystem.hpp:41, .cpp:115/165 | Evaluates `sm->evaluate(skel, pose)` and nothing after it. |
| Notify sink `AnimNotifyFn(entity, name, user)`; `_Begin/_End` suffix convention for notify states | AnimSystem.hpp:33, AnimSystem.cpp `fireState` | Montages reuse the same sink and convention. |
| Provider relay precedent (framework holds a function pointer the composition root installs) | `framework_abi.h:253,276`; `RagdollSystem.hpp:109-116`; installs at `Runtime/src/GameApp.cpp:1746-1752`, `sandbox/src/SandboxApp.cpp:549-554` | Pattern for montages. |
| Physics character handle lives in C# (`AverCharacter._capsule`), native code cannot see it; `Drive` sets velocity every tick and copies the capsule centre back (`Character.cs`: `Drive`, `SyncFromSimulation`) | `scripting/csharp/Aver.Framework/Character.cs` | Root motion for a capsule must be consumed in C#. |
| Per-frame order in both hosts: `tickGameplay` (C# Drive), `game::tickAnimGraphs(dt)`, `ragdoll.tick`, `animSystem().tick` | `Runtime/src/GameApp.cpp:1991-1996`, `sandbox/src/SandboxApp.cpp:2134-2138` | Root motion computed in anim tick reaches a capsule one frame later (accepted, section 8). |
| Content ids: any file under Content gets `fnv1a64(path)`; resolver `GameContent::resolveAnimAsset` (`Runtime/src/GameContent.cpp:117`) | | `.ocmontage` needs no registration anywhere. |
| Asset editor registry: `assetEditors_.registerFactory(&editor::makeAnimStateMachineEditor)` (`SandboxApp.cpp:445`); create entries `cbCreateStateMachine` (`SandboxContentBrowser.cpp:600`), type label table (`:1741`) | | Pattern for the montage editor. |

Missing: montages, sections, slots, a post-state-machine stage, per-bone blending, any caller of `addPose`, any reader of the two clip flags, root bone extraction/application, C#/graph surface for any of it.

## 2. Frame order and data flow (contract)

```
host frame:
  tickGameplay            (C# calls PlayMontage / StopMontage / SetAnimParam ...)
  tickAnimGraphs(dt)      AnimGraphSystem::tick: params -> sm.tick(dt) [+ root delta]  -> anim.addRootMotion(e, delta)
  ragdoll.tick(dt)
  AnimSystem::tick(dt):
    prune (existing) + montages_.prune(world)
    montages_.tick(world, dt, notifySink)        // advance clocks, weights, sections; collect events + root deltas
    for each animator entity:
       advance CAnimator clock (existing), notifies (existing)
       restPose
       base = poseSrc_ ? graph(sm.evaluate -> pipeline nodes) : clip sample     // pipeline slot nodes call evaluateSlot()
       DefaultSlot montages (if no pipeline slot node named it)                    // full body, over base
       root motion: mix base/montage deltas -> accumulate -> Apply writes CLocal
       poseMod_ (control rig, then ragdoll)                                        // unchanged
       poseToSkinning
```

Montages therefore sit **before** the control rig and the ragdoll, so IK and ragdoll blending layer on top of montage poses.

## 3. Assets

### 3.1 `.ocmontage` (JSON, UTF-8; `"format":"ocmontage"`, `"version":1`)

Written/parsed in `AnimGraphAsset.cpp` with the existing `esc`/`num`/`header` helpers; floats `%.9g` (round-trips f32); parse does not call `valid()`; unknown keys ignored; a newer version is refused.

```json
{
  "format": "ocmontage",
  "version": 1,
  "name": "SwordCombo",
  "slot": "DefaultSlot",
  "blendIn": 0.15,
  "blendOut": 0.2,
  "blendInCurve": "smooth",
  "blendOutCurve": "smooth",
  "blendOutTrigger": -1,
  "rate": 1,
  "additive": "auto",
  "segments": [
    {"clip": "Anims/slash1.ocanim", "start": 0, "end": -1, "rate": 1},
    {"clip": "Anims/slash2.ocanim", "start": 0.1, "end": 0.9, "rate": 1.2}
  ],
  "sections": [
    {"name": "Windup",  "start": 0.0},
    {"name": "Slash",   "start": 0.4, "next": "Recover"},
    {"name": "Recover", "start": 0.9, "next": "@end"}
  ],
  "notifies": [
    {"time": 0.5, "name": "HitStart", "duration": 0.1},
    {"time": 0.7, "name": "Whoosh"}
  ]
}
```

Semantics (all normative):
- **Timeline**: montage time (seconds, rate 1) is the concatenation of segments. A segment plays clip seconds `[start, end]` (`end < 0` = clip duration) at `segment.rate`, so it occupies `(end-start)/rate` montage seconds. Clip loop flags are ignored (clamped inside `[start,end]`). `clip` is a project asset path; id = `fnv1a64(path)` through the existing resolver.
- **Sections** partition the timeline: section `i` spans `[start_i, start_{i+1})` in time order, the last to the montage length. The first section must start at 0 (if none is authored, the loader synthesizes `Default` at 0). `next` omitted/`""` = the following section in time order (the last one ends the montage); `"@end"` = the montage ends when this section ends (blend-out begins early, below); a section's own name = loop.
- **Blend**: `blendIn`/`blendOut` seconds (0 = cut). Curves `linear|smooth` (smoothstep of the linear alpha). `blendOutTrigger` seconds before the end (of the montage or of an `@end` section) at which the automatic blend-out starts; `-1` = use `blendOut`; clamped to the length of the ending section. `rate` is the asset speed (instance rate multiplies it); montage rate must be >= 0, 0 pauses, negative is clamped to 0 (no reverse playback in v1).
- **additive**: `"auto"` (default; true when the first resolved segment's clip carries `kOcAnimAdditiveBase`), `true`, `false`. An additive montage is applied with `addPose(base, sample, firstFrameOfFirstSegment, 1)`; its reference is the first frame of the first segment.
- **notifies**: `duration > 0` makes a notify state (`<Name>_Begin`/`<Name>_End` through the same sink). Clip-embedded instant notifies inside a segment also fire (clip time crossings, split at segment boundaries). Clip-embedded notify STATES inside a montage segment are ignored in v1 (the open/close leak guarantees live in `AnimSystem::stepNotifyStates` and are not extended; montage-level states are guaranteed to close on stop, end, entity death, `clear()`).
- `valid()`: >= 1 segment; each `rate > 0`; `end < 0 || end > start >= 0`; section names unique, non-empty, starts finite and ascending after sorting; `next` is empty, `@end` or an existing section; notify `time` finite, `duration >= 0`; blend times >= 0.

### 3.2 `.ocasm` pipeline (version 2, optional)

New top-level key after `machines`. Written as `"version": 2` only when `pipeline` is non-empty; otherwise `"version": 1` and byte-identical to today (`AnimGraphAssetTest` already pins that round-trip). `kStateMachineVersion` becomes 2; `header()` accepts <= 2.

```json
"pipeline": [
  {"type": "slot", "slot": "DefaultSlot"},
  {"type": "layered",
   "source": {"kind": "slot", "slot": "UpperBody"},
   "branches": [{"bone": "spine_01", "depth": 3, "weight": 1.0}, {"bone": "hand_l", "weight": 0.0}],
   "weight": 1.0, "weightParam": "upperAlpha"},
  {"type": "layered",
   "source": {"kind": "clip", "asset": "Anims/aim.ocanim", "loop": true, "speed": 1},
   "branches": [{"bone": "spine_01"}], "weight": 1.0},
  {"type": "additive",
   "source": {"kind": "blendspace", "asset": "Anims/lean.ocblend", "xParam": "lean", "yParam": ""},
   "ref": {"kind": "firstFrame"},
   "weight": 1.0, "weightParam": "leanAmount"}
]
```
- Nodes run in order on the running pose; node 0 receives the state machine's pose. `weightParam` (float parameter of this machine) multiplies `weight`; unknown name = 1.
- **`slot`**: applies the slot's montage pose full-body with the slot coverage as weight.
- **`layered`**: per-bone blend of `source` over the running pose. `source.kind`: `slot`, `clip` (looping per `loop`, own clock at `speed`), `blendspace` (`asset`, `xParam`, `yParam` read from this machine). `branches`: per bone-name entry `{bone, depth, weight}`. Mask value of a bone = `weight * ramp` of the NEAREST ancestor-or-self entry bone (deepest wins; none = 0), `ramp = depth > 0 ? min(1, (k+1)/depth) : 1` with `k` = distance in bones from the entry bone. A bone name absent from the skeleton is skipped (not an error; logged once per asset). Per-bone weight = `mask * node weight * (slot coverage for slot sources)`. Blend: lerp T/S, slerp R on the running pose (the source is sampled ONTO a copy of the running pose, so bones the source does not animate keep the base).
- **`additive`**: `source` as above (no `slot` kind needed, but allowed); `ref.kind`: `firstFrame` (default; first frame of a clip source / first sample of a blend space), `rest`, or `{"kind":"clip","asset":"...","time":0.0}`. Applied with `addPose(running, sourcePose, refPose, weight)`. `refPose` is cached at bind.
- **Slot lookup**: a slot name consumed by a `slot`/`layered` node is applied there (marked consumed this tick). Montages in `DefaultSlot` that no node consumed are applied full-body after the pipeline. Montages in any other unconsumed slot are ignored and warned once ("montage slot 'X' has no slot node in <machine>").
- `valid()` additions (`AnimStateMachineAsset::valid`, AnimStateMachine.cpp:23): known node types, non-empty `source`, `branches` non-empty for `layered`, finite weights, `depth >= 0`, `ref` known.
- Not in v1: a `machine` source (second state machine as a layer), mesh-space rotation blend, per-node curves.

### 3.3 Clip flags (`.ocanim`, no byte change)

- Bit1 (`kOcAnimAdditiveBase`): meaning pinned as **"this clip is authored as an additive delta"**. Read by `.ocmontage` `"additive":"auto"`. Editor label changes from "Additive base" to "Additive" (the constant keeps its name; renaming it is churn). `FORMAT_SPECS.md` is owned by the data-tables agent: do NOT edit it; the semantic goes into the new `docs/ANIM_MONTAGES.md` (section 9).
- Bit2 (`kOcAnimRootMotion`): "extract this clip's root bone motion when the entity's `CRootMotion` mode asks". A clip without it is never extracted (zero behaviour change for existing content).

## 4. `modules/anim` (pure; Core + Formats only)

### 4.1 New: `RootMotion.hpp` / `RootMotion.cpp`

```cpp
inline constexpr u32 kRootAxisX = 1, kRootAxisY = 2, kRootAxisZ = 4, kRootAxisYaw = 8;
inline constexpr u32 kRootAxesDefault = kRootAxisX | kRootAxisY | kRootAxisYaw;
inline constexpr u32 kNoBone = 0xFFFFFFFFu;

struct RootSpec {                 // built once per entity per tick by AnimSystem::rootSpec
    u32 bone = kNoBone;
    u32 axes = kRootAxesDefault;
    Transform rest;               // the skeleton's rest transform of `bone`
    bool valid() const { return bone != kNoBone; }
};
struct RootDelta {                // planar rigid motion in the root's own frame at the START of the step
    Vec3 move{0, 0, 0};           // cm, model space, rotated by -yaw0
    f32  yaw = 0.0f;              // radians about +Z
    bool any() const;
};

u32  rootBoneOf(const fmt::OcSkeleton& s);                       // OcSkeleton::rootBone if in range, else first bone with parent -1, else kNoBone
f32  yawOf(const Quat& q);                                        // twist of q about +Z, radians (swing-twist)
bool clipHasRootMotion(const fmt::OcAnimation& c, const RootSpec& s);   // flag set, not additive, has a track on s.bone
// Root motion between two clip times (already clip-wrapped). wrap=true: t1 < t0 means forward through the loop seam
// (t0 -> duration, then 0 -> t1). Delta = C(t0)^-1 * C(t1) restricted to planar translation + yaw,
// move = Rz(-yaw0) * (p1 - p0) masked by s.axes, yaw = yaw1 - yaw0 (wrapped to (-pi, pi]) if kRootAxisYaw.
RootDelta rootDeltaBetween(const fmt::OcAnimation& c, const RootSpec& s, f32 t0, f32 t1, bool wrap);
// acc += w * d  (blend weights sum to 1 at the call sites)
void addWeighted(RootDelta& acc, const RootDelta& d, f32 w);
// a toward b by t (componentwise)
RootDelta lerpDelta(const RootDelta& a, const RootDelta& b, f32 t);
// Compose: acc = acc o d   (acc.move += Rz(acc.yaw) * d.move; acc.yaw += d.yaw). Used by the accumulator.
void composeDelta(RootDelta& acc, const RootDelta& d);
// Replaces the extracted components of the root bone in `p` with the skeleton rest values:
// X/Y/Z positions per s.axes; yaw via swing-twist (q = Rz(yaw) * swing -> Rz(yawRest) * swing). No-op when !s.valid().
void lockRoot(Pose& p, const RootSpec& s);
```
The lock reference is the **skeleton rest transform** (Unreal's default root lock), not the clip's first frame: the entity moves by relative deltas, the pose root never carries the extracted components. Pitch/roll of the root are never touched. Assumes Z-up engine-space skeletons whose heading is a rotation about +Z (risk 1).

### 4.2 Changes to existing anim files

- **AnimSampler.hpp/.cpp**: add `void sampleTrack(const fmt::OcTrack& t, f32 seconds, Transform& inOut);` (the body of the loop in `sampleAnimation`, which then calls it; no behaviour change) and `void notifiesCrossedIn(const std::vector<fmt::OcNotify>& notifies, f32 duration, const ClipStep& step, std::vector<u32>& outIndices);` (the existing function becomes a one-line delegate; same crossing logic, so montage and clip notifies cannot drift).
- **Pose.hpp/.cpp**: add
  ```cpp
  // Per-bone blend: bone i moves from a toward b by w[i] (clamped). w.size() != bones: out = a.
  void blendPoseMasked(const Pose& a, const Pose& b, const std::vector<f32>& w, Pose& out);
  // Branch filter -> per-bone weights (rules in 3.2). Unknown bone names are skipped; returns the count of entries that matched.
  struct BranchEntry { std::string bone; i32 depth = 0; f32 weight = 1.0f; };
  u32 buildBranchMask(const fmt::OcSkeleton& skel, const std::vector<BranchEntry>& entries, std::vector<f32>& outWeights);
  ```
- **AnimPipeline.hpp/.cpp (new)** holds both the asset structs (so `AnimStateMachineAsset` can embed them) and the runtime:
  ```cpp
  enum class PipeNodeKind : u8 { Slot = 0, Layered = 1, Additive = 2 };
  enum class PipeSourceKind : u8 { None = 0, Slot = 1, Clip = 2, BlendSpace = 3 };
  enum class PipeRefKind : u8 { FirstFrame = 0, Rest = 1, Clip = 2 };
  struct PipeSource { PipeSourceKind kind = PipeSourceKind::None; std::string slot, asset, xParam, yParam; bool loop = true; f32 speed = 1.0f; };
  struct PipeRef { PipeRefKind kind = PipeRefKind::FirstFrame; std::string asset; f32 time = 0.0f; };
  struct PipeNode {
      PipeNodeKind kind = PipeNodeKind::Slot;
      std::string slot;                       // Slot nodes
      PipeSource source;                      // Layered / Additive
      std::vector<BranchEntry> branches;      // Layered
      PipeRef ref;                            // Additive
      f32 weight = 1.0f; std::string weightParam;
  };
  // Supplied per evaluation by the host (AnimGraphSystem): the slot's montage pose over `base`, and its coverage 0..1.
  using SlotEvalFn = std::function<bool(u64 slotHash, const Pose& base, Pose& out, f32& coverage)>;
  using ParamFn = std::function<f32(const std::string&)>;
  class AnimPipeline {
  public:
      void bind(const std::vector<PipeNode>* nodes, const fmt::OcSkeleton* skel, AsmClipFn clips, AsmBlendSpaceFn spaces);
      void tick(f32 dt, const ParamFn& param);                                   // clip / blend-space source clocks
      void evaluate(const ParamFn& param, const SlotEvalFn& slot, Pose& inOut);  // runs the nodes in order
      u64 consumedSlot(usize i) const;                                           // slot hash node i consumed this evaluate, or 0
      f32 rootWeight(u64 slotHash) const;                                        // 1 for a slot node, mask[rootBone]*weight for layered; 0 if not consumed
  };
  ```
  `AsmClipFn`/`AsmBlendSpaceFn` are the existing aliases (AnimStateMachine.hpp:86-87); to avoid a cycle `AnimPipeline.hpp` includes `BlendSpace.hpp` and declares the two aliases' underlying `std::function` types itself (`AnimStateMachine.hpp` then includes `AnimPipeline.hpp` and keeps its aliases as the same types).
- **AnimStateMachine.hpp/.cpp**: `AnimStateMachineAsset` gains `std::vector<PipeNode> pipeline;`. Add
  ```cpp
  void setRootMotion(const RootSpec* spec);   // null = off; copied
  RootDelta consumeRootDelta();               // sum since last call, layer-weighted
  ```
  In `tick` (cpp:252): for each live layer with a clip state, when the spec is set and `clipHasRootMotion`, compute `rootDeltaBetween(clip, spec, timeBefore, timeAfter, state.loop)` around the existing time update (lines 259-272); for blend-space layers use `BlendSpacePlayer::consumeRootDelta()`. After the alpha update and the `erase` (cpp:290), `acc_ += layerWeights()[i] * delta_i` via `addWeighted`. `evaluate` (cpp:318) calls `lockRoot` on each layer's pose BEFORE blending, for clips with `clipHasRootMotion` only. (Weights are the post-update ones; a layer erased in the same step loses that step's share: at most one tick of error, accepted.)
- **BlendSpace.hpp/.cpp**: `setRootMotion(const RootSpec*)`, `RootDelta consumeRootDelta()`; `advance` (cpp:336) computes per-sample deltas from `sampleTime(i)` before/after (wrap=true, blend spaces are cyclic) weighted by `weights_`; `evaluate` (cpp:355) locks each sample pose.
- **AnimGraphAsset.hpp/.cpp**: `kStateMachineVersion = 2`, `kMontageVersion = 1`; `writeStateMachine`/`parseStateMachine` handle `pipeline`; new `std::string writeMontage(const MontageAsset&)`, `bool parseMontage(std::string_view, MontageAsset&, std::string* why = nullptr)`, `saveMontage`, `loadMontage` (same four-function shape as blend space/state machine).

### 4.3 New: `Montage.hpp` / `Montage.cpp`

```cpp
enum class BlendCurve : u8 { Linear = 0, Smooth = 1 };
struct MontageSegment { std::string clip; f32 start = 0.0f; f32 end = -1.0f; f32 rate = 1.0f; };
struct MontageSection { std::string name; f32 start = 0.0f; std::string next; };   // next: "" following, "@end", or a section name
struct MontageNotify  { f32 time = 0.0f; std::string name; f32 duration = 0.0f; };
inline constexpr const char* kDefaultSlot = "DefaultSlot";

struct MontageAsset {
    std::string name;
    std::string slot = kDefaultSlot;
    f32 blendIn = 0.15f, blendOut = 0.2f, blendOutTrigger = -1.0f, rate = 1.0f;
    BlendCurve inCurve = BlendCurve::Smooth, outCurve = BlendCurve::Smooth;
    i32 additive = -1;                           // -1 auto, 0 no, 1 yes
    std::vector<MontageSegment> segments;
    std::vector<MontageSection> sections;        // kept sorted by start
    std::vector<MontageNotify>  notifies;
    bool valid(std::string* why = nullptr) const;
    i32  sectionIndex(std::string_view name) const;     // -1 if absent
};

struct MontageEvent {
    enum class Kind : u8 { Started, Notify, NotifyBegin, NotifyEnd, SectionStarted, BlendOut, Interrupted, Ended };
    Kind kind; std::string name;                 // notify or section name; empty for the lifecycle kinds
};
using MontageClipFn = std::function<const fmt::OcAnimation*(const std::string&)>;

class MontagePlayer {
public:
    // Resolves clips and the timeline. False when no segment resolves to a clip (nothing to play).
    bool bind(const MontageAsset* asset, MontageClipFn clips);
    void start(i32 section, f32 rate, f32 blendInOverride);   // section < 0 = first; blendInOverride < 0 = asset; restarts if running
    void stop(f32 blendOutOverride);                         // < 0 = asset; 0 = cut. Raises Interrupted (once) before BlendOut when stopped early
    bool jumpToSection(i32 section);                         // immediate, weight kept; fires SectionStarted, no notifies for the skipped span
    bool setNextSection(i32 from, i32 to);                   // to < 0 restores the authored link; false if indices out of range
    void setRate(f32 r);                                     // >= 0, 0 pauses
    void setRootMotion(const RootSpec* spec);
    void advance(f32 dt);                                    // clock, section chain, weight, notifies, root delta
    // Samples ONTO a copy of `base` so bones the montage does not animate keep the base. Additive montages do
    // addPose(base, sample, firstFrameRef, 1). Does not apply the weight: the slot blends by coverage.
    void evaluate(const fmt::OcSkeleton& skel, const Pose& base, Pose& out) const;
    void drainEvents(std::vector<MontageEvent>& out);
    RootDelta consumeRootDelta();                            // unweighted
    bool  hasRootMotion() const;                             // any bound segment clip passes clipHasRootMotion
    f32   weight() const;                                    // 0..1 after the curve
    f32   position() const;                                  // montage seconds
    f32   length() const;
    f32   rate() const;                                      // instance rate (asset rate not included)
    i32   section() const;
    bool  finished() const;                                  // weight reached 0 after BlendOut, or cut; Ended already raised
    bool  additive() const;
    const MontageAsset* asset() const;
};
```
`advance(dt)` rules: speed = `asset.rate * rate`; `pos += dt*speed`; while `pos >= sectionEnd(cur)` follow `next` (override, else authored, else following section), setting `pos = start(next) + overshoot`, raising `SectionStarted`, at most `sections+1` hops per tick (zero-length guard); an `@end`/last section whose end is reached ends the montage: `pos = end`, start blend-out if not started. Auto blend-out when the ending section has no loop and `sectionEnd - pos <= trigger` (trigger = `blendOutTrigger >= 0 ? it : blendOut`, clamped to the section length). Weight: in-phase `a += dt/blendIn` (instant if 0), out-phase `a -= dt/blendOut` (cut if 0), `weight = curve(a)`. Notify crossing: each travelled interval (a tick may contain several, split at section jumps; none for `jumpToSection`) goes through `notifiesCrossedIn` on `asset.notifies` (instant ones fire `Notify`; `duration > 0` open/close `NotifyBegin/NotifyEnd` with an `open_` flag vector; every open state is closed by `Interrupted`-finish, `Ended`, `bind`, and destruction paths via `finish()` before `drainEvents`) and, per segment overlap, through `notifiesCrossedIn` on that clip's instant notifies (clip time interval mapped from the segment). Events: `Started` on `start`, `Interrupted` on early `stop`, `BlendOut` when blend-out begins, `Ended` when the weight reaches 0 (after BlendOut) or at a cut. Root delta: per travelled interval, per segment overlap whose clip passes `clipHasRootMotion`, `rootDeltaBetween(clip, spec, c0, c1, false)` summed with `composeDelta` (segments are sequential in time).

## 5. `modules/anim.scene`

### 5.1 `CRootMotion` (registered at runtime like `CAnimGraph`; no Components.hpp or scene ABI change)

In `AnimSystem.hpp`:
```cpp
inline constexpr i32 kRootMotionNone = 0, kRootMotionIgnore = 1, kRootMotionApply = 2, kRootMotionAccumulate = 3;
inline constexpr u32 kRootSrcGraph = 1, kRootSrcMontage = 2;      // CRootMotion::sources; 0 reads as both
inline constexpr u32 kRootMotionExternal = 0x1;                   // CRootMotion::flags: the caller (a physics character) consumes the accumulator
struct CRootMotion {
    i32 mode = 0;          // kRootMotion*; zero-filled = None, so a raw attach changes nothing
    u32 sources = 0;       // 0 reads as kRootSrcGraph | kRootSrcMontage
    u32 axes = 0;          // 0 reads as X | Y | Yaw
    u32 flags = 0;
    f32 scale = 0.0f;      // multiplier on translation; 0 reads as 1
    f32 accumX = 0.0f, accumY = 0.0f, accumZ = 0.0f;   // cm, in the entity's own frame at the START of the accumulation
    f32 accumYaw = 0.0f;   // radians
    f32 accumSeconds = 0.0f;  // simulated time the accumulator covers
    u32 reserved = 0;
};
static_assert(sizeof(CRootMotion) == 44, "CRootMotion must be padding-free");
```
- `static u32 AnimSystem::registerComponents(scene::World&)`: `registerComponent<CRootMotion>("CRootMotion")`; fields `mode, sources, axes, flags` (I32), `scale, accumX, accumY, accumZ, accumYaw, accumSeconds` (F32), `reserved` (I32, readOnly). The accumulators are **writable** (a script clears them by writing zero, like a trigger slot). `static CRootMotion* attach(world, e, mode, axes, scale)` assigns a fresh value over the zero-filled bytes.
- Called from `game::registerGameSystems()` (`GameSystemsWiring.hpp:62-66`), next to `AnimGraphSystem::registerComponents`; both hosts already call it.

### 5.2 `AnimSystem` changes (the existing system grows; no new tick entry point)

```cpp
// public
static u32 registerComponents(scene::World& world);
MontageSystem& montages() { return montages_; }
// Fills `out` from the entity's CRootMotion and skeleton; false when mode == None, the source bit is off, or no root bone.
bool rootSpec(const scene::World& world, scene::Entity e, u32 sourceBit, RootSpec& out);
// AnimGraphSystem feeds the state machine's per-tick delta here (called before tick(); consumed by it).
void addRootMotion(scene::Entity e, const RootDelta& d);
// Slot lookup for pipeline nodes: montage pose over `base` and coverage. False when nothing plays in that slot.
bool evaluateSlot(scene::Entity e, u64 slotHash, const fmt::OcSkeleton& skel, const Pose& base, Pose& out, f32& coverage);
using SlotRootWeightFn = f32 (*)(scene::Entity e, u64 slotHash, void* user);
void setSlotRootWeight(SlotRootWeightFn fn, void* user);     // AnimGraphSystem installs AnimPipeline::rootWeight; default 1
```
In `tick` (AnimSystem.cpp:133):
1. After the prune block: `montages_.prune(world)` (every tick; the map holds only entities with active players) and `montages_.tick(world, dt, sink)`. Per entity the tick skips advancing when that entity's `CAnimator.flags & kAnimatorPaused`. `sink` forwards events through `notify_` incrementing `fired_` exactly like `deliver` (nothing delivered with no sink installed, no backlog).
2. CAnimator clip root motion: around line 268, remember `before = a->time`; after the advance, if the clip passes `clipHasRootMotion(spec)` and `kRootSrcGraph` is on and the entity is not `sourced`, `delta = rootDeltaBetween(clip, spec, wrapClipTime(before), wrapClipTime(a->time), !once)`. Scrub guard: a per-entity `RootClock {u64 clip; f32 time; bool valid}` (a map like `clocks_`, pruned with it); if `|a->time - clock.time| > 2*dt*|speed| + 1e-4` or the clip changed, the delta is 0 for that tick (an external write is a scrub, not motion).
3. Between pose source/clip sample (line 318-333) and `poseMod_` (line 341): (a) `montages_` default-slot application: for every slot of the entity not consumed by the pipeline this tick and equal to `DefaultSlot`, `evaluateSlot` then `blendPose(p.pose, slotPose, coverage)`; (b) `lockRoot` for the CAnimator clip path (the sampled pose; the graph path locks inside its evaluators); (c) root-motion mix and accumulate (5.4).
4. `clear()` also clears montage state and assets (after closing open notify states through the sink, same reentrancy guard as the existing code).
- Slot consumption bookkeeping: `evaluateSlot` marks `(entity, slotHash)` consumed in a per-tick set cleared at the top of the entity loop.

### 5.3 `MontageSystem` (new: `AnimMontages.hpp` / `AnimMontages.cpp`, member of `AnimSystem`)

```cpp
class MontageSystem {
public:
    // Loads (cached by id, failure cached as null) and starts. False: unknown asset/unbound clips, entity without a skeleton.
    bool play(AnimSystem& anim, scene::World& w, scene::Entity e, u64 montageId, u64 slotOverride,
              u64 sectionHash, f32 rate, f32 blendIn);
    u32  stop(scene::Entity e, u64 montageId /*0 = all*/, f32 blendOut);
    u32  stopSlot(scene::Entity e, u64 slotHash, f32 blendOut);
    bool jump(scene::Entity e, u64 montageId /*0 = newest*/, u64 sectionHash);
    bool setNextSection(scene::Entity e, u64 montageId, u64 fromHash, u64 toHash /*0 = authored*/);
    bool setRate(scene::Entity e, u64 montageId, f32 rate);
    bool isPlaying(scene::Entity e, u64 montageId /*0 = any*/) const;
    bool state(scene::Entity e, u64 montageId, f32 out[4] /*pos,len,weight,rate*/, u64* sectionHash) const;
    void stopAll();                                        // Play stop / level change
    void clear();                                          // drops assets and players (after closing)
    // internals used by AnimSystem
    void tick(scene::World&, f32 dt, const std::function<void(scene::Entity, const char*)>& sink);
    void prune(const scene::World&);
    bool evaluateSlot(...);                                // 5.2
    bool rootDelta(scene::Entity e, u64 slotHash, RootDelta& out, f32& weight) const;   // mixed over the slot's stack
};
```
- **Identity**: one player per `(entity, montageId)`; `play` of a running montage restarts it. **Per slot** a vector (max 4) ordered oldest first; `play` appends and calls `stop(blendOut)` on the previous top (raising its `Interrupted`); the oldest is dropped when the cap is hit. Players with `finished()` are erased after their events are drained.
- **Slot pose** (`evaluateSlot`): `acc = none, cover = 0`; for each player oldest to newest with weight `w`: `pose_m = player.evaluate(skel, base, tmp)`; first: `acc = pose_m`, `cover = w`; next: `acc = blendPose(acc, pose_m, w)`, `cover = 1 - (1 - cover)(1 - w)`. Returns `acc`, `cover`. Scratch `Pose` buffers live in the system (no allocation per tick after warm-up).
- **Event names through the existing notify sink** (`AnimNotifyFn`, so `OnEvent`/`CustomEvent` nodes receive them with no new wire): notify `Name` -> `Name`; states -> `Name_Begin`/`Name_End`; lifecycle -> `<MontageName>_Started`, `<MontageName>_Interrupted`, `<MontageName>_BlendOut`, `<MontageName>_Ended`; section entry -> `<MontageName>_Section_<Section>`. `<MontageName>` = asset `name`, else the file stem.
- **Entity death/prune**: the system closes the players (`finish()`, drain, deliver `NotifyEnd`s and `Ended`) exactly as `closeAllOpen` does for clocks. `Ended` always fires once per play.
- **Skeleton/animator**: `play` adds `CAnimator` if absent (so AnimSystem poses the entity; a CAnimator with `clip == 0` poses rest) and requires `CSkeletalMesh.skeleton`.
- **Provider** (declared in `AnimMontages.hpp`, defined in `AnimMontages.cpp`, signatures match `aver_fw_anim_fn`, section 6): `i32 montageProvider(i32 op, i32 entity, i64 h0, i64 h1, i64 h2, f32 f0, f32 f1, f32* outF, i64* outI, void* user)`; `user` = `&animSystem()`. Declared in this module so the framework never links Anim.Scene (the ragdoll arrangement, `RagdollSystem.hpp:109`).

### 5.4 Root motion mixing, accumulation and application (in `AnimSystem::tick`, per entity)

```
spec  = rootSpec(world, e, ...)                       // None -> skip everything, delta pipeline never runs
base  = pendingBase_[e] (state machine, from addRootMotion) OR CAnimator delta   // already zero unless a flagged clip moved
d     = base
for each slot s of the entity (stable order): if (montage root delta available: montage hasRootMotion, kRootSrcMontage on)
    d = lerpDelta(d, slotDelta_s, coverage_s * slotRootWeight(e, s))   // a montage with no root-motion clip does not override the base
scale = CRootMotion.scale(0->1) * world.localTransform(e).scale.x
d.move *= scale
mode Ignore:      drop d.
mode Accumulate:  composeDelta(accum, d) into CRootMotion.accum*, accumSeconds += dt.
mode Apply:       composeDelta(accum, d), accumSeconds += dt; then
                  if (!(flags & kRootMotionExternal)): write CLocal and zero the accumulator:
                       pos += rot.rotate(accumMove); rot = rot * Rz(accumYaw);   // E o delta, the object-animation rule (AnimSystem.cpp:401)
                       via world.setLocalTransform(e, xf)                         // NOT a raw write: see writeObject's note
                  else (External): cap — if accumSeconds > 0.5 nobody is consuming: zero it.
```
`lockRoot` already ran inside each evaluator for clips that passed `clipHasRootMotion`, so for modes Ignore/Apply/Accumulate the mesh stays on the entity while the entity moves; mode None leaves the pose untouched (legacy). Applied in the entity's parent-local frame (a child of a scaled parent moves by the unscaled delta: documented limit). Entities driven by a dynamic rigid body or the Synapse agent steering are not handled (those systems overwrite CLocal); kinematic and plain entities are.

### 5.5 `AnimGraphSystem` changes (AnimGraphSystem.hpp/.cpp)

- `Instance` gains `AnimPipeline pipe; std::vector<f32> scratch`; bound at instance creation (`tick`, cpp:221-228) with `&loaded->asset.pipeline`, the entity's skeleton (resolved through `anim_->skeleton(CSkeletalMesh.skeleton)`; the pipeline binds lazily on the first tick that has a skeleton) and the same clip/blend-space resolvers.
- `tick` (cpp:115): before `sm.tick`, `RootSpec spec; const bool rm = anim_->rootSpec(world, e, kRootSrcGraph, spec); sm.setRootMotion(rm ? &spec : nullptr);` after it, `anim_->addRootMotion(e, sm.consumeRootDelta());`. Also `inst.pipe.tick(dt*rate, param)`.
- `sourceThunk` (cpp:165): `sm->evaluate(skel, pose)` then `pipe.evaluate(param, slotFn, pose)` where `slotFn` calls `anim_->evaluateSlot(e, ...)`. Installs `setSlotRootWeight` once in `install` (reads `Instance::pipe.rootWeight`).
- A machine with an empty pipeline is exactly today's code path (cost: one empty-vector check).

## 6. C ABI: `modules/framework/include/aver/framework/framework_anim_abi.h` + `src/AnimAbi.cpp`

New header (conventions of `framework_blackboard_abi.h`: `int32_t/int64_t/float/const char*` only, 1 success / 0 refusal, `#include "aver/framework/framework_abi.h"` for `AVER_FW_ABI`/`AVER_FW_CALL`). Own version: `#define AVER_FW_ANIM_ABI_VERSION 1`, `AVER_FW_ABI int32_t aver_fw_anim_abi_version(void);`. `AVER_FW_ABI_VERSION_MINOR` is NOT touched.

```c
#define AVER_FW_ANIM_OP_MONTAGE_PLAY        1   /* h0 montage, h1 slot (0 authored), h2 start section (0 first), f0 rate, f1 blendIn (<0 authored) */
#define AVER_FW_ANIM_OP_MONTAGE_STOP        2   /* h0 montage (0 all), f0 blendOut (<0 authored); returns number stopped */
#define AVER_FW_ANIM_OP_MONTAGE_STOP_SLOT   3   /* h0 slot, f0 blendOut */
#define AVER_FW_ANIM_OP_MONTAGE_JUMP        4   /* h0 montage (0 newest), h1 section */
#define AVER_FW_ANIM_OP_MONTAGE_NEXT        5   /* h0 montage, h1 from section, h2 to section (0 restores authored) */
#define AVER_FW_ANIM_OP_MONTAGE_RATE        6   /* h0 montage, f0 rate */
#define AVER_FW_ANIM_OP_MONTAGE_PLAYING     7   /* h0 montage (0 any); returns 1/0 */
#define AVER_FW_ANIM_OP_MONTAGE_STATE       8   /* h0 montage (0 newest); outF[0..3] = position, length, weight, rate; outI[0] = section name hash */

typedef int32_t (AVER_FW_CALL* aver_fw_anim_fn)(int32_t op, int32_t entity, int64_t h0, int64_t h1, int64_t h2,
                                                float f0, float f1, float* outF, int64_t* outI, void* user);
AVER_FW_ABI int32_t aver_fw_set_anim_provider(aver_fw_anim_fn fn, void* user);   /* null clears; always 1 */

AVER_FW_ABI int32_t aver_fw_montage_play(int32_t entity, int64_t montage, int64_t slot, int64_t section, float rate, float blendIn);
AVER_FW_ABI int32_t aver_fw_montage_stop(int32_t entity, int64_t montage, float blendOut);
AVER_FW_ABI int32_t aver_fw_montage_stop_slot(int32_t entity, int64_t slot, float blendOut);
AVER_FW_ABI int32_t aver_fw_montage_jump(int32_t entity, int64_t montage, int64_t section);
AVER_FW_ABI int32_t aver_fw_montage_set_next_section(int32_t entity, int64_t montage, int64_t fromSection, int64_t toSection);
AVER_FW_ABI int32_t aver_fw_montage_set_rate(int32_t entity, int64_t montage, float rate);
AVER_FW_ABI int32_t aver_fw_montage_is_playing(int32_t entity, int64_t montage);
AVER_FW_ABI int32_t aver_fw_montage_state(int32_t entity, int64_t montage, float* outPosLenWeightRate /*4 floats*/, int64_t* outSectionHash);
```
All hashes are `fnv1a64` of the path or name (`Assets.ObjectIdOf` in C#). No provider: every function returns 0 (state leaves outputs untouched), the answers of an entity with nothing to play. `AnimAbi.cpp` is added to `modules/framework/CMakeLists.txt` `add_library(Aver.Framework SHARED ...)` list (one line; GAS may add its own source line there: trivial merge).

Root motion needs no ABI: it is `CRootMotion` fields over the generic scene field ABI (5.1).

## 7. C# API (`scripting/csharp/Aver.Framework`)

New files (no edits to `Native.cs`, `Entity.cs`, `Animation.cs`, `AnimGraph.cs`):

**`AnimNative.cs`**: `internal static class FwAnim` with `[DllImport("Aver.Framework")]` for the nine exports above and `aver_fw_anim_abi_version` (pattern of `TimersEventsNative.cs`; strings are never passed, only hashes).

**`Montage.cs`**
```csharp
public readonly partial struct Entity
{
    public bool PlayMontage(string montage, float rate = 1f, string? section = null, string? slot = null, float blendIn = -1f);
    public bool StopMontage(string? montage = null, float blendOut = -1f);          // null = every montage on this entity
    public bool StopMontageSlot(string slot, float blendOut = -1f);
    public bool JumpToMontageSection(string montage, string section);
    public bool SetMontageNextSection(string montage, string fromSection, string? toSection);   // null restores the authored link
    public bool SetMontageRate(string montage, float rate);                          // 0 pauses
    public bool IsMontagePlaying(string? montage = null);
    public bool TryGetMontageState(string? montage, out float position, out float length, out float weight, out float rate);
    public bool IsInMontageSection(string montage, string section);                  // by hash, via TryGetMontageState's section hash
}
internal static class MontageGraphInterop     // int-entity / primitive signatures for GraphCompiler (AnimGraphInterop pattern, AnimGraph.cs:98)
{
    internal static bool PlayMontageForGraph(int entity, string montage, string section, string slot, float rate, float blendIn);
    internal static bool StopMontageForGraph(int entity, string montage, float blendOut);
    internal static bool StopMontageSlotForGraph(int entity, string slot, float blendOut);
    internal static bool JumpToMontageSectionForGraph(int entity, string montage, string section);
    internal static bool SetMontageNextSectionForGraph(int entity, string montage, string from, string to);
    internal static bool SetMontageRateForGraph(int entity, string montage, float rate);
    internal static bool IsMontagePlayingForGraph(int entity, string montage);
    internal static bool GetMontageStateForGraph(int entity, string montage, out float position, out float length, out float weight);
    internal static bool IsInMontageSectionForGraph(int entity, string montage, string section);
}
```
`PlayMontage` also ensures the entity has `CAnimator` (`AddComponent(Component.Animator)` if absent, exactly as `PlayAnimation` does). Empty-string args mean "not given" for the graph versions.

**`RootMotion.cs`**
```csharp
public enum RootMotionMode { None = 0, Ignore = 1, Apply = 2, Accumulate = 3 }
[Flags] public enum RootMotionAxes { X = 1, Y = 2, Z = 4, Yaw = 8, Planar = X | Y | Yaw }
public readonly partial struct Entity
{
    // Adds CRootMotion if absent; writes scale, axes, sources, then mode last (mode 0 = off).
    public bool SetRootMotion(RootMotionMode mode, RootMotionAxes axes = RootMotionAxes.Planar, float scale = 1f,
                              bool fromMontages = true, bool fromGraph = true);
    public RootMotionMode RootMotion { get; }                       // None when the component is absent
    // Reads the accumulator (cm in this entity's own frame at the start of the period, radians, seconds) and zeroes it.
    public bool ConsumeRootMotion(out Vec3 translation, out float yawRadians, out float seconds);
    public bool PeekRootMotion(out Vec3 translation, out float yawRadians, out float seconds);
}
internal static class RootMotionGraphInterop
{
    internal static bool SetRootMotionForGraph(int entity, int mode, int axes, float scale);
    internal static bool ConsumeRootMotionForGraph(int entity, out float x, out float y, out float z, out float yawDegrees, out float seconds);
}
```
**`Character.cs`** (edit, `Drive` after `SyncFromSimulation()`, before the velocity write): when `_capsule != 0` and `Self.RootMotion == RootMotionMode.Apply`: set `CRootMotion.flags |= 1` (External); `ConsumeRootMotion`; if `seconds > 1e-4`: world displacement = `WalkForward*m.X + WalkRight*m.Y` (the yaw frame, Character.cs `WalkForward`/`WalkRight`) plus `Vec3.Up*m.Z`; set the horizontal capsule velocity to `displacement.xy / seconds` in place of the input `wish` (root motion overrides input while it produces motion), the vertical component only when the Z axis is extracted; `_yaw += yawRadians` in degrees then `ApplyLookRotation()`. When mode leaves Apply or the capsule is destroyed (`OnUnbound`), clear the External bit. Without a capsule (`_capsule == 0`, direct translate path) nothing changes: `AnimSystem` applies to `CLocal` itself.

Scene-field interplay: `Drive` adopts a rotation it did not write (AdoptToleranceDeg), so `AnimSystem` writing `CLocal` yaw on a non-capsule character is also adopted.

## 8. Runtime and packaged-game wiring, save/load

- `Runtime/src/GameApp.cpp` and `sandbox/src/SandboxApp.cpp`: next to the ragdoll provider block (GameApp.cpp:1746-1752, SandboxApp.cpp:549-554) add, inside `#if AVER_MODULE_SCENE && AVER_MODULE_FRAMEWORK`: `aver_fw_set_anim_provider(&anim::montageProvider, &anim::animSystem());` (include `aver/framework/framework_anim_abi.h`). No other host change: montages tick inside `AnimSystem::tick`, which both hosts already call every frame.
- `GameSystemsWiring.hpp`: `registerGameSystems()` also calls `anim::AnimSystem::registerComponents(world)`; `resetGameSystems()` also calls `anim::animSystem().montages().stopAll()` (Play stop / level unload).
- **Order caveat (accepted)**: root motion computed in this frame's `AnimSystem::tick` is consumed by the NEXT frame's `Drive`: one frame of latency for a physics character, none for a plain entity (applied inside the tick).
- **Save/load**: `CRootMotion` (mode, sources, axes, flags, scale; accumulators are plain floats) is saved by the generic component capture like `CAnimGraph`. **Montage players are transient and not saved**: after a load nothing is playing (a save mid-combo resumes at the state machine's pose, same as ragdolls and CAnimGraph instances today). `.ocmontage` ships as ordinary Content (extension-agnostic index).
- Editor outside Play: nothing plays montages, so `AnimSystem::tick` pays two empty-map checks. Paused Play holds each entity's montages through the per-entity `kAnimatorPaused` check.
- D3D12/Vulkan parity: n/a (CPU pose math; the skinning matrices path is untouched).

## 9. Editor

Pattern: `AnimStateMachineEditor.hpp/.cpp` (headless free functions + class + factory, `AVER_WITH_IMGUI` draw guard) and `SnapshotUndo`.

1. **`MontageEditor.hpp/.cpp` (new, `.ocmontage` tab)**: `editor::makeMontageEditor(path)`, registered with one line in `SandboxApp.cpp` (next to :445) and the file added to `sandbox/CMakeLists.txt` (next to :72). Free functions for tests: `montageStarter()` (one empty segment list + `Default` section), `mtgAddSegment/mtgRemoveSegment/mtgMoveSegment`, `mtgAddSection/mtgRemoveSection/mtgRenameSection/mtgSetNext` (renames repair every `next` that named it; removal repairs links to the following section), `mtgAddNotify/mtgRemoveNotify`, `mtgTimeline(asset, clipDuration fn) -> total length + segment boundaries` (ImGui-free). UI: top row slot / blend in-out + curve / rate / additive; a timeline lane drawn from the segment boundaries with draggable section markers and notify markers (snap to 1/30 s), a segments list (drop a `.ocanim` from the Content Browser to append), a details panel for the selected section (name, next: dropdown `[following] [@end] <sections>`), and a simulator like the state-machine tab's: a Play/Pause/scrub bar driving a `MontagePlayer` bound to the real clips, showing position, section, weight, the event log and a "jump to section" button. **No skinned preview in v1** (use the Anim tab per clip; PIE shows the result).
2. **`AnimStateMachineEditor`** (edit): a "Pipeline" collapsible in `drawDetails()` (cpp:626): ordered node list (add slot/layered/additive, move up/down, delete), per node the fields of 3.2, bone names via a combo filled from the first `.ocskel` found with the same resolution as `AnimEditor` (`setAnimEditorContentRoot`), free text fallback. Free functions (header, headless): `asmAddPipelineNode(asset, kind)`, `asmRemovePipelineNode`, `asmMovePipelineNode(asset, index, delta)`, `asmPipelineSlotNames(asset)`, `asmPipelineProblems(asset) -> std::vector<std::string>` (duplicate slot names, empty branches, unknown param). Saving through the existing `saveStateMachine` already writes version 2 when non-empty.
3. **`AnimEditor.cpp`** (small edit): relabel line 2470 "Additive base" -> "Additive"; when "Root motion" is set show the clip's total root delta (`rootDeltaBetween(clip, spec, 0, duration, false)`: "root moves X cm, Y cm, yaw D deg over T s") and add a session-only "Lock root in preview" checkbox that runs `lockRoot` on the preview pose (the preview samples clips directly, `AnimEditor.cpp:1048-1061`).
4. **Content Browser** (`SandboxContentBrowser.cpp`): `cbCreateMontage()` (copy of `cbCreateBlendSpace`, `saveMontage(path, editor::montageStarter())`, extension `.ocmontage`), menu entry beside the others, and a type-label row `{".ocmontage", {ICON_..., "Animation Montage"}}` next to :1741; declaration in `SandboxApp.hpp`. These shared files are append-only for this feature.

## 10. Graph nodes (for the graph-node agent)

Category `"Animation"`, in `GraphNodeDefsGameSystems.hpp` + `GraphGameSystemNodeTable.cs` (the table pattern of `SetAnimGraph`/`TriggerAnim`, `GraphGameSystemNodeTable.cs:44-49`). Exec nodes have pins `exec`, `entity:int`, outputs `then`, `success:bool`. String inputs are NODE-line attributes (`attr`), as `machine=` is for `SetAnimGraph`. The wrapped methods are in `MontageGraphInterop` / `RootMotionGraphInterop` (section 7).

| Node | Pins (besides exec/entity/then/success) | Attrs | Wraps |
|---|---|---|---|
| `PlayMontage` | in `rate:float=1`, `blendIn:float=-1` | `montage`, `section` (optional), `slot` (optional) | `PlayMontageForGraph` |
| `StopMontage` | in `blendOut:float=-1` | `montage` (empty = all) | `StopMontageForGraph` |
| `StopMontageSlot` | in `blendOut:float=-1` | `slot` | `StopMontageSlotForGraph` |
| `JumpToMontageSection` | | `montage`, `section` | `JumpToMontageSectionForGraph` |
| `SetMontageNextSection` | | `montage`, `from`, `to` (empty `to` = authored) | `SetMontageNextSectionForGraph` |
| `SetMontageRate` | in `rate:float` | `montage` | `SetMontageRateForGraph` |
| `IsMontagePlaying` (pure) | `entity` -> out `result:bool` | `montage` (empty = any) | `IsMontagePlayingForGraph` |
| `GetMontageState` (pure) | `entity` -> out `found:bool`, `position:float`, `length:float`, `weight:float` | `montage` | `GetMontageStateForGraph` |
| `IsInMontageSection` (pure) | `entity` -> out `result:bool` | `montage`, `section` | `IsInMontageSectionForGraph` |
| `SetRootMotion` | in `mode:int` (0 None, 1 Ignore, 2 Apply, 3 Accumulate), `axes:int=11`, `scale:float=1` | | `SetRootMotionForGraph` |
| `ConsumeRootMotion` | out `x,y,z:float` (cm), `yaw:float` (degrees), `seconds:float` | | `ConsumeRootMotionForGraph` |

Montage events and notifies need no node: they arrive as graph events by name (`OnEvent`/`CustomEvent`). Layered/additive weights are machine parameters, driven by the existing `SetAnimParamFloat` node. Tests: section 11.

## 11. Tests (all auto-registered: any `*Test` executable under `tests/` is picked up by the walk at the bottom of the root `CMakeLists.txt`; only add the `add_executable`)

**`tests/anim` (link `Aver.Anim Aver.Formats Aver.Core Aver.Platform`, like the existing five)**
- `MontageTest` (`src/MontageTest.cpp`): `.ocmontage` write->parse->write byte-identical, `valid()` refusals, a newer version refused; timeline length with rate/trim segments; section chain: default order, `@end`, loop on self, `setNextSection` override then restore; `jumpToSection` keeps weight and fires `SectionStarted` once; blend-in/out numbers on a fixed dt (linear curve exact; smooth curve midpoint 0.5), `blendOutTrigger` start time, cut when `blendOut == 0`; `stop` early raises `Interrupted` then `BlendOut` then `Ended` exactly once each; instant notifies fire once per crossing (including across a loop seam and across a section jump, none for the skipped span); a notify state opened then `stop` closes with `NotifyEnd`; clip-embedded notify at a segment boundary fires once; `evaluate` leaves untracked bones at `base`; additive montage equals `addPose` by hand.
- `RootMotionTest` (`src/RootMotionTest.cpp`): `rootDeltaBetween` linear translation; yawed root (+90 degrees at t0, +X in the clip -> `move` along the root's own forward, not model +X); loop-seam wrap sums `t0->dur` and `0->t1`; axis mask (Z excluded keeps vertical motion in the pose); flag unset / additive clip / no root track -> empty delta; `lockRoot` zeroes exactly the masked components to rest and leaves rotation swing (pitch/roll) intact; `composeDelta` of a curved path equals the closed form (quarter circle); weighted blending of two layers; state-machine integration: two states with different root speeds mid-blend give the weighted delta, `consumeRootDelta` resets; blend-space integration; a non-flagged clip contributes nothing and is not locked.
- `AnimPipelineTest` (`src/AnimPipelineTest.cpp`): `.ocasm` with no pipeline writes version 1 byte-identical to the pre-change golden (reuse `AnimGraphAssetTest` fixture), with a pipeline writes version 2 and round-trips; `valid()` refusals; `buildBranchMask` (depth ramp values, nearest-ancestor override with a weight-0 carve-out, unknown bone skipped); `blendPoseMasked`; layered `clip` source moves only the branch; additive `firstFrame`/`rest`/`clip` refs vs hand-computed `addPose`; `weightParam` scales; slot nodes call the `SlotEvalFn` and report consumed slots; `rootWeight` (slot 1, layered with root outside the branch 0).

**`tests/anim.scene` (add to `tests/anim.scene/CMakeLists.txt`, same link set as `AnimGraphSystemTest`)**
- `MontageSystemTest`: play on an entity with a skeleton and a plain CAnimator clip: DefaultSlot montage shows in `pose()` and `skinning()`; rest/untracked bones keep the base; second montage in the same slot supersedes and the first raises `Interrupted` and fades; two slots coexist; a pipeline `slot`+`layered` branch (UpperBody) moves only the branch bones while the state machine plays below; unconsumed non-default slot is ignored; events and notifies reach the sink with the exact names of 5.3 (count via `notifiesFired()`); entity destroyed mid-montage delivers `NotifyEnd` + `Ended` once; `clear()` the same; paused animator holds the montage; play on an entity without a skeleton returns false; asset load failure cached (one warn); the provider ops (`montageProvider`) round-trip every op including `STATE` outputs and `0 = newest/any` handles.
- `RootMotionSystemTest`: component registration and `attach` defaults (zero-filled = None changes nothing, pose identical to before); modes: Ignore (pose root locked, entity unmoved), Accumulate (entity unmoved, accumulator grows, clearing by writing zero works), Apply on a yawed 90 degree entity moves along world +Y for a clip moving +X, and yaw delta rotates the entity (`rot * Rz`); `scale` and entity scale; the External flag leaves `CLocal` alone and the 0.5 s cap drops stale accumulation; state-machine path (via `AnimGraphSystem`) and montage path; mix: a flagged montage at coverage 0.5 gives the average of base and montage deltas, an unflagged montage leaves the base; scrub guard (external `CAnimator.time` write produces no delta); CAnimator clip change resets the clock; sources mask (`kRootSrcMontage` only ignores the state machine).

**`tests/framework_anim` (new directory; one `add_subdirectory(tests/framework_anim)` line in the root `CMakeLists.txt` beside `framework_timers`, line ~528; links `Aver.Framework Aver.Core`)**
- `FrameworkAnimAbiTest`: `aver_fw_anim_abi_version() == 1`; no provider: every call returns 0 and `state` leaves outputs untouched; installed fake provider receives the exact `op`, `entity`, hashes and floats for each of the nine functions and its return and out values are relayed; clearing the provider restores the zeros.

**`tests/editor`**
- `MontageEditorTest` (new, compiles `sandbox/src/MontageEditor.cpp` like `AnimGraphEditorTest`, `AVER_WITH_IMGUI` undefined): the `mtg*` edits and the link repair on rename/remove, `mtgTimeline`, load/save/dirty/undo bookkeeping, a corrupt file reports `loadError`. Extend `AnimGraphEditorTest.cpp` with the `asmPipeline*` edits and `asmPipelineProblems`.

**C# (`scripting/csharp/Aver.Graph.Tests`, for the graph-node agent)**: `MontageNodeTests.cs` in the `GameSystemNodeTests` mould (every table row names a real `MontageGraphInterop`/`RootMotionGraphInterop` method matching its pins, defaults, emitted IL valid, exec node refused by the pure compiler, pure reads work, missing attribute fails by name), registered in `Program.cs`.

Verification the main loop runs (not the implementer's tests): headed PIE with a character that plays a two-section montage in an UpperBody slot over locomotion and an attack montage with root motion on a physics character that stays on the ground and stops at a wall.

## 12. Files, partitioned

**Create**
- `modules/anim/include/aver/anim/{RootMotion,Montage,AnimPipeline}.hpp`, `modules/anim/src/{RootMotion,Montage,AnimPipeline}.cpp`
- `modules/anim.scene/include/aver/anim/AnimMontages.hpp`, `modules/anim.scene/src/AnimMontages.cpp`
- `modules/framework/include/aver/framework/framework_anim_abi.h`, `modules/framework/src/AnimAbi.cpp`
- `scripting/csharp/Aver.Framework/{AnimNative,Montage,RootMotion}.cs`
- `sandbox/src/MontageEditor.hpp`, `sandbox/src/MontageEditor.cpp`
- `docs/ANIM_MONTAGES.md` (user-facing summary of sections 3-6 and the clip flag semantics; add a pointer line to `docs/ANIM_BLEND_STATE.md` section 3)
- `tests/anim/src/{MontageTest,RootMotionTest,AnimPipelineTest}.cpp`; `tests/anim.scene/src/{MontageSystemTest,RootMotionSystemTest}.cpp`; `tests/framework_anim/{CMakeLists.txt,src/FrameworkAnimAbiTest.cpp}`; `tests/editor/src/MontageEditorTest.cpp`

**Modify (this feature owns these)**
- `modules/anim/`: `CMakeLists.txt` (3 sources), `Pose.hpp/.cpp`, `AnimSampler.hpp/.cpp`, `AnimStateMachine.hpp/.cpp`, `BlendSpace.hpp/.cpp`, `AnimGraphAsset.hpp/.cpp`
- `modules/anim.scene/`: `CMakeLists.txt` (1 source), `AnimSystem.hpp/.cpp`, `AnimGraphSystem.hpp/.cpp`
- `scripting/csharp/Aver.Framework/Character.cs`
- `Runtime/include/aver/game/GameSystemsWiring.hpp` (two lines), `Runtime/src/GameApp.cpp` and `sandbox/src/SandboxApp.cpp` (the provider line + include, plus the factory registration line in SandboxApp)
- `sandbox/src/AnimStateMachineEditor.hpp/.cpp`, `sandbox/src/AnimEditor.cpp`, `sandbox/src/SandboxContentBrowser.cpp`, `sandbox/src/SandboxApp.hpp` (`cbCreateMontage` decl), `sandbox/CMakeLists.txt` (1 source)
- `tests/anim/CMakeLists.txt`, `tests/anim.scene/CMakeLists.txt`, `tests/editor/CMakeLists.txt` (`MontageEditorTest`, and `AnimGraphEditorTest` already compiles the asm editor), root `CMakeLists.txt` (1 `add_subdirectory` line)

**Shared files, append-only (another wave-2 feature may also touch; whichever lands second merges by keeping both blocks)**
- `modules/framework/CMakeLists.txt` (source list), `Runtime/src/GameApp.cpp`, `sandbox/src/SandboxApp.cpp`, `sandbox/src/SandboxContentBrowser.cpp` and `sandbox/CMakeLists.txt` (data-tables/localization may add create entries and sources), root `CMakeLists.txt`, `GameSystemsWiring.hpp`.

**Owned by other features (do not edit)**: `framework_abi.h` and `FrameworkAbi.cpp` (input/GAS version bumps), `Input*.cs`, `EnhancedInput.cs`, `PlayerController.cs`, GAS files in `Aver.Framework`, `docs/formats/FORMAT_SPECS.md` and `modules/formats` (data tables/localization), `modules/rhi.vulkan` (another agent). Graph-node files (`GraphNodeDefsGameSystems.hpp`, `GraphGameSystemNodeTable.cs`, `GraphCompiler.cs`, `OcGraphParser.cs`, `Aver.Graph.Tests`) belong to the graph-node agent working from section 10.

`modules/anim/module.json` and `modules/anim.scene/module.json` need no change (no new `Aver.*` edge); run `scripts/module-guard-audit.py` after.

## 13. Risks and open questions for the owner

1. **Root yaw assumes a Z-up engine-space skeleton** whose heading is a rotation about +Z. Rigs imported from glTF with a rotated root bone (Armature -90 degrees about X) may extract zero yaw. Fix belongs in the importer; v1 documents it and the editor readout shows the extracted delta so it is visible.
2. **Root lock reference = skeleton rest** (Unreal's default). A clip whose root starts away from rest will pop by that offset when extraction turns on. Alternative (lock to the clip's first frame) is a one-line change in `lockRoot`; say if you prefer it.
3. **One frame of latency** for physics-character root motion (anim tick runs after gameplay). Removing it needs a pre-pass that predicts the delta before `Drive`; deferred.
4. **Montages are not saved**; a load stops them. Saving in-flight montage state (id, section, position) is possible later via the save host seams.
5. **No skinned montage preview** in the editor tab in v1, and clip-embedded notify STATES inside montage segments are ignored (montage-level states work).
6. **Unconsumed non-default slots are ignored** (with one warning), unlike a convenience "apply everywhere" rule. Chosen so a `UpperBody` montage cannot silently play full-body on a graph without that slot.
7. **`.ocasm` version 2 is refused by older builds** when a pipeline is present (version gate in `header()`); pipeline-less assets stay version 1.
8. **Not covered**: root motion for entities moved by a dynamic Jolt body or Synapse agent steering (they overwrite `CLocal`); reverse montage playback; slot groups (UE's per-group interruption); a `machine` layered source; mesh-space layered blend.
9. `modules/framework/CMakeLists.txt` and the two host files are touched by several wave-2 features: serialise the merges (all my edits there are single appended lines).
