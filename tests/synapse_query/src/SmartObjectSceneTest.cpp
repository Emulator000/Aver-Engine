// SmartObjectSystem against a real scene::World: the CSmartObject component and its String field, slot
// transforms that follow the owner, claim / use / release with the six mirrored board keys and the events in
// order, USE event / bt / statetree behaviours and their RELEASE modes, a user or object that goes away
// mid-use, and the five behaviour-tree leaves.
#include "aver/synapse/SynapseAi.hpp"
#include "aver/synapse/SynapseAgent.hpp"
#include "aver/synapse/SynapseBt.hpp"

#include "aver/core/Log.hpp"
#include "aver/scene/World.hpp"
#include "aver/scene/scene_abi.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

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
static int eventIndex(scene::Entity e, const char* name) {
    for (usize i = 0; i < g_events.size(); ++i)
        if (g_events[i].first == e && g_events[i].second == name) return static_cast<int>(i);
    return -1;
}

static bool near(f32 a, f32 b, f32 eps = 0.05f) { return std::fabs(a - b) <= eps; }

static scene::Entity spawn(scene::World& w, const char* name, f32 x, f32 y = 0.0f, f32 z = 0.0f) {
    Transform t;
    t.position = Vec3{x, y, z};
    return w.create(name, scene::kInvalidEntity, t);
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
static std::string q(const std::filesystem::path& p) { return "\"" + p.generic_string() + "\""; }

// Leaves for the abort tests.
static int g_workCalls = 0, g_workAborts = 0;
static BtStatus workAction(i32, const fmt::OcBtNode&, f32, f32&, void*) { ++g_workCalls; return BtStatus::Running; }
static void workAbort(i32, const fmt::OcBtNode&, void*) { ++g_workAborts; }

static BtAsset oneAction(const char* name, f32 p0) {
    BtAsset a;
    BtAssetNode root;
    root.node.kind = fmt::OcBtNodeKind::Action;
    root.node.parent = fmt::kOcBtNoParent;
    root.node.name = name;
    root.node.params[0] = p0;
    a.nodes.push_back(root);
    return a;
}

enum Slot { Sit = 0, WorkBt, WorkBtManual, WorkSt, AbortBt, AbortSt, Plain };

static std::string benchPath;

static void writeAssets() {
    const std::filesystem::path d = tempDir();
    std::string why;
    saveBtAsset((d / "so_wait.ocbt").generic_string(), oneAction("Wait", 0.05f), &why);
    saveBtAsset((d / "so_abort.ocbt").generic_string(), oneAction("SoWork", 0.0f), &why);
    writeFile(d / "so_wait.ocst", "OCST 1\nSTATE A\n  TASK Wait p0=0.05\nEND\n");
    writeFile(d / "so_abort.ocst", "OCST 1\nSTATE A\n  TASK SoWork\nEND\n");
    const std::filesystem::path bench = d / "so_bench.ocso";
    writeFile(bench,
              "OCSO 1\nTAGS Furniture.Seat\n"
              "SLOT sit\n  POS -40 0 45\n  ENTRY 80 0 0 180\n  TAGS Activity.Sit\n  USE event Sat\nEND\n"
              "SLOT work_bt\n  POS 0 40 0\n  USE bt " + q(d / "so_wait.ocbt") + "\nEND\n"
              "SLOT work_bt_manual\n  POS 0 80 0\n  USE bt " + q(d / "so_wait.ocbt") + "\n  RELEASE manual\nEND\n"
              "SLOT work_st\n  POS 0 120 0\n  USE statetree " + q(d / "so_wait.ocst") + "\nEND\n"
              "SLOT abort_bt\n  POS 0 160 0\n  USE bt " + q(d / "so_abort.ocbt") + "\nEND\n"
              "SLOT abort_st\n  POS 0 200 0\n  USE statetree " + q(d / "so_abort.ocst") + "\nEND\n"
              "SLOT plain\n  POS 0 240 0\n  COOLDOWN 1\nEND\n");
    benchPath = bench.generic_string();
}

static SoSlotState stateOf(SynapseAi& ai, scene::World& w, scene::Entity obj, u32 slot) {
    SoSlotInfo i;
    return ai.smartObjects().slotInfo(w, obj, slot, i) ? i.state : SoSlotState::Disabled;
}

static void component(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    check(w.componentId("CSmartObject") != 0 && ai.smartObjects().componentType() == w.componentId("CSmartObject"), "CSmartObject registers and appears in componentId");
    const u32 field = w.fieldId("CSmartObject.asset");
    check(field != 0, "its 'asset' field is registered");
    const scene::Entity e = spawn(w, "Bench", 0);
    auto* c = ai.smartObjects().attach(w, e, "");
    check(c != nullptr && c->enabled == 1, "attach applies the real defaults (enabled)");
    check(aver_scene_set_str(static_cast<int32_t>(e), static_cast<int32_t>(field), "AI/Bench.ocso") == 1 &&
              std::string(aver_scene_get_str(static_cast<int32_t>(e), static_cast<int32_t>(field))) == "AI/Bench.ocso",
          "the String field round-trips through aver_scene_set_str / get_str");
    const scene::Entity plain = spawn(w, "Plain", 0);
    check(ai.smartObjects().slotCount(w, plain) == -1, "an entity without the component is not a smart object");
    check(ai.smartObjects().slotCount(w, e) == -1, "a missing asset leaves the object unregistered (and only logs)");
}

static void world(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    const scene::Entity b = spawn(w, "Bench", 1000);
    check(ai.smartObjects().attach(w, b, benchPath) != nullptr, "attach with an .ocso path");
    check(ai.smartObjects().slotCount(w, b) == 7, "the definition loads: seven slots");
    SoSlotInfo s;
    check(ai.smartObjects().slotInfo(w, b, Sit, s) && near(s.world.pos.x, 960) && near(s.world.pos.z, 45) && near(s.world.entry.x, 1080) && near(s.world.entryYawDeg, 180),
          "slot positions are the owner's world matrix times the local ones");

    w.setLocalPosition(b, Vec3{0, 500, 0});
    ai.smartObjects().sync(w);
    check(ai.smartObjects().slotInfo(w, b, Sit, s) && near(s.world.pos.x, -40) && near(s.world.pos.y, 500) && near(s.world.pos.z, 45), "moving the owner moves its slots");
    w.setLocalRotation(b, Quat::fromAxisAngle(Vec3{0, 0, 1}, 1.57079632679f));
    ai.smartObjects().sync(w);
    check(ai.smartObjects().slotInfo(w, b, Sit, s) && near(s.world.pos.x, 0) && near(s.world.pos.y, 460) && near(s.world.yawDeg, 90, 0.1f), "rotating the owner swings the slots and their facing");
    check(ai.smartObjects().setEnabled(w, b, false), "the component can be disabled");
    SoRefusal why = SoRefusal::None;
    const scene::Entity u = spawn(w, "User", 0);
    check(ai.smartObjects().claim(w, u, SoSlotRef{b, Sit}, {}, -1, &why) == 0 && why == SoRefusal::Disabled, "a disabled object refuses claims");
    ai.smartObjects().setEnabled(w, b, true);
    w.setLocalRotation(b, Quat::fromAxisAngle(Vec3{0, 0, 1}, 0.0f));
    w.setLocalPosition(b, Vec3{1000, 0, 0});
    ai.smartObjects().sync(w);
}

static void claimEventsAndKeys(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    btSystem().clearBlackboards();
    const scene::Entity b = spawn(w, "Bench", 1000);
    ai.smartObjects().attach(w, b, benchPath);
    const scene::Entity u = spawn(w, "User", 0);
    g_events.clear();
    ai.setNotify(&sink, nullptr);

    const u32 claim = ai.smartObjects().claim(w, u, SoSlotRef{b, Sit}, {}, -1, nullptr);
    check(claim != 0, "claim");
    check(sawEvent(u, "SmartObjectClaimed") && sawEvent(b, "SmartObjectSlotClaimed"), "claim raises SmartObjectClaimed on the user and SmartObjectSlotClaimed on the object");
    check(eventIndex(u, "SmartObjectClaimed") < eventIndex(b, "SmartObjectSlotClaimed"), "...in that order");
    Blackboard* board = btSystem().blackboard(u);
    check(board && board->getEntity("SmartObject") == b && board->getInt("SmartObjectSlot") == Sit && board->getInt("SmartObjectHandle") == static_cast<i64>(claim), "the claim is mirrored: SmartObject, SmartObjectSlot, SmartObjectHandle");
    check(board && near(board->getVec3("SmartObjectPos").x, 960) && near(board->getVec3("SmartObjectEntry").x, 1080) && near(board->getFloat("SmartObjectYaw"), 0), "...and SmartObjectPos, SmartObjectEntry, SmartObjectYaw");
    check(board && board->has("SmartObjectHandle"), "the keys are defined on the fly");

    g_events.clear();
    check(ai.smartObjects().use(w, claim), "use");
    check(stateOf(ai, w, b, Sit) == SoSlotState::Occupied, "the slot is Occupied");
    check(sawEvent(u, "SmartObjectUsed") && sawEvent(b, "SmartObjectSlotUsed"), "use raises SmartObjectUsed and SmartObjectSlotUsed");
    check(sawEvent(u, "Sat"), "USE event <Name> raises that event on the user at Use");

    g_events.clear();
    check(ai.smartObjects().release(w, claim, SoReleaseReason::Released), "release");
    check(sawEvent(u, "SmartObjectReleased") && sawEvent(b, "SmartObjectSlotReleased"), "release raises SmartObjectReleased and SmartObjectSlotReleased");
    check(board && board->getEntity("SmartObject") == 0 && board->getInt("SmartObjectHandle") == 0, "...and, with no claim left, clears the mirrored keys");
    SoReleaseReason why;
    check(ai.smartObjects().endedReason(claim, why) && why == SoReleaseReason::Released, "the reason is remembered for a while");
    ai.setNotify(nullptr, nullptr);
}

static void behaviours(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    btSystem().clearBlackboards();
    g_events.clear();
    ai.setNotify(&sink, nullptr);
    const scene::Entity b = spawn(w, "Bench", 1000);
    ai.smartObjects().attach(w, b, benchPath);
    const scene::Entity u1 = spawn(w, "U1", 0);
    const scene::Entity u2 = spawn(w, "U2", 0);
    const scene::Entity u3 = spawn(w, "U3", 0);

    // USE bt, RELEASE auto: runs the tree on the user, releases Completed when it succeeds.
    const u32 c1 = ai.smartObjects().claim(w, u1, SoSlotRef{b, WorkBt}, {}, -1, nullptr);
    ai.smartObjects().use(w, c1);
    ai.tick(w, nullptr, 0.02f);
    ai.tick(w, nullptr, 0.02f);
    check(stateOf(ai, w, b, WorkBt) == SoSlotState::Occupied, "USE bt: still running after 0.04 s of a 0.05 s Wait");
    ai.tick(w, nullptr, 0.02f);
    check(stateOf(ai, w, b, WorkBt) == SoSlotState::Free, "USE bt: the tree finishes and the slot is released");
    SoReleaseReason why;
    check(ai.smartObjects().endedReason(c1, why) && why == SoReleaseReason::Completed && sawEvent(u1, "SmartObjectReleased"), "...as Completed, with the event");

    // RELEASE manual: stays Occupied after the behaviour finishes.
    const u32 c2 = ai.smartObjects().claim(w, u2, SoSlotRef{b, WorkBtManual}, {}, -1, nullptr);
    ai.smartObjects().use(w, c2);
    for (int i = 0; i < 6; ++i) ai.tick(w, nullptr, 0.02f);
    SoClaimInfo ci;
    check(ai.smartObjects().claimInfo(c2, ci) && ci.state == SoSlotState::Occupied, "RELEASE manual: the slot stays Occupied after the tree finished");
    check(ai.smartObjects().release(w, c2, SoReleaseReason::Released), "...until released by hand");

    // USE statetree.
    const u32 c3 = ai.smartObjects().claim(w, u3, SoSlotRef{b, WorkSt}, {}, -1, nullptr);
    ai.smartObjects().use(w, c3);
    for (int i = 0; i < 6; ++i) ai.tick(w, nullptr, 0.02f);
    check(stateOf(ai, w, b, WorkSt) == SoSlotState::Free && ai.smartObjects().endedReason(c3, why) && why == SoReleaseReason::Completed, "USE statetree: runs to Succeeded and releases Completed");

    // Claimed but not used: the behaviour does not run.
    const u32 c4 = ai.smartObjects().claim(w, u1, SoSlotRef{b, WorkBt}, {}, -1, nullptr);
    for (int i = 0; i < 6; ++i) ai.tick(w, nullptr, 0.02f);
    check(c4 != 0 && stateOf(ai, w, b, WorkBt) == SoSlotState::Claimed, "a Claimed (not Used) slot runs no behaviour");
    ai.smartObjects().release(w, c4, SoReleaseReason::Released);
    ai.setNotify(nullptr, nullptr);
}

static void goneAndAborts(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    btSystem().clearBlackboards();
    g_workCalls = g_workAborts = 0;
    btSystem().registry().registerAction("SoWork", &workAction, nullptr, &workAbort);
    const scene::Entity b = spawn(w, "Bench", 1000);
    ai.smartObjects().attach(w, b, benchPath);
    const scene::Entity u = spawn(w, "User", 0);
    const scene::Entity v = spawn(w, "User2", 0);

    const u32 c = ai.smartObjects().claim(w, u, SoSlotRef{b, AbortBt}, {}, -1, nullptr);
    ai.smartObjects().use(w, c);
    ai.tick(w, nullptr, 0.02f);
    ai.tick(w, nullptr, 0.02f);
    check(g_workCalls == 2 && g_workAborts == 0, "the behaviour's leaf runs every tick while Occupied");
    w.destroy(u);
    w.flush();
    ai.tick(w, nullptr, 0.02f);
    check(g_workAborts == 1, "a user destroyed mid-use: the running leaf is aborted");
    SoClaimInfo ci;
    SoReleaseReason why;
    check(!ai.smartObjects().claimInfo(c, ci) && stateOf(ai, w, b, AbortBt) == SoSlotState::Free && ai.smartObjects().endedReason(c, why) && why == SoReleaseReason::UserGone,
          "...and the slot is free (UserGone)");

    g_workCalls = g_workAborts = 0;
    const u32 c2 = ai.smartObjects().claim(w, v, SoSlotRef{b, AbortSt}, {}, -1, nullptr);
    ai.smartObjects().use(w, c2);
    ai.tick(w, nullptr, 0.02f);
    check(g_workCalls == 1, "a state-tree behaviour runs its task");
    ai.smartObjects().release(w, c2, SoReleaseReason::Released);
    check(g_workAborts == 1, "releasing by hand aborts the running state-tree task");

    // The object going away frees its claims.
    const u32 c3 = ai.smartObjects().claim(w, v, SoSlotRef{b, Plain}, {}, -1, nullptr);
    g_events.clear();
    ai.setNotify(&sink, nullptr);
    w.destroy(b);
    w.flush();
    ai.tick(w, nullptr, 0.02f);
    check(c3 != 0 && !ai.smartObjects().claimInfo(c3, ci) && ai.smartObjects().endedReason(c3, why) && why == SoReleaseReason::ObjectGone, "an object destroyed with a claim on it: ObjectGone");
    check(sawEvent(v, "SmartObjectReleased"), "...and the user hears about it");
    ai.setNotify(nullptr, nullptr);
}

static void leaves(scene::World& w, SynapseAi& ai) {
    clearWorld(w);
    btSystem().clearBlackboards();
    BtRegistry& reg = btSystem().registry();
    const auto action = [&](const char* n) { return reg.findAction(n); };
    const auto condition = [&](const char* n) { return reg.findCondition(n); };
    check(action("ClaimSmartObject") && action("MoveToSmartObject") && action("UseSmartObject") && action("ReleaseSmartObject") && condition("HasSmartObjectClaim"),
          "the five smart-object leaves are registered");

    const scene::Entity b = spawn(w, "Bench", 1000);
    ai.smartObjects().attach(w, b, benchPath);
    agentSystem().registerComponents(w);
    const scene::Entity u = spawn(w, "User", 0);
    auto* agent = agentSystem().attach(w, u);
    const i32 subject = static_cast<i32>(u);
    f32 el = 0.0f;

    fmt::OcBtNode claim;
    claim.kind = fmt::OcBtNodeKind::Action;
    claim.name = "ClaimSmartObject";
    claim.stringParam = "Activity.Sit";
    claim.params[0] = 5000.0f;
    fmt::OcBtNode has;
    has.kind = fmt::OcBtNodeKind::Condition;
    has.name = "HasSmartObjectClaim";
    const auto* hasE = condition("HasSmartObjectClaim");
    const auto* claimE = action("ClaimSmartObject");
    check(!hasE->fn(subject, has, hasE->user), "no claim yet");
    fmt::OcBtNode noSuch = claim;
    noSuch.stringParam = "Activity.Fly";
    check(claimE->fn(subject, noSuch, 0.02f, el, claimE->user) == BtStatus::Failure, "ClaimSmartObject: no slot with those tags: Failure");
    check(claimE->fn(subject, claim, 0.02f, el, claimE->user) == BtStatus::Success, "ClaimSmartObject: finds and claims the nearest matching slot");
    u32 held = 0;
    check(ai.smartObjects().claimsOf(u, &held, 1) == 1, "...and the user holds it");
    check(hasE->fn(subject, has, hasE->user), "HasSmartObjectClaim");
    has.params[0] = 1.0f;
    check(!hasE->fn(subject, has, hasE->user), "HasSmartObjectClaim with params[0] = 1 needs the claim to be in use");

    // MoveToSmartObject: a goal at the entry point, Running until arrival.
    fmt::OcBtNode move;
    move.kind = fmt::OcBtNodeKind::Action;
    move.name = "MoveToSmartObject";
    const auto* moveE = action("MoveToSmartObject");
    el = 0.0f;
    check(moveE->fn(subject, move, 0.02f, el, moveE->user) == BtStatus::Running, "MoveToSmartObject: Running while far from the entry point");
    check(agent && near(agent->goalXCm, 1080) && static_cast<AgentStatus>(agent->status) == AgentStatus::Requested, "...with the agent's goal set to the entry point");
    w.setLocalPosition(u, Vec3{1070, 0, 0});
    el = 0.0f;
    check(moveE->fn(subject, move, 0.02f, el, moveE->user) == BtStatus::Success, "...Success once within the arrive radius");

    // UseSmartObject: uses the claim; the slot is event-only, so it stays Occupied until released.
    fmt::OcBtNode use;
    use.kind = fmt::OcBtNodeKind::Action;
    use.name = "UseSmartObject";
    const auto* useE = action("UseSmartObject");
    el = 0.0f;
    check(useE->fn(subject, use, 0.02f, el, useE->user) == BtStatus::Running, "UseSmartObject: uses the claim, Running while it is held");
    check(hasE->fn(subject, has, hasE->user), "...and HasSmartObjectClaim(in use) is now true");
    check(useE->fn(subject, use, 0.02f, el, useE->user) == BtStatus::Running, "...Running on later ticks");
    const auto* relE = action("ReleaseSmartObject");
    fmt::OcBtNode rel;
    rel.kind = fmt::OcBtNodeKind::Action;
    rel.name = "ReleaseSmartObject";
    float el2 = 0.0f;
    check(relE->fn(subject, rel, 0.02f, el2, relE->user) == BtStatus::Success && ai.smartObjects().claimsOf(u, &held, 1) == 0, "ReleaseSmartObject: releases everything the subject holds");
    check(useE->fn(subject, use, 0.02f, el, useE->user) == BtStatus::Failure, "UseSmartObject: Failure when the claim was released as anything but Completed");

    // UseSmartObject through a behaviour that completes: Success.
    const scene::Entity u2 = spawn(w, "User2", 0);
    const u32 c = ai.smartObjects().claim(w, u2, SoSlotRef{b, WorkBt}, {}, -1, nullptr);
    el = 0.0f;
    const i32 s2 = static_cast<i32>(u2);
    check(c != 0 && useE->fn(s2, use, 0.02f, el, useE->user) == BtStatus::Running, "UseSmartObject on a slot with a behaviour: Running");
    for (int i = 0; i < 4; ++i) ai.tick(w, nullptr, 0.02f);
    check(useE->fn(s2, use, 0.02f, el, useE->user) == BtStatus::Success, "...Success when the behaviour completed and the slot was released as Completed");

    // onAbort releases.
    const scene::Entity u3 = spawn(w, "User3", 0);
    ai.smartObjects().claim(w, u3, SoSlotRef{b, Sit}, {}, -1, nullptr);
    el = 0.0f;
    useE->fn(static_cast<i32>(u3), use, 0.02f, el, useE->user);
    check(useE->onAbort != nullptr, "UseSmartObject has an abort");
    if (useE->onAbort) useE->onAbort(static_cast<i32>(u3), use, useE->user);
    check(ai.smartObjects().claimsOf(u3, &held, 1) == 0, "...which releases the claim");
}

int main() {
    AVER_INFO("SmartObjectSceneTest");
    scene::World& w = scene::World::instance();
    SynapseAi ai;
    BtSystem& sys = btSystem();
    sys.registerComponents(w);
    perceptionSystem().registerComponents(w);
    registerBuiltinBehaviors(sys);
    ai.registerComponents(w);
    ai.registerBehaviors(sys.registry());
    writeAssets();

    component(w, ai);
    world(w, ai);
    claimEventsAndKeys(w, ai);
    behaviours(w, ai);
    goneAndAborts(w, ai);
    leaves(w, ai);
    if (g_failures == 0) AVER_INFO("SmartObjectSceneTest: all passed");
    return g_failures;
}
