// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for the physics wave 1b graph nodes: ragdolls (RagdollStart/Stop, SetRagdollStrength,
// RagdollImpulse), shape queries (BoxCast, CapsuleCast, OverlapBox, RaycastAll, BodiesAtPoint),
// SetJointBreakable, SetLockedAxes, SetCcd and CharacterCanStand.
//
// WHAT THIS CAN AND CANNOT PROVE. This process has no engine binary beside it, so no physics world
// exists. What it proves: the default pin sets, that each node compiles on the compiler(s) it is meant
// for and is refused by the other, that dead handles and zero-length sweeps answer without touching
// native code, and that a live-handle call reaches the intended P/Invoke (an unresolved native call
// throws, naming the symbol where the loader says which). Whether Jolt then answers correctly is the
// C++ tests' job (QueryTest, JointBreakTest, RagdollTest, CharacterExtTest).
using System;
using System.Linq;
using Aver.Graph;

static class PhysicsWave1bNodeTests
{
    // Hand-written expectations, in pin order, "inputs>outputs". Deliberately restated here rather than
    // shared with the parser so a typo on either side shows up.
    private const string Filters = "layerMask:int,ignoreBody:int,includeSensors:bool";

    private static readonly (string Type, string Spec)[] Shapes =
    {
        ("RagdollStart",       "exec:exec,entity:int,mode:int,blend:float>then:exec,success:bool"),
        ("RagdollStop",        "exec:exec,entity:int,blend:float>then:exec,success:bool"),
        ("SetRagdollStrength", "exec:exec,entity:int,strength:float>then:exec,success:bool"),
        ("RagdollImpulse",     "exec:exec,entity:int,x:float,y:float,z:float>then:exec,success:bool"),
        ("BoxCast",
            "exec:exec,cx:float,cy:float,cz:float,hx:float,hy:float,hz:float,yaw:float,pitch:float,roll:float," +
            "dirX:float,dirY:float,dirZ:float,maxDist:float," + Filters +
            ">then:exec,hit:bool,body:int,distance:float,pointX:float,pointY:float,pointZ:float,normalX:float,normalY:float,normalZ:float"),
        ("CapsuleCast",
            "exec:exec,cx:float,cy:float,cz:float,radius:float,height:float,yaw:float,pitch:float,roll:float," +
            "dirX:float,dirY:float,dirZ:float,maxDist:float," + Filters +
            ">then:exec,hit:bool,body:int,distance:float,pointX:float,pointY:float,pointZ:float,normalX:float,normalY:float,normalZ:float"),
        ("OverlapBox",
            "exec:exec,cx:float,cy:float,cz:float,hx:float,hy:float,hz:float,yaw:float,pitch:float,roll:float," + Filters +
            ">then:exec,count:int,body:int"),
        ("RaycastAll",
            "exec:exec,originX:float,originY:float,originZ:float,dirX:float,dirY:float,dirZ:float,maxDist:float," + Filters +
            ">then:exec,count:int,body:int,distance:float,pointX:float,pointY:float,pointZ:float"),
        ("BodiesAtPoint",      "exec:exec,x:float,y:float,z:float," + Filters + ">then:exec,count:int,body:int"),
        ("SetJointBreakable",  "exec:exec,joint:int,maxForceN:float,maxTorqueNm:float>then:exec,success:bool"),
        ("SetLockedAxes",
            "exec:exec,body:int,lockX:bool,lockY:bool,lockZ:bool,lockRotX:bool,lockRotY:bool,lockRotZ:bool>then:exec,success:bool"),
        ("SetCcd",             "exec:exec,body:int,enabled:bool>then:exec,success:bool"),
        ("CharacterCanStand",  "entity:int,radius:float,halfHeight:float>canStand:bool,success:bool"),
    };

    public static int RunAll()
    {
        int failures = 0;
        failures += TestPinShapes();
        failures += TestExecNodesAreRefusedByPullCompiler();
        failures += TestCharacterCanStandCompilesOnBothCompilers();
        failures += TestDeadHandleWritesReturnFalseWithoutNative();
        failures += TestZeroLengthCastsMissWithoutNative();
        failures += TestOverlapQueriesReachTheNativeCalls();
        failures += TestCharacterCanStandReachesTheNativeLookup();
        failures += TestRagdollNodesReachTheNativeLiveCheck();
        failures += TestRagdollImpulseBoneAttribute();
        failures += TestHandDeclaredPinsMissingAnInputFailClearly();
        return failures;
    }

    private static string Fmt(Pin p) => p.Name + ":" + p.Type.ToString().ToLowerInvariant();

    private static string SpecOf(Node n) =>
        string.Join(",", n.Pins.Where(p => !p.IsOutput).Select(Fmt)) + ">" +
        string.Join(",", n.Pins.Where(p => p.IsOutput).Select(Fmt));

    /// A one-node push graph: OnTick -> the node, with one OUT. Fine for any node with exec pins.
    private static string ExecGraph(string nodeLine, string outPin) =>
        "OCGRAPH 1\nNODE tick OnTick\n" + nodeLine + "\nLINK tick.exec n.exec\nENTRY tick OnTick\nOUT n " + outPin + "\n";

    private static int TestPinShapes()
    {
        Console.WriteLine("Test: the thirteen wave 1b nodes get exactly the pins the palette declares");
        try
        {
            var sb = new System.Text.StringBuilder("OCGRAPH 1\n");
            foreach (var (type, _) in Shapes) sb.Append("NODE ").Append(type).Append(' ').Append(type).Append('\n');
            if (!OcGraphParser.Parse(sb.ToString(), out var graph, out var err))
            {
                Console.WriteLine($"  FAIL: Parse error: {err}");
                return 1;
            }
            int bad = 0;
            foreach (var (type, spec) in Shapes)
            {
                string got = SpecOf(graph.Nodes[type]);
                if (got != spec)
                {
                    Console.WriteLine($"  FAIL: {type}\n    expected {spec}\n    got      {got}");
                    bad++;
                }
            }
            if (bad != 0) return 1;
            Console.WriteLine("  PASS: 13 pin sets match, in order");
            return 0;
        }
        catch (Exception ex) { Console.WriteLine($"  FAIL: {ex.Message}"); return 1; }
    }

    // Every exec node here acts on the world or caches a query, so a no-ENTRY graph (Compile()) has no
    // notion of WHEN to run it: refused, naming the node, like Spawn and AddImpulse.
    private static int TestExecNodesAreRefusedByPullCompiler()
    {
        Console.WriteLine("Test: the twelve exec nodes are refused by Compile() (no ENTRY), naming the node type");
        try
        {
            int bad = 0;
            foreach (var (type, spec) in Shapes.Where(s => s.Spec.StartsWith("exec:")))
            {
                // The first DATA output: Compile() rejects an exec-typed OUT before it reaches the node.
                string dataOut = spec.Split('>')[1].Split(',').First(o => !o.EndsWith(":exec")).Split(':')[0];
                string text = "OCGRAPH 1\nNODE n " + type + "\nOUT n " + dataOut + "\n";
                if (!OcGraphParser.Parse(text, out var graph, out var perr))
                {
                    Console.WriteLine($"  FAIL: {type}: Parse error: {perr}");
                    bad++;
                    continue;
                }
                var compiled = new GraphCompiler(graph).Compile(out var cerr);
                if (compiled != null || cerr == null || cerr.IndexOf(type, StringComparison.OrdinalIgnoreCase) < 0)
                {
                    Console.WriteLine($"  FAIL: {type}: expected a refusal naming it, got compiled={(compiled != null)} err='{cerr}'");
                    bad++;
                }
            }
            if (bad != 0) return 1;
            Console.WriteLine("  PASS: all twelve refused");
            return 0;
        }
        catch (Exception ex) { Console.WriteLine($"  FAIL: {ex.Message}"); return 1; }
    }

    // A pure read, so BOTH compilers must take it -- the bug shape this file's neighbours keep hitting
    // is a node implemented in only one path.
    private static int TestCharacterCanStandCompilesOnBothCompilers()
    {
        Console.WriteLine("Test: CharacterCanStand compiles on BOTH compilers (a pure read)");
        try
        {
            const string pull = "OCGRAPH 1\nNODE cs CharacterCanStand\nOUT cs canStand\nOUT cs success\n";
            if (!OcGraphParser.Parse(pull, out var g1, out var e1)) { Console.WriteLine($"  FAIL: Parse error: {e1}"); return 1; }
            if (new GraphCompiler(g1).Compile(out var c1) == null) { Console.WriteLine($"  FAIL: Compile() refused it: {c1}"); return 1; }

            const string push = "OCGRAPH 1\nNODE tick OnTick\nENTRY tick OnTick\nNODE cs CharacterCanStand\nOUT cs canStand\nOUT cs success\n";
            if (!OcGraphParser.Parse(push, out var g2, out var e2)) { Console.WriteLine($"  FAIL: Parse error: {e2}"); return 1; }
            if (new GraphCompiler(g2).CompileEntryPoint("OnTick", out var c2) == null) { Console.WriteLine($"  FAIL: CompileEntryPoint() refused it: {c2}"); return 1; }

            Console.WriteLine("  PASS: both compilers accept it");
            return 0;
        }
        catch (Exception ex) { Console.WriteLine($"  FAIL: {ex.Message}"); return 1; }
    }

    // An unwired body/joint pin reads 0, a dead handle: the wrapper must answer false BEFORE any native
    // call (this process has none), which is also what a stale handle does in a running game.
    private static int TestDeadHandleWritesReturnFalseWithoutNative()
    {
        Console.WriteLine("Test: SetLockedAxes / SetCcd / SetJointBreakable on a dead handle report success=false, no native call");
        try
        {
            int bad = 0;
            foreach (var type in new[] { "SetLockedAxes", "SetCcd", "SetJointBreakable" })
            {
                if (!OcGraphParser.Parse(ExecGraph("NODE n " + type, "success"), out var graph, out var perr))
                { Console.WriteLine($"  FAIL: {type}: Parse error: {perr}"); bad++; continue; }
                var fn = new GraphCompiler(graph).CompileEntryPoint("OnTick", out var cerr);
                if (fn == null) { Console.WriteLine($"  FAIL: {type}: compile: {cerr}"); bad++; continue; }
                object? got;
                try { got = fn.DynamicInvoke(); }
                catch (Exception ex) { Console.WriteLine($"  FAIL: {type}: threw {ex.InnerException?.Message ?? ex.Message}"); bad++; continue; }
                if (got is not bool b || b)
                { Console.WriteLine($"  FAIL: {type}: expected false, got {got}"); bad++; }
            }
            if (bad != 0) return 1;
            Console.WriteLine("  PASS: all three answered false cleanly");
            return 0;
        }
        catch (Exception ex) { Console.WriteLine($"  FAIL: {ex.Message}"); return 1; }
    }

    // No direction (every pin unwired) leaves nothing to sweep: a miss, decided in managed code. Also the
    // proof that the nine exec-cached outputs of a cast are readable from the OUT list.
    private static int TestZeroLengthCastsMissWithoutNative()
    {
        Console.WriteLine("Test: BoxCast / CapsuleCast with no direction are a clean miss (hit=false), no native call");
        try
        {
            int bad = 0;
            foreach (var type in new[] { "BoxCast", "CapsuleCast" })
            {
                foreach (string pin in new[] { "hit", "body" })
                {
                    if (!OcGraphParser.Parse(ExecGraph("NODE n " + type, pin), out var graph, out var perr))
                    { Console.WriteLine($"  FAIL: {type}.{pin}: Parse error: {perr}"); bad++; continue; }
                    var fn = new GraphCompiler(graph).CompileEntryPoint("OnTick", out var cerr);
                    if (fn == null) { Console.WriteLine($"  FAIL: {type}.{pin}: compile: {cerr}"); bad++; continue; }
                    object? got;
                    try { got = fn.DynamicInvoke(); }
                    catch (Exception ex) { Console.WriteLine($"  FAIL: {type}.{pin}: threw {ex.InnerException?.Message ?? ex.Message}"); bad++; continue; }
                    bool ok = pin == "hit" ? got is bool b && !b : got is int i && i == 0;
                    if (!ok) { Console.WriteLine($"  FAIL: {type}.{pin}: expected a miss, got {got}"); bad++; }
                }
            }
            if (bad != 0) return 1;
            Console.WriteLine("  PASS: both casts miss, hit=false and body=0");
            return 0;
        }
        catch (Exception ex) { Console.WriteLine($"  FAIL: {ex.Message}"); return 1; }
    }

    /// Walks an exception chain to the first loader-level failure. EntryPointNotFoundException names the
    /// symbol it wanted; the other two mean the library itself was not there, which also proves the call
    /// was attempted but cannot name it.
    private static Exception? FindLoaderFailure(Exception ex)
    {
        for (Exception? at = ex; at != null; at = at.InnerException)
            if (at is EntryPointNotFoundException or DllNotFoundException or BadImageFormatException) return at;
        return null;
    }

    private static int ExpectNativeCall(string label, string text, string? symbol)
    {
        Console.WriteLine($"Test: {label}");
        try
        {
            if (!OcGraphParser.Parse(text, out var graph, out var perr)) { Console.WriteLine($"  FAIL: Parse error: {perr}"); return 1; }
            var fn = new GraphCompiler(graph).CompileEntryPoint("OnTick", out var cerr);
            if (fn == null) { Console.WriteLine($"  FAIL: compile: {cerr}"); return 1; }
            try
            {
                var got = fn.DynamicInvoke();
                Console.WriteLine($"  FAIL: expected a native call to throw in this hostless process, but it returned {got}");
                return 1;
            }
            catch (Exception ex)
            {
                var load = FindLoaderFailure(ex);
                if (load == null) { Console.WriteLine($"  FAIL: threw, but not from the native loader: {ex}"); return 1; }
                if (symbol != null && load is EntryPointNotFoundException && !load.Message.Contains(symbol))
                { Console.WriteLine($"  FAIL: called something else -- expected '{symbol}', got: {load.Message}"); return 1; }
                Console.WriteLine($"  PASS: reached the native call ({load.GetType().Name}): {load.Message}");
                return 0;
            }
        }
        catch (Exception ex) { Console.WriteLine($"  FAIL: {ex.Message}"); return 1; }
    }

    private static int TestOverlapQueriesReachTheNativeCalls()
    {
        int f = 0;
        f += ExpectNativeCall("OverlapBox calls aver_phys_overlap_box", ExecGraph("NODE n OverlapBox", "count"), "aver_phys_overlap_box");
        f += ExpectNativeCall("RaycastAll calls aver_phys_raycast_all", ExecGraph("NODE n RaycastAll", "count"), "aver_phys_raycast_all");
        f += ExpectNativeCall("BodiesAtPoint calls aver_phys_point_query", ExecGraph("NODE n BodiesAtPoint", "count"), "aver_phys_point_query");
        return f;
    }

    private static int TestCharacterCanStandReachesTheNativeLookup()
    {
        // Pull path: the node is a pure read, so it is reached through Compile() with no ENTRY at all.
        Console.WriteLine("Test: CharacterCanStand (PULL) looks the character up through aver_phys_character_of_entity");
        try
        {
            if (!OcGraphParser.Parse("OCGRAPH 1\nNODE cs CharacterCanStand\nOUT cs canStand\n", out var graph, out var perr))
            { Console.WriteLine($"  FAIL: Parse error: {perr}"); return 1; }
            var fn = new GraphCompiler(graph).Compile(out var cerr);
            if (fn == null) { Console.WriteLine($"  FAIL: compile: {cerr}"); return 1; }
            try
            {
                var got = fn.DynamicInvoke();
                Console.WriteLine($"  FAIL: expected the native lookup to throw here, but it returned {got}");
                return 1;
            }
            catch (Exception ex)
            {
                var load = FindLoaderFailure(ex);
                if (load == null) { Console.WriteLine($"  FAIL: threw, but not from the native loader: {ex}"); return 1; }
                if (load is EntryPointNotFoundException && !load.Message.Contains("aver_phys_character_of_entity"))
                { Console.WriteLine($"  FAIL: expected aver_phys_character_of_entity, got: {load.Message}"); return 1; }
                Console.WriteLine($"  PASS: reached the lookup ({load.GetType().Name}): {load.Message}");
                return 0;
            }
        }
        catch (Exception ex) { Console.WriteLine($"  FAIL: {ex.Message}"); return 1; }
    }

    // The wrappers check Entity.IsAlive first, a P/Invoke (aver_scene_valid): so a graph reaching one
    // proves entity, mode and blend all flowed through the compiled call.
    private static int TestRagdollNodesReachTheNativeLiveCheck()
    {
        int f = 0;
        foreach (var (type, extra) in new[] { ("RagdollStart", ""), ("RagdollStop", ""), ("SetRagdollStrength", ""), ("RagdollImpulse", " bone=head") })
            f += ExpectNativeCall($"{type} checks its entity through aver_scene_valid first",
                                  ExecGraph("NODE n " + type + extra, "success"), "aver_scene_valid");
        return f;
    }

    private static int TestRagdollImpulseBoneAttribute()
    {
        Console.WriteLine("Test: RagdollImpulse reads bone= from the NODE line, and compiles with or without it");
        try
        {
            if (!OcGraphParser.Parse(ExecGraph("NODE n RagdollImpulse bone=spine_02", "success"), out var graph, out var perr))
            { Console.WriteLine($"  FAIL: Parse error: {perr}"); return 1; }
            if (!graph.Nodes["n"].Attrs.TryGetValue("bone", out var bone) || bone != "spine_02")
            { Console.WriteLine($"  FAIL: bone= was not captured, got '{bone}'"); return 1; }
            if (new GraphCompiler(graph).CompileEntryPoint("OnTick", out var e1) == null)
            { Console.WriteLine($"  FAIL: compile with bone=: {e1}"); return 1; }

            // No bone= is the root-part push, not an error.
            if (!OcGraphParser.Parse(ExecGraph("NODE n RagdollImpulse", "success"), out var g2, out var perr2))
            { Console.WriteLine($"  FAIL: Parse error: {perr2}"); return 1; }
            if (new GraphCompiler(g2).CompileEntryPoint("OnTick", out var e2) == null)
            { Console.WriteLine($"  FAIL: compile without bone=: {e2}"); return 1; }

            Console.WriteLine("  PASS: bone= captured; both forms compile");
            return 0;
        }
        catch (Exception ex) { Console.WriteLine($"  FAIL: {ex.Message}"); return 1; }
    }

    // One hand-declared PIN suppresses every default (AddDefaultPins' rule), so an author who declares
    // only exec/then gets a message naming the missing input pin, not a runtime fault.
    private static int TestHandDeclaredPinsMissingAnInputFailClearly()
    {
        Console.WriteLine("Test: BoxCast with hand-declared PINs that omit its inputs fails to compile, naming the pin");
        try
        {
            const string text =
                "OCGRAPH 1\nNODE tick OnTick\nNODE n BoxCast\n" +
                "PIN n exec in exec\nPIN n then out exec\nPIN n hit out bool\n" +
                "LINK tick.exec n.exec\nENTRY tick OnTick\nOUT n hit\n";
            if (!OcGraphParser.Parse(text, out var graph, out var perr)) { Console.WriteLine($"  FAIL: Parse error: {perr}"); return 1; }
            var fn = new GraphCompiler(graph).CompileEntryPoint("OnTick", out var cerr);
            if (fn != null) { Console.WriteLine("  FAIL: expected a compile failure"); return 1; }
            if (cerr == null || cerr.IndexOf("cx", StringComparison.Ordinal) < 0)
            { Console.WriteLine($"  FAIL: expected an error naming the missing input pin, got: {cerr}"); return 1; }
            Console.WriteLine($"  PASS: {cerr}");
            return 0;
        }
        catch (Exception ex) { Console.WriteLine($"  FAIL: {ex.Message}"); return 1; }
    }
}
