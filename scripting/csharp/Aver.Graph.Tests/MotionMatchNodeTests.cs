// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for the motion-matching nodes (docs/plans/wave5/MOTION_MATCHING.md section 11): the 6 rows in
// GraphMotionMatchNodeTable.cs, every method on MotionMatchGraphInterop. Bare-process scope like MontageNodeTests:
// the wiring the table owns, not what the matcher picks (tests/anim and tests/anim.scene do that).
using System;
using System.Linq;
using Aver.Graph;
using Row = NodeRowKit.Row;

static class MotionMatchNodeTests
{
    private static readonly Row[] Rows =
    {
        new("setmotionmatching", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "facing:int:in", "drive:int:in", "playRate:float:in", "then:exec:out", "success:bool:out" }, new[] { "facing=0", "drive=0", "playRate=1" }),
        new("setmotioninput", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "velocityX:float:in", "velocityY:float:in", "facingYaw:float:in", "then:exec:out", "success:bool:out" }, new[] { "facingYaw=0" }),
        new("setmotiontags", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "then:exec:out", "success:bool:out" }),
        new("requestmotionsearch", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "then:exec:out", "success:bool:out" }),
        new("getmotionstate", GenericKind.Pure, new[] { "entity:int:in", "found:bool:out", "clipTime:float:out", "cost:float:out", "switches:int:out" }),
        new("ismotionclip", GenericKind.Pure, new[] { "entity:int:in", "result:bool:out" }),
    };

    public static int RunAll()
    {
        int failures = 0;
        string[] types = Rows.Select(r => r.Type).ToArray();
        failures += NodeRowKit.CheckRows("motion matching", Rows);
        failures += TestRowSetIsTheDesignSet();
        failures += NodeRowKit.CheckDefaultPins("motion matching", Rows);
        failures += TestAttributesReachTheNode();
        failures += NodeRowKit.CheckIlValid("motion matching", types);
        failures += NodeRowKit.CheckCompilerSplit("motion matching", types);
        failures += TestMissingAttributeFailsByName();
        failures += TestDriveChain();
        return failures;
    }

    private static int Fail(string msg) => NodeRowKit.Fail(msg);

    private static int TestRowSetIsTheDesignSet()
    {
        Console.WriteLine("Test: the motion-matching rows are exactly the designed set of 6, 2 pure reads and 4 exec writes");
        var want = Rows.Select(r => r.Type).OrderBy(x => x).ToArray();
        var got = GameSystemNodes.All.Where(s => s.Owner.Name == "MotionMatchGraphInterop").Select(s => s.Type).OrderBy(x => x).ToArray();
        if (!got.SequenceEqual(want))
            return Fail($"table has [{string.Join(", ", got.Except(want))}] extra and lacks [{string.Join(", ", want.Except(got))}]");
        if (Rows.Length != 6) return Fail($"expected 6 rows, test lists {Rows.Length}");
        if (Rows.Count(r => r.Kind == GenericKind.Pure) != 2) return Fail("only GetMotionState and IsMotionClip are reads");
        Console.WriteLine("  PASS");
        return 0;
    }

    // The tag lists and the database ride NODE attributes, not pins, and reach the parsed node.
    private static int TestAttributesReachTheNode()
    {
        Console.WriteLine("Test: database=, required= and excluded= reach the node and leave no pin or default behind");
        const string text = "OCGRAPH 1\nNODE m SetMotionMatching database=Content/Anim/Locomotion.ocmotion\nNODE t SetMotionTags required=Walk,Loop excluded=Pivot\n";
        if (!OcGraphParser.Parse(text, out var g, out var err)) return Fail("parse: " + err);
        if (g.Nodes["m"].Attrs["database"] != "Content/Anim/Locomotion.ocmotion") return Fail("database= did not reach Node.Attrs");
        if (g.Nodes["t"].Attrs["required"] != "Walk,Loop" || g.Nodes["t"].Attrs["excluded"] != "Pivot") return Fail("tag lists did not reach Node.Attrs");
        if (g.Nodes["t"].Pins.Any(p => p.Name is "required" or "excluded")) return Fail("a tag list must not be a pin");
        Console.WriteLine("  PASS");
        return 0;
    }

    // The clip path is the only required attribute; an empty database means 'from a state', the tag lists mean 'none'.
    private static int TestMissingAttributeFailsByName()
    {
        Console.WriteLine("Test: Is Motion Clip without clip= fails to compile, naming it; the database and tag lists compile bare");
        int f = 0;
        f += NodeRowKit.CheckMissingAttribute("NODE n IsMotionClip", "clip=", exec: false);
        f += NodeRowKit.CheckOptionalAttribute("NODE n SetMotionMatching", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n SetMotionTags", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n SetMotionInput", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n RequestMotionSearch", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n GetMotionState", exec: false);
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // The per-tick loop of a locomotion graph: start matching, steer it, constrain it, ask for a search, read the cost.
    private static int TestDriveChain()
    {
        Console.WriteLine("Test: Set Motion Matching, Input and Tags chain into Request Motion Search; State and Is Clip feed later nodes");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE mm SetMotionMatching database=a.ocmotion\nNODE mi SetMotionInput\nNODE mt SetMotionTags required=Walk\n" +
                            "NODE rs RequestMotionSearch\nNODE gs GetMotionState\nNODE ic IsMotionClip clip=Content/Anim/Walk.ocanim\nNODE br Branch\nNODE pc Print\nNODE ps PrintInt\n" +
                            "LINK tick.exec mm.exec\nLINK mm.then mi.exec\nLINK mi.then mt.exec\nLINK mt.then rs.exec\nLINK rs.then br.exec\n" +
                            "LINK ic.result br.cond\nLINK br.true pc.exec\nLINK gs.cost pc.value\nLINK pc.then ps.exec\nLINK gs.switches ps.value\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }
}
