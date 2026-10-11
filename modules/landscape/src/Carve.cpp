// River and basin carving: see Carve.hpp.
#include "aver/landscape/Carve.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aver::landscape {
namespace {

BrushRect rectFromBox(const fmt::OcLandData& d, f32 minX, f32 minY, f32 maxX, f32 maxY) {
    BrushRect r;
    if (!d.valid()) return r;
    const f32 gmaxX = d.originCm[0] + d.extentCm(), gmaxY = d.originCm[1] + d.extentCm();
    if (maxX < d.originCm[0] || minX > gmaxX || maxY < d.originCm[1] || minY > gmaxY) return r;
    const i32 hi = static_cast<i32>(d.sampleCount) - 1;
    auto idx = [&](f32 w, f32 o, bool up) {
        const f32 f = (w - o) / d.spacingCm;
        return std::clamp(static_cast<i32>(up ? std::ceil(f) : std::floor(f)), 0, hi);
    };
    r.x0 = static_cast<u32>(idx(minX, d.originCm[0], false));
    r.x1 = static_cast<u32>(idx(maxX, d.originCm[0], true));
    r.y0 = static_cast<u32>(idx(minY, d.originCm[1], false));
    r.y1 = static_cast<u32>(idx(maxY, d.originCm[1], true));
    r.empty = false;
    return r;
}

void refreshBounds(fmt::OcLandData& d) {
    f32 lo = d.heights[0], hi = d.heights[0];
    for (f32 v : d.heights) { lo = std::fmin(lo, v); hi = std::fmax(hi, v); }
    d.boundsMin[0] = d.originCm[0];
    d.boundsMin[1] = d.originCm[1];
    d.boundsMin[2] = lo;
    d.boundsMax[0] = d.originCm[0] + d.extentCm();
    d.boundsMax[1] = d.originCm[1] + d.extentCm();
    d.boundsMax[2] = hi;
}

// One 3x3 relaxation over the carved samples, blending toward the neighbourhood mean by `smooth`, and
// never ending above the pre-carve height (so the carve still only lowers).
void relax(fmt::OcLandData& d, const BrushRect& r, const std::vector<f32>& original, f32 smooth) {
    if (smooth <= 0.0f) return;
    const f32 s = std::min(smooth, 1.0f);
    const u32 n = d.sampleCount;
    const std::vector<f32> cur = d.heights;
    for (u32 y = r.y0; y <= r.y1; ++y) {
        for (u32 x = r.x0; x <= r.x1; ++x) {
            const usize at = static_cast<usize>(y) * n + x;
            if (!(cur[at] < original[at])) continue;
            f32 sum = 0.0f;
            int cnt = 0;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const i32 nx = static_cast<i32>(x) + ox, ny = static_cast<i32>(y) + oy;
                    if (nx < 0 || ny < 0 || nx >= static_cast<i32>(n) || ny >= static_cast<i32>(n)) continue;
                    sum += cur[static_cast<usize>(ny) * n + static_cast<usize>(nx)];
                    ++cnt;
                }
            }
            const f32 v = cur[at] + (sum / static_cast<f32>(cnt) - cur[at]) * s;
            d.heights[at] = std::min(v, original[at]);
        }
    }
}

} // namespace

BrushRect carveRect(const fmt::OcLandData& d, const CarvePoint* pts, u32 n, const CarveParams& p) {
    if (!pts || n == 0) return BrushRect{};
    f32 minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
    for (u32 i = 0; i < n; ++i) {
        const f32 reach = pts[i].halfWidth + p.bankCm;
        minX = std::min(minX, pts[i].x - reach); maxX = std::max(maxX, pts[i].x + reach);
        minY = std::min(minY, pts[i].y - reach); maxY = std::max(maxY, pts[i].y + reach);
    }
    return rectFromBox(d, minX, minY, maxX, maxY);
}

BrushRect basinRect(const fmt::OcLandData& d, const f32* poly, u32 count, const CarveParams& p) {
    if (!poly || count < 3) return BrushRect{};
    f32 minX = poly[0], maxX = poly[0], minY = poly[1], maxY = poly[1];
    for (u32 i = 0; i < count; ++i) {
        minX = std::min(minX, poly[i * 2]); maxX = std::max(maxX, poly[i * 2]);
        minY = std::min(minY, poly[i * 2 + 1]); maxY = std::max(maxY, poly[i * 2 + 1]);
    }
    return rectFromBox(d, minX - p.bankCm, minY - p.bankCm, maxX + p.bankCm, maxY + p.bankCm);
}

BrushRect carveRiver(fmt::OcLandData& d, const CarvePoint* pts, u32 n, const CarveParams& p) {
    const BrushRect r = carveRect(d, pts, n, p);
    if (r.empty || p.depthCm <= 0.0f) return r;
    const f32 tanSlope = std::tan(p.bankSlopeDeg * 0.017453292f);
    const std::vector<f32> original = d.heights;
    bool changed = false;
    for (u32 y = r.y0; y <= r.y1; ++y) {
        for (u32 x = r.x0; x <= r.x1; ++x) {
            const f32 wx = d.originCm[0] + static_cast<f32>(x) * d.spacingCm;
            const f32 wy = d.originCm[1] + static_cast<f32>(y) * d.spacingCm;
            // Nearest point on the polyline, carrying surfaceZ and halfWidth along the segment.
            f32 bestD2 = 1e30f, bz = 0.0f, bw = 0.0f;
            const u32 segs = n == 1 ? 1 : n - 1;
            for (u32 s = 0; s < segs; ++s) {
                const CarvePoint& a = pts[s];
                const CarvePoint& b = pts[n == 1 ? 0 : s + 1];
                const f32 ex = b.x - a.x, ey = b.y - a.y;
                const f32 len2 = ex * ex + ey * ey;
                f32 t = len2 > 1e-9f ? ((wx - a.x) * ex + (wy - a.y) * ey) / len2 : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                const f32 px = a.x + ex * t - wx, py = a.y + ey * t - wy;
                const f32 d2 = px * px + py * py;
                if (d2 < bestD2) {
                    bestD2 = d2;
                    bz = a.surfaceZ + (b.surfaceZ - a.surfaceZ) * t;
                    bw = a.halfWidth + (b.halfWidth - a.halfWidth) * t;
                }
            }
            const f32 lateral = std::sqrt(bestD2);
            if (lateral > bw + p.bankCm) continue;
            f32 bed;
            if (lateral <= bw) {
                const f32 q = bw > 1e-4f ? lateral / bw : 0.0f;
                bed = bz - p.depthCm * (1.0f - q * q);
            } else {
                bed = bz + (lateral - bw) * tanSlope;
            }
            f32& h = d.heights[static_cast<usize>(y) * d.sampleCount + x];
            if (bed < h) { h = bed; changed = true; }
        }
    }
    if (changed) {
        relax(d, r, original, p.smooth);
        refreshBounds(d);
    }
    return r;
}

BrushRect carveBasin(fmt::OcLandData& d, const f32* poly, u32 count, f32 levelCm, const CarveParams& p) {
    const BrushRect r = basinRect(d, poly, count, p);
    if (r.empty || p.depthCm <= 0.0f) return r;
    const f32 tanSlope = std::tan(p.bankSlopeDeg * 0.017453292f);
    const std::vector<f32> original = d.heights;
    bool changed = false;
    for (u32 y = r.y0; y <= r.y1; ++y) {
        for (u32 x = r.x0; x <= r.x1; ++x) {
            const f32 wx = d.originCm[0] + static_cast<f32>(x) * d.spacingCm;
            const f32 wy = d.originCm[1] + static_cast<f32>(y) * d.spacingCm;
            bool inside = false;
            f32 best2 = 1e30f;
            for (u32 i = 0, j = count - 1; i < count; j = i++) {
                const f32 ax = poly[j * 2], ay = poly[j * 2 + 1], bx = poly[i * 2], by = poly[i * 2 + 1];
                if ((ay > wy) != (by > wy) && wx < (bx - ax) * (wy - ay) / (by - ay) + ax) inside = !inside;
                const f32 ex = bx - ax, ey = by - ay;
                const f32 len2 = ex * ex + ey * ey;
                f32 t = len2 > 1e-9f ? ((wx - ax) * ex + (wy - ay) * ey) / len2 : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                const f32 px = ax + ex * t - wx, py = ay + ey * t - wy;
                best2 = std::min(best2, px * px + py * py);
            }
            const f32 dist = std::sqrt(best2);
            f32 bed;
            if (inside) {
                const f32 t = p.bankCm > 1e-4f ? std::clamp(dist / p.bankCm, 0.0f, 1.0f) : 1.0f;
                bed = levelCm - p.depthCm * (t * t * (3.0f - 2.0f * t));
            } else {
                if (dist > p.bankCm) continue;
                bed = levelCm + dist * tanSlope;
            }
            f32& h = d.heights[static_cast<usize>(y) * d.sampleCount + x];
            if (bed < h) { h = bed; changed = true; }
        }
    }
    if (changed) {
        relax(d, r, original, p.smooth);
        refreshBounds(d);
    }
    return r;
}

} // namespace aver::landscape
