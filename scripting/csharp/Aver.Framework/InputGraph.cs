// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Graph-facing input triggers: the methods the InputActionState / AddInputTrigger / AddInputModifier /
// ClearInputConfig nodes call. Actions are addressed by name (a NODE-line attribute), so a graph needs
// no handle pin.

namespace Aver.Framework;

/// <summary>Scalar-signature seam for the Enhanced Input graph nodes. Internal; Aver.Graph reaches it
/// through InternalsVisibleTo, the same way it reaches <c>BlackboardGraph</c>.</summary>
internal static class InputGraph
{
    // The registered action's handle, or 0 for an empty or unknown name.
    private static int Find(string action) =>
        string.IsNullOrEmpty(action) ? 0 : Fw.aver_fw_action_find(action);

    /// <summary>Everything the last gameplay update knows about <paramref name="action"/>. False, with
    /// zeros, for an unknown action.</summary>
    internal static bool ActionStateForGraph(string action, out int state, out bool started, out bool ongoing,
        out bool triggered, out bool completed, out bool canceled, out float elapsed, out float triggeredTime,
        out float x, out float y, out float z)
    {
        state = 0; started = ongoing = triggered = completed = canceled = false;
        elapsed = triggeredTime = x = y = z = 0f;
        int h = Find(action);
        if (h == 0) return false;

        state = FwIn.aver_fw_action_state(h);
        int ev = FwIn.aver_fw_action_events(h);
        started = (ev & (int)ActionEvent.Started) != 0;
        ongoing = (ev & (int)ActionEvent.Ongoing) != 0;
        triggered = (ev & (int)ActionEvent.Triggered) != 0;
        completed = (ev & (int)ActionEvent.Completed) != 0;
        canceled = (ev & (int)ActionEvent.Canceled) != 0;
        elapsed = FwIn.aver_fw_action_elapsed(h);
        triggeredTime = FwIn.aver_fw_action_triggered_time(h);
        var v = new float[3];
        FwIn.aver_fw_action_value3(h, v);
        x = v[0]; y = v[1]; z = v[2];
        return true;
    }

    /// <summary>Appends an action-level trigger spec. False for an unknown action or a spec the parser rejects.</summary>
    internal static bool AddTriggerForGraph(string action, string spec)
    {
        int h = Find(action);
        return h != 0 && !string.IsNullOrEmpty(spec) && FwIn.aver_fw_action_add_trigger(h, -1, spec) != 0;
    }

    /// <summary>Appends an action-level modifier spec. False for an unknown action or a spec the parser rejects.</summary>
    internal static bool AddModifierForGraph(string action, string spec)
    {
        int h = Find(action);
        return h != 0 && !string.IsNullOrEmpty(spec) && FwIn.aver_fw_action_add_modifier(h, -1, spec) != 0;
    }

    /// <summary>Drops an action's own triggers and modifiers; run it before the Add nodes in OnStart,
    /// since adds are not idempotent.</summary>
    internal static bool ClearConfigForGraph(string action)
    {
        int h = Find(action);
        if (h == 0) return false;
        FwIn.aver_fw_action_clear_config(h);
        return true;
    }
}
