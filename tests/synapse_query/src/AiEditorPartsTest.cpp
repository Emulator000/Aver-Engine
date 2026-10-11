// The AI editors' headless cores (compiled with AVER_WITH_IMGUI undefined, like BtGraphEditorTest): the
// shared tree layout against a verbatim copy of the layout the BT editor used before it was extracted,
// the live-value flash bookkeeping, the content-relative path helper, the StateTree editor's edit
// bookkeeping / undo / save / load, and the EQS editor's option and test edits plus "Run on selection"
// against a registered in-memory query.
#include "AiEditorParts.hpp"
#include "BtGraphEditor.hpp"
#include "EqsEditor.hpp"
#include "StateTreeEditor.hpp"

#include "aver/core/Log.hpp"
#include "aver/scene/World.hpp"
#include "aver/synapse/SynapseAi.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::editor;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static std::filesystem::path tempDir() {
    const std::filesystem::path d = std::filesystem::temp_directory_path() / "aver_synapse_query_editor_test";
    std::filesystem::create_directories(d);
    return d;
}
static std::string writeFile(const std::string& name, const std::string& text) {
    const std::filesystem::path p = tempDir() / name;
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
    return p.generic_string();
}
static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---- the layout the BT editor used before the extraction (verbatim logic) ------------------------------

static f32 refNodeHeight(const synapse::BtAssetNode& n, const BtLayoutParams& p) {
    f32 h = p.headerH + p.padY;
    h += p.lineH * static_cast<f32>(n.decorators.size());
    if (!n.comment.empty()) h += p.lineH;
    return h;
}

struct RefState {
    const std::vector<std::vector<i32>>& kids;
    const BtLayoutParams& p;
    std::vector<BtNodeBox>& boxes;
    f32 cursor = 0.0f;
};

static void refPlaceX(RefState& s, i32 idx) {
    const std::vector<i32>& kids = s.kids[static_cast<usize>(idx)];
    if (kids.empty()) {
        s.boxes[static_cast<usize>(idx)].x = s.cursor;
        s.cursor += s.p.nodeW + s.p.gapX;
        return;
    }
    for (const i32 k : kids) refPlaceX(s, k);
    const f32 first = s.boxes[static_cast<usize>(kids.front())].x;
    const f32 last = s.boxes[static_cast<usize>(kids.back())].x;
    s.boxes[static_cast<usize>(idx)].x = (first + last) * 0.5f;
}

static std::vector<BtNodeBox> refLayout(const synapse::BtAsset& asset, const BtLayoutParams& p) {
    const usize n = asset.nodes.size();
    std::vector<BtNodeBox> boxes(n);
    if (n == 0) return boxes;
    std::vector<std::vector<i32>> kids(n);
    std::vector<i32> depth(n, 0);
    for (usize i = 1; i < n; ++i) {
        const i32 parent = asset.nodes[i].node.parent;
        if (parent < 0 || static_cast<usize>(parent) >= i) continue;
        kids[static_cast<usize>(parent)].push_back(static_cast<i32>(i));
        depth[i] = depth[static_cast<usize>(parent)] + 1;
    }
    RefState s{kids, p, boxes};
    refPlaceX(s, 0);
    i32 maxDepth = 0;
    for (const i32 d : depth) maxDepth = std::max(maxDepth, d);
    std::vector<f32> rowH(static_cast<usize>(maxDepth) + 1, 0.0f);
    for (usize i = 0; i < n; ++i) {
        boxes[i].w = p.nodeW;
        boxes[i].h = refNodeHeight(asset.nodes[i], p);
        rowH[static_cast<usize>(depth[i])] = std::max(rowH[static_cast<usize>(depth[i])], boxes[i].h);
    }
    std::vector<f32> rowY(rowH.size(), 0.0f);
    for (usize d = 1; d < rowY.size(); ++d) rowY[d] = rowY[d - 1] + rowH[d - 1] + p.gapY;
    for (usize i = 0; i < n; ++i) boxes[i].y = rowY[static_cast<usize>(depth[i])];
    return boxes;
}

static bool sameBoxes(const std::vector<BtNodeBox>& a, const std::vector<BtNodeBox>& b) {
    if (a.size() != b.size()) return false;
    for (usize i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].w != b[i].w || a[i].h != b[i].h) return false;
    return true;
}

static synapse::BtAssetNode node(fmt::OcBtNodeKind kind, i32 parent, const char* name = "") {
    synapse::BtAssetNode n;
    n.node.kind = kind;
    n.node.parent = parent;
    n.node.name = name;
    return n;
}

static void layout() {
    BtLayoutParams p;
    synapse::BtAsset starter = synapse::btAssetStarter();
    check(sameBoxes(btLayout(starter, p), refLayout(starter, p)), "layoutTree through btLayout equals the old layout: the starter tree");

    synapse::BtAsset wide;
    wide.nodes.push_back(node(fmt::OcBtNodeKind::Selector, fmt::kOcBtNoParent));
    for (int i = 0; i < 4; ++i) {
        wide.nodes.push_back(node(fmt::OcBtNodeKind::Sequence, 0));
        const i32 seq = static_cast<i32>(wide.nodes.size()) - 1;
        wide.nodes.back().comment = i % 2 ? "a comment makes this row taller" : "";
        for (int j = 0; j <= i; ++j) {
            wide.nodes.push_back(node(fmt::OcBtNodeKind::Action, seq, "Wait"));
            if (j == 1) wide.nodes.back().decorators.push_back(synapse::BtDecorator{});
        }
    }
    check(sameBoxes(btLayout(wide, p), refLayout(wide, p)), "...a wide tree with comments and decorators (varying row heights)");

    synapse::BtAsset chain;
    chain.nodes.push_back(node(fmt::OcBtNodeKind::Sequence, fmt::kOcBtNoParent));
    for (int i = 1; i < 9; ++i) chain.nodes.push_back(node(i == 8 ? fmt::OcBtNodeKind::Action : fmt::OcBtNodeKind::Sequence, i - 1, i == 8 ? "Wait" : ""));
    check(sameBoxes(btLayout(chain, p), refLayout(chain, p)), "...a deep chain");

    synapse::BtAsset broken = wide;
    broken.nodes[3].node.parent = 99;   // an orphan: drawn at x = 0 on the first row, as before
    broken.nodes[5].node.parent = fmt::kOcBtNoParent;
    check(sameBoxes(btLayout(broken, p), refLayout(broken, p)), "...a malformed tree (orphans)");
    p.nodeW = 250.0f; p.gapX = 10.0f; p.gapY = 33.0f;
    check(sameBoxes(btLayout(wide, p), refLayout(wide, p)), "...and with other spacing");
    check(btLayout(synapse::BtAsset{}, p).empty(), "an empty tree has no boxes");

    // The forest form: two roots side by side.
    const std::vector<i32> parents = {-1, 0, 0, -1, 3};
    const std::vector<f32> heights = {30, 30, 30, 30, 30};
    TreeLayoutParams lp;
    const std::vector<TreeBox> boxes = layoutTree(parents, heights, lp);
    check(boxes.size() == 5 && boxes[1].x == 0 && near(boxes[2].x, lp.nodeW + lp.gapX) && near(boxes[0].x, (boxes[1].x + boxes[2].x) * 0.5f), "layoutTree: a root is centred over its children");
    check(near(boxes[3].x, 2 * (lp.nodeW + lp.gapX)) && boxes[4].x == boxes[3].x, "...the second root starts after the first's last leaf, a single child sits under its parent");
    check(boxes[1].y == boxes[2].y && near(boxes[1].y, 30 + lp.gapY) && boxes[0].y == 0 && boxes[3].y == 0, "...rows by depth");
    const TreeBox bounds = treeBounds(boxes);
    check(near(bounds.w, 3 * lp.nodeW + 2 * lp.gapX) && bounds.x == 0, "treeBounds");
    check(treeHitTest(boxes, boxes[1].x + 5, boxes[1].y + 5) == 1 && treeHitTest(boxes, -50, -50) == -1, "treeHitTest");
    const std::vector<BtNodeBox> sb = btLayout(starter, BtLayoutParams{});
    check(btHitTest(sb, sb[0].x + 1.0f, sb[0].y + 1.0f) == 0, "btHitTest still finds the root");
}

static void liveBoard() {
    synapse::Blackboard b;
    synapse::BbKeyDef k;
    k.name = "Alert";
    k.type = synapse::BbType::Bool;
    b.defineKey(k);
    LiveBoard lb;
    lb.update(&b, 0.016f);
    check(lb.flashOf("Alert") == 0.0f, "no flash on the first look");
    b.setBool("Alert", true);
    lb.update(&b, 0.0f);
    check(near(lb.flashOf("Alert"), 1.0f), "a changed value flashes at 1");
    lb.update(&b, 0.5f);
    check(near(lb.flashOf("Alert"), 0.25f), "...fading at 1.5 per second");
    lb.update(&b, 5.0f);
    check(lb.flashOf("Alert") == 0.0f && lb.flashOf("Nope") == 0.0f, "...to zero, and unknown keys read 0");
    lb.update(nullptr, 0.016f);
    b.setBool("Alert", false);
    lb.update(&b, 0.0f);
    check(lb.flashOf("Alert") == 0.0f, "no live board forgets the stamps (no flash on re-attach)");

    CanvasView v;
    v.panX = 99; v.zoom = 2.0f; v.panning = true;
    v.reset();
    check(v.panX == 24.0f && v.panY == 24.0f && v.zoom == 1.0f && !v.panning, "CanvasView::reset");

    synapse::setAiContentRoot("C:/proj/Content/");
    check(aiRelativeAssetPath("C:\\proj\\content\\AI\\Guard.ocst") == "AI/Guard.ocst", "aiRelativeAssetPath: backslashes and case do not matter");
    check(aiRelativeAssetPath("C:/other/AI/Guard.ocst").empty() && aiRelativeAssetPath("C:/proj/Content").empty(), "...outside the root, or the root itself: empty");
    synapse::setAiContentRoot("");
    check(aiRelativeAssetPath("C:/proj/Content/AI/Guard.ocst").empty(), "...no root: empty");
}

// ---- the StateTree editor -----------------------------------------------------------------------------

static const char* kGuard =
    "OCST 1\nDESC \"Guard\"\nKEY Alert bool agent false\nKEY Target entity agent\n"
    "STATE Patrol\n  ENTER\n    IF bb !Alert\n  TASK Wait p0=3\n  ON event:Alarm TO Combat\nEND\n"
    "STATE Combat\n  COMPLETE none\n  TASK Wait p0=1\n"
    "  STATE Chase\n    TASK Wait p0=1\n    ON tick TO Combat.Search\n      IF bb !Alert\n  END\n"
    "  STATE Search\n    TASK Wait p0=4\n    ON completed TO Patrol\n  END\n"
    "  ON failed TO Patrol\nEND\n";

static void stateTreeEditor() {
    const std::string path = writeFile("editor_guard.ocst", kGuard);
    StateTreeEditor ed(path);
    check(ed.loaded() && ed.asset().states.size() == 4 && ed.selected() == 0 && !ed.dirty(), "loads, selects the first state, starts clean");
    check(!ed.canUndo() && !ed.canRedo(), "nothing to undo yet");

    check(ed.addState(true) && ed.asset().states.size() == 5 && ed.dirty() && ed.asset().states[static_cast<usize>(ed.selected())].parent == 0, "add a child state: selected, dirty");
    check(ed.canUndo(), "...undoable");
    ed.undo();
    check(ed.asset().states.size() == 4 && ed.dirty() && ed.canRedo(), "undo restores the tree (the tab stays dirty), redo is available");
    ed.redo();
    check(ed.asset().states.size() == 5, "redo");
    ed.undo();

    ed.select(1);
    check(ed.renameSelected("Fight"), "rename Combat");
    check(ed.asset().states[0].transitions[0].targetPath == "Fight" && ed.asset().states[2].transitions[0].targetPath == "Fight.Search", "...Goto paths follow");
    check(!ed.renameSelected("Patrol") && !ed.renameSelected("bad name") && !ed.renameSelected("NEXT"), "rename refuses a clash, a bad name and a reserved name");
    ed.undo();
    check(ed.asset().states[1].name == "Combat" && ed.asset().states[0].transitions[0].targetPath == "Combat", "undo of a rename restores the paths");

    ed.select(3);
    check(ed.deleteSelected() && ed.lastRetargeted() == 1 && ed.asset().states.size() == 3, "delete Search: the transition into it is retargeted (reported)");
    check(ed.asset().states[2].transitions[0].target == synapse::StTargetKind::Next, "...to NEXT");
    ed.undo();
    check(ed.asset().states.size() == 4 && ed.asset().states[2].transitions[0].target == synapse::StTargetKind::Goto && ed.asset().states[2].transitions[0].targetState == 3, "undo of a delete brings the state and the Goto back");

    ed.select(0);
    check(ed.moveSelected(1) && ed.asset().states[0].name == "Combat", "move Patrol after Combat (selection follows)");
    check(ed.asset().states[static_cast<usize>(ed.selected())].name == "Patrol", "...the selection is still Patrol");
    ed.undo();
    ed.select(3);
    check(ed.reparentSelected(2) && ed.asset().findPath("Combat.Chase.Search") >= 0, "reparent Search under Chase");
    ed.undo();

    // flags
    ed.select(1);
    check(ed.setGroup(true) && ed.asset().states[1].group && !ed.setGroup(true), "setGroup applies once");
    check(ed.setDisabled(true) && ed.asset().states[1].disabled, "setDisabled");
    check(ed.setComplete(2) && ed.asset().states[1].completeExplicit && ed.asset().states[1].complete == synapse::StComplete::Any, "setComplete any");
    check(ed.setComplete(0) && !ed.asset().states[1].completeExplicit, "setComplete default clears the explicit flag");
    check(ed.setComplete(9) == false, "setComplete refuses a bad mode");
    check(ed.setComment("note") && ed.asset().states[1].comment == "note", "setComment");

    // enter conditions
    ed.select(0);
    check(ed.addEnter(true) && ed.asset().states[0].enter.size() == 2 && ed.asset().states[0].enter[1].bb.key == "Alert", "addEnter(bb) tests the first declared key");
    check(ed.addEnter(false) && !ed.asset().states[0].enter[2].isBlackboard && ed.asset().states[0].enter[2].leaf.kind == fmt::OcBtNodeKind::Condition, "addEnter(cond) adds a Condition leaf");
    check(ed.removeEnter(2) && ed.asset().states[0].enter.size() == 2 && !ed.removeEnter(7), "removeEnter");
    synapse::StCond c = ed.asset().states[0].enter[1];
    c.negate = true;
    check(ed.setEnter(1, c) && ed.asset().states[0].enter[1].negate, "setEnter");

    // tasks
    check(ed.addTask("MoveTo") && ed.asset().states[0].tasks.size() == 2 && ed.asset().states[0].tasks[1].name == "MoveTo", "addTask");
    check(ed.moveTask(1, -1) && ed.asset().states[0].tasks[0].name == "MoveTo" && !ed.moveTask(0, -1), "moveTask (and not past the start)");
    fmt::OcBtNode t = ed.asset().states[0].tasks[0];
    t.params[1] = 5.0f;
    check(ed.setTask(0, t) && ed.asset().states[0].tasks[0].params[1] == 5.0f && !ed.setTask(0, fmt::OcBtNode{}), "setTask (an empty leaf name is refused)");
    check(ed.removeTask(0) && ed.asset().states[0].tasks.size() == 1, "removeTask");
    for (int i = 0; i < 15; ++i) ed.addTask("Wait");
    check(ed.asset().states[0].tasks.size() == 16 && !ed.addTask("Wait"), "16 tasks per state, no more");

    // transitions
    check(ed.addTransition() && ed.asset().states[0].transitions.size() == 2, "addTransition");
    check(ed.setTransitionTrigger(1, synapse::StTrigger::Event, "Ping") && ed.asset().states[0].transitions[1].event == "Ping", "trigger event:Ping");
    check(!ed.setTransitionTrigger(1, synapse::StTrigger::Event, "not valid"), "...an invalid event name is refused");
    check(ed.setTransitionTarget(1, synapse::StTargetKind::Goto, 1) && ed.asset().states[0].transitions[1].targetPath == "Combat", "target Goto Combat");
    check(!ed.setTransitionTarget(1, synapse::StTargetKind::Goto, 99), "...a bad state index is refused");
    check(ed.setTransitionTarget(1, synapse::StTargetKind::Succeeded, -1) && ed.asset().states[0].transitions[1].targetPath.empty(), "target SUCCEEDED clears the path");
    check(ed.addTransitionCond(1, true) && ed.asset().states[0].transitions[1].conds.size() == 1, "transition condition");
    check(ed.moveTransition(1, -1) && ed.asset().states[0].transitions[0].event == "Ping", "moveTransition");
    check(ed.removeTransitionCond(0, 0) && ed.removeTransition(0) && ed.asset().states[0].transitions.size() == 1, "remove condition and transition");

    // evaluators and header
    check(ed.addEvaluator("UpdateThreat") && ed.asset().evaluators.size() == 1 && !ed.addEvaluator("1bad"), "addEvaluator");
    check(ed.removeEvaluator(0) && ed.asset().evaluators.empty(), "removeEvaluator");
    check(ed.setRestartOnFinish(false) && !ed.asset().restartOnFinish, "setRestartOnFinish");
    check(ed.setDescription("Guard v2") && ed.asset().desc == "Guard v2", "setDescription");

    // schema: renaming a key rewrites its uses
    ed.select(0);
    check(ed.renameKey("Alert", "Alarm"), "rename the key Alert");
    check(ed.asset().schema.indexOf("Alarm") >= 0 && ed.asset().states[0].enter[0].bb.key == "Alarm", "...the schema and the enter condition follow");
    check(ed.asset().states[2].transitions[0].conds[0].bb.key == "Alarm", "...and a transition condition in another state");
    check(!ed.renameKey("Alarm", "Target") && !ed.renameKey("Nope", "X") && !ed.renameKey("Alarm", "1bad"), "...a clash, a missing key and a bad name are refused");
    synapse::BbKeyDef def;
    def.name = "Ammo";
    def.type = synapse::BbType::Int;
    check(ed.addKey(def) && !ed.addKey(def), "addKey (and not twice)");
    synapse::BbKeyDef changed = def;
    changed.type = synapse::BbType::Float;
    check(ed.setKey("Ammo", changed) && ed.asset().schema.find("Ammo")->type == synapse::BbType::Float, "setKey changes the type");
    check(ed.removeKey("Ammo") && ed.asset().schema.indexOf("Ammo") < 0, "removeKey");

    // save and load
    check(ed.dirty(), "dirty before saving");
    std::string why;
    check(ed.save(&why) && !ed.dirty(), "save writes the file and clears dirty: " + why);
    check(readFile(path) == synapse::writeStateTree(ed.asset()), "...the file is exactly writeStateTree");
    StateTreeEditor again(path);
    check(again.loaded() && again.asset().states.size() == ed.asset().states.size() && again.asset().schema.indexOf("Alarm") >= 0, "...and reloads");
    ed.select(1);
    ed.setComment("unsaved");
    writeFile("editor_guard.ocst", kGuard);
    ed.onFileChanged();
    check(ed.asset().states[1].comment == "unsaved", "a dirty tab keeps its edits when the file changes on disk");
    ed.save(nullptr);
    writeFile("editor_guard.ocst", kGuard);
    ed.onFileChanged();
    check(ed.asset().desc == "Guard" && ed.asset().states[1].comment.empty() && !ed.dirty(), "a clean tab reloads");

    check(makeStateTreeEditor(path) != nullptr && makeStateTreeEditor("x.ocbt") == nullptr && makeStateTreeEditor(tempDir().generic_string() + "/NOPE.OCST") != nullptr, "the factory claims .ocst only (any case)");
    StateTreeEditor missing((tempDir() / "no_such.ocst").generic_string());
    check(!missing.loaded() && !missing.loadError().empty(), "a missing file reports why");

    const std::string startPath = (tempDir() / "editor_start.ocst").generic_string();
    check(stateTreeWriteStarter(startPath, &why), "the starter writes: " + why);
    StateTreeEditor starter(startPath);
    check(starter.loaded() && starter.asset().states.size() == 2 && starter.asset().states[0].name == "Idle", "...and opens at once");
    check(!starter.addEnter(true), "addEnter(bb) with no keys declared is refused");
    check(starter.addState(false) && starter.asset().roots.size() == 3, "add sibling with a root selected adds a root");
    starter.select(-1);
    check(starter.deleteSelected() == false, "nothing selected, nothing to delete");
}

// ---- the EQS editor -------------------------------------------------------------------------------------

static void eqsEditor() {
    const std::string path = (tempDir() / "editor_query.oceqs").generic_string();
    std::string why;
    check(editor::eqsWriteStarter(path, &why), "the starter query writes: " + why);
    EqsEditor ed(path);
    check(ed.loaded() && ed.query().options.size() == 1 && ed.query().options[0].tests.size() == 1 && !ed.dirty(), "loads the starter: one option, one test");
    const std::string starterDesc = ed.query().desc;

    check(ed.setDescription("Find a spot") && ed.query().desc == "Find a spot" && ed.dirty(), "setDescription marks dirty");
    check(ed.setResultMode(synapse::EqsResultMode::Top, 0, 0) && ed.query().modeN == 1, "RESULT top clamps its count to at least 1");
    check(ed.setResultMode(synapse::EqsResultMode::Percent, 0, 500.0f) && ed.query().modeP == 100.0f, "...and a percentage to 100");
    check(ed.setResultMode(synapse::EqsResultMode::Best, 7, 7.0f) && ed.query().modeN == 0 && ed.query().modeP == 0.0f, "best carries no count");
    check(ed.setRunAll(true) && ed.query().runAll && !ed.setRunAll(true), "setRunAll");
    synapse::EqsQuery::Output out;
    out.pos = "CoverPos";
    check(ed.setOutput(out) && ed.query().out.pos == "CoverPos" && !ed.setOutput(out), "setOutput");
    check(ed.addContext("foe", "Foe") && !ed.addContext("foe", "Other") && !ed.addContext("querier", "K") && !ed.addContext("x", ""), "addContext (not twice, not a built-in, not without a key)");
    check(ed.removeContext(0) && ed.query().contexts.empty(), "removeContext");

    check(ed.addOption() && ed.query().options.size() == 2 && ed.option() == 1, "addOption selects the new option");
    check(ed.addTest(1, synapse::EqsTestKind::Trace) && ed.addTest(1, synapse::EqsTestKind::Tag) && ed.query().options[1].tests.size() == 2, "addTest");
    check(ed.query().options[1].tests[1].tags.v.size() == 1, "...a tag test starts with a name (a nameless one cannot be written)");
    check(ed.moveTest(1, 1, -1) && ed.query().options[1].tests[0].kind == synapse::EqsTestKind::Tag && !ed.moveTest(1, 0, -1), "moveTest");
    check(ed.setTestKind(1, 0, synapse::EqsTestKind::Random) && ed.query().options[1].tests[0].kind == synapse::EqsTestKind::Random, "setTestKind starts from the kind's defaults");
    synapse::EqsTest tt = ed.query().options[1].tests[0];
    tt.seed = 5;
    check(ed.setTest(1, 0, tt) && ed.query().options[1].tests[0].seed == 5, "setTest");
    check(ed.removeTest(1, 0) && ed.query().options[1].tests.size() == 1, "removeTest");
    for (int i = 0; i < 20; ++i) ed.addTest(1, synapse::EqsTestKind::Random);
    check(ed.query().options[1].tests.size() == 16, "16 tests per option");

    check(ed.setGeneratorKind(1, synapse::EqsGen::Grid) && ed.query().options[1].gen.kind == synapse::EqsGen::Grid && ed.query().options[1].gen.halfX == 500, "setGeneratorKind starts from the kind's defaults");
    check(ed.setGeneratorKind(1, synapse::EqsGen::Actors) && !ed.query().options[1].gen.namePrefix.empty(), "...an actors generator gets a filter (exactly one is required)");
    check(ed.moveOption(1, -1) && ed.option() == 0 && ed.query().options[0].gen.kind == synapse::EqsGen::Actors, "moveOption carries the selection");
    for (int i = 0; i < 10; ++i) ed.addOption();
    check(ed.query().options.size() == 8, "8 options");
    while (ed.query().options.size() > 1) ed.removeOption(ed.query().options.size() - 1);
    check(!ed.removeOption(0) && ed.query().options.size() == 1, "the last option cannot be removed");

    ed.undo();
    check(ed.query().options.size() == 2 && ed.canRedo(), "undo steps back");
    while (ed.canUndo()) ed.undo();
    check(ed.query().options.size() == 1 && ed.query().desc == starterDesc && ed.dirty(), "undo all the way back (the tab stays dirty)");

    // save, then edit, then preview the UNSAVED edit
    check(ed.setGeneratorKind(0, synapse::EqsGen::Points), "make the option a points generator");
    synapse::EqsGenerator g = ed.query().options[0].gen;
    g.points = {Vec3{100, 0, 0}, Vec3{200, 0, 0}, Vec3{300, 0, 0}};
    check(ed.setGenerator(0, g), "set three points");
    check(ed.save(&why) && !ed.dirty(), "save: " + why);
    synapse::EqsQuery onDisk;
    check(synapse::loadEqsQuery(path, onDisk, &why) && synapse::writeEqsQuery(onDisk) == synapse::writeEqsQuery(ed.query()), "...the file reads back identical");

    scene::World& w = scene::World::instance();
    const scene::Entity querier = w.create("EqsQuerier");
    synapse::SynapseAi ai;
    ai.registerComponents(w);
    check(ed.runPreview(ai, querier, 0) && ed.previewHandle() != 0, "Run on selection: runs the in-memory query synchronously");
    const synapse::EqsResult* r = ai.eqs().result(ed.previewHandle());
    check(r && r->status == synapse::EqsResult::Status::Finished && r->items.size() == 3, "...3 items (the starter's Top 5 over three points)");
    check(!ai.eqs().debugRuns().empty() && ai.eqs().debugRuns()[0].pinned, "...pinned on screen until replaced");
    const u32 first = ed.previewHandle();

    synapse::EqsGenerator ng = ed.query().options[0].gen;
    ng.points = {Vec3{1, 2, 3}};
    ed.setGenerator(0, ng);   // an unsaved edit
    check(ed.runPreview(ai, querier, 0), "preview again with an unsaved edit");
    r = ai.eqs().result(ed.previewHandle());
    check(r && r->items.size() == 1 && near(r->items[0].pos.x, 1.0f), "...it ran the edit, not the file");
    check(ai.eqs().status(first) == synapse::EqsSystem::Status::Unknown, "...and the previous result was released");

    ed.setGeneratorKind(0, synapse::EqsGen::Nav);
    check(ed.runPreview(ai, querier, 0) && !ed.previewMessage().empty(), "a failing run still returns, with the reason in previewMessage");
    ed.setGeneratorKind(0, synapse::EqsGen::Points);

    ed.setTestKind(0, 0, synapse::EqsTestKind::Distance);
    check(makeEqsEditor(path) != nullptr && makeEqsEditor("x.ocst") == nullptr, "the factory claims .oceqs only");
    EqsEditor missing((tempDir() / "no_such.oceqs").generic_string());
    check(!missing.loaded() && !missing.loadError().empty(), "a missing file reports why");

    ed.setGeneratorKind(0, synapse::EqsGen::Ring);
    synapse::EqsGenerator badRing = ed.query().options[0].gen;
    badRing.outer = 10.0f;
    badRing.inner = 500.0f;   // outer below inner: the parser refuses it
    ed.setGenerator(0, badRing);
    check(!ed.save(&why) && !why.empty(), "save refuses a query that would not read back, and says why");
}

int main() {
    AVER_INFO("AiEditorPartsTest");
    layout();
    liveBoard();
    stateTreeEditor();
    eqsEditor();
    if (g_failures == 0) AVER_INFO("AiEditorPartsTest: all passed");
    return g_failures;
}
