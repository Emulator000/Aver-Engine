// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// P/Invoke declarations for the Aver.Synapse.Abi C ABI (modules/synapse.abi/include/aver/synapse/
// synapse_ai_abi.h). Names are copied from the header character for character so the parity check
// can compare spelling.
using System.Runtime.InteropServices;

namespace Aver.Synapse;

internal static class Native
{
    private const string Lib = "Aver.Synapse.Abi";

    // ---- Reason channel --------------------------------------------------------------------------
    [DllImport(Lib)] internal static extern int    aver_syn_last_error();

    // ---- Crowd -----------------------------------------------------------------------------------
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_attach(int entity);
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_configure(int entity, float radiusCm, float maxSpeedCm, float maxAccelCm, float priority);
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_set_mode(int entity, int mode, float x, float y, float z);
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_set_enabled(int entity, int enabled);
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_velocity(int entity, out float outVx, out float outVy, out float outSpeed);
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_set_backend(int backend);
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_backend();
    [DllImport(Lib)] internal static extern IntPtr aver_syn_crowd_backend_name();
    [DllImport(Lib)] internal static extern void   aver_syn_crowd_set_gpu_backend(IntPtr icrowdBackend);
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_set_max_agents(int maxAgents);
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_max_agents();
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_agent_count();
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_overflow_count();
    [DllImport(Lib)] internal static extern int    aver_syn_crowd_set_drive(int drive);

    // ---- Hearing ---------------------------------------------------------------------------------
    [DllImport(Lib)] internal static extern int aver_syn_hearing_attach(int entity);
    [DllImport(Lib)] internal static extern int aver_syn_hearing_configure(int entity, float sensitivity, float maxRangeCm, float memorySec);
    [DllImport(Lib)] internal static extern int aver_syn_emit_noise(float x, float y, float z, float loudnessCm, int tag, int sourceEntity);
    [DllImport(Lib)] internal static extern int aver_syn_hearing_get(int entity, out float outX, out float outY, out float outZ, out float outLevel, out int outTag, out float outConfidence, out float outTimeSinceSec);
    [DllImport(Lib)] internal static extern int aver_syn_hearing_forget(int entity);

    // ---- Cover -----------------------------------------------------------------------------------
    [DllImport(Lib)] internal static extern int aver_syn_cover_add(float x, float y, float dirX, float dirY, int height, float arcHalfAngleDeg);
    [DllImport(Lib)] internal static extern int aver_syn_cover_remove(int coverId);
    [DllImport(Lib)] internal static extern int aver_syn_cover_marker_attach(int entity, int height, float arcHalfAngleDeg);
    [DllImport(Lib)] internal static extern int aver_syn_cover_set_auto_generate(int on, float minSpacingCm);
    [DllImport(Lib)] internal static extern int aver_syn_cover_count();
    [DllImport(Lib)] internal static extern int aver_syn_cover_find(int seeker, float threatX, float threatY, float threatZ, float maxSeekCm, float minThreatDistCm, int requireHigh, out float outX, out float outY);
    [DllImport(Lib)] internal static extern int aver_syn_cover_release(int seeker);
    [DllImport(Lib)] internal static extern int aver_syn_cover_is_covered(int seeker, float threatX, float threatY, float threatZ);

    // ---- Squads ----------------------------------------------------------------------------------
    [DllImport(Lib)] internal static extern int aver_syn_squad_attach(int entity, int squadId, float spacingCm);
    [DllImport(Lib)] internal static extern int aver_syn_squad_set_target(int squadId, float x, float y, float z);
    [DllImport(Lib)] internal static extern int aver_syn_squad_clear_target(int squadId);
    [DllImport(Lib)] internal static extern int aver_syn_squad_slot(int entity, out int outRole, out float outX, out float outY, out float outZ);
    [DllImport(Lib)] internal static extern int aver_syn_squad_spacing_push(int entity, out float outDx, out float outDy);
    // ---- Queries, smart objects, managed seam (synapse_query_abi.h) -----------------------------------
    [DllImport(Lib)] internal static extern int aver_syn_query_abi_version();
    [DllImport(Lib)] internal static extern int aver_syn_set_content_root([MarshalAs(UnmanagedType.LPUTF8Str)] string dir);
    [DllImport(Lib)] internal static extern int aver_syn_eqs_set_budget(float maxMs, int maxUnitsPerTick);

    [DllImport(Lib)] internal static extern unsafe int aver_syn_managed_set_dispatch(delegate* unmanaged[Cdecl]<ManagedCallNative*, nint, int> fn, nint user);
    [DllImport(Lib)] internal static extern int  aver_syn_managed_register(int kind, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
    [DllImport(Lib)] internal static extern void aver_syn_managed_clear();
    [DllImport(Lib)] internal static extern int  aver_syn_managed_installed();

    [DllImport(Lib)] internal static extern int    aver_syn_eqs_load([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_reload([MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Lib)] internal static extern IntPtr aver_syn_eqs_last_message();
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_start(int queryId, int querier, int target, float px, float py, float pz, int hasPoint, [MarshalAs(UnmanagedType.LPUTF8Str)] string? userTagsCsv, int priority);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_run(int queryId, int querier, int target, float px, float py, float pz, int hasPoint, [MarshalAs(UnmanagedType.LPUTF8Str)] string? userTagsCsv);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_status(int handle);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_count(int handle);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_item(int handle, int index, float[] outPos3, out int outEntity, out float outScore, out int outObject, out int outSlot);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_test_value(int handle, int index, int test, out float outValue);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_stats(int handle, out int generated, out int passed, out int evaluations, out int truncated);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_cancel(int handle);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_release(int handle);
    [DllImport(Lib)] internal static extern int    aver_syn_eqs_pending();

    [DllImport(Lib)] internal static extern int aver_syn_so_attach(int entity, [MarshalAs(UnmanagedType.LPUTF8Str)] string assetPath);
    [DllImport(Lib)] internal static extern int aver_syn_so_set_enabled(int entity, int enabled);
    [DllImport(Lib)] internal static extern int aver_syn_so_slot_count(int entity);
    [DllImport(Lib)] internal static extern int aver_syn_so_slot_info(int entity, int slot, float[] outPos3, out float outYaw, float[] outEntry3, out float outEntryYaw, out int outState, out int outUser);
    [DllImport(Lib)] internal static extern int aver_syn_so_set_slot_enabled(int entity, int slot, int enabled);
    [DllImport(Lib)] internal static extern int aver_syn_so_find(int user, float x, float y, float z, float radius, [MarshalAs(UnmanagedType.LPUTF8Str)] string? slotTagsCsv, [MarshalAs(UnmanagedType.LPUTF8Str)] string? userTagsCsv, int flags, int[] outObjects, int[] outSlots, int cap);
    [DllImport(Lib)] internal static extern int aver_syn_so_claim(int user, int @object, int slot, [MarshalAs(UnmanagedType.LPUTF8Str)] string? userTagsCsv, float ttlSec);
    [DllImport(Lib)] internal static extern int aver_syn_so_last_refusal();
    [DllImport(Lib)] internal static extern int aver_syn_so_use(int claim);
    [DllImport(Lib)] internal static extern int aver_syn_so_release(int claim, int reason);
    [DllImport(Lib)] internal static extern int aver_syn_so_claim_info(int claim, out int outObject, out int outSlot, out int outUser, out int outState, float[] outPos3, out float outYaw, float[] outEntry3, out float outEntryYaw);
    [DllImport(Lib)] internal static extern int aver_syn_so_claims_of(int user, int[] @out, int cap);
}

/// <summary>Layout twin of AverSynManagedCall (synapse_query_abi.h). The native side static_asserts its own twin.</summary>
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct ManagedCallNative
{
    public int StructBytes, Kind, NameId, Subject;
    public float Dt, Elapsed;
    public int First, Status;
    public fixed float P[4];
    public byte* Text;
    public fixed float Pos[3];
    public int ItemEntity;
    public fixed float Ctx[3];
    public int CtxEntity;
    public float Value;
}

/// <summary>Why a Synapse call failed, mirroring aver::AbiError in
/// modules/core/include/aver/core/ErrorCodes.hpp. Every value except <see cref="Ok"/> is negative,
/// so <c>(int)code &lt; 0</c> means "failed" even for a code added after this assembly was built.
/// </summary>
public enum SynapseError
{
    /// <summary>No error was recorded by the last call that records one.</summary>
    Ok = 0,
    /// <summary>The entity is dead or non-positive, or lacks the component the call needs.</summary>
    BadHandle = -1,
    /// <summary>A required out-parameter was null.</summary>
    NullPointer = -2,
    /// <summary>The component type was never registered (aver_syn_ai_register not called).</summary>
    NotInitialised = -3,
    /// <summary>A count past what exists, such as a full noise queue.</summary>
    OutOfRange = -4,
    /// <summary>A real request this build cannot serve.</summary>
    Unsupported = -5,
    /// <summary>A value that is not legal: an unknown mode, backend or drive, or a negative cap.</summary>
    InvalidArgument = -6,
    /// <summary>The request was legal and the memory was not there.</summary>
    AllocationFailed = -7,
}

/// <summary>Reads the reason the last Synapse call failed.</summary>
///
/// <remarks>Every call returns 1 for success and 0 for failure, so the reason travels on its own
/// entry point (a negative return would be truthy). A 0 with <see cref="LastError"/> equal to
/// <see cref="SynapseError.Ok"/> is a valid question with no answer yet, such as a crowd agent that is
/// not simulated or a listener that remembers nothing. THREAD-LOCAL, and set to Ok on success.</remarks>
public static class SynapseAi
{
    /// <summary>Why the last Synapse call on this thread failed, or <see cref="SynapseError.Ok"/>.</summary>
    public static SynapseError LastError => (SynapseError)Native.aver_syn_last_error();
}
