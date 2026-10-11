// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: the .ocgas text format (docs/formats/FORMAT_SPECS.md, section 11d).
// This file parses one file into plain records; GameplayDefs links the records of a whole set.

using System.Globalization;

namespace Aver.Framework;

internal sealed class AttrRec
{
    public string Name = "";
    public int Line;
    public float Base;
    public float Min = float.NegativeInfinity;
    public float Max = float.PositiveInfinity;
    public string? MinFrom;
    public string? MaxFrom;
    public bool Meta;
    public string? Forward;
    public float Scale = 1f;
}

internal sealed class SetRec
{
    public string Name = "";
    public int Line;
    public List<AttrRec> Attrs = new();
}

internal sealed class ModRec
{
    public int Line;
    public string Attr = "";
    public ModOp Op;
    public MagnitudeKind Kind;
    public float Value;
    public float Add;
    public string? CallerTag;
    public string? FromAttr;
    public bool FromSource;
}

internal sealed class EffectRec
{
    public string Name = "";
    public int Line;
    public EffectPolicy Policy;
    public float Duration;
    public float Period;
    public int PeriodLine;
    public bool ExecuteOnApply;
    public List<ModRec> Mods = new();
    public List<string> Asset = new(), Grants = new(), Requires = new(), BlockedBy = new(),
                        OngoingRequires = new(), OngoingBlockedBy = new(), Removes = new();
    public StackScope Stack;
    public int StackLimit = 1;
    public bool StackRefresh = true;
    public StackExpire StackExpire;
    public string? Cue;
}

internal sealed class AbilityRec
{
    public string Name = "";
    public int Line;
    public List<string> Tags = new(), Cancels = new(), Blocks = new(), Owns = new(), Requires = new(), BlockedBy = new(),
                        TriggerEvents = new();
    public string? Cost;
    public int CostLine;
    public string? Cooldown;
    public int CooldownLine;
    public AbilityCommit Commit;
    public float Duration;
    public bool TriggerGranted;
    public bool Retrigger;
    public string? Behaviour;
}

internal sealed class ActorRec
{
    public string Name = "";
    public int Line;
    public List<(string Name, int Line)> Sets = new();
    public List<(string Name, float Value, int Line)> Attrs = new();
    public List<(string Name, int Level, int Line)> Grants = new();
    public List<(string Name, int Line)> Applies = new();
    public List<string> Tags = new();
}

/// <summary>One parsed .ocgas file.</summary>
internal sealed class GasFileParse
{
    public string Source = "";
    public List<(string Name, int Line)> Tags = new();
    public List<SetRec> Sets = new();
    public List<EffectRec> Effects = new();
    public List<AbilityRec> Abilities = new();
    public List<ActorRec> Actors = new();
}

/// <summary>Reads the .ocgas text format. The first problem in a file refuses the whole file with a
/// <c>source:line: message</c> text.</summary>
internal static class OcGasParser
{
    internal const int MaxModifiers = 16;
    internal const int MaxTagsPerLine = 8;

    private sealed class ParseFailure : Exception
    {
        public ParseFailure(string message) : base(message) { }
    }

    private enum Block { None, AttrSet, Effect, Ability, Actor }

    private static readonly HashSet<string> s_known = new(StringComparer.Ordinal)
    {
        "TAG", "ATTRSET", "EFFECT", "ABILITY", "ACTORDEFAULTS", "ATTR", "POLICY", "PERIOD", "MOD", "ASSET", "GRANTS",
        "REQUIRES", "BLOCKEDBY", "ONGOING_REQUIRES", "ONGOING_BLOCKEDBY", "REMOVES", "STACK", "CUE", "TAGS", "CANCELS",
        "BLOCKS", "OWNS", "COST", "COOLDOWN", "COMMIT", "DURATION", "TRIGGER", "RETRIGGER", "BEHAVIOUR", "GRANT",
        "APPLY", "ADDTAG",
    };

    /// <summary>Parses <paramref name="text"/>. Returns null and sets <paramref name="error"/> on the first problem.</summary>
    internal static GasFileParse? Parse(string text, string source, out string error)
    {
        error = "";
        var file = new GasFileParse { Source = source };
        try
        {
            new Reader(source, file).Run(text);
            return file;
        }
        catch (ParseFailure ex)
        {
            error = ex.Message;
            return null;
        }
    }

    private sealed class Reader
    {
        private readonly string _source;
        private readonly GasFileParse _file;
        private int _line;
        private Block _block;
        private SetRec? _set;
        private EffectRec? _effect;
        private AbilityRec? _ability;
        private ActorRec? _actor;

        public Reader(string source, GasFileParse file)
        {
            _source = source;
            _file = file;
        }

        private ParseFailure Fail(string message) => new ParseFailure($"{_source}:{_line}: {message}");
        private ParseFailure FailAt(int line, string message) => new ParseFailure($"{_source}:{line}: {message}");

        public void Run(string text)
        {
            string[] lines = text.Split('\n');
            bool header = false;
            for (int i = 0; i < lines.Length; ++i)
            {
                _line = i + 1;
                string raw = lines[i].TrimEnd('\r');
                if (i == 0) raw = raw.TrimStart((char)0xFEFF);
                int hash = raw.IndexOf('#');
                string body = hash >= 0 ? raw.Substring(0, hash) : raw;
                string[] tok = body.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                if (tok.Length == 0) continue;
                bool indented = char.IsWhiteSpace(body[0]);
                string kw = tok[0].ToUpperInvariant();

                if (!header)
                {
                    if (kw != "OCGAS") throw Fail("missing 'OCGAS 1' header");
                    if (tok.Length != 2 || tok[1] != "1") throw Fail("unsupported OCGAS version (expected 1)");
                    header = true;
                    continue;
                }
                Dispatch(kw, tok, indented);
            }
            if (!header) throw new ParseFailure($"{_source}:1: missing 'OCGAS 1' header");
            CloseBlock();
        }

        private void Dispatch(string kw, string[] tok, bool indented)
        {
            switch (kw)
            {
                case "TAG":
                    CloseBlock();
                    if (tok.Length < 2) throw Fail("TAG needs a tag name");
                    for (int i = 1; i < tok.Length; ++i) _file.Tags.Add((Tag(tok[i]), _line));
                    break;
                case "ATTRSET" when !(_block == Block.Actor && indented):
                {
                    CloseBlock();
                    string name = OneName(tok, "ATTRSET");
                    if (_file.Sets.Exists(s => s.Name == name)) throw Fail($"duplicate attribute set '{name}'");
                    _set = new SetRec { Name = name, Line = _line };
                    _file.Sets.Add(_set);
                    _block = Block.AttrSet;
                    break;
                }
                case "EFFECT":
                {
                    CloseBlock();
                    string name = OneName(tok, "EFFECT");
                    if (_file.Effects.Exists(s => s.Name == name)) throw Fail($"duplicate effect '{name}'");
                    _effect = new EffectRec { Name = name, Line = _line };
                    _file.Effects.Add(_effect);
                    _block = Block.Effect;
                    break;
                }
                case "ABILITY":
                {
                    CloseBlock();
                    string name = OneName(tok, "ABILITY");
                    if (_file.Abilities.Exists(s => s.Name == name)) throw Fail($"duplicate ability '{name}'");
                    _ability = new AbilityRec { Name = name, Line = _line };
                    _file.Abilities.Add(_ability);
                    _block = Block.Ability;
                    break;
                }
                case "ACTORDEFAULTS":
                {
                    CloseBlock();
                    string name = OneName(tok, "ACTORDEFAULTS");
                    if (_file.Actors.Exists(s => s.Name == name)) throw Fail($"duplicate ACTORDEFAULTS for class '{name}'");
                    _actor = new ActorRec { Name = name, Line = _line };
                    _file.Actors.Add(_actor);
                    _block = Block.Actor;
                    break;
                }
                default:
                    SubRecord(kw, tok);
                    break;
            }
        }

        // Checks what a finished block must satisfy as a whole.
        private void CloseBlock()
        {
            if (_block == Block.Effect && _effect != null && _effect.Period > 0f && _effect.Policy == EffectPolicy.Instant)
                throw FailAt(_effect.PeriodLine, $"effect '{_effect.Name}': PERIOD needs POLICY duration or infinite");
            _block = Block.None;
        }

        private void SubRecord(string kw, string[] tok)
        {
            if (_block == Block.None)
            {
                if (!s_known.Contains(kw)) throw Fail($"unknown record '{tok[0]}'");
                throw Fail($"record '{tok[0]}' is outside any block");
            }
            bool handled = _block switch
            {
                Block.AttrSet => AttrSetRecord(kw, tok),
                Block.Effect => EffectRecord(kw, tok),
                Block.Ability => AbilityRecord(kw, tok),
                _ => ActorRecord(kw, tok),
            };
            if (handled) return;
            if (!s_known.Contains(kw)) throw Fail($"unknown record '{tok[0]}'");
            throw Fail($"record '{tok[0]}' is not valid in {BlockName()}");
        }

        private string BlockName() => _block switch
        {
            Block.AttrSet => "an ATTRSET block",
            Block.Effect => "an EFFECT block",
            Block.Ability => "an ABILITY block",
            _ => "an ACTORDEFAULTS block",
        };

        // ---- token helpers ------------------------------------------------------------------

        private string OneName(string[] tok, string kw)
        {
            if (tok.Length != 2) throw Fail($"{kw} needs exactly one name");
            return DefName(tok[1]);
        }

        private string DefName(string t)
        {
            if (!GameplayDefs.IsValidDefName(t)) throw Fail($"invalid name '{t}'");
            return t;
        }

        private string Tag(string t)
        {
            if (!TagRegistry.IsValidName(t)) throw Fail($"invalid gameplay tag '{t}'");
            return t;
        }

        private string AttrName(string t)
        {
            if (!AttributeRegistry.IsValidName(t)) throw Fail($"invalid attribute name '{t}'");
            return t;
        }

        private float Num(string t)
        {
            if (!float.TryParse(t, NumberStyles.Float, CultureInfo.InvariantCulture, out float v) || !float.IsFinite(v))
                throw Fail($"bad number '{t}'");
            return v;
        }

        private int Int(string t)
        {
            if (!int.TryParse(t, NumberStyles.Integer, CultureInfo.InvariantCulture, out int v)) throw Fail($"bad integer '{t}'");
            return v;
        }

        private void TagList(string[] tok, List<string> into)
        {
            if (tok.Length < 2) throw Fail($"{tok[0].ToUpperInvariant()} needs at least one tag");
            if (tok.Length - 1 > MaxTagsPerLine) throw Fail($"{tok[0].ToUpperInvariant()} lists more than {MaxTagsPerLine} tags");
            for (int i = 1; i < tok.Length; ++i) into.Add(Tag(tok[i]));
        }

        private static bool SplitKv(string t, out string key, out string value)
        {
            int eq = t.IndexOf('=');
            if (eq <= 0) { key = ""; value = ""; return false; }
            key = t.Substring(0, eq).ToLowerInvariant();
            value = t.Substring(eq + 1);
            return true;
        }

        // ---- ATTRSET and ACTORDEFAULTS attribute lines --------------------------------------

        private bool AttrSetRecord(string kw, string[] tok)
        {
            if (kw != "ATTR") return false;
            if (tok.Length < 3) throw Fail("ATTR needs a name and a base value");
            var a = new AttrRec { Name = AttrName(tok[1]), Line = _line, Base = Num(tok[2]) };
            if (_set!.Attrs.Exists(x => x.Name == a.Name)) throw Fail($"duplicate attribute '{a.Name}' in set '{_set.Name}'");
            for (int i = 3; i < tok.Length; ++i)
            {
                string t = tok[i];
                if (string.Equals(t, "meta", StringComparison.OrdinalIgnoreCase)) { a.Meta = true; continue; }
                if (!SplitKv(t, out string key, out string value)) throw Fail($"unexpected '{t}' on ATTR");
                switch (key)
                {
                    case "min":
                        if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float lo) && float.IsFinite(lo)) a.Min = lo;
                        else a.MinFrom = AttrName(value);
                        break;
                    case "max":
                        if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float hi) && float.IsFinite(hi)) a.Max = hi;
                        else a.MaxFrom = AttrName(value);
                        break;
                    case "forward": a.Forward = AttrName(value); break;
                    case "scale": a.Scale = Num(value); break;
                    default: throw Fail($"unknown ATTR option '{key}'");
                }
            }
            if (!a.Meta && (a.Forward != null)) throw Fail("forward= needs the meta flag");
            _set.Attrs.Add(a);
            return true;
        }

        private bool ActorRecord(string kw, string[] tok)
        {
            switch (kw)
            {
                case "ATTRSET":
                    _actor!.Sets.Add((OneName(tok, "ATTRSET"), _line));
                    return true;
                case "ATTR":
                    if (tok.Length != 3) throw Fail("ATTR needs a name and a base value");
                    _actor!.Attrs.Add((AttrName(tok[1]), Num(tok[2]), _line));
                    return true;
                case "GRANT":
                {
                    if (tok.Length < 2 || tok.Length > 3) throw Fail("GRANT needs an ability name and an optional level");
                    int level = tok.Length == 3 ? Int(tok[2]) : 1;
                    if (level < 1) throw Fail("GRANT level must be at least 1");
                    _actor!.Grants.Add((DefName(tok[1]), level, _line));
                    return true;
                }
                case "APPLY":
                    _actor!.Applies.Add((OneName(tok, "APPLY"), _line));
                    return true;
                case "ADDTAG":
                    TagList(tok, _actor!.Tags);
                    return true;
                default:
                    return false;
            }
        }

        // ---- EFFECT --------------------------------------------------------------------------

        private bool EffectRecord(string kw, string[] tok)
        {
            EffectRec e = _effect!;
            switch (kw)
            {
                case "POLICY":
                    if (tok.Length < 2) throw Fail("POLICY needs instant, duration <seconds> or infinite");
                    switch (tok[1].ToLowerInvariant())
                    {
                        case "instant":
                            if (tok.Length != 2) throw Fail("POLICY instant takes no value");
                            e.Policy = EffectPolicy.Instant;
                            break;
                        case "infinite":
                            if (tok.Length != 2) throw Fail("POLICY infinite takes no value");
                            e.Policy = EffectPolicy.Infinite;
                            break;
                        case "duration":
                        {
                            if (tok.Length != 3) throw Fail("POLICY duration needs a number of seconds");
                            float d = Num(tok[2]);
                            if (d < 0f) throw Fail("duration cannot be negative");
                            e.Policy = EffectPolicy.Duration;
                            e.Duration = d;
                            break;
                        }
                        default: throw Fail($"unknown POLICY '{tok[1]}'");
                    }
                    return true;
                case "PERIOD":
                {
                    if (tok.Length < 2 || tok.Length > 3) throw Fail("PERIOD needs seconds and an optional 'apply'");
                    float p = Num(tok[1]);
                    if (p <= 0f) throw Fail("PERIOD must be greater than 0");
                    if (tok.Length == 3)
                    {
                        if (!string.Equals(tok[2], "apply", StringComparison.OrdinalIgnoreCase)) throw Fail($"unexpected '{tok[2]}' on PERIOD");
                        e.ExecuteOnApply = true;
                    }
                    e.Period = p;
                    e.PeriodLine = _line;
                    return true;
                }
                case "MOD":
                    ModLine(e, tok);
                    return true;
                case "ASSET": TagList(tok, e.Asset); return true;
                case "GRANTS": TagList(tok, e.Grants); return true;
                case "REQUIRES": TagList(tok, e.Requires); return true;
                case "BLOCKEDBY": TagList(tok, e.BlockedBy); return true;
                case "ONGOING_REQUIRES": TagList(tok, e.OngoingRequires); return true;
                case "ONGOING_BLOCKEDBY": TagList(tok, e.OngoingBlockedBy); return true;
                case "REMOVES": TagList(tok, e.Removes); return true;
                case "CUE":
                    if (tok.Length != 2) throw Fail("CUE needs one tag");
                    e.Cue = Tag(tok[1]);
                    return true;
                case "STACK":
                    StackLine(e, tok);
                    return true;
                default:
                    return false;
            }
        }

        private void ModLine(EffectRec e, string[] tok)
        {
            if (e.Mods.Count >= MaxModifiers) throw Fail($"an effect may have at most {MaxModifiers} modifiers");
            if (tok.Length < 4) throw Fail("MOD needs an attribute, add|multiply|override and a magnitude");
            var m = new ModRec { Line = _line, Attr = AttrName(tok[1]) };
            switch (tok[2].ToLowerInvariant())
            {
                case "add": m.Op = ModOp.Add; break;
                case "multiply": m.Op = ModOp.Multiply; break;
                case "override": m.Op = ModOp.Override; break;
                default: throw Fail($"unknown modifier operation '{tok[2]}'");
            }
            if (tok[3].IndexOf('=') < 0)
            {
                if (tok.Length != 4) throw Fail($"unexpected '{tok[4]}' after a constant magnitude");
                m.Kind = MagnitudeKind.Constant;
                m.Value = Num(tok[3]);
            }
            else
            {
                bool hasCaller = false, hasFrom = false;
                float scale = 1f, add = 0f;
                for (int i = 3; i < tok.Length; ++i)
                {
                    if (!SplitKv(tok[i], out string key, out string value)) throw Fail($"unexpected '{tok[i]}' on MOD");
                    switch (key)
                    {
                        case "caller": m.CallerTag = Tag(value); hasCaller = true; break;
                        case "from":
                        {
                            int dot = value.IndexOf('.');
                            if (dot <= 0) throw Fail("from= must be source.<Attribute> or target.<Attribute>");
                            string who = value.Substring(0, dot).ToLowerInvariant();
                            if (who != "source" && who != "target") throw Fail("from= must be source.<Attribute> or target.<Attribute>");
                            m.FromSource = who == "source";
                            m.FromAttr = AttrName(value.Substring(dot + 1));
                            hasFrom = true;
                            break;
                        }
                        case "scale": scale = Num(value); break;
                        case "add": add = Num(value); break;
                        default: throw Fail($"unknown MOD option '{key}'");
                    }
                }
                if (hasCaller == hasFrom) throw Fail("MOD needs exactly one of caller= or from=");
                m.Kind = hasCaller ? MagnitudeKind.SetByCaller : MagnitudeKind.AttributeBased;
                m.Value = scale;
                m.Add = hasFrom ? add : 0f;
                if (hasCaller && add != 0f) throw Fail("add= belongs to from= magnitudes");
            }
            e.Mods.Add(m);
        }

        private void StackLine(EffectRec e, string[] tok)
        {
            if (tok.Length < 2) throw Fail("STACK needs none, target <n> or source <n>");
            int i = 2;
            switch (tok[1].ToLowerInvariant())
            {
                case "none": e.Stack = StackScope.None; break;
                case "target":
                case "source":
                {
                    if (tok.Length < 3) throw Fail($"STACK {tok[1]} needs a limit");
                    int n = Int(tok[2]);
                    if (n < 1) throw Fail("stack limit must be at least 1");
                    e.Stack = string.Equals(tok[1], "target", StringComparison.OrdinalIgnoreCase) ? StackScope.Target : StackScope.Source;
                    e.StackLimit = n;
                    i = 3;
                    break;
                }
                default: throw Fail($"unknown STACK scope '{tok[1]}'");
            }
            for (; i < tok.Length; ++i)
            {
                switch (tok[i].ToLowerInvariant())
                {
                    case "refresh": e.StackRefresh = true; break;
                    case "keep": e.StackRefresh = false; break;
                    case "expire":
                        if (i + 1 >= tok.Length) throw Fail("STACK expire needs clear or one");
                        ++i;
                        switch (tok[i].ToLowerInvariant())
                        {
                            case "clear": e.StackExpire = StackExpire.ClearAll; break;
                            case "one": e.StackExpire = StackExpire.RemoveOne; break;
                            default: throw Fail($"unknown STACK expire mode '{tok[i]}'");
                        }
                        break;
                    default: throw Fail($"unexpected '{tok[i]}' on STACK");
                }
            }
        }

        // ---- ABILITY -------------------------------------------------------------------------

        private bool AbilityRecord(string kw, string[] tok)
        {
            AbilityRec a = _ability!;
            switch (kw)
            {
                case "TAGS": TagList(tok, a.Tags); return true;
                case "CANCELS": TagList(tok, a.Cancels); return true;
                case "BLOCKS": TagList(tok, a.Blocks); return true;
                case "OWNS": TagList(tok, a.Owns); return true;
                case "REQUIRES": TagList(tok, a.Requires); return true;
                case "BLOCKEDBY": TagList(tok, a.BlockedBy); return true;
                case "COST":
                    a.Cost = OneName(tok, "COST");
                    a.CostLine = _line;
                    return true;
                case "COOLDOWN":
                    a.Cooldown = OneName(tok, "COOLDOWN");
                    a.CooldownLine = _line;
                    return true;
                case "COMMIT":
                    if (tok.Length != 2) throw Fail("COMMIT needs activate or manual");
                    switch (tok[1].ToLowerInvariant())
                    {
                        case "activate": a.Commit = AbilityCommit.Activate; break;
                        case "manual": a.Commit = AbilityCommit.Manual; break;
                        default: throw Fail($"unknown COMMIT mode '{tok[1]}'");
                    }
                    return true;
                case "DURATION":
                {
                    if (tok.Length != 2) throw Fail("DURATION needs a number of seconds");
                    float d = Num(tok[1]);
                    if (d < 0f) throw Fail("DURATION cannot be negative");
                    a.Duration = d;
                    return true;
                }
                case "TRIGGER":
                    if (tok.Length == 2 && string.Equals(tok[1], "granted", StringComparison.OrdinalIgnoreCase))
                        a.TriggerGranted = true;
                    else if (tok.Length == 3 && string.Equals(tok[1], "event", StringComparison.OrdinalIgnoreCase))
                        a.TriggerEvents.Add(Tag(tok[2]));
                    else throw Fail("TRIGGER needs 'event <tag>' or 'granted'");
                    return true;
                case "RETRIGGER":
                    if (tok.Length != 2) throw Fail("RETRIGGER needs on or off");
                    switch (tok[1].ToLowerInvariant())
                    {
                        case "on": a.Retrigger = true; break;
                        case "off": a.Retrigger = false; break;
                        default: throw Fail($"RETRIGGER needs on or off, not '{tok[1]}'");
                    }
                    return true;
                case "BEHAVIOUR":
                    a.Behaviour = OneName(tok, "BEHAVIOUR");
                    return true;
                default:
                    return false;
            }
        }
    }
}
