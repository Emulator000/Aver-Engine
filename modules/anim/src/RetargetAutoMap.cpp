// Auto-mapping chains between two skeletons by bone-name logic: normalise names, extract a side, a role and an
// ordinal, build chains from role templates, and fall back to structural chains for what the table does not
// understand. Pure string and hierarchy logic; the result is a DRAFT (RetargetAsset.hpp).
#include "aver/anim/RetargetAsset.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace aver::anim {

namespace {

enum class Role : u8 { None, Pelvis, Spine, Neck, Head, Clavicle, UpperArm, LowerArm, Hand, Thigh, Calf, Foot, Ball, Finger, LegBare };

struct Info {
    Role role = Role::None;
    int side = 0;           // 0 none, 1 left, 2 right
    int ordinal = -1;
    int finger = -1;        // 0 thumb .. 4 pinky
    std::string norm;       // all tokens, lowercased and joined: the exact-match key
};

std::string lowerS(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

std::string stripPrefix(const std::string& name) {
    static const char* prefixes[] = {"mixamorig:", "mixamorig_", "mixamorig", "bip001 ", "bip01 ", "bip001_", "bip01_",
                                     "cc_base_", "def-", "def_", "def.", "org-", "b_"};
    std::string cur = name;
    for (int pass = 0; pass < 3; ++pass) {
        const std::string low = lowerS(cur);
        bool cut = false;
        for (const char* p : prefixes) {
            const usize n = std::char_traits<char>::length(p);
            if (low.size() > n && startsWith(low, p)) { cur = cur.substr(n); cut = true; break; }
        }
        if (!cut) break;
    }
    return cur;
}

// Splits on separators, camelCase and letter/digit boundaries; lowercases.
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] { if (!cur.empty()) { out.push_back(lowerS(cur)); cur.clear(); } };
    for (usize i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (!std::isalnum(c)) { flush(); continue; }
        if (!cur.empty()) {
            const unsigned char p = static_cast<unsigned char>(cur.back());
            const bool digitChange = std::isdigit(c) != std::isdigit(p);
            const bool camel = std::islower(p) && std::isupper(c);
            const bool acronym = std::isupper(p) && std::isupper(c) && i + 1 < s.size() && std::islower(static_cast<unsigned char>(s[i + 1]));
            if (digitChange || camel || acronym) flush();
        }
        cur.push_back(static_cast<char>(c));
    }
    flush();
    // "leftarm" / "rightfoot" glued: split a leading side word.
    std::vector<std::string> fixed;
    for (const std::string& t : out) {
        if (t.size() > 4 && startsWith(t, "left")) { fixed.push_back("left"); fixed.push_back(t.substr(4)); }
        else if (t.size() > 5 && startsWith(t, "right")) { fixed.push_back("right"); fixed.push_back(t.substr(5)); }
        else fixed.push_back(t);
    }
    return fixed;
}

Info classify(const std::string& boneName) {
    Info info;
    std::vector<std::string> tok = tokenize(stripPrefix(boneName));
    for (usize i = 0; i < tok.size(); ++i) info.norm += (i ? "_" : "") + tok[i];

    std::vector<std::string> words;
    for (const std::string& t : tok) {
        if (t == "l" || t == "left")  { info.side = 1; continue; }
        if (t == "r" || t == "right") { info.side = 2; continue; }
        if (std::isdigit(static_cast<unsigned char>(t[0]))) { info.ordinal = std::atoi(t.c_str()); continue; }
        words.push_back(t);
    }
    auto has = [&](const char* w) { return std::find(words.begin(), words.end(), w) != words.end(); };

    static const char* fingers[] = {"thumb", "index", "middle", "ring", "pinky"};
    for (int f = 0; f < 5; ++f)
        if (has(fingers[f])) { info.role = Role::Finger; info.finger = f; return info; }
    if (has("little")) { info.role = Role::Finger; info.finger = 4; return info; }
    if (has("twist") || has("roll") || has("end") || has("nub") || has("ik") || has("helper") || has("corrective")) return info;

    if (has("pelvis") || has("hips") || has("hip")) info.role = Role::Pelvis;
    else if (has("forearm") || has("lowerarm") || has("elbow") || (has("lower") && has("arm")) || (has("fore") && has("arm"))) info.role = Role::LowerArm;
    else if (has("upperarm") || has("uparm") || (has("upper") && has("arm"))) info.role = Role::UpperArm;
    else if (has("thigh") || has("upleg") || has("upperleg") || (has("up") && has("leg")) || (has("upper") && has("leg"))) info.role = Role::Thigh;
    else if (has("calf") || has("shin") || has("lowerleg") || (has("lower") && has("leg"))) info.role = Role::Calf;
    else if (has("clavicle") || has("collar") || has("shoulder")) info.role = Role::Clavicle;
    else if (has("hand") || has("wrist")) info.role = Role::Hand;
    else if (has("foot") || has("ankle")) info.role = Role::Foot;
    else if (has("toe") || has("toes") || has("ball") || has("toebase")) info.role = Role::Ball;
    else if (has("spine") || has("chest") || has("torso") || has("abdomen")) info.role = Role::Spine;
    else if (has("neck")) info.role = Role::Neck;
    else if (has("head")) info.role = Role::Head;
    else if (has("arm")) info.role = Role::UpperArm;
    else if (has("leg")) info.role = Role::LegBare;
    return info;
}

struct Rig {
    const fmt::OcSkeleton& s;
    std::vector<Info> info;
    std::vector<Vec3> restPos;

    explicit Rig(const fmt::OcSkeleton& skel) : s(skel) {
        for (const fmt::OcBone& b : s.bones) info.push_back(classify(b.name));
        bool anyThigh = false, anyUpper = false;
        for (const Info& i : info) { anyThigh = anyThigh || i.role == Role::Thigh; anyUpper = anyUpper || i.role == Role::UpperArm; }
        for (usize k = 0; k < info.size(); ++k) {
            if (info[k].role == Role::LegBare) info[k].role = anyThigh ? Role::Calf : Role::Thigh;
            // A shoulder is a clavicle only when there is an upper arm to hang off it.
            if (info[k].role == Role::Clavicle && !anyUpper && lowerS(s.bones[k].name).find("shoulder") != std::string::npos)
                info[k].role = Role::UpperArm;
        }
        restPos.resize(s.bones.size());
        Pose p;
        restPose(s, p);
        std::vector<Mat4> m;
        poseToModel(s, p, m);
        for (usize i = 0; i < m.size(); ++i) restPos[i] = Vec3{m[i].m[3][0], m[i].m[3][1], m[i].m[3][2]};
    }

    std::vector<u32> bonesOf(Role r, int side, int finger = -1) const {
        std::vector<u32> out;
        for (usize i = 0; i < info.size(); ++i)
            if (info[i].role == r && info[i].side == side && (finger < 0 || info[i].finger == finger)) out.push_back(static_cast<u32>(i));
        return out;
    }
    // First and last of a role's bones walking down the hierarchy from the lowest index.
    bool span(const std::vector<u32>& list, u32& first, u32& last) const {
        if (list.empty()) return false;
        first = list.front();
        last = first;
        for (bool moved = true; moved;) {
            moved = false;
            for (const u32 b : list)
                if (s.bones[b].parent == static_cast<i32>(last)) { last = b; moved = true; break; }
        }
        return true;
    }
    bool ancestorOrSelf(i32 anc, i32 b) const {
        for (usize guard = 0; b >= 0 && guard <= s.bones.size(); ++guard) {
            if (b == anc) return true;
            b = s.bones[static_cast<usize>(b)].parent;
        }
        return false;
    }
    int depthOf(u32 b) const {
        int d = 0;
        for (i32 k = s.bones[b].parent; k >= 0; k = s.bones[static_cast<usize>(k)].parent) ++d;
        return d;
    }
};

struct Ends { u32 a = 0, b = 0; bool ok = false; };

struct Template {
    std::string name;
    Ends src, dst;
    bool ik = false;
};

}  // namespace

AutoMapResult autoMapChains(const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst) {
    AutoMapResult R;
    Rig S(src), D(dst);
    auto conf = [&](u32 sa, u32 sb, u32 da, u32 db) {
        return (S.info[sa].norm == D.info[da].norm && S.info[sb].norm == D.info[db].norm) ? RtgConfidence::Exact : RtgConfidence::Synonym;
    };
    auto addChain = [&](const std::string& name, u32 sa, u32 sb, u32 da, u32 db, bool ik) {
        RetargetChainDef c;
        c.name = name;
        c.srcStart = src.bones[sa].name; c.srcEnd = src.bones[sb].name;
        c.dstStart = dst.bones[da].name; c.dstEnd = dst.bones[db].name;
        c.mode = ik ? RtgMode::Ik : RtgMode::Fk;
        R.asset.chains.push_back(std::move(c));
        R.confidence.push_back(conf(sa, sb, da, db));
    };
    auto pair = [&](const std::string& name, Role role, int side, int finger, bool single, bool ik) {
        u32 sa = 0, sb = 0, da = 0, db = 0;
        if (!S.span(S.bonesOf(role, side, finger), sa, sb) || !D.span(D.bonesOf(role, side, finger), da, db)) return;
        if (single) { sb = sa; db = da; }
        addChain(name, sa, sb, da, db, ik);
    };

    pair("Pelvis", Role::Pelvis, 0, -1, true, false);
    pair("Spine", Role::Spine, 0, -1, false, false);
    pair("Neck", Role::Neck, 0, -1, false, false);
    pair("Head", Role::Head, 0, -1, true, false);
    for (int side = 1; side <= 2; ++side) {
        const std::string sn = side == 1 ? "Left" : "Right";
        pair(sn + "Clavicle", Role::Clavicle, side, -1, true, false);
        // Arms and legs run from the first role bone down to the last (upper arm to hand, thigh to foot).
        auto limb = [&](const std::string& name, Role top, Role end, bool ik) {
            u32 st = 0, sx = 0, dt = 0, dx = 0;
            if (!S.span(S.bonesOf(top, side), st, sx) || !D.span(D.bonesOf(top, side), dt, dx)) return;
            const std::vector<u32> sEnd = S.bonesOf(end, side), dEnd = D.bonesOf(end, side);
            if (sEnd.empty() || dEnd.empty()) return;
            const u32 sbot = sEnd.front(), dbot = dEnd.front();
            if (!S.ancestorOrSelf(static_cast<i32>(st), static_cast<i32>(sbot)) || !D.ancestorOrSelf(static_cast<i32>(dt), static_cast<i32>(dbot))) return;
            addChain(name, st, sbot, dt, dbot, ik);
        };
        limb(sn + "Arm", Role::UpperArm, Role::Hand, false);
        limb(sn + "Leg", Role::Thigh, Role::Foot, true);
        pair(sn + "Toe", Role::Ball, side, -1, true, false);
        static const char* fn[] = {"Thumb", "Index", "Middle", "Ring", "Pinky"};
        for (int f = 0; f < 5; ++f) pair(sn + fn[f], Role::Finger, side, f, false, false);
    }

    // Pelvis, and a root bone above it on both sides.
    {
        u32 sp = 0, dp = 0, x = 0;
        if (S.span(S.bonesOf(Role::Pelvis, 0), sp, x) && D.span(D.bonesOf(Role::Pelvis, 0), dp, x)) {
            R.asset.root.srcBone = src.bones[sp].name;
            R.asset.root.dstBone = dst.bones[dp].name;
            const i32 spp = src.bones[sp].parent, dpp = dst.bones[dp].parent;
            if (spp >= 0 && dpp >= 0) {
                R.asset.rootMotion.srcBone = src.bones[static_cast<usize>(spp)].name;
                R.asset.rootMotion.dstBone = dst.bones[static_cast<usize>(dpp)].name;
            }
        }
    }

    // Structural chains for bones the table did not recognise: split the hierarchy at branches and leaves, pair
    // by (side, order). Only for rigs the table barely understands (quadrupeds, creatures): on a humanoid it
    // would pair twist and helper bones with unrelated leftovers.
    if (R.asset.chains.size() < 4) {
        auto coveredOf = [&](const Rig& rig, bool isSrc) {
            std::vector<u8> cov(rig.s.bones.size(), 0);
            for (const RetargetChainDef& c : R.asset.chains) {
                const std::string& a = isSrc ? c.srcStart : c.dstStart;
                const std::string& b = isSrc ? c.srcEnd : c.dstEnd;
                i32 ia = -1, ib = -1;
                for (usize i = 0; i < rig.s.bones.size(); ++i) {
                    if (rig.s.bones[i].name == a) ia = static_cast<i32>(i);
                    if (rig.s.bones[i].name == b) ib = static_cast<i32>(i);
                }
                for (i32 k = ib; k >= 0; k = rig.s.bones[static_cast<usize>(k)].parent) {
                    cov[static_cast<usize>(k)] = 1;
                    if (k == ia) break;
                }
            }
            return cov;
        };
        struct Seg { u32 a, b; int side; int depth; };
        auto segments = [&](const Rig& rig, const std::vector<u8>& cov) {
            std::vector<Seg> out;
            const usize n = rig.s.bones.size();
            auto inU = [&](usize i) { return !cov[i] && rig.info[i].role == Role::None; };
            std::vector<std::vector<u32>> kids(n);
            for (usize i = 0; i < n; ++i) {
                const i32 p = rig.s.bones[i].parent;
                if (inU(i) && p >= 0 && inU(static_cast<usize>(p))) kids[static_cast<usize>(p)].push_back(static_cast<u32>(i));
            }
            std::vector<u32> stack;
            for (usize i = 0; i < n; ++i) {
                const i32 p = rig.s.bones[i].parent;
                if (inU(i) && (p < 0 || !inU(static_cast<usize>(p)))) stack.push_back(static_cast<u32>(i));
            }
            std::reverse(stack.begin(), stack.end());
            while (!stack.empty()) {
                const u32 first = stack.back();
                stack.pop_back();
                u32 cur = first;
                while (kids[cur].size() == 1) cur = kids[cur][0];
                for (usize k = kids[cur].size(); k-- > 0;) stack.push_back(kids[cur][k]);
                int side = 0;
                for (i32 k = static_cast<i32>(cur); k >= 0; k = rig.s.bones[static_cast<usize>(k)].parent) {
                    if (rig.info[static_cast<usize>(k)].side) { side = rig.info[static_cast<usize>(k)].side; break; }
                    if (k == static_cast<i32>(first)) break;
                }
                if (side == 0) {
                    const f32 y = rig.restPos[cur].y;
                    side = y < -1.0f ? 1 : (y > 1.0f ? 2 : 0);
                }
                out.push_back({first, cur, side, rig.depthOf(first)});
            }
            return out;
        };
        const std::vector<Seg> ss = segments(S, coveredOf(S, true)), ds = segments(D, coveredOf(D, false));
        for (int side = 0; side <= 2; ++side) {
            std::vector<Seg> a, b;
            for (const Seg& g : ss) if (g.side == side) a.push_back(g);
            for (const Seg& g : ds) if (g.side == side) b.push_back(g);
            auto order = [](const Seg& x, const Seg& y) { return x.depth != y.depth ? x.depth < y.depth : x.a < y.a; };
            std::stable_sort(a.begin(), a.end(), order);
            std::stable_sort(b.begin(), b.end(), order);
            for (usize k = 0; k < std::min(a.size(), b.size()); ++k) {
                RetargetChainDef c;
                c.name = "Struct" + std::string(side == 1 ? "L" : side == 2 ? "R" : "C") + std::to_string(k);
                c.srcStart = src.bones[a[k].a].name; c.srcEnd = src.bones[a[k].b].name;
                c.dstStart = dst.bones[b[k].a].name; c.dstEnd = dst.bones[b[k].b].name;
                R.asset.chains.push_back(std::move(c));
                R.confidence.push_back(RtgConfidence::Structural);
            }
        }
    }

    // Keep what validates: chains must hang under the target pelvis when one was found.
    if (!R.asset.root.dstBone.empty()) {
        i32 pel = -1;
        for (usize i = 0; i < dst.bones.size(); ++i) if (dst.bones[i].name == R.asset.root.dstBone) pel = static_cast<i32>(i);
        std::vector<RetargetChainDef> keep;
        std::vector<RtgConfidence> keepC;
        for (usize i = 0; i < R.asset.chains.size(); ++i) {
            i32 st = -1;
            for (usize k = 0; k < dst.bones.size(); ++k) if (dst.bones[k].name == R.asset.chains[i].dstStart) st = static_cast<i32>(k);
            if (D.ancestorOrSelf(pel, st)) { keep.push_back(R.asset.chains[i]); keepC.push_back(R.confidence[i]); }
        }
        R.asset.chains = std::move(keep);
        R.confidence = std::move(keepC);
    }
    return R;
}

} // namespace aver::anim
