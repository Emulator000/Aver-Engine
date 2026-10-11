// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: the entry points the Aver Node gameplay-ability nodes call (docs/GAMEPLAY_ABILITIES.md).

using Aver.Scripting;

namespace Aver.Framework;

/// <summary>What the Abilities graph nodes call. Reached by reflection from Aver.Graph, like <c>BlackboardGraph</c>.
/// Graph strings are node attributes, never pins, so every method takes <c>(..., int xId, string x, ...)</c>: a non-zero
/// id wins, otherwise the attribute names the tag, attribute, effect or ability. A call with neither fails (false, 0 or an
/// invalid handle) and logs once. Nothing here throws.</summary>
internal static class GasGraph
{
    private static float Cap(float v) => float.IsPositiveInfinity(v) ? 1e30f : v;

    private static AbilitySystem? Sys(int entity)
    {
        if (entity == 0) return null;
        return AbilitySystem.Of(new Entity(entity));
    }

    // For operations that create state: the entity gets a system on demand if it is alive.
    private static AbilitySystem? SysCreate(int entity)
    {
        if (entity == 0) return null;
        AbilitySystem? s = AbilitySystem.Of(new Entity(entity));
        if (s != null) return s;
        return GameplayAbilities.Host.IsAlive(entity) ? AbilitySystem.GetOrAdd(new Entity(entity)) : null;
    }

    private static GameplayTag TagFor(int id, string tag, bool intern)
    {
        if (id != 0) return GameplayTag.FromId(id);
        if (string.IsNullOrEmpty(tag))
        {
            AbilitySystem.LogOnce("graph:notag", "[GAS] a gameplay node has neither a tag id pin nor a tag= attribute");
            return GameplayTag.None;
        }
        if (intern) return GameplayTag.Request(tag);
        GameplayTag.TryFind(tag, out GameplayTag t);
        return t;
    }

    private static AttributeRef AttributeFor(int id, string name)
    {
        if (id != 0) return AttributeRef.FromId(id);
        if (string.IsNullOrEmpty(name))
        {
            AbilitySystem.LogOnce("graph:noattr", "[GAS] a gameplay node has neither an attribute id pin nor an attribute= attribute");
            return default;
        }
        AttributeRef.TryFind(name, out AttributeRef a);
        return a;
    }

    private static EffectDef? EffectFor(int id, string name)
    {
        if (id != 0) return GameplayDefs.FindEffectById(id);
        if (string.IsNullOrEmpty(name))
        {
            AbilitySystem.LogOnce("graph:noeffect", "[GAS] a gameplay node has neither an effect id pin nor an effect= attribute");
            return null;
        }
        return GameplayDefs.FindEffect(name);
    }

    private static AbilityDef? AbilityFor(int id, string name)
    {
        if (id != 0) return GameplayDefs.FindAbilityById(id);
        if (string.IsNullOrEmpty(name))
        {
            AbilitySystem.LogOnce("graph:noability", "[GAS] a gameplay node has neither an ability id pin nor an ability= attribute");
            return null;
        }
        return GameplayDefs.FindAbility(name);
    }

    // The entity's handle for a granted ability, or invalid.
    private static AbilityHandle HandleFor(AbilitySystem s, int id, string name)
    {
        AbilityDef? d = AbilityFor(id, name);
        return d == null ? default : s.Find(d.Name);
    }

    // ---- ids ------------------------------------------------------------------------------------

    internal static int TagIdForGraph(string tag) => string.IsNullOrEmpty(tag) ? 0 : GameplayTag.Request(tag).Id;

    internal static int AttributeIdForGraph(string attribute) => string.IsNullOrEmpty(attribute) ? 0 : AttributeRef.Of(attribute).Id;

    internal static int EffectIdForGraph(string effect) => string.IsNullOrEmpty(effect) ? 0 : GameplayDefs.FindEffect(effect)?.Id ?? 0;

    internal static int AbilityIdForGraph(string ability) => string.IsNullOrEmpty(ability) ? 0 : GameplayDefs.FindAbility(ability)?.Id ?? 0;

    // ---- tags -----------------------------------------------------------------------------------

    internal static bool HasTagForGraph(int entity, int tagId, string tag)
    {
        AbilitySystem? s = Sys(entity);
        return s != null && s.HasTag(TagFor(tagId, tag, false));
    }

    internal static bool HasTagsForGraph(int entity, bool all, string tags)
    {
        AbilitySystem? s = Sys(entity);
        if (s == null || string.IsNullOrWhiteSpace(tags)) return false;
        bool any = false;
        foreach (string raw in tags.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            bool has = GameplayTag.TryFind(raw, out GameplayTag t) && s.HasTag(t);
            if (has) any = true;
            else if (all) return false;
        }
        return all || any;
    }

    internal static int TagCountForGraph(int entity, int tagId, string tag)
    {
        AbilitySystem? s = Sys(entity);
        return s == null ? 0 : s.TagCount(TagFor(tagId, tag, false));
    }

    internal static bool AddTagForGraph(int entity, int tagId, string tag, int count)
    {
        GameplayTag t = TagFor(tagId, tag, true);
        AbilitySystem? s = SysCreate(entity);
        if (s == null || !t.IsValid || !s.HasAuthority) return false;
        s.AddLooseTag(t, Math.Max(1, count));
        return true;
    }

    internal static bool RemoveTagForGraph(int entity, int tagId, string tag, int count)
    {
        AbilitySystem? s = Sys(entity);
        GameplayTag t = TagFor(tagId, tag, false);
        if (s == null || !t.IsValid || !s.HasAuthority) return false;
        int before = s.TagCount(t);
        s.RemoveLooseTag(t, Math.Max(1, count));
        return s.TagCount(t) != before;
    }

    // ---- attributes -----------------------------------------------------------------------------

    internal static bool GetAttributeForGraph(int entity, int attrId, string attribute, out float value, out float baseValue)
    {
        value = 0f;
        baseValue = 0f;
        AbilitySystem? s = Sys(entity);
        AttributeRef a = AttributeFor(attrId, attribute);
        if (s == null || !s.Has(a)) return false;
        value = s.GetValue(a);
        baseValue = s.GetBase(a);
        return true;
    }

    internal static bool SetAttributeBaseForGraph(int entity, int attrId, string attribute, float value)
    {
        AbilitySystem? s = Sys(entity);
        AttributeRef a = AttributeFor(attrId, attribute);
        return s != null && s.Has(a) && s.SetBase(a, value);
    }

    internal static bool AddAttributeSetForGraph(int entity, string set)
    {
        if (string.IsNullOrEmpty(set))
        {
            AbilitySystem.LogOnce("graph:noset", "[GAS] AddAttributeSet has no set= attribute");
            return false;
        }
        AbilitySystem? s = SysCreate(entity);
        return s != null && s.AddAttributeSet(set);
    }

    // ---- effects --------------------------------------------------------------------------------

    internal static int ApplyEffectForGraph(int target, int source, int effectId, string effect, string callerTag, float callerValue, out bool applied)
    {
        applied = false;
        EffectDef? def = EffectFor(effectId, effect);
        AbilitySystem? s = SysCreate(target);
        if (def == null || s == null) return 0;
        EffectSpec spec = EffectSpec.Of(def).From(new Entity(source != 0 ? source : target));
        if (!string.IsNullOrEmpty(callerTag))
        {
            GameplayTag t = GameplayTag.Request(callerTag);
            if (t.IsValid) spec.SetByCaller(t, callerValue);
        }
        ActiveEffectHandle h = s.Apply(spec, out EffectFailure failure);
        applied = failure == EffectFailure.None;
        return h.Value;
    }

    internal static bool RemoveEffectForGraph(int entity, int handle)
    {
        AbilitySystem? s = Sys(entity);
        return s != null && handle != 0 && s.Remove(new ActiveEffectHandle(handle));
    }

    internal static int RemoveEffectsByTagForGraph(int entity, int tagId, string tag)
    {
        AbilitySystem? s = Sys(entity);
        GameplayTag t = TagFor(tagId, tag, false);
        return s == null || !t.IsValid ? 0 : s.RemoveByTag(t);
    }

    internal static int RemoveEffectsByDefForGraph(int entity, int effectId, string effect)
    {
        AbilitySystem? s = Sys(entity);
        EffectDef? d = EffectFor(effectId, effect);
        return s == null || d == null ? 0 : s.RemoveByDef(d);
    }

    internal static bool EffectInfoForGraph(int entity, int effectId, string effect, out int stacks, out float remaining, out int handle)
    {
        stacks = 0;
        remaining = 0f;
        handle = 0;
        AbilitySystem? s = Sys(entity);
        EffectDef? d = EffectFor(effectId, effect);
        if (s == null || d == null || !s.TryGetEffect(d, out ActiveEffectInfo info)) return false;
        stacks = info.Stacks;
        remaining = Cap(info.Remaining);
        handle = info.Handle.Value;
        return true;
    }

    // ---- abilities ------------------------------------------------------------------------------

    internal static int GrantAbilityForGraph(int entity, int abilityId, string ability, int level)
    {
        AbilityDef? d = AbilityFor(abilityId, ability);
        AbilitySystem? s = SysCreate(entity);
        return d == null || s == null ? 0 : s.Grant(d, Math.Max(1, level)).Value;
    }

    internal static bool RevokeAbilityForGraph(int entity, int abilityId, string ability)
    {
        AbilitySystem? s = Sys(entity);
        if (s == null) return false;
        AbilityHandle h = HandleFor(s, abilityId, ability);
        return h.IsValid && s.Revoke(h);
    }

    internal static bool TryActivateForGraph(int entity, int abilityId, string ability, int target, out int reason)
    {
        AbilitySystem? s = Sys(entity);
        if (s == null) { reason = (int)AbilityFailure.NoSystem; return false; }
        AbilityHandle h = HandleFor(s, abilityId, ability);
        AbilityFailure f = h.IsValid ? s.TryActivate(h, new Entity(target)) : AbilityFailure.NotGranted;
        reason = (int)f;
        return f == AbilityFailure.None;
    }

    internal static bool TryActivateByTagForGraph(int entity, int tagId, string tag, int target, out int abilityId, out int reason)
    {
        abilityId = 0;
        AbilitySystem? s = Sys(entity);
        if (s == null) { reason = (int)AbilityFailure.NoSystem; return false; }
        GameplayTag t = TagFor(tagId, tag, false);
        AbilityHandle h = s.TryActivateByTag(t, new Entity(target), out AbilityFailure f);
        reason = (int)f;
        if (f == AbilityFailure.None) abilityId = s.DefOf(h)?.Id ?? 0;
        return f == AbilityFailure.None;
    }

    internal static bool CanActivateForGraph(int entity, int abilityId, string ability, out int reason)
    {
        AbilitySystem? s = Sys(entity);
        if (s == null) { reason = (int)AbilityFailure.NoSystem; return false; }
        AbilityHandle h = HandleFor(s, abilityId, ability);
        AbilityFailure f = h.IsValid ? s.CanActivate(h) : AbilityFailure.NotGranted;
        reason = (int)f;
        return f == AbilityFailure.None;
    }

    internal static bool CommitAbilityForGraph(int entity, int abilityId, string ability)
    {
        AbilitySystem? s = Sys(entity);
        if (s == null) return false;
        AbilityHandle h = HandleFor(s, abilityId, ability);
        return h.IsValid && s.Commit(h);
    }

    internal static bool EndAbilityForGraph(int entity, int abilityId, string ability, bool cancelled)
    {
        AbilitySystem? s = Sys(entity);
        if (s == null) return false;
        AbilityHandle h = HandleFor(s, abilityId, ability);
        return h.IsValid && s.End(h, cancelled);
    }

    internal static int CancelAbilitiesByTagForGraph(int entity, int tagId, string tag)
    {
        AbilitySystem? s = Sys(entity);
        GameplayTag t = TagFor(tagId, tag, false);
        return s == null || !t.IsValid ? 0 : s.CancelAbilities(t);
    }

    internal static bool IsAbilityActiveForGraph(int entity, int abilityId, string ability)
    {
        AbilitySystem? s = Sys(entity);
        if (s == null) return false;
        AbilityHandle h = HandleFor(s, abilityId, ability);
        return h.IsValid && s.IsActive(h);
    }

    internal static float AbilityCooldownForGraph(int entity, int abilityId, string ability, out bool onCooldown)
    {
        onCooldown = false;
        AbilitySystem? s = Sys(entity);
        if (s == null) return 0f;
        AbilityHandle h = HandleFor(s, abilityId, ability);
        if (!h.IsValid) return 0f;
        onCooldown = s.IsOnCooldown(h);
        return Cap(s.CooldownRemaining(h));
    }

    internal static int SendGameplayEventForGraph(int entity, int instigator, int tagId, string tag, float magnitude)
    {
        AbilitySystem? s = SysCreate(entity);
        GameplayTag t = TagFor(tagId, tag, true);
        return s == null || !t.IsValid ? 0 : s.SendGameplayEvent(t, new Entity(instigator), magnitude);
    }
}
