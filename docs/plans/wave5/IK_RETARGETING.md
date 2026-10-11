# Feature #24: Control Rig + IK Rig + Retargeter

Design contract for the implementing agent. Parity plan: `docs/plans/UE_FEATURE_PARITY_PLAN.md` row 24, wave 5. Nothing here is built. Rules in force: upgrade the existing systems (`modules/anim` Ik, `.ocrig`/`ControlRigSystem`, the pose-modifier seam, the ragdoll builder's limits; no parallel animation system), module boundaries hold (`docs/MODULES.md`, `module.json`), every C# capability gets graph nodes, no new third-party code, lean comments, patent safety (section 13), D3D12/Vulkan parity (nothing here touches a GPU API: it ends in `Pose` and `poseToSkinning`). Format and depth follow `docs/plans/wave2/MONTAGES_ROOT_MOTION.md`, which is built (montages, slots, AnimPipeline, `docs/ANIM_MONTAGES.md`).

## 0. Decisions in one screen

1. **Solvers are pure and live in `modules/anim`**: new `IkSolvers.hpp/.cpp` next to `Ik.hpp` (FABRIK and CCD chains, pole vector, tip rotation, swing-twist joint limits, a chain builder). `twoBoneIk` and `aimAt` stay exactly as they are and are reused (analytic is the right tool for a 3-joint limb).
2. **Three ways in, one engine.** (a) New `.ocrig` v2 **ops** (`fabrik`, `ccd`, `copybone`, `setbone`) with **named runtime inputs** (`INPUT` lines, `@name` values): the "rig graph ops" of the existing Control Rig. (b) A new **IK Rig asset `.ocikrig`** (JSON): chains, priorities, goals, joint limits, body (pelvis) solve and **foot placement** on physics raycasts, attached by a new `CIkRig` component. (c) The **Retargeter** (`.ocretarget`, JSON) reuses the same solvers for `ik`-mode chains.
3. **One pose modifier stays one.** `ControlRigSystem` is still the single `PoseModifierFn` (RagdollSystem chains it, `RagdollSystem.cpp:112`). Order per entity: `.ocrig` ops, then the `CIkRig` stack, then (chained) the ragdoll. Montages and the state machine run before all of it (`AnimSystem.cpp:522` is still the only edit point).
4. **Joint limits reuse the ragdoll convention, not the ragdoll module.** `Aver.Anim` cannot depend on `Aver.Ragdoll` (it depends on Anim). The IK limit record has the ragdoll's exact meaning (swing1, swing2, twist half-angles in degrees about the segment toward the longest child, `physics_ragdoll_abi.h:37-40`). The IK Rig stores them baked; the editor fills them from `ragdoll::buildRagdoll` / a `.ocragdoll` override (the sandbox links both). Runtime never touches the ragdoll module.
5. **Goals are runtime data, not assets.** Script, graph node or an entity socket supplies goal position/rotation and a blended alpha; goals and `.ocrig` inputs are transient (not saved), like montage players.
6. **Foot placement is asymmetric offset IK, not "snap to ground"**: ground higher than the animation assumes always lifts the foot, ground lower only pulls a planted foot (plant weight from a clip curve or foot height), pelvis drops by the lowest foot, all critically smoothed per entity. Ray casts go through a host-installed function pointer (anim.scene has no physics edge).
7. **Retargeting = model-space rotation deltas by chain, resampled across different bone counts, with optional IK for end effectors**, root/pelvis scaled by rest height. Used three ways from the same function: the editor preview, the **batch tool** (writes new `.ocanim`), and an on-demand **runtime bake** cache so a clip authored for skeleton A just plays on skeleton B.
8. **ABI**: new `framework_ik_abi.h` with its own version (`AVER_FW_IK_ABI_VERSION 1`), relayed by a provider exactly like `framework_anim_abi.h`. `AVER_FW_ABI_VERSION_MINOR`, `scene_abi.h` and `Components.hpp` are not touched (both new components are dynamically registered).
9. **No `Pose` model-space type is added.** Every solver does `poseToModel` once, works on arrays of model matrices/positions, and writes `Pose::local` back (the contract `Ik.hpp:12-15` already states).

## 1. What exists today

| Piece | Where | State |
|---|---|---|
| `Pose` = `std::vector<Transform> local`; `poseToModel` (row vector, `model = local * parentModel`) | `modules/anim/include/aver/anim/Pose.hpp:17,30` | Complete. Quat `operator*` is Hamilton (`Math.hpp:99`): `Q_model(i) = Q_model(parent) * Q_local(i)`; `fromQuat(a*b) == fromQuat(b)*fromQuat(a)` (found by a real skeleton, `Ik.cpp:60-71`). No `Quat::inverse`; conjugate by negating xyz (`RagdollBuilder.cpp:201`). |
| `twoBoneIk(skel, pose, root, mid, tip, goal, pole)` analytic; clamps unreachable; false on unlinked chain | `modules/anim/include/aver/anim/Ik.hpp:38`, `Ik.cpp:128` | Built, tested (`tests/anim/src/IkTest.cpp`, 215 lines, incl. `testParentedChainWithRestRotations`). |
| `aimAt(skel, pose, bone, target, localAxis)`, `bonePositionModel` | `Ik.hpp:51,59`, `Ik.cpp:95,119` | Built. |
| `applyModelDelta` (model-space axis/angle -> local quaternion, the sign/order trap) | `Ik.cpp:72` (anonymous namespace) | Private. The new solvers need it: promoted to `IkMath.hpp` (section 3.1). |
| `.ocrig` text v1: `OP twobone`/`OP aim`, literal goals, per-op `weight` | `modules/formats/include/aver/formats/OcRig.hpp:40-90`, `OcRig.cpp:82-157` | Built. No inputs, no references, no FABRIK/CCD, no copy/set ops. The scout's `CopyBone`, `SetBone`, parameter-driven values are recorded as NOT built (`docs/recon/control-rig.md:§4`). |
| `CControlRig{rig, weight}`, `ControlRigSystem` (single modifier), `applyRigOp` (solve on a copy, slerp back by weight), `rigFor`, `findBone` | `modules/anim.scene/include/aver/anim/ControlRig.hpp:30,41,84,88`, `ControlRig.cpp:21,28,82,101,106,131` | Built. Ops are literal and fixed; the only runtime inputs are `CControlRig::weight`. **No dt reaches the modifier** (`PoseModifierFn(e, skel, pose, user)`, `AnimSystem.hpp:53`), so nothing can smooth. |
| Modifier + source seams; ragdoll chains the modifier | `AnimSystem.hpp:53,59,112,117`, `AnimSystem.cpp:417` (source), `:522` (modifier), `RagdollSystem.hpp:26-32`, `RagdollSystem.cpp:89,112` | Built. `AnimSystem::tick(world, dt)` is at `AnimSystem.cpp:216`. |
| Ragdoll limits: `RagdollDef::limitsDeg` (3/bone: swing1 swing2 twist), defaults 45/45/30, `.ocragdoll` `limits` lines | `modules/ragdoll/include/aver/ragdoll/RagdollBuilder.hpp:21-34,59-75`, `RagdollBuilder.cpp:90,224` | Built. Segment direction = toward the longest child (`RagdollBuilder.cpp:184-190`). |
| Composition roots install rig then ragdoll then the montage provider | `Runtime/src/GameApp.cpp:1832-1847`, `sandbox/src/SandboxApp.cpp:600-615` | Built. Per-frame order: `tickAnimGraphs`, `ragdollSystem().tick`, `animSystem().tick` (`GameApp.cpp:2092-2096`, `SandboxApp.cpp:2222-2226`). |
| Physics rays: `aver_phys_raycast_ex(layerMask, ignoreBody)` (point + normal + entity stamp), `aver_phys_raycast_all` | `modules/physics/include/aver/physics/physics_layers_abi.h:87`, `physics_queries_abi.h:122`, `physics_abi.h:570` | Built. Landscape heightfield hits report entity 0 (`physics_abi.h:560-566`). A character is a visible broadphase body. Native code cannot see the C# capsule handle (`ANIM_MONTAGES` section 1). |
| Host-supplied ray function pointer precedent | `AudioScene::setRayFn` (`modules/audio.scene/include/aver/audio/AudioScene.hpp:71`) | Pattern for the ground probe. |
| Skeleton/clip formats: `OcSkeleton{bones, sockets}`, `OcAnimation{skeletonRef, flags, tracks, notifies, curves}`; `skeletonRef` is a file-stem hint, compatibility is by bone INDEX | `modules/formats/include/aver/formats/OcAnim.hpp:52,199`, `sandbox/src/AnimEditor.cpp:958-962` | Built. **No retargeting anywhere**: a clip for another skeleton plays garbage by index (the editor only greys it out, `AnimEditor.cpp:1007`). |
| `AnimSystem::clip(id)` is the one clip loader; consumers: CAnimator (`AnimSystem.cpp`), state machine/pipeline (`AnimGraphSystem.cpp:117,142,157`), montages (`AnimMontages.cpp:127`) | | All resolve clips with no entity or skeleton in the call. |
| `GameContent::pathsWithExtension(ext)`, `adopt(project)` (calls `AnimSystem::clear`) | `Runtime/include/aver/game/GameContent.hpp:37`; adopt call sites `sandbox/src/SandboxProject.cpp:165`, `GameApp.cpp` | Built. Content ids = `fnv1a64(content-relative path)`. |
| Graph node `SetControlRig` + C# `Entity.SetControlRig` (writes `weight` every call: zero-fill trap) | `sandbox/src/GraphNodeDefs.hpp:866`, `scripting/csharp/Aver.Framework/Animation.cs:37`, `GraphInterop.cs:215`, `GraphCompiler.cs:550,1791` | Built. The only script/graph reach into rigs. |
| Montage ABI/graph-node/editor pattern to copy | `framework_anim_abi.h`, `AnimAbi.cpp`, `Montage.cs`, `GraphGameSystemNodeTable.cs:109-118`, `GraphNodeDefsGameSystems.hpp:208-240`, `MontageEditor.cpp`, `SandboxApp.cpp:462`, `SandboxContentBrowser.cpp:738,1293,1927` | Built (wave 2). |
| AnimEditor bone tree is read-only; no rig authoring ("Slice 4: NOT BUILT") | `sandbox/src/AnimEditor.cpp:2348 drawBones`, `docs/recon/control-rig.md:§5` | Missing. |

Missing (this feature): FABRIK, CCD, joint limits in IK, pole vectors beyond two-bone, effector rotation, multi-effector/priority stack, pelvis/body solve, foot placement, raycast access from the anim side, runtime-driven goals, `.ocrig` inputs and references, any C#/graph reach beyond `SetControlRig`, any retargeting (asset, chain mapping, editor, batch, runtime), any rig authoring UI.

## 2. Frame order and data flow (contract)

```
host frame:
  tickGameplay                 (C#: SetIkGoal / BindIkGoal / SetRigInput / SetFootIkWeight ...)
  tickAnimGraphs, ragdoll.tick (unchanged)
  AnimSystem::tick(dt):        sets tickDt_/tickIndex_ first
    per animated entity:
      pose = state machine | montages | clip      (clip via clipFor(id, skeletonId): retarget bake if skeletons differ)
      root motion (unchanged)
      poseMod_ == ControlRigSystem::modifierThunk -> applyTo(e, skel, pose):
          1. .ocrig ops in file order         (inputs resolved: literal | @input | bone:name)
          2. CIkRig stack                      (body/pelvis, chains by priority, feet)
          3. [chained] RagdollSystem::apply   (unchanged)
      poseToSkinning
```
IK therefore layers on montages, state machines, root motion and any future pose source (motion matching included), and ragdoll blending still has the last word.

## 3. `modules/anim` (pure; Core + Formats only)

### 3.1 `IkMath.hpp` (new, private to the module: `modules/anim/src/IkMath.hpp`)

Moves out of `Ik.cpp`'s anonymous namespace (Ik.cpp then includes it; behaviour unchanged, `IkTest` is the proof): `originOf`, `transformDir`, `rotationBetween`, `applyModelDelta`, `linked`. Adds:
```cpp
inline Quat conj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }
// Sets bone's MODEL rotation to `want` by rewriting its LOCAL rotation, given its parent's model rotation.
// local = conj(Q_parent) * want   (Hamilton; see Ik.cpp:60-71 for why this is not the matrix order)
void setModelRotation(const fmt::OcSkeleton&, Pose&, const std::vector<Mat4>& model, u32 bone, const Quat& want);
Quat modelRotation(const Mat4& m);   // normalised rotation of a model matrix (scale removed)
// Shortest arc taking unit a to unit b (identity when parallel; a perpendicular axis when opposed).
Quat arc(const Vec3& a, const Vec3& b);
```
**Test rule (mandatory, from the earlier failure):** every new solver test uses a parented chain with non-identity rest rotations, not only the parentless identity arm.

### 3.2 `IkSolvers.hpp/.cpp` (new)

```cpp
enum class IkSolverKind : u8 { TwoBone = 0, Fabrik = 1, Ccd = 2 };

// Same meaning as RagdollDef::limitsDeg (3 per bone), half-angles. `axis` is the segment direction in the BONE's
// local space (the twist axis); defaultLimitAxis() applies the ragdoll rule. Reference = the bone's REST local rotation.
struct IkJointLimit {
    bool enabled = false;
    f32 swing1Deg = 180, swing2Deg = 180, twistDeg = 180;
    Vec3 axis{1, 0, 0};
};
struct IkChain {                       // root-first, each bone the parent of the next, >= 2 bones
    std::vector<u32> bones;
    std::vector<IkJointLimit> limits;  // empty or one per bone (the tip's entry is ignored)
    bool valid(const fmt::OcSkeleton& s) const;
};
struct IkGoal {
    Vec3 position{0, 0, 0};            // model space, cm
    bool hasRotation = false;
    Quat rotation = Quat::identity();  // desired MODEL rotation of the tip bone
    f32  rotationWeight = 1.0f;
    bool hasPole = false;
    Vec3 pole{0, 0, 0};                // model-space point the middle of the chain bends toward
    f32  poleWeight = 1.0f;
    f32  weight = 1.0f;                // solved on a copy and slerped back per bone (applyRigOp's rule)
};
struct IkParams {
    IkSolverKind kind = IkSolverKind::Fabrik;
    u32 maxIterations = 10;            // FABRIK 10, CCD 12 are the asset defaults
    f32 toleranceCm = 0.1f;
    f32 maxStepDeg = 30.0f;            // CCD: per-joint rotation cap per iteration (damping)
};
struct IkResult { bool solved = false; bool reached = false; f32 errorCm = 0.0f; u32 iterations = 0; };
struct IkScratch { std::vector<Mat4> model; std::vector<Vec3> pos; std::vector<f32> len; std::vector<Quat> frame; Pose copy; };

bool buildChain(const fmt::OcSkeleton&, u32 start, u32 end, IkChain& out);          // walks end up to start; false if start is not an ancestor
void defaultLimitAxis(const fmt::OcSkeleton&, u32 bone, Vec3& axisLocal);           // toward the longest child (RagdollBuilder.cpp:184); +X for a leaf
// Clamps `local` (a bone's local rotation) to the limit about its rest rotation. Returns true when it changed.
bool clampJointRotation(const IkJointLimit&, const Quat& restLocal, Quat& local);
IkResult solveChain(const fmt::OcSkeleton&, Pose&, const IkChain&, const IkGoal&, const IkParams&, IkScratch&);
```
**Dispatch.** `TwoBone` needs `bones.size() == 3` and calls `twoBoneIk` (limits are not applied: documented), the rest below. The solved pose is written to `pose.local` rotations only (translations/scales untouched); the root bone of the chain never translates.

**Common frame.** `poseToModel` once; `pos[i]` = model positions; `len[i]` = `|pos[i+1]-pos[i]|` (held for the whole solve: bones never stretch; asserted in tests to 0.1 cm). `total = sum(len)`. Unreachable (`|goal - pos[0]| > total`): all variants straighten toward the goal and report `reached=false`.

**FABRIK** (Aristidou & Lasenby 2011, academic paper; section 13). Up to `maxIterations`: backward pass (tip placed on goal, each joint pulled to `len` from its successor), forward pass (root restored to its start position, each joint pushed to `len` from its predecessor), stop when `|tip - goal| <= toleranceCm`. **Cone constraints in the forward pass** (the paper's rotational constraint): joint `i` carries a frame `F[i]` (swing-only model rotation, `F[0]` = the root bone's input model rotation). Its cone centre is `c_i = F[i] * (Rr_i * axis_i)` where `Rr_i` is the bone's rest local rotation and `axis_i` the limit axis; the candidate direction `d = normalize(p'[i+1]-p[i])` is clamped by the **elliptic cone** rule below against `c_i` (frame of the cone = `F[i]*Rr_i`), `p[i+1] = p[i] + len[i]*d_clamped`, and `F[i+1] = arc(c_i, d_clamped) * F[i]`. Twist is not part of the iteration; it is clamped in the write-back.

**Elliptic cone clamp.** In the limit frame let `a` be the axis, `e1` = bone-local +Z (+Y when `a` is within 5 degrees of Z), `e2 = cross(a, e1)` (same choice as `physics_ragdoll_abi.h:37-40`, so a limit means the same thing in ragdoll and IK). A direction `a'` at angle `theta = acos(dot(a,a'))` from `a` toward `phi = atan2(dot(a',e2), dot(a',e1))` is allowed up to `thetaMax(phi) = 1 / sqrt((cos phi / s1)^2 + (sin phi / s2)^2)` (s1, s2 in radians). Beyond it `a'' = cos(thetaMax) a + sin(thetaMax)(cos phi e1 + sin phi e2)`. `clampJointRotation` does the same on a full local rotation: `q = conj(Rr) * local`, split `q = swing * twist` about `axis`, clamp the swing with the cone rule and the twist angle to `[-twistDeg, twistDeg]`, return `Rr * swing' * twist'`.

**Write-back (all solvers).** Root to tip: for bone `i` the wanted segment direction `d_i = normalize(pos'[i+1]-pos'[i])`, the current one `c_i = Q_model(i) * segmentLocal_i` (`segmentLocal_i` = normalised translation of the next chain bone, rest local); apply `arc(c_i, d_i)` via `setModelRotation` (twist of the animated pose is preserved: swing-only), then `clampJointRotation` against the bone's limit, then refresh that bone's and its children's model matrices before the next bone. The tip bone is only rotated for `hasRotation`.

**CCD** (Wang & Chen 1991, classical). Per iteration tip-1 down to the root: rotate joint `j` so `(tip - joint)` aligns with `(goal - joint)`, the angle capped by `maxStepDeg`, then `clampJointRotation`, then recompute model matrices of `j`'s subtree in the chain (O(n^2) for n <= 8). Stops at `toleranceCm`. Natural with limits and best for spines/tails.

**Pole vector** (chains with >= 3 joints, FABRIK and CCD): after the position solve and before write-back, roll the intermediate joints about the root-tip axis by the angle that brings the chain's bend (the mean intermediate joint projected perpendicular to the axis) toward the pole's perpendicular projection, scaled by `poleWeight`; segment lengths are preserved (rigid rotation of positions). No pole = the bend the input pose already had (unchanged by the roll).

**Tip rotation.** `hasRotation`: after write-back, slerp the tip bone's model rotation toward `rotation` by `rotationWeight` through `setModelRotation`.

**Weight.** `goal.weight < 1`: solve on `IkScratch::copy`, slerp rotations back per bone (exactly `applyRigOp`, `ControlRig.cpp:51-57`; scaling the goal is wrong, IK is not linear).

### 3.3 `IkRigAsset.hpp/.cpp` (new): `.ocikrig`

JSON, UTF-8, `"format":"ocikrig"`, `"version":1`, written/parsed like `.ocmontage` (floats `%.9g`, parse does not call `valid()`, unknown keys ignored, newer version refused, byte-identical round trip). The JSON string/number helpers are private to `AnimGraphAsset.cpp` today; new private header `modules/anim/src/AnimJson.hpp` carries copies (`esc`, `num`, `str`) for the new assets. **Do not refactor `AnimGraphAsset.cpp` in this feature** (shared-file rule, section 11).

```json
{
  "format": "ocikrig", "version": 1,
  "name": "HeroIk",
  "skeleton": "Characters/hero.ocskel",
  "body": {"bone": "pelvis", "goal": "Hips", "maxOffsetCm": 40},
  "chains": [
    {"name": "Spine",   "start": "spine_01", "end": "spine_03", "solver": "ccd", "priority": 0,
     "goal": "Chest", "iterations": 8, "toleranceCm": 0.5, "maxStepDeg": 20, "limits": true, "tipRotation": true},
    {"name": "LeftArm", "start": "upperarm_l", "end": "hand_l", "solver": "twobone", "priority": 1,
     "goal": "LeftHand", "pole": {"mode": "animated"}, "tipRotation": true},
    {"name": "LeftLeg", "start": "thigh_l", "end": "foot_l", "solver": "twobone", "priority": 2,
     "goal": "", "pole": {"mode": "animated"}},
    {"name": "Tail",    "start": "tail_01", "end": "tail_05", "solver": "fabrik", "priority": 3,
     "goal": "Tail", "iterations": 10, "toleranceCm": 0.1, "limits": true}
  ],
  "limits": [
    {"bone": "lowerarm_l", "swing1": 5, "swing2": 5, "twist": 60},
    {"bone": "calf_l", "swing1": 5, "swing2": 5, "twist": 10, "axis": [0, 0, -1]}
  ],
  "feet": [
    {"chain": "LeftLeg", "toe": "ball_l", "soleOffsetCm": -1, "plantCurve": "",
     "plantLowCm": 2, "plantHighCm": 10, "probeUpCm": 50, "probeDownCm": 60,
     "alignToNormal": true, "maxAlignDeg": 30}
  ],
  "foot": {"pelvisMode": "lowest", "smoothing": 14, "maxPelvisDropCm": 40, "maxFootOffsetCm": 45, "layerMask": 4294967295}
}
```
Semantics (normative):
- `skeleton`: content-relative path (hash = skeleton id). A rig applied to an entity whose skeleton id differs binds anyway by bone NAME and warns once; bones that do not resolve disable their chain (logged once per asset, never an error).
- **chains**: `start`..`end` is the parent path (inclusive); `buildChain` fails -> chain disabled. `solver`: `twobone` (requires exactly 3 bones, else falls back to `fabrik` with a warning), `fabrik`, `ccd`. `goal`: the name a script uses (`SetIkGoal`); empty = no external goal (the chain is driven only by foot placement, or not at all). `limits:true` applies the `limits` list entries for that chain's bones (bones without an entry use `defaultLimitAxis` and 180/180/180, i.e. unlimited). `tipRotation`: also follow the goal rotation. `pole.mode`: `animated` (default: keep the bend side the input pose had), `bone` (`"bone": "name"` = that bone's model position), `vector` (`"dirModel":[x,y,z]` from the mid joint, 50 cm out).
- **priority** (ascending = more important; ties keep file order): chains are solved in that order. A bone solved by a chain with effective weight > 0 is **claimed**; a later chain whose bones overlap claimed ones skips them (its start moves to the first unclaimed bone; its root follows the earlier chain because model matrices are rebuilt before each chain); a chain with nothing left is skipped and listed in the report. This is the multi-effector rule: a hand IK on the weapon (priority 0) wins over a foot IK for a shared spine, and the spine chain (priority 0) is solved before the arms that hang off it.
- **body**: optional pelvis solve. Pelvis translation = (foot-placement shift, section 4.3) + (goal `Hips` offset toward its position scaled by alpha), clamped to `maxOffsetCm`, applied by translating `body.bone`'s local position by the model-space offset converted through its parent. Runs before every chain.
- **limits**: `swing1`, `swing2`, `twist` degrees (ragdoll meaning); `axis` optional bone-local twist axis (default `defaultLimitAxis`).
- **feet**: one entry per leg chain; the chain's `end` is the ankle bone ("foot"). `soleOffsetCm`: ankle height above the sole; `-1` = derive at bind: `restModel(foot).z - groundZ` with `groundZ` = lowest rest model z among foot/toe bones. `plantCurve`: float curve name on the playing clip (0 swing, 1 planted; read through `AnimSystem::curveValue`); empty = automatic (`plant = 1 - smoothstep(plantLowCm, plantHighCm, animatedFootHeight)`). `maxAlignDeg` caps the foot rotation toward the ground normal.
- **foot**: `pelvisMode` `lowest` (v1; `none` disables the pelvis shift), `smoothing` (1/s of the critically damped follow), caps in cm, `layerMask` (bitmask of physics layers the probe may hit; default all).
- `valid()`: unique chain and goal names (case-sensitive), `priority >= 0`, `iterations 1..64`, tolerances/angles finite and `>= 0`, limit values in `[0,180]`, every `feet[].chain` names a chain whose solver is `twobone` or whose `end` is the foot, `soleOffsetCm` finite, probe distances `> 0`.

### 3.4 `Retarget.hpp/.cpp` + `RetargetAsset` (new): `.ocretarget`

```json
{
  "format": "ocretarget", "version": 1,
  "name": "Mannequin_to_Hero",
  "source": "Characters/mannequin.ocskel",
  "target": "Characters/hero.ocskel",
  "root":  {"srcBone": "pelvis", "dstBone": "pelvis", "scaleH": "auto", "scaleV": "auto", "offsetCm": [0, 0, 0]},
  "rootMotion": {"srcBone": "root", "dstBone": "root", "scale": "auto"},
  "unmapped": "rest",
  "chains": [
    {"name": "Spine",    "src": ["spine_01", "spine_03"],  "dst": ["spine_01", "spine_04"], "mode": "fk",   "rotation": "interpolate", "translation": "none"},
    {"name": "Head",     "src": ["neck_01", "head"],       "dst": ["neck_01", "head"],      "mode": "fk"},
    {"name": "LeftArm",  "src": ["upperarm_l", "hand_l"],  "dst": ["upperarm_l", "hand_l"], "mode": "ik",   "anchor": "auto", "scale": "auto", "pole": "fk", "tipRotation": true},
    {"name": "LeftLeg",  "src": ["thigh_l", "foot_l"],     "dst": ["thigh_l", "foot_l"],    "mode": "ik",   "anchor": "root"},
    {"name": "LeftToe",  "src": ["ball_l", "ball_l"],      "dst": ["ball_l", "ball_l"],     "mode": "fk"},
    {"name": "Weapon",   "src": ["weapon_r", "weapon_r"],  "dst": ["gun_r", "gun_r"],       "mode": "fk",   "translation": "scaled"}
  ]
}
```
Semantics (normative):
- `source`/`target`: content-relative skeleton paths. A chain is a parent path `[start, end]` in its skeleton (inclusive; single-bone chains allowed). Chains of one side must not share a bone; `valid(src, dst, why)` checks every name exists, the path is parent-linked, no overlap, and `root.dstBone` is an ancestor-or-self of every mapped target chain start.
- `mode`: `fk` (default), `ik`, `none` (target chain stays at rest relative to its parent). `rotation`: `interpolate` (default; bone counts may differ) or `oneToOne` (counts must match, else validation error). `translation`: `none` (default), `scaled` (local translation delta scaled by the dst/src rest bone length ratio; props, tails). `unmapped`: `rest` (default; keeps target rest local) or `inherit` (rest local, which inherits the parent's retargeted motion either way).
- `root.scaleH`/`scaleV`: `"auto"` or a number. Auto = `dstPelvisRestHeight / srcPelvisRestHeight` where height = pelvis rest model z minus the lowest rest model z of the mapped chain ends of IK chains (the ground), falling back to the pelvis z above the model origin. `rootMotion.scale` auto = `scaleH`.
- `anchor` (ik chains): `root` (goal = `dstGround + (srcTip - srcGround)` scaled by `(scaleH, scaleH, scaleV)` about the model origin; legs and anything that touches the ground), `start` (goal = `dstStart + (srcTip - srcStart) * (dstChainLen / srcChainLen)`; arms), `auto` (default: `root` when the source chain end's rest z is below the pelvis rest z, else `start`). `scale` overrides the factor in `start`/`root`. `pole`: `fk` (default: the FK-transferred middle joint position) or `none`. `tipRotation`: restore the FK tip model rotation after the solve.
- Auto-map (`autoMapChains(src, dst) -> RetargetAsset + confidence list`), pure name logic: normalise each bone name (strip known prefixes `mixamorig:`, `Bip01 `, `Bip001 `, `CC_Base_`, `b_`, `DEF-`, lowercase, split camel/underscore/dot tokens), extract **side** (`l/r/left/right`, as a prefix/suffix token or `.L`/`_L`), a **role** from a synonym table (pelvis=hips=hip; spine/chest/torso; neck; head; clavicle=shoulder (when a `upperarm` also exists); upperarm=arm=uparm; lowerarm=forearm=elbow; hand=wrist; thigh=upleg=upperleg; calf=shin=lowerleg=leg-when-thigh-exists; foot=ankle; ball=toe=toebase; finger names thumb/index/middle/ring/pinky + index) and an **ordinal**. Chains are found from role templates (Spine, Neck+Head, Arm L/R, Leg L/R, Finger chains, Toe) between the matched role bones; chains for rigs the table does not understand (quadrupeds, creatures) fall back to **structural chains** (split the hierarchy at bones with 2+ children and at leaves) paired by `(side, depth order)`. Every proposal carries a confidence (`exact`, `synonym`, `structural`) shown in the editor; the result is a draft, never applied silently.

**Retargeter** (`Retarget.hpp`):
```cpp
struct RetargetIssue { enum class Kind : u8 { Info, Warn, Error } kind; std::string text; };
struct RetargetOptions { f32 fps = 30.0f; f32 rotToleranceDeg = 0.05f; f32 posToleranceCm = 0.05f; bool reduceKeys = true; };
class Retargeter {
public:
    // Resolves every name to indices on the two skeletons, builds the per-chain resampling tables and the
    // auto scales. False + issues when the asset does not fit (missing bone, overlap, bad path).
    bool bind(const RetargetAsset& a, const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst, std::vector<RetargetIssue>* issues = nullptr);
    // srcPose (local, src bone order) -> dstPose (written whole: target rest, then retargeted). Allocation-free after warm-up.
    void retarget(const Pose& srcPose, Pose& dstPose) const;
    f32 scaleH() const; f32 scaleV() const;
    const std::vector<u32>& mappedDstBones() const;
};
```
**Algorithm per frame** (all rotations are Hamilton, `Q_model(i) = Q_model(parent) * Q_local(i)`; use `IkMath` helpers, never hand-rolled local conversion):
1. `srcModel = poseToModel(srcPose)`; rest model rotations `srcRest[]`, `dstRest[]` are precomputed at bind.
2. **FK transfer (every chain, including `ik` chains).** For source bone `s` of chain: `D_s = Q_model(srcPose, s) * conj(srcRest[s])` (a world-space rotation delta; independent of each skeleton's bone axes, which is what makes T-pose vs A-pose and differing bone frames work). For target bone `j` of an M-bone chain against an N-bone source chain, parameter `u_j` = cumulative rest length fraction of `j` along the target chain (centre of segment), the source delta at `u_j` is `slerp(D_s, D_s+1, f)` between the two bracketing source bones by cumulative rest length (N == M with `oneToOne` or equal counts degenerates to `D_s`). Wanted target model rotation `W_j = D(u_j) * dstRest[j]`. Written root-to-tip through `setModelRotation`; unmapped target bones keep `restLocal` (and so inherit).
3. **Root.** `dstPelvisPos = dstRestPelvisPos + (srcPelvisPos - srcRestPelvisPos) * (scaleH, scaleH, scaleV) + offsetCm`, converted to the pelvis's parent-local frame; pelvis rotation by step 2 (a one-bone `fk` chain). **rootMotion** bone (above the pelvis, e.g. a ground `root`): translation delta scaled by `rootMotion.scale`, rotation delta copied; this keeps `kOcAnimRootMotion` extraction (`RootMotion.hpp` `rootDeltaBetween`) valid on the baked clip.
4. **IK chains.** Goal per `anchor` above; pole = FK-transferred middle joint position (`pole:"fk"`); `solveChain` with kind `twobone` for 3-bone target chains, `fabrik` otherwise (no limits); then `tipRotation`. An unreachable goal straightens the limb (shorter-legged target keeps feet at the end of its reach; never stretches).
5. `translation:"scaled"` chains copy `srcLocalPosition - srcRestLocalPosition` scaled by length ratio onto the target's rest local position (after step 2).

**Clip retarget (batch and runtime bake share it):**
```cpp
// Output: Keyframed, Linear, sampled at opt.fps from 0 to in.duration inclusive (a final key at duration),
// one track per mapped target bone (rotation always; translation for the pelvis, rootMotion bone, `scaled`
// chains; no scale). duration/flags (loop, root motion; NOT additive)/curves/notifies/notifyDurations are
// copied verbatim; skeletonRef = the target skeleton's file stem. reduceKeys drops keys whose removal moves
// the value less than the tolerances (rotation measured as angle, position in cm), per track, keeping first/last.
bool retargetClip(const Retargeter&, const fmt::OcAnimation& in, const fmt::OcSkeleton& src, fmt::OcAnimation& out,
                  const RetargetOptions& opt, std::vector<RetargetIssue>* issues);
// Whole-folder driver for the editor panel and the CLI: clips whose skeleton does not match `source` are skipped with an
// issue; additive clips (kOcAnimAdditiveBase) and object clips (kOcAnimObject) are skipped with a warning. Output name
// = <stem><suffix>.ocanim in outDir; never overwrites a file that is not a previous retarget output unless `overwrite`.
struct RetargetBatchResult { u32 written = 0, skipped = 0, failed = 0; std::vector<std::string> files; std::vector<RetargetIssue> issues; };
RetargetBatchResult retargetFolder(const RetargetAsset&, const std::string& srcSkelPath, const std::string& dstSkelPath,
                                   const std::vector<std::string>& clipPaths, const std::string& outDir,
                                   const std::string& suffix, const RetargetOptions&, bool overwrite);
```
`retargetFolder` needs file IO only (Core/Formats/Platform are already linked by tests); it is pure of scene and GPU.

Not in v1: additive clips, blend shapes/morph curves (curves are copied by name, unmapped), retarget poses, scale-by-bone-length for skeletons with different bone counts inside `oneToOne`, fingers beyond what the table matches (structural fallback handles the rest).

### 3.5 `.ocrig` v2 (`modules/formats`, extends `OcRig.hpp/.cpp`)

```
OCRIG 2
NAME HeroReach
# runtime inputs a script sets with SetRigInput; the default applies until it does
INPUT GunTarget vec3 space=world default=0,0,0
INPUT LookAt    vec3 space=world
INPUT Aim       float default=0
OP fabrik root=upperarm_r tip=hand_r goal=@GunTarget pole=bone:elbow_r+0,-30,0 iter=10 tol=0.1 limits=1 weight=@Aim
OP ccd    root=spine_01 tip=head goal=@LookAt iter=12 tol=0.5 step=20 limits=1 weight=0.6
OP copybone dst=ik_hand_l src=hand_r space=model pos=1 rot=1 weight=1
OP setbone  bone=weapon pos=0,0,0 rot=0,0,0,1 space=local weight=1
OP twobone  root=thigh_l mid=calf_l tip=foot_l goal=bone:foot_l+0,0,5 pole=0,50,0 weight=1
```
- Header: reader accepts `OCRIG 1` and `OCRIG 2`; the writer emits `OCRIG 1` (byte-identical to today) unless the data has any `INPUT`, any new op kind, or any `@`/`bone:` reference, then `OCRIG 2` (same version-gate idea as `.ocasm`; v1 readers refuse v2 by the existing "unsupported version" error).
- **Value grammar** for `goal`/`pole`/`at`/`axis`/`pos`/`tiprot`: `x,y,z` (literal, model space cm, as v1), `@name` (an `INPUT` of type vec3/quat; world-space inputs are converted to model space with the entity's inverse world matrix at apply), `bone:name[+x,y,z]` (model position of that bone in the pose as it stands when the op runs, plus a model-space offset). `weight` is a number or `@name` (float input). `tiprot=x,y,z,w` or `@name` (quat input) sets a tip rotation for fabrik/ccd/twobone; `tiprotw=` its weight.
- **New ops**: `fabrik`/`ccd` (`root`, `tip`; the chain is the parent path, `buildChain`), `iter` default 10 (FABRIK) / 12 (CCD), `tol` default 0.1 / 0.5, `step` (CCD, deg), `limits=1` uses the bone-local limits of `CIkRig`'s rig if attached **else** `defaultLimitAxis` with the ragdoll default 45/45/30 (so `limits=1` is never a silent no-op); `copybone` (`dst`, `src`, `space=model|local`, `pos=0|1`, `rot=0|1`; copies from the pose as it stands); `setbone` (`bone`, `pos`, `rot` as xyzw, `space=model|local`; values may be refs). Existing `twobone`/`aim` accept the same refs; their literal syntax is unchanged.
- **Data** (`OcRig.hpp`): `OcRigOpKind` gains `Fabrik = 2, Ccd = 3, CopyBone = 4, SetBone = 5`; `OcRigOp` gains `OcRigRef targetRef, hintRef, rotRef; std::string weightInput; u32 iterations; f32 tolerance, maxStepDeg, rotWeight; bool limits, copyPos, copyRot; bool modelSpace; std::string dst;`; `OcRigData` gains `std::vector<OcRigInput> inputs`.
  ```cpp
  struct OcRigRef { enum class Kind : u8 { Literal = 0, Input = 1, Bone = 2 } kind = Kind::Literal; std::string name; Vec3 offset{0,0,0}; };
  enum class OcRigInputType : u8 { Float = 0, Vec3 = 1, Quat = 2 };
  struct OcRigInput { std::string name; OcRigInputType type = OcRigInputType::Vec3; bool world = true; Vec3 defVec{0,0,0}; Quat defQuat; f32 defFloat = 0.0f; };
  ```
  Literal values keep living in `target`/`hint` so v1 files and tests are untouched. `valid()` additions: input names unique and non-empty; every `@name` names an input of a matching type; fabrik/ccd name root and tip distinct; copy/set name their bones; iterations 1..64; `bone:` offsets finite.

## 4. `modules/anim.scene`

### 4.1 Changes to `ControlRigSystem` (ControlRig.hpp/.cpp)

- `applyRigOp` is generalised: `bool applyRigOp(const fmt::OcSkeleton&, Pose&, const fmt::OcRigOp&, f32 weight, const RigEval& ev)` where `RigEval` resolves refs (`Vec3 vec(const OcRigRef&, ...)`, `f32 scalar(...)`, world->model conversion, current-pose bone positions). The v1 overload stays (empty `RigEval`, literal only), so `ControlRigTest` and `OcRigTest` compile unchanged. New ops use `solveChain` on a copy and the per-bone slerp-back (the existing pattern, `ControlRig.cpp:39-57`).
- **Inputs**: `ControlRigSystem::setInput(scene::Entity, u64 nameHash, const f32* v, u32 n)`, `clearInput(e, hash /*0 = all*/)`. Stored in `std::unordered_map<scene::Entity, Inputs>` (small fixed array of 8 slots of `{hash, f32[4]}`; a 9th distinct name is refused with one warning). Missing input = the rig's declared default. Pruned with the dead-entity sweep in 4.3.
- **Time**: `applyTo` reads `anim_->tickDt()` / `tickIndex()` (4.4).
- `registerComponents` also registers `CIkRig` (4.2); both hosts already call it (`GameApp.cpp:1832`, `SandboxApp.cpp:600`), so no host line changes for registration.

### 4.2 `CIkRig` and `IkRigSystem` (new: `IkRigSystem.hpp`, `IkRigSystem.cpp`; a member of `ControlRigSystem`, not a second modifier)

```cpp
inline constexpr u32 kIkRigFeetOff = 0x1, kIkRigGoalsOff = 0x2, kIkRigBodyOff = 0x4;   // CIkRig::flags
struct CIkRig {
    u64 asset = 0;       // .ocikrig ObjectId
    f32 weight = 0.0f;   // zero-filled reads 0 = off: attach through ControlRigSystem::attachIk or write weight every time
    u32 flags = 0;       // kIkRig*
    u32 reserved[2] = {};
};
static_assert(sizeof(CIkRig) == 24, "CIkRig must be padding-free");
```
Registered dynamically (fields `asset` I64, `weight` F32, `flags` I32, `reserved` I32 read-only; same `b.verify` assert as `ControlRig.cpp:61-68`). `static CIkRig* ControlRigSystem::attachIk(world, e, u64 asset, f32 weight = 1)`.

**Runtime state (transient, per entity, pruned):**
```cpp
struct IkGoalState {          // one per goal name hash
    Vec3 pos; Quat rot; bool hasRot; u8 space;      // 0 world, 1 entity-model
    scene::Entity target; u64 socket; Vec3 off; Quat rotOff; bool bound;   // bound = follows target's socket each tick
    f32 alpha, alphaGoal, alphaRate;                // blended toward alphaGoal at alphaRate/s
    bool script;                                    // false until a script touches it: an untouched goal contributes nothing
    f32 errorCm; bool reached;                      // OUT, last solve
};
struct FootState { f32 offset, plant, pelvis; Vec3 normal; f32 alpha, alphaGoal, alphaRate; bool hadHit; };
```
**Goal resolution per tick** (`bound`): target entity alive and posed -> `anim_->socketModel(target, socket, m)` composed with the target's world matrix and `off/rotOff`; target dead or socket missing -> alpha eases to 0 (the goal is released, not frozen). World goals convert to model space with the entity's inverse world matrix (scale included: model units are skeleton centimetres).

**Solve per entity** (`IkRigRuntime::apply(e, skel, pose, dt)`):
1. Load the rig (cached by id; failure cached once) and the **Bound** form per skeleton id: chains as `IkChain` with limits baked (ragdoll axis rule where the file gives none), foot data (`restUpLocal` = `conj(restModelRot(foot)) * (0,0,1)`, `groundZ`, `soleOffset`), body bone, priority order, claim sets. Cache invalidated by `clear()` and by the skeleton id changing.
2. `weight = CIkRig.weight`; `<= 0` -> nothing, no probes. Result is blended per chain: effective chain weight = `rig weight * goal alpha` (goal chains) or `rig weight * foot alpha` (foot-only chains).
3. Body/pelvis shift (5.3), then chains in priority order with the claim rule, each `solveChain` with its `IkParams`. A chain with both a goal and a foot entry: the goal wins (priority path), the foot entry only supplies the ground offset when the goal alpha is 0 (blended by `1 - alpha`).
4. Publish OUT fields (`errorCm`, `reached`, `FootState`) for `GOAL_STATE`/`FOOT_STATE` queries.

### 4.3 Foot placement (algorithm, normative)

Per foot `f` each tick, model space (cm), `W` = entity world matrix, `s` = its uniform scale:
1. `animFoot` = model position of the foot bone in the incoming pose; `hAnim = animFoot.z - groundZ - soleOffset` (>= 0 when lifted).
2. Probe: origin = `W * (animFoot.x, animFoot.y, groundZ + probeUpCm)`, direction world -Z, distance `probeUpCm + probeDownCm`, mask `layerMask`, ignoring hits whose entity stamp is the entity itself (the host function re-casts past the self hit, 4.5). `groundRef` = `(W * (animFoot.x, animFoot.y, groundZ)).z`. Hit: `delta = (hit.z - groundRef) / s`; no hit: `delta = 0`, `normal = up`.
3. `plant`: the clip curve `plantCurve` through `AnimSystem::curveValue(e, hash)` when named and present, else `1 - smoothstep(plantLowCm, plantHighCm, hAnim)`.
4. **Asymmetric target**: `target = delta > 0 ? delta : delta * plant` (ground above the assumption always lifts a swinging foot so it does not clip a step; ground below only pulls a planted foot), clamped to `+-maxFootOffsetCm`, scaled by the foot alpha.
5. Smoothing: `offset += (target - offset) * (1 - exp(-smoothing * dt))`; the same for the ground normal (slerp). First tick after attach snaps (no rise from zero).
6. **Pelvis**: `pelvisTarget = min(0, min over feet(offset))` (`lowest` mode), clamped to `-maxPelvisDropCm`, smoothed the same way; applied to `body.bone` before the leg solves. Each leg's goal is `animFoot + (0,0,offset)` (NOT shifted by the pelvis drop: the pelvis moves and the leg reaches back down to the target).
7. Leg solve: `IkGoal.position` as above, `pole` = `animated` bend, `solveChain` (twobone for 3-bone legs). Then **foot align**: wanted up = `slerp(up, groundNormalModel, plant)` limited to `maxAlignDeg` from model up; rotate the foot bone by `arc(currentUp, wantedUp)` where `currentUp = Q_model(foot) * restUpLocal`; toe bone untouched (the animation keeps its toe roll).
8. Budget: `ControlRigSystem::maxFootProbesPerTick` (default 128); entities past the budget hold their last offsets for that tick. No probe function installed (editor outside Play, a build without physics) = every foot sees "no hit": offsets ease to 0 and the character animates unchanged.

### 4.4 `AnimSystem` changes (the existing system grows; three additive lines, shared-file rules, section 11)

```cpp
// AnimSystem.hpp public:
f32  tickDt() const { return tickDt_; }          // dt of the tick in progress / the last tick
u64  tickIndex() const { return tickIndex_; }    // ++ per tick(); state with a lifetime uses it to prune
// AnimSystem.hpp private: f32 tickDt_ = 0; u64 tickIndex_ = 0;
// AnimSystem.cpp:216 first lines of tick(): tickDt_ = dt; ++tickIndex_;
```
and the retarget lookup (section 4.6): `const fmt::OcAnimation* clipFor(u64 clipId, u64 skeletonId);` plus `registerRetargets(...)`, used at the `clip(a->clip)` call in `tick` and by `AnimGraphSystem`/`MontageSystem` resolvers.

### 4.5 Ground probe (host function pointer)

```cpp
// ControlRig.hpp
using GroundProbeFn = bool (*)(scene::Entity self, const Vec3& originWorld, f32 distanceCm, u32 layerMask,
                               Vec3& outPoint, Vec3& outNormal, void* user);
void ControlRigSystem::setGroundProbe(GroundProbeFn fn, void* user);   // null clears
```
Host implementation, new header-only `Runtime/include/aver/game/GameIkProbe.hpp` (included by both hosts, `#if AVER_MODULE_PHYSICS`): `inline bool probeGround(...)` calling `aver_phys_raycast_ex(origin, down, distance, layerMask, /*ignoreBody*/ 0, point, normal, &hitEntity)`; if `hitEntity == self` (the character's own broadphase body) it re-casts from `hit + 0.5 cm` along the ray, at most 4 times; `inline void installIkGroundProbe()` = `anim::controlRigSystem().setGroundProbe(&probeGround, nullptr)`. Called next to the ragdoll install block (`GameApp.cpp:1837`, `SandboxApp.cpp:605`). The probe is the only physics contact; `Aver.Anim.Scene` gains no edge.

### 4.6 Retarget runtime (in `AnimSystem`, new `AnimRetarget.hpp/.cpp` in anim.scene)

`RetargetCatalog` keyed by `(fnv1a64(lowercase source skeleton stem), destination skeleton id)` -> `.ocretarget` asset id, filled by `AnimSystem::registerRetargets(const std::vector<std::pair<u64, std::string>>& idAndPath)` (the host passes every `.ocretarget` in content; the header-only JSON fields `source`/`target` are read, the target path hash is the destination skeleton id). `clipFor(clipId, skelId)`: loads the clip; if `clip->skeletonRef` stem (lowercase) differs from the stem of the skeleton file `skelId` resolves to AND the catalog has an entry for (that stem, `skelId`), return the **baked** clip from a cache keyed `(clipId, skelId)` produced by `retargetClip` with default options (one-off main-thread cost, a 2 s clip x 60 bones is far below a millisecond-scale hitch; measured by the implementer with `AVER_HITCH_MS`); otherwise return the plain clip. A failed bake is cached as null and falls back to the plain clip with one warning. `clear()` drops the baked clips (they live beside `clips_`). Callers: `AnimSystem::tick` (skeleton id from `CSkeletalMesh.skeleton`), `AnimGraphSystem` resolvers (capture a pointer to a per-`Instance` `u64 skeleton` refreshed each tick; **if the state machine caches clip pointers at bind, rebind when it changes**, same condition as `inst.pipeSkel`, `AnimGraphSystem.cpp:150-160`), `MontageSystem` bind at play (`AnimMontages.cpp:127`; the entity's skeleton id is known there).

### 4.7 Provider (declared in `IkRigSystem.hpp`, defined in `IkRigSystem.cpp`; signature = `aver_fw_ik_fn`, section 5)

`i32 ikProvider(i32 op, i32 entity, i64 h0, i64 h1, i64 h2, const f32* in, i32 inCount, f32* outF, i64* outI, void* user)`, `user` = `&controlRigSystem()`. Declared in this module so the framework never links Anim.Scene (the ragdoll/montage arrangement). `stopAll`-style reset: `ControlRigSystem::clearRuntime()` (inputs, goals, foot states) called from `game::resetGameSystems()` (`GameSystemsWiring.hpp`, one line, next to `montages().stopAll()`).

## 5. C ABI: `modules/framework/include/aver/framework/framework_ik_abi.h` + `src/IkAbi.cpp`

Conventions of `framework_anim_abi.h`: `int32_t/int64_t/float` only, 1 success / 0 refusal, includes `framework_abi.h` for `AVER_FW_ABI`/`AVER_FW_CALL`, own version, **no `AVER_FW_ABI_VERSION_MINOR` bump**. No provider: every call returns 0 and writes nothing.

```c
#define AVER_FW_IK_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_ik_abi_version(void);

#define AVER_FW_IK_OP_RIG_INPUT_FLOAT 1   /* h0 input name hash, in[0] */
#define AVER_FW_IK_OP_RIG_INPUT_VEC3  2   /* h0 input, in[0..2] */
#define AVER_FW_IK_OP_RIG_INPUT_QUAT  3   /* h0 input, in[0..3] xyzw */
#define AVER_FW_IK_OP_RIG_INPUT_CLEAR 4   /* h0 input (0 = all) */
#define AVER_FW_IK_OP_GOAL_SET        5   /* h0 goal, h1 space (0 world, 1 entity model), in[0..2] position, in[3..6] rotation when inCount == 7 */
#define AVER_FW_IK_OP_GOAL_BIND       6   /* h0 goal, h1 target entity, h2 socket name hash (0 = target origin), in[0..2] offset, in[3..6] rotation offset when inCount == 7 */
#define AVER_FW_IK_OP_GOAL_WEIGHT     7   /* h0 goal, in[0] weight, in[1] blend seconds (<0 = 0.15) */
#define AVER_FW_IK_OP_GOAL_CLEAR      8   /* h0 goal (0 = all): script releases it, alpha blends to 0 */
#define AVER_FW_IK_OP_GOAL_STATE      9   /* h0 goal; outF[0] alpha, outF[1] error cm, outF[2] reached 0/1 */
#define AVER_FW_IK_OP_FOOT_WEIGHT    10   /* h0 foot chain name hash (0 = every foot), in[0] weight, in[1] blend seconds */
#define AVER_FW_IK_OP_FOOT_STATE     11   /* h0 foot chain hash (0 = pelvis); outF[0] offset cm, outF[1] plant, outF[2..4] ground normal */

typedef int32_t (AVER_FW_CALL* aver_fw_ik_fn)(int32_t op, int32_t entity, int64_t h0, int64_t h1, int64_t h2,
                                              const float* in, int32_t inCount, float* outF, int64_t* outI, void* user);
AVER_FW_ABI int32_t aver_fw_set_ik_provider(aver_fw_ik_fn fn, void* user);   /* null clears; always 1 */

AVER_FW_ABI int32_t aver_fw_rig_set_input_float(int32_t entity, int64_t name, float value);
AVER_FW_ABI int32_t aver_fw_rig_set_input_vec3(int32_t entity, int64_t name, float x, float y, float z);
AVER_FW_ABI int32_t aver_fw_rig_set_input_quat(int32_t entity, int64_t name, float x, float y, float z, float w);
AVER_FW_ABI int32_t aver_fw_rig_clear_input(int32_t entity, int64_t name);
AVER_FW_ABI int32_t aver_fw_ik_set_goal(int32_t entity, int64_t goal, int32_t space, float px, float py, float pz,
                                        int32_t hasRotation, float qx, float qy, float qz, float qw);
AVER_FW_ABI int32_t aver_fw_ik_bind_goal(int32_t entity, int64_t goal, int32_t targetEntity, int64_t socket,
                                         float ox, float oy, float oz);
AVER_FW_ABI int32_t aver_fw_ik_set_goal_weight(int32_t entity, int64_t goal, float weight, float blendSeconds);
AVER_FW_ABI int32_t aver_fw_ik_clear_goal(int32_t entity, int64_t goal);
AVER_FW_ABI int32_t aver_fw_ik_goal_state(int32_t entity, int64_t goal, float* outAlphaErrorReached /*3 floats*/);
AVER_FW_ABI int32_t aver_fw_ik_set_foot_weight(int32_t entity, int64_t foot, float weight, float blendSeconds);
AVER_FW_ABI int32_t aver_fw_ik_foot_state(int32_t entity, int64_t foot, float* outOffsetPlantNormal /*5 floats*/);
```
All hashes are `fnv1a64` of the name (`Assets.ObjectIdOf` in C#). Goal and foot ops return 0 when the entity has no `CIkRig` or the rig has no such goal/foot (a goal name the rig does not declare is refused, not stored, so a typo is visible). `IkAbi.cpp` is one appended line in `modules/framework/CMakeLists.txt`. Host install: `aver_fw_set_ik_provider(&anim::ikProvider, &anim::controlRigSystem());` beside the anim provider line (`GameApp.cpp:1847`, `SandboxApp.cpp:615`, inside `#if AVER_MODULE_SCENE && AVER_MODULE_FRAMEWORK`).

## 6. C# API (`scripting/csharp/Aver.Framework`)

New files only (no edits to `Native.cs`, `Animation.cs`, `Entity.cs`):

**`IkNative.cs`**: `internal static class FwIk` with `[DllImport("Aver.Framework")]` for the twelve exports above (pattern `AnimNative.cs`).

**`Ik.cs`**
```csharp
public enum IkSpace { World = 0, Model = 1 }
public readonly partial struct Entity
{
    // CIkRig by NAME; weight is written every call (the zero-fill trap, Animation.cs:25-37). Ensures CAnimator.
    public bool SetIkRig(string rigAsset, float weight = 1f);
    public bool SetIkRigFlags(bool feet = true, bool goals = true, bool body = true);
    public bool SetIkGoal(string goal, Vec3 position, IkSpace space = IkSpace.World);
    public bool SetIkGoal(string goal, Vec3 position, Quat rotation, IkSpace space = IkSpace.World);
    public bool BindIkGoal(string goal, Entity target, string? socket = null, Vec3 offset = default);
    public bool SetIkGoalWeight(string goal, float weight, float blendSeconds = 0.15f);   // 1 engages, 0 releases
    public bool ClearIkGoal(string? goal = null);                                          // null = every goal
    public bool TryGetIkGoalState(string goal, out float alpha, out float errorCm, out bool reached);
    public bool SetFootIkWeight(float weight, float blendSeconds = 0.2f, string? foot = null);   // null = every foot
    public bool TryGetFootIkState(string? foot, out float offsetCm, out float plant, out Vec3 groundNormal);   // null = pelvis
    public bool SetRigInput(string input, float value);
    public bool SetRigInput(string input, Vec3 value);
    public bool SetRigInput(string input, Quat value);
    public bool ClearRigInput(string? input = null);
}
internal static class IkGraphInterop      // int-entity / primitive signatures for GraphCompiler (MontageGraphInterop pattern)
{
    internal static bool SetIkRigForGraph(int entity, string rig, float weight);
    internal static bool SetIkGoalForGraph(int entity, string goal, float x, float y, float z, bool useRotation, float pitch, float yaw, float roll, int space);
    internal static bool BindIkGoalForGraph(int entity, string goal, int target, string socket, float ox, float oy, float oz);
    internal static bool SetIkGoalWeightForGraph(int entity, string goal, float weight, float blendSeconds);
    internal static bool ClearIkGoalForGraph(int entity, string goal);
    internal static bool GetIkGoalStateForGraph(int entity, string goal, out float alpha, out float errorCm, out bool reached);
    internal static bool SetFootIkWeightForGraph(int entity, string foot, float weight, float blendSeconds);
    internal static bool GetFootIkStateForGraph(int entity, string foot, out float offsetCm, out float plant, out float nx, out float ny, out float nz);
    internal static bool SetRigInputFloatForGraph(int entity, string input, float value);
    internal static bool SetRigInputVec3ForGraph(int entity, string input, float x, float y, float z);
    internal static bool ClearRigInputForGraph(int entity, string input);
}
```
`SetIkGoal` before `SetIkGoalWeight`: a goal set with weight never raised has alpha 0 and does nothing; **`SetIkGoal` raises the alpha goal to 1 on first use of a goal** (blend 0.15 s) unless `SetIkGoalWeight` already set it, so the one-line call works; `SetIkGoalWeight(goal, 0)` releases. Strings are never passed to native, only hashes. Empty strings in the graph versions mean "not given".

## 7. Runtime, packaged game and save/load

- Hosts: `controlRigSystem().registerComponents/install` already exist (`GameApp.cpp:1832`, `SandboxApp.cpp:600`); add (a) `game::installIkGroundProbe()` and (b) the IK provider line, inside the existing `#if` blocks; (c) `animSystem().registerRetargets(content_.idsWithExtension(".ocretarget"))` after every `content_.adopt(...)` (`SandboxProject.cpp:165` and the runtime's adopt site). `GameContent` gains `std::vector<std::pair<u64, std::string>> idsWithExtension(std::string_view ext) const;` next to `pathsWithExtension` (`GameContent.hpp:37`; sorted, same loop).
- Content: `.ocikrig`, `.ocretarget`, `.ocrig`, baked `.ocanim` are ordinary Content (extension-agnostic index, `ANIM_MONTAGES` section 1); no pack/exporter change.
- **Save/load**: `CControlRig` and `CIkRig` (asset, weight, flags) are saved by the generic component capture; **goals, rig inputs, foot smoothing state are transient**. After a load, goals read as unset (alpha 0) until the script sets them again; foot state snaps on the first tick (no pop: feet land on the ground in one frame, the same as spawning).
- Edit mode outside Play: no probe hits (no physics bodies built) -> feet follow the animation; rigs with goals set by script do nothing. The editors preview solves themselves (section 8).
- Cost: nothing attached = the existing one component probe in `applyTo` plus one for `CIkRig`. Per attached character: <= 2 rays (feet) + solver arithmetic (a 3-bone chain: two quaternion applies; FABRIK 10 x n). Budgeted by `maxFootProbesPerTick`.
- D3D12/Vulkan parity: n/a (CPU pose math).

## 8. Editor (extend the existing editors; patterns: `MontageEditor`, `AnimStateMachineEditor`, `SnapshotUndo`, headless free functions + `AVER_WITH_IMGUI` draw guard)

Shared preview: **`RigPreview.hpp/.cpp` (new)**: an ImGui-draw-list skeleton view (orbit camera, bones as lines, joints as dots, chain highlighting, goal gizmos as draggable handles on a camera-facing plane) with its camera/projection/picking math ImGui-free and tested. No GPU, no skinned mesh in v1: the AnimEditor's skinned preview is private to `AnimEditor.cpp` (`g_skin`, `AnimSkinFeature`) and is not worth extracting for this.

1. **`IkRigEditor.hpp/.cpp` (new, `.ocikrig`)**: `editor::makeIkRigEditor(path)`. Left: skeleton bone tree (from the asset's `skeleton`, picker for the file); middle: `RigPreview` with chains coloured, goals as handles you drag (live solve with the real `solveChain`/foot code, a "Reach test" that orbits a goal around the chain), a sample clip dropdown and a synthetic ground plane with a height/slope slider for foot placement (the pure foot function with a plane as the probe); right: Chains table (start/end bone combos, solver, priority, iterations, tolerance, pole mode, limits on/off), Limits (per bone swing1/swing2/twist drag fields, axis arrows drawn in the view, **Import from ragdoll** = `ragdoll::buildRagdoll(skel)` `limitsDeg` + a `<skeleton>.ocragdoll` override, hidden when `AVER_MODULE_RAGDOLL` is off, plus a manual "Reset to 45/45/30"), Feet, Body, a Problems list (`ikRigProblems`). Free functions (header, headless): `ikrigStarter(skelPath)`, `ikrigAddChain/RemoveChain/MoveChain`, `ikrigSetPriority`, `ikrigRenameChain` (repairs `feet[].chain`), `ikrigAddLimit/RemoveLimit/ImportLimits(def)`, `ikrigAddFoot/RemoveFoot`, `ikrigProblems(asset, skel) -> std::vector<std::string>` (unknown bones, unlinked chains, overlap with priorities, twobone with != 3 bones, duplicate goals, foot without leg chain).
2. **`RetargetEditor.hpp/.cpp` (new, `.ocretarget`)**: `editor::makeRetargetEditor(path)`. Source/target skeleton pickers; chain mapping table (src start/end and dst start/end bone combos, mode, rotation, translation, anchor, scale), **Auto-map** button (draft + confidence column, never applied until accepted), root panel with the computed `scaleH/scaleV` shown and overridable; two side-by-side `RigPreview` views (source pose from a chosen source clip, retargeted target pose live from `Retargeter::retarget`), a mapped-bone overlay (unmapped target bones drawn in grey); **Batch panel**: a multi-select list of source `.ocanim` (Content Browser drag-and-drop or "all clips for this skeleton" via the same stem scan as `AnimEditor::scanClips`), output folder, suffix (default `_<TargetStem>`), fps, key tolerances, overwrite checkbox, "Retarget N clips", and a results table fed by `RetargetBatchResult`. Free functions: `rtgStarter(srcPath, dstPath)`, `rtgAddChain/RemoveChain/SetBones`, `rtgAutoMap(src, dst)`, `rtgApplyMap(asset, draft, acceptMask)`, `rtgProblems(asset, src, dst)`, `rtgScalePreview(asset, src, dst) -> {scaleH, scaleV}`.
3. **`RigEditor.hpp/.cpp` (new, `.ocrig`)**: the "Slice 4" the recon left open: ops list (add `twobone/aim/fabrik/ccd/copybone/setbone`, reorder, delete), per-op forms with bone combos and value-source pickers (literal | `@input` | `bone:`), the INPUT table, weight sliders, the `RigPreview` showing the op results live on the rest pose or a sample clip, Problems list. Free functions `rigAddOp/RemoveOp/MoveOp/SetValueRef`, `rigAddInput/RemoveInput/RenameInput` (renames repair every `@ref`), `rigProblems`. Saves through `saveOcRig` (v1 when expressible).
4. **`AnimEditor.cpp` (small edit)**: in a skeleton tab only, two buttons under the bones panel ("Create IK Rig for this skeleton", "Create Retargeter from this skeleton") calling `SandboxApp::cbCreateIkRig(skelPath)` / `cbCreateRetarget(srcSkelPath)`; the clip-tab incompatible-clip row (`AnimEditor.cpp:1007`) gains a "Retarget to this skeleton..." action that opens the `.ocretarget` tab if exactly one matches, else the Batch panel. No other change.
5. **Content Browser (`SandboxContentBrowser.cpp`, append-only)**: `cbCreateIkRig`, `cbCreateRetarget`, `cbCreateRig` (copy of `cbCreateMontage`, `SandboxContentBrowser.cpp:738`), menu entries beside `:1293`, type-label rows `{".ocikrig", "IK Rig"}`, `{".ocretarget", "IK Retargeter"}`, `{".ocrig", "Control Rig"}` next to `:1927`; declarations in `SandboxApp.hpp`. Factory registration: three lines beside `SandboxApp.cpp:462`.
6. **CLI**: `AverAssetC retarget <asset.ocretarget> --clips <path|dir>... --out <dir> [--suffix S] [--fps N] [--overwrite]` (new subcommand in `tools/AverAssetC.cpp`, JSON-Lines results per file and the required `{"summary":true,...}` line, the tool's existing contract; links `Aver.Anim`). Thin wrapper over `retargetFolder`.

## 9. Graph nodes (for the graph-node agent; Blueprint parity)

Category `"Animation"`, in `GraphNodeDefsGameSystems.hpp` + `GraphGameSystemNodeTable.cs` (pattern `GraphGameSystemNodeTable.cs:109-118`). Exec nodes have `exec`, `entity:int`, outputs `then`, `success:bool`; strings are NODE-line attributes. The existing `SetControlRig` stays as is.

| Node | Pins (besides exec/entity/then/success) | Attrs | Wraps |
|---|---|---|---|
| `SetIkRig` | in `weight:float=1` | `rig` | `SetIkRigForGraph` |
| `SetIkGoal` | in `x,y,z:float`, `useRotation:bool`, `pitch,yaw,roll:float` (degrees), `space:int` (0 world, 1 model) | `goal` | `SetIkGoalForGraph` |
| `BindIkGoal` | in `target:int`, `ox,oy,oz:float` | `goal`, `socket` (optional) | `BindIkGoalForGraph` |
| `SetIkGoalWeight` | in `weight:float`, `blendSeconds:float=0.15` | `goal` | `SetIkGoalWeightForGraph` |
| `ClearIkGoal` | | `goal` (empty = all) | `ClearIkGoalForGraph` |
| `GetIkGoalState` (pure) | `entity` -> out `found:bool`, `alpha`, `errorCm:float`, `reached:bool` | `goal` | `GetIkGoalStateForGraph` |
| `SetFootIkWeight` | in `weight:float`, `blendSeconds:float=0.2` | `foot` (empty = all) | `SetFootIkWeightForGraph` |
| `GetFootIkState` (pure) | `entity` -> out `found:bool`, `offsetCm`, `plant`, `nx,ny,nz:float` | `foot` (empty = pelvis) | `GetFootIkStateForGraph` |
| `SetRigInputFloat` | in `value:float` | `input` | `SetRigInputFloatForGraph` |
| `SetRigInputVec3` | in `x,y,z:float` | `input` | `SetRigInputVec3ForGraph` |
| `ClearRigInput` | | `input` (empty = all) | `ClearRigInputForGraph` |

Table rows follow the existing shape, e.g.
`Reg("setikgoalweight", GenericKind.Exec, typeof(IkGraphInterop), "SetIkGoalWeightForGraph", "entity:int,weight:float,blendSeconds:float=0.15", "success:bool", "p:entity,@goal,p:weight,p:blendSeconds", "success")` and
`Reg("getikgoalstate", GenericKind.Pure, typeof(IkGraphInterop), "GetIkGoalStateForGraph", "entity:int", "found:bool,alpha:float,errorCm:float,reached:bool", "p:entity,@goal,>alpha,>errorCm,>reached", "found")`.
Retargeting and the batch tool are editor/CLI operations and have no node (a runtime graph has no use for rewriting assets); runtime retargeting needs no node because it is automatic (4.6).

## 10. Tests (all auto-registered: any `*Test` executable under `tests/` is picked up by the root `CMakeLists.txt` walk; only add `add_executable`)

**`tests/anim` (link `Aver.Anim Aver.Formats Aver.Core Aver.Platform`)**
- `IkSolversTest`: for FABRIK and CCD on a **parented chain with rotated rests** (the `IkTest` lesson) and a 5-bone chain: reachable goal lands within `toleranceCm`; segment lengths held to 0.1 cm; unreachable goal straightens at it (`reached=false`, error equals `|goal| - reach`); `weight 0.5` is the per-bone slerp of the weight-1 solve; pole vector puts the bend on the pole side (and the mirrored pole mirrors it); no pole preserves the input bend side; tip rotation reaches within 1 degree at weight 1; **limits**: a 140-degree-limited hinge never exceeds it within 0.5 degree (swing and twist) on both solvers, the elliptic cone (swing1 != swing2) bounds each direction, `clampJointRotation` idempotent, limits off = unconstrained; CCD `maxStepDeg` bounds the first iteration; degenerate inputs (goal at root, zero-length bone, chain not parent-linked, pose/skeleton mismatch) return `solved=false` leaving the pose untouched; `buildChain`; `defaultLimitAxis` equals the ragdoll rule on the `RagdollBuilderTest` fixture; claim/priority ordering is covered in `IkRigSolveTest`.
- `IkRigAssetTest`: write->parse->write byte-identical; `valid()` refusals; newer version refused; unknown keys ignored.
- `IkRigSolveTest` (`IkRigSolver` free function over a `GroundQuery` callback and goal list, no scene): multi-effector with priorities (two chains sharing a spine bone: the lower priority skips the claimed bone and still reaches when it can; a chain with nothing left is reported), body shift from a lowered foot, foot placement numbers: flat ground = zero offset (pose bit-identical), step up lifts a swinging foot, step down only pulls a planted foot, pelvis drops by the lowest foot and caps at `maxPelvisDropCm`, normal alignment capped at `maxAlignDeg`, plant curve vs automatic plant, smoothing step response, no-hit eases to 0, first tick snaps.
- `RetargetTest`: identity retarget (same skeleton) reproduces the pose within 0.01 degree for 20 random poses; scaled skeleton (1.3x): pelvis translation scaled, rotations equal; **T-pose source to A-pose target**: the source's raised arm maps to the same model-space direction on the target; chain resampling 3 -> 4 bones and 4 -> 3 (end bones equal, intermediate monotone); `oneToOne` with mismatched counts is a validation error; IK chain: the target foot lands on the scaled goal within 0.5 cm for a reachable pose, and a shorter-legged target straightens without stretching; `tipRotation`; `translation:scaled`; unmapped bones keep rest; auto-map on three name fixtures (Mixamo, UE mannequin style, Bip01 style) hits the expected pairs and marks structural fallbacks; `retargetClip`: valid `.ocanim`, duration/curves/notifies/loop/root-motion flags copied, additive and object clips refused, key reduction error under the tolerances, `skeletonRef` = target stem, root-motion extraction on the baked clip equals the source's scaled by `scaleH` (reusing `rootDeltaBetween`); `retargetFolder` skip/overwrite rules.
- Extend `IkTest` only to prove `Ik.cpp` behaves identically after the `IkMath.hpp` move (existing cases unchanged).

**`tests/formats`**: extend `OcRigTest`: v1 file parses and re-writes byte-identical (golden), v2 with every new op/INPUT/ref round-trips, `valid()` refusals (unknown input, wrong type, duplicate input), a v2 file is refused by the v1 version check text, `@`/`bone:` grammar errors name the line.

**`tests/anim.scene` (same link set as `ControlRigTest`)**
- Extend `ControlRigTest`: `setInput` drives an `@input` goal; `bone:` refs see earlier ops' results; input default until set; ninth input refused; entity prune.
- `IkRigSystemTest`: `CIkRig` attach/zero-fill (`weight` 0 changes nothing, `attachIk` works); a fake `GroundProbeFn` returning a step: feet and pelvis follow through the real tick, flat probe leaves `pose()` identical to no rig; the probe's self-hit skip contract (a fake that reports the entity itself once); goals set/bind: a hand follows a second entity's socket across ticks, target destroyed releases to alpha 0 with no pop beyond the smoothing; alpha blend timing; `GOAL_STATE`/`FOOT_STATE` outputs; chain order with `RagdollSystem` installed (rig runs first, ragdoll sees the IK pose); montage playing underneath still gets IK; budget (`maxFootProbesPerTick`) holds offsets; `clearRuntime`; the provider (`ikProvider`) round-trips every op including `inCount` 3 vs 7.
- `RetargetSystemTest`: `registerRetargets`, a clip authored for skeleton A plays on an entity with skeleton B through CAnimator, through a state machine and through a montage (pose equals `Retargeter` output at that time), a clip with a matching stem is untouched (same pointer as `clip()`), failed bake falls back once with one warning, cache dropped by `clear()`, hot-registering a second asset.

**`tests/framework_ik` (new directory; one `add_subdirectory(tests/framework_ik)` line in the root `CMakeLists.txt` beside `framework_anim` at line 536; links `Aver.Framework Aver.Core`)**: `FrameworkIkAbiTest`: `aver_fw_ik_abi_version() == 1`; no provider: all zeros and outputs untouched; fake provider receives exact `op`, `entity`, hashes, `inCount` (3 vs 7) and floats for each of the twelve functions, return and out values relayed; clearing restores zeros.

**`tests/editor`** (new targets compile the editor `.cpp` with `AVER_WITH_IMGUI` undefined, like `MontageEditorTest`): `IkRigEditorTest` (all `ikrig*` edits, rename repair, ImportLimits from a `RagdollDef`, problems, load/save/dirty/undo, corrupt file reports `loadError`), `RetargetEditorTest` (`rtg*` edits, auto-map accept mask, scale preview, batch driver against temp files), `RigEditorTest` (`rig*` edits, rename repair, v1 round trip when expressible), `RigPreviewTest` (orbit/projection/pick math).

**C# (`scripting/csharp/Aver.Graph.Tests`, graph-node agent)**: `IkNodeTests.cs` in the `GameSystemNodeTests` mould (every row names a real `IkGraphInterop` method matching its pins and defaults, IL valid, exec node refused by the pure compiler, pure reads work, missing attribute fails by name), registered in `Program.cs`; `Aver.Framework.Tests`: the `Entity` API against the fake provider.

**Verification the main loop runs (not the implementer's tests)**: headed PIE with a character walking over steps and a slope (feet planted, pelvis drops, no foot floating, no pop on stairs) with `--gpu-timing`-free CPU frame time compared with the IK rig off; a hand IK reaching onto a moving crate's socket; the `.ocrig` look-at (CCD spine) following the camera; mannequin walk clip retargeted to a proportionally different rig side by side in the Retarget editor and played in PIE via the runtime bake; batch retarget of a folder, then diff in the AnimEditor.

## 11. Files, partitioned

Slices (dependency order; B and C run in parallel after A, D after B and C): **A** solvers + formats; **B** anim.scene runtime + ABI + C# + nodes; **C** retargeter + bake + tool; **D** editors.

**Create**
- A: `modules/anim/include/aver/anim/{IkSolvers,IkRigAsset}.hpp`, `modules/anim/src/{IkSolvers,IkRigAsset,IkMath.hpp,AnimJson.hpp}`, `modules/anim/include/aver/anim/IkRigSolver.hpp` + `src/IkRigSolver.cpp` (the pure stack: body, chain order/claims, feet over a `GroundQuery` callback); `tests/anim/src/{IkSolversTest,IkRigAssetTest,IkRigSolveTest}.cpp`
- B: `modules/anim.scene/include/aver/anim/IkRigSystem.hpp`, `modules/anim.scene/src/IkRigSystem.cpp`; `modules/framework/include/aver/framework/framework_ik_abi.h`, `modules/framework/src/IkAbi.cpp`; `scripting/csharp/Aver.Framework/{IkNative,Ik}.cs`; `Runtime/include/aver/game/GameIkProbe.hpp`; `tests/anim.scene/src/IkRigSystemTest.cpp`; `tests/framework_ik/{CMakeLists.txt,src/FrameworkIkAbiTest.cpp}`; `docs/ANIM_IK_RETARGET.md` (user-facing summary of sections 3-6 and the limits)
- C: `modules/anim/include/aver/anim/{Retarget,RetargetAsset}.hpp`, `modules/anim/src/{Retarget,RetargetAsset,RetargetAutoMap}.cpp`; `modules/anim.scene/include/aver/anim/AnimRetarget.hpp`, `modules/anim.scene/src/AnimRetarget.cpp`; `tests/anim/src/RetargetTest.cpp`; `tests/anim.scene/src/RetargetSystemTest.cpp`
- D: `sandbox/src/{RigPreview,IkRigEditor,RetargetEditor,RigEditor}.{hpp,cpp}`; `tests/editor/src/{IkRigEditorTest,RetargetEditorTest,RigEditorTest,RigPreviewTest}.cpp`

**Modify (this feature owns these)**
- A: `modules/anim/src/Ik.cpp` (move helpers to `IkMath.hpp`; no behaviour change), `modules/anim/include/aver/anim/Ik.hpp` (comment only if needed), `modules/formats/include/aver/formats/OcRig.hpp`, `modules/formats/src/OcRig.cpp`, `tests/formats/src/OcRigTest.cpp`, `tests/anim/CMakeLists.txt`
- B: `modules/anim.scene/include/aver/anim/ControlRig.hpp`, `modules/anim.scene/src/ControlRig.cpp`, `tests/anim.scene/src/ControlRigTest.cpp`, `tests/anim.scene/CMakeLists.txt`, `Runtime/include/aver/game/GameSystemsWiring.hpp` (one line in `resetGameSystems`)
- C: `modules/anim.scene/src/AnimGraphSystem.cpp` + `.hpp` (resolver skeleton capture only), `modules/anim.scene/src/AnimMontages.cpp` (the one bind lambda), `tools/AverAssetC.cpp` (new subcommand block) and its link line in `tests/formats/CMakeLists.txt`, `Runtime/include/aver/game/GameContent.hpp` + `Runtime/src/GameContent.cpp` (`idsWithExtension`), `sandbox/src/SandboxProject.cpp` (one `registerRetargets` line after `adopt`), `Runtime/src/GameApp.cpp` (same line at its adopt site)
- D: `sandbox/src/AnimEditor.cpp` (the two buttons and the retarget action only), `sandbox/src/SandboxContentBrowser.cpp`, `sandbox/src/SandboxApp.hpp`, `sandbox/CMakeLists.txt`, `tests/editor/CMakeLists.txt`

**Shared files, append-only or single-line (other wave-5 features may also touch; the second to land merges by keeping both blocks)**
- `modules/anim/CMakeLists.txt` and `modules/anim.scene/CMakeLists.txt` (source lists; motion matching adds its own sources), `modules/framework/CMakeLists.txt` (one source line), root `CMakeLists.txt` (one `add_subdirectory`), `Runtime/src/GameApp.cpp` and `sandbox/src/SandboxApp.cpp` (probe install, IK provider, factory lines; motion matching/sequencer may add theirs), `sandbox/src/SandboxContentBrowser.cpp`, `sandbox/CMakeLists.txt`.
- **`modules/anim.scene/include/aver/anim/AnimSystem.hpp` / `AnimSystem.cpp`**: this feature adds ONLY `tickDt()/tickIndex()` plus their two assignments, `clipFor`/`registerRetargets` and the single call-site change at the `clip(a->clip)` line. It does not touch `PoseSourceFn`, `setPoseSource`, the montage/root-motion blocks, or `poseMod_` handling. Motion matching owns any pose-source change; if it needs the entity's skeleton id for its pose database it can read `CSkeletalMesh` itself.

**Owned by other wave-5 features (do not edit)**: `modules/anim` pose database / motion-matching sources, `AnimGraphAsset.*`, `AnimStateMachine.*`, `BlendSpace.*`, `Montage.*`, `AnimPipeline.*` (motion matching may extend them; this feature only reads `AnimGraphSystem` resolvers), `Character.cs`, `modules/formats/include/aver/formats/OcWorld.hpp` (SEQUENCE records), `sandbox/src/SequenceEditor.*`, `Runtime/include/aver/game/LevelSequence.hpp`, `framework_abi.h`, `framework_anim_abi.h`, `modules/rhi.vulkan`. Graph-node files (`GraphNodeDefsGameSystems.hpp`, `GraphGameSystemNodeTable.cs`, `GraphCompiler.cs`, `OcGraphParser.cs`, `Aver.Graph.Tests`) belong to the graph-node agent working from section 9.

`module.json` changes: none. Edges used: Anim (Core, Formats) unchanged; Anim.Scene (Anim, Scene, Core, Formats) unchanged (the physics ray comes through a function pointer installed by the host); Framework unchanged; the sandbox editors link Anim and optionally Ragdoll (already a sandbox dependency). Run `scripts/module-guard-audit.py` and `module-matrix.ps1` after.

## 12. Slices and order of landing

A (solvers, `.ocikrig`, `.ocrig` v2) is frozen first: B and C code against these headers. B ships foot IK end to end before goals (the highest visible value, and the part that exercises the probe/host seam), then goals/inputs/ABI/C#/nodes. C ships `retargetClip` + CLI first, then the runtime bake. D last; `RigPreview` first because three editors share it.

## 13. Patent check (live, 2026-10-11) and risks

**Patents.** FABRIK: web search found no patent, only the academic paper (Aristidou and Lasenby, 2011, Graphical Models) and open-source implementations (Apache-2.0 solver libraries). CCD is classical (Wang and Chen, 1991). Analytic two-bone IK is textbook trigonometry. Name-based / structural chain matching for retargeting: no granted patent found on bone-name matching; one granted retargeting patent surfaced, **US11410402B2** (posture transfer between digital human model skeletons, a prismatic-joint/optimisation method), which is a different technique from the delta-rotation chain transfer used here (claims not read). Motion-matching patents are out of scope for this feature (feature #25 owns that check). This is a search result, not a freedom-to-operate opinion; the design uses the published, unencumbered forms and does not copy Epic's code.

**Risks and open questions for the owner**
1. **Control Rig is an op list, not a node-graph editor.** `.ocrig` stays a text list with named inputs and references (Blueprint-style visual authoring of rigs is out of scope); the new `RigEditor` is a form editor over the list. Say if you want a graph editor later.
2. **FABRIK limits are approximate**: cone constraints in the iteration, twist only in the write-back. Fine for arms/tails/spines, not a replacement for full-body position-based IK. The multi-effector rule is sequential-with-claims, not a simultaneous solve; spines pulled by two hands at once will favour the higher priority.
3. **Quat/matrix convention trap** (Hamilton product vs row-vector matrices; the earlier IK shipped a pose wrong by 1,766 cm that no identity-rest fixture caught). The doc makes parented, rotated-rest fixtures mandatory in every new test.
4. **Foot IK assumes the animation was authored on flat ground at the entity origin** (`groundRef`). A character whose root is offset from its feet needs `groundZ`/`soleOffsetCm` overridden in the asset. The capsule is not moved by the pelvis drop (the physics character owns the entity); big drops past `maxPelvisDropCm` are clamped, not compensated.
5. **Ground probe contract is host-side**: layer mask and self-hit skipping live in `GameIkProbe.hpp`. A project that puts characters and floors on custom layers needs `layerMask` set in the rig. Edit mode outside Play has no hits by design.
6. **Retarget quality**: proportion differences are handled by scaling and IK, not by per-bone-length corrections inside FK chains; fingers/twist bones depend on the name table or manual mapping; additive clips, morph curves and retarget poses are not supported in v1. Auto-map is a draft with a confidence column, never silent.
7. **Runtime retarget is on by default** when a catalog entry matches the clip's skeleton stem and the entity's skeleton (baked once per clip/skeleton pair, held in memory until `clear()`); `skeletonRef` is a stem hint, so two different skeletons that share a file stem would be treated as the same. Say if it should be opt-in per project.
8. **Goals/inputs are not saved** (transient like montage players); `CIkRig` weight zero-fill trap handled by `attachIk` and `SetIkRig` writing weight every call.
9. **Not covered**: IK on entities moved by dynamic Jolt bodies beyond the normal pose-modifier path, distance LOD for IK (only a probe budget in v1), skinned previews in the three new editors (the AnimEditor's skin feature is private), look-at/aim constraints across multiple bones beyond `aim` and CCD spine chains, physics-driven foot sliding correction, retargeting of `CControlRig` rigs themselves.
