// River and basin carving: only lowers, depth and width profile, bank slope, rect containment,
// concave basins, no-op carve, exact undo by before/after. CPU only. Exit code = failure count.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/landscape/Carve.hpp"

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

// n x n at spacing 50 cm, flat at `h`.
static fmt::OcLandData makeFlat(u32 n, f32 h) {
    fmt::OcLandData d;
    d.sampleCount = n;
    d.spacingCm = 50.0f;
    d.heights.assign(static_cast<usize>(n) * n, h);
    return d;
}

int main() {
    AVER_INFO("=== river ===");
    {
        fmt::OcLandData d = makeFlat(81, 1000.0f);   // 4000 cm square; river along y = 2000
        const std::vector<f32> orig = d.heights;
        const CarvePoint pts[2] = {{0.0f, 2000.0f, 900.0f, 200.0f}, {4000.0f, 2000.0f, 900.0f, 200.0f}};
        CarveParams p;
        p.depthCm = 200.0f; p.bankCm = 300.0f; p.bankSlopeDeg = 35.0f; p.smooth = 0.0f;
        const BrushRect r = carveRiver(d, pts, 2, p);
        const BrushRect pre = carveRect(makeFlat(81, 1000.0f), pts, 2, p);
        check(!r.empty && r.x0 == pre.x0 && r.y0 == pre.y0 && r.x1 == pre.x1 && r.y1 == pre.y1, "carveRiver returns carveRect");

        bool onlyLowers = true, insideRect = true;
        for (u32 y = 0; y < 81; ++y)
            for (u32 x = 0; x < 81; ++x) {
                const f32 h = d.heights[y * 81 + x], o = orig[y * 81 + x];
                if (h > o) onlyLowers = false;
                if (h != o && (x < r.x0 || x > r.x1 || y < r.y0 || y > r.y1)) insideRect = false;
            }
        check(onlyLowers, "only ever lowers");
        check(insideRect, "carveRect contains every changed sample");
        check(std::fabs(d.heights[40 * 81 + 40] - 700.0f) < 1e-3f, "centre bed = surface - depth (700)");
        // lateral 100 (2 samples off) = half of halfWidth 200 -> 900 - 200*(1-0.25) = 750
        check(std::fabs(d.heights[42 * 81 + 40] - 750.0f) < 1e-3f, "parabolic width profile");
        check(std::fabs(d.heights[38 * 81 + 40] - 750.0f) < 1e-3f, "symmetric");
        // bank: lateral 300 -> 900 + 100*tan35 = 970.02
        const f32 bank = 900.0f + 100.0f * std::tan(35.0f * 3.14159265f / 180.0f);
        check(std::fabs(d.heights[46 * 81 + 40] - bank) < 0.01f, "bank slope meets the terrain");
        check(d.heights[60 * 81 + 40] == 1000.0f, "far terrain untouched");
        check(d.boundsMin[2] == 700.0f, "bounds refreshed");

        // Undo by before/after is exact.
        for (u32 y = r.y0; y <= r.y1; ++y)
            for (u32 x = r.x0; x <= r.x1; ++x) d.heights[y * 81 + x] = orig[y * 81 + x];
        check(d.heights == orig, "restoring the rect restores everything");

        // A no-op carve: terrain already lower than the bed.
        fmt::OcLandData low = makeFlat(81, 100.0f);
        carveRiver(low, pts, 2, p);
        check(low.heights == makeFlat(81, 100.0f).heights, "no-op when terrain is already lower");

        // smoothing never raises above the original
        fmt::OcLandData sm = makeFlat(81, 1000.0f);
        CarveParams ps = p; ps.smooth = 1.0f;
        carveRiver(sm, pts, 2, ps);
        bool ok = true;
        for (usize i = 0; i < sm.heights.size(); ++i) if (sm.heights[i] > 1000.0f) ok = false;
        check(ok && sm.heights[40 * 81 + 40] < 1000.0f && sm.heights[40 * 81 + 40] > 700.0f, "smoothing relaxes but still carves and never raises");
    }

    AVER_INFO("=== basin ===");
    {
        fmt::OcLandData d = makeFlat(81, 1000.0f);
        // concave L shape
        const f32 poly[] = {1000, 1000, 3000, 1000, 3000, 2000, 2000, 2000, 2000, 3000, 1000, 3000};
        CarveParams p;
        p.depthCm = 300.0f; p.bankCm = 400.0f; p.bankSlopeDeg = 45.0f; p.smooth = 0.0f;
        const BrushRect r = carveBasin(d, poly, 6, 800.0f, p);
        check(!r.empty, "basin rect");
        // deep inside the L (1500,1500) is > 400 from every edge? edges at x=1000 (500 away) y=1000 (500 away): yes
        const f32 deep = d.heights[30 * 81 + 30];
        check(std::fabs(deep - 500.0f) < 1e-3f, "interior beyond the bank width sits at level - depth");
        // the notch (2500,2500) is outside the L: distance to nearest edge = 500 > bank 400 -> untouched
        check(d.heights[50 * 81 + 50] == 1000.0f, "the concave notch is outside");
        // just outside an edge: x = 950 (1 sample out): bed = 800 + 50*tan45 = 850 < 1000
        check(std::fabs(d.heights[30 * 81 + 19] - 850.0f) < 0.01f, "outside bank follows the slope");
        check(d.heights[30 * 81 + 5] == 1000.0f, "beyond the bank nothing changes");
        // edge itself (inside, dist 0 -> level)
        const f32 edge = d.heights[30 * 81 + 20];   // x = 1000, on the polygon edge
        check(std::fabs(edge - 800.0f) < 1.0f, "at the rim the bed is the water level");
        bool onlyLowers = true;
        for (f32 h : d.heights) if (h > 1000.0f) onlyLowers = false;
        check(onlyLowers, "only lowers");
    }

    AVER_INFO("Carve tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
