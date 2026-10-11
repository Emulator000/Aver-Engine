// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// P/Invoke declarations for the input triggers and modifiers half of framework_abi.h (minor 11).

using System.Runtime.InteropServices;

namespace Aver.Framework;

/// <summary>The trigger/modifier input ABI. A separate class from <see cref="Fw"/> so Native.cs, which
/// other features also extend, stays untouched.</summary>
internal static class FwIn
{
    private const string Lib = "Aver.Framework";

    [DllImport(Lib)] internal static extern int aver_fw_action_bind_ex(int action, int source, int key, float scale, int component, int contextPriority);
    [DllImport(Lib)] internal static extern int aver_fw_action_add_trigger(int action, int binding, [MarshalAs(UnmanagedType.LPUTF8Str)] string spec);
    [DllImport(Lib)] internal static extern int aver_fw_action_add_modifier(int action, int binding, [MarshalAs(UnmanagedType.LPUTF8Str)] string spec);
    [DllImport(Lib)] internal static extern IntPtr aver_fw_action_config_error();
    [DllImport(Lib)] internal static extern void aver_fw_action_clear_config(int action);
    [DllImport(Lib)] internal static extern void aver_fw_action_update(float dt);
    [DllImport(Lib)] internal static extern void aver_fw_action_reset_state();
    [DllImport(Lib)] internal static extern int aver_fw_action_state(int action);
    [DllImport(Lib)] internal static extern int aver_fw_action_events(int action);
    [DllImport(Lib)] internal static extern float aver_fw_action_elapsed(int action);
    [DllImport(Lib)] internal static extern float aver_fw_action_triggered_time(int action);
    [DllImport(Lib)] internal static extern void aver_fw_action_value3(int action, float[] out3);
    [DllImport(Lib)] internal static extern int aver_fw_action_count();
    [DllImport(Lib)] internal static extern IntPtr aver_fw_action_name(int action);
    [DllImport(Lib)] internal static extern int aver_fw_action_value_type(int action);
    [DllImport(Lib)] internal static extern int aver_fw_action_event_count();
    [DllImport(Lib)] internal static extern int aver_fw_action_event_at(int index, out int action, out int mask);

    [DllImport(Lib)] internal static extern int aver_fw_input_scheme_trigger_count(int owner);
    [DllImport(Lib)] internal static extern IntPtr aver_fw_input_scheme_trigger_spec(int owner, int index);
    [DllImport(Lib)] internal static extern int aver_fw_input_scheme_modifier_count(int owner);
    [DllImport(Lib)] internal static extern IntPtr aver_fw_input_scheme_modifier_spec(int owner, int index);
}
