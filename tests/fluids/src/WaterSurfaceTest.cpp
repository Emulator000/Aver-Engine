// WaterSurface's CPU half: the grid layout, the flat grid builder, the camera snapping, and the
// WATER record's `cell`/`radius` tokens. No device and no GPU -- the compute pass is covered by
// WaterSurfaceCompileTest.
#include "aver/fluids/WaterSurface.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcWorld.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::fluids;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }

static void testDefaultLayout() {
    AVER_INFO("--- default (camera-following) layout ---");
    const WaterSurfaceDesc d;   // cell 100, radius 12800
    const WaterGridLayout l = waterGridLayout(d);
    check(l.vertsX == 257 && l.vertsY == 257, "256x256 cells over 2 * radius");
    check(near(l.cellX, 100.0f) && near(l.cellY, 100.0f), "the requested cell size is kept");
    check(l.skirtRings >= 6 && std::pow(2.0f, static_cast<f32>(l.skirtRings)) >= l.skirtScale,
          "horizon rings at most double each step");
    check(waterSkirtVertexCount(l) == 4u * 256u * l.skirtRings, "an unbounded grid has horizon rings");
    check(waterGridVertexCount(l) == 257u * 257u + 4u * 256u * l.skirtRings, "vertex count");
    check(waterGridIndexCount(l) == 256u * 256u * 6u + 4u * 256u * 6u * l.skirtRings, "index count");
    check(l.skirtScale * 0.5f * l.cellX * 256.0f >= 999999.0f, "the ring reaches about 10 km");

    WaterSurfaceDesc odd = d;
    odd.radiusCm = 1050.0f;   // 21 cells -> bumped to 22 so a vertex sits on the origin
    const WaterGridLayout lo = waterGridLayout(odd);
    check(((lo.vertsX - 1) & 1u) == 0u, "an odd cell count is made even");
}

static void testCoarsening() {
    AVER_INFO("--- coarsening and clamping ---");
    WaterSurfaceDesc d;
    d.radiusCm = 100000.0f;   // 2000 cells at 100 cm
    const WaterGridLayout l = waterGridLayout(d);
    check(l.vertsX == kWaterMaxCellsPerAxis + 1, "a huge grid is capped at kWaterMaxCellsPerAxis");
    check(near(l.cellX * static_cast<f32>(l.vertsX - 1), 200000.0f, 0.5f), "and keeps its extent by growing the cell");

    WaterSurfaceDesc z;
    z.radiusCm = 0.0f;
    z.cellCm = 0.0f;
    const WaterGridLayout lz = waterGridLayout(z);
    check(lz.vertsX >= 3 && lz.vertsX <= kWaterMaxCellsPerAxis + 1 && lz.cellX > 0.0f,
          "zero radius and cell are clamped to a usable grid, not refused");
}

static void testBounded() {
    AVER_INFO("--- bounded layout ---");
    WaterSurfaceDesc d;
    d.bounded = true;
    d.boundsMinCm[0] = 0.0f;   d.boundsMinCm[1] = 0.0f;
    d.boundsMaxCm[0] = 1000.0f; d.boundsMaxCm[1] = 400.0f;
    const WaterGridLayout l = waterGridLayout(d);
    check(l.vertsX == 11 && l.vertsY == 5, "the bounds divided into cells per axis");
    check(near(l.cellX, 100.0f) && near(l.cellY, 100.0f), "cells fill the bounds exactly");

    d.boundsMaxCm[0] = 450.0f;   // 4.5 cells: the cell size adjusts so the grid still fills the bounds
    const WaterGridLayout l2 = waterGridLayout(d);
    check(near(l2.cellX * static_cast<f32>(l2.vertsX - 1), 450.0f), "a non-multiple extent is filled exactly");

    WaterSurfaceDesc sw = d;
    std::swap(sw.boundsMinCm[1], sw.boundsMaxCm[1]);
    const WaterGridLayout ls = waterGridLayout(sw);
    check(ls.vertsY == waterGridLayout(d).vertsY, "swapped bounds give the same grid");

    f32 o[2];
    waterGridOrigin(d, l, 12345.0f, -999.0f, o);
    check(near(o[0], 225.0f) && near(o[1], 200.0f), "a bounded grid sits at the bounds' centre whatever the camera does");
}

static void testBuilder() {
    AVER_INFO("--- grid builder ---");
    WaterSurfaceDesc d;
    d.radiusCm = 800.0f;   // 16 cells
    const WaterGridLayout l = waterGridLayout(d);
    std::vector<rhi::MeshVertex> v;
    std::vector<u32> idx;
    buildWaterGrid(l, v, idx);
    check(v.size() == waterGridVertexCount(l) && idx.size() == waterGridIndexCount(l), "counts match the layout");

    const rhi::MeshVertex& c = v[(l.vertsY / 2) * l.vertsX + l.vertsX / 2];
    check(near(c.px, 0.0f) && near(c.py, 0.0f), "the centre vertex is on the origin");
    check(near(v.front().px, -800.0f) && near(v[l.vertsX * l.vertsY - 1].py, 800.0f), "the grid spans 2 * radius");

    bool inRange = true, allUp = true, flat = true;
    for (u32 i : idx) if (i >= v.size()) inRange = false;
    for (usize t = 0; t + 2 < idx.size(); t += 3) {
        const rhi::MeshVertex& a = v[idx[t]];
        const rhi::MeshVertex& b = v[idx[t + 1]];
        const rhi::MeshVertex& cc = v[idx[t + 2]];
        // (C - A) x (B - A), the engine's front-facing normal (FluidVolume::recomputeNormals).
        const f32 eABx = b.px - a.px, eABy = b.py - a.py;
        const f32 eACx = cc.px - a.px, eACy = cc.py - a.py;
        if (!(eACx * eABy - eACy * eABx > 0.0f)) allUp = false;
    }
    for (const rhi::MeshVertex& m : v) if (m.pz != 0.0f || m.nz != 1.0f) flat = false;
    check(inRange, "every index addresses a vertex");
    check(allUp, "every triangle faces +Z");
    check(flat, "the seed grid is flat with +Z normals");

    WaterGridLayout empty;
    buildWaterGrid(empty, v, idx);
    check(v.empty() && idx.empty(), "an empty layout builds nothing");
}

static void testSnapping() {
    AVER_INFO("--- camera snapping ---");
    const WaterSurfaceDesc d;
    const WaterGridLayout l = waterGridLayout(d);
    f32 a[2], b[2], c[2];
    waterGridOrigin(d, l, 1234.5f, -50.0f, a);
    waterGridOrigin(d, l, 1299.9f, -99.9f, b);
    waterGridOrigin(d, l, 1300.1f, -100.1f, c);
    check(near(a[0], 1200.0f) && near(a[1], -100.0f), "the camera is floored to the cell lattice");
    check(near(b[0], a[0]) && near(b[1], a[1]), "moving inside a cell does not move the grid");
    check(near(c[0] - a[0], 100.0f) && near(c[1] - a[1], -100.0f), "crossing a boundary moves it by exactly one cell");

    // Every vertex of the moved grid lands on a world lattice point, so nothing swims.
    std::vector<rhi::MeshVertex> v;
    std::vector<u32> idx;
    buildWaterGrid(l, v, idx);
    bool lattice = true;
    for (usize i = 0; i < usize(l.vertsX) * l.vertsY; i += 997) {   // the horizon ring is off-lattice
        const f32 wx = c[0] + v[i].px, wy = c[1] + v[i].py;
        if (std::fabs(wx / l.cellX - std::round(wx / l.cellX)) > 1e-3f ||
            std::fabs(wy / l.cellY - std::round(wy / l.cellY)) > 1e-3f) lattice = false;
    }
    check(lattice, "world positions stay on the cell lattice after a re-centre");
}

static void testWaterRecord() {
    AVER_INFO("--- WATER cell / radius tokens ---");
    using namespace aver::fmt;
    OcWorldData w;
    std::string err;
    check(parseOcworld("OCWORLD 1\nNAME T\nWATER name Sea level 12 infinite material M_Sea cell 50 radius 6400\n", w, &err),
          "a record with cell and radius parses");
    check(w.waters.size() == 1 && w.waters[0].cellCm == 50.0 && w.waters[0].radiusCm == 6400.0, "and carries both");
    check(w.waters[0].material == "M_Sea", "alongside the material");

    OcWorldData d;
    check(parseOcworld("OCWORLD 1\nNAME T\nWATER name Sea level 0 infinite material M_Sea\n", d, &err), "a record naming neither parses");
    check(d.waters[0].cellCm == 100.0 && d.waters[0].radiusCm == 12800.0, "and gets the defaults");
    check(writeOcworld(d).find(" cell ") == std::string::npos && writeOcworld(d).find(" radius ") == std::string::npos,
          "defaults are not written back");

    OcWorldData rt;
    check(parseOcworld(writeOcworld(w), rt, &err) && rt.waters.size() == 1 && rt.waters[0].cellCm == 50.0 &&
              rt.waters[0].radiusCm == 6400.0,
          "non-default values round-trip");

    OcWorldData bad;
    check(parseOcworld("OCWORLD 1\nNAME T\nWATER name Sea level 0 infinite cell -3 radius 0 frobnicate 7\n", bad, &err) &&
              bad.waters[0].cellCm == 100.0 && bad.waters[0].radiusCm == 12800.0,
          "non-positive sizes keep the defaults and an unknown token is tolerated");
}

int main() {
    AVER_INFO("WaterSurfaceTest");
    testDefaultLayout();
    testCoarsening();
    testBounded();
    testBuilder();
    testSnapping();
    testWaterRecord();
    AVER_INFO("=== {} assertions, {} failed ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
