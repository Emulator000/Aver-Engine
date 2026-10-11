// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Test runner for the data-table binder and localization formatting. Returns the failure count.

using Aver.Framework;

static class Program
{
    private static int s_failures;

    internal static void Check(bool ok, string what)
    {
        if (ok) { Console.WriteLine("  ok    " + what); return; }
        s_failures++;
        Console.WriteLine("  FAIL  " + what);
    }

    static int Main()
    {
        Console.WriteLine("=== binder ===");
        BinderTests.Run();
        Console.WriteLine("=== plural and format ===");
        FormatTests.Run();
        Console.WriteLine("=== text references ===");
        ReferenceTests.Run();

        Console.WriteLine(s_failures == 0 ? "\nAll tests passed." : $"\n{s_failures} test(s) failed.");
        return s_failures;
    }
}

/// <summary>An in-memory table: the binder never needs the native store.</summary>
sealed class FakeTable : IRawTable
{
    private readonly string[] _cols;
    private readonly DataColumnType[] _types;
    private readonly List<(string Name, string[] Cells)> _rows = new();

    public FakeTable(params string[] columns)
    {
        // "Name:type" sets the declared type like a CSV header does.
        _cols = new string[columns.Length];
        _types = new DataColumnType[columns.Length];
        for (int i = 0; i < columns.Length; i++)
        {
            int colon = columns[i].IndexOf(':');
            _cols[i] = colon < 0 ? columns[i] : columns[i].Substring(0, colon);
            if (colon >= 0) Enum.TryParse(columns[i].Substring(colon + 1), true, out _types[i]);
        }
    }

    public FakeTable Row(string name, params string[] cells) { _rows.Add((name, cells)); return this; }

    public int ColumnCount => _cols.Length;
    public string ColumnName(int c) => _cols[c];
    public DataColumnType ColumnType(int c) => _types[c];
    public int RowCount => _rows.Count;
    public string RowName(int r) => _rows[r].Name;
    public string Cell(int r, int c) => r < _rows.Count && c < _rows[r].Cells.Length ? _rows[r].Cells[c] : string.Empty;
}
