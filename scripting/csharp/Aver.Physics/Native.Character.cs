// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
//
// P/Invokes for the character additions at the end of physics_character_abi.h: character-vs-character,
// the inner body, the can-stand query, contacts and internal edge removal. Names are copied from the
// header character for character, as in Native.cs (tests/abi/src/AbiEnumTest.cpp compares them).
using System.Runtime.InteropServices;

namespace Aver.Physics;

internal static partial class Native
{
    [DllImport(Lib)] internal static extern int aver_phys_set_character_vs_character(int on);
    [DllImport(Lib)] internal static extern int aver_phys_character_vs_character();
    [DllImport(Lib)] internal static extern int aver_phys_set_character_inner_body_default(int on);
    [DllImport(Lib)] internal static extern int aver_phys_character_set_inner_body(int ch, int on);
    [DllImport(Lib)] internal static extern int aver_phys_character_has_inner_body(int ch);
    [DllImport(Lib)] internal static extern int aver_phys_character_can_set_shape(int ch, float radius, float halfHeight);
    [DllImport(Lib)] internal static extern int aver_phys_character_contact_count(int ch);
    [DllImport(Lib)] internal static extern int aver_phys_character_contact_get(int ch, int index, int[]? outBody, float[]? outPoint3, float[]? outNormal3);
    [DllImport(Lib)] internal static extern int aver_phys_set_character_edge_removal_default(int on);
    [DllImport(Lib)] internal static extern int aver_phys_character_set_edge_removal(int ch, int on);
    [DllImport(Lib)] internal static extern int aver_phys_character_edge_removal(int ch);
}
