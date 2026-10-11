// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: the save payload (channel "gas") and the managed-state provider callbacks.

using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using Aver.Scripting;

namespace Aver.Framework;

public sealed partial class AbilitySystem
{
    private bool _restoring;

    private static string Num(float v) =>
        float.IsPositiveInfinity(v) ? "inf" : v.ToString("R", CultureInfo.InvariantCulture);

    // The save payload: Base values, loose tags, grants and active effects. Current values, handles, running
    // abilities and pending notifications are not saved; they are recomputed or start fresh.
    internal string SaveText()
    {
        var sb = new StringBuilder();
        sb.Append("GASSTATE 1\n");
        foreach (string set in _setNames) sb.Append("SET ").Append(set).Append('\n');
        foreach (AttrState a in _attrs)
            if (!a.Def.IsMeta) sb.Append("ATTR ").Append(a.Ref.Name).Append(' ').Append(Num(a.Base)).Append('\n');
        foreach (GameplayTag t in _loose) sb.Append("LOOSE ").Append(t.Name).Append(' ').Append(_loose.CountOf(t)).Append('\n');
        foreach (Granted g in _granted) sb.Append("GRANT ").Append(g.Def.Name).Append(' ').Append(g.Level).Append('\n');
        double now = Now;
        foreach (ActiveEffect e in _effects)
        {
            float remaining = double.IsPositiveInfinity(e.ExpiresAt) ? float.PositiveInfinity : (float)Math.Max(0.0, e.ExpiresAt - now);
            sb.Append("EFFECT ").Append(e.Def.Name).Append(" stacks=").Append(e.Stacks).Append(" remaining=").Append(Num(remaining));
            if (e.Def.Period > 0f) sb.Append(" period=").Append(Num((float)Math.Max(0.0, e.NextPeriod - now)));
            if (e.Duration != e.Def.Duration && e.Def.Policy == EffectPolicy.Duration) sb.Append(" dur=").Append(Num(e.Duration));
            if (e.Mags.Length > 0)
            {
                sb.Append(" mags=");
                for (int i = 0; i < e.Mags.Length; ++i) sb.Append(i > 0 ? "," : "").Append(Num(e.Mags[i]));
            }
            sb.Append(" source=").Append(e.SourceHandle == _owner ? "self" : "none").Append('\n');
        }
        return sb.ToString();
    }

    // Drops everything a restore replaces, without notifications.
    private void ClearForRestore()
    {
        _effects.Clear();
        _effectByHandle.Clear();
        _queue.Clear();
        _execCounts.Clear();
        _ongoingCount = 0;
        _attrs.Clear();
        _attrById.Clear();
        _setNames.Clear();
        _granted.Clear();
        _tags.ClearInternal();
        _loose.ClearInternal();
        _blocking.ClearInternal();
    }

    private static void SkewWarn(string what, string name) =>
        LogOnce($"saveskew:{what}:{name}", $"[GAS] save is from a build with different definitions: unknown {what} '{name}' skipped");

    private static bool TryFloat(string t, out float v)
    {
        if (t == "inf") { v = float.PositiveInfinity; return true; }
        return float.TryParse(t, NumberStyles.Float, CultureInfo.InvariantCulture, out v) && !float.IsNaN(v);
    }

    // Rebuilds this system from a payload that has already passed the header check. Raises no notifications:
    // it is a restore, not a change.
    internal void RestoreFrom(string[] lines)
    {
        bool authority = HasAuthority;
        HasAuthority = true;
        _restoring = true;
        GasBatch.Begin();
        _silent++;
        try
        {
            ClearForRestore();
            var attrLines = new List<string[]>();
            var effectLines = new List<string[]>();
            var sets = new List<string>();
            var loose = new List<string[]>();
            var grants = new List<string[]>();
            for (int i = 1; i < lines.Length; ++i)
            {
                string[] tok = lines[i].TrimEnd('\r').Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                if (tok.Length == 0 || tok[0].StartsWith('#')) continue;
                switch (tok[0])
                {
                    case "SET" when tok.Length >= 2: sets.Add(tok[1]); break;
                    case "ATTR" when tok.Length >= 3: attrLines.Add(tok); break;
                    case "LOOSE" when tok.Length >= 3: loose.Add(tok); break;
                    case "GRANT" when tok.Length >= 2: grants.Add(tok); break;
                    case "EFFECT" when tok.Length >= 2: effectLines.Add(tok); break;
                    default: LogOnce("saveline:" + tok[0], $"[GAS] save payload has an unrecognised line '{tok[0]}'; skipped"); break;
                }
            }

            foreach (string set in sets)
            {
                AttributeSetDef? def = GameplayDefs.FindAttributeSet(set);
                if (def == null) { SkewWarn("attribute set", set); continue; }
                AddAttributeSet(def);
            }
            foreach (string[] tok in attrLines)
            {
                if (!AttributeRef.TryFind(tok[1], out AttributeRef r) || !_attrById.TryGetValue(r.Id, out AttrState? st)) { SkewWarn("attribute", tok[1]); continue; }
                if (TryFloat(tok[2], out float v) && !st.Def.IsMeta) st.Base = v;
            }
            // Bounds read each other's Current, so settle every Current from Base before anything refreshes.
            foreach (AttrState a in _attrs) a.Current = a.Base;
            foreach (string[] tok in loose)
            {
                GameplayTag t = GameplayTag.Request(tok[1]);
                if (t.IsValid && int.TryParse(tok[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out int n) && n > 0) AddLooseTag(t, n);
            }
            foreach (string[] tok in grants)
            {
                AbilityDef? def = GameplayDefs.FindAbility(tok[1]);
                if (def == null) { SkewWarn("ability", tok[1]); continue; }
                int level = 1;
                if (tok.Length >= 3) int.TryParse(tok[2], NumberStyles.Integer, CultureInfo.InvariantCulture, out level);
                Grant(def, Math.Max(1, level));
            }
            foreach (string[] tok in effectLines) RestoreEffect(tok);

            for (int pass = 0; pass < 2; ++pass)
                foreach (AttrState a in _attrs.ToArray()) RefreshOne(a, default, 0);
        }
        finally
        {
            _silent--;
            _restoring = false;
            GasBatch.End();
            HasAuthority = authority;
        }
    }

    private void RestoreEffect(string[] tok)
    {
        EffectDef? def = GameplayDefs.FindEffect(tok[1]);
        if (def == null) { SkewWarn("effect", tok[1]); return; }
        if (def.Policy == EffectPolicy.Instant) return;
        if (_effects.Count >= MaxEffects) return;

        int stacks = 1;
        float remaining = def.Policy == EffectPolicy.Duration ? def.Duration : float.PositiveInfinity;
        float period = def.Period;
        float dur = def.Duration;
        bool self = false;
        float[]? mags = null;
        for (int i = 2; i < tok.Length; ++i)
        {
            int eq = tok[i].IndexOf('=');
            if (eq <= 0) continue;
            string key = tok[i].Substring(0, eq);
            string val = tok[i].Substring(eq + 1);
            switch (key)
            {
                case "stacks": int.TryParse(val, NumberStyles.Integer, CultureInfo.InvariantCulture, out stacks); break;
                case "remaining": if (TryFloat(val, out float r)) remaining = r; break;
                case "period": if (TryFloat(val, out float p)) period = p; break;
                case "dur": if (TryFloat(val, out float d)) dur = d; break;
                case "source": self = val == "self"; break;
                case "mags":
                {
                    string[] parts = val.Split(',', StringSplitOptions.RemoveEmptyEntries);
                    mags = new float[parts.Length];
                    for (int k = 0; k < parts.Length; ++k) TryFloat(parts[k], out mags[k]);
                    break;
                }
            }
        }

        int count = def.Modifiers.Count;
        var resolved = new float[count];
        for (int i = 0; i < count; ++i)
            resolved[i] = mags != null && i < mags.Length ? mags[i] : ResolveMagnitude(def.Modifiers[i], null, def, _owner);
        if (mags != null && mags.Length != count) SkewWarn("modifier count in effect", def.Name);

        double now = Now;
        int limit = def.Stack == StackScope.None ? 1 : Math.Max(1, def.StackLimit);
        var e = new ActiveEffect
        {
            Handle = _nextEffect++,
            Def = def,
            SourceHandle = self ? _owner : 0,
            Stacks = Math.Clamp(stacks, 1, limit),
            Start = now,
            Seq = ++_effectSeq,
            Mags = resolved,
            Duration = dur,
            HasOngoing = def.OngoingRequired.Count > 0 || def.OngoingBlocked.Count > 0,
        };
        if (def.Policy == EffectPolicy.Duration) e.ExpiresAt = now + Math.Max(0f, remaining);
        AddInstance(e);
        if (!double.IsPositiveInfinity(e.ExpiresAt)) Schedule(WorkKind.Expire, e.Handle, e.ExpiresAt);
        if (def.Period > 0f)
        {
            e.PeriodIndex = 1;
            e.NextPeriod = now + Math.Clamp(period, 0f, def.Period);
            e.Start = e.NextPeriod - def.Period;
            Schedule(WorkKind.Period, e.Handle, e.NextPeriod);
        }
    }

    // Ends running abilities without notifications; the system is about to be dropped.
    internal void Teardown()
    {
        if (_dead) return;
        _silent++;
        try
        {
            foreach (Granted g in _granted.ToArray())
                if (g.Active) EndCore(g, true);
        }
        finally { _silent--; }
    }
}

/// <summary>The "gas" state channel: the save payload text and the callbacks the framework's managed-state provider is built from.</summary>
internal static class GasSave
{
    internal const string Channel = "gas";
    private const string Header = "GASSTATE 1";

    /// <summary>The payload for a system.</summary>
    internal static string Write(AbilitySystem system) => system.SaveText();

    /// <summary>Restores the entity's system from a payload, creating the system if absent. False (and nothing changed) for a bad header or version.</summary>
    internal static bool Read(Entity entity, string text)
    {
        if (entity.Handle == 0 || text == null) return false;
        string[] lines = text.Split('\n');
        string first = lines.Length > 0 ? lines[0].Trim().TrimStart((char)0xFEFF) : "";
        if (first != Header)
        {
            Log.Warn($"[GAS] save payload for entity {entity.Handle} has an unsupported header '{first}'; ignored");
            return false;
        }
        AbilitySystem.GetOrAdd(entity).RestoreFrom(lines);
        return true;
    }

    // ---- provider callbacks (framework_state_abi.h) ---------------------------------------------

    internal static int StateCount(int entity, IntPtr user) => AbilitySystem.Of(new Entity(entity)) != null ? 1 : 0;

    internal static int StateAt(int entity, int index, IntPtr nameBuf, int nameCap, IntPtr dataBuf, int dataCap, IntPtr user)
    {
        try
        {
            AbilitySystem? s = AbilitySystem.Of(new Entity(entity));
            if (s == null || index != 0) return -1;
            byte[] payload = Encoding.UTF8.GetBytes(Write(s));
            if (nameBuf != IntPtr.Zero && nameCap > 0)
            {
                byte[] nm = Encoding.UTF8.GetBytes(Channel + "\0");
                int n = Math.Min(nm.Length, nameCap);
                Marshal.Copy(nm, 0, nameBuf, n);
                if (n == nameCap) Marshal.WriteByte(nameBuf, nameCap - 1, 0);
            }
            if (dataBuf != IntPtr.Zero && dataCap > 0)
                Marshal.Copy(payload, 0, dataBuf, Math.Min(payload.Length, dataCap));
            return payload.Length;
        }
        catch (Exception ex)
        {
            Log.Error($"[GAS] saving entity {entity} failed: {ex.GetType().Name}: {ex.Message}");
            return -1;
        }
    }

    internal static int StateSet(int entity, IntPtr name, IntPtr data, int dataLen, IntPtr user)
    {
        try
        {
            if (Marshal.PtrToStringUTF8(name) != Channel || dataLen < 0 || (data == IntPtr.Zero && dataLen > 0)) return 0;
            var bytes = new byte[dataLen];
            if (dataLen > 0) Marshal.Copy(data, bytes, 0, dataLen);
            return Read(new Entity(entity), Encoding.UTF8.GetString(bytes)) ? 1 : 0;
        }
        catch (Exception ex)
        {
            Log.Error($"[GAS] restoring entity {entity} failed: {ex.GetType().Name}: {ex.Message}");
            return 0;
        }
    }
}
