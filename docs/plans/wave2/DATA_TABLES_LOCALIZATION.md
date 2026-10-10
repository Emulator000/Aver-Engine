# Wave 2 design: data tables and localization (frozen contract)

2026-10-11. Parity plan row "+ Tools: data tables, localization" (docs/plans/UE_FEATURE_PARITY_PLAN.md:49, wave 2 at :56). Design only; the implementing agent follows this without the author. Rules held: upgrade what exists, general purpose, module boundaries (docs/MODULES.md), D3D12/Vulkan neutral (no GPU code), Blueprint parity, no third-party code, lean comments.

Decisions in one paragraph. There is **no new module and no new DLL**. Parsers and writers go in `Aver.Formats` (base target, like `.ocinput`, `OcInput.hpp:1-45`). The runtime stores and their plain-C ABI go in `Aver.Framework` as **two new ABI headers beside the existing separate ones** (`framework_timers_abi.h:1-20` is the precedent), so `framework_abi.h` and its version are untouched. `Aver.UI` stays Core-only: it gets a text-resolver hook, and the host wires `aver_fw_loc_resolve` into it (a function-pointer relay, the repo's way to avoid a link edge). Data tables are plain `.csv` / `.json` files (no new on-disk format) bound to C# records; string tables are `.ocstrings` (engine-native, comment-preserving) or `.csv`. Hot reload rides the editor's existing `DirectoryWatcher` pump. A packaged game already ships all of it (stage-game.ps1 drops only sources and intermediates).

## 1. What exists today, and what is missing

Searched the whole tree (excluding third_party, .claude/worktrees): **nothing** implements localization, culture, string tables or data tables. The only "culture" hits are `CultureInfo.InvariantCulture` in the graph parser. Facts the design stands on:

| Area | Today | Gap |
|---|---|---|
| UI text storage | `UiWidgetProps::text`, `items`, `placeholder` are plain UTF-8 strings (`modules/ui/include/aver/ui/UiWidget.hpp:51,64,65`). Layout and draw read them raw: `UiTreeLayout.cpp:94-130` (measure), `UiTreeDraw.cpp:119-131` (text), `:152-161` (button, key-bind prompt "Press a key..." at :157), `:172-175` (toggle), `:202-212` (choice), `:214-234` (list rows), `:248-249` (placeholder), `:268-269` (progress label). | No key resolution. |
| UI built-in English | `uiDefaultSettingsModel` labels/tabs/choices `UiScreens.cpp:69-87`; screen title/tabs/buttons `:125,132,156,207,211`; menu labels `:326-333`; C# labels in `UiGameSettings.cs` and `RebindRow.Label` (`SettingsScreen.cs:12`). | Hard-coded English. |
| UI immediate text | `aver_ui_text`, `aver_ui_text_width` (`ui.abi/src/UiAbi.cpp:103-114`), `Hud.Text` (`Aver.UI/Hud.cs:139`). | No resolution. |
| UI ABI | `ui_widget_abi.h:98-104` environment section; one tree `UiWidgetAbi.cpp:27` (`g_tree`). No version constant in the UI headers. | No resolver hook. |
| Fonts | The one shipped font `assets/fonts/Roboto-Regular.ocfont` has **95 glyphs, U+0020..U+007E**. `UiFont::glyph()` returns null for anything else and the draw skips it (`UiFont.hpp:56-59`, "an unknown codepoint contributes nothing"). `scripts/bake-font.py` bakes one contiguous `--first/--last` range. Game loads `<exe>\Roboto-Regular.ocfont` (`Runtime/src/GameApp.cpp:757`); deployed by `sandbox/CMakeLists.txt:562-572`. | Accented Latin, Greek and Cyrillic would render as blanks. |
| Project settings | `ProjectDesc` (`OcProject.hpp:13-35`), keys parsed `OcProject.cpp:60-140`, owned-key list `:296-300`, written `:347-348`; `stage-game.ps1` carries every key through (`PACKAGING.md`, "rewritten manifest carries every KEY"). | No default-culture key. |
| Player settings | `Settings` C#/`settings_abi.h:35-76` (a process-wide key/value store, one binary = one store). Editor path `<project>\Saved\Settings.ini` (`SandboxPlay.cpp:303`); runtime `userDataDir()\<Project>\Settings.ini` (`GameApp.cpp:1171`). Opened by C# `HostBridge.ConfigureInput` (`HostBridge.cs:401-408`). Nothing native links `Aver.Settings` (game.allowlist comment). | Nowhere to remember the chosen culture except here. |
| File watcher | `DirectoryWatcher` (`platform/DirectoryWatcher.hpp:28-55`); the editor watches the content root and drains it once a frame in `SandboxApp::pumpContentWatch` (`SandboxAutosave.cpp:431-466`), routing to open tabs via `AssetEditor::onFileChanged` (`AssetEditor.hpp:49`). A shipped game has no watcher. | Nothing reloads non-asset-tab data; C# has no content-root notion (`UiSystem.OpenLayout(path)` takes a raw path, `Widgets.cs:363`). |
| Editor asset pattern | Factory registry `SandboxApp.cpp:443-454`; model editor `InputSchemeEditor.hpp` (text-merge on save, `SnapshotUndo<State>`, dirty tab keeps edits on disk change `InputSchemeEditor.cpp:75-84`); Content Browser create `SandboxContentBrowser.cpp:522-534`, menu `:1136,1164`, icon rows `:1733-1739`; headless editor tests compile the .cpp with `AVER_WITH_IMGUI` undefined (`tests/editor/CMakeLists.txt:270-275`). | No table/string editor. |
| Graph nodes | Table-driven rows (`GraphGameSystemNodeTable.cs:15-87`, `Reg(type, kind, owner, method, ins, outs, args, ret)`, validated by `GameSystemNodes.Validate`, `GraphGameSystemNodes.cs`), palette mirror `sandbox/src/GraphNodeDefsGameSystems.hpp`. **Pins are only float/int/bool/exec** (`Graph.cs:15-27`); strings travel as NODE attributes (`@key`, `@path|name|text`). `AN_SetUiText text=` exists. | No data/loc nodes. |
| Events | `Events.Post/Dispatch` over `framework_timers_abi.h` (`Events.cs:228-235`); `AN_OnEvent` binds a graph to a name. `HostBridge.DispTickAll` group 0 already pumps UI (`HostBridge.cs:1467-1469`). | Nothing announces a culture change. |
| Packaging | Content copy drops only `.cs .fs .csproj .fsproj .pdb .user .sln` and bin/obj/Source dirs (`stage-game.ps1:369-385`). `Aver.Framework.dll` is already staged (`game.allowlist`). | Nothing to add. |

## 2. Design overview

```
Content/Localization/<culture>/<Table>.ocstrings|.csv     Content/Data/*.csv|*.json
        |                                                         |
 aver::fmt::OcStrings (parse/write/merge)             aver::fmt::OcTable (csv/json parse, write, validate)   [Aver.Formats]
        |                                                         |
 aver_fw_loc_* store (chain, lazy culture load,        aver_fw_table_* store (handles, versions, hot reload) [Aver.Framework.dll]
  missing list, epoch)                                            |
        |   \__ aver_fw_loc_resolve  --relay-->  aver_ui_set_text_resolver  [Aver.UI.Abi.dll -> UiTree::shown()]
        |                                                         |
 C#  Loc / LocText / LocFormat                          C#  DataTable<T> / DataTable / DataTableBinder
        |                                                         |
 graph nodes (LocGraph.cs)                              graph nodes (DataContentGraph.cs)
Editor: StringTableEditor, DataTableEditor, Project Settings > Localization, UI layout editor culture preview,
        content-watch hot reload -> aver_fw_loc_reload_path / aver_fw_table_reload_path
Hosts : game::initLocalization (Runtime.Game.Core) called by GameApp and SandboxApp
```

### 2.1 Cultures

A culture tag is BCP-47-like: `ll`, `ll-RR`, `ll-Ssss`, `ll-Ssss-RR` (e.g. `en`, `pt-BR`, `zh-Hans`). Normalised by the native store: `_` becomes `-`, language lowercase, script Titlecase, region UPPERCASE; matching is on the normalised form. A tag is malformed unless it matches `[A-Za-z]{2,3}(-[A-Za-z]{4})?(-([A-Za-z]{2}|[0-9]{3}))?`.

Fallback chain for active culture `A` and project default `D`: `A`, parent of `A` (drop the last subtag; `zh-Hans-CN` -> `zh-Hans` -> `zh`), `D`, parent of `D`, then the reference's inline default, then the literal `[Table:Key]`. Duplicates removed. `D` comes from `LOCALE.DEFAULT` (default `en`).

Active culture at startup, first hit wins: host override (`--culture <tag>` on the runtime and the editor; the editor's saved preview-culture pref) > `Settings` string `locale.culture` > the OS UI language (`GetUserDefaultLocaleName`) reduced by `bestCultureMatch` to the nearest available culture folder > `D`. `Loc.SetCulture(tag, persist: true)` writes `locale.culture` and flushes; the editor preview never persists.

### 2.2 Text references (the LOCTEXT analogue)

Any UI text field (text, items, placeholder, menu labels, settings labels, `Hud.Text`, `aver_ui_text`) may hold a **reference** instead of literal text:

```
reference := "@" Table ":" Key [ "=" DefaultText ]
Table     := [A-Za-z0-9_]+
Key       := [A-Za-z0-9_.\-]+
```

- `DefaultText` is everything after the first `=` (may be empty, may contain `=`). It is what shows if no culture in the chain has the key, and what Gather writes into the default culture's table.
- A string starting with `@@` is the literal that starts with a single `@` (`@@mention` shows `@mention`). A string starting with `@` that does not match the grammar is literal (no error).
- Resolution result for a valid reference: first hit in the chain; else `DefaultText` if the reference has an `=`; else `[Table:Key]`.
- Which text is translated is decided by the text itself, so the same rule works in `.ocui`, `.ocgraph` attributes, C# strings, event payloads and data-table cells. Typed text (TextInput contents, `TextChanged` events) is never resolved.
- Events keep raw strings: a `Choice`/`List` event's `text` is the raw item (the key), `index` is the stable identity.

## 3. File formats (exact)

### 3.1 Layout on disk

```
Content/Localization/<culture>/<Table>.ocstrings      one file per table per culture; <Table> must match [A-Za-z0-9_]+
Content/Localization/<culture>/<Table>.csv            same table as CSV (translators); .ocstrings wins if both exist (warn)
Content/Data/<anything>.csv|.json                     data tables (any folder; "Data" is only the editor's default)
```
Culture folders are discovered by listing `Localization/`; the folder name is normalised (`pt_br` -> `pt-BR`). A folder whose name is not a valid tag is ignored with a warning.

### 3.2 `.ocstrings` (OC dialect: `#` comment lines, `KEY value` records, `"quoted"` strings with `\" \\ \n \t`)

```
OCSTRINGS 1
CULTURE de
# Main menu
ENTRY Menu.Play "Spielen"
ENTRY Menu.Quit "Beenden" NOTE "Shown on the quit button; keep short"
```
- `OCSTRINGS 1` required first record; a higher version is rejected. `CULTURE` optional; the folder is authoritative, a mismatch is a diagnostic.
- `ENTRY <key> "<text>" [NOTE "<note>"]`. Key grammar as above. Text token may be bare only if it has no whitespace; the writer always quotes.
- `#` starts a comment only at the start of a line or of a token outside quotes (so `#` inside text is safe).
- Unknown record kinds are skipped and preserved by the writer (forward compat).
- Duplicate key: last wins, diagnostic. A malformed `ENTRY` line is **skipped with a diagnostic naming the line** (a typo must not blank a table at runtime); only a missing/unsupported header fails the parse.
- Writer merge (`existing` text given): line-preserving. Walk existing lines; an `ENTRY` whose key is still present is rewritten only if its text/note changed (else byte-identical); one whose key is gone is dropped; comments, blanks and unknown records stay put; new keys append after the last `ENTRY` (or at end), in `entries` order. Fresh write (no `existing`): header, `CULTURE`, entries in order.

### 3.3 String-table CSV

RFC 4180 (section 3.4). Header row required and case-insensitive: `Key,Text` plus optional `Note`. Table name = file stem.

### 3.4 Data tables: `.csv` and `.json`

**CSV**: RFC 4180 (quotes, doubled quotes, embedded newlines, CRLF or LF, optional UTF-8 BOM stripped). The delimiter is `,`, or `;` when the header line outside quotes has no `,` but has `;` (Excel in many locales); the writer keeps the detected delimiter and line ending. Row 1 = headers. **Column 1 is the row name** (its header text is kept but not interpreted; `Name` by convention). Remaining headers are column names; a header may carry a type hint `Name:type` where `type` is one of `string int float bool vec3 text strings` (a suffix that is not one of these is part of the name). Rows must have exactly the header's cell count (error names the line). Empty row name or a duplicate row name (ASCII case-insensitive) is an error.

```
Name,Damage:int,Weight:float,Stackable:bool,Display Name:text,Tags:strings,Tint:vec3
sword,25,1.5,false,@Items:sword.name=Sword,melee|iron,"1 0.8 0.2"
```
Cell conventions (the reader never converts; cells are stored as text and the type is advisory except for validation): bool is `true/false/1/0/yes/no`; vec3 is three numbers separated by space, `,`, `;` or `|`; strings is `|`-separated; text is a reference (section 2.2) or literal.

**JSON** (parsed with the existing strict `fmt::parseJson`, `Json.hpp:67`): either an array of objects `[{"Name":"sword","Damage":25}]` (row name from the first present key of `Name`, `RowName`, `name`; objects without one are an error) or an object of objects `{"sword":{"Damage":25}}` (row name = member key). Columns = union of member names in first-seen order. Cell text: string as is; number as shortest round-trip text; bool `true/false`; null empty; array of scalars joined by `|`; nested object is an error ("flatten it"). Column type hints are inferred from the first non-null value (number -> int if integral else float, bool, string, array -> strings) and marked `hinted` so the writer emits numbers/bools/arrays back as JSON values, not strings.

### 3.5 Project key

`LOCALE.DEFAULT <tag>` (default `en`). `ProjectDesc` gains `std::string localeDefault;` (empty = unstated = `en`); parse next to `GAME.MODE` (`OcProject.cpp:79-80`), add to `kOwned` (`:296-300`) and to the owned write block beside `GAME.MODE` (`:347-348`, written only when non-empty). `stage-game.ps1` carries it through unchanged.

## 4. Native design

### 4.1 Aver.Formats (new files, base target; add to `modules/formats/CMakeLists.txt` in the base `SOURCES` list)

`include/aver/formats/OcStrings.hpp` + `src/OcStrings.cpp`
```cpp
namespace aver::fmt {
struct OcStringEntry { std::string key, text, note; };
struct OcStringsData { int version = 1; std::string culture; std::vector<OcStringEntry> entries; };
// False only for a missing/unsupported header (err set). Per-line problems go to *diag, the line is skipped.
bool parseOcstrings(std::string_view text, OcStringsData& out, std::vector<std::string>* diag = nullptr, std::string* err = nullptr);
bool parseStringsCsv(std::string_view text, OcStringsData& out, std::vector<std::string>* diag = nullptr, std::string* err = nullptr);
std::string writeOcstrings(const OcStringsData& d, std::string_view existing = "");   // merge rule 3.2
std::string writeStringsCsv(const OcStringsData& d);
bool loadStrings(const std::string& path, OcStringsData& out, std::vector<std::string>* diag, std::string* err); // by extension
bool validLocKey(std::string_view); bool validLocTable(std::string_view);
// Reference grammar (section 2.2). Returns false when `s` is not a reference (including "@@...").
struct LocRef { std::string table, key, defaultText; bool hasDefault = false; };
bool parseLocRef(std::string_view s, LocRef& out);
std::string formatLocRef(const LocRef&);                       // inverse; "@Table:Key" or "@Table:Key=Default"
std::string normalizeCulture(std::string_view tag);            // "" when malformed
std::vector<std::string> cultureChain(std::string_view active, std::string_view dflt);   // section 2.1, tags only
}
```
`include/aver/formats/OcTable.hpp` + `src/OcTable.cpp`
```cpp
namespace aver::fmt {
enum class OcTableType : u8 { String, Int, Float, Bool, Vec3, Text, Strings };   // == AVER_FW_TABLE_COL_*
struct OcTableColumn { std::string name; OcTableType type = OcTableType::String; bool hinted = false; };
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
    std::string_view cell(usize r, usize c) const;
};
bool parseTableCsv (std::string_view text, OcTableData& out, std::string* err = nullptr);
bool parseTableJson(std::string_view text, OcTableData& out, std::string* err = nullptr);
bool loadTable(const std::string& path, OcTableData& out, std::string* err = nullptr);   // by extension; JSON that is not table-shaped fails with a clear reason
std::string writeTableCsv (const OcTableData&);
std::string writeTableJson(const OcTableData&);
bool saveTable(const std::string& path, const OcTableData&, std::string* err = nullptr); // atomic (writeFileTextAtomic)
// Human-readable problems: duplicate/empty row names, cells that do not parse as their hinted type.
std::vector<std::string> validateTable(const OcTableData&);
const char* tableTypeWord(OcTableType); bool parseTableTypeWord(std::string_view, OcTableType&);
}
```
`include/aver/formats/LocGather.hpp` + `src/LocGather.cpp` (pure text scanners; the editor walks files)
```cpp
namespace aver::fmt {
struct LocUse { std::string table, key, defaultText; bool hasDefault = false; std::string source; /* "path:line" */ };
void gatherOcui   (std::string_view text, std::string_view path, std::vector<LocUse>& out);  // SET text|items|placeholder values (items split on '\n')
void gatherOcgraph(std::string_view text, std::string_view path, std::vector<LocUse>& out);  // any NODE attribute value that is a reference
void gatherCsharp (std::string_view text, std::string_view path, std::vector<LocUse>& out);  // Loc.Text/Format/Plural("T","K"[,"D"]) and new LocText("T","K"[,"D"]); string literals only, // comments skipped
void gatherTable  (const OcTableData&, std::string_view path, std::vector<LocUse>& out);     // every cell that is a reference
using LocCorpus = std::map<std::string /*culture*/, std::map<std::string /*table*/, OcStringsData>>;
struct LocReport {
    struct Missing { std::string culture, table, key; std::string source; };
    struct Unused  { std::string culture, table, key; };
    struct Conflict{ std::string table, key; std::vector<std::string> defaults; };
    std::vector<Missing> missing;   // used, absent in a culture (default culture absent only when no inline default)
    std::vector<Unused> unused;     // present in a table, never used
    std::vector<Conflict> conflicts;// same Table:Key with different inline defaults
    std::vector<Missing> untranslated; // non-default culture text identical to the default culture's
};
LocReport buildLocReport(const std::vector<LocUse>& used, const LocCorpus&, std::string_view defaultCulture);
// Gather: adds a default-culture entry (text = inline default) for every used key missing there; returns count added.
usize applyGather(const std::vector<LocUse>& used, LocCorpus&, std::string_view defaultCulture);
}
```
Also `ProjectDesc::localeDefault` (3.5).

### 4.2 Aver.Framework: `modules/framework/include/aver/framework/framework_loc_abi.h` (+ `src/LocalizationAbi.cpp`)

Own version constant, framework_abi.h untouched; add both new .cpp files to the `add_library(Aver.Framework SHARED ...)` list (`modules/framework/CMakeLists.txt`). Conventions as `framework_timers_abi.h`: `int32_t/float/const char*`, UTF-8, single game thread, returned `const char*` valid until the next call that returns a string.

```c
#include "aver/framework/framework_abi.h"
#define AVER_FW_LOC_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_loc_abi_version(void);

/* Scans utf8Dir (e.g. <content>/Localization) for culture folders; defaultCulture is LOCALE.DEFAULT ("" = "en").
 * Replaces any previous state, loads the default culture, makes it active. Returns the number of culture
 * folders found, -1 if utf8Dir is not a directory (the store stays usable: everything resolves to defaults). */
AVER_FW_ABI int32_t aver_fw_loc_init(const char* utf8Dir, const char* utf8DefaultCulture);
AVER_FW_ABI void    aver_fw_loc_clear(void);                       /* back to the empty store */
AVER_FW_ABI int32_t aver_fw_loc_culture_count(void);               /* folders found, normalised, sorted */
AVER_FW_ABI const char* aver_fw_loc_culture_at(int32_t index);     /* "" out of range */
AVER_FW_ABI const char* aver_fw_loc_default_culture(void);
AVER_FW_ABI const char* aver_fw_loc_culture(void);                 /* active, normalised */
/* -1 malformed tag (nothing changes); 0 set, but neither it nor its parent has tables (resolves from the
 * fallback); 1 set. Loads the new chain's tables lazily, bumps the epoch, and POSTS the framework event
 * "loc.culture_changed" (aver_fw_event_post, one string arg = the culture) so AN_OnEvent graphs can react. */
AVER_FW_ABI int32_t aver_fw_loc_set_culture(const char* utf8Tag);
AVER_FW_ABI int32_t aver_fw_loc_epoch(void);                       /* bumps on init/set_culture/reload/clear */

/* The relay target. utf8Ref is the WHOLE raw string. Returns -1 when it is not a reference (caller keeps the
 * literal; "@@x" is not a reference but the caller still unescapes - see UiTree::shown). Otherwise returns the
 * byte length of the resolved text and copies up to cap-1 bytes + NUL into out (out NULL or cap <= 0: length
 * only; a return >= cap means truncated, retry with a bigger buffer). Never fails for a valid reference: it
 * falls through the chain, then the inline default, then "[Table:Key]". Records misses (below). */
AVER_FW_ABI int32_t aver_fw_loc_resolve(const char* utf8Ref, char* out, int32_t cap);
/* Table/key lookup through the chain WITHOUT default or bracket fallback. Same length/copy rule; -1 if absent. */
AVER_FW_ABI int32_t aver_fw_loc_get(const char* utf8Table, const char* utf8Key, char* out, int32_t cap);
AVER_FW_ABI int32_t aver_fw_loc_has(const char* utf8Table, const char* utf8Key);
/* A file under <dir>/<culture>/ changed or was deleted on disk. If its culture is loaded, reparse that table
 * (a failed parse keeps the old table and logs), bump the epoch. 1 if it was a localization file, else 0. */
AVER_FW_ABI int32_t aver_fw_loc_reload_path(const char* utf8AbsPath);
/* "Table:Key" requested since init/clear that no culture in the chain had, unique, in first-hit order. */
AVER_FW_ABI int32_t aver_fw_loc_missing_count(void);
AVER_FW_ABI const char* aver_fw_loc_missing_at(int32_t index);
AVER_FW_ABI void    aver_fw_loc_missing_clear(void);
```
Store rules (in `LocalizationAbi.cpp`; one file-static store like `FrameworkAbi.cpp` does for the input scheme): cultures load lazily when they enter the chain; each file parsed through `fmt::loadStrings`, diagnostics logged `AVER_WARN("[Loc] <file>: <diag>")`. Missing key: recorded once; logged `AVER_WARN` once when no inline default masks it, `AVER_TRACE` when one does (a pre-Gather project is normal). Lookup is a hash map per (culture, table).

`framework_table_abi.h` (+ `src/DataTableAbi.cpp`), `AVER_FW_TABLE_ABI_VERSION 1`:
```c
#define AVER_FW_TABLE_COL_STRING 0
#define AVER_FW_TABLE_COL_INT    1
#define AVER_FW_TABLE_COL_FLOAT  2
#define AVER_FW_TABLE_COL_BOOL   3
#define AVER_FW_TABLE_COL_VEC3   4
#define AVER_FW_TABLE_COL_TEXT   5
#define AVER_FW_TABLE_COL_STRINGS 6
AVER_FW_ABI int32_t aver_fw_table_abi_version(void);

/* The project's content root. Relative table paths resolve against it; hosts set it when a project opens. */
AVER_FW_ABI int32_t aver_fw_content_set_root(const char* utf8Dir);
AVER_FW_ABI const char* aver_fw_content_root(void);

/* Loads (or returns the existing handle for) a .csv/.json table. Path is absolute or content-relative; the
 * key is the normalised absolute path. Returns a handle > 0, or 0 with aver_fw_table_error() set. */
AVER_FW_ABI int32_t aver_fw_table_load(const char* utf8Path);
AVER_FW_ABI const char* aver_fw_table_error(void);
AVER_FW_ABI const char* aver_fw_table_path(int32_t h);                /* absolute; "" invalid */
AVER_FW_ABI int32_t aver_fw_table_unload(int32_t h);
/* Re-reads the file. A parse failure KEEPS the old data (a half-saved file must not wipe a live table),
 * sets the error, returns 0. Success swaps the data, bumps the table's version and the global epoch, returns 1. */
AVER_FW_ABI int32_t aver_fw_table_reload(int32_t h);
AVER_FW_ABI int32_t aver_fw_table_reload_path(const char* utf8AbsPath);   /* count of loaded tables reloaded; "" or NULL = reload every loaded table (watcher overflow) */
AVER_FW_ABI int32_t aver_fw_table_version(int32_t h);                 /* -1 invalid; starts at 1 */
AVER_FW_ABI int32_t aver_fw_tables_epoch(void);                       /* bumps on any successful (re)load */
AVER_FW_ABI int32_t aver_fw_table_column_count(int32_t h);            /* excludes the row-name column */
AVER_FW_ABI const char* aver_fw_table_column_name(int32_t h, int32_t c);
AVER_FW_ABI int32_t aver_fw_table_column_type(int32_t h, int32_t c);  /* AVER_FW_TABLE_COL_* (advisory) */
AVER_FW_ABI int32_t aver_fw_table_column_find(int32_t h, const char* name);   /* -1 */
AVER_FW_ABI int32_t aver_fw_table_row_count(int32_t h);
AVER_FW_ABI const char* aver_fw_table_row_name(int32_t h, int32_t r);
AVER_FW_ABI int32_t aver_fw_table_row_find(int32_t h, const char* name);      /* -1 */
AVER_FW_ABI const char* aver_fw_table_cell(int32_t h, int32_t r, int32_t c);  /* "" out of range */
```
`const char*` from `row_name`/`cell`/`column_name` point into table storage: valid until that table is reloaded or unloaded; `error`/`path` until the next call that returns a string. Add a paragraph to `docs/ABI.md` section 4 next to the timers/blackboard sentence (line ~255), naming both headers and versions.

### 4.3 Aver.UI + Aver.UI.Abi

`UiTree` (`modules/ui/include/aver/ui/UiTree.hpp`, `src/UiTree.cpp`):
```cpp
using UiTextResolveFn = std::function<bool(std::string_view raw, std::string& out)>; // true = raw was a reference, out = resolved
void setTextResolver(UiTextResolveFn fn, std::function<u32()> epoch = {});
// Display form of an authored string: raw itself unless it starts with '@'; "@@x" -> "@x"; a valid reference ->
// resolver(raw) if set and true, else its inline default, else "[Table:Key]". Cached per raw string; the cache is
// cleared when epoch() changes (checked at the top of update(), layout() and draw()). Returns raw for non-references.
const std::string& shown(const std::string& raw) const;
```
The reference grammar is needed here without Formats (Core-only): implement a ~25-line private `uiParseTextRef` in `UiText.cpp` (same grammar as `parseLocRef`; a test asserts the two agree on a table of cases). Apply `shown()` at: `UiTreeLayout.cpp:97,101,102,108,109,113,122,126` (text and items), `UiTreeDraw.cpp:120,124,127,156,157,174,206,228,249,268` and the `drawBlock` calls fed from them, plus `placeholder` measuring if any. **Do not** apply it to `displayText` (TextInput, `UiTreeInput.cpp:317`) or to event payloads (`UiTreeInput.cpp:194,302,393,400,482,565,588`). `setText()` keeps storing raw.

`UiScreens.cpp`: replace English literals with references (key prefix `UI:settings.`/`UI:common.`/`UI:menu.`, inline default = the old text): lines 69-87 (labels, tabs, quality choices), 125, 132 source, 156, 207, 211; `UiTreeDraw.cpp:157` becomes `@UI:controls.pressKey=Press a key...`; `UiLayoutAsset.cpp:259-269,406-412` starter text stays literal (editor starters). Existing UI tests that read `widget.text` for these must read `tree.shown(widget.text)` instead (UiWidgetTest); behaviour with no resolver is unchanged because a reference shows its inline default.

`ui_widget_abi.h`, in the environment section (after `aver_ui_scale`, :104):
```c
/* Installs the key resolver: given a raw string starting with '@', returns the resolved byte length (copying up to cap-1
 * bytes + NUL into out) or -1 if it is not a reference. aver_fw_loc_resolve has exactly this signature, so a host
 * passes it directly. NULL clears. Also applies to aver_ui_text / aver_ui_text_width. `epoch` (may be NULL) is polled
 * once per update/layout/draw; when its value changes the tree drops its resolved-string cache. */
typedef int32_t (*AverUiTextResolveFn)(const char* utf8Raw, char* out, int32_t cap);
typedef int32_t (*AverUiTextEpochFn)(void);
AVER_UI_API void aver_ui_set_text_resolver(AverUiTextResolveFn resolve, AverUiTextEpochFn epoch);  /* aver_fw_loc_epoch */
AVER_UI_API const char* aver_ui_widget_get_shown_text(uint32_t id);   /* text after resolution (inspection, tests) */
```
`UiWidgetAbi.cpp`: the setter wraps the C pointers into `g_tree.setTextResolver(...)` (retrying with a larger buffer when the return is >= cap) and stores them for `UiAbi.cpp`'s `aver_ui_text` / `aver_ui_text_width` (`:103-114`) through a helper declared in `UiAbiState.hpp` (`bool resolveUiText(const char* raw, std::string& out)`, defined in `UiAbi.cpp`). C#: `Native.cs` declarations, `Widget.ShownText` (`Widgets.cs`), and nothing else (`Widget.Text` stays raw). `render.ui` is unchanged (it only submits the draw list).

### 4.4 Hosts (Runtime.Game.Core, shared by GameApp and SandboxApp)

New `Runtime/include/aver/game/GameLocalization.hpp` + `Runtime/src/GameLocalization.cpp` in `Aver.Runtime.Game.Core` (`Runtime/CMakeLists.txt`: add the source, add `Aver.Framework` and `Aver.Settings` to its link/`if(TARGET ...)` half, guarded `#if AVER_MODULE_FRAMEWORK`). Linking `Aver.Settings` natively is new (it was managed-only, game.allowlist comment): harmless, the DLL is already staged unconditionally, and the both-ways import check now sees it.
```cpp
namespace aver::game {
struct LocalizationInit {
    const fmt::ProjectDesc* project = nullptr;
    std::string settingsPath;        // runtime: userDataDir()\<Project>\Settings.ini ; editor: <project>\Saved\Settings.ini
    std::string cultureOverride;     // --culture, or the editor's preview-culture pref; empty = auto
    bool readSavedCulture = true;    // false in the editor (preview never reads the player's saved choice)
};
// aver_fw_content_set_root(contentDir); aver_fw_loc_init(contentDir/Localization, project.localeDefault);
// picks the culture per 2.1; installs aver_ui_set_text_resolver(&aver_fw_loc_resolve, &aver_fw_loc_epoch);
// logs one line. Returns the chosen culture.
std::string initLocalization(const LocalizationInit&);
std::string systemUiCulture();                                   // "" when unknown (GetUserDefaultLocaleName)
std::string bestCultureMatch(const std::vector<std::string>& available, std::string_view wanted); // pure: exact, else same language (prefer same script/region), else ""
// Hot reload: for each non-deleted event under contentRoot, aver_fw_loc_reload_path + aver_fw_table_reload_path
// (deletions also, so a removed table drops). Returns the number of files that touched either store.
usize onContentFilesChanged(const std::string& contentRoot, const std::vector<FileEvent>&);
}
```
Call sites:
- `GameApp.cpp`: after `content_.adopt(project_)` (:1079) call `initLocalization`; add `GameConfig::culture` + `--culture <tag>` in `parseArgs` (:402-); factor the settings path computation out of `initScripting` (:1171) into one member used by both. `loadGameUiFont` (:752) unchanged.
- `SandboxProject.cpp` `applyProject`: after `content_.adopt(project_)` (:146) call `initLocalization` with `cultureOverride = prefs "loc.previewCulture"`, `readSavedCulture=false`; `--culture` flag in `SandboxMain.cpp`.
- `SandboxPlay.cpp`: at Play start, inside the block at :288-309, call `aver_fw_loc_init` again (same dir/default) so a saved table edit applies, then restore the preview culture; `aver_fw_loc_missing_clear()` so the Play report is per session.
- `SandboxAutosave.cpp` `pumpContentWatch` (:448-456): inside the event loop call `game::onContentFilesChanged` once with the whole `watchEvents_`; on watcher overflow (:434-447) call `aver_fw_loc_init` again (same dir and default, then restore the active culture) and `aver_fw_table_reload_path("")` (reload all).

### 4.5 Fonts

Re-bake the shipped font for the cultures v1 supports (Latin incl. accents, Greek, Cyrillic): add `--ranges "32-126,160-591,880-1023,1024-1327,8208-8231,8364"` to `scripts/bake-font.py` (skip code points the TTF lacks, i.e. a glyph whose mask equals the .notdef mask), regenerate `assets/fonts/Roboto-Regular.{ocfont,png}` (about 800 glyphs; atlas height grows to a power of two). Filenames and the deploy rule (`sandbox/CMakeLists.txt:562-572`) are unchanged. CJK, Arabic, Hebrew, Indic are **not** covered (needs another font, shaping and bidi; open question 1).

## 5. C# API (all in `scripting/csharp/Aver.Framework`, namespace `Aver.Framework`)

`Native.cs`-style P/Invoke classes go in the new files (`internal static class FwLoc`, `FwTable`, `[DllImport("Aver.Framework")]`, resolved by the existing `NativeResolver.cs`), not in `Native.cs`.

**`Localization.cs`**
```csharp
public static class Loc {
    public static string Culture { get; }                    // active, normalised
    public static string DefaultCulture { get; }
    public static IReadOnlyList<string> Cultures { get; }    // folders found
    public static CultureInfo CultureInfo { get; }           // for number/date formatting; Invariant if the OS lacks it
    public static bool SetCulture(string tag, bool persist = true);   // false when malformed; persist -> Settings "locale.culture" + Flush
    public static string Text(string table, string key, string? defaultText = null);   // chain, then default, then "[Table:Key]"
    public static bool TryGet(string table, string key, out string text);               // no fallback to default text
    public static string Resolve(string textOrReference);    // "@T:K=D" resolved, "@@x" -> "@x", anything else unchanged
    public static string Format(string table, string key, string? defaultText, params object?[] args);   // string.Format(CultureInfo, Text(..), args)
    public static string Plural(string table, string key, long count, params object?[] args);           // looks up key.<category>, falls back to key.other, then key; args[0] defaults to count
    public static IReadOnlyList<string> MissingKeys { get; }  // "Table:Key" since the last clear
    public static void ClearMissing();
    public static event Action<string>? CultureChanged;       // raised from Pump(), game thread, arg = new culture
    public static void Pump();                                // compares aver_fw_loc_epoch + culture; raises CultureChanged
}
public readonly struct LocText {                              // the LOCTEXT field: lazy, re-resolves after a culture switch
    public LocText(string table, string key, string? defaultText = null);
    public LocText(string reference);                         // "@T:K=D" or literal
    public string Table { get; } public string Key { get; } public string? Default { get; } public bool IsEmpty { get; }
    public override string ToString();                        // Loc.Resolve
    public string Format(params object?[] args);
    public static implicit operator string(LocText t);
}
public static class LocFormat {                               // pure, testable without natives
    public static string PluralCategory(string culture, long n);   // "one" "few" "many" "other"
    public static string Apply(string template, CultureInfo culture, object?[] args);   // composite format; a bad template returns it unformatted
}
```
`PluralCategory` rules by primary language: `ja zh ko vi th id ms` -> other; `fr pt` -> one for n in {0,1}; `ru uk be` -> one (n%10==1 and n%100!=11), few (n%10 in 2..4 and n%100 not in 12..14), many (otherwise for integers), other; `pl` -> one (n==1), few (n%10 in 2..4 and n%100 not in 12..14), many (other integers); `cs sk` -> one (n==1), few (n in 2..4), other; default (`en de es it nl sv da nb fi el hu tr ...`) -> one if n==1 else other. Table keys then read `Items.count.one` etc.

`Loc.SetCulture` calls `aver_fw_loc_set_culture`; `Pump()` is where `CultureChanged` fires so UI code is never re-entered mid-frame.

**`DataTables.cs`**
```csharp
public enum DataColumnType { String, Int, Float, Bool, Vec3, Text, Strings }      // == AVER_FW_TABLE_COL_*
public interface IRawTable { int ColumnCount { get; } string ColumnName(int c); DataColumnType ColumnType(int c);
                             int RowCount { get; } string RowName(int r); string Cell(int r, int c); }
[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)] public sealed class DataColumnAttribute : Attribute { public DataColumnAttribute(string name); }
[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)] public sealed class DataIgnoreAttribute : Attribute { }
[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)] public sealed class DataRowNameAttribute : Attribute { }   // receives the row name (string member)

public static class DataTableBinder {                          // pure: no natives
    public static T[] Bind<T>(IRawTable table, List<string> problems);        // row i of the result is table row i
}
public sealed class DataTable : IRawTable {                    // untyped view, what graph nodes use
    public string Path { get; } public int Version { get; }
    public int RowCount { get; } public int ColumnCount { get; }
    public int FindRow(string name);  public int FindColumn(string name);       // -1
    public string RowName(int r); public string ColumnName(int c); public DataColumnType ColumnType(int c);
    public string Cell(int r, int c);
    public string GetString(int r, int c, string fallback = ""); public int GetInt(int r, int c, int fallback = 0);
    public float GetFloat(int r, int c, float fallback = 0); public bool GetBool(int r, int c, bool fallback = false);
    public bool TryGetVec3(int r, int c, out Aver.Scene.Vec3 v);
}
public sealed class DataTable<T> : IReadOnlyCollection<KeyValuePair<string, T>> {
    public string Path { get; } public int Version { get; } public int Count { get; }
    public IReadOnlyList<string> Problems { get; }             // bind diagnostics: "row 'sword' column 'Damage': 'abc' is not an int"
    public IEnumerable<string> RowNames { get; } public IEnumerable<T> Rows { get; }
    public bool TryGetRow(string name, out T row);             // ASCII case-insensitive
    public T GetRow(string name);                              // KeyNotFoundException naming table and row
    public event Action<DataTable<T>>? Reloaded;               // raised from DataTables.Pump on a hot reload
}
public static class DataTables {
    public static DataTable<T> Load<T>(string path);           // cached per (absolute path, T); relative to aver_fw_content_root; never null - a missing/bad file logs and yields an empty table that fills when the file appears
    public static DataTable Open(string path);                 // untyped, cached per path
    public static void Pump();                                 // aver_fw_tables_epoch changed -> rebuild affected typed tables, raise Reloaded; also rebuilds tables with Text columns when aver_fw_loc_epoch changed
}
internal static class ContentPump { internal static void Tick() { Loc.Pump(); DataTables.Pump(); } }
```
Binder rules: members are public instance properties with setters (incl. `init`) and public fields, minus `[DataIgnore]`. Column match to member: case-insensitive with spaces and underscores ignored (`Display Name` matches `DisplayName`), `[DataColumn("x")]` overrides. Construction: public parameterless constructor (class or struct) then assign members; otherwise a single public constructor whose parameter names match columns (positional record). Supported member types: `string`, `bool`, all integer types, `float`, `double`, enums (name case-insensitive or integer), `Aver.Scene.Vec3`, `LocText`, `T[]`/`List<T>`/`IReadOnlyList<T>` of the scalars above (cell split on `|`), and `Nullable<>` of the scalars (empty cell = null). Parsing is invariant-culture (`1.5`, `1.5f`). A missing column leaves the member default and adds a problem; an extra column adds an info problem; a conversion failure leaves the default and adds a problem naming row, column and text. A `Text` column bound to `string` receives the **resolved** text at bind time (re-bound on a culture change); bound to `LocText` it stays lazy.

`ContentPump.Tick()` is called from `HostBridge.DispTickAll` group 0 next to the UI pump: add exactly one line after `HostBridge.cs:1469`: `try { Aver.Framework.ContentPump.Tick(); } catch (DllNotFoundException) { }` (make `ContentPump` `internal`; Bridge already has InternalsVisibleTo, `Aver.Framework.csproj`).

`UiGameSettings.Create()` (`UiGameSettings.cs`): when `Loc.Cultures.Count > 1` add a Choice setting `game.language` (label `@UI:settings.language=Language`, tab `@UI:settings.tab.game=Game`, choices = `CultureInfo.NativeName` of each culture) and handle `setting:game.language` by `Loc.SetCulture(Loc.Cultures[index])`; `ApplyAll()` selects the current index. Its other labels become references with inline defaults like section 4.3.

## 6. Editor surface (all in `sandbox/src`, headless core testable with `AVER_WITH_IMGUI` undefined)

1. **`DataTableEditor.{hpp,cpp}`** - asset tab for `.csv` / table-shaped `.json`, modelled on `InputSchemeEditor` (state = `fmt::OcTableData`, `SnapshotUndo<OcTableData>`, dirty tab keeps edits on disk change, `onFileChanged` reloads when clean). Factory `makeDataTableEditor(path)`: `.csv` always (except CSVs directly under `Localization/<culture>/`, which belong to the string-table editor, registered first); `.json` only if `loadTable` succeeds, else returns null so the shell opens it. ImGui grid (`BeginTable`, scroll both axes): header shows `name : type`; cells use a widget by type (checkbox for bool, `InputInt`/`InputFloat`, `DragFloat3` for vec3, text otherwise; a `text` cell shows its resolved preview under the cell when it is a reference); invalid cells tinted with the `validateTable` message in the tooltip. Toolbar: filter box (substring over row names and cells), Add/Duplicate/Delete row, Add/Delete/Rename column, column type combo (sets `hinted`), "Rename row" with uniqueness check, banner "changed on disk" for a dirty tab. Headless API: `addRow/duplicateRow/removeRow/renameRow(index,name)`, `addColumn/removeColumn/renameColumn/setColumnType`, `setCell(r,c,text)`, `problems()`, `save(why)`, `undo/redo`, `loaded()/loadError()/data()`.
2. **`StringTableEditor.{hpp,cpp}`** - asset tab for `Localization/<culture>/<Table>.ocstrings|.csv`; one tab per table. It loads that table in **every** culture folder and shows a grid: rows = union of keys, columns = Key, Note, then one column per culture (default culture first, editable); cells missing in a culture are tinted and an empty edit creates the entry. Toolbar: Add key (to all cultures, empty text for non-default), Delete key, Rename key (cascades to every culture), Add culture (creates the folder and copies keys with empty text), a "Show missing only" toggle, **Gather** and **Report**. Save writes each dirty culture file through `writeOcstrings(data, existingText)` (comments survive; CSV files through `writeStringsCsv`). Diagnostics from parsing are listed at the bottom.
3. **Gather / Report** (`LocGatherRun.{hpp,cpp}`, pure over a file list so it is testable): walks the content root (skip `bin/ obj/ Source/`), runs `gatherOcui` on `.ocui`, `gatherOcgraph` on `.ocgraph`, `gatherCsharp` on `.cs`, `gatherTable` on data tables; loads the corpus; `buildLocReport`. "Gather" runs `applyGather` and writes the default culture files (creating `Localization/<default>/<Table>.ocstrings` as needed); "Report" shows the lists (missing per culture, unused, conflicts, untranslated) with a "Copy as text" button and "Open table" on a row. The same report appears in a **Play session summary**: when Play ends, if `aver_fw_loc_missing_count() > 0`, one `notifyOutcome` toast "N localization keys were missing" with the keys in the console log.
4. **Project Settings > Description** (`SandboxSettings.cpp`, new block after the `GAME.MODE` picker, before `ImGui::PopItemWidth()` at ~:826): "Default culture" combo (cultures found + free text) writing `project_.localeDefault` with `projectDirty_ = true`; "Preview culture" combo (editor-only, `EditorPrefs` key `loc.previewCulture`, applies immediately via `aver_fw_loc_set_culture`).
5. **UI Layout editor** (`UiLayoutEditor.{hpp,cpp}`): install the resolver on its private `UiTree` (`setTextResolver` with `aver_fw_loc_resolve`/`aver_fw_loc_epoch`) so the preview shows the preview culture; a culture combo in its toolbar (same pref); in the inspector, text/placeholder/items fields get a "Localize" button that rewrites the literal to `@UI:<Layout>.<Widget>.<prop>=<literal>`, and a read-only "Shown:" line under any field holding a reference.
6. **Content Browser** (`SandboxContentBrowser.cpp`): "New Data Table" (`Data/NewTable.csv`: `Name,Value:int` / `row1,1`, via `cbFreeAssetPath` + `editor::writeNewFile`), "New String Table" (asks the table name; writes `Localization/<default>/<Name>.ocstrings`), icon rows for `.ocstrings` (and `.csv` as "Data Table"), a context-menu "Open as Data Table" for `.json`.
7. **Registrations** in `SandboxApp.cpp` beside :443-454, appended in this order (first match wins): `makeStringTableEditor`, `makeDataTableEditor`.
8. **Hot reload**: `pumpContentWatch` as in 4.4. Open tabs get `onFileChanged` through the existing path.

## 7. Runtime, packaged game, save/load

- The packaged game needs no staging change: `.ocstrings`, `.csv`, `.json` are copied by the content filter (`stage-game.ps1:369-385`); `Game.ocproject` carries `LOCALE.DEFAULT`; `Aver.Framework.dll` and `Aver.Settings.dll` are already in `game.allowlist`. `verify-game.ps1` needs no change. No `.ocpak` interaction: tables are read loose like every other asset until the pak slice lands (the pak's `IContentSource::read` is the future single read point; both stores read through `platform::readFileText`).
- Only the editor hot reloads (the watcher exists only there). The standalone runtime loads on startup.
- Save/load: **the culture is not part of a save** (it is the player's setting, `locale.culture`); data tables are read-only content, never saved. `LocText` and table rows hold no state to persist. Because `.ocsave` stores only world data, loading a save never changes language.
- Gameplay code that cached a resolved string must listen: `Loc.CultureChanged` or the bus event `loc.culture_changed` (graph: `AN_OnEvent event=loc.culture_changed`).
- Module guards: everything C# is inert without natives (`DllNotFoundException` swallowed in the pump line); every native call from `sandbox`/`Runtime` is under `#if AVER_MODULE_FRAMEWORK`. `module-guard-audit.py` and the module matrix rows are unaffected (no new option, no manifest edit, no catalog regeneration).

## 8. Graph nodes (for the graph-node agent; pins are float/int/bool only, so names and paths are attributes)

C# targets are new public static classes written by this feature: `DataContentGraph` (`scripting/csharp/Aver.Framework/DataContentGraph.cs`) and `LocGraph` (`LocGraph.cs`). Methods (all `public static`, reading through `DataTables.Open` which caches by path):

```csharp
// DataContentGraph
bool FindRowForGraph(string table, string row, out int rowIndex)
bool FindRowByIntForGraph(string table, string column, int value, out int rowIndex)   // first row whose column parses to value
int  RowCountForGraph(string table)
bool GetIntForGraph  (string table, string column, int row, out int value)
bool GetFloatForGraph(string table, string column, int row, out float value)
bool GetBoolForGraph (string table, string column, int row, out bool value)
bool GetVec3ForGraph (string table, string column, int row, out float x, out float y, out float z)
bool SetUiTextForGraph(int widget, string table, string column, int row)    // widget text := cell (a reference still resolves in the UI)
// LocGraph
bool SetCultureForGraph(string culture)
bool IsCultureForGraph(string culture)            // active tag equals it, or starts with it + "-"
int  CultureCountForGraph()
int  CurrentCultureIndexForGraph()                // -1 if unknown
bool SetCultureByIndexForGraph(int index)
bool SetUiItemsToCulturesForGraph(int widget)     // Choice/List items := culture native names, selection := current
```
Rows to append verbatim to `Table()` in `GraphGameSystemNodeTable.cs` (marked block `// ---- data tables and localization (docs/plans/wave2/DATA_TABLES_LOCALIZATION.md) ----`), using `using Aver.Framework;` already present:
```csharp
Reg("an_finddatatablerow", GenericKind.Pure, typeof(DataContentGraph), "FindRowForGraph", "", "row:int,found:bool", "@table,@row,>row", "found"),
Reg("an_finddatatablerowbyint", GenericKind.Pure, typeof(DataContentGraph), "FindRowByIntForGraph", "value:int", "row:int,found:bool", "@table,@column,p:value,>row", "found"),
Reg("an_datatablerowcount", GenericKind.Pure, typeof(DataContentGraph), "RowCountForGraph", "", "count:int", "@table", "count"),
Reg("an_getdatatableint", GenericKind.Pure, typeof(DataContentGraph), "GetIntForGraph", "row:int", "value:int,success:bool", "@table,@column,p:row,>value", "success"),
Reg("an_getdatatablefloat", GenericKind.Pure, typeof(DataContentGraph), "GetFloatForGraph", "row:int", "value:float,success:bool", "@table,@column,p:row,>value", "success"),
Reg("an_getdatatablebool", GenericKind.Pure, typeof(DataContentGraph), "GetBoolForGraph", "row:int", "value:bool,success:bool", "@table,@column,p:row,>value", "success"),
Reg("an_getdatatablevec3", GenericKind.Pure, typeof(DataContentGraph), "GetVec3ForGraph", "row:int", "x:float,y:float,z:float,success:bool", "@table,@column,p:row,>x,>y,>z", "success"),
Reg("an_setuitextfromtable", GenericKind.Exec, typeof(DataContentGraph), "SetUiTextForGraph", "widget:int,row:int", "success:bool", "p:widget,@table,@column,p:row", "success"),
Reg("an_setculture", GenericKind.Exec, typeof(LocGraph), "SetCultureForGraph", "", "success:bool", "@culture", "success"),
Reg("an_isculture", GenericKind.Pure, typeof(LocGraph), "IsCultureForGraph", "", "matches:bool", "@culture", "matches"),
Reg("an_culturecount", GenericKind.Pure, typeof(LocGraph), "CultureCountForGraph", "", "count:int", "", "count"),
Reg("an_currentcultureindex", GenericKind.Pure, typeof(LocGraph), "CurrentCultureIndexForGraph", "", "index:int", "", "index"),
Reg("an_setculturebyindex", GenericKind.Exec, typeof(LocGraph), "SetCultureByIndexForGraph", "index:int", "success:bool", "p:index", "success"),
Reg("an_setuiitemstocultures", GenericKind.Exec, typeof(LocGraph), "SetUiItemsToCulturesForGraph", "widget:int", "success:bool", "p:widget", "success"),
```
Pure reads return a failed lookup as `found/success=false` with zeroed outs (never throw). Palette mirror rows for `GraphNodeDefsGameSystems.hpp` (categories `"Data"` and `"Localization"`; pin order as in the table, exec pins first for Exec nodes, `then` after the inputs), with attrs: `table` ("Data table path"), `row` ("Row name"), `column` ("Column"), `culture` ("Culture"). Example:
```cpp
t.push_back({"AN_FindDataTableRow", "Find Data Table Row", "Data", {
    pin("row", "int", true), pin("found", "bool", true)}, {attr("table", "Data table"), attr("row", "Row name")}});
t.push_back({"AN_GetDataTableFloat", "Get Data Table Float", "Data", {
    pin("row", "int", false), pin("value", "float", true), pin("success", "bool", true)},
    {attr("table", "Data table"), attr("column", "Column")}});
t.push_back({"AN_SetCulture", "Set Culture", "Localization", {
    pin("exec", "exec", false), pin("then", "exec", true), pin("success", "bool", true)}, {attr("culture", "Culture")}});
```
Add the 14 rows to `docs/AVER_NODE_NODES.md` under "Game systems". **Text in graphs**: no string pins exist, so localized text reaches the screen by writing a reference into `AN_SetUiText text=` (e.g. `text="@UI:hud.ammo=Ammo"`; the UI resolves it) or `AN_SetUiTextFromTable`; reacting to a language change uses the existing `AN_OnEvent event=loc.culture_changed`. Verify the NODE-line parser keeps `=` and `@` inside a quoted attribute value (a test below).

## 9. Tests (all new files; none edits an existing test except where stated)

Register native tests with `add_executable(...)` + `target_link_libraries` only (no `add_test`; `aver_register_ctest_in` picks up `*Test`).

| Test | Where | Asserts |
|---|---|---|
| `OcStringsTest` | `tests/formats/src/OcStringsTest.cpp`, target in `tests/formats/CMakeLists.txt` | header required and version-gated; ENTRY with escapes `\" \\ \n \t`; `#` inside text; NOTE; unknown records skipped and preserved; duplicate key last-wins with diagnostic; malformed ENTRY skipped with the line number and the rest still loads; `write(parse(x))` merge: unchanged entries byte-identical, edited rewritten, removed dropped, comments/blanks stay, new keys appended; fresh write round trip; CSV `Key,Text,Note` incl. quotes, embedded newline, BOM, `;` delimiter; `parseLocRef`/`formatLocRef` table of cases (valid, `=` in default, empty default, `@@`, `@mention`, bad table chars); `normalizeCulture` (`pt_br`->`pt-BR`, `zh-hans`->`zh-Hans`, malformed -> ""); `cultureChain` (`zh-Hans-CN`, default `en-GB`, dedup) |
| `OcTableTest` | `tests/formats/src/OcTableTest.cpp` | CSV: quotes, doubled quotes, CRLF/LF, embedded newline, BOM, `;` detection, ragged row error with line, empty/duplicate (case-insensitive) row name errors, `Damage:int` hint stripped and recorded, unknown suffix kept in the name; JSON array-of-objects and object-of-objects, type inference, `null`, array->`|`, nested object error, non-table JSON error; writers round trip both (CSV keeps delimiter/EOL; JSON numbers/bools stay typed); `findRow`/`findColumn` case-insensitivity; `validateTable` messages; `saveTable` atomic write to a temp dir |
| `LocGatherTest` | `tests/formats/src/LocGatherTest.cpp` | `gatherOcui` finds text/items/placeholder refs with line numbers; `gatherOcgraph` attribute refs; `gatherCsharp` finds `Loc.Text("UI","Play","Play")`, `new LocText("UI","Quit")`, ignores commented-out and non-literal args; `gatherTable` finds cell refs; `buildLocReport` (missing per culture, unused, conflict, untranslated); `applyGather` adds default-culture entries only, idempotent |
| `OcProjectLocaleTest` (in `OcStringsTest.cpp`) | same | `LOCALE.DEFAULT de` parses, `writeOcproject` round trips it, absent key stays empty and is not written |
| `FrameworkContentTest` | `tests/framework_content/src/FrameworkContentTest.cpp`, new `tests/framework_content/CMakeLists.txt`, `add_subdirectory(tests/framework_content)` after `tests/framework_timers` in root `CMakeLists.txt:528` | links `Aver.Framework Aver.Formats Aver.Core Aver.Platform`; temp-dir fixture. Loc: init counts cultures; resolve hit / chain (`pt-BR`->`pt`->default) / inline default / bracket fallback; length-only and truncated-buffer contract; `@@x` and non-reference return -1; `set_culture` return codes (-1 malformed, 0 no tables, 1) and epoch bump; lazy culture load; `get/has`; missing list unique and clearable; `reload_path` picks up an edited file, drops a deleted one, **keeps old data on a malformed file**; `.ocstrings` beats `.csv`; event `loc.culture_changed` posted (subscribe through `aver_fw_event_subscribe`, flush). Tables: load idempotent per path (relative via content root equals absolute), column/row find, `cell` out of range, version/epoch bump on reload, **failed reload keeps data**, `reload_path` across handles, JSON table, unload invalidates the handle |
| `GameLocalizationTest` | `tests/game/src/GameLocalizationTest.cpp`, target in `tests/game/CMakeLists.txt` (links `Aver.Runtime.Game.Core Aver.Framework Aver.Core`) | `bestCultureMatch` (exact, language-only, `pt-PT` wants -> `pt-BR` available, none); `initLocalization` precedence override > saved > system > default using a temp Settings.ini and fixture dir; `readSavedCulture=false` ignores the saved value; `onContentFilesChanged` reloads a changed string file and a changed table, and handles a deleted one |
| `UiLocalizeTest` | `tests/ui/src/UiLocalizeTest.cpp`, target in `tests/ui/CMakeLists.txt` (links `Aver.UI Aver.Core Aver.Platform`) | `shown()`: literal passthrough, `@@x`, malformed `@`, reference with resolver, with inline default and no resolver, bracket fallback; epoch change clears the cache (text flips without touching the widget); `UiEstimatedMetrics` layout width follows the resolved string (a longer German string widens an auto-sized button); a recording painter receives the resolved text for text, button, toggle, choice item, list rows, placeholder; `TextInput` typed `@x` is not resolved; events carry raw items; `.ocui` round trip keeps references byte-exact; `uiParseTextRef` agrees with `fmt::parseLocRef` on the shared case table (copy the table, do not link Formats); built-in settings screen shows English by default and German with a fake resolver |
| `UiLocalizeAbiTest` | `tests/ui/src/UiLocalizeAbiTest.cpp` (links `Aver.UI.Abi`) | `aver_ui_set_text_resolver` with a fake C function, `aver_ui_widget_get_shown_text`, retry on a small buffer, `aver_ui_text_width` uses the resolved string |
| `DataTableEditorTest` | `tests/editor/src/DataTableEditorTest.cpp` (compiles `sandbox/src/DataTableEditor.cpp`, pattern at `tests/editor/CMakeLists.txt:270-275`) | load csv/json; every edit op incl. refused duplicate row/column names; undo/redo; save round trip preserves delimiter and EOL; dirty tab keeps edits on `onFileChanged`, clean tab reloads; non-table JSON returns null from the factory |
| `StringTableEditorTest` | `tests/editor/src/StringTableEditorTest.cpp` | multi-culture grid model; add/rename/delete key cascades; missing cells; save merges (comment survives); Add culture; Gather/Report through `LocGatherRun` on a temp content tree |
| `Aver.DataLoc.Tests` | new console project `scripting/csharp/Aver.DataLoc.Tests` (house pattern: returns failure count; references `Aver.Framework`; natives not needed) | `DataTableBinder`: positional record, class with `init` props, struct, `[DataColumn]`/`[DataIgnore]`/`[DataRowName]`, enum by name and number, bool synonyms, `1.5f`, `Vec3`, `string[]`/`List<string>`, nullable empty cell, missing column default + problem, extra column info, bad int problem text names row and column, spaces/underscores column matching; `LocFormat.PluralCategory` table for en/fr/ru/pl/cs/ja; `Apply` with `{0:N1}` under `de` and `en`, bad template does not throw. Not wired into ctest (like `Aver.Graph.Tests`, invoke by hand) |
| `DataLocNodeTests` | `scripting/csharp/Aver.Graph.Tests/DataLocNodeTests.cs`, one `failures += DataLocNodeTests.RunAll();` in `Program.cs` | `GameSystemNodes.Validate()` clean for the 14 new rows; default pins; Exec rows refused by the pull compiler, Pure rows accepted; missing `table=`/`culture=` fails by name; emitted IL valid; a NODE line with `text="@UI:Play=Play"` keeps `=` and `@` |

**Manual verification (main loop, headed, after build)**: (1) a project with `Localization/en` + `Localization/de`, a UI layout using `@UI:...` references, Play with `--culture de` shows German including umlauts (font re-bake), the in-game language Choice switches at runtime and `AN_OnEvent loc.culture_changed` fires; (2) edit a `Data/*.csv` and a `.ocstrings` file externally while Play runs: values change within the watcher debounce, and a deliberately broken save keeps the old values and logs one warning; (3) stage the game (`aver_package`) and run the packaged exe with `--culture de`.

## 10. Files, partitioned

**Create (this feature owns):**
- Formats: `modules/formats/include/aver/formats/{OcStrings,OcTable,LocGather}.hpp`, `modules/formats/src/{OcStrings,OcTable,LocGather}.cpp`.
- Framework native: `modules/framework/include/aver/framework/{framework_loc_abi.h,framework_table_abi.h}`, `modules/framework/src/{LocalizationAbi,DataTableAbi}.cpp`.
- Runtime lib: `Runtime/include/aver/game/GameLocalization.hpp`, `Runtime/src/GameLocalization.cpp`.
- C#: `scripting/csharp/Aver.Framework/{Localization,LocNative,DataTables,DataTableBinder,DataTablesNative,DataContentGraph,LocGraph}.cs`; `scripting/csharp/Aver.DataLoc.Tests/{Aver.DataLoc.Tests.csproj,Program.cs}`.
- Editor: `sandbox/src/{DataTableEditor,StringTableEditor,LocGatherRun}.{hpp,cpp}`.
- Tests: the files in section 9 (`tests/formats/src/{OcStrings,OcTable,LocGather}Test.cpp`, `tests/framework_content/*`, `tests/game/src/GameLocalizationTest.cpp`, `tests/ui/src/{UiLocalize,UiLocalizeAbi}Test.cpp`, `tests/editor/src/{DataTableEditor,StringTableEditor}Test.cpp`, `scripting/csharp/Aver.Graph.Tests/DataLocNodeTests.cs`).
- Docs: `docs/DATA_TABLES.md`, `docs/LOCALIZATION.md` (user docs: formats, workflow, C# and graph usage).

**Modify (this feature owns the change, small and additive):**
- `modules/formats/CMakeLists.txt` (three sources), `modules/formats/include/aver/formats/OcProject.hpp` + `src/OcProject.cpp` (`LOCALE.DEFAULT` only).
- `modules/framework/CMakeLists.txt` (two sources). `framework_abi.h`/`FrameworkAbi.cpp` are **not** touched (the input wave-2 feature owns those).
- `modules/ui/include/aver/ui/UiTree.hpp`, `src/{UiTree,UiTreeLayout,UiTreeDraw,UiText,UiScreens}.cpp`; `modules/ui.abi/include/aver/ui/ui_widget_abi.h`, `src/{UiWidgetAbi,UiAbi}.cpp`, `src/UiAbiState.hpp`.
- `scripting/csharp/Aver.UI/{Native.cs,Widgets.cs}` (ShownText, declarations), `scripting/csharp/Aver.Framework/UiGameSettings.cs` (language item, reference labels).
- `Runtime/CMakeLists.txt`, `Runtime/include/aver/game/GameApp.hpp` + `Runtime/src/GameApp.cpp` (init call, `--culture`, settings-path helper).
- `sandbox/CMakeLists.txt` (sources), `sandbox/src/{SandboxApp.cpp,SandboxApp.hpp}` (two factory lines + includes + two `cbCreate*` decls), `SandboxContentBrowser.cpp`, `SandboxProject.cpp`, `SandboxPlay.cpp` (Play-start re-init block only), `SandboxAutosave.cpp` (the watch hook only), `SandboxSettings.cpp` (the Localization block only), `SandboxMain.cpp` (`--culture`), `UiLayoutEditor.{hpp,cpp}`.
- `scripts/bake-font.py`, `assets/fonts/Roboto-Regular.{ocfont,png}`.
- Root `CMakeLists.txt` (one `add_subdirectory(tests/framework_content)`), `tests/{formats,ui,game,editor}/CMakeLists.txt`, `docs/{GAME_UI,PROJECTS,ABI}.md`.

**Shared with other wave-2 features (the other side owns the file; this feature supplies text):**
- `scripting/csharp/Aver.Graph/GraphGameSystemNodeTable.cs`, `sandbox/src/GraphNodeDefsGameSystems.hpp`, `docs/AVER_NODE_NODES.md`: the graph-node agent applies section 8 verbatim.
- `scripting/csharp/Aver.Scripting.Bridge/HostBridge.cs`: this feature adds exactly one line in `DispTickAll` after the UI pump (:1469); the input feature owns `ConfigureInput` (:401); other features that add pump lines there sit on adjacent lines and merge trivially. Nothing else in the file is touched.
- `scripting/csharp/Aver.Framework/Aver.Framework.csproj`: not edited (SDK globbing picks up new files).
- `SandboxApp.cpp` factory list and `SandboxContentBrowser.cpp` menu/icon tables are append-only registries every wave-2 editor touches; append, do not reorder.

**Not touched:** `modules/anim*`, `Aver.Framework` ability files (GAS), `EnhancedInput.cs`, `InputScheme.cs`, `framework_abi.h` input section, `cmake/modules.catalog.json` (no manifest changes), `scripts/*.allowlist`, `scripts/stage-game.ps1`.

## 11. Risks and open questions for the owner

1. **Scripts beyond Latin/Greek/Cyrillic.** The UI font is a baked bitmap with no fallback, shaping or bidi (`GAME_UI.md`, "What is not done"). v1 covers accented Latin, Greek, Cyrillic. CJK needs a licensed font and per-culture font swap (a `FONT` record per culture folder is the obvious shape); Arabic/Hebrew/Indic need shaping and bidi. Decide the target cultures. The editor's own ImGui font has no Cyrillic/Greek ranges, so the editor preview of those shows boxes while the game is right.
2. **`@` as the reference marker.** Any literal string starting with `@Table:Key` is now a reference; `@@` escapes it. Acceptable, or prefer an explicit `text.key` property in `.ocui`? (The `@` rule also gives graph attributes, data cells and C# strings one mechanism, which is why it was chosen.)
3. **Runtime now links `Aver.Settings` natively** for the saved culture; the DLL was managed-only and is already staged. Reopening the same path later from `ConfigureInput` is harmless (nothing pending).
4. **Existing UI tests** read raw widget text for the built-in screens; they must switch to `shown()`. Behaviour with no resolver is unchanged (a reference shows its inline default).
5. **Plurals** are categories only (no gender, no ICU MessageFormat, no ordinals). Enough for item counts.
6. **Row names are ASCII case-insensitive** (UE FName parity): `Sword` and `sword` collide. Tell me if case-sensitive is wanted.
7. **Graphs cannot carry strings**, so dynamic row lookup is by int column (`AN_FindDataTableRowByInt`) and row names are static attributes. A string pin type is a graph-language change, out of scope here.
8. **One wide CSV per table (all cultures as columns)** is a common translator workflow and is not supported (one file per culture per table); the string-table editor grid is the in-engine equivalent. Add an importer later if wanted.
9. **No Windows-only assumption in the design** except `systemUiCulture()` (`GetUserDefaultLocaleName`); everything else is portable, matching the D3D12/Vulkan parity rule (no GPU code at all).
