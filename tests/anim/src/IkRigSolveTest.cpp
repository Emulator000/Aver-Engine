// IkRigSolveTest -- the pure IK Rig stack: bind, priorities and claims, body shift, and foot placement
// over a ground-query callback. No scene, no physics: the "ground" is a plane the test moves around.
//
// Foot placement is checked against numbers worked out from the algorithm's rules (asymmetric target,
// critically damped follow, pelvis = lowest foot) rather than "looks planted".
#include "IkRigFixture.hpp"

#include "aver/anim/IkRigSolver.hpp"
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <string>

using namespace aver;
using namespace ikfix;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static std::string f2s(f32 v) { char b[64]; std::snprintf(b, sizeof b, "%.3f", v); return b; }
static f32 gap(const Vec3& a, const Vec3& b) { return (a - b).size(); }

// Two planes, left (model y < 0) and right, with their own height, normal and hit/miss.
struct Ground {
    f32 leftZ = 0.0f, rightZ = 0.0f;
    Vec3 leftN{0, 0, 1}, rightN{0, 0, 1};
    bool leftHit = true, rightHit = true;
    int casts = 0;
};

static bool groundFn(const Vec3& o, f32 dist, u32, Vec3& p, Vec3& n, void* user) {
    auto* g = static_cast<Ground*>(user);
    ++g->casts;
    const bool left = o.y < 0.0f;
    if (!(left ? g->leftHit : g->rightHit)) return false;
    const f32 z = left ? g->leftZ : g->rightZ;
    if (o.z - z < 0.0f || o.z - z > dist) return false;
    p = Vec3{o.x, o.y, z};
    n = left ? g->leftN : g->rightN;
    return true;
}

struct CurveBox { f32 left = -1.0f; };   // < 0 = not present
static bool curveFn(u64, f32& out, void* user) {
    auto* c = static_cast<CurveBox*>(user);
    if (c->left < 0.0f) return false;
    out = c->left;
    return true;
}

static anim::IkRigAsset legsAsset() {
    anim::IkRigAsset a;
    a.skeleton = "h.ocskel";
    a.body.bone = "pelvis";
    a.body.maxOffsetCm = 100.0f;
    for (const char* side : {"_l", "_r"}) {
        anim::IkRigChainDef c;
        c.name = std::string("Leg") + side;
        c.start = std::string("thigh") + side;
        c.end = std::string("foot") + side;
        c.solver = anim::IkSolverKind::TwoBone;
        c.priority = 2;
        a.chains.push_back(c);
        anim::IkRigFootDef f;
        f.chain = c.name;
        f.toe = std::string("ball") + side;
        if (std::string(side) == "_l") f.plantCurve = "Plant_L";
        a.feet.push_back(f);
    }
    return a;
}

struct Rig {
    Humanoid h = makeHumanoid();
    anim::IkRigAsset asset;
    anim::IkRigBound bound;
    anim::IkRigState state;
    anim::IkScratch scratch;
    anim::IkRigReport rep;
    anim::Pose rest;
    Ground ground;
    CurveBox curve;
    Rig(const anim::IkRigAsset& a) : asset(a) {
        anim::bindIkRig(asset, h.skel, bound);
        anim::restPose(h.skel, rest);
    }
    anim::IkRigInputs inputs(f32 dt = 1.0f / 60.0f) {
        anim::IkRigInputs in;
        in.dt = dt;
        in.ground = &groundFn;
        in.groundUser = &ground;
        in.curve = &curveFn;
        in.curveUser = &curve;
        return in;
    }
    void tick(anim::Pose& p, const anim::IkRigInputs& in) { anim::solveIkRig(h.skel, bound, state, in, p, rep, scratch); }
};

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO("IK rig stack");

    AVER_INFO("=== bind ===");
    {
        anim::IkRigAsset a = legsAsset();
        a.chains[0].name = "LeftLeg";
        a.chains[1].name = "RightLeg";
        a.feet[0].chain = "LeftLeg";
        a.feet[1].chain = "RightLeg";
        a.chains[1].priority = 0;
        Rig r(a);
        check(r.bound.ok && r.bound.chains.size() == 2 && r.bound.feet.size() == 2, "both legs and both feet bound");
        check(r.bound.chains[0].name == "RightLeg", "chains are in ascending priority order");
        check(r.bound.chains[0].chain.bones.size() == 3 && r.bound.chains[0].params.kind == anim::IkSolverKind::TwoBone,
              "the leg is a 3-bone twobone chain");
        const Vec3 footRest = modelPos(r.h.skel, r.rest, r.h.footL);
        check(r.bound.feet[r.bound.findFoot(fnv1a64(std::string("LeftLeg")))].groundZ == 0.0f, "ground is the model origin plane when the feet stand above it");
        const f32 sole = r.bound.feet[r.bound.findFoot(fnv1a64(std::string("LeftLeg")))].soleOffset;
        check(std::fabs(sole - footRest.z) < 1e-3f, "the sole offset is the rest ankle height, " + f2s(sole));

        anim::IkRigAsset bad = a;
        bad.chains[0].start = "no_such_bone";
        bad.chains[1].solver = anim::IkSolverKind::TwoBone;
        bad.chains[1].start = "pelvis";              // 4 bones to the foot is not a leg
        Rig rb(bad);
        check(rb.bound.chains.size() == 1 && !rb.bound.issues.empty(), "an unresolved chain is disabled and reported, not an error");
        check(rb.bound.chains[0].params.kind == anim::IkSolverKind::Fabrik, "a twobone chain that is not 3 bones falls back to fabrik");
        check(rb.bound.feet.size() == 1, "and the foot of the disabled chain is dropped");
    }

    AVER_INFO("=== flat ground changes nothing, bit for bit ===");
    {
        Rig r(legsAsset());
        anim::Pose p = r.rest;
        const anim::IkRigInputs in = r.inputs();
        for (int i = 0; i < 5; ++i) r.tick(p, in);
        check(samePose(p, r.rest), "five ticks over a plane at the assumed ground leave the pose identical");
        check(!r.rep.changed && r.rep.probes == 2, "nothing was reported changed, two probes were cast");
        check(r.ground.casts == 10, "one ray per foot per tick");
    }

    AVER_INFO("=== a step under one foot lifts it, on the first tick, and nothing else ===");
    {
        Rig r(legsAsset());
        anim::Pose p = r.rest;
        r.ground.leftZ = 8.0f;
        r.tick(p, r.inputs());
        const Vec3 l0 = modelPos(r.h.skel, r.rest, r.h.footL), l1 = modelPos(r.h.skel, p, r.h.footL);
        const Vec3 r0 = modelPos(r.h.skel, r.rest, r.h.footR), r1 = modelPos(r.h.skel, p, r.h.footR);
        check(std::fabs((l1.z - l0.z) - 8.0f) < 0.6f && gap(Vec3{l1.x, l1.y, 0}, Vec3{l0.x, l0.y, 0}) < 0.6f,
              "the left ankle rose " + f2s(l1.z - l0.z) + " cm with no sideways drift");
        check(gap(r0, r1) < 1e-3f, "the right ankle did not move");
        check(std::fabs(modelPos(r.h.skel, p, r.h.pelvis).z - modelPos(r.h.skel, r.rest, r.h.pelvis).z) < 1e-3f, "a step up does not drop the pelvis");
        check(std::fabs(r.state.feet[0].offset - 8.0f) < 1e-4f, "the first tick snaps: offset is the target, not eased from zero");
    }

    AVER_INFO("=== ground below pulls a planted foot; the pelvis drops by the lowest foot ===");
    {
        Rig r(legsAsset());
        anim::Pose p = r.rest;
        r.ground.leftZ = -5.0f;
        r.tick(p, r.inputs());
        const f32 pelvisDz = modelPos(r.h.skel, p, r.h.pelvis).z - modelPos(r.h.skel, r.rest, r.h.pelvis).z;
        check(std::fabs(pelvisDz + 5.0f) < 0.1f, "the pelvis dropped 5 cm, got " + f2s(pelvisDz));
        const f32 dl = modelPos(r.h.skel, p, r.h.footL).z - modelPos(r.h.skel, r.rest, r.h.footL).z;
        check(std::fabs(dl + 5.0f) < 0.7f, "the planted left ankle followed the ground down, " + f2s(dl));
        check(std::fabs(modelPos(r.h.skel, p, r.h.footR).z - modelPos(r.h.skel, r.rest, r.h.footR).z) < 0.7f,
              "the right foot, on the original ground, stays where it was (the leg reaches back down)");
    }

    AVER_INFO("=== a swinging foot is only lifted, never pulled: plant curve and automatic plant ===");
    {
        Rig r(legsAsset());
        anim::Pose p = r.rest;
        r.ground.leftZ = -5.0f;
        r.curve.left = 0.0f;                 // the clip says the left foot is in the air
        r.tick(p, r.inputs());
        check(samePose(p, r.rest) && std::fabs(r.state.feet[0].offset) < 1e-6f, "the curve says swing: ground below does nothing");

        Rig r2(legsAsset());
        anim::Pose q = r2.rest;
        r2.ground.leftZ = 8.0f;
        r2.curve.left = 0.0f;
        r2.tick(q, r2.inputs());
        check(std::fabs(r2.state.feet[0].offset - 8.0f) < 1e-4f, "but ground above a swinging foot still lifts it");

        // Automatic plant (no curve named on the right foot): lift the right foot with the animation.
        Rig r3(legsAsset());
        anim::Pose w = r3.rest;
        w.local[r3.h.thighR].rotation = (Quat::fromAxisAngle(Vec3{0, 1, 0}, -0.9f) * w.local[r3.h.thighR].rotation).normalized();
        r3.ground.rightZ = -5.0f;
        r3.tick(w, r3.inputs());
        check(r3.state.feet[1].plant < 0.05f, "a foot lifted by the animation reads as not planted, plant " + f2s(r3.state.feet[1].plant));
        check(std::fabs(r3.state.feet[1].offset) < 1e-4f, "so the ground below it does not pull it");
        check(r.state.feet[0].plant == 0.0f, "the named curve value is used as the plant weight");
    }

    AVER_INFO("=== caps: pelvis drop and foot offset ===");
    {
        Rig r(legsAsset());
        anim::Pose p = r.rest;
        r.ground.leftZ = -50.0f;
        r.tick(p, r.inputs());
        check(std::fabs(r.state.feet[0].offset + 45.0f) < 1e-4f, "the foot offset is capped at maxFootOffsetCm (45)");
        const f32 pelvisDz = modelPos(r.h.skel, p, r.h.pelvis).z - modelPos(r.h.skel, r.rest, r.h.pelvis).z;
        check(std::fabs(pelvisDz + 40.0f) < 0.1f, "and the pelvis drop at maxPelvisDropCm (40), got " + f2s(pelvisDz));
    }

    AVER_INFO("=== normal alignment, capped ===");
    {
        Rig r(legsAsset());
        anim::Pose p = r.rest;
        const f32 s45 = std::sin(45.0f * kDegToRad);
        r.ground.leftN = Vec3{s45, 0, s45};
        r.tick(p, r.inputs());
        const f32 a = angleDeg(modelRot(r.h.skel, r.rest, r.h.footL), modelRot(r.h.skel, p, r.h.footL));
        check(std::fabs(a - 30.0f) < 1.0f, "a 45 degree slope tilts the planted foot by the 30 degree cap, got " + f2s(a));
        check(angleDeg(modelRot(r.h.skel, r.rest, r.h.footR), modelRot(r.h.skel, p, r.h.footR)) < 1e-3f, "the other foot is untouched");

        anim::IkRigAsset off = legsAsset();
        off.feet[0].alignToNormal = false;
        Rig r2(off);
        anim::Pose q = r2.rest;
        r2.ground.leftN = Vec3{s45, 0, s45};
        r2.tick(q, r2.inputs());
        check(samePose(q, r2.rest), "alignToNormal=false leaves the pose alone");
    }

    AVER_INFO("=== smoothing: step response, no hit, first tick ===");
    {
        Rig r(legsAsset());
        anim::Pose p = r.rest;
        r.tick(p, r.inputs());                     // snap to flat
        r.ground.leftZ = 8.0f;
        p = r.rest;
        r.tick(p, r.inputs());
        const f32 k = 1.0f - std::exp(-14.0f / 60.0f);
        check(std::fabs(r.state.feet[0].offset - 8.0f * k) < 0.01f, "one tick moves 1 - exp(-smoothing dt) of the way, " + f2s(r.state.feet[0].offset) + " vs " + f2s(8.0f * k));
        for (int i = 0; i < 180; ++i) { p = r.rest; r.tick(p, r.inputs()); }
        check(std::fabs(r.state.feet[0].offset - 8.0f) < 0.05f, "and converges on the target");
        r.ground.leftHit = false;                   // the ground is gone (a ledge, an edge)
        p = r.rest;
        r.tick(p, r.inputs());
        check(std::fabs(r.state.feet[0].offset - 8.0f * (1.0f - k)) < 0.05f, "no hit eases toward zero");
        for (int i = 0; i < 180; ++i) { p = r.rest; r.tick(p, r.inputs()); }
        check(std::fabs(r.state.feet[0].offset) < 0.05f, "and arrives");
        anim::Pose none = r.rest;
        anim::IkRigInputs in = r.inputs();
        in.ground = nullptr;                        // no probe installed at all
        r.state = anim::IkRigState{};
        r.tick(none, in);
        check(samePose(none, r.rest), "with no ground function the character animates unchanged");
        r.ground.leftHit = true;
        r.ground.leftZ = 8.0f;
        r.state = anim::IkRigState{};
        anim::Pose held = r.rest;
        r.tick(held, r.inputs());                   // offset 8
        anim::IkRigInputs budget = r.inputs();
        budget.probe = false;
        r.ground.leftZ = 0.0f;
        const int casts = r.ground.casts;
        anim::Pose again = r.rest;
        r.tick(again, budget);
        check(r.ground.casts == casts && std::fabs(r.state.feet[0].offset - 8.0f) < 1e-4f, "past the probe budget the feet hold their last offsets and cast nothing");
    }

    AVER_INFO("=== weight, flags ===");
    {
        Rig r(legsAsset());
        r.ground.leftZ = 8.0f;
        anim::Pose p = r.rest;
        anim::IkRigInputs in = r.inputs();
        in.weight = 0.0f;
        r.tick(p, in);
        check(samePose(p, r.rest) && r.ground.casts == 0, "weight 0: nothing changes and no ray is cast");
        in.weight = 1.0f;
        in.flags = anim::kIkRigFeetOff;
        r.tick(p, in);
        check(samePose(p, r.rest) && r.ground.casts == 0, "feet-off flag: same");
        in.flags = 0;
        in.weight = 0.5f;
        r.tick(p, in);
        const f32 half = modelPos(r.h.skel, p, r.h.footL).z - modelPos(r.h.skel, r.rest, r.h.footL).z;
        check(half > 2.5f && half < 5.5f, "half weight is roughly half the lift, " + f2s(half));
        Rig rf(legsAsset());
        anim::Pose q = rf.rest;
        rf.ground.leftZ = 8.0f;
        anim::IkRigInputs fa = rf.inputs();
        const f32 alphas[2] = {0.0f, 1.0f};
        fa.footAlpha = alphas;
        rf.tick(q, fa);
        check(samePose(q, rf.rest), "a foot alpha of 0 disengages that foot");
    }

    // ---- priorities and claims, goals ----
    anim::IkRigAsset claim;
    claim.skeleton = "h.ocskel";
    {
        anim::IkRigChainDef spine; spine.name = "Spine"; spine.start = "spine_01"; spine.end = "spine_03";
        spine.solver = anim::IkSolverKind::Ccd; spine.priority = 0; spine.goal = "Chest";
        spine.iterations = 60; spine.toleranceCm = 0.05f;
        anim::IkRigChainDef reach; reach.name = "Reach"; reach.start = "spine_02"; reach.end = "neck";
        reach.solver = anim::IkSolverKind::Fabrik; reach.priority = 1; reach.goal = "Reach"; reach.iterations = 40; reach.toleranceCm = 0.05f;
        anim::IkRigChainDef tip; tip.name = "Tip"; tip.start = "spine_03"; tip.end = "neck";
        tip.solver = anim::IkSolverKind::Fabrik; tip.priority = 2; tip.goal = "Tip";
        // Listed in the "wrong" order so priority, not file order, decides.
        claim.chains = {tip, reach, spine};
        claim.body.bone = "pelvis";
        claim.body.goal = "Hips";
        claim.body.maxOffsetCm = 30.0f;
    }

    AVER_INFO("=== priorities: the claimed bone is skipped, the rest still reaches ===");
    {
        Rig r(claim);
        check(r.bound.chains.size() == 3 && r.bound.chains[0].name == "Spine" && r.bound.chains[1].name == "Reach" &&
              r.bound.chains[2].name == "Tip", "solve order is Spine, Reach, Tip");
        const i32 gChest = r.bound.findGoal(fnv1a64(std::string("Chest")));
        const i32 gReach = r.bound.findGoal(fnv1a64(std::string("Reach")));
        const i32 gTip = r.bound.findGoal(fnv1a64(std::string("Tip")));
        check(gChest >= 0 && gReach >= 0 && gTip >= 0, "goals are found by name hash");

        const Vec3 s1 = modelPos(r.h.skel, r.rest, r.h.spine1);
        std::vector<anim::IkGoalValue> goals(r.bound.goalHashes.size());
        const Vec3 chestGoal = s1 + Vec3{18, 6, 17};
        goals[static_cast<usize>(gChest)] = {true, chestGoal, false, Quat::identity(), 1.0f};

        // First, the spine alone.
        anim::Pose alone = r.rest;
        anim::IkRigInputs in = r.inputs();
        in.goals = goals.data();
        in.goalCount = goals.size();
        r.tick(alone, in);
        const Vec3 tipAlone = modelPos(r.h.skel, alone, r.h.spine3);
        check(gap(tipAlone, chestGoal) < 0.3f, "the spine's top bone lands on its goal, off by " + f2s(gap(tipAlone, chestGoal)));
        check(r.rep.goalReached[static_cast<usize>(gChest)] == 1, "the report says the Chest goal was reached");

        // Now Reach and Tip too: they share spine_02 / spine_03 with the claimed spine.
        // One 10 cm segment can only sit ON the sphere of its length around its root.
        goals[static_cast<usize>(gReach)] = {true, tipAlone + Vec3{4, 3, 5}.getSafeNormal() * 10.0f, false, Quat::identity(), 1.0f};
        goals[static_cast<usize>(gTip)] = {true, tipAlone + Vec3{0, 0, 9}, false, Quat::identity(), 1.0f};
        anim::Pose both = r.rest;
        anim::IkRigState fresh;
        r.state = fresh;
        r.tick(both, in);
        check(angleDeg(alone.local[r.h.spine1].rotation, both.local[r.h.spine1].rotation) < 1e-3f &&
              angleDeg(alone.local[r.h.spine2].rotation, both.local[r.h.spine2].rotation) < 1e-3f,
              "the claimed spine bones are exactly as the higher priority chain left them");
        check(r.rep.chains[1].solved && !r.rep.chains[1].claimedOut, "Reach started from the first unclaimed bone and solved");
        const Vec3 neck = modelPos(r.h.skel, both, r.h.neck);
        check(gap(neck, goals[static_cast<usize>(gReach)].pos) < 0.5f, "and the neck reaches its goal, off by " + f2s(gap(neck, goals[static_cast<usize>(gReach)].pos)));
        check(r.rep.chains[2].claimedOut && !r.rep.chains[2].solved, "Tip had nothing left and is reported claimed out");
    }

    AVER_INFO("=== goal alpha blends; an untouched or released goal does nothing ===");
    {
        Rig r(claim);
        const i32 gChest = r.bound.findGoal(fnv1a64(std::string("Chest")));
        std::vector<anim::IkGoalValue> goals(r.bound.goalHashes.size());
        const Vec3 s1 = modelPos(r.h.skel, r.rest, r.h.spine1);
        anim::IkRigInputs in = r.inputs();
        in.goals = goals.data();
        in.goalCount = goals.size();
        anim::Pose p = r.rest;
        r.tick(p, in);
        check(samePose(p, r.rest), "goals nobody set leave the pose alone");
        goals[static_cast<usize>(gChest)] = {true, s1 + Vec3{18, 6, 17}, false, Quat::identity(), 0.0f};
        r.tick(p, in);
        check(samePose(p, r.rest), "alpha 0 is released");
        goals[static_cast<usize>(gChest)].alpha = 0.5f;
        r.tick(p, in);
        const Vec3 half = modelPos(r.h.skel, p, r.h.spine3);
        goals[static_cast<usize>(gChest)].alpha = 1.0f;
        anim::Pose full = r.rest;
        r.tick(full, in);
        const Vec3 fullPos = modelPos(r.h.skel, full, r.h.spine3), restPos = modelPos(r.h.skel, r.rest, r.h.spine3);
        const f32 frac = gap(half, restPos) / gap(fullPos, restPos);
        check(frac > 0.3f && frac < 0.7f, "alpha 0.5 lands about halfway, " + f2s(frac));
        in.flags = anim::kIkRigGoalsOff;
        anim::Pose off = r.rest;
        r.tick(off, in);
        check(samePose(off, r.rest), "the goals-off flag disables goal chains");
    }

    AVER_INFO("=== a goal-driven leg and the body goal ===");
    {
        anim::IkRigAsset a = legsAsset();
        a.chains[0].goal = "LeftFoot";
        a.body.goal = "Hips";
        a.body.maxOffsetCm = 30.0f;
        Rig r(a);
        anim::IkRigInputs in = r.inputs();
        std::vector<anim::IkGoalValue> goals(r.bound.goalHashes.size());
        in.goals = goals.data();
        in.goalCount = goals.size();
        const Vec3 f0 = modelPos(r.h.skel, r.rest, r.h.footL);
        const i32 gl = r.bound.findGoal(fnv1a64(std::string("LeftFoot")));
        const Vec3 want = f0 + Vec3{12, -4, 14};
        goals[static_cast<usize>(gl)] = {true, want, false, Quat::identity(), 1.0f};
        anim::Pose p = r.rest;
        r.tick(p, in);
        check(gap(modelPos(r.h.skel, p, r.h.footL), want) < 0.3f, "the left ankle reaches the goal");
        check(r.rep.goalReached[static_cast<usize>(gl)] == 1 && r.rep.goalError[static_cast<usize>(gl)] < 0.3f, "and the report agrees");

        // Body goal far away: the pelvis moves toward it but never past maxOffsetCm.
        const i32 gh = r.bound.findGoal(fnv1a64(std::string("Hips")));
        const Vec3 pel0 = modelPos(r.h.skel, r.rest, r.h.pelvis);
        goals[static_cast<usize>(gl)].alpha = 0.0f;
        goals[static_cast<usize>(gh)] = {true, pel0 + Vec3{200, 0, 0}, false, Quat::identity(), 1.0f};
        anim::Pose q = r.rest;
        r.tick(q, in);
        const f32 moved = gap(modelPos(r.h.skel, q, r.h.pelvis), pel0);
        check(std::fabs(moved - 30.0f) < 0.5f, "the body offset is clamped to maxOffsetCm, moved " + f2s(moved));
    }

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
