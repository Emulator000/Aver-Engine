# Ragdolls

A ragdoll is a skeleton of capsule parts joined by swing-twist limits. `aver_phys_ragdoll_*`
(`modules/physics/include/aver/physics/physics_ragdoll_abi.h`, implemented in
`modules/physics/src/PhysicsRagdoll.cpp`, C# in `scripting/csharp/Aver.Physics/Ragdoll.cs`) builds one
from a world-space pose and drives it in one of three modes. It is Jolt's `Ragdoll` behind the engine's
plain-C ABI; nothing Jolt-shaped crosses the boundary.

Units and axes are the engine's: centimetres, +X forward, +Y right, +Z up, left-handed, quaternions
`xyzw`, degrees for the joint limits, kilograms. Impulses are kg*cm/s.

## Building one

```c
int32_t rd = aver_phys_ragdoll_create(boneCount, parents, worldPos, worldRot,
                                      capsules, limitsDeg, totalMassKg, userLayer);
```

Everything is per bone, bones ordered parents first (`parents[i] < i`, or `-1`):

| array | size | meaning |
| --- | --- | --- |
| `worldPos` / `worldRot` | 3n / 4n | the pose to build in |
| `capsules` | 7n | `ax ay az bx by bz radius`: a segment in the **bone's** frame, cm. `radius <= 0`: no part |
| `limitsDeg` | 3n | `swing1 swing2 twist` half-angles of the joint to the nearest ancestor part |

Only a bone with a radius gets a rigid body. The others ride rigidly on their nearest ancestor part and
have no body; a bone with neither a part nor an ancestor part keeps its target pose. Joints, limits and
masses live on the reduced skeleton of parts, and a part's joint sits at its **bone's origin**.

* **Mass** is `totalMassKg` shared by capsule volume (`pi r^2 L + 4/3 pi r^3`). Jolt's `Stabilize()`
  is not applied: it would flatten the mass ratios the caller asked for.
* **Joint frame**: the twist axis is the capsule segment (the bone's local X for a sphere). `swing1`
  swings toward the bone's local +Z (toward +Y when the segment lies along Z), `swing2` toward the other
  perpendicular. Limits are symmetric and clamped to 0..180.
* **Collision**: parts of one ragdoll never collide (a `GroupFilterTable` with every pair off; the
  ragdoll handle is its collision group, so two ragdolls always collide). Against the world, every part
  is on `userLayer` (0..15, see `physics_layers_abi.h`).
* **Handles**: each part is an ordinary body handle (`aver_phys_raycast`, contacts,
  `aver_phys_set_entity`, `aver_phys_body_position` all work). `body_of_bone` and `of_body` map between
  bones and handles.

## Modes

| mode | parts | joints |
| --- | --- | --- |
| `KINEMATIC` (0, the start) | moved to the target pose; push dynamic bodies | off |
| `LIMP` (1) | simulated | free within limits, light friction |
| `POWERED` (2) | simulated | motors pull each part toward its target orientation **relative to its parent part** |

* A new ragdoll is kinematic with the creation pose as target.
* Kinematic motion is a velocity that reaches the target within a step, recomputed every fixed step by
  a `JPH::PhysicsStepListener` (registered only while a ragdoll exists), so parts stop when the target
  stops changing. `set_target` also moves them between steps and wakes sleepers.
* Leaving kinematic keeps the velocity the parts had: an animated body keeps its momentum into the fall.
* `POWERED` does not drive the root part. The pose is held; the body is free. A caller that wants it
  anchored joins the root to the world (`physics_joints_abi.h`) or feeds `set_target` from where the
  root has gone.
* `set_strength` (0..1, default 0.5) sets the motors' spring frequency (2 + 8 s Hz, critically damped)
  and torque ceiling (`20 s` times the torque that holds the joint's subtree out horizontally), so one
  value reads the same for a child and a giant. 0 is no pull at all.
* `set_pose` teleports, zeroes velocities, and makes the pose the target in every mode.
* `get_pose` reads the parts; a bone without a part is `ancestor part now * (ancestor part target)^-1 *
  bone target`.
* `add_impulse` is refused in kinematic mode (nothing to push).

## Lifetime

* `destroy` removes the ragdoll and its parts. Joints you made on a part must be removed first, as for
  any body.
* `aver_phys_remove_body(part)` destroys the whole ragdoll first (`releaseRagdollOfBody`), so no joint
  outlives its parts; that call then finds its own handle already gone and returns 0.
* `aver_phys_shutdown` destroys every ragdoll before the world (`destroyAllRagdolls`).

## C#

```csharp
var bones = new[] {
    new RagdollBone(-1, Float3.Zero, new Float3(50, 0, 0), 8f, 60f, 60f, 20f),
    new RagdollBone( 0, Float3.Zero, new Float3(50, 0, 0), 8f, 60f, 60f, 20f),
};
var rd = Ragdoll.Create(bones, positions, rotations, totalMassKg: 30f, userLayer: 0);
rd.Mode = RagdollMode.Powered;
rd.SetTarget(animPositions, animRotations);   // every frame, from the animation
```

## Tests

`tests/physics/src/RagdollTest.cpp` checks observed motion through the C ABI only: limp falls, lands and
rests joined; kinematic reaches a moved target and does not drift; a root-pinned chain sags less when
powered than limp, and not at all better at strength 0; own parts never touch while another ragdoll's do;
mass follows volume; riders stay rigid with their part; impulses land as impulse / mass; the layer
reaches every part; destroy, `remove_body` and shutdown leave nothing behind; bad input is refused with
the right `aver_phys_last_error`.
