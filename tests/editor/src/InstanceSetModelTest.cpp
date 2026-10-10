// The instance-set editing model (sandbox/src/InstanceSetModel) and the preview gizmo maths
// (sandbox/src/PreviewGizmo), both ImGui-free: load/export round trip, delta undo/redo and dirty,
// add/delete/duplicate, frustum culling, ray picking against a brute-force reference, marquee, and
// the gizmo's axis pick + drag on a known camera.
//
// AVER_WITH_IMGUI is deliberately undefined for this target -- PreviewGizmo.cpp's draw() sits behind
// it -- so what is compiled is exactly the part worth testing without a window.
#include "InstanceSetModel.hpp"
#include "PreviewGizmo.hpp"

#include "aver/core/Log.hpp"
#include "aver/formats/OcInstances.hpp"

#include <chrono>
#include <cmath>
#include <string>

using namespace aver;
using namespace aver::editor;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("  FAIL  {}", what);
    ++g_failures;
}

static bool near(f32 a, f32 b, f32 eps = 1e-2f) { return std::fabs(a - b) <= eps; }

// Row-vector world matrix: yaw about +Z (CCW from above), uniform scale, translation.
static void makeM(f32 out[12], f32 x, f32 y, f32 z, f32 yawDeg = 0.0f, f32 scale = 1.0f) {
    const f32 a = yawDeg * 3.14159265f / 180.0f, c = std::cos(a) * scale, s = std::sin(a) * scale;
    const f32 m[12] = {c, s, 0,   -s, c, 0,   0, 0, scale,   x, y, z};
    xf::copy(out, m);
}

// Camera: Z-up look-at + perspective, row-vector, D3D depth 0..1 (the convention the model documents).
static void makeViewProj(f32 vp[16], const f32 eye[3], const f32 target[3], f32 fovYDeg, f32 aspect) {
    auto norm = [](f32 v[3]) { const f32 l = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); for (int i = 0; i < 3; ++i) v[i] /= l; };
    auto cross = [](const f32 a[3], const f32 b[3], f32 o[3]) {
        o[0] = a[1]*b[2] - a[2]*b[1]; o[1] = a[2]*b[0] - a[0]*b[2]; o[2] = a[0]*b[1] - a[1]*b[0];
    };
    f32 f[3] = {target[0]-eye[0], target[1]-eye[1], target[2]-eye[2]};
    norm(f);
    const f32 up[3] = {0, 0, 1};
    f32 r[3], u[3];
    cross(f, up, r); norm(r);
    cross(r, f, u);
    auto dot = [](const f32 a[3], const f32 b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
    const f32 V[16] = {r[0], u[0], f[0], 0,   r[1], u[1], f[1], 0,   r[2], u[2], f[2], 0,
                       -dot(eye, r), -dot(eye, u), -dot(eye, f), 1};
    const f32 t = std::tan(fovYDeg * 0.5f * 3.14159265f / 180.0f);
    const f32 zn = 10.0f, zf = 50000.0f;
    const f32 P[16] = {1.0f / (aspect * t), 0, 0, 0,   0, 1.0f / t, 0, 0,   0, 0, zf / (zf - zn), 1,
                       0, 0, -zn * zf / (zf - zn), 0};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            f32 s = 0;
            for (int k = 0; k < 4; ++k) s += V[i*4+k] * P[k*4+j];
            vp[i*4+j] = s;
        }
}

static fmt::OcInstanceData fixture() {
    fmt::OcInstanceData d;
    f32 m[12];
    auto row = [&](f32 x, f32 y, f32 z) { makeM(m, x, y, z); d.transforms.insert(d.transforms.end(), m, m + 12); };
    row(0, 0, 0); row(100, 0, 0); row(200, 0, 0); row(300, 0, 0);   // group 0: rows 0..3
    row(0, 500, 0); row(100, 500, 0);                                  // group 1: rows 4..5
    d.groups.push_back({"Meshes/Tree.ocmesh", fmt::kOcInstanceFlagCastShadow, 0, 4});
    d.groups.push_back({"Meshes/Rock.ocmesh", 0, 4, 2});
    return d;
}

static std::vector<InstanceId> idsOf(u32 g, std::initializer_list<u32> rows) {
    std::vector<InstanceId> v;
    for (u32 r : rows) v.push_back(makeInstanceId(g, r));
    return v;
}

int main() {
    AVER_INFO("InstanceSetModelTest");

    AVER_INFO("load / export round trip");
    {
        const fmt::OcInstanceData src = fixture();
        InstanceSetModel m;
        check(m.load(src), "loads a valid table");
        check(m.aliveCount() == 6 && m.groupCount() == 2, "6 rows in 2 groups");
        check(!m.dirty() && !m.canUndo(), "clean, nothing to undo after load");
        fmt::OcInstanceData out;
        m.exportTo(out);
        check(out.valid(), "the export is valid");
        check(out.transforms == src.transforms, "transforms survive bit for bit");
        check(out.groups.size() == 2 && out.groups[1].first == 4 && out.groups[1].count == 2 &&
              out.groups[0].asset == "Meshes/Tree.ocmesh" && out.groups[0].flags == fmt::kOcInstanceFlagCastShadow &&
              out.groups[1].flags == 0, "groups keep asset, flags and contiguous ranges");

        fmt::OcInstanceData bad = fixture();
        bad.transforms.pop_back();
        InstanceSetModel m2;
        std::string why;
        check(!m2.load(bad, &why) && !why.empty() && m2.aliveCount() == 0, "an invalid table is refused");

        // Cells are a bake artefact: dropped on export.
        fmt::OcInstanceData celled = fixture();
        check(fmt::cellOcInstances(celled, 250.0f, {0.0f, 0.0f}) && !celled.cells.empty(), "fixture can be celled");
        InstanceSetModel m3;
        m3.load(celled);
        fmt::OcInstanceData out3;
        m3.exportTo(out3);
        check(out3.cells.empty() && out3.runs.empty() && out3.cellCm == 0.0f && out3.valid() &&
              out3.transforms.size() == celled.transforms.size(), "export clears cells so the bake recomputes them");
        std::vector<u8> bytes;
        check(fmt::writeOcInstances(out3, bytes), "and the export writes as an .ocinst");
    }

    AVER_INFO("add / delete / duplicate");
    {
        InstanceSetModel m;
        m.load(fixture());
        f32 mat[12];
        makeM(mat, 1000, 0, 0);
        const InstanceId a = m.add("Meshes/Tree.ocmesh", mat);
        check(instanceGroup(a) == 0 && instanceRow(a) == 4 && m.alive(a) && m.aliveCount() == 7, "add appends to the asset's group");
        const InstanceId b = m.add("Meshes/Bush.ocmesh", mat);
        check(instanceGroup(b) == 2 && m.groupCount() == 3 && m.groupFlags(2) == fmt::kOcInstanceFlagCastShadow,
              "an unknown asset makes a new group");
        check(m.add("", mat) == kNoInstance, "an empty asset is refused");
        m.selectNone();
        check(m.deleteInstances(idsOf(0, {1, 2})) == 2 && m.aliveCount() == 6, "delete drops two rows");
        check(m.deleteInstances(idsOf(0, {1})) == 0, "deleting a dead row is a no-op");
        check(m.matrix(makeInstanceId(0, 3)) != nullptr && m.alive(makeInstanceId(0, 3)), "ids of the others are stable");
        fmt::OcInstanceData out;
        m.exportTo(out);
        check(out.valid() && out.groups.size() == 3 && out.groups[0].count == 3 && out.groups[1].first == 3 &&
              out.groups[2].first == 5, "export drops dead rows and stays contiguous");

        m.select(idsOf(1, {0, 1}));
        const f32 off[3] = {0, 0, 50};
        const auto dups = m.duplicateSelected(off);
        check(dups.size() == 2 && m.aliveCount() == 8 && m.selection() == dups, "duplicate adds copies and selects them");
        check(near(m.matrix(dups[0])[11], 50.0f) && near(m.matrix(dups[0])[9], 0.0f), "copies sit at the offset");
        check(instanceGroup(dups[0]) == 1, "and stay in the source's group");

        m.select(dups);
        check(m.deleteSelected() == 2 && m.selection().empty(), "delete-selected empties the selection");
    }

    AVER_INFO("delta undo / redo / dirty");
    {
        InstanceSetModel m;
        m.load(fixture());
        fmt::OcInstanceData base;
        m.exportTo(base);

        f32 mat[12];
        makeM(mat, 7, 8, 9);
        const InstanceId a = m.add("Meshes/Tree.ocmesh", mat);
        check(m.dirty() && m.canUndo() && !m.canRedo(), "an edit makes it dirty and undoable");
        check(m.undo() && !m.alive(a) && m.aliveCount() == 6 && !m.dirty(), "undo of add removes it and is clean again");
        check(m.redo() && m.alive(a) && near(m.matrix(a)[10], 8.0f) && m.dirty(), "redo restores it");

        m.markSaved();
        check(!m.dirty(), "markSaved cleans");
        m.deleteInstances(idsOf(0, {0, 1}));
        check(m.dirty(), "an edit after saving is dirty");
        check(m.undo() && !m.dirty(), "undoing back to the saved state is clean");
        check(m.undo() && m.dirty(), "undoing past it is dirty");
        m.redo();
        m.redo();
        m.undo();
        f32 mv[12];
        makeM(mv, 1, 1, 1);
        m.setTransform(idsOf(0, {0})[0], mv);   // discards the redo branch that held the saved point? (saved is behind)
        check(!m.canRedo(), "a new edit clears redo");

        // Whole-history round trip.
        while (m.undo()) {}
        fmt::OcInstanceData back;
        m.exportTo(back);
        check(back.transforms == base.transforms && back.groups.size() == base.groups.size(), "undoing everything restores the table");
        while (m.redo()) {}
        check(m.alive(a) && near(m.matrix(idsOf(0, {0})[0])[9], 1.0f), "and redoing everything re-applies it");

        // A gizmo drag is one undo step, and its base does not compound.
        InstanceSetModel d;
        d.load(fixture());
        d.select(idsOf(0, {0, 1, 2}));
        d.beginEdit();
        for (int i = 1; i <= 10; ++i) {
            f32 delta[12];
            const f32 dir[3] = {0, 1, 0};
            gizmo::translationDelta(dir, 10.0f * i, delta);
            d.applyDeltaToSelected(delta);
        }
        check(near(d.matrix(idsOf(0, {1})[0])[10], 100.0f), "total delta applied each frame does not compound");
        d.endEdit();
        check(d.undoDepth() == 1, "a drag is one undo step");
        d.undo();
        check(near(d.matrix(idsOf(0, {1})[0])[10], 0.0f) && !d.dirty(), "and one undo restores it");

        d.beginEdit();
        const f32 dir[3] = {1, 0, 0};
        f32 delta[12];
        gizmo::translationDelta(dir, 500.0f, delta);
        d.applyDeltaToSelected(delta);
        d.cancelEdit();
        check(near(d.matrix(idsOf(0, {0})[0])[9], 0.0f) && d.undoDepth() == 0 && !d.dirty(), "cancelEdit reverts without a step");

        // A no-op edit (moved and moved back inside one drag) records nothing.
        d.beginEdit();
        gizmo::translationDelta(dir, 50.0f, delta);
        d.applyDeltaToSelected(delta);
        gizmo::translationDelta(dir, 0.0f, delta);
        d.applyDeltaToSelected(delta);
        d.endEdit();
        check(d.undoDepth() == 0, "a drag that returns to the start records nothing");
    }

    AVER_INFO("a 10^6-row table is edited by delta, never snapshotted");
    {
        fmt::OcInstanceData big;
        constexpr u32 N = 1'000'000;
        big.transforms.resize(static_cast<usize>(N) * 12);
        for (u32 i = 0; i < N; ++i) {
            f32 m[12];
            makeM(m, static_cast<f32>(i % 1000) * 100.0f, static_cast<f32>(i / 1000) * 100.0f, 0);
            xf::copy(&big.transforms[static_cast<usize>(i) * 12], m);
        }
        big.groups.push_back({"Meshes/Grass.ocmesh", fmt::kOcInstanceFlagCastShadow, 0, N});
        InstanceSetModel m;
        check(m.load(big) && m.aliveCount() == N, "loads a million rows");
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i) {
            f32 mv[12];
            makeM(mv, static_cast<f32>(i), 1, 2);
            m.setTransform(makeInstanceId(0, static_cast<u32>(i) * 997u), mv);
        }
        for (int i = 0; i < 200; ++i) m.undo();
        for (int i = 0; i < 200; ++i) m.redo();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        AVER_INFO("  200 edits + 200 undos + 200 redos on 1M rows: {:.1f} ms", ms);
        check(ms < 1000.0, "600 history operations on a million rows stay cheap");
        check(m.indexRebuilds() == 0, "and none of them built the spatial index");
        fmt::OcInstanceData out;
        m.exportTo(out);
        check(out.transforms.size() == big.transforms.size(), "export is complete");
    }

    AVER_INFO("culling against hand-placed boxes");
    {
        InstanceSetModel m;
        fmt::OcInstanceData d;
        f32 mat[12];
        auto row = [&](f32 x, f32 y, f32 z) { makeM(mat, x, y, z); d.transforms.insert(d.transforms.end(), mat, mat + 12); };
        row(0, 0, 100);        // 0 in view, near
        row(4000, 0, 100);     // 1 in view, far
        row(-6000, 0, 100);    // 2 behind the camera
        row(0, 20000, 100);    // 3 far off to the side
        row(0, 0, 90000);      // 4 way above the view
        d.groups.push_back({"Meshes/A.ocmesh", 1, 0, 5});
        m.load(d);   // default local bounds +-50 cm
        const f32 eye[3] = {-3000, 0, 100}, target[3] = {0, 0, 100};
        CullParams p;
        makeViewProj(p.viewProj, eye, target, 60.0f, 1.0f);
        p.eye[0] = eye[0]; p.eye[1] = eye[1]; p.eye[2] = eye[2];
        CullResult r;
        m.cull(p, r);
        check(r.rows.size() == 1 && r.rows[0] == std::vector<u32>({0, 1}) && r.total == 2 && !r.capped,
              "only the two boxes in the frustum survive");

        p.maxCount = 1;
        m.cull(p, r);
        check(r.total == 1 && r.capped && r.rows[0] == std::vector<u32>({0}), "a cap keeps the NEAREST instance");

        p.maxCount = 0;
        p.pixelScale = 1080.0f / (2.0f * std::tan(30.0f * 3.14159265f / 180.0f));
        p.minPixels = 45.0f;   // near box projects ~54 px, far one ~23 px
        m.cull(p, r);
        check(r.rows[0] == std::vector<u32>({0}), "the pixel-size cull drops the small distant one");

        // Culling follows edits: delete the near one, move the far one out of view.
        p.pixelScale = 0.0f;
        m.deleteInstances(idsOf(0, {0}));
        makeM(mat, 4000, 30000, 100);
        m.setTransform(makeInstanceId(0, 1), mat);
        m.cull(p, r);
        check(r.total == 0, "edits are reflected (dead and moved-away instances are gone)");
        m.undo();
        m.undo();
        m.cull(p, r);
        check(r.rows[0] == std::vector<u32>({0, 1}), "and undo brings them back");

        // Marquee: a sub-frustum around the near box only.
        InstanceSetModel q;
        fmt::OcInstanceData d2;
        auto row2 = [&](f32 x, f32 y, f32 z) { makeM(mat, x, y, z); d2.transforms.insert(d2.transforms.end(), mat, mat + 12); };
        row2(0, 0, 100); row2(0, 600, 100); row2(0, -600, 100);
        d2.groups.push_back({"Meshes/A.ocmesh", 1, 0, 3});
        q.load(d2);
        f32 sub[16];
        check(subFrustumViewProj(p.viewProj, -0.1f, -0.1f, 0.1f, 0.1f, sub), "a marquee sub-frustum builds");
        q.selectInFrustum(sub);
        check(q.selection() == idsOf(0, {0}), "marquee around screen centre selects only the centre box");
        check(!subFrustumViewProj(p.viewProj, 0.2f, 0.2f, 0.2f, 0.5f, sub), "an empty marquee is refused");
        q.selectInFrustum(p.viewProj, SelectMode::Replace);
        check(q.selection().size() == 3, "the full frustum selects all three");

        // Box select.
        const f32 lo[3] = {-60, 500, 0}, hi[3] = {60, 700, 200};
        q.selectInBox(lo, hi);
        check(q.selection() == idsOf(0, {1}), "box select finds the one box in the region");
        const f32 lo2[3] = {-60, -700, 0}, hi2[3] = {60, -500, 200};
        q.selectInBox(lo2, hi2, SelectMode::Add);
        check(q.selection() == idsOf(0, {1, 2}), "Add extends the selection");
        q.selectInBox(lo, hi, SelectMode::Toggle);
        check(q.selection() == idsOf(0, {2}), "Toggle flips it");
        q.selectGroup(0, SelectMode::Replace);
        check(q.selection().size() == 3, "selectGroup takes the whole group");
        q.select(idsOf(0, {1}), SelectMode::Remove);
        check(q.selection() == idsOf(0, {0, 2}), "Remove subtracts");
    }

    AVER_INFO("ray picking");
    {
        InstanceSetModel m;
        fmt::OcInstanceData d;
        f32 mat[12];
        auto row = [&](f32 x, f32 y, f32 z, f32 yaw = 0, f32 s = 1) {
            makeM(mat, x, y, z, yaw, s);
            d.transforms.insert(d.transforms.end(), mat, mat + 12);
        };
        row(0, 0, 0);          // 0
        row(4000, 0, 0);       // 1 behind 0 along +X
        row(0, 5000, 0, 45);   // 2 rotated 45 deg about Z
        row(8000, 0, 0, 0, 2); // 3 scale 2 (+-100)
        d.groups.push_back({"Meshes/A.ocmesh", 1, 0, 4});
        m.load(d);

        PickHit h;
        const f32 o1[3] = {-3000, 0, 0}, dx[3] = {1, 0, 0};
        check(m.pick(o1, dx, 1e6f, h) && h.id == makeInstanceId(0, 0) && near(h.t, 2950.0f), "a ray down +X hits the nearer box at the right t");
        const f32 dx2[3] = {2, 0, 0};
        check(m.pick(o1, dx2, 1e6f, h) && near(h.t, 1475.0f), "t is in the units of the (unnormalised) direction");
        check(!m.pick(o1, dx, 2000.0f, h), "maxDist limits the ray");
        const f32 dxn[3] = {-1, 0, 0};
        check(!m.pick(o1, dxn, 1e6f, h), "a ray pointing away hits nothing");

        const f32 down[3] = {0, 0, -1};
        const f32 o2[3] = {0, 0, 1000};
        check(m.pick(o2, down, 1e6f, h) && h.id == makeInstanceId(0, 0) && near(h.t, 950.0f), "straight down onto the top face");
        const f32 o3[3] = {8000, 0, 1000};
        check(m.pick(o3, down, 1e6f, h) && h.id == makeInstanceId(0, 3) && near(h.t, 900.0f), "a scaled instance's box is scaled");

        // 45 degree box: the world AABB is +-70.7 but the shape is a diamond.
        const f32 o4[3] = {60, 5060, 1000};   // inside the AABB, outside the diamond
        check(!m.pick(o4, down, 1e6f, h), "the exact test rejects a ray through the AABB corner");
        const f32 o5[3] = {30, 5030, 1000};
        check(m.pick(o5, down, 1e6f, h) && h.id == makeInstanceId(0, 2), "but a ray through the diamond hits");

        // Picking follows edits.
        makeM(mat, 0, 0, 500);
        m.setTransform(makeInstanceId(0, 0), mat);
        check(m.pick(o2, down, 1e6f, h) && near(h.t, 450.0f), "a moved instance is picked at its new place");
        m.deleteInstances(idsOf(0, {0}));
        check(!m.pick(o2, down, 1e6f, h), "a deleted one is not picked");
        m.undo();
        m.undo();
        check(m.pick(o2, down, 1e6f, h) && near(h.t, 950.0f), "undo makes it pickable where it was");
        check(m.indexRebuilds() == 1, "all of that was incremental (one index build)");
    }

    AVER_INFO("ray picking vs brute force over a few thousand instances");
    {
        u32 seed = 12345;
        auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return static_cast<f32>((seed >> 8) & 0xFFFF) / 65535.0f; };
        InstanceSetModel m;
        fmt::OcInstanceData d;
        constexpr int N = 3000;
        f32 mat[12];
        for (int i = 0; i < N; ++i) {
            makeM(mat, rnd() * 20000.0f - 10000.0f, rnd() * 20000.0f - 10000.0f, rnd() * 400.0f,
                  rnd() * 360.0f, 0.5f + rnd() * 3.0f);
            d.transforms.insert(d.transforms.end(), mat, mat + 12);
        }
        d.groups.push_back({"Meshes/A.ocmesh", 1, 0, N});
        m.load(d);
        InstanceBounds b;
        b.lo[0] = -40; b.hi[0] = 40; b.lo[1] = -20; b.hi[1] = 20; b.lo[2] = 0; b.hi[2] = 120;
        m.setAssetBounds("Meshes/A.ocmesh", b);
        m.setCellSize(1500.0f);

        int mismatches = 0, hits = 0;
        for (int k = 0; k < 300; ++k) {
            const f32 o[3] = {rnd() * 24000.0f - 12000.0f, rnd() * 24000.0f - 12000.0f, rnd() * 800.0f - 100.0f};
            f32 dir[3] = {rnd() - 0.5f, rnd() - 0.5f, (rnd() - 0.5f) * 0.1f};
            if (k % 7 == 0) { dir[0] = 0; dir[1] = 0; dir[2] = -1; }   // vertical
            if (k % 11 == 0) { dir[1] = 0; dir[2] = 0; dir[0] = 1; }   // axis-aligned
            // Reference: every instance, exact slab test in its own space.
            f32 bestT = 1e9f;
            u32 bestRow = ~0u;
            for (u32 r = 0; r < static_cast<u32>(N); ++r) {
                f32 inv[12];
                if (!xf::invert(m.matrix(makeInstanceId(0, r)), inv)) continue;
                f32 lo_[3], ld[3];
                xf::point(inv, o, lo_);
                xf::vector(inv, dir, ld);
                f32 t0 = -1e30f, t1 = 1e30f;
                bool ok = true;
                for (int a = 0; a < 3 && ok; ++a) {
                    if (ld[a] == 0.0f) { ok = lo_[a] >= b.lo[a] && lo_[a] <= b.hi[a]; continue; }
                    f32 ta = (b.lo[a] - lo_[a]) / ld[a], tb = (b.hi[a] - lo_[a]) / ld[a];
                    if (ta > tb) std::swap(ta, tb);
                    t0 = std::max(t0, ta); t1 = std::min(t1, tb);
                    ok = t0 <= t1;
                }
                if (!ok || t1 < 0.0f) continue;
                const f32 t = std::max(t0, 0.0f);
                if (t < bestT) { bestT = t; bestRow = r; }
            }
            PickHit h;
            const bool got = m.pick(o, dir, 1.0e9f, h);
            if (bestRow != ~0u) ++hits;
            if (got != (bestRow != ~0u)) { ++mismatches; continue; }
            if (got && (instanceRow(h.id) != bestRow && !near(h.t, bestT, 0.05f + bestT * 1e-4f))) ++mismatches;
        }
        AVER_INFO("  {} of 300 rays hit something", hits);
        check(hits > 20, "the random rays actually exercise hits");
        check(mismatches == 0, "grid-traversal pick agrees with the brute-force reference on every ray");

        // Frustum cull vs brute force on the same set.
        CullParams p;
        const f32 eye[3] = {-12000, -12000, 3000}, target[3] = {0, 0, 0};
        makeViewProj(p.viewProj, eye, target, 50.0f, 1.6f);
        p.eye[0] = eye[0]; p.eye[1] = eye[1]; p.eye[2] = eye[2];
        CullResult r;
        m.cull(p, r);
        usize ref = 0;
        for (u32 row = 0; row < static_cast<u32>(N); ++row) {
            f32 lo[3], hi[3];
            m.worldBounds(makeInstanceId(0, row), lo, hi);
            bool inside = true;
            f32 cs[3] = {(lo[0]+hi[0])*0.5f, (lo[1]+hi[1])*0.5f, (lo[2]+hi[2])*0.5f};
            (void)cs;
            // brute: the 8 corners must not all lie outside one plane
            const f32* vp = p.viewProj;
            for (int plane = 0; plane < 6 && inside; ++plane) {
                bool allOut = true;
                for (int c = 0; c < 8; ++c) {
                    const f32 x = (c & 1) ? hi[0] : lo[0], y = (c & 2) ? hi[1] : lo[1], z = (c & 4) ? hi[2] : lo[2];
                    f32 clip[4];
                    for (int j = 0; j < 4; ++j) clip[j] = x*vp[j] + y*vp[4+j] + z*vp[8+j] + vp[12+j];
                    f32 val;
                    switch (plane) {
                    case 0: val = clip[3] + clip[0]; break;
                    case 1: val = clip[3] - clip[0]; break;
                    case 2: val = clip[3] + clip[1]; break;
                    case 3: val = clip[3] - clip[1]; break;
                    case 4: val = clip[2]; break;
                    default: val = clip[3] - clip[2]; break;
                    }
                    if (val >= 0.0f) { allOut = false; break; }
                }
                if (allOut) inside = false;
            }
            if (inside) ++ref;
        }
        check(r.total == ref && r.total > 0 && r.total < static_cast<u32>(N), "frustum cull matches a brute-force corner test (count)");
    }

    AVER_INFO("gizmo: frames, axis pick, drag");
    {
        using namespace gizmo;
        gizmo::View v;
        v.width = 800; v.height = 600;
        const f32 eye[3] = {-2000, -2000, 1500}, target[3] = {0, 0, 0};
        makeViewProj(v.viewProj, eye, target, 50.0f, 800.0f / 600.0f);

        f32 mat[12];
        makeM(mat, 0, 0, 0);
        Frame f;
        frameFromMatrix(Mode::Translate, Space::World, mat, f);
        f32 o[2];
        check(project(v, f.pivot, o) && near(o[0], 400.0f, 0.5f) && near(o[1], 300.0f, 0.5f), "the look-at target projects to the image centre");
        const f32 behind[3] = {-9000, -9000, 6000};
        f32 tmp[2];
        check(!project(v, behind, tmp), "a point behind the eye does not project");

        f32 tip[3][2];
        for (int a = 0; a < 3; ++a) check(axisTip(v, f, a, tip[a]), "axis " + std::to_string(a) + " has a tip");
        for (int a = 0; a < 3; ++a) {
            const f32 dx = tip[a][0] - o[0], dy = tip[a][1] - o[1];
            check(near(std::sqrt(dx*dx + dy*dy), kHandlePixels, 0.05f), "handle " + std::to_string(a) + " is 64 px long");
        }
        for (int a = 0; a < 3; ++a) {
            check(pickAxis(v, f, Mode::Translate, tip[a]) == a, "the cursor on tip " + std::to_string(a) + " picks axis " + std::to_string(a));
            const f32 mid[2] = {(o[0] + tip[a][0]) * 0.5f + 3.0f, (o[1] + tip[a][1]) * 0.5f};
            check(pickAxis(v, f, Mode::Translate, mid) == a, "and so does the middle of the handle (3 px off)");
        }
        const f32 far_[2] = {700.0f, 80.0f};
        check(pickAxis(v, f, Mode::Translate, far_) == kAxisNone, "a far cursor picks nothing");
        check(pickAxis(v, f, Mode::Scale, o) == kAxisUniform, "Scale picks the centre as the uniform handle");

        // Translate along Y: drag the Y handle by the pixel length of 100 cm.
        {
            Drag drag;
            check(beginDrag(drag, v, f, Mode::Translate, 1, tip[1]), "a translate drag begins on the Y handle");
            const f32 cur[2] = {tip[1][0] + drag.dirPx[0] * drag.pxPerCm * 100.0f, tip[1][1] + drag.dirPx[1] * drag.pxPerCm * 100.0f};
            f32 delta[12];
            check(updateDrag(drag, cur, delta), "update yields a delta");
            check(near(delta[9], 0.0f) && near(delta[10], 100.0f, 0.05f) && near(delta[11], 0.0f), "100 cm of cursor travel moves 100 cm along world Y");
            check(near(drag.amount, 100.0f, 0.05f), "and reports the amount");

            Snap snap;
            snap.translateCm = 50.0f;
            Drag sd;
            beginDrag(sd, v, f, Mode::Translate, 1, tip[1], snap);
            const f32 cur2[2] = {tip[1][0] + sd.dirPx[0] * sd.pxPerCm * 120.0f, tip[1][1] + sd.dirPx[1] * sd.pxPerCm * 120.0f};
            updateDrag(sd, cur2, delta);
            check(near(delta[10], 100.0f, 0.05f), "snapping rounds 120 cm to the 50 cm grid");
            endDrag(sd);
            check(!updateDrag(sd, cur2, delta), "an ended drag does nothing");
        }

        // Rotate about Z by a quarter turn (one handle length), then a pivot that is not the origin.
        {
            Drag drag;
            beginDrag(drag, v, f, Mode::Rotate, 2, tip[2]);
            const f32 cur[2] = {tip[2][0] + drag.dirPx[0] * kHandlePixels, tip[2][1] + drag.dirPx[1] * kHandlePixels};
            f32 delta[12];
            updateDrag(drag, cur, delta);
            check(near(drag.amount, 90.0f, 0.01f), "a handle length of motion is a quarter turn");
            const f32 p[3] = {100, 0, 0};
            f32 r[3];
            xf::point(delta, p, r);
            check(near(r[0], 0.0f) && near(r[1], 100.0f) && near(r[2], 0.0f), "+90 about +Z takes +X to +Y");

            f32 off[12];
            const f32 pivot[3] = {500, 300, 40}, zax[3] = {0, 0, 1};
            rotationDelta(pivot, zax, 37.0f, off);
            xf::point(off, pivot, r);
            check(near(r[0], 500.0f) && near(r[1], 300.0f) && near(r[2], 40.0f), "a rotation about a pivot leaves the pivot fixed");
            const f32 q[3] = {600, 300, 40};
            xf::point(off, q, r);
            const f32 c = std::cos(37.0f * 3.14159265f / 180.0f), s = std::sin(37.0f * 3.14159265f / 180.0f);
            check(near(r[0], 500.0f + 100.0f * c) && near(r[1], 300.0f + 100.0f * s), "and carries other points round it");
            // The group moves rigidly: a matrix times that delta rotates its position about the pivot too.
            f32 inst[12], out[12];
            makeM(inst, 600, 300, 40);
            xf::mul(inst, off, out);
            check(near(out[9], r[0]) && near(out[10], r[1]) && near(out[0], c) && near(out[1], s), "row * delta rotates basis and position together");
        }

        // Scale: Local space, instance yawed 90 degrees, double along its own X.
        {
            makeM(mat, 0, 0, 0, 90.0f);
            Frame lf;
            frameFromMatrix(Mode::Scale, Space::World, mat, lf);   // Scale ignores `World`
            check(near(lf.axes[0][0], 0.0f) && near(lf.axes[0][1], 1.0f), "scale handles are always local (yawed X points along world Y)");
            Frame wf;
            frameFromMatrix(Mode::Translate, Space::World, mat, wf);
            check(near(wf.axes[0][0], 1.0f) && near(wf.axes[0][1], 0.0f), "translate in World space keeps world axes");
            frameFromMatrix(Mode::Translate, Space::Local, mat, wf);
            check(near(wf.axes[0][0], 0.0f) && near(wf.axes[0][1], 1.0f), "translate in Local space follows the object");

            f32 stip[2];
            check(axisTip(v, lf, 0, stip), "the local X handle has a tip");
            check(pickAxis(v, lf, Mode::Scale, stip) == 0, "and is pickable");
            Drag drag;
            beginDrag(drag, v, lf, Mode::Scale, 0, stip);
            const f32 cur[2] = {stip[0] + drag.dirPx[0] * kHandlePixels, stip[1] + drag.dirPx[1] * kHandlePixels};
            f32 delta[12], out[12];
            updateDrag(drag, cur, delta);
            check(near(drag.amount, 2.0f, 1e-3f), "a handle length of motion doubles");
            xf::mul(mat, delta, out);
            const f32 r0 = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
            const f32 r1 = std::sqrt(out[3]*out[3] + out[4]*out[4] + out[5]*out[5]);
            check(near(r0, 2.0f, 1e-3f) && near(r1, 1.0f, 1e-3f), "row 0 doubled, row 1 untouched");

            Drag ud;
            beginDrag(ud, v, lf, Mode::Scale, kAxisUniform, o);
            const f32 cur2[2] = {o[0] + kHandlePixels, o[1]};
            updateDrag(ud, cur2, delta);
            xf::mul(mat, delta, out);
            const f32 u0 = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
            const f32 u2 = out[8];
            check(near(ud.amount, 2.0f, 1e-3f) && near(u0, 2.0f, 1e-3f) && near(u2, 2.0f, 1e-3f), "uniform scale doubles every axis");

            Snap sn;
            sn.scaleStep = 0.25f;
            Drag sd;
            beginDrag(sd, v, lf, Mode::Scale, 0, stip, sn);
            const f32 cur3[2] = {stip[0] + sd.dirPx[0] * kHandlePixels * 0.4f, stip[1] + sd.dirPx[1] * kHandlePixels * 0.4f};
            updateDrag(sd, cur3, delta);
            check(near(sd.amount, 1.5f, 1e-3f), "scale snaps the factor (1.4 -> 1.5 at step 0.25)");
        }

        // Edge-on handle: looking straight down the X axis, the X handle has no screen direction.
        {
            gizmo::View e;
            e.width = 800; e.height = 600;
            const f32 eye2[3] = {-3000, 0, 0}, t2[3] = {0, 0, 0};
            makeViewProj(e.viewProj, eye2, t2, 50.0f, 800.0f / 600.0f);
            Frame ef;
            frameFromMatrix(Mode::Translate, Space::World, mat, ef);
            f32 t[2];
            check(!axisTip(e, ef, 0, t), "an edge-on handle has no tip");
            Drag drag;
            const f32 c[2] = {400, 300};
            check(!beginDrag(drag, e, ef, Mode::Translate, 0, c) && !drag.active, "and cannot be dragged");
            check(axisTip(e, ef, 1, t), "the other two are fine");
        }
    }

    AVER_INFO("InstanceSetModelTest: {} of {} checks passed", g_checks - g_failures, g_checks);
    return g_failures ? 1 : 0;
}
