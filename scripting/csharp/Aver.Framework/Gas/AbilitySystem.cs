// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: the per-entity AbilitySystem (attributes, tags, effects, abilities). See docs/GAMEPLAY_ABILITIES.md.

using System.Text;
using Aver.Scripting;

namespace Aver.Framework;

/// <summary>Notification batching shared by every system. A mutating public call opens a batch; the
/// notifications it queued fire, in order, when the outermost batch closes, so a handler never sees a
/// half-applied effect. A handler that mutates starts a fresh batch (a nested flush); nesting deeper
/// than <see cref="MaxNesting"/> drops that batch's notifications with one warning.</summary>
internal static class GasBatch
{
    internal const int MaxNesting = 8;

    private static int s_depth;
    private static int s_flushLevel;
    private static List<Action> s_pending = new();
    private static bool s_warned;

    internal static void Begin() => s_depth++;

    internal static void Enqueue(Action a) => s_pending.Add(a);

    internal static void End()
    {
        if (--s_depth > 0) return;
        s_depth = 0;
        if (s_pending.Count == 0) return;
        if (s_flushLevel >= MaxNesting)
        {
            s_pending.Clear();
            if (!s_warned)
            {
                s_warned = true;
                Log.Warn($"[GAS] notification handlers re-entered more than {MaxNesting} deep; further notifications dropped");
            }
            return;
        }
        List<Action> batch = s_pending;
        s_pending = new List<Action>();
        s_flushLevel++;
        try
        {
            foreach (Action a in batch)
            {
                try { a(); }
                catch (Exception ex) { Log.Error($"[GAS] notification handler threw: {ex.GetType().Name}: {ex.Message}"); }
            }
        }
        finally { s_flushLevel--; }
    }

    internal static void Reset()
    {
        s_depth = 0;
        s_flushLevel = 0;
        s_pending.Clear();
        s_warned = false;
    }
}

/// <summary>The gameplay ability component of one entity: attributes with modifiers, counted tags, active
/// effects and granted abilities. Single game thread only. Every mutating call is authoritative: on a
/// system with <see cref="HasAuthority"/> false it returns its failure value instead of acting.</summary>
public sealed partial class AbilitySystem
{
    internal const int MaxEffects = 256;
    internal const int MaxAbilities = 64;
    internal const int MaxLooseTags = 128;
    internal const int MaxPeriodsPerUpdate = 8;
    private const float Eps = 1e-6f;

    // ---- registry -------------------------------------------------------------------------------

    private static readonly Dictionary<int, AbilitySystem> s_byEntity = new();
    private static readonly List<AbilitySystem> s_all = new();
    private static readonly HashSet<string> s_logged = new(StringComparer.Ordinal);

    internal static IReadOnlyList<AbilitySystem> All => s_all;

    internal static void ClearAll()
    {
        foreach (AbilitySystem s in s_all) s._dead = true;
        s_all.Clear();
        s_byEntity.Clear();
        s_logged.Clear();
    }

    internal static void LogOnce(string key, string message)
    {
        if (s_logged.Add(key)) Log.Warn(message);
    }

    /// <summary>The entity's system, or null if it has none.</summary>
    public static AbilitySystem? Of(Entity e) => e.Handle != 0 && s_byEntity.TryGetValue(e.Handle, out AbilitySystem? s) ? s : null;

    /// <summary>The entity's system, created if absent.</summary>
    public static AbilitySystem GetOrAdd(Entity e)
    {
        if (e.Handle == 0) throw new ArgumentException("an invalid entity cannot carry an ability system", nameof(e));
        if (s_byEntity.TryGetValue(e.Handle, out AbilitySystem? s)) return s;
        s = new AbilitySystem(e.Handle);
        s_byEntity[e.Handle] = s;
        s_all.Add(s);
        return s;
    }

    /// <summary>Drops the entity's system without raising any notification. False if it had none.</summary>
    public static bool Remove(Entity e) => RemoveByHandle(e.Handle);

    internal static bool RemoveByHandle(int handle)
    {
        if (!s_byEntity.Remove(handle, out AbilitySystem? s)) return false;
        s_all.Remove(s);
        s._dead = true;
        return true;
    }

    // ---- state ----------------------------------------------------------------------------------

    private sealed class ModEntry
    {
        public ActiveEffect Eff = null!;
        public ModOp Op;
        public float Mag;
    }

    private sealed class AttrState
    {
        public AttributeRef Ref;
        public AttributeDef Def = null!;
        public string SetName = "";
        public float Base;
        public float Current;
        public float RepBase;                    // last Base reported to the replication sink
        public readonly List<ModEntry> Mods = new();
    }

    internal sealed class ActiveEffect
    {
        public int Handle;
        public EffectDef Def = null!;
        public int SourceHandle;
        public int Stacks = 1;
        public double Start;
        public double ExpiresAt = double.PositiveInfinity;
        public double NextPeriod = double.PositiveInfinity;
        public long PeriodIndex;
        public float[] Mags = Array.Empty<float>();
        public float Duration;
        public bool Inhibited;
        public bool Engaged;
        public bool HasOngoing;
        public long Seq;
    }

    private sealed class Granted
    {
        public int Handle;
        public AbilityDef Def = null!;
        public int Level;
        public GameplayAbility? Behaviour;
        public bool Active;
        public bool Committed;
        public Entity Target;
        public double AutoEndAt = double.PositiveInfinity;
    }

    private enum WorkKind { Period = 0, Expire = 1, AbilityEnd = 2 }

    private readonly record struct Work(WorkKind Kind, int Handle, double Due);

    private readonly int _owner;
    private bool _dead;
    private int _silent;
    private readonly GameplayTagContainer _tags = new() { Locked = true };       // loose + granted + owned
    private readonly GameplayTagContainer _loose = new() { Locked = true };
    private readonly GameplayTagContainer _blocking = new() { Locked = true };   // BlocksTags of running abilities
    private readonly List<TagFlip> _flips = new();
    private readonly List<AttrState> _attrs = new();
    private readonly Dictionary<int, AttrState> _attrById = new();
    private readonly List<string> _setNames = new();
    private readonly List<ActiveEffect> _effects = new();
    private readonly Dictionary<int, ActiveEffect> _effectByHandle = new();
    private readonly List<Granted> _granted = new();
    private readonly PriorityQueue<Work, (double Due, int Kind, long Seq)> _queue = new();
    private readonly Dictionary<int, int> _execCounts = new();
    private int _nextEffect = 1;
    private int _nextAbility = 1;
    private long _seq;
    private long _effectSeq;
    private int _ongoingCount;
    private bool _reeval;
    private bool _reevalDirty;

    private AbilitySystem(int owner) => _owner = owner;

    /// <summary>The entity this system belongs to.</summary>
    public Entity Owner => new Entity(_owner);
    internal int OwnerHandle => _owner;
    internal bool IsDead => _dead;

    /// <summary>False on a client proxy: every mutating call then refuses (see docs/GAMEPLAY_ABILITIES.md, replication seam).</summary>
    public bool HasAuthority { get; set; } = true;

    private static double Now => GameplayAbilities.Host.Now;

    /// <summary>Raised after an attribute's Current value changes.</summary>
    public event Action<AttributeChange>? AttributeChanged;
    /// <summary>Raised when a tag (or an ancestor of a tag) becomes present or absent: tag, present, count.</summary>
    public event Action<GameplayTag, bool, int>? TagChanged;
    /// <summary>Raised when an effect is applied (instant effects report an invalid handle).</summary>
    public event Action<ActiveEffectInfo>? EffectApplied;
    /// <summary>Raised when an effect is removed or expires.</summary>
    public event Action<ActiveEffectInfo>? EffectRemoved;
    /// <summary>Raised when an ability starts (the flag is always false).</summary>
    public event Action<AbilityHandle, bool>? AbilityActivated;
    /// <summary>Raised when an ability ends; the flag is true if it was cancelled.</summary>
    public event Action<AbilityHandle, bool>? AbilityEnded;
    /// <summary>Lets code adjust an attribute's new Current value before it is stored.</summary>
    public PreAttributeChange? PreAttributeChange;

    internal void DropCallbacks()
    {
        AttributeChanged = null;
        TagChanged = null;
        EffectApplied = null;
        EffectRemoved = null;
        AbilityActivated = null;
        AbilityEnded = null;
        PreAttributeChange = null;
        foreach (Granted g in _granted) g.Behaviour = null;
    }

    // ---- notification plumbing ------------------------------------------------------------------

    private void Notify(Action a)
    {
        if (_silent > 0) return;
        GasBatch.Enqueue(a);
    }

    // A behaviour hook: queued like a notification, but run at once while restoring (no events then).
    private void NotifyHook(Action a)
    {
        if (_silent > 0) { SafeHook(a); return; }
        GasBatch.Enqueue(() => SafeHook(a));
    }

    private static void SafeHook(Action a)
    {
        try { a(); }
        catch (Exception ex) { Log.Error($"[GAS] ability behaviour threw: {ex.GetType().Name}: {ex.Message}"); }
    }

    private static void Fire<T>(Action<T>? ev, T arg)
    {
        if (ev == null) return;
        foreach (Delegate d in ev.GetInvocationList())
        {
            try { ((Action<T>)d)(arg); }
            catch (Exception ex) { Log.Error($"[GAS] event handler threw: {ex.GetType().Name}: {ex.Message}"); }
        }
    }

    private static void Fire<T1, T2>(Action<T1, T2>? ev, T1 a, T2 b)
    {
        if (ev == null) return;
        foreach (Delegate d in ev.GetInvocationList())
        {
            try { ((Action<T1, T2>)d)(a, b); }
            catch (Exception ex) { Log.Error($"[GAS] event handler threw: {ex.GetType().Name}: {ex.Message}"); }
        }
    }

    private static void Fire<T1, T2, T3>(Action<T1, T2, T3>? ev, T1 a, T2 b, T3 c)
    {
        if (ev == null) return;
        foreach (Delegate d in ev.GetInvocationList())
        {
            try { ((Action<T1, T2, T3>)d)(a, b, c); }
            catch (Exception ex) { Log.Error($"[GAS] event handler threw: {ex.GetType().Name}: {ex.Message}"); }
        }
    }

    // Raises the generic and the specific bus event, each only when something listens.
    private void Bus(string generic, string specific, int sender, params object[] args)
    {
        IGasHost host = GameplayAbilities.Host;
        try
        {
            if (host.ListenerCount(generic) > 0) host.Raise(generic, sender, _owner, args);
            string full = generic + "." + specific;
            if (host.ListenerCount(full) > 0) host.Raise(full, sender, _owner, args);
        }
        catch (Exception ex)
        {
            Log.Error($"[GAS] raising {generic} threw: {ex.GetType().Name}: {ex.Message}");
        }
    }

    // ---- attributes -----------------------------------------------------------------------------

    /// <summary>Adds the registered set called <paramref name="name"/>. False if unknown, refused or not authoritative.</summary>
    public bool AddAttributeSet(string name)
    {
        AttributeSetDef? def = GameplayDefs.FindAttributeSet(name);
        if (def == null)
        {
            Log.Warn($"[GAS] unknown attribute set '{name}'");
            return false;
        }
        return AddAttributeSet(def);
    }

    /// <summary>Adds a set. Refused (logged) when an attribute name already exists on this entity or the bounds form a cycle.
    /// Adding a set that is already present succeeds without effect.</summary>
    public bool AddAttributeSet(AttributeSetDef def)
    {
        ArgumentNullException.ThrowIfNull(def);
        if (!HasAuthority) return false;
        if (_setNames.Contains(def.Name)) return true;

        var seen = new HashSet<int>();
        foreach (AttributeDef a in def.Attributes)
        {
            AttributeRef r = AttributeRef.Of(a.Name);
            if (!r.IsValid || !seen.Add(r.Id))
            {
                Log.Error($"[GAS] attribute set '{def.Name}' has an invalid or repeated attribute '{a.Name}'");
                return false;
            }
            if (_attrById.ContainsKey(r.Id))
            {
                Log.Error($"[GAS] attribute '{a.Name}' of set '{def.Name}' already exists on entity {_owner}");
                return false;
            }
        }
        string? cycle = AttributeBounds.FindCycle(_attrs.Select(s => s.Def).Concat(def.Attributes));
        if (cycle != null)
        {
            Log.Error($"[GAS] attribute set '{def.Name}' has cyclic bounds: {cycle}");
            return false;
        }

        GasBatch.Begin();
        _silent++;
        try
        {
            foreach (AttributeDef a in def.Attributes)
            {
                float start = a.IsMeta ? 0f : a.DefaultBase;
                var st = new AttrState { Ref = AttributeRef.Of(a.Name), Def = a, SetName = def.Name, Base = start, Current = start, RepBase = start };
                _attrs.Add(st);
                _attrById[st.Ref.Id] = st;
            }
            _setNames.Add(def.Name);
            // Twice, so a bound declared later in the set (or in another set) is seen.
            for (int pass = 0; pass < 2; ++pass)
                foreach (AttrState s in _attrs.ToArray()) RefreshOne(s, default, 0);
        }
        finally
        {
            _silent--;
            GasBatch.End();
        }
        return true;
    }

    /// <summary>True if the entity has this attribute.</summary>
    public bool Has(AttributeRef a) => a.IsValid && _attrById.ContainsKey(a.Id);

    /// <summary>The attribute's Current value (Base plus active modifiers); 0 if absent or a meta attribute.</summary>
    public float GetValue(AttributeRef a) => a.IsValid && _attrById.TryGetValue(a.Id, out AttrState? s) ? s.Current : 0f;

    /// <summary>The attribute's permanent Base value; 0 if absent.</summary>
    public float GetBase(AttributeRef a) => a.IsValid && _attrById.TryGetValue(a.Id, out AttrState? s) ? s.Base : 0f;

    /// <summary>Every attribute on this entity, in the order the sets were added.</summary>
    public IEnumerable<AttributeRef> Attributes => _attrs.Select(s => s.Ref).ToList();

    /// <summary>Sets an attribute's Base (clamped to its bounds). False if absent, meta or not authoritative.</summary>
    public bool SetBase(AttributeRef a, float value, Entity instigator = default)
    {
        if (!HasAuthority) return false;
        if (!a.IsValid || !_attrById.TryGetValue(a.Id, out AttrState? st) || st.Def.IsMeta || float.IsNaN(value)) return false;
        GasBatch.Begin();
        try
        {
            st.Base = value;
            RefreshOne(st, instigator, 0);
        }
        finally { GasBatch.End(); }
        return true;
    }

    private float MinOf(AttrState a) =>
        a.Def.MinFrom.IsValid && _attrById.TryGetValue(a.Def.MinFrom.Id, out AttrState? m) ? m.Current : a.Def.Min;

    private float MaxOf(AttrState a) =>
        a.Def.MaxFrom.IsValid && _attrById.TryGetValue(a.Def.MaxFrom.Id, out AttrState? m) ? m.Current : a.Def.Max;

    private static float ClampTo(float v, float lo, float hi)
    {
        if (v > hi) v = hi;
        if (v < lo) v = lo;
        return v;
    }

    // Base, plus the aggregated modifiers, clamped: Add sum, Multiply product, newest Override wins.
    private static float Evaluate(AttrState a, float lo, float hi)
    {
        float v = a.Base;
        if (a.Mods.Count > 0)
        {
            float add = 0f, prod = 1f;
            ModEntry? ov = null;
            foreach (ModEntry m in a.Mods)
            {
                int stacks = m.Eff.Stacks;
                switch (m.Op)
                {
                    case ModOp.Add: add += m.Mag * stacks; break;
                    case ModOp.Multiply: prod *= 1f + (m.Mag - 1f) * stacks; break;
                    default:
                        if (ov == null || m.Eff.Seq >= ov.Eff.Seq) ov = m;
                        break;
                }
            }
            v += add;
            v *= prod;
            if (ov != null) v = ov.Mag;
        }
        return ClampTo(v, lo, hi);
    }

    // Re-clamps Base, recomputes Current, announces a change and refreshes attributes bounded by this one.
    private void RefreshOne(AttrState a, Entity instigator, int depth)
    {
        if (a.Def.IsMeta) return;
        float lo = MinOf(a), hi = MaxOf(a);
        a.Base = ClampTo(a.Base, lo, hi);
        float v = Evaluate(a, lo, hi);
        PreAttributeChange? pre = PreAttributeChange;
        if (pre != null)
        {
            try { pre(this, a.Ref, ref v); }
            catch (Exception ex) { Log.Error($"[GAS] PreAttributeChange threw: {ex.GetType().Name}: {ex.Message}"); }
        }
        float old = a.Current;
        a.Current = v;
        if (a.Base != a.RepBase)
        {
            a.RepBase = a.Base;
            ReplicateBase(a);
        }
        if (Math.Abs(v - old) > Eps)
        {
            QueueAttributeChange(a.Ref, old, v, instigator);
            if (depth < 8)
                foreach (AttrState d in _attrs)
                    if (d.Def.MinFrom == a.Ref || d.Def.MaxFrom == a.Ref) RefreshOne(d, instigator, depth + 1);
        }
    }

    private void QueueAttributeChange(AttributeRef attr, float oldValue, float newValue, Entity instigator)
    {
        if (_silent > 0) return;
        var ch = new AttributeChange(this, attr, oldValue, newValue, instigator);
        Notify(() =>
        {
            Fire(AttributeChanged, ch);
            Bus("GAS.AttributeChanged", attr.Name, instigator.Handle, attr.Id, newValue, oldValue);
        });
    }

    // An instant operation on Base (instant effects and periodic executions). A meta attribute forwards or announces.
    private void ApplyBaseMod(AttributeRef attr, ModOp op, float mag, int instigator, int depth)
    {
        if (!attr.IsValid || !_attrById.TryGetValue(attr.Id, out AttrState? a))
        {
            LogOnce($"noattr:{_owner}:{attr.Id}", $"[GAS] entity {_owner} has no attribute '{attr.Name}'; modifier skipped");
            return;
        }
        var inst = new Entity(instigator);
        if (a.Def.IsMeta)
        {
            float m = op == ModOp.Multiply ? 0f : mag;
            if (m == 0f) return;
            AttributeRef fwd = a.Def.ForwardTo;
            if (fwd.IsValid)
            {
                if (depth < 4) ApplyBaseMod(fwd, ModOp.Add, m * a.Def.ForwardScale, instigator, depth + 1);
            }
            else QueueAttributeChange(a.Ref, 0f, m, inst);
            return;
        }
        switch (op)
        {
            case ModOp.Add: a.Base += mag; break;
            case ModOp.Multiply: a.Base *= mag; break;
            default: a.Base = mag; break;
        }
        RefreshOne(a, inst, 0);
    }

    // ---- tags -----------------------------------------------------------------------------------

    /// <summary>The effective tags (loose, granted by effects and owned by running abilities) as a read-only view.</summary>
    public GameplayTagContainer Tags => _tags;

    /// <summary>True if a tag matching <paramref name="t"/> is held (hierarchical).</summary>
    public bool HasTag(GameplayTag t) => _tags.Has(t);

    /// <summary>References held to <paramref name="t"/> and its descendants.</summary>
    public int TagCount(GameplayTag t) => _tags.ImpliedCount(t);

    /// <summary>Adds loose references to a tag (not tied to any effect).</summary>
    public void AddLooseTag(GameplayTag t, int count = 1)
    {
        if (!HasAuthority || !t.IsValid || count <= 0) return;
        if (!_loose.HasExact(t) && _loose.Count >= MaxLooseTags)
        {
            LogOnce($"looselimit:{_owner}", $"[GAS] entity {_owner} reached {MaxLooseTags} loose tags; '{t.Name}' not added");
            return;
        }
        GasBatch.Begin();
        try
        {
            _loose.MutateInternal(t, count, null);
            MutateTags(t, count);
            ReplicateLoose(t);
        }
        finally { GasBatch.End(); }
    }

    /// <summary>Removes loose references (never more than were added loosely).</summary>
    public void RemoveLooseTag(GameplayTag t, int count = 1)
    {
        if (!HasAuthority || !t.IsValid || count <= 0) return;
        int n = Math.Min(count, _loose.CountOf(t));
        if (n <= 0) return;
        GasBatch.Begin();
        try
        {
            _loose.MutateInternal(t, -n, null);
            MutateTags(t, -n);
            ReplicateLoose(t);
        }
        finally { GasBatch.End(); }
    }

    // Changes the effective tag counts, queues one TagChanged per presence flip, then re-checks inhibition.
    private void MutateTags(GameplayTag t, int delta)
    {
        _flips.Clear();
        _tags.MutateInternal(t, delta, _flips);
        if (_flips.Count == 0) return;
        foreach (TagFlip f in _flips)
        {
            TagFlip flip = f;
            Notify(() =>
            {
                Fire(TagChanged, flip.Tag, flip.Present, flip.Count);
                Bus("GAS.TagChanged", flip.Tag.Name, 0, flip.Tag.Id, flip.Present ? 1 : 0, flip.Count);
            });
        }
        TagsChanged();
    }

    private bool ShouldInhibit(ActiveEffect e) =>
        !_tags.HasAll(e.Def.OngoingRequired) || _tags.HasAny(e.Def.OngoingBlocked);

    // Inhibits or releases effects whose ongoing requirements changed; loops until the tag set settles.
    private void TagsChanged()
    {
        if (_ongoingCount == 0) return;
        if (_reeval) { _reevalDirty = true; return; }
        _reeval = true;
        try
        {
            for (int guard = 0; guard < 16; ++guard)
            {
                _reevalDirty = false;
                foreach (ActiveEffect e in _effects.ToArray())
                {
                    if (!e.HasOngoing || !_effectByHandle.ContainsKey(e.Handle)) continue;
                    bool inhibit = ShouldInhibit(e);
                    if (inhibit == e.Inhibited) continue;
                    e.Inhibited = inhibit;
                    if (inhibit) Disengage(e); else Engage(e);
                }
                if (!_reevalDirty) break;
            }
        }
        finally { _reeval = false; }
    }

    // ---- effects --------------------------------------------------------------------------------

    private ActiveEffectInfo InfoOf(ActiveEffect e)
    {
        float remaining = double.IsPositiveInfinity(e.ExpiresAt) ? float.PositiveInfinity : (float)Math.Max(0.0, e.ExpiresAt - Now);
        return new ActiveEffectInfo(new ActiveEffectHandle(e.Handle), e.Def, new Entity(e.SourceHandle), e.Stacks, remaining, e.Inhibited);
    }

    // One modifier's magnitude. SetByCaller reads the spec (missing = 0, logged once); AttributeBased snapshots now.
    private float ResolveMagnitude(ModifierDef m, EffectSpec? spec, EffectDef def, int source)
    {
        switch (m.Kind)
        {
            case MagnitudeKind.SetByCaller:
            {
                float v = 0f;
                if (spec == null || spec.Caller == null || !spec.Caller.TryGetValue(m.CallerTag.Id, out v))
                {
                    v = 0f;
                    if (spec != null)
                        LogOnce($"nocaller:{def.Name}:{m.CallerTag.Id}", $"[GAS] effect '{def.Name}' needs a SetByCaller value for '{m.CallerTag.Name}'; using 0");
                }
                return v * m.Value;
            }
            case MagnitudeKind.AttributeBased:
            {
                AbilitySystem? from = this;
                if (m.FromSource && source != 0) from = Of(new Entity(source));
                float v = from != null && from.Has(m.FromAttribute) ? from.GetValue(m.FromAttribute) : 0f;
                return v * m.Value + m.Add;
            }
            default:
                return m.Value;
        }
    }

    private float[] ResolveMagnitudes(EffectDef def, EffectSpec? spec, int source)
    {
        IReadOnlyList<ModifierDef> mods = def.Modifiers;
        var mags = new float[mods.Count];
        for (int i = 0; i < mags.Length; ++i) mags[i] = ResolveMagnitude(mods[i], spec, def, source);
        return mags;
    }

    /// <summary>Applies an effect to this entity. Returns the active handle (invalid for an instant effect or a failure);
    /// <paramref name="failure"/> says why nothing was applied.</summary>
    public ActiveEffectHandle Apply(EffectSpec spec, out EffectFailure failure)
    {
        ArgumentNullException.ThrowIfNull(spec);
        failure = EffectFailure.None;
        if (!HasAuthority) { failure = EffectFailure.NoAuthority; return default; }
        EffectDef? def = spec.Resolve();
        if (def == null)
        {
            failure = EffectFailure.UnknownEffect;
            LogOnce($"noeffect:{spec.EffectName}", $"[GAS] no effect named '{spec.EffectName}'");
            return default;
        }
        if (_dead || !GameplayAbilities.Host.IsAlive(_owner)) { failure = EffectFailure.NoSystem; return default; }
        GasBatch.Begin();
        try { return ApplyCore(spec, def, out failure); }
        finally { GasBatch.End(); }
    }

    /// <summary>Applies an effect and discards the failure reason.</summary>
    public ActiveEffectHandle Apply(EffectSpec spec) => Apply(spec, out _);

    /// <summary>Applies an effect to <paramref name="target"/>, creating its system if needed. Invalid handle if the entity is not alive.</summary>
    public static ActiveEffectHandle ApplyTo(Entity target, EffectSpec spec)
    {
        if (target.Handle == 0 || !GameplayAbilities.Host.IsAlive(target.Handle)) return default;
        return GetOrAdd(target).Apply(spec);
    }

    private ActiveEffectHandle ApplyCore(EffectSpec spec, EffectDef def, out EffectFailure failure)
    {
        failure = EffectFailure.None;
        if (!_tags.HasAll(def.AppRequired) || !_tags.HasNone(def.AppBlocked))
        {
            failure = EffectFailure.TagRequirement;
            return default;
        }
        int source = spec.SourceEntity.Handle;
        if (def.RemovesWithTags.Count > 0) RemoveMatching(def.RemovesWithTags, def);

        if (def.Policy == EffectPolicy.Instant)
        {
            float[] mags = ResolveMagnitudes(def, spec, source);
            for (int i = 0; i < mags.Length; ++i)
                ApplyBaseMod(def.Modifiers[i].Attribute, def.Modifiers[i].Op, mags[i], source, 0);
            var info = new ActiveEffectInfo(default, def, spec.SourceEntity, 1, 0f, false);
            QueueEffectEvent(true, info);
            QueueCue(def, source, 2);
            return default;
        }

        // Stacking: look for the instance a new application joins.
        if (def.Stack != StackScope.None)
        {
            ActiveEffect? existing = null;
            foreach (ActiveEffect x in _effects)
            {
                if (!ReferenceEquals(x.Def, def)) continue;
                if (def.Stack == StackScope.Source && x.SourceHandle != source) continue;
                existing = x;
                break;
            }
            if (existing != null)
            {
                bool full = existing.Stacks >= Math.Max(1, def.StackLimit);
                if (!full) existing.Stacks++;
                if (def.StackRefresh && !double.IsPositiveInfinity(existing.ExpiresAt))
                {
                    existing.ExpiresAt = Now + existing.Duration;
                    Schedule(WorkKind.Expire, existing.Handle, existing.ExpiresAt);
                }
                if (full) failure = EffectFailure.StackLimit;
                else
                {
                    RefreshEffectAttributes(existing);
                    QueueEffectEvent(true, InfoOf(existing));
                }
                ReplicateEffect(1, existing);
                return new ActiveEffectHandle(existing.Handle);
            }
        }

        if (_effects.Count >= MaxEffects)
        {
            failure = EffectFailure.LimitReached;
            LogOnce($"effectlimit:{_owner}", $"[GAS] entity {_owner} reached {MaxEffects} active effects; '{def.Name}' not applied");
            return default;
        }

        double now = Now;
        var e = new ActiveEffect
        {
            Handle = _nextEffect++,
            Def = def,
            SourceHandle = source,
            Start = now,
            Seq = ++_effectSeq,
            Mags = ResolveMagnitudes(def, spec, source),
            Duration = spec.DurationOverride ?? def.Duration,
            HasOngoing = def.OngoingRequired.Count > 0 || def.OngoingBlocked.Count > 0,
        };
        if (def.Policy == EffectPolicy.Duration) e.ExpiresAt = now + e.Duration;
        AddInstance(e);
        if (!double.IsPositiveInfinity(e.ExpiresAt)) Schedule(WorkKind.Expire, e.Handle, e.ExpiresAt);
        if (def.Period > 0f)
        {
            e.PeriodIndex = 1;
            e.NextPeriod = e.Start + def.Period;
            Schedule(WorkKind.Period, e.Handle, e.NextPeriod);
        }
        if (def.Period > 0f && def.ExecuteOnApply && !e.Inhibited) ExecutePeriodic(e);
        QueueEffectEvent(true, InfoOf(e));
        QueueCue(def, source, 1);
        ReplicateEffect(0, e);
        return new ActiveEffectHandle(e.Handle);
    }

    // Registers a new instance and engages it unless its ongoing requirements already inhibit it.
    internal void AddInstance(ActiveEffect e)
    {
        _effects.Add(e);
        _effectByHandle[e.Handle] = e;
        if (e.HasOngoing) _ongoingCount++;
        e.Inhibited = e.HasOngoing && ShouldInhibit(e);
        if (!e.Inhibited) Engage(e);
    }

    private void Schedule(WorkKind kind, int handle, double due) =>
        _queue.Enqueue(new Work(kind, handle, due), (due, (int)kind, ++_seq));

    // Adds the instance's modifiers to the aggregators and its granted tags to the system.
    private void Engage(ActiveEffect e)
    {
        e.Engaged = true;
        var touched = new List<AttrState>();
        IReadOnlyList<ModifierDef> mods = e.Def.Modifiers;
        // A periodic effect acts on Base at each execution; its modifiers never enter the aggregators.
        for (int i = 0; i < mods.Count && e.Def.Period <= 0f; ++i)
        {
            ModifierDef m = mods[i];
            if (!m.Attribute.IsValid || !_attrById.TryGetValue(m.Attribute.Id, out AttrState? a) || a.Def.IsMeta) continue;
            a.Mods.Add(new ModEntry { Eff = e, Op = m.Op, Mag = e.Mags[i] });
            if (!touched.Contains(a)) touched.Add(a);
        }
        foreach (GameplayTag t in e.Def.GrantedTags) MutateTags(t, 1);
        var src = new Entity(e.SourceHandle);
        foreach (AttrState a in touched) RefreshOne(a, src, 0);
    }

    private void Disengage(ActiveEffect e)
    {
        e.Engaged = false;
        var touched = new List<AttrState>();
        foreach (AttrState a in _attrs)
            if (a.Mods.RemoveAll(x => ReferenceEquals(x.Eff, e)) > 0) touched.Add(a);
        foreach (GameplayTag t in e.Def.GrantedTags) MutateTags(t, -1);
        var src = new Entity(e.SourceHandle);
        foreach (AttrState a in touched) RefreshOne(a, src, 0);
    }

    private void RefreshEffectAttributes(ActiveEffect e)
    {
        if (!e.Engaged) return;
        var src = new Entity(e.SourceHandle);
        foreach (ModifierDef m in e.Def.Modifiers)
            if (m.Attribute.IsValid && _attrById.TryGetValue(m.Attribute.Id, out AttrState? a)) RefreshOne(a, src, 0);
    }

    // One periodic execution: the modifiers act on Base, once per stack.
    private void ExecutePeriodic(ActiveEffect e)
    {
        IReadOnlyList<ModifierDef> mods = e.Def.Modifiers;
        for (int s = 0; s < e.Stacks; ++s)
            for (int i = 0; i < mods.Count; ++i)
                ApplyBaseMod(mods[i].Attribute, mods[i].Op, e.Mags[i], e.SourceHandle, 0);
        QueueCue(e.Def, e.SourceHandle, 2);
    }

    private void RemoveInstance(ActiveEffect e, bool notify)
    {
        if (!_effectByHandle.Remove(e.Handle)) return;
        _effects.Remove(e);
        if (e.HasOngoing) _ongoingCount--;
        ActiveEffectInfo info = InfoOf(e);
        if (e.Engaged) Disengage(e);
        if (notify)
        {
            QueueEffectEvent(false, info);
            QueueCue(e.Def, e.SourceHandle, 0);
            ReplicateEffect(2, e);
        }
    }

    private static bool EffectMatches(EffectDef d, GameplayTagContainer tags)
    {
        foreach (GameplayTag q in tags)
        {
            foreach (GameplayTag t in d.AssetTags) if (t.Matches(q)) return true;
            foreach (GameplayTag t in d.GrantedTags) if (t.Matches(q)) return true;
        }
        return false;
    }

    private int RemoveMatching(GameplayTagContainer tags, EffectDef? except)
    {
        var doomed = new List<ActiveEffect>();
        foreach (ActiveEffect e in _effects)
        {
            if (except != null && ReferenceEquals(e.Def, except)) continue;
            if (EffectMatches(e.Def, tags)) doomed.Add(e);
        }
        foreach (ActiveEffect e in doomed) RemoveInstance(e, true);
        return doomed.Count;
    }

    /// <summary>Removes one active effect. False if it was not active or the system is not authoritative.</summary>
    public bool Remove(ActiveEffectHandle h)
    {
        if (!HasAuthority || !_effectByHandle.TryGetValue(h.Value, out ActiveEffect? e)) return false;
        GasBatch.Begin();
        try { RemoveInstance(e, true); }
        finally { GasBatch.End(); }
        return true;
    }

    /// <summary>Removes every active effect whose asset or granted tags match <paramref name="t"/>. Returns the count.</summary>
    public int RemoveByTag(GameplayTag t)
    {
        if (!HasAuthority || !t.IsValid) return 0;
        GasBatch.Begin();
        try { return RemoveMatching(new GameplayTagContainer(t), null); }
        finally { GasBatch.End(); }
    }

    /// <summary>Removes every active instance of a definition. Returns the count.</summary>
    public int RemoveByDef(EffectDef d)
    {
        if (!HasAuthority) return 0;
        var doomed = _effects.Where(e => ReferenceEquals(e.Def, d)).ToList();
        if (doomed.Count == 0) return 0;
        GasBatch.Begin();
        try { foreach (ActiveEffect e in doomed) RemoveInstance(e, true); }
        finally { GasBatch.End(); }
        return doomed.Count;
    }

    /// <summary>Looks an active effect up by handle.</summary>
    public bool TryGetEffect(ActiveEffectHandle h, out ActiveEffectInfo info)
    {
        if (_effectByHandle.TryGetValue(h.Value, out ActiveEffect? e)) { info = InfoOf(e); return true; }
        info = default;
        return false;
    }

    /// <summary>The first active instance of a definition.</summary>
    public bool TryGetEffect(EffectDef d, out ActiveEffectInfo info)
    {
        foreach (ActiveEffect e in _effects)
            if (ReferenceEquals(e.Def, d)) { info = InfoOf(e); return true; }
        info = default;
        return false;
    }

    /// <summary>A snapshot of every active effect in application order.</summary>
    public IReadOnlyList<ActiveEffectInfo> ActiveEffects => _effects.Select(InfoOf).ToList();

    private void QueueEffectEvent(bool applied, ActiveEffectInfo info)
    {
        if (_silent > 0) return;
        Notify(() =>
        {
            Fire(applied ? EffectApplied : EffectRemoved, info);
            Bus(applied ? "GAS.EffectApplied" : "GAS.EffectRemoved", info.Def.Name, info.Source.Handle,
                info.Def.Id, info.Handle.Value, info.Stacks);
        });
    }

    // Cue phases: 0 removed, 1 added, 2 executed.
    private void QueueCue(EffectDef def, int source, int phase)
    {
        if (!def.Cue.IsValid || _silent > 0) return;
        GameplayTag cue = def.Cue;
        Notify(() => Bus("GAS.Cue", cue.Name, source, cue.Id, phase));
    }

    // ---- scheduled work -------------------------------------------------------------------------

    internal double NextDue => _queue.TryPeek(out _, out (double Due, int Kind, long Seq) p) ? p.Due : double.PositiveInfinity;

    // Moves the next periodic execution past `now`, dropping the executions in between.
    private void SkipPeriodsTo(ActiveEffect e, double now)
    {
        double period = e.Def.Period;
        long idx = (long)Math.Floor((now - e.Start) / period) + 1;
        if (idx <= e.PeriodIndex) idx = e.PeriodIndex + 1;
        e.PeriodIndex = idx;
        e.NextPeriod = e.Start + idx * period;
        Schedule(WorkKind.Period, e.Handle, e.NextPeriod);
    }

    // Runs expiry, periodic executions and ability auto-ends that fell due, in due order.
    internal void ProcessDue(double now)
    {
        if (NextDue > now) return;
        GasBatch.Begin();
        try
        {
            _execCounts.Clear();
            int guard = 0;
            while (guard++ < 100000 && _queue.TryPeek(out Work w, out (double Due, int Kind, long Seq) pr) && pr.Due <= now)
            {
                _queue.Dequeue();
                switch (w.Kind)
                {
                    case WorkKind.Period:
                    {
                        if (!_effectByHandle.TryGetValue(w.Handle, out ActiveEffect? e) || e.NextPeriod != w.Due) break;
                        if (e.Inhibited) { SkipPeriodsTo(e, now); break; }
                        _execCounts.TryGetValue(e.Handle, out int done);
                        if (done >= MaxPeriodsPerUpdate) { SkipPeriodsTo(e, now); break; }
                        _execCounts[e.Handle] = done + 1;
                        e.PeriodIndex++;
                        e.NextPeriod = e.Start + e.PeriodIndex * (double)e.Def.Period;
                        Schedule(WorkKind.Period, e.Handle, e.NextPeriod);
                        ExecutePeriodic(e);
                        break;
                    }
                    case WorkKind.Expire:
                    {
                        if (!_effectByHandle.TryGetValue(w.Handle, out ActiveEffect? e) || e.ExpiresAt != w.Due) break;
                        if (e.Def.StackExpire == StackExpire.RemoveOne && e.Stacks > 1)
                        {
                            e.Stacks--;
                            e.ExpiresAt = w.Due + e.Duration;
                            Schedule(WorkKind.Expire, e.Handle, e.ExpiresAt);
                            RefreshEffectAttributes(e);
                            ReplicateEffect(1, e);
                        }
                        else RemoveInstance(e, true);
                        break;
                    }
                    default:
                    {
                        Granted? g = FindGranted(w.Handle);
                        if (g != null && g.Active && g.AutoEndAt == w.Due) EndCore(g, false);
                        break;
                    }
                }
            }
        }
        finally { GasBatch.End(); }
    }

    // ---- abilities ------------------------------------------------------------------------------

    private Granted? FindGranted(int handle)
    {
        foreach (Granted g in _granted) if (g.Handle == handle) return g;
        return null;
    }

    private Granted? FindGranted(AbilityHandle h) => h.IsValid ? FindGranted(h.Value) : null;

    /// <summary>Grants a registered ability (idempotent per ability). Invalid handle if unknown, full or not authoritative.</summary>
    public AbilityHandle Grant(string abilityName, int level = 1)
    {
        AbilityDef? def = GameplayDefs.FindAbility(abilityName);
        if (def == null)
        {
            Log.Warn($"[GAS] no ability named '{abilityName}'");
            return default;
        }
        return Grant(def, level);
    }

    internal AbilityHandle Grant(AbilityDef def, int level)
    {
        if (!HasAuthority) return default;
        foreach (Granted x in _granted)
            if (ReferenceEquals(x.Def, def)) return new AbilityHandle(x.Handle);
        if (_granted.Count >= MaxAbilities)
        {
            LogOnce($"abilitylimit:{_owner}", $"[GAS] entity {_owner} reached {MaxAbilities} granted abilities; '{def.Name}' not granted");
            return default;
        }
        var g = new Granted { Handle = _nextAbility++, Def = def, Level = level };
        var handle = new AbilityHandle(g.Handle);
        GasBatch.Begin();
        try
        {
            _granted.Add(g);
            g.Behaviour = GameplayAbilities.CreateBehaviour(def, this, handle, level);
            ReplicateGrant(true, g);
            GameplayAbility? b = g.Behaviour;
            if (b != null) NotifyHook(() => b.OnGranted());
            if (def.TriggerOnGranted && !_restoring) TryActivate(handle);   // ability runs are not saved
        }
        finally { GasBatch.End(); }
        return handle;
    }

    /// <summary>Revokes an ability, ending it first if it is running.</summary>
    public bool Revoke(AbilityHandle h)
    {
        if (!HasAuthority) return false;
        Granted? g = FindGranted(h);
        if (g == null) return false;
        GasBatch.Begin();
        try
        {
            if (g.Active) EndCore(g, true);
            _granted.Remove(g);
            GameplayAbility? b = g.Behaviour;
            if (b != null) NotifyHook(() => b.OnRevoked());
            ReplicateGrant(false, g);
        }
        finally { GasBatch.End(); }
        return true;
    }

    /// <summary>The handle of a granted ability by name; invalid if not granted.</summary>
    public AbilityHandle Find(string abilityName)
    {
        foreach (Granted g in _granted)
            if (g.Def.Name == abilityName) return new AbilityHandle(g.Handle);
        return default;
    }

    internal AbilityDef? DefOf(AbilityHandle h) => FindGranted(h)?.Def;

    internal int LevelOf(AbilityHandle h) => FindGranted(h)?.Level ?? 0;

    /// <summary>True while the ability is running.</summary>
    public bool IsActive(AbilityHandle h) => FindGranted(h)?.Active ?? false;

    private static bool AbilityMatches(AbilityDef d, GameplayTagContainer tags)
    {
        foreach (GameplayTag q in tags)
            foreach (GameplayTag a in d.AbilityTags)
                if (a.Matches(q)) return true;
        return false;
    }

    private bool BlockedByRunning(AbilityDef d)
    {
        if (_blocking.Count == 0) return false;
        foreach (GameplayTag a in d.AbilityTags)
            foreach (GameplayTag b in _blocking)
                if (a.Matches(b)) return true;
        return false;
    }

    private static bool GrantsAny(EffectDef d, GameplayTagContainer tags)
    {
        foreach (GameplayTag t in d.GrantedTags)
            foreach (GameplayTag c in tags)
                if (t.Matches(c)) return true;
        return false;
    }

    // Unreal's default CheckCost: every cost modifier must leave its attribute's Base within the minimum.
    private bool CanAfford(EffectDef cost)
    {
        var sim = new Dictionary<int, float>();
        IReadOnlyList<ModifierDef> mods = cost.Modifiers;
        for (int i = 0; i < mods.Count; ++i)
        {
            ModifierDef m = mods[i];
            if (!m.Attribute.IsValid || !_attrById.TryGetValue(m.Attribute.Id, out AttrState? a)) return false;
            if (a.Def.IsMeta) continue;
            float mag = ResolveMagnitude(m, null, cost, _owner);
            float b = sim.TryGetValue(m.Attribute.Id, out float prev) ? prev : a.Base;
            b = m.Op switch { ModOp.Add => b + mag, ModOp.Multiply => b * mag, _ => mag };
            if (b < MinOf(a) - Eps) return false;
            sim[m.Attribute.Id] = b;
        }
        return true;
    }

    private bool OnCooldown(AbilityDef d) => d.Cooldown != null && _tags.HasAny(d.Cooldown.GrantedTags);

    // The documented check order. `retrigger` lets an already-running retriggerable ability through.
    private AbilityFailure Check(Granted g)
    {
        AbilityDef d = g.Def;
        if (g.Active && !d.Retrigger) return AbilityFailure.AlreadyActive;
        if (!_tags.HasAll(d.RequiredTags)) return AbilityFailure.MissingRequiredTag;
        if (_tags.HasAny(d.BlockedTags)) return AbilityFailure.BlockedByTag;
        if (BlockedByRunning(d)) return AbilityFailure.BlockedByAbility;
        if (OnCooldown(d)) return AbilityFailure.OnCooldown;
        if (d.Cost != null && !CanAfford(d.Cost)) return AbilityFailure.CannotAfford;
        if (g.Behaviour != null)
        {
            bool ok = true;
            try { ok = g.Behaviour.CanActivate(); }
            catch (Exception ex) { Log.Error($"[GAS] ability behaviour threw: {ex.GetType().Name}: {ex.Message}"); }
            if (!ok) return AbilityFailure.BehaviourRefused;
        }
        return AbilityFailure.None;
    }

    /// <summary>Whether the ability could activate now, without activating it.</summary>
    public AbilityFailure CanActivate(AbilityHandle h)
    {
        if (!HasAuthority) return AbilityFailure.NoAuthority;
        Granted? g = FindGranted(h);
        return g == null ? AbilityFailure.NotGranted : Check(g);
    }

    /// <summary>Activates an ability. Returns <see cref="AbilityFailure.None"/> on success, otherwise the first failing check
    /// (which is also raised as a <c>GAS.AbilityFailed</c> event).</summary>
    public AbilityFailure TryActivate(AbilityHandle h, Entity target = default)
    {
        if (!HasAuthority)
        {
            IGasClientRequests? cr = GameplayAbilities.ClientRequests;
            if (cr == null) return AbilityFailure.NoAuthority;
            cr.RequestActivate(this, h, target);
            return AbilityFailure.None;
        }
        Granted? g = FindGranted(h);
        if (g == null) return AbilityFailure.NotGranted;
        GasBatch.Begin();
        try
        {
            if (g.Active && g.Def.Retrigger) EndCore(g, true);
            AbilityFailure f = Check(g);
            if (f == AbilityFailure.None) f = ActivateCore(g, target);
            if (f != AbilityFailure.None)
            {
                AbilityDef d = g.Def;
                AbilityFailure why = f;
                Notify(() => Bus("GAS.AbilityFailed", d.Name, _owner, d.Id, (int)why));
            }
            return f;
        }
        finally { GasBatch.End(); }
    }

    private AbilityFailure ActivateCore(Granted g, Entity target)
    {
        AbilityDef d = g.Def;
        if (d.CancelsTags.Count > 0) CancelMatching(d.CancelsTags, g);
        foreach (GameplayTag t in d.OwnedTags) MutateTags(t, 1);
        foreach (GameplayTag t in d.BlocksTags) _blocking.MutateInternal(t, 1, null);
        g.Active = true;
        g.Committed = false;
        g.Target = target;
        g.AutoEndAt = double.PositiveInfinity;
        if (g.Behaviour != null) g.Behaviour.Target = target;

        if (d.Commit == AbilityCommit.Activate && !CommitCore(g))
        {
            EndCore(g, true);
            return AbilityFailure.CannotAfford;
        }

        var handle = new AbilityHandle(g.Handle);
        int level = g.Level;
        Notify(() =>
        {
            Fire(AbilityActivated, handle, false);
            Bus("GAS.AbilityActivated", d.Name, _owner, d.Id, level);
        });
        GameplayAbility? b = g.Behaviour;
        if (b != null) NotifyHook(() => b.OnActivate());
        ReplicateActivated(handle);

        if (g.Active && d.Duration > 0f)
        {
            g.AutoEndAt = Now + d.Duration;
            Schedule(WorkKind.AbilityEnd, g.Handle, g.AutoEndAt);
        }
        return AbilityFailure.None;
    }

    // Applies cost then cooldown as one batch; nothing is applied if the cost can no longer be paid.
    private bool CommitCore(Granted g)
    {
        AbilityDef d = g.Def;
        if (d.Cost != null && !CanAfford(d.Cost)) return false;
        var self = new Entity(_owner);
        if (d.Cost != null) Apply(EffectSpec.Of(d.Cost).From(self));
        if (d.Cooldown != null) Apply(EffectSpec.Of(d.Cooldown).From(self));
        g.Committed = true;
        return true;
    }

    /// <summary>Pays the running ability's cost and cooldown. False if it is not running, was already committed or cannot be paid.</summary>
    public bool Commit(AbilityHandle h)
    {
        if (!HasAuthority) return false;
        Granted? g = FindGranted(h);
        if (g == null || !g.Active || g.Committed) return false;
        GasBatch.Begin();
        try { return CommitCore(g); }
        finally { GasBatch.End(); }
    }

    /// <summary>Ends a running ability. False if it was not running.</summary>
    public bool End(AbilityHandle h, bool cancelled = false)
    {
        if (!HasAuthority) return false;
        Granted? g = FindGranted(h);
        if (g == null || !g.Active) return false;
        GasBatch.Begin();
        try { return EndCore(g, cancelled); }
        finally { GasBatch.End(); }
    }

    private bool EndCore(Granted g, bool cancelled)
    {
        if (!g.Active) return false;
        AbilityDef d = g.Def;
        g.Active = false;
        g.AutoEndAt = double.PositiveInfinity;
        foreach (GameplayTag t in d.OwnedTags) MutateTags(t, -1);
        foreach (GameplayTag t in d.BlocksTags) _blocking.MutateInternal(t, -1, null);
        var handle = new AbilityHandle(g.Handle);
        Notify(() =>
        {
            Fire(AbilityEnded, handle, cancelled);
            Bus("GAS.AbilityEnded", d.Name, _owner, d.Id, cancelled ? 1 : 0);
        });
        GameplayAbility? b = g.Behaviour;
        if (b != null) NotifyHook(() => b.OnEnd(cancelled));
        ReplicateEnded(handle, cancelled);
        return true;
    }

    private int CancelMatching(GameplayTagContainer tags, Granted? except)
    {
        var doomed = new List<Granted>();
        foreach (Granted g in _granted)
            if (g.Active && !ReferenceEquals(g, except) && AbilityMatches(g.Def, tags)) doomed.Add(g);
        foreach (Granted g in doomed) EndCore(g, true);
        return doomed.Count;
    }

    /// <summary>Cancels every running ability whose tags match <paramref name="tag"/>. Returns the count.</summary>
    public int CancelAbilities(GameplayTag tag)
    {
        if (!HasAuthority || !tag.IsValid) return 0;
        GasBatch.Begin();
        try { return CancelMatching(new GameplayTagContainer(tag), null); }
        finally { GasBatch.End(); }
    }

    /// <summary>Seconds until the ability's cooldown ends (0 if ready; +infinity for an endless cooldown effect).</summary>
    public float CooldownRemaining(AbilityHandle h)
    {
        Granted? g = FindGranted(h);
        if (g == null || g.Def.Cooldown == null) return 0f;
        GameplayTagContainer cd = g.Def.Cooldown.GrantedTags;
        double now = Now;
        float best = 0f;
        foreach (ActiveEffect e in _effects)
        {
            if (e.Inhibited || !GrantsAny(e.Def, cd)) continue;
            float rem = double.IsPositiveInfinity(e.ExpiresAt) ? float.PositiveInfinity : (float)Math.Max(0.0, e.ExpiresAt - now);
            if (rem > best) best = rem;
        }
        return best;
    }

    /// <summary>True while a cooldown tag of the ability is held (whether or not an effect with a known duration holds it).</summary>
    internal bool IsOnCooldown(AbilityHandle h)
    {
        Granted? g = FindGranted(h);
        return g != null && OnCooldown(g.Def);
    }

    /// <summary>Raises a gameplay event on this entity and activates every granted ability triggered by it (hierarchical match).
    /// Returns how many activated.</summary>
    public int SendGameplayEvent(GameplayTag tag, Entity instigator = default, float magnitude = 0f, Entity target = default)
    {
        if (!HasAuthority || !tag.IsValid) return 0;
        GasBatch.Begin();
        try
        {
            Notify(() => Bus("GAS.GameplayEvent", tag.Name, instigator.Handle, tag.Id, magnitude, target.Handle));
            int activated = 0;
            foreach (Granted g in _granted.ToArray())
            {
                bool hit = false;
                foreach (GameplayTag trig in g.Def.TriggerEvents)
                    if (tag.Matches(trig)) { hit = true; break; }
                if (hit && TryActivate(new AbilityHandle(g.Handle), target) == AbilityFailure.None) activated++;
            }
            return activated;
        }
        finally { GasBatch.End(); }
    }

    /// <summary>Activates the first granted ability whose tags match <paramref name="tag"/> and that can run.
    /// Returns its handle, or an invalid handle with the first failure reason.</summary>
    public AbilityHandle TryActivateByTag(GameplayTag tag, Entity target, out AbilityFailure failure)
    {
        failure = AbilityFailure.NotGranted;
        if (!tag.IsValid) return default;
        bool first = true;
        foreach (Granted g in _granted.ToArray())
        {
            bool hit = false;
            foreach (GameplayTag a in g.Def.AbilityTags)
                if (a.Matches(tag)) { hit = true; break; }
            if (!hit) continue;
            var h = new AbilityHandle(g.Handle);
            AbilityFailure f = TryActivate(h, target);
            if (f == AbilityFailure.None) { failure = AbilityFailure.None; return h; }
            if (first) { failure = f; first = false; }
        }
        return default;
    }

    internal IReadOnlyList<AbilityHandle> GrantedHandles() => _granted.Select(g => new AbilityHandle(g.Handle)).ToList();

    // ---- replication seam -----------------------------------------------------------------------

    private IGasReplicationSink? Sink => _silent > 0 ? null : GameplayAbilities.Replication;

    private void ReplicateBase(AttrState a)
    {
        IGasReplicationSink? sink = Sink;
        if (sink == null) return;
        try { sink.AttributeBase(this, a.Ref, a.Base); }
        catch (Exception ex) { Log.Error($"[GAS] replication sink threw: {ex.GetType().Name}: {ex.Message}"); }
    }

    private void ReplicateLoose(GameplayTag t)
    {
        IGasReplicationSink? sink = Sink;
        if (sink == null) return;
        try { sink.LooseTag(this, t, _loose.CountOf(t)); }
        catch (Exception ex) { Log.Error($"[GAS] replication sink threw: {ex.GetType().Name}: {ex.Message}"); }
    }

    // kind: 0 added, 1 changed, 2 removed.
    private void ReplicateEffect(int kind, ActiveEffect e)
    {
        IGasReplicationSink? sink = Sink;
        if (sink == null) return;
        try
        {
            ActiveEffectInfo info = InfoOf(e);
            if (kind == 0) sink.EffectAdded(this, info, e.Mags);
            else if (kind == 1) sink.EffectChanged(this, info, e.Mags);
            else sink.EffectRemoved(this, info, e.Mags);
        }
        catch (Exception ex) { Log.Error($"[GAS] replication sink threw: {ex.GetType().Name}: {ex.Message}"); }
    }

    private void ReplicateGrant(bool granted, Granted g)
    {
        IGasReplicationSink? sink = Sink;
        if (sink == null) return;
        try
        {
            if (granted) sink.AbilityGranted(this, new AbilityHandle(g.Handle), g.Def.Name, g.Level);
            else sink.AbilityRevoked(this, new AbilityHandle(g.Handle), g.Def.Name, g.Level);
        }
        catch (Exception ex) { Log.Error($"[GAS] replication sink threw: {ex.GetType().Name}: {ex.Message}"); }
    }

    private void ReplicateActivated(AbilityHandle h)
    {
        IGasReplicationSink? sink = Sink;
        if (sink == null) return;
        try { sink.AbilityActivated(this, h); }
        catch (Exception ex) { Log.Error($"[GAS] replication sink threw: {ex.GetType().Name}: {ex.Message}"); }
    }

    private void ReplicateEnded(AbilityHandle h, bool cancelled)
    {
        IGasReplicationSink? sink = Sink;
        if (sink == null) return;
        try { sink.AbilityEnded(this, h, cancelled); }
        catch (Exception ex) { Log.Error($"[GAS] replication sink threw: {ex.GetType().Name}: {ex.Message}"); }
    }

    // ---- diagnostics ----------------------------------------------------------------------------

    private static string F(float v) =>
        float.IsPositiveInfinity(v) ? "inf" : v.ToString("0.###", System.Globalization.CultureInfo.InvariantCulture);

    /// <summary>A multi-line description of attributes, tags, effects and abilities, for logs.</summary>
    public string Describe()
    {
        var sb = new StringBuilder();
        sb.Append("AbilitySystem entity ").Append(_owner).Append(HasAuthority ? " [authority]" : " [proxy]").Append('\n');
        sb.Append("Attributes:\n");
        foreach (AttrState a in _attrs)
            sb.Append("  ").Append(a.Ref.Name).Append(" base=").Append(F(a.Base)).Append(" current=").Append(F(a.Current))
              .Append(a.Def.IsMeta ? " (meta)" : "").Append('\n');
        sb.Append("Tags:\n");
        foreach (GameplayTag t in _tags) sb.Append("  ").Append(t.Name).Append(" x").Append(_tags.CountOf(t)).Append('\n');
        sb.Append("Effects:\n");
        foreach (ActiveEffect e in _effects)
        {
            ActiveEffectInfo i = InfoOf(e);
            sb.Append("  #").Append(e.Handle).Append(' ').Append(e.Def.Name).Append(" stacks=").Append(e.Stacks)
              .Append(" remaining=").Append(F(i.Remaining)).Append(e.Inhibited ? " inhibited" : "").Append('\n');
        }
        sb.Append("Abilities:\n");
        foreach (Granted g in _granted)
            sb.Append("  #").Append(g.Handle).Append(' ').Append(g.Def.Name).Append(" level=").Append(g.Level)
              .Append(g.Active ? " active" : " idle").Append('\n');
        return sb.ToString();
    }
}
