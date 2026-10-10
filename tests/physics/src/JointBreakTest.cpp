// JointBreakTest -- breakable joints, soft limits, and the per-body extras (locked axes, continuous
// collision, velocity caps, sleep permission, sensor), through the plain-C ABI only.
//
// OBSERVED MOTION, NOT RETURN CODES, as JointTest.cpp beside it. Each claim is checked against a number
// worked out without the code under test:
//
//   * a 10 kg weight on a point joint pulls with m*g = ~98 N, so a 200 N joint holds it and a 50 N one
//     lets go; an impulse that overloads the 200 N one breaks it too,
//   * a hinge pushed into its limit at speed stops at the limit when hard, goes past it when sprung, and
//     comes back,
//   * a locked axis is checked on EACH engine axis separately: Jolt's axes are a permutation of the
//     engine's, so a lock that landed on the wrong one passes any single-axis test but fails the other two,
//   * CCD is checked against its own absence: the same fast sphere and the same thin wall, discrete
//     tunnels through and continuous does not.
//
// Hand-run, exit code = failure count, same shape as JointTest.cpp.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/core/Math.hpp"
#include "aver/physics/physics_abi.h"
#include "aver/physics/physics_body_ext_abi.h"
#include "aver/physics/physics_joints_abi.h"

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

static bool near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static std::string f2s(f32 v) { char b[64]; std::snprintf(b, sizeof b, "%.4f", v); return b; }

static const f32 kDt = 1.0f / 60.0f;

static void step(int n) { for (int i = 0; i < n; ++i) aver_phys_step(kDt); }

static void beginWorldNoGravity() {
    aver_phys_init();
    aver_phys_set_gravity(0.0f, 0.0f, 0.0f);
}

static void pos(int32_t b, float p[3]) { aver_phys_body_position(b, p); }

// ---- breakable joints --------------------------------------------------------------------------------

// A weight hung 100 cm below a pivot, joined to the world by a point joint set to `breakN`. Returns the
// joint; the weight is written to `outBody`.
static int32_t hangWeight(float breakN, int32_t& outBody) {
    outBody = aver_phys_add_dynamic_box(0.0f, 0.0f, 400.0f, 10.0f, 10.0f, 10.0f, 10.0f);   // 10 kg
    const float pivot[3] = {0.0f, 0.0f, 500.0f};
    const int32_t j = aver_phys_joint_point(outBody, AVER_PHYS_WORLD_BODY, pivot);
    if (j) aver_phys_joint_set_break(j, breakN, 0.0f);
    return j;
}

static void testHangingWeightBreaksOnlyWhenOverloaded() {
    AVER_INFO("=== a weight on a breakable joint: held below the threshold, dropped above it ===");

    // 10 kg * 9.8 m/s^2 = ~98 N. A 200 N joint carries it.
    aver_phys_init();
    int32_t weight = 0;
    int32_t strong = hangWeight(200.0f, weight);
    check(strong != 0, "the strong joint is created");
    step(180);
    float p[3];
    pos(weight, p);
    check(aver_phys_joint_break_count() == 0, "98 N on a 200 N joint: no break event");
    check(aver_phys_joint_enabled(strong) == 1, "the 200 N joint is still enabled");
    check(near(p[2], 400.0f, 3.0f), "and the weight still hangs at z=400, got " + f2s(p[2]));

    aver_phys_shutdown();

    // 50 N is below the 98 N the weight pulls: it breaks, and falls.
    aver_phys_init();
    int32_t w2 = 0;
    const int32_t weak = hangWeight(50.0f, w2);
    step(120);
    check(aver_phys_joint_break_count() == 1, "98 N on a 50 N joint: exactly one break event");
    int32_t ej = 0; float fN = 0.0f, tNm = 0.0f;
    check(aver_phys_joint_break_get(0, &ej, &fN, &tNm) == 1, "the event reads back");
    check(ej == weak, "it names the joint that broke");
    check(fN > 50.0f && fN < 300.0f, "with the force that broke it, ~98 N, got " + f2s(fN));
    check(aver_phys_joint_enabled(weak) == 0, "the broken joint is disabled");
    float q[3]; pos(w2, q);
    check(q[2] < 300.0f, "and the weight has fallen, z=" + f2s(q[2]));

    // Drained once: after a clear the queue is empty and nothing more arrives for the dead joint.
    aver_phys_joint_break_clear();
    check(aver_phys_joint_break_count() == 0, "clearing empties the queue");
    step(60);
    check(aver_phys_joint_break_count() == 0, "a broken joint does not report again");
    check(aver_phys_joint_break_get(0, &ej, &fN, &tNm) == 0, "get past the end is refused");

    aver_phys_shutdown();
}

// The same weight, given a sideways speed the joint has to cancel at its pivot point, loads the joint far
// past what it carries at rest.
static void testImpactBreaksAJointThatHoldsAtRest() {
    AVER_INFO("=== an impact breaks a joint that carries the same weight at rest ===");
    aver_phys_init();
    int32_t weight = 0;
    const int32_t j = hangWeight(400.0f, weight);
    step(60);
    check(aver_phys_joint_break_count() == 0, "98 N on a 400 N joint: holds at rest");
    // The pivot is 100 cm above the weight's centre, so a 20 m/s sideways velocity puts 20 m/s on the
    // pivot point itself, which the joint must remove in one step: thousands of newtons.
    aver_phys_body_set_velocity(weight, 2000.0f, 0.0f, 0.0f);
    step(10);
    check(aver_phys_joint_break_count() == 1, "a 20 m/s sideways kick (thousands of N) breaks it");
    int32_t ej = 0; float fN = 0.0f, tNm = 0.0f;
    aver_phys_joint_break_get(0, &ej, &fN, &tNm);
    check(ej == j && fN > 400.0f, "the reported load is past the threshold, got " + f2s(fN) + " N");
    aver_phys_shutdown();
}

static void testBreakRefusals() {
    AVER_INFO("=== break arguments ===");
    beginWorldNoGravity();
    const int32_t b = aver_phys_add_dynamic_box(0, 0, 0, 10, 10, 10, 1.0f);
    const float pt[3] = {0, 0, 0};
    const int32_t j = aver_phys_joint_point(b, AVER_PHYS_WORLD_BODY, pt);
    check(aver_phys_joint_set_break(j, 100.0f, 50.0f) == 1, "a valid threshold is accepted");
    check(aver_phys_joint_set_break(j, -1.0f, 0.0f) == 0, "a negative force is refused");
    check(aver_phys_joint_set_break(j, 0.0f, NAN) == 0, "a NaN torque is refused");
    check(aver_phys_joint_set_break(j, 0.0f, 0.0f) == 1, "0/0 disarms it");
    check(aver_phys_joint_set_break(9999, 1.0f, 1.0f) == 0, "a dead handle is refused");
    aver_phys_joint_remove(j);
    aver_phys_shutdown();
}

// ---- soft limits -------------------------------------------------------------------------------------

// A box centred on a world hinge (so spinning it about its centre is spinning it about the hinge), in zero
// gravity, spun into its +-0.5 rad limit at 3 rad/s. Returns the
// largest |angle| reached over 2 s and, through `finalAngle`, where it ended up.
static float spinIntoLimit(float springHz, float damping, float& finalAngle) {
    beginWorldNoGravity();
    const int32_t b = aver_phys_add_dynamic_box(0.0f, 0.0f, 0.0f, 20.0f, 20.0f, 20.0f, 1.0f);
    const float pivot[3] = {0, 0, 0}, axis[3] = {1, 0, 0}, normal[3] = {0, 1, 0};
    const int32_t j = aver_phys_joint_hinge(b, AVER_PHYS_WORLD_BODY, pivot, axis, normal, -0.5f, 0.5f);
    if (springHz > 0.0f) aver_phys_joint_set_limit_spring(j, springHz, damping);
    aver_phys_body_set_angular_velocity(b, 3.0f, 0.0f, 0.0f);
    float peak = 0.0f, v = 0.0f;
    for (int i = 0; i < 180; ++i) {
        aver_phys_step(kDt);
        aver_phys_joint_value(j, &v);
        if (std::fabs(v) > peak) peak = std::fabs(v);
    }
    finalAngle = std::fabs(v);
    aver_phys_shutdown();
    return peak;
}

static void testSoftLimitOvershootsAndReturns() {
    AVER_INFO("=== a limit spring lets a hinge exceed its limit briefly, then pulls it back ===");
    float hardEnd = 0.0f, softEnd = 0.0f;
    const float hardPeak = spinIntoLimit(0.0f, 0.0f, hardEnd);
    const float softPeak = spinIntoLimit(1.5f, 1.0f, softEnd);
    check(hardPeak < 0.5f + 0.06f, "hard limit: peak |angle| ~0.5, got " + f2s(hardPeak));
    check(softPeak > hardPeak + 0.04f, "sprung: goes past it, peak " + f2s(softPeak) + " vs hard " + f2s(hardPeak));
    check(softEnd < softPeak - 0.02f, "and comes back from the peak, ended at " + f2s(softEnd));
    check(softEnd < 0.5f + 0.03f, "to the limit rather than staying past it, ended at " + f2s(softEnd));

    beginWorldNoGravity();
    const int32_t b = aver_phys_add_dynamic_box(0, 0, 0, 10, 10, 10, 1.0f);
    const int32_t c = aver_phys_add_dynamic_box(0, 50, 0, 10, 10, 10, 1.0f);
    const float pt[3] = {0, 0, 0}, ax[3] = {1, 0, 0}, nm[3] = {0, 1, 0};
    const int32_t ball = aver_phys_joint_point(b, AVER_PHYS_WORLD_BODY, pt);
    check(aver_phys_joint_set_limit_spring(ball, 2.0f, 0.5f) == 0, "a point joint has no limit spring");
    const int32_t hinge = aver_phys_joint_hinge(c, AVER_PHYS_WORLD_BODY, pt, ax, nm, -0.5f, 0.5f);
    check(hinge != 0 && aver_phys_joint_set_limit_spring(hinge, 2.0f, 0.5f) == 1, "a hinge takes one");
    check(aver_phys_joint_set_limit_spring(hinge, -1.0f, 0.5f) == 0, "a negative frequency is refused");
    check(aver_phys_joint_set_limit_spring(hinge, 0.0f, 0.0f) == 1, "0 Hz makes it hard again");
    aver_phys_shutdown();
}

// ---- locked axes -------------------------------------------------------------------------------------

static void testLockedTranslationOnEachAxis() {
    AVER_INFO("=== locking one translation axis freezes that engine axis only ===");
    const uint32_t flag[3] = {AVER_PHYS_LOCK_TRANSLATION_X, AVER_PHYS_LOCK_TRANSLATION_Y, AVER_PHYS_LOCK_TRANSLATION_Z};
    const char* name[3] = {"X", "Y", "Z"};
    for (int axis = 0; axis < 3; ++axis) {
        beginWorldNoGravity();
        const int32_t b = aver_phys_add_dynamic_box(0, 0, 0, 10, 10, 10, 5.0f);
        check(aver_phys_body_set_locked_axes(b, flag[axis]) == 1, std::string("lock translation ") + name[axis]);
        aver_phys_body_set_velocity(b, 100.0f, 100.0f, 100.0f);
        step(60);
        float p[3]; pos(b, p);
        for (int i = 0; i < 3; ++i) {
            if (i == axis) check(near(p[i], 0.0f, 0.01f), std::string("axis ") + name[i] + " stays put, got " + f2s(p[i]));
            else           check(p[i] > 80.0f, std::string("axis ") + name[i] + " moved freely, got " + f2s(p[i]));
        }
        uint32_t mask = 0;
        check(aver_phys_body_locked_axes(b, &mask) == 1 && mask == flag[axis], "the mask reads back unchanged");
        aver_phys_shutdown();
    }
}

static void testLockedRotationOnEachAxis() {
    AVER_INFO("=== locking one rotation axis stops spin about that engine axis only ===");
    const uint32_t flag[3] = {AVER_PHYS_LOCK_ROTATION_X, AVER_PHYS_LOCK_ROTATION_Y, AVER_PHYS_LOCK_ROTATION_Z};
    const char* name[3] = {"X", "Y", "Z"};
    for (int axis = 0; axis < 3; ++axis) {
        for (int spin = 0; spin < 3; ++spin) {
            beginWorldNoGravity();
            const int32_t b = aver_phys_add_dynamic_box(0, 0, 0, 10, 10, 10, 5.0f);   // a cube: no gyroscopic coupling
            aver_phys_body_set_locked_axes(b, flag[axis]);
            float w[3] = {0, 0, 0};
            w[spin] = 3.0f;
            aver_phys_body_set_angular_velocity(b, w[0], w[1], w[2]);
            step(30);
            float q[4]; aver_phys_body_rotation(b, q);
            const float turned = 2.0f * std::acos(std::fmin(1.0f, std::fabs(q[3])));
            if (spin == axis)
                check(turned < 0.01f, std::string("spin about locked ") + name[axis] + " does nothing, turned " + f2s(turned));
            else
                check(turned > 0.5f, std::string("spin about ") + name[spin] + " with " + name[axis] + " locked still turns, got " + f2s(turned));
            aver_phys_shutdown();
        }
    }
}

static void testPlaneLock() {
    AVER_INFO("=== a body locked to a plane stays on it under gravity and a push ===");
    aver_phys_init();   // default gravity
    const int32_t b = aver_phys_add_dynamic_box(0, 0, 100, 10, 10, 10, 5.0f);
    const uint32_t planeXY = AVER_PHYS_LOCK_TRANSLATION_Z | AVER_PHYS_LOCK_ROTATION_X | AVER_PHYS_LOCK_ROTATION_Y;
    check(aver_phys_body_set_locked_axes(b, planeXY) == 1, "lock Z translation and X/Y rotation");
    aver_phys_body_add_impulse(b, 1000.0f, 500.0f, 3000.0f);
    aver_phys_body_set_angular_velocity(b, 4.0f, 3.0f, 2.0f);
    step(60);
    float p[3]; pos(b, p);
    float q[4]; aver_phys_body_rotation(b, q);
    check(near(p[2], 100.0f, 0.01f), "z unchanged despite gravity and an upward impulse, got " + f2s(p[2]));
    check(p[0] > 100.0f && p[1] > 50.0f, "x and y moved: " + f2s(p[0]) + ", " + f2s(p[1]));
    check(std::fabs(q[0]) < 1e-3f && std::fabs(q[1]) < 1e-3f, "no rotation about X or Y");
    check(std::fabs(q[2]) > 0.1f, "but it spun about Z, qz=" + f2s(q[2]));
    aver_phys_shutdown();
}

static void testLockKeepsMass() {
    AVER_INFO("=== locking keeps the mass, including through a full translation lock ===");
    beginWorldNoGravity();
    const int32_t b = aver_phys_add_dynamic_box(0, 0, 0, 10, 10, 10, 5.0f);

    aver_phys_body_set_locked_axes(b, AVER_PHYS_LOCK_ALL_ROTATION);
    float m = 0.0f;
    aver_phys_body_mass(b, &m);
    check(near(m, 5.0f, 0.01f), "rotation locked: still 5 kg, got " + f2s(m));
    aver_phys_body_add_impulse(b, 500.0f, 0.0f, 0.0f);   // kg*cm/s: v = 500/5 = 100 cm/s
    float v[3];
    aver_phys_body_velocity(b, v);
    check(near(v[0], 100.0f, 0.5f), "so an impulse of 500 gives 100 cm/s, got " + f2s(v[0]));

    // All translation locked: nothing a force or impulse does moves it, and Jolt keeps no mass for it.
    aver_phys_body_set_velocity(b, 0, 0, 0);
    check(aver_phys_body_set_locked_axes(b, AVER_PHYS_LOCK_ALL_TRANSLATION) == 1, "lock all translation");
    aver_phys_body_add_impulse(b, 500.0f, 0.0f, 0.0f);
    aver_phys_body_add_force(b, 0.0f, 90000.0f, 0.0f);
    step(30);
    float p[3];
    pos(b, p);
    check(near(p[0], 0.0f, 0.01f) && near(p[1], 0.0f, 0.01f), "it does not travel, at " + f2s(p[0]) + ", " + f2s(p[1]));

    // Unlocked, the mass has come back, not a default or a 1/0.
    aver_phys_body_set_locked_axes(b, 0);
    aver_phys_body_mass(b, &m);
    check(near(m, 5.0f, 0.01f), "unlocked again: the 5 kg is back, got " + f2s(m));
    aver_phys_body_set_velocity(b, 0, 0, 0);
    aver_phys_body_add_impulse(b, 500.0f, 0.0f, 0.0f);
    aver_phys_body_velocity(b, v);
    check(near(v[0], 100.0f, 0.5f), "and an impulse of 500 again gives 100 cm/s, got " + f2s(v[0]));
    aver_phys_shutdown();
}

static void testLockRefusals() {
    AVER_INFO("=== locked-axes refusals ===");
    beginWorldNoGravity();
    const int32_t dyn = aver_phys_add_dynamic_box(0, 0, 0, 10, 10, 10, 1.0f);
    const int32_t stat = aver_phys_add_static_box(0, 0, -100, 100, 100, 10);
    check(aver_phys_body_set_locked_axes(dyn, 0x3Fu) == 0, "all six locked is refused");
    check(aver_phys_body_set_locked_axes(dyn, 0x40u) == 0, "an unknown bit is refused");
    check(aver_phys_body_set_locked_axes(stat, AVER_PHYS_LOCK_TRANSLATION_X) == 0, "a static body is refused");
    check(aver_phys_body_set_locked_axes(9999, AVER_PHYS_LOCK_TRANSLATION_X) == 0, "a dead handle is refused");
    uint32_t mask = 7;
    check(aver_phys_body_locked_axes(stat, &mask) == 0, "reading a static body's lock is refused");

    // Every one of the 63 legal masks round-trips through Jolt's different axes and back.
    bool all = true;
    for (uint32_t m = 0; m < 0x3F; ++m) {
        uint32_t out = 0xFFFFFFFFu;
        if (aver_phys_body_set_locked_axes(dyn, m) != 1 || aver_phys_body_locked_axes(dyn, &out) != 1 || out != m) all = false;
    }
    check(all, "all 63 legal masks read back exactly");
    aver_phys_shutdown();
}

// ---- continuous collision ----------------------------------------------------------------------------

// A 5 cm sphere fired at 33 m/s (55 cm per step) at a 2 cm thick wall: it never overlaps the wall at the
// end of any step, so only a swept test can see it.
static float fireAtThinWall(bool ccd) {
    beginWorldNoGravity();
    aver_phys_add_static_box(300.0f, 0.0f, 0.0f, 1.0f, 100.0f, 100.0f);
    const int32_t b = aver_phys_add_dynamic_sphere(0.0f, 0.0f, 0.0f, 5.0f, 1.0f);
    if (ccd) aver_phys_body_set_ccd(b, 1);
    aver_phys_body_set_velocity(b, 3300.0f, 0.0f, 0.0f);
    step(90);
    float p[3]; pos(b, p);
    aver_phys_shutdown();
    return p[0];
}

static void testCcdStopsTunnelling() {
    AVER_INFO("=== continuous collision stops a fast sphere that discrete mode lets through ===");
    const float discrete = fireAtThinWall(false);
    const float swept = fireAtThinWall(true);
    check(discrete > 320.0f, "discrete: the sphere tunnels through the wall, ended at x=" + f2s(discrete));
    check(swept < 300.0f && swept > 280.0f, "CCD: it stops at the wall face (~294), ended at x=" + f2s(swept));

    beginWorldNoGravity();
    const int32_t b = aver_phys_add_dynamic_sphere(0, 0, 0, 5, 1);
    const int32_t s = aver_phys_add_static_box(0, 0, -100, 100, 100, 10);
    int32_t on = -1;
    check(aver_phys_body_ccd(b, &on) == 1 && on == 0, "CCD is off by default");
    aver_phys_body_set_ccd(b, 1);
    check(aver_phys_body_ccd(b, &on) == 1 && on == 1, "and reads on after being set");
    check(aver_phys_body_set_ccd(s, 1) == 0, "a static body has no motion quality to set");
    aver_phys_shutdown();
}

// ---- velocity caps, sleeping, sensor ------------------------------------------------------------------

static void testVelocityCaps() {
    AVER_INFO("=== velocity caps clamp, in cm/s and deg/s ===");
    beginWorldNoGravity();
    const int32_t b = aver_phys_add_dynamic_box(0, 0, 0, 10, 10, 10, 1.0f);
    float lin = 0.0f, ang = 0.0f;
    aver_phys_body_max_linear_velocity(b, &lin);
    aver_phys_body_max_angular_velocity(b, &ang);
    check(near(lin, 50000.0f, 1.0f), "Jolt's default linear cap reads 50000 cm/s, got " + f2s(lin));
    check(ang > 2000.0f && ang < 3000.0f, "and its angular cap ~2700 deg/s, got " + f2s(ang));

    check(aver_phys_body_set_max_linear_velocity(b, 1000.0f) == 1, "set the linear cap to 1000 cm/s");
    aver_phys_body_set_velocity(b, 5000.0f, 0.0f, 0.0f);
    step(5);
    float v[3]; aver_phys_body_velocity(b, v);
    check(v[0] <= 1001.0f && v[0] > 900.0f, "a 5000 cm/s push is clamped to the cap, got " + f2s(v[0]));
    aver_phys_body_max_linear_velocity(b, &lin);
    check(near(lin, 1000.0f, 0.5f), "the cap reads back");

    check(aver_phys_body_set_max_angular_velocity(b, 90.0f) == 1, "set the angular cap to 90 deg/s");
    aver_phys_body_set_angular_velocity(b, 0.0f, 0.0f, 10.0f);   // 10 rad/s, far past 90 deg/s = 1.57 rad/s
    step(5);
    float w[3]; aver_phys_body_angular_velocity(b, w);
    const float speed = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    check(speed <= 1.58f && speed > 1.4f, "spin is clamped to 90 deg/s = 1.571 rad/s, got " + f2s(speed));
    aver_phys_body_max_angular_velocity(b, &ang);
    check(near(ang, 90.0f, 0.05f), "and reads back in degrees, got " + f2s(ang));

    check(aver_phys_body_set_max_linear_velocity(b, -1.0f) == 0, "a negative cap is refused");
    aver_phys_shutdown();
}

static void testAllowSleeping() {
    AVER_INFO("=== a body told not to sleep stays awake at rest ===");
    aver_phys_init();
    aver_phys_add_static_box(0, 0, -10, 500, 500, 10);
    const int32_t sleepy = aver_phys_add_dynamic_box(-100, 0, 10, 10, 10, 10, 1.0f);
    const int32_t wired = aver_phys_add_dynamic_box(100, 0, 10, 10, 10, 10, 1.0f);
    int32_t on = -1;
    check(aver_phys_body_allow_sleeping(wired, &on) == 1 && on == 1, "sleeping is allowed by default");
    check(aver_phys_body_set_allow_sleeping(wired, 0) == 1, "switch it off for one body");
    check(aver_phys_body_allow_sleeping(wired, &on) == 1 && on == 0, "it reads off");
    step(240);
    check(aver_phys_body_is_active(sleepy) == 0, "the default body fell asleep at rest");
    check(aver_phys_body_is_active(wired) == 1, "the other is still awake");
    aver_phys_shutdown();
}

static void testBodyBecomesSensor() {
    AVER_INFO("=== an existing body becomes a sensor, and a solid again ===");

    // As a sensor a ball falls through it and the overlap queue reports the entry.
    aver_phys_init();
    const int32_t plate = aver_phys_add_static_box(0, 0, 0, 100, 100, 10);
    const int32_t ball = aver_phys_add_dynamic_sphere(0, 0, 200, 10, 1);
    int32_t on = -1;
    check(aver_phys_body_set_sensor(plate, 1) == 1, "turn the plate into a sensor");
    check(aver_phys_body_sensor(plate, &on) == 1 && on == 1, "it reads as a sensor");
    bool entered = false;
    for (int i = 0; i < 120; ++i) {
        aver_phys_step(kDt);
        for (int e = 0, n = aver_phys_overlap_count(); e < n; ++e) {
            int32_t s = 0, bo = 0, en = 0;
            aver_phys_overlap_get(e, &s, &bo, &en);
            if (s == plate && bo == ball && en == 1) entered = true;
        }
    }
    float p[3]; pos(ball, p);
    check(entered, "the overlap queue reported the ball entering");
    check(p[2] < -100.0f, "and the ball fell through, z=" + f2s(p[2]));
    aver_phys_shutdown();

    // Switched on and back off, it is a floor again.
    aver_phys_init();
    const int32_t floor = aver_phys_add_static_box(0, 0, 0, 100, 100, 10);
    const int32_t ball2 = aver_phys_add_dynamic_sphere(0, 0, 100, 10, 1);
    aver_phys_body_set_sensor(floor, 1);
    aver_phys_body_set_sensor(floor, 0);
    check(aver_phys_body_sensor(floor, &on) == 1 && on == 0, "back to solid");
    step(120);
    float q[3]; pos(ball2, q);
    check(near(q[2], 20.0f, 2.0f), "the ball rests on top (10 + radius 10), z=" + f2s(q[2]));
    aver_phys_shutdown();
}

// ---------------------------------------------------------------------------------------------------

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO("joints and bodies: breakable joints, soft limits, locked axes, CCD, caps, sleep, sensor");

    testHangingWeightBreaksOnlyWhenOverloaded();
    testImpactBreaksAJointThatHoldsAtRest();
    testBreakRefusals();
    testSoftLimitOvershootsAndReturns();
    testLockedTranslationOnEachAxis();
    testLockedRotationOnEachAxis();
    testPlaneLock();
    testLockKeepsMass();
    testLockRefusals();
    testCcdStopsTunnelling();
    testVelocityCaps();
    testAllowSleeping();
    testBodyBecomesSensor();

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
