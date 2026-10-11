// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// What the localization graph nodes (AN_SetCulture, AN_IsCulture, ...) call. The culture is a node attribute,
// pins carry ints and bools only. See docs/LOCALIZATION.md.

using System.Globalization;
using Aver.UI;

namespace Aver.Framework;

/// <summary>The localization graph nodes' entry points.</summary>
public static class LocGraph
{
    /// <summary>AN_SetCulture: switches (and remembers) the culture. False for a malformed tag.</summary>
    public static bool SetCultureForGraph(string culture) => Loc.SetCulture(culture);

    /// <summary>AN_IsCulture: the active tag equals <paramref name="culture"/>, or starts with it and a '-' ("de" matches "de-AT").</summary>
    public static bool IsCultureForGraph(string culture)
    {
        string want = (culture ?? string.Empty).Replace('_', '-').Trim();
        if (want.Length == 0) return false;
        string active = Loc.Culture;
        return active.Equals(want, StringComparison.OrdinalIgnoreCase) ||
               active.StartsWith(want + "-", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>AN_CultureCount: how many cultures the project ships.</summary>
    public static int CultureCountForGraph() => Loc.Cultures.Count;

    /// <summary>AN_CurrentCultureIndex: the active culture's place among the shipped ones, -1 if unknown.</summary>
    public static int CurrentCultureIndexForGraph()
    {
        IReadOnlyList<string> all = Loc.Cultures;
        for (string tag = Loc.Culture; tag.Length > 0;)
        {
            for (int i = 0; i < all.Count; i++) if (all[i].Equals(tag, StringComparison.OrdinalIgnoreCase)) return i;
            int dash = tag.LastIndexOf('-');
            if (dash < 0) break;
            tag = tag.Substring(0, dash);
        }
        return -1;
    }

    /// <summary>AN_SetCultureByIndex.</summary>
    public static bool SetCultureByIndexForGraph(int index)
    {
        IReadOnlyList<string> all = Loc.Cultures;
        return index >= 0 && index < all.Count && Loc.SetCulture(all[index]);
    }

    /// <summary>AN_SetUiItemsToCultures: a Choice or List shows the shipped cultures by their own names, the current one selected.</summary>
    public static bool SetUiItemsToCulturesForGraph(int widget)
    {
        try
        {
            var w = new Widget((uint)widget);
            if (!w.Exists) return false;
            w.SetItems(Loc.Cultures.Select(NativeName));
            w.Selected = Math.Max(0, CurrentCultureIndexForGraph());
            return true;
        }
        catch (DllNotFoundException) { return false; }
    }

    /// <summary>The culture's own name ("Deutsch"), the tag itself when the OS does not know it.</summary>
    internal static string NativeName(string tag)
    {
        try
        {
            CultureInfo ci = CultureInfo.GetCultureInfo(tag);
            string n = ci.NativeName;
            return n.Length == 0 ? tag : char.ToUpper(n[0], ci) + n.Substring(1);
        }
        catch (CultureNotFoundException) { return tag; }
    }
}
