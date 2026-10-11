// BtSystem hosting StateTrees against a real scene::World: loadBrain by extension, setBrain attaching the
// component, a .ocst ticking under BtSystem::tick with lastStatus mapped, schema binding and team boards,
// the RunBehaviorTree / RunStateTree nesting leaves (and their depth and recursion guards), reloadTree,
// the blackboard relay's brain ops, the debug view and trace, pruning, and a plain BT unchanged.
#include "aver/synapse/SynapseBt.hpp"

#include "aver/core/Log.hpp"
#include "aver/scene/World.hpp"

#include <filesystem>
#include <fstream>
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

static void clearWorld(scene::World& w) {
    std::vector<scene::Entity> all;
    for (u32 i = 0; i < w.count(); ++i) all.push_back(w.at(i));
    for (const scene::Entity e : all) if (w.valid(e) && !w.destroyPending(e)) w.destroy(e);
    w.flush();
}

static std::map<std::string, int> g_calls, g_starts, g_aborts;
static BtStatus actAction(i32, const fmt::OcBtNode& n, f32, f32& elapsed, void*) {
    ++g_calls[n.stringParam];
    if (elapsed == 0.0f) ++g_starts[n.stringParam];
    elapsed += 1.0f;
    return BtStatus::Running;
}
static void actAbort(i32, const fmt::OcBtNode& n, void*) { ++g_aborts[n.stringParam]; }

static std::filesystem::path tempDir() {
    const std::filesystem::path d = std::filesystem::temp_directory_path() / "aver_synapse_query_test";
    std::filesystem::create_directories(d);
    return d;
}
static std::string writeFile(const std::string& name, const std::string& text) {
    const std::filesystem::path p = tempDir() / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
    return p.generic_string();
}
static std::string quotePath(const std::string& s) { return "\"" + s + "\""; }

static scene::Entity spawn(scene::World& w, const char* name) { return w.create(name); }

static void tickN(BtSystem& sys, scene::World& w, int n, f32 dt = 0.02f) {
    for (int i = 0; i < n; ++i) sys.tick(w, dt);
}

static void loading(BtSystem& sys, scene::World& w) {
    clearWorld(w);
    std::string why;
    BtAsset bt;
    BtAssetNode root;
    root.node.kind = fmt::OcBtNodeKind::Action;
    root.node.name = "Wait";
    root.node.params[0] = 0.05f;
    bt.nodes.push_back(root);
    const std::string btPath = (tempDir() / "sys_wait.ocbt").generic_string();
    check(saveBtAsset(btPath, bt, &why), "write a .ocbt: " + why);
    const std::string stPath = writeFile("sys_idle.ocst", "OCST 1\nSTATE A\n  TASK StAct s=idle\nEND\n");

    const u64 btId = sys.loadBrain(btPath), stId = sys.loadBrain(stPath);
    check(btId != 0 && sys.findTree(btId) != nullptr, "loadBrain: .ocbt loads as a behaviour tree");
    check(stId != 0 && sys.findStateTree(stId) != nullptr && sys.findTree(stId) == nullptr, "loadBrain: .ocst loads as a state tree");
    check(sys.loadBrain(btPath) == btId && sys.loadBrain(stPath) == stId, "...cached, same ids");
    check(sys.loadBrain("brain.txt") == 0 && sys.loadBrain("nothing.ocst") == 0, "any other extension, or a file that does not load, is 0");

    const scene::Entity e = spawn(w, "NPC");
    check(w.component<CSynapseBehavior>(e, sys.componentType()) == nullptr, "a fresh entity has no behaviour component");
    check(sys.setBrain(w, e, stPath), "setBrain on an entity without the component");
    const auto* b = w.component<CSynapseBehavior>(e, sys.componentType());
    check(b && b->treeAssetId == stId && sys.brainKind(e) == 2, "...attaches it and assigns the state tree");
    check(sys.setBrain(w, e, btPath) && sys.brainKind(e) == 1, "setBrain switches an entity to a behaviour tree");
    check(!sys.setBrain(w, e, "nothing.ocst") && sys.brainKind(e) == 1, "a failed setBrain changes nothing");
    check(sys.brainKind(spawn(w, "Plain")) == 0, "an entity with no brain has kind 0");
}

static void ticking(BtSystem& sys, scene::World& w) {
    clearWorld(w);
    sys.clearBlackboards();
    sys.clearStateTrees();
    g_calls.clear(); g_starts.clear(); g_aborts.clear();
    const std::string path = writeFile("sys_tick.ocst",
        "OCST 1\nONFINISH stop\nKEY Ammo int agent 7\nKEY Alert bool shared\nSTATE A\n  TASK Wait p0=0.05\n  TASK StAct s=bg\nEND\n");
    const scene::Entity e = spawn(w, "A");
    check(sys.setBrain(w, e, path), "assign");
    sys.tick(w, 0.02f);
    check(sys.brainStatus(e) == 1 && w.component<CSynapseBehavior>(e, sys.componentType())->lastStatus == 0, "Running maps to lastStatus 0 and brain status 1");
    Blackboard* board = sys.blackboard(e);
    check(board && board->getInt("Ammo") == 7, "the tree's KEYs became the board's schema, with their defaults");
    check(sys.activeStatePath(e) == "A" && sys.inState(e, "A") && !sys.inState(e, "B"), "activeStatePath and inState");
    tickN(sys, w, 3);
    // Wait finishes, but the background task keeps the state open: COMPLETE all.
    check(sys.brainStatus(e) == 1 && g_calls["bg"] == 4, "COMPLETE all: a still-running task keeps the tree running");

    const std::string done = writeFile("sys_done.ocst", "OCST 1\nONFINISH stop\nSTATE A\n  TASK Wait p0=0.05\nEND\n");
    const scene::Entity f = spawn(w, "B");
    sys.setBrain(w, f, done);
    tickN(sys, w, 4);
    check(sys.brainStatus(f) == 2 && w.component<CSynapseBehavior>(f, sys.componentType())->lastStatus == 1, "Succeeded maps to lastStatus 1 and brain status 2");
    const std::string failing = writeFile("sys_fail.ocst", "OCST 1\nONFINISH stop\nSTATE A\n  TASK Missing\nEND\n");
    const scene::Entity g = spawn(w, "C");
    sys.setBrain(w, g, failing);
    tickN(sys, w, 1);
    check(sys.brainStatus(g) == 3 && w.component<CSynapseBehavior>(g, sys.componentType())->lastStatus == 2, "Failed maps to lastStatus 2 and brain status 3 (an unregistered task fails)");

    // Shared keys live on the team board.
    const scene::Entity t1 = spawn(w, "T1"), t2 = spawn(w, "T2");
    sys.setBrain(w, t1, path);
    sys.setBrain(w, t2, path);
    sys.setTeam(w, t1, "red");
    sys.setTeam(w, t2, "red");
    sys.tick(w, 0.02f);
    sys.blackboard(t1)->setBool("Alert", true);
    check(sys.blackboard(t2)->getBool("Alert"), "a Shared KEY is on the team board both entities see");
    sys.blackboard(t1)->setInt("Ammo", 1);
    check(sys.blackboard(t2)->getInt("Ammo") == 7, "...while an agent KEY stays private");
}

static void nesting(BtSystem& sys, scene::World& w) {
    clearWorld(w);
    sys.clearBlackboards();
    sys.clearStateTrees();

    std::string why;
    BtAsset bt;
    BtAssetNode root;
    root.node.kind = fmt::OcBtNodeKind::Action;
    root.node.name = "Wait";
    root.node.params[0] = 0.05f;
    bt.nodes.push_back(root);
    const std::string waitBt = (tempDir() / "sys_nest_wait.ocbt").generic_string();
    saveBtAsset(waitBt, bt, &why);

    // A state tree whose task is a behaviour tree.
    const std::string hostSt = writeFile("sys_host.ocst", "OCST 1\nONFINISH stop\nSTATE A\n  TASK RunBehaviorTree s=" + quotePath(waitBt) + "\nEND\n");
    const scene::Entity e = spawn(w, "E");
    sys.setBrain(w, e, hostSt);
    tickN(sys, w, 2);
    check(sys.brainStatus(e) == 1, "a state tree running a behaviour tree: Running while the BT runs");
    tickN(sys, w, 3);
    check(sys.brainStatus(e) == 2, "...Succeeded when the BT succeeds");

    // A behaviour tree whose leaf is a state tree.
    const std::string waitSt = writeFile("sys_nest_wait.ocst", "OCST 1\nSTATE A\n  TASK Wait p0=0.05\nEND\n");
    BtAsset host;
    BtAssetNode hroot;
    hroot.node.kind = fmt::OcBtNodeKind::Action;
    hroot.node.name = "RunStateTree";
    hroot.node.stringParam = waitSt;
    host.nodes.push_back(hroot);
    const std::string hostBt = (tempDir() / "sys_nest_host.ocbt").generic_string();
    saveBtAsset(hostBt, host, &why);
    const scene::Entity f = spawn(w, "F");
    sys.setBrain(w, f, hostBt);
    sys.tick(w, 0.02f);
    check(sys.brainStatus(f) == 1, "a behaviour tree running a state tree: Running");
    bool succeeded = false;
    for (int i = 0; i < 6 && !succeeded; ++i) {
        sys.tick(w, 0.02f);
        succeeded = w.component<CSynapseBehavior>(f, sys.componentType())->lastStatus == 1;
    }
    check(succeeded, "...Success when the nested tree succeeds");

    // Nesting deeper than 8 fails the leaf.
    const int kChain = 11;
    std::vector<std::string> chain;
    for (int i = 0; i < kChain; ++i) chain.push_back((tempDir() / ("sys_chain" + std::to_string(i) + ".ocst")).generic_string());
    for (int i = 0; i < kChain; ++i) {
        const std::string task = i + 1 < kChain ? "RunStateTree s=" + quotePath(chain[static_cast<usize>(i + 1)]) : "Wait p0=0.05";
        writeFile("sys_chain" + std::to_string(i) + ".ocst", "OCST 1\nONFINISH stop\nSTATE A\n  TASK " + task + "\nEND\n");
    }
    const scene::Entity g = spawn(w, "G");
    sys.setBrain(w, g, chain[0]);
    sys.tick(w, 0.02f);
    check(sys.brainStatus(g) == 3, "an 11-deep chain of RunStateTree: the leaf at depth 8 fails and the failure climbs to the top");

    // A tree that runs itself fails instead of recursing.
    const std::string self = (tempDir() / "sys_self.ocst").generic_string();
    writeFile("sys_self.ocst", "OCST 1\nONFINISH stop\nSTATE A\n  TASK RunStateTree s=" + quotePath(self) + "\nEND\n");
    const scene::Entity h = spawn(w, "H");
    sys.setBrain(w, h, self);
    sys.tick(w, 0.02f);
    check(sys.brainStatus(h) == 3, "a state tree that runs itself fails (no re-entry)");
}

static void reloading(BtSystem& sys, scene::World& w) {
    clearWorld(w);
    sys.clearBlackboards();
    sys.clearStateTrees();
    g_calls.clear(); g_starts.clear(); g_aborts.clear();
    const std::string path = writeFile("sys_reload.ocst", "OCST 1\nKEY One int agent 1\nSTATE A\n  TASK StAct s=v1\nEND\n");
    const scene::Entity e = spawn(w, "E");
    sys.setBrain(w, e, path);
    tickN(sys, w, 2);
    check(g_calls["v1"] == 2 && sys.blackboard(e)->has("One"), "v1 runs");
    writeFile("sys_reload.ocst", "OCST 1\nKEY Two int agent 2\nSTATE A\n  TASK StAct s=v2\nEND\n");
    check(sys.reloadTree(path), "reloadTree re-reads the file");
    check(g_aborts["v1"] == 1, "...the running task of the old tree was aborted");
    sys.tick(w, 0.02f);
    check(g_calls["v2"] == 1 && g_calls["v1"] == 2, "...the entity runs the new tree from its start");
    check(sys.blackboard(e)->has("Two") && sys.blackboard(e)->getInt("Two") == 2, "...and its schema is applied to the board");
    writeFile("sys_reload.ocst", "OCST 1\nSTATE A\n  BOGUS\nEND\n");
    check(!sys.reloadTree(path), "a file that no longer parses is refused");
    sys.tick(w, 0.02f);
    check(g_calls["v2"] == 2, "...and the old tree keeps running");
}

static void relay(BtSystem& sys, scene::World& w) {
    clearWorld(w);
    sys.clearBlackboards();
    sys.clearStateTrees();
    g_calls.clear(); g_starts.clear(); g_aborts.clear();
    const std::string path = writeFile("sys_relay.ocst",
        "OCST 1\nSTATE P\n  STATE K\n    TASK StAct s=k\n    ON event:Go TO P.L\n  END\n  STATE L\n    TASK StAct s=l\n  END\nEND\n");
    const scene::Entity e = spawn(w, "E");
    const i32 ent = static_cast<i32>(e);
    i64 iv = 0;
    f32 fv[3] = {};
    char text[64] = {};

    check(blackboardRelay(kBbRelayBrainSet, ent, path.c_str(), 0, nullptr, nullptr, nullptr, 0, nullptr) == 1, "op 6: set the brain");
    check(blackboardRelay(kBbRelayBrainKind, ent, "", 0, &iv, nullptr, nullptr, 0, nullptr) == 1 && iv == 2, "op 7: kind is a state tree");
    check(blackboardRelay(kBbRelayBrainStatus, ent, "", 0, &iv, nullptr, nullptr, 0, nullptr) == 1 && iv == 0, "op 11: status is idle before the first tick");
    sys.tick(w, 0.02f);
    check(blackboardRelay(kBbRelayBrainStatus, ent, "", 0, &iv, nullptr, nullptr, 0, nullptr) == 1 && iv == 1, "...running after it");
    check(blackboardRelay(kBbRelayBrainState, ent, "", 0, nullptr, nullptr, text, 64, nullptr) == 1 && std::string(text) == "P.K", "op 9: the active leaf path");
    check(blackboardRelay(kBbRelayBrainInState, ent, "P", 0, nullptr, nullptr, nullptr, 0, nullptr) == 1, "op 10: an ancestor is in the chain");
    check(blackboardRelay(kBbRelayBrainInState, ent, "P.K", 0, nullptr, nullptr, nullptr, 0, nullptr) == 1, "...and the leaf");
    check(blackboardRelay(kBbRelayBrainInState, ent, "P.L", 0, nullptr, nullptr, nullptr, 0, nullptr) == 0, "...but not a state that is not active");
    iv = 9;
    fv[0] = 1.5f;
    check(blackboardRelay(kBbRelayBrainEvent, ent, "Go", 0, &iv, fv, nullptr, 0, nullptr) == 1, "op 8: send an event with a payload");
    sys.tick(w, 0.02f);
    check(sys.activeStatePath(e) == "P.L" && sys.blackboard(e)->getInt("EventPayload") == 9 && sys.blackboard(e)->getFloat("EventPayloadF") == 1.5f, "...it moves the tree and writes the payload keys");
    check(g_aborts["k"] == 1, "...aborting the task of the state it left");
    // L was entered at the end of that update (its task idle, not Running); one more update starts it.
    sys.tick(w, 0.02f);
    check(g_calls["l"] == 1, "...the new state's task runs on the next update");
    check(blackboardRelay(kBbRelayBrainStop, ent, "", 0, nullptr, nullptr, nullptr, 0, nullptr) == 1, "op 13: stop");
    sys.tick(w, 0.02f);
    check(blackboardRelay(kBbRelayBrainStatus, ent, "", 0, &iv, nullptr, nullptr, 0, nullptr) == 1 && iv == 0 && g_aborts["l"] == 1, "...exits the states (aborting tasks) and reads idle");
    const int callsStopped = g_calls["l"];
    sys.tick(w, 0.02f);
    check(g_calls["l"] == callsStopped, "a stopped brain does nothing");
    check(blackboardRelay(kBbRelayBrainRestart, ent, "", 0, nullptr, nullptr, nullptr, 0, nullptr) == 1, "op 12: restart");
    sys.tick(w, 0.02f);
    check(sys.activeStatePath(e) == "P.K", "...starts again from the top");
    check(blackboardRelay(kBbRelayBrainEvent, 0x7FFFFFF0, "Go", 0, &iv, fv, nullptr, 0, nullptr) == 0, "a dead entity answers 0");
    const scene::Entity plain = spawn(w, "Plain");
    check(blackboardRelay(kBbRelayBrainEvent, static_cast<i32>(plain), "Go", 0, &iv, fv, nullptr, 0, nullptr) == 0, "an event for an entity with no state tree answers 0");
    check(blackboardRelay(kBbRelayBrainKind, static_cast<i32>(plain), "", 0, &iv, nullptr, nullptr, 0, nullptr) == 1 && iv == 0, "its kind is 0");
}

static void debugAndPrune(BtSystem& sys, scene::World& w) {
    clearWorld(w);
    sys.clearBlackboards();
    sys.clearStateTrees();
    g_calls.clear(); g_starts.clear(); g_aborts.clear();
    const std::string path = writeFile("sys_debug.ocst", "OCST 1\nSTATE A\n  TASK StAct s=d\nEND\n");
    const scene::Entity e = spawn(w, "E");
    sys.setBrain(w, e, path);
    sys.watch(e);
    sys.tick(w, 0.02f);
    StateTreeDebugView v;
    check(sys.stateTreeDebugView(e, v) && v.tree && v.state && v.board, "the debug view carries tree, instance and board");
    check(v.state && v.state->trace && v.state->traceTasks.size() > 0 && v.state->traceTasks[0].size() == 1 && v.state->traceTasks[0][0] == 0, "the watched entity records the last status of each task (0 = running)");
    check(v.state && v.state->tickCount == 1, "tickCount advances");
    sys.watch(scene::kInvalidEntity);
    sys.tick(w, 0.02f);
    sys.stateTreeDebugView(e, v);
    check(v.state && !v.state->trace, "an unwatched entity records no trace");

    w.destroy(e);
    w.flush();
    sys.tick(w, 0.02f);
    check(g_aborts["d"] == 1, "pruning an entity that is gone aborts its running tasks");
    check(!sys.stateTreeDebugView(e, v), "...and forgets its instance");
}

static void plainBt(BtSystem& sys, scene::World& w) {
    clearWorld(w);
    sys.clearBlackboards();
    sys.clearStateTrees();
    const std::string path = (tempDir() / "sys_plain.ocbt").generic_string();
    BtAsset bt;
    BtAssetNode root;
    root.node.kind = fmt::OcBtNodeKind::Action;
    root.node.name = "Wait";
    root.node.params[0] = 0.05f;
    bt.nodes.push_back(root);
    saveBtAsset(path, bt, nullptr);
    const scene::Entity e = spawn(w, "E");
    sys.setBrain(w, e, path);
    sys.tick(w, 0.02f);
    check(sys.brainKind(e) == 1 && sys.brainStatus(e) == 1 && sys.runningCount() == 1, "a behaviour tree still ticks and reports Running");
    tickN(sys, w, 3);
    check(sys.brainStatus(e) == 1 || sys.brainStatus(e) == 2, "...and finishes or restarts as before");
    check(sys.brainStop(e) && sys.brainStatus(e) == 0, "brainStop on a behaviour tree");
    sys.tick(w, 0.02f);
    check(sys.brainStatus(e) == 0, "a stopped behaviour tree is not ticked");
    check(sys.brainRestart(e), "brainRestart brings it back");
    sys.tick(w, 0.02f);
    check(sys.brainStatus(e) == 1, "...running again");
    check(!sys.stateTreeEvent(e, "Go", 0, 0.0f), "events are for state trees only");
}

int main() {
    AVER_INFO("StateTreeSystemTest");
    scene::World& w = scene::World::instance();
    BtSystem& sys = btSystem();
    check(sys.registerComponents(w) != 0, "CSynapseBehavior registers");
    perceptionSystem().registerComponents(w);
    registerBuiltinBehaviors(sys);
    sys.registry().registerAction("StAct", &actAction, nullptr, &actAbort);

    loading(sys, w);
    ticking(sys, w);
    nesting(sys, w);
    reloading(sys, w);
    relay(sys, w);
    debugAndPrune(sys, w);
    plainBt(sys, w);
    if (g_failures == 0) AVER_INFO("StateTreeSystemTest: all passed");
    return g_failures;
}
