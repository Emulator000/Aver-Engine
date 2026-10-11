// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Attributes: defaults, clamps, dependent bounds, meta attributes, hooks and notification batching.

using Aver.Framework;

static class AttributeTests
{
    public static int RunAll() => T.Suite(() =>
    {
        T.Run("attributes: defaults at AddAttributeSet", Defaults);
        T.Run("attributes: clamp to literal min and max", LiteralClamp);
        T.Run("attributes: MaxFrom clamps, shrinking re-clamps Base down and growing never raises it", DependentBounds);
        T.Run("attributes: cyclic bounds are refused", Cycles);
        T.Run("attributes: a duplicate name across sets is refused", Duplicates);
        T.Run("attributes: PreAttributeChange can clamp Current", PreChange);
        T.Run("attributes: notifications fire at batch end, in order, after the whole effect applied", Deferred);
        T.Run("attributes: handler re-entry past 8 drops further notifications with one warning", NestingGuard);
        T.Run("attributes: meta attributes forward and read 0", MetaForward);
        T.Run("attributes: a meta attribute without forward is announced", MetaAnnounce);
        T.Run("attributes: unknown set and bad set are reported", UnknownSet);
    });

    private static void Defaults()
    {
        AbilitySystem s = T.Hero();
        T.CheckValue("Health", 100f, s.GetValue(T.Attr("Health")));
        T.CheckValue("Mana", 50f, s.GetValue(T.Attr("Mana")));
        T.CheckValue("Base equals default", 100f, s.GetBase(T.Attr("Health")));
        T.Check("Has", s.Has(T.Attr("Speed")) && !s.Has(T.Attr("Nonexistent")));
        T.Check("a meta attribute reads 0", s.GetValue(T.Attr("Damage")) == 0f);
        T.Check("adding the same set again succeeds without effect", s.AddAttributeSet("Character") && s.Attributes.Count() == 6);
    }

    private static void LiteralClamp()
    {
        AbilitySystem s = T.Hero();
        AttributeRef mana = T.Attr("Mana");
        s.SetBase(mana, -5f);
        T.CheckValue("min=0 holds", 0f, s.GetValue(mana));
        T.CheckValue("...and Base itself is clamped", 0f, s.GetBase(mana));
        s.SetBase(mana, 500f);
        T.CheckValue("max=MaxMana holds", 50f, s.GetValue(mana));
        s.SetBase(T.Attr("Speed"), 1e6f);
        T.CheckValue("an unbounded attribute takes any value", 1e6f, s.GetValue(T.Attr("Speed")));
        T.Check("SetBase on a meta attribute is refused", !s.SetBase(T.Attr("Damage"), 5f));
        T.Check("SetBase on an unknown attribute is refused", !s.SetBase(T.Attr("Nonexistent"), 5f));
    }

    private static void DependentBounds()
    {
        AbilitySystem s = T.Hero();
        AttributeRef hp = T.Attr("Health"), max = T.Attr("MaxHealth");
        s.SetBase(hp, 500f);
        T.CheckValue("Health clamps to MaxHealth", 100f, s.GetValue(hp));
        s.SetBase(max, 60f);
        T.CheckValue("shrinking MaxHealth re-clamps Health's Base down", 60f, s.GetBase(hp));
        T.CheckValue("...and its Current", 60f, s.GetValue(hp));
        s.SetBase(max, 200f);
        T.CheckValue("growing MaxHealth does not raise Health", 60f, s.GetValue(hp));
        s.SetBase(hp, 150f);
        T.CheckValue("Health can now rise to the new bound", 150f, s.GetValue(hp));
    }

    private static void Cycles()
    {
        AbilitySystem s = T.Hero();
        AttributeSetDef cyc = GameplayDefs.AttributeSet("Cyc")
            .Attribute("CycA", 0f, minFrom: "CycB")
            .Attribute("CycB", 0f, maxFrom: "CycA").Build();
        T.Check("AddAttributeSet refuses a cycle", !s.AddAttributeSet(cyc));
        T.Check("...and logs it", T.Logs.Any(l => l.Message.Contains("cyclic")));
        T.Check("nothing was added", !s.Has(T.Attr("CycA")));
        T.Check("Register refuses it too", GameplayDefs.AttributeSet("Cyc2").Attribute("CycC", 0f, minFrom: "CycD").Attribute("CycD", 0f, minFrom: "CycC").Register() == null);
    }

    private static void Duplicates()
    {
        AbilitySystem s = T.Hero();
        AttributeSetDef dup = GameplayDefs.AttributeSet("Other").Attribute("Health", 5f).Attribute("Armor", 1f).Build();
        T.Check("a set redefining Health is refused", !s.AddAttributeSet(dup));
        T.Check("...and none of its attributes were added", !s.Has(T.Attr("Armor")));
    }

    private static void PreChange()
    {
        AbilitySystem s = T.Hero();
        AttributeRef speed = T.Attr("Speed");
        s.PreAttributeChange = (AbilitySystem sys, AttributeRef a, ref float v) =>
        {
            if (a == speed && v > 20f) v = 20f;
        };
        s.SetBase(speed, 99f);
        T.CheckValue("Current went through the hook", 20f, s.GetValue(speed));
        T.CheckValue("Base keeps what was set", 99f, s.GetBase(speed));
    }

    private static void Deferred()
    {
        AbilitySystem s = T.Hero("EFFECT Fx.Both\n  POLICY instant\n  MOD Health add -10\n  MOD Mana add -5\n");
        AttributeRef hp = T.Attr("Health"), mana = T.Attr("Mana");
        var seen = new List<(string Name, float HealthThen, float ManaThen)>();
        s.AttributeChanged += c => seen.Add((c.Attribute.Name, s.GetValue(hp), s.GetValue(mana)));
        T.Apply(s, "Fx.Both");
        T.Check("two changes", seen.Count == 2, $"got {seen.Count}");
        if (seen.Count == 2)
        {
            T.Check("in modifier order", seen[0].Name == "Health" && seen[1].Name == "Mana");
            T.Check("each handler saw the fully applied effect", seen.All(x => T.Near(x.HealthThen, 90f) && T.Near(x.ManaThen, 45f)));
        }
    }

    private static void NestingGuard()
    {
        AbilitySystem s = T.Hero();
        AttributeRef speed = T.Attr("Speed");
        int calls = 0;
        s.AttributeChanged += c =>
        {
            if (c.Attribute != speed) return;
            calls++;
            s.SetBase(speed, c.NewValue + 1f);
        };
        s.SetBase(speed, 100f);
        T.Check("the cascade stopped", calls >= 1 && calls <= 12, $"handler ran {calls} times");
        T.Check("one warning only", T.WarnCount("re-entered") == 1, $"{T.WarnCount("re-entered")} warnings");
        T.Check("the value reached the cascade's end", s.GetValue(speed) >= 100f);
        T.Check("the system still works afterwards", s.SetBase(T.Attr("Mana"), 10f) && T.Near(s.GetValue(T.Attr("Mana")), 10f));
    }

    private static void MetaForward()
    {
        AbilitySystem s = T.Hero("EFFECT Fx.Hit\n  POLICY instant\n  MOD Damage add 30\n");
        AttributeRef hp = T.Attr("Health"), dmg = T.Attr("Damage");
        var changes = new List<string>();
        s.AttributeChanged += c => changes.Add(c.Attribute.Name);
        T.Apply(s, "Fx.Hit");
        T.CheckValue("30 Damage became -30 Health (scale -1)", 70f, s.GetValue(hp));
        T.CheckValue("the meta attribute reads 0 again", 0f, s.GetValue(dmg));
        T.Check("only Health announced a change", changes.Count == 1 && changes[0] == "Health");
    }

    private static void MetaAnnounce()
    {
        AbilitySystem s = T.Hero();
        var set = GameplayDefs.AttributeSet("Bare").Meta("Incoming").Build();
        s.AddAttributeSet(set);
        GameplayDefs.Effect("Fx.Poke").Instant().Modify("Incoming", ModOp.Add, 7f).Register();
        var seen = new List<AttributeChange>();
        s.AttributeChanged += seen.Add;
        T.Apply(s, "Fx.Poke");
        T.Check("one announcement with old 0 and new 7", seen.Count == 1 && seen[0].OldValue == 0f && seen[0].NewValue == 7f);
        T.CheckValue("it reads 0 afterwards", 0f, s.GetValue(T.Attr("Incoming")));
    }

    private static void UnknownSet()
    {
        AbilitySystem s = T.Hero();
        T.Check("unknown set name", !s.AddAttributeSet("NoSuchSet"));
        T.Check("...is logged", T.WarnCount("unknown attribute set") == 1);
    }
}
