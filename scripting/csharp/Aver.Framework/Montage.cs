// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Animation montages from a script: play a .ocmontage on a slot of a skinned entity, jump between its
// sections, stop it, ask where it is. Design and asset format: docs/ANIM_MONTAGES.md.
//
// RELAYED through the framework like ragdolls are (aver_fw_montage_*): the framework links neither the
// animation module nor the scene's pose system, so a host that installed no provider answers false from
// every call here rather than throwing. Montage events and notifies arrive as graph events by name
// (OnEvent / CustomEvent), through the same notify sink as clip notifies.

using Aver.Scene;

namespace Aver.Framework;

public readonly partial struct Entity
{
    private static long MontageHash(string? name) => string.IsNullOrEmpty(name) ? 0 : Assets.ObjectIdOf(name);

    /// <summary>Plays <paramref name="montage"/> (a .ocmontage asset path) on this entity, adding an
    /// animator if there is none. The entity needs a skeleton (<see cref="SetSkeleton"/>). Playing a
    /// montage that is already running restarts it; a different montage in the same slot takes over and
    /// the previous one blends out.</summary>
    /// <param name="rate">Speed multiplier on the asset's own rate; 0 starts paused.</param>
    /// <param name="section">Section to start at; null or empty starts at the first.</param>
    /// <param name="slot">Slot to play in; null or empty uses the montage's authored slot.</param>
    /// <param name="blendIn">Blend-in seconds; negative uses the montage's.</param>
    public bool PlayMontage(string montage, float rate = 1f, string? section = null, string? slot = null,
                            float blendIn = -1f)
    {
        if (string.IsNullOrEmpty(montage)) return false;
        if (!HasComponent(Component.Animator)) AddComponent(Component.Animator);
        return FwAnim.aver_fw_montage_play(Handle, MontageHash(montage), MontageHash(slot), MontageHash(section),
                                           rate, blendIn) != 0;
    }

    /// <summary>Blends the montage out; null or empty stops every montage on this entity. A negative
    /// <paramref name="blendOut"/> uses the montage's own, 0 cuts. True when at least one was stopped.</summary>
    public bool StopMontage(string? montage = null, float blendOut = -1f) =>
        FwAnim.aver_fw_montage_stop(Handle, MontageHash(montage), blendOut) > 0;

    /// <summary>Blends out every montage playing in <paramref name="slot"/>.</summary>
    public bool StopMontageSlot(string slot, float blendOut = -1f) =>
        !string.IsNullOrEmpty(slot) && FwAnim.aver_fw_montage_stop_slot(Handle, MontageHash(slot), blendOut) > 0;

    /// <summary>Jumps a playing montage to a named section at once; its blend weight is kept.</summary>
    public bool JumpToMontageSection(string montage, string section) =>
        !string.IsNullOrEmpty(montage) && !string.IsNullOrEmpty(section) &&
        FwAnim.aver_fw_montage_jump(Handle, MontageHash(montage), MontageHash(section)) != 0;

    /// <summary>Overrides where <paramref name="fromSection"/> goes when it ends. A null or empty
    /// <paramref name="toSection"/> restores the authored link.</summary>
    public bool SetMontageNextSection(string montage, string fromSection, string? toSection) =>
        !string.IsNullOrEmpty(montage) && !string.IsNullOrEmpty(fromSection) &&
        FwAnim.aver_fw_montage_set_next_section(Handle, MontageHash(montage), MontageHash(fromSection), MontageHash(toSection)) != 0;

    /// <summary>Changes a playing montage's speed (instance rate); 0 pauses it.</summary>
    public bool SetMontageRate(string montage, float rate) =>
        !string.IsNullOrEmpty(montage) && FwAnim.aver_fw_montage_set_rate(Handle, MontageHash(montage), rate) != 0;

    /// <summary>True while the montage (null or empty = any montage) is playing or blending out.</summary>
    public bool IsMontagePlaying(string? montage = null) =>
        FwAnim.aver_fw_montage_is_playing(Handle, MontageHash(montage)) != 0;

    /// <summary>Where a playing montage is: position and length in montage seconds, blend weight 0..1 and
    /// instance rate. Null or empty reads the newest montage. False (outputs 0) when nothing plays.</summary>
    public bool TryGetMontageState(string? montage, out float position, out float length, out float weight,
                                   out float rate) =>
        ReadMontageState(montage, out position, out length, out weight, out rate, out _);

    /// <summary>True while the montage is inside the named section (compared by hash).</summary>
    public bool IsInMontageSection(string montage, string section)
    {
        if (string.IsNullOrEmpty(section)) return false;
        return ReadMontageState(montage, out _, out _, out _, out _, out long sec) && sec == MontageHash(section);
    }

    private bool ReadMontageState(string? montage, out float position, out float length, out float weight,
                                  out float rate, out long sectionHash)
    {
        var v = new float[4];
        if (FwAnim.aver_fw_montage_state(Handle, MontageHash(montage), v, out sectionHash) == 0)
        {
            position = length = weight = rate = 0f;
            sectionHash = 0;
            return false;
        }
        position = v[0]; length = v[1]; weight = v[2]; rate = v[3];
        return true;
    }
}

/// <summary>The graph-node surface for the montage nodes: the same Entity calls with the int-entity /
/// primitive-only signatures GraphCompiler's emitted IL needs. An empty string means "not given".</summary>
internal static class MontageGraphInterop
{
    internal static bool PlayMontageForGraph(int entity, string montage, string section, string slot, float rate, float blendIn) =>
        new Entity(entity).PlayMontage(montage, rate, section, slot, blendIn);

    internal static bool StopMontageForGraph(int entity, string montage, float blendOut) =>
        new Entity(entity).StopMontage(montage, blendOut);

    internal static bool StopMontageSlotForGraph(int entity, string slot, float blendOut) =>
        new Entity(entity).StopMontageSlot(slot, blendOut);

    internal static bool JumpToMontageSectionForGraph(int entity, string montage, string section) =>
        new Entity(entity).JumpToMontageSection(montage, section);

    internal static bool SetMontageNextSectionForGraph(int entity, string montage, string from, string to) =>
        new Entity(entity).SetMontageNextSection(montage, from, to);

    internal static bool SetMontageRateForGraph(int entity, string montage, float rate) =>
        new Entity(entity).SetMontageRate(montage, rate);

    internal static bool IsMontagePlayingForGraph(int entity, string montage) =>
        new Entity(entity).IsMontagePlaying(montage);

    internal static bool GetMontageStateForGraph(int entity, string montage, out float position, out float length, out float weight) =>
        new Entity(entity).TryGetMontageState(montage, out position, out length, out weight, out _);

    internal static bool IsInMontageSectionForGraph(int entity, string montage, string section) =>
        new Entity(entity).IsInMontageSection(montage, section);
}
