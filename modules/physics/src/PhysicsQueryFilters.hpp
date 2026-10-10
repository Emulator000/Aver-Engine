// PhysicsQueryFilters.hpp -- the filter plumbing every spatial query shares: the layer mask, the body
// skip, and the Jolt-id -> handle reverse lookup.
//
// SRC-LOCAL, like PhysicsInternal.hpp: it names JPH:: types. These are the three helpers that used to
// sit in PhysicsLayers.cpp's anonymous namespace, lifted unchanged so PhysicsQueries.cpp reuses them
// rather than growing a second copy; QueryBodyFilter is the one new piece (sensor handling).
#pragma once
#include "PhysicsInternal.hpp"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace aver::physics::detail {

// Accepts an object layer whose USER half is in the caller's mask. The broad-phase half is not
// consulted: a caller filtering by layer is asking about game categories, not about whether a body
// happens to be static.
class MaskLayerFilter final : public JPH::ObjectLayerFilter {
public:
    explicit MaskLayerFilter(uint32_t mask) : mask_(mask) {}
    bool ShouldCollide(JPH::ObjectLayer l) const override {
        return (mask_ & (1u << Layers::userOf(l))) != 0u;
    }
private:
    uint32_t mask_;
};

// The body handle behind a Jolt id, searching characters too -- a character's inner body is created
// and owned by Jolt, so it is never in g_world->bodies. byId answers the common case in one lookup.
inline int32_t handleForBodyId(const JPH::BodyID& id) {
    if (const auto it = g_world->byId.find(id); it != g_world->byId.end()) return it->second;
    for (const auto& [h, ch] : g_world->characters)
        if (!ch->GetInnerBodyID().IsInvalid() && ch->GetInnerBodyID() == id) return h;
    return 0;
}

// The Jolt id to skip for a caller's `ignoreBody`, or an invalid id for "skip nothing". A handle that
// does not resolve means "nothing to ignore", not an error: the usual way to get one is the handle of
// a body destroyed a moment ago, and failing the query would turn that into a weapon that stops firing.
inline JPH::BodyID ignoredIdOf(int32_t ignoreBody) {
    if (ignoreBody == 0) return JPH::BodyID();
    if (const JPH::BodyID* id = findBody(ignoreBody)) return *id;
    if (JPH::CharacterVirtual* ch = findCharacter(ignoreBody)) return ch->GetInnerBodyID();
    return JPH::BodyID();
}

// Skips up to two bodies (the caster and the caller's extra ignore) and, unless asked, every sensor:
// a trigger volume is not something a bullet or a probe should stop on.
class QueryBodyFilter final : public JPH::BodyFilter {
public:
    QueryBodyFilter(const JPH::BodyID& a, const JPH::BodyID& b, bool includeSensors)
        : a_(a), b_(b), sensors_(includeSensors) {}
    bool ShouldCollide(const JPH::BodyID& other) const override { return other != a_ && other != b_; }
    bool ShouldCollideLocked(const JPH::Body& body) const override { return sensors_ || !body.IsSensor(); }
private:
    JPH::BodyID a_, b_;
    bool sensors_;
};

} // namespace aver::physics::detail
