// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Level sequences from a script: play, pause, stop, scrub and rebind the sequences authored in the level's
// Sequencer. The tracks themselves (transform, camera, property, event, audio, animation, visibility,
// camera cuts, sub-sequences) are authored, not scripted; this is the transport. Design: docs/SEQUENCER.md.
//
// RELAYED through the framework like montages are (aver_fw_sequence_*): the framework does not link the
// runtime, so a host that installed no provider answers false from every call here rather than throwing.
// A sequence raises "<Name>_Started" and "<Name>_Finished" on the event bus, and every event key a
// sequence fires arrives under the name it was authored with (Events.Subscribe, an AN_OnEvent graph entry).

using Aver.Scene;

namespace Aver.Framework;

/// <summary>The level's sequences, addressed by name.</summary>
public static class LevelSequences
{
    private static long Hash(string? name) => string.IsNullOrEmpty(name) ? 0 : Assets.ObjectIdOf(name);

    /// <summary>Starts or resumes <paramref name="sequence"/>. A negative <paramref name="startSeconds"/>
    /// continues from where it is (a stopped or finished sequence starts at 0); a <paramref name="rate"/>
    /// of 0 keeps the current rate. False for a sequence the level does not have.</summary>
    public static bool Play(string sequence, float startSeconds = -1f, float rate = 0f) =>
        !string.IsNullOrEmpty(sequence) && FwSequence.aver_fw_sequence_play(Hash(sequence), startSeconds, rate) != 0;

    /// <summary>Pauses the sequence where it is; what it wrote stays.</summary>
    public static bool Pause(string sequence) =>
        !string.IsNullOrEmpty(sequence) && FwSequence.aver_fw_sequence_pause(Hash(sequence)) != 0;

    /// <summary>Stops the sequence (null or empty = every sequence): back to time 0, everything it wrote is
    /// put back as authored, its sounds stop.</summary>
    public static bool Stop(string? sequence = null) => FwSequence.aver_fw_sequence_stop(Hash(sequence)) != 0;

    /// <summary>Jumps to a time. Sampled tracks (transform, camera, properties, animation) show that time
    /// exactly; events and sounds do not fire for the skipped span.</summary>
    public static bool SetTime(string sequence, float seconds) =>
        !string.IsNullOrEmpty(sequence) && FwSequence.aver_fw_sequence_set_time(Hash(sequence), seconds) != 0;

    /// <summary>Playback speed multiplier, 0 or more (0 holds the clock).</summary>
    public static bool SetRate(string sequence, float rate) =>
        !string.IsNullOrEmpty(sequence) && FwSequence.aver_fw_sequence_set_rate(Hash(sequence), rate) != 0;

    /// <summary>True while the sequence (null or empty = any sequence) is playing.</summary>
    public static bool IsPlaying(string? sequence = null) => FwSequence.aver_fw_sequence_is_playing(Hash(sequence)) != 0;

    /// <summary>The sequence's clock, length and rate. False (outputs 0) for a sequence the level lacks.</summary>
    public static bool TryGetState(string sequence, out float time, out float length, out float rate, out bool playing)
    {
        time = length = rate = 0f;
        playing = false;
        if (string.IsNullOrEmpty(sequence)) return false;
        var v = new float[3];
        if (FwSequence.aver_fw_sequence_state(Hash(sequence), v, out int p) == 0) return false;
        time = v[0]; length = v[1]; rate = v[2];
        playing = p != 0;
        return true;
    }

    /// <summary>Makes <paramref name="actor"/> the actor that the sequence's tracks bound by the name
    /// <paramref name="bindingName"/> drive (spawnable actors, a character picked at run time).
    /// <see cref="Entity.None"/> clears the override. False when the sequence has no track bound by that name.</summary>
    public static bool Bind(string sequence, string bindingName, Entity actor) =>
        !string.IsNullOrEmpty(sequence) && !string.IsNullOrEmpty(bindingName) &&
        FwSequence.aver_fw_sequence_bind(Hash(sequence), Hash(bindingName), actor.Handle) != 0;
}

/// <summary>The graph-node surface for the sequencer nodes: the same calls with the int-entity /
/// primitive-only signatures GraphCompiler's emitted IL needs. An empty string means "not given".</summary>
internal static class SequenceGraphInterop
{
    internal static bool PlaySequenceForGraph(string sequence, float start, float rate) =>
        LevelSequences.Play(sequence, start, rate);

    internal static bool PauseSequenceForGraph(string sequence) => LevelSequences.Pause(sequence);

    internal static bool StopSequenceForGraph(string sequence) => LevelSequences.Stop(sequence);

    internal static bool SetSequenceTimeForGraph(string sequence, float seconds) => LevelSequences.SetTime(sequence, seconds);

    internal static bool SetSequenceRateForGraph(string sequence, float rate) => LevelSequences.SetRate(sequence, rate);

    internal static bool BindSequenceActorForGraph(string sequence, string bindingName, int entity) =>
        LevelSequences.Bind(sequence, bindingName, new Entity(entity));

    internal static bool IsSequencePlayingForGraph(string sequence) => LevelSequences.IsPlaying(sequence);

    internal static bool GetSequenceStateForGraph(string sequence, out float time, out float length, out float rate) =>
        LevelSequences.TryGetState(sequence, out time, out length, out rate, out _);
}
