# IK Rig, Control Rig inputs and Retargeting

Three ways to bend a pose after the animation has played, and one way to move a clip between skeletons. They share
one set of pure solvers in `Aver.Anim` and one pose-modifier seam in `Aver.Anim.Scene`. Design contract:
`docs/plans/wave5/IK_RETARGETING.md`.

| Asset | Extension | What it is |
|---|---|---|
| IK Rig | `.ocikrig` (JSON) | Chains with solvers, priorities and goals, joint limits, a pelvis solve and foot placement. Attached to an entity by `CIkRig`. |
| Control Rig | `.ocrig` (text, `OCRIG 1` / `OCRIG 2`) | A short list of ops: `twobone`, `aim`, and in v2 `fabrik`, `ccd`, `copybone`, `setbone`, with named runtime inputs. Attached by `CControlRig`. |
| IK Retargeter | `.ocretarget` (JSON) | Which chains of a source skeleton drive which chains of a target skeleton, and how. |

## Frame order

```
state machine | montages | clip   ->  root motion  ->  pose modifier (ControlRigSystem):
      1. the entity's .ocrig ops, in file order
      2. its CIkRig stack: pelvis shift, chains by priority, foot placement
      3. (chained) the ragdoll
  ->  skinning
```
IK therefore layers on anything that produces a pose, and ragdoll blending still has the last word.

## Solvers (`modules/anim/include/aver/anim/IkSolvers.hpp`)

* `twoBoneIk`, `aimAt`: unchanged, analytic.
* `solveChain` with `IkSolverKind::Fabrik` or `Ccd` over a parent path (`buildChain`), with a goal position, an optional
  tip rotation, an optional pole point and a blend weight. Only local rotations are written; the root bone of a chain
  never translates; bones never stretch (an out-of-reach goal straightens the chain at it and reports `reached = false`).
* Joint limits use the ragdoll's convention: swing1 / swing2 / twist half-angles in degrees about the segment toward the
  longest child. The swing is bounded by the elliptic cone. FABRIK applies the cone inside its iteration and the twist at
  write-back, so FABRIK limits are approximate; CCD applies both exactly, bone by bone.
* A pole vector rolls the intermediate joints about the root-tip axis. With no pole the bend side of the input pose is kept.

## The IK Rig asset

```json
{ "format": "ocikrig", "version": 1, "name": "HeroIk", "skeleton": "Characters/hero.ocskel",
  "body":  {"bone": "pelvis", "goal": "Hips", "maxOffsetCm": 40},
  "chains": [
    {"name": "LeftLeg", "start": "thigh_l", "end": "foot_l", "solver": "twobone", "priority": 2,
     "goal": "LeftFoot", "pole": {"mode": "animated"}},
    {"name": "Spine", "start": "spine_01", "end": "spine_03", "solver": "ccd", "priority": 0,
     "goal": "Chest", "iterations": 8, "toleranceCm": 0.5, "maxStepDeg": 20, "limits": true, "tipRotation": true} ],
  "limits": [{"bone": "calf_l", "swing1": 5, "swing2": 5, "twist": 10, "axis": [0, 0, -1]}],
  "feet":   [{"chain": "LeftLeg", "toe": "ball_l", "soleOffsetCm": -1, "plantCurve": "",
              "plantLowCm": 2, "plantHighCm": 10, "probeUpCm": 50, "probeDownCm": 60,
              "alignToNormal": true, "maxAlignDeg": 30}],
  "foot":   {"pelvisMode": "lowest", "smoothing": 14, "maxPelvisDropCm": 40, "maxFootOffsetCm": 45,
             "layerMask": 4294967295} }
```

* Bones are named and bound to the entity's skeleton by NAME at first use (a rig authored for another skeleton still
  binds; chains whose bones do not resolve are disabled and listed once in the log).
* `priority`: ascending is more important. Chains solve in that order. A bone a chain has rotated is **claimed**; a later
  chain that overlaps claimed bones starts from the first unclaimed bone below them, or is skipped (and reported) when
  nothing is left. A hand IK on a weapon (priority 0) therefore beats a foot chain sharing the spine.
* `goal` names a target a script drives (below). A chain with no goal is driven only by foot placement, or not at all.
* `twobone` needs exactly three bones; anything else falls back to FABRIK with a log line.

### Foot placement

Each tick, per foot: a ray is cast **down** from above the animated ankle through a host-supplied probe. The target is
asymmetric: ground **above** the animation's assumed ground always lifts the foot; ground **below** only pulls a foot
that is planted (plant weight from the clip float curve named by `plantCurve`, or automatically from the ankle's height
above its rest standing height). Offsets are clamped, critically smoothed (`smoothing`, 1/s) and snap on the first tick.
The pelvis drops by the lowest foot (`pelvisMode: lowest`), and the legs reach back to their targets. The foot is tilted
by the ground's tilt (blended by plant weight, capped by `maxAlignDeg`) as a delta on the animated orientation, so toe-off
and swing pitch survive. Flat ground at the assumed height leaves the pose bit-identical.

The assumed ground is the model origin plane (or the lowest foot/toe rest height when that is below it), so animations
authored on flat ground at the entity origin just work; a character whose root is far from its feet needs
`soleOffsetCm` set in the rig. Outside Play there are no physics bodies: every foot sees no ground and the character
animates unchanged. At most `ControlRigSystem::maxFootProbesPerTick` rays (default 128) are cast per tick; entities past
the budget hold their last offsets.

The ground probe is installed by the host (`game::installIkGroundProbe()` in `GameIkProbe.hpp`): a downward
`aver_phys_raycast_ex`, stepping past the character's own body. `Aver.Anim.Scene` has no physics edge.

## Driving it from a script

```csharp
actor.SetIkRig("Rigs/HeroIk.ocikrig");                 // weight is written every call (the zero-fill trap)
actor.SetIkGoal("LeftFoot", position);                  // engages over 0.15 s the first time
actor.BindIkGoal("LeftFoot", crate, socket: "Handle");  // follows the crate's socket; lets go if the crate dies
actor.SetIkGoalWeight("LeftFoot", 0f, 0.5f);            // release over half a second
actor.SetFootIkWeight(0f);                              // every foot off (blend 0.2 s); name a leg chain for one foot
actor.TryGetIkGoalState("LeftFoot", out var alpha, out var errorCm, out var reached);
actor.TryGetFootIkState("LeftLeg", out var offsetCm, out var plant, out var groundNormal);   // null = the pelvis
actor.SetRigInput("LookAt", point);                     // a named input of an OCRIG 2 rig
```
Goals, rig inputs and foot smoothing are **not saved**: after a load they read as unset until the script sets them
again. A goal name the rig does not declare is refused (the call returns false), so a typo is visible. The graph nodes
are listed in the design doc, section 9. The C ABI is `framework_ik_abi.h` (`AVER_FW_IK_ABI_VERSION 1`), relayed by
`anim::ikProvider`.

## Control Rig v2 (`.ocrig`)

```
OCRIG 2
NAME HeroReach
INPUT GunTarget vec3 space=world default=0,0,0
INPUT Aim float default=0
OP fabrik root=upperarm_r tip=hand_r goal=@GunTarget pole=bone:elbow_r+0,-30,0 iter=10 tol=0.1 limits=1 weight=@Aim
OP copybone dst=ik_hand_l src=hand_r space=model pos=1 rot=1 weight=1
OP setbone bone=weapon pos=0,0,0 rot=0,0,0,1 space=local weight=1
```
A value is a literal (`x,y,z`, model space cm), an input (`@name`; world-space inputs are converted with the entity's
inverse world matrix) or a bone's position as the pose stands when the op runs (`bone:name+x,y,z`). The writer emits
`OCRIG 1` byte-identically unless the data needs v2. `limits=1` uses the entity's `CIkRig` limits when it has one, else
the ragdoll defaults (45/45/30). A ninth distinct input name on one entity is refused.

## Retargeting

**Method.** Every source bone's model-space rotation *delta* from its rest pose (`Q_pose * conj(Q_rest)`) is a world-space
rotation, independent of either skeleton's bone axes. A target chain bone takes the delta of the source bone at the same
fraction along the chain (cumulative rest length; bone counts may differ), applied to its own rest rotation after a
swing-only alignment of the two rests' bone directions. That alignment is the automatic retarget pose: a T-pose source arm
raised 90 degrees raises an A-pose target arm to the same direction. The pelvis translation is scaled by the ratio of the
pelvises' rest heights above the ground (auto) or by `root.scaleH` / `scaleV`; a root bone above the pelvis moves by its own
scaled delta. IK chains (legs, usually) are solved to goals from the source, so feet land where scaled feet land, and a
shorter-legged target straightens its legs instead of stretching.

**The asset** (`.ocretarget`): `source`, `target` (content-relative skeleton paths), `root`, `rootMotion`, `unmapped` and
`chains` (`src` / `dst` parent paths, `mode` fk | ik | none, `rotation` interpolate | oneToOne, `translation` none |
scaled, `anchor` auto | root | start, `scale`, `pole`, `tipRotation`). **Auto-map** (`autoMapChains`) proposes chains from
bone-name logic (prefix stripping, side and role words, a synonym table) and falls back to structural chains for rigs it
does not understand; every proposal carries a confidence (exact, synonym, structural) and is a draft.

**Three uses of one function:**

1. The Retarget editor previews it live.
2. **Batch**: the editor's Batch panel and the CLI write new `.ocanim` files:
   `AverAssetC retarget Retargets/Mannequin_to_Hero.ocretarget --clips Animations/Mannequin --out Animations/Hero [--suffix _Hero] [--fps 30] [--overwrite]`
   (one JSON line per clip; clips for another skeleton, additive clips and object clips are skipped; a file that is not a
   previous retarget output is never overwritten without `--overwrite`).
3. **Runtime**: when the project holds a `.ocretarget` whose source stem matches a clip's `skeletonRef` and whose target is
   the entity's skeleton, `AnimSystem::clipFor` bakes the clip once (kept until `AnimSystem::clear()`) and every consumer
   (CAnimator, state machines, montages) plays the bake. A clip already authored for the entity's skeleton is untouched.
   Matching is by file stem: two different skeletons sharing a stem are treated as the same.

Baked clips are keyframed (default 30 fps, key reduction at 0.05 degrees / 0.05 cm), carry the source's duration, loop and
root-motion flags, curves and notifies, and name the target stem as `skeletonRef`.

## Limits

Not in v1: additive clips, morph curves, explicit retarget poses (the rest alignment is automatic), per-bone-length
corrections inside FK chains, distance LOD for IK (only the probe budget), skinned previews in the new editors, physics
foot-slide correction, retargeting of control rigs themselves. FABRIK limits are approximate (see Solvers); the multi-effector
rule is sequential with claims, not a simultaneous solve.
