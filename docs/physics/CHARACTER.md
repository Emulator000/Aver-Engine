# Character additions

The character is Jolt's `CharacterVirtual`: a capsule that is swept and resolved rather than
simulated. These additions live at the end of `physics_character_abi.h` (C#: `CharacterBody`). All
units and axes are the engine's.

## Character vs character

`aver_phys_set_character_vs_character(on)` / `aver_phys_character_vs_character()`; C#
`CharacterBody.CharacterVsCharacter`.

A `CharacterVirtual` is not in the broad phase, so two of them meet only if the module tests them
against each other. Every character is registered with one `JPH::CharacterVsCharacterCollisionSimple`.
**On by default.** With it on, a character walking into another stops at it, reads the other's
velocity as a moving obstacle, and can stand on it. With it off they pass through each other.

A single character is unaffected either way, which is why the default could be on: the list holds only
the character itself and it is skipped.

The collision list is brute force (each character query tests the others in turn), right for a handful
of characters, not for a crowd.

Reset to the default by `aver_phys_shutdown`, so set it after `aver_phys_init`.

## Inner body

`aver_phys_character_set_inner_body(ch, on)`, `aver_phys_character_has_inner_body(ch)`,
`aver_phys_set_character_inner_body_default(on)`; C# `HasInnerBody`, `InnerBodyByDefault`.

The inner body is a kinematic rigid body with the character's capsule, moved with it, on the default
moving layer (user layer 0). It is why a raycast, a sensor or a falling crate sees a character. Every
character had one already; this makes it optional.

* The default decides what **new** characters get.
* `set_inner_body` on an existing character **rebuilds** it, because Jolt fixes the inner body at
  construction. The new character keeps the handle, position, rotation, velocity, shape, slope,
  mass, strength, entity stamp and edge-removal setting (via `GetCharacterVirtualSettings`); ground
  state is found again with `RefreshContacts`. Stair, gravity and carried-velocity settings live in
  tables keyed by the handle and are untouched. Calling it with the state the character already has does
  nothing.
* There is no per-character layer; the inner body uses the default moving layer.

### Why character movement ignores inner bodies

An inner body is an ordinary body as far as a character's own sweep goes, so two characters would
otherwise meet twice: once through each other's inner body (a kinematic body one step stale, with no
velocity) and once through the collision list. All character queries therefore run with a body filter
(`characterBodyFilter()`, `PhysicsCharacter.cpp`) that skips every character's inner body. The
character-vs-character switch alone decides whether characters collide, independent of inner bodies.

`aver_phys_character_ground_body` still names a character you are standing on (found through the
contact list, since Jolt keeps no body id for it).

## Can it stand?

`aver_phys_character_can_set_shape(ch, radius, halfHeight)`; C# `CanSetShape(radius, height)`.

Answers whether a capsule fits, changing nothing. The two numbers are centimetres: `halfHeight` is
the distance from the capsule's centre to its top (caps included), i.e. half the TOTAL height that
`aver_phys_character_set_shape` takes. The C# wrapper takes the total height and halves it.

It tests the capsule with the **feet** where the character's feet are now. `set_shape` swaps the capsule
around the character's position (its centre), so growing it in place sinks it into the floor and
`set_shape` would refuse every time; the question a game asks is "can I stand here", so the query anchors
the feet. To apply a yes: move the position up by the change in half height, then `set_shape` with
`halfHeight * 2`.

It is a plain shape query against the broad phase, not a `SetShape` and restore, so it disturbs neither
the shape nor the active contacts. Static and dynamic bodies count; sensors and other characters do
not. Up to 1 cm of overlap is accepted (a resting capsule sits a padding away from the floor).
Returns 0 with `aver_phys_last_error` `Unsupported` for "would not fit" and `InvalidArgument` for
numbers that cannot form a capsule.

## Contacts

`aver_phys_character_contact_count(ch)`, `aver_phys_character_contact_get(ch, index, outBody,
outPoint3, outNormal3)`; C# `ContactCount`, `GetContact(i)`.

From `GetActiveContacts`, rebuilt each fixed step. A contact is listed if it actually collided, or is
within half a centimetre, and is not a sensor or discarded; predictive contacts further away are left
out. `outBody` is the handle of the body touched, or of the other character, or 0 for geometry with no
handle (a heightfield). The normal is a unit vector pointing from the thing touched toward the character
(up for a floor). Any out-pointer may be null.

## Enhanced internal edge removal

`aver_phys_character_set_edge_removal(ch, on)`, `aver_phys_character_edge_removal(ch)`,
`aver_phys_set_character_edge_removal_default(on)`; C# `EdgeRemoval`, `EdgeRemovalByDefault`.

Discards ghost contacts at the internal edges of triangle meshes, so a character crosses mesh seams
without catching, for a little extra work per query. **On by default.** It only acts on triangle edges;
on boxes, spheres and other primitives it changes nothing, which is why the existing tests are not
affected. If a mesh level ever shows a cost, `aver_phys_set_character_edge_removal_default(0)` turns it
off for new characters.

## Where the code is

| Piece | File |
| --- | --- |
| ABI | `modules/physics/include/aver/physics/physics_character_abi.h` |
| Features, state, filter | `modules/physics/src/PhysicsCharacter.cpp` |
| Seam to the world | `modules/physics/src/PhysicsCharacterInternal.hpp` |
| Create/destroy/step hooks | `modules/physics/src/PhysicsWorld.cpp` |
| C# | `scripting/csharp/Aver.Physics/CharacterBody.cs`, `Native.Character.cs` |
| Test | `tests/physics/src/CharacterExtTest.cpp` |
