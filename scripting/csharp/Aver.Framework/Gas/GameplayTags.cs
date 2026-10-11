// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: hierarchical gameplay tags and ref-counted tag containers. See docs/GAMEPLAY_ABILITIES.md.

using System.Collections;
using Aver.Scripting;

namespace Aver.Framework;

/// <summary>The interned tag table. Ids are sequential from 1 in registration order; id 0 is "none".</summary>
internal static class TagRegistry
{
    internal const int MaxTags = 4096;
    internal const int MaxSegments = 8;
    internal const int MaxLength = 128;

    internal static readonly List<string> Names = new() { "" };
    internal static readonly List<int> Parents = new() { 0 };
    internal static readonly List<int> Depths = new() { 0 };
    private static readonly Dictionary<string, int> s_ids = new(StringComparer.Ordinal);

    private static bool IsLetter(char c) => (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');

    /// <summary>Segments <c>[A-Za-z][A-Za-z0-9_]*</c> joined by dots; at most 8 segments and 128 characters.</summary>
    internal static bool IsValidName(string? name)
    {
        if (string.IsNullOrEmpty(name) || name.Length > MaxLength) return false;
        int segments = 1;
        bool segmentStart = true;
        foreach (char c in name)
        {
            if (c == '.')
            {
                if (segmentStart) return false;
                segments++;
                segmentStart = true;
                continue;
            }
            if (segmentStart)
            {
                if (!IsLetter(c)) return false;
                segmentStart = false;
            }
            else if (!(IsLetter(c) || (c >= '0' && c <= '9') || c == '_')) return false;
        }
        return !segmentStart && segments <= MaxSegments;
    }

    internal static int Find(string name) => s_ids.TryGetValue(name, out int id) ? id : 0;

    /// <summary>Interns a VALID name and its ancestors. 0 when the table is full.</summary>
    internal static int Intern(string name)
    {
        if (s_ids.TryGetValue(name, out int existing)) return existing;
        int dot = name.LastIndexOf('.');
        int parent = 0;
        if (dot >= 0)
        {
            parent = Intern(name.Substring(0, dot));
            if (parent == 0) return 0;
        }
        if (Names.Count > MaxTags) return 0;
        int id = Names.Count;
        Names.Add(name);
        Parents.Add(parent);
        Depths.Add(dot < 0 ? 1 : Depths[parent] + 1);
        s_ids[name] = id;
        return id;
    }
}

/// <summary>A hierarchical gameplay tag such as <c>State.Stunned.Hard</c>. A tag matches itself and every ancestor.</summary>
/// <remarks>Separate from the 32-bit <c>CTags</c> mask on entities: that mask is flat scene ABI, this is
/// unbounded, named and ref-counted in containers. Ids depend on registration order, so persistent data
/// (saves, graph variables) must use names.</remarks>
public readonly struct GameplayTag : IEquatable<GameplayTag>
{
    private readonly int _id;

    internal GameplayTag(int id) => _id = id;

    /// <summary>The interned id; 0 is none.</summary>
    public int Id => _id;
    /// <summary>True for a real tag.</summary>
    public bool IsValid => _id != 0;
    /// <summary>The invalid tag.</summary>
    public static GameplayTag None => default;

    /// <summary>Interns <paramref name="name"/> (and its ancestors). An invalid name logs and returns <see cref="None"/>.</summary>
    public static GameplayTag Request(string name)
    {
        if (!TagRegistry.IsValidName(name))
        {
            Log.Warn($"[GAS] invalid gameplay tag name '{name}'");
            return None;
        }
        int id = TagRegistry.Intern(name);
        if (id == 0) Log.Error($"[GAS] gameplay tag table is full ({TagRegistry.MaxTags}); '{name}' not registered");
        return new GameplayTag(id);
    }

    /// <summary>Looks a tag up without interning it.</summary>
    public static bool TryFind(string name, out GameplayTag tag)
    {
        int id = string.IsNullOrEmpty(name) ? 0 : TagRegistry.Find(name);
        tag = new GameplayTag(id);
        return id != 0;
    }

    /// <summary>The tag with this id, or <see cref="None"/> when no such id is registered.</summary>
    public static GameplayTag FromId(int id) => id >= 1 && id < TagRegistry.Names.Count ? new GameplayTag(id) : None;

    /// <summary>The full dotted name; empty for none.</summary>
    public string Name => _id == 0 ? "" : TagRegistry.Names[_id];
    /// <summary>The tag one segment up, or none for a root tag.</summary>
    public GameplayTag Parent => _id == 0 ? None : new GameplayTag(TagRegistry.Parents[_id]);
    /// <summary>Number of segments (1 for a root tag, 0 for none).</summary>
    public int Depth => _id == 0 ? 0 : TagRegistry.Depths[_id];

    /// <summary>True when this tag equals <paramref name="other"/> or descends from it.</summary>
    public bool Matches(GameplayTag other)
    {
        if (_id == 0 || other._id == 0) return false;
        for (int t = _id; t != 0; t = TagRegistry.Parents[t])
            if (t == other._id) return true;
        return false;
    }

    /// <summary>True only for the same tag.</summary>
    public bool MatchesExact(GameplayTag other) => _id != 0 && _id == other._id;

    /// <summary>Every registered tag in id order.</summary>
    public static IReadOnlyList<GameplayTag> All
    {
        get
        {
            var list = new List<GameplayTag>(TagRegistry.Names.Count);
            for (int i = 1; i < TagRegistry.Names.Count; ++i) list.Add(new GameplayTag(i));
            return list;
        }
    }

    /// <inheritdoc/>
    public bool Equals(GameplayTag other) => _id == other._id;
    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is GameplayTag t && Equals(t);
    /// <inheritdoc/>
    public override int GetHashCode() => _id;
    /// <summary>Equality.</summary>
    public static bool operator ==(GameplayTag a, GameplayTag b) => a._id == b._id;
    /// <summary>Inequality.</summary>
    public static bool operator !=(GameplayTag a, GameplayTag b) => a._id != b._id;
    /// <inheritdoc/>
    public override string ToString() => Name;
}

/// <summary>A tag whose presence changed in a container (reported to the ability system).</summary>
internal readonly record struct TagFlip(GameplayTag Tag, bool Present, int Count);

/// <summary>A ref-counted set of tags. <see cref="Has"/> is hierarchical: holding <c>State.Stunned.Hard</c>
/// satisfies a query for <c>State.Stunned</c>.</summary>
public sealed class GameplayTagContainer : IEnumerable<GameplayTag>
{
    private readonly Dictionary<int, int> _exact = new();     // tag -> count added exactly
    private readonly Dictionary<int, int> _implied = new();   // tag -> count of itself plus every descendant added
    internal bool Locked;                                      // a read-only view handed out by AbilitySystem

    /// <summary>An empty container.</summary>
    public GameplayTagContainer() { }

    /// <summary>A container holding each tag once.</summary>
    public GameplayTagContainer(params GameplayTag[] tags)
    {
        foreach (GameplayTag t in tags) MutateInternal(t, 1, null);
    }

    private void ThrowIfLocked()
    {
        if (Locked) throw new InvalidOperationException("this tag container is a read-only view");
    }

    /// <summary>Adds <paramref name="count"/> references to the tag. Returns the exact count afterwards.</summary>
    public int Add(GameplayTag t, int count = 1)
    {
        ThrowIfLocked();
        return count <= 0 ? CountOf(t) : MutateInternal(t, count, null);
    }

    /// <summary>Removes up to <paramref name="count"/> references (the count floors at 0). Returns the exact count afterwards.</summary>
    public int Remove(GameplayTag t, int count = 1)
    {
        ThrowIfLocked();
        return count <= 0 ? CountOf(t) : MutateInternal(t, -count, null);
    }

    /// <summary>Adjusts the exact count by <paramref name="delta"/> and appends every presence change of the tag
    /// and its ancestors to <paramref name="flips"/>. Returns the exact count afterwards.</summary>
    internal int MutateInternal(GameplayTag t, int delta, List<TagFlip>? flips)
    {
        if (!t.IsValid || delta == 0) return CountOf(t);
        _exact.TryGetValue(t.Id, out int cur);
        if (delta < 0)
        {
            int take = Math.Min(-delta, cur);
            if (take == 0) return cur;
            delta = -take;
        }
        int next = cur + delta;
        if (next == 0) _exact.Remove(t.Id); else _exact[t.Id] = next;
        for (int id = t.Id; id != 0; id = TagRegistry.Parents[id])
        {
            _implied.TryGetValue(id, out int before);
            int after = before + delta;
            if (after == 0) _implied.Remove(id); else _implied[id] = after;
            if (flips != null && (before == 0) != (after == 0))
                flips.Add(new TagFlip(new GameplayTag(id), after != 0, after));
        }
        return next;
    }

    /// <summary>Exact references held for this tag (descendants are not counted).</summary>
    public int CountOf(GameplayTag t) => t.IsValid && _exact.TryGetValue(t.Id, out int n) ? n : 0;

    /// <summary>References to this tag and every descendant, as <see cref="Has"/> sees them.</summary>
    internal int ImpliedCount(GameplayTag t) => t.IsValid && _implied.TryGetValue(t.Id, out int n) ? n : 0;

    /// <summary>True when any held tag matches <paramref name="t"/> (equal or descendant).</summary>
    public bool Has(GameplayTag t) => t.IsValid && _implied.ContainsKey(t.Id);

    /// <summary>True only when this exact tag is held.</summary>
    public bool HasExact(GameplayTag t) => t.IsValid && _exact.ContainsKey(t.Id);

    /// <summary>True when at least one tag of <paramref name="o"/> is satisfied. False for an empty argument.</summary>
    public bool HasAny(GameplayTagContainer o)
    {
        foreach (int id in o._exact.Keys)
            if (_implied.ContainsKey(id)) return true;
        return false;
    }

    /// <summary>True when every tag of <paramref name="o"/> is satisfied. True for an empty argument.</summary>
    public bool HasAll(GameplayTagContainer o)
    {
        foreach (int id in o._exact.Keys)
            if (!_implied.ContainsKey(id)) return false;
        return true;
    }

    /// <summary>True when no tag of <paramref name="o"/> is satisfied.</summary>
    public bool HasNone(GameplayTagContainer o) => !HasAny(o);

    /// <summary>Number of distinct exact tags held.</summary>
    public int Count => _exact.Count;

    /// <summary>Drops every tag.</summary>
    public void Clear()
    {
        ThrowIfLocked();
        ClearInternal();
    }

    internal void ClearInternal()
    {
        _exact.Clear();
        _implied.Clear();
    }

    /// <summary>Exact tags in id order.</summary>
    public IEnumerator<GameplayTag> GetEnumerator()
    {
        var ids = new List<int>(_exact.Keys);
        ids.Sort();
        var tags = new List<GameplayTag>(ids.Count);
        foreach (int id in ids) tags.Add(new GameplayTag(id));
        return tags.GetEnumerator();
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    /// <inheritdoc/>
    public override string ToString() => string.Join(", ", this.Select(t => t.Name));
}
