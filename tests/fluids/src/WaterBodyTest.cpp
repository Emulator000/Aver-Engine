// Spline rivers and lakes (WaterBody.hpp): the spline, both meshes and the point query. CPU only, no
// device. Exit code = failure count.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/fluids/WaterBody.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::fluids;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static SplinePoint sp(f32 x, f32 y, f32 z = 0, f32 w = 0, f32 f = 0) { SplinePoint p; p.x = x; p.y = y; p.z = z; p.width = w; p.flow = f; return p; }

// Twice the signed area a triangle covers seen from +Z, positive for the engine's front-facing winding
// (clockwise from +Z), matching buildWaterGrid.
static f32 frontArea2(const rhi::MeshVertex& a, const rhi::MeshVertex& b, const rhi::MeshVertex& c) {
    return -((b.px - a.px) * (c.py - a.py) - (b.py - a.py) * (c.px - a.px));
}

static f64 shoelace(const std::vector<Spline::Sample>& s) {
    f64 a = 0;
    for (usize i = 0, j = s.size() - 1; i < s.size(); j = i++) a += static_cast<f64>(s[j].pos[0]) * s[i].pos[1] - static_cast<f64>(s[i].pos[0]) * s[j].pos[1];
    return std::fabs(0.5 * a);
}

static LakeDesc makeLake(const std::vector<std::pair<int, int>>& pts, f32 level, f32 maxEdge = 2500.0f) {
    LakeDesc l;
    std::vector<SplinePoint> v;
    for (auto& p : pts) v.push_back(sp(static_cast<f32>(p.first), static_cast<f32>(p.second)));
    l.outline.set(v, true);
    l.levelCm = level;
    l.maxEdgeCm = maxEdge;
    return l;
}

static void lakeChecks(const std::string& name, const LakeDesc& l) {
    std::vector<rhi::MeshVertex> v;
    std::vector<u32> idx;
    buildLakeMesh(l, v, idx);
    check(!v.empty() && idx.size() % 3 == 0, name + ": produced triangles");

    std::vector<Spline::Sample> outline;
    l.outline.sample(300.0f, outline);
    const f64 want = shoelace(outline);
    f64 area = 0;
    bool facing = true, flat = true;
    f32 maxEdge = 0;
    std::map<std::pair<u32, u32>, int> uses;
    for (usize t = 0; t < idx.size(); t += 3) {
        const f32 a2 = frontArea2(v[idx[t]], v[idx[t + 1]], v[idx[t + 2]]);
        if (a2 < -1e-3f) facing = false;
        area += 0.5 * std::fabs(a2);
        for (int e = 0; e < 3; ++e) {
            const u32 a = idx[t + e], b = idx[t + (e + 1) % 3];
            uses[{std::min(a, b), std::max(a, b)}]++;
            maxEdge = std::max(maxEdge, std::hypot(v[a].px - v[b].px, v[a].py - v[b].py));
        }
    }
    for (auto& x : v) if (x.pz != l.levelCm || x.nz != 1.0f) flat = false;
    check(std::fabs(area - want) / want < 1e-3, name + ": mesh area equals the outline's shoelace area");
    check(facing, name + ": every triangle faces +Z");
    check(flat, name + ": flat at the level with +Z normals");
    check(maxEdge <= l.maxEdgeCm + 0.5f, name + ": every edge <= maxEdgeCm (" + std::to_string(maxEdge) + ")");
    // Watertight: an edge is used by 1 (boundary) or 2 triangles, and the boundary is one closed loop.
    bool ok = true;
    std::map<u32, int> boundaryDegree;
    for (auto& kv : uses) {
        if (kv.second > 2) ok = false;
        if (kv.second == 1) { boundaryDegree[kv.first.first]++; boundaryDegree[kv.first.second]++; }
    }
    for (auto& kv : boundaryDegree) if (kv.second != 2) ok = false;
    check(ok, name + ": watertight (edges shared by two triangles, boundary a closed loop)");
}

int main() {
    AVER_INFO("=== spline ===");
    {
        Spline s;
        s.set({sp(0, 0, 0, 100, 0), sp(1000, 0, 0, 100, 0), sp(2000, 500, 100, 400, 50), sp(3000, 500, 100, 400, 50), sp(4000, 0, 200, 800, 300)}, false);
        std::vector<Spline::Sample> out;
        s.sample(50.0f, out);
        bool inRange = true, monotone = true;
        for (usize i = 0; i < out.size(); ++i) {
            if (out[i].pos[2] < -1e-3f || out[i].pos[2] > 200.001f) inRange = false;
            if (out[i].width < 99.999f || out[i].width > 800.001f) inRange = false;
            if (out[i].flow < -1e-3f || out[i].flow > 300.001f) inRange = false;
            if (i > 0 && out[i].pos[2] < out[i - 1].pos[2] - 1e-3f) monotone = false;
        }
        check(inRange, "z, width and flow never overshoot the control values");
        check(monotone, "monotone z stays monotone");
        check(out.size() >= 4 * 4 + 1, "at least 4 steps per segment");
        const Spline::Sample e = s.at(s.length());
        check(std::fabs(e.pos[0] - 4000) < 0.05f && std::fabs(e.pos[1]) < 0.05f && std::fabs(e.pos[2] - 200) < 0.05f, "at(length) is the last point");
        const Spline::Sample b = s.at(0);
        check(std::fabs(b.pos[0]) < 0.05f && std::fabs(b.width - 100) < 0.05f, "at(0) is the first point");
        check(s.at(-5).pos[0] == b.pos[0] && s.at(1e9f).pos[0] == e.pos[0], "arc length is clamped");
        f32 cs = 0, cd = 0;
        const Spline::Sample onCurve = s.at(1500.0f);
        check(s.closest(onCurve.pos[0], onCurve.pos[1], cs, cd) && cd < 0.5f && std::fabs(cs - 1500.0f) < 5.0f, "closest finds the curve");
        out.clear();
        s.sample(1.0f, out);
        check(out.size() <= 4096, "at most 4096 samples");

        Spline loop;
        loop.set({sp(0, 0), sp(1000, 0), sp(1000, 1000), sp(0, 1000)}, true);
        check(loop.closed(), "closed spline");
        const Spline::Sample w0 = loop.at(0), w1 = loop.at(loop.length());
        check(std::hypot(w0.pos[0] - w1.pos[0], w0.pos[1] - w1.pos[1]) < 0.5f, "closed spline wraps");
        std::vector<Spline::Sample> lo;
        loop.sample(100.0f, lo);
        check(lo.size() >= 16 && std::hypot(lo.back().pos[0] - lo.front().pos[0], lo.back().pos[1] - lo.front().pos[1]) > 10.0f, "closed sampling omits the repeat");
    }

    AVER_INFO("=== river mesh ===");
    {
        RiverDesc r;
        r.spline.set({sp(0, 0, 100, 400, 180), sp(2000, 0, 100, 400, 180), sp(4000, 0, 100, 400, 180)}, false);
        r.flowScale = 2.0f;
        r.bankSlow = 0.25f;
        std::vector<rhi::MeshVertex> v;
        std::vector<u32> idx;
        buildRiverMesh(r, 250.0f, v, idx);
        check(!v.empty() && v.size() % 3 == 0, "three vertices per row");
        const usize rows = v.size() / 3;
        check(idx.size() == (rows - 1) * 12, "twelve indices per row pair");
        bool up = true;
        for (usize t = 0; t < idx.size(); t += 3)
            if (frontArea2(v[idx[t]], v[idx[t + 1]], v[idx[t + 2]]) <= 0.0f) up = false;
        check(up, "all triangles face +Z");
        const rhi::MeshVertex& c = v[3 * 5 + 1];
        const rhi::MeshVertex& l = v[3 * 5 + 0];
        const rhi::MeshVertex& rr = v[3 * 5 + 2];
        check(std::fabs(c.u - 360.0f) < 0.01f && std::fabs(c.v) < 0.01f, "centre uv is tangent * flow * flowScale");
        check(std::fabs(l.u - 90.0f) < 0.01f && std::fabs(rr.u - 90.0f) < 0.01f, "bank uv scaled by bankSlow");
        check(std::fabs(l.py - 200.0f) < 0.01f && std::fabs(rr.py + 200.0f) < 0.01f && std::fabs(c.py) < 0.01f, "left bank is +Y of an +X river, width 400");
        check(std::fabs(c.nz - 1.0f) < 1e-4f, "flat river has a straight-up normal");

        RiverDesc bad;
        bad.spline.set({sp(0, 0)}, false);
        buildRiverMesh(bad, 250.0f, v, idx);
        check(v.empty() && idx.empty(), "a one-point river is nothing");
    }

    AVER_INFO("=== lake mesh ===");
    {
        lakeChecks("square", makeLake({{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}}, 150.0f, 800.0f));
        lakeChecks("clockwise", makeLake({{0, 0}, {0, 4000}, {4000, 4000}, {4000, 0}}, 80.0f, 1000.0f));
        // A C-shaped (concave) outline.
        lakeChecks("concave", makeLake({{0, 0}, {6000, 0}, {6000, 1500}, {1800, 1500}, {1800, 3000}, {6000, 3000}, {6000, 4500}, {0, 4500}}, 0.0f, 1200.0f));
        lakeChecks("large", makeLake({{-20000, -20000}, {20000, -20000}, {20000, 20000}, {-20000, 20000}}, 0.0f, 2500.0f));
    }

    AVER_INFO("=== WaterBodySet ===");
    {
        RiverDesc r;
        r.spline.set({sp(0, 0, 100, 400, 100), sp(2000, 0, 100, 400, 100), sp(4000, 0, 100, 400, 100)}, false);
        r.bankSlow = 0.5f;
        r.depthCm = 200.0f;
        LakeDesc l = makeLake({{300, -400}, {1300, -400}, {1300, 400}, {300, 400}}, 150.0f);
        l.depthCm = 500.0f;
        WaterBodySet set;
        const u32 ri = set.addRiver(r);
        const u32 li = set.addLake(l);
        set.finalize();
        check(set.bodyCount() == 2 && ri != li, "two bodies");

        WaterQuery q;
        check(set.sample(2500, 0, q) && q.over && q.kind == WaterKind::River && q.body == ri, "inside the river");
        check(std::fabs(q.surfaceZ - 100) < 0.01f, "river surface z");
        check(std::fabs(q.current[0] - 100) < 0.5f && std::fabs(q.current[1]) < 0.5f && q.current[2] == 0.0f, "current points downstream at full speed mid-channel");
        check(set.sample(2500, 150, q) && std::fabs(q.current[0] - 62.5f) < 1.0f, "bank current scaled (62.5 at 3/4 of the half width)");
        check(!set.sample(2500, 250, q) && !q.over && q.kind == WaterKind::None, "outside the river");
        check(!set.sample(-500, 0, q), "past the river's end");
        check(set.sample(800, 0, q) && q.kind == WaterKind::Lake && q.body == li && std::fabs(q.surfaceZ - 150) < 0.01f, "highest surface wins where bodies overlap");
        check(q.current[0] == 0 && q.normal[2] == 1.0f, "lake is still");
        check(set.sample(800, 0, q) && std::fabs(q.depthCm - 500) < 0.01f, "authored depth without a ground function");
        set.ground = [](f32, f32, f32& g) { g = 40.0f; return true; };
        check(set.sample(2500, 0, q) && std::fabs(q.depthCm - 60.0f) < 0.01f, "depth = surface - ground");
        set.ground = [](f32, f32, f32&) { return false; };
        check(set.sample(2500, 0, q) && std::fabs(q.depthCm - 200.0f) < 0.01f, "ground miss falls back to the authored depth");
        check(set.sample(1500, 0, q) && q.kind == WaterKind::River, "river outside the lake's outline");
    }

    AVER_INFO("WaterBody tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
