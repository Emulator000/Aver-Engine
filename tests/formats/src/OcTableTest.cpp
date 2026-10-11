// Data-table tests: .csv and table-shaped .json parse, write, round trip, validate and save
// (docs/plans/wave2/DATA_TABLES_LOCALIZATION.md section 3.4).
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcTable.hpp"
#include "aver/platform/FileSystem.hpp"

#include <filesystem>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::fmt;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) {
        AVER_INFO("   PASS  {}", what);
    } else {
        AVER_ERROR("   FAIL  {}", what);
        ++g_failures;
    }
}

static bool anyContains(const std::vector<std::string>& v, const std::string& needle) {
    for (const std::string& s : v) if (s.find(needle) != std::string::npos) return true;
    return false;
}

static void testCsvBasics() {
    AVER_INFO("=== CSV basics ===");
    const std::string csv =
        "Name,Damage:int,Weight:float,Stackable:bool,Display Name:text,Tags:strings,Tint:vec3\n"
        "sword,25,1.5,false,@Items:sword.name=Sword,melee|iron,\"1 0.8 0.2\"\n"
        "bow,12,0.9,no,Bow,ranged,\"0 1 0\"\n";
    OcTableData d;
    std::string err;
    check(parseTableCsv(csv, d, &err), "parses");
    check(d.rowCount() == 2 && d.columns.size() == 6, "two rows, six columns (row-name column excluded)");
    check(d.rowNameHeader == "Name", "row-name header kept");
    check(d.columns[0].name == "Damage" && d.columns[0].type == OcTableType::Int && d.columns[0].hinted, "Damage:int hint stripped and recorded");
    check(d.columns[2].type == OcTableType::Bool && d.columns[3].type == OcTableType::Text && d.columns[3].name == "Display Name", "bool and text hints");
    check(d.columns[4].type == OcTableType::Strings && d.columns[5].type == OcTableType::Vec3, "strings and vec3 hints");
    check(d.cell(0, 0) == "25" && d.cell(0, 3) == "@Items:sword.name=Sword" && d.cell(0, 4) == "melee|iron" && d.cell(0, 5) == "1 0.8 0.2", "cells are text, quotes removed");
    check(d.cell(5, 0).empty() && d.cell(0, 99).empty(), "cell() out of range is empty");
    check(d.findRow("SWORD") == 0 && d.findRow("bow") == 1 && d.findRow("axe") == -1, "findRow is ASCII case-insensitive");
    check(d.findColumn("displayname") == 3 && d.findColumn("display_name") == 3 && d.findColumn("Display Name") == 3 &&
          d.findColumn("DAMAGE") == 0 && d.findColumn("nope") == -1, "findColumn ignores case, spaces and underscores");

    OcTableData u;
    check(parseTableCsv("Name,Odd:thing,Plain\nr,1,2\n", u, &err), "unknown suffix parses");
    check(u.columns[0].name == "Odd:thing" && !u.columns[0].hinted && u.columns[0].type == OcTableType::String,
          "an unknown suffix stays part of the name");
}

static void testCsvSyntax() {
    AVER_INFO("=== CSV syntax ===");
    OcTableData d;
    std::string err;
    check(parseTableCsv("Name,Text\r\nr1,\"a \"\"b\"\", c\"\r\nr2,\"line1\r\nline2\"\r\n", d, &err), "CRLF, doubled quotes, embedded newline parse");
    check(d.cell(0, 0) == "a \"b\", c", "quotes and delimiter inside a cell");
    check(d.cell(1, 0) == "line1\r\nline2", "embedded newline kept");
    check(d.crlf && d.delimiter == ',', "CRLF and ',' recorded");

    check(parseTableCsv("\xEF\xBB\xBF" "Name,V\nr,1\n", d, &err) && d.rowNameHeader == "Name" && d.cell(0, 0) == "1", "UTF-8 BOM stripped");
    check(parseTableCsv("Name;V;W\nr;1,5;x\n", d, &err) && d.delimiter == ';' && d.cell(0, 0) == "1,5" && !d.crlf, "';' delimiter detected, LF recorded");
    check(parseTableCsv("Name,V\nr,1\n,,\n", d, &err) && d.rowCount() == 1, "an all-empty trailing row is ignored");

    err.clear();
    check(!parseTableCsv("Name,A,B\nr,1\n", d, &err) && err.find("line 2") != std::string::npos, "a ragged row fails and names the line");
    err.clear();
    check(!parseTableCsv("Name,A\n,1\n", d, &err) && err.find("line 2") != std::string::npos && err.find("empty row name") != std::string::npos, "empty row name fails");
    err.clear();
    check(!parseTableCsv("Name,A\nSword,1\nsword,2\n", d, &err) && err.find("line 3") != std::string::npos, "duplicate row names (case-insensitive) fail");
    err.clear();
    check(!parseTableCsv("Name,A\nr,\"open\n", d, &err) && !err.empty(), "an unterminated quote fails");
    check(!parseTableCsv("", d, &err), "an empty file fails");
    check(!parseTableCsv("Name,\nr,1\n", d, &err), "an unnamed column fails");
}

static void testCsvRoundTrip() {
    AVER_INFO("=== CSV round trip ===");
    const std::string csv =
        "Name,Damage:int,Note\r\n"
        "sword,25,\"a,b\"\r\n"
        "bow,12,\"say \"\"hi\"\"\"\r\n"
        "ax,3,\"two\nlines\"\r\n";
    OcTableData d;
    check(parseTableCsv(csv, d), "parses");
    check(writeTableCsv(d) == csv, "CRLF/comma file writes back byte for byte");
    const std::string semi = "Name;Damage:int\nsword;25\nbow;12\n";
    OcTableData s;
    check(parseTableCsv(semi, s) && writeTableCsv(s) == semi, "';' delimiter and LF are kept");
    OcTableData again;
    check(parseTableCsv(writeTableCsv(d), again) && again.cells == d.cells && again.rowNames == d.rowNames, "parse(write(x)) == x");
}

static void testJson() {
    AVER_INFO("=== JSON ===");
    OcTableData d;
    std::string err;
    const std::string arr = R"X([
  {"Name":"sword","Damage":25,"Weight":1.5,"Stackable":false,"Tags":["melee","iron"],"Note":null},
  {"Name":"bow","Damage":12,"Weight":0.9,"Stackable":true,"Tags":["ranged"],"Extra":"x"}
])X";
    check(parseTableJson(arr, d, &err), "array of objects parses");
    check(d.source == OcTableData::Source::Json && !d.jsonObjectOfObjects, "JSON source, array form");
    check(d.rowNames.size() == 2 && d.rowNames[0] == "sword" && d.rowNameHeader == "Name", "row names from Name");
    const i32 dmg = d.findColumn("Damage"), wt = d.findColumn("Weight"), st = d.findColumn("Stackable"), tg = d.findColumn("Tags"), ex = d.findColumn("Extra"), nt = d.findColumn("Note");
    check(dmg == 0 && d.columns[0].type == OcTableType::Int && d.columns[0].hinted, "integral number infers int");
    check(d.columns[static_cast<usize>(wt)].type == OcTableType::Float, "fractional number infers float");
    check(d.columns[static_cast<usize>(st)].type == OcTableType::Bool, "bool infers bool");
    check(d.columns[static_cast<usize>(tg)].type == OcTableType::Strings, "array infers strings");
    check(d.cell(0, static_cast<usize>(dmg)) == "25" && d.cell(0, static_cast<usize>(wt)) == "1.5" && d.cell(0, static_cast<usize>(st)) == "false", "scalar cell text");
    check(d.cell(0, static_cast<usize>(tg)) == "melee|iron", "array joined with '|'");
    check(d.cell(0, static_cast<usize>(nt)).empty(), "null is empty");
    check(d.cell(0, static_cast<usize>(ex)).empty() && d.cell(1, static_cast<usize>(ex)) == "x", "a member missing in one row is empty there; columns are the union");

    const std::string obj = R"X({"sword":{"Damage":25,"Name":"Blade"},"bow":{"Damage":12}})X";
    check(parseTableJson(obj, d, &err), "object of objects parses");
    check(d.jsonObjectOfObjects && d.rowNames.size() == 2 && d.rowNames[0] == "sword", "row name = member key");
    check(d.findColumn("Name") >= 0, "a 'Name' member is an ordinary column in this form");

    check(parseTableJson(R"X([{"RowName":"a","V":1}])X", d, &err) && d.rowNameHeader == "RowName" && d.findColumn("RowName") == -1, "RowName accepted as the row name member");
    check(parseTableJson(R"X([{"name":"a","V":1}])X", d, &err) && d.rowNames[0] == "a", "name accepted as the row name member");

    err.clear();
    check(!parseTableJson(R"X([{"V":1}])X", d, &err) && err.find("element 0") != std::string::npos, "an object without a row name fails");
    err.clear();
    check(!parseTableJson(R"X([{"Name":"a","Sub":{"x":1}}])X", d, &err) && err.find("flatten") != std::string::npos, "a nested object fails with 'flatten it'");
    err.clear();
    check(!parseTableJson(R"X({"a":1,"b":2})X", d, &err) && err.find("not a data table") != std::string::npos, "a plain settings object is not table-shaped");
    check(!parseTableJson("[1,2,3]", d, &err), "an array of scalars is not table-shaped");
    check(!parseTableJson("42", d, &err), "a scalar root is not table-shaped");
    check(!parseTableJson("{bad json", d, &err), "invalid JSON fails");
    err.clear();
    check(!parseTableJson(R"X([{"Name":"A","V":1},{"Name":"a","V":2}])X", d, &err) && err.find("repeats") != std::string::npos, "duplicate row names fail");
}

static void testJsonRoundTrip() {
    AVER_INFO("=== JSON writer ===");
    const std::string arr = R"X([
  {"Name":"sword","Damage":25,"Weight":1.5,"Stackable":false,"Tags":["melee","iron"],"Label":"a \"q\"\nb"},
  {"Name":"bow","Damage":12,"Weight":0.9,"Stackable":true,"Tags":["ranged"],"Label":""}
])X";
    OcTableData d;
    check(parseTableJson(arr, d), "parses");
    const std::string out = writeTableJson(d);
    check(out.find("\"Damage\": 25") != std::string::npos && out.find("\"Weight\": 1.5") != std::string::npos, "numbers are written as numbers");
    check(out.find("\"Stackable\": false") != std::string::npos && out.find("\"Stackable\": true") != std::string::npos, "bools are written as bools");
    check(out.find("\"Tags\": [\"melee\", \"iron\"]") != std::string::npos, "arrays are written as arrays");
    check(out.find("\"Damage\": \"") == std::string::npos, "no number was quoted");
    OcTableData back;
    check(parseTableJson(out, back), "the output reparses");
    check(back.rowNames == d.rowNames && back.cells == d.cells && back.columns.size() == d.columns.size(), "same rows and cells after a round trip");
    check(writeTableJson(back) == out, "writing again is stable");

    const std::string obj = R"X({"sword":{"Damage":25},"bow":{"Damage":12}})X";
    OcTableData o;
    check(parseTableJson(obj, o), "object form parses");
    const std::string oo = writeTableJson(o);
    OcTableData ob;
    check(parseTableJson(oo, ob) && ob.jsonObjectOfObjects && ob.rowNames == o.rowNames && ob.cells == o.cells, "object-of-objects round trips in the same shape");

    // A CSV table written as JSON keeps its hinted types as JSON values; unhinted columns are strings.
    OcTableData c;
    check(parseTableCsv("Name,N:int,Flag:bool,Plain\nr,3,yes,7\n", c), "csv parses");
    c.source = OcTableData::Source::Json;
    const std::string cj = writeTableJson(c);
    check(cj.find("\"N\": 3") != std::string::npos && cj.find("\"Flag\": true") != std::string::npos && cj.find("\"Plain\": \"7\"") != std::string::npos,
          "hinted int/bool become numbers/bools, an unhinted column stays a string");
    OcTableData bad;
    check(parseTableCsv("Name,N:int\nr,abc\ns,\n", bad), "csv with a bad int parses");
    const std::string bj = writeTableJson(bad);
    check(bj.find("\"N\": \"abc\"") != std::string::npos && bj.find("\"N\": null") != std::string::npos, "an unparsable hinted cell stays a string, an empty one is null");
}

static void testValidate() {
    AVER_INFO("=== validateTable ===");
    OcTableData d;
    check(parseTableCsv("Name,Dmg:int,W:float,B:bool,V:vec3,T:text,S:strings\nok,1,1.5,yes,\"1 2 3\",x,a|b\nbad,abc,x,maybe,\"1 2\",y,z\nempty,,,,,,\n", d), "parses");
    const std::vector<std::string> p = validateTable(d);
    check(anyContains(p, "row 'bad' column 'Dmg': 'abc' is not an int"), "bad int message");
    check(anyContains(p, "row 'bad' column 'W'") && anyContains(p, "is not a float"), "bad float message");
    check(anyContains(p, "row 'bad' column 'B'") && anyContains(p, "is not a bool"), "bad bool message");
    check(anyContains(p, "row 'bad' column 'V'") && anyContains(p, "is not a vec3"), "bad vec3 message");
    check(!anyContains(p, "row 'ok'") && !anyContains(p, "row 'empty'"), "good and empty cells raise nothing");
    check(p.size() == 4, "exactly the four bad cells");
    check(cellProblem(d, 1, 0) == "an int" && cellProblem(d, 0, 0).empty() && cellProblem(d, 2, 0).empty(), "cellProblem names the type, empty for good and empty cells");
    check(cellProblem(d, 99, 0).empty() && cellProblem(d, 0, 99).empty() && cellProblem(d, 1, 4).empty(), "...and for out of range or an untyped column");

    OcTableData n;
    check(parseTableCsv("Name,Display Name,display_name\nr,a,b\n", n), "duplicate column names parse");
    check(anyContains(validateTable(n), "duplicates column"), "...and validate reports them");
    OcTableData m;
    m.columns.push_back({"A", OcTableType::String, false});
    m.rowNames = {"X", "x", ""};
    m.cells = {"1", "2", "3"};
    const std::vector<std::string> mp = validateTable(m);
    check(anyContains(mp, "duplicates row") && anyContains(mp, "empty name"), "duplicate and empty row names reported on a hand-built table");
    check(validateTable(OcTableData{}).empty(), "an empty table is valid");
}

static void testTypeWords() {
    AVER_INFO("=== type words ===");
    OcTableType t;
    check(parseTableTypeWord("INT", t) && t == OcTableType::Int && parseTableTypeWord(" vec3 ", t) && t == OcTableType::Vec3 &&
          parseTableTypeWord("strings", t) && t == OcTableType::Strings && !parseTableTypeWord("double", t), "parseTableTypeWord");
    check(std::string(tableTypeWord(OcTableType::Text)) == "text" && std::string(tableTypeWord(OcTableType::String)) == "string", "tableTypeWord");
}

static void testFiles() {
    AVER_INFO("=== loadTable / saveTable ===");
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "aver_ocstable_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string csvPath = (dir / "items.csv").string();
    const std::string jsonPath = (dir / "items.json").string();

    OcTableData d;
    check(parseTableCsv("Name;Dmg:int\nsword;25\n", d), "csv parses");
    std::string err;
    check(saveTable(csvPath, d, &err), "saveTable csv");
    OcTableData back;
    check(loadTable(csvPath, back, &err) && back.delimiter == ';' && back.cell(0, 0) == "25", "loadTable reads it back with its delimiter");
    check(saveTable(jsonPath, back, &err), "saveTable json by extension");
    OcTableData jb;
    check(loadTable(jsonPath, jb, &err) && jb.source == OcTableData::Source::Json && jb.cell(0, 0) == "25", "loadTable json");
    check(!fs::exists(csvPath + ".tmp"), "no temp litter after an atomic save");

    err.clear();
    check(!loadTable((dir / "nope.csv").string(), back, &err) && !err.empty(), "a missing file fails with a reason");
    err.clear();
    check(!loadTable((dir / "x.txt").string(), back, &err) && err.find("extension") != std::string::npos, "an unsupported extension fails");
    const std::string notTable = (dir / "settings.json").string();
    check(writeFileText(notTable, "{\"volume\": 3}"), "wrote a non-table json");
    err.clear();
    check(!loadTable(notTable, back, &err) && err.find("not a data table") != std::string::npos, "a settings-shaped JSON is refused as not a table");
    fs::remove_all(dir, ec);
}

int main() {
    testCsvBasics();
    testCsvSyntax();
    testCsvRoundTrip();
    testJson();
    testJsonRoundTrip();
    testValidate();
    testTypeWords();
    testFiles();

    AVER_INFO("==================================================");
    AVER_INFO("OcTable tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
