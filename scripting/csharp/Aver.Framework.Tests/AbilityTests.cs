// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Abilities: every failure in its documented order, cost and cooldown, commit, tag counts, triggers and behaviours.

using Aver.Framework;

static class AbilityTests
{
    private const string Defs =
        "EFFECT Fx.Cost\n  POLICY instant\n  MOD Mana add -20\n" +
        "EFFECT Fx.CD\n  POLICY duration 5\n  GRANTS Cd.Fireball\n" +
        "EFFECT Fx.CD2\n  POLICY duration 5\n  GRANTS Cd.Manual\n" +
        "ABILITY Ab.Fireball\n  TAGS Ability.Cast.Fire\n  OWNS State.Casting\n  BLOCKS Ability.Cast\n  REQUIRES State.Alive\n" +
        "  BLOCKEDBY State.Stunned\n  COST Fx.Cost\n  COOLDOWN Fx.CD\n" +
        "ABILITY Ab.Ice\n  TAGS Ability.Cast.Ice\n" +
        "ABILITY Ab.Manual\n  COMMIT manual\n  COST Fx.Cost\n  COOLDOWN Fx.CD2\n" +
        "ABILITY Ab.Timed\n  DURATION 2\n" +
        "ABILITY Ab.Retrig\n  RETRIGGER on\n  OWNS State.Retrig\n" +
        "ABILITY Ab.OnHit\n  TRIGGER event Event.Hit\n" +
        "ABILITY Ab.Passive\n  TRIGGER granted\n  OWNS State.Passive\n" +
        "ABILITY Ab.Canceller\n  TAGS Ability.Dash\n  CANCELS Ability.Cast\n" +
        "ABILITY Ab.Holder\n  TAGS Ability.Hold\n  OWNS State.Holding\n  BLOCKS Ability.Foo\n" +
        "ABILITY Ab.Foo\n  TAGS Ability.Foo\n" +
        "ABILITY Ab.Rec\n  BEHAVIOUR RecBeh\n" +
        "ABILITY Ab.Refuse\n  BEHAVIOUR RefuseBeh\n";

    public static int RunAll() => T.Suite(() =>
    {
        T.Run("abilities: failure codes are pinned", PinnedCodes);
        T.Run("abilities: every failure is reachable, in the documented order", FailureOrder);
        T.Run("abilities: a failed cost check applies nothing", CostCheckAppliesNothing);
        T.Run("abilities: activation pays cost and starts the cooldown", ActivationCommits);
        T.Run("abilities: manual commit is atomic and fails clean", ManualCommit);
        T.Run("abilities: the cooldown blocks until it expires, then allows", CooldownTiming);
        T.Run("abilities: owned and blocking tag counts balance after End and Revoke", TagCountsBalance);
        T.Run("abilities: CancelsTags ends running abilities", CancelsTags);
        T.Run("abilities: Retrigger restarts a running ability", Retrigger);
        T.Run("abilities: Duration auto-ends the ability", AutoEnd);
        T.Run("abilities: gameplay events and granted triggers", Triggers);
        T.Run("abilities: C# behaviour hooks run in order", BehaviourHooks);
        T.Run("abilities: a behaviour can refuse activation", BehaviourRefuses);
        T.Run("abilities: Grant is idempotent per ability", GrantIdempotent);
        T.Run("abilities: TryActivateByTag and CancelAbilities", ByTag);
    });

    private static AbilityHandle Grant(AbilitySystem s, string name) => s.Grant(name);

    private static void PinnedCodes()
    {
        T.Check("None", (int)AbilityFailure.None == 0);
        T.Check("NoSystem", (int)AbilityFailure.NoSystem == 1);
        T.Check("NotGranted", (int)AbilityFailure.NotGranted == 2);
        T.Check("AlreadyActive", (int)AbilityFailure.AlreadyActive == 3);
        T.Check("MissingRequiredTag", (int)AbilityFailure.MissingRequiredTag == 4);
        T.Check("BlockedByTag", (int)AbilityFailure.BlockedByTag == 5);
        T.Check("BlockedByAbility", (int)AbilityFailure.BlockedByAbility == 6);
        T.Check("OnCooldown", (int)AbilityFailure.OnCooldown == 7);
        T.Check("CannotAfford", (int)AbilityFailure.CannotAfford == 8);
        T.Check("BehaviourRefused", (int)AbilityFailure.BehaviourRefused == 9);
        T.Check("NoAuthority", (int)AbilityFailure.NoAuthority == 10);
    }

    private static void FailureOrder()
    {
        AbilitySystem s = T.Hero(Defs);
        T.Check("NoSystem (through the graph entry point)", !GasGraph.TryActivateForGraph(77, 0, "Ab.Fireball", 0, out int reason) && reason == 1);
        T.Check("NotGranted", s.TryActivate(default) == AbilityFailure.NotGranted);
        AbilityHandle fire = Grant(s, "Ab.Fireball"), ice = Grant(s, "Ab.Ice");
        T.Check("a granted ability exists", fire.IsValid && ice.IsValid && fire != ice);

        GameplayTag alive = T.Tag("State.Alive"), stunned = T.Tag("State.Stunned");
        s.AddLooseTag(stunned);
        T.Check("required tags are checked before blockers", s.TryActivate(fire) == AbilityFailure.MissingRequiredTag);
        s.AddLooseTag(alive);
        T.Check("BlockedByTag", s.TryActivate(fire) == AbilityFailure.BlockedByTag);
        s.RemoveLooseTag(stunned);

        T.Check("it activates", s.TryActivate(fire) == AbilityFailure.None && s.IsActive(fire));
        T.Check("BlockedByAbility (a running ability blocks Ability.Cast)", s.TryActivate(ice) == AbilityFailure.BlockedByAbility);
        T.Check("AlreadyActive is checked first", s.TryActivate(fire) == AbilityFailure.AlreadyActive);
        s.End(fire);

        s.SetBase(T.Attr("Mana"), 0f);
        T.Check("cooldown is checked before cost", s.TryActivate(fire) == AbilityFailure.OnCooldown);
        T.At(105.0);
        T.Check("CannotAfford once the cooldown is over", s.TryActivate(fire) == AbilityFailure.CannotAfford);

        AbilityHandle refuse = Grant(s, "Ab.Refuse");
        T.Check("BehaviourRefused", s.TryActivate(refuse) == AbilityFailure.BehaviourRefused);

        s.HasAuthority = false;
        T.Check("NoAuthority", s.TryActivate(ice) == AbilityFailure.NoAuthority && s.CanActivate(ice) == AbilityFailure.NoAuthority);
        s.HasAuthority = true;
        T.Check("CanActivate predicts without activating", s.CanActivate(ice) == AbilityFailure.None && !s.IsActive(ice));
    }

    private static void CostCheckAppliesNothing()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle fire = Grant(s, "Ab.Fireball");
        s.AddLooseTag(T.Tag("State.Alive"));
        s.SetBase(T.Attr("Mana"), 10f);
        T.Check("cannot afford 20 with 10", s.TryActivate(fire) == AbilityFailure.CannotAfford);
        T.CheckValue("Mana untouched", 10f, s.GetValue(T.Attr("Mana")));
        T.Check("no cooldown started", s.ActiveEffects.Count == 0 && !s.HasTag(T.Tag("Cd.Fireball")));
        T.Check("not active, no owned tag", !s.IsActive(fire) && !s.HasTag(T.Tag("State.Casting")));
        s.SetBase(T.Attr("Mana"), 20f);
        T.Check("exactly enough is enough", s.TryActivate(fire) == AbilityFailure.None);
        T.CheckValue("spent down to the minimum", 0f, s.GetValue(T.Attr("Mana")));
    }

    private static void ActivationCommits()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle fire = Grant(s, "Ab.Fireball");
        s.AddLooseTag(T.Tag("State.Alive"));
        T.Check("activates", s.TryActivate(fire) == AbilityFailure.None);
        T.CheckValue("cost paid", 30f, s.GetValue(T.Attr("Mana")));
        T.Check("cooldown tag held", s.HasTag(T.Tag("Cd.Fireball")));
        T.Check("owned tag held", s.HasTag(T.Tag("State.Casting")));
        T.CheckValue("cooldown remaining", 5f, s.CooldownRemaining(fire));
    }

    private static void ManualCommit()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle m = Grant(s, "Ab.Manual");
        GameplayTag cd = T.Tag("Cd.Manual");
        T.Check("activates", s.TryActivate(m) == AbilityFailure.None);
        T.CheckValue("nothing paid yet", 50f, s.GetValue(T.Attr("Mana")));
        T.Check("no cooldown yet", !s.HasTag(cd));
        T.Check("Commit", s.Commit(m));
        T.CheckValue("cost paid", 30f, s.GetValue(T.Attr("Mana")));
        T.Check("cooldown started", s.HasTag(cd));
        T.Check("a second Commit is refused", !s.Commit(m));
        T.CheckValue("...and charged nothing", 30f, s.GetValue(T.Attr("Mana")));
        s.End(m);

        T.At(105.0);
        s.SetBase(T.Attr("Mana"), 25f);
        T.Check("activates again", s.TryActivate(m) == AbilityFailure.None);
        s.SetBase(T.Attr("Mana"), 5f);
        T.Check("Commit fails once the cost can no longer be paid", !s.Commit(m));
        T.CheckValue("nothing was charged", 5f, s.GetValue(T.Attr("Mana")));
        T.Check("and no cooldown was started", !s.HasTag(cd));
        T.Check("the ability keeps running", s.IsActive(m));
    }

    private static void CooldownTiming()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle fire = Grant(s, "Ab.Fireball");
        s.AddLooseTag(T.Tag("State.Alive"));
        s.TryActivate(fire);
        s.End(fire);
        T.Advance(2);
        T.CheckValue("3 s left after 2 s", 3f, s.CooldownRemaining(fire));
        T.Check("on cooldown", s.TryActivate(fire) == AbilityFailure.OnCooldown);
        T.At(104.9);
        T.Check("still on cooldown at 104.9", s.TryActivate(fire) == AbilityFailure.OnCooldown);
        T.At(105.0);
        T.CheckValue("no cooldown left", 0f, s.CooldownRemaining(fire));
        T.Check("allowed at 105", s.TryActivate(fire) == AbilityFailure.None);
    }

    private static void TagCountsBalance()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle hold = Grant(s, "Ab.Holder"), foo = Grant(s, "Ab.Foo");
        GameplayTag holding = T.Tag("State.Holding");
        s.TryActivate(hold);
        T.Check("owned tag held while running", s.HasTag(holding));
        T.Check("blocked while running", s.CanActivate(foo) == AbilityFailure.BlockedByAbility);
        s.End(hold);
        T.Check("owned tag released on End", !s.HasTag(holding) && s.TagCount(holding) == 0);
        T.Check("block released on End", s.CanActivate(foo) == AbilityFailure.None);

        s.TryActivate(hold);
        T.Check("Revoke while running", s.Revoke(hold));
        T.Check("owned tag released on Revoke", !s.HasTag(holding));
        T.Check("block released on Revoke", s.CanActivate(foo) == AbilityFailure.None);
        T.Check("the handle is dead", !s.IsActive(hold) && s.TryActivate(hold) == AbilityFailure.NotGranted);

        AbilityHandle re = Grant(s, "Ab.Retrig");
        for (int i = 0; i < 4; ++i) s.TryActivate(re);
        T.Check("retriggering does not leak owned-tag counts", s.TagCount(T.Tag("State.Retrig")) == 1);
        s.End(re);
        T.Check("...and one End releases it", s.TagCount(T.Tag("State.Retrig")) == 0);
    }

    private static void CancelsTags()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle ice = Grant(s, "Ab.Ice"), dash = Grant(s, "Ab.Canceller");
        var ended = new List<(int Handle, bool Cancelled)>();
        s.AbilityEnded += (h, c) => ended.Add((h.Value, c));
        s.TryActivate(ice);
        T.Check("ice is running", s.IsActive(ice));
        T.Check("the canceller activates", s.TryActivate(dash) == AbilityFailure.None);
        T.Check("ice was cancelled", !s.IsActive(ice) && ended.Count == 1 && ended[0] == (ice.Value, true));
        T.Check("the canceller itself keeps running", s.IsActive(dash));
    }

    private static void Retrigger()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle re = Grant(s, "Ab.Retrig");
        int activated = 0;
        var ended = new List<bool>();
        s.AbilityActivated += (h, c) => activated++;
        s.AbilityEnded += (h, c) => ended.Add(c);
        T.Check("first activation", s.TryActivate(re) == AbilityFailure.None);
        T.Check("second activation is not AlreadyActive", s.TryActivate(re) == AbilityFailure.None);
        T.Check("two activations, the first run was cancelled", activated == 2 && ended.Count == 1 && ended[0]);
        T.Check("running", s.IsActive(re));
    }

    private static void AutoEnd()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle timed = Grant(s, "Ab.Timed");
        var ended = new List<bool>();
        s.AbilityEnded += (h, c) => ended.Add(c);
        s.TryActivate(timed);
        T.At(101.9);
        T.Check("running at 101.9", s.IsActive(timed) && ended.Count == 0);
        T.At(102.0);
        T.Check("ended at 102, not cancelled", !s.IsActive(timed) && ended.Count == 1 && !ended[0]);
        s.TryActivate(timed);
        s.End(timed);
        T.At(110.0);
        T.Check("an early End leaves nothing to fire later", ended.Count == 2);
    }

    private static void Triggers()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle hit = Grant(s, "Ab.OnHit");
        T.Check("not running before the event", !s.IsActive(hit));
        T.Check("an unrelated event triggers nothing", s.SendGameplayEvent(T.Tag("Event.Miss")) == 0);
        T.Check("a descendant of the trigger tag triggers it (hierarchical)", s.SendGameplayEvent(T.Tag("Event.Hit.Crit")) == 1 && s.IsActive(hit));
        T.Check("already running: the second event activates nothing", s.SendGameplayEvent(T.Tag("Event.Hit")) == 0);

        AbilityHandle passive = Grant(s, "Ab.Passive");
        T.Check("a passive ability activates when granted", s.IsActive(passive) && s.HasTag(T.Tag("State.Passive")));
    }

    private static void BehaviourHooks()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle rec = Grant(s, "Ab.Rec");
        T.Check("granted hook", RecordingBehaviour.Trail.SequenceEqual(new[] { "granted" }), string.Join(",", RecordingBehaviour.Trail));
        s.TryActivate(rec);
        s.End(rec);
        s.TryActivate(rec);
        s.End(rec, cancelled: true);
        s.Revoke(rec);
        string[] expected = { "granted", "can", "activate", "end:False", "can", "activate", "end:True", "revoked" };
        T.Check("hooks in order", RecordingBehaviour.Trail.SequenceEqual(expected), string.Join(",", RecordingBehaviour.Trail));

        RecordingBehaviour.Trail.Clear();
        rec = Grant(s, "Ab.Rec");
        s.TryActivate(rec);
        s.Revoke(rec);
        T.Check("revoking a running ability cancels it first",
                RecordingBehaviour.Trail.SequenceEqual(new[] { "granted", "can", "activate", "end:True", "revoked" }),
                string.Join(",", RecordingBehaviour.Trail));
    }

    private static void BehaviourRefuses()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle refuse = Grant(s, "Ab.Refuse");
        T.Check("refused", s.TryActivate(refuse) == AbilityFailure.BehaviourRefused && !s.IsActive(refuse));
        T.Check("a registered behaviour name raises no missing-behaviour warning", T.WarnCount("no [AverAbility]") == 0 && T.WarnCount("which no") == 0);
    }

    private static void GrantIdempotent()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle a = Grant(s, "Ab.Rec");
        AbilityHandle b = Grant(s, "Ab.Rec");
        T.Check("the same handle", a == b);
        T.Check("one granted hook", RecordingBehaviour.Trail.Count == 1);
        T.Check("Find", s.Find("Ab.Rec") == a && !s.Find("Ab.Ice").IsValid);
        T.Check("an unknown ability grants nothing", !s.Grant("Ab.Nope").IsValid && T.WarnCount("no ability named") == 1);
    }

    private static void ByTag()
    {
        AbilitySystem s = T.Hero(Defs);
        AbilityHandle fire = Grant(s, "Ab.Fireball"), ice = Grant(s, "Ab.Ice");
        s.AddLooseTag(T.Tag("State.Alive"));
        AbilityHandle got = s.TryActivateByTag(T.Tag("Ability.Cast"), default, out AbilityFailure f);
        T.Check("the first granted ability whose tag matches activates", got == fire && f == AbilityFailure.None && s.IsActive(fire));
        got = s.TryActivateByTag(T.Tag("Ability.Cast"), default, out f);
        T.Check("nothing else can activate; the first failure comes back", !got.IsValid && f != AbilityFailure.None);
        T.Check("CancelAbilities by tag", s.CancelAbilities(T.Tag("Ability.Cast")) == 1 && !s.IsActive(fire));
        T.Check("an unmatched tag", s.CancelAbilities(T.Tag("Ability.Nothing")) == 0);
        T.Check("ice is still grantable and idle", !s.IsActive(ice));
    }
}
