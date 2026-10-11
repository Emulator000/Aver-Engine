#pragma once
// The Control Rig editor tab: an .ocrig opened as an asset -- the "Slice 4" the control-rig scout left open. A form
// editor over the op list (add twobone / aim / fabrik / ccd / copybone / setbone, reorder, delete), per-op forms with
// bone combos and a value-source picker (literal | @input | bone:), the INPUT table, weight sliders, and a skeleton
// view that shows the ops' result live on the rest pose or a sample clip (vec3 inputs are draggable handles). Saves
// through saveOcRig, which writes OCRIG 1 whenever the data is expressible in it. Semantics: docs/ANIM_IK_RETARGET.md.
//
// THE HOOK INTO SandboxApp.cpp is the factory line beside the other animation editors:
//   assetEditors_.registerFactory(&editor::makeRigEditor);   // .ocrig
// The rig names no skeleton; the tab previews against one picked from the project (an editor preference, not saved).
#include "AssetEditor.hpp"
#include "RigPreview.hpp"
#include "SnapshotUndo.hpp"

#include "aver/formats/OcRig.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aver::editor {

// An empty rig with a name; valid, does nothing.
fmt::OcRigData rigStarter(const std::string& name);

// Appends an op of `kind` with sensible defaults (chain bones taken from `boneHints` in order when given, else
// placeholders the problems list flags). Returns its index.
i32 rigAddOp(fmt::OcRigData& rig, fmt::OcRigOpKind kind, const std::vector<std::string>& boneHints = {});
bool rigRemoveOp(fmt::OcRigData& rig, i32 index);
// Moves an op by `delta` places, clamped; returns the new index or -1.
i32 rigMoveOp(fmt::OcRigData& rig, i32 index, i32 delta);

// Which value of an op a reference is set on.
enum class RigSlot : u8 { Target, Hint, Rotation, Weight };
// Points a slot at a literal, an input or a bone: position. An Input must name a declared input of the matching type
// (vec3 for Target / Hint, quat for Rotation, float for Weight); a Bone needs a name and is only for Target / Hint.
// Switching back to Literal clears the reference (the literal value is kept). False when refused.
bool rigSetValueRef(fmt::OcRigData& rig, i32 op, RigSlot slot, fmt::OcRigRef::Kind kind, const std::string& name = {},
                    const Vec3& offset = {0, 0, 0});

// Adds a named input (a unique name is derived from `name`); returns its index.
i32 rigAddInput(fmt::OcRigData& rig, const std::string& name, fmt::OcRigInputType type);
// Removes an input; every reference to it falls back to its literal.
bool rigRemoveInput(fmt::OcRigData& rig, i32 index);
// Renames an input and every @ref to it (target, hint, rotation, weight). False for an empty or taken name.
bool rigRenameInput(fmt::OcRigData& rig, i32 index, const std::string& name);

// Everything wrong or suspicious, against the skeleton when one is given: whatever valid() refuses, unknown bones,
// chain ops whose root and tip are not parent-linked, inputs nothing reads.
std::vector<std::string> rigProblems(const fmt::OcRigData& rig, const fmt::OcSkeleton* skel);

class RigEditor final : public AssetEditor {
public:
    explicit RigEditor(std::string path);

    const std::string& path() const override { return path_; }
    std::string title() const override;
    bool dirty() const override { return dirty_; }
    void draw(Engine& e) override;
    bool save(std::string* why) override;
    void onFileChanged() override;

    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const fmt::OcRigData& rig() const { return rig_; }
    i32 selectedOp() const { return selOp_; }
    void selectOp(i32 i) { selOp_ = i; }
    std::vector<std::string> problems() const { return rigProblems(rig_, skelOk_ ? &skel_ : nullptr); }

    void pushUndo() { history_.push(rig_); }
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void markDirty() { dirty_ = true; }

    // Each pushes one undo entry; the ImGui layer calls these, and so do tests.
    i32 addOp(fmt::OcRigOpKind kind);
    bool removeOp(i32 index);
    i32 moveOp(i32 index, i32 delta);
    bool setValueRef(i32 op, RigSlot slot, fmt::OcRigRef::Kind kind, const std::string& name = {}, const Vec3& offset = {0, 0, 0});
    i32 addInput(const std::string& name, fmt::OcRigInputType type);
    bool removeInput(i32 index);
    bool renameInput(i32 index, const std::string& name);

    // The preview skeleton (an editor preference): an absolute .ocskel path.
    bool setPreviewSkeleton(const std::string& path);
    bool skeletonLoaded() const { return skelOk_; }

private:
    void loadFromDisk();

    std::string path_;
    fmt::OcRigData rig_;
    bool loaded_ = false;
    std::string loadError_;
    bool dirty_ = false;
    SnapshotUndo<fmt::OcRigData> history_;
    i32 selOp_ = -1;
    fmt::OcSkeleton skel_;
    bool skelOk_ = false;
    std::string skelPath_;

#if AVER_WITH_IMGUI
    bool gesture_ = false;
    bool endGesture_ = false;
    fmt::OcRigData before_;
    RigView view_;
    bool framed_ = false;
    std::map<std::string, Vec3> testVec_;        // preview value of each vec3 input (model space; dragged in the view)
    std::map<std::string, f32> testFloat_;
    std::vector<std::string> skelChoices_;
    bool skelScanned_ = false;
    i32 selBone_ = -1;
    void touch(bool changed);
    void drawOps();
    void drawOpForm(fmt::OcRigOp& op, i32 index);
    void drawInputs();
    void drawProblemsTab();
    void drawPreview(float width, float height);
    void scanSkeletons();
    bool boneCombo(const char* label, std::string& bone);
    void valueRow(i32 index, fmt::OcRigOp& op, RigSlot slot, const char* label, Vec3& literal, bool isVec);
#endif
};

std::unique_ptr<AssetEditor> makeRigEditor(const std::string& path);

} // namespace aver::editor
