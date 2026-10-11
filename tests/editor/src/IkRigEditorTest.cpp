// IkRigEditorTest -- the .ocikrig editor tab's headless core: the structural edits (renames repair the feet that name a
// chain), the suggested starter rig, limits and ragdoll-style import, the problems list, skeleton lookup, and the tab's
// load / save / dirty / undo bookkeeping. Same arrangement as MontageEditorTest: the editor source is compiled
// directly with AVER_WITH_IMGUI undefined.
#include "../../anim/src/RetargetFixture.hpp"

#include "IkRigEditor.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace aver;
using namespace aver::editor;
using aver::anim::IkSolverKind;

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
    AVER_INFO("IK rig editor core");

    const rtfix::Rig rig = rtfix::makeRig(rtfix::Params{});
    const fmt::OcSkeleton& skel = rig.skel;

    AVER_INFO("=== the starter and the suggestion ===");
    {
        const anim::IkRigAsset s = ikrigStarter("Characters/hero.ocskel");
        check(s.skeleton == "Characters/hero.ocskel" && s.name == "heroIk" && s.chains.empty(), "ikrigStarter names the skeleton and starts empty");
        const anim::IkRigAsset a = ikrigSuggest(skel, "Characters/hero.ocskel");
        std::string why;
        check(a.valid(&why), "the suggested rig is valid: " + why);
        auto find = [&](const char* n) -> const anim::IkRigChainDef* {
            for (const auto& c : a.chains) if (c.name == n) return &c;
            return nullptr;
        };
        const auto* spine = find("Spine");
        const auto* legL = find("LeftLeg");
        const auto* armR = find("RightArm");
        check(spine && spine->solver == IkSolverKind::Ccd && spine->goal == "Chest" && spine->priority == 0, "the spine is a CCD chain with a Chest goal at priority 0");
        check(legL && legL->solver == IkSolverKind::TwoBone && legL->start == "thigh_l" && legL->end == "foot_l" && legL->goal == "LeftFoot", "legs are two-bone from the thigh to the foot");
        check(armR && armR->solver == IkSolverKind::TwoBone && armR->start == "upperarm_r" && armR->end == "hand_r" && armR->goal == "RightHand" && armR->tipRotation,
              "arms are two-bone with a hand goal that follows rotation");
        check(a.body.bone == "pelvis" && a.body.goal == "Hips", "the pelvis is the body");
        check(a.feet.size() == 2 && a.feet[0].chain == "LeftLeg" && a.feet[0].toe == "ball_l", "each leg gets a foot with its toe");
        check(ikrigProblems(a, skel).empty(), "and the suggestion has no problems");
    }

    AVER_INFO("=== chains ===");
    {
        anim::IkRigAsset a = ikrigStarter("h.ocskel");
        check(ikrigAddChain(a, "Arm", "", "hand_l") == -1, "an empty bone name is refused");
        const i32 c0 = ikrigAddChain(a, "Arm", "upperarm_l", "hand_l", IkSolverKind::TwoBone);
        const i32 c1 = ikrigAddChain(a, "Arm", "upperarm_r", "hand_r", IkSolverKind::Fabrik);
        check(c0 == 0 && c1 == 1 && a.chains[1].name == "Arm 2", "a second chain with the same name is uniquified");
        check(a.chains[0].priority == 0 && a.chains[1].priority == 1, "priorities count up");
        check(a.chains[1].iterations == 10 && ikrigAddChain(a, "Tail", "a", "b", IkSolverKind::Ccd) == 2 && a.chains[2].iterations == 12 && a.chains[2].toleranceCm == 0.5f,
              "a CCD chain starts with the CCD defaults");
        check(ikrigAddFoot(a, "Arm", "ball_l") == 0 && ikrigAddFoot(a, "Arm", "") == -1 && ikrigAddFoot(a, "Nope", "") == -1, "a foot goes on an existing chain once");
        check(ikrigRenameChain(a, 0, "LeftArm") && a.feet[0].chain == "LeftArm", "renaming a chain repairs the foot that named it");
        check(!ikrigRenameChain(a, 0, "Arm 2") && !ikrigRenameChain(a, 0, "") && ikrigRenameChain(a, 0, "LeftArm"), "a taken or empty name is refused, the same name is fine");
        check(ikrigMoveChain(a, 0, 1) == 1 && a.chains[1].name == "LeftArm" && ikrigMoveChain(a, 1, 99) == 2, "moving clamps to the ends");
        check(ikrigSetPriority(a, 0, 5) && !ikrigSetPriority(a, 0, -1) && !ikrigSetPriority(a, 9, 1), "priority is set, a negative or out-of-range one refused");
        check(ikrigRemoveChain(a, 2) && a.feet.empty(), "removing a chain removes its feet");
        check(!ikrigRemoveChain(a, 9), "an out-of-range remove is refused");
    }

    AVER_INFO("=== limits ===");
    {
        anim::IkRigAsset a = ikrigSuggest(skel, "h.ocskel");
        check(ikrigAddLimit(a, "calf_l", 5, 5, 10) >= 0 && a.limits.size() == 1, "a limit is added");
        check(ikrigAddLimit(a, "calf_l", 7, 8, 9) == 0 && a.limits.size() == 1 && a.limits[0].swing2 == 8.0f, "adding again updates it");
        check(ikrigAddLimit(a, "calf_l", 200, 0, 0) == -1 && ikrigAddLimit(a, "", 1, 1, 1) == -1, "an angle over 180 or an empty bone is refused");
        check(ikrigRemoveLimit(a, "calf_l") && !ikrigRemoveLimit(a, "calf_l") && a.limits.empty(), "removing reports whether it was there");

        std::vector<f32> deg(skel.bones.size() * 3);
        for (usize i = 0; i < skel.bones.size(); ++i) { deg[i * 3] = 10.0f + static_cast<f32>(i); deg[i * 3 + 1] = 20.0f; deg[i * 3 + 2] = 30.0f; }
        a.limits.clear();
        const u32 n = ikrigImportLimits(a, skel, deg.data(), skel.bones.size());
        std::set<std::string> seen;
        for (const auto& l : a.limits) seen.insert(l.bone);
        check(n > 0 && n == a.limits.size(), "an import writes one limit per chain bone, " + std::to_string(n));
        check(seen.count("thigh_l") && seen.count("calf_l") && !seen.count("foot_l") && !seen.count("hand_l"), "tips are not given limits");
        const auto it = std::find_if(a.limits.begin(), a.limits.end(), [](const auto& l) { return l.bone == "calf_l"; });
        check(it != a.limits.end() && it->swing2 == 20.0f && it->twist == 30.0f, "the numbers come from the ragdoll-shaped array");
        check(ikrigResetLimits(a, skel) == n && std::all_of(a.limits.begin(), a.limits.end(), [](const auto& l) { return l.swing1 == 45.0f && l.twist == 30.0f; }),
              "reset puts every one back to 45 / 45 / 30");
        check(ikrigImportLimits(a, skel, nullptr, 0) == 0, "a null array imports nothing");
    }

    AVER_INFO("=== the problems list ===");
    {
        anim::IkRigAsset a = ikrigStarter("h.ocskel");
        ikrigAddChain(a, "Good", "thigh_l", "foot_l", IkSolverKind::TwoBone);
        a.chains[0].goal = "G";
        ikrigAddChain(a, "Bad", "no_such", "foot_l");
        ikrigAddChain(a, "Backwards", "foot_l", "thigh_l");
        ikrigAddChain(a, "Four", "pelvis", "foot_l", IkSolverKind::TwoBone);
        a.chains[3].priority = 0;
        ikrigAddChain(a, "Orphan", "upperarm_l", "hand_l");
        a.body.bone = "nope";
        ikrigAddLimit(a, "ghost", 1, 1, 1);
        a.feet.push_back({});
        a.feet.back().chain = "Good";
        a.feet.back().toe = "toeless";
        const std::vector<std::string> p = ikrigProblems(a, skel);
        check(anyContains(p, "start bone 'no_such'"), "an unknown start bone");
        check(anyContains(p, "not an ancestor"), "a chain that is not parent-linked");
        check(anyContains(p, "twobone needs exactly 3 bones"), "a twobone chain that is not three bones");
        check(anyContains(p, "share bone") && anyContains(p, "same priority"), "equal-priority chains sharing a bone");
        check(anyContains(p, "nothing drives it"), "a chain with no goal and no foot");
        check(anyContains(p, "body bone 'nope'"), "an unknown body bone");
        check(anyContains(p, "limit on 'ghost'"), "a limit on an unknown bone");
        check(anyContains(p, "toe bone 'toeless'"), "an unknown toe bone");
        a.chains[1].name = "Good";
        check(anyContains(ikrigProblems(a, skel), "duplicate chain name"), "and anything valid() refuses is listed too");
    }

    AVER_INFO("=== finding the skeleton ===");
    const std::string root = (std::filesystem::temp_directory_path() / "aver-ikrig-editor-test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root + "/Proj/Content/Characters", ec);
    std::filesystem::create_directories(root + "/Proj/Content/Rigs", ec);
    std::string why;
    check(fmt::saveOcSkel(root + "/Proj/Content/Characters/hero.ocskel", skel, &why), "(skeleton written) " + why);
    {
        const std::string rigPath = root + "/Proj/Content/Rigs/hero.ocikrig";
        const std::string found = ikrigResolveSkeleton(rigPath, "Characters/hero.ocskel");
        check(!found.empty() && std::filesystem::path(found).filename() == "hero.ocskel", "a content-relative reference resolves through the Content root");
        check(ikrigResolveSkeleton(rigPath, "Characters/nothing.ocskel").empty() && ikrigResolveSkeleton(rigPath, "").empty(), "a missing or empty one is empty");
    }

    AVER_INFO("=== the tab ===");
    {
        const std::string rigPath = root + "/Proj/Content/Rigs/hero.ocikrig";
        anim::IkRigAsset a = ikrigSuggest(skel, "Characters/hero.ocskel");
        check(anim::saveIkRig(rigPath, a, &why), "(rig written) " + why);

        IkRigEditor ed(rigPath);
        check(ed.loaded() && ed.skeletonLoaded() && !ed.dirty(), "it loads, finds its skeleton and starts clean");
        check(ed.skeleton().bones.size() == skel.bones.size(), "with the skeleton's bones");
        check(ed.problems().empty(), "and the suggested rig reports no problems");
        check(ed.title().find("hero.ocikrig") != std::string::npos, "the title names the file");

        const usize chains = ed.asset().chains.size();
        const i32 idx = ed.addChain("Tail", "spine_01", "spine_02", IkSolverKind::Fabrik);
        check(idx == static_cast<i32>(chains) && ed.dirty() && ed.canUndo() && ed.selectedChain() == idx, "an edit adds the chain, dirties, selects and records undo");
        ed.undo();
        check(ed.asset().chains.size() == chains && ed.canRedo(), "undo removes it");
        ed.redo();
        check(ed.asset().chains.size() == chains + 1, "and redo brings it back");
        check(ed.renameChain(0, "Renamed") && ed.asset().feet.empty() == false, "a rename goes through");
        ed.removeChain(static_cast<i32>(ed.asset().chains.size()) - 1);
        check(!ed.asset().chains.empty() && ed.save(&why) && !ed.dirty(), "save clears dirty: " + why);

        anim::IkRigAsset back;
        check(anim::loadIkRig(rigPath, back, &why) && back.chains[0].name == "Renamed", "and the file holds the edit");

        const u32 imported = [&] {
            std::vector<f32> deg(ed.skeleton().bones.size() * 3, 33.0f);
            return ed.importLimits(deg.data(), ed.skeleton().bones.size());
        }();
        check(imported > 0 && ed.dirty() && ed.asset().limits.size() == imported, "importLimits is one undoable edit");
        ed.undo();
        check(ed.asset().limits.empty(), "that undoes whole");

        // A file changing on disk under a dirty tab keeps the edits; a clean tab reloads.
        ed.addLimit("calf_l", 5, 5, 5);
        anim::IkRigAsset other = back;
        other.name = "Other";
        anim::saveIkRig(rigPath, other, &why);
        ed.onFileChanged();
        check(ed.asset().limits.size() == 1, "a dirty tab keeps its unsaved edits when the file changes");
        ed.save(&why);
        anim::saveIkRig(rigPath, other, &why);
        ed.onFileChanged();
        check(ed.asset().name == "Other" && !ed.dirty(), "a clean tab reloads");
    }
    {
        const std::string bad = root + "/Proj/Content/Rigs/broken.ocikrig";
        std::ofstream(bad) << "{ this is not json";
        IkRigEditor ed(bad);
        check(!ed.loaded() && !ed.loadError().empty(), "a corrupt file reports a load error");
        std::string w;
        check(!ed.save(&w) && !w.empty(), "and refuses to save over it");
        check(ed.addChain("X", "a", "b", IkSolverKind::Fabrik) == -1, "edits are refused");
        check(makeIkRigEditor(bad) != nullptr && makeIkRigEditor(root + "/x.txt") == nullptr, "the factory claims .ocikrig only");
    }

    std::filesystem::remove_all(root, ec);
    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
