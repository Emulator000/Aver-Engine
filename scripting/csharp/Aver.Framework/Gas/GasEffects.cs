// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: effect definitions, specs and handles.

namespace Aver.Framework;

/// <summary>How long an effect lasts.</summary>
public enum EffectPolicy
{
    /// <summary>Applies once to Base values and is gone.</summary>
    Instant,
    /// <summary>Modifies Current values for a fixed time.</summary>
    Duration,
    /// <summary>Modifies Current values until removed.</summary>
    Infinite,
}

/// <summary>What a modifier does to its attribute.</summary>
public enum ModOp
{
    /// <summary>Adds the magnitude (times stacks).</summary>
    Add,
    /// <summary>Multiplies by the magnitude (as <c>1 + (m-1) * stacks</c>).</summary>
    Multiply,
    /// <summary>Replaces the value; the most recently applied override wins.</summary>
    Override,
}

/// <summary>Where a modifier's magnitude comes from.</summary>
public enum MagnitudeKind
{
    /// <summary>A literal number.</summary>
    Constant,
    /// <summary>A value supplied in the spec under a tag, times a scale.</summary>
    SetByCaller,
    /// <summary>An attribute's Current value on the source or target, times a scale plus an offset, snapshotted at application.</summary>
    AttributeBased,
}

/// <summary>How repeated applications of one effect combine.</summary>
public enum StackScope
{
    /// <summary>Every application is an independent instance.</summary>
    None,
    /// <summary>One instance per effect on the target.</summary>
    Target,
    /// <summary>One instance per (effect, source entity).</summary>
    Source,
}

/// <summary>What happens to the stacks when the duration runs out.</summary>
public enum StackExpire
{
    /// <summary>All stacks go.</summary>
    ClearAll,
    /// <summary>One stack goes and the duration restarts until none are left.</summary>
    RemoveOne,
}

/// <summary>Why an effect was not applied.</summary>
public enum EffectFailure
{
    /// <summary>Applied.</summary>
    None,
    /// <summary>The target has no system or is not alive.</summary>
    NoSystem,
    /// <summary>The system is a client proxy and may not apply effects.</summary>
    NoAuthority,
    /// <summary>No effect with that name or definition.</summary>
    UnknownEffect,
    /// <summary>The target's tags fail the effect's application requirements.</summary>
    TagRequirement,
    /// <summary>The stack is full.</summary>
    StackLimit,
    /// <summary>The system already carries the maximum number of effects.</summary>
    LimitReached,
}

/// <summary>One attribute change an effect makes.</summary>
public sealed class ModifierDef
{
    /// <summary>The attribute modified.</summary>
    public AttributeRef Attribute;
    /// <summary>What it does.</summary>
    public ModOp Op;
    /// <summary>Where the magnitude comes from.</summary>
    public MagnitudeKind Kind;
    /// <summary>The constant (Constant) or the scale (SetByCaller, AttributeBased).</summary>
    public float Value;
    /// <summary>AttributeBased only: added after scaling.</summary>
    public float Add;
    /// <summary>SetByCaller only: the key the caller supplies the value under.</summary>
    public GameplayTag CallerTag;
    /// <summary>AttributeBased only: the attribute read.</summary>
    public AttributeRef FromAttribute;
    /// <summary>AttributeBased only: read it from the source (true) or the target (false).</summary>
    public bool FromSource;
}

/// <summary>An immutable-by-convention gameplay effect definition (immutable once registered).</summary>
public sealed class EffectDef
{
    /// <summary>Unique name.</summary>
    public string Name = "";
    /// <summary>Registry id, assigned at registration (0 until then).</summary>
    public int Id;
    /// <summary>Instant, duration or infinite.</summary>
    public EffectPolicy Policy;
    /// <summary>Seconds, for <see cref="EffectPolicy.Duration"/>.</summary>
    public float Duration;
    /// <summary>Seconds between periodic executions; 0 = not periodic.</summary>
    public float Period;
    /// <summary>A periodic effect also executes once when applied.</summary>
    public bool ExecuteOnApply;
    /// <summary>A stacking application resets the expiry (default true).</summary>
    public bool StackRefresh = true;
    /// <summary>Stacking scope.</summary>
    public StackScope Stack;
    /// <summary>Maximum stacks.</summary>
    public int StackLimit = 1;
    /// <summary>What expiry does to the stacks.</summary>
    public StackExpire StackExpire;
    /// <summary>The attribute changes.</summary>
    public IReadOnlyList<ModifierDef> Modifiers = Array.Empty<ModifierDef>();
    /// <summary>Tags that describe the effect (matched by cleanses).</summary>
    public GameplayTagContainer AssetTags = new();
    /// <summary>Tags the target holds while the effect is active.</summary>
    public GameplayTagContainer GrantedTags = new();
    /// <summary>The target must have all of these to be affected.</summary>
    public GameplayTagContainer AppRequired = new();
    /// <summary>The target must have none of these to be affected.</summary>
    public GameplayTagContainer AppBlocked = new();
    /// <summary>While any of these is missing the effect is inhibited.</summary>
    public GameplayTagContainer OngoingRequired = new();
    /// <summary>While any of these is present the effect is inhibited.</summary>
    public GameplayTagContainer OngoingBlocked = new();
    /// <summary>Applying removes active effects whose asset or granted tags match any of these.</summary>
    public GameplayTagContainer RemovesWithTags = new();
    /// <summary>Optional cue tag announced on the <c>GAS.Cue</c> event.</summary>
    public GameplayTag Cue;

    /// <inheritdoc/>
    public override string ToString() => Name;
}

/// <summary>One application of an effect: which effect, who applies it, and any per-application values.</summary>
public sealed class EffectSpec
{
    internal EffectDef? Def;
    internal string? EffectName;
    internal Entity SourceEntity;
    internal Dictionary<int, float>? Caller;
    internal float? DurationOverride;

    private EffectSpec() { }

    /// <summary>A spec for the registered effect called <paramref name="effectName"/> (looked up at application).</summary>
    public static EffectSpec Of(string effectName) => new EffectSpec { EffectName = effectName };

    /// <summary>A spec for a definition object.</summary>
    public static EffectSpec Of(EffectDef def) => new EffectSpec { Def = def };

    /// <summary>The entity applying the effect.</summary>
    public EffectSpec From(Entity source)
    {
        SourceEntity = source;
        return this;
    }

    /// <summary>Supplies the value for a SetByCaller modifier keyed by <paramref name="tag"/>.</summary>
    public EffectSpec SetByCaller(GameplayTag tag, float value)
    {
        Caller ??= new Dictionary<int, float>();
        Caller[tag.Id] = value;
        return this;
    }

    /// <summary>Overrides the duration for this application.</summary>
    public EffectSpec WithDuration(float seconds)
    {
        DurationOverride = seconds;
        return this;
    }

    internal EffectDef? Resolve() => Def ?? (EffectName == null ? null : GameplayDefs.FindEffect(EffectName));
}

/// <summary>A handle to an active effect on one system. 0 is invalid; handles are per-system and never reused.</summary>
public readonly struct ActiveEffectHandle : IEquatable<ActiveEffectHandle>
{
    internal ActiveEffectHandle(int value) => Value = value;

    /// <summary>The raw handle.</summary>
    public int Value { get; }
    /// <summary>True for a non-zero handle; says nothing about whether the effect is still active.</summary>
    public bool IsValid => Value != 0;
    /// <inheritdoc/>
    public bool Equals(ActiveEffectHandle other) => Value == other.Value;
    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is ActiveEffectHandle h && Equals(h);
    /// <inheritdoc/>
    public override int GetHashCode() => Value;
    /// <summary>Equality.</summary>
    public static bool operator ==(ActiveEffectHandle a, ActiveEffectHandle b) => a.Value == b.Value;
    /// <summary>Inequality.</summary>
    public static bool operator !=(ActiveEffectHandle a, ActiveEffectHandle b) => a.Value != b.Value;
    /// <inheritdoc/>
    public override string ToString() => $"Effect#{Value}";
}

/// <summary>A snapshot of one active effect.</summary>
/// <param name="Handle">Its handle (invalid for an instant effect).</param>
/// <param name="Def">The definition.</param>
/// <param name="Source">The entity that applied it.</param>
/// <param name="Stacks">Current stack count.</param>
/// <param name="Remaining">Seconds left; +infinity if it never expires.</param>
/// <param name="Inhibited">True while its ongoing requirements are unmet.</param>
public readonly record struct ActiveEffectInfo(ActiveEffectHandle Handle, EffectDef Def, Entity Source, int Stacks, float Remaining, bool Inhibited);
