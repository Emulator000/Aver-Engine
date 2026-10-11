// Helpers shared by Ik.cpp, IkSolvers.cpp and the retargeter. Private to the module (not under include/).
// The conventions are the whole point of this file: Hamilton quaternions, row-vector matrices, and the
// order trap between the two. Nothing outside the module hand-rolls a local<->model conversion.
#pragma once

#include "aver/anim/Pose.hpp"
#include "aver/core/Math.hpp"

#include <cmath>

namespace aver::anim::ikm {

constexpr f32 kEps = 1e-5f;

// The translation row of a row-vector model matrix.
inline Vec3 originOf(const Mat4& m) { return Vec3{m.m[3][0], m.m[3][1], m.m[3][2]}; }

// Transforms a DIRECTION by a row-vector matrix: v * M, ignoring the translation row.
inline Vec3 transformDir(const Vec3& v, const Mat4& m) {
    return Vec3{v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
                v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
                v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2]};
}

inline f32 lengthOf(const Vec3& v) { return std::sqrt(dot(v, v)); }

// Conjugate = inverse for a unit quaternion (Math.hpp has no Quat::inverse).
inline Quat conj(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

// Normalised rotation of a model matrix, scale removed.
inline Quat modelRotation(const Mat4& m) { return transformFromMatrix(m).rotation.normalized(); }

// The rotation taking unit vector `from` onto unit vector `to`, as an axis and an angle.
// False when they are already parallel (nothing to do) or exactly opposed with no axis to pick.
inline bool rotationBetween(const Vec3& from, const Vec3& to, Vec3& axis, f32& angle) {
    const f32 d = dot(from, to);
    if (d > 1.0f - 1e-7f) return false;                 // already there
    const Vec3 c = cross(from, to);
    const f32 s = lengthOf(c);
    if (s < 1e-7f) {
        // PARALLEL with a dot just under the cut above (vectors that are not exactly unit): nothing to do.
        // Without this a zero cross product was always read as antiparallel and turned the bone by 180.
        if (d > 0.0f) return false;
        // ANTIPARALLEL: the axis is undetermined, so any perpendicular will do -- and one must be
        // chosen rather than bailing, because "turn around completely" is a legitimate request that
        // a chain doubled back on itself will actually make.
        const Vec3 pick = std::fabs(from.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        axis = cross(from, pick).getSafeNormal();
        angle = 3.14159265358979f;
        return true;
    }
    axis = c * (1.0f / s);
    angle = std::atan2(s, d);
    return true;
}

// Shortest arc taking unit a to unit b (identity when parallel; a perpendicular axis when opposed).
inline Quat arc(const Vec3& a, const Vec3& b) {
    Vec3 axis{0, 0, 1};
    f32 angle = 0.0f;
    if (!rotationBetween(a, b, axis, angle)) return Quat::identity();
    return Quat::fromAxisAngle(axis, angle);
}

// Applies a MODEL-space rotation to one bone by rewriting its LOCAL rotation.
//
// THE CONVERSION, derived from poseToModel's own composition (`model = local * parentModel`, row
// vectors, Pose.cpp). Wanting `model_new = model_old * D`:
//
//     local_new * parentModel = local_old * parentModel * D
//     local_new               = local_old * (parentModel * D * parentModel^-1)
//
// and conjugating a rotation that way leaves the angle alone and carries the axis into the parent's
// frame -- if D turns about `n`, then `parentModel * D * parentModel^-1` turns about `n * parentModel^-1`.
// So only the AXIS needs transforming, which is why this takes an axis and an angle rather than a
// quaternion.
//
// BUT THE QUATERNION PRODUCT IS WRITTEN THE OTHER WAY ROUND FROM THAT DERIVATION, AND THAT IS
// MEASURED, NOT A TYPO. Math.hpp's Quat::operator* does NOT compose in the same order as
// Mat4::operator* does under fromQuat: `fromQuat(a * b)` is `fromQuat(b) * fromQuat(a)`. So the
// matrix-order `local_old * D` becomes the quaternion-order `D * local_old` here.
//
// It was found by a real skeleton, because no fixture caught it. IkTest's arm has a PARENTLESS root
// and identity rest rotations throughout, which makes both halves of this function untestable: with
// no parent there is no conjugation to get wrong, and with an identity local rotation the product
// commutes and the order cannot matter. Character.ocskel's LeftArm has a real parent chain and real
// rest rotations, and with the derived order the hand landed 1,766 cm from a goal that was 6,633 cm
// away and well inside a 9,980 cm reach -- bone lengths perfect, chain rigid, simply the wrong pose.
// testParentedChainWithRestRotations in IkTest now covers exactly that shape.
inline void applyModelDelta(const fmt::OcSkeleton& skel, Pose& pose, const std::vector<Mat4>& model,
                            u32 bone, const Vec3& axisModel, f32 angle) {
    if (std::fabs(angle) < 1e-6f) return;

    Vec3 axis = axisModel;
    const i32 parent = skel.bones[bone].parent;
    if (parent >= 0 && static_cast<usize>(parent) < model.size()) {
        // Normalised after transforming, because a scaled bone makes the inverse scale the axis too
        // and an axis only carries direction.
        axis = transformDir(axisModel, model[static_cast<usize>(parent)].inverse()).getSafeNormal();
    }
    pose.local[bone].rotation =
        (Quat::fromAxisAngle(axis, angle) * pose.local[bone].rotation).normalized();
}

// True when `child`'s parent is `parent` and both index real bones.
inline bool linked(const fmt::OcSkeleton& skel, u32 parent, u32 child) {
    if (parent >= skel.bones.size() || child >= skel.bones.size()) return false;
    return skel.bones[child].parent == static_cast<i32>(parent);
}

// Sets bone's MODEL rotation to `want` by rewriting its LOCAL rotation, given the parent's model matrix.
// Hamilton: Q_model(i) = Q_model(parent) * Q_local(i), so local = conj(Q_parent) * want. (The matrix
// order is the other way round; see applyModelDelta.)
inline void setModelRotation(const fmt::OcSkeleton& skel, Pose& pose, const std::vector<Mat4>& model,
                             u32 bone, const Quat& want) {
    const i32 parent = skel.bones[bone].parent;
    const Quat qp = (parent >= 0 && static_cast<usize>(parent) < model.size())
                  ? modelRotation(model[static_cast<usize>(parent)]) : Quat::identity();
    pose.local[bone].rotation = (conj(qp) * want).normalized();
}

} // namespace aver::anim::ikm
