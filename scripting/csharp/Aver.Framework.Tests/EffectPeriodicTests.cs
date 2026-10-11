// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Periodic effects: execution counts over a clock sweep, execute-on-apply, the per-update cap, Base vs aggregator.

using Aver.Framework;

static class EffectPeriodicTests
{
    private const string Defs =
        "EFFECT Fx.Poison\n  POLICY duration 10\n  PERIOD 1\n  MOD Health add -3\n" +
        "EFFECT Fx.PoisonNow\n  POLICY duration 10\n  PERIOD 1 apply\n  MOD Health add -3\n" +
        "EFFECT Fx.Bleed\n  POLICY infinite\n  PERIOD 1\n  MOD Speed add -1\n" +
        "EFFECT Fx.Gated\n  POLICY infinite\n  PERIOD 1\n  MOD Speed add -1\n  ONGOING_REQUIRES State.Gate\n";

    public static int RunAll() => T.Suite(() =>
    {
        T.Run("periodic: executes once per elapsed period", CountsOverASweep);
        T.Run("periodic: PERIOD n apply also executes at application", ExecuteOnApply);
        T.Run("periodic: a long frame executes at most 8 periods and drops the surplus", CapPerUpdate);
        T.Run("periodic: modifiers hit Base, not the aggregator", HitsBase);
        T.Run("periodic: an inhibited effect skips its executions", InhibitedSkips);
        T.Run("periodic: the effect ends with its duration and stops executing", StopsAtExpiry);
    });

    private static void CountsOverASweep()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef hp = T.Attr("Health");
        T.Apply(s, "Fx.Poison");
        T.At(100.5);
        T.CheckValue("nothing before the first period", 100f, s.GetValue(hp));
        T.At(101.0);
        T.CheckValue("one execution at +1 s", 97f, s.GetValue(hp));
        T.At(103.2);
        T.CheckValue("two more by +3.2 s", 91f, s.GetValue(hp));
        T.At(103.9);
        T.CheckValue("none between periods", 91f, s.GetValue(hp));
        T.At(104.0);
        T.CheckValue("the fourth at +4 s", 88f, s.GetValue(hp));
    }

    private static void ExecuteOnApply()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef hp = T.Attr("Health");
        T.Apply(s, "Fx.PoisonNow");
        T.CheckValue("executed at application", 97f, s.GetValue(hp));
        T.At(101.0);
        T.CheckValue("and again after a period", 94f, s.GetValue(hp));
        T.Reset();
        s = T.Hero(Defs);
        T.Apply(s, "Fx.Poison");
        T.CheckValue("without 'apply' nothing happens at application", 100f, s.GetValue(T.Attr("Health")));
    }

    private static void CapPerUpdate()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        T.Apply(s, "Fx.Bleed");
        T.At(110.5);
        T.CheckValue("ten periods elapsed, eight executed", 2f, s.GetValue(speed));
        T.At(110.9);
        T.CheckValue("the surplus was dropped, not queued", 2f, s.GetValue(speed));
        T.At(111.0);
        T.CheckValue("the schedule realigned to +11 s", 1f, s.GetValue(speed));
    }

    private static void HitsBase()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        ActiveEffectHandle h = T.Apply(s, "Fx.Bleed");
        T.At(103.0);
        T.CheckValue("Base moved", 7f, s.GetBase(speed));
        T.CheckValue("Current agrees", 7f, s.GetValue(speed));
        s.Remove(h);
        T.CheckValue("removing the effect keeps what it did", 7f, s.GetValue(speed));
        T.Advance(5);
        T.CheckValue("and it stopped", 7f, s.GetValue(speed));
    }

    private static void InhibitedSkips()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        T.Apply(s, "Fx.Gated");
        T.CheckValue("no gate tag: inhibited", 10f, s.GetValue(speed));
        T.At(103.5);
        T.CheckValue("three periods passed while inhibited, none executed", 10f, s.GetValue(speed));
        s.AddLooseTag(T.Tag("State.Gate"));
        T.At(104.0);
        T.CheckValue("with the gate open the next period executes", 9f, s.GetValue(speed));
    }

    private static void StopsAtExpiry()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef hp = T.Attr("Health");
        T.Apply(s, "Fx.Poison");
        T.At(105.0);
        T.CheckValue("five executions after five seconds", 85f, s.GetValue(hp));
        T.At(110.0);
        T.CheckValue("ten executions over ten seconds (the last at the expiry instant)", 70f, s.GetValue(hp));
        T.Check("gone", s.ActiveEffects.Count == 0);
        T.Advance(5);
        T.CheckValue("no more after expiry", 70f, s.GetValue(hp));
    }
}
