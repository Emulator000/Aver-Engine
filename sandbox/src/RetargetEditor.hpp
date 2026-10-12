#pragma once
// The IK Retargeter editor tab: an .ocretarget opened as an asset. A chain mapping table (source and target bone
// combos, mode, rotation, translation, anchor, scale), an Auto-map that proposes chains with a confidence column and
// is never applied until accepted, the root panel with the computed scales, two side-by-side skeleton views (a source
// clip's pose and the live retargeted pose, unmapped target bones in grey), and a Batch panel that writes retargeted
// .ocanim files. Semantics: docs/ANIM_IK_RETARGET.md.
//
// THE HOOK INTO SandboxApp.cpp is the factory line beside the other animation editors:
//   assetEditors_.registerFactory(&editor::makeRetargetEditor);   // .ocretarget
// plus the files in sandbox/CMakeLists.txt. The edits are FREE FUNCTIONS so a headless test reaches them.
#include "AssetEditor.hpp"
#include "RigPreview.hpp"
#include "SnapshotUndo.hpp"

#include "aver/anim/Retarget.hpp"
#include "aver/anim/RetargetAsset.hpp"

#include <memory>
#include <string>
#include <vector>

namespace aver::editor {

// A new retargeter between two skeleton files (content-relative references): names, no chains, auto scales.
anim::RetargetAsset rtgStarter(const std::string& srcRef, const std::string& dstRef);

// A chain name not already used.
std::string rtgUniqueChainName(const anim::RetargetAsset& a, const std::string& base);
// Appends a chain, returns its index; -1 for an empty bone name.
i32 rtgAddChain(anim::RetargetAsset& a, const std::string& name, const std::string& srcStart, const std::string& srcEnd,
                const std::string& dstStart, const std::string& dstEnd, anim::RtgMode mode = anim::RtgMode::Fk);
bool rtgRemoveChain(anim::RetargetAsset& a, i32 index);
// Re-points a chain at other bones. False for an out-of-range index or an empty name.
bool rtgSetBones(anim::RetargetAsset& a, i32 index, const std::string& srcStart, const std::string& srcEnd,
                 const std::string& dstStart, const std::string& dstEnd);

// Proposes chains between two skeletons by name logic (anim::autoMapChains).
anim::AutoMapResult rtgAutoMap(const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst);
// Applies the accepted chains of a draft (acceptMask[i] true) to the asset: a chain of the same name is replaced, a
// new one appended. With `applyRoot` the draft's pelvis and root-motion pairs are copied too. Returns how many
// chains were taken. A mask shorter than the draft rejects the rest.
u32 rtgApplyMap(anim::RetargetAsset& a, const anim::AutoMapResult& draft, const std::vector<bool>& acceptMask, bool applyRoot);

// Everything wrong with the asset against its two skeletons: unknown bones, paths that are not parent-linked, a bone in
// two chains, oneToOne counts that differ, ik chains with fewer than two bones, chains outside the root's subtree.
std::vector<std::string> rtgProblems(const anim::RetargetAsset& a, const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst);

struct RtgScales { f32 scaleH = 1.0f, scaleV = 1.0f; bool bound = false; };
// The scales the asset resolves to on these skeletons (auto ones computed from the rest heights).
RtgScales rtgScalePreview(const anim::RetargetAsset& a, const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst);

class RetargetEditor final : public AssetEditor {
public:
    explicit RetargetEditor(std::string path);

    const std::string& path() const override { return path_; }
    std::string title() const override;
    bool dirty() const override { return dirty_; }
    void draw(Engine& e) override;
    bool save(std::string* why) override;
    void onFileChanged() override;

    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const anim::RetargetAsset& asset() const { return asset_; }
    bool skeletonsLoaded() const { return srcOk_ && dstOk_; }
    const fmt::OcSkeleton& sourceSkeleton() const { return src_; }
    const fmt::OcSkeleton& targetSkeleton() const { return dst_; }
    i32 selectedChain() const { return selChain_; }
    void selectChain(i32 c) { selChain_ = c; }
    std::vector<std::string> problems() const;
    RtgScales scales() const;

    void pushUndo() { history_.push(asset_); }
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void markDirty() { dirty_ = true; bindStale_ = true; }

    // Each pushes one undo entry; the ImGui layer calls these, and so do tests.
    i32 addChain(const std::string& name, const std::string& srcStart, const std::string& srcEnd,
                 const std::string& dstStart, const std::string& dstEnd, anim::RtgMode mode);
    bool removeChain(i32 index);
    // Runs the auto-mapper into the draft (not applied).
    void autoMap();
    const anim::AutoMapResult& draft() const { return draft_; }
    std::vector<bool>& acceptMask() { return accept_; }
    // Applies the accepted draft chains as one undo entry. Returns how many were taken.
    u32 applyDraft(bool applyRoot);

    // The Batch panel's work, callable headless: retargets the clips (absolute paths) into `outDir`.
    anim::RetargetBatchResult runBatch(const std::vector<std::string>& clips, const std::string& outDir, const std::string& suffix,
                                       const anim::RetargetOptions& opt, bool overwrite);

private:
    void loadFromDisk();
    void loadSkeletons();
    void rebind();

    std::string path_;
    anim::RetargetAsset asset_;
    bool loaded_ = false;
    std::string loadError_;
    bool dirty_ = false;
    SnapshotUndo<anim::RetargetAsset> history_;
    fmt::OcSkeleton src_, dst_;
    bool srcOk_ = false, dstOk_ = false;
    std::string srcPath_, dstPath_;
    i32 selChain_ = -1;
    bool bindStale_ = true;
    anim::AutoMapResult draft_;
    std::vector<bool> accept_;
    anim::Retargeter rt_;
    bool rtOk_ = false;
    std::vector<anim::RetargetIssue> bindIssues_;

#if AVER_WITH_IMGUI
    bool gesture_ = false;
    bool endGesture_ = false;
    anim::RetargetAsset before_;
    RigView srcView_, dstView_;
    bool framed_ = false;
    std::vector<std::string> clipPaths_, clipNames_;
    i32 clipIndex_ = -1;
    fmt::OcAnimation clip_;
    bool clipOk_ = false;
    f32 clipTime_ = 0.0f;
    bool clipPlay_ = true;
    bool clipsScanned_ = false;
    std::vector<bool> batchSel_;
    char outDir_[512] = {};
    char suffix_[64] = {};
    bool suffixSet_ = false;
    anim::RetargetOptions opt_;
    bool overwrite_ = false;
    anim::RetargetBatchResult batch_;
    bool batchRan_ = false;
    void touch(bool changed);
    void drawTop();
    void drawChains();
    void drawChainDetails();
    void drawRoot();
    void drawDraft();
    void drawBatch();
    void drawProblems();
    void drawViews(float width, float height);
    void scanClips();
    void loadClip(i32 index);
    bool boneCombo(const char* label, const fmt::OcSkeleton& skel, std::string& bone);
#endif
};

std::unique_ptr<AssetEditor> makeRetargetEditor(const std::string& path);

} // namespace aver::editor
