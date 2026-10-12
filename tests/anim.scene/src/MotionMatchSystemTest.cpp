// Motion matching on entities: CMotionMatcher registers, a database loads on a worker and bakes a sidecar,
// the standalone matcher poses an entity from its measured motion, a state machine can hold a motionmatch
// state, root motion modes do what they say, and the guards (AnimSystem::clear, a dying entity, a crowd)
// hold. No GPU, no scripting host.
//
// The failure this exists to catch is the recurring one: every layer below passes its own test and the
// pieces are wired together wrongly (a query nothing fills, a pose source nothing calls, a root delta
// computed and never applied).
#include "../../anim/src/MotionTestKit.hpp"

#include "aver/anim/AnimGraphAsset.hpp"
#include "aver/anim/AnimGraphSystem.hpp"
#include "aver/anim/AnimMontages.hpp"
#include "aver/anim/AnimSystem.hpp"
#include "aver/anim/MotionMatchSystem.hpp"
#include "aver/core/Hash.hpp"
#include "aver/formats/OcAnim.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/World.hpp"

#include <chrono>
#include <filesystem>
#include <map>
#include <thread>

using namespace aver;
using mt::check;
using mt::checkNear;

namespace {

std::map<u64, std::string> g_paths;
std::string g_dir;
const f32 kDt = 1.0f / 60.0f;

u64 hashOf(const char* s) { return fnv1a64(std::string_view(s)); }

u64 reg(const std::string& ref, const std::string& file) {
    const u64 id = hashOf(ref.c_str());
    g_paths[id] = g_dir + "/" + file;
    return id;
}

std::string resolvePath(u64 id, void*) {
    const auto it = g_paths.find(id);
    return it == g_paths.end() ? std::string{} : it->second;
}

// The locomotion set with its clips named like files; `rootVel` off leaves the trajectory to decide.
anim::MotionAsset locoAsset(bool rootVel) {
    mt::ClipTable scratch;
    anim::MotionAsset a = mt::locomotionAsset(scratch);
    a.skeleton = "rig.ocskel";
    a.bones = {{"foot_l", 0.3f, 0.3f}, {"foot_r", 0.3f, 0.3f}};
    for (anim::MotionClipEntry& e : a.clips) e.clip += ".ocanim";
    if (!rootVel) { a.rootVelocity = 0.0f; a.futurePos = 3.0f; }   // the wanted trajectory alone decides
    a.name = rootVel ? "Loco" : "LocoNoVel";
    return a;
}

scene::World* g_w = nullptr;
anim::AnimSystem* g_sys = nullptr;
std::vector<scene::Entity> g_movers;

// One engine frame in the host's order: the script moves its entities, then motion matching and the state
// machines tick, then the animation system.
void frame(f32 dt = kDt) {
    anim::motionMatchSystem().tick(*g_w, dt);
    anim::animGraphSystem().tick(*g_w, dt);
    g_sys->tick(*g_w, dt);
}

void waitReady() {
    frame();   // lets the systems notice a content epoch change and start whatever has to load
    for (int i = 0; i < 4000 && anim::motionMatchSystem().pendingBuilds() > 0; ++i) {
        frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    frame();
}

// A script-driven character: it walks along +X at `speed` and tells the matcher so.
void drive(scene::Entity e, f32 speed, int frames) {
    const u32 type = g_w->componentId("CMotionMatcher");
    for (int i = 0; i < frames; ++i) {
        Vec3 p = g_w->localTransform(e).position;
        p.x += speed * kDt;
        g_w->setLocalPosition(e, p);
        if (auto* c = g_w->component<anim::CMotionMatcher>(e, type)) c->desiredVelX = speed;
        frame();
    }
}

scene::Entity makeEntity(const char* name, u64 skeleton, u64 database) {
    const scene::Entity e = g_w->create(name);
    auto* sm = static_cast<scene::CSkeletalMesh*>(g_w->addComponent(e, scene::kComponentSkeletalMesh));
    if (sm) sm->skeleton = skeleton;
    if (database) anim::MotionMatchSystem::attach(*g_w, e, database);
    return e;
}

const anim::CMotionMatcher* mm(scene::Entity e) {
    return g_w->component<anim::CMotionMatcher>(e, g_w->componentId("CMotionMatcher"));
}

void setParam(anim::CAnimGraph& c, const char* name, f32 v) {
    const u64 h = hashOf(name);
    u32 slot = anim::kAnimGraphSlots;
    for (u32 i = 0; i < anim::kAnimGraphSlots; ++i)
        if (c.paramHash[i] == h) { slot = i; break; }
    if (slot == anim::kAnimGraphSlots)
        for (u32 i = 0; i < anim::kAnimGraphSlots; ++i)
            if (c.paramHash[i] == 0) { slot = i; break; }
    if (slot == anim::kAnimGraphSlots) return;
    c.paramHash[slot] = h;
    c.paramValue[slot] = v;
}

f32 footSwing(scene::Entity e) {
    const anim::Pose* p = g_sys->pose(e);
    return p && p->local.size() > 2 ? std::fabs(p->local[2].position.x) : 0.0f;
}

void retire(scene::Entity e) {
    g_w->destroy(e);
    g_w->flush();
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("MotionMatchSystemTest");

    std::error_code ec;
    g_dir = (std::filesystem::temp_directory_path(ec) / "aver-motionmatch-test").string();
    std::filesystem::remove_all(g_dir, ec);
    std::filesystem::create_directories(g_dir, ec);

    std::string why;
    const anim::MotionAsset withVel = locoAsset(true), noVel = locoAsset(false);
    const u64 kRigId = reg("rig.ocskel", "rig.ocskel");
    check(fmt::saveOcSkel(g_dir + "/rig.ocskel", mt::rig(), &why), "the skeleton writes " + why);
    mt::ClipTable made;
    mt::locomotionAsset(made);
    for (const char* n : {"idle", "walk", "run"}) {
        reg(std::string(n) + ".ocanim", std::string(n) + ".ocanim");
        check(fmt::saveOcAnim(g_dir + "/" + n + ".ocanim", *made.find(n), &why), std::string(n) + " writes " + why);
    }
    const u64 locoId = reg("Loco.ocmotion", "Loco.ocmotion"), noVelId = reg("LocoNoVel.ocmotion", "LocoNoVel.ocmotion");
    check(anim::saveMotionAsset(g_dir + "/Loco.ocmotion", withVel, &why), "Loco.ocmotion writes " + why);
    check(anim::saveMotionAsset(g_dir + "/LocoNoVel.ocmotion", noVel, &why), "LocoNoVel.ocmotion writes " + why);

    scene::World& w = scene::World::instance();
    anim::AnimSystem& sys = anim::animSystem();
    g_w = &w;
    g_sys = &sys;
    sys.clear();
    sys.setResolver(&resolvePath, nullptr);

    AVER_INFO("=== the component registers ===");
    anim::AnimGraphSystem::registerComponents(w);
    anim::AnimSystem::registerComponents(w);
    const u32 type = anim::MotionMatchSystem::registerComponents(w);
    check(type != 0 && w.componentId("CMotionMatcher") == type, "CMotionMatcher got a type id and answers to its name");
    check(w.componentVerified(type) && w.componentSize(type) == 136, "its field table covers the struct (136 bytes) with no gap");
    check(w.fieldId("CMotionMatcher.requireTag3") != 0 && w.fieldId("CMotionMatcher.excludeTag3") != 0 &&
          w.fieldId("CMotionMatcher.switches") != 0 && w.fieldId("CMotionMatcher.desiredVelX") != 0,
          "the last tag slots and the outputs are reachable by name");
    {
        const scene::FieldDesc* in = w.field(w.fieldId("CMotionMatcher.inputSource"));
        check(in && !in->readOnly && in->kind == scene::FieldKind::I32, "inputSource is a writable I32 field");
        check(w.fieldId("CMotionMatcher.reserved") == 0, "and the old read-only 'reserved' name is gone");
    }
    anim::animGraphSystem().install(sys, w);
    anim::motionMatchSystem().install(sys, w);
    anim::MotionMatchSystem& mms = anim::motionMatchSystem();
    check(sys.hasPoseSource() && sys.poseSource() != nullptr, "installing leaves a pose source in place");
    {
        const scene::Entity e = w.create("raw");
        auto* raw = static_cast<anim::CMotionMatcher*>(w.addComponent(e, type));
        check(raw && raw->playRate == 0.0f && raw->database == 0 && raw->flags == 0, "zero-filled storage is a valid, enabled component");
        anim::CMotionMatcher* c = anim::MotionMatchSystem::attach(w, e, 77);
        check(c && c->database == 77 && c->switches == 0, "attach assigns a fresh value");
        retire(e);
    }

    AVER_INFO("=== a standalone matcher: loading, then posing ===");
    const scene::Entity hero = makeEntity("hero", kRigId, locoId);
    frame();
    check(sys.pose(hero) != nullptr, "the entity is posed from the first frame (rest or matcher), even while the database loads");
    waitReady();
    check(mms.loadedCount() == 1 && mms.pendingBuilds() == 0, "the database loaded");
    check(mms.buildCount() == 1, "built once (no sidecar yet)");
    check(std::filesystem::exists(g_dir + "/Loco.ocmbake"), "and baked a sidecar beside the asset");
    for (int i = 0; i < 20; ++i) frame();
    check(mm(hero)->clip == hashOf("idle.ocanim"), "standing still plays idle");
    check(mm(hero)->row != 0xFFFFFFFFu && mm(hero)->switches >= 1, "outputs: row and switches are written back");

    AVER_INFO("=== the pick follows the measured motion ===");
    drive(hero, 400.0f, 100);
    check(mm(hero)->clip == hashOf("run.ocanim"), "moving at 400 cm/s plays run");
    check(std::isfinite(mm(hero)->cost) && mm(hero)->clipTime >= 0.0f, "cost and clip time are written back");
    drive(hero, 150.0f, 100);
    check(mm(hero)->clip == hashOf("walk.ocanim"), "moving at 150 cm/s plays walk");
    drive(hero, 0.0f, 130);
    check(mm(hero)->clip == hashOf("idle.ocanim"), "stopping plays idle again");
    {
        const anim::Pose* p = sys.pose(hero);
        check(p && (p->local[0].position - mt::rig().bones[0].translation).size() < 1e-3f, "the output pose has its planar root locked");
    }

    AVER_INFO("=== the trajectory, measured and predicted ===");
    {
        drive(hero, 200.0f, 90);
        anim::MotionMatchSystem::Debug d;
        check(mms.debug(hero, d), "debug view is available");
        check(d.predicted.size() == 4 && d.matched.size() == 4 && d.history.size() > 10, "points: 4 predicted, 4 matched, a history");
        const f32 closed = anim::springDisplacement(200.0f, 200.0f, 0.2f, 0.2f);
        checkNear(d.predicted[0].x - d.origin.x, closed, 1.5f, "predicted position at 0.2 s equals the closed form for the measured speed");
        checkNear(d.predicted[3].x - d.origin.x, 200.0f, 3.0f, "and at 1.0 s");
        checkNear(d.origin.x, w.localTransform(hero).position.x, 1e-3f, "the origin is the entity");
        check(d.groupNames.size() == d.groupCurrent.size() && !d.groupNames.empty(), "group names line up with group costs");
        check(!d.clip.empty() && d.switches >= 1, "clip name and switch count are reported");
        check(!mms.debug(scene::kInvalidEntity, d), "no debug data for a stale handle");
    }

    AVER_INFO("=== the sidecar and the content epoch ===");
    {
        mms.clear();
        frame();
        waitReady();
        check(mms.loadedCount() == 1 && mms.buildCount() == 0, "after a clear the database comes from the sidecar (no build)");
        drive(hero, 150.0f, 30);
        check(mm(hero)->switches >= 1, "and drives the entity as before");

        // A clip changes on disk: AnimSystem::clear() is what a project reload calls; the key no longer matches.
        mt::ClipTable changed;
        changed.add("walk", mt::walkClip(1.0f, 170.0f, true, 10.0f));
        check(fmt::saveOcAnim(g_dir + "/walk.ocanim", *changed.find("walk"), &why), "the walk clip is rewritten " + why);
        sys.clear();
        waitReady();
        check(mms.buildCount() == 1, "a changed clip byte rebuilds instead of trusting the sidecar");
        // restore
        check(fmt::saveOcAnim(g_dir + "/walk.ocanim", *made.find("walk"), &why), "walk restored " + why);
        sys.clear();
        waitReady();
        check(mms.loadedCount() == 1, "and loads again");
    }

    AVER_INFO("=== clear() during a load; an entity that dies mid-load ===");
    {
        sys.clear();
        frame();                 // starts a load
        mms.clear();             // joins the worker and drops
        check(mms.pendingBuilds() == 0 && mms.loadedCount() == 0, "clear() joins a running load and drops everything");
        frame();
        sys.clear();             // content epoch moves while it loads
        frame();
        waitReady();
        check(mms.pendingBuilds() == 0, "an AnimSystem::clear() during a load is survived");

        mms.clear();
        frame();
        retire(hero);
        for (int i = 0; i < 30; ++i) frame();
        waitReady();
        check(mms.pendingBuilds() == 0 && mms.query(hero) == nullptr, "an entity destroyed mid-load leaves nothing behind");
    }

    AVER_INFO("=== a crowd: searches are staggered ===");
    {
        std::vector<scene::Entity> crowd;
        for (int i = 0; i < 10; ++i) crowd.push_back(makeEntity("npc", kRigId, locoId));
        mms.setMaxSearchesPerTick(2);
        for (int i = 0; i < 4; ++i) frame();
        waitReady();
        auto driveAll = [&](int frames, bool record, u32& worst, u32& total) {
            for (int f = 0; f < frames; ++f) {
                for (const scene::Entity e : crowd) {
                    Vec3 p = w.localTransform(e).position;
                    p.x += 150.0f * kDt;
                    w.setLocalPosition(e, p);
                    w.component<anim::CMotionMatcher>(e, type)->desiredVelX = 150.0f;
                }
                frame();
                if (record) { worst = std::max(worst, mms.searchesLastTick()); total += mms.searchesLastTick(); }
            }
        };
        u32 worst = 0, total = 0;
        driveAll(40, false, worst, total);   // start-up: every matcher starts, which is never held back
        driveAll(120, true, worst, total);
        check(worst <= 2, "never more than maxSearchesPerTick interval searches in a tick (worst " + std::to_string(worst) + ")");
        check(total >= 150, "yet every entity still searches about every interval (" + std::to_string(total) + " of ~200 in 2 s)");
        bool all = true;
        for (const scene::Entity e : crowd) all &= mm(e)->clip == hashOf("walk.ocanim");
        check(all, "and the crowd walks");
        mms.setMaxSearchesPerTick(4);
        for (const scene::Entity e : crowd) retire(e);
    }

    AVER_INFO("=== precedence: a state machine owns its entity ===");
    {
        anim::AnimStateMachineAsset a;
        a.name = "plain";
        anim::AsmMachine m;
        m.name = "Root";
        anim::AsmState s;
        s.name = "Walk";
        s.asset = "walk.ocanim";
        m.states = {s};
        a.machines = {m};
        const u64 plainId = reg("plain.ocasm", "plain.ocasm");
        check(anim::saveStateMachine(g_dir + "/plain.ocasm", a, &why), "plain machine writes " + why);
        const scene::Entity e = makeEntity("both", kRigId, locoId);
        anim::AnimGraphSystem::attach(w, e, plainId);
        for (int i = 0; i < 60; ++i) frame();
        check(anim::animGraphSystem().instance(e) != nullptr, "the machine runs");
        check(mm(e)->clip == 0 && mm(e)->switches == 0, "and the standalone matcher never touches the entity");
        retire(e);
    }

    AVER_INFO("=== a motionmatch state inside a state machine ===");
    {
        anim::AnimStateMachineAsset a;
        a.name = "loco";
        a.params = {{"move", anim::AsmParamType::Bool, 0.0f}};
        anim::AsmMachine m;
        m.name = "Root";
        anim::AsmState idle, move;
        idle.name = "Idle";
        idle.asset = "idle.ocanim";
        move.name = "Move";
        move.kind = anim::AsmStateKind::MotionMatch;
        move.asset = "Loco.ocmotion";
        move.tagsRequired = {"run"};   // the walking character is held to the run clip: proof the state's tags apply
        m.states = {idle, move};
        anim::AsmTransition in, out;
        in.from = 0; in.to = 1; in.blendTime = 0.2f;
        in.conditions = {{"move", anim::AsmOp::IsTrue, 0.0f}};
        out.from = 1; out.to = 0; out.blendTime = 0.2f;
        out.conditions = {{"move", anim::AsmOp::IsFalse, 0.0f}};
        m.transitions = {in, out};
        a.machines = {m};
        check(a.valid(&why), "the machine is valid " + why);
        const u64 locoMachine = reg("loco.ocasm", "loco.ocasm");
        check(anim::saveStateMachine(g_dir + "/loco.ocasm", a, &why), "it writes " + why);

        const scene::Entity e = makeEntity("walker", kRigId, 0);
        anim::CAnimGraph* g = anim::AnimGraphSystem::attach(w, e, locoMachine);
        mms.clear();
        for (int i = 0; i < 30; ++i) frame();
        check(g && anim::animGraphSystem().instance(e) != nullptr, "the machine runs");
        check(mm(e) != nullptr, "a machine with a motionmatch state gets a CMotionMatcher for scripts to drive");
        f32 idleSwing = 0;
        for (int i = 0; i < 30; ++i) { frame(); idleSwing = std::max(idleSwing, footSwing(e)); }
        checkNear(idleSwing, 0.0f, 1e-3f, "Idle plays the idle clip (the feet do not swing)");

        g = w.component<anim::CAnimGraph>(e, w.componentId("CAnimGraph"));
        setParam(*g, "move", 1.0f);
        for (int i = 0; i < 20; ++i) frame();
        waitReady();
        drive(e, 150.0f, 90);
        g = w.component<anim::CAnimGraph>(e, w.componentId("CAnimGraph"));
        check(g->activeState == hashOf("Move"), "the transition took the machine to Move");
        f32 swing = 0;
        for (int i = 0; i < 30; ++i) { drive(e, 150.0f, 1); swing = std::max(swing, footSwing(e)); }
        check(swing > 15.0f, "the pose is the matcher's, held to run by the state's tags (foot swing " + std::to_string(swing) + ")");
        check(mms.query(e) != nullptr, "the entity's query is published for the machine");

        setParam(*g, "move", 0.0f);
        for (int i = 0; i < 40; ++i) frame();
        g = w.component<anim::CAnimGraph>(e, w.componentId("CAnimGraph"));
        check(g->activeState == hashOf("Idle"), "and it comes back to Idle");
        retire(e);
    }

    AVER_INFO("=== root motion: simulation, Apply, Accumulate, External ===");
    {
        // No root-velocity term: the desired trajectory alone picks the clip, so a standing character that
        // wants to walk starts walking, and the animation (not a script) is what moves it.
        const auto settle = [&] { mms.clear(); for (int i = 0; i < 5; ++i) frame(); waitReady(); };
        const scene::Entity sim = makeEntity("sim", kRigId, noVelId);
        const scene::Entity app = makeEntity("apply", kRigId, noVelId);
        const scene::Entity acc = makeEntity("accum", kRigId, noVelId);
        const scene::Entity ext = makeEntity("external", kRigId, noVelId);
        anim::AnimSystem::attach(w, app, anim::kRootMotionApply);
        anim::AnimSystem::attach(w, acc, anim::kRootMotionAccumulate);
        anim::CRootMotion* er = anim::AnimSystem::attach(w, ext, anim::kRootMotionApply);
        if (er) er->flags = anim::kRootMotionExternal;
        settle();
        const u32 rtype = w.componentId("CRootMotion");
        auto want = [&](scene::Entity e) { w.component<anim::CMotionMatcher>(e, type)->desiredVelX = 150.0f; };
        for (const scene::Entity e : {sim, app, acc, ext}) want(e);
        f32 maxAccum = 0.0f;
        for (int i = 0; i < 20; ++i) {
            frame();
            maxAccum = std::max(maxAccum, w.component<anim::CRootMotion>(ext, rtype)->accumX);
        }
        check(std::fabs(w.localTransform(ext).position.x) < 1e-3f && maxAccum > 1.0f,
              "External: the entity stays put and the delta waits in the accumulator");
        for (int i = 0; i < 160; ++i) frame();

        check(std::fabs(w.localTransform(sim).position.x) < 1e-3f, "Simulation (no CRootMotion): the entity is not moved by the animation");
        const anim::Pose* sp = sys.pose(sim);
        check(sp && (sp->local[0].position - mt::rig().bones[0].translation).size() < 1e-3f, "and the pose's root is locked either way");
        check(mm(sim)->clip != 0 && mm(sim)->clip != hashOf("idle.ocanim"), "(the walking pose still plays)");
        const Vec3 pa = w.localTransform(app).position;
        check(pa.x > 120.0f && std::fabs(pa.y) < 1.0f, "Apply: the entity moves along its heading by the clip's travel (x " + std::to_string(pa.x) + ")");
        const anim::CRootMotion* rm = w.component<anim::CRootMotion>(acc, rtype);
        check(std::fabs(w.localTransform(acc).position.x) < 1e-3f && rm && rm->accumX > 100.0f, "Accumulate: the entity stays and the accumulator grows");
        check(mm(app)->clip == hashOf("walk.ocanim"), "(Apply walks at the pace it asked for)");
        for (const scene::Entity e : {sim, app, acc, ext}) retire(e);
    }

    AVER_INFO("=== a montage over a matched entity ===");
    {
        fmt::OcAnimation wave;
        wave.duration = 0.5f;
        fmt::OcTrack t;
        t.boneIndex = 1;
        t.channels = fmt::kOcChannelTranslation;
        t.times = {0.0f, 0.5f};
        t.values = {0, 0, 150, 0, 0, 150};
        wave.tracks.push_back(t);
        reg("wave.ocanim", "wave.ocanim");
        check(fmt::saveOcAnim(g_dir + "/wave.ocanim", wave, &why), "wave clip writes " + why);
        anim::MontageAsset ma;
        ma.name = "wave";
        ma.blendIn = 0.0f;
        ma.blendOut = 0.1f;
        ma.inCurve = anim::BlendCurve::Linear;
        ma.outCurve = anim::BlendCurve::Linear;
        ma.segments = {{"wave.ocanim", 0.0f, -1.0f, 1.0f}};
        ma.sections = {{"Default", 0.0f, ""}};
        const u64 montageId = reg("wave.ocmontage", "wave.ocmontage");
        check(anim::saveMontage(g_dir + "/wave.ocmontage", ma, &why), "montage writes " + why);

        const scene::Entity e = makeEntity("waver", kRigId, locoId);
        mms.clear();
        waitReady();
        drive(e, 150.0f, 60);
        const f32 hipsBefore = sys.pose(e)->local[1].position.z;
        const f32 t0 = mm(e)->clipTime;
        check(sys.montages().play(sys, w, e, montageId, 0, 0, 1.0f, -1.0f), "the montage plays");
        drive(e, 150.0f, 6);
        check(sys.pose(e)->local[1].position.z > hipsBefore + 40.0f, "the montage overrides the matched pose");
        check(mm(e)->clipTime != t0, "while the matcher keeps ticking underneath");
        drive(e, 150.0f, 60);
        checkNear(sys.pose(e)->local[1].position.z, hipsBefore, 1.0f, "after the blend-out the matcher's pose is back");
        sys.montages().stopAll();
        retire(e);
    }

    AVER_INFO("=== inputSource: the matcher follows how the entity moves ===");
    {
        mms.clear();
        const scene::Entity follower = makeEntity("follower", kRigId, locoId);
        const scene::Entity script = makeEntity("script", kRigId, locoId);
        const scene::Entity zeroed = g_w->create("zeroed");   // a raw zero-filled matcher, only the database set
        if (auto* sm = static_cast<scene::CSkeletalMesh*>(g_w->addComponent(zeroed, scene::kComponentSkeletalMesh))) sm->skeleton = kRigId;
        auto* zc = static_cast<anim::CMotionMatcher*>(g_w->addComponent(zeroed, type));
        if (zc) zc->database = locoId;
        waitReady();
        auto* fc = w.component<anim::CMotionMatcher>(follower, type);
        auto* sc = w.component<anim::CMotionMatcher>(script, type);
        check(fc && sc && zc && fc->inputSource == anim::kMotionInputScript && zc->inputSource == 0,
              "a fresh and a zero-filled matcher both start on the Script input");
        if (fc) fc->inputSource = anim::kMotionInputEntityVelocity;
        // The follower and the script entity are both moved at 200 cm/s by something other than a script that
        // talks to the matcher: nobody writes desiredVel here.
        for (int i = 0; i < 90; ++i) {
            for (const scene::Entity e : {follower, script}) {
                Vec3 p = w.localTransform(e).position;
                p.x += 200.0f * kDt;
                w.setLocalPosition(e, p);
            }
            frame();
        }
        check(fc && std::fabs(fc->desiredVelX - 200.0f) <= 20.0f,
              "Entity velocity: desiredVelX settles within 10% of the 200 cm/s the entity moves at (" +
                  std::to_string(fc ? fc->desiredVelX : 0.0f) + ")");
        check(fc && std::fabs(fc->desiredVelY) <= 20.0f, "and desiredVelY stays near zero");
        check(fc && fc->clip != 0 && fc->clip != hashOf("idle.ocanim"),
              "and the matcher picks a moving clip with no script involved");
        check(sc && sc->desiredVelX == 0.0f && sc->desiredVelY == 0.0f && sc->desiredYaw == 0.0f,
              "Script input never writes the desired fields back, however the entity moves");
        check(zc && zc->desiredVelX == 0.0f && zc->inputSource == 0 && zc->clip == hashOf("idle.ocanim"),
              "a zero-filled matcher on a standing entity still idles, exactly as before");
        for (const scene::Entity e : {follower, script, zeroed}) retire(e);
    }

    sys.montages().stopAll();
    mms.clear();
    anim::animGraphSystem().clear();
    std::filesystem::remove_all(g_dir, ec);

    AVER_INFO("{} failure(s)", mt::g_failures);
    return mt::g_failures == 0 ? 0 : 1;
}
