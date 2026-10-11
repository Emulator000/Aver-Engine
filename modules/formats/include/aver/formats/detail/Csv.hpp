#pragma once
// RFC 4180 reader and writer shared by the string-table (.csv) and data-table (.csv) formats.
// Cells stay text; nothing here converts. Views alias the caller's buffer only for the input.
#include "aver/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::fmt::detail {

struct CsvRecord {
    std::vector<std::string> cells;
    usize line = 0;   // 1-based line the record starts on
};

inline std::string_view csvStripBom(std::string_view t) {
    if (t.size() >= 3 && static_cast<u8>(t[0]) == 0xEF && static_cast<u8>(t[1]) == 0xBB && static_cast<u8>(t[2]) == 0xBF)
        t.remove_prefix(3);
    return t;
}

// ',' unless the header line (outside quotes) has no ',' but has ';' (Excel in many locales).
inline char csvDetectDelimiter(std::string_view t) {
    bool quoted = false, comma = false, semi = false;
    for (const char c : t) {
        if (c == '"') quoted = !quoted;
        else if (!quoted) {
            if (c == '\n') break;
            if (c == ',') comma = true;
            else if (c == ';') semi = true;
        }
    }
    return (!comma && semi) ? ';' : ',';
}

// True when the first line break is CRLF (or there is no line break at all: the default).
inline bool csvUsesCrlf(std::string_view t) {
    const usize nl = t.find('\n');
    return nl == std::string_view::npos || (nl > 0 && t[nl - 1] == '\r');
}

// Blank lines are skipped. After a closing quote, stray text up to the delimiter is kept (lenient).
inline bool csvParse(std::string_view t, char delim, std::vector<CsvRecord>& out, std::string* err) {
    out.clear();
    const usize n = t.size();
    usize i = 0, line = 1;
    while (i < n) {
        if (t[i] == '\n') { ++i; ++line; continue; }
        if (t[i] == '\r') { ++i; if (i < n && t[i] == '\n') { ++i; } ++line; continue; }
        CsvRecord rec;
        rec.line = line;
        for (;;) {
            std::string f;
            if (i < n && t[i] == '"') {
                ++i;
                bool closed = false;
                while (i < n) {
                    const char c = t[i++];
                    if (c == '"') {
                        if (i < n && t[i] == '"') { f.push_back('"'); ++i; }
                        else { closed = true; break; }
                    } else {
                        if (c == '\n') ++line;
                        f.push_back(c);
                    }
                }
                if (!closed) {
                    if (err) *err = "line " + std::to_string(rec.line) + ": unterminated quoted cell";
                    return false;
                }
                while (i < n && t[i] != delim && t[i] != '\n' && t[i] != '\r') f.push_back(t[i++]);
            } else {
                while (i < n && t[i] != delim && t[i] != '\n' && t[i] != '\r') f.push_back(t[i++]);
            }
            rec.cells.push_back(std::move(f));
            if (i < n && t[i] == delim) { ++i; continue; }
            break;
        }
        if (i < n && t[i] == '\r') ++i;
        if (i < n && t[i] == '\n') { ++i; ++line; }
        out.push_back(std::move(rec));
    }
    return true;
}

inline void csvWriteField(std::string& out, std::string_view f, char delim) {
    const bool quote = f.find_first_of("\"\r\n") != std::string_view::npos || f.find(delim) != std::string_view::npos;
    if (!quote) { out.append(f); return; }
    out.push_back('"');
    for (const char c : f) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
}

} // namespace aver::fmt::detail
