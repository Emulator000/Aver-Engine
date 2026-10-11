// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Modifier evaluation: Base, Add sum, Multiply product, Override, clamp; instant vs lasting effects; stacks.

using Aver.Framework;

static class EffectModifierTests
{
    private const string Defs =
        "EFFECT Fx.AddTen\n  POLICY infinite\n  MOD Speed add 10\n" +
        "EFFECT Fx.AddFive\n  POLICY infinite\n  MOD Speed add 5\n" +
        "EFFECT Fx.Double\n  POLICY infinite\n  MOD Speed multiply 2\n" +
        "EFFECT Fx.Triple\n  POLICY infinite\n  MOD Speed multiply 3\n" +
        "EFFECT Fx.Set50\n  POLICY infinite\n  MOD Speed override 50\n" +
        "EFFECT Fx.Set7\n  POLICY infinite\n  MOD Speed override 7\n" +
        "EFFECT Fx.Huge\n  POLICY infinite\n  MOD Health add 500\n" +
        "EFFECT Fx.Hit\n  POLICY instant\n  MOD Health add -30\n" +
        "EFFECT Fx.Halve\n  POLICY instant\n  MOD Speed multiply 0.5\n" +
        "EFFECT Fx.Pin\n  POLICY instant\n  MOD Speed override 3\n" +
        "EFFECT Fx.Stk\n  POLICY infinite\n  STACK target 3\n  MOD Speed add 4\n" +
        "EFFECT Fx.StkMul\n  POLICY infinite\n  STACK target 3\n  MOD Speed multiply 2\n" +
        "EFFECT Fx.StkSet\n  POLICY infinite\n  STACK target 3\n  MOD Speed override 9\n";

    public static int RunAll() => T.Suite(() =>
    {
        T.Run("modifiers: Add sums, Multiply multiplies the sum, in that order", AddThenMultiply);
        T.Run("modifiers: the most recently applied Override wins", OverrideWins);
        T.Run("modifiers: the result is clamped to the attribute's bounds", ClampsResult);
        T.Run("modifiers: instant effects change Base and persist", InstantChangesBase);
        T.Run("modifiers: lasting effects change Current only and unwind on removal", LastingChangesCurrent);
        T.Run("modifiers: stacks scale Add and Multiply, not Override", StackScaling);
        T.Run("modifiers: AttributeBased and SetByCaller magnitudes", Magnitudes);
    });

    private static void AddThenMultiply()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        T.Apply(s, "Fx.AddTen");
        T.CheckValue("10 + 10", 20f, s.GetValue(speed));
        T.Apply(s, "Fx.AddFive");
        T.CheckValue("10 + 10 + 5", 25f, s.GetValue(speed));
        ActiveEffectHandle dbl = T.Apply(s, "Fx.Double");
        T.CheckValue("(10 + 15) * 2", 50f, s.GetValue(speed));
        T.Apply(s, "Fx.Triple");
        T.CheckValue("(10 + 15) * 2 * 3", 150f, s.GetValue(speed));
        s.Remove(dbl);
        T.CheckValue("remove the doubler", 75f, s.GetValue(speed));
    }

    private static void OverrideWins()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        T.Apply(s, "Fx.AddTen");
        ActiveEffectHandle a = T.Apply(s, "Fx.Set50");
        T.CheckValue("an override replaces the sum", 50f, s.GetValue(speed));
        ActiveEffectHandle b = T.Apply(s, "Fx.Set7");
        T.CheckValue("the later override wins", 7f, s.GetValue(speed));
        s.Remove(b);
        T.CheckValue("remove it and the earlier one returns", 50f, s.GetValue(speed));
        s.Remove(a);
        T.CheckValue("remove that and the sum returns", 20f, s.GetValue(speed));
    }

    private static void ClampsResult()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef hp = T.Attr("Health");
        ActiveEffectHandle h = T.Apply(s, "Fx.Huge");
        T.CheckValue("100 + 500 clamps to MaxHealth", 100f, s.GetValue(hp));
        T.CheckValue("Base is untouched", 100f, s.GetBase(hp));
        s.Remove(h);
        T.CheckValue("back to 100", 100f, s.GetValue(hp));
    }

    private static void InstantChangesBase()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef hp = T.Attr("Health"), speed = T.Attr("Speed");
        ActiveEffectHandle h = T.Apply(s, "Fx.Hit");
        T.Check("an instant effect leaves no handle", !h.IsValid);
        T.CheckValue("Base 100 - 30", 70f, s.GetBase(hp));
        T.CheckValue("Current follows", 70f, s.GetValue(hp));
        T.Check("it is not an active effect", s.ActiveEffects.Count == 0);
        T.Apply(s, "Fx.Halve");
        T.CheckValue("instant multiply scales Base", 5f, s.GetBase(speed));
        T.Apply(s, "Fx.Pin");
        T.CheckValue("instant override sets Base", 3f, s.GetBase(speed));
    }

    private static void LastingChangesCurrent()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        ActiveEffectHandle h = T.Apply(s, "Fx.AddTen");
        T.CheckValue("Base is untouched", 10f, s.GetBase(speed));
        T.CheckValue("Current is Base + 10", 20f, s.GetValue(speed));
        T.Check("it is an active effect", s.ActiveEffects.Count == 1 && s.TryGetEffect(h, out ActiveEffectInfo info) && info.Remaining == float.PositiveInfinity);
        s.Remove(h);
        T.CheckValue("removal restores Current", 10f, s.GetValue(speed));
        T.Check("the effect is gone", s.ActiveEffects.Count == 0 && !s.Remove(h));
    }

    private static void StackScaling()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        for (int i = 0; i < 3; ++i) T.Apply(s, "Fx.Stk");
        T.CheckValue("three stacks of +4", 22f, s.GetValue(speed));
        s.RemoveByDef(GameplayDefs.FindEffect("Fx.Stk")!);
        T.CheckValue("cleared", 10f, s.GetValue(speed));

        T.Apply(s, "Fx.StkMul");
        T.CheckValue("one stack of x2", 20f, s.GetValue(speed));
        T.Apply(s, "Fx.StkMul");
        T.CheckValue("two stacks: 1 + (2-1)*2 = x3", 30f, s.GetValue(speed));
        T.Apply(s, "Fx.StkMul");
        T.CheckValue("three stacks: x4", 40f, s.GetValue(speed));
        s.RemoveByDef(GameplayDefs.FindEffect("Fx.StkMul")!);

        for (int i = 0; i < 3; ++i) T.Apply(s, "Fx.StkSet");
        T.CheckValue("override ignores stacks", 9f, s.GetValue(speed));
    }

    private const string MagDefs =
        "EFFECT Fx.Caller\n  POLICY duration 10\n  MOD Speed add caller=Data.Amount scale=2\n" +
        "EFFECT Fx.FromTarget\n  POLICY infinite\n  MOD Speed add from=target.Mana scale=0.5 add=1\n" +
        "EFFECT Fx.FromSource\n  POLICY infinite\n  MOD Speed add from=source.Mana scale=1\n";

    private static void Magnitudes()
    {
        AbilitySystem s = T.Hero(MagDefs);
        AttributeRef speed = T.Attr("Speed");
        ActiveEffectHandle c = s.Apply(EffectSpec.Of("Fx.Caller").SetByCaller(T.Tag("Data.Amount"), 4f));
        T.CheckValue("caller 4 x scale 2", 18f, s.GetValue(speed));
        s.Remove(c);
        T.Check("no warning when the caller supplied it", T.WarnCount("SetByCaller") == 0);
        s.Apply(EffectSpec.Of("Fx.Caller"));
        T.CheckValue("a missing caller value reads 0", 10f, s.GetValue(speed));
        T.Check("...and warns once", T.WarnCount("SetByCaller") == 1);

        s.Apply(EffectSpec.Of("Fx.FromTarget"));
        T.CheckValue("target.Mana 50 x 0.5 + 1", 36f, s.GetValue(speed));
        s.SetBase(T.Attr("Mana"), 10f);
        T.CheckValue("snapshotted: later changes do not move it", 36f, s.GetValue(speed));

        AbilitySystem other = AbilitySystem.GetOrAdd(T.E(2));
        other.AddAttributeSet("Character");
        other.SetBase(T.Attr("Mana"), 30f);
        s.Apply(EffectSpec.Of("Fx.FromSource").From(T.E(2)));
        T.CheckValue("source.Mana 30 added", 66f, s.GetValue(speed));
    }
}
