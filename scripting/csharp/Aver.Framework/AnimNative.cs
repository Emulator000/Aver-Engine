// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// P/Invoke declarations for framework_anim_abi.h.

using System.Runtime.InteropServices;

namespace Aver.Framework;

/// <summary>The animation montage C ABI (framework_anim_abi.h). Declared here, not in a bridge assembly,
/// for the same reason as <see cref="Fw"/>: only this assembly has the DllImport resolver. Strings are
/// never passed, only fnv1a64 hashes (<c>Assets.ObjectIdOf</c>).</summary>
internal static class FwAnim
{
    private const string Lib = "Aver.Framework";

    [DllImport(Lib)] internal static extern int aver_fw_anim_abi_version();

    [DllImport(Lib)] internal static extern int aver_fw_montage_play(int entity, long montage, long slot, long section, float rate, float blendIn);
    [DllImport(Lib)] internal static extern int aver_fw_montage_stop(int entity, long montage, float blendOut);
    [DllImport(Lib)] internal static extern int aver_fw_montage_stop_slot(int entity, long slot, float blendOut);
    [DllImport(Lib)] internal static extern int aver_fw_montage_jump(int entity, long montage, long section);
    [DllImport(Lib)] internal static extern int aver_fw_montage_set_next_section(int entity, long montage, long fromSection, long toSection);
    [DllImport(Lib)] internal static extern int aver_fw_montage_set_rate(int entity, long montage, float rate);
    [DllImport(Lib)] internal static extern int aver_fw_montage_is_playing(int entity, long montage);
    [DllImport(Lib)] internal static extern int aver_fw_montage_state(int entity, long montage, float[] outPosLenWeightRate, out long outSectionHash);
}
