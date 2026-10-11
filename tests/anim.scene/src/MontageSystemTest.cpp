// Montages on entities: the DefaultSlot over a plain CAnimator clip, slot supersession, pipeline slots
// over a state machine, the events that reach the notify sink, the entity-death and clear() guarantees,
// and the provider the framework relays to. No GPU, no scripting host.
//
// The failure this exists to catch is the recurring one: every layer below passes its own test and the
// pieces are wired together wrongly (a montage that plays and is never applied, a slot a pipeline
// consumed that is then applied a second time full-body, a notify state that stays open on a dead
// entity).
#include "aver/anim/AnimGraphAsset.hpp"
#include "aver/anim/AnimGraphSystem.hpp"
#include "aver/anim/AnimMontages.hpp"
#include "aver/anim/AnimSystem.hpp"
#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcAnim.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/World.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
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
static const u64 kSkelId = 0x5EE1AA78ull;

static u64 hashOf(const char* s) { return fnv1a64(std::string_view(s)); }

static u64 reg(const std::string& ref, const std::string& file) {
    const u64 id = hashOf(ref.c_str());
    g_paths[id] = g_dir + "/" + file;
    return id;
}

static std::string resolvePath(u64 id, void*) {
    if (id == kSkelId) return g_dir + "/rig.ocskel";
    const auto it = g_paths.find(id);
    return it == g_paths.end() ? std::string{} : it->second;
}

// root(0) -> spine(1) -> head(2) -> hand(3); root -> leg(4). All at the origin at rest.
static fmt::OcSkeleton rig() {
    fmt::OcSkeleton s;
    auto bone = [](const char* n, i32 parent) {
        fmt::OcBone b;
        b.name = n;
        b.parent = parent;
        return b;
    };
    s.bones = {bone("root", -1), bone("spine", 0), bone("head", 1), bone("hand", 2), bone("leg", 0)};
    s.rootBone = 0;
    return s;
}

struct Slide { u16 bone; f32 from, to; };

static fmt::OcAnimation clipOf(f32 dur, std::vector<Slide> slides) {
    fmt::OcAnimation c;
    c.duration = dur;
    c.flags = fmt::kOcAnimLoop;
    for (const Slide& s : slides) {
        fmt::OcTrack t;
        t.boneIndex = s.bone;
        t.channels = fmt::kOcChannelTranslation;
        t.times = {0.0f, dur};
        t.values = {s.from, 0, 0, s.to, 0, 0};
        c.tracks.push_back(t);
    }
    return c;
}

static std::vector<std::string> g_ev;
static void recordNotify(scene::Entity, const char* name, void*) { g_ev.emplace_back(name); }
static std::string seq() {
    std::string out;
    for (const std::string& n : g_ev) { if (!out.empty()) out += ","; out += n; }
    return out;
}
static int count(const std::string& name) {
    int n = 0;
    for (const std::string& e : g_ev) n += e == name ? 1 : 0;
    return n;
}

static anim::MontageAsset montage(const char* name, const char* clip) {
    anim::MontageAsset a;
    a.name = name;
    a.blendIn = 0.0f;
    a.blendOut = 0.2f;
    a.inCurve = anim::BlendCurve::Linear;
    a.outCurve = anim::BlendCurve::Linear;
    a.segments = {{clip, 0.0f, -1.0f, 1.0f}};
    a.sections = {{"Default", 0.0f, ""}};
    return a;
}

static u64 saveMontageAs(const char* ref, const anim::MontageAsset& a) {
    std::string why;
    if (!anim::saveMontage(g_dir + "/" + ref, a, &why)) AVER_ERROR("   {} did not write: {}", ref, why);
    return reg(ref, ref);
}

static scene::Entity makeEntity(scene::World& w, const char* name, u64 clip, bool skeleton = true) {
    const scene::Entity e = w.create(name);
    if (skeleton) {
        auto* sm = static_cast<scene::CSkeletalMesh*>(w.addComponent(e, scene::kComponentSkeletalMesh));
        if (sm) sm->skeleton = kSkelId;
    }
    auto* a = static_cast<scene::CAnimator*>(w.addComponent(e, scene::kComponentAnimator));
    if (a) a->clip = clip;
    return e;
}

static void retire(scene::World& w, anim::AnimSystem& sys, scene::Entity e) {
    sys.montages().stopAll();   // silent: no events from here on
    w.destroy(e);
    w.flush();
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("MontageSystemTest");

    std::error_code ec;
    g_dir = (std::filesystem::temp_directory_path(ec) / "aver-montage-test").string();
    std::filesystem::remove_all(g_dir, ec);
    std::filesystem::create_directories(g_dir, ec);

    std::string why;
    check(fmt::saveOcSkel(g_dir + "/rig.ocskel", rig(), &why), "the skeleton writes " + why);
    auto saveClip = [&](const char* ref, const fmt::OcAnimation& c) {
        check(fmt::saveOcAnim(g_dir + "/" + ref, c, &why), std::string(ref) + " writes " + why);
        return reg(ref, ref);
    };
    // The base animation: the leg at 7 and the hand at -3, held.
    const u64 baseId = saveClip("base.ocanim", clipOf(1.0f, {{4, 7.0f, 7.0f}, {3, -3.0f, -3.0f}}));
    saveClip("m1.ocanim", clipOf(1.0f, {{3, 0.0f, 100.0f}}));
    saveClip("m2.ocanim", clipOf(1.0f, {{3, 200.0f, 200.0f}}));
    saveClip("mu.ocanim", clipOf(1.0f, {{1, 40.0f, 40.0f}, {4, 99.0f, 99.0f}}));

    anim::MontageAsset m1 = montage("M1", "m1.ocanim");
    anim::MontageAsset m2 = montage("M2", "m2.ocanim");
    m2.blendIn = 0.2f;
    anim::MontageAsset mw = montage("MW", "m1.ocanim");
    mw.notifies = {{0.1f, "Win", 0.5f}};
    anim::MontageAsset mn = montage("MN", "m1.ocanim");
    mn.notifies = {{0.1f, "Hit", 0.0f}, {0.2f, "Win", 0.4f}};
    anim::MontageAsset ms = montage("MS", "m1.ocanim");
    ms.sections = {{"Intro", 0.0f, ""}, {"Main", 0.5f, ""}};
    anim::MontageAsset mu = montage("MU", "mu.ocanim");
    mu.slot = "UpperBody";
    const u64 m1Id = saveMontageAs("m1.ocmontage", m1);
    const u64 m2Id = saveMontageAs("m2.ocmontage", m2);
    const u64 mwId = saveMontageAs("mw.ocmontage", mw);
    const u64 mnId = saveMontageAs("mn.ocmontage", mn);
    const u64 msId = saveMontageAs("ms.ocmontage", ms);
    const u64 muId = saveMontageAs("mu.ocmontage", mu);
    {
        std::ofstream bad(g_dir + "/bad.ocmontage");
        bad << "{ this is not a montage";
    }
    const u64 badId = reg("bad.ocmontage", "bad.ocmontage");

    scene::World& w = scene::World::instance();
    anim::AnimSystem& sys = anim::animSystem();
    sys.clear();
    sys.setResolver(&resolvePath, nullptr);
    sys.setNotifySink(&recordNotify, nullptr);
    anim::AnimSystem::registerComponents(w);

    auto handX = [&](scene::Entity e) { const anim::Pose* p = sys.pose(e); return p ? p->local[3].position.x : -999.0f; };
    auto boneX = [&](scene::Entity e, usize b) { const anim::Pose* p = sys.pose(e); return p ? p->local[b].position.x : -999.0f; };

    AVER_INFO("=== a DefaultSlot montage over a plain CAnimator clip ===");
    {
        g_ev.clear();
        const scene::Entity e = makeEntity(w, "hero", baseId);
        sys.tick(w, 0.1f);
        checkNear(handX(e), -3.0f, 1e-4f, "before any montage the base clip's hand pose shows");
        check(sys.montages().play(sys, w, e, m1Id, 0, 0, 1.0f, -1.0f), "play succeeds");
        check(sys.montages().isPlaying(e, 0) && sys.montages().isPlaying(e, m1Id), "and the entity is playing it");
        sys.tick(w, 0.5f);
        checkNear(handX(e), 50.0f, 1e-3f, "the montage's hand replaces the clip's at its position (0.5 s of a 100 cm slide)");
        checkNear(boneX(e, 4), 7.0f, 1e-4f, "a bone the montage does not animate keeps the base clip's pose");
        u32 n = 0;
        const Mat4* skin = sys.skinning(e, n);
        check(skin && n == 5, "skinning matrices exist for the five bones");
        if (skin && n == 5) {
            checkNear(skin[3].m[3][0], 50.0f, 1e-3f, "and the hand's skinning matrix carries the montage pose");
            checkNear(skin[4].m[3][0], 7.0f, 1e-3f, "and the leg's the base pose");
        }
        check(seq() == "M1_Started,M1_Section_Default", "Started and the first section reached the sink, got '" + seq() + "'");
        check(sys.notifiesFired() == g_ev.size(), "and were counted by notifiesFired()");

        // Past the end: blend-out, then Ended once, and the base pose returns.
        g_ev.clear();
        sys.tick(w, 0.4f);   // pos 0.9: inside the blend-out trigger
        check(seq() == "M1_BlendOut", "the automatic blend-out raised BlendOut, got '" + seq() + "'");
        g_ev.clear();
        sys.tick(w, 0.2f);
        sys.tick(w, 0.1f);
        check(seq() == "M1_Ended", "Ended arrives once the fade completes, got '" + seq() + "'");
        check(!sys.montages().isPlaying(e, 0), "the finished montage is dropped");
        checkNear(handX(e), -3.0f, 1e-4f, "and the base pose is back");
        retire(w, sys, e);
    }

    AVER_INFO("=== the newest montage in a slot supersedes the older ===");
    {
        g_ev.clear();
        const scene::Entity e = makeEntity(w, "super", baseId);
        check(sys.montages().play(sys, w, e, m1Id, 0, 0, 1.0f, -1.0f), "M1 plays");
        sys.tick(w, 0.1f);
        check(sys.montages().play(sys, w, e, m2Id, 0, 0, 1.0f, -1.0f), "M2 plays in the same slot");
        sys.tick(w, 0.1f);
        // M1 is half faded out at pos 0.2 (hand 20), M2 half faded in (hand 200): the slot mixes to
        // 20 + (200 - 20) * 0.5 = 110 at coverage 1 - 0.5 * 0.5 = 0.75, over the base's -3.
        checkNear(handX(e), -3.0f + (110.0f + 3.0f) * 0.75f, 0.05f, "both blend, by weight, oldest to newest");
        check(seq() == "M1_Started,M1_Section_Default,M1_Interrupted,M1_BlendOut,M2_Started,M2_Section_Default",
              "the old one raised Interrupted then BlendOut, the new one Started; got '" + seq() + "'");
        g_ev.clear();
        sys.tick(w, 0.1f);
        check(seq() == "M1_Ended", "the old one ends once its fade completes, got '" + seq() + "'");
        checkNear(handX(e), 200.0f, 1e-3f, "and the newest holds the pose alone");
        check(sys.montages().isPlaying(e, m2Id) && !sys.montages().isPlaying(e, m1Id), "only M2 remains");
        retire(w, sys, e);
    }

    AVER_INFO("=== notifies and notify states reach the sink by name ===");
    {
        g_ev.clear();
        const scene::Entity e = makeEntity(w, "notes", baseId);
        check(sys.montages().play(sys, w, e, mnId, 0, 0, 1.0f, -1.0f), "MN plays");
        sys.tick(w, 0.15f);
        check(seq() == "MN_Started,MN_Section_Default,Hit", "an instant notify fires under its own name, got '" + seq() + "'");
        g_ev.clear();
        sys.tick(w, 0.1f);
        check(seq() == "Win_Begin", "a state opens as Name_Begin, got '" + seq() + "'");
        g_ev.clear();
        sys.tick(w, 0.4f);
        check(seq() == "Win_End", "and closes as Name_End, got '" + seq() + "'");
        g_ev.clear();
        sys.tick(w, 0.3f);
        sys.tick(w, 0.2f);
        check(seq() == "MN_BlendOut,MN_Ended", "the lifecycle closes the montage, got '" + seq() + "'");
        retire(w, sys, e);
    }

    AVER_INFO("=== refusals ===");
    {
        const scene::Entity noSkel = makeEntity(w, "noskel", baseId, false);
        check(!sys.montages().play(sys, w, noSkel, m1Id, 0, 0, 1.0f, -1.0f), "an entity without a skeleton refuses");
        const scene::Entity e = makeEntity(w, "refuse", baseId);
        check(!sys.montages().play(sys, w, e, hashOf("nothing.ocmontage"), 0, 0, 1.0f, -1.0f), "an unknown asset refuses");
        check(!sys.montages().play(sys, w, e, hashOf("nothing.ocmontage"), 0, 0, 1.0f, -1.0f), "and refuses again (the failure is cached)");
        check(!sys.montages().play(sys, w, e, badId, 0, 0, 1.0f, -1.0f), "a corrupt file refuses");
        check(!sys.montages().play(sys, w, e, 0, 0, 0, 1.0f, -1.0f), "montage id 0 refuses");
        check(!sys.montages().isPlaying(e, 0), "and nothing is playing");
        retire(w, sys, noSkel);
        retire(w, sys, e);
    }

    AVER_INFO("=== a paused animator holds its montages ===");
    {
        g_ev.clear();
        const scene::Entity e = makeEntity(w, "paused", baseId);
        sys.montages().play(sys, w, e, m1Id, 0, 0, 1.0f, -1.0f);
        sys.tick(w, 0.1f);
        auto* a = w.component<scene::CAnimator>(e, scene::kComponentAnimator);
        if (a) a->flags |= scene::kAnimatorPaused;
        sys.tick(w, 0.5f);
        f32 v[4] = {};
        u64 sec = 0;
        check(sys.montages().state(e, m1Id, v, &sec), "state is readable");
        checkNear(v[0], 0.1f, 1e-4f, "the position did not move while paused");
        a = w.component<scene::CAnimator>(e, scene::kComponentAnimator);
        if (a) a->flags &= ~scene::kAnimatorPaused;
        sys.tick(w, 0.2f);
        sys.montages().state(e, m1Id, v, &sec);
        checkNear(v[0], 0.3f, 1e-4f, "and continues from there");
        retire(w, sys, e);
    }

    AVER_INFO("=== an entity destroyed mid-montage closes its notify state and ends once ===");
    {
        g_ev.clear();
        const scene::Entity e = makeEntity(w, "dies", baseId);
        sys.montages().play(sys, w, e, mwId, 0, 0, 1.0f, -1.0f);
        sys.tick(w, 0.2f);
        check(count("Win_Begin") == 1 && count("Win_End") == 0, "the state is open");
        w.destroy(e);
        w.flush();
        sys.tick(w, 0.05f);
        check(count("Win_End") == 1 && count("MW_Ended") == 1, "destroying the entity closes it and raises Ended, got '" + seq() + "'");
        sys.tick(w, 0.5f);
        check(count("Win_End") == 1 && count("MW_Ended") == 1, "and nothing is delivered twice");
        check(sys.montages().empty(), "the players are gone");
    }

    AVER_INFO("=== clear() closes everything the same way ===");
    {
        g_ev.clear();
        const scene::Entity e = makeEntity(w, "cleared", baseId);
        sys.montages().play(sys, w, e, mwId, 0, 0, 1.0f, -1.0f);
        sys.tick(w, 0.2f);
        g_ev.clear();
        sys.clear();
        check(seq() == "Win_End,MW_Ended", "clear() delivers NotifyEnd then Ended, got '" + seq() + "'");
        check(sys.montages().empty(), "and drops the players");
        sys.setResolver(&resolvePath, nullptr);
        sys.setNotifySink(&recordNotify, nullptr);
        g_ev.clear();
        sys.tick(w, 0.5f);
        check(g_ev.empty(), "nothing is left to deliver afterwards");
        retire(w, sys, e);

        // stopAll() is the silent one.
        const scene::Entity s = makeEntity(w, "silent", baseId);
        sys.montages().play(sys, w, s, mwId, 0, 0, 1.0f, -1.0f);
        sys.tick(w, 0.2f);
        g_ev.clear();
        sys.montages().stopAll();
        sys.tick(w, 0.1f);
        check(g_ev.empty() && !sys.montages().isPlaying(s, 0), "stopAll() ends play without delivering events");
        retire(w, sys, s);
    }

    AVER_INFO("=== the provider relays every operation ===");
    {
        g_ev.clear();
        const scene::Entity e = makeEntity(w, "scripted", baseId);
        const i32 ent = static_cast<i32>(e);
        void* user = &sys;
        constexpr i32 kPlay = 1, kStop = 2, kStopSlot = 3, kJump = 4, kNext = 5, kRate = 6, kPlaying = 7, kState = 8;
        f32 f[4] = {};
        i64 i[1] = {};

        check(anim::montageProvider(kPlay, ent, static_cast<i64>(msId), 0, static_cast<i64>(hashOf("Main")), 1.0f, -1.0f,
                                    nullptr, nullptr, user) == 1,
              "PLAY starts at a named section");
        check(anim::montageProvider(kPlaying, ent, 0, 0, 0, 0, 0, nullptr, nullptr, user) == 1, "PLAYING with montage 0 means any");
        check(anim::montageProvider(kState, ent, 0, 0, 0, 0, 0, f, i, user) == 1, "STATE with montage 0 reads the newest");
        checkNear(f[0], 0.5f, 1e-5f, "STATE position is the section start");
        checkNear(f[1], 1.0f, 1e-5f, "STATE length");
        checkNear(f[2], 1.0f, 1e-5f, "STATE weight (no blend-in)");
        checkNear(f[3], 1.0f, 1e-5f, "STATE rate");
        check(i[0] == static_cast<i64>(hashOf("Main")), "STATE reports the section's name hash");

        check(anim::montageProvider(kJump, ent, 0, static_cast<i64>(hashOf("Intro")), 0, 0, 0, nullptr, nullptr, user) == 1,
              "JUMP with montage 0 targets the newest");
        anim::montageProvider(kState, ent, static_cast<i64>(msId), 0, 0, 0, 0, f, i, user);
        check(i[0] == static_cast<i64>(hashOf("Intro")) && std::fabs(f[0]) < 1e-5f, "and the montage is in Intro at 0");
        check(anim::montageProvider(kJump, ent, 0, static_cast<i64>(hashOf("Nope")), 0, 0, 0, nullptr, nullptr, user) == 0,
              "JUMP to an unknown section refuses");

        check(anim::montageProvider(kNext, ent, static_cast<i64>(msId), static_cast<i64>(hashOf("Main")),
                                    static_cast<i64>(hashOf("Intro")), 0, 0, nullptr, nullptr, user) == 1,
              "NEXT overrides a link");
        check(anim::montageProvider(kNext, ent, static_cast<i64>(msId), static_cast<i64>(hashOf("Main")), 0, 0, 0,
                                    nullptr, nullptr, user) == 1,
              "and a 0 target restores it");
        check(anim::montageProvider(kNext, ent, static_cast<i64>(msId), static_cast<i64>(hashOf("Nope")), 0, 0, 0,
                                    nullptr, nullptr, user) == 0,
              "NEXT from an unknown section refuses");

        check(anim::montageProvider(kRate, ent, static_cast<i64>(msId), 0, 0, 0.5f, 0, nullptr, nullptr, user) == 1, "RATE");
        anim::montageProvider(kState, ent, static_cast<i64>(msId), 0, 0, 0, 0, f, i, user);
        checkNear(f[3], 0.5f, 1e-6f, "and STATE reads the new rate");

        check(anim::montageProvider(kStopSlot, ent, static_cast<i64>(hashOf("Elsewhere")), 0, 0, 0.0f, 0, nullptr, nullptr, user) == 0,
              "STOP_SLOT on an empty slot stops nothing");
        check(anim::montageProvider(kStopSlot, ent, static_cast<i64>(hashOf("DefaultSlot")), 0, 0, 0.0f, 0, nullptr, nullptr, user) == 1,
              "STOP_SLOT stops the slot's montage and returns how many");
        check(anim::montageProvider(kPlaying, ent, 0, 0, 0, 0, 0, nullptr, nullptr, user) == 0, "and it is no longer playing (a cut)");

        anim::montageProvider(kPlay, ent, static_cast<i64>(msId), 0, 0, 1.0f, -1.0f, nullptr, nullptr, user);
        anim::montageProvider(kPlay, ent, static_cast<i64>(m1Id), static_cast<i64>(hashOf("Other")), 0, 1.0f, -1.0f,
                              nullptr, nullptr, user);
        check(anim::montageProvider(kStop, ent, 0, 0, 0, 0.0f, 0, nullptr, nullptr, user) == 2, "STOP with montage 0 stops all and counts them");
        check(anim::montageProvider(kPlaying, ent, 0, 0, 0, 0, 0, nullptr, nullptr, user) == 0, "nothing plays afterwards");

        f[0] = -1.0f;
        check(anim::montageProvider(kState, ent, 0, 0, 0, 0, 0, f, i, user) == 0 && f[0] == -1.0f,
              "STATE with nothing playing leaves the outputs alone");
        check(anim::montageProvider(99, ent, 0, 0, 0, 0, 0, nullptr, nullptr, user) == 0, "an unknown op is 0");
        check(anim::montageProvider(kPlaying, ent, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr) == 0, "no user is 0");
        retire(w, sys, e);
    }

    AVER_INFO("=== pipeline slots over a state machine ===");
    {
        anim::AnimStateMachineAsset a;
        a.name = "pm";
        anim::AsmMachine m;
        m.name = "Root";
        anim::AsmState idle;
        idle.name = "Idle";
        idle.asset = "base.ocanim";
        m.states = {idle};
        a.machines = {m};
        anim::PipeNode n;
        n.kind = anim::PipeNodeKind::Layered;
        n.source.kind = anim::PipeSourceKind::Slot;
        n.source.slot = "UpperBody";
        n.branches = {{"spine", 0, 1.0f}};
        a.pipeline = {n};
        check(a.valid(&why), "the machine with a pipeline is valid " + why);
        check(anim::saveStateMachine(g_dir + "/pm.ocasm", a, &why), "it writes " + why);
        const u64 machineId = reg("pm.ocasm", "pm.ocasm");

        anim::AnimGraphSystem::registerComponents(w);
        anim::AnimGraphSystem graphs;
        graphs.install(sys, w);

        g_ev.clear();
        const scene::Entity e = w.create("graphHero");
        auto* skm = static_cast<scene::CSkeletalMesh*>(w.addComponent(e, scene::kComponentSkeletalMesh));
        if (skm) skm->skeleton = kSkelId;
        anim::AnimGraphSystem::attach(w, e, machineId);
        auto step = [&](f32 dt) {
            graphs.tick(w, dt);
            sys.tick(w, dt);
        };
        step(0.1f);
        checkNear(boneX(e, 4), 7.0f, 1e-4f, "the state machine's pose shows before any montage (leg)");
        checkNear(boneX(e, 1), 0.0f, 1e-6f, "and the spine is at rest");

        check(sys.montages().play(sys, w, e, muId, 0, 0, 1.0f, -1.0f), "an UpperBody montage plays");
        step(0.1f);
        checkNear(boneX(e, 1), 40.0f, 1e-3f, "its spine pose is applied through the pipeline's branch");
        checkNear(boneX(e, 4), 7.0f, 1e-4f, "the leg, which the montage also animates but the branch excludes, keeps the machine's pose");
        checkNear(boneX(e, 3), -3.0f, 1e-4f, "a branch bone the montage does not animate keeps the machine's pose (hand)");
        check(count("MU_Started") == 1, "its events reached the sink");

        // A second slot, DefaultSlot, with no node of its own: applied full-body, beside the first.
        check(sys.montages().play(sys, w, e, m1Id, 0, 0, 1.0f, -1.0f), "a DefaultSlot montage plays at the same time");
        step(0.1f);
        checkNear(handX(e), 10.0f, 1e-3f, "the DefaultSlot montage poses the hand");
        checkNear(boneX(e, 1), 40.0f, 1e-3f, "and the UpperBody one still holds the spine: two slots coexist");
        checkNear(boneX(e, 4), 7.0f, 1e-4f, "and the branch still excludes the leg");

        // The same UpperBody montage on an entity with no slot node is ignored.
        const scene::Entity plain = makeEntity(w, "plain", baseId);
        check(sys.montages().play(sys, w, plain, muId, 0, 0, 1.0f, -1.0f), "the same montage plays on a plain animator");
        step(0.1f);
        checkNear(boneX(plain, 1), 0.0f, 1e-6f, "but with no slot node for UpperBody nothing is applied");
        check(sys.montages().isPlaying(plain, muId), "while it still plays (its events and clock run)");

        sys.montages().stopAll();
        w.destroy(e);
        w.destroy(plain);
        w.flush();
        graphs.uninstall(sys);
    }

    AVER_INFO(g_failures == 0 ? "MontageSystemTest: PASS" : "MontageSystemTest: FAIL");
    return g_failures == 0 ? 0 : 1;
}
