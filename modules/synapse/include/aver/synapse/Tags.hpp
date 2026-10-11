#pragma once
// Hierarchical string tags for the AI formats ("Furniture.Seat"). Pure and header-only. Names cross
// every module boundary as strings (each module copy interns separately), so there are no ids here.
#include "aver/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::synapse {

// [A-Za-z][A-Za-z0-9_]*(.[A-Za-z][A-Za-z0-9_]*)*, at most 128 characters.
inline bool tagNameValid(std::string_view s) {
    if (s.empty() || s.size() > 128) return false;
    bool segStart = true;
    for (const char c : s) {
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = (c >= '0' && c <= '9') || c == '_';
        if (segStart) {
            if (!alpha) return false;
            segStart = false;
        } else if (c == '.') {
            segStart = true;
        } else if (!alpha && !digit) {
            return false;
        }
    }
    return !segStart;
}

// `tag` equals `query`, or lies under it ("A.B.C" matches "A.B").
inline bool tagMatches(std::string_view tag, std::string_view query) {
    if (tag == query) return true;
    return tag.size() > query.size() && tag.substr(0, query.size()) == query && tag[query.size()] == '.';
}

struct TagList {
    std::vector<std::string> v;   // at most 16 entries

    bool empty() const { return v.empty(); }
    bool operator==(const TagList& o) const { return v == o.v; }

    // An entry of *this matches an entry of the query (hierarchically).
    bool hasAny(const TagList& q) const {
        for (const std::string& t : v)
            for (const std::string& s : q.v)
                if (tagMatches(t, s)) return true;
        return false;
    }
    // Every entry of the query is matched by some entry of *this. True for an empty query.
    bool hasAll(const TagList& q) const {
        for (const std::string& s : q.v) {
            bool found = false;
            for (const std::string& t : v)
                if (tagMatches(t, s)) { found = true; break; }
            if (!found) return false;
        }
        return true;
    }
    bool hasNone(const TagList& q) const { return !hasAny(q); }

    // "A.B, C D": commas or whitespace separate; invalid names and extras past 16 are dropped.
    static TagList parse(std::string_view csv) {
        TagList r;
        usize i = 0;
        while (i < csv.size()) {
            while (i < csv.size() && (csv[i] == ',' || csv[i] == ' ' || csv[i] == '\t')) ++i;
            usize j = i;
            while (j < csv.size() && csv[j] != ',' && csv[j] != ' ' && csv[j] != '\t') ++j;
            if (j > i) {
                const std::string_view name = csv.substr(i, j - i);
                if (tagNameValid(name) && r.v.size() < 16) {
                    bool dup = false;
                    for (const std::string& e : r.v) dup = dup || e == name;
                    if (!dup) r.v.emplace_back(name);
                }
            }
            i = j;
        }
        return r;
    }
    // "A.B,C" for the C ABI.
    std::string csv() const {
        std::string r;
        for (const std::string& t : v) {
            if (!r.empty()) r += ',';
            r += t;
        }
        return r;
    }
};

} // namespace aver::synapse
