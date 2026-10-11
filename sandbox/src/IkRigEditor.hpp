#pragma once
// The IK Rig editor tab: an .ocikrig opened as an asset. Left, the skeleton's bones; middle, a skeleton view with
// the chains coloured and the goals as handles you drag (the real solver runs live), a sample clip and a synthetic
// ground (height and slope) for foot placement; right, the chains table, joint limits, feet, body and a problems
// list. Semantics: docs/ANIM_IK_RETARGET.md.
//
// THE HOOK INTO SandboxApp.cpp is the factory line beside the other animation editors:
//   assetEditors_.registerFactory(&editor::makeIkRigEditor);   // .ocikrig
// plus the files in sandbox/CMakeLists.txt.
//
// The edits are FREE FUNCTIONS, so a headless test reaches them (see BtEditor.hpp for the argument).
#include "AssetEditor.hpp"
#include "RigPreview.hpp"
#include "SnapshotUndo.hpp"

#include "aver/anim/IkRigAsset.hpp"
#include "aver/anim/IkRigSolver.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aver::editor {

// A new rig for a skeleton file (content-relative reference): foot defaults, no chains.
anim::IkRigAsset ikrigStarter(const std::string& skeletonRef);
// A new rig with chains already suggested from bone names (the retargeter's role logic): spine as CCD, arms and
// legs as two-bone (FABRIK when they are not three bones), the pelvis as the body, feet on the legs.
anim::IkRigAsset ikrigSuggest(const fmt::OcSkeleton& skel, const std::string& skeletonRef);

// A chain name not already used: `base`, then `base 2`, `base 3`...
std::string ikrigUniqueChainName(const anim::IkRigAsset& a, const std::string& base);
// Appends a chain (priority one past the lowest), returns its index. -1 for an empty bone name.
i32 ikrigAddChain(anim::IkRigAsset& a, const std::string& name, const std::string& start, const std::string& end,
                  anim::IkSolverKind solver = anim::IkSolverKind::Fabrik);
// Removes the chain and the feet that named it.
bool ikrigRemoveChain(anim::IkRigAsset& a, i32 index);
// Moves a chain within the file order by `delta`; returns the new index or -1.
i32 ikrigMoveChain(anim::IkRigAsset& a, i32 index, i32 delta);
bool ikrigSetPriority(anim::IkRigAsset& a, i32 index, i32 priority);
// Renames a chain and repairs feet[].chain. False for an empty or taken name.
bool ikrigRenameChain(anim::IkRigAsset& a, i32 index, const std::string& name);

// Adds a limit for a bone, or updates the one it has. Returns its index, -1 for an empty bone or an angle outside [0,180].
i32 ikrigAddLimit(anim::IkRigAsset& a, const std::string& bone, f32 swing1, f32 swing2, f32 twist);
bool ikrigRemoveLimit(anim::IkRigAsset& a, const std::string& bone);
// Copies swing1 / swing2 / twist (3 per bone, ragdoll::RagdollDef::limitsDeg) onto every bone of the rig's chains
// (the tip excluded), keeping any axis a limit already carries. Returns how many limits were written.
u32 ikrigImportLimits(anim::IkRigAsset& a, const fmt::OcSkeleton& skel, const f32* limitsDeg3PerBone, usize boneCount);
// The ragdoll default (45 / 45 / 30) for every bone of every chain that asks for limits.
u32 ikrigResetLimits(anim::IkRigAsset& a, const fmt::OcSkeleton& skel);

// Adds foot placement to a leg chain (the chain's end is the ankle). Returns the index, -1 for an unknown chain or a duplicate.
i32 ikrigAddFoot(anim::IkRigAsset& a, const std::string& chain, const std::string& toe);
bool ikrigRemoveFoot(anim::IkRigAsset& a, i32 index);

// Everything wrong or suspicious about the rig against its skeleton, one sentence each: unknown bones, a chain that
// is not parent-linked, twobone chains that are not three bones, two chains of equal priority sharing a bone,
// duplicate goal names, a foot with no leg chain -- and anything IkRigAsset::valid() refuses.
std::vector<std::string> ikrigProblems(const anim::IkRigAsset& a, const fmt::OcSkeleton& skel);

// The skeleton file an .ocikrig names: tries the content root (the nearest ancestor directory named "Content"), the
// rig's own folder and the parent of the root. Empty when none exists.
std::string ikrigResolveSkeleton(const std::string& rigPath, const std::string& skeletonRef);

class IkRigEditor final : public AssetEditor {
public:
    explicit IkRigEditor(std::string path);

    const std::string& path() const override { return path_; }
    std::string title() const override;
    bool dirty() const override { return dirty_; }
    void draw(Engine& e) override;
    bool save(std::string* why) override;
    void onFileChanged() override;

    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const anim::IkRigAsset& asset() const { return asset_; }
    bool skeletonLoaded() const { return skelOk_; }
    const fmt::OcSkeleton& skeleton() const { return skel_; }
    i32 selectedChain() const { return selChain_; }
    void selectChain(i32 c) { selChain_ = c; }
    std::vector<std::string> problems() const;

    void pushUndo() { history_.push(asset_); }
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void markDirty() { dirty_ = true; bindStale_ = true; }

    // Each pushes one undo entry and selects what it made; the ImGui layer calls these, and so do tests.
    i32 addChain(const std::string& name, const std::string& start, const std::string& end, anim::IkSolverKind solver);
    bool removeChain(i32 index);
    i32 moveChain(i32 index, i32 delta);
    bool setPriority(i32 index, i32 priority);
    bool renameChain(i32 index, const std::string& name);
    i32 addLimit(const std::string& bone, f32 s1, f32 s2, f32 tw);
    bool removeLimit(const std::string& bone);
    u32 importLimits(const f32* limitsDeg3PerBone, usize boneCount);
    i32 addFoot(const std::string& chain, const std::string& toe);
    bool removeFoot(i32 index);

private:
    void loadFromDisk();
    void loadSkeleton();
    void rebind();

    std::string path_;
    anim::IkRigAsset asset_;
    bool loaded_ = false;
    std::string loadError_;
    bool dirty_ = false;
    SnapshotUndo<anim::IkRigAsset> history_;
    fmt::OcSkeleton skel_;
    bool skelOk_ = false;
    std::string skelPath_;
    i32 selChain_ = -1;
    bool bindStale_ = true;

    // The live preview: the real stack over a pose, a synthetic ground and draggable goals.
    anim::IkRigBound bound_;
    anim::IkRigState state_;
    anim::IkRigReport report_;
    anim::IkScratch scratch_;
    anim::Pose restPose_;

#if AVER_WITH_IMGUI
    bool gesture_ = false;
    bool endGesture_ = false;
    anim::IkRigAsset before_;
    RigView view_;
    bool framed_ = false;
    std::map<std::string, Vec3> goalPos_;        // goal name -> model-space position the handle holds
    std::map<std::string, bool> goalOn_;
    f32 groundHeight_ = 0.0f;
    f32 groundSlopeDeg_ = 0.0f;
    bool groundOn_ = true;
    bool reachTest_ = false;
    f32 reachRadius_ = 40.0f;
    f32 reachPhase_ = 0.0f;
    i32 selBone_ = -1;
    std::string status_;
    std::vector<std::string> clipPaths_;
    std::vector<std::string> clipNames_;
    i32 clipIndex_ = -1;
    fmt::OcAnimation clip_;
    bool clipOk_ = false;
    f32 clipTime_ = 0.0f;
    bool clipsScanned_ = false;
    void touch(bool changed);
    void drawLeft();
    void drawPreview(float width, float height);
    void drawRight();
    void drawChains();
    void drawLimits();
    void drawFeet();
    void drawBody();
    void drawProblems();
    void scanClips();
    void loadClip(i32 index);
    bool boneCombo(const char* label, std::string& bone);
#endif
};

std::unique_ptr<AssetEditor> makeIkRigEditor(const std::string& path);

} // namespace aver::editor
