// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for the IK and rig-input nodes (docs/plans/wave5/IK_RETARGETING.md section 9): the 11 rows in
// GraphIkNodeTable.cs, every method on IkGraphInterop. Bare-process scope like MontageNodeTests: the wiring the
// table owns (a real method behind every row, pins, defaults, push-only versus pure, required attributes,
// valid IL), not what the rig does with a goal (tests/anim.scene's IkRigSystemTest does that).
using System;
using System.Linq;
using Aver.Graph;
using Row = NodeRowKit.Row;

static class IkNodeTests
{
    private static readonly Row[] Rows =
    {
        new("setikrig", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "weight:float:in", "then:exec:out", "success:bool:out" }, new[] { "weight=1" }),
        new("setikgoal", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "x:float:in", "y:float:in", "z:float:in", "useRotation:bool:in", "pitch:float:in", "yaw:float:in", "roll:float:in", "space:int:in", "then:exec:out", "success:bool:out" }),
        new("bindikgoal", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "target:int:in", "ox:float:in", "oy:float:in", "oz:float:in", "then:exec:out", "success:bool:out" }),
        new("setikgoalweight", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "weight:float:in", "blendSeconds:float:in", "then:exec:out", "success:bool:out" }, new[] { "blendSeconds=0.15" }),
        new("clearikgoal", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "then:exec:out", "success:bool:out" }),
        new("getikgoalstate", GenericKind.Pure, new[] { "entity:int:in", "found:bool:out", "alpha:float:out", "errorCm:float:out", "reached:bool:out" }),
        new("setfootikweight", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "weight:float:in", "blendSeconds:float:in", "then:exec:out", "success:bool:out" }, new[] { "blendSeconds=0.2" }),
        new("getfootikstate", GenericKind.Pure, new[] { "entity:int:in", "found:bool:out", "offsetCm:float:out", "plant:float:out", "nx:float:out", "ny:float:out", "nz:float:out" }),
        new("setriginputfloat", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "value:float:in", "then:exec:out", "success:bool:out" }),
        new("setriginputvec3", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "x:float:in", "y:float:in", "z:float:in", "then:exec:out", "success:bool:out" }),
        new("clearriginput", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "then:exec:out", "success:bool:out" }),
    };

    public static int RunAll()
    {
        int failures = 0;
        string[] types = Rows.Select(r => r.Type).ToArray();
        failures += NodeRowKit.CheckRows("IK and rig input", Rows);
        failures += TestRowSetIsTheDesignSet();
        failures += NodeRowKit.CheckDefaultPins("IK and rig input", Rows);
        failures += TestDocumentedDefaults();
        failures += NodeRowKit.CheckIlValid("IK and rig input", types);
        failures += NodeRowKit.CheckCompilerSplit("IK and rig input", types);
        failures += TestMissingAttributeFailsByName();
        failures += TestGoalChain();
        failures += TestFootAndInputChain();
        return failures;
    }

    private static int Fail(string msg) => NodeRowKit.Fail(msg);

    private static int TestRowSetIsTheDesignSet()
    {
        Console.WriteLine("Test: the IK rows are exactly the designed set of 11, 4 pure reads and exec writes");
        var want = Rows.Select(r => r.Type).OrderBy(x => x).ToArray();
        var got = GameSystemNodes.All.Where(s => s.Owner.Name == "IkGraphInterop").Select(s => s.Type).OrderBy(x => x).ToArray();
        if (!got.SequenceEqual(want))
            return Fail($"table has [{string.Join(", ", got.Except(want))}] extra and lacks [{string.Join(", ", want.Except(got))}]");
        if (Rows.Length != 11) return Fail($"expected 11 rows, test lists {Rows.Length}");
        var pure = Rows.Where(r => r.Kind == GenericKind.Pure).Select(r => r.Type).OrderBy(x => x).ToArray();
        if (!pure.SequenceEqual(new[] { "getfootikstate", "getikgoalstate" })) return Fail("only the two state reads may be pure");
        Console.WriteLine("  PASS");
        return 0;
    }

    // The values the design names: goal weight blends in 0.15 s, foot weight in 0.2 s, rig weight 1.
    private static int TestDocumentedDefaults()
    {
        Console.WriteLine("Test: rig weight defaults to 1, goal blend to 0.15 s, foot blend to 0.2 s");
        const string text = "OCGRAPH 1\nNODE r SetIkRig rig=a.ocikrig\nNODE g SetIkGoalWeight goal=hand\nNODE f SetFootIkWeight\n";
        if (!OcGraphParser.Parse(text, out var g, out var err)) return Fail("parse: " + err);
        object? V(string node, string pin) => g.PinnedValues.FirstOrDefault(v => v.NodeId == node && v.PinName == pin)?.Value;
        bool ok = V("r", "weight") is float w && w == 1f && V("g", "blendSeconds") is float gb && Math.Abs(gb - 0.15f) < 1e-6f &&
                  V("f", "blendSeconds") is float fb && Math.Abs(fb - 0.2f) < 1e-6f;
        if (!ok) return Fail("a default is missing or wrong");
        if (V("g", "weight") != null) return Fail("goal weight should have no default");
        if (g.Nodes["r"].Attrs["rig"] != "a.ocikrig") return Fail("rig= did not reach Node.Attrs");
        Console.WriteLine("  PASS");
        return 0;
    }

    // Rig, goal and input names are required where a call means nothing without one; clearing and foot nodes take "all" from empty.
    private static int TestMissingAttributeFailsByName()
    {
        Console.WriteLine("Test: IK nodes missing a required attribute fail to compile, naming it; the 'empty = all' ones compile bare");
        int f = 0;
        f += NodeRowKit.CheckMissingAttribute("NODE n SetIkRig", "rig=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n SetIkGoal", "goal=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n BindIkGoal", "goal=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n SetIkGoalWeight", "goal=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n GetIkGoalState", "goal=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n SetRigInputFloat", "input=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n SetRigInputVec3", "input=", exec: true);
        // Optional: socket on Bind (empty = the target's origin), goal on Clear, foot on both foot nodes, input on Clear.
        f += NodeRowKit.CheckOptionalAttribute("NODE n BindIkGoal goal=hand", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n ClearIkGoal", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n SetFootIkWeight", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n GetFootIkState", exec: false);
        f += NodeRowKit.CheckOptionalAttribute("NODE n ClearRigInput", exec: true);
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // Wear a rig, reach with a hand, then branch on whether the solve got there and feed the blend and error onward.
    private static int TestGoalChain()
    {
        Console.WriteLine("Test: Set IK Rig, Set IK Goal and Bind IK Goal chain into Get IK Goal State, which gates a branch");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE rig SetIkRig rig=a.ocikrig\nNODE sg SetIkGoal goal=hand\nNODE bg BindIkGoal goal=hand socket=grip\n" +
                            "NODE gw SetIkGoalWeight goal=hand\nNODE gs GetIkGoalState goal=hand\nNODE br Branch\nNODE pa Print\nNODE pe Print\nNODE cl ClearIkGoal goal=hand\n" +
                            "LINK tick.exec rig.exec\nLINK rig.then sg.exec\nLINK sg.then bg.exec\nLINK bg.then gw.exec\nLINK gw.then br.exec\n" +
                            "LINK gs.reached br.cond\nLINK br.true pa.exec\nLINK gs.alpha pa.value\nLINK pa.then pe.exec\nLINK gs.errorCm pe.value\nLINK pe.then cl.exec\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }

    // Foot placement and the rig's named inputs: weight a foot, read its offset, set an input and clear it.
    private static int TestFootAndInputChain()
    {
        Console.WriteLine("Test: Set Foot IK Weight, Get Foot IK State, Set Rig Input (float and vector) and Clear Rig Input chain");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE fw SetFootIkWeight foot=leg_l\nNODE fs GetFootIkState foot=leg_l\nNODE pr Print\nNODE pl Print\n" +
                            "NODE si SetRigInputFloat input=lookWeight\nNODE sv SetRigInputVec3 input=lookAt\nNODE ci ClearRigInput\n" +
                            "LINK tick.exec fw.exec\nLINK fw.then pr.exec\nLINK fs.offsetCm pr.value\nLINK pr.then pl.exec\nLINK fs.plant pl.value\n" +
                            "LINK pl.then si.exec\nLINK fs.plant si.value\nLINK si.then sv.exec\nLINK fs.nx sv.x\nLINK fs.ny sv.y\nLINK fs.nz sv.z\nLINK sv.then ci.exec\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }
}
