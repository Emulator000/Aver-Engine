// A small humanoid with ROTATED rest transforms, shared by the IK rig and retargeting tests. The root is
// yawed and the legs carry rest tilts: a fixture with an identity-rest, parentless chain cannot tell a right
// local<->model conversion from a wrong one (see IkTest.testParentedChainWithRestRotations).
#pragma once

#include "aver/anim/Pose.hpp"
#include "aver/formats/OcAnim.hpp"

#include <string>
#include <vector>

namespace ikfix {

using namespace aver;

struct Humanoid {
    fmt::OcSkeleton skel;
    u32 root = 0, pelvis = 0, spine1 = 0, spine2 = 0, spine3 = 0, neck = 0;
    u32 thighL = 0, calfL = 0, footL = 0, toeL = 0, thighR = 0, calfR = 0, footR = 0, toeR = 0;
};

inline u32 addBone(fmt::OcSkeleton& s, const std::string& name, i32 parent, const Vec3& t, const Quat& r = Quat::identity()) {
    fmt::OcBone b;
    b.name = name;
    b.parent = parent;
    b.translation = t;
    b.rotation = r;
    s.bones.push_back(b);
    return static_cast<u32>(s.bones.size() - 1);
}

inline Humanoid makeHumanoid() {
    Humanoid h;
    fmt::OcSkeleton& s = h.skel;
    h.root = addBone(s, "root", -1, Vec3{0, 0, 0}, Quat::fromAxisAngle(Vec3{0, 0, 1}, 0.35f));
    h.pelvis = addBone(s, "pelvis", static_cast<i32>(h.root), Vec3{0, 0, 100});
    h.spine1 = addBone(s, "spine_01", static_cast<i32>(h.pelvis), Vec3{0, 0, 15}, Quat::fromAxisAngle(Vec3{0, 1, 0}, 0.10f));
    h.spine2 = addBone(s, "spine_02", static_cast<i32>(h.spine1), Vec3{0, 0, 15}, Quat::fromAxisAngle(Vec3{1, 0, 0}, -0.12f));
    h.spine3 = addBone(s, "spine_03", static_cast<i32>(h.spine2), Vec3{0, 0, 15}, Quat::fromAxisAngle(Vec3{0, 1, 0}, 0.08f));
    h.neck = addBone(s, "neck", static_cast<i32>(h.spine3), Vec3{0, 0, 10});
    for (int side = 0; side < 2; ++side) {
        const f32 sy = side == 0 ? -10.0f : 10.0f;
        const char* sfx = side == 0 ? "_l" : "_r";
        const u32 thigh = addBone(s, std::string("thigh") + sfx, static_cast<i32>(h.pelvis), Vec3{0, sy, 0},
                                  Quat::fromAxisAngle(Vec3{1, 0, 0}, side == 0 ? 0.08f : -0.08f));
        const u32 calf = addBone(s, std::string("calf") + sfx, static_cast<i32>(thigh), Vec3{4, 0, -45},
                                 Quat::fromAxisAngle(Vec3{0, 1, 0}, 0.12f));
        const u32 foot = addBone(s, std::string("foot") + sfx, static_cast<i32>(calf), Vec3{-4, 0, -45},
                                 Quat::fromAxisAngle(Vec3{0, 1, 0}, -0.05f));
        const u32 toe = addBone(s, std::string("ball") + sfx, static_cast<i32>(foot), Vec3{12, 0, -8});
        (side == 0 ? h.thighL : h.thighR) = thigh;
        (side == 0 ? h.calfL : h.calfR) = calf;
        (side == 0 ? h.footL : h.footR) = foot;
        (side == 0 ? h.toeL : h.toeR) = toe;
    }
    s.rootBone = 0;
    return h;
}

inline Vec3 modelPos(const fmt::OcSkeleton& s, const anim::Pose& p, u32 bone) {
    std::vector<Mat4> m;
    anim::poseToModel(s, p, m);
    return Vec3{m[bone].m[3][0], m[bone].m[3][1], m[bone].m[3][2]};
}

inline Quat modelRot(const fmt::OcSkeleton& s, const anim::Pose& p, u32 bone) {
    std::vector<Mat4> m;
    anim::poseToModel(s, p, m);
    return transformFromMatrix(m[bone]).rotation.normalized();
}

inline f32 angleDeg(const Quat& a, const Quat& b) {
    const Quat r = a * Quat{-b.x, -b.y, -b.z, b.w};
    return 2.0f * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::fabs(r.w)) * kRadToDeg;
}

inline bool samePose(const anim::Pose& a, const anim::Pose& b) {
    if (a.local.size() != b.local.size()) return false;
    for (usize i = 0; i < a.local.size(); ++i) {
        const Transform &x = a.local[i], &y = b.local[i];
        if (x.position.x != y.position.x || x.position.y != y.position.y || x.position.z != y.position.z ||
            x.rotation.x != y.rotation.x || x.rotation.y != y.rotation.y || x.rotation.z != y.rotation.z ||
            x.rotation.w != y.rotation.w || x.scale.x != y.scale.x)
            return false;
    }
    return true;
}

} // namespace ikfix
