// Weightmap paint: see Paint.hpp.
#include "aver/landscape/Paint.hpp"

#include <algorithm>
#include <cmath>

namespace aver::landscape {
namespace {

constexpr usize kMaxL = fmt::kOcLandMaxLayers;

usize nnOf(const fmt::OcLandData& d) { return static_cast<usize>(d.sampleCount) * d.sampleCount; }

// Reads the float weights (sum 1) of one sample into w[0..L).
void loadWeights(const fmt::OcLandData& d, usize at, usize L, f32 w[kMaxL]) {
    const usize nn = nnOf(d);
    int sum = 0;
    for (usize p = 0; p + 1 < L; ++p) {
        const u8 v = d.weights[p * nn + at];
        w[p + 1] = static_cast<f32>(v) / 255.0f;
        sum += v;
    }
    w[0] = static_cast<f32>(sum >= 255 ? 0 : 255 - sum) / 255.0f;
}

// Quantises w[0..L) (any scale > 0) to bytes summing to exactly 255 by largest remainder, ties to the
// lower index, and stores layers 1..L-1.
void storeWeights(fmt::OcLandData& d, usize at, usize L, const f32 w[kMaxL]) {
    const usize nn = nnOf(d);
    f32 total = 0.0f;
    for (usize i = 0; i < L; ++i) total += std::max(w[i], 0.0f);
    int q[kMaxL] = {};
    f32 frac[kMaxL] = {};
    int given = 0;
    if (total <= 1e-9f) {
        q[0] = 255;
        given = 255;
    } else {
        for (usize i = 0; i < L; ++i) {
            const f32 s = std::max(w[i], 0.0f) / total * 255.0f;
            const f32 fl = std::floor(s);
            q[i] = static_cast<int>(fl);
            frac[i] = s - fl;
            given += q[i];
        }
    }
    while (given < 255) {
        usize best = 0;
        for (usize i = 1; i < L; ++i)
            if (frac[i] > frac[best]) best = i;
        ++q[best];
        frac[best] = -1.0f;
        ++given;
    }
    while (given > 255) {   // float slop only
        usize best = 0;
        for (usize i = 1; i < L; ++i)
            if (q[i] > q[best]) best = i;
        --q[best];
        --given;
    }
    for (usize p = 0; p + 1 < L; ++p) d.weights[p * nn + at] = static_cast<u8>(q[p + 1]);
}

// One Paint step on a sample's float weights (b = brush share this tick).
void paintStep(f32 w[kMaxL], usize L, usize layer, f32 b, f32 target) {
    const f32 old = w[layer];
    if (old >= target || old >= 1.0f - 1e-6f) return;
    const f32 nw = old + (target - old) * b;
    const f32 k = (1.0f - nw) / (1.0f - old);
    for (usize i = 0; i < L; ++i) w[i] = i == layer ? nw : w[i] * k;
}

void eraseStep(f32 w[kMaxL], usize L, usize layer, f32 b) {
    if (layer == 0) return;
    const f32 old = w[layer];
    const f32 nw = old * (1.0f - b);
    const f32 freed = old - nw;
    f32 others = 0.0f;
    for (usize i = 0; i < L; ++i)
        if (i != layer) others += w[i];
    w[layer] = nw;
    if (others > 1e-6f) {
        for (usize i = 0; i < L; ++i)
            if (i != layer) w[i] += freed * w[i] / others;
    } else {
        w[0] += freed;
    }
}

} // namespace

BrushRect paintRect(const fmt::OcLandData& d, const PaintParams& p) {
    BrushParams bp;
    bp.centerCm[0] = p.centerCm[0];
    bp.centerCm[1] = p.centerCm[1];
    bp.radiusCm = p.radiusCm;
    return brushRect(d, bp);
}

BrushRect applyPaint(fmt::OcLandData& d, const PaintParams& p, f32 amount) {
    const BrushRect r = paintRect(d, p);
    const usize L = d.layerNames.size();
    if (r.empty || amount <= 0.0f || L < 2 || p.layer >= L || !d.layersValid()) return r;

    // Smooth reads neighbours: snapshot the padded region's float weights first.
    std::vector<f32> snap;
    u32 sx0 = 0, sy0 = 0, sw = 0;
    if (p.mode == PaintMode::Smooth) {
        sx0 = r.x0 > 0 ? r.x0 - 1 : 0;
        sy0 = r.y0 > 0 ? r.y0 - 1 : 0;
        const u32 sx1 = std::min(r.x1 + 1, d.sampleCount - 1);
        const u32 sy1 = std::min(r.y1 + 1, d.sampleCount - 1);
        sw = sx1 - sx0 + 1;
        snap.resize(static_cast<usize>(sw) * (sy1 - sy0 + 1) * kMaxL);
        for (u32 y = sy0; y <= sy1; ++y)
            for (u32 x = sx0; x <= sx1; ++x)
                loadWeights(d, static_cast<usize>(y) * d.sampleCount + x, L,
                            &snap[(static_cast<usize>(y - sy0) * sw + (x - sx0)) * kMaxL]);
    }

    for (u32 y = r.y0; y <= r.y1; ++y) {
        for (u32 x = r.x0; x <= r.x1; ++x) {
            const f32 wx = d.originCm[0] + static_cast<f32>(x) * d.spacingCm;
            const f32 wy = d.originCm[1] + static_cast<f32>(y) * d.spacingCm;
            const f32 dx = wx - p.centerCm[0], dy = wy - p.centerCm[1];
            const f32 dist = std::sqrt(dx * dx + dy * dy);
            if (dist > p.radiusCm) continue;
            const f32 b = std::clamp(amount * p.strength * brushFalloff(dist / p.radiusCm, p.falloff), 0.0f, 1.0f);
            if (b <= 0.0f) continue;
            const usize at = static_cast<usize>(y) * d.sampleCount + x;
            f32 w[kMaxL] = {};
            loadWeights(d, at, L, w);
            switch (p.mode) {
                case PaintMode::Paint: paintStep(w, L, p.layer, b, p.target); break;
                case PaintMode::Erase: eraseStep(w, L, p.layer, b); break;
                case PaintMode::Smooth: {
                    f32 mean[kMaxL] = {};
                    int cnt = 0;
                    for (int oy = -1; oy <= 1; ++oy) {
                        for (int ox = -1; ox <= 1; ++ox) {
                            const i32 nx = static_cast<i32>(x) + ox, ny = static_cast<i32>(y) + oy;
                            if (nx < 0 || ny < 0 || nx >= static_cast<i32>(d.sampleCount) || ny >= static_cast<i32>(d.sampleCount))
                                continue;
                            const usize si = static_cast<usize>(ny - static_cast<i32>(sy0)) * sw + static_cast<usize>(nx - static_cast<i32>(sx0));
                            const f32* s = &snap[si * kMaxL];
                            for (usize i = 0; i < L; ++i) mean[i] += s[i];
                            ++cnt;
                        }
                    }
                    for (usize i = 0; i < L; ++i) w[i] += (mean[i] / static_cast<f32>(cnt) - w[i]) * b;
                    break;
                }
            }
            storeWeights(d, at, L, w);
        }
    }
    return r;
}

bool ensureLayer(fmt::OcLandData& d, std::string_view name, u32& outIndex) {
    const std::string nm(name);
    if (!fmt::ocLandLayerNameValid(nm)) return false;
    for (usize i = 0; i < d.layerNames.size(); ++i) {
        if (d.layerNames[i] == nm) { outIndex = static_cast<u32>(i); return true; }
    }
    if (d.layerNames.size() >= kMaxL || !d.valid()) return false;
    d.layerNames.push_back(nm);
    if (d.layerNames.size() > 1) d.weights.resize(d.weights.size() + nnOf(d), 0);
    outIndex = static_cast<u32>(d.layerNames.size() - 1);
    return true;
}

bool removeLayer(fmt::OcLandData& d, u32 index) {
    if (index == 0 || index >= d.layerNames.size() || !d.layersValid()) return false;
    const usize nn = nnOf(d);
    const auto first = d.weights.begin() + static_cast<std::ptrdiff_t>((index - 1) * nn);
    d.weights.erase(first, first + static_cast<std::ptrdiff_t>(nn));
    d.layerNames.erase(d.layerNames.begin() + index);
    return true;
}

void fillLayer(fmt::OcLandData& d, u32 layer) {
    if (!d.layersValid() || layer >= d.layerNames.size()) return;
    std::fill(d.weights.begin(), d.weights.end(), static_cast<u8>(0));
    if (layer == 0) return;
    const usize nn = nnOf(d);
    std::fill(d.weights.begin() + static_cast<std::ptrdiff_t>((layer - 1) * nn),
              d.weights.begin() + static_cast<std::ptrdiff_t>(layer * nn), static_cast<u8>(255));
}

BrushRect autoFillBySlope(fmt::OcLandData& d, u32 layer, f32 minSlopeDeg, f32 maxSlopeDeg,
                          f32 minZCm, f32 maxZCm, f32 featherDeg, f32 amount) {
    BrushRect r;
    const usize L = d.layerNames.size();
    if (!d.valid() || !d.layersValid() || layer >= L || L < 2 || amount <= 0.0f) return r;
    const u32 n = d.sampleCount;
    r.x0 = 0; r.y0 = 0; r.x1 = n - 1; r.y1 = n - 1; r.empty = false;
    const f32 toDeg = 57.29577951f;
    const f32 feather = std::max(featherDeg, 0.0f);
    for (u32 y = 0; y < n; ++y) {
        for (u32 x = 0; x < n; ++x) {
            const f32 z = d.heightAt(x, y);
            if (z < minZCm || z > maxZCm) continue;
            const u32 xm = x > 0 ? x - 1 : x, xp = x + 1 < n ? x + 1 : x;
            const u32 ym = y > 0 ? y - 1 : y, yp = y + 1 < n ? y + 1 : y;
            const f32 gx = (d.heightAt(xp, y) - d.heightAt(xm, y)) / (static_cast<f32>(xp - xm) * d.spacingCm);
            const f32 gy = (d.heightAt(x, yp) - d.heightAt(x, ym)) / (static_cast<f32>(yp - ym) * d.spacingCm);
            const f32 slope = std::atan(std::sqrt(gx * gx + gy * gy)) * toDeg;
            f32 aff = 1.0f;
            if (slope < minSlopeDeg) aff = feather > 0.0f ? 1.0f - (minSlopeDeg - slope) / feather : 0.0f;
            else if (slope > maxSlopeDeg) aff = feather > 0.0f ? 1.0f - (slope - maxSlopeDeg) / feather : 0.0f;
            aff = std::clamp(aff, 0.0f, 1.0f) * std::min(amount, 1.0f);
            if (aff <= 0.0f) continue;
            const usize at = static_cast<usize>(y) * n + x;
            f32 w[kMaxL] = {};
            loadWeights(d, at, L, w);
            paintStep(w, L, layer, aff, 1.0f);
            storeWeights(d, at, L, w);
        }
    }
    return r;
}

bool layerWeightsAt(const fmt::OcLandData& d, f32 worldX, f32 worldY, f32 out[8]) {
    for (int i = 0; i < 8; ++i) out[i] = 0.0f;
    if (!d.valid()) return false;
    const f32 fx = (worldX - d.originCm[0]) / d.spacingCm;
    const f32 fy = (worldY - d.originCm[1]) / d.spacingCm;
    const f32 maxF = static_cast<f32>(d.sampleCount - 1);
    if (!(fx >= 0.0f && fy >= 0.0f && fx <= maxF && fy <= maxF)) return false;
    const usize L = d.layerNames.size();
    if (L == 0 || !d.layersValid()) { out[0] = 1.0f; return true; }
    const u32 x0 = static_cast<u32>(fx), y0 = static_cast<u32>(fy);
    const u32 x1 = std::min(x0 + 1, d.sampleCount - 1), y1 = std::min(y0 + 1, d.sampleCount - 1);
    const f32 tx = fx - static_cast<f32>(x0), ty = fy - static_cast<f32>(y0);
    for (usize l = 0; l < L; ++l) {
        const u32 li = static_cast<u32>(l);
        const f32 a = d.weightAt(li, x0, y0), b = d.weightAt(li, x1, y0);
        const f32 c = d.weightAt(li, x0, y1), e = d.weightAt(li, x1, y1);
        const f32 top = a + (b - a) * tx, bot = c + (e - c) * tx;
        out[l] = (top + (bot - top) * ty) / 255.0f;
    }
    f32 sum = 0.0f;
    for (int i = 0; i < 8; ++i) sum += out[i];
    if (sum > 1e-6f) { for (int i = 0; i < 8; ++i) out[i] /= sum; }
    else out[0] = 1.0f;
    return true;
}

u32 dominantLayer(const f32 w[8], u32 count) {
    u32 best = 0;
    for (u32 i = 1; i < count && i < 8; ++i)
        if (w[i] > w[best]) best = i;
    return best;
}

void packWeightTextures(const fmt::OcLandData& d, const i32 chan[8], u32 matCount,
                        std::vector<u8>& rgbaA, std::vector<u8>& rgbaB) {
    const usize nn = nnOf(d);
    rgbaA.assign(nn * 4, 0);
    rgbaB.assign(nn * 4, 0);
    const usize L = d.layerNames.size();
    if (nn == 0 || L < 2 || !d.layersValid()) return;
    const u32 cap = std::min<u32>(matCount, 8);
    // Section layers map to material channels by the caller's table (the section's own base layer
    // included: it is implicit in the section but may be any channel of the material).
    for (usize i = 0; i < nn; ++i) {
        int mw[8] = {};
        for (u32 s = 0; s < L; ++s) {
            const i32 c = chan[s];
            const u32 ch = (c >= 0 && static_cast<u32>(c) < cap) ? static_cast<u32>(c) : 0u;
            mw[ch] += s == 0 ? [&] {
                int sum = 0;
                for (usize p = 0; p + 1 < L; ++p) sum += d.weights[p * nn + i];
                return sum >= 255 ? 0 : 255 - sum;
            }() : d.weights[(s - 1) * nn + i];
        }
        for (u32 c = 1; c < 8; ++c) {
            const u8 v = static_cast<u8>(std::min(mw[c], 255));
            if (c <= 4) rgbaA[i * 4 + (c - 1)] = v;
            else rgbaB[i * 4 + (c - 5)] = v;
        }
    }
}

void buildMipChainRGBA8(u32 n, const std::vector<u8>& level0, std::vector<std::vector<u8>>& levels) {
    levels.clear();
    if (n == 0 || level0.size() != static_cast<usize>(n) * n * 4) return;
    levels.push_back(level0);
    u32 cur = n;
    while (cur > 1) {
        const u32 nx = std::max(1u, cur / 2);
        const std::vector<u8>& src = levels.back();
        std::vector<u8> dst(static_cast<usize>(nx) * nx * 4);
        for (u32 y = 0; y < nx; ++y) {
            for (u32 x = 0; x < nx; ++x) {
                const u32 x0 = std::min(2 * x, cur - 1), x1 = std::min(2 * x + 1, cur - 1);
                const u32 y0 = std::min(2 * y, cur - 1), y1 = std::min(2 * y + 1, cur - 1);
                for (u32 c = 0; c < 4; ++c) {
                    const u32 sum = src[(static_cast<usize>(y0) * cur + x0) * 4 + c] + src[(static_cast<usize>(y0) * cur + x1) * 4 + c] +
                                    src[(static_cast<usize>(y1) * cur + x0) * 4 + c] + src[(static_cast<usize>(y1) * cur + x1) * 4 + c];
                    dst[(static_cast<usize>(y) * nx + x) * 4 + c] = static_cast<u8>((sum + 2) / 4);
                }
            }
        }
        levels.push_back(std::move(dst));
        cur = nx;
    }
}

} // namespace aver::landscape
