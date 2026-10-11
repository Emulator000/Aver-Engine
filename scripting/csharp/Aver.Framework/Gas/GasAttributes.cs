// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: attribute names, attribute set definitions and change notifications.

using Aver.Scripting;

namespace Aver.Framework;

/// <summary>The interned attribute name table (global per process, limit 1024).</summary>
internal static class AttributeRegistry
{
    internal const int MaxAttributes = 1024;
    internal static readonly List<string> Names = new() { "" };
    private static readonly Dictionary<string, int> s_ids = new(StringComparer.Ordinal);

    internal static bool IsValidName(string? name)
    {
        if (string.IsNullOrEmpty(name) || name.Length > 64) return false;
        for (int i = 0; i < name.Length; ++i)
        {
            char c = name[i];
            bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            if (!(letter || (i > 0 && c >= '0' && c <= '9'))) return false;
        }
        return true;
    }

    internal static int Find(string name) => s_ids.TryGetValue(name, out int id) ? id : 0;

    internal static int Intern(string name)
    {
        if (s_ids.TryGetValue(name, out int id)) return id;
        if (Names.Count > MaxAttributes) return 0;
        id = Names.Count;
        Names.Add(name);
        s_ids[name] = id;
        return id;
    }
}

/// <summary>An interned attribute name. Two attributes with the same name are the same attribute.</summary>
public readonly struct AttributeRef : IEquatable<AttributeRef>
{
    private readonly int _id;

    internal AttributeRef(int id) => _id = id;

    /// <summary>The interned id; 0 is none.</summary>
    public int Id => _id;
    /// <summary>True for a real attribute.</summary>
    public bool IsValid => _id != 0;
    /// <summary>The attribute name; empty for none.</summary>
    public string Name => _id == 0 ? "" : AttributeRegistry.Names[_id];

    /// <summary>Interns <paramref name="name"/> (letters, digits and underscores, not starting with a digit).
    /// An invalid name logs and returns the invalid ref.</summary>
    public static AttributeRef Of(string name)
    {
        if (!AttributeRegistry.IsValidName(name))
        {
            Log.Warn($"[GAS] invalid attribute name '{name}'");
            return default;
        }
        int id = AttributeRegistry.Intern(name);
        if (id == 0) Log.Error($"[GAS] attribute table is full ({AttributeRegistry.MaxAttributes}); '{name}' not registered");
        return new AttributeRef(id);
    }

    /// <summary>The attribute with this id, or the invalid ref.</summary>
    internal static AttributeRef FromId(int id) => id >= 1 && id < AttributeRegistry.Names.Count ? new AttributeRef(id) : default;

    /// <summary>Looks a name up without interning it.</summary>
    internal static bool TryFind(string name, out AttributeRef a)
    {
        int id = string.IsNullOrEmpty(name) ? 0 : AttributeRegistry.Find(name);
        a = new AttributeRef(id);
        return id != 0;
    }

    /// <inheritdoc/>
    public bool Equals(AttributeRef other) => _id == other._id;
    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is AttributeRef a && Equals(a);
    /// <inheritdoc/>
    public override int GetHashCode() => _id;
    /// <summary>Equality.</summary>
    public static bool operator ==(AttributeRef a, AttributeRef b) => a._id == b._id;
    /// <summary>Inequality.</summary>
    public static bool operator !=(AttributeRef a, AttributeRef b) => a._id != b._id;
    /// <inheritdoc/>
    public override string ToString() => Name;
}

/// <summary>One attribute of a set: its default Base, its bounds and its meta behaviour.</summary>
public sealed class AttributeDef
{
    /// <summary>Attribute name (global per system).</summary>
    public string Name = "";
    /// <summary>Base value a fresh entity starts with.</summary>
    public float DefaultBase;
    /// <summary>Literal lower bound (default -infinity).</summary>
    public float Min = float.NegativeInfinity;
    /// <summary>Literal upper bound (default +infinity).</summary>
    public float Max = float.PositiveInfinity;
    /// <summary>When valid, another attribute's Current value is the lower bound instead of <see cref="Min"/>.</summary>
    public AttributeRef MinFrom;
    /// <summary>When valid, another attribute's Current value is the upper bound instead of <see cref="Max"/>.</summary>
    public AttributeRef MaxFrom;
    /// <summary>A meta attribute is never stored; an instant Add to it is forwarded (or just announced) and it reads 0 again.</summary>
    public bool IsMeta;
    /// <summary>Meta only: the attribute an instant Add is forwarded to.</summary>
    public AttributeRef ForwardTo;
    /// <summary>Meta only: multiplier applied to the forwarded amount.</summary>
    public float ForwardScale = 1f;
}

/// <summary>A named group of attributes added to an entity together.</summary>
public sealed class AttributeSetDef
{
    /// <summary>Set name.</summary>
    public string Name = "";
    /// <summary>The attributes in declaration order.</summary>
    public IReadOnlyList<AttributeDef> Attributes = Array.Empty<AttributeDef>();

    /// <summary>An empty set (fill the fields).</summary>
    public AttributeSetDef() { }

    /// <summary>A set with its attributes.</summary>
    public AttributeSetDef(string name, IReadOnlyList<AttributeDef> attributes)
    {
        Name = name;
        Attributes = attributes;
    }
}

/// <summary>A change in an attribute's Current value, delivered after the mutation that caused it has finished.</summary>
public readonly record struct AttributeChange(AbilitySystem System, AttributeRef Attribute, float OldValue, float NewValue, Entity Instigator);

/// <summary>Lets a game adjust an attribute's new Current value before it is stored (a custom clamp).</summary>
public delegate void PreAttributeChange(AbilitySystem system, AttributeRef attribute, ref float newValue);

/// <summary>Bound-chain checks shared by definition linking and <see cref="AbilitySystem.AddAttributeSet(AttributeSetDef)"/>.</summary>
internal static class AttributeBounds
{
    /// <summary>Returns a description of a MinFrom/MaxFrom cycle among <paramref name="defs"/>, or null. Later defs
    /// of the same name replace earlier ones.</summary>
    internal static string? FindCycle(IEnumerable<AttributeDef> defs)
    {
        var byId = new Dictionary<int, AttributeDef>();
        foreach (AttributeDef d in defs)
        {
            AttributeRef r = AttributeRef.Of(d.Name);
            if (r.IsValid) byId[r.Id] = d;
        }
        var state = new Dictionary<int, int>();   // 1 = on the current path, 2 = done
        var path = new List<int>();
        foreach (int start in byId.Keys.OrderBy(k => k))
        {
            string? c = Visit(start, byId, state, path);
            if (c != null) return c;
        }
        return null;
    }

    private static string? Visit(int id, Dictionary<int, AttributeDef> byId, Dictionary<int, int> state, List<int> path)
    {
        if (state.TryGetValue(id, out int s))
        {
            if (s == 2) return null;
            int at = path.IndexOf(id);
            var names = new List<string>();
            for (int i = at; i < path.Count; ++i) names.Add(AttributeRegistry.Names[path[i]]);
            names.Add(AttributeRegistry.Names[id]);
            return string.Join(" -> ", names);
        }
        if (!byId.TryGetValue(id, out AttributeDef? d)) return null;
        state[id] = 1;
        path.Add(id);
        foreach (AttributeRef next in new[] { d.MinFrom, d.MaxFrom })
        {
            if (!next.IsValid) continue;
            string? c = Visit(next.Id, byId, state, path);
            if (c != null) return c;
        }
        path.RemoveAt(path.Count - 1);
        state[id] = 2;
        return null;
    }
}
