// EqsSystem against a real scene::World: contexts from entities, the board and perception, the actors
// generator (tag bits, name prefix, class provider), the slots generator over a real smart object, the
// RunEqsQuery / EqsHasResult leaves, queue order and budget across ticks, result expiry, a destroyed
// querier, the finished / failed events, and (when physics is built) a wall that blocks a trace.
#include "aver/synapse/SynapseAi.hpp"
#include "aver/synapse/SynapseBt.hpp"

#include "aver/core/Log.hpp"
#include "aver/scene/World.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#if AVER_MODULE_PHYSICS
#include "aver/physics/physics_abi.h"
#endif

using namespace aver;
using namespace aver::synapse;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static void clearWorld(scene::World& w) {
    std::vector<scene::Entity> all;
    for (u32 i = 0; i < w.count(); ++i) all.push_back(w.at(i));
    for (const scene::Entity e : all) if (w.valid(e) && !w.destroyPending(e)) w.destroy(e);
    w.flush();
}

static std::vector<std::pair<scene::Entity, std::string>> g_events;
static void sink(scene::Entity e, const char* name, void*) { g_events.emplace_back(e, name); }

static bool sawEvent(scene::Entity e, const char* name) {
    for (const auto& ev : g_events)
        if (ev.first == e && ev.second == name) return true;
    return false;
}

static bool near(f32 a, f32 b, f32 eps = 0.01f) { return std::fabs(a - b) <= eps; }

static scene::Entity spawn(scene::World& w, const char* name, f32 x, f32 y = 0.0f, f32 z = 0.0f) {
    Transform t;
    t.position = Vec3{x, y, z};
    return w.create(name, scene::kInvalidEntity, t);
}

static u32 reg(SynapseAi& ai, const char* name, const std::string& text) {
    EqsQuery q;
    std::string why;
    if (!parseEqsQuery(text, name, q, &why)) {
        ++g_failures;
        AVER_ERROR("  FAIL  query '{}' did not parse: {}", name, why);
        return 0;
    }
    return ai.eqs().registerQuery(name, q);
}

static EqsRunParams params(scene::Entity querier, scene::Entity target = scene::kInvalidEntity) {
    EqsRunParams p;
    p.querier = querier;
    p.target = target;
    return p;
}

static const std::string kPoints = "OPTION\n  GENERATOR points list=\"100,0,0;200,0,0;300,0,0;400,0,0\"\n";

// A fake class provider: entities 'g_classA' for the class name "Guard".
static std::vector<i32> g_classEntities;
static i32 classProvider(const char* className, i32, i32* out, i32 cap, void*) {
    if (std::string(className) != "Guard") return 0;
    i32 n = 0;
    for (const i32 e : g_classEntities)
        if (n < cap) out[n++] = e;
    return n;
}

static void contexts(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    btSystem().clearBlackboards();
    const scene::Entity q = spawn(w, "Querier", 0);
    const scene::Entity t = spawn(w, "Target", 500);
    const scene::Entity foe = spawn(w, "Foe", 400);

    const u32 toTarget = reg(ai, "toTarget", "OCEQS 1\nRESULT best\n" + kPoints + "  TEST distance to=target curve=inverse\n");
    u32 h = ai.eqs().runSync(toTarget, params(q, t));
    const EqsResult* r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && near(r->items[0].pos.x, 400), "the 'target' context is the request's target entity");
    ai.eqs().release(h);

    const u32 noTarget = reg(ai, "noTarget", "OCEQS 1\nOPTION\n  GENERATOR points list=\"100,0,0\"\n  TEST distance to=target purpose=filter max=1000\n");
    h = ai.eqs().runSync(noTarget, params(q));
    r = ai.eqs().result(h);
    check(r && r->items.empty() && r->status == EqsResult::Status::Finished, "an unset target is an unresolved context: a filter rejects everything");
    ai.eqs().release(h);

    // Board-declared contexts: an Entity key and a Vec3 key.
    Blackboard* board = btSystem().blackboard(q, true);
    BbKeyDef k;
    k.name = "Foe";
    k.type = BbType::Entity;
    board->defineKey(k);
    board->setEntity("Foe", foe);
    k.name = "Spot";
    k.type = BbType::Vec3;
    board->defineKey(k);
    board->setVec3("Spot", Vec3{100, 0, 0});
    const u32 toFoe = reg(ai, "toFoe", "OCEQS 1\nCONTEXT foe key=Foe\nRESULT best\n" + kPoints + "  TEST distance to=foe curve=inverse\n");
    h = ai.eqs().runSync(toFoe, params(q));
    r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && near(r->items[0].pos.x, 400), "a CONTEXT bound to an Entity board key resolves to that entity's position");
    ai.eqs().release(h);
    const u32 toSpot = reg(ai, "toSpot", "OCEQS 1\nCONTEXT spot key=Spot\nRESULT best\n" + kPoints + "  TEST distance to=spot curve=inverse\n");
    h = ai.eqs().runSync(toSpot, params(q));
    r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && near(r->items[0].pos.x, 100), "a CONTEXT bound to a Vec3 key is a point");
    ai.eqs().release(h);

    // The target context can be redeclared onto a board key; the request's target is the fallback.
    board->setEntity("Foe", scene::kInvalidEntity);
    const u32 viaKey = reg(ai, "viaKey", "OCEQS 1\nCONTEXT target key=Foe\nRESULT best\n" + kPoints + "  TEST distance to=target curve=inverse\n");
    h = ai.eqs().runSync(viaKey, params(q, t));
    r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && near(r->items[0].pos.x, 400), "CONTEXT target key=...: an unset key falls back to the request's target");
    ai.eqs().release(h);

    // 'seen' is the querier's last known perceived target.
    perceptionSystem().registerComponents(w);
    auto* per = perceptionSystem().attach(w, q);
    per->lastKnownTargetEntity = static_cast<i32>(foe);
    const u32 seen = reg(ai, "seen", "OCEQS 1\nRESULT best\n" + kPoints + "  TEST distance to=seen curve=inverse\n");
    h = ai.eqs().runSync(seen, params(q));
    r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && near(r->items[0].pos.x, 400), "the 'seen' context follows CSynapsePerception.lastKnownTargetEntity");
    ai.eqs().release(h);

    // The 'point' context.
    const u32 point = reg(ai, "point", "OCEQS 1\nRESULT best\n" + kPoints + "  TEST distance to=point curve=inverse\n");
    EqsRunParams pp = params(q);
    pp.point = Vec3{300, 0, 0};
    pp.hasPoint = true;
    h = ai.eqs().runSync(point, pp);
    r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && near(r->items[0].pos.x, 300), "the 'point' context is the request's point");
    ai.eqs().release(h);

    // Dot against the querier's world forward (+X rotated): a querier turned to face +Y.
    w.setLocalRotation(q, Quat::fromAxisAngle(Vec3{0, 0, 1}, 1.57079632679f));
    const u32 dot = reg(ai, "dot", "OCEQS 1\nOPTION\n  GENERATOR points list=\"100,0,0;0,100,0\"\n  TEST dot purpose=filter min=0.9\n");
    h = ai.eqs().runSync(dot, params(q));
    r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && near(r->items[0].pos.y, 100), "dot uses the querier's world forward");
    ai.eqs().release(h);
}

static void actors(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    const scene::Entity q = spawn(w, "Querier", 0);
    const scene::Entity g1 = spawn(w, "Guard1", 100);
    const scene::Entity g2 = spawn(w, "Guard2", 200);
    const scene::Entity o1 = spawn(w, "Other", 300);
    // One at a time: adding a component can grow the pool and move the earlier one.
    static_cast<scene::CTags*>(w.addComponent(g1, scene::kComponentTags))->bits = 2;
    static_cast<scene::CTags*>(w.addComponent(o1, scene::kComponentTags))->bits = 6;

    const u32 byBits = reg(ai, "byBits", "OCEQS 1\nOPTION\n  GENERATOR actors tagbits=2\n");
    u32 h = ai.eqs().runSync(byBits, params(q));
    const EqsResult* r = ai.eqs().result(h);
    check(r && r->items.size() == 2 && r->items[0].entity != 0, "actors by tagbits: the entities carrying any of the bits");
    ai.eqs().release(h);
    const u32 byName = reg(ai, "byName", "OCEQS 1\nOPTION\n  GENERATOR actors named=Guard\n  TEST distance curve=inverse\n");
    h = ai.eqs().runSync(byName, params(q));
    r = ai.eqs().result(h);
    check(r && r->items.size() == 2 && r->items[0].entity == g1 && r->items[1].entity == g2, "actors by name prefix, nearest first with a distance score");
    ai.eqs().release(h);

    g_classEntities = {static_cast<i32>(g2), static_cast<i32>(o1)};
    const u32 byClass = reg(ai, "byClass", "OCEQS 1\nOPTION\n  GENERATOR actors class=Guard\n");
    h = ai.eqs().runSync(byClass, params(q));
    r = ai.eqs().result(h);
    check(r && r->items.empty(), "actors by class returns nothing without a provider");
    ai.eqs().release(h);
    ai.eqs().setActorProvider(&classProvider, nullptr);
    h = ai.eqs().runSync(byClass, params(q));
    r = ai.eqs().result(h);
    check(r && r->items.size() == 2 && r->items[0].entity == g2, "...and goes through the host provider once one is installed");
    ai.eqs().release(h);
    ai.eqs().setActorProvider(nullptr, nullptr);

    const u32 tagTest = reg(ai, "tagTest", "OCEQS 1\nOPTION\n  GENERATOR actors named=Guard\n  TEST tag bits=2 purpose=filter\n");
    h = ai.eqs().runSync(tagTest, params(q));
    r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && r->items[0].entity == g1, "a tag test reads the entity's CTags");
    ai.eqs().release(h);
}

static std::filesystem::path tempDir() {
    const std::filesystem::path d = std::filesystem::temp_directory_path() / "aver_synapse_query_test";
    std::filesystem::create_directories(d);
    return d;
}

static void writeFile(const std::filesystem::path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

static void slots(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    const std::filesystem::path bench = tempDir() / "scene_bench.ocso";
    writeFile(bench, "OCSO 1\nTAGS Furniture.Seat\nSLOT left\n  POS -40 0 45\n  TAGS Activity.Sit\nEND\nSLOT right\n  POS 40 0 45\n  REQUIRES User.Humanoid\nEND\n");
    const scene::Entity q = spawn(w, "Querier", 0);
    const scene::Entity b = spawn(w, "Bench", 1000);
    check(ai.smartObjects().attach(w, b, bench.generic_string()) != nullptr, "the bench takes CSmartObject");

    const u32 find = reg(ai, "slotQ", "OCEQS 1\nOPTION\n  GENERATOR slots radius=2000\n  TEST distance curve=inverse\n");
    u32 h = ai.eqs().runSync(find, params(q));
    const EqsResult* r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && r->items[0].object == b && r->items[0].slot == 0, "slots: only the slot the request's user tags allow (right needs User.Humanoid)");
    check(r && near(r->items[0].pos.x, 960) && near(r->items[0].pos.z, 45), "...at its world position (the owner is at x = 1000)");
    ai.eqs().release(h);

    EqsRunParams p = params(q);
    p.userTags = TagList::parse("User.Humanoid");
    h = ai.eqs().runSync(find, p);
    r = ai.eqs().result(h);
    check(r && r->items.size() == 2 && r->items[0].slot == 0 && r->items[1].slot == 1, "with the tag, both slots, nearest first");
    check(r && r->tagSets.size() >= 2 && r->tagSets[r->items[0].tagSet].hasAny(TagList::parse("Activity.Sit")), "a slot item carries the slot's tags");
    ai.eqs().release(h);

    const u32 tagged = reg(ai, "slotTag", "OCEQS 1\nOPTION\n  GENERATOR slots radius=2000\n  TEST tag names=Activity.Sit purpose=filter\n");
    h = ai.eqs().runSync(tagged, p);
    r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && r->items[0].slot == 0, "a tag test filters slot items by the slot's tags");
    ai.eqs().release(h);

    const u32 cl = ai.smartObjects().claim(w, q, SoSlotRef{b, 0}, p.userTags, -1.0f, nullptr);
    h = ai.eqs().runSync(find, p);
    r = ai.eqs().result(h);
    check(cl != 0 && r && r->items.size() == 1 && r->items[0].slot == 1, "a claimed slot is no longer offered (free=1)");
    ai.eqs().release(h);
    ai.smartObjects().release(w, cl, SoReleaseReason::Released);
}

static void leaves(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    btSystem().clearBlackboards();
    const scene::Entity q = spawn(w, "Querier", 0);
    reg(ai, "leafq", "OCEQS 1\nOUTPUT pos=CoverPos entity=CoverEntity score=CoverScore count=CoverCount\nRESULT best\n" + kPoints + "  TEST distance curve=inverse\n");
    reg(ai, "leafnone", "OCEQS 1\n" + kPoints + "  TEST distance max=0 purpose=filter\n");
    BtRegistry& breg = btSystem().registry();
    const BtRegistry::ActionEntry* run = breg.findAction("RunEqsQuery");
    const BtRegistry::ConditionEntry* has = breg.findCondition("EqsHasResult");
    check(run && has, "RunEqsQuery and EqsHasResult are registered");
    if (!run || !has) return;

    fmt::OcBtNode node;
    node.kind = fmt::OcBtNodeKind::Action;
    node.name = "RunEqsQuery";
    node.stringParam = "leafq";
    fmt::OcBtNode haveNode = node;
    haveNode.kind = fmt::OcBtNodeKind::Condition;
    haveNode.name = "EqsHasResult";
    const i32 subject = static_cast<i32>(q);

    f32 elapsed = 0.0f;
    check(run->fn(subject, node, 0.016f, elapsed, run->user) == BtStatus::Running, "the first tick starts a run: Running");
    check(!has->fn(subject, haveNode, has->user), "EqsHasResult is false before any run finished");
    ai.tick(w, nullptr, 0.016f);
    check(run->fn(subject, node, 0.016f, elapsed, run->user) == BtStatus::Success, "the next tick finds the result: Success");
    Blackboard* b = btSystem().blackboard(q);
    check(b && near(b->getVec3("CoverPos").x, 100) && b->getEntity("CoverEntity") == 0 && near(b->getFloat("CoverScore"), 1.0f) && b->getInt("CoverCount") == 1,
          "the OUTPUT keys are defined on the user's board and filled from the best item");
    check(has->fn(subject, haveNode, has->user), "EqsHasResult is now true");
    haveNode.params[0] = 5.0f;
    check(has->fn(subject, haveNode, has->user), "...and still true inside its max age");
    ai.tick(w, nullptr, 10.0f);
    check(!has->fn(subject, haveNode, has->user), "...false once the last result is older than params[0] seconds");
    haveNode.params[0] = 0.0f;
    check(has->fn(subject, haveNode, has->user), "params[0] = 0 accepts any age");

    fmt::OcBtNode none = node;
    none.stringParam = "leafnone";
    elapsed = 0.0f;
    run->fn(subject, none, 0.016f, elapsed, run->user);
    ai.tick(w, nullptr, 0.016f);
    check(run->fn(subject, none, 0.016f, elapsed, run->user) == BtStatus::Failure, "a run with zero items: Failure");

    fmt::OcBtNode bad = node;
    bad.stringParam = "no_such_query";
    elapsed = 0.0f;
    check(run->fn(subject, bad, 0.016f, elapsed, run->user) == BtStatus::Failure, "an unknown query: Failure");

    elapsed = 0.0f;
    run->fn(subject, node, 0.016f, elapsed, run->user);
    check(ai.eqs().pending() == 1, "a started run is pending");
    check(run->onAbort != nullptr, "the leaf has an abort");
    if (run->onAbort) run->onAbort(subject, node, run->user);
    check(ai.eqs().pending() == 0, "onAbort cancels the run");

    fmt::OcBtNode slow = node;
    slow.params[0] = 0.5f;
    elapsed = 0.0f;
    run->fn(subject, slow, 0.016f, elapsed, run->user);
    check(run->fn(subject, slow, 1.0f, elapsed, run->user) == BtStatus::Failure && ai.eqs().pending() == 0, "params[0] is a timeout: the run is cancelled and the leaf fails");
}

static void queueing(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    const scene::Entity q = spawn(w, "Querier", 0);
    const u32 qid = reg(ai, "grid25", "OCEQS 1\nOPTION\n  GENERATOR grid halfx=200 halfy=200 step=100\n  TEST distance\n");
    ai.eqs().setBudget(0.0f, 60);   // units only: deterministic across machines
    const u32 a = ai.eqs().start(qid, params(q), 0);
    const u32 b = ai.eqs().start(qid, params(q), 5);
    const u32 c = ai.eqs().start(qid, params(q), 0);
    check(a && b && c && ai.eqs().pending() == 3 && ai.eqs().status(a) == EqsSystem::Status::Queued, "three runs queue");
    int doneAt[3] = {-1, -1, -1};
    for (int tick = 1; tick <= 12; ++tick) {
        ai.tick(w, nullptr, 0.016f);
        const u32 hs[3] = {a, b, c};
        for (int i = 0; i < 3; ++i)
            if (doneAt[i] < 0 && ai.eqs().status(hs[i]) == EqsSystem::Status::Finished) doneAt[i] = tick;
    }
    check(doneAt[0] > 0 && doneAt[1] > 0 && doneAt[2] > 0, "all three finish");
    check(doneAt[1] >= 2, "the unit budget spreads a run over several ticks");
    check(doneAt[1] < doneAt[0] && doneAt[0] < doneAt[2], "higher priority first, then first come first served");
    const EqsResult* r = ai.eqs().result(a);
    check(r && r->items.size() == 25 && r->evaluations == 25, "a time-sliced run gives the full result");
    ai.eqs().setBudget(1.0f, 0);

    // Results are kept 30 s of game time unless released.
    ai.tick(w, nullptr, 31.0f);
    check(ai.eqs().status(a) == EqsSystem::Status::Unknown && ai.eqs().result(a) == nullptr, "an unreleased result expires after 30 seconds");

    // A destroyed querier cancels its runs.
    const scene::Entity victim = spawn(w, "Victim", 0);
    ai.eqs().setBudget(0.0f, 1);
    const u32 doomed = ai.eqs().start(qid, params(victim), 0);
    ai.tick(w, nullptr, 0.016f);
    w.destroy(victim);
    w.flush();
    ai.tick(w, nullptr, 0.016f);
    check(ai.eqs().status(doomed) == EqsSystem::Status::Cancelled, "a run whose querier was destroyed is cancelled");
    ai.eqs().setBudget(1.0f, 0);

    // The queue is bounded.
    ai.eqs().reset();
    u32 accepted = 0;
    for (int i = 0; i < 70; ++i) accepted += ai.eqs().start(qid, params(q), 0) != 0 ? 1 : 0;
    check(accepted == 64, "at most 64 runs may wait");
    ai.eqs().reset();
}

static void events(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    g_events.clear();
    ai.setNotify(&sink, nullptr);
    const scene::Entity q = spawn(w, "Querier", 0);
    const u32 ok = reg(ai, "evOk", "OCEQS 1\n" + kPoints);
    const u32 bad = reg(ai, "evBad", "OCEQS 1\nOPTION\n  GENERATOR nav\n");
    u32 h = ai.eqs().runSync(ok, params(q));
    check(sawEvent(q, "EqsFinished"), "a finished run raises EqsFinished on the querier");
    ai.eqs().release(h);
    h = ai.eqs().runSync(bad, params(q));
    check(sawEvent(q, "EqsFailed") && ai.eqs().status(h) == EqsSystem::Status::Failed, "a failed run raises EqsFailed");
    const EqsResult* r = ai.eqs().result(h);
    check(r && !r->reason.empty(), "...with a reason");
    ai.eqs().release(h);

    g_events.clear();
    const u32 started = ai.eqs().start(ok, params(q), 0);
    check(g_events.empty(), "a queued run raises nothing yet");
    ai.tick(w, nullptr, 0.016f);
    check(sawEvent(q, "EqsFinished") && started != 0, "...until a tick finishes it");
    ai.setNotify(nullptr, nullptr);
}

#if AVER_MODULE_PHYSICS
static void physicsTrace(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    check(aver_phys_init() == 1, "physics world starts");
    const scene::Entity q = spawn(w, "Querier", 0);
    const i32 wall = aver_phys_add_static_box(250.0f, 0.0f, 80.0f, 10.0f, 200.0f, 200.0f);
    check(wall != 0, "a wall between the querier and +X");
    const u32 id = reg(ai, "wallQ", "OCEQS 1\nOPTION\n  GENERATOR points list=\"500,0,0;0,500,0\"\n  TEST trace purpose=filter\n");
    const u32 h = ai.eqs().runSync(id, params(q));
    const EqsResult* r = ai.eqs().result(h);
    check(r && r->items.size() == 1 && near(r->items[0].pos.y, 500), "trace: the item behind the wall is blocked, the clear one is kept");
    ai.eqs().release(h);
    aver_phys_remove_body(wall);
}
#endif

int main() {
    AVER_INFO("EqsSceneTest");
    scene::World& w = scene::World::instance();
    SynapseAi ai;
    BtSystem& sys = btSystem();
    sys.registerComponents(w);
    perceptionSystem().registerComponents(w);
    registerBuiltinBehaviors(sys);
    ai.registerComponents(w);
    ai.registerBehaviors(sys.registry());
    ai.setNotify(nullptr, nullptr);

    contexts(w, ai);
    actors(w, ai);
    slots(w, ai);
    leaves(w, ai);
    queueing(w, ai);
    events(w, ai);
#if AVER_MODULE_PHYSICS
    physicsTrace(w, ai);
#endif
    if (g_failures == 0) AVER_INFO("EqsSceneTest: all passed");
    return g_failures;
}
