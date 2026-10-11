// The .ocst asset: the documented guard brain parses with every field in place, each refusal names its
// line, validation warns without refusing, the writer is exact, and the structural edit helpers keep the
// asset valid. Pure.
#include "aver/synapse/StateTreeAsset.hpp"

#include "aver/core/Log.hpp"

#include <string>

using namespace aver;
using namespace aver::synapse;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

// The section 5.2 example. (The design doc's listing leaves Patrol without its END; every state closes here.)
static const char* kGuard =
    "OCST 1\n"
    "DESC \"Guard brain\"\n"
    "ONFINISH restart\n"
    "KEY Alert bool agent false\n"
    "KEY Target entity agent\n"
    "EVAL UpdateThreat p0=1500\n"
    "STATE Patrol\n"
    "  ENTER\n"
    "    IF bb !Alert\n"
    "  TASK PatrolStep p0=3\n"
    "  ON event:Alarm TO Combat\n"
    "END\n"
    "STATE Combat\n"
    "  COMPLETE none\n"
    "  TASK RunBehaviorTree s=\"Content/AI/Combat.ocbt\"\n"
    "  STATE Chase\n"
    "    ENTER\n"
    "      IF cond CanSeeTarget\n"
    "    TASK MoveTo p0=0 p1=0 p2=0\n"
    "    ON tick TO Combat.Search\n"
    "      IF bb !CanSeeTarget\n"
    "  END\n"
    "  STATE Search\n"
    "    TASK Wait p0=4\n"
    "    ON completed TO Patrol\n"
    "  END\n"
    "  ON failed TO Patrol\n"
    "END\n";

static StateTreeAsset guard() {
    StateTreeAsset a;
    std::string why;
    if (!parseStateTree(kGuard, "guard.ocst", a, &why)) {
        ++g_failures;
        AVER_ERROR("  FAIL  the guard example did not parse: {}", why);
    }
    return a;
}

static std::string refusal(const std::string& text) {
    StateTreeAsset a;
    std::string why;
    if (parseStateTree(text, "t.ocst", a, &why)) return "";
    return why + ((a.states.empty() && a.roots.empty()) ? "" : " [not empty]");
}

static bool refusedAt(const std::string& text, int line, const char* needle = nullptr) {
    const std::string why = refusal(text);
    if (why.rfind("t.ocst:" + std::to_string(line) + ":", 0) != 0) {
        AVER_ERROR("    got '{}'", why);
        return false;
    }
    return !needle || why.find(needle) != std::string::npos;
}

// Every Goto resolves to the state its path names, and the text round-trips.
static bool consistent(const StateTreeAsset& a, std::string* problem = nullptr) {
    for (usize s = 0; s < a.states.size(); ++s) {
        const StState& st = a.states[s];
        if (st.parent >= 0) {
            bool listed = false;
            for (const i32 c : a.states[static_cast<usize>(st.parent)].children) listed = listed || c == static_cast<i32>(s);
            if (!listed || st.parent >= static_cast<i32>(s)) { if (problem) *problem = "parent link of " + st.name; return false; }
        }
        for (const StTransition& t : st.transitions) {
            if (t.target != StTargetKind::Goto) continue;
            if (t.targetState < 0 || a.statePath(t.targetState) != t.targetPath) { if (problem) *problem = "goto in " + st.name; return false; }
        }
    }
    const std::string w = writeStateTree(a);
    StateTreeAsset b;
    std::string why;
    if (!parseStateTree(w, "again", b, &why)) { if (problem) *problem = "reparse: " + why; return false; }
    if (writeStateTree(b) != w) { if (problem) *problem = "second write differs"; return false; }
    return true;
}

static void parseExample() {
    StateTreeAsset a = guard();
    check(a.desc == "Guard brain" && a.restartOnFinish, "DESC and ONFINISH restart");
    check(a.schema.keys.size() == 2 && a.schema.keys[0].name == "Alert" && a.schema.keys[0].type == BbType::Bool && a.schema.keys[1].type == BbType::Entity, "KEYs become the schema");
    check(a.evaluators.size() == 1 && a.evaluators[0].name == "UpdateThreat" && a.evaluators[0].params[0] == 1500, "EVAL");
    check(a.states.size() == 4 && a.roots.size() == 2, "four states in two roots");
    check(a.states[0].name == "Patrol" && a.states[1].name == "Combat" && a.states[2].name == "Chase" && a.states[3].name == "Search", "states are in pre-order");
    check(a.states[2].parent == 1 && a.states[1].children.size() == 2 && a.states[1].children[0] == 2, "nesting");
    const StState& patrol = a.states[0];
    check(patrol.enter.size() == 1 && patrol.enter[0].isBlackboard && patrol.enter[0].bb.key == "Alert" && patrol.enter[0].bb.op == BbOp::NotSet, "ENTER with IF bb !Alert");
    check(patrol.tasks.size() == 1 && patrol.tasks[0].name == "PatrolStep" && patrol.tasks[0].params[0] == 3, "TASK with p0");
    check(patrol.transitions.size() == 1 && patrol.transitions[0].trigger == StTrigger::Event && patrol.transitions[0].event == "Alarm" &&
              patrol.transitions[0].target == StTargetKind::Goto && patrol.transitions[0].targetState == 1, "ON event:Alarm TO Combat resolves");
    const StState& combat = a.states[1];
    check(combat.completeExplicit && combat.complete == StComplete::None && stEffectiveComplete(combat) == StComplete::None, "COMPLETE none");
    check(combat.tasks[0].stringParam == "Content/AI/Combat.ocbt", "a task's s= string");
    check(combat.transitions.size() == 1 && combat.transitions[0].trigger == StTrigger::Failed && combat.transitions[0].targetState == 0, "ON failed TO Patrol");
    const StState& chase = a.states[2];
    check(chase.enter.size() == 1 && !chase.enter[0].isBlackboard && chase.enter[0].leaf.name == "CanSeeTarget" && chase.enter[0].leaf.kind == fmt::OcBtNodeKind::Condition, "IF cond <Leaf>");
    check(chase.transitions[0].trigger == StTrigger::Tick && chase.transitions[0].targetState == 3 && chase.transitions[0].conds.size() == 1 &&
              chase.transitions[0].conds[0].bb.key == "CanSeeTarget", "ON tick TO Combat.Search with its IF");
    check(stEffectiveComplete(patrol) == StComplete::All && stEffectiveComplete(a.states[3]) == StComplete::All, "COMPLETE defaults to all when there are tasks");
    check(a.statePath(3) == "Combat.Search" && a.findPath("Combat.Chase") == 2 && a.findPath("Nope") == -1 && a.findPath("Combat.Nope") == -1, "statePath and findPath");

    StateTreeAsset b;
    std::string why;
    check(parseStateTree("OCST 1\nONFINISH stop\nSTATE G\n  GROUP\n  DISABLED\n  COMMENT \"a note\"\n  COMPLETE any\n  STATE K\n  END\nEND\n", "b", b, &why), "GROUP, DISABLED, COMMENT, COMPLETE any parse: " + why);
    check(!b.restartOnFinish && b.states[0].group && b.states[0].disabled && b.states[0].comment == "a note" && b.states[0].complete == StComplete::Any, "...with their values");
    check(parseStateTree("OCST 1\nKEY V vec3 shared 1,2,3\nKEY S string agent \"two words\"\nKEY N int agent -4\nSTATE A\nEND\n", "k", b, &why), "KEY defaults of several types: " + why);
    check(b.schema.keys[0].scope == BbScope::Shared && b.schema.keys[0].defaultValue.v.z == 3 && b.schema.keys[1].defaultValue.s == "two words" && b.schema.keys[2].defaultValue.i == -4, "...are typed");
    check(parseStateTree("OCST 1\nSTATE A\n  ON completed TO SUCCEEDED\n  ON failed TO FAILED\n  ON tick TO NEXT\nEND\n", "k", b, &why) &&
              b.states[0].transitions[0].target == StTargetKind::Succeeded && b.states[0].transitions[1].target == StTargetKind::Failed && b.states[0].transitions[2].target == StTargetKind::Next,
          "SUCCEEDED, FAILED and NEXT targets");
    check(parseStateTree("OCST 1\nKEY Ammo int agent 0\nSTATE A\n  ENTER\n    IF not bb Ammo >= 3\n    IF not cond Ready p1=2 s=\"x y\"\nEND\n", "k", b, &why), "IF not ...: " + why);
    check(b.states[0].enter[0].negate && b.states[0].enter[0].bb.op == BbOp::Ge && b.states[0].enter[0].bb.value.i == 3 && b.states[0].enter[1].negate &&
              b.states[0].enter[1].leaf.params[1] == 2 && b.states[0].enter[1].leaf.stringParam == "x y",
          "...negation, comparison value and leaf params");
}

static void refusals() {
    check(refusedAt("OCST 1\nSTATE A\n  ON tick TO Nowhere\nEND\n", 3, "does not name a state"), "an unresolvable TO path names the ON line");
    check(refusedAt("OCST 1\nSTATE A\nEND\nSTATE A\nEND\n", 4, "duplicate state"), "a duplicate root");
    check(refusedAt("OCST 1\nSTATE A\n  STATE B\n  END\n  STATE B\n  END\nEND\n", 5, "duplicate state"), "a duplicate sibling");
    check(refusedAt("OCST 1\nSTATE A\n  IF bb X\nEND\n", 3, "IF outside"), "IF outside ENTER / ON");
    check(refusedAt("OCST 1\nIF bb X\n", 2), "IF at top level");
    check(refusedAt("OCST 1\nSTATE A\n  TASK Leaf\n", 2, "not closed"), "an unclosed STATE names the STATE line");
    check(refusedAt("OCST 1\nKEY Ammo int agent 0\nSTATE A\n  ENTER\n    IF bb Ammo ?? 3\nEND\n", 5, "bad expression"), "a bad blackboard expression");
    check(refusedAt("OCST 1\nSTATE NEXT\nEND\n", 2, "bad state name"), "NEXT is reserved");
    check(refusedAt("OCST 1\nSTATE SUCCEEDED\nEND\n", 2, "bad state name"), "SUCCEEDED is reserved");
    check(refusedAt("OCST 1\nSTATE 9x\nEND\n", 2, "bad state name"), "a bad state name");
    check(refusedAt("OCST 1\nSTATE A\n  TASK Leaf p4=1\nEND\n", 3, "p index outside 0..3"), "p-index outside 0..3");
    check(refusedAt("OCST 1\nSTATE A\n  TASK Leaf q=1\nEND\n", 3, "unknown key"), "an unknown task key");
    check(refusedAt("OCST 1\nSTATE A\n  TASK 9Leaf\nEND\n", 3, "bad leaf name"), "a bad leaf name");
    check(refusedAt("OCST 1\nSTATE A\n  ON sometimes TO A\nEND\n", 3, "unknown trigger"), "an unknown trigger");
    check(refusedAt("OCST 1\nSTATE A\n  ON tick A\nEND\n", 3, "ON needs"), "ON without TO");
    check(refusedAt("OCST 1\nSTATE A\n  COMPLETE sometimes\nEND\n", 3, "COMPLETE must be"), "an unknown COMPLETE");
    check(refusedAt("OCST 1\nONFINISH maybe\nSTATE A\nEND\n", 2, "ONFINISH must be"), "an unknown ONFINISH");
    check(refusedAt("OCST 1\nEND\n", 2, "END without STATE"), "END without STATE");
    check(refusedAt("OCST 1\nSTATE A\nEND\nKEY X int agent\n", 4, "before the first STATE"), "KEY after a STATE");
    check(refusedAt("OCST 1\nKEY X int agent\nKEY X bool agent\nSTATE A\nEND\n", 3, "duplicate key"), "a duplicate key");
    check(refusedAt("OCST 1\nKEY X int middle\n", 2, "scope"), "a bad key scope");
    check(refusedAt("OCST 1\nKEY X float agent abc\n", 2, "bad default"), "a bad key default");
    check(refusedAt("OCST 1\nTASK Leaf\n", 2, "outside a STATE"), "TASK outside a STATE");
    check(refusedAt("OCST 1\nFLY\n", 2, "unknown record"), "an unknown record");
    check(refusedAt("OCST 2\n", 1, "unsupported version"), "a wrong version");

    // Depth: 16 nested states are fine, 17 are not.
    std::string deep = "OCST 1\n";
    for (int i = 0; i < 16; ++i) deep += "STATE S" + std::to_string(i) + "\n";
    for (int i = 0; i < 16; ++i) deep += "END\n";
    check(refusal(deep).empty(), "16 levels of nesting are allowed");
    std::string deeper = "OCST 1\n";
    for (int i = 0; i < 17; ++i) deeper += "STATE S" + std::to_string(i) + "\n";
    for (int i = 0; i < 17; ++i) deeper += "END\n";
    check(refusal(deeper).find("t.ocst:18:") == 0 && refusal(deeper).find("deeper than 16") != std::string::npos, "the 17th level is refused");

    // Counts: 256 states, 16 tasks, 16 transitions.
    std::string wide = "OCST 1\n";
    for (int i = 0; i < 256; ++i) wide += "STATE W" + std::to_string(i) + "\nEND\n";
    check(refusal(wide).empty(), "256 states are allowed");
    wide += "STATE Overflow\nEND\n";
    check(refusal(wide).find("more than 256 states") != std::string::npos, "the 257th state is refused");
    std::string tasks = "OCST 1\nSTATE A\n";
    for (int i = 0; i < 16; ++i) tasks += "  TASK T" + std::to_string(i) + "\n";
    check(refusal(tasks + "END\n").empty(), "16 tasks are allowed");
    check(refusal(tasks + "  TASK More\nEND\n").find("more than 16 tasks") != std::string::npos, "the 17th task is refused");
    std::string trans = "OCST 1\nSTATE A\n";
    for (int i = 0; i < 16; ++i) trans += "  ON tick TO A\n";
    check(refusal(trans + "END\n").empty(), "16 transitions are allowed");
    check(refusal(trans + "  ON tick TO A\nEND\n").find("more than 16 transitions") != std::string::npos, "the 17th transition is refused");

    StateTreeAsset a;
    std::string why;
    check(!parseStateTree("OCST 1\nSTATE A\n  ON tick TO Nowhere\nEND\n", "x", a, &why) && a.states.empty() && a.roots.empty(), "a failed parse leaves the output empty");
}

static void validation() {
    StateTreeAsset a = guard();
    BtRegistry reg;
    const auto act = [](i32, const fmt::OcBtNode&, f32, f32&, void*) { return BtStatus::Success; };
    const auto cond = [](i32, const fmt::OcBtNode&, void*) { return true; };
    reg.registerAction("UpdateThreat", act);
    reg.registerAction("PatrolStep", act);
    reg.registerAction("RunBehaviorTree", act);
    reg.registerAction("MoveTo", act);
    reg.registerAction("Wait", act);
    reg.registerCondition("CanSeeTarget", cond);
    check(validateStateTree(a, &reg).empty(), "a fully registered tree has no warnings");
    check(validateStateTree(a).empty(), "without a registry, leaf names are not checked");

    BtRegistry partial;
    partial.registerAction("Wait", act);
    const auto w = validateStateTree(a, &partial);
    bool unknownTask = false, unknownCond = false, unknownEval = false;
    for (const std::string& s : w) {
        unknownTask = unknownTask || s.find("task 'PatrolStep'") != std::string::npos;
        unknownCond = unknownCond || s.find("condition 'CanSeeTarget'") != std::string::npos;
        unknownEval = unknownEval || s.find("'UpdateThreat'") != std::string::npos;
    }
    check(unknownTask && unknownCond && unknownEval, "unknown tasks, conditions and evaluators are warnings, not refusals");

    StateTreeAsset b;
    std::string why;
    check(parseStateTree("OCST 1\nSTATE A\n  ENTER\n    IF bb Mystery\n  ON tick TO B\nEND\nSTATE B\n  DISABLED\nEND\n", "w", b, &why), "parse: " + why);
    const auto w2 = validateStateTree(b);
    bool undeclared = false, disabled = false;
    for (const std::string& s : w2) {
        undeclared = undeclared || s.find("'Mystery'") != std::string::npos;
        disabled = disabled || s.find("disabled state B") != std::string::npos;
    }
    check(undeclared && disabled, "an undeclared key and a transition into a disabled state are warnings");
    check(parseStateTree("OCST 1\nSTATE A\n  ENTER\n    IF bb Target\n    IF bb EventPayload\nEND\n", "w", b, &why) && validateStateTree(b).empty(), "reserved keys (Target, EventPayload, ...) need no declaration");
}

static void writer() {
    StateTreeAsset a = guard();
    const std::string w1 = writeStateTree(a);
    StateTreeAsset b;
    std::string why;
    check(parseStateTree(w1, "g", b, &why), "the written text parses: " + why);
    check(writeStateTree(b) == w1, "a second write is byte-identical");
    check(w1.rfind("OCST 1\n", 0) == 0 && w1.find("DESC \"Guard brain\"") != std::string::npos, "header and DESC");
    check(w1.find("ONFINISH") == std::string::npos, "ONFINISH restart is the default and is omitted");
    check(w1.find("TO Combat.Search") != std::string::npos && w1.find("IF bb !CanSeeTarget") != std::string::npos && w1.find("IF cond CanSeeTarget") != std::string::npos, "targets are dotted paths and conditions read as written");
    check(b.states.size() == a.states.size() && b.states[2].transitions[0].targetState == 3, "structure survives");

    StateTreeAsset c;
    check(parseStateTree("OCST 1\nONFINISH stop\nKEY F float agent 0.25\nKEY V vec3 agent 1.5,0,-2\nSTATE G\n  GROUP\n  COMPLETE any\n  COMMENT \"two words\"\n  STATE K\n    TASK T s=\"a b\" p2=-1.5\n    ON event:Go TO SUCCEEDED\n      IF not cond C p0=1\n  END\nEND\n", "c", c, &why), "an odd tree parses: " + why);
    const std::string c1 = writeStateTree(c);
    StateTreeAsset d;
    check(parseStateTree(c1, "c", d, &why) && writeStateTree(d) == c1, "the odd tree's second write is byte-identical");
    check(!d.restartOnFinish && d.states[0].group && d.states[1].tasks[0].params[2] == -1.5f && d.states[1].transitions[0].conds[0].negate, "...and its fields survive");
    check(consistent(stStarter()), "the starter tree is valid and exact");
}

static void edits() {
    std::string problem;
    StateTreeAsset a = guard();
    const i32 flee = stAddState(a, 1, "Flee");
    check(flee >= 0 && a.states.size() == 5 && a.findPath("Combat.Flee") == flee && a.states[1].children.size() == 3, "add: a new last child of Combat");
    check(consistent(a, &problem), "add: still consistent: " + problem);
    check(stAddState(a, 1, "Flee") == -1 && stAddState(a, 1, "bad name") == -1, "add refuses a duplicate or invalid name");
    const i32 auto1 = stAddState(a, -1, "");
    check(auto1 >= 0 && a.states[static_cast<usize>(auto1)].name == "State" && a.roots.size() == 3, "add with no name picks 'State' as a new root");
    check(stAddState(a, -1, "") >= 0 && a.findPath("State2") >= 0, "...and 'State2' next");
    check(consistent(a, &problem), "still consistent: " + problem);

    a = guard();
    check(stRenameState(a, 1, "Fight"), "rename Combat");
    check(a.states[0].transitions[0].targetPath == "Fight" && a.states[2].transitions[0].targetPath == "Fight.Search", "rename rewrites every Goto path into the renamed subtree");
    check(writeStateTree(a).find("TO Fight.Search") != std::string::npos && consistent(a, &problem), "...in the written text too: " + problem);
    check(!stRenameState(a, 1, "Patrol") && !stRenameState(a, 1, "NEXT") && !stRenameState(a, 1, "has space"), "rename refuses a sibling name, a reserved name and a bad name");

    a = guard();
    i32 retargeted = 0;
    check(stDeleteState(a, 3, &retargeted) && a.states.size() == 3 && retargeted == 1, "delete Search: one transition into it is retargeted");
    check(a.states[2].transitions[0].target == StTargetKind::Next && a.findPath("Combat.Search") == -1, "...to NEXT");
    check(consistent(a, &problem), "delete: consistent: " + problem);
    a = guard();
    check(stDeleteState(a, 1, &retargeted) && a.states.size() == 1 && retargeted == 1 && a.states[0].transitions[0].target == StTargetKind::Next, "delete Combat takes its subtree; Patrol's event transition into it becomes NEXT");
    check(consistent(a, &problem), "delete subtree: consistent: " + problem);
    check(!stDeleteState(a, 7), "delete refuses a bad index");

    a = guard();
    check(stMoveState(a, 0, 1) == 3 && a.states[0].name == "Combat" && a.states[a.roots[1]].name == "Patrol", "move Patrol after Combat");
    check(consistent(a, &problem), "move: Gotos follow their targets: " + problem);
    check(stMoveState(a, a.roots[1], 1) == -1, "move past the end is refused");
    check(stMoveState(a, a.roots[1], -5) == 0 && a.states[0].name == "Patrol", "move clamps to the first place");
    check(consistent(a, &problem), "move: consistent: " + problem);

    a = guard();
    const i32 moved = stReparent(a, 3, 2);   // Search under Chase
    check(moved >= 0 && a.findPath("Combat.Chase.Search") == moved, "reparent Search under Chase");
    check(a.states[a.findPath("Combat.Chase")].transitions[0].targetPath == "Combat.Chase.Search", "...and Chase's Goto into it follows");
    check(consistent(a, &problem), "reparent: consistent: " + problem);
    check(stReparent(a, 1, a.findPath("Combat.Chase.Search")) == -1 && stReparent(a, 1, 1) == -1, "reparent into itself or a descendant is refused");
    a = guard();
    stAddState(a, 0, "Search");
    check(stReparent(a, a.findPath("Combat.Search"), 0) == -1, "reparent refuses a sibling name clash");

    const StateTreeAsset s = stStarter();
    check(s.states.size() == 2 && s.states[0].name == "Idle" && s.states[1].name == "Active" && s.states[0].transitions[0].targetState == 1, "the starter: Idle -> Active on a Wait");
    check(s.states[0].tasks[0].name == "Wait", "...");
}

int main() {
    parseExample();
    refusals();
    validation();
    writer();
    edits();
    if (g_failures == 0) AVER_INFO("StateTreeAssetTest: all passed");
    return g_failures;
}
