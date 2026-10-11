// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for the sequencer nodes (docs/plans/wave5/SEQUENCER_TRACKS.md section 10): the 8 rows in
// GraphSequenceNodeTable.cs, every method on SequenceGraphInterop. Bare-process scope like MontageNodeTests: the
// wiring the table owns, not what a sequence does when played (tests/game's SequenceDirectorTest does that).
// A sequence is named by an attribute and none of these nodes takes an entity pin except Bind Sequence Actor.
using System;
using System.Linq;
using Aver.Graph;
using Row = NodeRowKit.Row;

static class SequenceNodeTests
{
    private static readonly Row[] Rows =
    {
        new("playsequence", GenericKind.Exec, new[] { "exec:exec:in", "start:float:in", "rate:float:in", "then:exec:out", "success:bool:out" }, new[] { "start=-1", "rate=0" }),
        new("pausesequence", GenericKind.Exec, new[] { "exec:exec:in", "then:exec:out", "success:bool:out" }),
        new("stopsequence", GenericKind.Exec, new[] { "exec:exec:in", "then:exec:out", "success:bool:out" }),
        new("setsequencetime", GenericKind.Exec, new[] { "exec:exec:in", "seconds:float:in", "then:exec:out", "success:bool:out" }),
        new("setsequencerate", GenericKind.Exec, new[] { "exec:exec:in", "rate:float:in", "then:exec:out", "success:bool:out" }),
        new("bindsequenceactor", GenericKind.Exec, new[] { "exec:exec:in", "entity:int:in", "then:exec:out", "success:bool:out" }),
        new("issequenceplaying", GenericKind.Pure, new[] { "result:bool:out" }),
        new("getsequencestate", GenericKind.Pure, new[] { "found:bool:out", "time:float:out", "length:float:out", "rate:float:out" }),
    };

    public static int RunAll()
    {
        int failures = 0;
        string[] types = Rows.Select(r => r.Type).ToArray();
        failures += NodeRowKit.CheckRows("sequencer", Rows);
        failures += TestRowSetIsTheDesignSet();
        failures += NodeRowKit.CheckDefaultPins("sequencer", Rows);
        failures += TestDocumentedDefaults();
        failures += NodeRowKit.CheckIlValid("sequencer", types);
        failures += NodeRowKit.CheckCompilerSplit("sequencer", types);
        failures += TestMissingAttributeFailsByName();
        failures += TestPlaybackChain();
        return failures;
    }

    private static int Fail(string msg) => NodeRowKit.Fail(msg);

    private static int TestRowSetIsTheDesignSet()
    {
        Console.WriteLine("Test: the sequencer rows are exactly the designed set of 8, 2 pure reads and 6 exec writes");
        var want = Rows.Select(r => r.Type).OrderBy(x => x).ToArray();
        var got = GameSystemNodes.All.Where(s => s.Owner.Name == "SequenceGraphInterop").Select(s => s.Type).OrderBy(x => x).ToArray();
        if (!got.SequenceEqual(want))
            return Fail($"table has [{string.Join(", ", got.Except(want))}] extra and lacks [{string.Join(", ", want.Except(got))}]");
        if (Rows.Length != 8) return Fail($"expected 8 rows, test lists {Rows.Length}");
        if (Rows.Count(r => r.Kind == GenericKind.Pure) != 2) return Fail("only Is Sequence Playing and Get Sequence State are reads");
        Console.WriteLine("  PASS");
        return 0;
    }

    // Play's defaults mean 'continue from where it is, at the current rate'; a pin that read 0 would restart every time.
    private static int TestDocumentedDefaults()
    {
        Console.WriteLine("Test: Play Sequence defaults start to -1 (continue) and rate to 0 (keep the current rate)");
        const string text = "OCGRAPH 1\nNODE p PlaySequence sequence=Intro\n";
        if (!OcGraphParser.Parse(text, out var g, out var err)) return Fail("parse: " + err);
        object? V(string pin) => g.PinnedValues.FirstOrDefault(v => v.NodeId == "p" && v.PinName == pin)?.Value;
        if (!(V("start") is float s && s == -1f && V("rate") is float r && r == 0f)) return Fail("a default is missing or wrong");
        if (g.Nodes["p"].Attrs["sequence"] != "Intro") return Fail("sequence= did not reach Node.Attrs");
        Console.WriteLine("  PASS");
        return 0;
    }

    // Everything but Stop and Is Playing needs the sequence's name; Stop and Is Playing take 'every' and 'any' from empty.
    private static int TestMissingAttributeFailsByName()
    {
        Console.WriteLine("Test: sequencer nodes missing sequence= fail to compile, naming it; Stop and Is Playing compile bare");
        int f = 0;
        f += NodeRowKit.CheckMissingAttribute("NODE n PlaySequence", "sequence=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n PauseSequence", "sequence=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n SetSequenceTime", "sequence=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n SetSequenceRate", "sequence=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n BindSequenceActor binding=Hero", "sequence=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n BindSequenceActor sequence=Intro", "binding=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n GetSequenceState", "sequence=", exec: false);
        f += NodeRowKit.CheckOptionalAttribute("NODE n StopSequence", exec: true);
        f += NodeRowKit.CheckOptionalAttribute("NODE n IsSequencePlaying", exec: false);
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // A cutscene trigger: bind the hero, play, speed it up, read the playhead, and stop it while it still plays.
    private static int TestPlaybackChain()
    {
        Console.WriteLine("Test: Bind, Play, Rate and Time chain; Is Playing gates Stop; State feeds later nodes");
        const string text = "OCGRAPH 1\nNODE tick OnTick\nNODE bd BindSequenceActor sequence=Intro binding=Hero\nNODE pl PlaySequence sequence=Intro\n" +
                            "NODE rt SetSequenceRate sequence=Intro\nNODE tm SetSequenceTime sequence=Intro\nNODE ip IsSequencePlaying sequence=Intro\nNODE br Branch\n" +
                            "NODE st StopSequence sequence=Intro\nNODE gs GetSequenceState sequence=Intro\nNODE pt Print\nNODE pr Print\n" +
                            "LINK tick.exec bd.exec\nLINK bd.then pl.exec\nLINK pl.then rt.exec\nLINK gs.rate rt.rate\nLINK rt.then tm.exec\nLINK gs.time tm.seconds\n" +
                            "LINK tm.then br.exec\nLINK ip.result br.cond\nLINK br.true st.exec\nLINK st.then pt.exec\nLINK gs.length pt.value\n" +
                            "LINK pt.then pr.exec\nLINK gs.time pr.value\nENTRY tick OnTick\n";
        string? bad = NodeRowKit.CompileAndRun(text, "OnTick");
        if (bad != null) return Fail(bad);
        Console.WriteLine("  PASS");
        return 0;
    }
}
