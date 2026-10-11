// The shared line-record tokenizer behind .oceqs, .ocso and .ocst: header rules, quoting and escapes,
// comments, CRLF, key=value splitting, and the float formatting the exact writers rely on. Pure.
#include "aver/synapse/TextRecords.hpp"

#include "aver/core/Log.hpp"

#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::synapse;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool read(const std::string& text, std::vector<TextRecord>& out, std::string* why = nullptr) {
    return readTextRecords(text, "OCTEST", 1, out, why);
}

static void headerRules() {
    std::vector<TextRecord> r;
    std::string why;
    check(read("OCTEST 1\nA 1\n", r, &why) && r.size() == 1, "a header then one record");
    check(!read("", r, &why) && r.empty() && why.find("line 1") == 0, "an empty file has no header");
    check(!read("A 1\n", r, &why) && why.find("line 1: expected header") == 0, "the first record must be the header");
    check(!read("OCTEST 2\n", r, &why) && why.find("unsupported version") != std::string::npos, "an unknown version is refused");
    check(!read("OCTEST\n", r, &why), "a header without a version is refused");
    check(!read("OCTEST 1\nA 1\nOCTEST 1\n", r, &why) && why.find("line 3: repeated header") == 0, "a repeated header names its line");
    check(read("# leading comment\n\n  \nOCTEST 1 # trailing\nA\n", r) && r.size() == 1 && r[0].line == 5, "comments and blank lines before the header are fine, lines stay 1-based");
    check(read("\xEF\xBB\xBFOCTEST 1\nA\n", r) && r.size() == 1, "a UTF-8 BOM is skipped");
    check(read("ocTest 1\r\na x\r\n", r) && r.size() == 1 && r[0].keyword == "A" && r[0].args.size() == 1 && r[0].args[0] == "x", "keywords are upper-cased and CRLF is fine");
}

static void tokens() {
    std::vector<TextRecord> r;
    check(read("OCTEST 1\nTASK Leaf p0=3 s=\"a b\" # note\n", r) && r.size() == 1, "a record with kv and a comment");
    const TextRecord& t = r[0];
    check(t.keyword == "TASK" && t.args.size() == 1 && t.args[0] == "Leaf", "args exclude key=value tokens");
    check(t.kv.size() == 2 && t.get("p0") == "3" && t.get("s") == "a b", "kv values are unquoted");
    check(t.has("s") && !t.has("q") && t.get("q", "dflt") == "dflt", "has() and the fallback");
    check(t.rest == "Leaf p0=3 s=\"a b\"", "rest keeps the raw text without the comment");

    check(read("OCTEST 1\nK Key=Value \"q=r\" \"two words\" a\\b\n", r) && r[0].kv.size() == 1 && r[0].kv[0].first == "key", "a key is lower-cased");
    check(r[0].args.size() == 3 && r[0].args[0] == "q=r" && r[0].args[1] == "two words" && r[0].args[2] == "a\\b", "a quoted token with '=' is an arg, backslashes outside quotes are literal");
    check(read("OCTEST 1\nK s=\"say \\\"hi\\\" \\\\ \"\n", r) && r[0].get("s") == "say \"hi\" \\ ", "the escapes \\\" and \\\\ inside quotes");
    check(read("OCTEST 1\nK a=\"x # y\" # c\n", r) && r[0].get("a") == "x # y", "# inside quotes does not start a comment");
    check(read("OCTEST 1\nK a=b=c\n", r) && r[0].get("a") == "b=c", "only the first '=' splits");
    check(read("OCTEST 1\nK ==x\n", r) && r[0].args.size() == 1, "a token starting with '=' is not a key");

    std::string why;
    check(!read("OCTEST 1\nK \"open\n", r, &why) && why.find("line 2: unterminated quote") == 0, "an unterminated quote is refused with its line");
}

static void quoting() {
    check(quoteToken("plain") == "plain", "a plain word is not quoted");
    check(quoteToken("") == "\"\"", "an empty string is quoted");
    check(quoteToken("a b") == "\"a b\"" && quoteToken("a=b") == "\"a=b\"" && quoteToken("a#b") == "\"a#b\"", "space, '=' and '#' force quotes");
    check(quoteToken("say \"x\"") == "\"say \\\"x\\\"\"", "quotes and backslashes are escaped");
    // Round trip through the reader.
    const char* samples[] = {"plain", "", "a b", "a=b", "q\"q", "back\\slash and space", "#hash", "tail "};
    bool ok = true;
    for (const char* s : samples) {
        std::vector<TextRecord> r;
        const std::string line = "OCTEST 1\nK v=" + quoteToken(s) + "\n";
        ok = ok && read(line, r) && r.size() == 1 && r[0].get("v", "?") == s;
    }
    check(ok, "quoteToken output reads back as the same text");
}

static void floats() {
    f32 v = 0;
    check(parseF32("1.5", v) && v == 1.5f, "parseF32 reads a decimal");
    check(!parseF32("", v) && !parseF32("abc", v) && !parseF32("1.5x", v), "parseF32 refuses junk");
    check(!parseF32("nan", v) && !parseF32("inf", v) && !parseF32("-inf", v), "parseF32 refuses non-finite");
    i32 n = 0;
    check(parseI32("-12", n) && n == -12 && !parseI32("1.5", n) && !parseI32("", n), "parseI32");

    check(fmtF32(0.0f) == "0" && fmtF32(1.5f) == "1.5" && fmtF32(-250.0f) == "-250", "fmtF32 is short for short values");
    std::mt19937 rng(7);
    std::uniform_int_distribution<u32> bits(0, 0xFFFFFFFFu);
    bool roundTrips = true;
    int tested = 0;
    while (tested < 100) {
        const u32 b = bits(rng);
        f32 f;
        std::memcpy(&f, &b, 4);
        if (!std::isfinite(f)) continue;
        ++tested;
        f32 back = 0;
        roundTrips = roundTrips && parseF32(fmtF32(f), back) && std::memcmp(&back, &f, 4) == 0;
    }
    check(roundTrips, "fmtF32 reads back as the same f32 for 100 random values");
}

int main() {
    headerRules();
    tokens();
    quoting();
    floats();
    if (g_failures == 0) AVER_INFO("TextRecordsTest: all passed");
    return g_failures;
}
