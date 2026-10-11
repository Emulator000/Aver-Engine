// The motion matcher end to end on a synthetic database (idle / walk 150 / run 400, root travel carried
// by unflagged clips): what it picks, how often it searches, when it refuses to switch, the root delta it
// reports, the pose it outputs, and the guards (skeleton mismatch, pause, patent rule 4).
#include "MotionTestKit.hpp"

#include "aver/anim/AnimStateMachine.hpp"
#include "aver/anim/MotionMatcher.hpp"
#include "aver/anim/Trajectory.hpp"
#include "aver/core/Hash.hpp"

#include <functional>
#include <memory>

using namespace aver;
using mt::check;
using mt::checkNear;

namespace {

constexpr f32 kDt = 1.0f / 60.0f;

struct Fix {
    mt::ClipTable t;
    anim::MotionAsset a;
    anim::PoseDatabase db;
    fmt::OcSkeleton skel = mt::rig();
    anim::MotionMatcher mm;
    bool ok = false;

    explicit Fix(const std::function<void(mt::ClipTable&, anim::MotionAsset&)>& setup = {}) {
        a = mt::locomotionAsset(t);
        // Trajectory dominates the pick; the pose part is only a tie-breaker.
        a.bones = {{"foot_l", 0.3f, 0.3f}, {"foot_r", 0.3f, 0.3f}};
        if (setup) setup(t, a);
        std::string why;
        ok = anim::buildPoseDatabase(a, skel, t.fn(), db, nullptr, &why) && mm.bind(&db, &skel, t.fn());
        if (!ok) AVER_ERROR("  fixture failed: {}", why);
    }
    u64 hash(const char* clip) const { return fnv1a64(std::string_view(clip)); }
};

// What a character walking steadily along +X at `speed` and wanting to keep doing so asks for.
anim::MotionQuery steady(const anim::PoseDatabase& db, f32 speed) {
    anim::TrajectoryPredictor tp;
    for (int i = 0; i <= 60; ++i) tp.observe(Vec3{speed * kDt * static_cast<f32>(i), 0, 0}, 0.0f, i ? kDt : 0.0f, 1.0f);
    tp.setIntent(speed, 0.0f, false, 0.0f, 5.0f);
    anim::MotionQuery q;
    const anim::MotionAsset& d = db.defaults();
    tp.fill({d.spring.velocityHalfLife, d.spring.turnHalfLife}, db.futureTimes(), db.pastTimes(), q);
    return q;
}

void run(anim::MotionMatcher& mm, int ticks, const anim::MotionQuery& q, const anim::MotionParams& p = {}, f32 dt = kDt) {
    for (int i = 0; i < ticks; ++i) mm.tick(dt, q, p);
}

f32 maxPosDiff(const anim::Pose& a, const anim::Pose& b, usize from = 1) {
    f32 m = 0;
    for (usize i = from; i < a.local.size() && i < b.local.size(); ++i)
        m = std::max(m, (a.local[i].position - b.local[i].position).size());
    return m;
}

anim::Pose clipPose(const fmt::OcSkeleton& skel, const fmt::OcAnimation& c, f32 t) {
    anim::Pose p;
    anim::restPose(skel, p);
    anim::sampleAnimation(c, t, p);
    return p;
}

} // namespace

int main() {
    AVER_INFO("MotionMatcherTest");
    const anim::MotionParams kDefault;

    AVER_INFO("picks by desired velocity");
    {
        struct Case { f32 speed; const char* clip; };
        for (const Case c : {Case{0.0f, "idle"}, Case{150.0f, "walk"}, Case{400.0f, "run"}}) {
            Fix f;
            check(f.ok, "fixture builds");
            run(f.mm, 1, steady(f.db, c.speed));
            check(f.mm.clipHash() == f.hash(c.clip), std::string("desired ") + std::to_string(static_cast<int>(c.speed)) + " cm/s plays " + c.clip);
            check(f.mm.info().switched && f.mm.info().searched && f.mm.info().switches == 1, "the first tick searched and started it");
            check(f.mm.info().row != anim::kNoRow && f.db.meta(f.mm.info().row).range == f.mm.info().range, "info names the playing row");
        }
    }

    AVER_INFO("search interval, forced search, acceptCost");
    {
        auto countSearches = [](f32 interval) {
            Fix f;
            anim::MotionParams p;
            p.searchInterval = interval;
            const anim::MotionQuery q = steady(f.db, 150.0f);
            int n = 0;
            for (int i = 0; i < 100; ++i) { f.mm.tick(0.01f, q, p); n += f.mm.info().searched; }
            return n;
        };
        const int a = countSearches(0.1f), b = countSearches(0.25f);
        check(a >= 9 && a <= 11, "interval 0.1 over 1 s searches about ten times (" + std::to_string(a) + ")");
        check(b >= 3 && b <= 5, "interval 0.25 searches about four times (" + std::to_string(b) + ")");

        Fix f;
        anim::MotionParams slow;
        slow.searchInterval = 1e6f;
        run(f.mm, 1, steady(f.db, 150.0f), slow);
        run(f.mm, 60, steady(f.db, 400.0f), slow);
        check(f.mm.clipHash() == f.hash("walk") && f.mm.info().switches == 1, "no periodic search, no switch");
        anim::MotionParams force = slow;
        force.forceSearch = true;
        run(f.mm, 1, steady(f.db, 400.0f), force);
        check(f.mm.clipHash() == f.hash("run") && f.mm.info().switches == 2, "forceSearch searches now and switches");

        Fix strict([](mt::ClipTable&, anim::MotionAsset& as) { as.search.acceptCost = 1e-3f; });
        run(strict.mm, 1, steady(strict.db, 150.0f));
        run(strict.mm, 30, steady(strict.db, 400.0f), force);
        check(strict.mm.clipHash() == strict.hash("walk") && strict.mm.info().switches == 1,
              "acceptCost: a best hit above it never displaces a valid pose");
        Fix loose;
        run(loose.mm, 1, steady(loose.db, 150.0f));
        run(loose.mm, 30, steady(loose.db, 400.0f), force);
        check(loose.mm.clipHash() == loose.hash("run"), "without acceptCost the same hit is taken");
    }

    AVER_INFO("no thrash");
    {
        Fix f([](mt::ClipTable& t, anim::MotionAsset& as) {
            t.add("walkA", mt::walkClip(1.0f, 150.0f, true, 10.0f));
            t.add("walkB", mt::walkClip(1.0f, 152.0f, true, 10.0f));
            as.tags = {"walk"};
            as.clips = {mt::entry("walkA", {"walk"}), mt::entry("walkB", {"walk"})};
        });
        check(f.ok, "fixture builds");
        const anim::MotionQuery q = steady(f.db, 151.0f);
        run(f.mm, 1, q);
        run(f.mm, 240, q);
        check(f.mm.info().switches == 1, "switchMargin: two near-equal clips never trade places (" + std::to_string(f.mm.info().switches) + ")");

        Fix g([](mt::ClipTable&, anim::MotionAsset& as) { as.search.switchMargin = 0.0f; });
        const anim::MotionQuery w = steady(g.db, 150.0f);
        run(g.mm, 1, w);
        run(g.mm, 240, w);
        check(g.mm.info().switches == 1, "continuityWindow: a hit near the playhead of the same range is the current motion");
    }

    AVER_INFO("end of a non-loop range");
    {
        Fix f([](mt::ClipTable& t, anim::MotionAsset& as) {
            t.add("once", mt::walkClip(2.0f, 150.0f, false, 10.0f));
            as.clips = {mt::entry("idle", {"idle"}), mt::entry("once", {"walk"})};
        });
        check(f.ok, "fixture builds");
        anim::MotionParams p;
        p.searchInterval = 1e6f;   // only the end of the clip can trigger a search
        const anim::MotionQuery q = steady(f.db, 150.0f);
        run(f.mm, 1, q, p);
        check(f.mm.clipHash() == f.hash("once"), "starts the non-loop walk");
        const f32 start = f.mm.info().clipTime;
        bool reached = false;
        u32 atEnd = 0;
        for (int i = 0; i < 200 && f.mm.info().switches < 2; ++i) {
            f.mm.tick(kDt, q, p);
            reached |= f.mm.info().clipTime > 1.99f;
            if (f.mm.info().switches >= 2) atEnd = static_cast<u32>(i);
        }
        check(f.mm.info().switches == 2, "running off the end forces a search and a switch (started at " + std::to_string(start) + ")");
        check(f.mm.info().clipTime < 1.2f && f.mm.info().row != anim::kNoRow && atEnd > 0, "to a continuation, with a valid playing row again");
        (void)reached;
    }

    AVER_INFO("tag filters");
    {
        Fix f;
        anim::MotionParams force;
        force.forceSearch = true;
        anim::MotionQuery q = steady(f.db, 150.0f);
        run(f.mm, 1, q);
        check(f.mm.clipHash() == f.hash("walk"), "starts on walk");
        q.require = f.db.tagBit(f.hash("run"));
        run(f.mm, 1, q, force);
        check(f.mm.clipHash() == f.hash("run"), "require constrains the pick (walk is the better fit, run is the only allowed)");
        q.require = 0;
        q.exclude = f.db.tagBit(f.hash("run"));
        run(f.mm, 1, q, force);
        check(f.mm.clipHash() == f.hash("walk"), "a playing row that breaks the exclude is left at once");
        q.exclude = f.db.tagBit(f.hash("walk")) | f.db.tagBit(f.hash("run"));
        run(f.mm, 1, q, force);
        check(f.mm.clipHash() == f.hash("idle"), "exclude removes candidates");
        q.exclude = 0xFFFFFFFFu;
        const u32 before = f.mm.info().switches;
        run(f.mm, 1, q, force);
        check(f.mm.info().switches == before, "when nothing passes the filter the matcher keeps playing");
    }

    AVER_INFO("clock, root delta");
    {
        Fix f;
        f.mm.setRootMotion(true, 0);
        const anim::MotionQuery q = steady(f.db, 150.0f);
        run(f.mm, 1, q);
        f.mm.consumeRootDelta();
        run(f.mm, 60, q);
        bool inRange = true;
        {
            const anim::MotionRange& r = f.db.ranges()[f.mm.info().range];
            inRange = f.mm.info().clipTime >= r.start && f.mm.info().clipTime < r.end;
        }
        const anim::RootDelta d = f.mm.consumeRootDelta();
        checkNear(d.move.x, 150.0f, 0.5f, "a second of walking reports the clip's travel although the clip lacks kOcAnimRootMotion");
        checkNear(d.move.y, 0.0f, 0.05f, "and nothing sideways");
        checkNear(d.yaw, 0.0f, 1e-4f, "or turning");
        check(inRange, "the loop clock wraps inside its range");
        check(!f.mm.consumeRootDelta().any(), "consume resets");

        anim::MotionParams fast;
        fast.playRate = 2.0f;
        run(f.mm, 60, q, fast);
        checkNear(f.mm.consumeRootDelta().move.x, 300.0f, 1.0f, "playRate 2 doubles the travel");

        Fix off;
        run(off.mm, 1, steady(off.db, 150.0f));
        run(off.mm, 60, steady(off.db, 150.0f));
        check(!off.mm.consumeRootDelta().any(), "root motion off reports nothing");

        Fix tall;
        tall.mm.setRootMotion(true, anim::kRootAxisX);   // X only
        run(tall.mm, 1, steady(tall.db, 150.0f));
        tall.mm.consumeRootDelta();
        run(tall.mm, 30, steady(tall.db, 150.0f));
        check(tall.mm.consumeRootDelta().move.x > 70.0f, "the axes mask is honoured");
        {   // looping through a long run never loses a lap
            Fix lap;
            lap.mm.setRootMotion(true, 0);
            run(lap.mm, 1, steady(lap.db, 400.0f));
            lap.mm.consumeRootDelta();
            run(lap.mm, 60 * 5, steady(lap.db, 400.0f));
            checkNear(lap.mm.consumeRootDelta().move.x, 400.0f * 5.0f, 3.0f, "five seconds of running = five seconds of travel");
        }
    }

    AVER_INFO("the output pose");
    {
        Fix f;
        const anim::MotionQuery q = steady(f.db, 150.0f);
        run(f.mm, 1, q);
        f32 swing = 0.0f;
        bool locked = true;
        for (int i = 0; i < 15; ++i) {
            run(f.mm, 1, q);
            swing = std::max(swing, std::fabs(f.mm.pose().local[2].position.x));
            locked &= (f.mm.pose().local[0].position - f.skel.bones[0].translation).size() < 1e-4f;
        }
        const anim::Pose& p = f.mm.pose();
        check(p.matches(f.skel), "pose matches the skeleton");
        check(locked, "the planar root is locked to the rest value (the mesh stays on the entity)");
        check(swing > 3.0f, "other bones follow the clip (foot swing)");
        anim::Pose out;
        f.mm.evaluate(f.skel, out);
        check(maxPosDiff(out, p, 0) == 0.0f, "evaluate copies pose()");
    }

    AVER_INFO("continuity across a switch");
    {
        // The search matches foot poses, so with the stock clips a hard cut only jumps by the row spacing.
        // Run feet 6 cm forward of walk's (the feet swing in antiphase) leave every run frame >= 6 cm off.
        Fix f([](mt::ClipTable& t, anim::MotionAsset&) {
            fmt::OcAnimation& rc = t.all()["run"];
            for (usize k = 1; k < rc.tracks.size(); ++k)
                for (usize i = 0; i < rc.tracks[k].values.size(); i += 3) rc.tracks[k].values[i] += 6.0f;
        });
        run(f.mm, 1, steady(f.db, 150.0f));
        run(f.mm, 45, steady(f.db, 150.0f));
        const anim::Pose before = f.mm.pose();
        anim::MotionParams force;
        force.forceSearch = true;
        f.mm.tick(kDt, steady(f.db, 400.0f), force);
        check(f.mm.info().switched && f.mm.clipHash() == f.hash("run"), "switches to run");
        const anim::Pose after = f.mm.pose();
        const anim::Pose cut = clipPose(f.skel, *f.t.find("run"), f.mm.info().clipTime);
        const f32 inertial = maxPosDiff(before, after), hard = maxPosDiff(before, cut);
        AVER_INFO("  max bone step: inertialized {:.3f} cm, hard cut would be {:.3f} cm", inertial, hard);
        check(hard > 1.5f && inertial < hard * 0.35f, "the pose is continuous across the switch (well below a hard cut)");

        run(f.mm, 90, steady(f.db, 400.0f));
        const anim::Pose settled = f.mm.pose();
        const anim::Pose target = clipPose(f.skel, *f.t.find("run"), f.mm.info().clipTime);
        check(maxPosDiff(settled, target) < 1e-3f && f.mm.info().switches == 2, "and converges onto the clip pose");
    }

    AVER_INFO("unbound, paused, mismatched");
    {
        anim::MotionMatcher none;
        none.tick(kDt, anim::MotionQuery{}, kDefault);
        anim::Pose rest;
        none.evaluate(mt::rig(), rest);
        check(!none.bound() && rest.matches(mt::rig()) && !none.consumeRootDelta().any(), "an unbound matcher is inert and evaluates the rest pose");
        check(none.clipHash() == 0, "no clip hash");

        Fix f;
        run(f.mm, 5, steady(f.db, 150.0f));
        const f32 t0 = f.mm.info().clipTime;
        anim::MotionParams paused;
        paused.paused = true;
        run(f.mm, 30, steady(f.db, 150.0f), paused);
        checkNear(f.mm.info().clipTime, t0, 1e-6f, "paused: the clock holds");
        check(!f.mm.info().searched, "and nothing searches");

        fmt::OcSkeleton other = mt::rig();
        other.bones[3].name = "foot_x";
        anim::MotionMatcher m2;
        check(!m2.bind(&f.db, &other, f.t.fn()) && !m2.bound(), "a skeleton whose bone names disagree refuses to bind");
        fmt::OcSkeleton fewer = mt::rig();
        fewer.bones.pop_back();
        check(!m2.bind(&f.db, &fewer, f.t.fn()), "so does one with another bone count");
        check(!m2.bind(nullptr, &f.skel, f.t.fn()) && !m2.bind(&f.db, nullptr, f.t.fn()), "and null inputs");
        check(m2.bind(&f.db, &f.skel, f.t.fn()) && m2.bound(), "the real skeleton binds");
        m2.unbind();
        check(!m2.bound(), "unbind");

        anim::MotionMatcher noQuery;
        noQuery.bind(&f.db, &f.skel, f.t.fn());
        noQuery.tick(kDt, anim::MotionQuery{}, kDefault);   // q.valid is false
        check(noQuery.info().range == anim::kNoRow && !noQuery.info().searched, "without a valid query nothing starts");
        anim::Pose r2;
        noQuery.evaluate(f.skel, r2);
        check(r2.matches(f.skel), "and the pose is the rest pose");
    }

    AVER_INFO("patent rule 4: the query never reads the output pose");
    {
        Fix clean, scribbled;
        const anim::MotionQuery walk = steady(clean.db, 150.0f), runQ = steady(clean.db, 400.0f);
        bool same = true;
        for (int i = 0; i < 240; ++i) {
            const anim::MotionQuery& q = i < 120 ? walk : runQ;
            clean.mm.tick(kDt, q, kDefault);
            scribbled.mm.tick(kDt, q, kDefault);
            // What a control rig, IK or ragdoll would do downstream: edit the pose. Even writing into the
            // matcher's own storage must not change what it searches for.
            anim::Pose copy;
            scribbled.mm.evaluate(scribbled.skel, copy);
            for (Transform& x : copy.local) x.position += Vec3{9, 9, 9};
            anim::Pose& inner = const_cast<anim::Pose&>(scribbled.mm.pose());
            for (Transform& x : inner.local) x.position += Vec3{25, -13, 7};
            same &= clean.mm.info().row == scribbled.mm.info().row && clean.mm.info().switches == scribbled.mm.info().switches &&
                    clean.mm.info().cost == scribbled.mm.info().cost && clean.mm.info().clipTime == scribbled.mm.info().clipTime;
        }
        check(same, "rows, costs, clip times and switch counts are identical with and without a modified pose");
        check(clean.mm.info().switches >= 2, "(and the run included a real switch)");
    }

    AVER_INFO("a motionmatch state in a state machine");
    {
        Fix f;
        anim::AnimStateMachineAsset a;
        a.params = {{"go", anim::AsmParamType::Bool, 0.0f}};
        anim::AsmMachine m;
        anim::AsmState idle, move;
        idle.name = "Idle";
        idle.asset = "idle";
        move.name = "Move";
        move.kind = anim::AsmStateKind::MotionMatch;
        move.asset = "loco";
        move.tagsRequired = {"run"};
        m.states = {idle, move};
        anim::AsmTransition t;
        t.from = 0;
        t.to = 1;
        t.blendTime = 0.0f;
        t.conditions = {{"go", anim::AsmOp::IsTrue, 0.0f}};
        m.transitions = {t};
        a.machines = {m};
        std::string why;
        check(a.valid(&why), "the machine is valid " + why);

        anim::AnimStateMachine sm;
        sm.bind(&a, [&](const std::string& n) { return f.t.find(n); }, nullptr);
        sm.setMotionResolver([&](const std::string& ref) { return ref == "loco" ? &f.db : nullptr; });
        const anim::MotionQuery q = steady(f.db, 150.0f);
        sm.setMotionContext(&f.skel, &q);
        anim::RootSpec spec;
        spec.bone = 0;
        spec.rest = Transform{f.skel.bones[0].translation, f.skel.bones[0].rotation, f.skel.bones[0].scale};
        sm.setRootMotion(&spec);
        for (int i = 0; i < 10; ++i) sm.tick(kDt);
        check(sm.activeState() == "Idle", "stays in Idle until the parameter is set");
        sm.setBool("go", true);
        for (int i = 0; i < 3; ++i) sm.tick(kDt);
        check(sm.activeState() == "Move", "the transition enters the motionmatch state");
        check(sm.normalizedTime() == 0.0f, "a motionmatch leaf reads normalised time 0 (it never ends)");
        sm.consumeRootDelta();
        f32 swing = 0;
        anim::Pose p;
        for (int i = 0; i < 60; ++i) {
            sm.tick(kDt);
            sm.evaluate(f.skel, p);
            swing = std::max(swing, std::fabs(p.local[2].position.x));
        }
        check(swing > 15.0f, "evaluate gives the matcher's pose, held to run by the state's required tag (swing " + std::to_string(swing) + ")");
        checkNear(sm.consumeRootDelta().move.x, 400.0f, 4.0f, "the layer reports the matched clip's root travel through the machine");
        check((p.local[0].position - f.skel.bones[0].translation).size() < 1e-3f, "and the root is locked in the pose");

        sm.setMotionContext(&f.skel, nullptr);
        for (int i = 0; i < 10; ++i) sm.tick(kDt);
        sm.evaluate(f.skel, p);
        check(p.matches(f.skel) && !sm.consumeRootDelta().any(), "without a query the layer idles on its last pose and reports nothing");

        anim::AnimStateMachine late;   // the database is not ready yet: rest pose, then it binds
        const anim::PoseDatabase* ready = nullptr;
        late.bind(&a, [&](const std::string& n) { return f.t.find(n); }, nullptr);
        late.setMotionResolver([&](const std::string&) { return ready; });
        late.setMotionContext(&f.skel, &q);
        late.setBool("go", true);
        for (int i = 0; i < 5; ++i) late.tick(kDt);
        late.evaluate(f.skel, p);
        check(late.activeState() == "Move" && std::fabs(p.local[2].position.x) < 1e-4f, "while the database loads the layer holds the rest pose");
        ready = &f.db;
        for (int i = 0; i < 30; ++i) late.tick(kDt);
        late.evaluate(f.skel, p);
        check(p.matches(f.skel) && f.t.find("run") != nullptr, "and the layer binds as soon as the resolver returns it");
    }

    AVER_INFO("debug views");
    {
        Fix f;
        run(f.mm, 1, steady(f.db, 150.0f));
        std::vector<f32> cur, best;
        f.mm.lastGroupCosts(cur, best);
        check(cur.size() == f.db.groups().size() && best.size() == cur.size(), "group costs: one per group");
        check(!f.mm.lastCandidates().empty() && f.mm.lastCandidates()[0].cost == f.mm.info().cost, "the best hit is listed");
        run(f.mm, 10, steady(f.db, 150.0f));
        f.mm.lastGroupCosts(cur, best);
        f32 sum = 0;
        for (const f32 c : best) sum += c;
        check(sum >= 0.0f, "and non-negative");
    }

    AVER_INFO("{} failure(s)", mt::g_failures);
    return mt::g_failures == 0 ? 0 : 1;
}
