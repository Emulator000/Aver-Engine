// The string-table editor tab's headless core: the multi-culture grid model, key edits that cascade, missing cells,
// save merging into each culture file (comments survive), Add culture, and Gather / Report on a temp content tree.
//
// AVER_WITH_IMGUI IS DELIBERATELY UNDEFINED for this target, FoliageTypeEditorTest's arrangement.
#include "StringTableEditor.hpp"

#include "aver/core/Log.hpp"
#include "aver/platform/FileSystem.hpp"

#include <filesystem>
#include <string>

using namespace aver;
namespace fs = std::filesystem;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("  FAIL  {}", what);
    ++g_failures;
}

static std::string g_dir;   // the content root

static std::string path(const std::string& rel) { return (fs::path(g_dir) / rel).generic_string(); }

static void writeText(const std::string& rel, const std::string& text) {
    const fs::path p = fs::path(g_dir) / rel;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    writeFileText(p.generic_string(), text);
}

static std::string readText(const std::string& rel) {
    std::string t;
    readFileText(path(rel), t);
    return t;
}

static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

static void fixture() {
    std::error_code ec;
    fs::remove_all(g_dir, ec);
    writeText("Localization/en/UI.ocstrings",
              "OCSTRINGS 1\nCULTURE en\n# Main menu\nENTRY Menu.Play \"Play\"\nENTRY Menu.Quit \"Quit\" NOTE \"short\"\n");
    writeText("Localization/de/UI.ocstrings", "OCSTRINGS 1\nCULTURE de\nENTRY Menu.Play \"Spielen\"\n");
    fs::create_directories(fs::path(g_dir) / "Localization/pt-BR", ec);
}

int main() {
    std::error_code ec;
    g_dir = (fs::temp_directory_path() / "aver_stringtable_editor_test").generic_string();

    AVER_INFO("=== the grid model ===");
    fixture();
    {
        editor::StringTableEditor ed(path("Localization/de/UI.ocstrings"));
        check(ed.loaded() && ed.table() == "UI", "loads, the table is the file stem");
        check(ed.cultureCount() == 3 && ed.culture(0) == "en" && ed.culture(1) == "de" && ed.culture(2) == "pt-BR",
              "cultures: the default first, then the folders sorted");
        check(ed.keyCount() == 2 && ed.key(0) == "Menu.Play" && ed.key(1) == "Menu.Quit", "keys are the union, default order first");
        check(ed.text(0, 0) == "Play" && ed.text(1, 0) == "Spielen" && ed.note(1) == "short", "texts and the note");
        check(!ed.isMissing(1, 0) && ed.isMissing(1, 1) && ed.isMissing(2, 0) && ed.isMissing(2, 1), "a cell a culture lacks is missing");
        check(ed.missingCount() == 3, "three cells are missing");
        check(!ed.dirty(), "a fresh tab is clean");
        check(ed.title().find("UI") == 0, "the title names the table");
    }

    AVER_INFO("=== a configured default culture ===");
    {
        editor::StringTableEditorHooks h;
        h.defaultCulture = [] { return std::string("de"); };
        editor::setStringTableEditorHooks(h);
        editor::StringTableEditor ed(path("Localization/en/UI.ocstrings"));
        check(ed.culture(0) == "de" && ed.culture(1) == "en", "the hook picks the default culture");
        check(ed.key(0) == "Menu.Play", "key order follows the default culture's file");
        editor::setStringTableEditorHooks({});
    }

    AVER_INFO("=== edits ===");
    fixture();
    {
        editor::StringTableEditor ed(path("Localization/en/UI.ocstrings"));
        check(ed.addKey("Menu.Options") && ed.keyCount() == 3 && ed.hasEntry(0, 2) && ed.text(0, 2).empty() && !ed.hasEntry(1, 2),
              "addKey adds the key to every culture (empty; present only in the default)");
        check(!ed.addKey("Menu.Options") && !ed.editError().empty(), "a duplicate key is refused");
        check(!ed.addKey("has space") && !ed.addKey("") && !ed.addKey("a:b"), "invalid keys are refused");
        check(ed.dirty() && ed.canUndo(), "edits dirty the tab");

        check(ed.setText(1, 1, "Beenden") && ed.hasEntry(1, 1) && !ed.isMissing(1, 1), "typing into a missing cell creates the entry");
        check(ed.setText(1, 1, "") && !ed.hasEntry(1, 1) && ed.isMissing(1, 1), "an empty edit in a non-default culture un-translates it");
        check(ed.setText(0, 2, "") && ed.hasEntry(0, 2), "the default culture may hold an empty text");
        check(!ed.setText(9, 0, "x") && !ed.setText(0, 9, "x"), "out of range is refused");

        check(ed.renameKey(0, "Menu.Start") && ed.key(0) == "Menu.Start" && ed.text(1, 0) == "Spielen", "renameKey renames the row in every culture");
        check(!ed.renameKey(1, "Menu.Start") && !ed.renameKey(1, "bad key"), "a rename onto a taken or invalid key is refused");
        check(ed.setNote(1, "keep short") && ed.note(1) == "keep short", "setNote");
        check(ed.removeKey(2) && ed.keyCount() == 2, "removeKey");
    }

    AVER_INFO("=== undo ===");
    fixture();
    {
        editor::StringTableEditor ed(path("Localization/en/UI.ocstrings"));
        ed.addKey("A");
        ed.setText(1, 0, "Eins");
        ed.undo();
        check(ed.text(1, 0) == "Spielen", "undo reverts a text");
        ed.undo();
        check(ed.keyCount() == 2, "...and an added key");
        ed.redo();
        check(ed.keyCount() == 3, "redo");
    }

    AVER_INFO("=== save merges into each file ===");
    fixture();
    {
        editor::StringTableEditor ed(path("Localization/en/UI.ocstrings"));
        ed.setText(0, 0, "Start");
        ed.setText(1, 1, "Beenden");
        ed.addKey("Menu.Options");
        ed.setText(0, 2, "Options");
        ed.setText(2, 0, "Jogar");              // pt-BR has no file yet
        ed.addCulture("fr");
        std::string why;
        check(ed.save(&why) && !ed.dirty(), "save clears the dirty flag: " + why);

        const std::string en = readText("Localization/en/UI.ocstrings");
        check(has(en, "# Main menu") && has(en, "ENTRY Menu.Play \"Start\"") && has(en, "ENTRY Menu.Options \"Options\"") && has(en, "NOTE \"short\""),
              "the default file keeps its comment and note and gains the edits");
        const std::string de = readText("Localization/de/UI.ocstrings");
        check(has(de, "ENTRY Menu.Quit \"Beenden\"") && has(de, "ENTRY Menu.Play \"Spielen\"") && !has(de, "Menu.Options"),
              "the de file gains its translation; an untranslated key is not written");
        check(has(readText("Localization/pt-BR/UI.ocstrings"), "ENTRY Menu.Play \"Jogar\""), "a culture with no file gets one");
        check(fs::exists(fs::path(g_dir) / "Localization/fr/UI.ocstrings") && !has(readText("Localization/fr/UI.ocstrings"), "ENTRY"),
              "an added culture gets a folder and a header-only file");

        editor::StringTableEditor again(path("Localization/en/UI.ocstrings"));
        check(again.cultureCount() == 4 && again.culture(2) == "fr" && again.text(1, 1) == "Beenden" && again.keyCount() == 3, "and reads back");
        check(again.missingCount() > 0 && !again.dirty(), "(fr is entirely missing)");
    }
    {
        check(editor::StringTableEditor(path("Localization/en/UI.ocstrings")).addCulture("pt_br") == false, "an existing culture (any spelling) is refused");
        editor::StringTableEditor ed(path("Localization/en/UI.ocstrings"));
        check(!ed.addCulture("pt_br") && !ed.addCulture("!!") && !ed.addCulture(""), "existing and malformed cultures are refused");
    }

    AVER_INFO("=== the files change on disk ===");
    fixture();
    {
        editor::StringTableEditor dirtyTab(path("Localization/en/UI.ocstrings"));
        dirtyTab.setText(0, 0, "Mine");
        writeText("Localization/en/UI.ocstrings", "OCSTRINGS 1\nENTRY Menu.Play \"Theirs\"\n");
        dirtyTab.onFileChanged();
        check(dirtyTab.text(0, 0) == "Mine" && dirtyTab.dirty(), "a dirty tab keeps its edits");
        editor::StringTableEditor cleanTab(path("Localization/en/UI.ocstrings"));
        writeText("Localization/en/UI.ocstrings", "OCSTRINGS 1\nENTRY Menu.Play \"Newer\"\n");
        cleanTab.onFileChanged();
        check(cleanTab.text(0, 0) == "Newer" && cleanTab.keyCount() == 1, "a clean tab reloads");
    }

    AVER_INFO("=== a broken culture file is left alone ===");
    fixture();
    writeText("Localization/de/UI.ocstrings", "this is not a string table\n");
    {
        editor::StringTableEditor ed(path("Localization/en/UI.ocstrings"));
        check(ed.loaded() && !ed.diagnostics().empty(), "the problem is listed");
        ed.setText(1, 0, "Spielen");
        ed.setText(0, 0, "Start");
        std::string why;
        check(!ed.save(&why) && !why.empty(), "saving reports the culture it could not write");
        check(readText("Localization/de/UI.ocstrings") == "this is not a string table\n", "...and leaves the file untouched");
        check(has(readText("Localization/en/UI.ocstrings"), "\"Start\""), "...while the good files are written");
    }

    AVER_INFO("=== a csv table ===");
    fixture();
    writeText("Localization/en/Items.csv", "Key,Text,Note\nSword,Sword,a blade\n");
    writeText("Localization/de/Items.csv", "Key,Text\nSword,Schwert\n");
    {
        check(editor::makeStringTableEditor(path("Localization/en/Items.csv")) != nullptr, "a csv under Localization/<culture>/ gets the editor");
        check(editor::makeStringTableEditor(path("Data/items.csv")) == nullptr, "a data csv does not");
        editor::StringTableEditor ed(path("Localization/en/Items.csv"));
        check(ed.loaded() && ed.text(1, 0) == "Schwert" && ed.note(0) == "a blade", "a csv table loads");
        ed.addKey("Bow");
        ed.setText(0, 1, "Bow");
        check(ed.save(nullptr), "saves");
        check(readText("Localization/en/Items.csv") == "Key,Text,Note\nSword,Sword,a blade\nBow,Bow,\n" ||
              has(readText("Localization/en/Items.csv"), "Bow"), "as a csv again");
    }

    AVER_INFO("=== gather and report ===");
    fixture();
    writeText("UI/menu.ocui", "OCUI 1\nNAME Menu\nWIDGET Root panel -\nWIDGET Title text Root\nSET Title text \"@UI:Menu.Title=Welcome\"\n"
                              "WIDGET Play button Root\nSET Play text \"@UI:Menu.Play=Play\"\n");
    writeText("Scripts/Hud.cs", "class Hud { string a = Loc.Text(\"UI\", \"Hud.Ammo\", \"Ammo\"); }\n");
    {
        editor::StringTableEditor ed(path("Localization/en/UI.ocstrings"));
        ed.setText(0, 0, "Start");
        check(!ed.runReport() && !ed.editError().empty(), "Report on a dirty tab is refused: it reads the disk");
        ed.save(nullptr);

        check(ed.runReport() && ed.hasRun(), "Report runs on a clean tab");
        const fmt::LocReport& r = ed.lastRun().report;
        bool missingTitleDe = false;
        for (const auto& m : r.missing) if (m.culture == "de" && m.table == "UI" && m.key == "Menu.Title") missingTitleDe = true;
        check(missingTitleDe, "a key the layout uses is missing in de");
        check(ed.lastRun().usedKeys == 3, "three distinct keys are used (" + std::to_string(ed.lastRun().usedKeys) + ")");
        check(has(ed.lastRun().text, "Menu.Title"), "the text form names it");

        check(ed.runGather() && ed.lastRun().added == 2, "Gather adds the two keys the default culture lacks");
        const std::string en = readText("Localization/en/UI.ocstrings");
        check(has(en, "ENTRY Menu.Title \"Welcome\"") && has(en, "ENTRY Hud.Ammo \"Ammo\"") && has(en, "# Main menu"), "...with the inline defaults, merged into the file");
        check(ed.keyCount() == 4 && !ed.dirty(), "and the tab shows them");
        check(ed.runGather() && ed.lastRun().added == 0, "gathering again adds nothing");
    }

    fs::remove_all(g_dir, ec);
    AVER_INFO("StringTableEditor tests: {} checks, {} failure(s)", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
