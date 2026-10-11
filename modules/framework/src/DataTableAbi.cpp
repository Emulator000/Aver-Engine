// Data-table store behind framework_table_abi.h: handles over parsed .csv/.json tables, versions, hot reload.
// One file-static store; single game thread. docs/DATA_TABLES.md.

#include "aver/framework/framework_table_abi.h"

#include "aver/core/Log.hpp"
#include "aver/formats/OcTable.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
namespace fmt = aver::fmt;

namespace {

struct Entry {
    std::string path;            // normalised absolute path
    fmt::OcTableData data;
    int32_t version = 1;
    bool alive = false;
};

struct Store {
    std::string root;
    std::string error;
    std::vector<Entry> entries;                       // handle - 1
    std::unordered_map<std::string, int32_t> byPath;  // key(path) -> handle
    int32_t epoch = 0;
};

Store g;

fs::path toPath(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
std::string fromPath(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// Absolute, lexically normal, forward slashes. Relative paths resolve against the content root.
std::string absolutePath(const std::string& in) {
    fs::path p = toPath(in);
    std::error_code ec;
    if (p.is_relative() && !g.root.empty()) p = toPath(g.root) / p;
    if (p.is_relative()) p = fs::absolute(p, ec);
    return fromPath(p.lexically_normal());
}

// The map key: case-folded on Windows, where the file system is.
std::string keyOf(std::string p) {
#if defined(_WIN32)
    for (char& c : p) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
#endif
    return p;
}

Entry* at(int32_t h) {
    if (h < 1 || static_cast<size_t>(h) > g.entries.size()) return nullptr;
    Entry& e = g.entries[static_cast<size_t>(h) - 1];
    return e.alive ? &e : nullptr;
}

bool reloadEntry(Entry& e) {
    fmt::OcTableData d;
    std::string err;
    if (!fmt::loadTable(e.path, d, &err)) {
        g.error = err;
        AVER_WARN("[Table] {}: {} (keeping the previous data)", e.path, err);
        return false;
    }
    e.data = std::move(d);
    ++e.version;
    ++g.epoch;
    return true;
}

} // namespace

extern "C" {

int32_t aver_fw_table_abi_version(void) { return AVER_FW_TABLE_ABI_VERSION; }

int32_t aver_fw_content_set_root(const char* utf8Dir) {
    if (!utf8Dir) return 0;
    std::string r = utf8Dir;
    for (char& c : r) if (c == '\\') c = '/';
    while (r.size() > 1 && r.back() == '/') r.pop_back();
    g.root = std::move(r);
    return 1;
}

const char* aver_fw_content_root(void) { return g.root.c_str(); }

int32_t aver_fw_table_load(const char* utf8Path) {
    g.error.clear();
    if (!utf8Path || !*utf8Path) { g.error = "empty table path"; return 0; }
    const std::string path = absolutePath(utf8Path);
    const std::string key = keyOf(path);
    const auto it = g.byPath.find(key);
    if (it != g.byPath.end()) return it->second;

    fmt::OcTableData d;
    if (!fmt::loadTable(path, d, &g.error)) {
        g.error = path + ": " + g.error;
        return 0;
    }
    Entry e;
    e.path = path;
    e.data = std::move(d);
    e.alive = true;
    g.entries.push_back(std::move(e));
    const int32_t h = static_cast<int32_t>(g.entries.size());
    g.byPath[key] = h;
    ++g.epoch;
    return h;
}

const char* aver_fw_table_error(void) { return g.error.c_str(); }

const char* aver_fw_table_path(int32_t h) {
    const Entry* e = at(h);
    return e ? e->path.c_str() : "";
}

int32_t aver_fw_table_unload(int32_t h) {
    Entry* e = at(h);
    if (!e) return 0;
    g.byPath.erase(keyOf(e->path));
    e->alive = false;
    e->data = fmt::OcTableData{};
    return 1;
}

int32_t aver_fw_table_reload(int32_t h) {
    g.error.clear();
    Entry* e = at(h);
    if (!e) { g.error = "invalid table handle"; return 0; }
    return reloadEntry(*e) ? 1 : 0;
}

int32_t aver_fw_table_reload_path(const char* utf8AbsPath) {
    g.error.clear();
    int32_t n = 0;
    if (!utf8AbsPath || !*utf8AbsPath) {
        for (Entry& e : g.entries) if (e.alive && reloadEntry(e)) ++n;
        return n;
    }
    const auto it = g.byPath.find(keyOf(absolutePath(utf8AbsPath)));
    if (it == g.byPath.end()) return 0;
    Entry* e = at(it->second);
    return (e && reloadEntry(*e)) ? 1 : 0;
}

int32_t aver_fw_table_version(int32_t h) {
    const Entry* e = at(h);
    return e ? e->version : -1;
}

int32_t aver_fw_tables_epoch(void) { return g.epoch; }

int32_t aver_fw_table_column_count(int32_t h) {
    const Entry* e = at(h);
    return e ? static_cast<int32_t>(e->data.columns.size()) : -1;
}

const char* aver_fw_table_column_name(int32_t h, int32_t c) {
    const Entry* e = at(h);
    if (!e || c < 0 || static_cast<size_t>(c) >= e->data.columns.size()) return "";
    return e->data.columns[static_cast<size_t>(c)].name.c_str();
}

int32_t aver_fw_table_column_type(int32_t h, int32_t c) {
    const Entry* e = at(h);
    if (!e || c < 0 || static_cast<size_t>(c) >= e->data.columns.size()) return -1;
    return static_cast<int32_t>(e->data.columns[static_cast<size_t>(c)].type);
}

int32_t aver_fw_table_column_find(int32_t h, const char* name) {
    const Entry* e = at(h);
    return (e && name) ? e->data.findColumn(name) : -1;
}

int32_t aver_fw_table_row_count(int32_t h) {
    const Entry* e = at(h);
    return e ? static_cast<int32_t>(e->data.rowNames.size()) : -1;
}

const char* aver_fw_table_row_name(int32_t h, int32_t r) {
    const Entry* e = at(h);
    if (!e || r < 0 || static_cast<size_t>(r) >= e->data.rowNames.size()) return "";
    return e->data.rowNames[static_cast<size_t>(r)].c_str();
}

int32_t aver_fw_table_row_find(int32_t h, const char* name) {
    const Entry* e = at(h);
    return (e && name) ? e->data.findRow(name) : -1;
}

const char* aver_fw_table_cell(int32_t h, int32_t r, int32_t c) {
    const Entry* e = at(h);
    if (!e || r < 0 || c < 0) return "";
    const size_t nc = e->data.columns.size();
    if (static_cast<size_t>(r) >= e->data.rowNames.size() || static_cast<size_t>(c) >= nc) return "";
    const size_t i = static_cast<size_t>(r) * nc + static_cast<size_t>(c);
    return i < e->data.cells.size() ? e->data.cells[i].c_str() : "";
}

} // extern "C"
