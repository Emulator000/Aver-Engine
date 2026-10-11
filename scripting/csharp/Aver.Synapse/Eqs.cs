// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Environment queries (EQS): load an .oceqs, run it for a querier, read the ranked items. Native side:
// synapse_query_abi.h; semantics and the file format: docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md.
using System.Runtime.InteropServices;

namespace Aver.Synapse;

/// <summary>Where a query run is. Mirrors AVER_SYN_EQS_*.</summary>
public enum EqsStatus { Unknown = 0, Queued = 1, Running = 2, Finished = 3, Failed = 4, Cancelled = 5 }

/// <summary>One ranked result: a position (centimetres), the entity it belongs to if any, its score in
/// [-1, 1], and for smart-object slot items the object entity and slot index.</summary>
public readonly record struct EqsItem(float X, float Y, float Z, int Entity, float Score, int Object, int Slot);

/// <summary>What a query run needs to know. <see cref="UserTags"/> is a comma-separated tag list the
/// smart-object slots' requirements are checked against.</summary>
public struct EqsRequest
{
    public int Querier, Target;
    public float PointX, PointY, PointZ;
    public bool HasPoint;
    public string? UserTags;
    public int Priority;
}

/// <summary>A handle to one run. A cheap value; the result lives in the native system until <see cref="Release"/>
/// (or 30 seconds after it finished).</summary>
public readonly struct EqsRun
{
    public int Handle { get; }
    internal EqsRun(int handle) => Handle = handle;

    public EqsStatus Status => Handle == 0 ? EqsStatus.Unknown : (EqsStatus)Native.aver_syn_eqs_status(Handle);
    /// <summary>Items of a finished run, 0 otherwise.</summary>
    public int Count => Handle == 0 ? 0 : Native.aver_syn_eqs_count(Handle);
    public bool IsDone => Status is EqsStatus.Finished or EqsStatus.Failed or EqsStatus.Cancelled;

    /// <summary>Item <paramref name="index"/>, best first.</summary>
    public bool TryGetItem(int index, out EqsItem item)
    {
        item = default;
        if (Handle == 0) return false;
        var pos = new float[3];
        if (Native.aver_syn_eqs_item(Handle, index, pos, out int entity, out float score, out int obj, out int slot) == 0) return false;
        item = new EqsItem(pos[0], pos[1], pos[2], entity, score, obj, slot);
        return true;
    }

    /// <summary>The best item, or <c>default</c> when there is none.</summary>
    public EqsItem Best => TryGetItem(0, out EqsItem i) ? i : default;

    /// <summary>Every item, best first. Allocates.</summary>
    public IReadOnlyList<EqsItem> Items()
    {
        int n = Count;
        var list = new List<EqsItem>(n);
        for (int i = 0; i < n; i++)
            if (TryGetItem(i, out EqsItem it)) list.Add(it);
        return list;
    }

    /// <summary>The raw value test number <paramref name="test"/> (declaration order in the winning option)
    /// produced for item <paramref name="index"/>; false when that test did not run for it.</summary>
    public bool TryGetTestValue(int index, int test, out float value)
    {
        value = 0f;
        return Handle != 0 && Native.aver_syn_eqs_test_value(Handle, index, test, out value) != 0;
    }

    public (int Generated, int Passed, int Evaluations, bool Truncated) Stats
    {
        get
        {
            if (Handle == 0 || Native.aver_syn_eqs_stats(Handle, out int g, out int p, out int e, out int t) == 0) return default;
            return (g, p, e, t != 0);
        }
    }

    public bool Cancel() => Handle != 0 && Native.aver_syn_eqs_cancel(Handle) != 0;
    public void Release()
    {
        if (Handle != 0) Native.aver_syn_eqs_release(Handle);
    }
}

/// <summary>Loading and running .oceqs queries. Paths are content-relative with '/'.</summary>
public static class Eqs
{
    /// <summary>Loads (and caches) a query; its id, or 0 when the file does not load (see <see cref="LastMessage"/>).</summary>
    public static int Load(string contentPath) => Native.aver_syn_eqs_load(contentPath);
    /// <summary>Re-reads a saved query; runs already started keep their copy.</summary>
    public static bool Reload(string contentPath) => Native.aver_syn_eqs_reload(contentPath) != 0;
    /// <summary>The parse or run failure text of the last load / run, "" when none.</summary>
    public static string LastMessage => Marshal.PtrToStringUTF8(Native.aver_syn_eqs_last_message()) ?? "";

    /// <summary>Queues a run; it finishes over the next ticks (watch <see cref="EqsRun.Status"/> or listen for the
    /// EqsFinished / EqsFailed event on the querier). Handle 0 when refused.</summary>
    public static EqsRun Start(int queryId, in EqsRequest req)
        => new(Native.aver_syn_eqs_start(queryId, req.Querier, req.Target, req.PointX, req.PointY, req.PointZ, req.HasPoint ? 1 : 0, req.UserTags, req.Priority));

    /// <summary>Runs to completion before returning.</summary>
    public static EqsRun Run(int queryId, in EqsRequest req)
        => new(Native.aver_syn_eqs_run(queryId, req.Querier, req.Target, req.PointX, req.PointY, req.PointZ, req.HasPoint ? 1 : 0, req.UserTags));

    /// <summary>Load + Start; handle 0 when the query does not load.</summary>
    public static EqsRun Start(string contentPath, in EqsRequest req)
    {
        int id = Load(contentPath);
        return id == 0 ? default : Start(id, req);
    }

    /// <summary>Per-tick time slice for queued runs: <paramref name="maxMs"/> milliseconds and / or
    /// <paramref name="maxUnitsPerTick"/> evaluation units (0 turns a limit off). Defaults 1 ms, no unit cap.</summary>
    public static void SetBudget(float maxMs, int maxUnitsPerTick) => Native.aver_syn_eqs_set_budget(maxMs, maxUnitsPerTick);

    /// <summary>Runs queued or in progress.</summary>
    public static int Pending => Native.aver_syn_eqs_pending();
}
