#pragma once
// The motion-matching database editor tab: an .ocmotion opened as an asset. Skeleton, sample rate and the
// search / spring / inertialization defaults at the top; the feature layout (bones with position and
// velocity weights, root velocity, future and past trajectory samples); the clip list with per-entry loop,
// bias, tags and tag ranges drawn on the clip's duration; a Bake button that writes the .ocmbake sidecar and
// shows the build report; and a query lab that runs the search against the baked rows. Semantics:
// docs/ANIM_MOTION_MATCHING.md.
//
// THE HOOK INTO SandboxApp.cpp IS TWO LINES, matching MontageEditor.hpp's note:
//   #include "MotionDbEditor.hpp"
//   assetEditors_.registerFactory(&editor::makeMotionDbEditor);   // .ocmotion
// plus the file in sandbox/CMakeLists.txt.
//
// NO SKINNED PREVIEW in v1: the Anim tab previews single clips and play-in-editor shows the matcher on a
// character.
#include "AssetEditor.hpp"
#include "SnapshotUndo.hpp"

#include "aver/anim/MotionAsset.hpp"
#include "aver/anim/PoseDatabase.hpp"
#include "aver/anim/Trajectory.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aver::editor {

// ---- edits as FREE FUNCTIONS, so a headless test reaches them (see BtEditor.hpp for the argument) ----

// A new database: no clips yet (drop some), the engine's search / spring defaults, the root velocity and
// trajectory features on and no bones (the rig decides which ones matter).
anim::MotionAsset motionStarter();

// Appends (or inserts at `at`, when in range) a clip entry that plays the whole clip. Returns its index, or -1
// for an empty reference.
i32 mdbAddClip(anim::MotionAsset& a, const std::string& clipRef, i32 at = -1);
bool mdbRemoveClip(anim::MotionAsset& a, i32 index);
// Moves an entry by `delta` places. Returns the new index or -1.
i32 mdbMoveClip(anim::MotionAsset& a, i32 index, i32 delta);

// Declares a tag. Returns its index, or -1 for an empty name, a duplicate, or the 33rd tag.
i32 mdbAddTag(anim::MotionAsset& a, const std::string& name);
// Removes a tag and strips it from every entry and range.
bool mdbRemoveTag(anim::MotionAsset& a, i32 index);
// Renames a tag and every entry / range that names it. False for an empty or taken name.
bool mdbRenameTag(anim::MotionAsset& a, i32 index, const std::string& name);

// A tag range on an entry, [start, end] in clip seconds (end < 0 = to the end of the entry). Returns its index,
// or -1 for a bad entry or a non-finite / reversed range.
i32 mdbAddRange(anim::MotionAsset& a, i32 clip, f32 start, f32 end);
bool mdbRemoveRange(anim::MotionAsset& a, i32 clip, i32 range);
// Moves both ends; start is clamped to >= 0 and the range keeps a positive length (end < 0 stays open).
bool mdbMoveRange(anim::MotionAsset& a, i32 clip, i32 range, f32 start, f32 end);

// Adds a feature bone. Returns its index, or -1 for an empty name or a bone already listed.
i32 mdbAddBone(anim::MotionAsset& a, const std::string& bone, f32 pos = 1.0f, f32 vel = 1.0f);
bool mdbRemoveBone(anim::MotionAsset& a, i32 index);

// Replaces the future (positive) or past (negative) sample times: values on the wrong side of zero and
// non-finite ones are dropped, the rest sorted by magnitude, de-duplicated and capped at 8. Returns how many stay.
u32 mdbSetTrajectoryTimes(anim::MotionAsset& a, bool future, const std::vector<f32>& times);

// Rounds to the editor's time grid (1/30 s by default).
f32 mdbSnap(f32 t, f32 step = 1.0f / 30.0f);

// Clip and skeleton references are content-relative with forward slashes (the engine hashes them as
// written). These convert between that and a file path, using the asset's own location: the nearest ancestor
// directory named "Content" is the root, else the asset's folder.
std::string mdbContentRootOf(const std::string& assetPath);
std::string mdbClipRefFromPath(const std::string& assetPath, const std::string& clipPath);
// The file a reference names, or empty when none of the candidate locations exists.
std::string mdbResolveClipPath(const std::string& assetPath, const std::string& ref);
// Where the bake of an asset lives: the asset's path with the extension .ocmbake.
std::string mdbSidecarPath(const std::string& assetPath);

// A build of the asset against the real files next to it. `db` is null unless ok.
struct MdbPreview {
    bool ok = false;
    std::string why;
    anim::MotionBuildReport report;
    std::unique_ptr<anim::PoseDatabase> db;
    u64 sourceKey = 0;
};
MdbPreview mdbBuildPreview(const anim::MotionAsset& a, const std::string& assetPath);
// mdbBuildPreview, then writes the sidecar with the same source key the runtime computes. False (with `why`)
// when the build fails; a sidecar that cannot be written leaves ok = true and a warning in the report.
bool mdbBake(const anim::MotionAsset& a, const std::string& assetPath, MdbPreview& out);

// ---- the query lab ----
struct MdbLabInput {
    f32 currentSpeed = 0.0f;        // cm/s the character is already moving at (along its heading)
    f32 wantedSpeed = 150.0f;       // cm/s it wants to reach
    f32 wantedHeadingDeg = 0.0f;    // the wanted direction relative to the heading
    u32 require = 0, exclude = 0;   // tag masks
};
struct MdbLabHit {
    u32 row = anim::kNoRow;
    std::string clip;
    f32 time = 0.0f;
    f32 cost = 0.0f;
    std::vector<f32> groupCosts;    // squared distance per feature group
};
struct MdbLabResult {
    anim::MotionQuery query;
    std::vector<MdbLabHit> hits;    // cheapest first
    double linearMicros = 0.0, kdMicros = 0.0;   // 0 when the database has no KD tree
    bool sameAnswer = true;         // the KD search returned the linear search's row
};
// Builds a query from the input (a synthetic second of history at currentSpeed, intent toward the wanted
// velocity; the pose part is the database mean) and searches. Nothing reads a pose.
MdbLabResult mdbRunQuery(const anim::PoseDatabase& db, const MdbLabInput& in, u32 topK = 8);

class MotionDbEditor final : public AssetEditor {
public:
    explicit MotionDbEditor(std::string path);

    const std::string& path() const override { return path_; }
    std::string title() const override;
    bool dirty() const override { return dirty_; }
    void draw(Engine& e) override;
    bool save(std::string* why) override;
    void onFileChanged() override;

    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const anim::MotionAsset& asset() const { return asset_; }
    i32 selectedClip() const { return selClip_; }
    void selectClip(i32 c) { selClip_ = c; }

    void pushUndo() { history_.push(asset_); }
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void markDirty() { dirty_ = true; previewStale_ = true; }

    // Each pushes one undo entry; the ImGui layer calls these, and so do tests.
    i32 addClip(const std::string& clipRef);
    bool removeClip(i32 index);
    i32 moveClip(i32 index, i32 delta);
    i32 addTag(const std::string& name);
    bool removeTag(i32 index);
    bool renameTag(i32 index, const std::string& name);
    i32 addRange(i32 clip, f32 start, f32 end);
    bool removeRange(i32 clip, i32 range);
    i32 addBone(const std::string& bone);
    bool removeBone(i32 index);
    bool setTrajectoryTimes(bool future, const std::vector<f32>& times);

    // Saves if needed, then builds and writes the sidecar. False when the asset is invalid or the build fails.
    bool bake(std::string* why);
    const MdbPreview& lastBake() const { return bake_; }
    // The last baked database, for the query lab; null before a successful bake.
    const anim::PoseDatabase* database() const { return bake_.db.get(); }

    // A clip's length in seconds, loaded through the asset's location and cached; 0 when unknown.
    f32 clipDuration(const std::string& clipRef);

private:
    void loadFromDisk();

    std::string path_;
    anim::MotionAsset asset_;
    bool loaded_ = false;
    std::string loadError_;
    bool dirty_ = false;
    SnapshotUndo<anim::MotionAsset> history_;
    i32 selClip_ = -1;

    std::map<std::string, f32> durations_;   // by reference; 0 remembers a miss
    MdbPreview bake_;
    bool previewStale_ = true;
    MdbLabInput lab_;
    MdbLabResult labResult_;

#if AVER_WITH_IMGUI
    // One undo entry per widget gesture: the first change of a drag pushes the state from before it.
    bool gesture_ = false;
    bool endGesture_ = false;
    anim::MotionAsset before_;
    i32 dragRange_ = -1, dragEdge_ = 0;      // tag-range handle being dragged: range index, 0 start / 1 end
    std::string timesFuture_, timesPast_;    // the comma lists being typed
    bool timesInit_ = false;
    void touch(bool changed);
    void drawHeader();
    void drawTags();
    void drawFeatures();
    void drawClips();
    void drawLane(i32 clip);
    void drawBake();
    void drawLab();
#endif
};

std::unique_ptr<AssetEditor> makeMotionDbEditor(const std::string& path);

} // namespace aver::editor
