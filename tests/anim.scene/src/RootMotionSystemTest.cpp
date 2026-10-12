// Root motion on entities: CRootMotion registers, each mode does what it says, and every source (a
// CAnimator clip, a state machine, a montage) reaches the same accumulator.
//
// The failure this exists to catch is the recurring one: every layer below passes its own test and the
// pieces are wired together wrongly (a delta computed and never applied, a lock that runs for a mode
// that did not ask for it, a montage whose root motion is counted on top of the base instead of
// replacing it). No GPU, no scripting host.
#include "aver/anim/AnimGraphAsset.hpp"
#include "aver/anim/AnimGraphSystem.hpp"
#include "aver/anim/AnimMontages.hpp"
#include "aver/anim/AnimSystem.hpp"
#include "aver/anim/RootMotion.hpp"
#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcAnim.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/World.hpp"

#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static void checkNear(f32 got, f32 want, f32 eps, const std::string& what) {
    if (std::fabs(got - want) <= eps) { AVER_INFO("   ok    {} ({:.4f})", what, got); return; }
    ++g_failures;
    AVER_ERROR("   FAIL  {} (got {:.5f}, want {:.5f})", what, got, want);
}

static std::map<u64, std::string> g_paths;
static std::string g_dir;
static const u64 kSkelId = 0x5EE1AA79ull;

static u64 reg(const std::string& ref, const std::string& file) {
    const u64 id = fnv1a64(std::string_view(ref));
    g_paths[id] = g_dir + "/" + file;
    return id;
}

static std::string resolvePath(u64 id, void*) {
    if (id == kSkelId) return g_dir + "/rig.ocskel";
    const auto it = g_paths.find(id);
    return it == g_paths.end() ? std::string{} : it->second;
}

static fmt::OcSkeleton rig() {
    fmt::OcSkeleton s;
    fmt::OcBone root, hand;
    root.name = "root";
    hand.name = "hand";
    hand.parent = 0;
    s.bones = {root, hand};
    s.rootBone = 0;
    return s;
}

// A root track: x from 0 to `to` over `dur`, and optionally a heading from 0 to `yawTo`.
static fmt::OcAnimation rootClip(f32 dur, f32 to, u8 flags, f32 yawTo = 0.0f) {
    fmt::OcAnimation c;
    c.duration = dur;
    c.flags = flags;
    fmt::OcTrack t;
    t.boneIndex = 0;
    const bool rot = yawTo != 0.0f;
    t.channels = fmt::kOcChannelTranslation | (rot ? fmt::kOcChannelRotation : 0);
    t.times = {0.0f, dur};
    auto key = [&](f32 x, f32 yaw) {
        t.values.insert(t.values.end(), {x, 0.0f, 0.0f});
        if (rot) {
            const Quat q = Quat::fromAxisAngle(Vec3{0, 0, 1}, yaw);
            t.values.insert(t.values.end(), {q.x, q.y, q.z, q.w});
        }
    };
    key(0.0f, 0.0f);
    key(to, yawTo);
    c.tracks.push_back(t);
    return c;
}

static constexpr u8 kLoopRm = fmt::kOcAnimLoop | fmt::kOcAnimRootMotion;

static anim::MontageAsset montage(const char* name, const char* clip, f32 blendIn = 0.0f) {
    anim::MontageAsset a;
    a.name = name;
    a.blendIn = blendIn;
    a.blendOut = 0.2f;
    a.inCurve = anim::BlendCurve::Linear;
    a.outCurve = anim::BlendCurve::Linear;
    a.segments = {{clip, 0.0f, -1.0f, 1.0f}};
    a.sections = {{"Default", 0.0f, ""}};
    return a;
}

static scene::World* g_w = nullptr;
static u32 g_rootType = 0;

static scene::Entity makeEntity(const char* name, u64 clip, i32 mode, u32 axes = 0, f32 scale = 1.0f) {
    scene::World& w = *g_w;
    const scene::Entity e = w.create(name);
    auto* sm = static_cast<scene::CSkeletalMesh*>(w.addComponent(e, scene::kComponentSkeletalMesh));
    if (sm) sm->skeleton = kSkelId;
    auto* a = static_cast<scene::CAnimator*>(w.addComponent(e, scene::kComponentAnimator));
    if (a) a->clip = clip;
    if (mode >= 0) anim::AnimSystem::attach(w, e, mode, axes, scale);
    return e;
}

static anim::CRootMotion* rm(scene::Entity e) { return g_w->component<anim::CRootMotion>(e, g_rootType); }

static void retire(anim::AnimSystem& sys, scene::Entity e) {
    sys.montages().stopAll();
    g_w->destroy(e);
    g_w->flush();
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("RootMotionSystemTest");

    std::error_code ec;
    g_dir = (std::filesystem::temp_directory_path(ec) / "aver-rootmotion-test").string();
    std::filesystem::remove_all(g_dir, ec);
    std::filesystem::create_directories(g_dir, ec);

    std::string why;
    check(fmt::saveOcSkel(g_dir + "/rig.ocskel", rig(), &why), "the skeleton writes " + why);
    auto saveClip = [&](const char* ref, const fmt::OcAnimation& c) {
        check(fmt::saveOcAnim(g_dir + "/" + ref, c, &why), std::string(ref) + " writes " + why);
        return reg(ref, ref);
    };
    const u64 walkId = saveClip("walk.ocanim", rootClip(1.0f, 100.0f, kLoopRm));
    const u64 walk2Id = saveClip("walk2.ocanim", rootClip(1.0f, 200.0f, kLoopRm));
    saveClip("run.ocanim", rootClip(1.0f, 300.0f, kLoopRm));
    const u64 plainId = saveClip("plain.ocanim", rootClip(1.0f, 100.0f, fmt::kOcAnimLoop));
    const u64 stillId = saveClip("still.ocanim", rootClip(1.0f, 0.0f, fmt::kOcAnimLoop));
    const u64 turnId = saveClip("turn.ocanim", rootClip(1.0f, 0.0f, kLoopRm, kPi * 0.5f));

    auto saveM = [&](const char* ref, const anim::MontageAsset& a) {
        check(anim::saveMontage(g_dir + "/" + ref, a, &why), std::string(ref) + " writes " + why);
        return reg(ref, ref);
    };
    const u64 mWalk = saveM("mwalk.ocmontage", montage("MW", "walk.ocanim"));
    const u64 mFast = saveM("mfast.ocmontage", montage("MF", "run.ocanim", 0.5f));
    const u64 mPlain = saveM("mplain.ocmontage", montage("MP", "plain.ocanim"));

    scene::World& w = scene::World::instance();
    g_w = &w;
    anim::AnimSystem& sys = anim::animSystem();
    sys.clear();
    sys.setResolver(&resolvePath, nullptr);

    AVER_INFO("=== the component registers ===");
    g_rootType = anim::AnimSystem::registerComponents(w);
    check(g_rootType != 0 && w.componentId("CRootMotion") == g_rootType, "CRootMotion got a type id and answers to its name");
    check(w.componentVerified(g_rootType), "its field table covers the struct with no gap");
    check(w.fieldId("CRootMotion.mode") != 0 && w.fieldId("CRootMotion.accumYaw") != 0 && w.fieldId("CRootMotion.flags") != 0,
          "its fields are reachable by name");
    {
        const scene::Entity e = makeEntity("attached", walkId, anim::kRootMotionApply, anim::kRootAxesDefault, 2.0f);
        const anim::CRootMotion* c = rm(e);
        check(c && c->mode == anim::kRootMotionApply && c->scale == 2.0f && c->axes == anim::kRootAxesDefault &&
                  c->accumX == 0.0f && c->flags == 0,
              "attach writes a fresh value over the zero-fill");
        retire(sys, e);
    }

    AVER_INFO("=== mode None (and a raw zero-filled attach) changes nothing ===");
    {
        const scene::Entity plain = makeEntity("noComponent", walkId, -1);
        const scene::Entity raw = makeEntity("rawAttach", walkId, -1);
        w.addComponent(raw, g_rootType);
        for (int i = 0; i < 2; ++i) sys.tick(w, 0.25f);
        const anim::Pose* a = sys.pose(plain);
        const anim::Pose* b = sys.pose(raw);
        check(a && b, "both are posed");
        if (a && b) {
            checkNear(a->local[0].position.x, 50.0f, 1e-3f, "with no component the clip's root travel is in the pose (0.5 s of 100 cm/s)");
            checkNear(b->local[0].position.x, 50.0f, 1e-3f, "a zero-filled component reads as None: the same pose");
        }
        check(rm(raw) && rm(raw)->accumX == 0.0f && w.localTransform(raw).position.x == 0.0f, "and nothing accumulated or moved");
        retire(sys, plain);
        retire(sys, raw);
    }

    AVER_INFO("=== Ignore: the root is held, the motion thrown away ===");
    {
        const scene::Entity e = makeEntity("ignore", walkId, anim::kRootMotionIgnore);
        for (int i = 0; i < 3; ++i) sys.tick(w, 0.25f);
        const anim::Pose* p = sys.pose(e);
        checkNear(p ? p->local[0].position.x : -1.0f, 0.0f, 1e-4f, "the pose root is locked to rest");
        check(w.localTransform(e).position.x == 0.0f, "the entity did not move");
        check(rm(e)->accumX == 0.0f && rm(e)->accumSeconds == 0.0f, "and nothing was kept");
        retire(sys, e);
    }

    AVER_INFO("=== Accumulate: the entity stays, the script gets the motion ===");
    {
        const scene::Entity e = makeEntity("accum", walkId, anim::kRootMotionAccumulate);
        sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 0.0f, 1e-4f, "the first tick only establishes the clock: no motion");
        sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 25.0f, 1e-3f, "a quarter second of a 100 cm/s clip is 25 cm");
        sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 50.0f, 1e-3f, "and it keeps adding");
        checkNear(rm(e)->accumSeconds, 0.75f, 1e-4f, "over the time the accumulator covers");
        const anim::Pose* p = sys.pose(e);
        checkNear(p ? p->local[0].position.x : -1.0f, 0.0f, 1e-4f, "the pose root is locked");
        check(w.localTransform(e).position.x == 0.0f, "the entity did not move");

        rm(e)->accumX = 0.0f;   // a script clears it by writing zero
        rm(e)->accumSeconds = 0.0f;
        sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 25.0f, 1e-3f, "after a clear it counts from zero");

        // That last step ran 0.75 -> 1.0, through the loop seam, and still counted 25. A whole lap is 100.
        rm(e)->accumX = 0.0f;
        for (int i = 0; i < 4; ++i) sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 100.0f, 1e-2f, "a full lap, seam included, is exactly one clip's travel");
        retire(sys, e);
    }

    AVER_INFO("=== scale and entity scale ===");
    {
        const scene::Entity e = makeEntity("scaled", walkId, anim::kRootMotionAccumulate, 0, 2.0f);
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 50.0f, 1e-3f, "CRootMotion.scale 2 doubles the translation");
        retire(sys, e);

        const scene::Entity f = makeEntity("big", walkId, anim::kRootMotionAccumulate, 0, 1.5f);
        Transform xf = w.localTransform(f);
        xf.scale = Vec3{2, 2, 2};
        w.setLocalTransform(f, xf);
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        checkNear(rm(f)->accumX, 25.0f * 1.5f * 2.0f, 1e-3f, "scale 1.5 x entity scale 2");
        retire(sys, f);
    }

    AVER_INFO("=== Apply moves the entity along its own heading ===");
    {
        const scene::Entity e = makeEntity("apply", walkId, anim::kRootMotionApply);
        Transform xf = w.localTransform(e);
        xf.rotation = Quat::fromAxisAngle(Vec3{0, 0, 1}, kPi * 0.5f);
        w.setLocalTransform(e, xf);
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        const Transform& t = w.localTransform(e);
        checkNear(t.position.x, 0.0f, 1e-3f, "facing +Y, a clip that moves +X does not move the entity along world X");
        checkNear(t.position.y, 25.0f, 1e-3f, "it moves along world +Y");
        check(rm(e)->accumX == 0.0f && rm(e)->accumSeconds == 0.0f, "and the accumulator is spent");
        retire(sys, e);

        const scene::Entity g = makeEntity("turner", turnId, anim::kRootMotionApply);
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        checkNear(anim::yawOf(w.localTransform(g).rotation), kPi * 0.5f * 0.25f, 1e-3f, "a yawing clip turns the entity (22.5 degrees in the second quarter)");
        sys.tick(w, 0.25f);
        checkNear(anim::yawOf(w.localTransform(g).rotation), kPi * 0.5f * 0.5f, 1e-3f, "and keeps turning (45 degrees at 0.75 s)");
        const anim::Pose* p = sys.pose(g);
        checkNear(p ? anim::yawOf(p->local[0].rotation) : 9.0f, 0.0f, 1e-4f, "while the pose root's heading is locked");
        retire(sys, g);
    }

    AVER_INFO("=== External: a physics character consumes it, nobody else may hoard it ===");
    {
        const scene::Entity e = makeEntity("external", walkId, anim::kRootMotionApply);
        rm(e)->flags = anim::kRootMotionExternal;
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        check(w.localTransform(e).position.x == 0.0f, "Apply leaves CLocal alone");
        checkNear(rm(e)->accumX, 25.0f, 1e-3f, "the motion waits in the accumulator");
        sys.tick(w, 0.25f);
        check(rm(e)->accumX == 0.0f && rm(e)->accumSeconds == 0.0f, "past 0.5 s with no taker it is dropped");
        check(w.localTransform(e).position.x == 0.0f, "and the entity still did not move");
        retire(sys, e);
    }

    AVER_INFO("=== Apply only while Play is live (an editor opts in) ===");
    {
        // Default off: Apply moves the entity whether or not a Play session is live, as every case above relies on.
        const scene::Entity d = makeEntity("default", walkId, anim::kRootMotionApply);
        check(!sys.objectAnimationLive(), "no Play session is live");
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        checkNear(w.localTransform(d).position.x, 25.0f, 1e-3f, "default: Apply moves the entity with no session live");
        retire(sys, d);

        sys.setRootMotionRequiresLive(true);
        const scene::Entity e = makeEntity("gated", walkId, anim::kRootMotionApply);
        const scene::Entity acc = makeEntity("gatedAccum", walkId, anim::kRootMotionAccumulate);
        for (int i = 0; i < 2; ++i) sys.tick(w, 0.25f);
        check(w.localTransform(e).position.x == 0.0f, "gated and not live: the entity stays at its placement");
        check(rm(e)->accumX == 0.0f && rm(e)->accumSeconds == 0.0f, "and nothing accumulates");
        const auto* an = w.component<scene::CAnimator>(e, scene::kComponentAnimator);
        check(sys.pose(e) != nullptr && an && an->time > 0.0f, "while the pose and the clock still advance");
        checkNear(rm(acc)->accumX, 25.0f, 1e-3f, "Accumulate is not gated");
        check(w.localTransform(acc).position.x == 0.0f, "and does not move the entity either");
        retire(sys, acc);
        retire(sys, e);

        sys.setObjectAnimationLive(true);
        const scene::Entity live = makeEntity("live", walkId, anim::kRootMotionApply);
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        checkNear(w.localTransform(live).position.x, 25.0f, 1e-3f, "gated and live: Apply moves the entity exactly as before");
        check(rm(live)->accumX == 0.0f && rm(live)->accumSeconds == 0.0f, "and spends the accumulator");
        retire(sys, live);

        // Leaving Play puts the gate back in force for what is placed next.
        sys.setObjectAnimationLive(false);
        const scene::Entity after = makeEntity("afterPlay", walkId, anim::kRootMotionApply);
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        check(w.localTransform(after).position.x == 0.0f, "once the session ends the entity is held again");
        retire(sys, after);
        sys.setRootMotionRequiresLive(false);
    }

    AVER_INFO("=== a scrub is not motion ===");
    {
        const scene::Entity e = makeEntity("scrub", walkId, anim::kRootMotionAccumulate);
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        rm(e)->accumX = 0.0f;
        auto* a = w.component<scene::CAnimator>(e, scene::kComponentAnimator);
        a->time = 0.9f;   // an external write to the clock
        sys.tick(w, 0.25f);
        check(rm(e)->accumX == 0.0f, "the jump produced no delta");
        sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 25.0f, 1e-3f, "and the next step is ordinary motion again");

        rm(e)->accumX = 0.0f;
        a = w.component<scene::CAnimator>(e, scene::kComponentAnimator);
        a->clip = walk2Id;
        sys.tick(w, 0.25f);
        check(rm(e)->accumX == 0.0f, "switching clips resets the clock: that tick produced no delta");
        sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 50.0f, 1e-3f, "and the new clip's 200 cm/s applies after");
        retire(sys, e);
    }

    AVER_INFO("=== sources ===");
    {
        const scene::Entity e = makeEntity("montageOnly", walkId, anim::kRootMotionAccumulate);
        rm(e)->sources = anim::kRootSrcMontage;
        sys.tick(w, 0.25f);
        sys.tick(w, 0.25f);
        check(rm(e)->accumX == 0.0f, "a montage-only entity ignores the clip's root motion");
        const anim::Pose* p = sys.pose(e);
        checkNear(p ? p->local[0].position.x : -1.0f, 50.0f, 1e-3f, "and does not lock its root either");
        retire(sys, e);
    }

    AVER_INFO("=== the montage path ===");
    {
        // A flagged montage over a clip with no root motion.
        const scene::Entity e = makeEntity("mpath", stillId, anim::kRootMotionAccumulate);
        sys.tick(w, 0.25f);
        check(sys.montages().play(sys, w, e, mWalk, 0, 0, 1.0f, -1.0f), "a flagged montage plays");
        sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 25.0f, 1e-3f, "its root motion reaches the accumulator");
        const anim::Pose* p = sys.pose(e);
        checkNear(p ? p->local[0].position.x : -1.0f, 0.0f, 1e-4f, "with the montage's root locked in the pose");
        retire(sys, e);
    }
    {
        // Half-faded-in montage over a moving clip: the deltas mix, they do not add.
        const scene::Entity e = makeEntity("mixed", walkId, anim::kRootMotionAccumulate);
        sys.tick(w, 0.25f);
        check(sys.montages().play(sys, w, e, mFast, 0, 0, 1.0f, -1.0f), "a flagged montage with a blend-in plays");
        sys.tick(w, 0.25f);
        // Base 25 cm; montage 75 cm at weight 0.5: halfway between them.
        checkNear(rm(e)->accumX, 50.0f, 1e-2f, "coverage 0.5 gives the average of base and montage deltas");
        retire(sys, e);
    }
    {
        // A montage whose clip carries no root motion does not override the base.
        const scene::Entity e = makeEntity("unflagged", walkId, anim::kRootMotionAccumulate);
        sys.tick(w, 0.25f);
        check(sys.montages().play(sys, w, e, mPlain, 0, 0, 1.0f, -1.0f), "an unflagged montage plays");
        sys.tick(w, 0.25f);
        checkNear(rm(e)->accumX, 25.0f, 1e-3f, "the base clip's root motion is untouched");
        retire(sys, e);
    }
    (void)plainId;

    AVER_INFO("=== the state machine path ===");
    {
        anim::AnimStateMachineAsset a;
        a.name = "rm";
        anim::AsmMachine m;
        m.name = "Root";
        anim::AsmState walk;
        walk.name = "Walk";
        walk.asset = "walk.ocanim";
        m.states = {walk};
        a.machines = {m};
        check(anim::saveStateMachine(g_dir + "/rm.ocasm", a, &why), "the machine writes " + why);
        const u64 machineId = reg("rm.ocasm", "rm.ocasm");

        anim::AnimGraphSystem::registerComponents(w);
        anim::AnimGraphSystem graphs;
        graphs.install(sys, w);

        auto build = [&](const char* name, u32 sources) {
            const scene::Entity e = w.create(name);
            auto* sm = static_cast<scene::CSkeletalMesh*>(w.addComponent(e, scene::kComponentSkeletalMesh));
            if (sm) sm->skeleton = kSkelId;
            anim::AnimGraphSystem::attach(w, e, machineId);
            anim::AnimSystem::attach(w, e, anim::kRootMotionAccumulate, 0, 1.0f);
            rm(e)->sources = sources;
            return e;
        };
        auto step = [&](f32 dt) {
            graphs.tick(w, dt);
            sys.tick(w, dt);
        };

        const scene::Entity e = build("graphRoot", 0);
        step(0.25f);
        checkNear(rm(e)->accumX, 25.0f, 1e-3f, "the machine's state delta accumulates from its first tick");
        step(0.25f);
        checkNear(rm(e)->accumX, 50.0f, 1e-3f, "and is counted once per tick, not twice");
        const anim::Pose* p = sys.pose(e);
        checkNear(p ? p->local[0].position.x : -1.0f, 0.0f, 1e-4f, "the machine's pose root is locked");
        check(w.localTransform(e).position.x == 0.0f, "Accumulate leaves the entity");

        const scene::Entity only = build("graphMontageOnly", anim::kRootSrcMontage);
        step(0.25f);
        step(0.25f);
        check(rm(only)->accumX == 0.0f, "sources = montage only: the machine's motion is not extracted");
        const anim::Pose* q = sys.pose(only);
        checkNear(q ? q->local[0].position.x : -1.0f, 50.0f, 1e-3f, "and its pose root is not locked");

        sys.montages().stopAll();
        w.destroy(e);
        w.destroy(only);
        w.flush();
        graphs.uninstall(sys);
    }

    AVER_INFO(g_failures == 0 ? "RootMotionSystemTest: PASS" : "RootMotionSystemTest: FAIL");
    return g_failures == 0 ? 0 : 1;
}
