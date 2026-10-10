// CharacterExtTest -- the character additions at the end of physics_character_abi.h: characters
// colliding with each other, the inner body, the can-stand query, the contact list and internal edge
// removal.
//
// Same rule as CharacterTest: assert observed motion and what a ray or a query reports, not return
// codes. The two that need it most are the first and last of the collision claims: two characters
// walking at each other must stop, and with the switch off must pass, because a switch that changes
// nothing is the shape this repository keeps shipping.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/core/Math.hpp"
#include "aver/physics/physics_abi.h"
#include "aver/physics/physics_character_abi.h"

#include <cmath>
#include <string>

using namespace aver;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool near(f32 a, f32 b, f32 eps) { return std::fabs(a - b) <= eps; }
static std::string f2s(f32 v) { char b[64]; std::snprintf(b, sizeof b, "%.2f", v); return b; }
static const f32 kDt = 1.0f / 60.0f;

static void settle(int steps) { for (int i = 0; i < steps; ++i) aver_phys_step(kDt); }

// A world whose floor top is z = 0.
static int32_t worldWithFloor() {
    aver_phys_init();
    return aver_phys_add_static_box(0, 0, -50.0f, 8000.0f, 8000.0f, 50.0f);
}

// A 180 cm tall, 30 cm radius character standing on that floor at (x, y).
static int32_t standingAt(f32 x, f32 y) { return aver_phys_character_create(30.0f, 180.0f, x, y, 90.0f); }

static f32 posX(int32_t ch) { float p[3] = {0, 0, 0}; aver_phys_character_position(ch, p); return p[0]; }

// A ray along -X at the character's chest height, from far on the +X side.
static int32_t rayAt(f32 x, f32 y, int32_t* outEntity = nullptr) {
    float pt[3] = {0, 0, 0}, nm[3] = {0, 0, 0};
    int32_t entity = 0;
    const int32_t h = aver_phys_raycast(x + 300.0f, y, 90.0f, -1.0f, 0.0f, 0.0f, 600.0f, pt, nm, &entity);
    if (outEntity) *outEntity = entity;
    return h;
}

// ---------------------------------------------------------------------------------------------------

// Two characters walk at each other fast enough to cross in the time given. With collision on they must
// meet and stay on their own sides; with it off the same walk must carry them through.
static void walkIntoEachOther(f32& outA, f32& outB) {
    const int32_t a = standingAt(-150.0f, 0.0f);
    const int32_t b = standingAt(150.0f, 0.0f);
    settle(30);
    for (int i = 0; i < 150; ++i) {
        aver_phys_character_set_velocity(a, 200.0f, 0.0f, 0.0f);
        aver_phys_character_set_velocity(b, -200.0f, 0.0f, 0.0f);
        aver_phys_step(kDt);
    }
    outA = posX(a);
    outB = posX(b);
}

static void testCharactersCollide() {
    AVER_INFO("=== characters collide with each other, and the switch is real ===");

    check(aver_phys_character_vs_character() == 1, "character-vs-character is on by default");

    worldWithFloor();
    f32 a = 0, b = 0;
    walkIntoEachOther(a, b);
    check(a < b, "walking at each other, they do not pass through (A at " + f2s(a) + ", B at " + f2s(b) + ")");
    check(b - a > 40.0f, "and stay about a body apart, " + f2s(b - a) + " cm between centres");
    aver_phys_shutdown();

    worldWithFloor();
    aver_phys_set_character_vs_character(0);
    check(aver_phys_character_vs_character() == 0, "the switch reads back off");
    walkIntoEachOther(a, b);
    check(a > b, "with it off the same walk carries them through each other (A at " + f2s(a) +
                 ", B at " + f2s(b) + ")");
    aver_phys_shutdown();
    check(aver_phys_character_vs_character() == 1, "and shutting the world down puts the default back");

    // A walks into a standing B: Jolt treats a moving character as a kinematic obstacle, so A keeps one body
    // width and pushes B along ahead of it.
    worldWithFloor();
    const int32_t ca = standingAt(-200.0f, 0.0f);
    const int32_t cb = standingAt(0.0f, 0.0f);
    settle(30);
    for (int i = 0; i < 150; ++i) {
        aver_phys_character_set_velocity(ca, 200.0f, 0.0f, 0.0f);
        aver_phys_step(kDt);
    }
    const f32 gap = posX(cb) - posX(ca);
    check(gap > 55.0f && gap < 70.0f, "walking into a standing character stops at a body width, gap " + f2s(gap));
    check(posX(cb) > 50.0f, "and pushes it along ahead of it, B at " + f2s(posX(cb)));

    // Pinned against a wall, B stops, and A stays in contact with it.
    aver_phys_add_static_box(posX(cb) + 100.0f, 0.0f, 100.0f, 20.0f, 300.0f, 100.0f);
    for (int i = 0; i < 90; ++i) {
        aver_phys_character_set_velocity(ca, 200.0f, 0.0f, 0.0f);
        aver_phys_step(kDt);
    }
    bool sawB = false;
    for (int i = 0, n = aver_phys_character_contact_count(ca); i < n; ++i) {
        int32_t body = 0;
        float pt[3] = {0, 0, 0}, nm[3] = {0, 0, 0};
        if (aver_phys_character_contact_get(ca, i, &body, pt, nm) == 1 && body == cb) {
            sawB = true;
            check(nm[0] < -0.7f, "the contact normal points from B back toward A (-X), x " + f2s(nm[0]));
        }
    }
    check(sawB, "A's contact list names B by its character handle");
    aver_phys_shutdown();
}

// ---------------------------------------------------------------------------------------------------

static void testInnerBody() {
    AVER_INFO("=== the inner body is what a ray sees, and can be turned off and on ===");
    worldWithFloor();
    const int32_t ch = standingAt(0.0f, 0.0f);
    aver_phys_set_entity(ch, 77);
    settle(30);

    int32_t entity = 0;
    check(aver_phys_character_has_inner_body(ch) == 1, "a character has an inner body by default");
    check(rayAt(0.0f, 0.0f, &entity) == ch && entity == 77, "and a ray through it hits it, with its entity");

    float before[3] = {0, 0, 0};
    aver_phys_character_position(ch, before);
    check(aver_phys_character_set_inner_body(ch, 0) == 1, "the inner body can be removed");
    check(aver_phys_character_has_inner_body(ch) == 0, "it reads back as gone");
    check(rayAt(0.0f, 0.0f) == 0, "and the same ray now passes through the character");
    float after[3] = {0, 0, 0};
    aver_phys_character_position(ch, after);
    check(near(before[0], after[0], 0.01f) && near(before[1], after[1], 0.01f) && near(before[2], after[2], 0.01f),
          "the rebuilt character is where it was");
    settle(10);
    check(aver_phys_character_grounded(ch) == 1, "and finds its ground again");

    // It still walks and still has its handle's other settings.
    aver_phys_character_set_velocity(ch, 100.0f, 0.0f, 0.0f);
    settle(30);
    check(posX(ch) > 30.0f, "it still walks without one, now at x " + f2s(posX(ch)));
    aver_phys_character_set_velocity(ch, 0.0f, 0.0f, 0.0f);
    settle(10);

    check(aver_phys_character_set_inner_body(ch, 1) == 1, "the inner body can be put back");
    check(rayAt(posX(ch), 0.0f, &entity) == ch && entity == 77,
          "and a ray hits it again, entity stamp intact");

    aver_phys_set_character_inner_body_default(0);
    const int32_t bare = standingAt(1000.0f, 0.0f);
    settle(10);
    check(aver_phys_character_has_inner_body(bare) == 0, "with the default off, a new character has none");
    check(rayAt(1000.0f, 0.0f) == 0, "and a ray passes through it");
    check(aver_phys_character_has_inner_body(ch) == 1, "while the existing one is untouched");
    aver_phys_set_character_inner_body_default(1);
    check(aver_phys_character_has_inner_body(standingAt(2000.0f, 0.0f)) == 1, "the default can be turned back on");
    aver_phys_shutdown();
}

// ---------------------------------------------------------------------------------------------------

static void testCanSetShape() {
    AVER_INFO("=== can-stand: true in the open, false under a low ceiling ===");

    // In the open, a standing character can be any height.
    {
        worldWithFloor();
        const int32_t ch = standingAt(0.0f, 0.0f);
        settle(60);
        check(aver_phys_character_can_set_shape(ch, 30.0f, 90.0f) == 1, "the same capsule fits");
        check(aver_phys_character_can_set_shape(ch, 30.0f, 120.0f) == 1, "and so does a taller one in the open");
        check(aver_phys_character_can_set_shape(ch, 30.0f, 60.0f) == 1, "and a shorter one");
        aver_phys_shutdown();
    }

    // Under a ceiling whose underside is at 200: 190 cm clears it, 220 and 240 do not.
    {
        worldWithFloor();
        const int32_t ch = standingAt(0.0f, 0.0f);
        settle(60);
        aver_phys_add_static_box(0.0f, 0.0f, 210.0f, 400.0f, 400.0f, 10.0f);
        settle(10);
        float p0[3] = {0, 0, 0}, p1[3] = {0, 0, 0};
        aver_phys_character_position(ch, p0);
        check(aver_phys_character_can_set_shape(ch, 30.0f, 95.0f) == 1, "a 190 cm capsule fits under the 200 cm ceiling");
        check(aver_phys_character_can_set_shape(ch, 30.0f, 110.0f) == 0, "a 220 cm capsule does not");
        check(aver_phys_last_error() == -5, "  reason: unsupported (would not fit)");
        check(aver_phys_character_can_set_shape(ch, 30.0f, 120.0f) == 0, "nor a 240 cm one");
        float r = 0, h = 0;
        aver_phys_character_shape(ch, &r, &h);
        aver_phys_character_position(ch, p1);
        check(near(r, 30.0f, 0.01f) && near(h, 180.0f, 0.01f) && near(p0[2], p1[2], 0.001f),
              "asking changed neither the shape nor the position");
        check(aver_phys_character_can_set_shape(ch, 30.0f, 20.0f) == 0 && aver_phys_last_error() == -6,
              "a half height too short for the radius is an invalid argument");
        check(aver_phys_character_can_set_shape(9999, 30.0f, 90.0f) == 0, "a dead handle cannot stand");
        aver_phys_shutdown();
    }

    // The case the query exists for: crouched under a low ceiling, cannot stand; crouched in the open, can.
    {
        worldWithFloor();
        const int32_t low = aver_phys_character_create(30.0f, 90.0f, 0.0f, 0.0f, 45.0f);
        const int32_t roomy = aver_phys_character_create(30.0f, 90.0f, 1000.0f, 0.0f, 45.0f);
        settle(60);
        aver_phys_add_static_box(0.0f, 0.0f, 110.0f, 400.0f, 400.0f, 10.0f);   // underside at 100
        settle(10);
        check(aver_phys_character_can_set_shape(low, 30.0f, 90.0f) == 0, "crouched under a 100 cm ceiling, 180 cm does not fit");
        check(aver_phys_character_can_set_shape(low, 30.0f, 45.0f) == 1, "but staying crouched does");
        check(aver_phys_character_can_set_shape(roomy, 30.0f, 90.0f) == 1, "crouched in the open, standing fits");
        aver_phys_shutdown();
    }
}

// ---------------------------------------------------------------------------------------------------

static void testContacts() {
    AVER_INFO("=== the contact list names the floor and the wall ===");
    const int32_t floorBody = worldWithFloor();
    // A wall whose face is at x = 110, across the character's path.
    const int32_t wall = aver_phys_add_static_box(130.0f, 0.0f, 100.0f, 20.0f, 400.0f, 100.0f);
    const int32_t ch = standingAt(0.0f, 0.0f);
    settle(60);

    const int32_t n = aver_phys_character_contact_count(ch);
    check(n >= 1, "a character standing on a floor has contacts, " + std::to_string(n));
    bool sawFloor = false;
    for (int32_t i = 0; i < n; ++i) {
        int32_t body = 0;
        float pt[3] = {0, 0, 0}, nm[3] = {0, 0, 0};
        if (aver_phys_character_contact_get(ch, i, &body, pt, nm) != 1 || body != floorBody) continue;
        sawFloor = true;
        check(nm[2] > 0.9f, "the floor's normal points up toward the character, z " + f2s(nm[2]));
        check(std::fabs(pt[2]) < 5.0f, "and the contact is at floor level, z " + f2s(pt[2]));
    }
    check(sawFloor, "one of them is the floor body");

    float pt[3] = {0, 0, 0}, nm[3] = {0, 0, 0};
    check(aver_phys_character_contact_get(ch, n, nullptr, pt, nm) == 0, "an index past the end is refused");
    check(aver_phys_character_contact_get(ch, -1, nullptr, pt, nm) == 0, "and so is a negative one");
    check(aver_phys_character_contact_get(ch, 0, nullptr, nullptr, nullptr) == 1, "every out-pointer is optional");

    // Walk into the wall.
    for (int i = 0; i < 90; ++i) {
        aver_phys_character_set_velocity(ch, 150.0f, 0.0f, 0.0f);
        aver_phys_step(kDt);
    }
    bool sawWall = false;
    for (int32_t i = 0, m = aver_phys_character_contact_count(ch); i < m; ++i) {
        int32_t body = 0;
        if (aver_phys_character_contact_get(ch, i, &body, pt, nm) != 1 || body != wall) continue;
        sawWall = true;
        check(nm[0] < -0.8f, "the wall's normal points back at the character (-X), x " + f2s(nm[0]));
        check(near(pt[0], 110.0f, 5.0f), "and the contact is on the wall's face, x " + f2s(pt[0]));
    }
    check(sawWall, "pushing into a wall puts the wall in the list");

    aver_phys_character_destroy(ch);
    check(aver_phys_character_contact_count(ch) == 0, "a destroyed character has no contacts");
    aver_phys_shutdown();
}

// ---------------------------------------------------------------------------------------------------

static void testEdgeRemovalAndDeadHandles() {
    AVER_INFO("=== internal edge removal, and the dead-handle answers ===");
    worldWithFloor();
    const int32_t ch = standingAt(0.0f, 0.0f);
    check(aver_phys_character_edge_removal(ch) == 1, "edge removal is on by default");
    check(aver_phys_character_set_edge_removal(ch, 0) == 1 && aver_phys_character_edge_removal(ch) == 0,
          "and can be turned off per character");

    aver_phys_set_character_edge_removal_default(0);
    const int32_t plain = standingAt(500.0f, 0.0f);
    check(aver_phys_character_edge_removal(plain) == 0, "with the default off a new character has it off");
    aver_phys_set_character_edge_removal_default(1);
    check(aver_phys_character_edge_removal(standingAt(900.0f, 0.0f)) == 1, "and back on");

    check(aver_phys_character_has_inner_body(9999) == 0, "a dead handle has no inner body");
    check(aver_phys_character_set_inner_body(9999, 1) == 0, "cannot be given one");
    check(aver_phys_character_set_edge_removal(9999, 1) == 0, "or an edge setting");
    check(aver_phys_character_edge_removal(9999) == 0, "and reads as off");
    check(aver_phys_character_contact_count(9999) == 0, "with no contacts");
    check(aver_phys_character_contact_get(9999, 0, nullptr, nullptr, nullptr) == 0, "and none to get");

    aver_phys_set_character_vs_character(0);
    aver_phys_set_character_edge_removal_default(0);
    aver_phys_set_character_inner_body_default(0);
    aver_phys_shutdown();
    aver_phys_init();
    check(aver_phys_character_vs_character() == 1, "a new world starts with characters colliding");
    const int32_t fresh = standingAt(0.0f, 0.0f);
    check(aver_phys_character_has_inner_body(fresh) == 1 && aver_phys_character_edge_removal(fresh) == 1,
          "and with the inner body and edge removal defaults restored");
    aver_phys_shutdown();
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO("character extras: vs character, inner body, can-stand, contacts, edge removal");

    testCharactersCollide();
    testInnerBody();
    testCanSetShape();
    testContacts();
    testEdgeRemovalAndDeadHandles();

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
