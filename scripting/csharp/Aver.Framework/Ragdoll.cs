// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Ragdolls on skinned entities: go limp, hold the animation with motors, blend back.
//
// RELAYED through the framework like animation curves are (aver_fw_ragdoll_*): the framework links
// neither the animation module nor physics, so a host that installed no ragdoll provider answers
// false / null from every call here rather than throwing.

using Aver.Scene;

namespace Aver.Framework;

/// <summary>How a ragdoll relates to the animation. Pinned to AVER_PHYS_RAGDOLL_*.</summary>
public enum RagdollMode
{
    /// <summary>The physics parts follow the animated pose; the skin shows the animation.</summary>
    Kinematic = 0,
    /// <summary>Fully simulated, motors off: the character goes limp.</summary>
    Limp = 1,
    /// <summary>Simulated, with motors driving each joint toward the animated pose (see
    /// <see cref="Entity.SetRagdollStrength"/>): a character that is hit and staggers but holds itself.</summary>
    Powered = 2,
}

public readonly partial struct Entity
{
    /// <summary>Turns this entity's skeleton into a physics ragdoll, built from its current animated
    /// pose, and blends the simulated pose over the animation across <paramref name="blendSeconds"/>.
    /// Calling it again on a live ragdoll changes its mode. False when the entity has no skeleton,
    /// no bone is large enough for a physics part, or the host has no ragdoll support.</summary>
    public bool StartRagdoll(RagdollMode mode = RagdollMode.Limp, float blendSeconds = 0.1f) =>
        Fw.aver_fw_ragdoll_start(Handle, (int)mode, blendSeconds) != 0;

    /// <summary>Blends back to the animation over <paramref name="blendSeconds"/>, then removes the
    /// ragdoll (<see cref="RagdollState"/> reads null from then on). 0 removes it at once.</summary>
    public bool StopRagdoll(float blendSeconds = 0.4f) =>
        Fw.aver_fw_ragdoll_stop(Handle, blendSeconds) != 0;

    /// <summary>The ragdoll's mode, or null when this entity has none (including after a
    /// <see cref="StopRagdoll"/> blend has finished).</summary>
    public RagdollMode? RagdollState
    {
        get
        {
            int m = Fw.aver_fw_ragdoll_mode(Handle);
            return m < 0 ? null : (RagdollMode)m;
        }
    }

    /// <summary>True while this entity has a ragdoll, in any mode.</summary>
    public bool HasRagdoll => Fw.aver_fw_ragdoll_mode(Handle) >= 0;

    /// <summary>How hard <see cref="RagdollMode.Powered"/> motors pull toward the animation, 0..1
    /// (default 0.5). False when there is no ragdoll.</summary>
    public bool SetRagdollStrength(float strength) =>
        Fw.aver_fw_ragdoll_set_strength(Handle, strength) != 0;

    /// <summary>Pushes the part of <paramref name="bone"/> with a world-space impulse in kg·cm/s,
    /// applied at the part's centre. A bone without a part of its own pushes its nearest ancestor's.
    /// Does nothing noticeable on a <see cref="RagdollMode.Kinematic"/> ragdoll.</summary>
    public bool AddRagdollImpulse(string bone, Vec3 impulse) =>
        !string.IsNullOrEmpty(bone) &&
        Fw.aver_fw_ragdoll_add_impulse(Handle, Assets.ObjectIdOf(bone), impulse.X, impulse.Y, impulse.Z) != 0;

    /// <summary>As <see cref="AddRagdollImpulse(string, Vec3)"/>, on the root part (the pelvis or
    /// whatever the skeleton's first part is): a shove of the whole body.</summary>
    public bool AddRagdollImpulse(Vec3 impulse) =>
        Fw.aver_fw_ragdoll_add_impulse(Handle, 0, impulse.X, impulse.Y, impulse.Z) != 0;
}
