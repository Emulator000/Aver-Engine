// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The replication seam: authority, the sink's callbacks and forwarded activation requests. Nothing is networked.

using Aver.Framework;

static class GasReplicationSeamTests
{
    private const string Defs =
        "EFFECT Fx.Buff\n  POLICY duration 5\n  MOD Speed add 1\n" +
        "EFFECT Fx.Hit\n  POLICY instant\n  MOD Health add -5\n" +
        "ABILITY Ab.Rep\n  TAGS Ability.Rep\n";

    private sealed class RecordingSink : IGasReplicationSink
    {
        public readonly List<string> Calls = new();
        public float LastBase;
        public float[]? LastMags;
        public void AttributeBase(AbilitySystem system, AttributeRef attribute, float newBase) { Calls.Add("base:" + attribute.Name); LastBase = newBase; }
        public void EffectAdded(AbilitySystem system, ActiveEffectInfo info, float[] magnitudes) { Calls.Add("effect+:" + info.Def.Name); LastMags = magnitudes; }
        public void EffectChanged(AbilitySystem system, ActiveEffectInfo info, float[] magnitudes) => Calls.Add("effect~:" + info.Def.Name);
        public void EffectRemoved(AbilitySystem system, ActiveEffectInfo info, float[] magnitudes) => Calls.Add("effect-:" + info.Def.Name);
        public void LooseTag(AbilitySystem system, GameplayTag tag, int count) => Calls.Add($"loose:{tag.Name}={count}");
        public void AbilityGranted(AbilitySystem system, AbilityHandle handle, string name, int level) => Calls.Add($"grant:{name}@{level}");
        public void AbilityRevoked(AbilitySystem system, AbilityHandle handle, string name, int level) => Calls.Add("revoke:" + name);
        public void AbilityActivated(AbilitySystem system, AbilityHandle handle) => Calls.Add("activate");
        public void AbilityEnded(AbilitySystem system, AbilityHandle handle, bool cancelled) => Calls.Add("end:" + cancelled);
    }

    private sealed class RecordingRequests : IGasClientRequests
    {
        public readonly List<(AbilityHandle Handle, int Target)> Requests = new();
        public void RequestActivate(AbilitySystem system, AbilityHandle handle, Entity target) => Requests.Add((handle, target.Handle));
    }

    public static int RunAll() => T.Suite(() =>
    {
        T.Run("replication: a non-authoritative system refuses every mutating call", RefusesWithoutAuthority);
        T.Run("replication: the sink hears each authoritative change once, with plain data", SinkCallbacks);
        T.Run("replication: a client's activation request is forwarded instead of failing", ForwardedRequests);
        T.Run("replication: reads work on a proxy", ReadsStillWork);
    });

    private static void RefusesWithoutAuthority()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle h = s.Grant("Ab.Rep");
        ActiveEffectHandle active = s.Apply(EffectSpec.Of("Fx.Buff"));
        s.HasAuthority = false;

        s.Apply(EffectSpec.Of("Fx.Hit"), out EffectFailure f);
        T.Check("Apply: NoAuthority", f == EffectFailure.NoAuthority);
        T.CheckValue("...and nothing changed", 100f, s.GetValue(T.Attr("Health")));
        T.Check("SetBase", !s.SetBase(T.Attr("Speed"), 99f) && s.GetBase(T.Attr("Speed")) == 10f);
        s.AddLooseTag(T.Tag("Rep.Tag"));
        T.Check("AddLooseTag does nothing", !s.HasTag(T.Tag("Rep.Tag")));
        T.Check("Remove effect", !s.Remove(active) && s.ActiveEffects.Count == 1);
        T.Check("RemoveByTag / RemoveByDef", s.RemoveByTag(T.Tag("Anything")) == 0 && s.RemoveByDef(GameplayDefs.FindEffect("Fx.Buff")!) == 0);
        T.Check("Grant", !s.Grant("Ab.Rep", 5).IsValid);
        T.Check("Revoke", !s.Revoke(h));
        T.Check("TryActivate: NoAuthority", s.TryActivate(h) == AbilityFailure.NoAuthority);
        T.Check("Commit / End / Cancel", !s.Commit(h) && !s.End(h) && s.CancelAbilities(T.Tag("Ability.Rep")) == 0);
        T.Check("SendGameplayEvent", s.SendGameplayEvent(T.Tag("Event.Rep")) == 0);
        T.Check("AddAttributeSet", !s.AddAttributeSet("Character"));

        s.HasAuthority = true;
        T.Check("with authority again, Apply works", s.Apply(EffectSpec.Of("Fx.Hit"), out f) == default && f == EffectFailure.None);
    }

    private static void SinkCallbacks()
    {
        AbilitySystem s = T.Hero(Defs);
        var sink = new RecordingSink();
        GameplayAbilities.Replication = sink;

        s.SetBase(T.Attr("Speed"), 55f);
        T.Check("SetBase: one AttributeBase with the new Base", sink.Calls.SequenceEqual(new[] { "base:Speed" }) && sink.LastBase == 55f, string.Join(",", sink.Calls));

        sink.Calls.Clear();
        ActiveEffectHandle h = s.Apply(EffectSpec.Of("Fx.Buff"));
        T.Check("a lasting effect: EffectAdded once, with its magnitudes", sink.Calls.SequenceEqual(new[] { "effect+:Fx.Buff" }) && sink.LastMags != null && sink.LastMags.Length == 1 && sink.LastMags[0] == 1f, string.Join(",", sink.Calls));

        sink.Calls.Clear();
        s.Remove(h);
        T.Check("removal: EffectRemoved once", sink.Calls.SequenceEqual(new[] { "effect-:Fx.Buff" }), string.Join(",", sink.Calls));

        sink.Calls.Clear();
        s.Apply(EffectSpec.Of("Fx.Hit"));
        T.Check("an instant effect changes a Base: one AttributeBase", sink.Calls.SequenceEqual(new[] { "base:Health" }), string.Join(",", sink.Calls));

        sink.Calls.Clear();
        s.AddLooseTag(T.Tag("Rep.Loose"), 2);
        T.Check("loose tag: the new loose count", sink.Calls.SequenceEqual(new[] { "loose:Rep.Loose=2" }), string.Join(",", sink.Calls));

        sink.Calls.Clear();
        AbilityHandle a = s.Grant("Ab.Rep", 4);
        T.Check("grant", sink.Calls.SequenceEqual(new[] { "grant:Ab.Rep@4" }), string.Join(",", sink.Calls));
        sink.Calls.Clear();
        s.TryActivate(a);
        s.End(a, cancelled: true);
        T.Check("activate then end", sink.Calls.SequenceEqual(new[] { "activate", "end:True" }), string.Join(",", sink.Calls));
        sink.Calls.Clear();
        s.Revoke(a);
        T.Check("revoke", sink.Calls.SequenceEqual(new[] { "revoke:Ab.Rep" }), string.Join(",", sink.Calls));

        sink.Calls.Clear();
        GasSave.Read(T.E(2), s.SaveText());
        T.Check("a restore is not a replicated change", sink.Calls.Count == 0, string.Join(",", sink.Calls));
    }

    private static void ForwardedRequests()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle h = s.Grant("Ab.Rep");
        s.HasAuthority = false;
        T.Check("without a request sink it fails", s.TryActivate(h, T.E(5)) == AbilityFailure.NoAuthority);
        var requests = new RecordingRequests();
        GameplayAbilities.ClientRequests = requests;
        T.Check("with one, the call is forwarded and does not fail", s.TryActivate(h, T.E(5)) == AbilityFailure.None);
        T.Check("it received the handle and target", requests.Requests.Count == 1 && requests.Requests[0].Handle == h && requests.Requests[0].Target == 5);
        T.Check("nothing activated locally", !s.IsActive(h));
    }

    private static void ReadsStillWork()
    {
        AbilitySystem s = T.Hero(Defs);
        s.Apply(EffectSpec.Of("Fx.Buff"));
        s.HasAuthority = false;
        T.CheckValue("GetValue", 11f, s.GetValue(T.Attr("Speed")));
        T.Check("ActiveEffects, HasTag, Describe", s.ActiveEffects.Count == 1 && !s.HasTag(T.Tag("Nothing.Here")) && s.Describe().Contains("[proxy]"));
    }
}
