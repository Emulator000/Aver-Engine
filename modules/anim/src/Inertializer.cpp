// Inertialization. See Inertializer.hpp.
//
// Each scalar offset follows x(t) = (a + b t) e^{-gamma t}, a = x0, b = v0 + gamma x0, advanced exactly
// over dt: x' = (a + b dt) e, v' = (b - gamma (a + b dt)) e, e = e^{-gamma dt}. If the spring would cross
// zero (x0 * b < 0) the initial velocity is replaced by -gamma x0, which makes b = 0 and the decay
// monotone: an offset never overshoots into the opposite sign.
#include "aver/anim/Inertializer.hpp"

#include "aver/anim/Trajectory.hpp"   // kSpringHalfLifeK

#include <algorithm>
#include <cmath>

namespace aver::anim {

namespace {

constexpr f32 kRestX = 1e-4f, kRestV = 1e-3f;

// Rotation vector (axis * angle, shortest arc) of a unit quaternion.
Vec3 quatLog(Quat q) {
    q = q.normalized();
    if (q.w < 0.0f) q = {-q.x, -q.y, -q.z, -q.w};
    const f32 s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-8f) return {2.0f * q.x, 2.0f * q.y, 2.0f * q.z};
    const f32 angle = 2.0f * std::atan2(s, q.w);
    const f32 k = angle / s;
    return {q.x * k, q.y * k, q.z * k};
}

Quat quatExp(const Vec3& r) {
    const f32 angle = r.size();
    if (angle < 1e-8f) return Quat{r.x * 0.5f, r.y * 0.5f, r.z * 0.5f, 1.0f}.normalized();
    const f32 s = std::sin(angle * 0.5f) / angle;
    return {r.x * s, r.y * s, r.z * s, std::cos(angle * 0.5f)};
}

Quat conjugate(const Quat& q) { return {-q.x, -q.y, -q.z, q.w}; }

// The nine offset components of `a` relative to `b`.
void relative(const Transform& a, const Transform& b, f32* out) {
    const Vec3 t = a.position - b.position;
    const Vec3 r = quatLog(a.rotation * conjugate(b.rotation));
    const Vec3 s = a.scale - b.scale;
    out[0] = t.x; out[1] = t.y; out[2] = t.z;
    out[3] = r.x; out[4] = r.y; out[5] = r.z;
    out[6] = s.x; out[7] = s.y; out[8] = s.z;
}

} // namespace

void Inertializer::setHalfLife(f32 seconds) { halfLife_ = std::max(seconds, 1e-3f); }

void Inertializer::reset() {
    x_.clear();
    v_.clear();
    bones_ = 0;
    active_ = false;
}

void Inertializer::begin(const Pose& from, const Pose& fromPrev, const Pose& to, const Pose& toPrev, f32 dtPrev,
                         u32 skipBone) {
    const usize n = from.local.size();
    if (n == 0 || to.local.size() != n || fromPrev.local.size() != n || toPrev.local.size() != n) {
        reset();
        return;
    }
    bones_ = static_cast<u32>(n);
    x_.assign(n * kChannels, 0.0f);
    v_.assign(n * kChannels, 0.0f);
    const f32 g = kSpringHalfLifeK / halfLife_;
    const f32 inv = dtPrev > 1e-5f ? 1.0f / dtPrev : 0.0f;
    f32 now[kChannels], before[kChannels];
    for (usize b = 0; b < n; ++b) {
        if (b == skipBone) continue;
        relative(from.local[b], to.local[b], now);
        relative(fromPrev.local[b], toPrev.local[b], before);
        for (u32 c = 0; c < kChannels; ++c) {
            const f32 x0 = now[c];
            f32 v0 = (now[c] - before[c]) * inv;   // velocity of `from` minus velocity of `to`
            if (x0 * (v0 + g * x0) < 0.0f) v0 = -g * x0;
            x_[b * kChannels + c] = x0;
            v_[b * kChannels + c] = v0;
        }
    }
    active_ = true;
}

void Inertializer::advance(f32 dt) {
    if (!active_ || dt <= 0.0f) return;
    const f32 g = kSpringHalfLifeK / halfLife_;
    const f32 e = std::exp(-g * dt);
    bool settled = true;
    for (usize i = 0; i < x_.size(); ++i) {
        const f32 a = x_[i], b = v_[i] + g * a;
        const f32 ab = a + b * dt;
        x_[i] = ab * e;
        v_[i] = (b - g * ab) * e;
        if (std::fabs(x_[i]) >= kRestX || std::fabs(v_[i]) >= kRestV) settled = false;
    }
    if (settled) reset();
}

void Inertializer::apply(Pose& p) const {
    if (!active_ || p.local.size() != bones_) return;
    for (usize b = 0; b < p.local.size(); ++b) {
        const f32* x = &x_[b * kChannels];
        Transform& t = p.local[b];
        t.position += Vec3{x[0], x[1], x[2]};
        t.scale += Vec3{x[6], x[7], x[8]};
        const Vec3 r{x[3], x[4], x[5]};
        if (r.sizeSquared() > 0.0f) t.rotation = (quatExp(r) * t.rotation).normalized();
    }
}

f32 Inertializer::offset(u32 bone, u32 channel) const {
    const usize i = static_cast<usize>(bone) * kChannels + channel;
    return (active_ && channel < kChannels && i < x_.size()) ? x_[i] : 0.0f;
}

f32 Inertializer::velocity(u32 bone, u32 channel) const {
    const usize i = static_cast<usize>(bone) * kChannels + channel;
    return (active_ && channel < kChannels && i < v_.size()) ? v_[i] : 0.0f;
}

} // namespace aver::anim
