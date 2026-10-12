# Motion matching

Parity feature 25 (`docs/plans/UE_FEATURE_PARITY_PLAN.md`). Design contract: `docs/plans/wave5/MOTION_MATCHING.md`.
This page is what ships: formats, runtime behaviour, the script surface, the patent rules and the limits.

Motion matching is one more pose source and one more state-machine state kind. It upgrades the existing
animation systems (`AnimSystem`, `AnimGraphSystem`, the state machine, montages and root motion); there is no
second animation system. It ends in a `Pose`, so D3D12 and Vulkan are untouched.

## 1. How it works

A **pose database** holds one feature row per sampled frame of a set of clips. Every row is a vector of
selected-bone positions and velocities, the root velocity, and a few samples of the root trajectory in the
future and in the past, all in the root's heading frame, normalised so a plain squared distance is the match
cost. Every `interval` seconds (default 0.1) the matcher builds a **query** from what the character is doing
and wants to do, finds the cheapest searchable row, and, when it beats the playing row by `switchMargin`,
jumps to it with **inertialization** (the old output and its velocity decay away over the new source; one pose
is evaluated, nothing is cross-faded).

- **The query.** The trajectory part comes from `TrajectoryPredictor`: a critically damped spring from the
  entity's measured velocity toward the wanted velocity (`desiredVel`), a spring on the heading, and the
  measured history. The pose part is read from the **database row being played**, never from the output
  skeleton (patent rule 4, section 6).
- **Search** is exact. SSE2 brute force with per-row filtering, tag/bias handling and an early-out; above
  `kdMinRows` (default 40,000) searchable rows an exact branch-and-bound KD tree over an 8-D PCA projection
  (the projection is non-expansive, so the result equals the linear scan, ties included: the lower row wins).
- **No thrash.** A switch happens only when `best + switchMargin < currentCost`; a best hit in the same range
  within `continuityWindow` of the playhead is "the current motion" and moves nothing; `acceptCost` keeps a
  valid current pose when even the best hit is poor.

## 2. Assets

### `.ocmotion` (JSON, `"format": "ocmotion"`, `"version": 1`)

```json
{
  "format": "ocmotion", "version": 1,
  "name": "Locomotion", "skeleton": "Skeletons/Character.ocskel", "rootBone": "", "sampleRate": 30,
  "tags": ["idle", "walk", "run", "stop"],
  "features": {
    "bones": [{"bone": "foot_l", "pos": 1.0, "vel": 0.8}, {"bone": "foot_r", "pos": 1.0, "vel": 0.8},
              {"bone": "hips", "pos": 0.5, "vel": 1.0}],
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
    {"clip": "Anims/stop_from_run.ocanim", "loop": false, "tags": ["stop"],
     "ranges": [{"start": 0.0, "end": 0.15, "tags": [], "noSearch": true}]}
  ]
}
```

- Every `bone` names a bone of `skeleton`; `rootBone` empty = the skeleton's root. A weight of 0 drops the
  group. Time lists hold at most 8 values, ascending in magnitude (future > 0, past < 0).
- Rows are taken at `sampleRate` Hz. A looping entry wraps (and continues its root travel across the seam);
  in a non-loop entry past samples before `start` clamp to `start`, and rows whose future horizon passes `end`
  are flagged **Tail**: they still play, they are never candidates.
- `bias` is added to the cost of every row of the entry. `noSearch` (entry or sub-range) keeps frames playable
  but never a candidate. Tags are declared once (at most 32) and used by entries and ranges.
- Clips do **not** need the `kOcAnimRootMotion` flag: a database declares its clips as root-carrying.
- Parsing never calls `valid()`; unknown keys are ignored; a newer version is refused; re-saving is
  byte-identical.

### `.ocmbake` (derived, AVR1 subtype `MMDB`)

A sidecar next to the asset (`Locomotion.ocmotion` -> `Locomotion.ocmbake`), keyed by a hash of the canonical
asset text, the builder version, the skeleton bytes and every clip's bytes. A missing, stale, truncated or
corrupt sidecar is a miss, not an error: the database is rebuilt on a worker thread and the sidecar rewritten
best effort. It ships as ordinary Content, so a packaged game normally loads it without building. Chunk layout
(`MHDR STRT GRPS NORM TRAJ RNGE ROWM FEAT [PCA8 KDTR]`, rows 32-byte aligned) is in `PoseDatabase.cpp`.

## 3. On an entity

### `CMotionMatcher` (standalone)

Registered at runtime like `CAnimGraph`; no C ABI and no new exports: a script drives it by writing fields.
Zero-filled storage is a valid, enabled component.

| Field | Meaning |
|---|---|
| `database` | `.ocmotion` ObjectId; 0 = none |
| `flags` | `kMotionPaused`, `kMotionForceSearch` (script sets, system clears), `kMotionFaceYaw` (else face the velocity) |
| `playRate`, `searchInterval` | 0 reads as 1 / the database default |
| `desiredVelX/Y` | world planar velocity (cm/s) the character wants |
| `desiredYaw` | radians, used with `kMotionFaceYaw` |
| `inputSource` | `kMotionInputScript` (0, the default: a script writes `desiredVel` / `desiredYaw`) or `kMotionInputEntityVelocity` (1: the matcher follows how the entity actually moves, whatever moves it; each tick it writes the measured velocity and heading into `desiredVelX/Y` and `desiredYaw`, so they read as what is used) |
| `velHalfLife`, `turnHalfLife` | spring half-lives; 0 = database default |
| `requireTag0..3`, `excludeTag0..3` | `fnv1a64(tag name)`; 0 = unused |
| OUT `clip`, `clipTime`, `cost`, `switches`, `row`, `sinceSwitch` | what is playing now |

`MotionMatchSystem` (installed by `game::registerGameSystems()`, ticked inside `game::tickAnimGraphs`, so
neither host changes) observes each entity's own motion, builds its query, and for standalone entities runs a
`MotionMatcher`, chained behind the state-machine pose source. A database that is still loading leaves the
entity on its rest pose (or its `CAnimator` clip). At most `setMaxSearchesPerTick` (default 4) interval searches
start per tick, longest-waiting first; forced searches and matchers with nothing playing are never held back.

### A `motionmatch` state in a state machine

`.ocasm` version 3 adds the state kind `"motionmatch"`: `asset` is the `.ocmotion`, `speed`/`speedParam` become
the play rate, and `tagsRequired` / `tagsExcluded` are OR-ed with the entity's own tag slots. The state mixes
with clip, blend-space and sub-machine states through the ordinary transition crossfade, and with montage slots
and the layered pipeline. A motionmatch state never ends: a transition leaving it may not use an exit time
(`valid()` refuses it). An entity whose machine has such a state gets a `CMotionMatcher` (database 0) so a script
can drive the trajectory with the same calls. Files without the new kind keep versions 1 and 2 byte for byte.

### Root motion

The matcher always locks the planar root of its output pose (the mesh stays on the entity). It reports the
extracted delta through `AnimSystem::addRootMotion` when the entity's `CRootMotion` asks for it:
**Simulation** (no `CRootMotion`, or mode None/Ignore): gameplay moves the entity and the matcher only picks
poses that fit the motion it observes. **Animation** (`Apply`/`Accumulate`): the clip's travel moves the entity
or fills the accumulator, exactly as wave-2 root motion; a physics character reads it through
`AverCharacter` (`kRootMotionExternal`). A montage over a matched entity composes unchanged.

## 4. Script surface (`MotionMatching.cs`)

```csharp
entity.SetMotionMatching("Content/Loco.ocmotion", MotionFacing.Velocity, MotionDrive.Simulation, playRate: 1f);
entity.SetMotionInput(worldVelocity, facingYawDegrees);          // every frame
entity.SetMotionTags(new[] { "walk" }, new[] { "stop" });        // <= 4 each, null clears
entity.RequestMotionSearch();
entity.SetMotionTuning(searchInterval: 0.1f, velocityHalfLife: 0.2f, turnHalfLife: 0.15f);
entity.PauseMotionMatching(true);
entity.TryGetMotionState(out var clipHex, out var clipTime, out var cost, out var switches);
entity.IsMotionClip("Anims/walk_fwd.ocanim");  entity.MotionCost;  entity.MotionSwitches;
```

`AverCharacter.MotionHalfLife` (default 0 = today's instantaneous velocity write): when the entity has a
`CMotionMatcher`, the character feeds it its wish and eases the capsule's horizontal velocity toward it with a
critically damped spring of that half-life, so the body and the matcher's predicted trajectory agree and feet do
not slide. An `AverCharacter` always faces its mouse-look yaw, so use `MotionFacing.Yaw` for it.

Graph nodes (Animation category): `SetMotionMatching`, `SetMotionInput`, `SetMotionTags`,
`RequestMotionSearch`, `GetMotionState`, `IsMotionClip` (wrapping `MotionMatchGraphInterop`).

## 5. Editor

`.ocmotion` opens in the **Motion Matching Database** editor (skeleton, feature layout, clips with tags, ranges
and bias, Bake with a build report, and a query lab to see what the search returns). The state-machine editor
has a "Motion matching" state kind. `Show > Motion Matching` draws each matched entity's history (grey),
predicted trajectory (green), the matched row's own trajectory (cyan) and the desired velocity (yellow), with
clip, cost and switch labels.

## 6. Patent rules (binding on whoever changes this code)

Checked live on 2026-10-11 (web search and Google Patents; claim text of the EA documents could not be read in
full, and a negative search is not clearance, so the owner may want counsel to read them):

- EA US 11,562,523 B1 / US 11,995,754 B2 (local bone phases, a motion match per local bone phase),
  US 12,403,400 B2 (periodic autoencoder motion phase manifold), US 9,827,496 B1 (ragdoll pose -> reference
  features -> best capture frame). All active.
- Ubisoft (Clavet GDC 2016, Holden et al. 2020) and Microsoft (Bollo, inertialization): no patent found; public
  techniques.

Therefore: (1) one feature vector, one search; (2) no phase, contact-phase, frequency or amplitude features;
(3) no body-part-partitioned search (an upper/lower body split needs a patent re-check, not a quiet option);
(4) the pose part of the query is read from the database row being played, never from a `Pose` after a modifier,
IK, ragdoll or montage touched it (the ragdoll runs after the matcher and is never read back; getting up from a
ragdoll with matching is a separate, reviewed item); (5) no neural components (learned motion matching is out of
scope). `MotionMatcherTest` carries a guard for rule 4.

## 7. Limits

- One skeleton per database (a different rig needs the retarget asset; the matcher's `MotionClipFn` is where it
  would plug in). No mirroring, no time-warp or foot locking (that is IK's job), SSE2 only (no AVX2 path).
- A parented character is matched in its parent's frame (the local transform); entities moved by a dynamic rigid
  body or AI steering overwrite `CLocal` like wave-2 root motion does.
- Memory is f32: 100k rows x 56 floats = 22 MB per database. Building is about a second per 100k rows; a good
  sidecar makes it instant. `pendingBuilds()` exists so a loading screen can wait on it.
- Search states inside a state machine are not part of the per-tick search budget.

## 8. Tests

`tests/anim`: `MotionAssetTest`, `PoseDatabaseTest`, `PoseSearchTest` (`--bench` prints linear / KD microseconds
for 100k rows), `TrajectoryTest`, `InertializerTest`, `MotionMatcherTest`. `tests/anim.scene`:
`MotionMatchSystemTest`. Editor and graph-node tests live with those tools.
