// .ocstrings / string-table CSV / text-reference / culture-tag tests (docs/plans/wave2/DATA_TABLES_LOCALIZATION.md
// sections 2 and 3), plus the OcProject LOCALE.DEFAULT key.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcProject.hpp"
#include "aver/formats/OcStrings.hpp"

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

static const OcStringEntry* find(const OcStringsData& d, const std::string& key) {
    for (const OcStringEntry& e : d.entries) if (e.key == key) return &e;
    return nullptr;
}

static bool anyContains(const std::vector<std::string>& v, const std::string& needle) {
    for (const std::string& s : v) if (s.find(needle) != std::string::npos) return true;
    return false;
}

static void testHeader() {
    AVER_INFO("=== .ocstrings header ===");
    OcStringsData d;
    std::string err;
    check(!parseOcstrings("", d, nullptr, &err) && !err.empty(), "an empty file is rejected with a reason");
    err.clear();
    check(!parseOcstrings("ENTRY A \"x\"\n", d, nullptr, &err), "ENTRY before the header is rejected");
    err.clear();
    check(!parseOcstrings("OCSTRINGS 2\n", d, nullptr, &err) && err.find("unsupported") != std::string::npos,
          "a higher version is rejected as unsupported");
    err.clear();
    check(!parseOcstrings("OCSTRINGS\n", d, nullptr, &err), "a header with no version is rejected");
    check(parseOcstrings("# lead comment\n\nOCSTRINGS 1\n", d, nullptr, &err) && d.entries.empty() && d.version == 1,
          "comments and blanks may precede the header; no entries is fine");
    check(parseOcstrings("OCSTRINGS 1\nCULTURE de\n", d) && d.culture == "de", "CULTURE is read");
}

static void testEntries() {
    AVER_INFO("=== .ocstrings entries ===");
    const std::string text = R"X(OCSTRINGS 1
CULTURE de
# Main menu
ENTRY Menu.Play "Spielen"
ENTRY Menu.Quit "Beenden" NOTE "Shown on the quit button; keep short"
ENTRY Esc "say \"hi\" \\ \n \t end"
ENTRY Hash "a # b"
ENTRY Trail "x" # a trailing comment
ENTRY Bare bare#hash
FUTURE record we do not know
ENTRY Empty ""
)X";
    OcStringsData d;
    std::vector<std::string> diag;
    std::string err;
    check(parseOcstrings(text, d, &diag, &err), "parses");
    check(diag.empty(), "no diagnostics for a clean file");
    check(d.entries.size() == 7, "seven entries (unknown record skipped)");
    check(find(d, "Menu.Play") && find(d, "Menu.Play")->text == "Spielen", "plain entry");
    check(find(d, "Menu.Quit") && find(d, "Menu.Quit")->note == "Shown on the quit button; keep short", "NOTE read");
    check(find(d, "Esc") && find(d, "Esc")->text == "say \"hi\" \\ \n \t end", "escapes \\\" \\\\ \\n \\t decode");
    check(find(d, "Hash") && find(d, "Hash")->text == "a # b", "# inside quotes is text");
    check(find(d, "Trail") && find(d, "Trail")->text == "x" && find(d, "Trail")->note.empty(), "a # after the tokens is a comment");
    check(find(d, "Bare") && find(d, "Bare")->text == "bare#hash", "# inside a bare token is text");
    check(find(d, "Empty") && find(d, "Empty")->text.empty(), "empty text is a valid entry");
    check(d.entries[0].key == "Menu.Play" && d.entries[1].key == "Menu.Quit", "file order is kept");
}

static void testDiagnostics() {
    AVER_INFO("=== .ocstrings malformed lines and duplicates ===");
    const std::string text =
        "OCSTRINGS 1\n"
        "ENTRY A \"one\"\n"
        "ENTRY Bad \"unterminated\n"
        "ENTRY bad$key \"x\"\n"
        "ENTRY Nope\n"
        "ENTRY A \"two\"\n"
        "ENTRY C \"ok\" EXTRA \"y\"\n"
        "ENTRY D \"fine\"\n";
    OcStringsData d;
    std::vector<std::string> diag;
    check(parseOcstrings(text, d, &diag, nullptr), "a file with bad lines still parses");
    check(find(d, "A") && find(d, "A")->text == "two", "duplicate key: last wins");
    check(anyContains(diag, "duplicate"), "...with a diagnostic");
    check(anyContains(diag, "line 3"), "the unterminated line is named by number");
    check(anyContains(diag, "line 4"), "an invalid key is named by number");
    check(anyContains(diag, "line 5"), "a missing text is named by number");
    check(anyContains(diag, "line 7"), "trailing junk after the text is named by number");
    check(find(d, "D") && find(d, "D")->text == "fine", "the rest of the table still loads");
    check(!find(d, "Bad") && !find(d, "C"), "malformed entries are skipped, not half-loaded");
}

static void testMerge() {
    AVER_INFO("=== .ocstrings merge write ===");
    const std::string existing =
        "OCSTRINGS 1\n"
        "CULTURE de\n"
        "# Main menu\n"
        "ENTRY Menu.Play \"Spielen\"\n"
        "ENTRY Menu.Quit \"Beenden\" NOTE \"short\"\n"
        "FUTURE x y\n"
        "\n"
        "ENTRY Old \"weg\"\n";
    OcStringsData d;
    check(parseOcstrings(existing, d), "existing parses");

    check(writeOcstrings(d, existing) == existing, "an unchanged table merges back byte for byte");

    OcStringsData e = d;
    e.entries[1].text = "Beenden!";                         // edit
    e.entries.erase(e.entries.begin() + 2);                   // drop Old
    e.entries.push_back({"New.Key", "Neu", ""});              // add
    const std::string expect =
        "OCSTRINGS 1\n"
        "CULTURE de\n"
        "# Main menu\n"
        "ENTRY Menu.Play \"Spielen\"\n"
        "ENTRY Menu.Quit \"Beenden!\" NOTE \"short\"\n"
        "ENTRY New.Key \"Neu\"\n"
        "FUTURE x y\n"
        "\n";
    check(writeOcstrings(e, existing) == expect,
          "edited rewritten, removed dropped, new appended after the last ENTRY, comments/unknown/blank stay");

    // Odd spacing and a trailing comment survive when the entry itself did not change.
    const std::string odd = "OCSTRINGS 1\nENTRY   A    \"x\"   # keep me\n";
    OcStringsData od;
    check(parseOcstrings(odd, od) && writeOcstrings(od, odd) == odd, "an untouched entry keeps its spacing and comment");

    // A malformed line is not ours to delete.
    const std::string bad = "OCSTRINGS 1\nENTRY A \"x\"\nENTRY B \"oops\n";
    OcStringsData bd;
    check(parseOcstrings(bad, bd) && writeOcstrings(bd, bad) == bad, "a malformed line is preserved by a merge");

    // CRLF files stay CRLF, including appended entries.
    const std::string crlf = "OCSTRINGS 1\r\nENTRY A \"x\"\r\n";
    OcStringsData cd;
    check(parseOcstrings(crlf, cd), "CRLF parses");
    cd.entries.push_back({"B", "y", ""});
    check(writeOcstrings(cd, crlf) == "OCSTRINGS 1\r\nENTRY A \"x\"\r\nENTRY B \"y\"\r\n", "CRLF is kept for appended entries");

    // No trailing newline on the existing file.
    const std::string nonl = "OCSTRINGS 1\nENTRY A \"x\"";
    OcStringsData nd;
    check(parseOcstrings(nonl, nd), "no trailing newline parses");
    nd.entries.push_back({"B", "y", ""});
    check(writeOcstrings(nd, nonl) == "OCSTRINGS 1\nENTRY A \"x\"\nENTRY B \"y\"\n", "appending to a file with no final newline adds one");

    // Appending when no ENTRY exists goes to the end.
    const std::string headerOnly = "OCSTRINGS 1\n# nothing yet\n";
    OcStringsData hd;
    check(parseOcstrings(headerOnly, hd), "header-only parses");
    hd.entries.push_back({"A", "x", ""});
    check(writeOcstrings(hd, headerOnly) == "OCSTRINGS 1\n# nothing yet\nENTRY A \"x\"\n", "new keys go at the end when there is no ENTRY");

    // A changed note rewrites the line.
    OcStringsData nn = d;
    nn.entries[0].note = "button";
    check(writeOcstrings(nn, existing).find("ENTRY Menu.Play \"Spielen\" NOTE \"button\"") != std::string::npos, "a note edit rewrites the line");
}

static void testFreshWriteRoundTrip() {
    AVER_INFO("=== .ocstrings fresh write ===");
    OcStringsData d;
    d.culture = "pt-BR";
    d.entries.push_back({"A", "say \"hi\"\nnext\t\\", "note \"q\""});
    d.entries.push_back({"B.c-d_e", "", ""});
    d.entries.push_back({"Hash", "a # b", ""});
    const std::string text = writeOcstrings(d);
    check(text.rfind("OCSTRINGS 1\nCULTURE pt-BR\n", 0) == 0, "fresh file: header then CULTURE");
    OcStringsData back;
    std::vector<std::string> diag;
    check(parseOcstrings(text, back, &diag) && diag.empty(), "fresh output reparses cleanly");
    check(back.culture == "pt-BR" && back.entries.size() == 3, "culture and count survive");
    bool same = true;
    for (usize i = 0; i < 3 && i < back.entries.size(); ++i)
        same = same && back.entries[i].key == d.entries[i].key && back.entries[i].text == d.entries[i].text &&
               back.entries[i].note == d.entries[i].note;
    check(same, "write(parse) round-trips escapes, empty text and notes");
    check(writeOcstrings(back) == text, "writing the reparsed table is byte-stable");
}

static void testCsv() {
    AVER_INFO("=== string-table CSV ===");
    OcStringsData d;
    std::vector<std::string> diag;
    std::string err;
    const std::string csv =
        "Key,Text,Note\r\n"
        "A,\"hello, \"\"world\"\"\",n1\r\n"
        "B,\"line1\nline2\",\r\n"
        "\r\n"
        "C,plain,\r\n";
    check(parseStringsCsv(csv, d, &diag, &err), "CSV parses");
    check(find(d, "A") && find(d, "A")->text == "hello, \"world\"" && find(d, "A")->note == "n1", "quotes and doubled quotes");
    check(find(d, "B") && find(d, "B")->text == "line1\nline2", "embedded newline");
    check(d.entries.size() == 3, "blank line skipped");

    check(parseStringsCsv(std::string("\xEF\xBB\xBF") + "key,TEXT\nA,x\n", d, nullptr, &err) && find(d, "A") && find(d, "A")->text == "x",
          "UTF-8 BOM stripped, header is case-insensitive, Note optional");
    check(parseStringsCsv("Key;Text\nA;x,y\n", d, nullptr, &err) && find(d, "A") && find(d, "A")->text == "x,y",
          "';' delimiter detected from the header");
    err.clear();
    check(!parseStringsCsv("Name,Value\nA,b\n", d, nullptr, &err) && !err.empty(), "missing Key/Text columns fail");
    check(!parseStringsCsv("", d, nullptr, &err), "an empty CSV fails");

    diag.clear();
    check(parseStringsCsv("Key,Text\nbad key,x\nA,1\nA,2\n", d, &diag, nullptr), "bad rows do not fail the table");
    check(anyContains(diag, "invalid key") && anyContains(diag, "duplicate"), "invalid key and duplicate are diagnosed");
    check(find(d, "A") && find(d, "A")->text == "2", "duplicate: last wins");

    OcStringsData w;
    w.entries.push_back({"A", "a,b", "note"});
    w.entries.push_back({"B", "q\"x\ny", ""});
    OcStringsData back;
    check(parseStringsCsv(writeStringsCsv(w), back) && back.entries.size() == 2 && back.entries[0].text == "a,b" &&
          back.entries[0].note == "note" && back.entries[1].text == "q\"x\ny", "writeStringsCsv round-trips");
    OcStringsData nonote;
    nonote.entries.push_back({"A", "x", ""});
    check(writeStringsCsv(nonote).find("Note") == std::string::npos, "no Note column when no entry has a note");
}

static void testLocRef() {
    AVER_INFO("=== text references ===");
    struct Case { const char* in; bool ok; const char* table; const char* key; bool hasDef; const char* def; };
    const Case cases[] = {
        {"@UI:Play", true, "UI", "Play", false, ""},
        {"@UI:Play=Play", true, "UI", "Play", true, "Play"},
        {"@UI:Play=", true, "UI", "Play", true, ""},
        {"@UI:Eq=a=b", true, "UI", "Eq", true, "a=b"},
        {"@T_1:k.sub-1=Some text here", true, "T_1", "k.sub-1", true, "Some text here"},
        {"@@x", false, "", "", false, ""},
        {"@mention", false, "", "", false, ""},
        {"@bad table:K", false, "", "", false, ""},
        {"@T:", false, "", "", false, ""},
        {"@:K", false, "", "", false, ""},
        {"@T:K x", false, "", "", false, ""},
        {"@T-x:K", false, "", "", false, ""},
        {"@T:K$", false, "", "", false, ""},
        {"plain", false, "", "", false, ""},
        {"", false, "", "", false, ""},
    };
    for (const Case& c : cases) {
        LocRef r;
        const bool ok = parseLocRef(c.in, r);
        bool good = ok == c.ok;
        if (good && ok) good = r.table == c.table && r.key == c.key && r.hasDefault == c.hasDef && r.defaultText == c.def;
        check(good, std::string("parseLocRef '") + c.in + "'");
        if (ok) check(formatLocRef(r) == c.in, std::string("formatLocRef inverts '") + c.in + "'");
    }
    check(validLocKey("a.b-c_1") && !validLocKey("") && !validLocKey("a b") && !validLocKey("a:b"), "validLocKey");
    check(validLocTable("UI_1") && !validLocTable("") && !validLocTable("a.b"), "validLocTable");
}

static void testCultures() {
    AVER_INFO("=== culture tags ===");
    check(normalizeCulture("pt_br") == "pt-BR", "pt_br -> pt-BR");
    check(normalizeCulture("zh-hans") == "zh-Hans", "zh-hans -> zh-Hans");
    check(normalizeCulture("ZH_hans_cn") == "zh-Hans-CN", "ZH_hans_cn -> zh-Hans-CN");
    check(normalizeCulture("es-419") == "es-419", "numeric region");
    check(normalizeCulture("EN") == "en", "language lowercased");
    check(normalizeCulture("e").empty() && normalizeCulture("english").empty() && normalizeCulture("en-").empty() &&
          normalizeCulture("en-US-x").empty() && normalizeCulture("").empty() && normalizeCulture("en-U1").empty(),
          "malformed tags -> empty");

    const auto chain = [](const char* a, const char* d) { return cultureChain(a, d); };
    check(chain("zh-Hans-CN", "en-GB") == std::vector<std::string>({"zh-Hans-CN", "zh-Hans", "zh", "en-GB", "en"}),
          "zh-Hans-CN with default en-GB");
    check(chain("pt_br", "en") == std::vector<std::string>({"pt-BR", "pt", "en"}), "pt_br normalised, parent, default");
    check(chain("en", "en") == std::vector<std::string>({"en"}), "duplicates removed");
    check(chain("", "") == std::vector<std::string>({"en"}), "empty active and default: en");
    check(chain("en-GB", "en-GB") == std::vector<std::string>({"en-GB", "en"}), "default's parent once");
    check(chain("!!", "de") == std::vector<std::string>({"de"}), "a malformed active tag is skipped");
}

static void testProjectLocale() {
    AVER_INFO("=== OcProject LOCALE.DEFAULT ===");
    const std::string original = "OCPROJECT 1\nNAME P\nENGINE Aver 0.1.0\nCONTENT Content\n";
    ProjectDesc d;
    std::string err;
    check(parseOcproject(original, d, &err) && d.localeDefault.empty(), "absent key leaves localeDefault empty");
    check(writeOcproject(d, original).find("LOCALE.DEFAULT") == std::string::npos, "an empty value is not written");

    const std::string withKey = original + "LOCALE.DEFAULT de\n";
    ProjectDesc e;
    check(parseOcproject(withKey, e, &err) && e.localeDefault == "de", "LOCALE.DEFAULT de parses");
    const std::string out = writeOcproject(e, withKey);
    ProjectDesc back;
    check(parseOcproject(out, back, &err) && back.localeDefault == "de", "round-trips through writeOcproject");
    check(writeOcproject(back, out) == out, "re-saving is byte-stable (the key is owned, not duplicated)");
    back.localeDefault.clear();
    check(writeOcproject(back, out).find("LOCALE.DEFAULT") == std::string::npos, "clearing removes the line");
}

int main() {
    testHeader();
    testEntries();
    testDiagnostics();
    testMerge();
    testFreshWriteRoundTrip();
    testCsv();
    testLocRef();
    testCultures();
    testProjectLocale();

    AVER_INFO("==================================================");
    AVER_INFO("OcStrings tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
