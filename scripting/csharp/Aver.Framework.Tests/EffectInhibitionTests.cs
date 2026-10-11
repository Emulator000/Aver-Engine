// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Ongoing requirements (inhibition) and counted tag grants.

using Aver.Framework;

static class EffectInhibitionTests
{
    private const string Defs =
        "EFFECT Fx.Gated\n  POLICY infinite\n  MOD Speed add 5\n  GRANTS Buff.Gated\n  ONGOING_REQUIRES State.Armed\n" +
        "EFFECT Fx.Unless\n  POLICY infinite\n  MOD Speed add 5\n  ONGOING_BLOCKEDBY State.Dead\n" +
        "EFFECT Fx.GatedTimed\n  POLICY duration 5\n  MOD Speed add 5\n  ONGOING_REQUIRES State.Armed\n" +
        "EFFECT Fx.GrantA\n  POLICY infinite\n  GRANTS Shared.Tag\n" +
        "EFFECT Fx.GrantB\n  POLICY infinite\n  GRANTS Shared.Tag\n" +
        "EFFECT Fx.Chain\n  POLICY infinite\n  GRANTS Chain.Tag\n  ONGOING_REQUIRES State.Armed\n" +
        "EFFECT Fx.ChainDependent\n  POLICY infinite\n  MOD Speed add 100\n  ONGOING_REQUIRES Chain.Tag\n";

    public static int RunAll() => T.Suite(() =>
    {
        T.Run("inhibition: ongoing-required toggles modifiers and granted tags as tags change", RequiredToggles);
        T.Run("inhibition: ongoing-blocked inhibits while the tag is present", BlockedToggles);
        T.Run("inhibition: the duration clock keeps running while inhibited", ClockUnaffected);
        T.Run("inhibition: two effects granting one tag keep it until both are gone", CountedGrants);
        T.Run("inhibition: a released effect's granted tag releases a dependent in the same batch", Cascade);
    });

    private static void RequiredToggles()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        GameplayTag armed = T.Tag("State.Armed"), gated = T.Tag("Buff.Gated");
        ActiveEffectHandle h = T.Apply(s, "Fx.Gated");
        T.CheckValue("requirement unmet: no modifier", 10f, s.GetValue(speed));
        T.Check("...no granted tag", !s.HasTag(gated));
        T.Check("...and reported as inhibited", s.TryGetEffect(h, out ActiveEffectInfo info) && info.Inhibited);
        s.AddLooseTag(armed);
        T.CheckValue("requirement met: modifier engaged", 15f, s.GetValue(speed));
        T.Check("...tag granted", s.HasTag(gated));
        T.Check("...no longer inhibited", s.TryGetEffect(h, out info) && !info.Inhibited);
        s.RemoveLooseTag(armed);
        T.CheckValue("requirement lost again", 10f, s.GetValue(speed));
        T.Check("...tag released", !s.HasTag(gated));
    }

    private static void BlockedToggles()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        T.Apply(s, "Fx.Unless");
        T.CheckValue("no blocker: engaged", 15f, s.GetValue(speed));
        s.AddLooseTag(T.Tag("State.Dead"));
        T.CheckValue("blocker present: inhibited", 10f, s.GetValue(speed));
        s.RemoveLooseTag(T.Tag("State.Dead"));
        T.CheckValue("blocker gone: engaged again", 15f, s.GetValue(speed));
    }

    private static void ClockUnaffected()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        GameplayTag armed = T.Tag("State.Armed");
        T.Apply(s, "Fx.GatedTimed");
        T.CheckValue("inhibited at first", 10f, s.GetValue(speed));
        T.At(102.0);
        s.AddLooseTag(armed);
        T.CheckValue("engaged mid-way", 15f, s.GetValue(speed));
        T.Check("3 s remain", T.Near(s.ActiveEffects[0].Remaining, 3f));
        s.RemoveLooseTag(armed);
        T.At(105.0);
        T.Check("it expired on schedule although inhibited", s.ActiveEffects.Count == 0);
        T.CheckValue("nothing left", 10f, s.GetValue(speed));
    }

    private static void CountedGrants()
    {
        AbilitySystem s = T.Hero(Defs);
        GameplayTag shared = T.Tag("Shared.Tag");
        ActiveEffectHandle a = T.Apply(s, "Fx.GrantA");
        ActiveEffectHandle b = T.Apply(s, "Fx.GrantB");
        T.Check("held, twice", s.HasTag(shared) && s.TagCount(shared) == 2);
        s.Remove(a);
        T.Check("one source gone: still held", s.HasTag(shared) && s.TagCount(shared) == 1);
        s.Remove(b);
        T.Check("both gone: released", !s.HasTag(shared) && s.TagCount(shared) == 0);

        var changes = new List<(string Tag, bool Present)>();
        s.TagChanged += (t, present, count) => { if (t.Name == "Shared.Tag") changes.Add((t.Name, present)); };
        a = T.Apply(s, "Fx.GrantA");
        b = T.Apply(s, "Fx.GrantB");
        s.Remove(a);
        s.Remove(b);
        T.Check("TagChanged fires on the flips only: on once, off once",
                changes.Count == 2 && changes[0] == ("Shared.Tag", true) && changes[1] == ("Shared.Tag", false),
                string.Join(",", changes));
    }

    private static void Cascade()
    {
        AbilitySystem s = T.Hero(Defs);
        AttributeRef speed = T.Attr("Speed");
        GameplayTag armed = T.Tag("State.Armed");
        T.Apply(s, "Fx.Chain");
        T.Apply(s, "Fx.ChainDependent");
        T.CheckValue("both inhibited", 10f, s.GetValue(speed));
        s.AddLooseTag(armed);
        T.CheckValue("arming engages Chain, whose tag engages the dependent", 110f, s.GetValue(speed));
        s.RemoveLooseTag(armed);
        T.CheckValue("disarming releases both", 10f, s.GetValue(speed));
    }
}
