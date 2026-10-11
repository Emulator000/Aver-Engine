// .ocmotion read, write and validation. See MotionAsset.hpp.
#include "aver/anim/MotionAsset.hpp"

#include "MotionJson.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace aver::anim {

namespace {

using mjson::esc;
using mjson::num;

std::string str(const fmt::JsonValue& v, std::string_view key) { return std::string(v[key].asString()); }

const char* accelName(SearchAccel a) {
    return a == SearchAccel::Linear ? "linear" : a == SearchAccel::KdTree ? "kdtree" : "auto";
}
SearchAccel accelFrom(std::string_view s) {
    return s == "linear" ? SearchAccel::Linear : s == "kdtree" ? SearchAccel::KdTree : SearchAccel::Auto;
}

std::string strList(const std::vector<std::string>& v) {
    std::string s = "[";
    for (usize i = 0; i < v.size(); ++i) s += (i ? ", " : "") + esc(v[i]);
    return s + "]";
}

std::string numList(const std::vector<f32>& v) {
    std::string s = "[";
    for (usize i = 0; i < v.size(); ++i) s += (i ? ", " : "") + num(v[i]);
    return s + "]";
}

void readStrings(const fmt::JsonValue& arr, std::vector<std::string>& out) {
    for (usize i = 0; i < arr.size(); ++i) out.emplace_back(arr[i].asString());
}

bool finite(f32 v) { return std::isfinite(v); }

// Strictly increasing in magnitude, finite, on the right side of zero, and short enough to store.
bool timesOk(const std::vector<f32>& t, bool future) {
    if (t.size() > kMaxTrajectorySamples) return false;
    f32 prev = 0.0f;
    for (const f32 v : t) {
        if (!finite(v) || (future ? v <= 0.0f : v >= 0.0f)) return false;
        if (std::fabs(v) <= std::fabs(prev)) return false;
        prev = v;
    }
    return true;
}

} // namespace

i32 MotionAsset::tagIndex(std::string_view n) const {
    for (usize i = 0; i < tags.size(); ++i)
        if (tags[i] == n) return static_cast<i32>(i);
    return -1;
}

bool MotionAsset::valid(std::string* why) const {
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    if (skeleton.empty()) return fail("no skeleton");
    if (clips.empty()) return fail("no clips");
    if (sampleRate < 5 || sampleRate > 120) return fail("sampleRate must be in [5, 120]");

    if (tags.size() > kMaxMotionTags) return fail("more than 32 tags");
    std::set<std::string> seen;
    for (const std::string& t : tags)
        if (t.empty() || !seen.insert(t).second) return fail("tag names must be unique and non-empty");

    bool anyGroup = false;
    for (const MotionBoneFeature& b : bones) {
        if (b.bone.empty()) return fail("a feature bone has no name");
        if (!finite(b.pos) || b.pos < 0.0f || !finite(b.vel) || b.vel < 0.0f) return fail("a bone weight is negative or not finite");
        if (b.pos > 0.0f || b.vel > 0.0f) anyGroup = true;
    }
    if (!finite(rootVelocity) || rootVelocity < 0.0f) return fail("rootVelocity weight is negative or not finite");
    if (!finite(futurePos) || futurePos < 0.0f || !finite(futureDir) || futureDir < 0.0f ||
        !finite(pastPos) || pastPos < 0.0f)
        return fail("a trajectory weight is negative or not finite");
    if (!timesOk(futureTimes, true)) return fail("future times must be positive, ascending, at most 8");
    if (!timesOk(pastTimes, false)) return fail("past times must be negative, ascending in magnitude, at most 8");
    if (rootVelocity > 0.0f) anyGroup = true;
    if ((futurePos > 0.0f || futureDir > 0.0f) && !futureTimes.empty()) anyGroup = true;
    if (pastPos > 0.0f && !pastTimes.empty()) anyGroup = true;
    if (!anyGroup) return fail("no feature group is enabled");

    auto tagsOk = [&](const std::vector<std::string>& v) {
        for (const std::string& t : v)
            if (tagIndex(t) < 0) return false;
        return true;
    };
    for (const MotionClipEntry& c : clips) {
        if (c.clip.empty()) return fail("a clip entry has no clip");
        if (!finite(c.start) || c.start < 0.0f) return fail("clip start must be >= 0");
        if (!finite(c.end) || (c.end >= 0.0f && c.end <= c.start)) return fail("clip end must be -1 or after start");
        if (!finite(c.bias)) return fail("clip bias is not finite");
        if (!tagsOk(c.tags)) return fail("clip '" + c.clip + "' uses an undeclared tag");
        for (const MotionTagRange& r : c.ranges) {
            if (!finite(r.start) || r.start < 0.0f) return fail("tag range start must be >= 0");
            if (!finite(r.end) || (r.end >= 0.0f && r.end <= r.start)) return fail("tag range end must be -1 or after start");
            if (!tagsOk(r.tags)) return fail("clip '" + c.clip + "' has a range with an undeclared tag");
        }
    }

    if (!finite(search.interval) || search.interval <= 0.0f) return fail("search interval must be > 0");
    if (!finite(search.switchMargin) || search.switchMargin < 0.0f) return fail("switchMargin must be >= 0");
    if (!finite(search.acceptCost)) return fail("acceptCost is not finite");
    if (!finite(search.continuityWindow) || search.continuityWindow < 0.0f) return fail("continuityWindow must be >= 0");
    if (search.kdMinRows < 1024) return fail("kdMinRows must be >= 1024");
    if (!finite(spring.velocityHalfLife) || spring.velocityHalfLife <= 0.0f ||
        !finite(spring.turnHalfLife) || spring.turnHalfLife <= 0.0f)
        return fail("spring half-lives must be > 0");
    if (!finite(spring.historySeconds) || spring.historySeconds <= 0.0f) return fail("historySeconds must be > 0");
    if (!finite(spring.faceSpeedMin) || spring.faceSpeedMin < 0.0f) return fail("faceSpeedMin must be >= 0");
    if (!finite(inertHalfLife) || inertHalfLife <= 0.0f) return fail("inertialization halfLife must be > 0");
    return true;
}

std::string writeMotionAsset(const MotionAsset& a) {
    std::string s = "{\n  \"format\": \"ocmotion\",\n  \"version\": " + std::to_string(kMotionAssetVersion) + ",\n";
    s += "  \"name\": " + esc(a.name) + ",\n  \"skeleton\": " + esc(a.skeleton) + ",\n  \"rootBone\": " + esc(a.rootBone) +
         ",\n  \"sampleRate\": " + std::to_string(a.sampleRate) + ",\n  \"tags\": " + strList(a.tags) + ",\n";

    s += "  \"features\": {\n    \"bones\": [";
    for (usize i = 0; i < a.bones.size(); ++i) {
        s += i ? ",\n      " : "\n      ";
        s += "{\"bone\": " + esc(a.bones[i].bone) + ", \"pos\": " + num(a.bones[i].pos) + ", \"vel\": " + num(a.bones[i].vel) + "}";
    }
    s += a.bones.empty() ? "],\n" : "\n    ],\n";
    s += "    \"rootVelocity\": " + num(a.rootVelocity) + ",\n";
    s += "    \"future\": {\"times\": " + numList(a.futureTimes) + ", \"pos\": " + num(a.futurePos) + ", \"dir\": " + num(a.futureDir) + "},\n";
    s += "    \"past\": {\"times\": " + numList(a.pastTimes) + ", \"pos\": " + num(a.pastPos) + "}\n  },\n";

    s += "  \"search\": {\"interval\": " + num(a.search.interval) + ", \"switchMargin\": " + num(a.search.switchMargin) +
         ", \"acceptCost\": " + num(a.search.acceptCost) + ", \"continuityWindow\": " + num(a.search.continuityWindow) +
         ", \"accel\": \"" + accelName(a.search.accel) + "\", \"kdMinRows\": " + std::to_string(a.search.kdMinRows) + "},\n";
    s += "  \"trajectory\": {\"velocityHalfLife\": " + num(a.spring.velocityHalfLife) + ", \"turnHalfLife\": " + num(a.spring.turnHalfLife) +
         ", \"historySeconds\": " + num(a.spring.historySeconds) + ", \"faceSpeedMin\": " + num(a.spring.faceSpeedMin) + "},\n";
    s += "  \"inertialization\": {\"halfLife\": " + num(a.inertHalfLife) + "},\n";

    s += "  \"clips\": [";
    for (usize i = 0; i < a.clips.size(); ++i) {
        const MotionClipEntry& c = a.clips[i];
        s += i ? ",\n    " : "\n    ";
        s += "{\"clip\": " + esc(c.clip) + ", \"start\": " + num(c.start) + ", \"end\": " + num(c.end) + ", \"loop\": ";
        s += c.loop < 0 ? "\"auto\"" : c.loop ? "true" : "false";
        s += ", \"bias\": " + num(c.bias) + std::string(", \"noSearch\": ") + (c.noSearch ? "true" : "false") +
             ", \"tags\": " + strList(c.tags) + ", \"ranges\": [";
        for (usize k = 0; k < c.ranges.size(); ++k) {
            const MotionTagRange& r = c.ranges[k];
            s += (k ? ", " : "") + std::string("{\"start\": ") + num(r.start) + ", \"end\": " + num(r.end) +
                 ", \"tags\": " + strList(r.tags) + std::string(", \"noSearch\": ") + (r.noSearch ? "true" : "false") + "}";
        }
        s += "]}";
    }
    s += a.clips.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return s;
}

bool parseMotionAsset(std::string_view text, MotionAsset& out, std::string* why) {
    fmt::JsonValue root;
    if (!fmt::parseJson(text, root, why)) return false;
    if (!mjson::header(root, "ocmotion", kMotionAssetVersion, why)) return false;

    MotionAsset a;
    a.name = str(root, "name");
    a.skeleton = str(root, "skeleton");
    a.rootBone = str(root, "rootBone");
    a.sampleRate = static_cast<u32>(std::max<i64>(0, root["sampleRate"].asInt(30)));
    readStrings(root["tags"], a.tags);

    const fmt::JsonValue& f = root["features"];
    const fmt::JsonValue& bs = f["bones"];
    for (usize i = 0; i < bs.size(); ++i) {
        MotionBoneFeature b;
        b.bone = str(bs[i], "bone");
        b.pos = bs[i]["pos"].asFloat(1.0f);
        b.vel = bs[i]["vel"].asFloat(1.0f);
        a.bones.push_back(std::move(b));
    }
    a.rootVelocity = f["rootVelocity"].asFloat(a.rootVelocity);
    auto times = [](const fmt::JsonValue& arr, std::vector<f32>& dst) {
        dst.clear();
        for (usize i = 0; i < arr.size(); ++i) dst.push_back(arr[i].asFloat());
    };
    if (f["future"].has("times")) times(f["future"]["times"], a.futureTimes);
    a.futurePos = f["future"]["pos"].asFloat(a.futurePos);
    a.futureDir = f["future"]["dir"].asFloat(a.futureDir);
    if (f["past"].has("times")) times(f["past"]["times"], a.pastTimes);
    a.pastPos = f["past"]["pos"].asFloat(a.pastPos);

    const fmt::JsonValue& se = root["search"];
    a.search.interval = se["interval"].asFloat(a.search.interval);
    a.search.switchMargin = se["switchMargin"].asFloat(a.search.switchMargin);
    a.search.acceptCost = se["acceptCost"].asFloat(a.search.acceptCost);
    a.search.continuityWindow = se["continuityWindow"].asFloat(a.search.continuityWindow);
    a.search.accel = accelFrom(se["accel"].asString("auto"));
    a.search.kdMinRows = static_cast<u32>(std::max<i64>(0, se["kdMinRows"].asInt(a.search.kdMinRows)));

    const fmt::JsonValue& tr = root["trajectory"];
    a.spring.velocityHalfLife = tr["velocityHalfLife"].asFloat(a.spring.velocityHalfLife);
    a.spring.turnHalfLife = tr["turnHalfLife"].asFloat(a.spring.turnHalfLife);
    a.spring.historySeconds = tr["historySeconds"].asFloat(a.spring.historySeconds);
    a.spring.faceSpeedMin = tr["faceSpeedMin"].asFloat(a.spring.faceSpeedMin);
    a.inertHalfLife = root["inertialization"]["halfLife"].asFloat(a.inertHalfLife);

    const fmt::JsonValue& cs = root["clips"];
    for (usize i = 0; i < cs.size(); ++i) {
        const fmt::JsonValue& v = cs[i];
        MotionClipEntry c;
        c.clip = str(v, "clip");
        c.start = v["start"].asFloat(0.0f);
        c.end = v["end"].asFloat(-1.0f);
        const fmt::JsonValue& lp = v["loop"];
        c.loop = lp.isBool() ? (lp.asBool() ? 1 : 0) : -1;
        c.bias = v["bias"].asFloat(0.0f);
        c.noSearch = v["noSearch"].asBool(false);
        readStrings(v["tags"], c.tags);
        const fmt::JsonValue& rs = v["ranges"];
        for (usize k = 0; k < rs.size(); ++k) {
            MotionTagRange r;
            r.start = rs[k]["start"].asFloat(0.0f);
            r.end = rs[k]["end"].asFloat(-1.0f);
            readStrings(rs[k]["tags"], r.tags);
            r.noSearch = rs[k]["noSearch"].asBool(false);
            c.ranges.push_back(std::move(r));
        }
        a.clips.push_back(std::move(c));
    }
    out = std::move(a);
    return true;
}

bool saveMotionAsset(const std::string& path, const MotionAsset& a, std::string* why) {
    return mjson::writeFile(path, writeMotionAsset(a), why);
}

bool loadMotionAsset(const std::string& path, MotionAsset& out, std::string* why) {
    std::string text;
    return mjson::readFile(path, text, why) && parseMotionAsset(text, out, why);
}

} // namespace aver::anim
