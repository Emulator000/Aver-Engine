// QueryTest -- box/capsule/body casts, overlaps, every-hit rays and the point query, through the plain-C
// ABI and nothing else.
//
// EVERY SCENE IS BUILT FROM BODIES THAT NEVER STEP: the world is not advanced, so a body is exactly
// where it was put and every expected distance is arithmetic on the numbers below, not an observation of
// settled physics. Scenes are deliberately OFF-AXIS -- the near wall in one test is on +X, in another on
// +Y, in another above -- because a swapped axis in the engine-to-Jolt change of basis still answers
// "something is in front of the box" correctly and only gives itself away when the wall is somewhere
// the wrong axis does not look. Box half extents are made unequal for the same reason.
//
// Distances carry a ~1.5 cm tolerance: a cast is a root find on a convex shape, not exact arithmetic.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/core/Math.hpp"
#include "aver/physics/physics_abi.h"
#include "aver/physics/physics_layers_abi.h"
#include "aver/physics/physics_queries_abi.h"

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

static std::string f2s(float v) { char b[64]; std::snprintf(b, sizeof b, "%.2f", v); return b; }
static bool near(float a, float b, float tol = 1.5f) { return std::fabs(a - b) <= tol; }
static const float kTol = 1.5f;

// A rotation of 90 degrees about one engine axis. The direction of the turn does not matter to any test
// here -- each only uses the fact that a long thin shape ends up lying along the OTHER horizontal axis.
struct Q { float x, y, z, w; };
static const float kS = 0.70710678f;
static const Q kIdentity{0, 0, 0, 1};
static const Q kYaw90{0, 0, kS, kS};     // about +Z: a long axis on X ends up on Y
static const Q kRoll90{0, kS, 0, kS};    // about +Y: a capsule standing on Z ends up lying on X

static int32_t wall(float x, float y, float z, float hx, float hy, float hz) {
    return aver_phys_add_static_box(x, y, z, hx, hy, hz);
}

static int32_t boxCast(float cx, float cy, float cz, float hx, float hy, float hz, Q q,
                       float dx, float dy, float dz, uint32_t mask, int32_t ignore, int32_t flags,
                       float* dist, float* pt, float* nrm) {
    return aver_phys_box_cast(cx, cy, cz, hx, hy, hz, q.x, q.y, q.z, q.w, dx, dy, dz,
                              mask, ignore, flags, dist, pt, nrm);
}

// ---------------------------------------------------------------------------------------------------

static void testBoxCast() {
    AVER_INFO("=== box cast: nearest hit, distance, normal, axes, rotation ===");
    aver_phys_init();
    const int32_t nearW = wall(500, 0, 0, 10, 200, 200);     // face at x = 490
    const int32_t farW  = wall(1000, 0, 0, 10, 200, 200);    // face at x = 990
    const int32_t right = wall(0, 800, 0, 200, 10, 200);     // face at y = 790
    const int32_t roof  = wall(0, 0, 600, 200, 200, 10);     // face at z = 590

    float d = -1, pt[3] = {0}, n[3] = {0};
    int32_t h = boxCast(0, 0, 0, 20, 20, 20, kIdentity, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == nearW, "a box swept along +X hits the NEAR wall, not the far one");
    check(near(d, 470.0f, kTol), "at distance 490 - 20 = 470 cm, got " + f2s(d));
    check(near(pt[0], 490.0f, kTol), "the contact point is on the wall face x = 490, got " + f2s(pt[0]));
    check(near(n[0], -1.0f, 0.05f) && std::fabs(n[1]) < 0.05f && std::fabs(n[2]) < 0.05f,
          "and the normal points back at the box, (-1,0,0), got (" + f2s(n[0]) + "," + f2s(n[1]) + "," + f2s(n[2]) + ")");

    // Unequal half extents: along X only hx matters.
    h = boxCast(0, 0, 0, 10, 50, 20, kIdentity, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == nearW && near(d, 480.0f, kTol), "hx = 10 reaches the +X wall at 490 - 10 = 480, got " + f2s(d));

    // Off-axis: along +Y only hy matters, and the wall is on +Y.
    h = boxCast(0, 0, 0, 10, 50, 20, kIdentity, 0, 2000, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == right && near(d, 740.0f, kTol), "swept along +Y it hits the right wall at 790 - 50 = 740, got " + f2s(d));
    check(near(n[1], -1.0f, 0.05f), "with normal (0,-1,0), got y = " + f2s(n[1]));

    // Up: only hz matters.
    h = boxCast(0, 0, 0, 10, 50, 20, kIdentity, 0, 0, 2000, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == roof && near(d, 570.0f, kTol), "swept along +Z it hits the roof at 590 - 20 = 570, got " + f2s(d));
    check(near(n[2], -1.0f, 0.05f), "with normal (0,0,-1), got z = " + f2s(n[2]));

    // The displacement is direction AND distance: a short one does not reach.
    h = boxCast(0, 0, 0, 20, 20, 20, kIdentity, 300, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == 0, "a 300 cm sweep stops short of a wall 470 cm away");
    h = boxCast(0, 0, 0, 20, 20, 20, kIdentity, -2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == 0, "and sweeping AWAY from every wall hits nothing");
    h = boxCast(0, 0, 0, 20, 20, 20, kIdentity, 0, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == 0, "a zero displacement is a miss, not a crash");

    // Rotation: a 100 x 10 x 10 cm bar swung 90 degrees about Z has 5 cm, not 50 cm, along X.
    h = boxCast(0, 0, 0, 50, 5, 5, kIdentity, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == nearW && near(d, 440.0f, kTol), "an unrotated bar reaches the wall at 490 - 50 = 440, got " + f2s(d));
    h = boxCast(0, 0, 0, 50, 5, 5, kYaw90, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == nearW && near(d, 485.0f, kTol), "the same bar yawed 90 degrees reaches it at 490 - 5 = 485, got " + f2s(d));
    h = aver_phys_box_cast(0, 0, 0, 20, 20, 20, 0, 0, 0, 0, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == nearW && near(d, 470.0f, kTol), "an all-zero quaternion means identity, not NaN");

    // Filters.
    aver_phys_body_set_layer(nearW, 4);
    aver_phys_body_set_layer(farW, 5);
    h = boxCast(0, 0, 0, 20, 20, 20, kIdentity, 2000, 0, 0, AVER_PHYS_LAYER_BIT(5), 0, 0, &d, pt, n);
    check(h == farW && near(d, 970.0f, kTol), "masked to layer 5 the sweep passes the near wall and hits the far one at 970, got " + f2s(d));
    h = boxCast(0, 0, 0, 20, 20, 20, kIdentity, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, nearW, 0, &d, pt, n);
    check(h == farW, "ignoreBody skips the near wall without needing a layer");
    h = boxCast(0, 0, 0, 20, 20, 20, kIdentity, 2000, 0, 0, AVER_PHYS_LAYER_BIT(9), 0, 0, &d, pt, n);
    check(h == 0, "a mask naming no wall's layer hits nothing");
    h = boxCast(0, 0, 0, 20, 20, 20, kIdentity, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, nullptr, nullptr, nullptr);
    check(h == nearW, "every out-pointer may be null");
    h = boxCast(0, 0, 0, 0, 20, 20, kIdentity, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == 0, "a box with a zero half extent is refused");

    aver_phys_shutdown();
}

static void testCapsuleCast() {
    AVER_INFO("=== capsule cast: total height, upright along +Z, rotation ===");
    aver_phys_init();
    aver_phys_add_static_box(0, 0, -50, 2000, 2000, 50);              // floor, top at z = 0
    const int32_t nearW = wall(500, 0, 300, 10, 200, 300);            // face at x = 490
    const int32_t right = wall(0, 800, 300, 200, 10, 300);            // face at y = 790

    float d = -1, pt[3] = {0}, n[3] = {0};
    // radius 20, TOTAL height 100: stands from z = 150 to z = 250 when centred at 200.
    int32_t h = aver_phys_capsule_cast(0, 0, 200, 20, 100, 0, 0, 0, 1, 0, 0, -1000,
                                       AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h != 0 && near(d, 150.0f, kTol), "dropped onto the floor it travels 200 - 50 = 150 cm (height is TOTAL), got " + f2s(d));
    check(near(n[2], 1.0f, 0.05f), "and the floor's normal is +Z, got z = " + f2s(n[2]));

    h = aver_phys_capsule_cast(0, 0, 200, 20, 100, 0, 0, 0, 1, 2000, 0, 0,
                               AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == nearW && near(d, 470.0f, kTol), "swept along +X it hits the wall at 490 - 20 = 470, got " + f2s(d));
    check(near(n[0], -1.0f, 0.05f), "with normal (-1,0,0)");

    h = aver_phys_capsule_cast(0, 0, 200, 20, 100, 0, 0, 0, 1, 0, 2000, 0,
                               AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == right && near(d, 770.0f, kTol), "swept along +Y it hits the right wall at 790 - 20 = 770, got " + f2s(d));
    check(near(n[1], -1.0f, 0.05f), "with normal (0,-1,0)");

    // Rolled onto its side it lies along X, so its end is 50 cm (not 20) from its centre that way.
    h = aver_phys_capsule_cast(0, 0, 200, 20, 100, kRoll90.x, kRoll90.y, kRoll90.z, kRoll90.w, 2000, 0, 0,
                               AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == nearW && near(d, 440.0f, kTol), "lying along X it reaches the wall at 490 - 50 = 440, got " + f2s(d));

    h = aver_phys_capsule_cast(0, 0, 200, 30, 50, 0, 0, 0, 1, 2000, 0, 0,
                               AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == 0, "a capsule too short for its radius is refused");

    h = aver_phys_capsule_cast(0, 0, 200, 20, 100, 0, 0, 0, 1, 2000, 0, 0,
                               AVER_PHYS_LAYER_BIT(7), 0, 0, &d, pt, n);
    check(h == 0, "and the layer mask applies to it too");

    aver_phys_shutdown();
}

static void testBodyCast() {
    AVER_INFO("=== body cast: the body's own shape, from its current pose, ignoring itself ===");
    aver_phys_init();
    const int32_t nearW = wall(500, 0, 0, 10, 200, 200);
    const int32_t farW  = wall(1000, 0, 0, 10, 200, 200);
    const int32_t box   = aver_phys_add_dynamic_box(0, 0, 100, 20, 20, 20, 5.0f);
    const int32_t bar   = aver_phys_add_dynamic_box(0, 0, -100, 50, 5, 5, 5.0f);
    aver_phys_body_set_rotation(bar, kYaw90.x, kYaw90.y, kYaw90.z, kYaw90.w);

    float d = -1, pt[3] = {0}, n[3] = {0};
    int32_t h = aver_phys_body_cast(box, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == nearW, "a body swept along +X hits the near wall and not itself");
    check(near(d, 470.0f, kTol), "at 490 - 20 = 470, got " + f2s(d));

    h = aver_phys_body_cast(box, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, nearW, 0, &d, pt, n);
    check(h == farW && near(d, 970.0f, kTol), "ignoreBody is a SECOND body to skip: the far wall at 970, got " + f2s(d));

    h = aver_phys_body_cast(bar, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == nearW && near(d, 485.0f, kTol), "a bar already yawed in the world uses its CURRENT pose: 490 - 5 = 485, got " + f2s(d));

    h = aver_phys_body_cast(999999, 2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == 0, "a dead handle is a miss");
    h = aver_phys_body_cast(box, -2000, 0, 0, AVER_PHYS_LAYER_MASK_ALL, 0, 0, &d, pt, n);
    check(h == 0, "and so is a sweep away from everything");

    aver_phys_shutdown();
}

// ---------------------------------------------------------------------------------------------------

// Four boxes of half extent 50 on three different axes, so a swapped axis lands the query on the wrong one.
struct Cluster { int32_t a, bx, c, d; };
static Cluster makeCluster() {
    Cluster k;
    k.a  = wall(0,   0,   0, 50, 50, 50);     // x -50..50
    k.bx = wall(200, 0,   0, 50, 50, 50);     // x 150..250
    k.c  = wall(0,   300, 0, 50, 50, 50);     // y 250..350
    k.d  = wall(0,   0, 300, 50, 50, 50);     // z 250..350
    return k;
}

static void testOverlaps() {
    AVER_INFO("=== overlap box / capsule / body ===");
    aver_phys_init();
    const Cluster k = makeCluster();
    int32_t out[16] = {0};
    const uint32_t all = AVER_PHYS_LAYER_MASK_ALL;

    int32_t n = aver_phys_overlap_box(100, 0, 0, 60, 60, 60, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 2, "a box spanning x 40..160 overlaps A and Bx, got " + std::to_string(n));
    bool hasA = false, hasB = false;
    for (int i = 0; i < n; ++i) { hasA |= out[i] == k.a; hasB |= out[i] == k.bx; }
    check(hasA && hasB, "and they are the right two");

    n = aver_phys_overlap_box(0, 300, 0, 10, 10, 10, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 1 && out[0] == k.c, "a small box on +Y finds only the +Y box");
    n = aver_phys_overlap_box(0, 0, 175, 10, 10, 100, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 1 && out[0] == k.d, "a tall thin box (hz = 100, z 75..275) finds only the +Z box");
    n = aver_phys_overlap_box(0, 0, 0, 500, 500, 500, 0, 0, 0, 1, all, 0, 0, out, 2);
    check(n == 2, "maxCount truncates: a box over all four with room for 2 returns 2");
    n = aver_phys_overlap_box(0, 0, 0, 500, 500, 500, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 4, "and with room it finds all four, once each, got " + std::to_string(n));
    n = aver_phys_overlap_box(5000, 0, 0, 10, 10, 10, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 0, "an empty region finds nothing");
    n = aver_phys_overlap_box(0, 0, 0, 10, 10, 10, 0, 0, 0, 1, all, 0, 0, nullptr, 16);
    check(n == 0, "a null output array is a count of 0, not a crash");

    // Rotation: a 400 cm bar centred at x = 400 reaches Bx; swung to lie along Y it reaches nothing.
    n = aver_phys_overlap_box(400, 0, 0, 200, 5, 5, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 1 && out[0] == k.bx, "an unrotated bar x 200..600 overlaps Bx");
    n = aver_phys_overlap_box(400, 0, 0, 200, 5, 5, kYaw90.x, kYaw90.y, kYaw90.z, kYaw90.w, all, 0, 0, out, 16);
    check(n == 0, "the same bar yawed 90 degrees lies along Y and misses everything");

    // Layers and ignore.
    aver_phys_body_set_layer(k.a, 3);
    n = aver_phys_overlap_box(0, 0, 0, 10, 10, 10, 0, 0, 0, 1, AVER_PHYS_LAYER_BIT(3), 0, 0, out, 16);
    check(n == 1 && out[0] == k.a, "masked to A's layer it finds A");
    n = aver_phys_overlap_box(0, 0, 0, 10, 10, 10, 0, 0, 0, 1, AVER_PHYS_LAYER_BIT(2), 0, 0, out, 16);
    check(n == 0, "masked to another layer it finds nothing");
    n = aver_phys_overlap_box(0, 0, 0, 10, 10, 10, 0, 0, 0, 1, all, k.a, 0, out, 16);
    check(n == 0, "ignoreBody skips A");
    aver_phys_body_set_layer(k.a, 0);

    // Capsule: radius 20, TOTAL height 100.
    n = aver_phys_overlap_capsule(0, 0, 210, 20, 100, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 1 && out[0] == k.d, "an upright capsule z 160..260 overlaps only the +Z box");
    n = aver_phys_overlap_capsule(0, 0, 150, 20, 100, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 0, "one at z 100..200 touches nothing");
    n = aver_phys_overlap_capsule(280, 0, 0, 20, 100, kRoll90.x, kRoll90.y, kRoll90.z, kRoll90.w, all, 0, 0, out, 16);
    check(n == 1 && out[0] == k.bx, "lying along X at x = 280 (230..330) it overlaps Bx");
    n = aver_phys_overlap_capsule(280, 0, 0, 20, 100, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 0, "standing at the same spot (x 260..300) it does not");
    n = aver_phys_overlap_capsule(0, 0, 210, 30, 50, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 0, "a capsule too short for its radius is refused");

    // Body overlap at an arbitrary pose.
    const int32_t probe = aver_phys_add_dynamic_box(0, -1000, 0, 60, 60, 60, 5.0f);
    const int32_t bar   = aver_phys_add_dynamic_box(0, -2000, 0, 200, 5, 5, 5.0f);
    n = aver_phys_overlap_body(probe, 100, 0, 0, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 2, "a 120 cm cube moved to x = 100 would overlap A and Bx, got " + std::to_string(n));
    n = aver_phys_overlap_body(probe, 0, 300, 0, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 1 && out[0] == k.c, "moved to +Y it would overlap only the +Y box");
    n = aver_phys_overlap_body(probe, 0, 0, 300, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 1 && out[0] == k.d, "moved to +Z, only the +Z box");
    n = aver_phys_overlap_body(probe, 0, -1000, 0, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 0, "left where it is it overlaps nothing, and never itself");
    n = aver_phys_overlap_body(bar, 400, 0, 0, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 1 && out[0] == k.bx, "a long bar placed at x = 400 reaches Bx");
    n = aver_phys_overlap_body(bar, 400, 0, 0, kYaw90.x, kYaw90.y, kYaw90.z, kYaw90.w, all, 0, 0, out, 16);
    check(n == 0, "and yawed to lie along Y it does not");
    n = aver_phys_overlap_body(probe, 100, 0, 0, 0, 0, 0, 1, all, k.a, 0, out, 16);
    check(n == 1 && out[0] == k.bx, "ignoreBody drops A from the probe's result");
    n = aver_phys_overlap_body(999999, 100, 0, 0, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 0, "a dead handle finds nothing");

    // Spheres count too.
    const int32_t ball = aver_phys_add_dynamic_sphere(0, 600, 0, 40, 1.0f);
    n = aver_phys_overlap_box(0, 600, 30, 10, 10, 10, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 1 && out[0] == ball, "a box over a sphere finds it");
    n = aver_phys_overlap_box(0, 600, 60, 10, 10, 10, 0, 0, 0, 1, all, 0, 0, out, 16);
    check(n == 0, "and a box above its top does not");

    aver_phys_shutdown();
}

static void testRaycastAll() {
    AVER_INFO("=== raycast_all: every hit, nearest first ===");
    aver_phys_init();
    // Created far-to-near so that creation order and distance order disagree.
    const int32_t w3 = wall(900, 0, 0, 10, 200, 200);
    const int32_t w1 = wall(300, 0, 0, 10, 200, 200);
    const int32_t w2 = wall(600, 0, 0, 10, 200, 200);
    const int32_t v1 = wall(0, 400, 0, 200, 10, 200);
    const int32_t v2 = wall(0, 700, 0, 200, 10, 200);
    const int32_t e1 = wall(0, 0, 300, 100, 100, 10);
    const int32_t e2 = wall(0, 0, 500, 100, 100, 10);
    const uint32_t all = AVER_PHYS_LAYER_MASK_ALL;

    int32_t b[8] = {0};
    float dist[8] = {0}, pts[24] = {0}, nrm[24] = {0};
    int32_t n = aver_phys_raycast_all(0, 0, 0, 1, 0, 0, 2000, all, 0, 0, 8, b, dist, pts, nrm);
    check(n == 3, "a ray along +X meets three walls, got " + std::to_string(n));
    check(b[0] == w1 && b[1] == w2 && b[2] == w3, "nearest first, whatever order they were created in");
    check(near(dist[0], 290) && near(dist[1], 590) && near(dist[2], 890),
          "at 290, 590 and 890 cm: " + f2s(dist[0]) + ", " + f2s(dist[1]) + ", " + f2s(dist[2]));
    check(near(pts[0], 290) && near(pts[3], 590) && near(pts[6], 890), "with points on the near faces, packed xyz per hit");
    check(near(nrm[0], -1, 0.05f) && near(nrm[3], -1, 0.05f) && near(nrm[6], -1, 0.05f), "and normals (-1,0,0)");

    n = aver_phys_raycast_all(0, 0, 0, 5, 0, 0, 2000, all, 0, 0, 8, b, dist, nullptr, nullptr);
    check(n == 3 && near(dist[0], 290), "a direction of any length is normalised, and the point/normal arrays may be null");
    n = aver_phys_raycast_all(0, 0, 0, 1, 0, 0, 2000, all, 0, 0, 2, b, dist, pts, nrm);
    check(n == 2 && b[0] == w1 && b[1] == w2, "maxCount 2 keeps the NEAREST two");
    n = aver_phys_raycast_all(0, 0, 0, 1, 0, 0, 700, all, 0, 0, 8, b, dist, pts, nrm);
    check(n == 2, "maxDistCm bounds the ray: 700 reaches two walls, got " + std::to_string(n));
    n = aver_phys_raycast_all(0, 0, 0, 1, 0, 0, 2000, all, 0, 0, 8, nullptr, dist, nullptr, nullptr);
    check(n == 3, "the body array may be null too");
    n = aver_phys_raycast_all(0, 0, 0, 0, 0, 0, 2000, all, 0, 0, 8, b, dist, pts, nrm);
    check(n == 0, "a zero direction hits nothing");
    n = aver_phys_raycast_all(0, 0, 0, -1, 0, 0, 2000, all, 0, 0, 8, b, dist, pts, nrm);
    check(n == 0, "and a ray away from every wall hits nothing");

    n = aver_phys_raycast_all(0, 0, 0, 0, 1, 0, 2000, all, 0, 0, 8, b, dist, pts, nrm);
    check(n == 2 && b[0] == v1 && b[1] == v2, "along +Y it meets the two Y walls, in order");
    check(near(dist[0], 390) && near(dist[1], 690), "at 390 and 690, got " + f2s(dist[0]) + ", " + f2s(dist[1]));
    check(near(pts[1], 390) && near(nrm[1], -1, 0.05f), "with the point's y and the normal's y carrying it");
    n = aver_phys_raycast_all(0, 0, 0, 0, 0, 1, 2000, all, 0, 0, 8, b, dist, pts, nrm);
    check(n == 2 && b[0] == e1 && b[1] == e2, "along +Z it meets the two ceilings, in order");
    check(near(dist[0], 290) && near(dist[1], 490) && near(nrm[2], -1, 0.05f), "at 290 and 490 with normal (0,0,-1)");

    n = aver_phys_raycast_all(0, 0, 0, 1, 0, 0, 2000, all, w1, 0, 8, b, dist, pts, nrm);
    check(n == 2 && b[0] == w2, "ignoreBody skips the first wall");
    aver_phys_body_set_layer(w2, 3);
    n = aver_phys_raycast_all(0, 0, 0, 1, 0, 0, 2000, AVER_PHYS_LAYER_BIT(3), 0, 0, 8, b, dist, pts, nrm);
    check(n == 1 && b[0] == w2, "a layer mask picks one wall out of three");

    n = aver_phys_raycast_all(300, 0, 0, 1, 0, 0, 2000, all, 0, 0, 8, b, dist, pts, nrm);
    check(n >= 1 && b[0] == w1 && dist[0] < 1.0f, "a ray starting INSIDE a solid box hits it at distance 0");

    aver_phys_shutdown();
}

static void testPointQuery() {
    AVER_INFO("=== point query ===");
    aver_phys_init();
    const int32_t a  = wall(0, 0, 0, 50, 50, 50);        // x -50..50
    const int32_t a2 = wall(30, 0, 0, 50, 50, 50);       // x -20..80
    const int32_t c  = wall(0, 300, 0, 50, 50, 50);
    const int32_t d  = wall(0, 0, 300, 50, 50, 50);
    const int32_t ball = aver_phys_add_dynamic_sphere(-300, 0, 0, 40, 1.0f);
    const uint32_t all = AVER_PHYS_LAYER_MASK_ALL;
    int32_t out[8] = {0};

    int32_t n = aver_phys_point_query(40, 0, 0, all, 0, 0, out, 8);
    check(n == 2, "a point inside two overlapping boxes returns both, got " + std::to_string(n));
    n = aver_phys_point_query(-40, 0, 0, all, 0, 0, out, 8);
    check(n == 1 && out[0] == a, "one only inside A returns A");
    n = aver_phys_point_query(0, 300, 0, all, 0, 0, out, 8);
    check(n == 1 && out[0] == c, "a point inside the +Y box returns it");
    n = aver_phys_point_query(0, 0, 300, all, 0, 0, out, 8);
    check(n == 1 && out[0] == d, "and inside the +Z box");
    n = aver_phys_point_query(0, 200, 0, all, 0, 0, out, 8);
    check(n == 0, "a point in the gap returns nothing");
    n = aver_phys_point_query(-300, 0, 30, all, 0, 0, out, 8);
    check(n == 1 && out[0] == ball, "a point inside a sphere returns it");
    n = aver_phys_point_query(-300, 0, 50, all, 0, 0, out, 8);
    check(n == 0, "and just outside it does not");
    n = aver_phys_point_query(40, 0, 0, all, 0, 0, out, 1);
    check(n == 1, "maxCount 1 returns one");
    n = aver_phys_point_query(40, 0, 0, all, a, 0, out, 8);
    check(n == 1 && out[0] == a2, "ignoreBody drops A and leaves A2");
    aver_phys_body_set_layer(a2, 6);
    n = aver_phys_point_query(40, 0, 0, AVER_PHYS_LAYER_BIT(6), 0, 0, out, 8);
    check(n == 1 && out[0] == a2, "a layer mask picks A2 out");
    n = aver_phys_point_query(40, 0, 0, all, 0, 0, nullptr, 8);
    check(n == 0, "a null output array is a count of 0");

    aver_phys_shutdown();
}

static void testSensors() {
    AVER_INFO("=== sensors are ignored unless asked for ===");
    aver_phys_init();
    const int32_t sensor = aver_phys_add_sensor_box(0, 0, 0, 100, 100, 100);   // x -100..100
    const int32_t back   = wall(500, 0, 0, 10, 200, 200);                       // face at x = 490
    const uint32_t all = AVER_PHYS_LAYER_MASK_ALL;
    int32_t b[8] = {0};
    float dist[8] = {0}, pts[24] = {0}, nrm[24] = {0}, d = 0, pt[3], n3[3];

    int32_t n = aver_phys_raycast_all(-500, 0, 0, 1, 0, 0, 2000, all, 0, 0, 8, b, dist, pts, nrm);
    check(n == 1 && b[0] == back, "a ray through a sensor sees only the wall behind it by default");
    n = aver_phys_raycast_all(-500, 0, 0, 1, 0, 0, 2000, all, 0, AVER_PHYS_QUERY_INCLUDE_SENSORS, 8, b, dist, pts, nrm);
    check(n == 2 && b[0] == sensor && b[1] == back, "and both, sensor first, with INCLUDE_SENSORS");
    check(near(dist[0], 400), "the sensor's face is 400 cm away, got " + f2s(dist[0]));

    int32_t h = boxCast(-500, 0, 0, 20, 20, 20, kIdentity, 2000, 0, 0, all, 0, 0, &d, pt, n3);
    check(h == back && near(d, 970.0f), "a box cast passes through it to the wall at 970, got " + f2s(d));
    h = boxCast(-500, 0, 0, 20, 20, 20, kIdentity, 2000, 0, 0, all, 0, AVER_PHYS_QUERY_INCLUDE_SENSORS, &d, pt, n3);
    check(h == sensor && near(d, 380.0f), "and hits the sensor at 400 - 20 = 380 with the flag, got " + f2s(d));
    h = aver_phys_capsule_cast(-500, 0, 0, 20, 100, 0, 0, 0, 1, 2000, 0, 0, all, 0, 0, &d, pt, n3);
    check(h == back, "a capsule cast ignores it as well");

    int32_t out[8] = {0};
    n = aver_phys_overlap_box(0, 0, 0, 10, 10, 10, 0, 0, 0, 1, all, 0, 0, out, 8);
    check(n == 0, "an overlap box sitting inside a sensor finds nothing by default");
    n = aver_phys_overlap_box(0, 0, 0, 10, 10, 10, 0, 0, 0, 1, all, 0, AVER_PHYS_QUERY_INCLUDE_SENSORS, out, 8);
    check(n == 1 && out[0] == sensor, "and finds it with the flag");
    n = aver_phys_overlap_capsule(0, 0, 0, 20, 100, 0, 0, 0, 1, all, 0, 0, out, 8);
    check(n == 0, "same for a capsule");
    n = aver_phys_point_query(0, 0, 0, all, 0, 0, out, 8);
    check(n == 0, "a point inside a sensor finds nothing by default");
    n = aver_phys_point_query(0, 0, 0, all, 0, AVER_PHYS_QUERY_INCLUDE_SENSORS, out, 8);
    check(n == 1 && out[0] == sensor, "and finds it with the flag");

    // The older _ex queries predate this flag and still see sensors; nothing about them changed.
    float p[3], nn[3]; int32_t ent = 0;
    check(aver_phys_raycast_ex(-500, 0, 0, 1, 0, 0, 2000, all, 0, p, nn, &ent) == sensor,
          "aver_phys_raycast_ex still sees the sensor (its behaviour is unchanged)");

    aver_phys_shutdown();
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO("spatial queries: shape casts, overlaps, raycast_all, point query");

    testBoxCast();
    testCapsuleCast();
    testBodyCast();
    testOverlaps();
    testRaycastAll();
    testPointQuery();
    testSensors();

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
