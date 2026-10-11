// .ocretarget read and write. See RetargetAsset.hpp.
#include "aver/anim/RetargetAsset.hpp"

#include "AnimJson.hpp"

#include <cmath>
#include <set>

namespace aver::anim {

using namespace ajson;

namespace {

const char* modeName(RtgMode m) { return m == RtgMode::Ik ? "ik" : m == RtgMode::None ? "none" : "fk"; }
const char* anchorName(RtgAnchor a) { return a == RtgAnchor::Root ? "root" : a == RtgAnchor::Start ? "start" : "auto"; }

std::string scaleS(const RtgScale& s) { return s.autoScale ? "\"auto\"" : num(s.value); }

RtgScale readScale(const fmt::JsonValue& v) {
    RtgScale s;
    if (v.isNumber()) { s.autoScale = false; s.value = v.asFloat(1.0f); }
    return s;
}

bool fail(std::string* why, const std::string& s) { if (why) *why = s; return false; }

i32 boneNamed(const fmt::OcSkeleton& s, const std::string& n) {
    for (usize i = 0; i < s.bones.size(); ++i)
        if (s.bones[i].name == n) return static_cast<i32>(i);
    return -1;
}

bool isAncestorOrSelf(const fmt::OcSkeleton& s, i32 anc, i32 b) {
    for (usize guard = 0; b >= 0 && guard <= s.bones.size(); ++guard) {
        if (b == anc) return true;
        b = s.bones[static_cast<usize>(b)].parent;
    }
    return false;
}

} // namespace

bool RetargetAsset::valid(std::string* why) const {
    std::set<std::string> names;
    for (const RetargetChainDef& c : chains) {
        if (c.name.empty()) return fail(why, "a chain has no name");
        if (!names.insert(c.name).second) return fail(why, "duplicate chain name '" + c.name + "'");
        if (c.srcStart.empty() || c.srcEnd.empty() || c.dstStart.empty() || c.dstEnd.empty())
            return fail(why, "chain '" + c.name + "' needs source and target bones");
        if (!c.scale.autoScale && !(std::isfinite(c.scale.value) && c.scale.value > 0.0f))
            return fail(why, "chain '" + c.name + "' scale must be positive");
    }
    for (const RtgScale* s : {&root.scaleH, &root.scaleV, &rootMotion.scale})
        if (!s->autoScale && !std::isfinite(s->value)) return fail(why, "a root scale is not finite");
    if (!std::isfinite(root.offsetCm.x + root.offsetCm.y + root.offsetCm.z)) return fail(why, "root offset is not finite");
    return true;
}

bool RetargetAsset::valid(const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst, std::string* why) const {
    if (!valid(why)) return false;
    auto need = [&](const fmt::OcSkeleton& s, const std::string& n, const char* side, i32& out) {
        out = boneNamed(s, n);
        return out >= 0 ? true : fail(why, std::string(side) + " bone '" + n + "' not found");
    };
    i32 b = 0;
    if (!root.srcBone.empty() && !need(src, root.srcBone, "source", b)) return false;
    i32 dstRoot = -1;
    if (!root.dstBone.empty() && !need(dst, root.dstBone, "target", dstRoot)) return false;
    if (!rootMotion.srcBone.empty() != !rootMotion.dstBone.empty()) return fail(why, "rootMotion needs both bones or neither");
    if (!rootMotion.srcBone.empty()) {
        if (!need(src, rootMotion.srcBone, "source", b) || !need(dst, rootMotion.dstBone, "target", b)) return false;
    }

    std::vector<u8> usedS(src.bones.size(), 0), usedD(dst.bones.size(), 0);
    for (const RetargetChainDef& c : chains) {
        i32 ss = 0, se = 0, ds = 0, de = 0;
        if (!need(src, c.srcStart, "source", ss) || !need(src, c.srcEnd, "source", se) ||
            !need(dst, c.dstStart, "target", ds) || !need(dst, c.dstEnd, "target", de)) return false;
        if (!isAncestorOrSelf(src, ss, se)) return fail(why, "chain '" + c.name + "': '" + c.srcStart + "' is not an ancestor of '" + c.srcEnd + "'");
        if (!isAncestorOrSelf(dst, ds, de)) return fail(why, "chain '" + c.name + "': '" + c.dstStart + "' is not an ancestor of '" + c.dstEnd + "'");
        u32 ns = 0, nd = 0;
        for (i32 k = se; k >= 0; k = src.bones[static_cast<usize>(k)].parent) {
            if (usedS[static_cast<usize>(k)]) return fail(why, "source bone '" + src.bones[static_cast<usize>(k)].name + "' is in two chains");
            usedS[static_cast<usize>(k)] = 1;
            ++ns;
            if (k == ss) break;
        }
        for (i32 k = de; k >= 0; k = dst.bones[static_cast<usize>(k)].parent) {
            if (usedD[static_cast<usize>(k)]) return fail(why, "target bone '" + dst.bones[static_cast<usize>(k)].name + "' is in two chains");
            usedD[static_cast<usize>(k)] = 1;
            ++nd;
            if (k == ds) break;
        }
        if (c.rotation == RtgRotation::OneToOne && ns != nd)
            return fail(why, "chain '" + c.name + "' is oneToOne but has " + std::to_string(ns) + " source and " + std::to_string(nd) + " target bones");
        if (dstRoot >= 0 && !isAncestorOrSelf(dst, dstRoot, ds))
            return fail(why, "chain '" + c.name + "' is not under the target root bone '" + root.dstBone + "'");
    }
    return true;
}

std::string writeRetarget(const RetargetAsset& a) {
    std::string s = "{\n  \"format\": \"ocretarget\",\n  \"version\": " + std::to_string(kRetargetVersion) + ",\n";
    s += "  \"name\": " + esc(a.name) + ",\n  \"source\": " + esc(a.source) + ",\n  \"target\": " + esc(a.target) + ",\n";
    s += "  \"root\": {\"srcBone\": " + esc(a.root.srcBone) + ", \"dstBone\": " + esc(a.root.dstBone) +
         ", \"scaleH\": " + scaleS(a.root.scaleH) + ", \"scaleV\": " + scaleS(a.root.scaleV) +
         ", \"offsetCm\": [" + num(a.root.offsetCm.x) + ", " + num(a.root.offsetCm.y) + ", " + num(a.root.offsetCm.z) + "]},\n";
    s += "  \"rootMotion\": {\"srcBone\": " + esc(a.rootMotion.srcBone) + ", \"dstBone\": " + esc(a.rootMotion.dstBone) +
         ", \"scale\": " + scaleS(a.rootMotion.scale) + "},\n";
    s += std::string("  \"unmapped\": \"") + (a.unmapped == RtgUnmapped::Inherit ? "inherit" : "rest") + "\",\n";
    s += "  \"chains\": [";
    for (usize i = 0; i < a.chains.size(); ++i) {
        const RetargetChainDef& c = a.chains[i];
        s += i ? ",\n    " : "\n    ";
        s += "{\"name\": " + esc(c.name) + ", \"src\": [" + esc(c.srcStart) + ", " + esc(c.srcEnd) + "], \"dst\": [" +
             esc(c.dstStart) + ", " + esc(c.dstEnd) + "], \"mode\": \"" + modeName(c.mode) + "\", \"rotation\": \"" +
             (c.rotation == RtgRotation::OneToOne ? "oneToOne" : "interpolate") + "\", \"translation\": \"" +
             (c.translation == RtgTranslation::Scaled ? "scaled" : "none") + "\", \"anchor\": \"" + anchorName(c.anchor) +
             "\", \"scale\": " + scaleS(c.scale) + ", \"pole\": \"" + (c.pole == RtgPole::None ? "none" : "fk") +
             "\", \"tipRotation\": " + (c.tipRotation ? "true" : "false") + "}";
    }
    s += a.chains.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return s;
}

bool parseRetarget(std::string_view json, RetargetAsset& out, std::string* why) {
    fmt::JsonValue root;
    if (!fmt::parseJson(json, root, why)) return false;
    if (!header(root, "ocretarget", kRetargetVersion, why)) return false;
    RetargetAsset a;
    a.name = str(root, "name");
    a.source = str(root, "source");
    a.target = str(root, "target");
    const fmt::JsonValue& r = root["root"];
    a.root.srcBone = str(r, "srcBone");
    a.root.dstBone = str(r, "dstBone");
    a.root.scaleH = readScale(r["scaleH"]);
    a.root.scaleV = readScale(r["scaleV"]);
    const fmt::JsonValue& off = r["offsetCm"];
    if (off.isArray() && off.size() >= 3) a.root.offsetCm = Vec3{off[0].asFloat(), off[1].asFloat(), off[2].asFloat()};
    const fmt::JsonValue& rm = root["rootMotion"];
    a.rootMotion.srcBone = str(rm, "srcBone");
    a.rootMotion.dstBone = str(rm, "dstBone");
    a.rootMotion.scale = readScale(rm["scale"]);
    a.unmapped = root["unmapped"].asString("rest") == "inherit" ? RtgUnmapped::Inherit : RtgUnmapped::Rest;
    const fmt::JsonValue& chains = root["chains"];
    for (usize i = 0; i < chains.size(); ++i) {
        const fmt::JsonValue& v = chains[i];
        RetargetChainDef c;
        c.name = str(v, "name");
        c.srcStart = std::string(v["src"][0].asString());
        c.srcEnd = std::string(v["src"][1].asString());
        c.dstStart = std::string(v["dst"][0].asString());
        c.dstEnd = std::string(v["dst"][1].asString());
        const std::string_view m = v["mode"].asString("fk");
        c.mode = m == "ik" ? RtgMode::Ik : m == "none" ? RtgMode::None : RtgMode::Fk;
        c.rotation = v["rotation"].asString("interpolate") == "oneToOne" ? RtgRotation::OneToOne : RtgRotation::Interpolate;
        c.translation = v["translation"].asString("none") == "scaled" ? RtgTranslation::Scaled : RtgTranslation::None;
        const std::string_view an = v["anchor"].asString("auto");
        c.anchor = an == "root" ? RtgAnchor::Root : an == "start" ? RtgAnchor::Start : RtgAnchor::Auto;
        c.scale = readScale(v["scale"]);
        c.pole = v["pole"].asString("fk") == "none" ? RtgPole::None : RtgPole::Fk;
        c.tipRotation = v["tipRotation"].asBool(false);
        a.chains.push_back(std::move(c));
    }
    out = std::move(a);
    return true;
}

bool saveRetarget(const std::string& path, const RetargetAsset& a, std::string* why) {
    return writeFile(path, writeRetarget(a), why);
}

bool loadRetarget(const std::string& path, RetargetAsset& out, std::string* why) {
    std::string text;
    return readFile(path, text, why) && parseRetarget(text, out, why);
}

} // namespace aver::anim
