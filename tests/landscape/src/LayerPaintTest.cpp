// Weightmap paint: brush modes keep every sample summing to exactly 255, Paint converges to its
// target, Erase conserves mass, layer table edits, bilinear lookup, slope auto-fill and the
// name-based remap into material weight textures. CPU only. Exit code = failure count.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/landscape/Paint.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::landscape;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static fmt::OcLandData makeFlat(u32 n, f32 spacing = 100.0f) {
    fmt::OcLandData d;
    d.sampleCount = n;
    d.spacingCm = spacing;
    d.heights.assign(static_cast<usize>(n) * n, 0.0f);
    return d;
}

static fmt::OcLandData makePaintable(u32 n, int layers) {
    fmt::OcLandData d = makeFlat(n);
    u32 idx = 0;
    const char* names[] = {"Grass", "Rock", "Sand", "Snow"};
    for (int i = 0; i < layers; ++i) ensureLayer(d, names[i], idx);
    return d;
}

static bool sumsAre255(const fmt::OcLandData& d) {
    u32 sum255 = 0;
    for (u32 y = 0; y < d.sampleCount; ++y)
        for (u32 x = 0; x < d.sampleCount; ++x) {
            int s = 0;
            for (u32 l = 0; l < d.layerNames.size(); ++l) s += d.weightAt(l, x, y);
            if (s == 255) ++sum255;
        }
    return sum255 == d.sampleCount * d.sampleCount;
}

int main() {
    AVER_INFO("=== layer table ===");
    {
        fmt::OcLandData d = makeFlat(17);
        u32 i = 99;
        check(ensureLayer(d, "Grass", i) && i == 0 && d.weights.empty(), "first layer is the base, no plane");
        check(ensureLayer(d, "Rock", i) && i == 1 && d.weights.size() == 17 * 17, "second layer adds a plane");
        check(ensureLayer(d, "Rock", i) && i == 1 && d.layerNames.size() == 2, "same name finds the existing layer");
        check(!ensureLayer(d, "bad name", i), "bad name refused");
        for (int k = 0; k < 6; ++k) ensureLayer(d, "L" + std::to_string(k), i);
        check(d.layerNames.size() == 8 && d.layersValid(), "eight layers allowed");
        check(!ensureLayer(d, "Ninth", i), "ninth refused");
    }

    AVER_INFO("=== paint ===");
    {
        fmt::OcLandData d = makePaintable(33, 3);
        PaintParams p;
        p.centerCm[0] = 1600.0f; p.centerCm[1] = 1600.0f;
        p.radiusCm = 600.0f;
        p.layer = 1;
        const BrushRect r = applyPaint(d, p, 1.0f);
        const BrushRect pr = paintRect(d, p);
        check(!r.empty && r.x0 == pr.x0 && r.x1 == pr.x1 && r.y0 == pr.y0 && r.y1 == pr.y1, "rect equals paintRect");
        check(sumsAre255(d), "sums are 255 after Paint");
        check(d.weightAt(1, 16, 16) == 255, "centre fully painted at strength 1");
        check(d.weightAt(1, 0, 0) == 0 && d.weights[0] == 0, "untouched samples are bit-identical");

        // Opacity cap and convergence
        fmt::OcLandData e = makePaintable(33, 3);
        p.target = 0.5f;
        p.strength = 0.5f;
        for (int k = 0; k < 40; ++k) applyPaint(e, p, 1.0f);
        const int w = e.weightAt(1, 16, 16);
        check(w >= 126 && w <= 129, "Paint converges to target 0.5 (got " + std::to_string(w) + ")");
        check(sumsAre255(e), "sums are 255 after repeated Paint");

        // Second layer over the first keeps proportions
        p.target = 1.0f; p.strength = 1.0f; p.layer = 2;
        applyPaint(d, p, 0.5f);
        check(sumsAre255(d) && d.weightAt(2, 16, 16) > 100 && d.weightAt(1, 16, 16) > 50, "paint over another layer blends and sums");

        // falloff shapes
        fmt::OcLandData f0 = makePaintable(33, 2), f1 = makePaintable(33, 2);
        PaintParams q;
        q.centerCm[0] = 1600.0f; q.centerCm[1] = 1600.0f; q.radiusCm = 1000.0f; q.layer = 1; q.strength = 1.0f;
        q.falloff = 0.0f; applyPaint(f0, q, 1.0f);
        q.falloff = 1.0f; applyPaint(f1, q, 1.0f);
        // sample 8 cells away (800 cm of 1000): hard disc is full, full smoothstep is partial
        check(f0.weightAt(1, 24, 16) == 255 && f1.weightAt(1, 24, 16) < 200, "falloff 0 is a hard disc, 1 a shoulder");
    }

    AVER_INFO("=== erase ===");
    {
        fmt::OcLandData d = makePaintable(33, 3);
        fillLayer(d, 2);
        PaintParams p;
        p.centerCm[0] = 1600.0f; p.centerCm[1] = 1600.0f; p.radiusCm = 600.0f; p.layer = 2; p.mode = PaintMode::Erase;
        p.strength = 0.5f;
        applyPaint(d, p, 1.0f);
        check(sumsAre255(d), "sums are 255 after Erase");
        check(d.weightAt(2, 16, 16) >= 126 && d.weightAt(2, 16, 16) <= 129, "erase halves the centre");
        check(d.weightAt(0, 16, 16) + d.weightAt(1, 16, 16) + d.weightAt(2, 16, 16) == 255, "freed mass lands on other layers");
        check(d.weightAt(0, 16, 16) >= 126, "with all others zero the base takes it");
        PaintParams b = p; b.layer = 0;
        const std::vector<u8> before = d.weights;
        applyPaint(d, b, 1.0f);
        check(before == d.weights, "erasing the base does nothing");
    }

    AVER_INFO("=== smooth ===");
    {
        fmt::OcLandData d = makePaintable(33, 2);
        PaintParams p;
        p.centerCm[0] = 1600.0f; p.centerCm[1] = 1600.0f; p.radiusCm = 300.0f; p.layer = 1; p.falloff = 0.0f;
        applyPaint(d, p, 1.0f);   // a hard blob
        auto mass = [&]() { int s = 0; for (u32 y = 0; y < 33; ++y) for (u32 x = 0; x < 33; ++x) s += d.weightAt(1, x, y); return s; };
        const int m0 = mass();
        PaintParams sm = p; sm.mode = PaintMode::Smooth; sm.radiusCm = 1200.0f; sm.falloff = 1.0f; sm.strength = 1.0f;
        for (int k = 0; k < 3; ++k) applyPaint(d, sm, 1.0f);
        check(sumsAre255(d), "sums are 255 after Smooth");
        const int m1 = mass();
        check(std::abs(m1 - m0) < m0 / 8, "smoothing roughly preserves the mass (" + std::to_string(m0) + " -> " + std::to_string(m1) + ")");
        check(d.weightAt(1, 20, 16) > 0, "the blob spread past its old edge");
    }

    AVER_INFO("=== remove / fill ===");
    {
        fmt::OcLandData d = makePaintable(9, 3);
        fillLayer(d, 2);
        check(d.weightAt(2, 4, 4) == 255 && d.weightAt(0, 4, 4) == 0, "fill sets the layer");
        check(removeLayer(d, 2) && d.layerNames.size() == 2 && d.layersValid(), "remove drops the layer");
        check(d.weightAt(0, 4, 4) == 255, "its weight folded into the base");
        check(!removeLayer(d, 0), "cannot remove the base");
    }

    AVER_INFO("=== layerWeightsAt ===");
    {
        fmt::OcLandData d = makePaintable(5, 2);
        d.weights[1 * 5 + 1] = 255;   // sample (1,1) is fully layer 1
        float w[8];
        check(layerWeightsAt(d, 100.0f, 100.0f, w) && std::fabs(w[1] - 1.0f) < 1e-5f, "exact at a sample centre");
        check(layerWeightsAt(d, 150.0f, 100.0f, w) && std::fabs(w[1] - 0.5f) < 1e-4f && std::fabs(w[0] - 0.5f) < 1e-4f, "bilinear between samples");
        check(!layerWeightsAt(d, -10.0f, 0.0f, w) && !layerWeightsAt(d, 500.0f, 0.0f, w), "false outside the footprint");
        float s = 0; for (int i = 0; i < 8; ++i) s += w[i];
        check(layerWeightsAt(d, 120.0f, 130.0f, w), "inside again");
        s = 0; for (int i = 0; i < 8; ++i) s += w[i];
        check(std::fabs(s - 1.0f) < 1e-4f, "weights sum to 1");
        // (120,130) is 0.8 x 0.7 of the way to the fully-layer-1 sample (1,1): w1 = 0.56.
        check(dominantLayer(w, 2) == 1, "dominant layer");
        const float tie[8] = {0.5f, 0.5f, 0, 0, 0, 0, 0, 0};
        check(dominantLayer(tie, 2) == 0, "ties go to the lower index");
        fmt::OcLandData un = makeFlat(5);
        check(layerWeightsAt(un, 100.0f, 100.0f, w) && w[0] == 1.0f, "unpainted = layer 0");
    }

    AVER_INFO("=== autoFillBySlope ===");
    {
        fmt::OcLandData d = makePaintable(33, 2);
        // west half flat, east half a 45 degree ramp (100 cm rise per 100 cm)
        for (u32 y = 0; y < 33; ++y)
            for (u32 x = 0; x < 33; ++x) d.heights[y * 33 + x] = x > 16 ? static_cast<f32>(x - 16) * 100.0f : 0.0f;
        autoFillBySlope(d, 1, 30.0f, 60.0f, -1e9f, 1e9f, 0.0f, 1.0f);
        check(d.weightAt(1, 24, 10) == 255, "steep band painted");
        check(d.weightAt(1, 6, 10) == 0, "flat band untouched");
        check(sumsAre255(d), "sums are 255");
        fmt::OcLandData g = makePaintable(33, 2);
        for (u32 y = 0; y < 33; ++y)
            for (u32 x = 0; x < 33; ++x) g.heights[y * 33 + x] = static_cast<f32>(x) * 100.0f;
        autoFillBySlope(g, 1, 30.0f, 60.0f, 0.0f, 1500.0f, 0.0f, 1.0f);
        check(g.weightAt(1, 10, 5) == 255 && g.weightAt(1, 25, 5) == 0, "height band gates the fill");
    }

    AVER_INFO("=== packWeightTextures ===");
    {
        // Section order: Grass(0) Rock(1) Sand(2). Material order: Sand(0) Grass(1) Snow(2) Rock(3); Sand is the base.
        fmt::OcLandData d = makePaintable(4, 3);
        d.weights[0] = 100;        // Rock at sample 0
        d.weights[16] = 50;        // Sand at sample 0
        const i32 chan[8] = {1, 3, 0, 0, 0, 0, 0, 0};   // Grass->1, Rock->3, Sand->0
        std::vector<u8> a, b;
        packWeightTextures(d, chan, 4, a, b);
        check(a.size() == 64 && b.size() == 64, "image sizes");
        // sample 0: Rock 100, Sand 50, Grass (implicit section base) 105
        check(a[0] == 105, "the section's base layer lands in its material channel (Grass -> 1, rgba index 0)");
        check(a[2] == 100, "Rock lands in material channel 3 (rgba index 2)");
        check(a[1] == 0 && a[3] == 0 && b[0] == 0, "other channels empty (Sand is the material base, implicit)");
        check(a[4] == 255 && a[4 + 2] == 0, "untouched sample is all Grass");
        // A section layer the material lacks folds into the material base.
        const i32 chan2[8] = {1, 9, 0, 0, 0, 0, 0, 0};
        packWeightTextures(d, chan2, 4, a, b);
        check(a[2] == 0 && a[0] == 105, "out-of-range channel folds");
        // Extra section layer with channel 5 goes to image B
        fmt::OcLandData e = makePaintable(4, 3);
        e.weights[0] = 77;
        const i32 chan3[8] = {0, 5, 0, 0, 0, 0, 0, 0};
        packWeightTextures(e, chan3, 8, a, b);
        check(b[0] == 77, "channel 5 is B.r");
    }

    AVER_INFO("=== mip chain ===");
    {
        std::vector<u8> l0(4 * 4 * 4, 0);
        for (usize i = 0; i < 16; ++i) l0[i * 4] = static_cast<u8>(i < 8 ? 200 : 0);
        std::vector<std::vector<u8>> lv;
        buildMipChainRGBA8(4, l0, lv);
        check(lv.size() == 3 && lv[1].size() == 16 && lv[2].size() == 4, "levels 4, 2, 1");
        check(lv[2][0] == 100, "box filter average");
        buildMipChainRGBA8(5, std::vector<u8>(100, 40), lv);
        // GPU mip sizes are max(1, n >> m): 5, 2, 1.
        check(lv.size() == 3 && lv[1].size() == 16 && lv.back().size() == 4 && lv.back()[0] == 40, "odd sizes halve like GPU mips (5, 2, 1)");
    }

    AVER_INFO("LayerPaint tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
