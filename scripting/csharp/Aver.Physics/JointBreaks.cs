// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
//
// Break events for joints armed with Joint.SetBreak. Unlike Physics.Contacts, which clears at the start of
// every Step, the native queue PERSISTS until drained (a Step may run several fixed steps, and a break
// must not be lost to a caller who polls less often) -- so JointBreaks() reads it and ClearJointBreaks()
// empties it, or DrainJointBreaks() does both.

using System.Collections.Generic;

namespace Aver.Physics;

/// <summary>A joint that gave way. The joint is disabled (re-enable it to repair it); it may have been
/// removed since, in which case <see cref="Joint"/> is a dead handle.</summary>
public readonly struct JointBreak
{
    public Joint Joint { get; }
    /// <summary>The linear load that broke it, newtons.</summary>
    public float ForceN { get; }
    /// <summary>The angular load that broke it, newton-metres.</summary>
    public float TorqueNm { get; }

    internal JointBreak(int joint, float forceN, float torqueNm) { Joint = new Joint(joint); ForceN = forceN; TorqueNm = torqueNm; }
}

public static partial class Physics
{
    /// <summary>Joints that broke since the queue was last cleared, oldest first. Does not clear it.</summary>
    public static IReadOnlyList<JointBreak> JointBreaks()
    {
        int n = Native.aver_phys_joint_break_count();
        var list = new List<JointBreak>(n);
        for (int i = 0; i < n; i++)
            if (Native.aver_phys_joint_break_get(i, out int joint, out float force, out float torque) != 0)
                list.Add(new JointBreak(joint, force, torque));
        return list;
    }

    /// <summary>Empties the break queue.</summary>
    public static void ClearJointBreaks() => Native.aver_phys_joint_break_clear();

    /// <summary>Reads the break queue and clears it, so each break is seen exactly once.</summary>
    public static IReadOnlyList<JointBreak> DrainJointBreaks()
    {
        var list = JointBreaks();
        if (list.Count > 0) Native.aver_phys_joint_break_clear();
        return list;
    }
}
