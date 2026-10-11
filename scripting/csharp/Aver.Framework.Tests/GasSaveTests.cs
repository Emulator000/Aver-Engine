// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The "gas" save payload and the managed-state provider callbacks.

using System.Runtime.InteropServices;
using System.Text;
using Aver.Framework;

static class GasSaveTests
{
    private const string Defs =
        "EFFECT Fx.Poison\n  POLICY duration 5\n  PERIOD 0.5\n  STACK target 3\n  MOD Health add -3\n  MOD Speed add -1\n  GRANTS Status.SavePoisoned\n" +
        "EFFECT Fx.Regen\n  POLICY infinite\n  MOD Mana add 2\n" +
        "EFFECT Fx.Hit\n  POLICY duration 10\n  MOD Speed add caller=Data.SaveMag scale=2\n" +
        "ABILITY Ab.Save\n  TAGS Ability.Save\n" +
        "ABILITY Ab.Passive2\n  TRIGGER granted\n  OWNS State.SavePassive\n";

    public static int RunAll() => T.Suite(() =>
    {
        T.Run("save: Write then Read into a fresh system reproduces everything byte for byte", RoundTrip);
        T.Run("save: unknown definitions are skipped with one warning each", UnknownDefinitions);
        T.Run("save: a bad header refuses the whole payload", BadHeader);
        T.Run("save: the provider callbacks carry the payload, including the retry for a small buffer", ProviderCallbacks);
        T.Run("save: a restore raises no notifications", QuietRestore);
        T.Run("save: ActorBeginPlay does not re-default a restored system", NoRedefault);
        T.Run("save: a passive ability does not re-run on load", PassiveNotRerun);
        T.Run("save: only entities with a system have a channel", ChannelCount);
    });

    private static AbilitySystem Build()
    {
        AbilitySystem s = T.Hero(Defs);
        s.SetBase(T.Attr("Health"), 73.5f);
        s.AddLooseTag(T.Tag("State.SaveAlive"));
        s.AddLooseTag(T.Tag("State.SaveStack"), 2);
        s.Grant("Ab.Save", 2);
        s.Apply(EffectSpec.Of("Fx.Poison").From(T.E(1)));
        s.Apply(EffectSpec.Of("Fx.Poison").From(T.E(1)));
        T.At(100.75);
        s.Apply(EffectSpec.Of("Fx.Regen").From(T.E(9)));
        s.Apply(EffectSpec.Of("Fx.Hit").SetByCaller(T.Tag("Data.SaveMag"), 4f));
        return s;
    }

    private static void RoundTrip()
    {
        AbilitySystem s = Build();
        string text1 = s.SaveText();
        string[] expected =
        {
            "GASSTATE 1", "SET Character", "ATTR Health 67.5", "ATTR Speed 8", "LOOSE State.SaveAlive 1", "LOOSE State.SaveStack 2",
            "GRANT Ab.Save 2",
        };
        foreach (string line in expected) T.Check($"the payload has '{line}'", text1.Split('\n').Contains(line), text1);
        T.Check("poison: stacks, remaining, period, resolved magnitudes, source", text1.Contains("EFFECT Fx.Poison stacks=2 remaining=4.25 period=0.25 mags=-3,-1 source=self"), text1);
        T.Check("regen: infinite, no period, source none", text1.Contains("EFFECT Fx.Regen stacks=1 remaining=inf mags=2 source=none"), text1);
        T.Check("the SetByCaller magnitude is the resolved one", text1.Contains("mags=8 source=none"), text1);

        AbilitySystem copy;
        T.Check("Read succeeds", GasSave.Read(T.E(2), text1));
        copy = AbilitySystem.Of(T.E(2))!;
        T.Check("a second Write is identical", copy.SaveText() == text1, copy.SaveText());
        foreach (string a in new[] { "Health", "MaxHealth", "Mana", "Speed" })
            T.CheckValue($"Current {a} recomputed", s.GetValue(T.Attr(a)), copy.GetValue(T.Attr(a)));
        T.Check("loose tags", copy.TagCount(T.Tag("State.SaveStack")) == 2 && copy.HasTag(T.Tag("State.SaveAlive")));
        T.Check("the effect's granted tag came back with the effect", copy.HasTag(T.Tag("Status.SavePoisoned")));
        T.Check("grants with their level", copy.Find("Ab.Save").IsValid);
        T.Check("effects", copy.ActiveEffects.Count == 3 && copy.ActiveEffects.Select(e => e.Stacks).OrderBy(x => x).SequenceEqual(new[] { 1, 1, 2 }));

        // The restored effects keep running on the game clock from where they were.
        T.At(101.0);
        T.CheckValue("the restored poison fires on its remaining period (-3 x2, -1 x2)", 61.5f, copy.GetValue(T.Attr("Health")));
        T.At(105.0);
        T.Check("and expires on its remaining time", !copy.ActiveEffects.Any(e => e.Def.Name == "Fx.Poison"));
    }

    private static void UnknownDefinitions()
    {
        T.Hero(Defs);
        const string payload = "GASSTATE 1\nSET Character\nSET NoSuchSet\nATTR Health 50\nATTR Ghost 5\nATTR Ghost 6\nGRANT Ab.Nope 1\n" +
                               "EFFECT Fx.Gone stacks=1 remaining=3 source=none\nEFFECT Fx.Gone stacks=2 remaining=3 source=none\n" +
                               "LOOSE State.Kept 1\n";
        T.Check("the payload is accepted", GasSave.Read(T.E(7), payload));
        AbilitySystem r = AbilitySystem.Of(T.E(7))!;
        T.CheckValue("known state restored", 50f, r.GetValue(T.Attr("Health")));
        T.Check("known loose tag restored", r.HasTag(T.Tag("State.Kept")));
        T.Check("one warning for the set", T.WarnCount("attribute set 'NoSuchSet'") == 1);
        T.Check("one warning for the attribute, though it appears twice", T.WarnCount("attribute 'Ghost'") == 1);
        T.Check("one warning for the ability", T.WarnCount("ability 'Ab.Nope'") == 1);
        T.Check("one warning for the effect, though it appears twice", T.WarnCount("effect 'Fx.Gone'") == 1);
        T.Check("nothing unknown was created", r.ActiveEffects.Count == 0 && !r.Find("Ab.Nope").IsValid);
    }

    private static void BadHeader()
    {
        T.Reset();
        T.Check("a wrong version", !GasSave.Read(T.E(3), "GASSTATE 2\nSET Character\n"));
        T.Check("no system was created for it", AbilitySystem.Of(T.E(3)) == null);
        T.Check("an empty payload", !GasSave.Read(T.E(3), ""));
        T.Check("a different format", !GasSave.Read(T.E(3), "OCGAS 1\n"));
        T.Check("an invalid entity", !GasSave.Read(default, "GASSTATE 1\n"));
        T.Check("each refusal warned", T.WarnCount("unsupported header") == 3);

        AbilitySystem s = T.Hero(Defs);
        s.SetBase(T.Attr("Speed"), 77f);
        T.Check("a refused payload leaves an existing system alone", !GasSave.Read(T.E(1), "NOPE\n") && s.GetBase(T.Attr("Speed")) == 77f);
        T.Check("a valid payload replaces its state", GasSave.Read(T.E(1), "GASSTATE 1\nSET Character\nATTR Speed 3\n") && s.GetBase(T.Attr("Speed")) == 3f);
    }

    private static void ProviderCallbacks()
    {
        AbilitySystem s = Build();
        string text = s.SaveText();
        IntPtr name = Marshal.AllocHGlobal(64), data = Marshal.AllocHGlobal(65536);
        try
        {
            T.Check("the entity has one channel", GasSave.StateCount(1, IntPtr.Zero) == 1);
            int len = GasSave.StateAt(1, 0, name, 64, data, 65536, IntPtr.Zero);
            T.Check("the channel is named gas", Marshal.PtrToStringUTF8(name) == "gas");
            byte[] expected = Encoding.UTF8.GetBytes(text);
            T.Check("the full length is returned", len == expected.Length, $"{len} vs {expected.Length}");
            var got = new byte[len];
            Marshal.Copy(data, got, 0, len);
            T.Check("the payload, newlines included, arrives intact", Encoding.UTF8.GetString(got) == text && text.Contains('\n'));

            int small = GasSave.StateAt(1, 0, name, 4, data, 16, IntPtr.Zero);
            T.Check("a small buffer still gets the FULL length, so the caller can retry", small == expected.Length);
            T.Check("a short name buffer is terminated", Marshal.PtrToStringUTF8(name) == "gas");
            T.Check("a bad index is -1", GasSave.StateAt(1, 1, name, 64, data, 65536, IntPtr.Zero) == -1 && GasSave.StateAt(99, 0, name, 64, data, 65536, IntPtr.Zero) == -1);

            IntPtr chan = Marshal.StringToCoTaskMemUTF8("gas");
            IntPtr other = Marshal.StringToCoTaskMemUTF8("quest");
            try
            {
                Marshal.Copy(expected, 0, data, expected.Length);
                T.Check("StateSet accepts the gas channel", GasSave.StateSet(4, chan, data, expected.Length, IntPtr.Zero) == 1);
                T.Check("...and rebuilds the same state", AbilitySystem.Of(T.E(4))?.SaveText() == text);
                T.Check("another channel is not ours", GasSave.StateSet(4, other, data, expected.Length, IntPtr.Zero) == 0);
                T.Check("a negative length is refused", GasSave.StateSet(4, chan, data, -1, IntPtr.Zero) == 0);
                byte[] junk = Encoding.UTF8.GetBytes("not a payload");
                Marshal.Copy(junk, 0, data, junk.Length);
                T.Check("a bad payload answers 0", GasSave.StateSet(8, chan, data, junk.Length, IntPtr.Zero) == 0 && AbilitySystem.Of(T.E(8)) == null);
            }
            finally
            {
                Marshal.FreeCoTaskMem(chan);
                Marshal.FreeCoTaskMem(other);
            }
        }
        finally
        {
            Marshal.FreeHGlobal(name);
            Marshal.FreeHGlobal(data);
        }
    }

    private static void QuietRestore()
    {
        AbilitySystem s = Build();
        string text = s.SaveText();
        T.Host.ListenToAll = true;
        T.Host.Events.Clear();
        int fired = 0;
        AbilitySystem.GetOrAdd(T.E(2)).AttributeChanged += _ => fired++;
        T.Check("restore into a system that already has a listener", GasSave.Read(T.E(2), text));
        T.Check("no bus events and no C# events", T.Host.Events.Count == 0 && fired == 0, $"{T.Host.Events.Count} events, {fired} callbacks");
        T.Check("the listener stays attached for later changes", AbilitySystem.Of(T.E(2))!.SetBase(T.Attr("Speed"), 99f) && fired == 1);
    }

    private static void NoRedefault()
    {
        T.Reset();
        T.Load(Defs);
        GameplayDefs.SetActorDefaults(new ActorDefaultsDef
        {
            ClassName = "SaveHero",
            AttributeSets = { "Character" },
            Grants = { new KeyValuePair<string, int>("Ab.Save", 1) },
            Tags = { "State.Defaulted" },
        });
        T.Host.Classes[5] = "SaveHero";
        T.Host.Classes[6] = "SaveHero";
        T.Check("a restored system exists before begin play", GasSave.Read(T.E(5), "GASSTATE 1\nSET Character\n"));
        GameplayAbilities.ActorBeginPlay(5);
        AbilitySystem restored = AbilitySystem.Of(T.E(5))!;
        T.Check("the restored system was not given the class defaults", !restored.HasTag(T.Tag("State.Defaulted")) && !restored.Find("Ab.Save").IsValid);

        GameplayAbilities.ActorBeginPlay(6);
        AbilitySystem fresh = AbilitySystem.Of(T.E(6))!;
        T.Check("a fresh actor gets them", fresh != null && fresh.HasTag(T.Tag("State.Defaulted")) && fresh.Find("Ab.Save").IsValid && fresh.Has(T.Attr("Health")));
        GameplayAbilities.ActorBeginPlay(6);
        T.Check("and only once", fresh!.TagCount(T.Tag("State.Defaulted")) == 1);
        T.Host.Classes[7] = "Unrelated";
        GameplayAbilities.ActorBeginPlay(7);
        T.Check("a class with no defaults gets no system", AbilitySystem.Of(T.E(7)) == null);
    }

    private static void PassiveNotRerun()
    {
        AbilitySystem s = T.Hero(Defs);
        s.Grant("Ab.Passive2");
        T.Check("granted: the passive runs", s.HasTag(T.Tag("State.SavePassive")));
        string text = s.SaveText();
        T.Check("restore", GasSave.Read(T.E(2), text));
        AbilitySystem copy = AbilitySystem.Of(T.E(2))!;
        T.Check("the grant is back, but the run is not (ability runs are not saved)", copy.Find("Ab.Passive2").IsValid && !copy.HasTag(T.Tag("State.SavePassive")));
    }

    private static void ChannelCount()
    {
        T.Reset();
        T.Check("no system, no channel", GasSave.StateCount(1, IntPtr.Zero) == 0);
        AbilitySystem.GetOrAdd(T.E(1));
        T.Check("a system, one channel", GasSave.StateCount(1, IntPtr.Zero) == 1);
        AbilitySystem.Remove(T.E(1));
        T.Check("removed again", GasSave.StateCount(1, IntPtr.Zero) == 0);
    }
}
