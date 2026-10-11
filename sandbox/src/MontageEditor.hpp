#pragma once
// The animation montage editor tab: an .ocmontage opened as an asset, drawn as a timeline of clip
// segments with draggable section and notify markers, a details panel, and a simulator that runs a
// MontagePlayer against the real clips. Semantics: docs/ANIM_MONTAGES.md.
//
// THE HOOK INTO SandboxApp.cpp IS TWO LINES, matching AnimStateMachineEditor.hpp's note:
//   #include "MontageEditor.hpp"
//   assetEditors_.registerFactory(&editor::makeMontageEditor);   // .ocmontage
// plus the file in sandbox/CMakeLists.txt.
//
// NO SKINNED PREVIEW in v1: the simulator shows position, section, weight and events, and the Anim tab
// previews a single clip. Play-in-editor shows the montage on a character.
#include "AssetEditor.hpp"
#include "SnapshotUndo.hpp"

#include "aver/anim/Montage.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aver::editor {

// ---- edits as FREE FUNCTIONS, so a headless test reaches them (see BtEditor.hpp for the argument) ----

// A new montage: no segments yet (add a clip), one Default section at 0, the engine's blend defaults.
anim::MontageAsset montageStarter();

// Appends (or inserts at `at`, when in range) a segment that plays the whole clip. Returns its index,
// or -1 for an empty clip name.
i32 mtgAddSegment(anim::MontageAsset& a, const std::string& clip, i32 at = -1);
bool mtgRemoveSegment(anim::MontageAsset& a, i32 index);
// Moves a segment within the timeline by `delta` places. Returns the new index or -1.
i32 mtgMoveSegment(anim::MontageAsset& a, i32 index, i32 delta);

// A section name not already used: `base`, then `base 2`, `base 3`...
std::string mtgUniqueSectionName(const anim::MontageAsset& a, const std::string& base);
// Adds a section starting at `start` (> 0; the first section is pinned at 0) with a unique name derived
// from `name`. Sections stay sorted by start. Returns the new index.
i32 mtgAddSection(anim::MontageAsset& a, const std::string& name, f32 start);
// Removes a section. Every `next` that named it takes over the removed section's own link (a section
// that led to the removed one now leads where it led), and removing the first section re-pins the new
// first one at 0. False for the only section or an index out of range.
bool mtgRemoveSection(anim::MontageAsset& a, i32 index);
// Renames a section and every `next` that named it. False for an empty or taken name.
bool mtgRenameSection(anim::MontageAsset& a, i32 index, const std::string& name);
// "" = the following section, "@end", or an existing section's name (its own name = loop).
bool mtgSetNext(anim::MontageAsset& a, i32 index, const std::string& next);
// Moves a section's start (> 0; section 0 cannot move) and re-sorts. Returns its new index, or -1.
i32 mtgMoveSection(anim::MontageAsset& a, i32 index, f32 start);

// Adds a notify (a state when duration > 0), kept sorted by time. Returns its index, or -1 for a
// non-finite time or a negative duration. An empty name becomes "Notify".
i32 mtgAddNotify(anim::MontageAsset& a, f32 time, const std::string& name, f32 duration);
bool mtgRemoveNotify(anim::MontageAsset& a, i32 index);
// Moves a notify in time (>= 0) and re-sorts. Returns its new index, or -1.
i32 mtgMoveNotify(anim::MontageAsset& a, i32 index, f32 time);

// Rounds to the editor's time grid (1/30 s by default).
f32 mtgSnap(f32 t, f32 step = 1.0f / 30.0f);

// Where each segment sits on the montage timeline, ImGui-free. `clipDuration` returns a clip's length in
// seconds, or <= 0 when it is unknown (such a segment then occupies its explicit range, or nothing).
struct MtgTimeline {
    f32 total = 0.0f;
    std::vector<f32> starts;     // montage time at which each segment begins
    std::vector<f32> lengths;    // montage seconds each segment occupies
};
using MtgDurationFn = std::function<f32(const std::string&)>;
MtgTimeline mtgTimeline(const anim::MontageAsset& a, const MtgDurationFn& clipDuration);

// A montage's clip references are content-relative with forward slashes (the engine hashes them as
// written). These convert between that and a file path, using the montage's own location: the nearest
// ancestor directory named "Content" is the root, else the montage's folder.
std::string mtgContentRootOf(const std::string& montagePath);
std::string mtgClipRefFromPath(const std::string& montagePath, const std::string& clipPath);
// The file a clip reference names, or empty when none of the candidate locations exists.
std::string mtgResolveClipPath(const std::string& montagePath, const std::string& clipRef);

class MontageEditor final : public AssetEditor {
public:
    explicit MontageEditor(std::string path);

    const std::string& path() const override { return path_; }
    std::string title() const override;
    bool dirty() const override { return dirty_; }
    void draw(Engine& e) override;
    bool save(std::string* why) override;
    void onFileChanged() override;

    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const anim::MontageAsset& asset() const { return asset_; }
    i32 selectedSection() const { return selSection_; }
    i32 selectedNotify() const { return selNotify_; }
    i32 selectedSegment() const { return selSegment_; }
    void selectSection(i32 s) { selSection_ = s; selNotify_ = selSegment_ = -1; }
    void selectNotify(i32 n) { selNotify_ = n; selSection_ = selSegment_ = -1; }
    void selectSegment(i32 g) { selSegment_ = g; selSection_ = selNotify_ = -1; }

    void pushUndo() { history_.push(asset_); }
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void markDirty() { dirty_ = true; simStale_ = true; }

    // Each pushes one undo entry and selects what it made; the ImGui layer calls these, and so do tests.
    i32 addSegment(const std::string& clipRef);
    bool removeSegment(i32 index);
    i32 moveSegment(i32 index, i32 delta);
    i32 addSection(const std::string& name, f32 start);
    bool removeSection(i32 index);
    bool renameSection(i32 index, const std::string& name);
    bool setNext(i32 index, const std::string& next);
    i32 moveSection(i32 index, f32 start);
    i32 addNotify(f32 time, const std::string& name, f32 duration);
    bool removeNotify(i32 index);
    i32 moveNotify(i32 index, f32 time);

    // A clip's length in seconds, loaded through the montage's location and cached; 0 when unknown.
    f32 clipDuration(const std::string& clipRef);
    MtgTimeline timeline() { return mtgTimeline(asset_, [this](const std::string& c) { return clipDuration(c); }); }

private:
    void loadFromDisk();
    const fmt::OcAnimation* clipFor(const std::string& clipRef);

    std::string path_;
    anim::MontageAsset asset_;
    bool loaded_ = false;
    std::string loadError_;
    bool dirty_ = false;
    SnapshotUndo<anim::MontageAsset> history_;

    i32 selSection_ = -1;
    i32 selNotify_ = -1;
    i32 selSegment_ = -1;

    // Clips by reference, loaded on demand; null remembers a failure.
    std::map<std::string, std::unique_ptr<fmt::OcAnimation>> clips_;

    // Simulator.
    anim::MontagePlayer sim_;
    bool simOn_ = false;
    bool simPlaying_ = false;
    bool simStale_ = true;
    bool simBound_ = false;
    std::vector<std::string> simLog_;

#if AVER_WITH_IMGUI
    // One undo entry per widget gesture: the first change of a drag pushes the state from before it.
    bool gesture_ = false;
    bool endGesture_ = false;
    anim::MontageAsset before_;
    f32 playhead_ = 0.0f;          // montage seconds; the simulator's position while it runs
    i32 dragSection_ = -1;         // marker being dragged on the timeline, or -1
    i32 dragNotify_ = -1;
    void touch(bool changed);
    void drawHeader();
    void drawTimeline();
    void drawSegments();
    void drawDetails();
    void drawSimulator();
    void simRebind();
    void simSeek(f32 t);
#endif
};

std::unique_ptr<AssetEditor> makeMontageEditor(const std::string& path);

} // namespace aver::editor
