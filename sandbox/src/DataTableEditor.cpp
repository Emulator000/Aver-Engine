// The .csv / .json data table editor tab. See the header for the two-line SandboxApp hook.
#include "DataTableEditor.hpp"

#include "EditorKeybinds.hpp"
#include "aver/core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#if AVER_WITH_IMGUI
#  include "EditorIcons.hpp"
#  include "imgui.h"
#  if AVER_MODULE_FRAMEWORK
#    include "aver/framework/framework_loc_abi.h"   // the resolved preview under a text cell
#  endif
#endif

namespace aver::editor {
namespace {

std::string lowerAscii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string trimmed(const std::string& s) {
    usize a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string extOf(const std::string& path) {
    std::string e = std::filesystem::path(path).extension().string();
    return lowerAscii(e);
}

// True for a .csv directly under Localization/<culture>/: the string-table editor's.
bool isStringTablePath(const std::string& path) {
    const std::filesystem::path p(path);
    const std::filesystem::path culture = p.parent_path();
    return lowerAscii(culture.parent_path().filename().string()) == "localization";
}

// "NewRow", "NewRow1", ... the first one `taken` does not claim.
template <typename Taken>
std::string freshName(const std::string& base, Taken taken) {
    std::string n = base;
    for (int i = 1; taken(n); ++i) n = base + std::to_string(i);
    return n;
}

} // namespace

DataTableEditor::DataTableEditor(std::string path) : path_(std::move(path)) { loadFromDisk(); }

void DataTableEditor::loadFromDisk() {
    fmt::OcTableData t;
    std::string err;
    if (!fmt::loadTable(path_, t, &err)) {
        loadError_ = err.empty() ? "failed to read the table" : err;
        return;                               // a tab that was loaded keeps its data (a half-saved file must not blank it)
    }
    data_ = std::move(t);
    loaded_ = true;
    loadError_.clear();
    dirty_ = false;
    changedOnDisk_ = false;
    editError_.clear();
    history_.clear();
    ++revision_;
}

void DataTableEditor::reloadFromDisk() { loadFromDisk(); }

std::string DataTableEditor::title() const {
    // No manual dirty marker: the host applies ImGuiWindowFlags_UnsavedDocument (see FoliageTypeEditor::title).
    return std::filesystem::path(path_).filename().string() + "###datatable:" + path_;
}

bool DataTableEditor::save(std::string* why) {
    if (!loaded_) {
        if (why) *why = "cannot save: file failed to load (" + loadError_ + ")";
        return false;
    }
    std::string err;
    if (!fmt::saveTable(path_, data_, &err)) {
        if (why) *why = err.empty() ? "could not write " + path_ : err;
        return false;
    }
    dirty_ = false;
    changedOnDisk_ = false;
    return true;
}

void DataTableEditor::onFileChanged() {
    // A dirty tab keeps its edits (FoliageTypeEditor's rule); it says so and offers the reload.
    if (dirty_) {
        changedOnDisk_ = true;
        AVER_WARN("[DataTableEditor] '{}' changed on disk, but this tab has unsaved edits -- keeping them", path_);
        return;
    }
    loadFromDisk();
}

void DataTableEditor::pushUndo() { history_.push(data_); }

void DataTableEditor::undo() {
    if (!history_.undo(data_)) return;
    touch();
}

void DataTableEditor::redo() {
    if (!history_.redo(data_)) return;
    touch();
}

bool DataTableEditor::refuse(std::string why) {
    editError_ = std::move(why);
    return false;
}

// ---- rows -------------------------------------------------------------------------------------------

bool DataTableEditor::addRow(std::string name) {
    if (!loaded_) return false;
    editError_.clear();
    name = trimmed(name);
    if (name.empty()) name = freshName("NewRow", [&](const std::string& n) { return data_.findRow(n) >= 0; });
    else if (data_.findRow(name) >= 0) return refuse("a row named '" + name + "' already exists");
    pushUndo();
    data_.rowNames.push_back(std::move(name));
    data_.cells.resize(data_.cells.size() + data_.columns.size());
    touch();
    return true;
}

bool DataTableEditor::duplicateRow(usize index) {
    if (!loaded_ || index >= data_.rowNames.size()) return false;
    editError_.clear();
    const std::string name = freshName(data_.rowNames[index] + "Copy", [&](const std::string& n) { return data_.findRow(n) >= 0; });
    pushUndo();
    const usize nc = data_.columns.size();
    const std::vector<std::string> row(data_.cells.begin() + static_cast<std::ptrdiff_t>(index * nc),
                                       data_.cells.begin() + static_cast<std::ptrdiff_t>((index + 1) * nc));
    data_.rowNames.insert(data_.rowNames.begin() + static_cast<std::ptrdiff_t>(index + 1), name);
    data_.cells.insert(data_.cells.begin() + static_cast<std::ptrdiff_t>((index + 1) * nc), row.begin(), row.end());
    touch();
    return true;
}

bool DataTableEditor::removeRow(usize index) {
    if (!loaded_ || index >= data_.rowNames.size()) return false;
    editError_.clear();
    pushUndo();
    const usize nc = data_.columns.size();
    data_.rowNames.erase(data_.rowNames.begin() + static_cast<std::ptrdiff_t>(index));
    data_.cells.erase(data_.cells.begin() + static_cast<std::ptrdiff_t>(index * nc),
                      data_.cells.begin() + static_cast<std::ptrdiff_t>((index + 1) * nc));
    touch();
    return true;
}

bool DataTableEditor::renameRow(usize index, std::string name) {
    if (!loaded_ || index >= data_.rowNames.size()) return false;
    editError_.clear();
    name = trimmed(name);
    if (name.empty()) return refuse("a row needs a name");
    const i32 clash = data_.findRow(name);
    if (clash >= 0 && static_cast<usize>(clash) != index) return refuse("a row named '" + name + "' already exists");
    if (data_.rowNames[index] == name) return true;
    pushUndo();
    data_.rowNames[index] = std::move(name);
    touch();
    return true;
}

// ---- columns ----------------------------------------------------------------------------------------

bool DataTableEditor::validNewColumnName(const std::string& name, usize ignoreIndex, std::string& why) const {
    if (name.empty()) { why = "a column needs a name"; return false; }
    if (name.find_first_of("\r\n") != std::string::npos) { why = "a column name is one line"; return false; }
    // "Dmg:int" read back would be a column Dmg typed int, not a column called "Dmg:int".
    const usize colon = name.rfind(':');
    fmt::OcTableType ignored;
    if (data_.source == fmt::OcTableData::Source::Csv && colon != std::string::npos &&
        fmt::parseTableTypeWord(std::string_view(name).substr(colon + 1), ignored)) {
        why = "'" + name + "' ends in a type word; set the type with the type box instead";
        return false;
    }
    const i32 clash = data_.findColumn(name);
    if (clash >= 0 && static_cast<usize>(clash) != ignoreIndex) {
        why = "a column named '" + data_.columns[static_cast<usize>(clash)].name + "' already exists";
        return false;
    }
    return true;
}

bool DataTableEditor::addColumn(std::string name) {
    if (!loaded_) return false;
    editError_.clear();
    name = trimmed(name);
    if (name.empty()) name = freshName("NewColumn", [&](const std::string& n) { return data_.findColumn(n) >= 0; });
    std::string why;
    if (!validNewColumnName(name, data_.columns.size(), why)) return refuse(why);
    pushUndo();
    const usize nr = data_.rowNames.size(), nc = data_.columns.size();
    std::vector<std::string> cells;
    cells.reserve(nr * (nc + 1));
    for (usize r = 0; r < nr; ++r) {
        for (usize c = 0; c < nc; ++c) cells.push_back(std::move(data_.cells[r * nc + c]));
        cells.emplace_back();
    }
    data_.cells = std::move(cells);
    data_.columns.push_back({std::move(name), fmt::OcTableType::String, false});
    touch();
    return true;
}

bool DataTableEditor::removeColumn(usize index) {
    if (!loaded_ || index >= data_.columns.size()) return false;
    editError_.clear();
    pushUndo();
    const usize nr = data_.rowNames.size(), nc = data_.columns.size();
    std::vector<std::string> cells;
    cells.reserve(nr * (nc - 1));
    for (usize r = 0; r < nr; ++r)
        for (usize c = 0; c < nc; ++c)
            if (c != index) cells.push_back(std::move(data_.cells[r * nc + c]));
    data_.cells = std::move(cells);
    data_.columns.erase(data_.columns.begin() + static_cast<std::ptrdiff_t>(index));
    touch();
    return true;
}

bool DataTableEditor::renameColumn(usize index, std::string name) {
    if (!loaded_ || index >= data_.columns.size()) return false;
    editError_.clear();
    name = trimmed(name);
    std::string why;
    if (!validNewColumnName(name, index, why)) return refuse(why);
    if (data_.columns[index].name == name) return true;
    pushUndo();
    data_.columns[index].name = std::move(name);
    touch();
    return true;
}

bool DataTableEditor::setColumnType(usize index, fmt::OcTableType type) {
    if (!loaded_ || index >= data_.columns.size()) return false;
    editError_.clear();
    fmt::OcTableColumn& col = data_.columns[index];
    if (col.hinted && col.type == type) return true;
    pushUndo();
    col.type = type;
    col.hinted = true;
    touch();
    return true;
}

bool DataTableEditor::setCell(usize row, usize column, std::string text) {
    if (!loaded_ || row >= data_.rowNames.size() || column >= data_.columns.size()) return false;
    editError_.clear();
    std::string& cell = data_.cells[row * data_.columns.size() + column];
    if (cell == text) return true;
    pushUndo();
    cell = std::move(text);
    touch();
    return true;
}

std::vector<usize> DataTableEditor::matchingRows(const std::string& filter) const {
    std::vector<usize> out;
    const std::string f = lowerAscii(filter);
    const usize nc = data_.columns.size();
    for (usize r = 0; r < data_.rowNames.size(); ++r) {
        bool hit = f.empty() || lowerAscii(data_.rowNames[r]).find(f) != std::string::npos;
        for (usize c = 0; c < nc && !hit; ++c) hit = lowerAscii(data_.cells[r * nc + c]).find(f) != std::string::npos;
        if (hit) out.push_back(r);
    }
    return out;
}

// ---- the window ----------------------------------------------------------------------------------------

#if AVER_WITH_IMGUI

namespace {

bool parseIntCell(const std::string& s, int& out) {
    const std::string t = trimmed(s);
    if (t.empty()) return false;
    const char* b = t.data() + (t[0] == '+' ? 1 : 0);
    const auto r = std::from_chars(b, t.data() + t.size(), out);
    return r.ec == std::errc() && r.ptr == t.data() + t.size();
}

bool parseFloatCell(const std::string& s, float& out) {
    std::string t = trimmed(s);
    if (t.size() > 1 && (t.back() == 'f' || t.back() == 'F')) t.pop_back();
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size()) return false;
    out = static_cast<float>(v);
    return true;
}

std::string floatText(float v) {
    char buf[48];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
}

bool parseVec3Cell(const std::string& s, float out[3]) {
    std::string t = trimmed(s);
    for (char& c : t) if (c == ',' || c == ';' || c == '|') c = ' ';
    int n = 0;
    usize i = 0;
    while (i < t.size() && n < 3) {
        while (i < t.size() && t[i] == ' ') ++i;
        usize j = i;
        while (j < t.size() && t[j] != ' ') ++j;
        if (j == i) break;
        if (!parseFloatCell(t.substr(i, j - i), out[n])) return false;
        ++n;
        i = j;
    }
    while (i < t.size() && t[i] == ' ') ++i;
    return n == 3 && i == t.size();
}

bool parseBoolCell(const std::string& s, bool& out) {
    const std::string t = lowerAscii(trimmed(s));
    if (t == "true" || t == "1" || t == "yes") { out = true; return true; }
    if (t == "false" || t == "0" || t == "no") { out = false; return true; }
    return false;
}

const ImVec4 kRed(1.0f, 0.45f, 0.4f, 1.0f);

} // namespace

void DataTableEditor::drawToolbar() {
    if (ImGui::Button(ICON_SAVE " Save") ||
        (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
         editor::keybinds().pressed(editor::CommandId::AssetSave, ImGui::GetIO()))) {
        std::string why;
        if (!save(&why)) AVER_ERROR("[DataTableEditor] save failed for '{}': {}", path_, why);
    }
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput) {
            if (canUndo() && editor::keybinds().pressed(editor::CommandId::EditUndo, io)) undo();
            if (canRedo() && editor::keybinds().pressed(editor::CommandId::EditRedo, io)) redo();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canUndo());
    if (ImGui::Button(ICON_UNDO " Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRedo());
    if (ImGui::Button(ICON_REDO " Redo")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    char fb[128];
    std::snprintf(fb, sizeof fb, "%s", filter_.c_str());
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::InputTextWithHint("##filter", ICON_SEARCH " filter rows", fb, sizeof fb)) filter_ = fb;

    // rows
    ImGui::TextUnformatted("Rows");
    ImGui::SameLine();
    if (ImGui::Button(ICON_ADD " Add##row")) {
        if (addRow()) selectedRow_ = static_cast<int>(data_.rowNames.size()) - 1;
    }
    const bool haveRow = selectedRow_ >= 0 && static_cast<usize>(selectedRow_) < data_.rowNames.size();
    ImGui::BeginDisabled(!haveRow);
    ImGui::SameLine();
    if (ImGui::Button("Duplicate##row") && duplicateRow(static_cast<usize>(selectedRow_))) ++selectedRow_;
    ImGui::SameLine();
    if (ImGui::Button(ICON_DELETE " Delete##row") && removeRow(static_cast<usize>(selectedRow_))) selectedRow_ = -1;
    ImGui::SameLine();
    char rb[128];
    std::snprintf(rb, sizeof rb, "%s", haveRow ? data_.rowNames[static_cast<usize>(selectedRow_)].c_str() : "");
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("##renamerow", rb, sizeof rb, ImGuiInputTextFlags_EnterReturnsTrue) && haveRow)
        renameRow(static_cast<usize>(selectedRow_), rb);
    ImGui::EndDisabled();

    // columns
    ImGui::TextUnformatted("Columns");
    ImGui::SameLine();
    if (ImGui::Button(ICON_ADD " Add##col")) {
        if (addColumn()) selectedCol_ = static_cast<int>(data_.columns.size()) - 1;
    }
    const bool haveCol = selectedCol_ >= 0 && static_cast<usize>(selectedCol_) < data_.columns.size();
    ImGui::BeginDisabled(!haveCol);
    ImGui::SameLine();
    if (ImGui::Button(ICON_DELETE " Delete##col") && removeColumn(static_cast<usize>(selectedCol_))) selectedCol_ = -1;
    ImGui::SameLine();
    char cb[128];
    std::snprintf(cb, sizeof cb, "%s", haveCol ? data_.columns[static_cast<usize>(selectedCol_)].name.c_str() : "");
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("##renamecol", cb, sizeof cb, ImGuiInputTextFlags_EnterReturnsTrue) && haveCol)
        renameColumn(static_cast<usize>(selectedCol_), cb);
    ImGui::SameLine();
    {
        static const char* kTypes[] = {"string", "int", "float", "bool", "vec3", "text", "strings"};
        int t = haveCol ? static_cast<int>(data_.columns[static_cast<usize>(selectedCol_)].type) : 0;
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("##coltype", &t, kTypes, 7) && haveCol)
            setColumnType(static_cast<usize>(selectedCol_), static_cast<fmt::OcTableType>(t));
    }
    ImGui::EndDisabled();
    if (!editError_.empty()) ImGui::TextColored(kRed, "%s", editError_.c_str());
}

bool DataTableEditor::drawCell(usize r, usize c, const std::string& id) {
    const usize nc = data_.columns.size();
    std::string& cell = data_.cells[r * nc + c];
    const fmt::OcTableColumn& col = data_.columns[c];
    const std::string bad = fmt::cellProblem(data_, r, c);
    if (!bad.empty()) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(150, 40, 40, 110));

    const auto beginEdit = [&] {
        if (!cellUndone_) { pushUndo(); cellUndone_ = true; }
    };
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);

    bool bv = false;
    int iv = 0;
    float fv = 0.0f, v3[3] = {0, 0, 0};
    const bool typedOk = bad.empty() && !trimmed(cell).empty() && col.hinted;
    const bool textActive = activeTextCell_ == static_cast<int>(r * nc + c);

    if (typedOk && !textActive && col.type == fmt::OcTableType::Bool && parseBoolCell(cell, bv)) {
        const bool numeric = trimmed(cell) == "1" || trimmed(cell) == "0";
        if (ImGui::Checkbox(id.c_str(), &bv)) {
            beginEdit();
            cell = numeric ? (bv ? "1" : "0") : (bv ? "true" : "false");
            changed = true;
            cellUndone_ = false;
        }
    } else if (typedOk && !textActive && col.type == fmt::OcTableType::Int && parseIntCell(cell, iv)) {
        if (ImGui::InputInt(id.c_str(), &iv, 0, 0)) { beginEdit(); cell = std::to_string(iv); changed = true; }
    } else if (typedOk && !textActive && col.type == fmt::OcTableType::Float && parseFloatCell(cell, fv)) {
        if (ImGui::InputFloat(id.c_str(), &fv, 0.0f, 0.0f, "%.6g")) { beginEdit(); cell = floatText(fv); changed = true; }
    } else if (typedOk && !textActive && col.type == fmt::OcTableType::Vec3 && parseVec3Cell(cell, v3)) {
        if (ImGui::DragFloat3(id.c_str(), v3, 0.01f)) {
            beginEdit();
            cell = floatText(v3[0]) + " " + floatText(v3[1]) + " " + floatText(v3[2]);
            changed = true;
        }
    } else {
        char buf[1024];
        std::snprintf(buf, sizeof buf, "%s", cell.c_str());
        if (ImGui::InputText(id.c_str(), buf, sizeof buf)) { beginEdit(); cell = buf; changed = true; }
        if (ImGui::IsItemActive()) activeTextCell_ = static_cast<int>(r * nc + c);
        else if (textActive) activeTextCell_ = -1;
    }
    if (ImGui::IsItemDeactivated()) cellUndone_ = false;
    if (!bad.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("'%s' is not %s", cell.c_str(), bad.c_str());

#if AVER_MODULE_FRAMEWORK
    if (col.type == fmt::OcTableType::Text && cell.size() > 3 && cell[0] == '@') {
        char out[512];
        if (aver_fw_loc_resolve(cell.c_str(), out, sizeof out) >= 0) ImGui::TextDisabled("%s", out);
    }
#endif
    if (changed) touch();
    return changed;
}

void DataTableEditor::drawGrid() {
    const usize nc = data_.columns.size();
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##grid", static_cast<int>(nc) + 1, flags, ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 3.0f))) return;
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    for (usize c = 0; c < nc; ++c) ImGui::TableSetupColumn(("##c" + std::to_string(c)).c_str(), ImGuiTableColumnFlags_WidthFixed, 150.0f);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(data_.rowNameHeader.empty() ? "Name" : data_.rowNameHeader.c_str());
    for (usize c = 0; c < nc; ++c) {
        ImGui::TableSetColumnIndex(static_cast<int>(c) + 1);
        const fmt::OcTableColumn& col = data_.columns[c];
        std::string label = col.name + " : " + (col.hinted ? fmt::tableTypeWord(col.type) : "string") + "###h" + std::to_string(c);
        if (ImGui::Selectable(label.c_str(), selectedCol_ == static_cast<int>(c))) selectedCol_ = static_cast<int>(c);
    }

    for (const usize r : matchingRows(filter_)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(static_cast<int>(r));
        if (ImGui::Selectable(data_.rowNames[r].c_str(), selectedRow_ == static_cast<int>(r))) selectedRow_ = static_cast<int>(r);
        for (usize c = 0; c < nc; ++c) {
            ImGui::TableSetColumnIndex(static_cast<int>(c) + 1);
            drawCell(r, c, "##cell" + std::to_string(c));
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DataTableEditor::draw(Engine& e) {
    (void)e;
    if (!loaded_) {
        ImGui::TextWrapped("This file could not be read: %s", loadError_.c_str());
        return;
    }
    if (!loadError_.empty())
        ImGui::TextColored(kRed, "The file on disk no longer reads (%s); showing the last good data.", loadError_.c_str());
    if (changedOnDisk_) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), ICON_WARNING " This file changed on disk; your edits here are kept.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload (discard my edits)")) reloadFromDisk();
        ImGui::SameLine();
        if (ImGui::SmallButton("Keep mine")) changedOnDisk_ = false;
    }
    drawToolbar();
    drawGrid();

    if (problemsRevision_ != revision_) {
        problemsCache_ = problems();
        problemsRevision_ = revision_;
    }
    if (problemsCache_.empty()) {
        ImGui::TextDisabled("%zu row(s), %zu column(s), no problems", data_.rowNames.size(), data_.columns.size());
    } else if (ImGui::TreeNode(("Problems (" + std::to_string(problemsCache_.size()) + ")").c_str())) {
        for (const std::string& p : problemsCache_) ImGui::TextColored(kRed, "%s", p.c_str());
        ImGui::TreePop();
    }
}

#else   // AVER_WITH_IMGUI

// The headless build (and tests/editor's own target, which leaves AVER_WITH_IMGUI undefined) still gets load/save/
// dirty/undo and every edit; only the window is absent.
void DataTableEditor::draw(Engine& e) { (void)e; }

#endif  // AVER_WITH_IMGUI

std::unique_ptr<AssetEditor> makeDataTableEditor(const std::string& path) {
    const std::string ext = extOf(path);
    if (ext == ".csv") {
        if (isStringTablePath(path)) return nullptr;
        return std::make_unique<DataTableEditor>(path);
    }
    if (ext == ".json") {
        fmt::OcTableData probe;
        if (!fmt::loadTable(path, probe, nullptr)) return nullptr;
        return std::make_unique<DataTableEditor>(path);
    }
    return nullptr;
}

} // namespace aver::editor
