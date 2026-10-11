// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// What the data-table graph nodes (AN_FindDataTableRow, AN_GetDataTableInt, ...) call. Graph pins carry no strings,
// so the table path, row and column names are node attributes; the row travels as an int. See docs/DATA_TABLES.md.

using Aver.UI;

namespace Aver.Framework;

/// <summary>The data-table graph nodes' entry points. Reads never throw: a missing table, column, row or an
/// unparsable cell is a failed lookup (the bool result is false and the outs are zero).</summary>
public static class DataContentGraph
{
    /// <summary>AN_FindDataTableRow: the index of the row called <paramref name="row"/> (ASCII case-insensitive).</summary>
    public static bool FindRowForGraph(string table, string row, out int rowIndex)
    {
        rowIndex = DataTables.Open(table).FindRow(row);
        if (rowIndex >= 0) return true;
        rowIndex = 0;
        return false;
    }

    /// <summary>AN_FindDataTableRowByInt: the first row whose <paramref name="column"/> holds <paramref name="value"/>.</summary>
    public static bool FindRowByIntForGraph(string table, string column, int value, out int rowIndex)
    {
        rowIndex = 0;
        DataTable t = DataTables.Open(table);
        int c = t.FindColumn(column);
        if (c < 0) return false;
        int n = t.RowCount;
        for (int r = 0; r < n; r++)
        {
            if (!DataCell.TryLong(t.Cell(r, c), out long v) || v != value) continue;
            rowIndex = r;
            return true;
        }
        return false;
    }

    /// <summary>AN_DataTableRowCount.</summary>
    public static int RowCountForGraph(string table) => DataTables.Open(table).RowCount;

    /// <summary>AN_GetDataTableInt.</summary>
    public static bool GetIntForGraph(string table, string column, int row, out int value)
    {
        value = 0;
        if (!Cell(table, column, row, out string cell) || !DataCell.TryLong(cell, out long v) || v < int.MinValue || v > int.MaxValue) return false;
        value = (int)v;
        return true;
    }

    /// <summary>AN_GetDataTableFloat.</summary>
    public static bool GetFloatForGraph(string table, string column, int row, out float value)
    {
        value = 0f;
        if (!Cell(table, column, row, out string cell) || !DataCell.TryDouble(cell, out double v)) return false;
        value = (float)v;
        return true;
    }

    /// <summary>AN_GetDataTableBool.</summary>
    public static bool GetBoolForGraph(string table, string column, int row, out bool value)
    {
        value = false;
        return Cell(table, column, row, out string cell) && DataCell.TryBool(cell, out value);
    }

    /// <summary>AN_GetDataTableVec3.</summary>
    public static bool GetVec3ForGraph(string table, string column, int row, out float x, out float y, out float z)
    {
        x = y = z = 0f;
        if (!Cell(table, column, row, out string cell) || !DataCell.TryVec3(cell, out Aver.Scene.Vec3 v)) return false;
        x = v.X; y = v.Y; z = v.Z;
        return true;
    }

    /// <summary>AN_SetUiTextFromTable: the widget's text becomes the cell (a text reference in it still resolves in the UI).</summary>
    public static bool SetUiTextForGraph(int widget, string table, string column, int row)
    {
        if (!Cell(table, column, row, out string cell)) return false;
        try { return UiGraphNodes.SetText(widget, cell); }
        catch (DllNotFoundException) { return false; }
    }

    private static bool Cell(string table, string column, int row, out string cell)
    {
        cell = string.Empty;
        DataTable t = DataTables.Open(table);
        int c = t.FindColumn(column);
        if (c < 0 || row < 0 || row >= t.RowCount) return false;
        cell = t.Cell(row, c);
        return true;
    }
}
