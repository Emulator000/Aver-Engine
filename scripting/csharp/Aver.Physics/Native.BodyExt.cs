// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
//
// P/Invokes for physics_body_ext_abi.h. Names copied from the header character for character, as in
// Native.cs (tests/abi/src/AbiEnumTest.cpp compares them).
using System.Runtime.InteropServices;

namespace Aver.Physics;

internal static partial class Native
{
    [DllImport(Lib)] internal static extern int aver_phys_body_set_locked_axes(int body, uint mask);
    [DllImport(Lib)] internal static extern int aver_phys_body_locked_axes(int body, out uint outMask);

    [DllImport(Lib)] internal static extern int aver_phys_body_set_ccd(int body, int on);
    [DllImport(Lib)] internal static extern int aver_phys_body_ccd(int body, out int outOn);

    [DllImport(Lib)] internal static extern int aver_phys_body_set_max_linear_velocity(int body, float cmPerSecond);
    [DllImport(Lib)] internal static extern int aver_phys_body_max_linear_velocity(int body, out float outCmPerSecond);
    [DllImport(Lib)] internal static extern int aver_phys_body_set_max_angular_velocity(int body, float degPerSecond);
    [DllImport(Lib)] internal static extern int aver_phys_body_max_angular_velocity(int body, out float outDegPerSecond);

    [DllImport(Lib)] internal static extern int aver_phys_body_set_allow_sleeping(int body, int on);
    [DllImport(Lib)] internal static extern int aver_phys_body_allow_sleeping(int body, out int outOn);

    [DllImport(Lib)] internal static extern int aver_phys_body_set_sensor(int body, int on);
    [DllImport(Lib)] internal static extern int aver_phys_body_sensor(int body, out int outOn);
}
