// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for the landscape and water nodes (docs/plans/wave3/LANDSCAPE_LAYERS_RIVERS.md section 3): the 7 rows
// in GraphTerrainNodeTable.cs, every method on TerrainGraph. All are pure reads. Bare-process scope like
// GasNodeTests: the wiring the table owns, plus the one guard that answers before any provider is asked
// (entity 0); what the landscape and the water report is the native and runtime tests' job.
using System;
using System.Linq;
using Aver.Graph;
using Row = NodeRowKit.Row;

static class TerrainNodeTests
{
    private static readonly Row[] Rows =
    {
        new("an_landscapesample", GenericKind.Pure, new[] { "x:float:in", "y:float:in", "found:bool:out", "z:float:out", "nx:float:out", "ny:float:out", "nz:float:out", "layer:int:out" }),
        new("an_landscapelayerid", GenericKind.Pure, new[] { "layerId:int:out" }),
        new("an_landscapelayerweight", GenericKind.Pure, new[] { "x:float:in", "y:float:in", "layerId:int:in", "weight:float:out" }, new[] { "layerId=-1" }),
        new("an_landscapesurface", GenericKind.Pure, new[] { "x:float:in", "y:float:in", "surfaceId:int:out", "friction:float:out", "restitution:float:out", "layer:int:out" }),
        new("an_surfaceid", GenericKind.Pure, new[] { "surfaceId:int:out" }),
        new("an_watersample", GenericKind.Pure, new[] { "x:float:in", "y:float:in", "z:float:in", "over:bool:out", "submerged:bool:out", "surfaceZ:float:out", "depth:float:out", "flowX:float:out", "flowY:float:out", "flowZ:float:out", "kind:int:out" }),
        new("an_entitywater", GenericKind.Pure, new[] { "entity:int:in", "heightCm:float:in", "over:bool:out", "submersion:float:out", "flowX:float:out", "flowY:float:out", "flowZ:float:out", "kind:int:out" }, new[] { "heightCm=180" }),
    };

    public static int RunAll()
    {
        int failures = 0;
        string[] types = Rows.Select(r => r.Type).ToArray();
        failures += NodeRowKit.CheckRows("landscape and water", Rows);
        failures += TestRowSetIsTheDesignSet();
        failures += NodeRowKit.CheckDefaultPins("landscape and water", Rows);
        failures += NodeRowKit.CheckIlValid("landscape and water", types);
        failures += NodeRowKit.CheckCompilerSplit("landscape and water", types);
        failures += TestMissingAttributeFailsByName();
        failures += TestDeadEntityIsNotOverWater();
        failures += TestLayerChain();
        failures += TestWaterChain();
        return failures;
    }

    private static int Fail(string msg) => NodeRowKit.Fail(msg);

    private static int TestRowSetIsTheDesignSet()
    {
        Console.WriteLine("Test: the landscape and water rows are exactly the designed set of 7, all pure");
        var got = GameSystemNodes.All.Where(s => s.Type.StartsWith("an_landscape", StringComparison.OrdinalIgnoreCase) ||
                                                 s.Type is "an_surfaceid" or "an_watersample" or "an_entitywater")
                                     .Select(s => s.Type).OrderBy(x => x).ToArray();
        var want = Rows.Select(r => r.Type).OrderBy(x => x).ToArray();
        if (!got.SequenceEqual(want))
            return Fail($"table has [{string.Join(", ", got.Except(want))}] extra and lacks [{string.Join(", ", want.Except(got))}]");
        if (Rows.Length != 7) return Fail($"expected 7 rows, test lists {Rows.Length}");
        if (Rows.Any(r => r.Kind != GenericKind.Pure)) return Fail("every terrain node is a read, so none may be Exec");
        Console.WriteLine("  PASS");
        return 0;
    }

    // The two name-only nodes need their name; the weight node takes a layer id pin or a name, so neither is required.
    private static int TestMissingAttributeFailsByName()
    {
        Console.WriteLine("Test: Layer Id and Surface Id without their attribute fail to compile, naming it; Layer Weight compiles bare");
        int f = 0;
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_LandscapeLayerId", "layer=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_SurfaceId", "surface=", exec: false);
        f += NodeRowKit.CheckOptionalAttribute("NODE n AN_LandscapeLayerWeight", exec: false);
        foreach (var t in new[] { "AN_LandscapeSample", "AN_LandscapeSurface", "AN_WaterSample", "AN_EntityWater" })
            f += NodeRowKit.CheckOptionalAttribute("NODE n " + t, exec: false);
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // Entity 0 returns before the water provider is consulted, so this holds without a host.
    private static int TestDeadEntityIsNotOverWater()
    {
        Console.WriteLine("Test: Entity Water of entity 0 is false with every out value zero");
        if (!GameSystemNodes.TryGet("an_entitywater", out var spec)) return Fail("no an_entitywater row");
        var args = new object?[] { 0, 180f, 9f, 9f, 9f, 9f, 9 };
        if (!spec.Method.Invoke(null, args)!.Equals(false)) return Fail("entity 0 should not be over water");
        if (!args[2]!.Equals(0f) || !args[3]!.Equals(0f) || !args[4]!.Equals(0f) || !args[5]!.Equals(0f) || !args[6]!.Equals(0))
            return Fail("entity 0 should zero submersion, flow and kind");
        Console.WriteLine("  PASS");
        return 0;
    }

    // A layer id resolved once feeds the weight and a surface read feeds a printer: ids ride int pins.
    private static int TestLayerChain()
    {
        Console.WriteLine("Test: Layer Id feeds Layer Weight; Sample and Surface outputs feed later nodes");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE id AN_LandscapeLayerId layer=Grass\nNODE w AN_LandscapeLayerWeight\n" +
                            "NODE s AN_LandscapeSample\nNODE sf AN_LandscapeSurface\nNODE br Branch\n" +
                            "NODE pr Print\nNODE pi PrintInt\n" +
                            "LINK tick.exec br.exec\nLINK s.found br.cond\nLINK br.true pr.exec\nLINK id.layerId w.layerId\nLINK w.weight pr.value\n" +
                            "LINK s.z w.x\nLINK pr.then pi.exec\nLINK sf.surfaceId pi.value\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }

    // Is this entity in water, how deep and how fast: the swim/buoyancy decision a character graph makes.
    private static int TestWaterChain()
    {
        Console.WriteLine("Test: Entity Water gates a branch and feeds its submersion and flow onward; Sample Water reads a point");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE ew AN_EntityWater\nNODE ws AN_WaterSample\nNODE br Branch\nNODE pr Print\nNODE pk PrintInt\n" +
                            "LINK tick.exec br.exec\nLINK ew.over br.cond\nLINK br.true pr.exec\nLINK ew.submersion pr.value\n" +
                            "LINK pr.then pk.exec\nLINK ws.kind pk.value\nLINK ew.flowX ws.x\nLINK ew.flowY ws.y\nLINK ew.flowZ ws.z\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }
}
