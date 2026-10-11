// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Input triggers (when an action fires) and modifiers (how its value is shaped), as typed factories
// over the one-token spec text the native layer parses.

using System.Globalization;
using System.Text;

namespace Aver.Framework;

/// <summary>Where an action is in its trigger cycle. Values are pinned to AVER_FW_TRIGGER_STATE_*
/// (framework_abi.h).</summary>
public enum TriggerState
{
    /// <summary>Not actuated, or the trigger's condition is not met.</summary>
    None = 0,
    /// <summary>Actuated and the trigger is still waiting (a hold timer running, a tap not yet released).</summary>
    Ongoing = 1,
    /// <summary>The trigger is firing.</summary>
    Triggered = 2,
}

/// <summary>What changed in an action's state during the last update, as a bit mask. Values are pinned
/// to AVER_FW_INPUT_EVENT_* (framework_abi.h).</summary>
[Flags]
public enum ActionEvent
{
    /// <summary>Nothing changed.</summary>
    None = 0,
    /// <summary>Left None this update.</summary>
    Started = 1,
    /// <summary>Is, or just became, Ongoing.</summary>
    Ongoing = 2,
    /// <summary>Is Triggered this update (every update while it stays Triggered).</summary>
    Triggered = 4,
    /// <summary>Went from Triggered to None.</summary>
    Completed = 8,
    /// <summary>Went from Ongoing to None without ever triggering.</summary>
    Canceled = 16,
}

/// <summary>How a <see cref="InputModifier.DeadZone"/> measures the input.</summary>
public enum DeadZoneType
{
    /// <summary>The whole vector's length.</summary>
    Radial,
    /// <summary>Each channel on its own.</summary>
    Axial,
}

/// <summary>Channel order of a <see cref="InputModifier.Swizzle"/>: output channel i reads the input
/// channel named by letter i.</summary>
public enum SwizzleOrder { XYZ, YXZ, ZYX, XZY, YZX, ZXY }

/// <summary>When an action fires. Immutable; carries the native spec text (see OcInput.cpp's header
/// comment for the grammar). The factories write the same canonical text the native formatter does, so
/// a spec built here compares equal to what the engine would print back; C# never parses a spec, the
/// native add call rejects bad text.</summary>
public readonly struct InputTrigger
{
    private const float DefaultActuation = 0.15f;
    private const float DefaultTapMax = 0.2f;

    /// <summary>The spec token, e.g. <c>hold:0.5:once</c>.</summary>
    public string Spec { get; }

    private InputTrigger(string spec) => Spec = spec;

    /// <summary>Wraps raw spec text. Not validated here.</summary>
    public static InputTrigger FromSpec(string spec) => new(spec ?? string.Empty);

    /// <summary>Fires every update the input is actuated. What an action without triggers does.</summary>
    public static InputTrigger Down(float actuation = DefaultActuation) => new(WithAct("down", actuation));

    /// <summary>Fires on the update the input becomes actuated.</summary>
    public static InputTrigger Pressed(float actuation = DefaultActuation) => new(WithAct("pressed", actuation));

    /// <summary>Ongoing while actuated; fires on the update it is let go.</summary>
    public static InputTrigger Released(float actuation = DefaultActuation) => new(WithAct("released", actuation));

    /// <summary>Ongoing until held for <paramref name="seconds"/>, then fires every update; with
    /// <paramref name="once"/> it fires for one update only and re-arms after a release.</summary>
    public static InputTrigger Hold(float seconds, bool once = false, float actuation = DefaultActuation)
    {
        string s = WithAct("hold:" + N(seconds), actuation);
        return new(once ? s + ":once" : s);
    }

    /// <summary>Fires on release, if the input had been held for at least <paramref name="seconds"/>.</summary>
    public static InputTrigger HoldAndRelease(float seconds, float actuation = DefaultActuation) =>
        new(WithAct("holdrelease:" + N(seconds), actuation));

    /// <summary>Fires on release, if the input was held no longer than <paramref name="maxSeconds"/>.</summary>
    public static InputTrigger Tap(float maxSeconds = DefaultTapMax, float actuation = DefaultActuation)
    {
        if (!Same(actuation, DefaultActuation)) return new("tap:" + N(maxSeconds) + ":" + N(actuation));
        return new(Same(maxSeconds, DefaultTapMax) ? "tap" : "tap:" + N(maxSeconds));
    }

    /// <summary>Fires every <paramref name="interval"/> seconds while held, at most
    /// <paramref name="limit"/> times when it is above zero; <paramref name="triggerOnStart"/> fires one
    /// on the press itself.</summary>
    public static InputTrigger Pulse(float interval, int limit = 0, bool triggerOnStart = true, float actuation = DefaultActuation)
    {
        var sb = new StringBuilder("pulse:").Append(N(interval));
        bool defAct = Same(actuation, DefaultActuation);
        if (limit != 0 || !defAct) sb.Append(':').Append(limit.ToString(CultureInfo.InvariantCulture));
        if (!defAct) sb.Append(':').Append(N(actuation));
        if (!triggerOnStart) sb.Append(":nostart");
        return new(sb.ToString());
    }

    /// <summary>Only fires while <paramref name="other"/> is also triggering (a modifier key).</summary>
    public static InputTrigger Chorded(InputAction other) => new("chord:" + other.Name);

    /// <summary>Fires once the <paramref name="steps"/> actions have triggered in order, each within
    /// <paramref name="window"/> seconds of the one before.</summary>
    public static InputTrigger Combo(float window, params InputAction[] steps)
    {
        var sb = new StringBuilder("combo:").Append(N(window)).Append(':');
        for (int i = 0; i < steps.Length; i++)
        {
            if (i > 0) sb.Append(',');
            sb.Append(steps[i].Name);
        }
        return new(sb.ToString());
    }

    /// <inheritdoc/>
    public override string ToString() => Spec;

    private static string WithAct(string s, float act) => Same(act, DefaultActuation) ? s : s + ":" + N(act);
    private static bool Same(float a, float b) => Math.Abs(a - b) <= 1e-6f;
    // %.6g of the float widened to double, as the native writer does.
    private static string N(float v) => ((double)v).ToString("G6", CultureInfo.InvariantCulture);
}

/// <summary>How an action's value is shaped. Immutable; carries the native spec text, built the same
/// canonical way as <see cref="InputTrigger"/>.</summary>
public readonly struct InputModifier
{
    /// <summary>The spec token, e.g. <c>deadzone:0.2:1:axial</c>.</summary>
    public string Spec { get; }

    private InputModifier(string spec) => Spec = spec;

    /// <summary>Wraps raw spec text. Not validated here.</summary>
    public static InputModifier FromSpec(string spec) => new(spec ?? string.Empty);

    /// <summary>Zeroes input below <paramref name="lower"/> and remaps lower..upper to 0..1. Radial
    /// measures the whole vector's length, so it only makes sense on an action (a binding carries one
    /// channel and so behaves axially).</summary>
    public static InputModifier DeadZone(float lower = 0.2f, float upper = 1f, DeadZoneType type = DeadZoneType.Radial)
    {
        string s = "deadzone";
        if (!Same(upper, 1f)) s += ":" + N(lower) + ":" + N(upper);
        else if (!Same(lower, 0.2f)) s += ":" + N(lower);
        if (type == DeadZoneType.Axial) s += ":axial";
        return new(s);
    }

    /// <summary>Multiplies every channel by <paramref name="uniform"/>.</summary>
    public static InputModifier Scale(float uniform) => new("scale:" + N(uniform));

    /// <summary>Multiplies each channel.</summary>
    public static InputModifier Scale(float x, float y, float z) => Triple("scale:", x, y, z);

    /// <summary>Negates the chosen channels. Choosing none is an identity.</summary>
    public static InputModifier Negate(bool x = true, bool y = true, bool z = true)
    {
        if (x && y && z) return new("negate");
        if (!x && !y && !z) return new("scale:1");
        return new("negate:" + (x ? "x" : "") + (y ? "y" : "") + (z ? "z" : ""));
    }

    /// <summary>Reorders the channels.</summary>
    public static InputModifier Swizzle(SwizzleOrder order) => new("swizzle:" + order.ToString().ToLowerInvariant());

    /// <summary>Exponential smoothing with the given time constant; 0 passes the value through.</summary>
    public static InputModifier Smooth(float timeConstantSeconds) => new("smooth:" + N(timeConstantSeconds));

    /// <summary>Raises each channel's magnitude to <paramref name="exponent"/>, keeping the sign.</summary>
    public static InputModifier ResponseCurve(float exponent) => new("curve:" + N(exponent));

    /// <summary>Per-channel response curve exponents.</summary>
    public static InputModifier ResponseCurve(float x, float y, float z) => Triple("curve:", x, y, z);

    /// <inheritdoc/>
    public override string ToString() => Spec;

    private static InputModifier Triple(string kind, float x, float y, float z) =>
        Same(x, y) && Same(y, z) ? new(kind + N(x)) : new(kind + N(x) + ":" + N(y) + ":" + N(z));

    private static bool Same(float a, float b) => Math.Abs(a - b) <= 1e-6f;
    private static string N(float v) => ((double)v).ToString("G6", CultureInfo.InvariantCulture);
}
