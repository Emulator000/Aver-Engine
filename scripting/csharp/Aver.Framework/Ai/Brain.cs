// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// An entity's AI brain: a behaviour tree (.ocbt) or a state tree (.ocst). Assign one, send a state tree
// events, ask where it is. Native side: framework_brain_abi.h (a relay to the blackboard provider);
// design: docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md.

using System.Runtime.InteropServices;
using System.Text;

namespace Aver.Framework;

/// <summary>P/Invoke into the brain relay of Aver.Framework. Internal: scripts use <see cref="Brain"/>.</summary>
internal static class FwBrain
{
    private const string Lib = "Aver.Framework";

    [DllImport(Lib)] internal static extern int aver_fw_brain_abi_version();
    [DllImport(Lib)] internal static extern int aver_fw_brain_set_asset(int entity, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Lib)] internal static extern int aver_fw_brain_kind(int entity);
    [DllImport(Lib)] internal static extern int aver_fw_brain_status(int entity);
    [DllImport(Lib)] internal static extern int aver_fw_brain_send_event(int entity, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, int payloadInt, float payloadFloat);
    [DllImport(Lib)] internal static extern int aver_fw_brain_state(int entity, byte[] buf, int cap);
    [DllImport(Lib)] internal static extern int aver_fw_brain_in_state(int entity, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
    [DllImport(Lib)] internal static extern int aver_fw_brain_restart(int entity);
    [DllImport(Lib)] internal static extern int aver_fw_brain_stop(int entity);
}

/// <summary>What kind of brain an entity has. Mirrors AVER_FW_BRAIN_NONE / _BT / _STATETREE.</summary>
public enum BrainKind { None = 0, BehaviourTree = 1, StateTree = 2 }

/// <summary>Where the brain is. Mirrors AVER_FW_BRAIN_IDLE and friends.</summary>
public enum BrainStatus { Idle = 0, Running = 1, Succeeded = 2, Failed = 3 }

/// <summary>One entity's brain (reach it with <c>entity.Brain</c>). A cheap value over the entity handle; every
/// call answers false / None / empty when the entity is dead or no AI provider is installed.</summary>
public readonly struct Brain
{
    private readonly int _entity;

    internal Brain(int entity) => _entity = entity;

    public BrainKind Kind => (BrainKind)FwBrain.aver_fw_brain_kind(_entity);
    public BrainStatus Status => (BrainStatus)FwBrain.aver_fw_brain_status(_entity);

    /// <summary>Gives the entity the brain at a content-relative path ("AI/Guard.ocst" or "AI/Guard.ocbt"),
    /// attaching the AI component when it has none. False when the file does not load.</summary>
    public bool SetAsset(string contentPath) => !string.IsNullOrEmpty(contentPath) && FwBrain.aver_fw_brain_set_asset(_entity, contentPath) != 0;

    /// <summary>Sends a state tree an event; its transitions on <c>event:Name</c> see it on the next update, and
    /// the blackboard keys EventPayload / EventPayloadF carry the payload. False for any other brain.</summary>
    public bool SendEvent(string name, int payloadInt = 0, float payloadFloat = 0f)
        => !string.IsNullOrEmpty(name) && FwBrain.aver_fw_brain_send_event(_entity, name, payloadInt, payloadFloat) != 0;

    /// <summary>The active leaf state's dotted path ("Combat.Chase"), "" when none.</summary>
    public string ActiveState
    {
        get
        {
            var buf = new byte[256];
            if (FwBrain.aver_fw_brain_state(_entity, buf, buf.Length) == 0) return "";
            int n = Array.IndexOf(buf, (byte)0);
            return Encoding.UTF8.GetString(buf, 0, n < 0 ? buf.Length : n);
        }
    }

    /// <summary>Whether a state is anywhere in the active chain ("Combat" is while "Combat.Chase" runs).</summary>
    public bool IsInState(string path) => !string.IsNullOrEmpty(path) && FwBrain.aver_fw_brain_in_state(_entity, path) != 0;

    /// <summary>Exits every state (aborting running tasks) and starts again at the next update.</summary>
    public bool Restart() => FwBrain.aver_fw_brain_restart(_entity) != 0;

    /// <summary>Exits every state and stays stopped until <see cref="Restart"/>.</summary>
    public bool Stop() => FwBrain.aver_fw_brain_stop(_entity) != 0;
}

public readonly partial struct Entity
{
    /// <summary>This entity's AI brain (see <see cref="Aver.Framework.Brain"/>).</summary>
    public Brain Brain => new(Handle);
}

/// <summary>What the Aver Node brain nodes call (AN_BrainSet and friends). Reached by reflection from
/// Aver.Graph, like <c>BlackboardGraph</c>. A failure returns false / 0 and never throws.</summary>
internal static class BrainGraph
{
    internal static bool SetAssetForGraph(int entity, string asset)
        => !string.IsNullOrEmpty(asset) && FwBrain.aver_fw_brain_set_asset(entity, asset) != 0;

    internal static bool SendEventForGraph(int entity, int payloadI, float payloadF, string eventName)
        => !string.IsNullOrEmpty(eventName) && FwBrain.aver_fw_brain_send_event(entity, eventName, payloadI, payloadF) != 0;

    internal static bool IsInStateForGraph(int entity, string state)
        => !string.IsNullOrEmpty(state) && FwBrain.aver_fw_brain_in_state(entity, state) != 0;

    /// <summary>status is a <see cref="BrainStatus"/> value, kind a <see cref="BrainKind"/> value.</summary>
    internal static int StatusForGraph(int entity, out int kind)
    {
        kind = FwBrain.aver_fw_brain_kind(entity);
        return FwBrain.aver_fw_brain_status(entity);
    }

    internal static bool RestartForGraph(int entity) => FwBrain.aver_fw_brain_restart(entity) != 0;
    internal static bool StopForGraph(int entity) => FwBrain.aver_fw_brain_stop(entity) != 0;
}
