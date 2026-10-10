// PhysicsCharacter.cpp -- the character-tuning half of the ABI: slope, stairs, richer ground state,
// shape swapping (crouch) and the mass/strength a CharacterVirtual uses to push the world around.
//
// physics_abi.h's own Character section (in PhysicsWorld.cpp) only reaches create/destroy/velocity/
// position/grounded -- a capsule built from a radius and a height with every other Jolt setting left
// at its default. This file is that gap, in its own translation unit and its own header
// (physics_character_abi.h) because physics_abi.h is not this change's to edit.
//
// SAME SHAPE AS PhysicsBody.cpp: resolve the handle, refuse a dead one, convert units, call Jolt. What
// earns a comment below is only where that is not the whole story -- the ground-body handle lookup that
// has to check two tables, stair stepping having nowhere on CharacterVirtual to live, and SetShape's
// failure being the feature rather than a bug to hide.
#include "aver/physics/physics_character_abi.h"
#include "Convert.hpp"
#include "PhysicsInternal.hpp"
#include "PhysicsCharacterInternal.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <cmath>
#include <memory>
#include <unordered_map>
#include <unordered_set>

using namespace aver;
using namespace aver::physics;
using namespace aver::physics::detail;

namespace {

// ---- Stair stepping side table -----------------------------------------------------------------
//
// NOWHERE ELSE FOR THIS TO LIVE. WalkStairs and StickToFloor take a step-up and a step-down as
// arguments to ExtendedUpdate, not as persistent state on CharacterVirtual -- there is no Jolt-side
// getter or setter this could forward to, unlike every other function below. This table is that
// missing piece of state, keyed by the same handle as everything else in this module.
//
// DEFAULTS MIRROR JOLT'S OWN: ExtendedUpdateSettings ships mWalkStairsStepUp{0,0.4,0} and
// mStickToFloorStepDown{0,-0.5,0} -- 40cm and 50cm once converted -- so a character that never calls
// the setter reads back the same numbers the engine's own step loop already runs with today, not zero.
struct StairSettings {
    float stepUpCm = 40.0f;
    float stepDownCm = 50.0f;
};

// CLEARED ON SHUTDOWN through detail::clearCharacterStairSettings(), called from aver_phys_shutdown
// beside the water table's own clear -- character handles restart at 1 on the next aver_phys_init, so
// without that a new world's first character would silently inherit whatever the previous world's
// handle 1 last set here.
std::unordered_map<int32_t, StairSettings> g_stairSettings;

// ---- Character extras: state ----------------------------------------------------------------------
//
// File-local because World (PhysicsInternal.hpp) is not this feature's to grow; reset by
// clearCharacterStairSettings, which aver_phys_shutdown already calls.
bool g_charVsChar = true;          // aver_phys_set_character_vs_character
bool g_innerBodyDefault = true;    // aver_phys_set_character_inner_body_default
bool g_edgeRemovalDefault = true;  // aver_phys_set_character_edge_removal_default
// Every live character, for CharacterVirtual::SetCharacterVsCharacterCollision. A brute-force list: each
// character query tests the others in turn, which is right for a handful of characters.
std::unique_ptr<JPH::CharacterVsCharacterCollisionSimple> g_cvc;
// The characters' inner bodies, for the filter below.
std::unordered_set<JPH::BodyID> g_innerIds;

// Character movement never collides with a character's INNER body: the inner body is there for rays,
// sensors and rigid bodies. Characters meet each other through g_cvc (when it is on), and meeting through
// both would be two contacts for one touch, with the second one a step stale.
class CharacterFilter final : public JPH::BodyFilter {
public:
    bool rejectSensors = false;   // the can-stand query asks about solid geometry only
    bool ShouldCollide(const JPH::BodyID& id) const override { return g_innerIds.find(id) == g_innerIds.end(); }
    bool ShouldCollideLocked(const JPH::Body& body) const override { return !(rejectSensors && body.IsSensor()); }
};
CharacterFilter g_filter;

// Keeps the deepest overlap of a shape query.
class DeepestOverlap final : public JPH::CollideShapeCollector {
public:
    void AddHit(const JPH::CollideShapeResult& r) override { if (r.mPenetrationDepth > depth) depth = r.mPenetrationDepth; }
    float depth = -1.0e9f;
};

// The handle of the character with this Jolt id, or 0.
int32_t handleOfCharacterId(const JPH::CharacterID& id) {
    if (id.IsInvalid()) return 0;
    for (const auto& [h, other] : g_world->characters)
        if (other->GetID() == id) return h;
    return 0;
}

// A contact worth reporting: touching, not merely predicted, and not a sensor volume.
// Within Jolt's default character padding (2 cm): a character being pushed along moves off by about that
// much each step before the pusher catches up, so a tighter bound drops a contact that is really there.
bool touching(const JPH::CharacterContact& c) {
    return !c.mIsSensorB && !c.mWasDiscarded && (c.mHadCollision || c.mDistance <= 0.025f);
}

// The handle behind a Jolt body id, checking both bodies AND characters' inner bodies -- the same
// two-table lookup aver_phys_raycast already needs in PhysicsWorld.cpp, and for the same reason: the
// only broadphase-visible things this module ever creates are addBody()'s bodies and a character's own
// inner body, and the inner body was never registered in g_world->bodies because Jolt creates and owns
// that BodyID itself.
int32_t handleOfBody(const JPH::BodyID& id) {
    if (id.IsInvalid()) return 0;
    const auto it = g_world->byId.find(id);
    if (it != g_world->byId.end()) return it->second;
    for (const auto& [h, other] : g_world->characters)
        if (!other->GetInnerBodyID().IsInvalid() && other->GetInnerBodyID() == id) return h;
    return 0;
}

// Lookup with the three-way error shape of findVehicle (PhysicsVehicle.cpp). detail::findCharacter
// leaves the slot alone, so this wraps it rather than replacing it.
JPH::CharacterVirtual* character(int32_t h) {
    if (!g_world) { setAbiError(AbiError::NotInitialised); return nullptr; }
    JPH::CharacterVirtual* c = findCharacter(h);
    setAbiError(c ? AbiError::Ok : AbiError::BadHandle);
    return c;
}

// An argument refusal: the 0 return stays, the reason goes on aver_phys_last_error().
int32_t refuse(AbiError e) { setAbiError(e); return 0; }

} // namespace

extern "C" {

// ---- Max slope angle ---------------------------------------------------------------------------

int32_t aver_phys_character_set_max_slope_angle(int32_t ch, float radians) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    c->SetMaxSlopeAngle(radians);
    return 1;
}

int32_t aver_phys_character_max_slope_angle(int32_t ch, float* outRadians) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (!outRadians) return refuse(AbiError::NullPointer);
    // CharacterBase stores only the COSINE (mCosMaxSlopeAngle) and GetCosMaxSlopeAngle is its one
    // accessor -- acos recovers the angle this same setter put in, not a second definition of it.
    *outRadians = std::acos(c->GetCosMaxSlopeAngle());
    return 1;
}

// ---- Stair stepping -----------------------------------------------------------------------------

int32_t aver_phys_character_set_stair_stepping(int32_t ch, float stepUpCm, float stepDownCm) {
    if (!character(ch)) return 0;
    g_stairSettings[ch] = StairSettings{stepUpCm, stepDownCm};
    return 1;
}

int32_t aver_phys_character_stair_stepping(int32_t ch, float* outStepUpCm, float* outStepDownCm) {
    if (!character(ch)) return 0;
    if (!outStepUpCm || !outStepDownCm) return refuse(AbiError::NullPointer);
    const auto it = g_stairSettings.find(ch);
    const StairSettings s = it != g_stairSettings.end() ? it->second : StairSettings{};
    *outStepUpCm = s.stepUpCm;
    *outStepDownCm = s.stepDownCm;
    return 1;
}

// ---- Gravity factor ----------------------------------------------------------------------------

int32_t aver_phys_character_set_gravity_factor(int32_t ch, float factor) {
    if (!character(ch)) return 0;
    if (!std::isfinite(factor) || factor < 0.0f) return refuse(AbiError::InvalidArgument);
    g_world->characterGravity[ch] = factor;
    return 1;
}

int32_t aver_phys_character_gravity_factor(int32_t ch, float* outFactor) {
    if (!character(ch)) return 0;
    if (!outFactor) return refuse(AbiError::NullPointer);
    const auto it = g_world->characterGravity.find(ch);
    *outFactor = it != g_world->characterGravity.end() ? it->second : 1.0f;
    return 1;
}

// ---- Ground state ------------------------------------------------------------------------------

int32_t aver_phys_character_ground_state(int32_t ch) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return -1;   // not 0: 0 is OnGround, a real answer -- same convention as body motion type
    switch (c->GetGroundState()) {
        case JPH::CharacterBase::EGroundState::OnGround:      return AVER_PHYS_GROUND_ON_GROUND;
        case JPH::CharacterBase::EGroundState::OnSteepGround: return AVER_PHYS_GROUND_ON_STEEP_GROUND;
        case JPH::CharacterBase::EGroundState::NotSupported:  return AVER_PHYS_GROUND_NOT_SUPPORTED;
        case JPH::CharacterBase::EGroundState::InAir:         return AVER_PHYS_GROUND_IN_AIR;
    }
    return -1;
}

// ---- What the character is standing on ----------------------------------------------------------

int32_t aver_phys_character_ground_normal(int32_t ch, float* outXyz) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (!outXyz) return refuse(AbiError::NullPointer);
    // fromJoltUnit, not fromJoltDir: a ground normal is a unit vector, same as a contact normal in
    // EventListener::OnContactAdded (PhysicsWorld.cpp).
    writeVec(outXyz, fromJoltUnit(c->GetGroundNormal()));
    return 1;
}

int32_t aver_phys_character_ground_position(int32_t ch, float* outXyz) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (!outXyz) return refuse(AbiError::NullPointer);
    writeVec(outXyz, fromJolt(c->GetGroundPosition()));
    return 1;
}

int32_t aver_phys_character_ground_body(int32_t ch) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (const int32_t h = handleOfBody(c->GetGroundBodyID())) return h;
    // Standing on another CHARACTER leaves no body id: Jolt copies that contact's position into the
    // ground position, so the contact at the same spot names it.
    const auto gs = c->GetGroundState();
    if (gs == JPH::CharacterBase::EGroundState::OnGround || gs == JPH::CharacterBase::EGroundState::OnSteepGround)
        for (const JPH::CharacterContact& k : c->GetActiveContacts())
            if (k.mHadCollision && !k.mCharacterIDB.IsInvalid() &&
                (k.mPosition - c->GetGroundPosition()).LengthSq() < 1.0e-8f)
                return handleOfCharacterId(k.mCharacterIDB);
    return 0;
}

int32_t aver_phys_character_ground_velocity(int32_t ch, float* outXyz) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (!outXyz) return refuse(AbiError::NullPointer);
    writeVec(outXyz, fromJoltDir(c->GetGroundVelocity()));
    return 1;
}

// ---- Inherited motion ---------------------------------------------------------------------------------
// World::characterCarry, which aver_phys_step owns (PhysicsWorld.cpp): read and written here, in Jolt's
// units and axes there.

int32_t aver_phys_character_inherited_velocity(int32_t ch, float* outXyz) {
    if (!character(ch)) return 0;
    if (!outXyz) return refuse(AbiError::NullPointer);
    const auto it = g_world->characterCarry.find(ch);
    writeVec(outXyz, it != g_world->characterCarry.end() ? fromJoltDir(it->second) : Vec3(0.0f, 0.0f, 0.0f));
    return 1;
}

int32_t aver_phys_character_set_inherited_velocity(int32_t ch, float x, float y, float z) {
    if (!character(ch)) return 0;
    g_world->characterCarry.insert_or_assign(ch, toJoltDir(Vec3(x, y, z)));
    return 1;
}

// ---- Shape (crouching) --------------------------------------------------------------------------

// Returns 0 with Unsupported when SetShape refuses because the new shape would not fit (a ceiling
// above a standing request); InvalidArgument when the dimensions or the Jolt capsule are illegal.
int32_t aver_phys_character_set_shape(int32_t ch, float radius, float height, float maxPenetrationCm) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;

    // Same derivation as aver_phys_character_create: Jolt's capsule takes the HALF height of its
    // cylinder, so the two caps come out of the total height first.
    const float rM = cmToM(radius);
    const float halfCyl = cmToM(height) * 0.5f - rM;
    if (halfCyl <= 0.0f) {
        AVER_WARN("[Physics] character shape {}cm tall is too short for radius {}cm", height, radius);
        return refuse(AbiError::InvalidArgument);
    }
    JPH::CapsuleShapeSettings capsule(halfCyl, rM);
    capsule.SetEmbedded();
    auto res = capsule.Create();
    if (res.HasError()) { AVER_WARN("[Physics] character shape: {}", res.GetError().c_str()); return refuse(AbiError::InvalidArgument); }

    // SetShape checks the NEW shape against the world before committing and reports failure rather
    // than embedding the character in whatever it no longer fits under -- that refusal is the entire
    // reason crouch/stand exists as a question with a real answer instead of a character clipping
    // through a low ceiling the moment it tries to stand.
    const bool ok = c->SetShape(res.Get(), cmToM(maxPenetrationCm),
                                g_world->system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                                g_world->system.GetDefaultLayerFilter(Layers::MOVING),
                                g_filter, {}, *g_world->temp);
    if (!ok) return refuse(AbiError::Unsupported);

    // The inner body is a separate shape reference Jolt does not update on its own (its own doc
    // comment on SetInnerBodyShape says to call this after a successful SetShape) -- skipping it
    // would leave every broadphase query (a raycast, above all) seeing the character's OLD size while
    // its actual collision volume had already changed.
    c->SetInnerBodyShape(res.Get());

    // The supporting-volume plane is also sized to the OLD radius (aver_phys_character_create sets it
    // to -rM at creation, to keep the capsule from catching on its own bottom cap) -- carried forward
    // unchanged here it would reject or accept ground contacts by the wrong capsule's geometry, so it
    // is recomputed for the new one.
    c->SetSupportingVolume(JPH::Plane(toJoltUnit(Vec3(0, 0, 1)), -rM));
    return 1;
}

int32_t aver_phys_character_shape(int32_t ch, float* outRadius, float* outHeight) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (!outRadius || !outHeight) return refuse(AbiError::NullPointer);
    // A capsule is the only shape either creator (aver_phys_character_create, and set_shape above)
    // ever gives a character, which is what makes the downcast safe; the sub-type is still asked
    // rather than assumed, so a third creator with another shape reads as a refusal, not as garbage.
    const JPH::Shape* shape = c->GetShape();
    if (!shape || shape->GetSubType() != JPH::EShapeSubType::Capsule) return refuse(AbiError::Unsupported);
    const JPH::CapsuleShape* capsule = static_cast<const JPH::CapsuleShape*>(shape);
    // The creators' derivation run backwards: the caps go back onto the cylinder's half height.
    *outRadius = mToCm(capsule->GetRadius());
    *outHeight = mToCm((capsule->GetHalfHeightOfCylinder() + capsule->GetRadius()) * 2.0f);
    return 1;
}

// ---- Mass and push strength -----------------------------------------------------------------------

int32_t aver_phys_character_set_mass(int32_t ch, float massKg) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (massKg <= 0.0f) {
        AVER_WARN("[Physics] character mass must be positive; {} kg refused", massKg);
        return refuse(AbiError::InvalidArgument);
    }
    c->SetMass(massKg);
    return 1;
}

int32_t aver_phys_character_mass(int32_t ch, float* outMassKg) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (!outMassKg) return refuse(AbiError::NullPointer);
    *outMassKg = c->GetMass();
    return 1;
}

int32_t aver_phys_character_set_max_strength(int32_t ch, float maxStrengthKgCmS2) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    // Same conversion as any other force in this ABI (physics_abi.h's force/impulse block): one
    // length dimension, centimetres here and metres (Jolt's Newtons) there.
    c->SetMaxStrength(cmToM(maxStrengthKgCmS2));
    return 1;
}

int32_t aver_phys_character_max_strength(int32_t ch, float* outMaxStrengthKgCmS2) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (!outMaxStrengthKgCmS2) return refuse(AbiError::NullPointer);
    *outMaxStrengthKgCmS2 = mToCm(c->GetMaxStrength());
    return 1;
}

// ---- Character vs character ---------------------------------------------------------------------------

int32_t aver_phys_set_character_vs_character(int32_t on) {
    g_charVsChar = on != 0;
    if (g_world)
        for (const auto& [h, c] : g_world->characters)
            c->SetCharacterVsCharacterCollision(g_charVsChar ? g_cvc.get() : nullptr);
    return 1;
}

int32_t aver_phys_character_vs_character(void) { return g_charVsChar ? 1 : 0; }

// ---- Inner body ----------------------------------------------------------------------------------------

int32_t aver_phys_set_character_inner_body_default(int32_t on) {
    g_innerBodyDefault = on != 0;
    return 1;
}

int32_t aver_phys_character_has_inner_body(int32_t ch) {
    JPH::CharacterVirtual* c = character(ch);
    return c && !c->GetInnerBodyID().IsInvalid() ? 1 : 0;
}

// Jolt fixes the inner body at construction, so the character is rebuilt from its own settings under the
// same handle and what the settings do not carry (velocity, user data, position) is copied across.
int32_t aver_phys_character_set_inner_body(int32_t ch, int32_t on) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if ((on != 0) == !c->GetInnerBodyID().IsInvalid()) return 1;

    JPH::Ref<JPH::CharacterVirtualSettings> s = new JPH::CharacterVirtualSettings(c->GetCharacterVirtualSettings());
    s->mInnerBodyShape = on != 0 ? c->GetShape() : nullptr;
    s->mInnerBodyIDOverride = JPH::BodyID();   // the old one is still alive while the new is built
    s->mInnerBodyLayer = Layers::MOVING;
    JPH::Ref<JPH::CharacterVirtual> n =
        new JPH::CharacterVirtual(s, c->GetPosition(), c->GetRotation(), c->GetUserData(), &g_world->system);
    n->SetLinearVelocity(c->GetLinearVelocity());

    characterDestroyed(c);
    g_world->characters[ch] = n;   // drops the old character, and its inner body with it
    characterCreated(n.GetPtr());
    // The ground state is not in the settings; find it the way a teleport does.
    n->RefreshContacts(g_world->system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                       g_world->system.GetDefaultLayerFilter(Layers::MOVING),
                       g_filter, {}, *g_world->temp);
    return 1;
}

// ---- Can it stand? -------------------------------------------------------------------------------------

int32_t aver_phys_character_can_set_shape(int32_t ch, float radius, float halfHeight) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    const JPH::Shape* cur = c->GetShape();
    if (!cur || cur->GetSubType() != JPH::EShapeSubType::Capsule) return refuse(AbiError::Unsupported);

    const float rM = cmToM(radius);
    const float newHalf = cmToM(halfHeight);
    const float halfCyl = newHalf - rM;
    if (!(rM > 0.0f) || !(halfCyl > 0.0f)) return refuse(AbiError::InvalidArgument);
    JPH::CapsuleShapeSettings capsule(halfCyl, rM);
    capsule.SetEmbedded();
    auto res = capsule.Create();
    if (res.HasError()) return refuse(AbiError::InvalidArgument);

    // Feet stay where they are, so growing the capsule moves its centre up by the change in half height
    // (the centre of mass sits one padding above the position, as in CharacterVirtual).
    const JPH::CapsuleShape* now = static_cast<const JPH::CapsuleShape*>(cur);
    const float oldHalf = now->GetHalfHeightOfCylinder() + now->GetRadius();
    const JPH::RVec3 centre = c->GetPosition() + c->GetUp() * (newHalf - oldHalf + c->GetCharacterPadding());

    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;   // the character's own default
    CharacterFilter solidOnly;
    solidOnly.rejectSensors = true;
    DeepestOverlap deepest;
    g_world->system.GetNarrowPhaseQuery().CollideShape(
        res.Get(), JPH::Vec3::sOne(), JPH::RMat44::sRotationTranslation(c->GetRotation(), centre), settings,
        JPH::RVec3::sZero(), deepest,
        g_world->system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        g_world->system.GetDefaultLayerFilter(Layers::MOVING), solidOnly, {});
    // A centimetre of overlap is the seating tolerance between a resting capsule and the floor.
    if (deepest.depth > cmToM(1.0f)) return refuse(AbiError::Unsupported);
    return 1;
}

// ---- Contacts ----------------------------------------------------------------------------------------------

int32_t aver_phys_character_contact_count(int32_t ch) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    int32_t n = 0;
    for (const JPH::CharacterContact& k : c->GetActiveContacts()) n += touching(k) ? 1 : 0;
    return n;
}

int32_t aver_phys_character_contact_get(int32_t ch, int32_t index, int32_t* outBody, float* outPoint3,
                                         float* outNormal3) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    if (index < 0) return refuse(AbiError::InvalidArgument);
    int32_t i = 0;
    for (const JPH::CharacterContact& k : c->GetActiveContacts()) {
        if (!touching(k) || i++ != index) continue;
        if (outBody) *outBody = k.mCharacterIDB.IsInvalid() ? handleOfBody(k.mBodyB) : handleOfCharacterId(k.mCharacterIDB);
        if (outPoint3) writeVec(outPoint3, fromJolt(k.mPosition));
        // Jolt's contact normal already points toward the character.
        if (outNormal3) writeVec(outNormal3, fromJoltUnit(k.mContactNormal));
        return 1;
    }
    return refuse(AbiError::InvalidArgument);
}

// ---- Internal edge removal ------------------------------------------------------------------------------

int32_t aver_phys_set_character_edge_removal_default(int32_t on) {
    g_edgeRemovalDefault = on != 0;
    return 1;
}

int32_t aver_phys_character_set_edge_removal(int32_t ch, int32_t on) {
    JPH::CharacterVirtual* c = character(ch);
    if (!c) return 0;
    c->SetEnhancedInternalEdgeRemoval(on != 0);
    return 1;
}

int32_t aver_phys_character_edge_removal(int32_t ch) {
    JPH::CharacterVirtual* c = character(ch);
    return c && c->GetEnhancedInternalEdgeRemoval() ? 1 : 0;
}

// ---- Entity to character ------------------------------------------------------------------------------

int32_t aver_phys_character_of_entity(int32_t entity) {
    // No match is an ordinary answer, so a live world records Ok.
    if (!g_world) { setAbiError(AbiError::NotInitialised); return 0; }
    setAbiError(AbiError::Ok);
    // 0 is every unstamped character's user data, so asking for it would name one at random.
    if (entity == 0) return 0;
    // A walk of the character table, like handleOfBody above: a world holds a handful of characters.
    // The same truncation aver_phys_raycast reads the stamp back with.
    for (const auto& [h, c] : g_world->characters)
        if (static_cast<int32_t>(c->GetUserData()) == entity) return h;
    return 0;
}

} // extern "C"

// ---- the step loop's half of stair stepping -------------------------------------------------------
//
// See PhysicsInternal.hpp for why this exists: Jolt takes both distances as arguments to
// ExtendedUpdate rather than as state, so the setter above can only record them and this is where they
// are read back. PhysicsWorld.cpp's fixed-step loop calls this once per character per step.
namespace aver::physics::detail {

void applyCharacterStairSettings(int32_t handle, JPH::CharacterVirtual::ExtendedUpdateSettings& out) {
    const auto it = g_stairSettings.find(handle);
    const StairSettings s = it != g_stairSettings.end() ? it->second : StairSettings{};
    // The engine's up, in Jolt's axes, scaled to each distance. toJoltUnit rather than toJolt because
    // the direction is a unit vector and the LENGTH is applied separately -- running (0,0,stepUpCm)
    // through toJolt would convert the centimetres twice.
    const JPH::Vec3 up = toJoltUnit(Vec3(0.0f, 0.0f, 1.0f));
    out.mWalkStairsStepUp     =  up * cmToM(s.stepUpCm);
    out.mStickToFloorStepDown = -up * cmToM(s.stepDownCm);
}

void clearCharacterStairSettings() {
    g_stairSettings.clear();
    // The extras go too. The characters are about to be released and must not be left pointing at the
    // collision list; the switches return to their defaults so one world's choice does not leak.
    if (g_world)
        for (const auto& [h, c] : g_world->characters) c->SetCharacterVsCharacterCollision(nullptr);
    g_cvc.reset();
    g_innerIds.clear();
    g_charVsChar = g_innerBodyDefault = g_edgeRemovalDefault = true;
}

void characterCreated(JPH::CharacterVirtual* c) {
    if (!g_cvc) g_cvc = std::make_unique<JPH::CharacterVsCharacterCollisionSimple>();
    g_cvc->Add(c);
    c->SetCharacterVsCharacterCollision(g_charVsChar ? g_cvc.get() : nullptr);
    if (!c->GetInnerBodyID().IsInvalid()) g_innerIds.insert(c->GetInnerBodyID());
}

void characterDestroyed(JPH::CharacterVirtual* c) {
    if (g_cvc) g_cvc->Remove(c);
    g_innerIds.erase(c->GetInnerBodyID());
}

bool characterInnerBodyDefault() { return g_innerBodyDefault; }
bool characterEdgeRemovalDefault() { return g_edgeRemovalDefault; }
const JPH::BodyFilter& characterBodyFilter() { return g_filter; }

} // namespace aver::physics::detail
