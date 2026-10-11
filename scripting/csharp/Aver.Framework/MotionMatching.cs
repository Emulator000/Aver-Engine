// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Motion matching from a script: the CMotionMatcher surface on Entity. Design: docs/ANIM_MOTION_MATCHING.md.
//
// No new P/Invoke. CMotionMatcher is registered at RUNTIME like CAnimGraph, so every call is a field read
// or write over the generic scene ABI and a host that never registered it gets false, not a throw. Radians
// in the component, degrees here (as CRootMotion yaw and AverCharacter.Yaw already differ).

using Aver.Scene;

namespace Aver.Framework;

/// <summary>Where the matched character faces. Pinned to kMotionFaceYaw in MotionMatchSystem.hpp.</summary>
public enum MotionFacing
{
    /// <summary>Toward its velocity when moving, otherwise it keeps its heading.</summary>
    Velocity = 0,
    /// <summary>Toward the yaw given to <see cref="Entity.SetMotionInput(Vec3, float)"/> (a strafing character).</summary>
    Yaw = 1,
}

/// <summary>Who moves the entity.</summary>
public enum MotionDrive
{
    /// <summary>Gameplay moves it (a physics character, a script); the matcher only picks poses that fit.</summary>
    Simulation = 0,
    /// <summary>The animation moves it: planar root motion is turned on (Apply) as a convenience.</summary>
    Animation = 1,
}

public readonly partial struct Entity
{
    private const int MotionTagSlots = 4;
    private const int MotionPausedBit = 1;
    private const int MotionForceSearchBit = 2;
    private const int MotionFaceYawBit = 4;
    private const float DegToRadMotion = MathF.PI / 180f;

    /// <summary>Runs motion matching on this entity with the database at <paramref name="database"/> (an
    /// .ocmotion asset path), adding the component if needed. An empty database leaves the choice to a
    /// motionmatch state of the entity's state machine. The entity needs a skeleton to be posed.
    ///
    /// <para><b>The database is written last</b>, and a component's storage arrives zero-filled (database 0
    /// = off), so the system never sees a half-written configuration.</para></summary>
    public bool SetMotionMatching(string database, MotionFacing facing = MotionFacing.Velocity,
                                  MotionDrive drive = MotionDrive.Simulation, float playRate = 1f)
    {
        if (!HasComponent("CMotionMatcher") && !AddComponent("CMotionMatcher")) return false;
        if (!HasComponent(Component.Animator)) AddComponent(Component.Animator);
        SetFloat("CMotionMatcher.playRate", playRate);
        int flags = GetInt("CMotionMatcher.flags") & ~MotionFaceYawBit;
        if (facing == MotionFacing.Yaw) flags |= MotionFaceYawBit;
        SetInt("CMotionMatcher.flags", flags);
        if (drive == MotionDrive.Animation) SetRootMotion(RootMotionMode.Apply, RootMotionAxes.Planar);
        return SetInt64("CMotionMatcher.database", string.IsNullOrEmpty(database) ? 0 : Assets.ObjectIdOf(database));
    }

    /// <summary>The world-space velocity (cm/s) the character is trying to reach and, for
    /// <see cref="MotionFacing.Yaw"/>, the facing in degrees (the AverCharacter.Yaw convention). Call every
    /// frame; the matcher builds its predicted trajectory from this and from the entity's measured motion.</summary>
    public bool SetMotionInput(Vec3 worldVelocity, float facingYawDegrees = 0f) =>
        SetMotionInput(worldVelocity.X, worldVelocity.Y, facingYawDegrees);

    /// <summary>The same with the planar velocity as two numbers.</summary>
    public bool SetMotionInput(float velocityX, float velocityY, float facingYawDegrees = 0f)
    {
        if (!HasComponent("CMotionMatcher")) return false;
        SetFloat("CMotionMatcher.desiredYaw", facingYawDegrees * DegToRadMotion);
        SetFloat("CMotionMatcher.desiredVelX", velocityX);
        return SetFloat("CMotionMatcher.desiredVelY", velocityY);
    }

    /// <summary>Tags the search must land on (<paramref name="required"/>) and must not
    /// (<paramref name="excluded"/>), by the names declared in the .ocmotion. At most four of each; null
    /// clears. False when over the slot count or the entity has no matcher.</summary>
    public bool SetMotionTags(string[]? required, string[]? excluded)
    {
        if (!HasComponent("CMotionMatcher")) return false;
        if ((required?.Length ?? 0) > MotionTagSlots || (excluded?.Length ?? 0) > MotionTagSlots) return false;
        for (int i = 0; i < MotionTagSlots; i++)
        {
            SetInt64("CMotionMatcher.requireTag" + i, required != null && i < required.Length ? Assets.ObjectIdOf(required[i]) : 0);
            SetInt64("CMotionMatcher.excludeTag" + i, excluded != null && i < excluded.Length ? Assets.ObjectIdOf(excluded[i]) : 0);
        }
        return true;
    }

    /// <summary>Asks for a search on the next tick, whatever the interval (the system clears the request).</summary>
    public bool RequestMotionSearch() =>
        HasComponent("CMotionMatcher") && SetInt("CMotionMatcher.flags", GetInt("CMotionMatcher.flags") | MotionForceSearchBit);

    /// <summary>Per-entity tuning; 0 keeps the database's own default. <paramref name="searchInterval"/> is
    /// seconds between searches, the half-lives (seconds) shape how fast the predicted velocity and heading
    /// approach the wanted ones.</summary>
    public bool SetMotionTuning(float searchInterval = 0f, float velocityHalfLife = 0f, float turnHalfLife = 0f)
    {
        if (!HasComponent("CMotionMatcher")) return false;
        SetFloat("CMotionMatcher.searchInterval", searchInterval);
        SetFloat("CMotionMatcher.velHalfLife", velocityHalfLife);
        return SetFloat("CMotionMatcher.turnHalfLife", turnHalfLife);
    }

    /// <summary>Freezes (or resumes) the matcher: no clock, no search; the pose holds.</summary>
    public bool PauseMotionMatching(bool paused)
    {
        if (!HasComponent("CMotionMatcher")) return false;
        int flags = GetInt("CMotionMatcher.flags");
        return SetInt("CMotionMatcher.flags", paused ? flags | MotionPausedBit : flags & ~MotionPausedBit);
    }

    /// <summary>What the matcher is playing: the id of the clip (hex of its asset hash, "" when nothing plays), the
    /// time in that clip, how well it fits the wanted trajectory (lower is better) and how many times it has
    /// switched. False when the entity has no matcher.</summary>
    public bool TryGetMotionState(out string? clipHashHex, out float clipTime, out float cost, out int switches)
    {
        if (!HasComponent("CMotionMatcher"))
        {
            clipHashHex = null;
            clipTime = cost = 0f;
            switches = 0;
            return false;
        }
        long clip = GetInt64("CMotionMatcher.clip");
        clipHashHex = clip == 0 ? "" : ((ulong)clip).ToString("X16");
        clipTime = GetFloat("CMotionMatcher.clipTime");
        cost = GetFloat("CMotionMatcher.cost");
        switches = GetInt("CMotionMatcher.switches");
        return true;
    }

    /// <summary>True while the matcher plays this clip (the path as written in the .ocmotion's clip list).</summary>
    public bool IsMotionClip(string clipPath) =>
        HasComponent("CMotionMatcher") && GetInt64("CMotionMatcher.clip") == Assets.ObjectIdOf(clipPath);

    /// <summary>Cost of the playing frame against the wanted trajectory at the last search.</summary>
    public float MotionCost => HasComponent("CMotionMatcher") ? GetFloat("CMotionMatcher.cost") : 0f;

    /// <summary>How many times the matcher has switched frames.</summary>
    public int MotionSwitches => HasComponent("CMotionMatcher") ? GetInt("CMotionMatcher.switches") : 0;
}

/// <summary>The graph-node surface for the motion-matching nodes: the same Entity calls with the int-entity /
/// primitive-only signatures GraphCompiler's emitted IL needs. Forwarded from the node table by the
/// integration step; see docs/ANIM_MOTION_MATCHING.md section 11.</summary>
internal static class MotionMatchGraphInterop
{
    internal static bool SetMotionMatchingForGraph(int entity, string database, int facing, int drive, float playRate) =>
        new Entity(entity).SetMotionMatching(database, (MotionFacing)facing, (MotionDrive)drive, playRate);

    internal static bool SetMotionInputForGraph(int entity, float velocityX, float velocityY, float facingYawDegrees) =>
        new Entity(entity).SetMotionInput(velocityX, velocityY, facingYawDegrees);

    /// <summary>Comma-separated tag names, empty = none.</summary>
    internal static bool SetMotionTagsForGraph(int entity, string required, string excluded) =>
        new Entity(entity).SetMotionTags(SplitTags(required), SplitTags(excluded));

    internal static bool RequestMotionSearchForGraph(int entity) =>
        new Entity(entity).RequestMotionSearch();

    internal static bool GetMotionStateForGraph(int entity, out float clipTime, out float cost, out int switches) =>
        new Entity(entity).TryGetMotionState(out _, out clipTime, out cost, out switches);

    internal static bool IsMotionClipForGraph(int entity, string clipPath) =>
        new Entity(entity).IsMotionClip(clipPath);

    private static string[]? SplitTags(string list) =>
        string.IsNullOrWhiteSpace(list)
            ? null
            : list.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
}
