// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// DataTableBinder: record shapes, attributes, member types, problems.

using Aver.Framework;
using Aver.Scene;

public enum Rarity { Common, Rare, Epic }

public record ItemRecord(string Name, int Damage, float Weight);

public class ItemClass
{
    public int Damage { get; init; }
    public string DisplayName { get; set; } = "";
    [DataRowName] public string Id { get; set; } = "";
    [DataIgnore] public int Skipped { get; set; } = 7;
    [DataColumn("Tint Color")] public Vec3 Tint { get; set; }
}

public struct ItemStruct
{
    public int Damage;
    public bool Stackable;
    public Rarity Rarity;
}

public class ItemLists
{
    public string[] Tags { get; set; } = Array.Empty<string>();
    public List<string> Names { get; set; } = new();
    public IReadOnlyList<int> Numbers { get; set; } = Array.Empty<int>();
    public int? Optional { get; set; }
    public float Ratio { get; set; }
    public LocText Label { get; set; }
    public string Shown { get; set; } = "";
}

public class WithBytes
{
    public byte Small { get; set; }
}

static class BinderTests
{
    public static void Run()
    {
        PositionalRecord();
        ClassWithAttributes();
        StructAndEnum();
        Lists();
        Problems();
        Matching();
    }

    private static void PositionalRecord()
    {
        var t = new FakeTable("Damage:int", "Weight:float").Row("sword", "25", "1.5").Row("bow", "12", "0.75f");
        var problems = new List<string>();
        ItemRecord[] rows = DataTableBinder.Bind<ItemRecord>(t, problems);
        Program.Check(rows.Length == 2 && rows[0].Damage == 25 && rows[0].Weight == 1.5f && rows[1].Weight == 0.75f, "a positional record binds by parameter name, 1.5 and 0.75f parse");
        Program.Check(problems.TrueForAll(p => p.StartsWith("info:") || p.Contains("'Name'")), "...with no problems apart from the unbound Name parameter");
        Program.Check(string.IsNullOrEmpty(rows[0].Name), "a Name parameter with no Name column stays default (the row name needs [DataRowName])");
    }

    private static void ClassWithAttributes()
    {
        var t = new FakeTable("Damage", "Display Name", "Tint Color", "Extra")
            .Row("sword", "30", "A sword", "1 0.5 0.25", "x");
        var problems = new List<string>();
        ItemClass[] rows = DataTableBinder.Bind<ItemClass>(t, problems);
        ItemClass c = rows[0];
        Program.Check(c.Damage == 30, "init-only properties bind");
        Program.Check(c.DisplayName == "A sword", "'Display Name' matches DisplayName (spaces ignored)");
        Program.Check(c.Id == "sword", "[DataRowName] receives the row name");
        Program.Check(c.Skipped == 7, "[DataIgnore] leaves the member alone");
        Program.Check(c.Tint.X == 1f && c.Tint.Y == 0.5f && c.Tint.Z == 0.25f, "[DataColumn] and a Vec3 cell");
        Program.Check(problems.Exists(p => p.StartsWith("info:") && p.Contains("Extra")), "an extra column adds an info problem");
    }

    private static void StructAndEnum()
    {
        var t = new FakeTable("Damage", "Stackable", "Rarity")
            .Row("a", "1", "yes", "Rare").Row("b", "2", "0", "2").Row("c", "3", "TRUE", "epic").Row("d", "4", "no", "");
        var problems = new List<string>();
        ItemStruct[] rows = DataTableBinder.Bind<ItemStruct>(t, problems);
        Program.Check(rows[0].Stackable && rows[0].Rarity == Rarity.Rare, "bool synonym 'yes' and an enum by name");
        Program.Check(!rows[1].Stackable && rows[1].Rarity == Rarity.Epic, "bool '0' and an enum by number");
        Program.Check(rows[2].Stackable && rows[2].Rarity == Rarity.Epic, "bool 'TRUE' and an enum name ignoring case");
        Program.Check(!rows[3].Stackable && rows[3].Rarity == Rarity.Common, "an empty enum cell keeps the default");
        Program.Check(rows[3].Damage == 4 && problems.Count == 0, "a struct binds with no problems");
    }

    private static void Lists()
    {
        var t = new FakeTable("Tags", "Names", "Numbers", "Optional", "Ratio", "Label", "Shown:text")
            .Row("a", "melee|iron", "x|y|z", "1|2|3", "5", "1.5f", "@UI:name=Sword", "@UI:shown=Hello")
            .Row("b", "", "", "", "", "", "", "");
        var problems = new List<string>();
        ItemLists[] rows = DataTableBinder.Bind<ItemLists>(t, problems);
        ItemLists a = rows[0], b = rows[1];
        Program.Check(a.Tags.Length == 2 && a.Tags[1] == "iron", "string[] splits on '|'");
        Program.Check(a.Names.Count == 3 && a.Names[2] == "z", "List<string>");
        Program.Check(a.Numbers.Count == 3 && a.Numbers[1] == 2, "IReadOnlyList<int>");
        Program.Check(a.Optional == 5 && a.Ratio == 1.5f, "nullable and a float with an f suffix");
        Program.Check(a.Label.Table == "UI" && a.Label.Key == "name" && a.Label.Default == "Sword", "LocText stays a lazy reference");
        Program.Check(a.Shown == "Hello", "a text column bound to string is resolved at bind time (inline default without natives)");
        Program.Check(b.Tags.Length == 0 && b.Names.Count == 0 && b.Optional == null && b.Ratio == 0f, "empty cells: empty lists, null, zero");
    }

    private static void Problems()
    {
        var t = new FakeTable("Damage", "Weight").Row("sword", "abc", "1").Row("bow", "12", "2");
        var problems = new List<string>();
        ItemRecord[] rows = DataTableBinder.Bind<ItemRecord>(t, problems);
        Program.Check(rows[0].Damage == 0 && rows[1].Damage == 12, "a bad cell leaves the default and the rest of the table binds");
        Program.Check(problems.Contains("row 'sword' column 'Damage': 'abc' is not an int"), "the problem names row, column and text");

        problems.Clear();
        ItemRecord[] none = DataTableBinder.Bind<ItemRecord>(new FakeTable("Weight").Row("a", "1"), problems);
        Program.Check(none[0].Damage == 0 && none[0].Weight == 1f, "a missing column leaves the default");
        Program.Check(problems.Exists(p => p.Contains("Damage") && p.Contains("missing")), "...and says so once");

        problems.Clear();
        WithBytes[] big = DataTableBinder.Bind<WithBytes>(new FakeTable("Small").Row("a", "300"), problems);
        Program.Check(big[0].Small == 0 && problems.Exists(p => p.Contains("byte")), "an out-of-range integer is reported");
    }

    private static void Matching()
    {
        var t = new FakeTable("display_name", "DAMAGE").Row("a", "n", "4");
        var problems = new List<string>();
        ItemClass[] rows = DataTableBinder.Bind<ItemClass>(t, problems);
        Program.Check(rows[0].DisplayName == "n" && rows[0].Damage == 4, "column match ignores case, spaces and underscores");
    }
}
