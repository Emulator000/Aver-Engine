// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Data tables: .csv / .json files as named rows bound to C# records. See docs/DATA_TABLES.md.

using System.Collections;
using Aver.Scene;
using Aver.Scripting;

namespace Aver.Framework;

/// <summary>An untyped view of a loaded data table (framework_table_abi.h). Cells are text; the getters convert.
/// A table whose file is missing or unreadable is empty and fills in when the file appears. This is what the
/// graph nodes use; scripts usually want <see cref="DataTable{T}"/>.</summary>
public sealed class DataTable : IRawTable
{
    private readonly string _path;
    private int _handle;
    private long _retryAt;
    private bool _warned;

    internal DataTable(string absolutePath) => _path = absolutePath;

    /// <summary>The absolute path of the file.</summary>
    public string Path => _path;

    /// <summary>Starts at 1 and rises on every successful reload; 0 while the file is not loaded.</summary>
    public int Version => Ensure() ? FwTable.aver_fw_table_version(_handle) : 0;

    /// <summary>True when the file is loaded.</summary>
    public bool IsLoaded => Ensure();

    /// <inheritdoc/>
    public int RowCount => Ensure() ? Math.Max(0, FwTable.aver_fw_table_row_count(_handle)) : 0;
    /// <inheritdoc/>
    public int ColumnCount => Ensure() ? Math.Max(0, FwTable.aver_fw_table_column_count(_handle)) : 0;

    /// <summary>The row called <paramref name="name"/> (ASCII case-insensitive), -1 if none.</summary>
    public int FindRow(string name) => Ensure() ? FwTable.aver_fw_table_row_find(_handle, name) : -1;
    /// <summary>The column called <paramref name="name"/> (case-insensitive, spaces and underscores ignored), -1 if none.</summary>
    public int FindColumn(string name) => Ensure() ? FwTable.aver_fw_table_column_find(_handle, name) : -1;

    /// <inheritdoc/>
    public string RowName(int r) => Ensure() ? FwTable.Str(FwTable.aver_fw_table_row_name(_handle, r)) : string.Empty;
    /// <inheritdoc/>
    public string ColumnName(int c) => Ensure() ? FwTable.Str(FwTable.aver_fw_table_column_name(_handle, c)) : string.Empty;
    /// <inheritdoc/>
    public DataColumnType ColumnType(int c)
    {
        int t = Ensure() ? FwTable.aver_fw_table_column_type(_handle, c) : 0;
        return t >= 0 && t <= (int)DataColumnType.Strings ? (DataColumnType)t : DataColumnType.String;
    }
    /// <inheritdoc/>
    public string Cell(int r, int c) => Ensure() ? FwTable.Str(FwTable.aver_fw_table_cell(_handle, r, c)) : string.Empty;

    /// <summary>The cell as text, or <paramref name="fallback"/> when out of range.</summary>
    public string GetString(int r, int c, string fallback = "") => InRange(r, c) ? Cell(r, c) : fallback;

    /// <summary>The cell as an int, or <paramref name="fallback"/> when out of range, empty or not a number.</summary>
    public int GetInt(int r, int c, int fallback = 0)
        => InRange(r, c) && DataCell.TryLong(Cell(r, c), out long v) && v >= int.MinValue && v <= int.MaxValue ? (int)v : fallback;

    /// <summary>The cell as a float (invariant culture, a trailing "f" allowed), or <paramref name="fallback"/>.</summary>
    public float GetFloat(int r, int c, float fallback = 0)
        => InRange(r, c) && DataCell.TryDouble(Cell(r, c), out double v) ? (float)v : fallback;

    /// <summary>The cell as a bool (true/false, yes/no, 1/0), or <paramref name="fallback"/>.</summary>
    public bool GetBool(int r, int c, bool fallback = false)
        => InRange(r, c) && DataCell.TryBool(Cell(r, c), out bool v) ? v : fallback;

    /// <summary>The cell as three numbers separated by space, comma, semicolon or bar.</summary>
    public bool TryGetVec3(int r, int c, out Vec3 v)
    {
        v = default;
        return InRange(r, c) && DataCell.TryVec3(Cell(r, c), out v);
    }

    private bool InRange(int r, int c) => r >= 0 && c >= 0 && r < RowCount && c < ColumnCount;

    // Loads on first use, and again (at most once a second) while the file is missing.
    internal bool Ensure()
    {
        if (_handle > 0) return true;
        long now = Environment.TickCount64;
        if (now < _retryAt) return false;
        _retryAt = now + 1000;
        try
        {
            _handle = FwTable.aver_fw_table_load(_path);
            if (_handle <= 0 && !_warned)
            {
                _warned = true;
                Log.Warn("[Table] " + FwTable.Str(FwTable.aver_fw_table_error()));
            }
        }
        catch (Exception e) when (e is DllNotFoundException or BadImageFormatException)
        {
            _handle = 0;
            _retryAt = long.MaxValue;
            if (!_warned) { _warned = true; Log.Warn("[Table] the native framework library is not loaded; '" + _path + "' stays empty"); }
        }
        return _handle > 0;
    }

    internal bool Missing => _handle <= 0;
}

/// <summary>A data table bound to <typeparamref name="T"/> rows (see <see cref="DataTableBinder"/> for the binding rules).
/// Rebuilt when the file reloads or, for tables with <c>text</c> columns, when the culture changes.</summary>
public sealed class DataTable<T> : IReadOnlyCollection<KeyValuePair<string, T>>, ITableRefresh
{
    private readonly DataTable _raw;
    private T[] _rows = Array.Empty<T>();
    private string[] _names = Array.Empty<string>();
    private readonly Dictionary<string, int> _index = new(StringComparer.OrdinalIgnoreCase);
    private List<string> _problems = new();
    private int _builtVersion = -1;
    private int _builtLocEpoch = -1;
    private bool _hasText;

    internal DataTable(DataTable raw) => _raw = raw;

    /// <summary>The absolute path of the file.</summary>
    public string Path => _raw.Path;
    /// <summary>The file's version as of the last build; 0 while it is not loaded.</summary>
    public int Version => Math.Max(0, _builtVersion);
    /// <summary>The number of rows.</summary>
    public int Count => _rows.Length;
    /// <summary>What went wrong while binding: "row 'sword' column 'Damage': 'abc' is not an int". "info:" lines are notes.</summary>
    public IReadOnlyList<string> Problems => _problems;
    /// <summary>The row names in file order.</summary>
    public IEnumerable<string> RowNames => _names;
    /// <summary>The rows in file order.</summary>
    public IEnumerable<T> Rows => _rows;

    /// <summary>The row called <paramref name="name"/> (ASCII case-insensitive).</summary>
    public bool TryGetRow(string name, out T row)
    {
        if (_index.TryGetValue(name, out int i)) { row = _rows[i]; return true; }
        row = default!;
        return false;
    }

    /// <summary>The row called <paramref name="name"/>; throws <see cref="KeyNotFoundException"/> naming the table and row.</summary>
    public T GetRow(string name)
        => TryGetRow(name, out T row) ? row : throw new KeyNotFoundException($"data table '{Path}' has no row '{name}'");

    /// <summary>Raised from <see cref="DataTables.Pump"/> after the table was rebuilt (file reload or culture change).</summary>
    public event Action<DataTable<T>>? Reloaded;

    /// <inheritdoc/>
    public IEnumerator<KeyValuePair<string, T>> GetEnumerator()
    {
        for (int i = 0; i < _rows.Length; i++) yield return new KeyValuePair<string, T>(_names[i], _rows[i]);
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    // ---- ITableRefresh ----------------------------------------------------------------------

    bool ITableRefresh.Missing => _raw.Missing;

    internal void Build()
    {
        if (!_raw.Ensure()) return;
        _builtVersion = _raw.Version;
        _builtLocEpoch = Loc.Epoch;
        int cols = _raw.ColumnCount;
        _hasText = false;
        for (int c = 0; c < cols; c++) if (_raw.ColumnType(c) == DataColumnType.Text) _hasText = true;

        var problems = new List<string>();
        try { _rows = DataTableBinder.Bind<T>(_raw, problems); }
        catch (Exception e) when (e is InvalidOperationException or ArgumentException or MissingMethodException or System.Reflection.TargetInvocationException)
        {
            problems.Add("cannot bind " + typeof(T).Name + ": " + e.Message);
            _rows = Array.Empty<T>();
        }
        _names = new string[_rows.Length];
        _index.Clear();
        for (int r = 0; r < _rows.Length; r++)
        {
            _names[r] = _raw.RowName(r);
            _index[_names[r]] = r;
        }
        _problems = problems;
        foreach (string p in problems)
            if (!p.StartsWith("info:", StringComparison.Ordinal)) Log.Warn("[Table] " + Path + ": " + p);
    }

    void ITableRefresh.Refresh()
    {
        if (!_raw.Ensure()) return;
        if (_raw.Version == _builtVersion && !(_hasText && Loc.Epoch != _builtLocEpoch)) return;
        Build();
        Reloaded?.Invoke(this);
    }
}

/// <summary>The rebuild hook <see cref="DataTables.Pump"/> drives.</summary>
internal interface ITableRefresh
{
    bool Missing { get; }
    void Refresh();
}

/// <summary>Loads and caches data tables. Paths are absolute or relative to the project's content root.</summary>
public static class DataTables
{
    private static readonly Dictionary<string, DataTable> s_raw = new(StringComparer.OrdinalIgnoreCase);
    private static readonly Dictionary<string, object> s_typed = new(StringComparer.OrdinalIgnoreCase);
    private static readonly List<ITableRefresh> s_refresh = new();
    private static int s_tableEpoch = -1, s_locEpoch = -1;
    private static long s_retryAt;

    /// <summary>The table bound to <typeparamref name="T"/> rows, cached per (path, T). Never null: a missing or bad file logs
    /// and yields an empty table that fills when the file appears.</summary>
    public static DataTable<T> Load<T>(string path)
    {
        string abs = Absolute(path);
        string key = abs + "|" + typeof(T).AssemblyQualifiedName;
        if (s_typed.TryGetValue(key, out object? existing)) return (DataTable<T>)existing;
        var table = new DataTable<T>(OpenAbsolute(abs));
        table.Build();
        s_typed[key] = table;
        s_refresh.Add(table);
        return table;
    }

    /// <summary>The untyped table, cached per path.</summary>
    public static DataTable Open(string path) => OpenAbsolute(Absolute(path));

    /// <summary>Rebuilds typed tables whose file reloaded and, for tables with <c>text</c> columns, the ones a culture change
    /// affects; raises <see cref="DataTable{T}.Reloaded"/>. Also retries tables whose file was missing. Called by the host's tick.</summary>
    public static void Pump()
    {
        if (s_refresh.Count == 0) return;
        int te, le;
        try { te = FwTable.aver_fw_tables_epoch(); le = Loc.Epoch; }
        catch (Exception e) when (e is DllNotFoundException or BadImageFormatException) { return; }
        long now = Environment.TickCount64;
        bool retry = now >= s_retryAt && s_refresh.Any(t => t.Missing);
        if (te == s_tableEpoch && le == s_locEpoch && !retry) return;
        s_tableEpoch = te;
        s_locEpoch = le;
        if (retry) s_retryAt = now + 1000;
        foreach (ITableRefresh t in s_refresh.ToArray()) t.Refresh();
    }

    // Drops the caches (tests; a project switch).
    internal static void Reset()
    {
        s_raw.Clear();
        s_typed.Clear();
        s_refresh.Clear();
        s_tableEpoch = s_locEpoch = -1;
    }

    private static DataTable OpenAbsolute(string abs)
    {
        if (!s_raw.TryGetValue(abs, out DataTable? t))
        {
            t = new DataTable(abs);
            t.Ensure();
            s_raw[abs] = t;
        }
        return t;
    }

    private static string Absolute(string path)
    {
        string p = path.Replace('\\', '/');
        if (System.IO.Path.IsPathRooted(p)) return p;
        string root;
        try { root = FwTable.Str(FwTable.aver_fw_content_root()).TrimEnd('/', '\\'); }
        catch (Exception e) when (e is DllNotFoundException or BadImageFormatException) { root = string.Empty; }
        return root.Length == 0 ? p : root + "/" + p;
    }
}

/// <summary>What the host's per-frame tick calls: culture-change events and data-table rebuilds.</summary>
internal static class ContentPump
{
    internal static void Tick()
    {
        Loc.Pump();
        DataTables.Pump();
    }
}
