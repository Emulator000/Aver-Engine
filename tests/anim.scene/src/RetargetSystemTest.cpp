// RetargetSystemTest -- a clip authored for skeleton A plays on an entity with skeleton B when the project has a
// .ocretarget: through a plain CAnimator, through a state machine and through a montage. The catalog, the bake
// cache and the rebinding of machines are what is under test; the retargeting math has its own test.
//
// THE FAILURE THIS CATCHES is the recurring one: the baker is right, the catalog is right, and one consumer still
// resolves its clip through the raw loader, so that path plays garbage by bone index.
#include "../../anim/src/RetargetFixture.hpp"

#include "aver/anim/AnimGraphAsset.hpp"
#include "aver/anim/AnimGraphSystem.hpp"
#include "aver/anim/AnimMontages.hpp"
#include "aver/anim/AnimSampler.hpp"
#include "aver/anim/AnimSystem.hpp"
#include "aver/anim/Retarget.hpp"
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/World.hpp"

#include <cmath>
#include <filesystem>
#include <map>
#include <string>

using namespace aver;
using namespace rtfix;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static std::string f2s(f32 v) { char b[64]; std::snprintf(b, sizeof b, "%.3f", v); return b; }
static f32 angleDeg(const Quat& a, const Quat& b) {
    const Quat r = a * conjq(b);
    return 2.0f * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::fabs(r.w)) * kRadToDeg;
}

static std::string g_dir;
static std::map<u64, std::string> g_paths;

static u64 reg(const std::string& ref, const std::string& file) {
    const u64 id = fnv1a64(std::string_view(ref));
    g_paths[id] = g_dir + "/" + file;
    return id;
}
static std::string resolvePath(u64 id, void*) {
    const auto it = g_paths.find(id);
    return it == g_paths.end() ? std::string{} : it->second;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO("runtime retargeting");

    std::error_code ec;
    g_dir = (std::filesystem::temp_directory_path(ec) / "aver-retarget-sys-test").string();
    std::filesystem::remove_all(g_dir, ec);
    std::filesystem::create_directories(g_dir, ec);
    std::string why;

    // Mannequin: T-pose, 3 spine bones, aligned bone frames. Hero: A-pose, 4 spine bones, world-aligned frames.
    const Rig mannequin = makeRig(Params{Naming::Ue, 1.0f, false, 3, true});
    const Rig hero = makeRig(Params{Naming::Ue, 1.0f, true, 4, false});
    const u64 mannequinId = reg("Characters/mannequin.ocskel", "mannequin.ocskel");
    const u64 heroId = reg("Characters/hero.ocskel", "hero.ocskel");
    check(fmt::saveOcSkel(g_dir + "/mannequin.ocskel", mannequin.skel, &why) && fmt::saveOcSkel(g_dir + "/hero.ocskel", hero.skel, &why),
          "(skeletons written) " + why);

    // A clip that holds the left arm straight up.
    anim::Pose raised;
    anim::restPose(mannequin.skel, raised);
    rotateBone(mannequin.skel, raised, mannequin.upper[0], Quat::fromAxisAngle(Vec3{1, 0, 0}, -radians(90.0f)));
    fmt::OcAnimation clip;
    clip.duration = 1.0f;
    clip.flags = fmt::kOcAnimLoop;
    clip.skeletonRef = "mannequin";
    {
        fmt::OcTrack t;
        t.boneIndex = static_cast<u16>(mannequin.upper[0]);
        t.channels = fmt::kOcChannelRotation;
        t.times = {0.0f, 1.0f};
        const Quat q = raised.local[static_cast<usize>(mannequin.upper[0])].rotation;
        t.values = {q.x, q.y, q.z, q.w, q.x, q.y, q.z, q.w};
        clip.tracks.push_back(t);
    }
    const u64 clipId = reg("Characters/raise.ocanim", "raise.ocanim");
    check(fmt::saveOcAnim(g_dir + "/raise.ocanim", clip, &why), "(clip written) " + why);
    // The same clip claiming a skeleton nobody can retarget from.
    fmt::OcAnimation alien = clip;
    alien.skeletonRef = "alien";
    const u64 alienId = reg("Characters/alien.ocanim", "alien.ocanim");
    check(fmt::saveOcAnim(g_dir + "/alien.ocanim", alien, &why), "(alien clip written) " + why);

    // The good retargeter, and one whose bones do not exist (a bind that fails).
    anim::RetargetAsset good = anim::autoMapChains(mannequin.skel, hero.skel).asset;
    good.name = "Mannequin_to_Hero";
    good.source = "Characters/mannequin.ocskel";
    good.target = "Characters/hero.ocskel";
    check(anim::saveRetarget(g_dir + "/good.ocretarget", good, &why), "(retargeter written) " + why);
    anim::RetargetAsset bad = good;
    bad.name = "Alien_to_Hero";
    bad.source = "Characters/alien.ocskel";
    bad.chains[0].srcStart = "no_such_bone";
    check(anim::saveRetarget(g_dir + "/bad.ocretarget", bad, &why), "(broken retargeter written) " + why);
    const u64 goodRtId = reg("Characters/good.ocretarget", "good.ocretarget");
    const u64 badRtId = reg("Characters/bad.ocretarget", "bad.ocretarget");

    // What the retargeter itself says for this clip, for comparison.
    anim::Retargeter reference;
    check(reference.bind(good, mannequin.skel, hero.skel), "(reference retargeter binds)");
    anim::Pose expected;
    reference.retarget(raised, expected);
    auto matchesExpected = [&](const anim::Pose& p, const std::string& what) {
        f32 worst = 0.0f;
        for (const u32 b : reference.mappedDstBones()) worst = std::fmax(worst, angleDeg(p.local[b].rotation, expected.local[b].rotation));
        check(worst < 0.5f, what + ": every mapped bone equals the retargeter's output, worst " + f2s(worst) + " degrees");
    };
    auto armDir = [&](const anim::Pose& p) { return dirModel(hero.skel, p, hero.upper[0], hero.lower[0]); };

    scene::World& w = scene::World::instance();
    anim::AnimSystem& sys = anim::animSystem();
    sys.clear();
    sys.setResolver(&resolvePath, nullptr);
    anim::AnimGraphSystem::registerComponents(w);
    anim::AnimGraphSystem graphs;
    graphs.install(sys, w);

    auto makeEntity = [&](const char* name, u64 skel, u64 clipRef, bool paused) {
        const scene::Entity e = w.create(name);
        auto* sm = static_cast<scene::CSkeletalMesh*>(w.addComponent(e, scene::kComponentSkeletalMesh));
        auto* an = static_cast<scene::CAnimator*>(w.addComponent(e, scene::kComponentAnimator));
        sm->skeleton = skel;
        an->clip = clipRef;
        an->flags = paused ? scene::kAnimatorPaused : 0u;
        return e;
    };

    AVER_INFO("=== with no catalog nothing changes: the same pointer, the raw clip ===");
    {
        check(sys.clipFor(clipId, heroId) == sys.clip(clipId) && sys.clip(clipId) != nullptr, "clipFor returns exactly clip() when no retargeter is registered");
        const scene::Entity e = makeEntity("raw", heroId, clipId, true);
        sys.tick(w, 1.0f / 60.0f);
        const anim::Pose* p = sys.pose(e);
        check(p && std::fabs(armDir(*p).z - 1.0f) > 0.001f, "(the raw clip on the wrong skeleton does not raise the arm)");
        w.destroy(e); w.flush();
    }

    AVER_INFO("=== registered: a plain CAnimator plays the bake ===");
    sys.registerRetargets({{badRtId, g_dir + "/bad.ocretarget"}, {goodRtId, g_dir + "/good.ocretarget"}});
    check(sys.retargets().catalogSize() == 2, "both retargeters are in the catalog");
    {
        check(sys.clipFor(clipId, mannequinId) == sys.clip(clipId), "a clip already authored for the entity's skeleton is untouched (same pointer)");
        const fmt::OcAnimation* baked = sys.clipFor(clipId, heroId);
        check(baked != nullptr && baked != sys.clip(clipId) && baked->skeletonRef == "hero", "for the other skeleton clipFor returns a baked clip with skeletonRef 'hero'");
        check(sys.clipFor(clipId, heroId) == baked, "and the second ask is the same baked clip (cached)");
        check(sys.retargets().bakedCount() == 1, "one bake so far");

        const scene::Entity e = makeEntity("animator", heroId, clipId, true);
        sys.tick(w, 1.0f / 60.0f);
        const anim::Pose* p = sys.pose(e);
        check(p != nullptr, "the entity is posed");
        if (p) {
            matchesExpected(*p, "CAnimator");
            check(armDir(*p).z > 0.99f, "and its left arm points up, z = " + f2s(armDir(*p).z));
        }
        w.destroy(e); w.flush();
    }

    AVER_INFO("=== a failed bake falls back to the plain clip ===");
    {
        const fmt::OcAnimation* a1 = sys.clipFor(alienId, heroId);
        check(a1 == sys.clip(alienId), "a retargeter that cannot bind leaves the clip as it was");
        check(sys.clipFor(alienId, heroId) == a1 && sys.retargets().bakedCount() == 1, "and asks again without baking anything");
    }

    AVER_INFO("=== through a state machine ===");
    {
        anim::AnimStateMachineAsset asset;
        asset.name = "raise";
        anim::AsmMachine m;
        m.name = "Root";
        anim::AsmState s;
        s.name = "Raise";
        s.asset = "Characters/raise.ocanim";
        s.loop = true;
        m.states = {s};
        asset.machines = {m};
        check(asset.valid(&why), "(the machine is valid) " + why);
        const u64 machineId = reg("Characters/raise.ocasm", "raise.ocasm");
        check(anim::saveStateMachine(g_dir + "/raise.ocasm", asset, &why), "(machine written) " + why);

        const scene::Entity e = w.create("machine");
        auto* sm = static_cast<scene::CSkeletalMesh*>(w.addComponent(e, scene::kComponentSkeletalMesh));
        sm->skeleton = heroId;
        anim::AnimGraphSystem::attach(w, e, machineId);
        graphs.tick(w, 1.0f / 60.0f);
        sys.tick(w, 1.0f / 60.0f);
        const anim::Pose* p = sys.pose(e);
        check(p != nullptr, "the machine entity is posed");
        if (p) check(armDir(*p).z > 0.99f, "its machine's clip is the bake: the arm points up, z = " + f2s(armDir(*p).z));

        AVER_INFO("=== hot-registering rebinds machines ===");
        // Registering again (a project that gained an asset) bumps the epoch; the machine rebinds to what resolves now.
        const u32 epoch = sys.retargetEpoch();
        sys.registerRetargets({{goodRtId, g_dir + "/good.ocretarget"}});
        check(sys.retargetEpoch() != epoch && sys.retargets().catalogSize() == 1, "re-registering replaces the catalog and moves the epoch");
        graphs.tick(w, 1.0f / 60.0f);
        sys.tick(w, 1.0f / 60.0f);
        p = sys.pose(e);
        check(p && armDir(*p).z > 0.99f, "and the machine still plays the bake after rebinding");
        w.destroy(e); w.flush();
    }

    AVER_INFO("=== through a montage ===");
    {
        anim::MontageAsset ma;
        ma.name = "RaiseArm";
        ma.blendIn = 0.0f;
        ma.blendOut = 0.2f;
        ma.inCurve = anim::BlendCurve::Linear;
        ma.outCurve = anim::BlendCurve::Linear;
        ma.segments = {{"Characters/raise.ocanim", 0.0f, -1.0f, 1.0f}};
        ma.sections = {{"Default", 0.0f, ""}};
        const u64 montageId = reg("Characters/raise.ocmontage", "raise.ocmontage");
        check(anim::saveMontage(g_dir + "/raise.ocmontage", ma, &why), "(montage written) " + why);

        const scene::Entity e = makeEntity("montage", heroId, 0, true);
        check(sys.montages().play(sys, w, e, montageId, 0, 0, 1.0f, 0.0f), "the montage plays");
        sys.tick(w, 1.0f / 60.0f);
        sys.tick(w, 1.0f / 60.0f);
        const anim::Pose* p = sys.pose(e);
        check(p != nullptr, "the montage entity is posed");
        if (p) check(armDir(*p).z > 0.99f, "the montage's segment is the bake: the arm points up, z = " + f2s(armDir(*p).z));
        w.destroy(e); w.flush();
    }

    AVER_INFO("=== clear() drops the bakes and the catalog ===");
    {
        sys.clear();
        check(sys.retargets().bakedCount() == 0 && sys.retargets().catalogSize() == 0, "nothing baked, nothing registered after clear()");
        sys.setResolver(&resolvePath, nullptr);
        check(sys.clipFor(clipId, heroId) == sys.clip(clipId), "and the raw clip is back until the host registers again");
    }

    std::filesystem::remove_all(g_dir, ec);
    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
