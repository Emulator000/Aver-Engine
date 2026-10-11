// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Tests for Enhanced Input's managed half: the trigger/modifier spec factories, the pinned enum
// values, and the OnInputAction entry binding. The native evaluation (hold timing, chords, ...) is
// FrameworkTest's; nothing here calls the engine except where a case says so, and those cases report
// SKIP when the framework DLL is not loadable, the same tolerance AudioNodeTests uses.
using System;
using System.Globalization;
using System.Linq;
using Aver.Framework;
using Aver.Graph;
using Row = NodeRowKit.Row;

static class EnhancedInputTests
{
    // The graph nodes (docs/plans/wave2/ENHANCED_INPUT.md section 7): rows in GraphGameSystemNodeTable.cs,
    // pin order as the table has it. The managed half and the nodes share this file and this registration.
    private static readonly Row[] NodeRows =
    {
        new("inputactionstate", GenericKind.Pure, new[] { "state:int:out", "started:bool:out", "ongoing:bool:out", "triggered:bool:out", "completed:bool:out", "canceled:bool:out", "elapsed:float:out", "triggeredTime:float:out", "x:float:out", "y:float:out", "z:float:out", "success:bool:out" }),
        new("addinputtrigger", GenericKind.Exec, new[] { "exec:exec:in", "then:exec:out", "success:bool:out" }),
        new("addinputmodifier", GenericKind.Exec, new[] { "exec:exec:in", "then:exec:out", "success:bool:out" }),
        new("clearinputconfig", GenericKind.Exec, new[] { "exec:exec:in", "then:exec:out", "success:bool:out" }),
        new("oninputaction", GenericKind.Trigger, new[] { "exec:exec:out" }),
    };

    public static int RunAll()
    {
        int failures = 0;

        failures += TestTriggerSpecs();
        failures += TestModifierSpecs();
        failures += TestSpecsIgnoreCurrentCulture();
        failures += TestEnumValuesArePinned();
        failures += TestActionReferencingTriggers();
        failures += TestOnInputActionBindsLikeOnEvent();

        string[] nodeTypes = NodeRows.Select(r => r.Type).ToArray();
        failures += NodeRowKit.CheckRows("Enhanced Input", NodeRows);
        failures += NodeRowKit.CheckDefaultPins("Enhanced Input", NodeRows);
        failures += NodeRowKit.CheckIlValid("Enhanced Input", nodeTypes);
        failures += NodeRowKit.CheckCompilerSplit("Enhanced Input", nodeTypes);
        failures += TestNodeMissingAttributeFailsByName();
        failures += TestSpecsSurviveTheNodeLine();
        failures += TestPolledStateChain();
        failures += TestOnInputActionEntryChain();

        return failures;
    }

    private static int Check(string what, string actual, string expected)
    {
        if (actual == expected) return 0;
        Console.WriteLine($"  FAIL: {what}: expected '{expected}', got '{actual}'");
        return 1;
    }

    // Canonical text copied as literals from OcInputTest's formatTriggerSpec/formatModifierSpec table,
    // so the two languages cannot drift apart unnoticed.
    private static int TestTriggerSpecs()
    {
        Console.WriteLine("Test: InputTrigger factories write the canonical spec text");
        int f = 0;
        f += Check("Down()", InputTrigger.Down().Spec, "down");
        f += Check("Down(0.3)", InputTrigger.Down(0.3f).Spec, "down:0.3");
        f += Check("Pressed()", InputTrigger.Pressed().Spec, "pressed");
        f += Check("Released(0.5)", InputTrigger.Released(0.5f).Spec, "released:0.5");
        f += Check("Hold(0.5)", InputTrigger.Hold(0.5f).Spec, "hold:0.5");
        f += Check("Hold once", InputTrigger.Hold(0.5f, once: true).Spec, "hold:0.5:once");
        f += Check("Hold act+once", InputTrigger.Hold(0.5f, once: true, actuation: 0.3f).Spec, "hold:0.5:0.3:once");
        f += Check("HoldAndRelease", InputTrigger.HoldAndRelease(1f).Spec, "holdrelease:1");
        f += Check("Tap()", InputTrigger.Tap().Spec, "tap");
        f += Check("Tap(0.18)", InputTrigger.Tap(0.18f).Spec, "tap:0.18");
        f += Check("Tap act", InputTrigger.Tap(0.2f, 0.5f).Spec, "tap:0.2:0.5");
        f += Check("Pulse", InputTrigger.Pulse(0.12f).Spec, "pulse:0.12");
        f += Check("Pulse limit", InputTrigger.Pulse(0.25f, 3).Spec, "pulse:0.25:3");
        f += Check("Pulse nostart", InputTrigger.Pulse(0.25f, 3, false).Spec, "pulse:0.25:3:nostart");
        f += Check("Pulse nostart unlimited", InputTrigger.Pulse(0.25f, 0, false).Spec, "pulse:0.25:nostart");
        f += Check("FromSpec passes through", InputTrigger.FromSpec("chord:Mod").Spec, "chord:Mod");
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    private static int TestModifierSpecs()
    {
        Console.WriteLine("Test: InputModifier factories write the canonical spec text");
        int f = 0;
        f += Check("DeadZone()", InputModifier.DeadZone().Spec, "deadzone");
        f += Check("DeadZone(0.1)", InputModifier.DeadZone(0.1f).Spec, "deadzone:0.1");
        f += Check("DeadZone(0.1,0.9,axial)", InputModifier.DeadZone(0.1f, 0.9f, DeadZoneType.Axial).Spec, "deadzone:0.1:0.9:axial");
        f += Check("DeadZone axial default", InputModifier.DeadZone(type: DeadZoneType.Axial).Spec, "deadzone:axial");
        f += Check("Scale(2)", InputModifier.Scale(2f).Spec, "scale:2");
        f += Check("Scale(1,2,3)", InputModifier.Scale(1f, 2f, 3f).Spec, "scale:1:2:3");
        f += Check("Scale uniform triple", InputModifier.Scale(2f, 2f, 2f).Spec, "scale:2");
        f += Check("Negate()", InputModifier.Negate().Spec, "negate");
        f += Check("Negate xz", InputModifier.Negate(true, false, true).Spec, "negate:xz");
        f += Check("Swizzle ZYX", InputModifier.Swizzle(SwizzleOrder.ZYX).Spec, "swizzle:zyx");
        f += Check("Smooth", InputModifier.Smooth(0.04f).Spec, "smooth:0.04");
        f += Check("ResponseCurve(1.4)", InputModifier.ResponseCurve(1.4f).Spec, "curve:1.4");
        f += Check("ResponseCurve triple", InputModifier.ResponseCurve(1f, 2f, 3f).Spec, "curve:1:2:3");
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    private static int TestSpecsIgnoreCurrentCulture()
    {
        Console.WriteLine("Test: spec text is culture-invariant (decimal comma cultures)");
        CultureInfo saved = CultureInfo.CurrentCulture;
        try
        {
            CultureInfo.CurrentCulture = new CultureInfo("de-DE");
            int f = 0;
            f += Check("Hold under de-DE", InputTrigger.Hold(0.5f, once: true).Spec, "hold:0.5:once");
            f += Check("Pulse under de-DE", InputTrigger.Pulse(0.25f, 3, false, 0.4f).Spec, "pulse:0.25:3:0.4:nostart");
            f += Check("DeadZone under de-DE", InputModifier.DeadZone(0.1f, 0.9f).Spec, "deadzone:0.1:0.9");
            if (f == 0) Console.WriteLine("  PASS");
            return f == 0 ? 0 : 1;
        }
        catch (CultureNotFoundException)
        {
            Console.WriteLine("  SKIP: de-DE not available (invariant globalization)");
            return 0;
        }
        finally { CultureInfo.CurrentCulture = saved; }
    }

    // The enum values are the ABI (framework_abi.h); AbiEnumTest checks them against the header, this
    // pins the arithmetic the event pump relies on.
    private static int TestEnumValuesArePinned()
    {
        Console.WriteLine("Test: TriggerState and ActionEvent values match the ABI constants");
        bool ok = (int)TriggerState.None == 0 && (int)TriggerState.Ongoing == 1 && (int)TriggerState.Triggered == 2 &&
                  (int)ActionEvent.Started == 1 && (int)ActionEvent.Ongoing == 2 && (int)ActionEvent.Triggered == 4 &&
                  (int)ActionEvent.Completed == 8 && (int)ActionEvent.Canceled == 16;
        if (!ok) { Console.WriteLine("  FAIL: an enum value drifted from AVER_FW_TRIGGER_STATE_* / AVER_FW_INPUT_EVENT_*"); return 1; }
        Console.WriteLine("  PASS");
        return 0;
    }

    // Chorded/Combo take InputActions, and an InputAction registers itself natively.
    private static int TestActionReferencingTriggers()
    {
        Console.WriteLine("Test: Chorded and Combo name their actions");
        try
        {
            InputAction fire = InputAction.Digital("EITestFire");
            InputAction jump = InputAction.Digital("EITestJump");
            int f = 0;
            f += Check("Chorded", InputTrigger.Chorded(fire).Spec, "chord:EITestFire");
            f += Check("Combo", InputTrigger.Combo(0.4f, fire, fire, jump).Spec, "combo:0.4:EITestFire,EITestFire,EITestJump");
            if (f == 0) Console.WriteLine("  PASS");
            return f == 0 ? 0 : 1;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            Console.WriteLine("  SKIP: framework DLL not loadable here (InputAction registers natively)");
            return 0;
        }
    }

    // OnInputAction is the same runtime as AN_OnEvent: BindGraph subscribes the entity to the bus event
    // named by the ENTRY. Needs the native event bus to observe the subscription.
    private static int TestOnInputActionBindsLikeOnEvent()
    {
        Console.WriteLine("Test: OnInputAction and AN_OnEvent entries both subscribe to Input.<action>.<event>");
        const string text = "OCGRAPH 1\n" +
                            "NODE a OnInputAction action=EITestBind event=Triggered\nENTRY a Input.EITestBind.Triggered\n" +
                            "NODE b AN_OnEvent\nENTRY b Input.EITestBind.Completed\n";
        if (!OcGraphParser.Parse(text, out var graph, out var err))
        {
            Console.WriteLine($"  FAIL: Parse error: {err}");
            return 1;
        }
        const int entity = 987654;
        try
        {
            GraphTimerEvents.BindGraph(entity, graph);
            int a = Events.ListenerCount("Input.EITestBind.Triggered");
            int b = Events.ListenerCount("Input.EITestBind.Completed");
            GraphTimerEvents.UnbindEntity(entity);
            if (a != 1 || b != 1)
            {
                Console.WriteLine($"  FAIL: listener counts after BindGraph were {a} and {b}, expected 1 and 1");
                return 1;
            }
            Console.WriteLine("  PASS");
            return 0;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            Console.WriteLine("  SKIP: framework DLL not loadable here (the event bus is native)");
            return 0;
        }
    }

    // Actions are addressed by NAME, so a node with no action= has nothing to act on; the trigger and
    // modifier specs are just as required.
    private static int TestNodeMissingAttributeFailsByName()
    {
        Console.WriteLine("Test: an Enhanced Input node missing its attribute fails to compile, naming it");
        int f = 0;
        f += NodeRowKit.CheckMissingAttribute("NODE n InputActionState", "action=", exec: false);
        f += NodeRowKit.CheckMissingAttribute("NODE n AddInputTrigger", "action=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n AddInputTrigger action=Jump", "trigger=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n AddInputModifier action=Move", "modifier=", exec: true);
        f += NodeRowKit.CheckMissingAttribute("NODE n ClearInputConfig", "action=", exec: true);
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // Specs carry ':' and ',' (hold:0.5:once, combo:0.4:A,B); the NODE line cuts at the first '=' only.
    private static int TestSpecsSurviveTheNodeLine()
    {
        Console.WriteLine("Test: trigger= and modifier= specs reach Node.Attrs intact");
        const string text = "OCGRAPH 1\nNODE t AddInputTrigger action=Charge trigger=hold:0.5:once\n" +
                            "NODE c AddInputTrigger action=Dash trigger=combo:0.4:Left,Right\nNODE m AddInputModifier action=Move modifier=deadzone:0.2:0.9:axial\n";
        if (!OcGraphParser.Parse(text, out var g, out var err))
        {
            Console.WriteLine($"  FAIL: parse: {err}");
            return 1;
        }
        int f = 0;
        f += Check("hold spec", g.Nodes["t"].Attrs["trigger"], "hold:0.5:once");
        f += Check("combo spec", g.Nodes["c"].Attrs["trigger"], "combo:0.4:Left,Right");
        f += Check("modifier spec", g.Nodes["m"].Attrs["modifier"], "deadzone:0.2:0.9:axial");
        if (f == 0) Console.WriteLine("  PASS");
        return f == 0 ? 0 : 1;
    }

    // Configure once at start (clear, then add: adds are not idempotent), then poll the state each tick.
    private static int TestPolledStateChain()
    {
        Console.WriteLine("Test: Clear/Add Input Trigger/Modifier chain from OnStart, and Input Action State feeding Branch and Print");
        const string start = "OCGRAPH 1\nNODE s OnStart\nNODE c ClearInputConfig action=Charge\n" +
                             "NODE t AddInputTrigger action=Charge trigger=hold:0.5\nNODE m AddInputModifier action=Charge modifier=deadzone\n" +
                             "LINK s.exec c.exec\nLINK c.then t.exec\nLINK t.then m.exec\nENTRY s OnStart\n";
        string? bad = NodeRowKit.CompileAndRun(start, "OnStart");
        if (bad != null) { Console.WriteLine("  FAIL: " + bad); return 1; }

        const string tick = "OCGRAPH 1\nNODE tick OnTick\nNODE st InputActionState action=Charge\nNODE b Branch\nNODE p Print\n" +
                            "LINK tick.exec b.exec\nLINK st.triggered b.cond\nLINK b.true p.exec\nLINK st.elapsed p.value\nENTRY tick OnTick\n";
        bad = NodeRowKit.CompileAndRun(tick, "OnTick");
        if (bad != null) { Console.WriteLine("  FAIL: " + bad); return 1; }
        Console.WriteLine("  PASS");
        return 0;
    }

    // OnInputAction is an entry node: its ENTRY names the bus event, and AN_EventPayload reads x, y, held time.
    private static int TestOnInputActionEntryChain()
    {
        Console.WriteLine("Test: OnInputAction starts a chain by its Input.<action>.<event> ENTRY, with action= and event= kept");
        const string text = "OCGRAPH 1\nNODE e OnInputAction action=Charge event=Completed\nNODE p AN_EventPayload\nNODE pr Print\n" +
                            "LINK e.exec p.exec\nLINK p.then pr.exec\nLINK p.f pr.value\nENTRY e Input.Charge.Completed\n";
        if (!OcGraphParser.Parse(text, out var g, out var err))
        {
            Console.WriteLine($"  FAIL: parse: {err}");
            return 1;
        }
        var e = g.Nodes["e"];
        if (e.Attrs["action"] != "Charge" || e.Attrs["event"] != "Completed")
        {
            Console.WriteLine("  FAIL: action= or event= did not reach Node.Attrs");
            return 1;
        }
        if (e.Pins.Count != 1 || e.Pins[0].Name != "exec" || !e.Pins[0].IsOutput || e.Pins[0].Type != PinType.Exec)
        {
            Console.WriteLine("  FAIL: OnInputAction should be a bare exec output");
            return 1;
        }
        string? bad = NodeRowKit.CompileAndRun(text, "Input.Charge.Completed");
        if (bad != null) { Console.WriteLine("  FAIL: " + bad); return 1; }
        Console.WriteLine("  PASS");
        return 0;
    }
}
