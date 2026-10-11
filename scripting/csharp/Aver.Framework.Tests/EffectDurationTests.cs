// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Effect lifetime on the game clock: expiry, infinite, duration override, removal, cleanse, application requirements.

using Aver.Framework;

static class EffectDurationTests
{
    private const string Defs =
        "EFFECT Fx.Buff\n  POLICY duration 5\n  ASSET Kind.Buff\n  GRANTS Status.Buffed\n  MOD Speed add 10\n" +
        "EFFECT Fx.Forever\n  POLICY infinite\n  MOD Speed add 1\n" +
        "EFFECT Fx.Cleanse\n  POLICY instant\n  REMOVES Status.Buffed\n" +
        "EFFECT Fx.CleanseAsset\n  POLICY instant\n  REMOVES Kind.Buff\n" +
        "EFFECT Fx.NeedsAlive\n  POLICY instant\n  REQUIRES State.Alive\n  MOD Speed add 1\n" +
        "EFFECT Fx.Shielded\n  POLICY instant\n  BLOCKEDBY Immune.Fire\n  MOD Speed add 1\n";

    public static int RunAll() => T.Suite(() =>
    {
        T.Run("duration: expires at the clock time, not before", ExpiresOnTime);
        T.Run("duration: an infinite effect never expires", InfiniteStays);
        T.Run("duration: WithDuration overrides the definition for one application", DurationOverride);
        T.Run("duration: removal restores Current and tags", RemovalRestores);
        T.Run("duration: RemovesWithTags cleanses by granted tag and by asset tag", Cleanse);
        T.Run("duration: application requirements and blockers refuse with TagRequirement", Requirements);
        T.Run("duration: remaining time counts down", RemainingCountsDown);
        T.Run("duration: a dead entity's system is dropped by Update", DeadEntity);
    });

    private static void ExpiresOnTime()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        T.Apply(s, "Fx.Buff");
        T.CheckValue("active", 20f, s.GetValue(speed));
        T.At(104.9);
        T.CheckValue("not yet at 104.9", 20f, s.GetValue(speed));
        T.Check("still listed", s.ActiveEffects.Count == 1);
        T.At(105.0);
        T.CheckValue("expired at exactly 105", 10f, s.GetValue(speed));
        T.Check("no longer listed and its tag is gone", s.ActiveEffects.Count == 0 && !s.HasTag(T.Tag("Status.Buffed")));
    }

    private static void InfiniteStays()
    {
        AbilitySystem s = T.Hero(Defs);
        T.Apply(s, "Fx.Forever");
        T.Advance(1e6);
        T.CheckValue("still applied a million seconds later", 11f, s.GetValue(T.Attr("Speed")));
        T.Check("remaining is infinite", s.ActiveEffects[0].Remaining == float.PositiveInfinity);
    }

    private static void DurationOverride()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        T.Apply(s, "Fx.Buff", duration: 2f);
        T.At(101.9);
        T.CheckValue("alive at 101.9", 20f, s.GetValue(speed));
        T.At(102.0);
        T.CheckValue("gone at 102", 10f, s.GetValue(speed));
        T.Apply(s, "Fx.Buff");
        T.At(106.9);
        T.CheckValue("the next application uses the definition's 5 s", 20f, s.GetValue(speed));
    }

    private static void RemovalRestores()
    {
        AbilitySystem s = T.Hero(Defs);
        GameplayTag buffed = T.Tag("Status.Buffed");
        ActiveEffectHandle h = T.Apply(s, "Fx.Buff");
        T.Check("tag granted while active", s.HasTag(buffed));
        T.Check("Remove", s.Remove(h));
        T.CheckValue("Current restored", 10f, s.GetValue(T.Attr("Speed")));
        T.Check("tag released", !s.HasTag(buffed));
        T.Advance(10);
        T.CheckValue("nothing happens at the old expiry time", 10f, s.GetValue(T.Attr("Speed")));
    }

    private static void Cleanse()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        T.Apply(s, "Fx.Buff");
        T.Apply(s, "Fx.Cleanse");
        T.CheckValue("cleansed by granted tag", 10f, s.GetValue(speed));
        T.Apply(s, "Fx.Buff");
        T.Apply(s, "Fx.CleanseAsset");
        T.CheckValue("cleansed by asset tag", 10f, s.GetValue(speed));
        T.Apply(s, "Fx.Forever");
        T.Apply(s, "Fx.Cleanse");
        T.CheckValue("an unrelated effect survives", 11f, s.GetValue(speed));
        T.Check("RemoveByTag counts", T.Apply(s, "Fx.Buff").IsValid && s.RemoveByTag(T.Tag("Kind.Buff")) == 1);
    }

    private static void Requirements()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        s.Apply(EffectSpec.Of("Fx.NeedsAlive"), out EffectFailure f);
        T.Check("missing required tag", f == EffectFailure.TagRequirement);
        T.CheckValue("nothing changed", 10f, s.GetBase(speed));
        s.AddLooseTag(T.Tag("State.Alive"));
        s.Apply(EffectSpec.Of("Fx.NeedsAlive"), out f);
        T.Check("with the tag it applies", f == EffectFailure.None);
        T.CheckValue("...and changed Base", 11f, s.GetBase(speed));

        s.Apply(EffectSpec.Of("Fx.Shielded"), out f);
        T.Check("no blocker: applies", f == EffectFailure.None);
        s.AddLooseTag(T.Tag("Immune.Fire.Total"));
        s.Apply(EffectSpec.Of("Fx.Shielded"), out f);
        T.Check("a descendant of the blocker tag blocks (hierarchical)", f == EffectFailure.TagRequirement);
        s.Apply(EffectSpec.Of("Fx.Missing"), out f);
        T.Check("unknown effect", f == EffectFailure.UnknownEffect);
    }

    private static void RemainingCountsDown()
    {
        AbilitySystem s = T.Hero(Defs);
        ActiveEffectHandle h = T.Apply(s, "Fx.Buff");
        T.Host.Time = 102.0;
        T.Check("3 s left", s.TryGetEffect(h, out ActiveEffectInfo info) && T.Near(info.Remaining, 3f));
        T.Check("not inhibited", !info.Inhibited);
    }

    private static void DeadEntity()
    {
        AbilitySystem s = T.Hero(Defs);
        T.Apply(s, "Fx.Forever");
        T.Host.Dead.Add(1);
        T.Check("applying to a dead entity fails", s.Apply(EffectSpec.Of("Fx.Forever"), out EffectFailure f) == default && f == EffectFailure.NoSystem);
        GameplayAbilities.Update();
        T.Check("Update dropped the system", AbilitySystem.Of(T.E(1)) == null);
    }
}
