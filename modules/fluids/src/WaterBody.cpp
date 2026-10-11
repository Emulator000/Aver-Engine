// Spline rivers and closed-outline lakes: see WaterBody.hpp.
#include "aver/fluids/WaterBody.hpp"

#include "aver/core/Log.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace aver::fluids {
namespace {

// ---- Catmull-Rom (centripetal, Barry-Goldman) ----

f64 knotStep(const f64 a[2], const f64 b[2]) {
    const f64 d = std::hypot(b[0] - a[0], b[1] - a[1]);
    return std::sqrt(std::max(d, 1e-4));   // alpha 0.5; a floor keeps coincident points finite
}

void catmullRom(const f64 p[4][2], f64 t, f64 out[2]) {
    const f64 t0 = 0.0, t1 = t0 + knotStep(p[0], p[1]), t2 = t1 + knotStep(p[1], p[2]), t3 = t2 + knotStep(p[2], p[3]);
    const f64 u = t1 + (t2 - t1) * t;
    for (int c = 0; c < 2; ++c) {
        const f64 a1 = (t1 - u) / (t1 - t0) * p[0][c] + (u - t0) / (t1 - t0) * p[1][c];
        const f64 a2 = (t2 - u) / (t2 - t1) * p[1][c] + (u - t1) / (t2 - t1) * p[2][c];
        const f64 a3 = (t3 - u) / (t3 - t2) * p[2][c] + (u - t2) / (t3 - t2) * p[3][c];
        const f64 b1 = (t2 - u) / (t2 - t0) * a1 + (u - t0) / (t2 - t0) * a2;
        const f64 b2 = (t3 - u) / (t3 - t1) * a2 + (u - t1) / (t3 - t1) * a3;
        out[c] = (t2 - u) / (t2 - t1) * b1 + (u - t1) / (t2 - t1) * b2;
    }
}

// ---- PCHIP (Fritsch-Carlson) ----

void pchipDerivs(const std::vector<f64>& x, const std::vector<f64>& y, std::vector<f64>& d) {
    const usize m = x.size();
    d.assign(m, 0.0);
    if (m < 2) return;
    std::vector<f64> h(m - 1), del(m - 1);
    for (usize k = 0; k + 1 < m; ++k) {
        h[k] = std::max(x[k + 1] - x[k], 1e-6);
        del[k] = (y[k + 1] - y[k]) / h[k];
    }
    if (m == 2) { d[0] = d[1] = del[0]; return; }
    for (usize k = 1; k + 1 < m; ++k) {
        if (del[k - 1] * del[k] <= 0.0) { d[k] = 0.0; continue; }
        const f64 w1 = 2.0 * h[k] + h[k - 1], w2 = h[k] + 2.0 * h[k - 1];
        d[k] = (w1 + w2) / (w1 / del[k - 1] + w2 / del[k]);
    }
    auto endDeriv = [&](f64 h0, f64 h1, f64 d0, f64 d1) {
        f64 v = ((2.0 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
        if (v * d0 <= 0.0) v = 0.0;
        else if (d0 * d1 <= 0.0 && std::fabs(v) > 3.0 * std::fabs(d0)) v = 3.0 * d0;
        return v;
    };
    d[0] = endDeriv(h[0], h[1], del[0], del[1]);
    d[m - 1] = endDeriv(h[m - 2], h[m - 3], del[m - 2], del[m - 3]);
}

} // namespace

// ---- Spline ----

void Spline::set(std::vector<SplinePoint> pts, bool closed) {
    pts_ = std::move(pts);
    segs_.clear(); knots_.clear(); vz_.clear(); vw_.clear(); vf_.clear();
    tabS_.clear(); tabXY_.clear();
    length_ = 0.0f;
    const u32 n = static_cast<u32>(pts_.size());
    closed_ = closed && n >= 3;
    if (n == 0) return;
    if (n == 1) { tabS_.push_back(0.0f); tabXY_.push_back(pts_[0].x); tabXY_.push_back(pts_[0].y); return; }

    const u32 m = closed_ ? n + 1 : n;
    knots_.assign(m, 0.0);
    vz_.resize(m); vw_.resize(m); vf_.resize(m);
    for (u32 k = 0; k < m; ++k) {
        const SplinePoint& p = pts_[k % n];
        vz_[k] = p.z; vw_[k] = p.width; vf_[k] = p.flow;
        if (k > 0) {
            const SplinePoint& q = pts_[(k - 1) % n];
            knots_[k] = knots_[k - 1] + std::max<f64>(std::hypot(static_cast<f64>(p.x - q.x), static_cast<f64>(p.y - q.y)), 1e-3);
        }
    }
    pchipDerivs(knots_, vz_, dz_);
    pchipDerivs(knots_, vw_, dw_);
    pchipDerivs(knots_, vf_, df_);

    auto pt = [&](i64 i, f64 out[2]) {
        if (closed_) {
            const SplinePoint& p = pts_[static_cast<usize>(((i % n) + n) % n)];
            out[0] = p.x; out[1] = p.y;
        } else if (i < 0) {   // phantom before the first point
            out[0] = 2.0 * pts_[0].x - pts_[1].x; out[1] = 2.0 * pts_[0].y - pts_[1].y;
        } else if (i >= static_cast<i64>(n)) {
            out[0] = 2.0 * pts_[n - 1].x - pts_[n - 2].x; out[1] = 2.0 * pts_[n - 1].y - pts_[n - 2].y;
        } else {
            out[0] = pts_[static_cast<usize>(i)].x; out[1] = pts_[static_cast<usize>(i)].y;
        }
    };
    const u32 nseg = closed_ ? n : n - 1;
    segs_.resize(nseg);
    for (u32 i = 0; i < nseg; ++i) {
        Seg& sg = segs_[i];
        for (int k = 0; k < 4; ++k) pt(static_cast<i64>(i) - 1 + k, sg.p[k]);
        sg.knot0 = knots_[i];
        sg.knotLen = std::max(knots_[i + 1] - knots_[i], 1e-6);
        sg.k0 = i; sg.k1 = i + 1;
    }

    tabS_.assign(static_cast<usize>(nseg) * kSub + 1, 0.0f);
    tabXY_.assign(tabS_.size() * 2, 0.0f);
    f64 acc = 0.0, prev[2] = {0, 0};
    for (u32 i = 0; i < nseg; ++i) {
        for (u32 j = 0; j <= kSub; ++j) {
            if (j == kSub && i + 1 < nseg) break;   // the next segment's j = 0 is this point
            const usize e = static_cast<usize>(i) * kSub + j;
            f64 xy[2];
            evalSeg(i, static_cast<f64>(j) / kSub, xy);
            if (e > 0) acc += std::hypot(xy[0] - prev[0], xy[1] - prev[1]);
            prev[0] = xy[0]; prev[1] = xy[1];
            tabS_[e] = static_cast<f32>(acc);
            tabXY_[e * 2] = static_cast<f32>(xy[0]);
            tabXY_[e * 2 + 1] = static_cast<f32>(xy[1]);
        }
    }
    length_ = tabS_.back();
}

void Spline::evalSeg(u32 seg, f64 t, f64 xy[2]) const { catmullRom(segs_[seg].p, t, xy); }

f64 Spline::pchip(const std::vector<f64>& d, const std::vector<f64>& y, u32 seg, f64 t) const {
    const Seg& sg = segs_[seg];
    const f64 h = sg.knotLen;
    const f64 t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * y[sg.k0] + (t3 - 2 * t2 + t) * h * d[sg.k0] +
           (-2 * t3 + 3 * t2) * y[sg.k1] + (t3 - t2) * h * d[sg.k1];
}

Spline::Sample Spline::at(f32 s) const {
    Sample out;
    if (pts_.empty()) return out;
    if (segs_.empty()) {   // a single point
        out.pos[0] = pts_[0].x; out.pos[1] = pts_[0].y; out.pos[2] = pts_[0].z;
        out.width = pts_[0].width; out.flow = pts_[0].flow;
        return out;
    }
    s = std::clamp(s, 0.0f, length_);
    const usize last = tabS_.size() - 2;   // last table interval
    usize e = static_cast<usize>(std::upper_bound(tabS_.begin(), tabS_.end(), s) - tabS_.begin());
    e = e == 0 ? 0 : e - 1;
    if (e > last) e = last;
    const f32 span = tabS_[e + 1] - tabS_[e];
    const f64 frac = span > 1e-9f ? std::clamp(static_cast<f64>(s - tabS_[e]) / span, 0.0, 1.0) : 0.0;
    const u32 seg = static_cast<u32>(e / kSub);
    const f64 t = (static_cast<f64>(e % kSub) + frac) / kSub;
    f64 xy[2];
    evalSeg(seg, t, xy);
    out.pos[0] = static_cast<f32>(xy[0]);
    out.pos[1] = static_cast<f32>(xy[1]);
    out.pos[2] = static_cast<f32>(pchip(dz_, vz_, seg, t));
    out.width = static_cast<f32>(std::max(pchip(dw_, vw_, seg, t), 0.0));
    out.flow = static_cast<f32>(pchip(df_, vf_, seg, t));
    out.s = s;
    const f64 dt = 1.0 / (kSub * 4.0);
    f64 a[2], b[2];
    evalSeg(seg, std::max(t - dt, 0.0), a);
    evalSeg(seg, std::min(t + dt, 1.0), b);
    f64 tx = b[0] - a[0], ty = b[1] - a[1];
    f64 len = std::hypot(tx, ty);
    if (len < 1e-9) {   // a cusp: fall back to the chord
        const SplinePoint& p0 = pts_[segs_[seg].k0 % pts_.size()];
        const SplinePoint& p1 = pts_[segs_[seg].k1 % pts_.size()];
        tx = p1.x - p0.x; ty = p1.y - p0.y;
        len = std::hypot(tx, ty);
    }
    if (len > 1e-9) { out.tangent[0] = static_cast<f32>(tx / len); out.tangent[1] = static_cast<f32>(ty / len); }
    return out;
}

void Spline::sample(f32 stepCm, std::vector<Sample>& out) const {
    out.clear();
    if (pts_.empty()) return;
    if (segs_.empty() || length_ <= 0.0f) { out.push_back(at(0.0f)); return; }
    const u32 nseg = static_cast<u32>(segs_.size());
    const f32 step = std::max(stepCm, 1.0f);
    u32 steps = std::max(static_cast<u32>(std::ceil(length_ / step)), 4u * nseg);
    steps = std::min(steps, closed_ ? 4096u : 4095u);   // the cap is on samples; an open curve has steps + 1
    const u32 count = closed_ ? steps : steps + 1;
    out.reserve(count);
    for (u32 i = 0; i < count; ++i) out.push_back(at(length_ * static_cast<f32>(i) / static_cast<f32>(steps)));
}

bool Spline::closest(f32 x, f32 y, f32& outS, f32& outDist) const {
    if (tabS_.empty()) return false;
    if (tabS_.size() == 1) {
        outS = 0.0f;
        outDist = std::hypot(x - tabXY_[0], y - tabXY_[1]);
        return true;
    }
    f32 best = 3.4e38f;
    for (usize e = 0; e + 1 < tabS_.size(); ++e) {
        const f32 ax = tabXY_[e * 2], ay = tabXY_[e * 2 + 1], bx = tabXY_[e * 2 + 2], by = tabXY_[e * 2 + 3];
        const f32 ex = bx - ax, ey = by - ay;
        const f32 l2 = ex * ex + ey * ey;
        const f32 t = l2 > 1e-12f ? std::clamp(((x - ax) * ex + (y - ay) * ey) / l2, 0.0f, 1.0f) : 0.0f;
        const f32 dx = ax + ex * t - x, dy = ay + ey * t - y;
        const f32 d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; outS = tabS_[e] + (tabS_[e + 1] - tabS_[e]) * t; }
    }
    outDist = std::sqrt(best);
    return true;
}

// ---- River mesh ----

void buildRiverMesh(const RiverDesc& r, f32 stepCm, std::vector<rhi::MeshVertex>& v, std::vector<u32>& idx) {
    v.clear();
    idx.clear();
    std::vector<Spline::Sample> rows;
    if (r.spline.pointCount() >= 2) r.spline.sample(std::min(stepCm, 250.0f), rows);
    if (rows.size() < 2) {
        static bool logged = false;
        if (!logged) { logged = true; AVER_WARN("[River] '{}' needs at least 2 points; nothing is drawn", r.name); }
        return;
    }
    const usize n = rows.size();
    v.reserve(n * 3);
    for (usize i = 0; i < n; ++i) {
        const Spline::Sample& s = rows[i];
        const Spline::Sample& a = rows[i > 0 ? i - 1 : i];
        const Spline::Sample& b = rows[i + 1 < n ? i + 1 : i];
        const f32 ds = b.s - a.s;
        const f32 dzds = ds > 1e-4f ? (b.pos[2] - a.pos[2]) / ds : 0.0f;
        f32 nx = -s.tangent[0] * dzds, ny = -s.tangent[1] * dzds, nz = 1.0f;
        const f32 il = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
        nx *= il; ny *= il; nz *= il;
        const f32 lx = -s.tangent[1], ly = s.tangent[0];   // left of travel
        const f32 half = s.width * 0.5f;
        const f32 flow = s.flow * r.flowScale;
        for (int c = 0; c < 3; ++c) {
            const f32 side = c == 0 ? 1.0f : c == 1 ? 0.0f : -1.0f;
            const f32 profile = c == 1 ? 1.0f : r.bankSlow;
            rhi::MeshVertex mv{};
            mv.px = s.pos[0] + lx * half * side;
            mv.py = s.pos[1] + ly * half * side;
            mv.pz = s.pos[2];
            mv.nx = nx; mv.ny = ny; mv.nz = nz;
            mv.u = s.tangent[0] * flow * profile;
            mv.v = s.tangent[1] * flow * profile;
            v.push_back(mv);
        }
    }
    idx.reserve((n - 1) * 12);
    for (u32 i = 0; i + 1 < n; ++i) {
        for (u32 j = 0; j < 2; ++j) {
            const u32 a = i * 3 + j, b = (i + 1) * 3 + j, c = a + 1, d = b + 1;
            idx.push_back(a); idx.push_back(b); idx.push_back(c);
            idx.push_back(b); idx.push_back(d); idx.push_back(c);
        }
    }
}

// ---- Lake mesh ----

namespace {

f64 signedArea(const std::vector<f64>& xy) {
    f64 a = 0;
    const usize n = xy.size() / 2;
    for (usize i = 0, j = n - 1; i < n; j = i++) a += xy[j * 2] * xy[i * 2 + 1] - xy[i * 2] * xy[j * 2 + 1];
    return 0.5 * a;
}

f64 cross2(const f64* a, const f64* b, const f64* c) {
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}

// Ear clipping a simple CCW polygon; returns CCW triangles as index triples into `xy`.
void earClip(const std::vector<f64>& xy, std::vector<u32>& tris) {
    const usize n = xy.size() / 2;
    std::vector<u32> ring(n);
    for (usize i = 0; i < n; ++i) ring[i] = static_cast<u32>(i);
    auto P = [&](u32 i) { return &xy[static_cast<usize>(i) * 2]; };
    usize guard = 0;
    while (ring.size() > 3 && guard++ < n * n + 16) {
        bool clipped = false;
        const usize m = ring.size();
        for (usize i = 0; i < m && !clipped; ++i) {
            const u32 ia = ring[(i + m - 1) % m], ib = ring[i], ic = ring[(i + 1) % m];
            if (cross2(P(ia), P(ib), P(ic)) <= 1e-9) continue;   // reflex or degenerate
            bool empty = true;
            for (usize k = 0; k < m && empty; ++k) {
                const u32 ik = ring[k];
                if (ik == ia || ik == ib || ik == ic) continue;
                const f64 d1 = cross2(P(ia), P(ib), P(ik)), d2 = cross2(P(ib), P(ic), P(ik)), d3 = cross2(P(ic), P(ia), P(ik));
                if (d1 >= -1e-9 && d2 >= -1e-9 && d3 >= -1e-9) empty = false;
            }
            if (!empty) continue;
            tris.push_back(ia); tris.push_back(ib); tris.push_back(ic);
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
        }
        if (!clipped) {   // numerically stuck: drop the flattest vertex so the loop always ends
            usize best = 0;
            f64 bc = 1e300;
            for (usize i = 0; i < m; ++i) {
                const f64 c = std::fabs(cross2(P(ring[(i + m - 1) % m]), P(ring[i]), P(ring[(i + 1) % m])));
                if (c < bc) { bc = c; best = i; }
            }
            ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(best));
        }
    }
    if (ring.size() == 3) { tris.push_back(ring[0]); tris.push_back(ring[1]); tris.push_back(ring[2]); }
}

} // namespace

void buildLakeMesh(const LakeDesc& l, std::vector<rhi::MeshVertex>& v, std::vector<u32>& idx) {
    v.clear();
    idx.clear();
    if (l.outline.pointCount() < 3) return;
    std::vector<Spline::Sample> samples;
    l.outline.sample(300.0f, samples);
    if (samples.size() < 3) return;
    std::vector<f64> xy;
    xy.reserve(samples.size() * 2);
    for (const Spline::Sample& s : samples) { xy.push_back(s.pos[0]); xy.push_back(s.pos[1]); }
    if (signedArea(xy) < 0) {   // normalise to counter-clockwise
        std::vector<f64> r(xy.size());
        const usize n = xy.size() / 2;
        for (usize i = 0; i < n; ++i) { r[i * 2] = xy[(n - 1 - i) * 2]; r[i * 2 + 1] = xy[(n - 1 - i) * 2 + 1]; }
        xy.swap(r);
    }
    std::vector<u32> tris;
    earClip(xy, tris);
    if (tris.empty()) return;

    std::vector<f64> px = xy;   // grows with the midpoints
    const f64 maxEdge = std::max<f64>(l.maxEdgeCm, 50.0);
    auto len = [&](u32 a, u32 b) { return std::hypot(px[a * 2] - px[b * 2], px[a * 2 + 1] - px[b * 2 + 1]); };
    auto key = [](u32 a, u32 b) { return a < b ? (static_cast<u64>(a) << 32) | b : (static_cast<u64>(b) << 32) | a; };
    for (int pass = 0; pass < 14; ++pass) {
        std::map<u64, u32> mid;   // split edge -> midpoint vertex (assigned below)
        bool any = false;
        for (usize t = 0; t < tris.size(); t += 3)
            for (int e = 0; e < 3; ++e) {
                const u32 a = tris[t + e], b = tris[t + (e + 1) % 3];
                if (len(a, b) > maxEdge) { mid[key(a, b)] = 0; any = true; }
            }
        if (!any) break;
        // Close the set: a triangle with one split edge splits all three, so its neighbours must follow.
        for (bool grew = true; grew;) {
            grew = false;
            for (usize t = 0; t < tris.size(); t += 3) {
                const u64 k0 = key(tris[t], tris[t + 1]), k1 = key(tris[t + 1], tris[t + 2]), k2 = key(tris[t + 2], tris[t]);
                const bool h0 = mid.count(k0) != 0, h1 = mid.count(k1) != 0, h2 = mid.count(k2) != 0;
                if ((h0 || h1 || h2) && !(h0 && h1 && h2)) {
                    mid[k0] = 0; mid[k1] = 0; mid[k2] = 0;
                    grew = true;
                }
            }
        }
        for (auto& kv : mid) {
            const u32 a = static_cast<u32>(kv.first >> 32), b = static_cast<u32>(kv.first & 0xffffffffu);
            kv.second = static_cast<u32>(px.size() / 2);
            px.push_back(0.5 * (px[a * 2] + px[b * 2]));
            px.push_back(0.5 * (px[a * 2 + 1] + px[b * 2 + 1]));
        }
        std::vector<u32> next;
        next.reserve(tris.size() * 2);
        for (usize t = 0; t < tris.size(); t += 3) {
            const u32 a = tris[t], b = tris[t + 1], c = tris[t + 2];
            const auto ab = mid.find(key(a, b));
            if (ab == mid.end()) { next.push_back(a); next.push_back(b); next.push_back(c); continue; }
            const u32 mab = ab->second, mbc = mid[key(b, c)], mca = mid[key(c, a)];
            const u32 q[12] = {a, mab, mca,  mab, b, mbc,  mca, mbc, c,  mab, mbc, mca};
            next.insert(next.end(), q, q + 12);
        }
        tris.swap(next);
    }

    const usize nv = px.size() / 2;
    v.resize(nv);
    for (usize i = 0; i < nv; ++i) {
        rhi::MeshVertex& mv = v[i];
        mv = rhi::MeshVertex{};
        mv.px = static_cast<f32>(px[i * 2]);
        mv.py = static_cast<f32>(px[i * 2 + 1]);
        mv.pz = l.levelCm;
        mv.nz = 1.0f;
    }
    // CCW triangles flipped to the engine's front-facing-from-+Z winding.
    idx.reserve(tris.size());
    for (usize t = 0; t < tris.size(); t += 3) { idx.push_back(tris[t]); idx.push_back(tris[t + 2]); idx.push_back(tris[t + 1]); }
}

// ---- WaterBodySet ----

namespace {
constexpr f32 kCellCm = 1000.0f;
constexpr f32 kRiverRowCm = 250.0f;
constexpr f32 kLakeStepCm = 300.0f;
}

void WaterBodySet::clear() {
    bodies_.clear();
    cells_.clear();
}

u32 WaterBodySet::addRiver(RiverDesc r) {
    Body b;
    b.kind = WaterKind::River;
    b.depthCm = r.depthCm;
    b.bankSlow = r.bankSlow;
    b.flowScale = r.flowScale;
    if (r.spline.pointCount() >= 2) r.spline.sample(kRiverRowCm, b.rows);
    bodies_.push_back(std::move(b));
    return static_cast<u32>(bodies_.size() - 1);
}

u32 WaterBodySet::addLake(LakeDesc l) {
    Body b;
    b.kind = WaterKind::Lake;
    b.depthCm = l.depthCm;
    b.levelCm = l.levelCm;
    if (l.outline.pointCount() >= 3) {
        std::vector<Spline::Sample> s;
        l.outline.sample(kLakeStepCm, s);
        for (const Spline::Sample& p : s) { b.poly.push_back(p.pos[0]); b.poly.push_back(p.pos[1]); }
        if (!b.poly.empty()) {
            b.minX = b.maxX = b.poly[0]; b.minY = b.maxY = b.poly[1];
            for (usize i = 0; i + 1 < b.poly.size(); i += 2) {
                b.minX = std::min(b.minX, b.poly[i]); b.maxX = std::max(b.maxX, b.poly[i]);
                b.minY = std::min(b.minY, b.poly[i + 1]); b.maxY = std::max(b.maxY, b.poly[i + 1]);
            }
        }
    }
    bodies_.push_back(std::move(b));
    return static_cast<u32>(bodies_.size() - 1);
}

void WaterBodySet::finalize() {
    cells_.clear();
    for (u32 bi = 0; bi < bodies_.size(); ++bi) {
        const Body& b = bodies_[bi];
        if (b.kind != WaterKind::River) continue;
        for (u32 k = 0; k + 1 < b.rows.size(); ++k) {
            const Spline::Sample& p = b.rows[k];
            const Spline::Sample& q = b.rows[k + 1];
            const f32 pad = 0.5f * std::max(p.width, q.width) + 1.0f;
            const i32 x0 = static_cast<i32>(std::floor((std::min(p.pos[0], q.pos[0]) - pad) / kCellCm));
            const i32 x1 = static_cast<i32>(std::floor((std::max(p.pos[0], q.pos[0]) + pad) / kCellCm));
            const i32 y0 = static_cast<i32>(std::floor((std::min(p.pos[1], q.pos[1]) - pad) / kCellCm));
            const i32 y1 = static_cast<i32>(std::floor((std::max(p.pos[1], q.pos[1]) + pad) / kCellCm));
            for (i32 cy = y0; cy <= y1; ++cy)
                for (i32 cx = x0; cx <= x1; ++cx) cells_[cellKey(cx, cy)].push_back({bi, k});
        }
    }
}

bool WaterBodySet::sample(f32 x, f32 y, WaterQuery& out) const {
    out = WaterQuery{};
    bool have = false;
    auto consider = [&](f32 z, WaterKind kind, u32 body, const f32 nrm[3], const f32 cur[3], f32 authoredDepth) {
        if (have && z <= out.surfaceZ) return;
        have = true;
        out.over = true;
        out.surfaceZ = z;
        out.kind = kind;
        out.body = body;
        for (int i = 0; i < 3; ++i) { out.normal[i] = nrm[i]; out.current[i] = cur[i]; }
        out.depthCm = authoredDepth;
    };
    for (u32 bi = 0; bi < bodies_.size(); ++bi) {
        const Body& b = bodies_[bi];
        if (b.kind != WaterKind::Lake || b.poly.size() < 6) continue;
        if (x < b.minX || x > b.maxX || y < b.minY || y > b.maxY) continue;
        bool inside = false;
        const usize n = b.poly.size() / 2;
        for (usize i = 0, j = n - 1; i < n; j = i++) {
            const f32 ax = b.poly[j * 2], ay = b.poly[j * 2 + 1], bx = b.poly[i * 2], by = b.poly[i * 2 + 1];
            if ((ay > y) != (by > y) && x < (bx - ax) * (y - ay) / (by - ay) + ax) inside = !inside;
        }
        if (inside) {
            const f32 up[3] = {0, 0, 1}, none[3] = {0, 0, 0};
            consider(b.levelCm, WaterKind::Lake, bi, up, none, b.depthCm);
        }
    }
    const auto it = cells_.find(cellKey(static_cast<i32>(std::floor(x / kCellCm)), static_cast<i32>(std::floor(y / kCellCm))));
    if (it != cells_.end()) {
        for (const Entry& e : it->second) {
            const Body& b = bodies_[e.body];
            const Spline::Sample& p = b.rows[e.seg];
            const Spline::Sample& q = b.rows[e.seg + 1];
            const f32 ex = q.pos[0] - p.pos[0], ey = q.pos[1] - p.pos[1];
            const f32 l2 = ex * ex + ey * ey;
            const f32 t = l2 > 1e-12f ? std::clamp(((x - p.pos[0]) * ex + (y - p.pos[1]) * ey) / l2, 0.0f, 1.0f) : 0.0f;
            const f32 cx = p.pos[0] + ex * t, cy = p.pos[1] + ey * t;
            // Lateral distance, signed against the tangent so a segment end cannot claim the far bank.
            const f32 lateral = std::hypot(x - cx, y - cy);
            const f32 width = p.width + (q.width - p.width) * t;
            const f32 half = 0.5f * width;
            if (half <= 0.0f || lateral > half) continue;
            const f32 z = p.pos[2] + (q.pos[2] - p.pos[2]) * t;
            f32 tx = p.tangent[0] + (q.tangent[0] - p.tangent[0]) * t;
            f32 ty = p.tangent[1] + (q.tangent[1] - p.tangent[1]) * t;
            const f32 tl = std::hypot(tx, ty);
            if (tl > 1e-6f) { tx /= tl; ty /= tl; }
            const f32 flow = (p.flow + (q.flow - p.flow) * t) * b.flowScale;
            const f32 profile = 1.0f + (b.bankSlow - 1.0f) * std::min(lateral / half, 1.0f);
            const f32 cur[3] = {tx * flow * profile, ty * flow * profile, 0.0f};
            const f32 ds = q.s - p.s;
            const f32 dzds = ds > 1e-4f ? (q.pos[2] - p.pos[2]) / ds : 0.0f;
            f32 nrm[3] = {-tx * dzds, -ty * dzds, 1.0f};
            const f32 il = 1.0f / std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + 1.0f);
            for (f32& c : nrm) c *= il;
            consider(z, WaterKind::River, e.body, nrm, cur, b.depthCm);
        }
    }
    if (have && ground) {
        f32 g = 0.0f;
        if (ground(x, y, g)) out.depthCm = std::max(out.surfaceZ - g, 0.0f);
    }
    return have;
}

} // namespace aver::fluids
