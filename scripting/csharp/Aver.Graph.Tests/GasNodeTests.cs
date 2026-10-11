// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for the gameplay-ability nodes (docs/plans/wave2/GAS.md section 3): the 28 AN_Gas* rows in
// GraphGasNodeTable.cs. Like GameSystemNodeTests, this bare process has no native engine, so it proves
// the wiring the table owns (a real GasGraph method behind every row, pins that match its parameters,
// default pins and values, push-only versus pure, required attributes by name, valid IL, entries by
// event name); what the ability system does is Aver.Framework.Tests' job.
using System;
using System.Linq;
using System.Reflection;
using Aver.Graph;
using Row = NodeRowKit.Row;

static class GasNodeTests
{
    // Pin order is the table's: exec, inputs, then, outputs. Copied from the design table in GAS.md.
    private static readonly Row[] Rows =
    {
        new("an_gastagid", GenericKind.Pure, new[] { "tagId:int:out" }),
        new("an_gasattributeid", GenericKind.Pure, new[] { "attrId:int:out" }),
        new("an_gaseffectid", GenericKind.Pure, new[] { "effectId:int:out" }),
        new("an_gasabilityid", GenericKind.Pure, new[] { "abilityId:int:out" }),
        new("an_gashastag", GenericKind.Pure, new[] { "entity:int:in", "tagId:int:in", "has:bool:out" }),
        new("an_gashastags", GenericKind.Pure, new[] { "entity:int:in", "all:bool:in", "has:bool:out" }),
        new("an_gastagcount", GenericKind.Pure, new[] { "entity:int:in", "tagId:int:in", "count:int:out" }),
        new("an_gasaddtag", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "tagId:int:in", "count:int:in", "then:exec:out", "success:bool:out" }, new[] { "count=1" }),
        new("an_gasremovetag", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "tagId:int:in", "count:int:in", "then:exec:out", "success:bool:out" }, new[] { "count=1" }),
        new("an_gasgetattribute", GenericKind.Pure, new[] { "entity:int:in", "attrId:int:in", "value:float:out", "base:float:out", "success:bool:out" }),
        new("an_gassetattribute", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "attrId:int:in", "value:float:in", "then:exec:out", "success:bool:out" }),
        new("an_gasaddattributeset", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "then:exec:out", "success:bool:out" }),
        new("an_gasapplyeffect", GenericKind.Exec, new[] { "exec:exec:in", "target:int:in", "source:int:in", "effectId:int:in", "callerValue:float:in", "then:exec:out", "handle:int:out", "applied:bool:out" }),
        new("an_gasremoveeffect", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "handle:int:in", "then:exec:out", "success:bool:out" }),
        new("an_gasremoveeffectsbytag", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "tagId:int:in", "then:exec:out", "count:int:out" }),
        new("an_gasremoveeffectsbydef", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "effectId:int:in", "then:exec:out", "count:int:out" }),
        new("an_gaseffectinfo", GenericKind.Pure, new[] { "entity:int:in", "effectId:int:in", "active:bool:out", "stacks:int:out", "remaining:float:out", "handle:int:out" }),
        new("an_gasgrantability", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "abilityId:int:in", "level:int:in", "then:exec:out", "handle:int:out" }, new[] { "level=1" }),
        new("an_gasrevokeability", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "abilityId:int:in", "then:exec:out", "success:bool:out" }),
        new("an_gastryactivate", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "abilityId:int:in", "target:int:in", "then:exec:out", "success:bool:out", "reason:int:out" }),
        new("an_gastryactivatebytag", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "tagId:int:in", "target:int:in", "then:exec:out", "success:bool:out", "abilityId:int:out", "reason:int:out" }),
        new("an_gascanactivate", GenericKind.Pure, new[] { "entity:int:in", "abilityId:int:in", "can:bool:out", "reason:int:out" }),
        new("an_gascommitability", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "abilityId:int:in", "then:exec:out", "success:bool:out" }),
        new("an_gasendability", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "abilityId:int:in", "cancelled:bool:in", "then:exec:out", "success:bool:out" }),
        new("an_gascancelbytag", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "tagId:int:in", "then:exec:out", "count:int:out" }),
        new("an_gasisabilityactive", GenericKind.Pure, new[] { "entity:int:in", "abilityId:int:in", "active:bool:out" }),
        new("an_gascooldown", GenericKind.Pure, new[] { "entity:int:in", "abilityId:int:in", "remaining:float:out", "onCooldown:bool:out" }),
        new("an_gassendevent", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "instigator:int:in", "tagId:int:in", "magnitude:float:in", "then:exec:out", "triggered:int:out" }),
    };

    public static int RunAll()
    {
        int failures = 0;
        string[] types = Rows.Select(r => r.Type).ToArray();
        failures += NodeRowKit.CheckRows("gameplay ability", Rows);
        failures += TestRowSetIsTheDesignSet();
        failures += NodeRowKit.CheckDefaultPins("gameplay ability", Rows);
        failures += TestDocumentedDefaults();
        failures += NodeRowKit.CheckIlValid("gameplay ability", types);
        failures += NodeRowKit.CheckCompilerSplit("gameplay ability", types);
        failures += TestMissingAttributeFailsByName();
        failures += TestSampleAbilityGraphs();
        failures += TestOutputsReadDownstream();
        failures += TestTagIdIsStable();
        return failures;
    }

    private static int Fail(string msg) => NodeRowKit.Fail(msg);

    // No row may go missing or be added without this list (and the design) changing with it.
    private static int TestRowSetIsTheDesignSet()
    {
        Console.WriteLine("Test: the GAS rows are exactly the designed set, all under the AN_Gas prefix");
        var got = GameSystemNodes.All.Where(s => s.Type.StartsWith("an_gas", StringComparison.OrdinalIgnoreCase))
                                     .Select(s => s.Type).OrderBy(x => x).ToArray();
        var want = Rows.Select(r => r.Type).OrderBy(x => x).ToArray();
        if (!got.SequenceEqual(want))
            return Fail($"table has [{string.Join(", ", got.Except(want))}] extra and lacks [{string.Join(", ", want.Except(got))}]");
        if (Rows.Length != 28) return Fail($"expected 28 rows, test lists {Rows.Length}");
        Console.WriteLine("  PASS");
        return 0;
    }

    // The defaults GAS.md names (count = 1, level = 1) and the multi-output shapes a graph depends on.
    private static int TestDocumentedDefaults()
    {
        Console.WriteLine("Test: count and level default to 1; Apply Effect, Get Attribute and Try Activate expose their outputs");
        const string text = "OCGRAPH 1\nNODE a AN_GasAddTag tag=State.Stunned\nNODE r AN_GasRemoveTag tag=State.Stunned\n" +
                            "NODE g AN_GasGrantAbility ability=Ability.Fireball\nNODE e AN_GasApplyEffect effect=Effect.Burn\n" +
                            "NODE h AN_GasGetAttribute attribute=Health\nNODE t AN_GasTryActivate ability=Ability.Fireball\n";
        if (!OcGraphParser.Parse(text, out var gr, out var err)) return Fail("parse: " + err);

        foreach (var (node, pin) in new[] { ("a", "count"), ("r", "count"), ("g", "level") })
        {
            var v = gr.PinnedValues.FirstOrDefault(p => p.NodeId == node && p.PinName == pin);
            if (v == null || v.Value is not int i || i != 1) return Fail($"{node}.{pin} does not default to int 1");
        }
        if (gr.PinnedValues.Any(p => p.NodeId == "e")) return Fail("Apply Effect should have no pinned defaults (source 0 = the target)");

        static bool Has(Node n, string name, PinType t, bool isOut) => n.Pins.Any(p => p.Name == name && p.Type == t && p.IsOutput == isOut);
        var e = gr.Nodes["e"]; var h = gr.Nodes["h"]; var t2 = gr.Nodes["t"];
        bool ok = Has(e, "handle", PinType.Int, true) && Has(e, "applied", PinType.Bool, true) &&
                  Has(e, "callerValue", PinType.Float, false) && Has(e, "source", PinType.Int, false) &&
                  Has(h, "value", PinType.Float, true) && Has(h, "base", PinType.Float, true) && Has(h, "success", PinType.Bool, true) &&
                  !h.Pins.Any(p => p.Type == PinType.Exec) &&
                  Has(t2, "success", PinType.Bool, true) && Has(t2, "reason", PinType.Int, true) && Has(t2, "target", PinType.Int, false);
        if (!ok) return Fail("an output or input pin of Apply Effect / Get Attribute / Try Activate is missing or mistyped");
        if (e.Attrs["effect"] != "Effect.Burn" || gr.Nodes["a"].Attrs["tag"] != "State.Stunned")
            return Fail("a dotted tag or effect name did not reach Node.Attrs intact");
        Console.WriteLine("  PASS");
        return 0;
    }

    // Required: the four id nodes, Has Tags, Add Attribute Set. Optional (an id pin may stand in): the rest.
    private static int TestMissingAttributeFailsByName()
    {
        Console.WriteLine("Test: a GAS node missing its required attribute fails to compile, naming it; the optional ones compile bare");
        int f = 0;
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GasTagId", "tag=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GasAttributeId", "attribute=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GasEffectId", "effect=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GasAbilityId", "ability=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GasHasTags", "tags=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_GasAddAttributeSet", "set=", exec: true);
        foreach (var t in new[] { "AN_GasHasTag", "AN_GasTagCount", "AN_GasGetAttribute", "AN_GasEffectInfo", "AN_GasCanActivate",
                                  "AN_GasIsAbilityActive", "AN_GasCooldown" })
            f += NodeRowKit.CheckOptionalAttribute("NODE n " + t, exec: false);
        foreach (var t in new[] { "AN_GasAddTag", "AN_GasRemoveTag", "AN_GasSetAttribute", "AN_GasApplyEffect", "AN_GasRemoveEffectsByTag",
                                  "AN_GasRemoveEffectsByDef", "AN_GasGrantAbility", "AN_GasRevokeAbility", "AN_GasTryActivate",
                                  "AN_GasTryActivateByTag", "AN_GasCommitAbility", "AN_GasEndAbility", "AN_GasCancelByTag", "AN_GasSendEvent" })
            f += NodeRowKit.CheckOptionalAttribute("NODE n " + t, exec: true);
        // Remove Gameplay Effect takes only pins.
        f += NodeRowKit.CheckOptionalAttribute("NODE n AN_GasRemoveEffect", exec: true);
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // Abilities and attributes reach a graph as bus events by name, so the existing AN_OnEvent is the entry.
    private static int TestSampleAbilityGraphs()
    {
        Console.WriteLine("Test: AN_OnEvent entries named GAS.AbilityActivated.* and GAS.AttributeChanged.* drive GAS nodes");
        const string ability = "GAS.AbilityActivated.Ability.Fireball";
        const string abilityGraph = "OCGRAPH 1\nNODE n AN_OnEvent\nNODE c AN_GasCommitAbility ability=Ability.Fireball\n" +
                                    "NODE e AN_GasEndAbility ability=Ability.Fireball\nLINK n.exec c.exec\nLINK c.then e.exec\n" +
                                    "ENTRY n " + ability + "\n";
        if (!OcGraphParser.Parse(abilityGraph, out var g, out var err)) return Fail("parse: " + err);
        if (!NodeRowKit.HasEntry(g, ability)) return Fail("the ENTRY record was not kept");
        string? bad = NodeRowKit.CompileAndRun(abilityGraph, ability);
        if (bad != null) return Fail("ability graph: " + bad);

        const string attr = "GAS.AttributeChanged.Health";
        const string attrGraph = "OCGRAPH 1\nNODE a AN_OnEvent\nNODE p AN_EventPayload\nNODE s AN_GasSetAttribute attribute=Mana\n" +
                                 "LINK a.exec p.exec\nLINK p.then s.exec\nLINK p.f s.value\nENTRY a " + attr + "\n";
        bad = NodeRowKit.CompileAndRun(attrGraph, attr);
        if (bad != null) return Fail("attribute graph: " + bad);
        Console.WriteLine("  PASS");
        return 0;
    }

    // Ids and handles ride int pins from one node to the next.
    private static int TestOutputsReadDownstream()
    {
        Console.WriteLine("Test: GAS outputs feed later nodes (effect handle, activation result, tag id)");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE tid AN_GasTagId tag=State.Burning\n" +
                            "NODE ap AN_GasApplyEffect effect=Effect.Burn\nNODE rm AN_GasRemoveEffect\n" +
                            "NODE ac AN_GasTryActivate ability=Ability.Fireball\nNODE br Branch\nNODE at AN_GasAddTag\n" +
                            "LINK tick.exec ap.exec\nLINK ap.then rm.exec\nLINK ap.handle rm.handle\n" +
                            "LINK rm.then ac.exec\nLINK ac.then br.exec\nLINK ac.success br.cond\n" +
                            "LINK br.true at.exec\nLINK tid.tagId at.tagId\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }

    // Tag ids are what the tagId pins carry; the same name must give the same id, different names different ids.
    // Managed only, but reached through the table's own method so no internal access is needed.
    private static int TestTagIdIsStable()
    {
        Console.WriteLine("Test: AN_GasTagId gives one stable id per tag name");
        if (!GameSystemNodes.TryGet("an_gastagid", out var spec)) return Fail("no an_gastagid row");
        try
        {
            object? a1 = spec.Method.Invoke(null, new object[] { "GraphTest.Alpha" });
            object? a2 = spec.Method.Invoke(null, new object[] { "GraphTest.Alpha" });
            object? b = spec.Method.Invoke(null, new object[] { "GraphTest.Beta" });
            object? none = spec.Method.Invoke(null, new object[] { "" });
            if (a1 is not int ia || a2 is not int ib || b is not int ic || none is not int id) return Fail("TagIdForGraph does not return int");
            if (ia == 0 || ia != ib) return Fail($"same name gave {ia} then {ib}");
            if (ic == ia || ic == 0) return Fail("two names share an id");
            if (id != 0) return Fail("an empty name should give id 0");
        }
        catch (Exception e) when (NodeRowKit.NativeMissing(e))
        {
            Console.WriteLine("  SKIP: the tag registry needs the framework DLL here");
            return 0;
        }
        Console.WriteLine("  PASS");
        return 0;
    }
}
