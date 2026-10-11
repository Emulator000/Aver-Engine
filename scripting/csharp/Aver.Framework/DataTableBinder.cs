// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Binds the rows of a data table (a .csv or .json file) to C# records. Pure: no natives. See docs/DATA_TABLES.md.

using System.Collections;
using System.Globalization;
using System.Reflection;
using Aver.Scene;

namespace Aver.Framework;

/// <summary>The type a table column declares in its header (<c>Damage:int</c>); advisory, the cell text is what is stored.
/// Values match <c>AVER_FW_TABLE_COL_*</c>.</summary>
public enum DataColumnType { String, Int, Float, Bool, Vec3, Text, Strings }

/// <summary>A table as the binder reads it: named columns, named rows, text cells.</summary>
public interface IRawTable
{
    /// <summary>The number of columns, not counting the row-name column.</summary>
    int ColumnCount { get; }
    /// <summary>The name of column <paramref name="c"/>.</summary>
    string ColumnName(int c);
    /// <summary>The declared type of column <paramref name="c"/>.</summary>
    DataColumnType ColumnType(int c);
    /// <summary>The number of rows.</summary>
    int RowCount { get; }
    /// <summary>The name of row <paramref name="r"/>.</summary>
    string RowName(int r);
    /// <summary>The cell text, empty when out of range.</summary>
    string Cell(int r, int c);
}

/// <summary>Binds a member to the column of this name instead of the member's own name.</summary>
[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)]
public sealed class DataColumnAttribute : Attribute
{
    /// <summary>The column header (without a type hint).</summary>
    public string Name { get; }
    /// <summary>Binds to the column called <paramref name="name"/>.</summary>
    public DataColumnAttribute(string name) => Name = name;
}

/// <summary>Leaves the member out of binding.</summary>
[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)]
public sealed class DataIgnoreAttribute : Attribute { }

/// <summary>The member (a string) receives the row's name.</summary>
[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field)]
public sealed class DataRowNameAttribute : Attribute { }

/// <summary>Cell text to values, invariant culture. Shared by the binder and <see cref="DataTable"/>.</summary>
internal static class DataCell
{
    internal static bool TryBool(string s, out bool v)
    {
        switch (s.Trim().ToLowerInvariant())
        {
            case "true": case "1": case "yes": v = true; return true;
            case "false": case "0": case "no": v = false; return true;
            default: v = false; return false;
        }
    }

    internal static bool TryLong(string s, out long v)
    {
        s = s.Trim();
        if (long.TryParse(s, NumberStyles.Integer, CultureInfo.InvariantCulture, out v)) return true;
        // "25.0" from a spreadsheet is still the integer 25.
        if (double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out double d) && d == Math.Floor(d) && Math.Abs(d) < 9.0e18)
        {
            v = (long)d;
            return true;
        }
        return false;
    }

    internal static bool TryDouble(string s, out double v)
    {
        s = s.Trim();
        if (double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out v)) return true;
        if (s.Length > 1 && (s[^1] == 'f' || s[^1] == 'F')) return double.TryParse(s.AsSpan(0, s.Length - 1), NumberStyles.Float, CultureInfo.InvariantCulture, out v);
        return false;
    }

    internal static bool TryVec3(string s, out Vec3 v)
    {
        v = default;
        string[] p = s.Split(new[] { ' ', ',', ';', '|' }, StringSplitOptions.RemoveEmptyEntries);
        if (p.Length != 3) return false;
        if (!TryDouble(p[0], out double x) || !TryDouble(p[1], out double y) || !TryDouble(p[2], out double z)) return false;
        v = new Vec3((float)x, (float)y, (float)z);
        return true;
    }

    internal static string[] SplitList(string s) => s.Length == 0 ? Array.Empty<string>() : s.Split('|');
}

/// <summary>Maps table rows onto <typeparamref name="T"/> by column name. Rules: public settable properties (including
/// <c>init</c>) and public fields; column match is case-insensitive ignoring spaces and underscores
/// (<c>Display Name</c> matches <c>DisplayName</c>), <see cref="DataColumnAttribute"/> overrides; a public parameterless
/// constructor is used when there is one, else the single public constructor with parameters matched to columns by name
/// (positional records). Member types: string, bool, integers, float, double, enums, <see cref="Vec3"/>, <see cref="LocText"/>,
/// arrays / <c>List</c> / <c>IReadOnlyList</c> of those scalars (cell split on '|'), and <c>Nullable</c> of the scalars
/// (empty cell is null).</summary>
public static class DataTableBinder
{
    /// <summary>Binds every row; element <c>i</c> of the result is table row <c>i</c>. Problems are appended to
    /// <paramref name="problems"/> (a bad cell leaves that member's default, a missing column leaves the default and is reported once).</summary>
    public static T[] Bind<T>(IRawTable table, List<string> problems)
    {
        Type type = typeof(T);
        var result = new T[table.RowCount];

        var colByNorm = new Dictionary<string, int>();
        for (int c = 0; c < table.ColumnCount; c++) colByNorm.TryAdd(Norm(table.ColumnName(c)), c);

        List<Member> members = Collect(type);
        ConstructorInfo? ctor = null;
        if (!type.IsValueType && type.GetConstructor(Type.EmptyTypes) == null)
        {
            ConstructorInfo[] ctors = type.GetConstructors();
            if (ctors.Length == 0) throw new InvalidOperationException(type.Name + " has no public constructor to bind rows with");
            ctor = ctors.OrderByDescending(x => x.GetParameters().Length).First();
        }

        var used = new HashSet<int>();
        List<Slot> slots = ctor == null ? SlotsForMembers(members, colByNorm, used, problems, type)
                                        : SlotsForCtor(ctor, members, colByNorm, used, problems, type);
        for (int c = 0; c < table.ColumnCount; c++)
            if (!used.Contains(c)) problems.Add($"info: column '{table.ColumnName(c)}' is not used by {type.Name}");

        for (int r = 0; r < result.Length; r++)
        {
            object?[] args = ctor != null ? new object?[ctor.GetParameters().Length] : Array.Empty<object?>();
            object? boxed = ctor == null ? Activator.CreateInstance(type) : null;
            foreach (Slot s in slots)
            {
                object? value = null;
                bool set = false;
                if (s.RowName)
                {
                    value = table.RowName(r);
                    set = true;
                }
                else if (s.Column >= 0)
                {
                    string cell = table.Cell(r, s.Column);
                    switch (TryConvert(cell, s.Type, table.ColumnType(s.Column), out value, out string why))
                    {
                        case Conv.Ok: set = true; break;
                        case Conv.Skip: break;
                        default:
                            problems.Add($"row '{table.RowName(r)}' column '{table.ColumnName(s.Column)}': '{cell}' is not {why}");
                            break;
                    }
                }
                if (ctor != null)
                {
                    args[s.Param] = set ? value : DefaultOf(s.Type);
                }
                else if (set)
                {
                    s.Member!.Set(boxed!, value);
                }
            }
            result[r] = ctor != null ? (T)ctor.Invoke(args) : (T)boxed!;
        }
        return result;
    }

    // ---- planning -------------------------------------------------------------------------------

    private sealed class Member
    {
        internal string Name = "";
        internal string Column = "";      // the column this member binds to
        internal Type Type = typeof(object);
        internal bool RowName;
        internal PropertyInfo? Prop;
        internal FieldInfo? Field;
        internal void Set(object target, object? value)
        {
            if (Prop != null) Prop.SetValue(target, value);
            else Field!.SetValue(target, value);
        }
    }

    private sealed class Slot
    {
        internal Member? Member;
        internal Type Type = typeof(object);
        internal int Column = -1;
        internal bool RowName;
        internal int Param = -1;          // constructor parameter index, -1 for member assignment
    }

    private static List<Member> Collect(Type type)
    {
        var list = new List<Member>();
        foreach (PropertyInfo p in type.GetProperties(BindingFlags.Public | BindingFlags.Instance))
        {
            if (p.GetIndexParameters().Length > 0 || p.SetMethod == null || !p.SetMethod.IsPublic) continue;
            if (p.GetCustomAttribute<DataIgnoreAttribute>() != null) continue;
            list.Add(Make(p.Name, p.PropertyType, p, null, p.GetCustomAttribute<DataColumnAttribute>(), p.GetCustomAttribute<DataRowNameAttribute>() != null));
        }
        foreach (FieldInfo f in type.GetFields(BindingFlags.Public | BindingFlags.Instance))
        {
            if (f.IsInitOnly || f.IsLiteral || f.GetCustomAttribute<DataIgnoreAttribute>() != null) continue;
            list.Add(Make(f.Name, f.FieldType, null, f, f.GetCustomAttribute<DataColumnAttribute>(), f.GetCustomAttribute<DataRowNameAttribute>() != null));
        }
        return list;
    }

    private static Member Make(string name, Type t, PropertyInfo? p, FieldInfo? f, DataColumnAttribute? attr, bool rowName)
        => new Member { Name = name, Column = attr?.Name ?? name, Type = t, Prop = p, Field = f, RowName = rowName };

    private static List<Slot> SlotsForMembers(List<Member> members, Dictionary<string, int> cols, HashSet<int> used, List<string> problems, Type type)
    {
        var slots = new List<Slot>();
        foreach (Member m in members)
        {
            var s = new Slot { Member = m, Type = m.Type, RowName = m.RowName };
            if (!m.RowName)
            {
                if (cols.TryGetValue(Norm(m.Column), out int c)) { s.Column = c; used.Add(c); }
                else problems.Add($"column '{m.Column}' for {type.Name}.{m.Name} is missing from the table (the default is kept)");
            }
            slots.Add(s);
        }
        return slots;
    }

    private static List<Slot> SlotsForCtor(ConstructorInfo ctor, List<Member> members, Dictionary<string, int> cols, HashSet<int> used, List<string> problems, Type type)
    {
        var slots = new List<Slot>();
        ParameterInfo[] ps = ctor.GetParameters();
        for (int i = 0; i < ps.Length; i++)
        {
            // A positional record's attributes sit on the property of the same name.
            Member? prop = members.FirstOrDefault(m => string.Equals(m.Name, ps[i].Name, StringComparison.OrdinalIgnoreCase));
            var s = new Slot { Type = ps[i].ParameterType, Param = i, RowName = prop?.RowName ?? false };
            if (!s.RowName)
            {
                string colName = prop?.Column ?? ps[i].Name ?? "";
                if (cols.TryGetValue(Norm(colName), out int c)) { s.Column = c; used.Add(c); }
                else problems.Add($"column '{colName}' for {type.Name}({ps[i].Name}) is missing from the table (the default is kept)");
            }
            slots.Add(s);
        }
        return slots;
    }

    private static string Norm(string s)
    {
        var sb = new System.Text.StringBuilder(s.Length);
        foreach (char ch in s) if (ch != ' ' && ch != '_') sb.Append(char.ToLowerInvariant(ch));
        return sb.ToString();
    }

    private static object? DefaultOf(Type t) => t.IsValueType ? Activator.CreateInstance(t) : null;

    // ---- conversion -----------------------------------------------------------------------------

    private enum Conv { Ok, Skip, Error }

    private static Conv TryConvert(string cell, Type target, DataColumnType columnType, out object? value, out string why)
    {
        value = null;
        why = "";
        Type? nullable = Nullable.GetUnderlyingType(target);
        if (nullable != null)
        {
            if (cell.Trim().Length == 0) return Conv.Ok;       // null
            return TryConvert(cell, nullable, columnType, out value, out why);
        }
        if (target == typeof(string))
        {
            value = columnType == DataColumnType.Text ? Loc.Resolve(cell) : cell;
            return Conv.Ok;
        }
        if (target == typeof(LocText)) { value = new LocText(cell); return Conv.Ok; }
        if (TryCollection(target, out Type? element))
        {
            var list = (IList)Activator.CreateInstance(typeof(List<>).MakeGenericType(element!))!;
            foreach (string part in DataCell.SplitList(cell))
            {
                Conv c = TryConvert(part, element!, DataColumnType.String, out object? item, out why);
                if (c == Conv.Error) return Conv.Error;
                list.Add(c == Conv.Skip ? DefaultOf(element!) : item);
            }
            if (target.IsArray)
            {
                Array arr = Array.CreateInstance(element!, list.Count);
                list.CopyTo(arr, 0);
                value = arr;
            }
            else
            {
                value = list;
            }
            return Conv.Ok;
        }

        if (cell.Trim().Length == 0) return Conv.Skip;         // an empty cell leaves the default
        if (target == typeof(bool))
        {
            if (DataCell.TryBool(cell, out bool b)) { value = b; return Conv.Ok; }
            why = "a bool (true/false, yes/no, 1/0)";
            return Conv.Error;
        }
        if (target == typeof(float) || target == typeof(double))
        {
            if (DataCell.TryDouble(cell, out double d)) { value = target == typeof(float) ? (object)(float)d : d; return Conv.Ok; }
            why = target == typeof(float) ? "a float" : "a double";
            return Conv.Error;
        }
        if (target == typeof(ulong))
        {
            if (ulong.TryParse(cell.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out ulong u)) { value = u; return Conv.Ok; }
            why = "a ulong";
            return Conv.Error;
        }
        if (IsInteger(target))
        {
            if (DataCell.TryLong(cell, out long l))
            {
                try { value = Convert.ChangeType(l, target, CultureInfo.InvariantCulture); return Conv.Ok; }
                catch (OverflowException) { why = "in range for " + Word(target); return Conv.Error; }
            }
            why = Article(Word(target));
            return Conv.Error;
        }
        if (target.IsEnum)
        {
            if (Enum.TryParse(target, cell.Trim(), true, out object? e)) { value = e; return Conv.Ok; }
            why = "a valid " + target.Name;
            return Conv.Error;
        }
        if (target == typeof(Vec3))
        {
            if (DataCell.TryVec3(cell, out Vec3 v)) { value = v; return Conv.Ok; }
            why = "a vec3 (three numbers)";
            return Conv.Error;
        }
        why = "a supported member type (" + target.Name + ")";
        return Conv.Error;
    }

    private static bool TryCollection(Type t, out Type? element)
    {
        element = null;
        if (t.IsArray) { element = t.GetElementType(); return true; }
        if (!t.IsGenericType) return false;
        Type def = t.GetGenericTypeDefinition();
        if (def == typeof(List<>) || def == typeof(IReadOnlyList<>) || def == typeof(IList<>) || def == typeof(IEnumerable<>) ||
            def == typeof(IReadOnlyCollection<>) || def == typeof(ICollection<>))
        {
            element = t.GetGenericArguments()[0];
            return true;
        }
        return false;
    }

    private static bool IsInteger(Type t) => t == typeof(int) || t == typeof(long) || t == typeof(short) || t == typeof(byte) ||
                                             t == typeof(sbyte) || t == typeof(ushort) || t == typeof(uint);

    private static string Word(Type t) => t == typeof(int) ? "int" : t == typeof(long) ? "long" : t == typeof(short) ? "short" :
                                          t == typeof(byte) ? "byte" : t == typeof(sbyte) ? "sbyte" : t == typeof(ushort) ? "ushort" :
                                          t == typeof(uint) ? "uint" : t.Name;

    private static string Article(string word) => (word[0] is 'a' or 'e' or 'i' or 'o' or 'u' ? "an " : "a ") + word;
}
