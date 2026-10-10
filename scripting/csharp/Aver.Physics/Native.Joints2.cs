// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
//
// P/Invokes for the springs and breakable joints added to physics_joints_abi.h. Names copied from the
// header character for character, as in Native.cs (tests/abi/src/AbiEnumTest.cpp compares them).
using System.Runtime.InteropServices;

namespace Aver.Physics;

internal static partial class Native
{
    [DllImport(Lib)] internal static extern int  aver_phys_joint_set_motor_spring(int joint, int axis, float frequencyHz, float damping);
    [DllImport(Lib)] internal static extern int  aver_phys_joint_set_limit_spring(int joint, float frequencyHz, float damping);

    [DllImport(Lib)] internal static extern int  aver_phys_joint_set_break(int joint, float maxForceN, float maxTorqueNm);
    [DllImport(Lib)] internal static extern int  aver_phys_joint_break_count();
    [DllImport(Lib)] internal static extern int  aver_phys_joint_break_get(int index, out int outJoint, out float outForceN, out float outTorqueNm);
    [DllImport(Lib)] internal static extern void aver_phys_joint_break_clear();
}
