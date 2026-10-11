// The data table editor tab's headless core: load, every row/column/cell edit (including the refusals), undo/redo, save
// round trips that keep the CSV delimiter and line ending, and the dirty-tab-keeps-edits rule.
//
// AVER_WITH_IMGUI IS DELIBERATELY UNDEFINED for this target, FoliageTypeEditorTest's arrangement.
#include "DataTableEditor.hpp"

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

static std::string g_dir;

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

static const char* kItems = "Name,Damage:int,Weight:float\r\nsword,25,1.5\r\nbow,12,0.75\r\n";

int main() {
    std::error_code ec;
    g_dir = (fs::temp_directory_path() / "aver_datatable_editor_test").generic_string();
    fs::remove_all(g_dir, ec);
    fs::create_directories(g_dir, ec);

    AVER_INFO("=== load ===");
    writeText("Data/items.csv", kItems);
    {
        editor::DataTableEditor ed(path("Data/items.csv"));
        check(ed.loaded() && ed.data().rowCount() == 2 && ed.data().columns.size() == 2, "a csv loads");
        check(ed.data().columns[0].type == fmt::OcTableType::Int && ed.data().columns[0].hinted, "type hints are read");
        check(!ed.dirty() && !ed.canUndo(), "a fresh tab is clean");
        check(ed.title().find("items.csv") == 0, "the title is the file name");
    }
    {
        editor::DataTableEditor missing(path("Data/none.csv"));
        check(!missing.loaded() && !missing.loadError().empty(), "a missing file reports why");
        std::string why;
        check(!missing.save(&why) && !why.empty(), "...and cannot save");
    }

    AVER_INFO("=== row edits ===");
    {
        editor::DataTableEditor ed(path("Data/items.csv"));
        check(ed.addRow() && ed.data().rowNames.back() == "NewRow" && ed.data().cells.size() == 6, "addRow picks a fresh name and adds empty cells");
        check(ed.addRow() && ed.data().rowNames.back() == "NewRow1", "...the next one is NewRow1");
        check(!ed.addRow("SWORD") && !ed.editError().empty(), "a duplicate row name (case-insensitive) is refused");
        const usize rows = ed.data().rowCount();
        check(ed.addRow("axe") && ed.data().rowCount() == rows + 1, "a named row is added");
        check(ed.dirty() && ed.canUndo(), "edits dirty the tab and push undo");

        check(ed.renameRow(0, "blade") && ed.data().rowNames[0] == "blade", "renameRow");
        check(!ed.renameRow(1, "BLADE") && ed.data().rowNames[1] == "bow", "a rename to another row's name is refused");
        check(ed.renameRow(0, "BLADE") && ed.data().rowNames[0] == "BLADE", "...but a case change of its own name is fine");
        check(!ed.renameRow(0, "  ") , "an empty name is refused");

        check(ed.duplicateRow(0) && ed.data().rowNames[1] == "BLADECopy" && ed.data().cell(1, 0) == "25" && ed.data().cell(2, 0) == "12",
              "duplicateRow copies the cells right after the original");
        check(ed.removeRow(1) && ed.data().rowNames[1] == "bow" && ed.data().cell(1, 0) == "12", "removeRow keeps the cells aligned");
        check(!ed.removeRow(99) && !ed.setCell(99, 0, "x") && !ed.setCell(0, 99, "x"), "out-of-range edits are refused");
    }

    AVER_INFO("=== column edits ===");
    {
        editor::DataTableEditor ed(path("Data/items.csv"));
        check(ed.addColumn() && ed.data().columns.back().name == "NewColumn" && ed.data().columns.size() == 3 && ed.data().cells.size() == 6,
              "addColumn adds a string column of empty cells");
        check(ed.data().cell(0, 0) == "25" && ed.data().cell(1, 1) == "0.75" && ed.data().cell(1, 2).empty(), "...and the old cells stay aligned");
        check(!ed.addColumn("damage"), "a duplicate column name is refused");
        check(!ed.renameColumn(2, "Dmg:int") && !ed.editError().empty(), "a name that ends in a type word is refused");
        check(ed.renameColumn(2, "Color") && ed.data().columns[2].name == "Color", "renameColumn");
        check(!ed.renameColumn(2, "weight"), "a rename onto another column is refused");
        check(ed.setColumnType(2, fmt::OcTableType::Vec3) && ed.data().columns[2].type == fmt::OcTableType::Vec3 && ed.data().columns[2].hinted,
              "setColumnType sets the type and hints it");
        check(ed.setCell(0, 2, "1 2 3") && ed.problems().empty(), "a good vec3 cell is valid");
        check(ed.setCell(1, 2, "1 2") && !ed.problems().empty(), "a bad vec3 cell is reported by problems()");
        check(ed.removeColumn(0) && ed.data().columns.size() == 2 && ed.data().cell(0, 0) == "1.5" && ed.data().cell(0, 1) == "1 2 3",
              "removeColumn drops its cells and keeps the rest aligned");
    }

    AVER_INFO("=== undo and redo ===");
    {
        editor::DataTableEditor ed(path("Data/items.csv"));
        const std::vector<std::string> before = ed.data().cells;
        ed.setCell(0, 0, "99");
        ed.addRow("x");
        check(ed.data().cell(0, 0) == "99" && ed.data().rowCount() == 3, "two edits");
        ed.undo();
        check(ed.data().rowCount() == 2 && ed.data().cell(0, 0) == "99", "undo reverts the last one");
        ed.undo();
        check(ed.data().cells == before, "...and the one before");
        ed.redo();
        check(ed.data().cell(0, 0) == "99", "redo reapplies");
    }

    AVER_INFO("=== save ===");
    {
        editor::DataTableEditor ed(path("Data/items.csv"));
        ed.setCell(0, 0, "30");
        ed.addRow("axe");
        ed.setCell(2, 1, "3.5");
        std::string why;
        check(ed.save(&why) && !ed.dirty(), "save clears the dirty flag: " + why);
        const std::string text = readText("Data/items.csv");
        check(text == "Name,Damage:int,Weight:float\r\nsword,30,1.5\r\nbow,12,0.75\r\naxe,,3.5\r\n", "the CSV keeps its header, hints and CRLF");
        editor::DataTableEditor again(path("Data/items.csv"));
        check(again.loaded() && again.data().rowCount() == 3 && again.data().cell(2, 1) == "3.5", "and reads back");
    }
    writeText("Data/semi.csv", "Name;Hp:int\nslime;5\n");
    {
        editor::DataTableEditor ed(path("Data/semi.csv"));
        ed.addRow("bat");
        ed.setCell(1, 0, "3");
        check(ed.save(nullptr), "a semicolon csv saves");
        check(readText("Data/semi.csv") == "Name;Hp:int\nslime;5\nbat;3\n", "...with its delimiter and LF line ending");
    }

    AVER_INFO("=== the file changes on disk ===");
    {
        editor::DataTableEditor dirtyTab(path("Data/items.csv"));
        dirtyTab.setCell(0, 0, "mine");
        writeText("Data/items.csv", "Name,Damage:int\nspear,7\n");
        dirtyTab.onFileChanged();
        check(dirtyTab.data().cell(0, 0) == "mine" && dirtyTab.dirty() && dirtyTab.changedOnDisk(), "a dirty tab keeps its edits and flags the change");
        dirtyTab.reloadFromDisk();
        check(dirtyTab.data().rowNames[0] == "spear" && !dirtyTab.dirty() && !dirtyTab.changedOnDisk(), "reloadFromDisk takes the disk version");

        editor::DataTableEditor cleanTab(path("Data/items.csv"));
        writeText("Data/items.csv", "Name,Damage:int\nspear,7\nlance,9\n");
        cleanTab.onFileChanged();
        check(cleanTab.data().rowCount() == 2 && !cleanTab.dirty(), "a clean tab reloads");
        writeText("Data/items.csv", "Name,Damage:int\nbroken\n");
        cleanTab.onFileChanged();
        check(cleanTab.loaded() && cleanTab.data().rowCount() == 2 && !cleanTab.loadError().empty(), "a half-saved file keeps the last good data and says why");
    }

    AVER_INFO("=== json ===");
    writeText("Data/mobs.json", "[{\"Name\":\"slime\",\"hp\":10,\"fast\":false},{\"Name\":\"bat\",\"hp\":4,\"fast\":true}]");
    {
        editor::DataTableEditor ed(path("Data/mobs.json"));
        check(ed.loaded() && ed.data().rowCount() == 2 && ed.data().source == fmt::OcTableData::Source::Json, "a json array of objects loads");
        ed.setCell(0, 0, "11");
        check(ed.save(nullptr), "saves");
        const std::string t = readText("Data/mobs.json");
        check(t.find("\"hp\": 11") != std::string::npos && t.find("\"fast\": false") != std::string::npos, "numbers and bools stay typed in the json");
    }

    AVER_INFO("=== the factory ===");
    {
        check(editor::makeDataTableEditor(path("Data/items.csv")) != nullptr, "a csv gets an editor");
        check(editor::makeDataTableEditor(path("Data/mobs.json")) != nullptr, "a table-shaped json gets an editor");
        writeText("Data/config.json", "[1, 2, 3]");
        check(editor::makeDataTableEditor(path("Data/config.json")) == nullptr, "a json that is not a table is declined");
        writeText("Localization/de/UI.csv", "Key,Text\nPlay,Spielen\n");
        check(editor::makeDataTableEditor(path("Localization/de/UI.csv")) == nullptr, "a csv under Localization/<culture>/ belongs to the string-table editor");
        check(editor::makeDataTableEditor(path("Data/readme.txt")) == nullptr, "other extensions are declined");
        const fmt::OcTableData starter = editor::dataTableStarter();
        check(starter.columns.size() == 1 && starter.rowNames.size() == 1 && fmt::writeTableCsv(starter) == "Name,Value:int\nrow1,1\n", "the starter table is Name,Value:int / row1,1");
    }

    fs::remove_all(g_dir, ec);
    AVER_INFO("DataTableEditor tests: {} checks, {} failure(s)", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
