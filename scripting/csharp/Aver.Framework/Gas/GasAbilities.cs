// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: ability definitions, handles and the C# behaviour base class.

namespace Aver.Framework;

/// <summary>Why an ability could not be activated. Values are pinned (the graph nodes expose them as ints).</summary>
public enum AbilityFailure
{
    /// <summary>Activated (or activatable).</summary>
    None = 0,
    /// <summary>The entity has no ability system.</summary>
    NoSystem = 1,
    /// <summary>The ability is not granted.</summary>
    NotGranted = 2,
    /// <summary>Already running and not retriggerable.</summary>
    AlreadyActive = 3,
    /// <summary>The owner lacks a required tag.</summary>
    MissingRequiredTag = 4,
    /// <summary>The owner holds a blocking tag.</summary>
    BlockedByTag = 5,
    /// <summary>A running ability blocks this one.</summary>
    BlockedByAbility = 6,
    /// <summary>A cooldown tag is held.</summary>
    OnCooldown = 7,
    /// <summary>The cost cannot be paid.</summary>
    CannotAfford = 8,
    /// <summary>The C# behaviour's <c>CanActivate</c> said no.</summary>
    BehaviourRefused = 9,
    /// <summary>The system is a client proxy.</summary>
    NoAuthority = 10,
}

/// <summary>When an activated ability pays its cost and cooldown.</summary>
public enum AbilityCommit
{
    /// <summary>At activation.</summary>
    Activate,
    /// <summary>Only when the ability (or its graph) calls Commit.</summary>
    Manual,
}

/// <summary>An ability definition (immutable once registered).</summary>
public sealed class AbilityDef
{
    /// <summary>Unique name.</summary>
    public string Name = "";
    /// <summary>Registry id, assigned at registration (0 until then).</summary>
    public int Id;
    /// <summary>The ability's own tags.</summary>
    public GameplayTagContainer AbilityTags = new();
    /// <summary>Activating cancels running abilities whose tags match any of these.</summary>
    public GameplayTagContainer CancelsTags = new();
    /// <summary>While running, abilities whose tags match any of these cannot activate.</summary>
    public GameplayTagContainer BlocksTags = new();
    /// <summary>Tags the owner holds while the ability runs.</summary>
    public GameplayTagContainer OwnedTags = new();
    /// <summary>The owner must have all of these to activate.</summary>
    public GameplayTagContainer RequiredTags = new();
    /// <summary>The owner must have none of these to activate.</summary>
    public GameplayTagContainer BlockedTags = new();
    /// <summary>Gameplay-event tags that activate the ability.</summary>
    public GameplayTagContainer TriggerEvents = new();
    /// <summary>An instant effect paid on commit.</summary>
    public EffectDef? Cost;
    /// <summary>A duration effect granting the cooldown tag(s), applied on commit.</summary>
    public EffectDef? Cooldown;
    /// <summary>When cost and cooldown are paid.</summary>
    public AbilityCommit Commit;
    /// <summary>Auto-end after this many seconds; 0 = until ended.</summary>
    public float Duration;
    /// <summary>Passive: activates when granted.</summary>
    public bool TriggerOnGranted;
    /// <summary>Activating while running ends the old run and starts a new one.</summary>
    public bool Retrigger;
    /// <summary>Name of a registered C# <see cref="GameplayAbility"/>, or null for a graph-only ability.</summary>
    public string? Behaviour;

    /// <inheritdoc/>
    public override string ToString() => Name;
}

/// <summary>A handle to a granted ability on one system. 0 is invalid; handles are per-system and never reused.</summary>
public readonly struct AbilityHandle : IEquatable<AbilityHandle>
{
    internal AbilityHandle(int value) => Value = value;

    /// <summary>The raw handle.</summary>
    public int Value { get; }
    /// <summary>True for a non-zero handle.</summary>
    public bool IsValid => Value != 0;
    /// <inheritdoc/>
    public bool Equals(AbilityHandle other) => Value == other.Value;
    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is AbilityHandle h && Equals(h);
    /// <inheritdoc/>
    public override int GetHashCode() => Value;
    /// <summary>Equality.</summary>
    public static bool operator ==(AbilityHandle a, AbilityHandle b) => a.Value == b.Value;
    /// <summary>Inequality.</summary>
    public static bool operator !=(AbilityHandle a, AbilityHandle b) => a.Value != b.Value;
    /// <inheritdoc/>
    public override string ToString() => $"Ability#{Value}";
}

/// <summary>Base class for a C#-authored ability. One instance exists per granted ability per system, so it may keep state.
/// Bind it to a definition with <c>[AverAbility("Name")]</c> and the definition's <c>BEHAVIOUR</c>.</summary>
public abstract class GameplayAbility
{
    /// <summary>The system this ability is granted to.</summary>
    public AbilitySystem Owner { get; private set; } = null!;
    /// <summary>The definition.</summary>
    public AbilityDef Def { get; private set; } = null!;
    /// <summary>This grant's handle.</summary>
    public AbilityHandle Handle { get; private set; }
    /// <summary>The granted level (stored and exposed; scales nothing until curves exist).</summary>
    public int Level { get; internal set; }
    /// <summary>The target of the current (or most recent) activation.</summary>
    public Entity Target { get; internal set; }

    internal void Bind(AbilitySystem owner, AbilityDef def, AbilityHandle handle, int level)
    {
        Owner = owner;
        Def = def;
        Handle = handle;
        Level = level;
    }

    /// <summary>The ability was granted.</summary>
    public virtual void OnGranted() { }
    /// <summary>The ability was revoked (after it has been ended if it was running).</summary>
    public virtual void OnRevoked() { }
    /// <summary>A last say before activation; return false to refuse with <see cref="AbilityFailure.BehaviourRefused"/>.</summary>
    public virtual bool CanActivate() => true;
    /// <summary>The ability started.</summary>
    public virtual void OnActivate() { }
    /// <summary>The ability ended; <paramref name="cancelled"/> is true when something else stopped it.</summary>
    public virtual void OnEnd(bool cancelled) { }

    /// <summary>Pays cost and cooldown (for <see cref="AbilityCommit.Manual"/> abilities).</summary>
    protected bool Commit() => Owner.Commit(Handle);
    /// <summary>Ends this ability.</summary>
    protected void End(bool cancelled = false) => Owner.End(Handle, cancelled);
}
