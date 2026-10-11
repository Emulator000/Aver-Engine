#pragma once
// The StateTree editor tab: a .ocst opened as a canvas of nested states (tasks and transitions listed in
// each box, Goto transitions drawn as dashed curves), with a State panel, the shared blackboard schema
// panel (Parameters), Evaluators and live Debug against a running entity. It reads and writes the plain
// text asset (synapse::StateTreeAsset), so a hand-edited file opens here unchanged.
//
// Everything that is not drawing is plain C++ (edit bookkeeping, undo, save / load), so the headless test
// compiles this file with AVER_WITH_IMGUI undefined, like BtGraphEditorTest.
//
// WIRING, SandboxApp.cpp beside the BT editors:
//   assetEditors_.registerFactory(&editor::makeStateTreeEditor);
// and "New State Tree" calls editor::stateTreeWriteStarter(path, &why).
#include "AiEditorParts.hpp"
#include "AssetEditor.hpp"
#include "EditorWidgets.hpp"
#include "SnapshotUndo.hpp"

#include "aver/synapse/StateTreeAsset.hpp"

#include <memory>
#include <string>
#include <vector>

namespace aver::editor {

// Writes the starter tree (Idle -> Active, each on a Wait) to `path`.
bool stateTreeWriteStarter(const std::string& path, std::string* why = nullptr);

// One-line texts the canvas and lists use.
std::string stTaskText(const fmt::OcBtNode& n);               // "Wait p0=3 \"s\""
std::string stCondText(const synapse::StCond& c);             // "not Alert is set" / "cond CanSeeTarget"
std::string stTransitionText(const synapse::StateTreeAsset& a, const synapse::StTransition& t);   // "tick -> Combat.Search"

class StateTreeEditor final : public AssetEditor, public SchemaEditHost {
public:
    explicit StateTreeEditor(std::string path);
    ~StateTreeEditor() override;

    const std::string& path() const override { return path_; }
    std::string title() const override;
    bool dirty() const override { return dirty_; }
    void draw(Engine& e) override;
    bool save(std::string* why) override;
    void onFileChanged() override;
    void resetLayout() override;

    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const synapse::StateTreeAsset& asset() const { return asset_; }
    i32 selected() const { return selected_; }
    void select(i32 index) { selected_ = index; }
    // Warnings from validateStateTree against the live leaf registry (unknown leaves, undeclared keys, ...).
    std::vector<std::string> warnings() const;
    // How many transitions the last delete retargeted to NEXT.
    i32 lastRetargeted() const { return lastRetargeted_; }

    void pushUndo() override { history_.push(asset_); }
    void markDirty() override { dirty_ = true; }
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // ---- edits, as the tab performs them: push undo, apply, fix the selection, mark dirty. False (asset
    // untouched) when refused.
    bool addState(bool asChild);                       // a child of the selection, or its sibling
    bool deleteSelected();
    bool renameSelected(const std::string& name);
    bool moveSelected(i32 delta);
    bool reparentSelected(i32 newParent);
    bool setGroup(bool on);
    bool setDisabled(bool on);
    bool setComplete(int mode);                        // 0 default, 1 all, 2 any, 3 none
    bool setComment(const std::string& text);

    bool addEnter(bool blackboard);
    bool removeEnter(usize index);
    bool setEnter(usize index, const synapse::StCond& c);

    bool addTask(const std::string& leaf);
    bool removeTask(usize index);
    bool setTask(usize index, const fmt::OcBtNode& n);
    bool moveTask(usize index, i32 delta);

    bool addTransition();
    bool removeTransition(usize index);
    bool moveTransition(usize index, i32 delta);
    bool setTransitionTrigger(usize index, synapse::StTrigger trigger, const std::string& event);
    // kind Goto needs a state index; the path is rewritten from it.
    bool setTransitionTarget(usize index, synapse::StTargetKind kind, i32 state);
    bool addTransitionCond(usize transition, bool blackboard);
    bool removeTransitionCond(usize transition, usize cond);
    bool setTransitionCond(usize transition, usize cond, const synapse::StCond& c);

    bool addEvaluator(const std::string& leaf);
    bool removeEvaluator(usize index);
    bool setEvaluator(usize index, const fmt::OcBtNode& n);

    bool setDescription(const std::string& text);
    bool setRestartOnFinish(bool on);

    // SchemaEditHost: keys of the tree's own blackboard (renaming rewrites every condition using the key).
    bool addKey(const synapse::BbKeyDef& def) override;
    bool removeKey(const std::string& name) override;
    bool renameKey(const std::string& from, const std::string& to) override;
    bool setKey(const std::string& name, const synapse::BbKeyDef& def) override;

private:
    void loadFromDisk();
    synapse::StState* sel();
    bool beginEdit();                // loaded and something selected; pushes the undo snapshot
    void endEdit() { dirty_ = true; }
    void resolveTargets();           // re-resolves every Goto path after an edit

    std::string path_;
    synapse::StateTreeAsset asset_;
    bool loaded_ = false;
    std::string loadError_;
    bool dirty_ = false;
    i32 selected_ = 0;
    i32 lastRetargeted_ = 0;
    SnapshotUndo<synapse::StateTreeAsset> history_;
    SplitPane split_;

    CanvasView view_;
    SchemaPanelState schemaState_;
    LiveBoard liveBoard_;
    u32 debugEntity_ = 0;

#if AVER_WITH_IMGUI
    void drawToolbar();
    void drawCanvas(f32 width);
    void drawStatePanel();
    void drawEvaluatorPanel();
    void drawDebugPanel();
    bool drawCondRow(synapse::StCond& c, const char* id);
    bool drawLeafRow(fmt::OcBtNode& n, const char* id, bool condition);
#endif
};

std::unique_ptr<AssetEditor> makeStateTreeEditor(const std::string& path);

} // namespace aver::editor
