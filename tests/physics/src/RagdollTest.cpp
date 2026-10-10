// RagdollTest -- the ragdoll ABI in physics_ragdoll_abi.h, through the plain-C surface only.
//
// THE STANDARD IS OBSERVED MOTION. A ragdoll built with the wrong joint frame, the wrong mass split or
// motors that never drive anything still returns a good handle, so almost every check below builds a
// chain, steps the world and asks where the parts went:
//
//   * limp falls and comes to rest lying on the floor, still joined,
//   * kinematic follows a moved target and stays on it (no drift once the target stops changing),
//   * powered holds a pose against gravity better than limp, and strength 0 does not hold at all,
//   * parts of one ragdoll never touch each other, while another ragdoll's parts do (the control),
//   * mass is shared by capsule volume, bones without a part ride their ancestor's part,
//   * body_of_bone / of_body round-trip, an impulse lands as impulse / mass in cm/s,
//   * destroy, remove_body and shutdown leave nothing behind, and bad input is refused with a reason.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/core/Math.hpp"
#include "aver/physics/physics_abi.h"
#include "aver/physics/physics_joints_abi.h"
#include "aver/physics/physics_layers_abi.h"
#include "aver/physics/physics_ragdoll_abi.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace aver;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool near(f32 a, f32 b, f32 eps) { return std::fabs(a - b) <= eps; }
static std::string f2s(f32 v) { char b[64]; std::snprintf(b, sizeof b, "%.3f", v); return b; }
static const f32 kDt = 1.0f / 60.0f;

// ---- Helpers ------------------------------------------------------------------------------------------

// A floor whose top is z = 0, 400 m square. Returns its body handle.
static int32_t beginWorld() {
    aver_phys_init();
    return aver_phys_add_static_box(0.0f, 0.0f, -50.0f, 20000.0f, 20000.0f, 50.0f);
}

static void run(int steps) {
    for (int i = 0; i < steps; ++i) aver_phys_step(kDt);
}

static Vec3 posOf(int32_t body) {
    float p[3] = {0, 0, 0};
    aver_phys_body_position(body, p);
    return Vec3(p[0], p[1], p[2]);
}

static Vec3 velOf(int32_t body) {
    float v[3] = {0, 0, 0};
    aver_phys_body_velocity(body, v);
    return Vec3(v[0], v[1], v[2]);
}

static f32 massOf(int32_t body) {
    float m = 0.0f;
    aver_phys_body_mass(body, &m);
    return m;
}

static int32_t lastError() { return aver_phys_last_error(); }
static int32_t errCode(AbiError e) { return static_cast<int32_t>(e); }

// A skeleton as the ABI takes it. Every bone added with addBone is a part of `lenCm` and `radiusCm`
// running along the bone's local +X from its origin; radius 0 is a bone with no part.
struct Chain {
    int n = 0;
    std::vector<int32_t> parents;
    std::vector<float> pos, rot, caps, lim;
    float massKg = 30.0f;

    void addBone(int parent, f32 x, f32 y, f32 z, f32 lenCm, f32 radiusCm) {
        parents.push_back(parent);
        pos.insert(pos.end(), {x, y, z});
        rot.insert(rot.end(), {0.0f, 0.0f, 0.0f, 1.0f});
        caps.insert(caps.end(), {0.0f, 0.0f, 0.0f, lenCm, 0.0f, 0.0f, radiusCm});
        lim.insert(lim.end(), {80.0f, 80.0f, 30.0f});
        ++n;
    }
};

// Three 50 cm parts of radius 8 laid end to end along +X at height z, the root at x = 0.
static Chain chain3(f32 z) {
    Chain c;
    for (int i = 0; i < 3; ++i) c.addBone(i - 1, 50.0f * static_cast<f32>(i), 0.0f, z, 50.0f, 8.0f);
    return c;
}

static int32_t create(const Chain& c, uint32_t layer = 0) {
    return aver_phys_ragdoll_create(c.n, c.parents.data(), c.pos.data(), c.rot.data(), c.caps.data(),
                                    c.lim.data(), c.massKg, layer);
}

static int32_t partOf(int32_t rd, int bone) { return aver_phys_ragdoll_body_of_bone(rd, bone); }

struct Pose { std::vector<float> p, r; };

static Pose poseOf(int32_t rd, int n) {
    Pose out;
    out.p.assign(3 * n, 0.0f);
    out.r.assign(4 * n, 0.0f);
    aver_phys_ragdoll_get_pose(rd, out.p.data(), out.r.data());
    return out;
}

static Vec3 bonePos(const Pose& po, int i) { return Vec3(po.p[3 * i], po.p[3 * i + 1], po.p[3 * i + 2]); }
static Quat boneRot(const Pose& po, int i) { return Quat(po.r[4 * i], po.r[4 * i + 1], po.r[4 * i + 2], po.r[4 * i + 3]); }
static f32 quatAlign(const Quat& a, const Quat& b) { return std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w); }

// ---- Building, counts, mass -------------------------------------------------------------------------

static void testBuildAndMass() {
    AVER_INFO("=== build, counts, mass sharing ===");
    beginWorld();
    const int32_t base = aver_phys_body_count();
    check(aver_phys_ragdoll_count() == 0, "no ragdolls in a fresh world");

    Chain c = chain3(300.0f);
    const int32_t rd = create(c);
    check(rd != 0, "a three-bone chain builds");
    check(aver_phys_ragdoll_count() == 1, "count is 1");
    check(aver_phys_ragdoll_bone_count(rd) == 3, "bone_count is the bone count");
    check(aver_phys_ragdoll_mode(rd) == AVER_PHYS_RAGDOLL_KINEMATIC, "a new ragdoll is kinematic");
    check(aver_phys_body_count() == base + 3, "each part is a registered body");

    bool distinct = true;
    int32_t part[3];
    for (int i = 0; i < 3; ++i) {
        part[i] = partOf(rd, i);
        float p[3];
        distinct = distinct && part[i] != 0 && aver_phys_body_position(part[i], p) == 1;
        for (int j = 0; j < i; ++j) distinct = distinct && part[i] != part[j];
    }
    check(distinct, "three distinct, live body handles");
    // The capsule runs 0..50 along +X from the bone, so its centre of mass is 25 cm along.
    const Vec3 p0 = posOf(part[0]);
    check(near(p0.x, 25.0f, 0.5f) && near(p0.y, 0.0f, 0.5f) && near(p0.z, 300.0f, 0.5f),
          "a part sits where its capsule was described, in cm");

    // Kinematic bodies have no mass to report; limp ones do. Equal capsules share the 30 kg equally.
    aver_phys_ragdoll_set_mode(rd, AVER_PHYS_RAGDOLL_LIMP);
    const f32 m0 = massOf(part[0]), m1 = massOf(part[1]), m2 = massOf(part[2]);
    check(near(m0, 10.0f, 0.05f) && near(m1, 10.0f, 0.05f) && near(m2, 10.0f, 0.05f),
          "equal capsules share the mass equally: " + f2s(m0) + " " + f2s(m1) + " " + f2s(m2));
    aver_phys_ragdoll_destroy(rd);

    // Unequal radii: the share follows the capsule volume pi r^2 L + 4/3 pi r^3.
    Chain u;
    u.massKg = 60.0f;
    u.addBone(-1, 0.0f, 0.0f, 300.0f, 50.0f, 8.0f);
    u.addBone(0, 50.0f, 0.0f, 300.0f, 50.0f, 16.0f);
    const int32_t ru = create(u);
    aver_phys_ragdoll_set_mode(ru, AVER_PHYS_RAGDOLL_LIMP);
    auto vol = [](f32 r, f32 l) { return kPi * r * r * l + 4.0f / 3.0f * kPi * r * r * r; };
    const f32 v0 = vol(8.0f, 50.0f), v1 = vol(16.0f, 50.0f);
    const f32 um0 = massOf(partOf(ru, 0)), um1 = massOf(partOf(ru, 1));
    check(near(um0 + um1, 60.0f, 0.1f), "the parts' masses sum to the total: " + f2s(um0 + um1));
    check(near(um1 / um0, v1 / v0, 0.03f * v1 / v0), "mass follows capsule volume: ratio " + f2s(um1 / um0) + " vs " + f2s(v1 / v0));
    aver_phys_shutdown();
}

// ---- Limp ---------------------------------------------------------------------------------------------

static void testLimp() {
    AVER_INFO("=== limp: falls, lands, rests, stays joined ===");
    beginWorld();
    Chain c = chain3(100.0f);
    const int32_t rd = create(c);
    aver_phys_ragdoll_set_mode(rd, AVER_PHYS_RAGDOLL_LIMP);
    check(aver_phys_ragdoll_mode(rd) == AVER_PHYS_RAGDOLL_LIMP, "mode reads back as limp");

    run(20);   // a third of a second: about 54 cm of free fall
    const f32 zMid = posOf(partOf(rd, 0)).z;
    check(zMid < 80.0f && zMid > 10.0f, "falling after a third of a second: z = " + f2s(zMid));

    run(400);
    bool onFloor = true, still = true;
    f32 worstZ = 0.0f, worstSpeed = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const int32_t b = partOf(rd, i);
        const f32 z = posOf(b).z, s = velOf(b).size();
        onFloor = onFloor && z > 5.0f && z < 14.0f;
        still = still && s < 20.0f;
        if (std::fabs(z - 8.0f) > std::fabs(worstZ - 8.0f)) worstZ = z;
        if (s > worstSpeed) worstSpeed = s;
    }
    check(onFloor, "every part rests on the floor, centre one radius up (worst z " + f2s(worstZ) + ")");
    check(still, "and has come to rest (worst speed " + f2s(worstSpeed) + " cm/s)");
    const f32 d01 = dist(posOf(partOf(rd, 0)), posOf(partOf(rd, 1)));
    const f32 d12 = dist(posOf(partOf(rd, 1)), posOf(partOf(rd, 2)));
    check(d01 > 40.0f && d01 < 60.0f && d12 > 40.0f && d12 < 60.0f,
          "the joints still hold the parts together: " + f2s(d01) + " " + f2s(d12));
    aver_phys_shutdown();
}

// ---- Kinematic --------------------------------------------------------------------------------------------

static void testKinematic() {
    AVER_INFO("=== kinematic: follows a moved target and stays on it ===");
    beginWorld();
    Chain c = chain3(300.0f);
    const int32_t rd = create(c);
    check(aver_phys_ragdoll_mode(rd) == AVER_PHYS_RAGDOLL_KINEMATIC, "starts kinematic");

    run(30);
    Pose rest = poseOf(rd, 3);
    check(near(bonePos(rest, 2).z, 300.0f, 0.5f), "held in the air at the creation pose, not falling");

    // The whole chain yawed 30 degrees about the origin and shifted: positions and rotations move together.
    const Quat q = Quat::fromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), 30.0f * kDegToRad);
    const Vec3 shift(100.0f, -60.0f, 30.0f);
    std::vector<float> tp, tr;
    std::vector<Vec3> want;
    for (int i = 0; i < 3; ++i) {
        const Vec3 w = q.rotate(Vec3(c.pos[3 * i], c.pos[3 * i + 1], c.pos[3 * i + 2])) + shift;
        want.push_back(w);
        tp.insert(tp.end(), {w.x, w.y, w.z});
        tr.insert(tr.end(), {q.x, q.y, q.z, q.w});
    }
    check(aver_phys_ragdoll_set_target(rd, tp.data(), tr.data()) == 1, "set_target accepts the pose");

    auto onTarget = [&](const std::string& when) {
        const Pose got = poseOf(rd, 3);
        f32 worstPos = 0.0f, worstAlign = 1.0f;
        for (int i = 0; i < 3; ++i) {
            worstPos = std::fmax(worstPos, dist(bonePos(got, i), want[i]));
            worstAlign = std::fmin(worstAlign, quatAlign(boneRot(got, i), q));
        }
        check(worstPos < 2.0f, when + ": every bone within 2 cm of the target (worst " + f2s(worstPos) + ")");
        check(worstAlign > 0.999f, when + ": and aligned with it (worst " + f2s(worstAlign) + ")");
    };
    run(30);
    onTarget("after half a second");
    run(120);
    onTarget("two seconds on, target unchanged");
    aver_phys_shutdown();
}

// ---- Powered ----------------------------------------------------------------------------------------------

// Pins the root to the world, holds the chain out horizontally in `mode`, and returns how far the tip
// part's centre has dropped after four seconds. Pinned because a free ragdoll falls as one piece whatever
// its joints do, which would make every mode look the same.
static f32 tipDrop(int mode, f32 strength) {
    beginWorld();
    Chain c = chain3(150.0f);
    const int32_t rd = create(c);
    aver_phys_ragdoll_set_strength(rd, strength);
    aver_phys_ragdoll_set_mode(rd, mode);
    aver_phys_ragdoll_set_target(rd, c.pos.data(), c.rot.data());
    const float pt[3] = {0.0f, 0.0f, 150.0f}, ax[3] = {1.0f, 0.0f, 0.0f}, ay[3] = {0.0f, 1.0f, 0.0f};
    const int32_t j = aver_phys_joint_fixed(partOf(rd, 0), AVER_PHYS_WORLD_BODY, pt, ax, ay);
    if (j == 0) AVER_ERROR("   the root pin was refused");
    run(240);
    const f32 drop = 150.0f - posOf(partOf(rd, 2)).z;
    aver_phys_shutdown();
    return drop;
}

static void testPowered() {
    AVER_INFO("=== powered: holds the pose against gravity ===");
    const f32 limp = tipDrop(AVER_PHYS_RAGDOLL_LIMP, 0.5f);
    const f32 stiff = tipDrop(AVER_PHYS_RAGDOLL_POWERED, 1.0f);
    const f32 weak = tipDrop(AVER_PHYS_RAGDOLL_POWERED, 0.0f);
    AVER_INFO("   tip drop (cm): limp {}  powered 1.0 {}  powered 0.0 {}", f2s(limp), f2s(stiff), f2s(weak));
    check(limp > 30.0f, "a limp chain pinned at the root sags: " + f2s(limp) + " cm");
    check(stiff < 0.5f * limp, "a powered one holds the pose better: " + f2s(stiff) + " cm");
    check(weak > 0.7f * limp, "strength 0 does not hold at all: " + f2s(weak) + " cm");

    // Mode changes keep working in both directions, and a dead handle is refused.
    beginWorld();
    Chain c = chain3(300.0f);
    const int32_t rd = create(c);
    check(aver_phys_ragdoll_set_mode(rd, AVER_PHYS_RAGDOLL_POWERED) == 1 &&
              aver_phys_ragdoll_mode(rd) == AVER_PHYS_RAGDOLL_POWERED, "kinematic -> powered");
    check(aver_phys_ragdoll_set_strength(rd, 5.0f) == 1, "an out-of-range strength is clamped, not refused");
    check(aver_phys_ragdoll_set_mode(rd, AVER_PHYS_RAGDOLL_LIMP) == 1, "powered -> limp");
    run(30);
    check(aver_phys_ragdoll_set_mode(rd, AVER_PHYS_RAGDOLL_KINEMATIC) == 1 &&
              aver_phys_ragdoll_mode(rd) == AVER_PHYS_RAGDOLL_KINEMATIC, "limp -> kinematic");
    run(10);
    aver_phys_ragdoll_set_target(rd, c.pos.data(), c.rot.data());
    run(60);
    check(near(bonePos(poseOf(rd, 3), 0).z, 300.0f, 1.5f), "kinematic again pulls the parts back to the target");
    aver_phys_shutdown();
}

// ---- Parts never collide with each other --------------------------------------------------------------------

static void testNoSelfCollision() {
    AVER_INFO("=== parts of one ragdoll never collide; another ragdoll's parts do ===");
    // Two parts on top of each other: collision would throw them apart at once.
    Chain c;
    c.addBone(-1, 0.0f, 0.0f, 1000.0f, 50.0f, 10.0f);
    c.addBone(0, 0.0f, 0.0f, 1000.0f, 50.0f, 10.0f);

    beginWorld();
    const int32_t a = create(c);
    aver_phys_ragdoll_set_mode(a, AVER_PHYS_RAGDOLL_LIMP);
    int selfContacts = 0;
    for (int s = 0; s < 60; ++s) {
        aver_phys_step(kDt);
        const int n = aver_phys_contact_count();
        for (int i = 0; i < n; ++i) {
            int32_t ba = 0, bb = 0;
            float p[3], nrm[3];
            aver_phys_contact_get(i, &ba, &bb, p, nrm);
            int32_t boneA = -1, boneB = -1;
            if (aver_phys_ragdoll_of_body(ba, &boneA) == a && aver_phys_ragdoll_of_body(bb, &boneB) == a) ++selfContacts;
        }
    }
    const f32 apart = dist(posOf(partOf(a, 0)), posOf(partOf(a, 1)));
    check(selfContacts == 0, "no contact between a ragdoll's own parts");
    check(apart < 1.0f, "overlapping parts stay put: " + f2s(apart) + " cm apart");
    aver_phys_shutdown();

    // The control: the same overlap between two DIFFERENT ragdolls does produce contacts and separation.
    beginWorld();
    Chain one;
    one.addBone(-1, 0.0f, 0.0f, 1000.0f, 50.0f, 10.0f);
    const int32_t b1 = create(one), b2 = create(one);
    aver_phys_ragdoll_set_mode(b1, AVER_PHYS_RAGDOLL_LIMP);
    aver_phys_ragdoll_set_mode(b2, AVER_PHYS_RAGDOLL_LIMP);
    int crossContacts = 0;
    for (int s = 0; s < 60; ++s) {
        aver_phys_step(kDt);
        const int n = aver_phys_contact_count();
        for (int i = 0; i < n; ++i) {
            int32_t ba = 0, bb = 0;
            float p[3], nrm[3];
            aver_phys_contact_get(i, &ba, &bb, p, nrm);
            const int32_t ra = aver_phys_ragdoll_of_body(ba, nullptr), rb = aver_phys_ragdoll_of_body(bb, nullptr);
            if (ra != 0 && rb != 0 && ra != rb) ++crossContacts;
        }
    }
    const f32 sep = dist(posOf(partOf(b1, 0)), posOf(partOf(b2, 0)));
    check(crossContacts > 0, "two ragdolls' parts do collide (the control)");
    check(sep > 3.0f, "and push each other apart: " + f2s(sep) + " cm");
    aver_phys_shutdown();
}

// ---- Collision layers ---------------------------------------------------------------------------------------

static void testLayer() {
    AVER_INFO("=== the user layer reaches every part ===");
    beginWorld();
    Chain c = chain3(100.0f);
    const int32_t rd = create(c, 3);
    bool onLayer = rd != 0;
    for (int i = 0; i < 3 && onLayer; ++i) onLayer = aver_phys_body_layer(partOf(rd, i)) == 3;
    check(onLayer, "every part is on the requested layer");
    aver_phys_set_layer_collision(3, 0, 0);
    aver_phys_ragdoll_set_mode(rd, AVER_PHYS_RAGDOLL_LIMP);
    run(120);
    check(posOf(partOf(rd, 1)).z < -20.0f, "with layer 3 not colliding with the floor's layer it falls through");
    aver_phys_shutdown();
}

// ---- Bones without a part, and the handle round trip -------------------------------------------------------

static void testRidersAndRoundTrip() {
    AVER_INFO("=== riders follow their part; body_of_bone and of_body round-trip ===");
    beginWorld();
    const int32_t floorBody = aver_phys_body_at(0);
    // 0 part, 1 rider on 0, 2 part under the rider, 3 rider on 2.
    Chain c;
    c.addBone(-1, 0.0f, 0.0f, 600.0f, 40.0f, 8.0f);
    c.addBone(0, 40.0f, 0.0f, 600.0f, 0.0f, 0.0f);
    c.addBone(1, 40.0f, 0.0f, 600.0f, 40.0f, 8.0f);
    c.addBone(2, 80.0f, 0.0f, 600.0f, 0.0f, 0.0f);
    const int32_t rd = create(c);
    check(rd != 0, "a skeleton with riders builds");

    const int32_t b0 = partOf(rd, 0), b2 = partOf(rd, 2);
    check(b0 != 0 && b2 != 0 && b0 != b2, "the two part bones have their own bodies");
    check(partOf(rd, 1) == b0, "a rider's body is its ancestor part's");
    check(partOf(rd, 3) == b2, "and so is the next one's");
    check(partOf(rd, -1) == 0 && partOf(rd, 4) == 0, "a bone out of range has none");

    int32_t bone = -7;
    check(aver_phys_ragdoll_of_body(b0, &bone) == rd && bone == 0, "of_body: part 0 belongs to the ragdoll, bone 0");
    check(aver_phys_ragdoll_of_body(b2, &bone) == rd && bone == 2, "of_body: the second part is bone 2");
    check(aver_phys_ragdoll_of_body(floorBody, &bone) == 0, "of_body: the floor belongs to no ragdoll");
    check(aver_phys_ragdoll_of_body(b0, nullptr) == rd, "of_body: the bone out-pointer may be null");
    check(aver_phys_ragdoll_of_body(0, &bone) == 0, "of_body: handle 0 belongs to none");

    // Kick the far end of part 0 upward so the chain tumbles as it falls, then look at the riders.
    aver_phys_ragdoll_set_mode(rd, AVER_PHYS_RAGDOLL_LIMP);
    const Vec3 c0 = posOf(b0);
    aver_phys_ragdoll_add_impulse(rd, 0, 0.0f, 0.0f, 1000.0f, c0.x + 20.0f, c0.y, c0.z);
    run(40);
    const Pose po = poseOf(rd, 4);
    const Vec3 p0 = bonePos(po, 0), p1 = bonePos(po, 1), p2 = bonePos(po, 2), p3 = bonePos(po, 3);
    const Vec3 expect1 = p0 + boneRot(po, 0).rotate(Vec3(40.0f, 0.0f, 0.0f));
    const Vec3 expect3 = p2 + boneRot(po, 2).rotate(Vec3(40.0f, 0.0f, 0.0f));
    check(dist(p1, expect1) < 0.5f, "rider 1 is rigid with part 0: off by " + f2s(dist(p1, expect1)) + " cm");
    check(dist(p3, expect3) < 0.5f, "rider 3 is rigid with part 2: off by " + f2s(dist(p3, expect3)) + " cm");
    check(dist(p1, p2) < 3.0f, "the joint is at bone 2's origin, where the rider is: " + f2s(dist(p1, p2)) + " cm");
    check(std::fabs(boneRot(po, 0).x) + std::fabs(boneRot(po, 0).y) + std::fabs(boneRot(po, 0).z) > 0.05f,
          "part 0 has turned, so the rider check is not trivially true");

    // An impulse on a rider lands on the part it rides.
    const f32 before = velOf(b2).y;
    check(aver_phys_ragdoll_add_impulse(rd, 3, 0.0f, 400.0f, 0.0f, p3.x, p3.y, p3.z) == 1, "an impulse on a rider is accepted");
    check(velOf(b2).y > before + 1.0f, "and moves the part it rides");
    aver_phys_shutdown();
}

// ---- Impulses and teleports ----------------------------------------------------------------------------------

static void testImpulseAndPose() {
    AVER_INFO("=== impulses in kg*cm/s; set_pose teleports and zeroes velocity ===");
    beginWorld();
    Chain c = chain3(600.0f);
    const int32_t rd = create(c);
    check(aver_phys_ragdoll_add_impulse(rd, 2, 0.0f, 5000.0f, 0.0f, 0.0f, 0.0f, 600.0f) == 0,
          "an impulse on a kinematic ragdoll is refused");

    aver_phys_ragdoll_set_mode(rd, AVER_PHYS_RAGDOLL_LIMP);
    const int32_t tip = partOf(rd, 2);
    const f32 m = massOf(tip);
    const Vec3 at = posOf(tip);
    check(aver_phys_ragdoll_add_impulse(rd, 2, 0.0f, 5000.0f, 0.0f, at.x, at.y, at.z) == 1, "an impulse on a limp one is accepted");
    const Vec3 v = velOf(tip);
    check(near(v.y, 5000.0f / m, 0.01f * 5000.0f / m) && near(v.x, 0.0f, 1.0f) && near(v.z, 0.0f, 1.0f),
          "5000 kg*cm/s on " + f2s(m) + " kg is " + f2s(5000.0f / m) + " cm/s toward +Y: got " + f2s(v.y));
    check(aver_phys_ragdoll_add_impulse(rd, 3, 0, 0, 0, 0, 0, 0) == 0 && aver_phys_ragdoll_add_impulse(rd, -1, 0, 0, 0, 0, 0, 0) == 0,
          "a bone out of range is refused");

    run(30);
    check(velOf(tip).size() > 100.0f, "it is moving after half a second");
    check(aver_phys_ragdoll_set_pose(rd, c.pos.data(), c.rot.data()) == 1, "set_pose accepts the creation pose");
    const Pose po = poseOf(rd, 3);
    bool home = true;
    for (int i = 0; i < 3; ++i) home = home && dist(bonePos(po, i), Vec3(c.pos[3 * i], c.pos[3 * i + 1], c.pos[3 * i + 2])) < 0.5f;
    check(home, "every bone is back at the pose");
    check(velOf(tip).size() < 0.5f, "with no velocity left: " + f2s(velOf(tip).size()) + " cm/s");
    aver_phys_shutdown();
}

// ---- Lifetime ---------------------------------------------------------------------------------------------

static void testLifetime() {
    AVER_INFO("=== destroy, remove_body, shutdown ===");
    beginWorld();
    const int32_t base = aver_phys_body_count();
    Chain c = chain3(300.0f);

    const int32_t r1 = create(c), r2 = create(c);
    check(aver_phys_ragdoll_count() == 2 && aver_phys_body_count() == base + 6, "two ragdolls, six parts");
    const int32_t old0 = partOf(r1, 0), old2 = partOf(r1, 2);
    check(aver_phys_ragdoll_destroy(r1) == 1, "destroy succeeds");
    check(aver_phys_ragdoll_count() == 1 && aver_phys_body_count() == base + 3, "and takes exactly its own parts with it");
    float p[3];
    check(aver_phys_body_position(old0, p) == 0 && aver_phys_ragdoll_of_body(old2, nullptr) == 0, "the old part handles are dead");
    check(aver_phys_ragdoll_destroy(r1) == 0 && aver_phys_ragdoll_mode(r1) == -1 &&
              aver_phys_ragdoll_set_mode(r1, AVER_PHYS_RAGDOLL_LIMP) == 0 && aver_phys_ragdoll_bone_count(r1) == 0,
          "the dead ragdoll handle is refused everywhere");
    run(20);
    check(aver_phys_ragdoll_set_mode(r2, AVER_PHYS_RAGDOLL_LIMP) == 1, "the other ragdoll still works");
    run(20);
    check(posOf(partOf(r2, 0)).z < 300.0f, "and still simulates");

    // Removing one part removes the ragdoll: no part may outlive its joints.
    const int32_t a = partOf(r2, 0), b = partOf(r2, 1), d = partOf(r2, 2);
    aver_phys_remove_body(b);
    check(aver_phys_ragdoll_count() == 0, "remove_body on a part destroys its ragdoll");
    check(aver_phys_body_count() == base, "and every part with it");
    check(aver_phys_body_position(a, p) == 0 && aver_phys_body_position(d, p) == 0, "the other parts' handles are dead");
    run(20);

    // A world that ends with a ragdoll in it.
    const int32_t live = create(c);
    aver_phys_ragdoll_set_mode(live, AVER_PHYS_RAGDOLL_LIMP);
    run(10);
    aver_phys_shutdown();
    check(aver_phys_ready() == 0 && aver_phys_ragdoll_count() == 0 && aver_phys_ragdoll_mode(live) == -1,
          "shutdown destroys the ragdolls with the world");

    beginWorld();
    const int32_t again = create(c);
    aver_phys_ragdoll_set_mode(again, AVER_PHYS_RAGDOLL_LIMP);
    run(30);
    check(again != 0 && posOf(partOf(again, 0)).z < 300.0f, "a fresh world builds and steps a ragdoll");
    aver_phys_shutdown();
}

// ---- Refusals ---------------------------------------------------------------------------------------------

static void testRefusals() {
    AVER_INFO("=== bad input is refused, with a reason ===");
    beginWorld();
    const int32_t base = aver_phys_body_count();
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();

    {
        Chain c = chain3(300.0f);
        check(aver_phys_ragdoll_create(0, c.parents.data(), c.pos.data(), c.rot.data(), c.caps.data(), c.lim.data(), 30.0f, 0) == 0 &&
                  lastError() == errCode(AbiError::InvalidArgument), "zero bones");
        check(aver_phys_ragdoll_create(c.n, nullptr, c.pos.data(), c.rot.data(), c.caps.data(), c.lim.data(), 30.0f, 0) == 0 &&
                  lastError() == errCode(AbiError::NullPointer), "a null array");
        c.massKg = 0.0f;
        check(create(c) == 0 && lastError() == errCode(AbiError::InvalidArgument), "zero mass");
        c.massKg = nan;
        check(create(c) == 0, "NaN mass");
    }
    {
        Chain c = chain3(300.0f);
        check(create(c, 16) == 0 && lastError() == errCode(AbiError::InvalidArgument), "a user layer past the last");
    }
    {
        Chain c = chain3(300.0f);
        c.parents[1] = 1;
        check(create(c) == 0 && lastError() == errCode(AbiError::InvalidArgument), "a bone that is its own parent");
        c.parents[1] = 2;
        check(create(c) == 0, "a parent that comes after its child");
        c.parents[1] = -5;
        check(create(c) == 0, "a parent below -1");
    }
    {
        Chain c = chain3(300.0f);
        c.rot[4] = c.rot[5] = c.rot[6] = c.rot[7] = 0.0f;
        check(create(c) == 0 && lastError() == errCode(AbiError::InvalidArgument), "a zero-length rotation");
        c = chain3(300.0f);
        c.pos[4] = nan;
        check(create(c) == 0, "a NaN position");
        c = chain3(300.0f);
        c.caps[6] = 0.0f; c.caps[13] = 0.0f; c.caps[20] = 0.0f;
        check(create(c) == 0 && lastError() == errCode(AbiError::InvalidArgument), "no bone with a part");
        c = chain3(300.0f);
        c.caps[6] = 0.001f;
        check(create(c) == 0, "a radius too small to simulate");
    }
    check(aver_phys_ragdoll_count() == 0 && aver_phys_body_count() == base, "refused builds leave nothing behind");

    Chain c = chain3(300.0f);
    const int32_t rd = create(c);
    check(aver_phys_ragdoll_set_mode(rd, 7) == 0 && aver_phys_ragdoll_mode(rd) == AVER_PHYS_RAGDOLL_KINEMATIC, "an unknown mode");
    check(aver_phys_ragdoll_set_strength(rd, nan) == 0, "a NaN strength");
    check(aver_phys_ragdoll_set_target(rd, nullptr, c.rot.data()) == 0 && aver_phys_ragdoll_set_pose(rd, c.pos.data(), nullptr) == 0,
          "a null pose");
    float out[12];
    check(aver_phys_ragdoll_get_pose(rd, nullptr, out) == 0, "a null output");
    c.pos[0] = nan;
    check(aver_phys_ragdoll_set_target(rd, c.pos.data(), c.rot.data()) == 0, "a NaN target");
    check(aver_phys_ragdoll_add_impulse(rd, 0, nan, 0, 0, 0, 0, 0) == 0, "a NaN impulse");
    aver_phys_shutdown();

    Chain after = chain3(300.0f);
    check(create(after) == 0 && lastError() == errCode(AbiError::NotInitialised), "no world, no ragdoll");
    check(aver_phys_ragdoll_count() == 0, "and none counted");
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO("ragdolls: build, limp, kinematic, powered, collision, lifetime");

    testBuildAndMass();
    testLimp();
    testKinematic();
    testPowered();
    testNoSelfCollision();
    testLayer();
    testRidersAndRoundTrip();
    testImpulseAndPose();
    testLifetime();
    testRefusals();

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
