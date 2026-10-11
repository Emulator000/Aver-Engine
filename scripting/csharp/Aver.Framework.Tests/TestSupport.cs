// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Shared harness: assertions, a clean slate per test, and the definitions most tests start from.

using Aver.Framework;
using Aver.Scripting;

static class T
{
    public static readonly FakeGasHost Host = new();
    public static readonly List<(Log.Level Level, string Message)> Logs = new();
    private static int s_failures;

    // Tags and attribute names are process-global and never forgotten, so tests use names no other test uses
    // where the id matters; everything else (systems, definitions, the clock, the event log) starts clean.
    public const string CharDefs = "OCGAS 1\n" +
        "ATTRSET Character\n" +
        "  ATTR Health 100 min=0 max=MaxHealth\n" +
        "  ATTR MaxHealth 100 min=1\n" +
        "  ATTR Mana 50 min=0 max=MaxMana\n" +
        "  ATTR MaxMana 50 min=0\n" +
        "  ATTR Speed 10\n" +
        "  ATTR Damage 0 meta forward=Health scale=-1\n";

    public static Entity E(int n) => new Entity(n);
    public static GameplayTag Tag(string n) => GameplayTag.Request(n);
    public static AttributeRef Attr(string n) => AttributeRef.Of(n);

    public static void Reset()
    {
        Host.Reset();
        GameplayAbilities.Host = Host;
        GameplayAbilities.Replication = null;
        GameplayAbilities.ClientRequests = null;
        GasBatch.Reset();
        AbilitySystem.ClearAll();
        GameplayDefs.Clear();
        RecordingBehaviour.Trail.Clear();
        Logs.Clear();
        Log.SetSink((l, m) => Logs.Add((l, m)));
    }

    /// <summary>Loads the character set plus <paramref name="extra"/> (records without a header) as one file.</summary>
    public static void Load(string extra = "")
    {
        if (!GameplayDefs.LoadText(CharDefs + extra, "test.ocgas", out string err)) throw new Exception("definitions did not load: " + err);
    }

    /// <summary>A clean slate with the Character set loaded and added to entity <paramref name="entity"/>.</summary>
    public static AbilitySystem Hero(string extraDefs = "", int entity = 1)
    {
        Reset();
        Load(extraDefs);
        AbilitySystem s = AbilitySystem.GetOrAdd(E(entity));
        s.AddAttributeSet("Character");
        return s;
    }

    public static ActiveEffectHandle Apply(AbilitySystem s, string effect, int source = 0, float? duration = null)
    {
        EffectSpec spec = EffectSpec.Of(effect);
        if (source != 0) spec.From(E(source));
        if (duration.HasValue) spec.WithDuration(duration.Value);
        return s.Apply(spec);
    }

    public static void Advance(double seconds)
    {
        Host.Time += seconds;
        GameplayAbilities.Update();
    }

    public static void At(double time)
    {
        Host.Time = time;
        GameplayAbilities.Update();
    }

    public static int WarnCount(string fragment) => Logs.Count(l => l.Level == Log.Level.Warn && l.Message.Contains(fragment));

    public static bool Near(float a, float b, float eps = 1e-3f) => MathF.Abs(a - b) <= eps;

    public static void Check(string what, bool ok, string detail = "")
    {
        if (ok) { Console.WriteLine($"  PASS: {what}"); return; }
        Console.WriteLine($"  FAIL: {what}{(detail.Length > 0 ? " -- " + detail : "")}");
        s_failures++;
    }

    public static void CheckValue(string what, float expected, float actual) =>
        Check(what, Near(expected, actual), $"expected {expected}, got {actual}");

    public static void Run(string name, Action test)
    {
        Console.WriteLine($"Test: {name}");
        try { test(); }
        catch (Exception ex)
        {
            Console.WriteLine($"  FAIL: threw {ex.GetType().Name}: {ex.Message}\n{ex.StackTrace}");
            s_failures++;
        }
    }

    /// <summary>Runs a file's tests and returns how many checks failed in them.</summary>
    public static int Suite(Action tests)
    {
        int before = s_failures;
        tests();
        return s_failures - before;
    }
}
