// Level sequence sampling and playback (LevelSequence.hpp).
#include "aver/game/LevelSequence.hpp"

#include "aver/world/LevelTransform.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace aver::game {

namespace {

using fmt::OcSeqInterp;
using fmt::OcSeqKey;
using fmt::OcSeqTrack;
using fmt::OcSeqTrackKind;

// The segment t falls in: keys k1 -> k2, with k0 / k3 the Catmull-Rom neighbours (clamped).
struct Seg {
    usize k0 = 0, k1 = 0, k2 = 0, k3 = 0;
    f64 u = 0;
    OcSeqInterp interp = OcSeqInterp::Smooth;
};

Seg findSeg(const std::vector<OcSeqKey>& keys, f64 t) {
    Seg s;
    const usize n = keys.size();
    if (n == 1 || t <= keys.front().t) return s;   // hold the first key (all indices 0)
    if (t >= keys.back().t) {
        s.k0 = s.k1 = s.k2 = s.k3 = n - 1;
        return s;
    }
    usize i = 0;
    while (i + 2 < n && keys[i + 1].t <= t) ++i;
    s.k1 = i;
    s.k2 = i + 1;
    s.k0 = i > 0 ? i - 1 : i;
    s.k3 = i + 2 < n ? i + 2 : i + 1;
    const f64 dt = keys[s.k2].t - keys[s.k1].t;
    s.u = dt > 1e-12 ? std::clamp((t - keys[s.k1].t) / dt, 0.0, 1.0) : 0.0;
    s.interp = keys[s.k1].interp;
    return s;
}

f64 catmullRom(f64 p0, f64 p1, f64 p2, f64 p3, f64 u) {
    const f64 u2 = u * u, u3 = u2 * u;
    return 0.5 * ((2.0 * p1) + (-p0 + p2) * u + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u2 +
                  (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * u3);
}

// Cubic Hermite across k1 -> k2 with the keys' own slopes (or the automatic ones): the leave slope of k1
// and the arrive slope of k2.
f64 hermite(const std::vector<OcSeqKey>& keys, const Seg& s, int c) {
    const OcSeqKey& a = keys[s.k1];
    const OcSeqKey& b = keys[s.k2];
    if (s.k1 == s.k2) return a.v[c];
    const f64 dt = b.t - a.t;
    f64 m1 = 0, m2 = 0, unused = 0;
    seqEffectiveTangent(keys, s.k1, static_cast<u32>(c), unused, m1);
    seqEffectiveTangent(keys, s.k2, static_cast<u32>(c), m2, unused);
    const f64 u = s.u, u2 = u * u, u3 = u2 * u;
    return (2.0 * u3 - 3.0 * u2 + 1.0) * a.v[c] + (u3 - 2.0 * u2 + u) * dt * m1 +
           (-2.0 * u3 + 3.0 * u2) * b.v[c] + (u3 - u2) * dt * m2;
}

// One scalar channel across the segment.
f64 sampleChannel(const std::vector<OcSeqKey>& keys, const Seg& s, int c) {
    const f64 p0 = keys[s.k0].v[c], p1 = keys[s.k1].v[c], p2 = keys[s.k2].v[c], p3 = keys[s.k3].v[c];
    switch (s.interp) {
        case OcSeqInterp::Step:   return p1;
        case OcSeqInterp::Linear: return p1 + (p2 - p1) * s.u;
        case OcSeqInterp::Smooth: return catmullRom(p0, p1, p2, p3, s.u);
        case OcSeqInterp::Cubic:  return hermite(keys, s, c);
    }
    return p1;
}

Quat keyRotation(const OcSeqKey& k) {
    return world::quatFromEulerDeg(Vec3{static_cast<f32>(k.v[5]), static_cast<f32>(k.v[4]),
                                        static_cast<f32>(k.v[3])});
}

// ---- camera path math (double precision) ---------------------------------------------------------

struct V3 { f64 x = 0, y = 0, z = 0; };
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, f64 k) { return {a.x * k, a.y * k, a.z * k}; }
f64 len3(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

V3 keyPos(const OcSeqKey& k) { return {k.v[0], k.v[1], k.v[2]}; }

// Centripetal Catmull-Rom through p1 -> p2 (Barry-Goldman), u in [0,1] along the segment. Centripetal
// knots never loop or overshoot at a sharp corner. An end segment passes the missing neighbour as a
// reflection of the inner one, so the path leaves the first key and reaches the last without a bulge.
V3 centripetal(V3 p0, V3 p1, V3 p2, V3 p3, f64 u) {
    constexpr f64 kMinKnot = 1e-4;   // coincident keys must not collapse a knot interval to zero
    const f64 t0 = 0.0;
    const f64 t1 = t0 + std::max(std::sqrt(len3(p1 - p0)), kMinKnot);
    const f64 t2 = t1 + std::max(std::sqrt(len3(p2 - p1)), kMinKnot);
    const f64 t3 = t2 + std::max(std::sqrt(len3(p3 - p2)), kMinKnot);
    const f64 t = t1 + (t2 - t1) * u;
    const V3 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
    const V3 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
    const V3 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
    const V3 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
    const V3 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
    return b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
}

// Unit quaternion, w first.
struct Q { f64 w = 1, x = 0, y = 0, z = 0; };
Q operator*(Q a, Q b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
Q conj(Q a) { return {a.w, -a.x, -a.y, -a.z}; }
f64 dot(Q a, Q b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
Q neg(Q a) { return {-a.w, -a.x, -a.y, -a.z}; }
Q normalize(Q a) {
    const f64 n = std::sqrt(dot(a, a));
    return n > 1e-12 ? Q{a.w / n, a.x / n, a.y / n, a.z / n} : Q{};
}
Q sameSide(Q q, Q ref) { return dot(q, ref) < 0.0 ? neg(q) : q; }   // q and -q are one rotation

// Yaw about +Z then pitch up: the camera's forward is (cosP cosY, cosP sinY, sinP).
Q quatFromYawPitch(f64 yaw, f64 pitch) {
    const Q qz{std::cos(yaw * 0.5), 0, 0, std::sin(yaw * 0.5)};
    const Q qy{std::cos(-pitch * 0.5), 0, std::sin(-pitch * 0.5), 0};
    return qz * qy;
}

void yawPitchFromQuat(Q q, f64& yaw, f64& pitch) {
    const f64 fx = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    const f64 fy = 2.0 * (q.x * q.y + q.w * q.z);
    const f64 fz = 2.0 * (q.x * q.z - q.w * q.y);
    yaw = std::atan2(fy, fx);
    pitch = std::asin(std::clamp(fz, -1.0, 1.0));
}

Q slerpQ(Q a, Q b, f64 t) {
    b = sameSide(b, a);
    const f64 d = std::clamp(dot(a, b), -1.0, 1.0);
    if (d > 0.9995) return normalize({a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t,
                                      a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
    const f64 th = std::acos(d), s = std::sin(th);
    const f64 wa = std::sin((1.0 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb};
}

V3 logQ(Q q) {   // unit quaternion -> rotation vector / 2
    const f64 s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (s < 1e-12) return {};
    const f64 k = std::atan2(s, q.w) / s;
    return {q.x * k, q.y * k, q.z * k};
}

Q expQ(V3 v) {
    const f64 th = len3(v);
    if (th < 1e-12) return {};
    const f64 k = std::sin(th) / th;
    return {std::cos(th), v.x * k, v.y * k, v.z * k};
}

// Squad's inner control point at qi, from its neighbours (qi itself at a path end: a flat tangent).
Q squadInner(Q prev, Q qi, Q next) {
    prev = sameSide(prev, qi);
    next = sameSide(next, qi);
    const Q inv = conj(qi);
    const V3 sum = logQ(inv * next) + logQ(inv * prev);
    return normalize(qi * expQ(sum * -0.25));
}

Q squad(Q q1, Q q2, Q s1, Q s2, f64 h) {
    return slerpQ(slerpQ(q1, q2, h), slerpQ(s1, s2, h), 2.0 * h * (1.0 - h));
}

Q keyQuat(const OcSeqKey& k) {
    constexpr f64 kDeg2Rad = 3.14159265358979323846 / 180.0;
    return quatFromYawPitch(k.v[3] * kDeg2Rad, k.v[4] * kDeg2Rad);
}

f32 f(f64 v) { return static_cast<f32>(v); }

} // namespace

f64 seqWrapTime(const fmt::OcSequence& seq, f64 t) {
    if (seq.loop) {
        if (seq.length <= 0) return 0;
        f64 r = std::fmod(t, seq.length);
        if (r < 0) r += seq.length;
        return r;
    }
    return std::clamp(t, 0.0, std::max(seq.length, 0.0));
}

bool sampleSeqTransform(const OcSeqTrack& track, f64 t, Transform& out) {
    if (track.kind != OcSeqTrackKind::Transform || track.keys.empty()) return false;
    const Seg s = findSeg(track.keys, t);
    out.position = Vec3{f(sampleChannel(track.keys, s, 0)), f(sampleChannel(track.keys, s, 1)),
                        f(sampleChannel(track.keys, s, 2))};
    out.scale    = Vec3{f(sampleChannel(track.keys, s, 6)), f(sampleChannel(track.keys, s, 7)),
                        f(sampleChannel(track.keys, s, 8))};
    if (s.interp == OcSeqInterp::Cubic) {   // Hermite on the authored Euler degrees: no short arc
        out.rotation = world::quatFromEulerDeg(Vec3{f(sampleChannel(track.keys, s, 5)), f(sampleChannel(track.keys, s, 4)),
                                                    f(sampleChannel(track.keys, s, 3))});
        return true;
    }
    const Quat q1 = keyRotation(track.keys[s.k1]);
    if (s.interp == OcSeqInterp::Step || s.k1 == s.k2) {
        out.rotation = q1;
    } else {
        const f32 u = f(s.u);
        const f32 w = s.interp == OcSeqInterp::Smooth ? u * u * (3.0f - 2.0f * u) : u;
        out.rotation = Quat::slerp(q1, keyRotation(track.keys[s.k2]), w).normalized();   // slerp takes the short arc
    }
    return true;
}

bool sampleSeqCamera(const OcSeqTrack& track, f64 t, SeqCameraPose& out) {
    if (track.kind != OcSeqTrackKind::Camera || track.keys.empty()) return false;
    constexpr f64 kDeg2Rad = 3.14159265358979323846 / 180.0;
    const std::vector<OcSeqKey>& keys = track.keys;
    const Seg s = findSeg(keys, t);
    const auto setKey = [&](const OcSeqKey& k) {   // a key's own pose, unchanged by the quaternion round trip
        out.position = Vec3{f(k.v[0]), f(k.v[1]), f(k.v[2])};
        out.yaw = f(k.v[3] * kDeg2Rad);
        out.pitch = f(k.v[4] * kDeg2Rad);
    };
    if (s.k1 == s.k2 || s.interp == OcSeqInterp::Step || s.u <= 0.0) { setKey(keys[s.k1]); return true; }
    if (s.u >= 1.0) { setKey(keys[s.k2]); return true; }
    if (s.interp == OcSeqInterp::Cubic) {   // Hermite on position and on the authored yaw / pitch degrees
        out.position = Vec3{f(sampleChannel(keys, s, 0)), f(sampleChannel(keys, s, 1)), f(sampleChannel(keys, s, 2))};
        out.yaw = f(sampleChannel(keys, s, 3) * kDeg2Rad);
        out.pitch = f(sampleChannel(keys, s, 4) * kDeg2Rad);
        return true;
    }

    V3 pos;
    Q q;
    const Q q1 = keyQuat(keys[s.k1]), q2 = keyQuat(keys[s.k2]);
    if (s.interp == OcSeqInterp::Linear) {
        pos = keyPos(keys[s.k1]) * (1.0 - s.u) + keyPos(keys[s.k2]) * s.u;
        q = slerpQ(q1, q2, s.u);
    } else {
        const V3 p1 = keyPos(keys[s.k1]), p2 = keyPos(keys[s.k2]);
        const V3 p0 = s.k0 == s.k1 ? p1 * 2.0 - p2 : keyPos(keys[s.k0]);
        const V3 p3 = s.k3 == s.k2 ? p2 * 2.0 - p1 : keyPos(keys[s.k3]);
        pos = centripetal(p0, p1, p2, p3, s.u);
        const Q a1 = squadInner(keyQuat(keys[s.k0]), q1, q2);
        const Q a2 = squadInner(q1, q2, keyQuat(keys[s.k3]));
        q = squad(q1, sameSide(q2, q1), a1, sameSide(a2, q1), s.u);
    }
    f64 yaw, pitch;
    yawPitchFromQuat(normalize(q), yaw, pitch);
    out.position = Vec3{f(pos.x), f(pos.y), f(pos.z)};
    out.yaw = f(yaw);
    out.pitch = f(pitch);
    return true;
}

bool sampleSeqMaterial(const OcSeqTrack& track, f64 t, f32 outScale[3]) {
    if (track.kind != OcSeqTrackKind::Material || track.keys.empty()) return false;
    const Seg s = findSeg(track.keys, t);
    const f64 intensity = std::max(0.0, sampleChannel(track.keys, s, 3));
    for (int c = 0; c < 3; ++c)
        outScale[c] = f(std::max(0.0, sampleChannel(track.keys, s, c)) * intensity);
    return true;
}

void seqKeyFromTransform(const Transform& xf, fmt::OcSeqKey& key) {
    const Vec3 e = world::eulerDegFromQuat(xf.rotation);   // (roll, pitch, yaw)
    key.v[0] = xf.position.x; key.v[1] = xf.position.y; key.v[2] = xf.position.z;
    key.v[3] = e.z; key.v[4] = e.y; key.v[5] = e.x;
    key.v[6] = xf.scale.x; key.v[7] = xf.scale.y; key.v[8] = xf.scale.z;
}

void seqKeyFromCamera(const SeqCameraPose& pose, fmt::OcSeqKey& key) {
    constexpr f64 kRad2Deg = 180.0 / 3.14159265358979323846;
    key.v[0] = pose.position.x; key.v[1] = pose.position.y; key.v[2] = pose.position.z;
    key.v[3] = pose.yaw * kRad2Deg;
    key.v[4] = pose.pitch * kRad2Deg;
}

// ---- curves, tangents, sections, crossings ----------------------------------------------------------

namespace {

f64 smooth01(f64 u) {
    u = std::clamp(u, 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

bool isRotationChannel(OcSeqTrackKind k, u32 c) {
    return (k == OcSeqTrackKind::Transform && c >= 3 && c <= 5) || (k == OcSeqTrackKind::Camera && (c == 3 || c == 4));
}

} // namespace

u32 seqChannelCount(const OcSeqTrack& track, u32 propertyArity) {
    switch (track.kind) {
        case OcSeqTrackKind::Transform: return 9;
        case OcSeqTrackKind::Camera:    return 5;
        case OcSeqTrackKind::Material:  return 4;
        case OcSeqTrackKind::Property:  return std::min(propertyArity, 4u);
        default:                        return 0;
    }
}

void seqAutoTangent(const std::vector<OcSeqKey>& keys, usize key, u32 channel, f64& arrive, f64& leave) {
    arrive = leave = 0.0;
    const usize n = keys.size();
    if (n < 2 || key >= n || channel >= 9) return;
    const usize a = key > 0 ? key - 1 : key;
    const usize b = key + 1 < n ? key + 1 : key;
    const f64 dt = keys[b].t - keys[a].t;
    if (dt <= 1e-12) return;
    arrive = leave = (keys[b].v[channel] - keys[a].v[channel]) / dt;
}

void seqEffectiveTangent(const std::vector<OcSeqKey>& keys, usize key, u32 channel, f64& arrive, f64& leave) {
    if (key < keys.size()) {
        const std::vector<f64>& tan = keys[key].tan;
        if (tan.size() > 2u * channel + 1u) {
            arrive = tan[2u * channel];
            leave = tan[2u * channel + 1u];
            return;
        }
    }
    seqAutoTangent(keys, key, channel, arrive, leave);
}

f64 sampleSeqChannel(const OcSeqTrack& track, u32 channel, f64 t) {
    if (track.keys.empty() || channel >= 9) return 0.0;
    const std::vector<OcSeqKey>& keys = track.keys;
    const Seg s = findSeg(keys, t);
    const int c = static_cast<int>(channel);
    if (isRotationChannel(track.kind, channel) && s.interp != OcSeqInterp::Cubic) {
        // The authored degrees, eased the way the rotation slerp eases its parameter.
        const f64 v1 = keys[s.k1].v[c], v2 = keys[s.k2].v[c];
        if (s.k1 == s.k2 || s.interp == OcSeqInterp::Step) return v1;
        const f64 w = s.interp == OcSeqInterp::Smooth ? smooth01(s.u) : s.u;
        f64 approx = v1 + (v2 - v1) * w;
        if (track.kind == OcSeqTrackKind::Camera && s.interp == OcSeqInterp::Smooth) {
            // The camera's squad is not an Euler lerp: take its real yaw / pitch, unwrapped next to the lerp.
            SeqCameraPose pose;
            if (sampleSeqCamera(track, t, pose)) {
                constexpr f64 kRad2Deg = 180.0 / 3.14159265358979323846;
                const f64 d = (channel == 3 ? pose.yaw : pose.pitch) * kRad2Deg;
                return channel == 3 ? d + 360.0 * std::round((approx - d) / 360.0) : d;
            }
        }
        return approx;
    }
    if (track.kind == OcSeqTrackKind::Camera && channel < 3 && s.interp == OcSeqInterp::Smooth) {
        SeqCameraPose pose;   // the centripetal spline, not the uniform one
        if (sampleSeqCamera(track, t, pose)) return channel == 0 ? pose.position.x : channel == 1 ? pose.position.y : pose.position.z;
    }
    return sampleChannel(keys, s, c);
}

bool sampleSeqProperty(const OcSeqTrack& track, f64 t, bool quat, bool stepOnly, f64 out[4]) {
    if (track.keys.empty()) return false;
    const std::vector<OcSeqKey>& keys = track.keys;
    Seg s = findSeg(keys, t);
    if (stepOnly) s.interp = OcSeqInterp::Step;
    if (quat) {
        const auto q = [&](const OcSeqKey& k) { return normalize(Q{k.v[3], k.v[0], k.v[1], k.v[2]}); };
        Q r = q(keys[s.k1]);
        if (s.k1 != s.k2 && s.interp != OcSeqInterp::Step) {
            const f64 w = s.interp == OcSeqInterp::Linear ? s.u : smooth01(s.u);   // Cubic reads as Smooth
            r = normalize(slerpQ(r, q(keys[s.k2]), w));
        }
        out[0] = r.x; out[1] = r.y; out[2] = r.z; out[3] = r.w;
        return true;
    }
    for (int c = 0; c < 4; ++c) out[c] = sampleChannel(keys, s, c);
    return true;
}

const OcSeqKey* seqActiveSection(const OcSeqTrack& track, f64 t, bool holdAfter, f64& local) {
    const std::vector<OcSeqKey>& keys = track.keys;
    usize n = 0;   // keys starting at or before t (sorted)
    while (n < keys.size() && keys[n].t <= t) ++n;
    local = 0.0;
    if (n == 0) return nullptr;
    const OcSeqKey& k = keys[n - 1];
    local = t - k.t;
    if (t < k.t + k.dur) return &k;
    return holdAfter ? &k : nullptr;
}

f32 seqSectionWeight(const OcSeqKey& key, f64 local, f64 fadeIn, f64 fadeOut) {
    if (local < 0.0) return 0.0f;
    f64 w = 1.0;
    if (fadeIn > 1e-9 && local < fadeIn) w *= smooth01(local / fadeIn);
    if (fadeOut > 1e-9 && key.dur > 0.0) {
        if (local >= key.dur) return 0.0f;
        const f64 left = key.dur - local;
        if (left < fadeOut) w *= smooth01(left / fadeOut);
    }
    return f(w);
}

usize seqCrossedKeys(const OcSeqTrack& track, f64 t0, f64 t1, bool inclusiveStart, f64 length, bool loop,
                     std::vector<u32>& out) {
    out.clear();
    const std::vector<OcSeqKey>& keys = track.keys;
    const auto from = [&](f64 kt) { return inclusiveStart ? kt >= t0 : kt > t0; };
    if (t1 >= t0) {
        for (usize i = 0; i < keys.size(); ++i)
            if (from(keys[i].t) && keys[i].t <= t1) out.push_back(static_cast<u32>(i));
    } else if (loop && length > 0.0) {   // a wrap: (t0, length] and then [0, t1]
        for (usize i = 0; i < keys.size(); ++i)
            if (from(keys[i].t) && keys[i].t <= length) out.push_back(static_cast<u32>(i));
        for (usize i = 0; i < keys.size(); ++i)
            if (keys[i].t >= 0.0 && keys[i].t <= t1) out.push_back(static_cast<u32>(i));
    }
    return out.size();
}

bool sampleSeqCut(const OcSeqTrack& cuts, f64 t, std::string& camera, std::string& prevCamera, f32& blendAlpha) {
    camera.clear();
    prevCamera.clear();
    blendAlpha = 1.0f;
    const std::vector<OcSeqKey>& keys = cuts.keys;
    if (keys.empty()) return false;
    usize n = 0;
    while (n < keys.size() && keys[n].t <= t) ++n;
    if (n == 0) return true;   // before the first cut: the caller's first camera
    const OcSeqKey& k = keys[n - 1];
    camera = k.text;
    const f64 blend = k.v[0];
    if (n >= 2 && blend > 1e-9) {
        const f64 u = (t - k.t) / blend;
        if (u < 1.0) {
            blendAlpha = f(smooth01(u));
            prevCamera = keys[n - 2].text;
        }
    }
    return true;
}

} // namespace aver::game
