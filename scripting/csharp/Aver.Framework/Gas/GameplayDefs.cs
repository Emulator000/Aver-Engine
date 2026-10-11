// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: the definition registry. Code-defined (builders) and file-defined (.ocgas)
// definitions share one namespace; files are linked together once, so references may point forward.

using Aver.Scripting;

namespace Aver.Framework;

/// <summary>Per-class defaults an actor's ability system starts with (from <c>ACTORDEFAULTS</c> or <c>[AverAbilitySystem]</c>).</summary>
internal sealed class ActorDefaultsDef
{
    public string ClassName = "";
    public List<string> AttributeSets = new();
    public List<KeyValuePair<string, float>> BaseOverrides = new();
    public List<KeyValuePair<string, int>> Grants = new();
    public List<string> Applies = new();
    public List<string> Tags = new();
}

/// <summary>Who defined a name, for duplicate messages.</summary>
internal readonly record struct GasOwner(GasFileParse? File, string Source, int Line)
{
    public string Where => Line > 0 ? $"{Source}:{Line}" : Source;
}

/// <summary>Everything linking needs to resolve a name: the registry plus the files being linked.</summary>
internal sealed class GasEnv
{
    public readonly Dictionary<string, GasOwner> Sets = new(StringComparer.Ordinal);
    public readonly Dictionary<string, GasOwner> Effects = new(StringComparer.Ordinal);
    public readonly Dictionary<string, GasOwner> Abilities = new(StringComparer.Ordinal);
    public readonly Dictionary<string, GasOwner> Actors = new(StringComparer.Ordinal);
    public readonly Dictionary<string, EffectPolicy> EffectPolicies = new(StringComparer.Ordinal);
    public readonly HashSet<string> AttrNames = new(StringComparer.Ordinal);
    public readonly HashSet<string> CyclicAttrs = new(StringComparer.Ordinal);

    internal static GasEnv Build(IReadOnlyList<GasFileParse> files)
    {
        var env = new GasEnv();
        var edges = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        void Edge(string src, string? dst)
        {
            if (dst == null) return;
            if (!edges.TryGetValue(src, out List<string>? l)) edges[src] = l = new List<string>();
            l.Add(dst);
        }

        foreach (KeyValuePair<string, AttributeSetDef> kv in GameplayDefs.RegisteredSets())
        {
            env.Sets[kv.Key] = new GasOwner(null, GameplayDefs.SourceOf("set", kv.Key), 0);
            foreach (AttributeDef a in kv.Value.Attributes)
            {
                env.AttrNames.Add(a.Name);
                if (a.MinFrom.IsValid) Edge(a.Name, a.MinFrom.Name);
                if (a.MaxFrom.IsValid) Edge(a.Name, a.MaxFrom.Name);
            }
        }
        foreach (KeyValuePair<string, EffectDef> kv in GameplayDefs.RegisteredEffects())
        {
            env.Effects[kv.Key] = new GasOwner(null, GameplayDefs.SourceOf("effect", kv.Key), 0);
            env.EffectPolicies[kv.Key] = kv.Value.Policy;
        }
        foreach (KeyValuePair<string, AbilityDef> kv in GameplayDefs.RegisteredAbilities())
            env.Abilities[kv.Key] = new GasOwner(null, GameplayDefs.SourceOf("ability", kv.Key), 0);
        foreach (string cls in GameplayDefs.RegisteredActorClasses())
            env.Actors[cls] = new GasOwner(null, GameplayDefs.SourceOf("actor", cls), 0);

        foreach (GasFileParse f in files)
        {
            foreach (SetRec s in f.Sets)
            {
                env.Sets.TryAdd(s.Name, new GasOwner(f, f.Source, s.Line));
                foreach (AttrRec a in s.Attrs)
                {
                    env.AttrNames.Add(a.Name);
                    Edge(a.Name, a.MinFrom);
                    Edge(a.Name, a.MaxFrom);
                }
            }
            foreach (EffectRec e in f.Effects)
            {
                env.Effects.TryAdd(e.Name, new GasOwner(f, f.Source, e.Line));
                env.EffectPolicies.TryAdd(e.Name, e.Policy);
            }
            foreach (AbilityRec a in f.Abilities) env.Abilities.TryAdd(a.Name, new GasOwner(f, f.Source, a.Line));
            foreach (ActorRec a in f.Actors) env.Actors.TryAdd(a.Name, new GasOwner(f, f.Source, a.Line));
        }

        // Attributes that sit on a MinFrom/MaxFrom cycle.
        var state = new Dictionary<string, int>(StringComparer.Ordinal);
        var path = new List<string>();
        void Visit(string n)
        {
            if (state.TryGetValue(n, out int st))
            {
                if (st == 1)
                    for (int i = path.IndexOf(n); i >= 0 && i < path.Count; ++i) env.CyclicAttrs.Add(path[i]);
                return;
            }
            state[n] = 1;
            path.Add(n);
            if (edges.TryGetValue(n, out List<string>? next))
                foreach (string t in next) Visit(t);
            path.RemoveAt(path.Count - 1);
            state[n] = 2;
        }
        foreach (string n in edges.Keys.ToList()) Visit(n);
        return env;
    }
}

internal static class GasLink
{
    // Returns the problems with one file against the whole set, as "source:line: message".
    internal static List<string> Validate(GasFileParse f, GasEnv env)
    {
        var errs = new List<string>();
        void Err(int line, string msg) => errs.Add($"{f.Source}:{line}: {msg}");
        void Dup(Dictionary<string, GasOwner> table, string kind, string name, int line)
        {
            if (table.TryGetValue(name, out GasOwner o) && !ReferenceEquals(o.File, f))
                Err(line, $"{kind} '{name}' is already defined at {o.Where}");
        }

        foreach (SetRec s in f.Sets)
        {
            Dup(env.Sets, "attribute set", s.Name, s.Line);
            foreach (AttrRec a in s.Attrs)
            {
                if (a.MinFrom != null && !env.AttrNames.Contains(a.MinFrom)) Err(a.Line, $"undefined attribute '{a.MinFrom}' in min=");
                if (a.MaxFrom != null && !env.AttrNames.Contains(a.MaxFrom)) Err(a.Line, $"undefined attribute '{a.MaxFrom}' in max=");
                if (a.Forward != null && !env.AttrNames.Contains(a.Forward)) Err(a.Line, $"undefined attribute '{a.Forward}' in forward=");
                if ((a.MinFrom != null || a.MaxFrom != null) && env.CyclicAttrs.Contains(a.Name))
                    Err(a.Line, $"attribute '{a.Name}' has cyclic min/max bounds");
            }
        }
        foreach (EffectRec e in f.Effects)
        {
            Dup(env.Effects, "effect", e.Name, e.Line);
            foreach (ModRec m in e.Mods)
            {
                if (!env.AttrNames.Contains(m.Attr)) Err(m.Line, $"modifier on attribute '{m.Attr}', which no ATTRSET defines");
                if (m.FromAttr != null && !env.AttrNames.Contains(m.FromAttr)) Err(m.Line, $"undefined attribute '{m.FromAttr}' in from=");
            }
        }
        foreach (AbilityRec a in f.Abilities)
        {
            Dup(env.Abilities, "ability", a.Name, a.Line);
            if (a.Cost != null)
            {
                if (!env.EffectPolicies.TryGetValue(a.Cost, out EffectPolicy p)) Err(a.CostLine, $"undefined effect '{a.Cost}' in COST");
                else if (p != EffectPolicy.Instant) Err(a.CostLine, $"COST effect '{a.Cost}' must be an instant effect");
            }
            if (a.Cooldown != null)
            {
                if (!env.EffectPolicies.TryGetValue(a.Cooldown, out EffectPolicy p)) Err(a.CooldownLine, $"undefined effect '{a.Cooldown}' in COOLDOWN");
                else if (p == EffectPolicy.Instant) Err(a.CooldownLine, $"COOLDOWN effect '{a.Cooldown}' must be a duration or infinite effect");
            }
        }
        foreach (ActorRec c in f.Actors)
        {
            Dup(env.Actors, "ACTORDEFAULTS for class", c.Name, c.Line);
            foreach (var x in c.Sets)
                if (!env.Sets.ContainsKey(x.Name)) Err(x.Line, $"undefined attribute set '{x.Name}'");
            foreach (var x in c.Attrs)
                if (!env.AttrNames.Contains(x.Name)) Err(x.Line, $"undefined attribute '{x.Name}'");
            foreach (var x in c.Grants)
                if (!env.Abilities.ContainsKey(x.Name)) Err(x.Line, $"undefined ability '{x.Name}' in GRANT");
            foreach (var x in c.Applies)
                if (!env.Effects.ContainsKey(x.Name)) Err(x.Line, $"undefined effect '{x.Name}' in APPLY");
        }
        return errs;
    }
}

/// <summary>The registry of attribute sets, effects, abilities and class defaults. Definitions are immutable once registered.
/// Single game thread only.</summary>
public static class GameplayDefs
{
    private static readonly Dictionary<string, EffectDef> s_effects = new(StringComparer.Ordinal);
    private static readonly List<EffectDef> s_effectList = new();
    private static readonly Dictionary<string, AbilityDef> s_abilities = new(StringComparer.Ordinal);
    private static readonly List<AbilityDef> s_abilityList = new();
    private static readonly Dictionary<string, AttributeSetDef> s_sets = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, ActorDefaultsDef> s_actors = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, string> s_sources = new(StringComparer.Ordinal);

    // ---- lookup ---------------------------------------------------------------------------------

    /// <summary>The registered effect called <paramref name="name"/>, or null.</summary>
    public static EffectDef? FindEffect(string name) => s_effects.TryGetValue(name, out EffectDef? d) ? d : null;
    /// <summary>The registered ability called <paramref name="name"/>, or null.</summary>
    public static AbilityDef? FindAbility(string name) => s_abilities.TryGetValue(name, out AbilityDef? d) ? d : null;
    /// <summary>The registered attribute set called <paramref name="name"/>, or null.</summary>
    public static AttributeSetDef? FindAttributeSet(string name) => s_sets.TryGetValue(name, out AttributeSetDef? d) ? d : null;

    internal static EffectDef? FindEffectById(int id) => id >= 1 && id <= s_effectList.Count ? s_effectList[id - 1] : null;
    internal static AbilityDef? FindAbilityById(int id) => id >= 1 && id <= s_abilityList.Count ? s_abilityList[id - 1] : null;
    internal static ActorDefaultsDef? FindActorDefaults(string className) => s_actors.TryGetValue(className, out ActorDefaultsDef? d) ? d : null;

    internal static IEnumerable<KeyValuePair<string, AttributeSetDef>> RegisteredSets() => s_sets.ToList();
    internal static IEnumerable<KeyValuePair<string, EffectDef>> RegisteredEffects() => s_effects.ToList();
    internal static IEnumerable<KeyValuePair<string, AbilityDef>> RegisteredAbilities() => s_abilities.ToList();
    internal static IEnumerable<string> RegisteredActorClasses() => s_actors.Keys.ToList();
    internal static string SourceOf(string kind, string name) => s_sources.TryGetValue(kind + ":" + name, out string? s) ? s : "code";

    /// <summary>Letters, digits, underscores and dots; at most 128 characters.</summary>
    internal static bool IsValidDefName(string? name)
    {
        if (string.IsNullOrEmpty(name) || name.Length > 128) return false;
        char c0 = name[0];
        if (!((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z') || c0 == '_')) return false;
        foreach (char c in name)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
        return true;
    }

    // ---- registration ---------------------------------------------------------------------------

    /// <summary>Registers an effect. A bad definition or a duplicate name is logged and ignored.</summary>
    public static void Register(EffectDef def) => TryRegister(def, "code");

    /// <summary>Registers an ability. A bad definition or a duplicate name is logged and ignored.</summary>
    public static void Register(AbilityDef def) => TryRegister(def, "code");

    /// <summary>Registers an attribute set. A bad definition or a duplicate name is logged and ignored.</summary>
    public static void Register(AttributeSetDef def) => TryRegister(def, "code");

    internal static bool TryRegister(EffectDef def, string source)
    {
        ArgumentNullException.ThrowIfNull(def);
        string? problem = ValidateEffect(def);
        if (problem == null && s_effects.ContainsKey(def.Name))
            problem = $"already defined at {SourceOf("effect", def.Name)}";
        if (problem != null)
        {
            Log.Error($"[GAS] effect '{def.Name}' ({source}): {problem}");
            return false;
        }
        def.Id = s_effectList.Count + 1;
        s_effects[def.Name] = def;
        s_effectList.Add(def);
        s_sources["effect:" + def.Name] = source;
        return true;
    }

    internal static bool TryRegister(AbilityDef def, string source)
    {
        ArgumentNullException.ThrowIfNull(def);
        string? problem = null;
        if (!IsValidDefName(def.Name)) problem = "invalid name";
        else if (s_abilities.ContainsKey(def.Name)) problem = $"already defined at {SourceOf("ability", def.Name)}";
        else if (def.Cost != null && def.Cost.Policy != EffectPolicy.Instant) problem = $"cost effect '{def.Cost.Name}' must be instant";
        else if (def.Cooldown != null && def.Cooldown.Policy == EffectPolicy.Instant) problem = $"cooldown effect '{def.Cooldown.Name}' must be a duration or infinite effect";
        else if (!float.IsFinite(def.Duration) || def.Duration < 0f) problem = "invalid duration";
        if (problem != null)
        {
            Log.Error($"[GAS] ability '{def.Name}' ({source}): {problem}");
            return false;
        }
        if (def.Cooldown != null && def.Cooldown.GrantedTags.Count == 0)
            Log.Warn($"[GAS] ability '{def.Name}': cooldown effect '{def.Cooldown.Name}' grants no tag, so it never blocks activation");
        def.Id = s_abilityList.Count + 1;
        s_abilities[def.Name] = def;
        s_abilityList.Add(def);
        s_sources["ability:" + def.Name] = source;
        return true;
    }

    internal static bool TryRegister(AttributeSetDef def, string source)
    {
        ArgumentNullException.ThrowIfNull(def);
        string? problem = null;
        var names = new HashSet<string>(StringComparer.Ordinal);
        if (!IsValidDefName(def.Name)) problem = "invalid name";
        else if (s_sets.ContainsKey(def.Name)) problem = $"already defined at {SourceOf("set", def.Name)}";
        else
        {
            foreach (AttributeDef a in def.Attributes)
            {
                if (!AttributeRegistry.IsValidName(a.Name) || !names.Add(a.Name)) { problem = $"invalid or repeated attribute '{a.Name}'"; break; }
                if (!float.IsFinite(a.DefaultBase)) { problem = $"attribute '{a.Name}' has a non-finite default"; break; }
            }
        }
        if (problem == null)
        {
            string? cycle = AttributeBounds.FindCycle(s_sets.Values.SelectMany(s => s.Attributes).Concat(def.Attributes));
            if (cycle != null) problem = $"cyclic bounds: {cycle}";
        }
        if (problem != null)
        {
            Log.Error($"[GAS] attribute set '{def.Name}' ({source}): {problem}");
            return false;
        }
        s_sets[def.Name] = def;
        s_sources["set:" + def.Name] = source;
        return true;
    }

    internal static string? ValidateEffect(EffectDef d)
    {
        if (!IsValidDefName(d.Name)) return "invalid name";
        if (!float.IsFinite(d.Duration) || d.Duration < 0f) return "invalid duration";
        if (!float.IsFinite(d.Period) || d.Period < 0f) return "invalid period";
        if (d.Policy == EffectPolicy.Instant && d.Period > 0f) return "an instant effect cannot be periodic";
        if (d.Modifiers.Count > OcGasParser.MaxModifiers) return $"more than {OcGasParser.MaxModifiers} modifiers";
        if (d.Stack != StackScope.None && d.StackLimit < 1) return "stack limit must be at least 1";
        foreach (ModifierDef m in d.Modifiers)
        {
            if (!m.Attribute.IsValid) return "a modifier has no attribute";
            if (m.Kind == MagnitudeKind.SetByCaller && !m.CallerTag.IsValid) return "a SetByCaller modifier has no tag";
            if (m.Kind == MagnitudeKind.AttributeBased && !m.FromAttribute.IsValid) return "an attribute-based modifier has no attribute";
            if (!float.IsFinite(m.Value) || !float.IsFinite(m.Add)) return "a modifier magnitude is not finite";
        }
        if (d.Policy == EffectPolicy.Instant && d.GrantedTags.Count > 0)
            Log.Warn($"[GAS] effect '{d.Name}' is instant, so its granted tags are ignored");
        return null;
    }

    /// <summary>Forgets every definition (ids restart at 1). Tags and attribute names stay interned.</summary>
    public static void Clear()
    {
        s_effects.Clear();
        s_effectList.Clear();
        s_abilities.Clear();
        s_abilityList.Clear();
        s_sets.Clear();
        s_actors.Clear();
        s_sources.Clear();
    }

    // ---- builders -------------------------------------------------------------------------------

    /// <summary>Starts a fluent effect definition: <c>GameplayDefs.Effect("Effect.Heal").Instant().Modify("Health", ModOp.Add, 25f).Register()</c>.</summary>
    public static EffectBuilder Effect(string name) => new EffectBuilder(name);
    /// <summary>Starts a fluent ability definition.</summary>
    public static AbilityBuilder Ability(string name) => new AbilityBuilder(name);
    /// <summary>Starts a fluent attribute set definition.</summary>
    public static AttributeSetBuilder AttributeSet(string name) => new AttributeSetBuilder(name);

    // ---- loading --------------------------------------------------------------------------------

    /// <summary>Loads one .ocgas text. Problems come back as <c>source:line: message</c> lines in <paramref name="error"/>;
    /// a file with a problem contributes nothing. References are resolved against what is already registered.</summary>
    public static bool LoadText(string text, string sourceName, out string error) =>
        LoadTexts(new[] { (sourceName, text) }, out error);

    /// <summary>Loads several .ocgas texts as one set, so references may point forward across files. A file that fails to parse
    /// or link is refused whole (the others still load).</summary>
    public static bool LoadTexts(IEnumerable<(string Name, string Text)> files, out string error)
    {
        var errors = new List<string>();
        var parsed = new List<GasFileParse>();
        foreach ((string name, string text) in files)
        {
            GasFileParse? f = OcGasParser.Parse(text, name, out string err);
            if (f == null) errors.Add(err); else parsed.Add(f);
        }

        // A file with an undefined reference, a duplicate or a cycle is dropped, then the rest are checked again
        // without it (a file that leaned on a dropped one fails in the next round).
        while (true)
        {
            GasEnv env = GasEnv.Build(parsed);
            var bad = new List<GasFileParse>();
            foreach (GasFileParse f in parsed)
            {
                List<string> problems = GasLink.Validate(f, env);
                if (problems.Count == 0) continue;
                errors.AddRange(problems);
                bad.Add(f);
            }
            if (bad.Count == 0) break;
            foreach (GasFileParse f in bad) parsed.Remove(f);
        }

        foreach (GasFileParse f in parsed) BuildFirstPass(f);
        foreach (GasFileParse f in parsed) BuildSecondPass(f);
        error = string.Join("\n", errors);
        return errors.Count == 0;
    }

    /// <summary>Loads every <c>*.ocgas</c> under <paramref name="contentRoot"/> (recursive, files sorted ordinally) as one set.
    /// A missing folder is not an error.</summary>
    public static bool LoadDirectory(string contentRoot, out string error)
    {
        error = "";
        if (string.IsNullOrEmpty(contentRoot) || !Directory.Exists(contentRoot)) return true;
        string root = Path.GetFullPath(contentRoot).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string label = Path.GetFileName(root);
        var texts = new List<(string Name, string Text)>();
        var problems = new List<string>();
        var paths = new List<string>(Directory.EnumerateFiles(root, "*.ocgas", SearchOption.AllDirectories));
        var relative = new List<(string Rel, string Path)>();
        foreach (string p in paths)
            relative.Add((Path.GetRelativePath(root, p).Replace('\\', '/'), p));
        relative.Sort((a, b) => string.CompareOrdinal(a.Rel, b.Rel));
        foreach ((string rel, string path) in relative)
        {
            string name = label.Length > 0 ? label + "/" + rel : rel;
            try { texts.Add((name, File.ReadAllText(path))); }
            catch (Exception ex) { problems.Add($"{name}: could not be read: {ex.Message}"); }
        }
        bool ok = LoadTexts(texts, out string linkErrors);
        if (linkErrors.Length > 0) problems.Add(linkErrors);
        error = string.Join("\n", problems);
        return ok && problems.Count == 0;
    }

    // ---- building defs from linked records ------------------------------------------------------

    private static GameplayTagContainer Tags(IEnumerable<string> names)
    {
        var c = new GameplayTagContainer();
        foreach (string n in names)
        {
            GameplayTag t = GameplayTag.Request(n);
            if (t.IsValid) c.Add(t);
        }
        return c;
    }

    private static void BuildFirstPass(GasFileParse f)
    {
        foreach (var tag in f.Tags) GameplayTag.Request(tag.Name);

        foreach (SetRec s in f.Sets)
        {
            var attrs = new List<AttributeDef>();
            foreach (AttrRec a in s.Attrs)
            {
                var d = new AttributeDef
                {
                    Name = a.Name,
                    DefaultBase = a.Base,
                    Min = a.Min,
                    Max = a.Max,
                    IsMeta = a.Meta,
                    ForwardScale = a.Scale,
                };
                AttributeRef.Of(a.Name);
                if (a.MinFrom != null) d.MinFrom = AttributeRef.Of(a.MinFrom);
                if (a.MaxFrom != null) d.MaxFrom = AttributeRef.Of(a.MaxFrom);
                if (a.Forward != null) d.ForwardTo = AttributeRef.Of(a.Forward);
                attrs.Add(d);
            }
            TryRegister(new AttributeSetDef(s.Name, attrs), f.Source);
        }

        foreach (EffectRec r in f.Effects)
        {
            var mods = new List<ModifierDef>();
            foreach (ModRec m in r.Mods)
            {
                var md = new ModifierDef { Attribute = AttributeRef.Of(m.Attr), Op = m.Op, Kind = m.Kind, Value = m.Value, Add = m.Add, FromSource = m.FromSource };
                if (m.CallerTag != null) md.CallerTag = GameplayTag.Request(m.CallerTag);
                if (m.FromAttr != null) md.FromAttribute = AttributeRef.Of(m.FromAttr);
                mods.Add(md);
            }
            var d = new EffectDef
            {
                Name = r.Name,
                Policy = r.Policy,
                Duration = r.Duration,
                Period = r.Period,
                ExecuteOnApply = r.ExecuteOnApply,
                Stack = r.Stack,
                StackLimit = r.StackLimit,
                StackRefresh = r.StackRefresh,
                StackExpire = r.StackExpire,
                Modifiers = mods,
                AssetTags = Tags(r.Asset),
                GrantedTags = Tags(r.Grants),
                AppRequired = Tags(r.Requires),
                AppBlocked = Tags(r.BlockedBy),
                OngoingRequired = Tags(r.OngoingRequires),
                OngoingBlocked = Tags(r.OngoingBlockedBy),
                RemovesWithTags = Tags(r.Removes),
            };
            if (r.Cue != null) d.Cue = GameplayTag.Request(r.Cue);
            TryRegister(d, f.Source);
        }
    }

    private static void BuildSecondPass(GasFileParse f)
    {
        foreach (AbilityRec r in f.Abilities)
        {
            var d = new AbilityDef
            {
                Name = r.Name,
                AbilityTags = Tags(r.Tags),
                CancelsTags = Tags(r.Cancels),
                BlocksTags = Tags(r.Blocks),
                OwnedTags = Tags(r.Owns),
                RequiredTags = Tags(r.Requires),
                BlockedTags = Tags(r.BlockedBy),
                TriggerEvents = Tags(r.TriggerEvents),
                Commit = r.Commit,
                Duration = r.Duration,
                TriggerOnGranted = r.TriggerGranted,
                Retrigger = r.Retrigger,
                Behaviour = r.Behaviour,
            };
            if (r.Cost != null) d.Cost = FindEffect(r.Cost);
            if (r.Cooldown != null) d.Cooldown = FindEffect(r.Cooldown);
            TryRegister(d, f.Source);
        }

        foreach (ActorRec r in f.Actors)
        {
            var d = new ActorDefaultsDef { ClassName = r.Name };
            foreach (var x in r.Sets) d.AttributeSets.Add(x.Name);
            foreach (var x in r.Attrs) d.BaseOverrides.Add(new KeyValuePair<string, float>(x.Name, x.Value));
            foreach (var x in r.Grants) d.Grants.Add(new KeyValuePair<string, int>(x.Name, x.Level));
            foreach (var x in r.Applies) d.Applies.Add(x.Name);
            d.Tags.AddRange(r.Tags);
            foreach (string t in r.Tags) GameplayTag.Request(t);
            s_actors[r.Name] = d;
            s_sources["actor:" + r.Name] = f.Source;
        }
    }

    /// <summary>Merges class defaults from code into the file-defined ones (the code entry wins on conflicts).</summary>
    internal static void SetActorDefaults(ActorDefaultsDef def)
    {
        s_actors[def.ClassName] = def;
        s_sources["actor:" + def.ClassName] = "code";
    }
}
