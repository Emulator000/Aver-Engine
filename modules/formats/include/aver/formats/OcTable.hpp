#pragma once
// Data tables: a .csv or table-shaped .json file read as named rows of named columns. Cells stay TEXT; the
// column type is advisory (it drives validation, the editor widgets and the JSON writer), the C# DataTable
// binder does the conversion. No new on-disk format. Text only, no engine dependency. docs/DATA_TABLES.md.
//
//   Name,Damage:int,Weight:float,Stackable:bool,Display Name:text,Tags:strings,Tint:vec3
//   sword,25,1.5,false,@Items:sword.name=Sword,melee|iron,"1 0.8 0.2"
#include "aver/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::fmt {

enum class OcTableType : u8 { String, Int, Float, Bool, Vec3, Text, Strings };   // == AVER_FW_TABLE_COL_*

struct OcTableColumn {
    std::string name;
    OcTableType type = OcTableType::String;
    bool hinted = false;     // the file stated (CSV `Name:type`) or the reader inferred (JSON) the type
};

struct OcTableData {
    enum class Source : u8 { Csv, Json } source = Source::Csv;
    std::string rowNameHeader = "Name";
    char delimiter = ',';  bool crlf = true;           // CSV round trip
    bool jsonObjectOfObjects = false;                  // JSON round trip
    std::vector<OcTableColumn> columns;                // excludes the row-name column
    std::vector<std::string> rowNames;
    std::vector<std::string> cells;                    // rowNames.size() * columns.size(), row-major

    usize rowCount() const { return rowNames.size(); }
    i32 findRow(std::string_view name) const;          // ASCII case-insensitive; -1
    i32 findColumn(std::string_view name) const;       // ASCII case-insensitive, spaces/underscores ignored; -1
    std::string_view cell(usize r, usize c) const;     // "" out of range
};

// Rows must have exactly the header's cell count; an empty or duplicate (ASCII case-insensitive) row name is an
// error. Errors name the line (CSV) or the member (JSON).
bool parseTableCsv (std::string_view text, OcTableData& out, std::string* err = nullptr);
bool parseTableJson(std::string_view text, OcTableData& out, std::string* err = nullptr);
// By extension. JSON that is not table-shaped fails with a clear reason.
bool loadTable(const std::string& path, OcTableData& out, std::string* err = nullptr);
std::string writeTableCsv (const OcTableData&);
std::string writeTableJson(const OcTableData&);
// Atomic (writeFileTextAtomic); the writer is picked by the path's extension (.json, else CSV).
bool saveTable(const std::string& path, const OcTableData&, std::string* err = nullptr);

// Why a hinted cell is not valid for its column type ("an int", "a float", "a bool (...)", "a vec3 (...)"), empty when it is fine,
// empty, unhinted or out of range. validateTable's per-cell rule, for the editor to tint the cell.
std::string cellProblem(const OcTableData&, usize row, usize column);

// Human-readable problems: duplicate or empty row/column names, hinted cells that do not parse as their type.
std::vector<std::string> validateTable(const OcTableData&);

const char* tableTypeWord(OcTableType);
bool parseTableTypeWord(std::string_view, OcTableType&);

} // namespace aver::fmt
