// PhysicsCharacterInternal.hpp -- the seam between PhysicsWorld.cpp (which creates, steps and destroys
// characters) and PhysicsCharacter.cpp (which owns the extras: character-vs-character collision, the
// body filter, the inner-body and edge-removal defaults).
//
// SRC-LOCAL for the same reason PhysicsInternal.hpp is: it names JPH:: types. Kept apart from that
// header only so the character work touches no file outside its own set; it can fold into it later.
#pragma once
#include "PhysicsInternal.hpp"

#include <Jolt/Physics/Body/BodyFilter.h>

namespace aver::physics::detail {

// Registers a freshly built (or rebuilt) character: joins the character-vs-character set, remembers its
// inner body, and applies the current character-vs-character switch. Defined in PhysicsCharacter.cpp.
void characterCreated(JPH::CharacterVirtual* c);
// The reverse, called BEFORE the character is released.
void characterDestroyed(JPH::CharacterVirtual* c);

// Defaults a new character is built with (aver_phys_set_character_*_default). Settings, not state, so
// they are read at creation only.
bool characterInnerBodyDefault();
bool characterEdgeRemovalDefault();

// The body filter every character query runs with. It hides the characters' INNER bodies from character
// movement: those bodies exist so rays, sensors and rigid bodies can see a character, and two characters
// meeting through them AND through CharacterVsCharacterCollisionSimple would collide twice.
// aver_phys_set_character_vs_character decides whether characters meet at all.
const JPH::BodyFilter& characterBodyFilter();

} // namespace aver::physics::detail
