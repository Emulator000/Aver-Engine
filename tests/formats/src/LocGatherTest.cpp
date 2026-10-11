// Localization gather scanners and the used-versus-present report (docs/plans/wave2/DATA_TABLES_LOCALIZATION.md 4.1).
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/LocGather.hpp"

#include <string>
#include <vector>

using namespace aver;
using namespace aver::fmt;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) {
        AVER_INFO("   PASS  {}", what);
    } else {
        AVER_ERROR("   FAIL  {}", what);
        ++g_failures;
    }
}

static const LocUse* findUse(const std::vector<LocUse>& v, const std::string& table, const std::string& key) {
    for (const LocUse& u : v) if (u.table == table && u.key == key) return &u;
    return nullptr;
}

static void testOcui() {
    AVER_INFO("=== gatherOcui ===");
    const std::string text =
        "OCUI 1\n"
        "WIDGET root Panel -\n"
        "SET title text \"@UI:menu.title=Main Menu\"\n"
        "SET list items \"@UI:a=A\" literal \"@UI:b\"\n"
        "SET input placeholder @UI:ph=Type\n"
        "SET btn text Play\n"
        "SET lbl text \"@@mention\"\n"
        "SET lbl name \"@UI:ignored\"\n"
        "# SET title text @UI:comment\n"
        "SET multi items \"@UI:c=C\\nplain\"\n";
    std::vector<LocUse> u;
    gatherOcui(text, "Menu.ocui", u);
    check(u.size() == 5, "five references found (literals, @@, other properties and comments ignored)");
    const LocUse* t = findUse(u, "UI", "menu.title");
    check(t && t->hasDefault && t->defaultText == "Main Menu" && t->source == "Menu.ocui:3", "text with a default and its line number");
    const LocUse* a = findUse(u, "UI", "a");
    const LocUse* b = findUse(u, "UI", "b");
    check(a && a->defaultText == "A" && a->source == "Menu.ocui:4" && b && !b->hasDefault, "each item of a list is checked");
    check(findUse(u, "UI", "ph") && findUse(u, "UI", "ph")->defaultText == "Type", "placeholder");
    check(findUse(u, "UI", "c") && findUse(u, "UI", "c")->defaultText == "C", "an item holding a newline is split");
    check(!findUse(u, "UI", "ignored") && !findUse(u, "UI", "comment"), "other properties and commented lines are not gathered");
}

static void testOcgraph() {
    AVER_INFO("=== gatherOcgraph ===");
    const std::string text =
        "OCGRAPH 1\n"
        "NODE a SetUiText widget=1 text=\"@UI:hud.ammo=Ammo\"\n"
        "NODE b Print text=@UI:x.y\n"
        "NODE c Foo label=\"has space @UI:no=thing\"\n"
        "# NODE d SetUiText text=@UI:comment\n"
        "COMP k Mesh text=@UI:notnode\n"
        "NODE e SetUiText \"text=@UI:quoted.whole=Whole Thing\"\n";
    std::vector<LocUse> u;
    gatherOcgraph(text, "G.ocgraph", u);
    check(u.size() == 3, "three NODE attribute references");
    const LocUse* a = findUse(u, "UI", "hud.ammo");
    check(a && a->defaultText == "Ammo" && a->source == "G.ocgraph:2", "quoted attribute value with a default");
    check(findUse(u, "UI", "x.y") && !findUse(u, "UI", "x.y")->hasDefault, "bare attribute value without a default");
    const LocUse* e = findUse(u, "UI", "quoted.whole");
    check(e && e->defaultText == "Whole Thing", "a whole quoted key=value token");
    check(!findUse(u, "UI", "no") && !findUse(u, "UI", "comment") && !findUse(u, "UI", "notnode"),
          "text that merely contains a reference, comments and non-NODE records are ignored");
}

static void testCsharp() {
    AVER_INFO("=== gatherCsharp ===");
    const std::string text =
        "using Aver.Framework;\n"
        "class C {\n"
        "  void F() {\n"
        "    var a = Loc.Text(\"UI\", \"Play\", \"Play\");\n"
        "    var b = new LocText(\"UI\", \"Quit\");\n"
        "    // var c = Loc.Text(\"UI\", \"Commented\", \"x\");\n"
        "    /* Loc.Text(\"UI\", \"Block\", \"x\") */\n"
        "    var d = Loc.Text(table, \"Dyn\");\n"
        "    var e = Loc.Format(\"UI\", \"Score\", \"Score: {0}\", 5);\n"
        "    var f = Loc.Plural(\"Items\", \"count\", n);\n"
        "    var g = \"Loc.Text(\\\"UI\\\", \\\"InString\\\", \\\"x\\\")\";\n"
        "    var h = Aver.Framework.Loc.Text(\"UI\", \"Qualified\");\n"
        "    var i = new LocText(\"@UI:ref.form=Ref\");\n"
        "    var j = Loc.Text(\"UI\", \"Bad Key\", \"x\");\n"
        "    var k = Loc.Format(\"UI\", \"NullDef\", null, 1);\n"
        "    var l = Loc.Text(\"UI\", \"Verb\", @\"Verbatim \"\"q\"\"\");\n"
        "    var m = MyLoc.Text(\"UI\", \"NotLoc\", \"x\");\n"
        "  }\n"
        "}\n";
    std::vector<LocUse> u;
    gatherCsharp(text, "Game/Menu.cs", u);
    check(u.size() == 8, "eight literal uses found");
    const LocUse* a = findUse(u, "UI", "Play");
    check(a && a->hasDefault && a->defaultText == "Play" && a->source == "Game/Menu.cs:4", "Loc.Text with a default and its line");
    const LocUse* b = findUse(u, "UI", "Quit");
    check(b && !b->hasDefault && b->source == "Game/Menu.cs:5", "new LocText without a default");
    check(findUse(u, "UI", "Score") && findUse(u, "UI", "Score")->defaultText == "Score: {0}", "Loc.Format takes the third argument as the default");
    const LocUse* p = findUse(u, "Items", "count");
    check(p && !p->hasDefault, "Loc.Plural carries the base key and no default");
    check(findUse(u, "UI", "Qualified"), "a qualified Aver.Framework.Loc.Text call");
    check(findUse(u, "UI", "ref.form") && findUse(u, "UI", "ref.form")->defaultText == "Ref", "the one-argument reference constructor");
    check(findUse(u, "UI", "NullDef") && !findUse(u, "UI", "NullDef")->hasDefault, "a null default is no default");
    check(findUse(u, "UI", "Verb") && findUse(u, "UI", "Verb")->defaultText == "Verbatim \"q\"", "a verbatim string default");
    check(!findUse(u, "UI", "Commented") && !findUse(u, "UI", "Block") && !findUse(u, "UI", "Dyn") &&
          !findUse(u, "UI", "InString") && !findUse(u, "UI", "Bad Key") && !findUse(u, "UI", "NotLoc"),
          "comments, string contents, non-literal table args, invalid keys and other types are ignored");
}

static void testTable() {
    AVER_INFO("=== gatherTable ===");
    OcTableData d;
    check(parseTableCsv("Name,Display Name:text,Plain\nsword,@Items:sword.name=Sword,x\nbow,Bow,@@y\n", d), "table parses");
    std::vector<LocUse> u;
    gatherTable(d, "Data/items.csv", u);
    check(u.size() == 1 && u[0].table == "Items" && u[0].key == "sword.name" && u[0].defaultText == "Sword", "only the reference cell");
    check(u[0].source == "Data/items.csv:2", "source is the CSV line");
}

static LocCorpus corpus() {
    LocCorpus c;
    OcStringsData en;
    en.culture = "en";
    en.entries = {{"Play", "Play", ""}, {"Quit", "Quit", ""}, {"Old", "o", ""}};
    c["en"]["UI"] = en;
    OcStringsData items;
    items.entries = {{"count.one", "1 item", ""}, {"count.other", "n items", ""}};
    c["en"]["Items"] = items;
    OcStringsData de;
    de.culture = "de";
    de.entries = {{"Play", "Spielen", ""}, {"Quit", "Quit", ""}};
    c["de"]["UI"] = de;
    return c;
}

static std::vector<LocUse> uses() {
    std::vector<LocUse> u;
    u.push_back({"UI", "Play", "Play", true, "a:1"});
    u.push_back({"UI", "Quit", "", false, "a:2"});
    u.push_back({"UI", "Score", "Score", true, "a:3"});
    u.push_back({"UI", "Score", "Points", true, "b:9"});
    u.push_back({"Items", "count", "", false, "a:4"});
    u.push_back({"UI", "Fresh", "", false, "a:5"});
    return u;
}

static void testReport() {
    AVER_INFO("=== buildLocReport ===");
    const LocReport r = buildLocReport(uses(), corpus(), "en");
    usize enMissing = 0, deMissing = 0;
    bool freshEn = false, scoreDe = false, itemsDe = false, scoreEn = false;
    for (const LocReport::Missing& m : r.missing) {
        if (m.culture == "en") { ++enMissing; freshEn = freshEn || (m.table == "UI" && m.key == "Fresh"); scoreEn = scoreEn || m.key == "Score"; }
        if (m.culture == "de") { ++deMissing; scoreDe = scoreDe || (m.table == "UI" && m.key == "Score"); itemsDe = itemsDe || (m.table == "Items" && m.key == "count"); }
    }
    check(enMissing == 1 && freshEn, "default culture: only the key with neither a table entry nor an inline default");
    check(!scoreEn, "an inline default covers the default culture");
    check(deMissing == 3 && scoreDe && itemsDe, "other culture: every used key it lacks (plural base found by .other/.one is present in en only)");
    check(r.unused.size() == 1 && r.unused[0].culture == "en" && r.unused[0].table == "UI" && r.unused[0].key == "Old",
          "unused: a table key nothing uses (plural forms count as use of their base)");
    check(r.conflicts.size() == 1 && r.conflicts[0].key == "Score" && r.conflicts[0].defaults.size() == 2, "conflicting inline defaults");
    check(r.untranslated.size() == 1 && r.untranslated[0].culture == "de" && r.untranslated[0].key == "Quit", "untranslated: identical to the default culture");

    const LocReport empty = buildLocReport(std::vector<LocUse>{}, LocCorpus{}, "");
    check(empty.missing.empty() && empty.unused.empty() && empty.conflicts.empty() && empty.untranslated.empty(), "nothing in, nothing out");
    std::vector<LocUse> one;
    one.push_back({"UI", "K", "", false, "s:1"});
    const LocReport noDefaultCulture = buildLocReport(one, LocCorpus{}, "fr");
    check(noDefaultCulture.missing.size() == 1 && noDefaultCulture.missing[0].culture == "fr", "the default culture is checked even with no tables on disk");
}

static void testApplyGather() {
    AVER_INFO("=== applyGather ===");
    LocCorpus c;
    const usize n = applyGather(uses(), c, "en");
    check(n == 5, "five distinct keys added (UI Play/Quit/Score/Fresh, Items count)");
    const OcStringsData& ui = c["en"]["UI"];
    const auto text = [&](const std::string& k) { for (const OcStringEntry& e : ui.entries) if (e.key == k) return e.text; return std::string("<none>"); };
    check(text("Play") == "Play" && text("Score") == "Score" && text("Quit").empty() && text("Fresh").empty(),
          "text is the inline default (the first one), empty without one");
    check(ui.culture == "en", "a created table is stamped with the culture");
    check(applyGather(uses(), c, "en") == 0, "idempotent");
    check(c.size() == 1 && c.count("en"), "only the default culture is touched");

    LocCorpus withPlural = corpus();
    const usize m = applyGather(uses(), withPlural, "en");
    check(m == 2, "existing keys and plural forms are not re-added (only Score and Fresh)");
    check(withPlural["de"]["UI"].entries.size() == 2, "other cultures are untouched");
}

int main() {
    testOcui();
    testOcgraph();
    testCsharp();
    testTable();
    testReport();
    testApplyGather();

    AVER_INFO("==================================================");
    AVER_INFO("LocGather tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
