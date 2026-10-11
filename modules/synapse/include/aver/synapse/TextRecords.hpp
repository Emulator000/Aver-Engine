#pragma once
// Line-record tokenizer shared by the AI text formats (.oceqs, .ocso, .ocst): "<HEADER> <version>",
// '#' comments, one record per line. Pure. Format rules: docs/formats/FORMAT_SPECS.md 11e-11g.
#include "aver/core/Types.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aver::synapse {

struct TextRecord {
    u32 line = 0;                                          // 1-based
    std::string keyword;                                   // upper-cased first token
    std::vector<std::string> args;                         // remaining tokens without '=' (quotes removed)
    std::vector<std::pair<std::string, std::string>> kv;   // tokens "key=value" (key lower-cased, value unquoted)
    std::string rest;                                      // raw text after the keyword, trimmed, comments removed

    // First kv match for `key` (lower-case), else `fallback`.
    std::string get(std::string_view key, std::string_view fallback = {}) const;
    bool has(std::string_view key) const;
};

// Splits `text` into records (the header line is checked and not returned). The first non-blank,
// non-comment line must be "<header> <version>". Tokens split on whitespace; a double-quoted token (or
// the value after '=') may contain spaces and the escapes \" and \\ ; '#' outside quotes starts a
// comment; CRLF and a UTF-8 BOM are fine. False, with `why` = "line N: ...", on a missing, repeated or
// unknown-version header or an unterminated quote; `out` is empty on failure.
bool readTextRecords(std::string_view text, std::string_view header, int version,
                     std::vector<TextRecord>& out, std::string* why);

// Quotes only when the text contains whitespace, '"', '#', '=' or is empty.
std::string quoteToken(std::string_view s);
// Shortest fixed-notation text that reads back as the same f32 (as OcLanes' writer).
std::string fmtF32(f32 v);
// Whole-string decimal parse; finite values only.
bool parseF32(std::string_view s, f32& out);
// Whole-string integer parse.
bool parseI32(std::string_view s, i32& out);

} // namespace aver::synapse
