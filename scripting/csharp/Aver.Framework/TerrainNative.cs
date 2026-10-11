// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// P/Invoke declarations for framework_terrain_abi.h (terrain layer and water queries).

using System.Runtime.InteropServices;

namespace Aver.Framework;

/// <summary>The terrain and water query C ABI (framework_terrain_abi.h). Declared here for the same reason as
/// <see cref="FwState"/>: only this assembly has the DllImport resolver. Only the QUERIES are bound: the providers
/// are native (the landscape and water live in the game runtime), so no managed callback is ever installed.</summary>
internal static class FwTerrain
{
    private const string Lib = "Aver.Framework";

    [DllImport(Lib)] internal static extern int aver_fw_terrain_abi_version();

    [DllImport(Lib)] internal static extern int aver_fw_terrain_sample(float x, float y, out float outZ, ref float outNormal3,
        out int outDominant, ref float outWeights);

    [DllImport(Lib)] internal static extern int aver_fw_terrain_layer_count();

    [DllImport(Lib)] internal static extern int aver_fw_terrain_layer(int layer, ref byte nameBuf, int nameCap, ref byte surfaceBuf,
        int surfaceCap, out float outFriction, out float outRestitution);

    [DllImport(Lib)] internal static extern int aver_fw_water_sample(float x, float y, float z, out float outSurfaceZ,
        ref float outNormal3, ref float outCurrent3, out float outDepth, out int outKind);
}
