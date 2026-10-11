// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// P/Invoke declarations for framework_ik_abi.h.

using System.Runtime.InteropServices;

namespace Aver.Framework;

/// <summary>The IK / control-rig C ABI (framework_ik_abi.h). Declared here, not in a bridge assembly, for the same
/// reason as <see cref="Fw"/>: only this assembly has the DllImport resolver. Strings are never passed, only
/// fnv1a64 hashes (<c>Assets.ObjectIdOf</c>).</summary>
internal static class FwIk
{
    private const string Lib = "Aver.Framework";

    [DllImport(Lib)] internal static extern int aver_fw_ik_abi_version();

    [DllImport(Lib)] internal static extern int aver_fw_rig_set_input_float(int entity, long name, float value);
    [DllImport(Lib)] internal static extern int aver_fw_rig_set_input_vec3(int entity, long name, float x, float y, float z);
    [DllImport(Lib)] internal static extern int aver_fw_rig_set_input_quat(int entity, long name, float x, float y, float z, float w);
    [DllImport(Lib)] internal static extern int aver_fw_rig_clear_input(int entity, long name);

    [DllImport(Lib)] internal static extern int aver_fw_ik_set_goal(int entity, long goal, int space, float px, float py, float pz,
                                                                    int hasRotation, float qx, float qy, float qz, float qw);
    [DllImport(Lib)] internal static extern int aver_fw_ik_bind_goal(int entity, long goal, int targetEntity, long socket,
                                                                     float ox, float oy, float oz);
    [DllImport(Lib)] internal static extern int aver_fw_ik_set_goal_weight(int entity, long goal, float weight, float blendSeconds);
    [DllImport(Lib)] internal static extern int aver_fw_ik_clear_goal(int entity, long goal);
    [DllImport(Lib)] internal static extern int aver_fw_ik_goal_state(int entity, long goal, float[] outAlphaErrorReached);

    [DllImport(Lib)] internal static extern int aver_fw_ik_set_foot_weight(int entity, long foot, float weight, float blendSeconds);
    [DllImport(Lib)] internal static extern int aver_fw_ik_foot_state(int entity, long foot, float[] outOffsetPlantNormal);
}
