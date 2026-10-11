// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The managed seam: behaviour-tree / state-tree leaves and EQS tests written in C#. Native keeps the names
// and calls ONE dispatch function (installed by Startup) for every call, the way timers do; a registered
// name then works in a .ocbt ("Action PatrolStep"), a .ocst ("TASK PatrolStep p0=3") or an .oceqs
// ("TEST custom name=Cover.Safe"). An exception in a leaf never reaches native code: it becomes Failure /
// false plus one logged line per name. Per-entity task state lives in the leaf author's own dictionary;
// AiTaskContext.First and .Aborting bracket a run.
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Aver.Synapse;

/// <summary>What an action leaf answers each tick.</summary>
public enum AiStatus { Running = 0, Success = 1, Failure = 2 }

/// <summary>Mirrors AVER_SYN_MANAGED_*.</summary>
public enum ManagedKind { Condition = 0, Action = 1, Abort = 2, EqsTest = 3 }

/// <summary>One call of an action or condition leaf. <see cref="Aborting"/> is true for the final call that
/// tells a running action it was cut short (a state exited, an observer aborted its branch); its status is ignored.</summary>
public struct AiTaskContext
{
    public int Entity;
    public float DeltaTime, Elapsed;
    public bool First, Aborting;
    public float P0, P1, P2, P3;
    public string Text;
}

/// <summary>One item a custom EQS test is asked about. Positions are centimetres.</summary>
public struct EqsTestContext
{
    public int Querier;
    public float X, Y, Z;
    public int ItemEntity;
    public float CtxX, CtxY, CtxZ;
    public int CtxEntity;
    public float P0, P1, P2, P3;
    public string Text;
}

public delegate AiStatus AiActionFn(in AiTaskContext c);
public delegate bool AiConditionFn(in AiTaskContext c);
/// <summary>The raw value; filtering and scoring (min / max / want / weight / curve) come from the asset.</summary>
public delegate float EqsTestFn(in EqsTestContext c);

[AttributeUsage(AttributeTargets.Method)]
public sealed class AverAiActionAttribute(string name) : Attribute { public string Name { get; } = name; }

[AttributeUsage(AttributeTargets.Method)]
public sealed class AverAiConditionAttribute(string name) : Attribute { public string Name { get; } = name; }

[AttributeUsage(AttributeTargets.Method)]
public sealed class AverEqsTestAttribute(string name) : Attribute { public string Name { get; } = name; }

/// <summary>One attributed method found by <see cref="ManagedAi.Collect"/>; <see cref="Error"/> is null when its
/// signature is right.</summary>
public readonly record struct ManagedLeaf(string Name, ManagedKind Kind, MethodInfo Method, string? Error);

public static class ManagedAi
{
    private sealed class Entry
    {
        public string Name = "";
        public ManagedKind Kind;
        public AiActionFn? Action;
        public AiConditionFn? Condition;
        public EqsTestFn? Test;
    }

    private static readonly Dictionary<int, Entry> s_entries = new();
    private static readonly HashSet<string> s_warned = new();
    private static bool s_started;

    /// <summary>Where a leaf's exception is reported (once per name). Defaults to stderr.</summary>
    public static Action<string> LogError { get; set; } = Console.Error.WriteLine;

    public static bool RegisterAction(string name, AiActionFn fn) => Register(ManagedKind.Action, name, e => e.Action = fn, fn != null);
    public static bool RegisterCondition(string name, AiConditionFn fn) => Register(ManagedKind.Condition, name, e => e.Condition = fn, fn != null);
    public static bool RegisterEqsTest(string name, EqsTestFn fn) => Register(ManagedKind.EqsTest, name, e => e.Test = fn, fn != null);

    private static bool Register(ManagedKind kind, string name, Action<Entry> bind, bool hasFn)
    {
        if (string.IsNullOrEmpty(name) || !hasFn) return false;
        Startup();   // idempotent; after a Shutdown (hot reload) the dispatch must come back before names do
        try
        {
            int id = Native.aver_syn_managed_register((int)kind, name);
            if (id <= 0) return false;
            var e = new Entry { Name = name, Kind = kind };
            bind(e);
            s_entries[id] = e;
            return true;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            LogError($"[ManagedAi] cannot register '{name}': the Synapse native library is not loaded");
            return false;
        }
    }

    /// <summary>Registers every public static method carrying <see cref="AverAiActionAttribute"/>,
    /// <see cref="AverAiConditionAttribute"/> or <see cref="AverEqsTestAttribute"/> with the matching signature.
    /// A bad signature is logged and skipped. Returns how many were registered.</summary>
    public static int ScanAssembly(Assembly asm)
    {
        int count = 0;
        foreach (ManagedLeaf leaf in Collect(asm))
        {
            if (leaf.Error != null)
            {
                LogError($"[ManagedAi] {leaf.Method.DeclaringType?.Name}.{leaf.Method.Name}: {leaf.Error}");
                continue;
            }
            bool ok = leaf.Kind switch
            {
                ManagedKind.Action => RegisterAction(leaf.Name, (AiActionFn)Delegate.CreateDelegate(typeof(AiActionFn), leaf.Method)),
                ManagedKind.Condition => RegisterCondition(leaf.Name, (AiConditionFn)Delegate.CreateDelegate(typeof(AiConditionFn), leaf.Method)),
                _ => RegisterEqsTest(leaf.Name, (EqsTestFn)Delegate.CreateDelegate(typeof(EqsTestFn), leaf.Method)),
            };
            if (ok) count++;
            else LogError($"[ManagedAi] '{leaf.Name}' was not registered (name taken, or it clashes with a native leaf)");
        }
        return count;
    }

    /// <summary>The attributed methods of an assembly and whether each signature is right. Makes no native calls.</summary>
    public static IReadOnlyList<ManagedLeaf> Collect(Assembly asm)
    {
        var list = new List<ManagedLeaf>();
        Type[] types;
        try { types = asm.GetTypes(); }
        catch (ReflectionTypeLoadException ex) { types = ex.Types.Where(t => t != null).ToArray()!; }
        foreach (Type t in types)
        {
            foreach (MethodInfo m in t.GetMethods(BindingFlags.Public | BindingFlags.Static | BindingFlags.DeclaredOnly))
            {
                if (m.GetCustomAttribute<AverAiActionAttribute>() is { } a)
                    list.Add(new ManagedLeaf(a.Name, ManagedKind.Action, m, Check(m, typeof(AiStatus), typeof(AiTaskContext))));
                if (m.GetCustomAttribute<AverAiConditionAttribute>() is { } c)
                    list.Add(new ManagedLeaf(c.Name, ManagedKind.Condition, m, Check(m, typeof(bool), typeof(AiTaskContext))));
                if (m.GetCustomAttribute<AverEqsTestAttribute>() is { } q)
                    list.Add(new ManagedLeaf(q.Name, ManagedKind.EqsTest, m, Check(m, typeof(float), typeof(EqsTestContext))));
            }
        }
        return list;
    }

    private static string? Check(MethodInfo m, Type ret, Type ctx)
    {
        ParameterInfo[] ps = m.GetParameters();
        if (m.ReturnType != ret) return $"must return {ret.Name}";
        if (ps.Length != 1 || ps[0].ParameterType != ctx.MakeByRefType()) return $"must take one parameter: in {ctx.Name}";
        return null;
    }

    /// <summary>Installs the dispatch function native code calls for every managed leaf and test.</summary>
    public static unsafe void Startup()
    {
        if (s_started) return;
        try
        {
            Native.aver_syn_managed_set_dispatch(&Dispatch, 0);
            s_started = true;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            LogError("[ManagedAi] the Synapse native library is not loaded: managed leaves are unavailable");
        }
    }

    /// <summary>Drops every name and the dispatch function. Call before the script assembly unloads: its
    /// delegates must not outlive it. Native leaves stay registered and answer Failure until re-registered.</summary>
    public static unsafe void Shutdown()
    {
        try
        {
            Native.aver_syn_managed_clear();
            Native.aver_syn_managed_set_dispatch(null, 0);
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { }
        s_entries.Clear();
        s_warned.Clear();
        s_started = false;
    }

    private static void ReportOnce(Entry e, Exception ex)
    {
        if (!s_warned.Add(e.Name)) return;
        try { LogError($"[ManagedAi] '{e.Name}' threw {ex.GetType().Name}: {ex.Message}"); } catch { }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static unsafe int Dispatch(ManagedCallNative* call, nint user)
    {
        Entry? e = null;
        try
        {
            if (call == null || call->StructBytes != sizeof(ManagedCallNative)) return 0;
            if (!s_entries.TryGetValue(call->NameId, out e)) return 0;
            string text = call->Text == null ? "" : Marshal.PtrToStringUTF8((nint)call->Text) ?? "";
            switch ((ManagedKind)call->Kind)
            {
                case ManagedKind.Condition when e.Condition != null:
                {
                    var c = TaskContext(call, text, aborting: false);
                    call->Status = e.Condition(in c) ? 1 : 0;
                    return 1;
                }
                case ManagedKind.Action when e.Action != null:
                {
                    var c = TaskContext(call, text, aborting: false);
                    call->Status = (int)e.Action(in c);
                    return 1;
                }
                case ManagedKind.Abort when e.Action != null:
                {
                    var c = TaskContext(call, text, aborting: true);
                    e.Action(in c);
                    return 1;
                }
                case ManagedKind.EqsTest when e.Test != null:
                {
                    var c = new EqsTestContext
                    {
                        Querier = call->Subject,
                        X = call->Pos[0], Y = call->Pos[1], Z = call->Pos[2],
                        ItemEntity = call->ItemEntity,
                        CtxX = call->Ctx[0], CtxY = call->Ctx[1], CtxZ = call->Ctx[2],
                        CtxEntity = call->CtxEntity,
                        P0 = call->P[0], P1 = call->P[1], P2 = call->P[2], P3 = call->P[3],
                        Text = text,
                    };
                    call->Value = e.Test(in c);
                    return 1;
                }
            }
            return 0;
        }
        catch (Exception ex)
        {
            if (e != null) ReportOnce(e, ex);
            return 0;
        }
    }

    private static unsafe AiTaskContext TaskContext(ManagedCallNative* call, string text, bool aborting) => new()
    {
        Entity = call->Subject,
        DeltaTime = call->Dt,
        Elapsed = call->Elapsed,
        First = call->First != 0,
        Aborting = aborting,
        P0 = call->P[0], P1 = call->P[1], P2 = call->P[2], P3 = call->P[3],
        Text = text,
    };
}
