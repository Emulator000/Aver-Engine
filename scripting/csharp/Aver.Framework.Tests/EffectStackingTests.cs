// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Stacking: none, per target, per source; refresh vs keep; expire clear vs one; periodic execution per stack.

using Aver.Framework;

static class EffectStackingTests
{
    private const string Defs =
        "EFFECT Fx.Indep\n  POLICY infinite\n  STACK none\n  MOD Speed add 1\n" +
        "EFFECT Fx.Stk\n  POLICY duration 10\n  STACK target 3 refresh\n  MOD Speed add 2\n" +
        "EFFECT Fx.StkKeep\n  POLICY duration 10\n  STACK target 3 keep\n  MOD Speed add 2\n" +
        "EFFECT Fx.StkOne\n  POLICY duration 10\n  STACK target 3 refresh expire one\n  MOD Speed add 2\n" +
        "EFFECT Fx.Src\n  POLICY infinite\n  STACK source 2\n  MOD Speed add 1\n" +
        "EFFECT Fx.StkPoison\n  POLICY duration 10\n  PERIOD 1\n  STACK target 3\n  MOD Speed add -1\n";

    public static int RunAll() => T.Suite(() =>
    {
        T.Run("stacking: none keeps independent instances", NoneIsIndependent);
        T.Run("stacking: target N increments to N, then refuses with StackLimit", TargetLimit);
        T.Run("stacking: refresh resets the expiry, keep does not", RefreshVsKeep);
        T.Run("stacking: expire one drops a stack and restarts the duration", ExpireOne);
        T.Run("stacking: source N separates sources", SourceScope);
        T.Run("stacking: periodic executions apply once per stack", PeriodicPerStack);
    });

    private static void NoneIsIndependent()
    {
        AbilitySystem s = T.Hero(Defs);
        ActiveEffectHandle a = T.Apply(s, "Fx.Indep");
        ActiveEffectHandle b = T.Apply(s, "Fx.Indep");
        T.Check("two instances with different handles", s.ActiveEffects.Count == 2 && a != b);
        T.CheckValue("both count", 12f, s.GetValue(T.Attr("Speed")));
        s.Remove(a);
        T.CheckValue("removing one leaves the other", 11f, s.GetValue(T.Attr("Speed")));
    }

    private static void TargetLimit()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        ActiveEffectHandle first = default;
        for (int i = 1; i <= 3; ++i)
        {
            ActiveEffectHandle h = s.Apply(EffectSpec.Of("Fx.Stk"), out EffectFailure f);
            if (i == 1) first = h;
            T.Check($"application {i} succeeds on the same instance", f == EffectFailure.None && h == first);
            T.Check($"stacks = {i}", s.TryGetEffect(h, out ActiveEffectInfo info) && info.Stacks == i);
        }
        T.CheckValue("three stacks of +2", 16f, s.GetValue(speed));
        ActiveEffectHandle again = s.Apply(EffectSpec.Of("Fx.Stk"), out EffectFailure full);
        T.Check("the fourth is refused with StackLimit", full == EffectFailure.StackLimit);
        T.Check("it still names the instance", again == first);
        T.Check("stacks stay at 3 and there is one instance", s.ActiveEffects.Count == 1 && s.ActiveEffects[0].Stacks == 3);
        T.CheckValue("value unchanged", 16f, s.GetValue(speed));
    }

    private static void RefreshVsKeep()
    {
        AbilitySystem s = T.Hero(Defs);
        T.Apply(s, "Fx.Stk");
        T.Apply(s, "Fx.StkKeep");
        T.At(104.0);
        T.Apply(s, "Fx.Stk");
        T.Apply(s, "Fx.StkKeep");
        T.At(110.0);
        T.Check("keep: expired at the original time", !s.TryGetEffect(GameplayDefs.FindEffect("Fx.StkKeep")!, out _));
        T.Check("refresh: still alive", s.TryGetEffect(GameplayDefs.FindEffect("Fx.Stk")!, out ActiveEffectInfo info) && T.Near(info.Remaining, 4f));
        T.At(114.0);
        T.Check("refresh: expired 10 s after the second application", !s.TryGetEffect(GameplayDefs.FindEffect("Fx.Stk")!, out _));
        T.CheckValue("all stacks went", 10f, s.GetValue(T.Attr("Speed")));
    }

    private static void ExpireOne()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        T.Apply(s, "Fx.StkOne");
        T.Apply(s, "Fx.StkOne");
        T.CheckValue("two stacks", 14f, s.GetValue(speed));
        T.At(110.0);
        T.Check("one stack left", s.ActiveEffects.Count == 1 && s.ActiveEffects[0].Stacks == 1);
        T.CheckValue("value follows", 12f, s.GetValue(speed));
        T.Check("the duration restarted", T.Near(s.ActiveEffects[0].Remaining, 10f));
        T.At(119.9);
        T.Check("still there before the restart ends", s.ActiveEffects.Count == 1);
        T.At(120.0);
        T.Check("gone when the last stack runs out", s.ActiveEffects.Count == 0);
        T.CheckValue("back to base", 10f, s.GetValue(speed));
    }

    private static void SourceScope()
    {
        AbilitySystem s = T.Hero(Defs);
        T.Apply(s, "Fx.Src", source: 10);
        T.Apply(s, "Fx.Src", source: 10);
        T.Check("one instance for source 10 with two stacks", s.ActiveEffects.Count == 1 && s.ActiveEffects[0].Stacks == 2);
        T.Apply(s, "Fx.Src", source: 11);
        T.Check("source 11 gets its own instance", s.ActiveEffects.Count == 2);
        s.Apply(EffectSpec.Of("Fx.Src").From(T.E(10)), out EffectFailure f);
        T.Check("source 10 is at its limit of 2", f == EffectFailure.StackLimit);
        T.CheckValue("2 + 1 stacks", 13f, s.GetValue(T.Attr("Speed")));
        T.Check("each instance remembers its source", s.ActiveEffects.Select(e => e.Source.Handle).OrderBy(x => x).SequenceEqual(new[] { 10, 11 }));
    }

    private static void PeriodicPerStack()
    {
        AbilitySystem s = T.Hero(Defs);
        T.Apply(s, "Fx.StkPoison");
        T.Apply(s, "Fx.StkPoison");
        T.At(101.0);
        T.CheckValue("two stacks, one period: -2", 8f, s.GetValue(T.Attr("Speed")));
        T.Apply(s, "Fx.StkPoison");
        T.At(102.0);
        T.CheckValue("three stacks, one more period: -3", 5f, s.GetValue(T.Attr("Speed")));
    }
}
