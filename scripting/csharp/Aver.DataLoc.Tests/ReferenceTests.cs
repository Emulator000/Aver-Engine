// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Text references through Loc / LocText without the native library: inline defaults, brackets, the @@ escape.

using Aver.Framework;

static class ReferenceTests
{
    public static void Run()
    {
        Program.Check(Loc.Resolve("plain") == "plain", "a literal is unchanged");
        Program.Check(Loc.Resolve("@@mention") == "@mention", "@@ unescapes");
        Program.Check(Loc.Resolve("@mention") == "@mention" && Loc.Resolve("@:x") == "@:x", "a malformed @ string is literal");
        Program.Check(Loc.Resolve("@UI:Play=Start") == "Start", "no store: the inline default");
        Program.Check(Loc.Resolve("@UI:Play") == "[UI:Play]", "no store and no default: the bracket form");
        Program.Check(Loc.Resolve("@UI:k=a=b") == "a=b", "the default is everything after the first '='");
        Program.Check(Loc.Text("UI", "Quit", "Quit game") == "Quit game", "Loc.Text falls back to the given default");
        Program.Check(Loc.Text("UI", "Quit") == "[UI:Quit]", "...or the bracket form");
        Program.Check(Loc.Text("bad table", "k", "d") == "d", "an invalid table name returns the default");

        var lt = new LocText("UI", "Play", "Play");
        Program.Check(lt.Table == "UI" && lt.Key == "Play" && lt.Default == "Play" && !lt.IsEmpty, "LocText fields");
        Program.Check(lt.ToString() == "Play" && (string)lt == "Play", "ToString and the implicit conversion resolve");
        var parsed = new LocText("@UI:Quit=Quit");
        Program.Check(parsed.Table == "UI" && parsed.Key == "Quit" && parsed.Default == "Quit", "LocText parses a reference string");
        var literal = new LocText("Just text");
        Program.Check(literal.ToString() == "Just text" && literal.Table == "", "a literal LocText");
        Program.Check(default(LocText).IsEmpty && default(LocText).ToString() == "", "default(LocText) is empty");
        Program.Check(new LocText("Score: {0}").Format(5) == "Score: 5", "LocText.Format");
        Program.Check(Loc.Plural("UI", "Items.count", 1).Length > 0, "Plural without a store does not throw");
    }
}
