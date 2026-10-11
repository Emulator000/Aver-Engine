// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for the data table and localization nodes (docs/plans/wave2/DATA_TABLES_LOCALIZATION.md
// section 8): 8 data rows (DataContentGraph) and 6 culture rows (LocGraph). Bare-process scope like
// GameSystemNodeTests: the wiring the table owns; table parsing and culture fallback are covered by
// Aver.DataLoc.Tests and the native tests.
using System;
using System.Linq;
using Aver.Graph;
using Row = NodeRowKit.Row;

static class DataLocNodeTests
{
    private static readonly Row[] Rows =
    {
        new("an_finddatatablerow", GenericKind.Pure, new[] { "row:int:out", "found:bool:out" }),
        new("an_finddatatablerowbyint", GenericKind.Pure, new[] { "value:int:in", "row:int:out", "found:bool:out" }),
        new("an_datatablerowcount", GenericKind.Pure, new[] { "count:int:out" }),
        new("an_getdatatableint", GenericKind.Pure, new[] { "row:int:in", "value:int:out", "success:bool:out" }),
        new("an_getdatatablefloat", GenericKind.Pure, new[] { "row:int:in", "value:float:out", "success:bool:out" }),
        new("an_getdatatablebool", GenericKind.Pure, new[] { "row:int:in", "value:bool:out", "success:bool:out" }),
        new("an_getdatatablevec3", GenericKind.Pure, new[] { "row:int:in", "x:float:out", "y:float:out", "z:float:out", "success:bool:out" }),
        new("an_setuitextfromtable", GenericKind.Exec, new[] { "exec:exec:in", "widget:int:in", "row:int:in", "then:exec:out", "success:bool:out" }),
        new("an_setculture", GenericKind.Exec, new[] { "exec:exec:in", "then:exec:out", "success:bool:out" }),
        new("an_isculture", GenericKind.Pure, new[] { "matches:bool:out" }),
        new("an_culturecount", GenericKind.Pure, new[] { "count:int:out" }),
        new("an_currentcultureindex", GenericKind.Pure, new[] { "index:int:out" }),
        new("an_setculturebyindex", GenericKind.Exec, new[] { "exec:exec:in", "index:int:in", "then:exec:out", "success:bool:out" }),
        new("an_setuiitemstocultures", GenericKind.Exec, new[] { "exec:exec:in", "widget:int:in", "then:exec:out", "success:bool:out" }),
    };

    public static int RunAll()
    {
        int failures = 0;
        string[] types = Rows.Select(r => r.Type).ToArray();
        failures += NodeRowKit.CheckRows("data table and localization", Rows);
        failures += NodeRowKit.CheckDefaultPins("data table and localization", Rows);
        failures += NodeRowKit.CheckIlValid("data table and localization", types);
        failures += NodeRowKit.CheckCompilerSplit("data table and localization", types);
        failures += TestMissingAttributeFailsByName();
        failures += TestNodeLineKeepsEqualsAndAt();
        failures += TestTableLookupChain();
        failures += TestCultureChangedEntry();
        return failures;
    }

    private static int Fail(string msg) => NodeRowKit.Fail(msg);

    // Every row names its table, column, row or culture in an attribute, and none of them may be guessed.
    private static int TestMissingAttributeFailsByName()
    {
        Console.WriteLine("Test: a data or culture node missing its attribute fails to compile, naming it");
        int f = 0;
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_FindDataTableRow", "table=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_FindDataTableRow table=Items.csv", "row=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_FindDataTableRowByInt table=Items.csv", "column=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_DataTableRowCount", "table=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GetDataTableInt", "table=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GetDataTableFloat table=Items.csv", "column=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GetDataTableBool table=Items.csv", "column=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GetDataTableVec3 table=Items.csv", "column=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_SetUiTextFromTable", "table=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_SetUiTextFromTable table=Items.csv", "column=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_SetCulture", "culture=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_IsCulture", "culture=", exec: false);
        // The index-based culture nodes take only pins.
        f += NodeRowKit.CheckOptionalAttribute("NODE n AN_CultureCount", exec: false);
        f += NodeRowKit.CheckOptionalAttribute("NODE n AN_CurrentCultureIndex", exec: false);
        f += NodeRowKit.CheckOptionalAttribute("NODE n AN_SetCultureByIndex", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n AN_SetUiItemsToCultures", exec: true);
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // Localized text reaches the screen as a reference in an attribute (text=@UI:Key=Default), so the
    // NODE-line parser must keep '=' and '@' inside the value. It splits on whitespace only and cuts at
    // the FIRST '=', and it does not strip quotes: a quoted value would keep its quote marks.
    private static int TestNodeLineKeepsEqualsAndAt()
    {
        Console.WriteLine("Test: a NODE line keeps '=' and '@' inside an attribute value (text=@UI:Play=Play)");
        const string text = "OCGRAPH 1\nNODE s AN_SetUiText text=@UI:hud.ammo=Ammo\nNODE t AN_FindDataTableRow table=Content/Data/Items.csv row=Iron_Sword\n";
        if (!OcGraphParser.Parse(text, out var g, out var err)) return Fail("parse: " + err);
        if (g.Nodes["s"].Attrs["text"] != "@UI:hud.ammo=Ammo") return Fail("text= became '" + g.Nodes["s"].Attrs["text"] + "'");
        if (g.Nodes["t"].Attrs["table"] != "Content/Data/Items.csv" || g.Nodes["t"].Attrs["row"] != "Iron_Sword")
            return Fail("table= or row= did not survive");
        Console.WriteLine("  PASS");
        return 0;
    }

    // Find a row by name, read two columns of it, and show them: the usual data-driven pickup.
    private static int TestTableLookupChain()
    {
        Console.WriteLine("Test: Find Data Table Row feeds Get Data Table Float/Int and Set UI Text From Table");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE f AN_FindDataTableRow table=Content/Data/Items.csv row=Sword\n" +
                            "NODE d AN_GetDataTableFloat table=Content/Data/Items.csv column=Damage\n" +
                            "NODE c AN_GetDataTableInt table=Content/Data/Items.csv column=Cost\n" +
                            "NODE ui AN_SetUiTextFromTable table=Content/Data/Items.csv column=Name\nNODE pr Print\nNODE pi PrintInt\n" +
                            "LINK tick.exec ui.exec\nLINK f.row ui.row\nLINK f.row d.row\nLINK f.row c.row\n" +
                            "LINK ui.then pr.exec\nLINK d.value pr.value\nLINK pr.then pi.exec\nLINK c.value pi.value\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }

    // A language change is the bus event loc.culture_changed, heard by the existing AN_OnEvent.
    private static int TestCultureChangedEntry()
    {
        Console.WriteLine("Test: AN_OnEvent loc.culture_changed + Is Culture gates a Set UI Text From Table");
        const string text = "OCGRAPH 1\nNODE e AN_OnEvent\nNODE c AN_IsCulture culture=fr\nNODE b Branch\n" +
                            "NODE t AN_SetUiTextFromTable table=Content/Data/Items.csv column=Name\n" +
                            "LINK e.exec b.exec\nLINK c.matches b.cond\nLINK b.true t.exec\nENTRY e loc.culture_changed\n";
        if (!OcGraphParser.Parse(text, out var g, out var err)) return Fail("parse: " + err);
        if (!NodeRowKit.HasEntry(g, "loc.culture_changed")) return Fail("the ENTRY record was not kept");
        string? bad = NodeRowKit.CompileAndRun(text, "loc.culture_changed");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }
}
