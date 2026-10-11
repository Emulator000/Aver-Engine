// RetargetEditorTest -- the .ocretarget editor tab's headless core: the chain edits, the auto-map draft and its accept
// mask, the problems list, the scales it shows, the batch driver against files in a temp tree, and the tab's load /
// save / dirty / undo bookkeeping. The editor source is compiled directly with AVER_WITH_IMGUI undefined.
#include "../../anim/src/RetargetFixture.hpp"

#include "RetargetEditor.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace aver;
using namespace aver::editor;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool anyContains(const std::vector<std::string>& v, const std::string& needle) {
    for (const std::string& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO("retarget editor core");

    const rtfix::Rig mannequin = rtfix::makeRig(rtfix::Params{rtfix::Naming::Ue, 1.0f, false, 3, true});
    const rtfix::Rig hero = rtfix::makeRig(rtfix::Params{rtfix::Naming::Mixamo, 1.3f, true, 4, false});

    AVER_INFO("=== the starter and the chain edits ===");
    {
        anim::RetargetAsset a = rtgStarter("Characters/mannequin.ocskel", "Characters/hero.ocskel");
        check(a.source == "Characters/mannequin.ocskel" && a.target == "Characters/hero.ocskel" && a.name == "mannequin_to_hero" && a.chains.empty(),
              "the starter names both skeletons and starts with no chains");
        check(rtgAddChain(a, "Arm", "", "b", "c", "d") == -1, "an empty bone name is refused");
        check(rtgAddChain(a, "Arm", "upperarm_l", "hand_l", "mixamorig:LeftArm", "mixamorig:LeftHand") == 0 &&
                  rtgAddChain(a, "Arm", "upperarm_r", "hand_r", "mixamorig:RightArm", "mixamorig:RightHand", anim::RtgMode::Ik) == 1 && a.chains[1].name == "Arm 2" &&
                  a.chains[1].mode == anim::RtgMode::Ik, "chains are added with unique names and a mode");
        check(rtgSetBones(a, 0, "a", "b", "c", "d") && a.chains[0].dstEnd == "d" && !rtgSetBones(a, 7, "a", "b", "c", "d") && !rtgSetBones(a, 0, "", "b", "c", "d"),
              "rtgSetBones re-points a chain, refusing a bad index or an empty name");
        check(rtgRemoveChain(a, 0) && a.chains.size() == 1 && !rtgRemoveChain(a, 5), "remove");
    }

    AVER_INFO("=== auto-map: a draft, accepted by mask ===");
    {
        const anim::AutoMapResult draft = rtgAutoMap(mannequin.skel, hero.skel);
        check(draft.asset.chains.size() >= 8 && draft.confidence.size() == draft.asset.chains.size(), "the draft has chains and a confidence for each");
        anim::RetargetAsset a = rtgStarter("m.ocskel", "h.ocskel");
        std::vector<bool> none(draft.asset.chains.size(), false);
        check(rtgApplyMap(a, draft, none, true) == 0 && a.chains.empty() && a.root.srcBone.empty(), "nothing accepted applies nothing (and not the root)");
        std::vector<bool> only(draft.asset.chains.size(), false);
        usize leftArm = 0;
        for (usize i = 0; i < draft.asset.chains.size(); ++i) if (draft.asset.chains[i].name == "LeftArm") leftArm = i;
        only[leftArm] = true;
        check(rtgApplyMap(a, draft, only, false) == 1 && a.chains.size() == 1 && a.chains[0].name == "LeftArm" && a.root.srcBone.empty(), "accepting one chain applies exactly it");
        a.chains[0].mode = anim::RtgMode::None;
        check(rtgApplyMap(a, draft, only, false) == 1 && a.chains.size() == 1 && a.chains[0].mode == anim::RtgMode::Fk, "accepting it again replaces the chain of that name");
        std::vector<bool> all(draft.asset.chains.size(), true);
        check(rtgApplyMap(a, draft, all, true) == draft.asset.chains.size() && a.root.srcBone == "pelvis" && a.root.dstBone == "mixamorig:Hips",
              "accepting all adds the rest and, asked to, the pelvis pair");
        check(rtgApplyMap(a, draft, std::vector<bool>{true}, false) == 1, "a mask shorter than the draft rejects the rest");
        std::string why;
        a.source = "m.ocskel"; a.target = "h.ocskel";
        check(a.valid(mannequin.skel, hero.skel, &why), "the applied asset validates against the skeletons: " + why);
        check(rtgProblems(a, mannequin.skel, hero.skel).empty(), "and has no problems");
        const RtgScales sc = rtgScalePreview(a, mannequin.skel, hero.skel);
        check(sc.bound && std::fabs(sc.scaleH - 1.3f) < 1e-3f && std::fabs(sc.scaleV - 1.3f) < 1e-3f, "the scale preview is the 1.3x rest-height ratio");
        a.root.scaleH = anim::RtgScale{false, 2.0f};
        check(std::fabs(rtgScalePreview(a, mannequin.skel, hero.skel).scaleH - 2.0f) < 1e-4f, "an explicit number overrides the auto scale");
        check(!rtgScalePreview(rtgStarter("a", "b"), fmt::OcSkeleton{}, hero.skel).bound, "no skeleton, no scales");
    }

    AVER_INFO("=== problems ===");
    {
        anim::RetargetAsset a = rtgStarter("m", "h");
        check(anyContains(rtgProblems(a, mannequin.skel, hero.skel), "no chains"), "an empty asset says to run Auto-map");
        rtgAddChain(a, "Bad", "no_such", "hand_l", "mixamorig:LeftArm", "mixamorig:LeftHand");
        rtgAddChain(a, "Backwards", "hand_l", "upperarm_l", "mixamorig:LeftHand", "mixamorig:LeftArm");
        rtgAddChain(a, "Overlap", "upperarm_r", "hand_r", "mixamorig:RightArm", "mixamorig:RightHand");
        rtgAddChain(a, "Overlap2", "lowerarm_r", "hand_r", "mixamorig:RightForeArm", "mixamorig:RightHand");
        rtgAddChain(a, "Counts", "spine_01", "spine_03", "mixamorig:Spine", "mixamorig:Spine3");
        a.chains.back().rotation = anim::RtgRotation::OneToOne;
        rtgAddChain(a, "Single", "foot_l", "foot_l", "mixamorig:LeftFoot", "mixamorig:LeftFoot", anim::RtgMode::Ik);
        a.root.dstBone = "mixamorig:LeftArm";
        const std::vector<std::string> p = rtgProblems(a, mannequin.skel, hero.skel);
        check(anyContains(p, "source bone 'no_such'"), "an unknown bone");
        check(anyContains(p, "not an ancestor of 'upperarm_l' on the source"), "a path that is not parent-linked");
        check(anyContains(p, "is in both"), "a bone in two chains");
        check(anyContains(p, "oneToOne"), "a oneToOne chain with different counts");
        check(anyContains(p, "ik chain needs at least two bones"), "an ik chain of one bone");
        check(anyContains(p, "not under the target root bone"), "a chain outside the root's subtree");
    }

    const std::string root = (std::filesystem::temp_directory_path() / "aver-retarget-editor-test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root + "/Proj/Content/Characters", ec);
    std::filesystem::create_directories(root + "/Proj/Content/Retargets", ec);
    std::string why;
    check(fmt::saveOcSkel(root + "/Proj/Content/Characters/mannequin.ocskel", mannequin.skel, &why) &&
              fmt::saveOcSkel(root + "/Proj/Content/Characters/hero.ocskel", hero.skel, &why), "(skeletons written) " + why);
    const std::string rtgPath = root + "/Proj/Content/Retargets/m2h.ocretarget";
    {
        anim::RetargetAsset a = rtgStarter("Characters/mannequin.ocskel", "Characters/hero.ocskel");
        check(anim::saveRetarget(rtgPath, a, &why), "(retargeter written) " + why);
    }

    AVER_INFO("=== the tab ===");
    {
        RetargetEditor ed(rtgPath);
        check(ed.loaded() && ed.skeletonsLoaded() && !ed.dirty(), "it loads, finds both skeletons, starts clean");
        check(ed.sourceSkeleton().bones.size() == mannequin.skel.bones.size() && ed.targetSkeleton().bones.size() == hero.skel.bones.size(), "with their bones");
        check(anyContains(ed.problems(), "no chains"), "an empty retargeter reports it");
        ed.autoMap();
        check(!ed.draft().asset.chains.empty() && ed.acceptMask().size() == ed.draft().asset.chains.size() && ed.acceptMask()[0],
              "autoMap fills the draft with everything accepted, and applies nothing");
        check(ed.asset().chains.empty() && !ed.dirty(), "(nothing applied yet)");
        const u32 n = ed.applyDraft(true);
        check(n > 8 && ed.dirty() && ed.canUndo() && ed.problems().empty(), "applying the draft is one undoable edit and leaves no problems");
        check(std::fabs(ed.scales().scaleH - 1.3f) < 1e-3f, "the tab shows the computed scale");
        ed.undo();
        check(ed.asset().chains.empty() && ed.canRedo(), "undo takes the whole draft out");
        ed.redo();
        check(ed.asset().chains.size() == n, "redo puts it back");
        const i32 added = ed.addChain("Extra", "neck_01", "head", "mixamorig:Neck", "mixamorig:Head", anim::RtgMode::Fk);
        check(added >= 0 && ed.selectedChain() == added, "addChain selects the chain");
        check(ed.removeChain(added) && ed.selectedChain() == -1, "and removeChain clears the selection");
        check(ed.save(&why) && !ed.dirty(), "save: " + why);
        anim::RetargetAsset back;
        check(anim::loadRetarget(rtgPath, back, &why) && back.chains.size() == n && back.root.dstBone == "mixamorig:Hips", "the file holds the applied draft");

        // The batch driver, through the tab.
        std::filesystem::create_directories(root + "/Proj/Content/Characters/Anim", ec);
        fmt::OcAnimation clip;
        clip.duration = 1.0f;
        clip.flags = fmt::kOcAnimLoop;
        clip.skeletonRef = "mannequin";
        fmt::OcTrack t;
        t.boneIndex = static_cast<u16>(mannequin.upper[0]);
        t.channels = fmt::kOcChannelRotation;
        t.times = {0.0f, 1.0f};
        t.values = {0, 0, 0, 1, 0, 0.3f, 0, 0.95f};
        clip.tracks.push_back(t);
        const std::string c1 = root + "/Proj/Content/Characters/Anim/swing.ocanim", c2 = root + "/Proj/Content/Characters/Anim/alien.ocanim";
        fmt::OcAnimation alien = clip;
        alien.skeletonRef = "somebody";
        check(fmt::saveOcAnim(c1, clip, &why) && fmt::saveOcAnim(c2, alien, &why), "(clips written) " + why);
        const std::string out = root + "/Proj/Content/Characters/Retargeted";
        const anim::RetargetBatchResult r = ed.runBatch({c1, c2}, out, "_hero", anim::RetargetOptions{}, false);
        check(r.written == 1 && r.skipped == 1 && r.failed == 0 && std::filesystem::exists(out + "/swing_hero.ocanim"),
              "the batch writes the matching clip and skips the other skeleton's");
        const anim::RetargetBatchResult again = ed.runBatch({c1}, out, "_hero", anim::RetargetOptions{}, false);
        check(again.written == 1, "and a second run overwrites its own earlier output");

        // Disk changes.
        ed.addChain("Keep", "neck_01", "head", "mixamorig:Neck", "mixamorig:Head", anim::RtgMode::Fk);
        anim::RetargetAsset other = back;
        other.name = "Other";
        anim::saveRetarget(rtgPath, other, &why);
        ed.onFileChanged();
        check(ed.asset().name != "Other", "a dirty tab keeps its edits when the file changes");
        ed.save(&why);
        anim::saveRetarget(rtgPath, other, &why);
        ed.onFileChanged();
        check(ed.asset().name == "Other", "a clean tab reloads");
    }
    {
        const std::string bad = root + "/Proj/Content/Retargets/broken.ocretarget";
        std::ofstream(bad) << "nonsense";
        RetargetEditor ed(bad);
        check(!ed.loaded() && !ed.loadError().empty(), "a corrupt file reports a load error");
        std::string w;
        check(!ed.save(&w) && ed.addChain("A", "a", "b", "c", "d", anim::RtgMode::Fk) == -1, "and refuses to save or edit");
        check(ed.runBatch({"x.ocanim"}, root + "/o", "_x", anim::RetargetOptions{}, false).failed == 1, "a batch with no skeletons fails cleanly");
        check(makeRetargetEditor(bad) != nullptr && makeRetargetEditor(root + "/x.txt") == nullptr, "the factory claims .ocretarget only");
    }

    std::filesystem::remove_all(root, ec);
    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
