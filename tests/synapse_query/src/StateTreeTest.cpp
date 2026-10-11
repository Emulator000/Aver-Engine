// The StateTree evaluator (tickStateTree) with fake leaves that count their calls and a real Blackboard:
// selection and enter conditions, concurrent tasks, COMPLETE modes, every trigger, leaf-first priority,
// default bubbling, re-entry, aborts, evaluators, events, ONFINISH, stop / restart and the bubbling cap.
// Pure: no world.
#include "aver/synapse/StateTree.hpp"

#include "aver/core/Log.hpp"

#include <map>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::synapse;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

// ---- fake leaves --------------------------------------------------------------------------------------
// Act: s = label, p0 = ticks until it resolves (0 = runs forever), p1 = 2 to fail instead of succeed.

static std::map<std::string, int> g_calls, g_starts, g_aborts, g_condCalls;
static std::map<std::string, bool> g_condVal;
static std::vector<std::string> g_order;

static void resetCounters() {
    g_calls.clear(); g_starts.clear(); g_aborts.clear(); g_condCalls.clear(); g_condVal.clear(); g_order.clear();
}

static BtStatus actAction(i32, const fmt::OcBtNode& n, f32, f32& elapsed, void*) {
    const std::string& label = n.stringParam;
    ++g_calls[label];
    if (elapsed == 0.0f) ++g_starts[label];
    g_order.push_back(label);
    elapsed += 1.0f;
    const int limit = static_cast<int>(n.params[0]);
    if (limit > 0 && elapsed >= static_cast<f32>(limit)) return n.params[1] == 2.0f ? BtStatus::Failure : BtStatus::Success;
    return BtStatus::Running;
}
static void actAbort(i32, const fmt::OcBtNode& n, void*) { ++g_aborts[n.stringParam]; }
static bool condFn(i32, const fmt::OcBtNode& n, void*) {
    ++g_condCalls[n.stringParam];
    return g_condVal[n.stringParam];
}

struct Fx {
    StateTreeAsset a;
    BtRegistry reg;
    Blackboard board;
    StateTreeInstance inst;

    explicit Fx(const std::string& text) {
        resetCounters();
        std::string why;
        if (!parseStateTree(text, "t.ocst", a, &why)) {
            ++g_failures;
            AVER_ERROR("  FAIL  test tree did not parse: {}", why);
        }
        board.applySchema(a.schema);
        reg.registerAction("Act", &actAction, nullptr, &actAbort);
        reg.registerCondition("Cond", &condFn);
    }
    StStatus tick() { return tickStateTree(a, reg, 1, 0.016f, inst, &board); }
    std::string leaf() const { return inst.active.empty() ? "" : a.statePath(inst.active.back()); }
    bool in(const char* path) const {
        for (const i32 s : inst.active)
            if (a.statePath(s) == path) return true;
        return false;
    }
};

static void selection() {
    Fx f("OCST 1\nKEY Open bool agent false\nKEY Never bool agent false\n"
         "STATE A\n  ENTER\n    IF bb Open\n  TASK Act s=a\nEND\n"
         "STATE B\n  TASK Act s=b\nEND\n");
    check(f.tick() == StStatus::Running && f.leaf() == "B", "the first root whose enter conditions pass is selected (A is closed, B is not)");
    check(g_calls["a"] == 0 && g_calls["b"] == 1, "...and only its tasks ran");
    f.board.setBool("Open", true);
    f.inst.restart();
    f.tick();
    check(f.leaf() == "A", "after restart(), a now-open A wins");
    check(g_aborts["b"] == 1, "restart() aborted B's running task");

    Fx g("OCST 1\nKEY Never bool agent false\n"
         "STATE G\n  GROUP\n  STATE K\n    ENTER\n      IF bb Never\n  END\nEND\n"
         "STATE P\n  STATE Q\n    ENTER\n      IF bb Never\n  END\nEND\n"
         "STATE H\n  TASK Act s=h\nEND\n");
    g.tick();
    check(g.leaf() == "H", "a GROUP and a parent with no selectable child are skipped");
    Fx h("OCST 1\nKEY Never bool agent false\nSTATE G\n  GROUP\n  STATE K\n    ENTER\n      IF bb Never\n  END\nEND\n");
    check(h.tick() == StStatus::Failed, "no selectable state at all: the tree Fails");

    Fx n("OCST 1\nSTATE A\n  ENTER\n    IF bb Open\n  TASK Act s=a\nEND\nSTATE B\n  TASK Act s=b\nEND\n");
    n.board.applySchema(n.a.schema);
    StateTreeInstance inst;
    check(tickStateTree(n.a, n.reg, 1, 0.016f, inst, nullptr) == StStatus::Running && !inst.active.empty() && n.a.states[static_cast<usize>(inst.active.back())].name == "B",
          "a null board reads every bb condition false, without crashing");

    Fx c("OCST 1\nSTATE A\n  ENTER\n    IF cond Cond s=ok\n  TASK Act s=a\nEND\nSTATE B\n  ENTER\n    IF not cond Cond s=ok\n  TASK Act s=b\nEND\n");
    g_condVal["ok"] = false;
    c.tick();
    check(c.leaf() == "B" && g_condCalls["ok"] >= 2, "IF cond calls the registered Condition leaf; IF not cond negates it");
    resetCounters();
    g_condVal["ok"] = true;
    Fx d("OCST 1\nSTATE A\n  ENTER\n    IF cond Cond s=ok\n  TASK Act s=a\nEND\nSTATE B\n  TASK Act s=b\nEND\n");
    g_condVal["ok"] = true;
    d.tick();
    check(d.leaf() == "A", "...and a true condition enters A");
}

static void tasksAndComplete() {
    Fx f("OCST 1\nSTATE S\n  TASK Act s=t1 p0=2\n  TASK Act s=t2\nEND\n");
    f.tick(); f.tick(); f.tick();
    check(g_calls["t1"] == 2 && g_calls["t2"] == 3, "tasks run concurrently; a finished task is not ticked again");
    check(g_starts["t1"] == 1 && g_starts["t2"] == 1, "...and each started once");

    Fx any("OCST 1\nSTATE S\n  COMPLETE any\n  TASK Act s=t1 p0=1\n  TASK Act s=t2\n  ON completed TO SUCCEEDED\nEND\n");
    check(any.tick() == StStatus::Succeeded, "COMPLETE any: one finished task completes the state");
    check(g_aborts["t2"] == 1 && any.inst.active.empty(), "...finishing the tree exits the state and aborts its running task");

    Fx all("OCST 1\nSTATE S\n  TASK Act s=t1 p0=1\n  TASK Act s=t2\n  ON completed TO SUCCEEDED\nEND\n");
    bool running = true;
    for (int i = 0; i < 5; ++i) running = running && all.tick() == StStatus::Running;
    check(running, "COMPLETE all (the default): a still-running task keeps the state open");

    Fx none("OCST 1\nSTATE S\n  COMPLETE none\n  TASK Act s=t1 p0=1\n  ON completed TO SUCCEEDED\nEND\n");
    running = true;
    for (int i = 0; i < 4; ++i) running = running && none.tick() == StStatus::Running;
    check(running, "COMPLETE none never completes");

    Fx allDone("OCST 1\nSTATE S\n  TASK Act s=t1 p0=1\n  TASK Act s=t2 p0=2\n  ON completed TO SUCCEEDED\nEND\n");
    allDone.tick();
    check(allDone.inst.status == StStatus::Running, "all: one of two done is not complete");
    check(allDone.tick() == StStatus::Succeeded, "all: both done completes");
}

static void triggers() {
    Fx t("OCST 1\nKEY Go bool agent false\nSTATE A\n  TASK Act s=a\n  ON tick TO B\n    IF bb Go\nEND\nSTATE B\n  TASK Act s=b\nEND\n");
    t.tick(); t.tick();
    check(t.leaf() == "A", "tick trigger: held back by a false IF");
    t.board.setBool("Go", true);
    t.tick();
    check(t.leaf() == "B", "...fires once the IF passes");
    check(t.inst.lastTo == t.a.findPath("B") && t.inst.lastFrom == t.a.findPath("A") && t.inst.lastTransitionTick == t.inst.tickCount, "lastFrom, lastTo and lastTransitionTick are recorded");

    Fx f("OCST 1\nSTATE A\n  TASK Act s=a p0=1 p1=2\n  ON failed TO C\nEND\nSTATE B\nEND\nSTATE C\n  TASK Act s=c\nEND\n");
    f.tick();
    check(f.leaf() == "C", "failed trigger: a failed task takes the ON failed transition");

    Fx c("OCST 1\nSTATE A\n  TASK Act s=a p0=1\n  ON completed TO C\nEND\nSTATE B\n  TASK Act s=b\nEND\nSTATE C\n  TASK Act s=c\nEND\n");
    c.tick();
    check(c.leaf() == "C", "completed trigger: explicit target beats the default NEXT");

    Fx e("OCST 1\nKEY Go bool agent true\nSTATE A\n  TASK Act s=a\n  ON event:Ping TO B\n    IF bb Go\nEND\nSTATE B\n  TASK Act s=b\nEND\n");
    e.tick();
    e.inst.sendEvent("Ping", 5, 2.5f);
    e.tick();
    check(e.leaf() == "B", "event trigger: sendEvent is seen by the next update");
    check(e.board.getInt("EventPayload") == 5 && e.board.getFloat("EventPayloadF") == 2.5f, "...and its payload lands in EventPayload / EventPayloadF");

    Fx once("OCST 1\nKEY Go bool agent false\nSTATE A\n  TASK Act s=a\n  ON event:Ping TO B\n    IF bb Go\nEND\nSTATE B\n  TASK Act s=b\nEND\n");
    once.tick();
    once.inst.sendEvent("Ping");
    once.tick();
    once.board.setBool("Go", true);
    once.tick();
    check(once.leaf() == "A", "an event is visible for exactly one update (it was consumed while the IF was false)");

    Fx cap("OCST 1\nSTATE A\n  TASK Act s=a\nEND\n");
    for (int i = 0; i < 20; ++i) cap.inst.sendEvent("E" + std::to_string(i));
    check(cap.inst.events.size() == 16, "the event queue holds 16");

    Fx wrong("OCST 1\nSTATE A\n  TASK Act s=a\n  ON event:Ping TO B\nEND\nSTATE B\n  TASK Act s=b\nEND\n");
    wrong.tick();
    wrong.inst.sendEvent("Pong");
    wrong.tick();
    check(wrong.leaf() == "A", "an event with another name does nothing");
}

static void priorityAndBubbling() {
    Fx p("OCST 1\nSTATE P\n  STATE C1\n    TASK Act s=c1\n    ON tick TO P.C2\n  END\n  STATE C2\n    TASK Act s=c2\n  END\n  ON tick TO Q\nEND\nSTATE Q\n  TASK Act s=q\nEND\n");
    p.tick();
    check(p.leaf() == "P.C2", "leaf-first: a child's tick transition beats its parent's");
    p.tick();
    check(p.leaf() == "Q", "...and the parent's applies once the child has none");

    Fx b("OCST 1\nSTATE A\n  TASK Act s=a p0=1\nEND\nSTATE B\n  TASK Act s=b p0=1\nEND\nSTATE C\n  TASK Act s=c p0=1\nEND\n");
    b.tick();
    check(b.leaf() == "B", "a complete leaf steps to the next sibling");
    b.tick();
    check(b.leaf() == "C", "...and on");
    check(b.tick() == StStatus::Succeeded, "the last root completing finishes the tree as Succeeded");
    check(b.tick() == StStatus::Running && b.leaf() == "B" && g_calls["a"] == 2, "ONFINISH restart (default): the next update starts again from the top");

    Fx s("OCST 1\nONFINISH stop\nSTATE A\n  TASK Act s=a p0=2\nEND\n");
    s.tick();
    check(s.tick() == StStatus::Succeeded, "ONFINISH stop: finishes");
    check(s.tick() == StStatus::Succeeded && g_calls["a"] == 2, "...and stays finished");
    s.inst.restart();
    check(s.tick() == StStatus::Running && g_calls["a"] == 3, "...until restart()");

    Fx par("OCST 1\nSTATE P\n  STATE K1\n    TASK Act s=k1 p0=1\n  END\n  STATE K2\n    TASK Act s=k2 p0=1\n  END\nEND\nSTATE Q\n  TASK Act s=q\nEND\n");
    par.tick();
    check(par.leaf() == "P.K2", "K1 completes: next sibling K2");
    par.tick();
    check(par.leaf() == "Q", "K2 completes with no next sibling: the parent completes and steps to Q");

    Fx fail("OCST 1\nSTATE K\n  TASK Act s=k p0=1 p1=2\nEND\n");
    check(fail.tick() == StStatus::Failed, "a failed leaf with no handler fails the tree");
    Fx handled("OCST 1\nSTATE P\n  STATE K\n    TASK Act s=k p0=1 p1=2\n  END\n  ON failed TO Q\nEND\nSTATE Q\n  TASK Act s=q\nEND\n");
    handled.tick();
    check(handled.leaf() == "Q", "a failed leaf fails its parent, whose ON failed handles it");

    // A transition into a state whose enter condition fails is skipped and the scan continues.
    Fx skip("OCST 1\nKEY Never bool agent false\nSTATE A\n  TASK Act s=a\n  ON tick TO B\n  ON tick TO C\nEND\nSTATE B\n  ENTER\n    IF bb Never\n  TASK Act s=b\nEND\nSTATE C\n  TASK Act s=c\nEND\n");
    skip.tick();
    check(skip.leaf() == "C", "a failed enter condition skips that transition; the next one fires");

    Fx succ("OCST 1\nSTATE A\n  TASK Act s=a\n  ON tick TO SUCCEEDED\nEND\n");
    check(succ.tick() == StStatus::Succeeded, "TO SUCCEEDED finishes the tree");
    Fx fl("OCST 1\nSTATE A\n  TASK Act s=a\n  ON tick TO FAILED\nEND\n");
    check(fl.tick() == StStatus::Failed, "TO FAILED fails the tree");
    Fx nx("OCST 1\nSTATE A\n  TASK Act s=a\n  ON tick TO NEXT\nEND\nSTATE B\n  TASK Act s=b\nEND\n");
    nx.tick();
    check(nx.leaf() == "B", "an explicit TO NEXT steps to the next sibling");
}

static void reentry() {
    Fx f("OCST 1\nSTATE P\n  TASK Act s=pt\n  STATE K\n    TASK Act s=kt\n    ON event:Again TO P.K\n    ON event:Reset TO P\n  END\nEND\n");
    f.tick(); f.tick();
    check(g_calls["pt"] == 2 && g_calls["kt"] == 2 && g_starts["pt"] == 1 && g_starts["kt"] == 1, "both states' tasks run each update");
    f.inst.sendEvent("Again");
    f.tick();
    f.tick();
    check(g_aborts["kt"] == 1 && g_starts["kt"] == 2, "re-entering the active state aborts its running task and starts it fresh");
    check(g_aborts["pt"] == 0 && g_starts["pt"] == 1 && f.in("P"), "...while the ancestor keeps running (not re-entered)");
    f.inst.sendEvent("Reset");
    f.tick();
    f.tick();
    check(g_aborts["pt"] == 1 && g_starts["pt"] == 2 && f.leaf() == "P.K", "a transition to an active ancestor re-enters it and its descendants");
}

static void evaluatorsAndLifecycle() {
    Fx e("OCST 1\nEVAL Act s=e1\nEVAL Act s=e2\nSTATE A\n  TASK Act s=t\nEND\n");
    e.tick(); e.tick();
    check(g_order == std::vector<std::string>({"e1", "e2", "t", "e1", "e2", "t"}), "evaluators run first, in order, every update");

    Fx s("OCST 1\nSTATE A\n  TASK Act s=a\nEND\n");
    s.tick();
    s.inst.stop();
    check(s.tick() == StStatus::Stopped && g_aborts["a"] == 1 && s.inst.active.empty(), "stop() exits the states, aborting running tasks");
    const int before = g_calls["a"];
    s.tick(); s.tick();
    check(g_calls["a"] == before && s.inst.status == StStatus::Stopped, "a stopped tree does nothing");
    s.inst.restart();
    check(s.tick() == StStatus::Running && g_calls["a"] == before + 1, "restart() brings it back");

    Fx ab("OCST 1\nSTATE A\n  TASK Act s=a\nEND\n");
    ab.tick();
    abortStateTree(ab.a, ab.reg, 1, ab.inst);
    check(g_aborts["a"] == 1 && ab.inst.status == StStatus::Stopped && ab.inst.active.empty(), "abortStateTree leaves every state and stops");
    check(ab.tick() == StStatus::Running && g_calls["a"] == 2, "...and the instance can start again");

    // A chain deeper than the 8-steps-per-update bubbling cap finishes over two updates.
    std::string deep = "OCST 1\n";
    for (int i = 0; i < 12; ++i) deep += "STATE S" + std::to_string(i) + "\n";
    deep += "TASK Act s=leaf p0=1\n";
    for (int i = 0; i < 12; ++i) deep += "END\n";
    Fx d(deep);
    check(d.tick() == StStatus::Running, "bubbling cap: a 12-deep completion climbs 8 levels in the first update (still running)");
    check(d.tick() == StStatus::Succeeded, "...and finishes in the second");
}

int main() {
    selection();
    tasksAndComplete();
    triggers();
    priorityAndBubbling();
    reentry();
    evaluatorsAndLifecycle();
    if (g_failures == 0) AVER_INFO("StateTreeTest: all passed");
    return g_failures;
}
