// .ocikrig read and write. See IkRigAsset.hpp.
#include "aver/anim/IkRigAsset.hpp"

#include "AnimJson.hpp"

#include <cmath>
#include <set>

namespace aver::anim {

using namespace ajson;

namespace {

const char* solverName(IkSolverKind k) {
    switch (k) {
        case IkSolverKind::TwoBone: return "twobone";
        case IkSolverKind::Ccd:     return "ccd";
        case IkSolverKind::Fabrik:  break;
    }
    return "fabrik";
}

const char* poleName(IkPoleMode m) {
    switch (m) {
        case IkPoleMode::Bone:   return "bone";
        case IkPoleMode::Vector: return "vector";
        case IkPoleMode::Animated: break;
    }
    return "animated";
}

std::string vec3s(const Vec3& v) { return "[" + num(v.x) + ", " + num(v.y) + ", " + num(v.z) + "]"; }

bool readVec3(const fmt::JsonValue& v, Vec3& out) {
    if (!v.isArray() || v.size() < 3) return false;
    out = Vec3{v[0].asFloat(), v[1].asFloat(), v[2].asFloat()};
    return true;
}

bool fail(std::string* why, const std::string& s) { if (why) *why = s; return false; }
bool finiteNonNeg(f32 v) { return std::isfinite(v) && v >= 0.0f; }

} // namespace

bool IkRigAsset::valid(std::string* why) const {
    std::set<std::string> chainNames, goalNames;
    if (!body.goal.empty()) goalNames.insert(body.goal);
    if (!finiteNonNeg(body.maxOffsetCm)) return fail(why, "body.maxOffsetCm must be finite and >= 0");
    for (const IkRigChainDef& c : chains) {
        if (c.name.empty()) return fail(why, "a chain has no name");
        if (!chainNames.insert(c.name).second) return fail(why, "duplicate chain name '" + c.name + "'");
        if (c.start.empty() || c.end.empty()) return fail(why, "chain '" + c.name + "' needs start and end bones");
        if (!c.goal.empty() && !goalNames.insert(c.goal).second)
            return fail(why, "duplicate goal name '" + c.goal + "'");
        if (c.priority < 0) return fail(why, "chain '" + c.name + "' has a negative priority");
        if (c.iterations < 1 || c.iterations > 64) return fail(why, "chain '" + c.name + "' iterations must be 1..64");
        if (!finiteNonNeg(c.toleranceCm) || !finiteNonNeg(c.maxStepDeg))
            return fail(why, "chain '" + c.name + "' has a bad tolerance or step");
        if (c.pole.mode == IkPoleMode::Bone && c.pole.bone.empty())
            return fail(why, "chain '" + c.name + "' pole mode 'bone' needs a bone");
        if (!std::isfinite(c.pole.dirModel.x + c.pole.dirModel.y + c.pole.dirModel.z))
            return fail(why, "chain '" + c.name + "' pole direction is not finite");
    }
    for (const IkRigLimitDef& l : limits) {
        if (l.bone.empty()) return fail(why, "a limit has no bone");
        for (const f32 a : {l.swing1, l.swing2, l.twist})
            if (!std::isfinite(a) || a < 0.0f || a > 180.0f)
                return fail(why, "limit angles must be in [0,180] (" + l.bone + ")");
    }
    std::set<std::string> footChains;
    for (const IkRigFootDef& f : feet) {
        if (!chainNames.count(f.chain)) return fail(why, "foot names unknown chain '" + f.chain + "'");
        if (!footChains.insert(f.chain).second) return fail(why, "two feet on chain '" + f.chain + "'");
        if (!std::isfinite(f.soleOffsetCm)) return fail(why, "foot soleOffsetCm is not finite");
        if (!(f.probeUpCm > 0.0f) || !(f.probeDownCm > 0.0f) || !std::isfinite(f.probeUpCm + f.probeDownCm))
            return fail(why, "foot probe distances must be > 0");
        if (!finiteNonNeg(f.plantLowCm) || !finiteNonNeg(f.plantHighCm) || !finiteNonNeg(f.maxAlignDeg))
            return fail(why, "foot has a negative plant range or align angle");
    }
    if (!finiteNonNeg(foot.smoothing) || !finiteNonNeg(foot.maxPelvisDropCm) || !finiteNonNeg(foot.maxFootOffsetCm))
        return fail(why, "foot settings must be finite and >= 0");
    return true;
}

std::string writeIkRig(const IkRigAsset& a) {
    std::string s = "{\n  \"format\": \"ocikrig\",\n  \"version\": " + std::to_string(kIkRigVersion) + ",\n";
    s += "  \"name\": " + esc(a.name) + ",\n  \"skeleton\": " + esc(a.skeleton) + ",\n";
    s += "  \"body\": {\"bone\": " + esc(a.body.bone) + ", \"goal\": " + esc(a.body.goal) +
         ", \"maxOffsetCm\": " + num(a.body.maxOffsetCm) + "},\n";
    s += "  \"chains\": [";
    for (usize i = 0; i < a.chains.size(); ++i) {
        const IkRigChainDef& c = a.chains[i];
        s += i ? ",\n    " : "\n    ";
        s += "{\"name\": " + esc(c.name) + ", \"start\": " + esc(c.start) + ", \"end\": " + esc(c.end) +
             ", \"solver\": \"" + solverName(c.solver) + "\", \"priority\": " + std::to_string(c.priority) +
             ", \"goal\": " + esc(c.goal) + ", \"iterations\": " + std::to_string(c.iterations) +
             ", \"toleranceCm\": " + num(c.toleranceCm) + ", \"maxStepDeg\": " + num(c.maxStepDeg) +
             ", \"limits\": " + (c.limits ? "true" : "false") + ", \"tipRotation\": " + (c.tipRotation ? "true" : "false") +
             ", \"pole\": {\"mode\": \"" + poleName(c.pole.mode) + "\"";
        if (c.pole.mode == IkPoleMode::Bone) s += ", \"bone\": " + esc(c.pole.bone);
        if (c.pole.mode == IkPoleMode::Vector) s += ", \"dirModel\": " + vec3s(c.pole.dirModel);
        s += "}}";
    }
    s += a.chains.empty() ? "],\n" : "\n  ],\n";
    s += "  \"limits\": [";
    for (usize i = 0; i < a.limits.size(); ++i) {
        const IkRigLimitDef& l = a.limits[i];
        s += i ? ",\n    " : "\n    ";
        s += "{\"bone\": " + esc(l.bone) + ", \"swing1\": " + num(l.swing1) + ", \"swing2\": " + num(l.swing2) +
             ", \"twist\": " + num(l.twist);
        if (l.hasAxis) s += ", \"axis\": " + vec3s(l.axis);
        s += "}";
    }
    s += a.limits.empty() ? "],\n" : "\n  ],\n";
    s += "  \"feet\": [";
    for (usize i = 0; i < a.feet.size(); ++i) {
        const IkRigFootDef& f = a.feet[i];
        s += i ? ",\n    " : "\n    ";
        s += "{\"chain\": " + esc(f.chain) + ", \"toe\": " + esc(f.toe) + ", \"soleOffsetCm\": " + num(f.soleOffsetCm) +
             ", \"plantCurve\": " + esc(f.plantCurve) + ", \"plantLowCm\": " + num(f.plantLowCm) +
             ", \"plantHighCm\": " + num(f.plantHighCm) + ", \"probeUpCm\": " + num(f.probeUpCm) +
             ", \"probeDownCm\": " + num(f.probeDownCm) + ", \"alignToNormal\": " + (f.alignToNormal ? "true" : "false") +
             ", \"maxAlignDeg\": " + num(f.maxAlignDeg) + "}";
    }
    s += a.feet.empty() ? "],\n" : "\n  ],\n";
    s += std::string("  \"foot\": {\"pelvisMode\": \"") + (a.foot.pelvisMode == IkPelvisMode::None ? "none" : "lowest") +
         "\", \"smoothing\": " + num(a.foot.smoothing) + ", \"maxPelvisDropCm\": " + num(a.foot.maxPelvisDropCm) +
         ", \"maxFootOffsetCm\": " + num(a.foot.maxFootOffsetCm) + ", \"layerMask\": " + std::to_string(a.foot.layerMask) +
         "}\n}\n";
    return s;
}

bool parseIkRig(std::string_view json, IkRigAsset& out, std::string* why) {
    fmt::JsonValue root;
    if (!fmt::parseJson(json, root, why)) return false;
    if (!header(root, "ocikrig", kIkRigVersion, why)) return false;
    IkRigAsset a;
    a.name = str(root, "name");
    a.skeleton = str(root, "skeleton");
    const fmt::JsonValue& body = root["body"];
    a.body.bone = str(body, "bone");
    a.body.goal = str(body, "goal");
    a.body.maxOffsetCm = body["maxOffsetCm"].asFloat(40.0f);

    const fmt::JsonValue& chains = root["chains"];
    for (usize i = 0; i < chains.size(); ++i) {
        const fmt::JsonValue& v = chains[i];
        IkRigChainDef c;
        c.name = str(v, "name");
        c.start = str(v, "start");
        c.end = str(v, "end");
        const std::string_view sv = v["solver"].asString("fabrik");
        c.solver = sv == "twobone" ? IkSolverKind::TwoBone : sv == "ccd" ? IkSolverKind::Ccd : IkSolverKind::Fabrik;
        c.priority = static_cast<i32>(v["priority"].asInt(0));
        c.goal = str(v, "goal");
        // Absent keys take the solver's asset defaults: FABRIK 10 iterations / 0.1 cm, CCD 12 / 0.5 cm.
        const bool ccd = c.solver == IkSolverKind::Ccd;
        c.iterations = static_cast<u32>(v["iterations"].asInt(ccd ? 12 : 10));
        c.toleranceCm = v["toleranceCm"].asFloat(ccd ? 0.5f : 0.1f);
        c.maxStepDeg = v["maxStepDeg"].asFloat(30.0f);
        c.limits = v["limits"].asBool(false);
        c.tipRotation = v["tipRotation"].asBool(false);
        const fmt::JsonValue& pole = v["pole"];
        const std::string_view pm = pole["mode"].asString("animated");
        c.pole.mode = pm == "bone" ? IkPoleMode::Bone : pm == "vector" ? IkPoleMode::Vector : IkPoleMode::Animated;
        c.pole.bone = str(pole, "bone");
        readVec3(pole["dirModel"], c.pole.dirModel);
        a.chains.push_back(std::move(c));
    }

    const fmt::JsonValue& limits = root["limits"];
    for (usize i = 0; i < limits.size(); ++i) {
        const fmt::JsonValue& v = limits[i];
        IkRigLimitDef l;
        l.bone = str(v, "bone");
        l.swing1 = v["swing1"].asFloat(180.0f);
        l.swing2 = v["swing2"].asFloat(180.0f);
        l.twist = v["twist"].asFloat(180.0f);
        l.hasAxis = readVec3(v["axis"], l.axis);
        a.limits.push_back(std::move(l));
    }

    const fmt::JsonValue& feet = root["feet"];
    for (usize i = 0; i < feet.size(); ++i) {
        const fmt::JsonValue& v = feet[i];
        IkRigFootDef f;
        f.chain = str(v, "chain");
        f.toe = str(v, "toe");
        f.soleOffsetCm = v["soleOffsetCm"].asFloat(-1.0f);
        f.plantCurve = str(v, "plantCurve");
        f.plantLowCm = v["plantLowCm"].asFloat(2.0f);
        f.plantHighCm = v["plantHighCm"].asFloat(10.0f);
        f.probeUpCm = v["probeUpCm"].asFloat(50.0f);
        f.probeDownCm = v["probeDownCm"].asFloat(60.0f);
        f.alignToNormal = v["alignToNormal"].asBool(true);
        f.maxAlignDeg = v["maxAlignDeg"].asFloat(30.0f);
        a.feet.push_back(std::move(f));
    }

    const fmt::JsonValue& foot = root["foot"];
    a.foot.pelvisMode = foot["pelvisMode"].asString("lowest") == "none" ? IkPelvisMode::None : IkPelvisMode::Lowest;
    a.foot.smoothing = foot["smoothing"].asFloat(14.0f);
    a.foot.maxPelvisDropCm = foot["maxPelvisDropCm"].asFloat(40.0f);
    a.foot.maxFootOffsetCm = foot["maxFootOffsetCm"].asFloat(45.0f);
    a.foot.layerMask = foot.has("layerMask") ? static_cast<u32>(static_cast<u64>(foot["layerMask"].asDouble()))
                                             : 0xFFFFFFFFu;
    out = std::move(a);
    return true;
}

bool saveIkRig(const std::string& path, const IkRigAsset& a, std::string* why) {
    return writeFile(path, writeIkRig(a), why);
}

bool loadIkRig(const std::string& path, IkRigAsset& out, std::string* why) {
    std::string text;
    return readFile(path, text, why) && parseIkRig(text, out, why);
}

} // namespace aver::anim
