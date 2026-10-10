# Physics wave 1b graph nodes

Visual scripting reaches C# parity, Blueprint style: the wave 1 physics APIs (ragdolls, shape queries,
breakable joints, body locks and CCD, character can-stand) each have a node. Nodes are coarse on
purpose; anything finer is C#.

Units follow the engine contract: centimetres, +X forward, +Y right, +Z up. Angles on these nodes are
degrees (yaw/pitch/roll, the same convention as Set Local Rotation). Handles ride on INT pins; a body
handle and a joint handle are not interchangeable.

Where each piece lives: pins are in `sandbox/src/GraphNodeDefs.hpp` (palette) and
`scripting/csharp/Aver.Graph/OcGraphParser.cs` (`AddDefaultPins`), emitters in `GraphCompiler.cs`, the
calls into the engine in `scripting/csharp/Aver.Framework/GraphInterop.cs`. Tests:
`scripting/csharp/Aver.Graph.Tests/PhysicsWave1bNodeTests.cs`.

## Ragdolls (category Scene)

All four take the skinned entity and report `success` = false when the entity is dead or the host has no
ragdoll support. The C# API is `Entity.StartRagdoll` and friends (`Aver.Framework/Ragdoll.cs`).

| Node | Inputs | Notes |
|---|---|---|
| Ragdoll Start | entity, mode, blend | mode 0 Kinematic, 1 Limp, 2 Powered. Blend is seconds. Calling it on a live ragdoll changes its mode. |
| Ragdoll Stop | entity, blend | Blends back to the animation, then removes the ragdoll. 0 removes it at once. |
| Set Ragdoll Strength | entity, strength | 0..1, only matters while Powered. |
| Ragdoll Impulse | entity, x, y, z | World-space impulse in kg*cm/s. Bone is the NODE-line attribute `bone=` (a string, so not a pin); empty pushes the root part. |

An unwired `mode` pin reads 0, which is Kinematic: wire a Const Int 1 for a limp ragdoll.

## Shape queries (category Physics)

Exec nodes, cached per visit like Sphere Cast: one native call when the exec walk reaches the node, then
every output pin is readable downstream. They cannot be used from a graph with no ENTRY (`Compile()`
refuses them, naming the node).

| Node | Shape inputs | Outputs |
|---|---|---|
| Box Cast | cx, cy, cz, hx, hy, hz, yaw, pitch, roll, dirX, dirY, dirZ, maxDist | hit, body, distance, pointX/Y/Z, normalX/Y/Z |
| Capsule Cast | cx, cy, cz, radius, height, yaw, pitch, roll, dirX, dirY, dirZ, maxDist | same |
| Overlap Box | cx, cy, cz, hx, hy, hz, yaw, pitch, roll | count, body |
| Raycast All | originX/Y/Z, dirX/Y/Z, maxDist | count, body, distance, pointX/Y/Z |
| Bodies At Point | x, y, z | count, body |

Every query also takes the filter pins `layerMask`, `ignoreBody`, `includeSensors`:

- `layerMask` 0 means all layers. An unwired int pin reads 0 and a mask matching nothing helps no one.
- `ignoreBody` 0 ignores nothing.
- Sensors are skipped unless `includeSensors` is true.

Things to know:

- A cast's direction need not be unit length; the sweep is `dir` normalised times `maxDist`. A zero
  direction or non-positive `maxDist` is a clean miss (`hit` false) with no native call.
- The cast origin is the shape's centre. Capsule `height` is TOTAL, both caps included, and the capsule
  stands along +Z before the rotation. The physics module refuses a capsule no taller than its caps
  (reported as a miss).
- `distance` is how far the shape's centre (the ray, for Raycast All) travelled before touching, in cm.
  `normal` points back toward the cast shape.
- A cast that starts already overlapping something and moves out of it reports no hit (Jolt's default
  back-face handling; the older Sphere Cast behaves the same).
- Overlap Box and Bodies At Point count DISTINCT bodies, capped at 256. Raycast All counts HITS, nearest
  first, so a ray through a body twice counts it twice; `body`, `distance` and `point` describe the
  nearest.
- Not exposed: Body Cast and Overlap Body (they take a body's own shape at another pose), Overlap
  Capsule, and the maxResults argument. Use C# for those.

## Joints and bodies (category Physics)

| Node | Inputs | Notes |
|---|---|---|
| Set Joint Breakable | joint, maxForceN, maxTorqueNm | Newtons and newton-metres (SI, unlike the cm-based forces elsewhere). 0 for a threshold means never check that part; both 0 disarms. A broken joint is disabled, not removed; Joint Set Enabled repairs it. False for a rack-and-pinion joint. |
| Set Locked Axes | body, lockX, lockY, lockZ, lockRotX, lockRotY, lockRotZ | WORLD axes, engine convention. Unwired flags are false (unlocked). Dynamic bodies only; locking all six is refused (use a static body). The body keeps its mass. |
| Set CCD | body, enabled | Continuous collision for a fast small body. Off by default. |

Break events are not a node: poll `Physics.DrainJointBreaks()` from C#, or watch the joint's value.

## Character Can Stand (category Character)

A pure read, usable from both compilers. Inputs: entity, radius, halfHeight. Outputs: `canStand`,
`success`.

It asks whether a capsule of that radius and HALF height would fit with the feet where they are now.
Nothing changes; use it before leaving a crouch. `success` is false when the entity has no live character
(no AverCharacter yet, or its capsule has not been created), so a missing capsule is not read as "blocked".
Other characters and sensors do not count as blocking; up to 1 cm of overlap is accepted.

## Not added

Getters such as "Has Ragdoll" and "Get Ragdoll Mode", the six-DOF per-axis joint motor, soft joint limits
and motor springs, velocity caps, allow-sleeping, set-sensor, and the character contact list. The C#
surface has them; they are one node each if wanted.
