// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for the level-instance nodes (docs/plans/wave3/LEVEL_INSTANCES.md section 3): the 11 AN_*LevelInstance*
// rows in GraphLevelInstanceNodeTable.cs, every method on GraphLevelInstances. Bare-process scope like
// GasNodeTests: the wiring the table owns, plus the zero-handle guards that answer before any native call;
// what an instance does is the native and runtime tests' job.
using System;
using System.Linq;
using Aver.Graph;
using Row = NodeRowKit.Row;

static class LevelInstanceNodeTests
{
    private static readonly Row[] Rows =
    {
        new("an_spawnlevelinstance", GenericKind.Exec, new[] { "exec:exec:in", "parent:int:in", "x:float:in", "y:float:in", "z:float:in", "yaw:float:in", "scale:float:in", "packed:bool:in", "then:exec:out", "entity:int:out" }, new[] { "scale=1" }),
        new("an_destroylevelinstance", GenericKind.Exec, new[] { "exec:exec:in", "root:int:in", "then:exec:out", "success:bool:out" }),
        new("an_getlevelinstanceroot", GenericKind.Pure, new[] { "entity:int:in", "root:int:out" }),
        new("an_findlevelinstanceentity", GenericKind.Pure, new[] { "root:int:in", "entity:int:out" }),
        new("an_setlevelinstanceloaded", GenericKind.Exec, new[] { "exec:exec:in", "root:int:in", "loaded:bool:in", "then:exec:out", "success:bool:out" }, new[] { "loaded=true" }),
        new("an_islevelinstanceloaded", GenericKind.Pure, new[] { "root:int:in", "loaded:bool:out" }),
        new("an_reloadlevelinstance", GenericKind.Exec, new[] { "exec:exec:in", "root:int:in", "then:exec:out", "success:bool:out" }),
        new("an_setlevelinstancepacked", GenericKind.Exec, new[] { "exec:exec:in", "root:int:in", "packed:bool:in", "then:exec:out", "success:bool:out" }),
        new("an_getlevelinstancebounds", GenericKind.Pure, new[] { "root:int:in", "minX:float:out", "minY:float:out", "minZ:float:out", "maxX:float:out", "maxY:float:out", "maxZ:float:out", "success:bool:out" }),
        new("an_getlevelinstancecount", GenericKind.Pure, new[] { "count:int:out" }),
        new("an_getlevelinstanceat", GenericKind.Pure, new[] { "index:int:in", "root:int:out" }),
    };

    public static int RunAll()
    {
        int failures = 0;
        string[] types = Rows.Select(r => r.Type).ToArray();
        failures += NodeRowKit.CheckRows("level instance", Rows);
        failures += TestRowSetIsTheDesignSet();
        failures += NodeRowKit.CheckDefaultPins("level instance", Rows);
        failures += NodeRowKit.CheckIlValid("level instance", types);
        failures += NodeRowKit.CheckCompilerSplit("level instance", types);
        failures += TestMissingAttributeFailsByName();
        failures += TestZeroHandlesAnswerWithoutNative();
        failures += TestPathAttributeKeepsSlashes();
        failures += TestSpawnChain();
        failures += TestResidencyEvents();
        return failures;
    }

    private static int Fail(string msg) => NodeRowKit.Fail(msg);

    private static int TestRowSetIsTheDesignSet()
    {
        Console.WriteLine("Test: the level-instance rows are exactly the designed set of 11");
        var got = GameSystemNodes.All.Where(s => s.Type.Contains("levelinstance", StringComparison.OrdinalIgnoreCase))
                                     .Select(s => s.Type).OrderBy(x => x).ToArray();
        var want = Rows.Select(r => r.Type).OrderBy(x => x).ToArray();
        if (!got.SequenceEqual(want))
            return Fail($"table has [{string.Join(", ", got.Except(want))}] extra and lacks [{string.Join(", ", want.Except(got))}]");
        if (Rows.Length != 11) return Fail($"expected 11 rows, test lists {Rows.Length}");
        Console.WriteLine("  PASS");
        return 0;
    }

    // Spawn needs the level; Find's path is optional because an empty path is the instance root.
    private static int TestMissingAttributeFailsByName()
    {
        Console.WriteLine("Test: Spawn Level Instance without level= fails to compile, naming it; the path= of Find stays optional");
        int f = 0;
        f += NodeRowKit.CheckMissingAttribute("NODE n AN_SpawnLevelInstance", "level=", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n AN_FindLevelInstanceEntity", exec: false);
        foreach (var t in new[] { "AN_DestroyLevelInstance", "AN_SetLevelInstanceLoaded", "AN_ReloadLevelInstance", "AN_SetLevelInstancePacked" })
            f += NodeRowKit.CheckOptionalAttribute("NODE n " + t, exec: true);
        foreach (var t in new[] { "AN_GetLevelInstanceRoot", "AN_IsLevelInstanceLoaded", "AN_GetLevelInstanceBounds", "AN_GetLevelInstanceCount", "AN_GetLevelInstanceAt" })
            f += NodeRowKit.CheckOptionalAttribute("NODE n " + t, exec: false);
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // Calls a row's own method by reflection; every case here answers from a managed guard before any native call.
    private static object? Call(string type, object?[] args)
    {
        if (!GameSystemNodes.TryGet(type, out var spec)) throw new InvalidOperationException("no row " + type);
        return spec.Method.Invoke(null, args);
    }

    private static int TestZeroHandlesAnswerWithoutNative()
    {
        Console.WriteLine("Test: a zero root, a zero entity, an empty level and a negative index answer false / 0, bounds zeroed");
        if (!Call("an_spawnlevelinstance", new object?[] { 0, 0f, 0f, 0f, 0f, 1f, false, "" })!.Equals(0)) return Fail("Spawn with no level should answer 0");
        if (!Call("an_destroylevelinstance", new object?[] { 0 })!.Equals(false)) return Fail("Destroy of root 0 should be false");
        if (!Call("an_getlevelinstanceroot", new object?[] { 0 })!.Equals(0)) return Fail("Root of entity 0 should be 0");
        if (!Call("an_findlevelinstanceentity", new object?[] { 0, "b2/c9" })!.Equals(0)) return Fail("Find in root 0 should be 0");
        if (!Call("an_setlevelinstanceloaded", new object?[] { 0, true })!.Equals(false)) return Fail("SetLoaded of root 0 should be false");
        if (!Call("an_islevelinstanceloaded", new object?[] { 0 })!.Equals(false)) return Fail("IsLoaded of root 0 should be false");
        if (!Call("an_reloadlevelinstance", new object?[] { 0 })!.Equals(false)) return Fail("Reload of root 0 should be false");
        if (!Call("an_setlevelinstancepacked", new object?[] { 0, true })!.Equals(false)) return Fail("SetPacked of root 0 should be false");
        if (!Call("an_getlevelinstanceat", new object?[] { -1 })!.Equals(0)) return Fail("At(-1) should be 0");
        var b = new object?[] { 0, 9f, 9f, 9f, 9f, 9f, 9f };
        if (!Call("an_getlevelinstancebounds", b)!.Equals(false)) return Fail("Bounds of root 0 should be false");
        if (b.Skip(1).Any(v => !v!.Equals(0f))) return Fail("Bounds of root 0 should zero every out value");
        Console.WriteLine("  PASS");
        return 0;
    }

    // The child path is hex ids joined by '/' through a nested instance; the NODE-line parser must keep it whole.
    private static int TestPathAttributeKeepsSlashes()
    {
        Console.WriteLine("Test: path=b2/c9 and a level path with slashes reach Node.Attrs intact");
        const string text = "OCGRAPH 1\nNODE f AN_FindLevelInstanceEntity path=b2/c9\nNODE s AN_SpawnLevelInstance level=Levels/Rooms/Room_A.ocworld\n";
        if (!OcGraphParser.Parse(text, out var g, out var err)) return Fail("parse: " + err);
        if (g.Nodes["f"].Attrs["path"] != "b2/c9") return Fail("path= became '" + g.Nodes["f"].Attrs["path"] + "'");
        if (g.Nodes["s"].Attrs["level"] != "Levels/Rooms/Room_A.ocworld") return Fail("level= did not survive");
        if (!g.PinnedValues.Any(p => p.NodeId == "s" && p.PinName == "scale" && p.Value is float v && v == 1f)) return Fail("scale does not default to 1");
        Console.WriteLine("  PASS");
        return 0;
    }

    // Spawn, look a child up by id path, load it by hand, read its bounds, destroy it.
    private static int TestSpawnChain()
    {
        Console.WriteLine("Test: Spawn feeds Find, Set Loaded, Bounds and Destroy; the root rides an int pin");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE sp AN_SpawnLevelInstance level=Levels/Room.ocworld\nNODE ld AN_SetLevelInstanceLoaded\n" +
                            "NODE fd AN_FindLevelInstanceEntity path=c3\nNODE bd AN_GetLevelInstanceBounds\nNODE rt AN_GetLevelInstanceRoot\n" +
                            "NODE ct AN_GetLevelInstanceCount\nNODE at AN_GetLevelInstanceAt\nNODE ds AN_DestroyLevelInstance\nNODE pr Print\nNODE pi PrintInt\n" +
                            "LINK tick.exec sp.exec\nLINK sp.then ld.exec\nLINK sp.entity ld.root\nLINK ld.then pr.exec\nLINK sp.entity fd.root\n" +
                            "LINK sp.entity bd.root\nLINK bd.maxZ pr.value\nLINK pr.then pi.exec\nLINK fd.entity rt.entity\nLINK rt.root pi.value\n" +
                            "LINK pi.then ds.exec\nLINK ct.count at.index\nLINK at.root ds.root\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }

    // Residency changes reach a graph as bus events, so the existing AN_OnEvent is the entry; payload 0 is the entity count.
    private static int TestResidencyEvents()
    {
        Console.WriteLine("Test: AN_OnEvent entries LevelInstance.Loaded and LevelInstance.Unloaded drive level-instance nodes");
        foreach (var ev in new[] { "LevelInstance.Loaded", "LevelInstance.Unloaded" })
        {
            string text = "OCGRAPH 1\nNODE e AN_OnEvent\nNODE p AN_EventPayload\nNODE b AN_GetLevelInstanceBounds\nNODE pi PrintInt\n" +
                          "LINK e.exec p.exec\nLINK p.then pi.exec\nLINK p.i pi.value\nLINK p.target b.root\nENTRY e " + ev + "\n";
            if (!OcGraphParser.Parse(text, out var g, out var err)) return Fail("parse: " + err);
            if (!NodeRowKit.HasEntry(g, ev)) return Fail("the ENTRY record for " + ev + " was not kept");
            string? bad = NodeRowKit.CompileAndRun(text, ev);
            if (bad != null) return Fail(ev + " graph: " + bad);
        }
        Console.WriteLine("  PASS");
        return 0;
    }
}
