// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The .ocgas text format: every field lands on its def, every mistake is refused with file:line, linking is global.

using Aver.Framework;

static class GameplayDefsParserTests
{
    public static int RunAll() => T.Suite(() =>
    {
        T.Run("ocgas: the fixture links and every field lands on its definition", FixtureFields);
        T.Run("ocgas: actor defaults and the second pass (abilities, cost, cooldown)", FixtureAbilities);
        T.Run("ocgas: each mistake is refused with a line-specific message and registers nothing", ErrorCases);
        T.Run("ocgas: too many modifiers and tags per line are refused", Limits);
        T.Run("ocgas: references may point forward across files", ForwardReferences);
        T.Run("ocgas: a failed file contributes nothing; the others still load", FailedFileIsolated);
        T.Run("ocgas: a duplicate against a code definition names both sources", CodeAndFileShareANamespace);
        T.Run("ocgas: directory loading is recursive and ordered", DirectoryLoad);
        T.Run("ocgas: builders produce what the parser produces", Builders);
        T.Run("ocgas: line endings, comments and case", Lexical);
    });

    private static string FixturePath() => Path.Combine(AppContext.BaseDirectory, "Fixtures", "combat.ocgas");

    private static void LoadFixture()
    {
        T.Reset();
        bool ok = GameplayDefs.LoadText(File.ReadAllText(FixturePath()), "combat.ocgas", out string err);
        T.Check("the fixture loads", ok, err);
    }

    private static void FixtureFields()
    {
        LoadFixture();
        AttributeSetDef? set = GameplayDefs.FindAttributeSet("Character");
        T.Check("the set exists with 7 attributes", set != null && set.Attributes.Count == 7);
        if (set == null) return;
        AttributeDef hp = set.Attributes[0];
        T.Check("Health: base 100, min 0, max from MaxHealth", hp.Name == "Health" && hp.DefaultBase == 100f && hp.Min == 0f && hp.MaxFrom.Name == "MaxHealth" && float.IsPositiveInfinity(hp.Max));
        T.Check("MaxHealth: min 1", set.Attributes[1].Min == 1f);
        AttributeDef dmg = set.Attributes[6];
        T.Check("Damage: meta, forwards to Health at -1", dmg.IsMeta && dmg.ForwardTo.Name == "Health" && dmg.ForwardScale == -1f);
        T.Check("TAG declared a tag and its parent", GameplayTag.TryFind("State.Stunned", out _) && GameplayTag.TryFind("State", out _));

        EffectDef? p = GameplayDefs.FindEffect("Effect.Poison");
        T.Check("Poison exists", p != null);
        if (p == null) return;
        T.Check("policy, duration, period, apply", p.Policy == EffectPolicy.Duration && p.Duration == 5f && p.Period == 1f && p.ExecuteOnApply);
        T.Check("four modifiers", p.Modifiers.Count == 4);
        ModifierDef m0 = p.Modifiers[0], m1 = p.Modifiers[1], m2 = p.Modifiers[2], m3 = p.Modifiers[3];
        T.Check("constant add -3", m0.Kind == MagnitudeKind.Constant && m0.Op == ModOp.Add && m0.Value == -3f && m0.Attribute.Name == "Health");
        T.Check("constant multiply 0.5", m1.Op == ModOp.Multiply && m1.Value == 0.5f && m1.Attribute.Name == "MoveSpeed");
        T.Check("SetByCaller with scale", m2.Kind == MagnitudeKind.SetByCaller && m2.CallerTag.Name == "Data.Damage" && m2.Value == -1f);
        T.Check("AttributeBased from the source", m3.Kind == MagnitudeKind.AttributeBased && m3.FromSource && m3.FromAttribute.Name == "Strength" && m3.Value == -0.5f && m3.Add == 1f);
        T.Check("asset and granted tags", p.AssetTags.Has(T.Tag("Effect.Damage.Poison")) && p.GrantedTags.Has(T.Tag("State.Poisoned")));
        T.Check("application requirements", p.AppRequired.Has(T.Tag("State.Alive")) && p.AppBlocked.Has(T.Tag("Immune.Poison")));
        T.Check("ongoing requirements", p.OngoingRequired.Has(T.Tag("State.Alive")) && p.OngoingBlocked.Has(T.Tag("State.Dead")));
        T.Check("removes", p.RemovesWithTags.Has(T.Tag("Effect.Poison.Weak")));
        T.Check("stacking", p.Stack == StackScope.Target && p.StackLimit == 3 && p.StackRefresh && p.StackExpire == StackExpire.ClearAll);
        T.Check("cue", p.Cue.Name == "Cue.Poison");
        T.Check("ids are assigned", p.Id >= 1 && GameplayDefs.FindEffectById(p.Id) == p);

        EffectDef regen = GameplayDefs.FindEffect("Effect.Regen")!;
        T.Check("infinite periodic with source stacking, keep, expire one",
                regen.Policy == EffectPolicy.Infinite && regen.Period == 2f && !regen.ExecuteOnApply && regen.Stack == StackScope.Source
                && regen.StackLimit == 2 && !regen.StackRefresh && regen.StackExpire == StackExpire.RemoveOne);
    }

    private static void FixtureAbilities()
    {
        LoadFixture();
        AbilityDef? f = GameplayDefs.FindAbility("Ability.Fireball");
        T.Check("Fireball exists", f != null);
        if (f == null) return;
        T.Check("tags", f.AbilityTags.Has(T.Tag("Ability.Cast.Fire")) && f.CancelsTags.Has(T.Tag("Ability.Cast.Ice")) && f.BlocksTags.Has(T.Tag("Ability.Cast")));
        T.Check("owned, required, blocked", f.OwnedTags.Has(T.Tag("State.Casting")) && f.RequiredTags.Has(T.Tag("State.Alive")) && f.BlockedTags.Has(T.Tag("State.Stunned")));
        T.Check("cost and cooldown resolved to the effect objects", f.Cost == GameplayDefs.FindEffect("Effect.FireballCost") && f.Cooldown == GameplayDefs.FindEffect("Effect.FireballCooldown"));
        T.Check("commit, duration, retrigger, passive, behaviour",
                f.Commit == AbilityCommit.Manual && f.Duration == 2.5f && f.Retrigger && f.TriggerOnGranted && f.Behaviour == "FireballBehaviour");
        T.Check("two trigger events", f.TriggerEvents.Has(T.Tag("Event.Hit")) && f.TriggerEvents.Has(T.Tag("Event.Cast")) && f.TriggerEvents.Count == 2);
        AbilityDef plain = GameplayDefs.FindAbility("Ability.Plain")!;
        T.Check("defaults: activate, no duration, no retrigger", plain.Commit == AbilityCommit.Activate && plain.Duration == 0f && !plain.Retrigger && !plain.TriggerOnGranted && plain.Cost == null);

        ActorDefaultsDef? hero = GameplayDefs.FindActorDefaults("Hero");
        T.Check("ACTORDEFAULTS landed", hero != null);
        if (hero == null) return;
        T.Check("sets", hero.AttributeSets.SequenceEqual(new[] { "Character" }));
        T.Check("base override", hero.BaseOverrides.Count == 1 && hero.BaseOverrides[0].Key == "Health" && hero.BaseOverrides[0].Value == 150f);
        T.Check("grants with and without a level", hero.Grants.Count == 2 && hero.Grants[0].Key == "Ability.Fireball" && hero.Grants[0].Value == 2 && hero.Grants[1].Value == 1);
        T.Check("apply and tags", hero.Applies.SequenceEqual(new[] { "Effect.Regen" }) && hero.Tags.SequenceEqual(new[] { "State.Alive", "State.Hero" }));
    }

    // (what, text, line, fragment, a name that must not exist afterwards)
    private static readonly (string What, string Text, int Line, string Fragment)[] Cases =
    {
        ("a missing header", "ATTRSET A\n", 1, "header"),
        ("an unsupported version", "OCGAS 2\n", 1, "version"),
        ("a record in the wrong block", "OCGAS 1\nATTRSET A\n  POLICY instant\n", 3, "not valid in an ATTRSET block"),
        ("a record outside any block", "OCGAS 1\nPOLICY instant\n", 2, "outside any block"),
        ("an unknown record", "OCGAS 1\nFOO bar\n", 2, "unknown record"),
        ("a bad number", "OCGAS 1\nATTRSET A\n  ATTR Hp abc\n", 3, "bad number"),
        ("a bad tag", "OCGAS 1\nTAG 1bad\n", 2, "invalid gameplay tag"),
        ("a duplicate effect in one file", "OCGAS 1\nEFFECT E\n  POLICY instant\nEFFECT E\n", 4, "duplicate effect"),
        ("a duplicate attribute", "OCGAS 1\nATTRSET A\n  ATTR Hp 1\n  ATTR Hp 2\n", 4, "duplicate attribute"),
        ("a modifier on an attribute no set defines", "OCGAS 1\nATTRSET A\n  ATTR Hp 1\nEFFECT E\n  MOD Nope add 1\n", 5, "no ATTRSET defines"),
        ("a bound naming an undefined attribute", "OCGAS 1\nATTRSET A\n  ATTR Hp 1 max=Ghost\n", 3, "undefined attribute 'Ghost'"),
        ("an undefined cost effect", "OCGAS 1\nABILITY Ab\n  COST Missing\n", 3, "undefined effect 'Missing'"),
        ("an undefined cooldown effect", "OCGAS 1\nABILITY Ab\n  COOLDOWN Missing\n", 3, "undefined effect 'Missing'"),
        ("a cost that is not instant", "OCGAS 1\nEFFECT Slow\n  POLICY duration 1\nABILITY Ab\n  COST Slow\n", 5, "must be an instant effect"),
        ("a cooldown that is instant", "OCGAS 1\nEFFECT Inst\n  POLICY instant\nABILITY Ab\n  COOLDOWN Inst\n", 5, "duration or infinite"),
        ("a period on an instant effect", "OCGAS 1\nEFFECT E\n  PERIOD 1\n", 3, "PERIOD needs POLICY duration or infinite"),
        ("a zero period", "OCGAS 1\nEFFECT E\n  POLICY infinite\n  PERIOD 0\n", 4, "greater than 0"),
        ("a bound cycle", "OCGAS 1\nATTRSET A\n  ATTR Aa 0 min=Bb\n  ATTR Bb 0 max=Aa\n", 3, "cyclic"),
        ("a granted ability that is undefined", "OCGAS 1\nACTORDEFAULTS H\n  GRANT Nope\n", 3, "undefined ability 'Nope'"),
        ("an applied effect that is undefined", "OCGAS 1\nACTORDEFAULTS H\n  APPLY Nope\n", 3, "undefined effect 'Nope'"),
        ("an unknown attribute set on a class", "OCGAS 1\nACTORDEFAULTS H\n  ATTRSET Nope\n", 3, "undefined attribute set 'Nope'"),
        ("a bad stack scope", "OCGAS 1\nEFFECT E\n  STACK sideways 3\n", 3, "unknown STACK scope"),
        ("a modifier with both caller and from", "OCGAS 1\nATTRSET A\n  ATTR Hp 1\nEFFECT E\n  MOD Hp add caller=X.Y from=source.Hp\n", 5, "exactly one of caller= or from="),
    };

    private static void ErrorCases()
    {
        foreach ((string what, string text, int line, string fragment) in Cases)
        {
            T.Reset();
            bool ok = GameplayDefs.LoadText(text, "test.ocgas", out string err);
            T.Check($"{what}: refused", !ok);
            T.Check($"{what}: names the line ({line})", err.Contains($"test.ocgas:{line}:"), err);
            T.Check($"{what}: says why", err.Contains(fragment), err);
            bool empty = GameplayDefs.FindEffect("E") == null && GameplayDefs.FindEffect("Slow") == null && GameplayDefs.FindAbility("Ab") == null
                         && GameplayDefs.FindAttributeSet("A") == null && GameplayDefs.FindActorDefaults("H") == null;
            T.Check($"{what}: nothing registered", empty);
        }
    }

    private static void Limits()
    {
        T.Reset();
        string many = "OCGAS 1\nATTRSET A\n  ATTR Hp 1\nEFFECT E\n" + string.Concat(Enumerable.Repeat("  MOD Hp add 1\n", 17));
        bool ok = GameplayDefs.LoadText(many, "test.ocgas", out string err);
        T.Check("17 modifiers refused at the 17th", !ok && err.Contains("test.ocgas:21:") && err.Contains("at most 16"), err);
        string tags = "OCGAS 1\nEFFECT E\n  GRANTS " + string.Join(' ', Enumerable.Range(0, 9).Select(i => "Lim.T" + i)) + "\n";
        ok = GameplayDefs.LoadText(tags, "test.ocgas", out err);
        T.Check("9 tags on one line refused", !ok && err.Contains("test.ocgas:3:") && err.Contains("more than 8"), err);
        tags = "OCGAS 1\nEFFECT E\n  GRANTS " + string.Join(' ', Enumerable.Range(0, 8).Select(i => "Lim.T" + i)) + "\n";
        T.Check("8 tags are fine", GameplayDefs.LoadText(tags, "test.ocgas", out err), err);
    }

    private static void ForwardReferences()
    {
        T.Reset();
        const string a = "OCGAS 1\nABILITY Fwd.Ability\n  COST Fwd.Cost\n  COOLDOWN Fwd.Cd\nACTORDEFAULTS FwdHero\n  ATTRSET FwdSet\n  GRANT Fwd.Ability\n  APPLY Fwd.Cost\n";
        const string b = "OCGAS 1\nATTRSET FwdSet\n  ATTR FwdHp 10\nEFFECT Fwd.Cost\n  POLICY instant\n  MOD FwdHp add -1\nEFFECT Fwd.Cd\n  POLICY duration 2\n  GRANTS Fwd.CdTag\n";
        bool ok = GameplayDefs.LoadTexts(new[] { ("a.ocgas", a), ("b.ocgas", b) }, out string err);
        T.Check("both files load", ok, err);
        AbilityDef? ab = GameplayDefs.FindAbility("Fwd.Ability");
        T.Check("the ability in the first file resolved its cost and cooldown from the second", ab != null && ab.Cost != null && ab.Cost.Name == "Fwd.Cost" && ab.Cooldown != null && ab.Cooldown.Name == "Fwd.Cd");
        T.Check("class defaults too", GameplayDefs.FindActorDefaults("FwdHero") != null);

        T.Reset();
        ok = GameplayDefs.LoadText(a, "a.ocgas", out err);
        T.Check("the first file alone cannot link", !ok && err.Contains("undefined effect 'Fwd.Cost'"), err);
    }

    private static void FailedFileIsolated()
    {
        T.Reset();
        const string good = "OCGAS 1\nATTRSET GoodSet\n  ATTR GoodHp 1\nEFFECT Good.Fx\n  POLICY instant\n  MOD GoodHp add 1\n";
        const string bad = "OCGAS 1\nEFFECT Bad.Fx\n  POLICY instant\n  MOD GoodHp add oops\n";
        const string unlinked = "OCGAS 1\nEFFECT Bad.Link\n  POLICY instant\n  MOD NoSuchAttr add 1\n";
        bool ok = GameplayDefs.LoadTexts(new[] { ("good.ocgas", good), ("bad.ocgas", bad), ("unlinked.ocgas", unlinked) }, out string err);
        T.Check("the call reports failure", !ok);
        T.Check("both problems are listed with their file names", err.Contains("bad.ocgas:4:") && err.Contains("unlinked.ocgas:4:"), err);
        T.Check("the good file loaded", GameplayDefs.FindEffect("Good.Fx") != null && GameplayDefs.FindAttributeSet("GoodSet") != null);
        T.Check("the bad ones did not", GameplayDefs.FindEffect("Bad.Fx") == null && GameplayDefs.FindEffect("Bad.Link") == null);

        T.Reset();
        const string leans = "OCGAS 1\nABILITY Leans\n  COST Bad.Fx2\n";
        const string broken = "OCGAS 1\nEFFECT Bad.Fx2\n  POLICY instant\n  MOD Nope add 1\n";
        ok = GameplayDefs.LoadTexts(new[] { ("leans.ocgas", leans), ("broken.ocgas", broken) }, out err);
        T.Check("a file that depends on a dropped one fails too", !ok && GameplayDefs.FindAbility("Leans") == null, err);
    }

    private static void CodeAndFileShareANamespace()
    {
        T.Reset();
        T.Check("a code effect registers", GameplayDefs.Effect("Dup.Eff").Instant().Modify("Health", ModOp.Add, 1f).Register() != null);
        bool ok = GameplayDefs.LoadText("OCGAS 1\nEFFECT Dup.Eff\n  POLICY instant\n", "dup.ocgas", out string err);
        T.Check("the file's duplicate is refused", !ok && err.Contains("dup.ocgas:2:"), err);
        T.Check("...naming the other source", err.Contains("already defined at code"), err);
        T.Check("a second code effect of that name is refused too", GameplayDefs.Effect("Dup.Eff").Instant().Register() == null);
    }

    private static void DirectoryLoad()
    {
        string root = Path.Combine(Path.GetTempPath(), "aver_gas_" + Guid.NewGuid().ToString("N"));
        try
        {
            T.Reset();
            T.Check("a missing folder is not an error", GameplayDefs.LoadDirectory(Path.Combine(root, "nope"), out string none) && none.Length == 0);

            Directory.CreateDirectory(Path.Combine(root, "sub"));
            File.WriteAllText(Path.Combine(root, "sub", "b.ocgas"), "OCGAS 1\nTAG Ord.Second\nABILITY Dir.B\n  COST Dir.CostA\n");
            File.WriteAllText(Path.Combine(root, "a.ocgas"), "OCGAS 1\nTAG Ord.First\nATTRSET DirSet\n  ATTR DirHp 5\nEFFECT Dir.CostA\n  POLICY instant\n  MOD DirHp add -1\n");
            File.WriteAllText(Path.Combine(root, "readme.txt"), "not a definition file");

            bool ok = GameplayDefs.LoadDirectory(root, out string err);
            T.Check("both files load, b leaning forward on a", ok && GameplayDefs.FindAbility("Dir.B")?.Cost?.Name == "Dir.CostA", err);
            T.Check("files load in ordinal path order: a.ocgas before sub/b.ocgas",
                    GameplayTag.TryFind("Ord.First", out GameplayTag first) && GameplayTag.TryFind("Ord.Second", out GameplayTag second) && first.Id < second.Id);

            File.WriteAllText(Path.Combine(root, "sub", "c.ocgas"), "OCGAS 1\nEFFECT Dir.Broken\n  MOD Missing add 1\n");
            T.Reset();
            ok = GameplayDefs.LoadDirectory(root, out err);
            T.Check("a broken file is reported with its folder-relative path", !ok && err.Contains("/sub/c.ocgas:3:"), err);
            T.Check("the other files still loaded", GameplayDefs.FindEffect("Dir.CostA") != null);
        }
        finally
        {
            try { Directory.Delete(root, recursive: true); } catch { }
        }
    }

    private static void Builders()
    {
        T.Reset();
        T.Check("attribute set builder", GameplayDefs.AttributeSet("Bld.Set").Attribute("BldHp", 80f, 0f, 100f).Attribute("BldMax", 100f, minFrom: "BldHp").Meta("BldIncoming", "BldHp", -1f).Register() != null);
        EffectDef? e = GameplayDefs.Effect("Bld.Poison").Duration(4f).Periodic(1f, executeOnApply: true)
            .Modify("BldHp", ModOp.Add, -2f).ModifyByCaller("BldHp", ModOp.Add, "Bld.Caller", -1f)
            .ModifyFromAttribute("BldHp", ModOp.Add, "BldMax", fromSource: true, scale: -0.1f, add: 1f)
            .GrantsTags("Bld.Poisoned").BlockedByTags("Bld.Immune").Stacking(StackScope.Target, 3, refresh: false, expire: StackExpire.RemoveOne)
            .Cue("Bld.Cue").Register();
        T.Check("effect builder", e != null && e.Policy == EffectPolicy.Duration && e.Duration == 4f && e.Period == 1f && e.ExecuteOnApply && e.Modifiers.Count == 3);
        if (e == null) return;
        T.Check("modifier kinds", e.Modifiers[1].Kind == MagnitudeKind.SetByCaller && e.Modifiers[2].Kind == MagnitudeKind.AttributeBased && e.Modifiers[2].FromSource);
        T.Check("tags, stacking and cue", e.GrantedTags.Has(T.Tag("Bld.Poisoned")) && e.AppBlocked.Has(T.Tag("Bld.Immune")) && e.Stack == StackScope.Target
                && e.StackLimit == 3 && !e.StackRefresh && e.StackExpire == StackExpire.RemoveOne && e.Cue.Name == "Bld.Cue");
        EffectDef? cost = GameplayDefs.Effect("Bld.Cost").Instant().Modify("BldHp", ModOp.Add, -5f).Register();
        AbilityDef? a = GameplayDefs.Ability("Bld.Strike").AbilityTags("Bld.Ability.Strike").Cost("Bld.Cost").Cooldown(e).Commit(AbilityCommit.Manual)
            .Duration(1.5f).TriggerOnEvent("Bld.Event").Retrigger().Behaviour("RecBeh").Register();
        T.Check("ability builder", a != null && a.Cost == cost && a.Cooldown == e && a.Commit == AbilityCommit.Manual && a.Duration == 1.5f && a.Retrigger && a.Behaviour == "RecBeh");
        T.Check("an undefined cost name is refused", GameplayDefs.Ability("Bld.Bad").Cost("Nope").Register() == null);
        T.Check("a periodic instant effect is refused", GameplayDefs.Effect("Bld.Bad").Instant().Periodic(1f).Register() == null);
        T.Check("a cooldown that grants nothing registers with a warning", GameplayDefs.Ability("Bld.NoTag").Cooldown(GameplayDefs.Effect("Bld.CdNoTag").Duration(1f).Register()!).Register() != null && T.WarnCount("grants no tag") == 1);

        // Code-built defs behave like file-built ones at runtime.
        AbilitySystem s = AbilitySystem.GetOrAdd(T.E(1));
        s.AddAttributeSet("Bld.Set");
        s.Apply(EffectSpec.Of("Bld.Cost"));
        T.CheckValue("applied through the registry", 75f, s.GetValue(T.Attr("BldHp")));
    }

    private static void Lexical()
    {
        T.Reset();
        const string text = "﻿OcGas 1\r\n# a comment\r\n\r\n  tag Lex.A   # trailing\r\nATTRSET LexSet\r\n\tattr LexHp 7\r\neffect Lex.E\r\n  policy DURATION 2\r\n  mod LexHp ADD 3\r\n";
        bool ok = GameplayDefs.LoadText(text, "lex.ocgas", out string err);
        T.Check("a BOM, CRLF, tabs, comments and lower-case keywords load", ok, err);
        EffectDef? e = GameplayDefs.FindEffect("Lex.E");
        T.Check("the effect parsed", e != null && e.Policy == EffectPolicy.Duration && e.Duration == 2f && e.Modifiers.Count == 1 && e.Modifiers[0].Value == 3f);
        T.Check("names are case-sensitive", GameplayDefs.FindEffect("lex.e") == null);

        T.Reset();
        ok = GameplayDefs.LoadText("OCGAS 1\nATTRSET S\n  ATTR Hp 1.5e1\n", "num.ocgas", out err);
        T.Check("invariant-culture numbers incl. exponents", ok && GameplayDefs.FindAttributeSet("S")!.Attributes[0].DefaultBase == 15f, err);
        T.Check("NaN is not a number here", !GameplayDefs.LoadText("OCGAS 1\nATTRSET S2\n  ATTR Hp NaN\n", "nan.ocgas", out err) && err.Contains("bad number"), err);
    }
}
