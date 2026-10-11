#pragma once
// The Data Table editor tab: a .csv or table-shaped .json opened as a grid -- rows named by column 1, typed columns
// (`Damage:int`), per-cell widgets by type. See aver/formats/OcTable.hpp for the format and docs/DATA_TABLES.md.
//
// THE HOOK INTO SandboxApp.cpp IS TWO LINES, InputSchemeEditor's pattern:
//   #include "DataTableEditor.hpp"
//   assetEditors_.registerFactory(&editor::makeDataTableEditor);   // APPENDED -- order is precedence
// The string-table editor (StringTableEditor.hpp) is registered BEFORE it and claims the CSVs directly under
// Localization/<culture>/; everything else with the .csv extension is a data table, and a .json only when it is
// table-shaped (otherwise the factory declines so the shell opens it).
//
// State IS fmt::OcTableData (SnapshotUndo of the whole table; tables are small). Save writes it back through
// fmt::saveTable, which keeps the CSV delimiter and line ending and the JSON shape. A dirty tab keeps its edits when
// the file changes on disk and says so; a clean tab reloads.
#include "AssetEditor.hpp"
#include "SnapshotUndo.hpp"

#include "aver/formats/OcTable.hpp"

#include <memory>
#include <string>
#include <vector>

namespace aver::editor {

class DataTableEditor final : public AssetEditor {
public:
    explicit DataTableEditor(std::string path);

    const std::string& path() const override { return path_; }
    std::string title() const override;
    bool dirty() const override { return dirty_; }
    void draw(Engine& e) override;
    bool save(std::string* why) override;
    void onFileChanged() override;

    // Reachable for a headless test, FoliageTypeEditor's reason.
    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const fmt::OcTableData& data() const { return data_; }
    bool changedOnDisk() const { return changedOnDisk_; }
    // Discards the tab's edits and reads the file again.
    void reloadFromDisk();

    void pushUndo();
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // ---- rows (names are unique, ASCII case-insensitively, and not empty) ----
    // Appends a row of empty cells. An empty `name` picks "NewRow", "NewRow1", ...; a taken one is refused.
    bool addRow(std::string name = {});
    // Copies row `index` right after itself as "<name>Copy", "<name>Copy1", ...
    bool duplicateRow(usize index);
    bool removeRow(usize index);
    bool renameRow(usize index, std::string name);

    // ---- columns (the row-name column is not one of them; names are unique like findColumn compares them) ----
    // Appends a string column of empty cells; an empty `name` picks "NewColumn", "NewColumn1", ...
    bool addColumn(std::string name = {});
    bool removeColumn(usize index);
    bool renameColumn(usize index, std::string name);
    // Sets the type and marks the column hinted (so the file states it and the writer keeps it typed).
    bool setColumnType(usize index, fmt::OcTableType type);

    bool setCell(usize row, usize column, std::string text);

    // validateTable's messages for the current state.
    std::vector<std::string> problems() const { return fmt::validateTable(data_); }
    // Why the last refused edit was refused; empty after a success.
    const std::string& editError() const { return editError_; }

    // The rows whose name or any cell contains `filter` (ASCII case-insensitive); all rows for an empty filter.
    std::vector<usize> matchingRows(const std::string& filter) const;

private:
    void loadFromDisk();
    bool refuse(std::string why);
    bool validNewColumnName(const std::string& name, usize ignoreIndex, std::string& why) const;

    std::string path_;
    fmt::OcTableData data_;
    bool loaded_ = false;
    std::string loadError_;
    bool dirty_ = false;
    bool changedOnDisk_ = false;
    std::string editError_;
    SnapshotUndo<fmt::OcTableData> history_;

    // Bumped by every edit; the problems list the grid shows is recomputed only when it moves.
    u32 revision_ = 0;
    u32 problemsRevision_ = ~0u;
    std::vector<std::string> problemsCache_;
    void touch() { dirty_ = true; ++revision_; }

#if AVER_WITH_IMGUI
    std::string filter_;
    int selectedRow_ = -1;
    int selectedCol_ = -1;
    bool cellUndone_ = false;      // one undo step per drag / typing burst in a cell
    int activeTextCell_ = -1;      // the cell a text box is being typed in (a typed widget would steal the focus mid-edit)
    void drawToolbar();
    void drawGrid();
    bool drawCell(usize r, usize c, const std::string& id);
#endif
};

// Creates a Data Table editor for a .csv, or a .json that is table-shaped; else nullptr.
std::unique_ptr<AssetEditor> makeDataTableEditor(const std::string& path);

// What the Content Browser's "New Data Table" writes: Name,Value:int / row1,1.
inline fmt::OcTableData dataTableStarter() {
    fmt::OcTableData d;
    d.crlf = false;
    d.columns.push_back({"Value", fmt::OcTableType::Int, true});
    d.rowNames.push_back("row1");
    d.cells.push_back("1");
    return d;
}

} // namespace aver::editor
