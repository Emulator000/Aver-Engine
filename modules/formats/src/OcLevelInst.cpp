// LEVELINST / LIOVERRIDE / ENDLEVELINST records of .ocworld. See OcLevelInst.hpp.
#include "aver/formats/OcLevelInst.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/detail/TextEscape.hpp"
#include "aver/formats/detail/TextScan.hpp"

#include <cstdio>
#include <cstdlib>

namespace aver::fmt {
using namespace aver::fmt::detail;

namespace {

// The shortest of %.9g and %.17g that reads back as exactly `v` (OcPrefab.cpp's spelling).
std::string numAuto(f64 v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    if (std::strtod(buf, nullptr) == v) return buf;
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

bool keyValueOf(std::string_view tok, std::string_view key, std::string_view& value) {
    if (tok.size() < key.size() || !startsWithCI(tok, key)) return false;
    value = tok.substr(key.size());
    return true;
}

} // namespace

std::string ocLevelInstIdText(u64 id) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(id));
    return b;
}

u64 ocLevelInstIdParse(std::string_view hex) {
    hex = trim(hex);
    if (hex.empty() || hex.size() > 16) return 0;
    u64 v = 0;
    for (const char c : hex) {
        u32 d;
        if (c >= '0' && c <= '9') d = static_cast<u32>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<u32>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<u32>(c - 'A' + 10);
        else return 0;
        v = (v << 4) | d;
    }
    return v;
}

bool ocLevelInstSplitIds(std::string_view path, std::vector<u64>& ids) {
    ids.clear();
    if (path.empty()) return false;
    usize start = 0;
    for (usize i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const u64 v = ocLevelInstIdParse(path.substr(start, i - start));
            if (v == 0) { ids.clear(); return false; }
            ids.push_back(v);
            start = i + 1;
        }
    }
    return true;
}

std::string ocLevelInstJoinIds(const std::vector<u64>& ids) {
    std::string s;
    for (usize i = 0; i < ids.size(); ++i) {
        if (i) s += '/';
        s += ocLevelInstIdText(ids[i]);
    }
    return s;
}

std::string writeOcExtentLine(const f64 mn[3], const f64 mx[3]) {
    std::string s = "EXTENT";
    for (int i = 0; i < 3; ++i) { s += ' '; s += numAuto(mn[i]); }
    for (int i = 0; i < 3; ++i) { s += ' '; s += numAuto(mx[i]); }
    s += '\n';
    return s;
}

bool parseOcLevelInstanceLine(const std::vector<std::string_view>& t,
                              std::vector<OcLevelInstance>& out, i32& open) {
    if (t.empty()) return false;
    if (equalsCI(t[0], "LEVELINST")) {
        OcLevelInstance inst;
        if (t.size() >= 2) inst.asset = pctDecode(t[1]);
        for (usize i = 2; i < t.size(); ++i) {
            std::string_view kv;
            if (equalsCI(t[i], "pos") && i + 3 < t.size()) {
                inst.x = parseF64(t[i + 1]); inst.y = parseF64(t[i + 2]); inst.z = parseF64(t[i + 3]); i += 3;
            } else if (equalsCI(t[i], "rot") && i + 3 < t.size()) {
                inst.yaw = parseF64(t[i + 1]); inst.pitch = parseF64(t[i + 2]); inst.roll = parseF64(t[i + 3]); i += 3;
            } else if (equalsCI(t[i], "scale") && i + 3 < t.size()) {
                inst.sx = parseF64(t[i + 1], 1.0); inst.sy = parseF64(t[i + 2], 1.0); inst.sz = parseF64(t[i + 3], 1.0); i += 3;
            } else if (equalsCI(t[i], "name") && i + 1 < t.size()) {
                inst.name = pctDecode(t[++i]);
            } else if (keyValueOf(t[i], "id=", kv)) {
                inst.id = ocLevelInstIdParse(kv);
            } else if (equalsCI(t[i], "packed")) {
                inst.packed = true;
            } else if (equalsCI(t[i], "stream") && i + 1 < t.size()) {
                const i32 v = parseI32(t[++i], -1);
                inst.stream = v == 0 ? 0 : v > 0 ? 1 : -1;
            }
            // Unknown tokens are skipped.
        }
        open = static_cast<i32>(out.size());
        out.push_back(std::move(inst));
        return true;
    }
    if (equalsCI(t[0], "LIOVERRIDE")) {
        if (open >= 0 && static_cast<usize>(open) < out.size()) {
            OcPrefabOverride ov;
            if (parseOcOverrideBody(t, 1, ov)) {
                if (ov.op == OcOverrideOp::Set) out[static_cast<usize>(open)].overrides.push_back(std::move(ov));
                else AVER_WARN("[LevelInstance] LIOVERRIDE {} is reserved for per-component overrides and was ignored",
                               ov.op == OcOverrideOp::AddComponent ? "addcomp" : "rmcomp");
            }
        }
        return true;
    }
    if (equalsCI(t[0], "ENDLEVELINST")) {
        open = -1;
        return true;
    }
    return false;
}

void appendOcLevelInstances(std::string& out, const std::vector<OcLevelInstance>& v) {
    if (v.empty()) return;
    out += '\n';
    for (const OcLevelInstance& in : v) {
        out += "LEVELINST "; out += pctEncode(in.asset);
        out += " pos " + numAuto(in.x) + " " + numAuto(in.y) + " " + numAuto(in.z);
        out += " rot " + numAuto(in.yaw) + " " + numAuto(in.pitch) + " " + numAuto(in.roll);
        out += " scale " + numAuto(in.sx) + " " + numAuto(in.sy) + " " + numAuto(in.sz);
        if (!in.name.empty()) { out += " name "; out += pctEncode(in.name); }
        if (in.id) { out += " id="; out += ocLevelInstIdText(in.id); }
        if (in.packed) out += " packed";
        if (in.stream >= 0) { out += " stream "; out += in.stream ? '1' : '0'; }
        out += '\n';
        for (const OcPrefabOverride& ov : in.overrides) {
            out += "  LIOVERRIDE ";
            appendOcOverrideBody(out, ov);
            out += '\n';
        }
        out += "ENDLEVELINST\n";
    }
}

} // namespace aver::fmt
