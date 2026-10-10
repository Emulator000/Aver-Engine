// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
//
// Per-body extras: locked axes, continuous collision, velocity caps, sleep permission, and turning an
// existing body into a sensor. See physics_body_ext_abi.h for the rules each one follows.

using System;

namespace Aver.Physics;

/// <summary>Degrees of freedom of a body that can be frozen, in ENGINE axes (X forward, Y right, Z up).
/// Mirrors AVER_PHYS_LOCK_* (physics_body_ext_abi.h). WORLD axes, not the body's own.</summary>
[Flags]
public enum LockAxes : uint
{
    None = 0,
    TranslationX = 0x01,
    TranslationY = 0x02,
    TranslationZ = 0x04,
    RotationX = 0x08,
    RotationY = 0x10,
    RotationZ = 0x20,
    AllTranslation = TranslationX | TranslationY | TranslationZ,
    AllRotation = RotationX | RotationY | RotationZ,
}

public readonly partial struct Body
{
    /// <summary>Which degrees of freedom are frozen. Setting keeps the body's mass; all six locked is
    /// refused (use a static body). Dynamic bodies only; reads <see cref="LockAxes.None"/> for anything
    /// else.</summary>
    public LockAxes LockedAxes
    {
        get => Native.aver_phys_body_locked_axes(Handle, out uint m) != 0 ? (LockAxes)m : LockAxes.None;
    }
    public bool SetLockedAxes(LockAxes axes) => Native.aver_phys_body_set_locked_axes(Handle, (uint)axes) != 0;

    /// <summary>Continuous collision: the body is swept along its motion so a fast small one cannot cross
    /// a thin wall between two fixed steps. Costs extra while it is fast; off by default.</summary>
    public bool Ccd
    {
        get => Native.aver_phys_body_ccd(Handle, out int on) != 0 && on != 0;
    }
    public bool SetCcd(bool on) => Native.aver_phys_body_set_ccd(Handle, on ? 1 : 0) != 0;

    /// <summary>The fastest this body may move, centimetres per second. 0 for a dead or static body.</summary>
    public float MaxLinearVelocity
    {
        get => Native.aver_phys_body_max_linear_velocity(Handle, out float v) != 0 ? v : 0f;
    }
    public bool SetMaxLinearVelocity(float cmPerSecond) => Native.aver_phys_body_set_max_linear_velocity(Handle, cmPerSecond) != 0;

    /// <summary>The fastest this body may spin, DEGREES per second (<see cref="AngularVelocity"/> itself is
    /// radians). 0 for a dead or static body.</summary>
    public float MaxAngularVelocity
    {
        get => Native.aver_phys_body_max_angular_velocity(Handle, out float v) != 0 ? v : 0f;
    }
    public bool SetMaxAngularVelocity(float degPerSecond) => Native.aver_phys_body_set_max_angular_velocity(Handle, degPerSecond) != 0;

    /// <summary>Whether the body may fall asleep at rest (default true). Turning it off wakes the body.</summary>
    public bool AllowSleeping
    {
        get => Native.aver_phys_body_allow_sleeping(Handle, out int on) != 0 && on != 0;
    }
    public bool SetAllowSleeping(bool on) => Native.aver_phys_body_set_allow_sleeping(Handle, on ? 1 : 0) != 0;

    /// <summary>True when the body is a sensor (trigger volume: detects overlap, pushes nothing).</summary>
    public bool IsSensor
    {
        get => Native.aver_phys_body_sensor(Handle, out int on) != 0 && on != 0;
    }
    /// <summary>Turns an existing body into a sensor, or back into a solid.</summary>
    public bool SetSensor(bool on) => Native.aver_phys_body_set_sensor(Handle, on ? 1 : 0) != 0;
}
