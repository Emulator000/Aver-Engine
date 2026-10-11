// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Root motion from a script: the CRootMotion surface on Entity. Design: docs/ANIM_MONTAGES.md.
//
// No new P/Invoke. CRootMotion is registered at RUNTIME like CAnimGraph, so every call is a field
// read or write over the generic scene ABI and a host that never registered it gets false, not a throw.

using Aver.Scene;

namespace Aver.Framework;

/// <summary>What the animation system does with the planar motion baked into a clip's root bone.
/// Pinned to kRootMotion* in AnimSystem.hpp.</summary>
public enum RootMotionMode
{
    /// <summary>Off: the clip plays as authored, root travel included.</summary>
    None = 0,
    /// <summary>The root is held at its rest value and the motion is thrown away.</summary>
    Ignore = 1,
    /// <summary>The root is held and the entity follows the motion (a physics character is moved by
    /// <see cref="AverCharacter"/>, anything else by the animation system itself).</summary>
    Apply = 2,
    /// <summary>The root is held and the motion collects in an accumulator the script reads with
    /// <see cref="Entity.ConsumeRootMotion"/>; the entity is not moved.</summary>
    Accumulate = 3,
}

/// <summary>Which root components are extracted. Pinned to kRootAxis*.</summary>
[Flags]
public enum RootMotionAxes
{
    X = 1,
    Y = 2,
    Z = 4,
    Yaw = 8,
    /// <summary>Ground travel and heading: X, Y and yaw.</summary>
    Planar = X | Y | Yaw,
}

public readonly partial struct Entity
{
    private const int RootSrcGraph = 1;
    private const int RootSrcMontage = 2;
    internal const int RootMotionExternalBit = 1;

    /// <summary>Turns root motion on, off, or changes how it is used, adding the component if needed.
    /// <paramref name="scale"/> multiplies translation. <paramref name="fromMontages"/> and
    /// <paramref name="fromGraph"/> choose which sources are extracted; with neither, root motion is off.
    ///
    /// <para><b>mode is written last</b>, and a component's storage arrives zero-filled (mode 0 = off), so
    /// a half-configured entity is never extracted with the wrong axes or scale.</para></summary>
    public bool SetRootMotion(RootMotionMode mode, RootMotionAxes axes = RootMotionAxes.Planar, float scale = 1f,
                              bool fromMontages = true, bool fromGraph = true)
    {
        if (!HasComponent("CRootMotion") && !AddComponent("CRootMotion")) return false;
        int sources = (fromGraph ? RootSrcGraph : 0) | (fromMontages ? RootSrcMontage : 0);
        if (sources == 0) mode = RootMotionMode.None;   // sources 0 would read as both
        SetFloat("CRootMotion.scale", scale);
        SetInt("CRootMotion.axes", (int)axes);
        SetInt("CRootMotion.sources", sources);
        return SetInt("CRootMotion.mode", (int)mode);
    }

    /// <summary>The root motion mode; <see cref="RootMotionMode.None"/> when the component is absent.</summary>
    public RootMotionMode RootMotion =>
        HasComponent("CRootMotion") ? (RootMotionMode)GetInt("CRootMotion.mode") : RootMotionMode.None;

    /// <summary>Reads the accumulator without clearing it: centimetres in this entity's own frame at the
    /// start of the period (X forward, Y right, Z up), radians of yaw, and the seconds it covers.</summary>
    public bool PeekRootMotion(out Vec3 translation, out float yawRadians, out float seconds)
    {
        if (!HasComponent("CRootMotion"))
        {
            translation = Vec3.Zero;
            yawRadians = seconds = 0f;
            return false;
        }
        translation = new Vec3(GetFloat("CRootMotion.accumX"), GetFloat("CRootMotion.accumY"), GetFloat("CRootMotion.accumZ"));
        yawRadians = GetFloat("CRootMotion.accumYaw");
        seconds = GetFloat("CRootMotion.accumSeconds");
        return true;
    }

    /// <summary>Reads the accumulator and zeroes it, so the next call covers only what happened since.</summary>
    public bool ConsumeRootMotion(out Vec3 translation, out float yawRadians, out float seconds)
    {
        if (!PeekRootMotion(out translation, out yawRadians, out seconds)) return false;
        SetFloat("CRootMotion.accumX", 0f);
        SetFloat("CRootMotion.accumY", 0f);
        SetFloat("CRootMotion.accumZ", 0f);
        SetFloat("CRootMotion.accumYaw", 0f);
        SetFloat("CRootMotion.accumSeconds", 0f);
        return true;
    }
}

/// <summary>The graph-node surface for the root motion nodes (int-entity / primitive-only signatures).</summary>
internal static class RootMotionGraphInterop
{
    internal static bool SetRootMotionForGraph(int entity, int mode, int axes, float scale) =>
        new Entity(entity).SetRootMotion((RootMotionMode)mode, (RootMotionAxes)axes, scale);

    internal static bool ConsumeRootMotionForGraph(int entity, out float x, out float y, out float z,
                                                   out float yawDegrees, out float seconds)
    {
        bool ok = new Entity(entity).ConsumeRootMotion(out Vec3 t, out float yaw, out seconds);
        x = t.X; y = t.Y; z = t.Z;
        yawDegrees = yaw * (180f / MathF.PI);
        return ok;
    }
}
