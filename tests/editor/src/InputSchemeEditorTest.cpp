// The .ocinput editor tab's headless core: load / save / dirty / undo, the trigger and modifier
// setters (validation, canonical storage, undo, ordering), and the rename / remove cascade into
// chord and combo references.
//
// AVER_WITH_IMGUI IS DELIBERATELY UNDEFINED for this target, FoliageTypeEditorTest's arrangement: the
// drawing half of InputSchemeEditor.cpp sits behind that macro, so what is exercised is exactly the
// part that needs no window.
#include "InputSchemeEditor.hpp"

#include "aver/core/Log.hpp"
#include "aver/formats/OcInput.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace aver;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("  FAIL  {}", what);
    ++g_failures;
}

static void writeText(const std::string& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

static std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static const char* kFixture =
    "OCINPUT 1\n"
    "# hand-written note that must survive a save\n"
    "NAME Test\n"
    "ACTION Move axis2\n"
    "ACTION Fire digital\n"
    "ACTION Jump digital\n"
    "ACTION Mod digital\n"
    "BIND Move key W component y\n"
    "BIND Fire key MouseLeft\n"
    "BIND Jump key Space\n"
    "BIND Mod key LeftCtrl\n"
    "CONTEXT Test priority 3\n";

static usize actionIndex(const fmt::OcInputData& d, const std::string& name) {
    for (usize i = 0; i < d.actions.size(); ++i) if (d.actions[i].name == name) return i;
    return d.actions.size();
}

int main() {
    AVER_INFO("InputSchemeEditorTest");

    const std::string dir = (std::filesystem::temp_directory_path() / "aver-inputscheme-editor").string();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/Test.ocinput";
    writeText(path, kFixture);

    using editor::InputSchemeEditor;
    using SpecKind = InputSchemeEditor::SpecKind;
    using SpecOwner = InputSchemeEditor::SpecOwner;

    AVER_INFO("load, edit, undo, save, reload");
    {
        InputSchemeEditor ed(path);
        check(ed.loaded(), "loads: " + ed.loadError());
        check(!ed.dirty() && !ed.canUndo(), "starts clean with nothing to undo");
        check(ed.data().actions.size() == 4 && ed.data().bindings.size() == 4, "the records came through");

        ed.setContextPriority(7);
        check(ed.dirty() && ed.canUndo(), "an edit marks it dirty and undoable");
        ed.undo();
        check(ed.data().contextPriority == 3, "undo restores the priority");
        ed.redo();
        check(ed.data().contextPriority == 7, "redo reapplies it");
        std::string why;
        check(ed.save(&why), "saves: " + why);
        check(!ed.dirty(), "a save clears dirty");

        InputSchemeEditor again(path);
        check(again.loaded() && again.data().contextPriority == 7, "reloads the saved priority");
        check(readText(path).find("# hand-written note that must survive a save") != std::string::npos,
              "a hand-written comment survives the text-merge save");
    }
    writeText(path, kFixture);

    AVER_INFO("a scheme with no triggers saves back unchanged");
    {
        InputSchemeEditor ed(path);
        std::string why;
        check(ed.save(&why), "saves: " + why);
        check(readText(path) == kFixture, "byte-identical when nothing was edited");
    }

    AVER_INFO("the starter scheme survives a load and save");
    {
        const std::string starterPath = dir + "/Starter.ocinput";
        const std::string text = fmt::writeOcinput(editor::inputSchemeStarterData());
        writeText(starterPath, text);
        InputSchemeEditor ed(starterPath);
        check(ed.loaded(), "loads: " + ed.loadError());
        std::string why;
        check(ed.save(&why) && readText(starterPath) == text, "saves identically");
    }

    AVER_INFO("addSpec / setSpec / removeSpec / moveSpec on an action and on a binding");
    {
        InputSchemeEditor ed(path);
        const usize fire = actionIndex(ed.data(), "Fire");
        const SpecOwner fireAction{true, fire};
        const SpecOwner fireBinding{false, 1};   // BIND Fire key MouseLeft

        check(ed.addSpec(fireAction, SpecKind::Trigger, "hold:0.5"), "adds an action trigger");
        check(ed.data().actions[fire].triggers.size() == 1 && ed.data().actions[fire].triggers[0] == "hold:0.5",
              "stored as canonical text");
        check(ed.dirty() && ed.canUndo(), "dirty and undoable");

        check(ed.addSpec(fireAction, SpecKind::Trigger, "HOLD:0.5:0.15:ONCE"), "keywords are case-insensitive");
        check(ed.data().actions[fire].triggers[1] == "hold:0.5:once", "and stored canonical (default actuation dropped)");

        check(ed.setSpec(fireAction, SpecKind::Trigger, 1, "tap:0.18"), "replaces an entry");
        check(ed.data().actions[fire].triggers[1] == "tap:0.18", "the new text is there");

        ed.moveSpec(fireAction, SpecKind::Trigger, 1, -1);
        check(ed.data().actions[fire].triggers[0] == "tap:0.18" && ed.data().actions[fire].triggers[1] == "hold:0.5",
              "moves an entry up");
        ed.moveSpec(fireAction, SpecKind::Trigger, 0, -1);
        check(ed.data().actions[fire].triggers[0] == "tap:0.18", "moving past the start is a no-op");

        ed.removeSpec(fireAction, SpecKind::Trigger, 0);
        check(ed.data().actions[fire].triggers.size() == 1 && ed.data().actions[fire].triggers[0] == "hold:0.5",
              "removes an entry");

        check(ed.addSpec(fireBinding, SpecKind::Modifier, "deadzone:0.1:0.9:axial"), "adds a binding modifier");
        check(ed.addSpec(fireBinding, SpecKind::Modifier, "negate:xz"), "adds a second one");
        check(ed.data().bindings[1].modifiers.size() == 2 && ed.data().bindings[1].modifiers[1] == "negate:xz",
              "in order");
        ed.moveSpec(fireBinding, SpecKind::Modifier, 0, 1);
        check(ed.data().bindings[1].modifiers[0] == "negate:xz", "modifier order can change");

        std::string why;
        check(ed.save(&why), "saves: " + why);
        fmt::OcInputData reread;
        std::string err;
        check(fmt::parseOcinput(readText(path), reread, &err), "the saved file parses: " + err);
        check(reread.actions[fire].triggers.size() == 1 && reread.bindings[1].modifiers.size() == 2,
              "and carries the specs");

        // Undo walks back through every spec edit.
        int undone = 0;
        while (ed.canUndo() && undone < 50) { ed.undo(); ++undone; }
        check(ed.data().actions[fire].triggers.empty() && ed.data().bindings[1].modifiers.empty(),
              "undo returns to the loaded state");
    }
    writeText(path, kFixture);

    AVER_INFO("a rejected spec changes nothing");
    {
        InputSchemeEditor ed(path);
        const SpecOwner fire{true, actionIndex(ed.data(), "Fire")};
        check(ed.addSpec(fire, SpecKind::Trigger, "tap"), "seed one valid trigger");
        ed.save(nullptr);
        check(!ed.dirty() && ed.canUndo(), "saved, with history kept");
        const fmt::OcInputData before = ed.data();

        check(!ed.addSpec(fire, SpecKind::Trigger, "hold:-1"), "negative seconds rejected");
        check(!ed.specError().empty(), "with a reason");
        check(!ed.addSpec(fire, SpecKind::Trigger, "frobnicate"), "unknown kind rejected");
        check(!ed.addSpec(fire, SpecKind::Modifier, "deadzone:0.9:0.1"), "lower >= upper rejected");
        check(!ed.addSpec(fire, SpecKind::Trigger, "combo:0.4:Fire"), "a combo of one step rejected");
        check(!ed.addSpec(fire, SpecKind::Trigger, "chord:Nope"), "a chord on an undeclared action rejected");
        check(!ed.addSpec(fire, SpecKind::Trigger, "chord:Fire"), "a chord on its own action rejected");
        check(!ed.addSpec(fire, SpecKind::Trigger, "hold:0.5 once"), "whitespace rejected");
        check(!ed.setSpec(fire, SpecKind::Trigger, 0, "hold:oops"), "a bad replacement rejected");
        check(!ed.setSpec(fire, SpecKind::Trigger, 9, "tap"), "an out-of-range entry rejected");
        check(!ed.addSpec(SpecOwner{true, 99}, SpecKind::Trigger, "tap"), "an out-of-range owner rejected");
        check(!ed.dirty(), "none of that dirtied the tab");
        check(ed.data().actions[fire.index].triggers == before.actions[fire.index].triggers, "data untouched");

        int undone = 0;
        while (ed.canUndo() && undone < 50) { ed.undo(); ++undone; }
        check(undone == 1, "and the undo history gained no entries (only the one valid add)");

        ed.addSpec(fire, SpecKind::Trigger, "tap");
        check(ed.addSpec(fire, SpecKind::Trigger, "pressed"), "a valid spec after the errors is accepted");
        check(ed.specError().empty(), "and clears the error");
    }
    writeText(path, kFixture);

    AVER_INFO("rename and remove cascade into chord and combo references");
    {
        InputSchemeEditor ed(path);
        const usize save = actionIndex(ed.data(), "Jump");
        const SpecOwner jump{true, save};
        check(ed.addSpec(jump, SpecKind::Trigger, "chord:Mod"), "Jump chords on Mod");
        check(ed.addSpec(SpecOwner{true, actionIndex(ed.data(), "Move")}, SpecKind::Trigger, "combo:0.4:Fire,Fire,Jump"),
              "Move has a combo over Fire, Fire, Jump");
        check(ed.addSpec(SpecOwner{false, 2}, SpecKind::Trigger, "chord:Mod"), "and the Jump binding chords on Mod");

        ed.renameAction(actionIndex(ed.data(), "Mod"), "Shift");
        check(ed.data().actions[save].triggers[0] == "chord:Shift", "the action-level chord was rewritten");
        check(ed.data().bindings[2].triggers[0] == "chord:Shift", "the binding-level chord was rewritten");
        check(ed.data().bindings[3].action == "Shift", "and the binding followed its action");

        ed.renameAction(actionIndex(ed.data(), "Fire"), "Shoot");
        const usize move = actionIndex(ed.data(), "Move");
        check(ed.data().actions[move].triggers[0] == "combo:0.4:Shoot,Shoot,Jump", "every step of a combo was rewritten");

        // A name the spec grammar cannot carry is refused while something refers to the action.
        ed.renameAction(actionIndex(ed.data(), "Shift"), "Left Shift");
        check(actionIndex(ed.data(), "Left Shift") == ed.data().actions.size() && !ed.specError().empty(),
              "an unsafe new name for a referenced action is refused with a reason");

        std::string why;
        check(ed.save(&why), "saves: " + why);
        fmt::OcInputData reread;
        std::string err;
        check(fmt::parseOcinput(readText(path), reread, &err), "the renamed file reloads: " + err);

        ed.removeAction(actionIndex(ed.data(), "Shift"));
        check(ed.data().actions[actionIndex(ed.data(), "Jump")].triggers.empty(), "removing Shift drops the chord on it");
        check(ed.data().bindings.size() == 3 && ed.data().bindings[2].triggers.empty() == true,
              "and its binding went with it, leaving no dangling chord");
        check(ed.save(&why), "saves: " + why);
        reread = fmt::OcInputData{};
        check(fmt::parseOcinput(readText(path), reread, &err), "the file after the removal reloads: " + err);

        ed.removeAction(actionIndex(ed.data(), "Shoot"));
        check(ed.data().actions[actionIndex(ed.data(), "Move")].triggers.empty(), "removing a combo step drops the whole combo");
        check(ed.save(&why), "saves: " + why);
        reread = fmt::OcInputData{};
        check(fmt::parseOcinput(readText(path), reread, &err), "that file reloads too: " + err);
    }

    if (g_failures == 0) { AVER_INFO("InputSchemeEditorTest: ALL PASS ({} checks)", g_checks); return 0; }
    AVER_ERROR("InputSchemeEditorTest: {} of {} checks FAILED", g_failures, g_checks);
    return 1;
}
