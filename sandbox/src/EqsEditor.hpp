#pragma once
// The environment-query editor tab: a .oceqs opened as a form (header fields, an option list, per option a
// generator form and a reorderable list of tests) with a "Run on selection" preview whose results table
// and viewport markers stay up until replaced. It reads and writes the plain text asset (synapse::EqsQuery).
//
// Everything that is not drawing is plain C++ (edit bookkeeping, undo, save / load, the preview run against
// a SynapseAi passed in), so the headless test compiles this file with AVER_WITH_IMGUI undefined.
//
// WIRING, SandboxApp.cpp beside the other editors:
//   assetEditors_.registerFactory(&editor::makeEqsEditor);
//   editor::setEqsSelectionSource(...);   // the scene selection, for "Run on selection"
// and "New EQS Query" calls editor::eqsWriteStarter(path, &why).
#include "AssetEditor.hpp"
#include "EditorWidgets.hpp"
#include "SnapshotUndo.hpp"

#include "aver/synapse/EqsAsset.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aver::synapse { class SynapseAi; }

namespace aver::editor {

// Writes the commented starter query to `path`.
bool eqsWriteStarter(const std::string& path, std::string* why = nullptr);

// The scene's selected entities, first = the querier, second = the target. Set once by the host.
using EqsSelectionFn = std::function<std::vector<u32>()>;
void setEqsSelectionSource(EqsSelectionFn fn);

class EqsEditor final : public AssetEditor {
public:
    explicit EqsEditor(std::string path);
    ~EqsEditor() override;

    const std::string& path() const override { return path_; }
    std::string title() const override;
    bool dirty() const override { return dirty_; }
    void draw(Engine& e) override;
    bool save(std::string* why) override;
    void onFileChanged() override;
    void resetLayout() override;

    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const synapse::EqsQuery& query() const { return query_; }
    i32 option() const { return option_; }
    void selectOption(i32 i) { option_ = i; }
    std::vector<std::string> warnings() const;

    void pushUndo() { history_.push(query_); }
    void markDirty() { dirty_ = true; }
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // ---- edits: push undo, apply, mark dirty. False (query untouched) when refused.
    bool setDescription(const std::string& text);
    bool setResultMode(synapse::EqsResultMode mode, u32 n, f32 percent);
    bool setRunAll(bool on);
    bool setOutput(const synapse::EqsQuery::Output& out);
    bool addContext(const std::string& name, const std::string& key);
    bool removeContext(usize index);

    bool addOption();
    bool removeOption(usize index);
    bool moveOption(usize index, i32 delta);
    // Replacing the generator's kind starts from that kind's defaults (centre carries over).
    bool setGeneratorKind(usize option, synapse::EqsGen kind);
    bool setGenerator(usize option, const synapse::EqsGenerator& g);
    bool addTest(usize option, synapse::EqsTestKind kind);
    bool removeTest(usize option, usize test);
    bool moveTest(usize option, usize test, i32 delta);
    bool setTestKind(usize option, usize test, synapse::EqsTestKind kind);
    bool setTest(usize option, usize test, const synapse::EqsTest& t);

    // ---- preview: round-trips the in-memory query through text (so it runs what Save would write), registers
    // it with `ai`, runs it synchronously and pins the run on screen. False with previewMessage() on a failure.
    bool runPreview(synapse::SynapseAi& ai, u32 querier, u32 target);
    u32 previewHandle() const { return previewHandle_; }
    const std::string& previewMessage() const { return previewMessage_; }

private:
    void loadFromDisk();

    std::string path_;
    synapse::EqsQuery query_;
    bool loaded_ = false;
    std::string loadError_;
    bool dirty_ = false;
    i32 option_ = 0;
    SnapshotUndo<synapse::EqsQuery> history_;
    SplitPane split_;
    u32 previewHandle_ = 0;
    u32 runCounter_ = 0;
    std::string previewMessage_;
    std::string newContextName_, newContextKey_;

#if AVER_WITH_IMGUI
    void drawHeader();
    void drawOptions();
    void drawGeneratorForm(usize option);
    void drawTestForm(usize option, usize test);
    void drawPreview();
#endif
};

std::unique_ptr<AssetEditor> makeEqsEditor(const std::string& path);

} // namespace aver::editor
