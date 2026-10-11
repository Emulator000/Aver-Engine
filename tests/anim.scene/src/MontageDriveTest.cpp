// Driven montage players (docs/SEQUENCER.md section 5): a sequencer places a player at an exact position with
// an exact weight every tick, so scrubbing is exact and nothing is left over. driveClip must show exactly
// sampleAnimation(clip, t) over rest; a second call at t2 moves it with no state kept; weight blends; no
// events and no notifies; tick() never advances a driven player; one that lapses for three ticks drops
// silently; releaseDriven puts the entity back on its own pose; it works over a state machine and a bare
// animator; an entity without a skeleton refuses; the CAnimator's bytes are untouched.
#include "aver/anim/AnimGraphAsset.hpp"
#include "aver/anim/AnimGraphSystem.hpp"
#include "aver/anim/AnimMontages.hpp"
#include "aver/anim/AnimSampler.hpp"
#include "aver/anim/AnimSystem.hpp"
#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcAnim.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/World.hpp"

#include <cmath>
#include <cstring>
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
static const u64 kOwner = 0xA11CE5EEDull;

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

// root(0) -> spine(1) -> head(2) -> hand(3); root -> leg(4).
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

static scene::Entity makeEntity(scene::World& w, const char* name, u64 clip, bool skeleton = true) {
    const scene::Entity e = w.create(name);
    if (skeleton) {
        auto* sm = static_cast<scene::CSkeletalMesh*>(w.addComponent(e, scene::kComponentSkeletalMesh));
        if (sm) sm->skeleton = kSkelId;
    }
    if (clip != 0) {
        auto* a = static_cast<scene::CAnimator*>(w.addComponent(e, scene::kComponentAnimator));
        if (a) a->clip = clip;
    }
    return e;
}

static void retire(scene::World& w, anim::AnimSystem& sys, scene::Entity e) {
    sys.montages().stopAll();
    w.destroy(e);
    w.flush();
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("MontageDriveTest");

    std::error_code ec;
    g_dir = (std::filesystem::temp_directory_path(ec) / "aver-montage-drive-test").string();
    std::filesystem::remove_all(g_dir, ec);
    std::filesystem::create_directories(g_dir, ec);

    std::string why;
    check(fmt::saveOcSkel(g_dir + "/rig.ocskel", rig(), &why), "the skeleton writes " + why);
    auto saveClip = [&](const char* ref, const fmt::OcAnimation& c) {
        check(fmt::saveOcAnim(g_dir + "/" + ref, c, &why), std::string(ref) + " writes " + why);
        return reg(ref, ref);
    };
    const u64 baseId = saveClip("base.ocanim", clipOf(1.0f, {{4, 7.0f, 7.0f}, {3, -3.0f, -3.0f}}));
    const u64 slideId = saveClip("slide.ocanim", clipOf(1.0f, {{3, 0.0f, 100.0f}}));
    const u64 upId = saveClip("up.ocanim", clipOf(1.0f, {{1, 40.0f, 40.0f}}));

    anim::MontageAsset mont;
    mont.name = "Slide";
    mont.blendIn = 0.0f;
    mont.blendOut = 0.2f;
    mont.segments = {{"slide.ocanim", 0.0f, -1.0f, 1.0f}};
    mont.sections = {{"Default", 0.0f, ""}};
    check(anim::saveMontage(g_dir + "/slide.ocmontage", mont, &why), "the montage writes " + why);
    const u64 montId = reg("slide.ocmontage", "slide.ocmontage");

    scene::World& w = scene::World::instance();
    anim::AnimSystem& sys = anim::animSystem();
    sys.clear();
    sys.setResolver(&resolvePath, nullptr);
    sys.setNotifySink(&recordNotify, nullptr);
    anim::AnimSystem::registerComponents(w);

    auto boneX = [&](scene::Entity e, usize b) { const anim::Pose* p = sys.pose(e); return p ? p->local[b].position.x : -999.0f; };

    AVER_INFO("=== driveClip over a bare animator ===");
    {
        g_ev.clear();
        const scene::Entity e = makeEntity(w, "driven", baseId);
        sys.tick(w, 0.1f);
        checkNear(boneX(e, 3), -3.0f, 1e-4f, "the base clip's hand shows first");
        const auto* cb = w.component<scene::CAnimator>(e, scene::kComponentAnimator);
        scene::CAnimator before = *cb;
        const u64 fired = sys.notifiesFired();

        // Exactly sampleAnimation(clip, t) over rest.
        const fmt::OcAnimation* clip = sys.clip(slideId);
        check(clip != nullptr, "the slide clip loads");
        anim::Pose expect;
        const anim::Pose* cur = sys.pose(e);
        check(cur != nullptr, "the entity has a pose");
        if (clip && cur) {
            anim::restPose(*sys.skeleton(kSkelId), expect);
            anim::sampleAnimation(*clip, 0.3f, expect);
        }
        check(sys.montages().driveClip(sys, w, e, slideId, 0.3f, 1.0f, kOwner), "driveClip accepts");
        sys.tick(w, 0.1f);
        if (clip) checkNear(boneX(e, 3), expect.local[3].position.x, 1e-4f, "driveClip at 0.3 s is exactly sampleAnimation(clip, 0.3)");
        checkNear(boneX(e, 4), 7.0f, 1e-4f, "a bone the clip does not animate keeps the base pose (leg)");

        // Scrub: every call places the player; nothing is left over from the last.
        const f32 times[] = {0.9f, 0.1f, 0.5f, 0.0f, 0.75f};
        for (const f32 t : times) {
            sys.montages().driveClip(sys, w, e, slideId, t, 1.0f, kOwner);
            sys.tick(w, 0.016f);
            checkNear(boneX(e, 3), 100.0f * t, 1e-3f, "scrubbing to " + std::to_string(t) + " is exact");
        }

        // Weight blends toward the base.
        sys.montages().driveClip(sys, w, e, slideId, 0.5f, 0.5f, kOwner);
        sys.tick(w, 0.016f);
        checkNear(boneX(e, 3), -3.0f + (50.0f + 3.0f) * 0.5f, 1e-3f, "weight 0.5 blends halfway over the base pose");

        // No events, no notifies, the animator untouched.
        check(g_ev.empty() && sys.notifiesFired() == fired, "no event reached the sink and notifiesFired did not move");
        const auto* ca = w.component<scene::CAnimator>(e, scene::kComponentAnimator);
        check(ca && ca->clip == before.clip && ca->speed == before.speed && ca->blendWeight == before.blendWeight && ca->flags == before.flags,
              "the CAnimator's clip, speed, weight and flags are unchanged");

        // Driven players are invisible to the script API.
        check(!sys.montages().isPlaying(e, 0) && sys.montages().stop(e, 0, 0.0f) == 0, "play / stop / state do not see a driven player");
        check(sys.montages().drivenCount(e) == 1, "but there is one driven player");

        // Not advanced by tick: the position stays where it was placed.
        sys.montages().driveClip(sys, w, e, slideId, 0.25f, 1.0f, kOwner);
        sys.tick(w, 0.5f);
        checkNear(boneX(e, 3), 25.0f, 1e-3f, "tick does not advance a driven player");

        // It lapses silently after more than two ticks without a call.
        g_ev.clear();
        sys.tick(w, 0.1f);
        check(sys.montages().drivenCount(e) == 1, "two ticks without a call: still there");
        sys.tick(w, 0.1f);   // the third tick since the last call
        check(sys.montages().drivenCount(e) == 0, "a driven player that lapsed for three ticks is gone");
        checkNear(boneX(e, 3), -3.0f, 1e-4f, "and the base pose is back");
        check(g_ev.empty(), "without a word");

        // releaseDriven is silent and immediate.
        sys.montages().driveClip(sys, w, e, slideId, 0.5f, 1.0f, kOwner);
        sys.tick(w, 0.016f);
        sys.montages().releaseDriven(e, kOwner, 0);
        sys.tick(w, 0.016f);
        checkNear(boneX(e, 3), -3.0f, 1e-4f, "releaseDriven leaves the entity on its own pose");
        check(g_ev.empty() && sys.montages().drivenCount(e) == 0, "with no Ended and no notifies");
        retire(w, sys, e);
    }

    AVER_INFO("=== driveMontage ===");
    {
        g_ev.clear();
        const scene::Entity e = makeEntity(w, "drivenMontage", baseId);
        check(sys.montages().driveMontage(sys, w, e, montId, 0.4f, 1.0f, kOwner), "driveMontage accepts");
        sys.tick(w, 0.016f);
        checkNear(boneX(e, 3), 40.0f, 1e-3f, "a montage is placed at its position");
        sys.montages().driveMontage(sys, w, e, montId, 5.0f, 1.0f, kOwner);
        sys.tick(w, 0.016f);
        checkNear(boneX(e, 3), 100.0f, 1e-3f, "past its length it holds the last pose");
        check(g_ev.empty(), "no Started, Section or Ended event");
        // A script's own play of the same montage is a separate player.
        check(sys.montages().play(sys, w, e, montId, 0, 0, 1.0f, -1.0f) && sys.montages().isPlaying(e, montId), "a script can play the same montage beside it");
        retire(w, sys, e);
    }

    AVER_INFO("=== a state machine entity keeps its machine under a driven clip ===");
    {
        anim::AnimStateMachineAsset a;
        a.name = "dm";
        anim::AsmMachine m;
        m.name = "Root";
        anim::AsmState idle;
        idle.name = "Idle";
        idle.asset = "base.ocanim";
        m.states = {idle};
        a.machines = {m};
        check(a.valid(&why), "the machine is valid " + why);
        check(anim::saveStateMachine(g_dir + "/dm.ocasm", a, &why), "it writes " + why);
        const u64 machineId = reg("dm.ocasm", "dm.ocasm");
        anim::AnimGraphSystem::registerComponents(w);
        anim::AnimGraphSystem graphs;
        graphs.install(sys, w);

        const scene::Entity e = w.create("graphDriven");
        auto* skm = static_cast<scene::CSkeletalMesh*>(w.addComponent(e, scene::kComponentSkeletalMesh));
        if (skm) skm->skeleton = kSkelId;
        anim::AnimGraphSystem::attach(w, e, machineId);
        auto step = [&](f32 dt) { graphs.tick(w, dt); sys.tick(w, dt); };
        step(0.1f);
        checkNear(boneX(e, 4), 7.0f, 1e-4f, "the machine's pose shows (leg)");
        check(sys.montages().driveClip(sys, w, e, upId, 0.5f, 1.0f, kOwner), "a clip is driven over it");
        step(0.016f);
        checkNear(boneX(e, 1), 40.0f, 1e-3f, "the driven clip lands on the spine");
        checkNear(boneX(e, 4), 7.0f, 1e-4f, "and the machine still owns the leg");
        sys.montages().releaseDriven(e, kOwner, 0);
        step(0.016f);
        checkNear(boneX(e, 1), 0.0f, 1e-6f, "released: the machine's pose alone");
        sys.montages().stopAll();
        w.destroy(e);
        w.flush();
        graphs.uninstall(sys);
    }

    AVER_INFO("=== refusals, owners and ordinary montages ===");
    {
        const scene::Entity noSkel = makeEntity(w, "noskel", baseId, false);
        check(!sys.montages().driveClip(sys, w, noSkel, slideId, 0.1f, 1.0f, kOwner), "an entity without a skeleton refuses");
        const scene::Entity e = makeEntity(w, "owners", baseId);
        check(!sys.montages().driveClip(sys, w, e, hashOf("nothing.ocanim"), 0.1f, 1.0f, kOwner), "an unknown clip refuses");
        check(!sys.montages().driveMontage(sys, w, e, hashOf("nothing.ocmontage"), 0.1f, 1.0f, kOwner), "an unknown montage refuses");
        check(sys.montages().drivenCount(e) == 0, "and nothing is left behind by a refusal");

        // Two owners drive independently; releasing one leaves the other.
        sys.montages().driveClip(sys, w, e, slideId, 0.5f, 1.0f, 111);
        sys.montages().driveClip(sys, w, e, upId, 0.5f, 1.0f, 222);
        check(sys.montages().drivenCount(e) == 2, "two owners, two players");
        sys.montages().releaseDriven(e, 111, 0);
        check(sys.montages().drivenCount(e) == 1, "releasing one owner leaves the other");

        // An ordinary montage still behaves: coverage and events unchanged.
        g_ev.clear();
        check(sys.montages().play(sys, w, e, montId, 0, 0, 1.0f, -1.0f), "an ordinary montage plays beside a driven one");
        sys.tick(w, 0.1f);
        check(g_ev.size() >= 1 && g_ev[0] == "Slide_Started", "and still raises its events");
        retire(w, sys, noSkel);
        retire(w, sys, e);
    }

    AVER_INFO(g_failures == 0 ? "MontageDriveTest: PASS" : "MontageDriveTest: FAIL");
    return g_failures == 0 ? 0 : 1;
}
