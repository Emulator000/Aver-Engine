// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: fluent builders. They produce the same def objects the .ocgas parser does.

namespace Aver.Framework;

internal static class BuilderTags
{
    internal static void AddAll(GameplayTagContainer into, string[] names)
    {
        foreach (string n in names)
        {
            GameplayTag t = GameplayTag.Request(n);
            if (t.IsValid) into.Add(t);
        }
    }
}

/// <summary>Fluent <see cref="EffectDef"/> builder: <c>GameplayDefs.Effect("Effect.Heal").Instant().Modify("Health", ModOp.Add, 25f).Register()</c>.</summary>
public sealed class EffectBuilder
{
    private readonly EffectDef _d = new();
    private readonly List<ModifierDef> _mods = new();

    internal EffectBuilder(string name) => _d.Name = name;

    /// <summary>Applies once to Base values (the default).</summary>
    public EffectBuilder Instant() { _d.Policy = EffectPolicy.Instant; return this; }
    /// <summary>Lasts <paramref name="seconds"/>.</summary>
    public EffectBuilder Duration(float seconds) { _d.Policy = EffectPolicy.Duration; _d.Duration = seconds; return this; }
    /// <summary>Lasts until removed.</summary>
    public EffectBuilder Infinite() { _d.Policy = EffectPolicy.Infinite; return this; }
    /// <summary>Executes its modifiers on Base every <paramref name="seconds"/> (needs a duration or infinite policy).</summary>
    public EffectBuilder Periodic(float seconds, bool executeOnApply = false) { _d.Period = seconds; _d.ExecuteOnApply = executeOnApply; return this; }

    /// <summary>A constant modifier.</summary>
    public EffectBuilder Modify(string attribute, ModOp op, float value)
    {
        _mods.Add(new ModifierDef { Attribute = AttributeRef.Of(attribute), Op = op, Kind = MagnitudeKind.Constant, Value = value });
        return this;
    }

    /// <summary>A modifier whose magnitude the spec supplies under <paramref name="callerTag"/>, times <paramref name="scale"/>.</summary>
    public EffectBuilder ModifyByCaller(string attribute, ModOp op, string callerTag, float scale = 1f)
    {
        _mods.Add(new ModifierDef { Attribute = AttributeRef.Of(attribute), Op = op, Kind = MagnitudeKind.SetByCaller, Value = scale, CallerTag = GameplayTag.Request(callerTag) });
        return this;
    }

    /// <summary>A modifier that reads another attribute (snapshotted at application): <c>value * scale + add</c>.</summary>
    public EffectBuilder ModifyFromAttribute(string attribute, ModOp op, string fromAttribute, bool fromSource, float scale = 1f, float add = 0f)
    {
        _mods.Add(new ModifierDef { Attribute = AttributeRef.Of(attribute), Op = op, Kind = MagnitudeKind.AttributeBased, Value = scale, Add = add,
                                    FromAttribute = AttributeRef.Of(fromAttribute), FromSource = fromSource });
        return this;
    }

    /// <summary>Tags that describe the effect.</summary>
    public EffectBuilder AssetTags(params string[] tags) { BuilderTags.AddAll(_d.AssetTags, tags); return this; }
    /// <summary>Tags held while the effect is active.</summary>
    public EffectBuilder GrantsTags(params string[] tags) { BuilderTags.AddAll(_d.GrantedTags, tags); return this; }
    /// <summary>The target must have all of these.</summary>
    public EffectBuilder RequiresTags(params string[] tags) { BuilderTags.AddAll(_d.AppRequired, tags); return this; }
    /// <summary>The target must have none of these.</summary>
    public EffectBuilder BlockedByTags(params string[] tags) { BuilderTags.AddAll(_d.AppBlocked, tags); return this; }
    /// <summary>While one is missing the effect is inhibited.</summary>
    public EffectBuilder OngoingRequiresTags(params string[] tags) { BuilderTags.AddAll(_d.OngoingRequired, tags); return this; }
    /// <summary>While one is present the effect is inhibited.</summary>
    public EffectBuilder OngoingBlockedByTags(params string[] tags) { BuilderTags.AddAll(_d.OngoingBlocked, tags); return this; }
    /// <summary>Applying removes active effects whose asset or granted tags match any of these.</summary>
    public EffectBuilder RemovesTags(params string[] tags) { BuilderTags.AddAll(_d.RemovesWithTags, tags); return this; }

    /// <summary>Stacking rules.</summary>
    public EffectBuilder Stacking(StackScope scope, int limit, bool refresh = true, StackExpire expire = StackExpire.ClearAll)
    {
        _d.Stack = scope;
        _d.StackLimit = limit;
        _d.StackRefresh = refresh;
        _d.StackExpire = expire;
        return this;
    }

    /// <summary>The cue tag announced on the <c>GAS.Cue</c> event.</summary>
    public EffectBuilder Cue(string tag) { _d.Cue = GameplayTag.Request(tag); return this; }

    /// <summary>The definition, not yet registered.</summary>
    public EffectDef Build()
    {
        _d.Modifiers = _mods.ToArray();
        return _d;
    }

    /// <summary>Registers the definition. Null (and a logged error) when it is invalid or the name is taken.</summary>
    public EffectDef? Register()
    {
        EffectDef d = Build();
        return GameplayDefs.TryRegister(d, "code") ? d : null;
    }
}

/// <summary>Fluent <see cref="AbilityDef"/> builder.</summary>
public sealed class AbilityBuilder
{
    private readonly AbilityDef _d = new();
    private string? _cost, _cooldown;
    private EffectDef? _costDef, _cooldownDef;

    internal AbilityBuilder(string name) => _d.Name = name;

    /// <summary>The ability's own tags.</summary>
    public AbilityBuilder AbilityTags(params string[] tags) { BuilderTags.AddAll(_d.AbilityTags, tags); return this; }
    /// <summary>Activating cancels running abilities whose tags match.</summary>
    public AbilityBuilder Cancels(params string[] tags) { BuilderTags.AddAll(_d.CancelsTags, tags); return this; }
    /// <summary>While running, abilities whose tags match cannot activate.</summary>
    public AbilityBuilder Blocks(params string[] tags) { BuilderTags.AddAll(_d.BlocksTags, tags); return this; }
    /// <summary>Tags the owner holds while the ability runs.</summary>
    public AbilityBuilder Owns(params string[] tags) { BuilderTags.AddAll(_d.OwnedTags, tags); return this; }
    /// <summary>The owner must have all of these.</summary>
    public AbilityBuilder RequiresTags(params string[] tags) { BuilderTags.AddAll(_d.RequiredTags, tags); return this; }
    /// <summary>The owner must have none of these.</summary>
    public AbilityBuilder BlockedByTags(params string[] tags) { BuilderTags.AddAll(_d.BlockedTags, tags); return this; }
    /// <summary>The instant effect paid on commit (looked up when registering).</summary>
    public AbilityBuilder Cost(string effectName) { _cost = effectName; return this; }
    /// <summary>The instant effect paid on commit.</summary>
    public AbilityBuilder Cost(EffectDef effect) { _costDef = effect; return this; }
    /// <summary>The duration effect whose granted tags are the cooldown (looked up when registering).</summary>
    public AbilityBuilder Cooldown(string effectName) { _cooldown = effectName; return this; }
    /// <summary>The duration effect whose granted tags are the cooldown.</summary>
    public AbilityBuilder Cooldown(EffectDef effect) { _cooldownDef = effect; return this; }
    /// <summary>When cost and cooldown are paid.</summary>
    public AbilityBuilder Commit(AbilityCommit commit) { _d.Commit = commit; return this; }
    /// <summary>Auto-end after this many seconds.</summary>
    public AbilityBuilder Duration(float seconds) { _d.Duration = seconds; return this; }
    /// <summary>Activates on a gameplay event whose tag matches.</summary>
    public AbilityBuilder TriggerOnEvent(string tag) { BuilderTags.AddAll(_d.TriggerEvents, new[] { tag }); return this; }
    /// <summary>Passive: activates when granted.</summary>
    public AbilityBuilder TriggerWhenGranted() { _d.TriggerOnGranted = true; return this; }
    /// <summary>Activating while running restarts it.</summary>
    public AbilityBuilder Retrigger(bool on = true) { _d.Retrigger = on; return this; }
    /// <summary>The <c>[AverAbility]</c> name of a C# behaviour.</summary>
    public AbilityBuilder Behaviour(string name) { _d.Behaviour = name; return this; }

    /// <summary>The definition, not yet registered. Cost and cooldown given by name must already be registered.</summary>
    public AbilityDef Build()
    {
        _d.Cost = _costDef ?? (_cost == null ? null : GameplayDefs.FindEffect(_cost));
        _d.Cooldown = _cooldownDef ?? (_cooldown == null ? null : GameplayDefs.FindEffect(_cooldown));
        return _d;
    }

    /// <summary>Registers the definition. Null (and a logged error) when it is invalid or the name is taken.</summary>
    public AbilityDef? Register()
    {
        if (_cost != null && _costDef == null && GameplayDefs.FindEffect(_cost) == null)
        {
            Aver.Scripting.Log.Error($"[GAS] ability '{_d.Name}': undefined cost effect '{_cost}'");
            return null;
        }
        if (_cooldown != null && _cooldownDef == null && GameplayDefs.FindEffect(_cooldown) == null)
        {
            Aver.Scripting.Log.Error($"[GAS] ability '{_d.Name}': undefined cooldown effect '{_cooldown}'");
            return null;
        }
        AbilityDef d = Build();
        return GameplayDefs.TryRegister(d, "code") ? d : null;
    }
}

/// <summary>Fluent <see cref="AttributeSetDef"/> builder.</summary>
public sealed class AttributeSetBuilder
{
    private readonly string _name;
    private readonly List<AttributeDef> _attrs = new();

    internal AttributeSetBuilder(string name) => _name = name;

    /// <summary>An attribute with a default Base and literal bounds, or bounds read from other attributes.</summary>
    public AttributeSetBuilder Attribute(string name, float defaultBase, float min = float.NegativeInfinity, float max = float.PositiveInfinity,
                                         string? minFrom = null, string? maxFrom = null)
    {
        var d = new AttributeDef { Name = name, DefaultBase = defaultBase, Min = min, Max = max };
        if (minFrom != null) d.MinFrom = AttributeRef.Of(minFrom);
        if (maxFrom != null) d.MaxFrom = AttributeRef.Of(maxFrom);
        AttributeRef.Of(name);
        _attrs.Add(d);
        return this;
    }

    /// <summary>A meta attribute: never stored; an instant Add is forwarded (times <paramref name="scale"/>) to <paramref name="forwardTo"/> or announced.</summary>
    public AttributeSetBuilder Meta(string name, string? forwardTo = null, float scale = 1f)
    {
        var d = new AttributeDef { Name = name, IsMeta = true, ForwardScale = scale };
        if (forwardTo != null) d.ForwardTo = AttributeRef.Of(forwardTo);
        AttributeRef.Of(name);
        _attrs.Add(d);
        return this;
    }

    /// <summary>The definition, not yet registered.</summary>
    public AttributeSetDef Build() => new AttributeSetDef(_name, _attrs.ToArray());

    /// <summary>Registers the set. Null (and a logged error) when it is invalid or the name is taken.</summary>
    public AttributeSetDef? Register()
    {
        AttributeSetDef d = Build();
        return GameplayDefs.TryRegister(d, "code") ? d : null;
    }
}
