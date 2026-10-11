// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// LocFormat: plural categories and composite formatting.

using System.Globalization;
using Aver.Framework;

static class FormatTests
{
    public static void Run()
    {
        Plurals();
        Apply();
    }

    private static void Cat(string culture, long n, string expect)
        => Program.Check(LocFormat.PluralCategory(culture, n) == expect, $"{culture} {n} -> {expect} (got {LocFormat.PluralCategory(culture, n)})");

    private static void Plurals()
    {
        Cat("en", 0, "other"); Cat("en", 1, "one"); Cat("en", 2, "other"); Cat("en-GB", 1, "one"); Cat("de", 1, "one");
        Cat("fr", 0, "one"); Cat("fr", 1, "one"); Cat("fr", 2, "other"); Cat("pt-BR", 0, "one");
        Cat("ru", 1, "one"); Cat("ru", 2, "few"); Cat("ru", 5, "many"); Cat("ru", 11, "many"); Cat("ru", 12, "many");
        Cat("ru", 21, "one"); Cat("ru", 22, "few"); Cat("ru", 25, "many"); Cat("ru", 111, "many"); Cat("ru", 0, "many");
        Cat("uk", 3, "few"); Cat("be", 21, "one");
        Cat("pl", 1, "one"); Cat("pl", 2, "few"); Cat("pl", 5, "many"); Cat("pl", 12, "many"); Cat("pl", 22, "few"); Cat("pl", 21, "many");
        Cat("cs", 1, "one"); Cat("cs", 3, "few"); Cat("cs", 5, "other"); Cat("sk", 4, "few");
        Cat("ja", 1, "other"); Cat("zh-Hans", 1, "other"); Cat("ko", 5, "other");
        Cat("en", -1, "one");
    }

    private static void Apply()
    {
        var de = CultureInfo.GetCultureInfo("de");
        var en = CultureInfo.GetCultureInfo("en");
        Program.Check(LocFormat.Apply("{0:N1} kg", de, new object?[] { 1234.5 }) == "1.234,5 kg", "{0:N1} under de uses the comma decimal");
        Program.Check(LocFormat.Apply("{0:N1} kg", en, new object?[] { 1234.5 }) == "1,234.5 kg", "...and the dot under en");
        Program.Check(LocFormat.Apply("{0} of {1}", en, new object?[] { 3, 10 }) == "3 of 10", "positional arguments");
        Program.Check(LocFormat.Apply("plain {text}", en, new object?[] { 1 }) == "plain {text}", "a bad template does not throw and comes back unformatted");
        Program.Check(LocFormat.Apply("{0} {1}", en, new object?[] { 1 }) == "{0} {1}", "too few arguments does not throw");
        Program.Check(LocFormat.Apply("a {b", en, Array.Empty<object?>()) == "a {b", "no arguments leaves the template alone");
    }
}
