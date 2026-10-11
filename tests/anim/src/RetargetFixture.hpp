// Humanoid skeletons built from MODEL-space rest transforms (bone frames independent of the geometry), in three
// naming schemes, with options for pose (T/A), spine bone count, leg length, scale and bone frames. Shared by the
// retargeting tests; every position is in centimetres, Z up, left = -Y.
#pragma once

#include "aver/anim/Pose.hpp"
#include "aver/formats/OcAnim.hpp"

#include <string>
#include <vector>

namespace rtfix {

using namespace aver;

inline Quat conjq(const Quat& q) { return Quat{-q.x, -q.y, -q.z, q.w}; }

// ---- skeleton fixtures ------------------------------------------------------------------------------------

enum class Naming { Ue, Mixamo, Bip };
enum Part { P_Root, P_Pelvis, P_Spine, P_Neck, P_Head, P_Clavicle, P_UpperArm, P_LowerArm, P_Hand, P_Thigh, P_Calf, P_Foot, P_Ball };

inline std::string nameOf(Naming n, Part p, int side /*0 l, 1 r, -1 none*/, int idx = 0) {
    const bool l = side == 0;
    switch (n) {
        case Naming::Ue: {
            const char* sfx = side < 0 ? "" : (l ? "_l" : "_r");
            switch (p) {
                case P_Root: return "root";
                case P_Pelvis: return "pelvis";
                case P_Spine: return "spine_0" + std::to_string(idx + 1);
                case P_Neck: return "neck_01";
                case P_Head: return "head";
                case P_Clavicle: return std::string("clavicle") + sfx;
                case P_UpperArm: return std::string("upperarm") + sfx;
                case P_LowerArm: return std::string("lowerarm") + sfx;
                case P_Hand: return std::string("hand") + sfx;
                case P_Thigh: return std::string("thigh") + sfx;
                case P_Calf: return std::string("calf") + sfx;
                case P_Foot: return std::string("foot") + sfx;
                case P_Ball: return std::string("ball") + sfx;
            }
            break;
        }
        case Naming::Mixamo: {
            const std::string pre = "mixamorig:";
            const std::string sd = l ? "Left" : "Right";
            switch (p) {
                case P_Root: return "Armature";
                case P_Pelvis: return pre + "Hips";
                case P_Spine: return pre + (idx == 0 ? "Spine" : "Spine" + std::to_string(idx));
                case P_Neck: return pre + "Neck";
                case P_Head: return pre + "Head";
                case P_Clavicle: return pre + sd + "Shoulder";
                case P_UpperArm: return pre + sd + "Arm";
                case P_LowerArm: return pre + sd + "ForeArm";
                case P_Hand: return pre + sd + "Hand";
                case P_Thigh: return pre + sd + "UpLeg";
                case P_Calf: return pre + sd + "Leg";
                case P_Foot: return pre + sd + "Foot";
                case P_Ball: return pre + sd + "ToeBase";
            }
            break;
        }
        case Naming::Bip: {
            const std::string sd = l ? "L " : "R ";
            switch (p) {
                case P_Root: return "Bip01";
                case P_Pelvis: return "Bip01 Pelvis";
                case P_Spine: return idx == 0 ? "Bip01 Spine" : "Bip01 Spine" + std::to_string(idx);
                case P_Neck: return "Bip01 Neck";
                case P_Head: return "Bip01 Head";
                case P_Clavicle: return "Bip01 " + sd + "Clavicle";
                case P_UpperArm: return "Bip01 " + sd + "UpperArm";
                case P_LowerArm: return "Bip01 " + sd + "Forearm";
                case P_Hand: return "Bip01 " + sd + "Hand";
                case P_Thigh: return "Bip01 " + sd + "Thigh";
                case P_Calf: return "Bip01 " + sd + "Calf";
                case P_Foot: return "Bip01 " + sd + "Foot";
                case P_Ball: return "Bip01 " + sd + "Toe0";
            }
            break;
        }
    }
    return "?";
}

struct Params {
    Naming naming = Naming::Ue;
    f32 s = 1.0f;
    bool aPose = false;
    int spine = 3;
    bool frames = false;       // bone frames aligned to the bones (X along the bone) instead of the model axes
    f32 legScale = 1.0f;
    bool withRoot = true;
    bool gun = false;          // an extra prop bone under the right hand, 10 cm out
    bool extra = false;        // an unmapped bone under the head
};

struct Rig {
    fmt::OcSkeleton skel;
    int root = -1, pelvis = -1, neck = -1, head = -1, gun = -1, extra = -1;
    std::vector<int> spine;
    int clav[2] = {-1, -1}, upper[2] = {-1, -1}, lower[2] = {-1, -1}, hand[2] = {-1, -1};
    int thigh[2] = {-1, -1}, calf[2] = {-1, -1}, foot[2] = {-1, -1}, ball[2] = {-1, -1};
};

struct Spec { std::string name; i32 parent; Vec3 pos; };

inline Rig makeRig(const Params& P) {
    std::vector<Spec> specs;
    Rig R;
    auto add = [&](const std::string& n, i32 parent, const Vec3& pos) { specs.push_back({n, parent, pos}); return static_cast<int>(specs.size() - 1); };
    const f32 s = P.s;
    const f32 pz = (10.0f + 90.0f * P.legScale) * s;
    if (P.withRoot) R.root = add(nameOf(P.naming, P_Root, -1), -1, Vec3{0, 0, 0});
    R.pelvis = add(nameOf(P.naming, P_Pelvis, -1), R.root, Vec3{0, 0, pz});
    int prev = R.pelvis;
    f32 top = pz;
    for (int k = 0; k < P.spine; ++k) {
        top = pz + (45.0f / static_cast<f32>(P.spine)) * static_cast<f32>(k + 1) * s;
        prev = add(nameOf(P.naming, P_Spine, -1, k), prev, Vec3{0, 0, top});
        R.spine.push_back(prev);
    }
    R.neck = add(nameOf(P.naming, P_Neck, -1), prev, Vec3{0, 0, top + 10 * s});
    R.head = add(nameOf(P.naming, P_Head, -1), R.neck, Vec3{0, 0, top + 25 * s});
    if (P.extra) R.extra = add("extra_bone", R.head, Vec3{5 * s, 0, top + 35 * s});
    for (int side = 0; side < 2; ++side) {
        const f32 sg = side == 0 ? -1.0f : 1.0f;
        R.clav[side] = add(nameOf(P.naming, P_Clavicle, side), R.spine.back(), Vec3{0, sg * 4 * s, top});
        R.upper[side] = add(nameOf(P.naming, P_UpperArm, side), R.clav[side], Vec3{0, sg * 15 * s, top});
        const Vec3 dir = P.aPose ? Vec3{0, sg * 0.70710678f, -0.70710678f} : Vec3{0, sg, 0};
        R.lower[side] = add(nameOf(P.naming, P_LowerArm, side), R.upper[side], Vec3{0, sg * 15 * s, top} + dir * (25 * s));
        R.hand[side] = add(nameOf(P.naming, P_Hand, side), R.lower[side], Vec3{0, sg * 15 * s, top} + dir * (50 * s));
        if (P.gun && side == 1) R.gun = add("gun", R.hand[side], Vec3{0, sg * 15 * s, top} + dir * (60 * s));
    }
    for (int side = 0; side < 2; ++side) {
        const f32 sg = side == 0 ? -1.0f : 1.0f;
        R.thigh[side] = add(nameOf(P.naming, P_Thigh, side), R.pelvis, Vec3{0, sg * 10 * s, pz});
        R.calf[side] = add(nameOf(P.naming, P_Calf, side), R.thigh[side], Vec3{4 * s, sg * 10 * s, pz - 45 * P.legScale * s});
        R.foot[side] = add(nameOf(P.naming, P_Foot, side), R.calf[side], Vec3{0, sg * 10 * s, pz - 90 * P.legScale * s});
        R.ball[side] = add(nameOf(P.naming, P_Ball, side), R.foot[side], Vec3{12 * s, sg * 10 * s, pz - 90 * P.legScale * s - 8 * s});
    }

    // Model-space frames: identity, or X along the first child.
    std::vector<Quat> q(specs.size(), Quat::identity());
    if (P.frames) {
        for (usize i = 0; i < specs.size(); ++i) {
            int child = -1;
            for (usize k = 0; k < specs.size() && child < 0; ++k)
                if (specs[k].parent == static_cast<i32>(i)) child = static_cast<int>(k);
            if (child < 0) { q[i] = specs[i].parent >= 0 ? q[static_cast<usize>(specs[i].parent)] : Quat::identity(); continue; }
            const Vec3 d = (specs[static_cast<usize>(child)].pos - specs[i].pos).getSafeNormal();
            const Vec3 x{1, 0, 0};
            const Vec3 c = cross(x, d);
            const f32 w = 1.0f + dot(x, d);
            q[i] = (c.size() < 1e-6f && w < 1e-3f) ? Quat{0, 0, 1, 0} : Quat{c.x, c.y, c.z, w}.normalized();
        }
    }
    for (usize i = 0; i < specs.size(); ++i) {
        fmt::OcBone b;
        b.name = specs[i].name;
        b.parent = specs[i].parent;
        const Quat qp = specs[i].parent >= 0 ? q[static_cast<usize>(specs[i].parent)] : Quat::identity();
        const Vec3 pp = specs[i].parent >= 0 ? specs[static_cast<usize>(specs[i].parent)].pos : Vec3{0, 0, 0};
        b.translation = conjq(qp).rotate(specs[i].pos - pp);
        b.rotation = (conjq(qp) * q[i]).normalized();
        R.skel.bones.push_back(b);
    }
    R.skel.rootBone = 0;
    return R;
}

inline Vec3 posOf(const fmt::OcSkeleton& s, const anim::Pose& p, int bone) {
    std::vector<Mat4> m;
    anim::poseToModel(s, p, m);
    return Vec3{m[static_cast<usize>(bone)].m[3][0], m[static_cast<usize>(bone)].m[3][1], m[static_cast<usize>(bone)].m[3][2]};
}
inline Quat rotOf(const fmt::OcSkeleton& s, const anim::Pose& p, int bone) {
    std::vector<Mat4> m;
    anim::poseToModel(s, p, m);
    return transformFromMatrix(m[static_cast<usize>(bone)]).rotation.normalized();
}
// World-space rotation of a bone applied on top of its rest: model rotation = R * restModel.
inline void rotateBone(const fmt::OcSkeleton& s, anim::Pose& p, int bone, const Quat& R) {
    anim::Pose rest;
    anim::restPose(s, rest);
    const Quat restModel = rotOf(s, rest, bone);
    const i32 par = s.bones[static_cast<usize>(bone)].parent;
    // The parent's CURRENT model rotation: callers rotate root-to-tip so it is final.
    const Quat qp = par >= 0 ? rotOf(s, p, par) : Quat::identity();
    p.local[static_cast<usize>(bone)].rotation = (conjq(qp) * (R * restModel)).normalized();
}


// A bone-to-bone direction in model space.
inline Vec3 dirModel(const fmt::OcSkeleton& s, const anim::Pose& p, int from, int to) { return (posOf(s, p, to) - posOf(s, p, from)).getSafeNormal(); }

} // namespace rtfix
