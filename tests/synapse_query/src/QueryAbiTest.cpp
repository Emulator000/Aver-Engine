// synapse_query_abi.h against the real Aver.Synapse.Abi DLL: every entry point with aver_syn_last_error
// (dead entity -1, null out pointer -2, bad index -4, bad argument -6, "no answer yet" records Ok), the
// event sink, and a C fake that plays the managed side through aver_syn_managed_*: an action that sees
// first / dt / elapsed and returns Running then Success, a condition, abort delivery, a custom EQS test
// that filters items, a refused clash with a native leaf, and clear().
#include "aver/core/Log.hpp"
#include "aver/scene/World.hpp"
#include "aver/synapse/Bt.hpp"
#include "aver/synapse/synapse_ai_abi.h"
#include "aver/synapse/synapse_query_abi.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

static void clearWorld(scene::World& w) {
    std::vector<scene::Entity> all;
    for (u32 i = 0; i < w.count(); ++i) all.push_back(w.at(i));
    for (const scene::Entity e : all) if (w.valid(e) && !w.destroyPending(e)) w.destroy(e);
    w.flush();
}

static std::filesystem::path tempDir() {
    const std::filesystem::path d = std::filesystem::temp_directory_path() / "aver_synapse_query_abi_test";
    std::filesystem::create_directories(d);
    return d;
}
static void writeFile(const std::string& name, const std::string& text) {
    std::ofstream f(tempDir() / name, std::ios::binary | std::ios::trunc);
    f << text;
}

// ---- the managed side ------------------------------------------------------------------------------

struct Seen {
    int calls = 0, aborts = 0;
    int firstOnFirst = -1, firstOnSecond = -1;
    float dt = 0, elapsedFirst = -1, elapsedSecond = -1, p0 = 0;
    std::string text;
    int subject = 0, nameId = 0, structBytes = 0;
};
static Seen g_seen;
static int g_actId = 0, g_condId = 0, g_testId = 0;

static int32_t managed(AverSynManagedCall* c, void*) {
    g_seen.structBytes = c->structBytes;
    if (c->kind == AVER_SYN_MANAGED_ACTION) {
        ++g_seen.calls;
        g_seen.subject = c->subject;
        g_seen.nameId = c->nameId;
        g_seen.dt = c->dt;
        g_seen.p0 = c->p[0];
        g_seen.text = c->text ? c->text : "(null)";
        if (g_seen.calls == 1) { g_seen.firstOnFirst = c->first; g_seen.elapsedFirst = c->elapsed; }
        if (g_seen.calls == 2) { g_seen.firstOnSecond = c->first; g_seen.elapsedSecond = c->elapsed; }
        c->status = g_seen.calls >= 2 ? 1 : 0;   // Running, then Success
        return 1;
    }
    if (c->kind == AVER_SYN_MANAGED_CONDITION) {
        c->status = c->p[0] > 0.5f ? 1 : 0;
        return 1;
    }
    if (c->kind == AVER_SYN_MANAGED_ABORT) {
        ++g_seen.aborts;
        return 1;
    }
    if (c->kind == AVER_SYN_MANAGED_EQS_TEST) {
        c->value = c->pos[0] / 100.0f;
        return 1;
    }
    return 0;
}

static std::vector<std::pair<int, std::string>> g_events;
static void onEvent(int32_t entity, const char* name, void*) { g_events.emplace_back(entity, name); }

static void versionAndWiring() {
    check(aver_syn_query_abi_version() == 1, "aver_syn_query_abi_version() == 1");
    check(aver_syn_set_content_root(nullptr) == 0 && aver_syn_last_error() == -2, "set_content_root(NULL): -2");
    check(aver_syn_set_content_root(tempDir().generic_string().c_str()) == 1 && aver_syn_last_error() == 0, "set_content_root");
    check(aver_syn_eqs_set_budget(-1.0f, 0) == 0 && aver_syn_last_error() == -6, "a negative budget: -6");
    check(aver_syn_eqs_set_budget(1.0f, 0) == 1 && aver_syn_last_error() == 0, "a legal budget");
}

static void eqs(scene::World& w) {
    writeFile("abi_q.oceqs", "OCEQS 1\nOPTION\n  GENERATOR points list=\"100,0,0;200,0,0;300,0,0;400,0,0\"\n  TEST distance curve=inverse\n");
    writeFile("abi_bad.oceqs", "OCEQS 1\nOPTION\n  GENERATOR blob\n");
    const int32_t q = static_cast<int32_t>(w.create("Querier"));

    const int32_t id = aver_syn_eqs_load("abi_q.oceqs");
    check(id > 0 && aver_syn_last_error() == 0 && std::string(aver_syn_eqs_last_message()).empty(), "eqs_load a relative path through the content root");
    check(aver_syn_eqs_load("abi_q.oceqs") == id, "...cached: the same id");
    check(aver_syn_eqs_load("abi_bad.oceqs") == 0 && !std::string(aver_syn_eqs_last_message()).empty(), "eqs_load of a bad file: 0 and a message");
    check(aver_syn_eqs_load(nullptr) == 0 && aver_syn_last_error() == -6, "eqs_load(NULL): -6");
    check(aver_syn_eqs_reload("abi_q.oceqs") == 1, "eqs_reload");

    const int32_t h = aver_syn_eqs_run(id, q, 0, 0, 0, 0, 0, nullptr);
    check(h > 0 && aver_syn_eqs_status(h) == AVER_SYN_EQS_FINISHED, "eqs_run is synchronous: Finished");
    check(aver_syn_eqs_count(h) == 4, "count");
    float pos[3] = {};
    int32_t ent = -1, obj = -1, slot = -2;
    float score = -1;
    check(aver_syn_eqs_item(h, 0, pos, &ent, &score, &obj, &slot) == 1 && near(pos[0], 100) && near(score, 1.0f) && ent == 0 && slot == -1, "item 0 is the nearest (curve=inverse)");
    check(aver_syn_eqs_item(h, 3, nullptr, nullptr, nullptr, nullptr, nullptr) == 1, "any out pointer may be NULL");
    check(aver_syn_eqs_item(h, 4, pos, &ent, &score, &obj, &slot) == 0 && aver_syn_last_error() == -4, "item past the end: -4");
    check(aver_syn_eqs_item(h, -1, pos, &ent, &score, &obj, &slot) == 0 && aver_syn_last_error() == -4, "a negative index: -4");
    check(aver_syn_eqs_item(99999, 0, pos, &ent, &score, &obj, &slot) == 0 && aver_syn_last_error() == -1, "an unknown handle: -1");
    float v = -1;
    check(aver_syn_eqs_test_value(h, 0, 0, &v) == 1 && near(v, 100.0f), "test_value: the raw distance of item 0");
    check(aver_syn_eqs_test_value(h, 0, 0, nullptr) == 0 && aver_syn_last_error() == -2, "test_value into NULL: -2");
    check(aver_syn_eqs_test_value(h, 0, 5, &v) == 0 && aver_syn_last_error() == -4, "test_value for a test that does not exist: -4");
    int32_t gen = 0, passed = 0, evals = 0, trunc = -1;
    check(aver_syn_eqs_stats(h, &gen, &passed, &evals, &trunc) == 1 && gen == 4 && passed == 4 && evals == 4 && trunc == 0, "stats");
    check(aver_syn_eqs_release(h) == 1 && aver_syn_eqs_status(h) == AVER_SYN_EQS_UNKNOWN, "release forgets the result");
    check(aver_syn_eqs_release(h) == 0 && aver_syn_last_error() == -1, "releasing twice: -1");

    check(aver_syn_eqs_run(0, q, 0, 0, 0, 0, 0, nullptr) == 0 && aver_syn_last_error() == -6, "an unknown query id: -6");

    const int32_t hs = aver_syn_eqs_start(id, q, 0, 0, 0, 0, 0, nullptr, 0);
    check(hs > 0 && aver_syn_eqs_status(hs) == AVER_SYN_EQS_QUEUED && aver_syn_eqs_pending() >= 1, "eqs_start queues a run");
    check(aver_syn_eqs_count(hs) == 0 && aver_syn_last_error() == 0, "a queued run has no items yet, and that is not an error");
    aver_syn_ai_tick(nullptr, 0.016f);
    check(aver_syn_eqs_status(hs) == AVER_SYN_EQS_FINISHED && aver_syn_eqs_count(hs) == 4, "the next tick finishes it");
    aver_syn_eqs_release(hs);

    aver_syn_eqs_set_budget(0.0f, 1);
    const int32_t hc = aver_syn_eqs_start(id, q, 0, 0, 0, 0, 0, nullptr, 0);
    check(aver_syn_eqs_cancel(hc) == 1 && aver_syn_eqs_status(hc) == AVER_SYN_EQS_CANCELLED, "cancel a queued run");
    check(aver_syn_eqs_cancel(hc) == 0 && aver_syn_last_error() == 0, "cancelling it again is a valid 0");
    check(aver_syn_eqs_count(hc) == 0, "a cancelled run has no items");
    aver_syn_eqs_release(hc);
    aver_syn_eqs_set_budget(1.0f, 0);

    g_events.clear();
    aver_syn_set_notify(&onEvent, nullptr);
    const int32_t hn = aver_syn_eqs_run(id, q, 0, 0, 0, 0, 0, nullptr);
    check(g_events.size() == 1 && g_events[0].first == q && g_events[0].second == "EqsFinished", "the notify sink hears EqsFinished");
    aver_syn_eqs_release(hn);
    aver_syn_set_notify(nullptr, nullptr);
}

static void smartObjects(scene::World& w) {
    writeFile("abi_bench.ocso", "OCSO 1\nTAGS Furniture.Seat\nSLOT free_slot\n  POS 100 0 0\nEND\nSLOT tagged\n  POS 200 0 0\n  REQUIRES User.Humanoid\n  COOLDOWN 1\nEND\n");
    const int32_t bench = static_cast<int32_t>(w.create("Bench"));
    const int32_t user = static_cast<int32_t>(w.create("User"));
    const int32_t dead = static_cast<int32_t>(w.create("Dead"));
    w.destroy(static_cast<scene::Entity>(dead));
    w.flush();

    check(aver_syn_so_attach(dead, "abi_bench.ocso") == 0 && aver_syn_last_error() == -1, "attach to a dead entity: -1");
    check(aver_syn_so_attach(bench, nullptr) == 0 && aver_syn_last_error() == -6, "attach with no path: -6");
    check(aver_syn_so_attach(bench, "abi_bench.ocso") == 1 && aver_syn_last_error() == 0, "attach");
    check(aver_syn_so_slot_count(bench) == 2, "slot_count");
    check(aver_syn_so_slot_count(user) == -1 && aver_syn_last_error() == 0, "slot_count of a non-object is -1");
    float p[3], e3[3], yaw = 0, eyaw = 0;
    int32_t state = -1, who = -1;
    check(aver_syn_so_slot_info(bench, 0, p, &yaw, e3, &eyaw, &state, &who) == 1 && state == AVER_SYN_SO_FREE && who == 0, "slot_info: Free, nobody");
    check(aver_syn_so_slot_info(bench, 5, p, &yaw, e3, &eyaw, &state, &who) == 0 && aver_syn_last_error() == -4, "slot_info past the end: -4");
    check(aver_syn_so_slot_info(user, 0, p, &yaw, e3, &eyaw, &state, &who) == 0 && aver_syn_last_error() == -1, "slot_info on a non-object: -1");
    check(aver_syn_so_set_slot_enabled(bench, 9, 1) == 0 && aver_syn_last_error() == -4, "set_slot_enabled past the end: -4");

    int32_t objs[8], slots[8];
    check(aver_syn_so_find(user, 0, 0, 0, 1000, nullptr, nullptr, 0, objs, slots, 8) == 1 && objs[0] == bench && slots[0] == 0, "find: only the slot with no requirement");
    check(aver_syn_so_find(user, 0, 0, 0, 1000, nullptr, "User.Humanoid", 0, objs, slots, 8) == 2 && slots[0] == 0 && slots[1] == 1, "find with user tags: both, nearest first");
    check(aver_syn_so_find(user, 0, 0, 0, 1000, "Furniture", "User.Humanoid", AVER_SYN_SO_FIND_MATCH_ALL, objs, slots, 8) == 2, "find by the object's tag");
    check(aver_syn_so_find(user, 0, 0, 0, 1000, nullptr, nullptr, 0, nullptr, slots, 8) == 0 && aver_syn_last_error() == -2, "find into NULL: -2");
    check(aver_syn_so_find(user, 0, 0, 0, 1000, nullptr, nullptr, 0, objs, slots, 0) == 0 && aver_syn_last_error() == -6, "find with cap 0: -6");

    g_events.clear();
    aver_syn_set_notify(&onEvent, nullptr);
    const int32_t c = aver_syn_so_claim(user, bench, 0, nullptr, -1.0f);
    check(c > 0 && aver_syn_last_error() == 0 && aver_syn_so_last_refusal() == AVER_SYN_SO_REFUSE_NONE, "claim");
    check(g_events.size() == 2 && g_events[0].second == "SmartObjectClaimed" && g_events[0].first == user && g_events[1].second == "SmartObjectSlotClaimed" && g_events[1].first == bench,
          "claim raises its two events in order");
    check(aver_syn_so_claim(user, bench, 0, nullptr, -1.0f) == 0 && aver_syn_last_error() == 0 && aver_syn_so_last_refusal() == AVER_SYN_SO_REFUSE_NOT_FREE, "a second claim: 0, no error, refusal NotFree");
    check(aver_syn_so_claim(user, bench, 1, nullptr, -1.0f) == 0 && aver_syn_so_last_refusal() == AVER_SYN_SO_REFUSE_TAGS, "missing REQUIRES: refusal Tags");
    check(aver_syn_so_claim(user, bench, 7, nullptr, -1.0f) == 0 && aver_syn_so_last_refusal() == AVER_SYN_SO_REFUSE_NO_SUCH_SLOT, "no such slot");
    check(aver_syn_so_claim(dead, bench, 0, nullptr, -1.0f) == 0 && aver_syn_last_error() == -1 && aver_syn_so_last_refusal() == AVER_SYN_SO_REFUSE_NO_USER, "a dead user: -1 and NoUser");

    int32_t co = 0, cs = 0, cu = 0, cst = 0;
    check(aver_syn_so_claim_info(c, &co, &cs, &cu, &cst, p, &yaw, e3, &eyaw) == 1 && co == bench && cs == 0 && cu == user && cst == AVER_SYN_SO_CLAIMED, "claim_info");
    int32_t mine[4];
    check(aver_syn_so_claims_of(user, mine, 4) == 1 && mine[0] == c, "claims_of");
    check(aver_syn_so_claims_of(user, nullptr, 4) == 0 && aver_syn_last_error() == -2, "claims_of into NULL: -2");
    g_events.clear();
    check(aver_syn_so_use(c) == 1 && aver_syn_so_claim_info(c, &co, &cs, &cu, &cst, p, &yaw, e3, &eyaw) == 1 && cst == AVER_SYN_SO_OCCUPIED, "use: Occupied");
    check(g_events.size() == 2 && g_events[0].second == "SmartObjectUsed", "use raises SmartObjectUsed");
    check(aver_syn_so_use(c) == 0 && aver_syn_last_error() == 0, "using it again is a valid 0");
    check(aver_syn_so_release(c, 9) == 0 && aver_syn_last_error() == -6, "release with a bad reason: -6");
    check(aver_syn_so_release(c, AVER_SYN_SO_REL_COMPLETED) == 1, "release");
    check(aver_syn_so_release(c, AVER_SYN_SO_REL_COMPLETED) == 0 && aver_syn_last_error() == -1, "a stale claim: -1");
    check(aver_syn_so_use(c) == 0 && aver_syn_last_error() == -1, "use on a stale claim: -1");
    check(aver_syn_so_claim_info(c, &co, &cs, &cu, &cst, p, &yaw, e3, &eyaw) == 0 && aver_syn_last_error() == -1, "claim_info on a stale claim: -1");
    aver_syn_set_notify(nullptr, nullptr);

    const int32_t t = aver_syn_so_claim(user, bench, 1, "User.Humanoid.Adult", -1.0f);
    check(t > 0, "hierarchical user tags satisfy a REQUIRES");
    aver_syn_so_release(t, AVER_SYN_SO_REL_RELEASED);
    check(aver_syn_so_slot_info(bench, 1, p, &yaw, e3, &eyaw, &state, &who) == 1 && state == AVER_SYN_SO_COOLDOWN, "a slot with a COOLDOWN enters Cooldown on release");
    check(aver_syn_so_set_enabled(bench, 0) == 1 && aver_syn_so_claim(user, bench, 0, nullptr, -1.0f) == 0 && aver_syn_so_last_refusal() == AVER_SYN_SO_REFUSE_DISABLED, "a disabled object refuses claims");
    aver_syn_so_set_enabled(bench, 1);
}

static void managedSeam() {
    synapse::BtRegistry reg;
    aver_syn_ai_register_behaviors(&reg);
    check(aver_syn_managed_installed() == 0, "no dispatch installed yet");
    check(aver_syn_managed_register(AVER_SYN_MANAGED_ACTION, "TakeCover") == 0, "a name that clashes with a native leaf is refused");
    check(aver_syn_managed_register(7, "Whatever") == 0 && aver_syn_last_error() == -6, "an unknown kind: -6");
    check(aver_syn_managed_register(AVER_SYN_MANAGED_ACTION, "") == 0 && aver_syn_last_error() == -6, "an empty name: -6");
    g_actId = aver_syn_managed_register(AVER_SYN_MANAGED_ACTION, "AbiAct");
    g_condId = aver_syn_managed_register(AVER_SYN_MANAGED_CONDITION, "AbiCond");
    g_testId = aver_syn_managed_register(AVER_SYN_MANAGED_EQS_TEST, "AbiTest");
    check(g_actId > 0 && g_condId > 0 && g_testId > 0 && g_actId != g_condId, "registering assigns ids");
    check(aver_syn_managed_register(AVER_SYN_MANAGED_ACTION, "AbiAct") == 0, "a taken name is refused");
    check(reg.findAction("AbiAct") != nullptr && reg.findCondition("AbiCond") != nullptr, "kinds 0 and 1 became leaves in the registry");

    fmt::OcBtNode node;
    node.kind = fmt::OcBtNodeKind::Action;
    node.name = "AbiAct";
    node.params[0] = 2.5f;
    node.stringParam = "hello";
    const synapse::BtRegistry::ActionEntry* act = reg.findAction("AbiAct");
    f32 elapsed = 0.0f;
    check(act->fn(5, node, 0.1f, elapsed, act->user) == synapse::BtStatus::Failure, "before a dispatch is installed the leaf fails (never crashes)");

    aver_syn_managed_set_dispatch(&managed, nullptr);
    check(aver_syn_managed_installed() == 1, "dispatch installed");
    elapsed = 0.0f;
    g_seen = Seen{};
    check(act->fn(5, node, 0.1f, elapsed, act->user) == synapse::BtStatus::Running, "first tick: the managed side answered Running");
    check(g_seen.firstOnFirst == 1 && near(g_seen.elapsedFirst, 0.0f, 1e-5f) && near(g_seen.dt, 0.1f) && g_seen.subject == 5 && g_seen.nameId == g_actId, "...and saw first = 1, elapsed 0, dt, the subject and its name id");
    check(near(g_seen.p0, 2.5f) && g_seen.text == "hello" && g_seen.structBytes == static_cast<int>(sizeof(AverSynManagedCall)), "...and the node's params, text and the struct size");
    check(act->fn(5, node, 0.1f, elapsed, act->user) == synapse::BtStatus::Success, "second tick: Success");
    check(g_seen.firstOnSecond == 0 && near(g_seen.elapsedSecond, 0.1f, 1e-4f), "...with first = 0 and elapsed = the seconds since the first tick");

    fmt::OcBtNode cond;
    cond.kind = fmt::OcBtNodeKind::Condition;
    cond.name = "AbiCond";
    const synapse::BtRegistry::ConditionEntry* ce = reg.findCondition("AbiCond");
    cond.params[0] = 1.0f;
    check(ce->fn(5, cond, ce->user), "a managed Condition: true");
    cond.params[0] = 0.0f;
    check(!ce->fn(5, cond, ce->user), "...and false");

    g_seen.aborts = 0;
    check(act->onAbort != nullptr, "a managed Action has an abort");
    act->onAbort(5, node, act->user);
    check(g_seen.aborts == 1, "abort is delivered to the managed side");

    // A custom EQS test filters items.
    writeFile("abi_custom.oceqs", "OCEQS 1\nOPTION\n  GENERATOR points list=\"100,0,0;200,0,0;300,0,0;400,0,0\"\n  TEST custom name=AbiTest purpose=filter min=2.5\n");
    const int32_t id = aver_syn_eqs_load("abi_custom.oceqs");
    scene::World& w = scene::World::instance();
    const int32_t q = static_cast<int32_t>(w.create("CustomQuerier"));
    int32_t h = aver_syn_eqs_run(id, q, 0, 0, 0, 0, 0, nullptr);
    check(id > 0 && aver_syn_eqs_count(h) == 2, "an EQS custom test filters: values 3 and 4 pass min=2.5");
    aver_syn_eqs_release(h);

    // clear(): everything goes inactive; leaves fail, tests reject.
    aver_syn_managed_clear();
    elapsed = 0.0f;
    check(act->fn(5, node, 0.1f, elapsed, act->user) == synapse::BtStatus::Failure, "after clear() an Action answers Failure");
    cond.params[0] = 1.0f;
    check(!ce->fn(5, cond, ce->user), "...a Condition false");
    h = aver_syn_eqs_run(id, q, 0, 0, 0, 0, 0, nullptr);
    check(aver_syn_eqs_count(h) == 0, "...and a custom EQS test rejects every item");
    aver_syn_eqs_release(h);
    check(aver_syn_managed_register(AVER_SYN_MANAGED_ACTION, "AbiAct") == g_actId, "registering the name again reactivates it in place (same id)");
    elapsed = 0.0f;
    g_seen = Seen{};
    check(act->fn(5, node, 0.1f, elapsed, act->user) == synapse::BtStatus::Running, "...and the leaf works again");

    aver_syn_managed_set_dispatch(nullptr, nullptr);
    check(aver_syn_managed_installed() == 0, "a NULL dispatch clears it");
    elapsed = 0.0f;
    check(act->fn(5, node, 0.1f, elapsed, act->user) == synapse::BtStatus::Failure, "...and the leaf fails again");
    aver_syn_managed_clear();
}

int main() {
    AVER_INFO("QueryAbiTest");
    scene::World& w = scene::World::instance();
    check(aver_syn_ai_register() == 1, "aver_syn_ai_register");
    clearWorld(w);
    aver_syn_ai_reset();

    versionAndWiring();
    eqs(w);
    smartObjects(w);
    managedSeam();
    if (g_failures == 0) AVER_INFO("QueryAbiTest: all passed");
    return g_failures;
}
