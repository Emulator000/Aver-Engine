// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for the montage and root motion nodes (docs/plans/wave2/MONTAGES_ROOT_MOTION.md section 10):
// 9 montage rows and 2 root motion rows in GraphGameSystemNodeTable.cs, wrapping MontageGraphInterop
// and RootMotionGraphInterop. Same bare-process scope as GameSystemNodeTests: the wiring the table owns,
// not what the animation system does with a montage (tests/anim.scene's MontageSystemTest does that).
using System;
using System.Linq;
using Aver.Graph;
using Row = NodeRowKit.Row;

static class MontageNodeTests
{
    private static readonly Row[] Rows =
    {
        new("playmontage", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "rate:float:in", "blendIn:float:in", "then:exec:out", "success:bool:out" }, new[] { "rate=1", "blendIn=-1" }),
        new("stopmontage", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "blendOut:float:in", "then:exec:out", "success:bool:out" }, new[] { "blendOut=-1" }),
        new("stopmontageslot", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "blendOut:float:in", "then:exec:out", "success:bool:out" }, new[] { "blendOut=-1" }),
        new("jumptomontagesection", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "then:exec:out", "success:bool:out" }),
        new("setmontagenextsection", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "then:exec:out", "success:bool:out" }),
        new("setmontagerate", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "rate:float:in", "then:exec:out", "success:bool:out" }),
        new("ismontageplaying", GenericKind.Pure, new[] { "entity:int:in", "result:bool:out" }),
        new("getmontagestate", GenericKind.Pure, new[] { "entity:int:in", "found:bool:out", "position:float:out", "length:float:out", "weight:float:out" }),
        new("isinmontagesection", GenericKind.Pure, new[] { "entity:int:in", "result:bool:out" }),
        new("setrootmotion", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "mode:int:in", "axes:int:in", "scale:float:in", "then:exec:out", "success:bool:out" }, new[] { "axes=11", "scale=1" }),
        new("consumerootmotion", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "then:exec:out", "success:bool:out", "x:float:out", "y:float:out", "z:float:out", "yaw:float:out", "seconds:float:out" }),
    };

    public static int RunAll()
    {
        int failures = 0;
        string[] types = Rows.Select(r => r.Type).ToArray();
        failures += NodeRowKit.CheckRows("montage and root motion", Rows);
        failures += NodeRowKit.CheckDefaultPins("montage and root motion", Rows);
        failures += TestDocumentedDefaults();
        failures += NodeRowKit.CheckIlValid("montage and root motion", types);
        failures += NodeRowKit.CheckCompilerSplit("montage and root motion", types);
        failures += TestMissingAttributeFailsByName();
        failures += TestRootMotionAccumulateChain();
        failures += TestSectionChain();
        return failures;
    }

    private static int Fail(string msg) => NodeRowKit.Fail(msg);

    // The values the design names: rate 1, blend -1 (the montage's own), axes 11 (X, Y, yaw), scale 1.
    private static int TestDocumentedDefaults()
    {
        Console.WriteLine("Test: rate and scale default to 1, blends to -1, axes to 11 (X, Y, yaw)");
        const string text = "OCGRAPH 1\nNODE p PlayMontage montage=Content/Anim/Attack.ocmontage\nNODE s StopMontage\n" +
                            "NODE r SetRootMotion\n";
        if (!OcGraphParser.Parse(text, out var g, out var err)) return Fail("parse: " + err);
        object? V(string node, string pin) => g.PinnedValues.FirstOrDefault(v => v.NodeId == node && v.PinName == pin)?.Value;
        bool ok = V("p", "rate") is float r1 && r1 == 1f && V("p", "blendIn") is float bi && bi == -1f &&
                  V("s", "blendOut") is float bo && bo == -1f &&
                  V("r", "axes") is int ax && ax == 11 && V("r", "scale") is float sc && sc == 1f;
        if (!ok) return Fail("a default is missing or wrong");
        // mode has no default: an unwired mode pin reads 0, which is None (docs say wire a Const Int).
        if (V("r", "mode") != null) return Fail("mode should have no default");
        if (g.Nodes["p"].Attrs["montage"] != "Content/Anim/Attack.ocmontage") return Fail("montage= did not reach Node.Attrs");
        Console.WriteLine("  PASS");
        return 0;
    }

    private static int TestMissingAttributeFailsByName()
    {
        Console.WriteLine("Test: a montage node missing its required attribute fails to compile, naming it; the optional ones compile bare");
        int f = 0;
        f += NodeRowKit.CheckMissingAttribute("NODE n PlayMontage", "montage=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n StopMontageSlot", "slot=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n JumpToMontageSection", "montage=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n JumpToMontageSection montage=a.ocmontage", "section=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n SetMontageNextSection montage=a.ocmontage", "from=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n SetMontageRate", "montage=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n IsInMontageSection montage=a.ocmontage", "section=", exec: false);
        // Optional: section and slot on Play, montage on Stop/IsPlaying/State, `to` on SetNextSection, montage on IsInSection.
        f += NodeRowKit.CheckOptionalAttribute("NODE n PlayMontage montage=a.ocmontage", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n StopMontage", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n SetMontageNextSection montage=a.ocmontage from=Loop", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n IsMontagePlaying", exec: false);
        f += NodeRowKit.CheckOptionalAttribute("NODE n GetMontageState", exec: false);
        f += NodeRowKit.CheckOptionalAttribute("NODE n IsInMontageSection section=Loop", exec: false);
        // The root motion nodes take only pins.
        f += NodeRowKit.CheckOptionalAttribute("NODE n SetRootMotion", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n ConsumeRootMotion", exec: true);
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // The accumulate workflow: turn it on once, then read and clear the travel each tick.
    private static int TestRootMotionAccumulateChain()
    {
        Console.WriteLine("Test: Set Root Motion (mode 3) then Consume Root Motion feeds its x and yaw to later nodes");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE m ConstInt value=3\nNODE sr SetRootMotion\nNODE cr ConsumeRootMotion\n" +
                            "NODE px Print\nNODE py Print\nLINK tick.exec sr.exec\nLINK m.value sr.mode\nLINK sr.then cr.exec\n" +
                            "LINK cr.then px.exec\nLINK cr.x px.value\nLINK px.then py.exec\nLINK cr.yaw py.value\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }

    // Branching on where a montage is: Is In Montage Section gates a section change.
    private static int TestSectionChain()
    {
        Console.WriteLine("Test: Play, then Is In Montage Section gates Jump To Montage Section");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE p PlayMontage montage=a.ocmontage section=Start slot=UpperBody\n" +
                            "NODE q IsInMontageSection montage=a.ocmontage section=Start\nNODE b Branch\n" +
                            "NODE j JumpToMontageSection montage=a.ocmontage section=Finish\n" +
                            "LINK tick.exec p.exec\nLINK p.then b.exec\nLINK q.result b.cond\nLINK b.true j.exec\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }
}
