// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// IK Rig and control-rig inputs from a script: attach an .ocikrig, drive its goals, weigh foot placement, set the
// named inputs of an OCRIG 2 rig. Design and asset formats: docs/ANIM_IK_RETARGET.md.
//
// RELAYED through the framework like montages are (aver_fw_ik_*): the framework links neither the animation module
// nor the scene's pose system, so a host that installed no provider answers false from every call here rather than
// throwing. Goals, rig inputs and foot smoothing are TRANSIENT (not saved): a script sets them again after a load.

using Aver.Scene;

namespace Aver.Framework;

/// <summary>The space an IK goal position is given in.</summary>
public enum IkSpace
{
    /// <summary>World space (the usual case: a target's position).</summary>
    World = 0,
    /// <summary>The entity's own model space (centimetres in the skeleton's frame).</summary>
    Model = 1,
}

public readonly partial struct Entity
{
    private static long IkHash(string? name) => string.IsNullOrEmpty(name) ? 0 : Assets.ObjectIdOf(name);

    /// <summary>Attaches an IK Rig (.ocikrig) to this entity, ensuring it has an animator.
    ///
    /// <para><b>weight is written every call</b>, for the reason <see cref="SetControlRig"/> spells out: the
    /// component arrives zero-filled and CIkRig's own `weight` default never runs, so an unwritten weight
    /// attaches a rig that does nothing.</para></summary>
    public bool SetIkRig(string rigAsset, float weight = 1f)
    {
        if (string.IsNullOrEmpty(rigAsset)) return false;
        if (!HasComponent(Component.Animator)) AddComponent(Component.Animator);
        if (!HasComponent("CIkRig") && !AddComponent("CIkRig")) return false;
        SetFloat("CIkRig.weight", weight);
        return SetInt64("CIkRig.asset", Assets.ObjectIdOf(rigAsset));
    }

    /// <summary>Turns parts of the attached IK Rig on or off: foot placement, script goals, the pelvis solve.</summary>
    public bool SetIkRigFlags(bool feet = true, bool goals = true, bool body = true) =>
        SetInt("CIkRig.flags", (feet ? 0 : 1) | (goals ? 0 : 2) | (body ? 0 : 4));

    /// <summary>Places a goal the rig declares (by name) at <paramref name="position"/>. The first call on a goal
    /// engages it over 0.15 s on its own unless <see cref="SetIkGoalWeight"/> already set a weight, so this one
    /// line is enough. False when the entity has no IK Rig or the rig declares no such goal.</summary>
    public bool SetIkGoal(string goal, Vec3 position, IkSpace space = IkSpace.World) =>
        !string.IsNullOrEmpty(goal) &&
        FwIk.aver_fw_ik_set_goal(Handle, IkHash(goal), (int)space, position.X, position.Y, position.Z, 0, 0f, 0f, 0f, 1f) != 0;

    /// <summary>As <see cref="SetIkGoal(string, Vec3, IkSpace)"/>, with the rotation the chain's tip should take
    /// (when the chain has tipRotation on).</summary>
    public bool SetIkGoal(string goal, Vec3 position, Quat rotation, IkSpace space = IkSpace.World) =>
        !string.IsNullOrEmpty(goal) &&
        FwIk.aver_fw_ik_set_goal(Handle, IkHash(goal), (int)space, position.X, position.Y, position.Z, 1,
                                 rotation.X, rotation.Y, rotation.Z, rotation.W) != 0;

    /// <summary>Makes the goal follow <paramref name="target"/>'s socket (null or empty = its origin) every tick,
    /// plus <paramref name="offset"/> in the socket's frame. If the target dies the goal lets go.</summary>
    public bool BindIkGoal(string goal, Entity target, string? socket = null, Vec3 offset = default) =>
        !string.IsNullOrEmpty(goal) && target.IsValid &&
        FwIk.aver_fw_ik_bind_goal(Handle, IkHash(goal), target.Handle, IkHash(socket), offset.X, offset.Y, offset.Z) != 0;

    /// <summary>Blends the goal toward <paramref name="weight"/> (1 engages, 0 releases) over
    /// <paramref name="blendSeconds"/>; negative uses 0.15 s, 0 cuts.</summary>
    public bool SetIkGoalWeight(string goal, float weight, float blendSeconds = 0.15f) =>
        !string.IsNullOrEmpty(goal) && FwIk.aver_fw_ik_set_goal_weight(Handle, IkHash(goal), weight, blendSeconds) != 0;

    /// <summary>Releases a goal (null or empty = every goal): its weight blends to 0.</summary>
    public bool ClearIkGoal(string? goal = null) => FwIk.aver_fw_ik_clear_goal(Handle, IkHash(goal)) != 0;

    /// <summary>The goal's blend weight, how far the chain's tip was from it after the last solve (cm), and
    /// whether the solve reached it. False when the entity has no IK Rig or the rig has no such goal.</summary>
    public bool TryGetIkGoalState(string goal, out float alpha, out float errorCm, out bool reached)
    {
        alpha = errorCm = 0f;
        reached = false;
        if (string.IsNullOrEmpty(goal)) return false;
        var v = new float[3];
        if (FwIk.aver_fw_ik_goal_state(Handle, IkHash(goal), v) == 0) return false;
        alpha = v[0]; errorCm = v[1]; reached = v[2] != 0f;
        return true;
    }

    /// <summary>Blends foot placement toward <paramref name="weight"/> (null or empty foot = every foot; a foot is
    /// named by its leg chain). Negative <paramref name="blendSeconds"/> uses 0.2 s.</summary>
    public bool SetFootIkWeight(float weight, float blendSeconds = 0.2f, string? foot = null) =>
        FwIk.aver_fw_ik_set_foot_weight(Handle, IkHash(foot), weight, blendSeconds) != 0;

    /// <summary>The foot's current vertical offset in cm, its plant weight (1 planted) and the ground normal it
    /// stands on. A null or empty <paramref name="foot"/> reads the pelvis (offset = how far it dropped).</summary>
    public bool TryGetFootIkState(string? foot, out float offsetCm, out float plant, out Vec3 groundNormal)
    {
        var v = new float[5];
        if (FwIk.aver_fw_ik_foot_state(Handle, IkHash(foot), v) == 0)
        {
            offsetCm = plant = 0f;
            groundNormal = Vec3.Up;
            return false;
        }
        offsetCm = v[0]; plant = v[1]; groundNormal = new Vec3(v[2], v[3], v[4]);
        return true;
    }

    /// <summary>Sets a named float input of this entity's OCRIG 2 control rig (drives an op's weight, say).</summary>
    public bool SetRigInput(string input, float value) =>
        !string.IsNullOrEmpty(input) && FwIk.aver_fw_rig_set_input_float(Handle, IkHash(input), value) != 0;

    /// <summary>Sets a named vec3 input: a goal, a pole, a look-at point. World-space inputs are converted to the
    /// skeleton's space by the rig.</summary>
    public bool SetRigInput(string input, Vec3 value) =>
        !string.IsNullOrEmpty(input) && FwIk.aver_fw_rig_set_input_vec3(Handle, IkHash(input), value.X, value.Y, value.Z) != 0;

    /// <summary>Sets a named quaternion input (a tip rotation).</summary>
    public bool SetRigInput(string input, Quat value) =>
        !string.IsNullOrEmpty(input) && FwIk.aver_fw_rig_set_input_quat(Handle, IkHash(input), value.X, value.Y, value.Z, value.W) != 0;

    /// <summary>Forgets an input so the rig's declared default applies again; null or empty clears every input.</summary>
    public bool ClearRigInput(string? input = null) => FwIk.aver_fw_rig_clear_input(Handle, IkHash(input)) != 0;
}

/// <summary>The graph-node surface for the IK nodes: the same Entity calls with the int-entity / primitive-only
/// signatures GraphCompiler's emitted IL needs. An empty string means "not given".</summary>
internal static class IkGraphInterop
{
    internal static bool SetIkRigForGraph(int entity, string rig, float weight) =>
        new Entity(entity).SetIkRig(rig, weight);

    /// <summary>Rotation is pitch / yaw / roll in degrees (the editor's order), used only when <paramref name="useRotation"/>.</summary>
    internal static bool SetIkGoalForGraph(int entity, string goal, float x, float y, float z, bool useRotation,
                                           float pitch, float yaw, float roll, int space)
    {
        var e = new Entity(entity);
        var sp = space == 1 ? IkSpace.Model : IkSpace.World;
        return useRotation
            ? e.SetIkGoal(goal, new Vec3(x, y, z), new Rot(yaw, pitch, roll).ToQuat(), sp)
            : e.SetIkGoal(goal, new Vec3(x, y, z), sp);
    }

    internal static bool BindIkGoalForGraph(int entity, string goal, int target, string socket, float ox, float oy, float oz) =>
        new Entity(entity).BindIkGoal(goal, new Entity(target), socket, new Vec3(ox, oy, oz));

    internal static bool SetIkGoalWeightForGraph(int entity, string goal, float weight, float blendSeconds) =>
        new Entity(entity).SetIkGoalWeight(goal, weight, blendSeconds);

    internal static bool ClearIkGoalForGraph(int entity, string goal) =>
        new Entity(entity).ClearIkGoal(goal);

    internal static bool GetIkGoalStateForGraph(int entity, string goal, out float alpha, out float errorCm, out bool reached) =>
        new Entity(entity).TryGetIkGoalState(goal, out alpha, out errorCm, out reached);

    internal static bool SetFootIkWeightForGraph(int entity, string foot, float weight, float blendSeconds) =>
        new Entity(entity).SetFootIkWeight(weight, blendSeconds, foot);

    internal static bool GetFootIkStateForGraph(int entity, string foot, out float offsetCm, out float plant,
                                                out float nx, out float ny, out float nz)
    {
        bool ok = new Entity(entity).TryGetFootIkState(foot, out offsetCm, out plant, out Vec3 n);
        nx = n.X; ny = n.Y; nz = n.Z;
        return ok;
    }

    internal static bool SetRigInputFloatForGraph(int entity, string input, float value) =>
        new Entity(entity).SetRigInput(input, value);

    internal static bool SetRigInputVec3ForGraph(int entity, string input, float x, float y, float z) =>
        new Entity(entity).SetRigInput(input, new Vec3(x, y, z));

    internal static bool ClearRigInputForGraph(int entity, string input) =>
        new Entity(entity).ClearRigInput(input);
}
