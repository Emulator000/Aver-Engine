// RigEditorTest -- the .ocrig editor tab's headless core: the op edits, the value-source pickers (literal | @input |
// bone:), input renames that repair every reference, the problems list, and the tab's load / save / dirty / undo
// bookkeeping -- including that a rig expressible in OCRIG 1 is saved as OCRIG 1 byte for byte. The editor source is
// compiled directly with AVER_WITH_IMGUI undefined.
#include "../../anim/src/RetargetFixture.hpp"

#include "RigEditor.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace aver;
using namespace aver::editor;
using fmt::OcRigInputType;
using fmt::OcRigOpKind;
using fmt::OcRigRef;

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

static std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("==================================================");
    AVER_INFO("control rig editor core");

    const rtfix::Rig rig = rtfix::makeRig(rtfix::Params{});

    AVER_INFO("=== ops ===");
    {
        fmt::OcRigData r = rigStarter("");
        check(r.name == "NewRig" && r.valid(), "an empty starter is valid");
        for (const OcRigOpKind k : {OcRigOpKind::TwoBoneIk, OcRigOpKind::AimAt, OcRigOpKind::Fabrik, OcRigOpKind::Ccd, OcRigOpKind::CopyBone, OcRigOpKind::SetBone})
            rigAddOp(r, k);
        std::string why;
        check(r.ops.size() == 6 && r.valid(&why), "every op kind is added with defaults that validate: " + why);
        check(r.ops[2].iterations == 10 && r.ops[3].iterations == 12 && r.ops[3].tolerance == 0.5f, "FABRIK and CCD get their own defaults");
        const i32 idx = rigAddOp(r, OcRigOpKind::TwoBoneIk, {"thigh_l", "calf_l", "foot_l"});
        check(r.ops[static_cast<usize>(idx)].root == "thigh_l" && r.ops[static_cast<usize>(idx)].tip == "foot_l", "bone hints fill the chain");
        check(rigMoveOp(r, idx, -2) == idx - 2 && r.ops[static_cast<usize>(idx - 2)].root == "thigh_l" && rigMoveOp(r, 0, -5) == 0, "move clamps to the ends");
        check(rigRemoveOp(r, 0) && r.ops.size() == 6 && !rigRemoveOp(r, 99), "remove");
    }

    AVER_INFO("=== value sources ===");
    {
        fmt::OcRigData r = rigStarter("v");
        const i32 vec = rigAddInput(r, "Goal", OcRigInputType::Vec3);
        const i32 flt = rigAddInput(r, "Amount", OcRigInputType::Float);
        const i32 qt = rigAddInput(r, "Grip", OcRigInputType::Quat);
        (void)vec; (void)flt; (void)qt;
        const i32 op = rigAddOp(r, OcRigOpKind::Fabrik, {"upperarm_r", "hand_r"});
        check(rigSetValueRef(r, op, RigSlot::Target, OcRigRef::Kind::Input, "Goal") && r.ops[0].targetRef.kind == OcRigRef::Kind::Input && r.ops[0].targetRef.name == "Goal",
              "a vec3 input is accepted as the goal");
        check(!rigSetValueRef(r, op, RigSlot::Target, OcRigRef::Kind::Input, "Amount") && !rigSetValueRef(r, op, RigSlot::Target, OcRigRef::Kind::Input, "Nope"),
              "a float input or an unknown one is refused");
        check(rigSetValueRef(r, op, RigSlot::Hint, OcRigRef::Kind::Bone, "elbow_r", Vec3{0, -30, 0}) && r.ops[0].hintRef.offset.y == -30.0f &&
                  !rigSetValueRef(r, op, RigSlot::Hint, OcRigRef::Kind::Bone, ""), "a bone: reference carries its offset and needs a name");
        check(rigSetValueRef(r, op, RigSlot::Rotation, OcRigRef::Kind::Input, "Grip") && !rigSetValueRef(r, op, RigSlot::Rotation, OcRigRef::Kind::Bone, "hand_r") &&
                  !rigSetValueRef(r, op, RigSlot::Rotation, OcRigRef::Kind::Input, "Goal"), "a rotation takes a quat input only");
        check(rigSetValueRef(r, op, RigSlot::Weight, OcRigRef::Kind::Input, "Amount") && r.ops[0].weightInput == "Amount" && !rigSetValueRef(r, op, RigSlot::Weight, OcRigRef::Kind::Input, "Goal"),
              "the weight takes a float input only");
        r.ops[0].hasTipRot = true;
        std::string why;
        check(r.valid(&why), "the rig with every reference validates: " + why);
        check(rigSetValueRef(r, op, RigSlot::Target, OcRigRef::Kind::Literal) && r.ops[0].targetRef.kind == OcRigRef::Kind::Literal &&
                  rigSetValueRef(r, op, RigSlot::Weight, OcRigRef::Kind::Literal) && r.ops[0].weightInput.empty(), "back to literal clears the reference");
        check(!rigSetValueRef(r, 9, RigSlot::Target, OcRigRef::Kind::Literal), "a bad op index is refused");

        // Renames repair; removals fall back.
        rigSetValueRef(r, op, RigSlot::Target, OcRigRef::Kind::Input, "Goal");
        rigSetValueRef(r, op, RigSlot::Weight, OcRigRef::Kind::Input, "Amount");
        check(rigRenameInput(r, 0, "GunTarget") && r.ops[0].targetRef.name == "GunTarget", "renaming an input repairs the reference to it");
        check(rigRenameInput(r, 1, "Aim") && r.ops[0].weightInput == "Aim", "including the weight");
        check(!rigRenameInput(r, 1, "GunTarget") && !rigRenameInput(r, 1, "") && !rigRenameInput(r, 1, "has space") && !rigRenameInput(r, 1, "a=b"), "a taken, empty or unwritable name is refused");
        check(rigAddInput(r, "Aim", OcRigInputType::Float) == 3 && r.inputs[3].name == "Aim2", "a new input with a taken name is uniquified");
        check(rigRemoveInput(r, 0) && r.ops[0].targetRef.kind == OcRigRef::Kind::Literal && r.valid(&why), "removing an input turns its references back into literals: " + why);
        check(!rigRemoveInput(r, 99), "out of range");
    }

    AVER_INFO("=== problems ===");
    {
        fmt::OcRigData r = rigStarter("p");
        rigAddOp(r, OcRigOpKind::Fabrik, {"upperarm_r", "hand_l"});                 // not linked
        rigAddOp(r, OcRigOpKind::TwoBoneIk, {"thigh_l", "calf_l", "ball_l"});       // not a parent chain
        rigAddOp(r, OcRigOpKind::CopyBone, {"nothing", "pelvis"});
        rigAddInput(r, "Unused", OcRigInputType::Float);
        const std::vector<std::string> p = rigProblems(r, &rig.skel);
        check(anyContains(p, "'upperarm_r' is not an ancestor of 'hand_l'"), "a fabrik op that is not a parent path");
        check(anyContains(p, "not a parent-linked chain"), "a two-bone op that is not a chain");
        check(anyContains(p, "dst bone 'nothing'"), "an unknown bone");
        check(anyContains(p, "input 'Unused' is not read"), "an input nothing reads");
        check(!anyContains(rigProblems(r, nullptr), "ancestor"), "without a skeleton only the file-level problems are listed");
    }

    const std::string root = (std::filesystem::temp_directory_path() / "aver-rig-editor-test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    const std::string golden =
        "OCRIG 1\n"
        "NAME ArmReach\n"
        "OP twobone root=shoulder mid=elbow tip=wrist goal=80,0,0 pole=0,-100,20 weight=1\n"
        "OP aim bone=head at=0,200,150 axis=0,0,1 weight=0.5\n";

    AVER_INFO("=== the tab ===");
    {
        const std::string path = root + "/arm.ocrig";
        { std::ofstream f(path, std::ios::binary); f << golden; }
        RigEditor ed(path);
        check(ed.loaded() && !ed.dirty() && ed.rig().ops.size() == 2, "it loads a v1 rig clean");
        std::string why;
        check(ed.save(&why), "save: " + why);
        check(slurp(path) == golden, "a v1 rig saves back as OCRIG 1, byte for byte");

        const i32 inp = ed.addInput("LookAt", OcRigInputType::Vec3);
        check(inp == 0 && ed.dirty() && ed.canUndo(), "adding an input is an undoable edit");
        check(ed.setValueRef(1, RigSlot::Target, OcRigRef::Kind::Input, "LookAt"), "pointing an op at it");
        check(ed.save(&why) && slurp(path).rfind("OCRIG 2\n", 0) == 0, "a rig that needs v2 is written as OCRIG 2");
        fmt::OcRigData back;
        check(fmt::loadOcRig(path, back, &why) && back.ops[1].targetRef.name == "LookAt" && back.inputs.size() == 1, "and it reads back with the input and the reference");
        check(ed.renameInput(0, "Gaze") && ed.rig().ops[1].targetRef.name == "Gaze", "a rename through the tab repairs the reference");
        ed.undo();
        check(ed.rig().ops[1].targetRef.name == "LookAt" && ed.rig().inputs[0].name == "LookAt", "and undoes whole");
        ed.undo();
        ed.undo();
        check(ed.rig().inputs.empty() && ed.rig().ops[1].targetRef.kind == OcRigRef::Kind::Literal, "undoing back to the loaded rig removes the input");
        ed.redo();
        check(ed.rig().inputs.size() == 1, "redo");
        check(ed.addOp(OcRigOpKind::Ccd) == 2 && ed.selectedOp() == 2 && ed.moveOp(2, -1) == 1 && ed.removeOp(1), "add, move and remove ops through the tab");

        check(ed.setPreviewSkeleton("") == false && !ed.skeletonLoaded(), "no preview skeleton to begin with");
        check(fmt::saveOcSkel(root + "/rig.ocskel", rig.skel, &why) && ed.setPreviewSkeleton(root + "/rig.ocskel") && ed.skeletonLoaded(), "a preview skeleton loads");
        check(anyContains(ed.problems(), "'shoulder'") || anyContains(ed.problems(), "root bone"), "and bones missing from it are reported");

        ed.addInput("Another", OcRigInputType::Float);
        { std::ofstream f(path, std::ios::binary); f << golden; }
        ed.onFileChanged();
        check(ed.rig().inputs.size() >= 1, "a dirty tab keeps its edits when the file changes");
        ed.save(&why);
        { std::ofstream f(path, std::ios::binary); f << golden; }
        ed.onFileChanged();
        check(ed.rig().inputs.empty() && !ed.dirty(), "a clean tab reloads");
    }
    {
        const std::string bad = root + "/broken.ocrig";
        { std::ofstream f(bad, std::ios::binary); f << "OCRIG 1\nOP wibble\n"; }
        RigEditor ed(bad);
        std::string w;
        check(!ed.loaded() && !ed.loadError().empty() && !ed.save(&w), "a rig that does not parse reports a load error and refuses to save");
        check(ed.addOp(OcRigOpKind::AimAt) == -1 && ed.addInput("x", OcRigInputType::Float) == -1, "and refuses edits");
        check(makeRigEditor(bad) != nullptr && makeRigEditor(root + "/x.txt") == nullptr, "the factory claims .ocrig only");
    }

    std::filesystem::remove_all(root, ec);
    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
