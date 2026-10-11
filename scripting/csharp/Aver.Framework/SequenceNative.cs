// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// P/Invoke declarations for framework_sequence_abi.h.

using System.Runtime.InteropServices;

namespace Aver.Framework;

/// <summary>The level sequence C ABI (framework_sequence_abi.h). Declared here, not in a bridge assembly,
/// for the same reason as <see cref="Fw"/>: only this assembly has the DllImport resolver. Sequences and
/// bindings are named by fnv1a64 hash (<c>Assets.ObjectIdOf</c>), never by string.</summary>
internal static class FwSequence
{
    private const string Lib = "Aver.Framework";

    [DllImport(Lib)] internal static extern int aver_fw_sequence_abi_version();

    [DllImport(Lib)] internal static extern int aver_fw_sequence_play(long sequence, float startSeconds, float rate);
    [DllImport(Lib)] internal static extern int aver_fw_sequence_pause(long sequence);
    [DllImport(Lib)] internal static extern int aver_fw_sequence_stop(long sequence);
    [DllImport(Lib)] internal static extern int aver_fw_sequence_set_time(long sequence, float seconds);
    [DllImport(Lib)] internal static extern int aver_fw_sequence_set_rate(long sequence, float rate);
    [DllImport(Lib)] internal static extern int aver_fw_sequence_is_playing(long sequence);
    [DllImport(Lib)] internal static extern int aver_fw_sequence_state(long sequence, float[] outTimeLengthRate, out int outPlaying);
    [DllImport(Lib)] internal static extern int aver_fw_sequence_bind(long sequence, long bindingName, int entity);
}
