// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Shared checks for the table-driven node families added in wave 2 (gameplay abilities, Enhanced Input,
// data tables and localization, montages and root motion). Each family's test file lists its rows and
// calls these, so the five assertions GameSystemNodeTests makes for the older families are made for the
// new ones by name, and a failure says which family and which node.
//
// Like GameSystemNodeTests this runs in a bare process with no native engine DLLs: it proves the WIRING
// (a real method behind every row, pins that match its parameters, default pins and values, push-only
// versus pure, required attributes, valid IL), not what the engine does when called.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using Aver.Graph;

static class NodeRowKit
{
    public static int Fail(string msg) { Console.WriteLine("  FAIL: " + msg); return 1; }

    /// <summary>One row to assert: node type, kind, and its pins as "name:type:in|out" in order.</summary>
    public readonly record struct Row(string Type, GenericKind Kind, string[] Pins, string[]? Defaults = null);

    // The NODE line for a table node, with a dummy value for every required attribute it takes.
    public static string NodeLine(GenericNodeSpec s, string id)
    {
        var sb = new StringBuilder($"NODE {id} {s.Type}");
        foreach (var a in s.Args.Where(x => x.StartsWith('@')))
        {
            string key = a[1..].TrimEnd('?').Split('|')[0];
            sb.Append($" {key}=dummy");
        }
        return sb.ToString();
    }

    /// <summary>Null when the graph compiled; otherwise the reason, whether the compiler returned it or threw it.</summary>
    public static string? CompileError(string text, string? entry)
    {
        if (!OcGraphParser.Parse(text, out var g, out var perr)) return "parse: " + perr;
        try
        {
            string? err = null;
            var fn = entry == null ? new GraphCompiler(g).Compile(out err) : new GraphCompiler(g).CompileEntryPoint(entry, out err);
            return fn == null ? err ?? "compile returned null" : null;
        }
        catch (Exception e) { return e.Message; }
    }

    /// <summary>Compiles the entry and invokes it once: null when the IL is valid (the native call may then fail,
    /// that is the bare process), otherwise the reason.</summary>
    public static string? CompileAndRun(string text, string entry)
    {
        if (!OcGraphParser.Parse(text, out var g, out var perr)) return "parse: " + perr;
        try
        {
            var fn = new GraphCompiler(g).CompileEntryPoint(entry, out var err);
            if (fn == null) return err ?? "compile returned null";
            try { fn.DynamicInvoke(); }
            catch (TargetInvocationException e) when (e.InnerException is not InvalidProgramException) { }
            return null;
        }
        catch (Exception e) { return e.GetType().Name + ": " + e.Message; }
    }

    /// <summary>True when an exception, or anything it wraps, is the native framework DLL not being loadable here.</summary>
    public static bool NativeMissing(Exception e)
    {
        for (Exception? x = e; x != null; x = x.InnerException)
            if (x is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException) return true;
        return false;
    }

    /// <summary>True when the graph text declares an ENTRY with this event name.</summary>
    public static bool HasEntry(Aver.Graph.Graph g, string eventName)
    {
        foreach (var (_, name) in g.EntryPoints)
            if (name == eventName) return true;
        return false;
    }

    /// <summary>Every row exists with the stated kind, and Validate() has no complaint about any of them.</summary>
    public static int CheckRows(string family, Row[] rows)
    {
        Console.WriteLine($"Test: {family} rows exist, have the right kind and match the methods they name");
        int bad = 0;
        foreach (var r in rows)
        {
            if (!GameSystemNodes.TryGet(r.Type, out var s)) { Console.WriteLine($"    {r.Type}: no row in the node table"); ++bad; continue; }
            if (s.Kind != r.Kind) { Console.WriteLine($"    {r.Type}: kind is {s.Kind}, expected {r.Kind}"); ++bad; }
        }
        foreach (var p in GameSystemNodes.Validate())
            if (rows.Any(r => p.StartsWith(r.Type + ":", StringComparison.OrdinalIgnoreCase))) { Console.WriteLine("    " + p); ++bad; }
        if (bad > 0) return Fail($"{bad} problem(s)");
        Console.WriteLine($"  PASS: {rows.Length} rows");
        return 0;
    }

    /// <summary>A bare NODE line gets exactly the listed pins, in order, and the listed default values
    /// ("pin=value" with the value's invariant text).</summary>
    public static int CheckDefaultPins(string family, Row[] rows)
    {
        Console.WriteLine($"Test: {family} bare NODE lines get the table's pins and default values");
        int bad = 0;
        foreach (var r in rows)
        {
            if (!GameSystemNodes.TryGet(r.Type, out var s)) { Console.WriteLine($"    {r.Type}: no row"); ++bad; continue; }
            string text = "OCGRAPH 1\n" + NodeLine(s, "n") + "\n";
            if (!OcGraphParser.Parse(text, out var g, out var err)) { Console.WriteLine($"    {r.Type}: parse: {err}"); ++bad; continue; }
            var got = g.Nodes["n"].Pins
                .Select(p => $"{p.Name}:{p.Type.ToString().ToLowerInvariant()}:{(p.IsOutput ? "out" : "in")}").ToArray();
            if (!got.SequenceEqual(r.Pins))
            {
                Console.WriteLine($"    {r.Type}: pins [{string.Join(", ", got)}], expected [{string.Join(", ", r.Pins)}]");
                ++bad;
            }
            foreach (var d in r.Defaults ?? Array.Empty<string>())
            {
                var kv = d.Split('=', 2);
                var pv = g.PinnedValues.FirstOrDefault(v => v.NodeId == "n" && v.PinName == kv[0]);
                string actual = pv?.Value is IFormattable f ? f.ToString(null, System.Globalization.CultureInfo.InvariantCulture)
                              : pv?.Value?.ToString()?.ToLowerInvariant() ?? "(none)";
                if (actual != kv[1]) { Console.WriteLine($"    {r.Type}: default of '{kv[0]}' is {actual}, expected {kv[1]}"); ++bad; }
            }
            // Every row without a default must leave no pinned value behind.
            if (r.Defaults == null && g.PinnedValues.Any(v => v.NodeId == "n"))
            { Console.WriteLine($"    {r.Type}: unexpected default pin values"); ++bad; }
        }
        if (bad > 0) return Fail($"{bad} problem(s)");
        Console.WriteLine("  PASS");
        return 0;
    }

    /// <summary>Parse, compile and invoke every listed row once. Invoking JIT-compiles the emitted IL, so an
    /// invalid program throws InvalidProgramException before any native call; anything else (the missing
    /// DLL, a null host) means the IL itself was fine.</summary>
    public static int CheckIlValid(string family, IEnumerable<string> types)
    {
        Console.WriteLine($"Test: the IL emitted for every {family} node is valid");
        int bad = 0, ran = 0;
        foreach (var type in types)
        {
            if (!GameSystemNodes.TryGet(type, out var s) || s.Kind == GenericKind.Trigger) continue;
            string text, outPin = s.Outputs.Length > 0 ? s.Outputs[0].Name : "";
            string? entry = null;
            if (s.Kind == GenericKind.Exec)
            {
                text = "OCGRAPH 1\nNODE tick OnTick\n" + NodeLine(s, "n") + "\nLINK tick.exec n.exec\nENTRY tick OnTick\n";
                entry = "OnTick";
            }
            else text = "OCGRAPH 1\n" + NodeLine(s, "n") + $"\nOUT n {outPin}\n";

            if (!OcGraphParser.Parse(text, out var g, out var perr)) { Console.WriteLine($"    {type}: parse: {perr}"); ++bad; continue; }
            Delegate? fn;
            string? cerr = null;
            try
            {
                fn = entry == null ? new GraphCompiler(g).Compile(out cerr) : new GraphCompiler(g).CompileEntryPoint(entry, out cerr);
            }
            catch (Exception e) { Console.WriteLine($"    {type}: compile threw {e.GetType().Name}: {e.Message}"); ++bad; continue; }
            if (fn == null) { Console.WriteLine($"    {type}: compile: {cerr}"); ++bad; continue; }
            try { fn.DynamicInvoke(); ++ran; }
            catch (TargetInvocationException e) when (e.InnerException is not InvalidProgramException) { ++ran; }
            catch (Exception e) { Console.WriteLine($"    {type}: invoke: {e.GetType().Name}: {e.Message}"); ++bad; }
        }
        if (bad > 0) return Fail($"{bad} node(s) did not compile or emitted invalid IL");
        Console.WriteLine($"  PASS: {ran} nodes compiled and JIT'd");
        return 0;
    }

    /// <summary>Every exec row is refused by Compile() with an error naming the node; every pure row compiles
    /// under it, and an exec chain that reads a pure row's output compiles under CompileEntryPoint.</summary>
    public static int CheckCompilerSplit(string family, IEnumerable<string> types)
    {
        Console.WriteLine($"Test: {family} exec nodes are push-only, pure reads work under both compilers");
        int bad = 0;
        foreach (var type in types)
        {
            if (!GameSystemNodes.TryGet(type, out var s) || s.Kind == GenericKind.Trigger) continue;
            if (s.Kind == GenericKind.Exec)
            {
                string? err = CompileError("OCGRAPH 1\n" + NodeLine(s, "n") + $"\nOUT n {s.Outputs[0].Name}\n", null);
                if (err == null) { Console.WriteLine($"    {type}: Compile() accepted an ungated exec node"); ++bad; }
                else if (!err.Contains(type, StringComparison.OrdinalIgnoreCase)) { Console.WriteLine($"    {type}: refusal does not name the node: {err}"); ++bad; }
            }
            else
            {
                string? pull = CompileError("OCGRAPH 1\n" + NodeLine(s, "n") + $"\nOUT n {s.Outputs[0].Name}\n", null);
                if (pull != null) { Console.WriteLine($"    {type}: Compile() refused a pure read: {pull}"); ++bad; }
                // Under the push compiler a pure node's first output feeds a Print node of the right pin type.
                var o = s.Outputs[0];
                // Print takes a float, PrintInt an int, Branch a bool (pin "cond").
                (string printer, string inPin) = o.Type == PinType.Float ? ("Print", "value")
                                               : o.Type == PinType.Bool ? ("Branch", "cond") : ("PrintInt", "value");
                string push = "OCGRAPH 1\nNODE tick OnTick\n" + NodeLine(s, "n") + $"\nNODE pr {printer}\nLINK tick.exec pr.exec\nLINK n.{o.Name} pr.{inPin}\nENTRY tick OnTick\n";
                string? perr = CompileError(push, "OnTick");
                if (perr != null) { Console.WriteLine($"    {type}: CompileEntryPoint refused a pure read feeding {printer}: {perr}"); ++bad; }
            }
        }
        if (bad > 0) return Fail($"{bad} problem(s)");
        Console.WriteLine("  PASS");
        return 0;
    }

    /// <summary>A node missing a required attribute fails to compile, naming it; an optional one does not need it.
    /// <paramref name="line"/> is the NODE line with the attribute left out.</summary>
    public static int CheckMissingAttribute(string line, string wantKey, bool exec)
    {
        string text = exec
            ? "OCGRAPH 1\nNODE tick OnTick\n" + line + "\nLINK tick.exec n.exec\nENTRY tick OnTick\n"
            : "OCGRAPH 1\n" + line + "\nOUT n " + PinFor(line) + "\n";
        string? err = CompileError(text, exec ? "OnTick" : null);
        if (err == null) return Fail($"'{line}' compiled with no {wantKey}");
        if (!err.Contains(wantKey)) return Fail($"'{line}': error does not mention {wantKey}: {err}");
        return 0;
    }

    public static int CheckOptionalAttribute(string line, bool exec)
    {
        string text = exec
            ? "OCGRAPH 1\nNODE tick OnTick\n" + line + "\nLINK tick.exec n.exec\nENTRY tick OnTick\n"
            : "OCGRAPH 1\n" + line + "\nOUT n " + PinFor(line) + "\n";
        string? err = CompileError(text, exec ? "OnTick" : null);
        return err == null ? 0 : Fail($"'{line}' should not need its optional attributes: {err}");
    }

    // The first output pin of the node on a "NODE n <type> ..." line.
    private static string PinFor(string line) =>
        GameSystemNodes.TryGet(line.Split(' ')[2], out var s) ? s.Outputs[0].Name : "";
}
