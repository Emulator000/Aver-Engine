// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Script-facing water queries: the ocean, lakes and rivers a level authors.

using System.Runtime.InteropServices;
using Aver.Scene;

namespace Aver.Framework;

/// <summary>What kind of water covers a point. Mirrors <c>AVER_FW_WATER_KIND_*</c> in <c>framework_terrain_abi.h</c>.</summary>
public enum WaterKind
{
    /// <summary>No water here.</summary>
    None = 0,
    /// <summary>The level's WATER surface (waves included).</summary>
    Ocean = 1,
    /// <summary>A closed-outline lake.</summary>
    Lake = 2,
    /// <summary>A spline river; it has a current.</summary>
    River = 3,
}

/// <summary>The water at one point.</summary>
/// <param name="Over">True when water covers this XY (the point may be above or below the surface).</param>
/// <param name="Submerged">True when the sampled point is under the surface.</param>
/// <param name="SurfaceZ">World height of the surface in centimetres (0 when there is none).</param>
/// <param name="DepthCm">Surface minus the ground below, in centimetres.</param>
/// <param name="Current">Water velocity in cm/s (zero in still water).</param>
/// <param name="Kind">Which kind of water this is.</param>
public readonly record struct WaterSample(bool Over, bool Submerged, float SurfaceZ, float DepthCm, Vec3 Current, WaterKind Kind);

/// <summary>Where the water is. Waves are included for the ocean, so the surface moves with the rendered one. All calls are
/// main-thread and never throw: with no water provider they answer "not over water".</summary>
public static class Water
{
    /// <summary>Samples the water at a world position (centimetres).</summary>
    public static WaterSample Sample(Vec3 p)
    {
        Span<float> normal = stackalloc float[3];
        Span<float> current = stackalloc float[3];
        if (FwTerrain.aver_fw_water_sample(p.X, p.Y, p.Z, out float surfaceZ, ref MemoryMarshal.GetReference(normal),
                ref MemoryMarshal.GetReference(current), out float depth, out int kind) == 0)
            return default;
        return new WaterSample(true, p.Z < surfaceZ, surfaceZ, depth, new Vec3(current[0], current[1], current[2]), (WaterKind)kind);
    }

    /// <summary>Samples the water at an entity's world position.</summary>
    public static WaterSample Sample(Entity e) => Sample(e.WorldPosition);

    /// <summary>True when the point is under the water surface.</summary>
    public static bool IsInWater(Vec3 p) => Sample(p).Submerged;

    /// <summary>The water current at a point in cm/s (zero out of the water or in still water).</summary>
    public static Vec3 CurrentAt(Vec3 p) => Sample(p).Current;
}
