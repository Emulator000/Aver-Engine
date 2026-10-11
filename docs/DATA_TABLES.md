# Data tables

A `.csv` or table-shaped `.json` file read as named rows of named columns and bound to C# records. No new on-disk format. Design: [plans/wave2/DATA_TABLES_LOCALIZATION.md](plans/wave2/DATA_TABLES_LOCALIZATION.md).

## CSV

RFC 4180 (quotes, doubled quotes, embedded newlines, CRLF or LF, UTF-8 BOM). The delimiter is `,`, or `;` when the header has no `,` but has `;` (Excel in many locales); the editor keeps the delimiter and line ending on save. Row 1 is the headers; **column 1 is the row name** (empty or duplicate names, ASCII case-insensitively, are errors). A header may carry a type hint `Name:type`, with type one of `string int float bool vec3 text strings`; any other suffix is part of the name. Rows must have exactly as many cells as the header.

```
Name,Damage:int,Weight:float,Stackable:bool,Display Name:text,Tags:strings,Tint:vec3
sword,25,1.5,false,@Items:sword.name=Sword,melee|iron,"1 0.8 0.2"
```

Cells are stored as text; the type is advisory (validation, editor widgets, the JSON writer). bool is `true/false/1/0/yes/no`, vec3 is three numbers separated by space, `,`, `;` or `|`, strings is `|`-separated, text is a [localization reference](LOCALIZATION.md) or literal.

## JSON

An array of objects `[{"Name":"sword","Damage":25}]` (row name from `Name`, `RowName` or `name`) or an object of objects `{"sword":{"Damage":25}}`. Columns are the union of members. Nested objects are an error ("flatten it"). Column types are inferred from the first non-null value, so numbers, bools and arrays are written back as JSON values. A JSON that is not table-shaped is not offered to the Data Table editor.

## C#

```csharp
public record Item(string Name, int Damage, float Weight);       // positional: parameters match columns
public class Gear { [DataRowName] public string Id; public int Damage { get; init; }
                    [DataColumn("Tint Color")] public Vec3 Tint; public LocText Label; [DataIgnore] public int Cache; }

DataTable<Item> items = DataTables.Load<Item>("Data/items.csv");   // relative to the content root
Item sword = items.GetRow("sword");                                // TryGetRow / Rows / RowNames; names case-insensitive
foreach (string p in items.Problems) ...                           // "row 'sword' column 'Damage': 'abc' is not an int"
items.Reloaded += t => ...;                                        // after a hot reload
DataTable raw = DataTables.Open("Data/items.csv");                 // untyped: FindRow, FindColumn, GetInt, GetFloat, ...
```

Binding: public settable properties (including `init`) and public fields; columns match members ignoring case, spaces and underscores; a public parameterless constructor is used if there is one, else the single public constructor by parameter name. Member types: `string`, `bool`, integers, `float`, `double`, enums (name or number), `Vec3`, `LocText`, arrays / `List` / `IReadOnlyList` of those scalars (split on `|`), `Nullable` of the scalars (empty cell is null). Parsing is invariant-culture (`1.5`, `1.5f`). A missing column keeps the default and is reported once; a bad cell keeps the default and is reported with row, column and text. A `text` column bound to `string` is resolved at bind time and re-bound on a culture change; bound to `LocText` it stays lazy.

A missing or unreadable file gives an empty table that fills in when the file appears. A reload that fails to parse keeps the old data. Only the editor watches files; a built game loads once.

## Graph nodes

`AN_FindDataTableRow`, `AN_FindDataTableRowByInt`, `AN_DataTableRowCount`, `AN_GetDataTableInt/Float/Bool/Vec3`, `AN_SetUiTextFromTable`. The table, row and column are node attributes (graph pins carry no strings); the row travels as an int. A failed lookup is `found/success = false` with zeroed outputs.

## Editor

The Data Table tab (double-click a `.csv` or table-shaped `.json`; New Data Table in the Content Browser) is a grid with per-type widgets (checkbox, int, float, vec3, text with a resolved preview under `text` cells), invalid cells tinted with the reason, filter, add / duplicate / delete / rename row, add / delete / rename column and a column type box. A tab with unsaved edits keeps them when the file changes on disk and offers a reload.
