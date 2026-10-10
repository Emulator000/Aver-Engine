// importUsdStage over hand-written .usda stages: !resetXformStack!, xformOpOrder in the text reader,
// overlapping GeomSubsets, exact-coplanar duplicate triangles, the exact-matrix flag on a placement,
// and the diagnostics the importer reports about all of them.
//
// Binary (USDC) layers cannot be authored here -- there is no crate writer -- so what only a crate
// reaches (a reset behind a reference, a nested arc) is covered through placementForMatrix, the one
// seam the stage importer exposes for it, and by the text-layer equivalents below.
//
// The stages are Z-up, metersPerUnit 0.01: the importer's scale folds to 1 and the engine axes to
// (x, -y, z), so a position in a case reads straight off the fixture.
#include "aver/formats/UsdImport.hpp"
#include "aver/core/Log.hpp"

#include "../../../modules/formats/src/UsdImportInternal.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace aver;
namespace fs = std::filesystem;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static void checkNear(f32 got, f32 want, f32 tol, const std::string& what) {
    check(std::fabs(got - want) <= tol,
          what + "  (got " + std::to_string(got) + ", want " + std::to_string(want) + ")");
}

static const char* kHeader =
    "#usda 1.0\n"
    "(\n"
    "    upAxis = \"Z\"\n"
    "    metersPerUnit = 0.01\n"
    ")\n";

// One triangle, points (1,0,0) (2,0,0) (1,1,0): the first vertex's position shows the rotation and
// scale a prim's ops baked into it.
static const char* kTri =
    "    int[] faceVertexCounts = [3]\n"
    "    int[] faceVertexIndices = [0, 1, 2]\n"
    "    point3f[] points = [(1, 0, 0), (2, 0, 0), (1, 1, 0)]\n";

static fs::path g_dir;

static bool importDoc(const std::string& body, fmt::UsdImportResult& r, const fmt::UsdImportOptions& opt = {}) {
    static int n = 0;
    const fs::path p = g_dir / ("case" + std::to_string(n++) + ".usda");
    {
        std::ofstream f(p, std::ios::binary);
        f << kHeader << body;
    }
    std::string why;
    const bool ok = fmt::importUsdStage(p.string(), r, opt, &why);
    if (!ok) AVER_INFO("    (importUsdStage said: {})", why);
    return ok;
}

static int findPlacement(const fmt::UsdImportResult& r, const std::string& name) {
    for (usize i = 0; i < r.placements.size(); ++i)
        if (r.placements[i].name == name) return static_cast<int>(i);
    return -1;
}

static const fmt::UsdSkipCount* skipReason(const fmt::UsdImportResult& r, const std::string& reason) {
    for (const fmt::UsdSkipCount& s : r.diagnostics.skipped) if (s.reason == reason) return &s;
    return nullptr;
}

// Checks one prim's placement position (engine space) and returns its first baked vertex x/y/z.
static void expectAt(const fmt::UsdImportResult& r, const std::string& name, f32 x, f32 y, f32 z,
                     const std::string& what, f32 out[3] = nullptr) {
    const int i = findPlacement(r, name);
    check(i >= 0, what + ": placement " + name + " exists");
    if (i < 0) return;
    const fmt::UsdPlacement& p = r.placements[static_cast<usize>(i)];
    checkNear(p.position.x, x, 1e-3f, what + " x");
    checkNear(p.position.y, y, 1e-3f, what + " y");
    checkNear(p.position.z, z, 1e-3f, what + " z");
    if (out && p.meshIndex >= 0 && static_cast<usize>(p.meshIndex) < r.meshes.size())
        for (int k = 0; k < 3; ++k) out[k] = r.meshes[static_cast<usize>(p.meshIndex)].positions[static_cast<usize>(k)];
}

static std::string prim(const char* name, const std::string& ops, const std::string& kids = {}) {
    return std::string("def Xform \"") + name + "\"\n{\n" + ops + kids + "}\n";
}

static std::string meshPrim(const char* name, const std::string& extra = {}) {
    return std::string("    def Mesh \"") + name + "\"\n    {\n" + kTri + extra + "    }\n";
}

int main() {
    std::error_code ec;
    g_dir = fs::temp_directory_path() / "aver_usdstage_test";
    fs::remove_all(g_dir, ec);
    fs::create_directories(g_dir, ec);

    AVER_INFO("=== !resetXformStack! (text layer) ===");
    {
        fmt::UsdImportResult r;
        const bool ok = importDoc(
            prim("A",
                 "    double3 xformOp:translate = (100, 0, 0)\n"
                 "    uniform token[] xformOpOrder = [\"xformOp:translate\"]\n",
                 meshPrim("n") +
                 prim("B",
                      "        double3 xformOp:translate = (0, 10, 0)\n"
                      "        uniform token[] xformOpOrder = [\"!resetXformStack!\", \"xformOp:translate\"]\n",
                      meshPrim("m"))),
            r);
        check(ok, "a stage with a reset prim imports");
        expectAt(r, "/A/n", 100, 0, 0, "an ordinary child inherits its parent's translate");
        expectAt(r, "/A/B/m", 0, -10, 0, "a mesh under a reset prim ignores the parent's translate (Y flips to -10)");
        const fmt::UsdSkipCount* rs = skipReason(r, "reset-xform-stack");
        check(rs && rs->count == 1, "the reset prim is counted once");
        check(rs && !rs->examples.empty() && rs->examples[0] == "/A/B", "and named by its path");
    }

    AVER_INFO("=== xformOpOrder in the text reader ===");
    {
        fmt::UsdImportResult r;
        const bool ok = importDoc(
            prim("P1", "    double3 xformOp:translate = (10, 0, 0)\n    float xformOp:rotateZ = 90\n"
                       "    uniform token[] xformOpOrder = [\"xformOp:translate\", \"xformOp:rotateZ\"]\n", meshPrim("m")) +
            prim("P2", "    double3 xformOp:translate = (10, 0, 0)\n    float xformOp:rotateZ = 90\n"
                       "    uniform token[] xformOpOrder = [\"xformOp:rotateZ\", \"xformOp:translate\"]\n", meshPrim("m")) +
            prim("P3", "    double3 xformOp:translate = (1, 0, 0)\n    double3 xformOp:scale = (2, 2, 2)\n"
                       "    uniform token[] xformOpOrder = [\"xformOp:translate\", \"xformOp:scale\"]\n", meshPrim("m")) +
            prim("P4", "    double3 xformOp:translate = (1, 0, 0)\n    double3 xformOp:scale = (2, 2, 2)\n"
                       "    uniform token[] xformOpOrder = [\"xformOp:scale\", \"xformOp:translate\"]\n", meshPrim("m")) +
            prim("P5", "    double3 xformOp:translate:a = (5, 0, 0)\n    double3 xformOp:translate:b = (2, 0, 0)\n"
                       "    uniform token[] xformOpOrder = [\"xformOp:translate:a\", \"!invert!xformOp:translate:b\"]\n", meshPrim("m")) +
            prim("P6", "    double3 xformOp:translate = (100, 0, 0)\n    double3 xformOp:scale = (1, 1, 1)\n"
                       "    uniform token[] xformOpOrder = [\"xformOp:scale\"]\n", meshPrim("m")) +
            prim("P7", "    double3 xformOp:translate = (7, 0, 0)\n    double3 xformOp:scale = (2, 2, 2)\n", meshPrim("m")),
            r);
        check(ok, "a stage of ordered ops imports");
        f32 v[3] = {0, 0, 0};
        expectAt(r, "/P1/m", 10, 0, 0, "[translate, rotateZ]: the translate is not rotated", v);
        checkNear(v[0], 0.0f, 1e-3f, "  and the vertex (1,0,0) is turned a quarter: x");
        checkNear(v[1], -1.0f, 1e-3f, "  y (USD +Y is the engine's -Y)");
        expectAt(r, "/P2/m", 0, -10, 0, "[rotateZ, translate]: the translate is rotated with it");
        expectAt(r, "/P3/m", 1, 0, 0, "[translate, scale]: the translate is not scaled", v);
        checkNear(v[0], 2.0f, 1e-3f, "  and the vertex is");
        expectAt(r, "/P4/m", 2, 0, 0, "[scale, translate]: the translate is scaled");
        expectAt(r, "/P5/m", 3, 0, 0, "suffixed ops and !invert!: translate:a then the inverse of translate:b");
        expectAt(r, "/P6/m", 0, 0, 0, "an op the order does not list is not applied");
        expectAt(r, "/P7/m", 7, 0, 0, "no xformOpOrder: scale, then translate, as before", v);
        checkNear(v[0], 2.0f, 1e-3f, "  scale reached the vertex");
    }

    AVER_INFO("=== overlapping GeomSubsets ===");
    {
        const std::string quad =
            "    int[] faceVertexCounts = [3, 3]\n"
            "    int[] faceVertexIndices = [0, 1, 2, 0, 2, 3]\n"
            "    point3f[] points = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)]\n";
        const std::string subsets =
            "    def GeomSubset \"A\"\n    {\n        uniform token elementType = \"face\"\n"
            "        int[] indices = [0, 1]\n        rel material:binding = </Looks/M1>\n    }\n"
            "    def GeomSubset \"B\"\n    {\n        uniform token elementType = \"face\"\n"
            "        int[] indices = [1]\n        rel material:binding = </Looks/M2>\n    }\n";
        fmt::UsdImportResult r;
        const bool ok = importDoc("def Mesh \"quad\"\n{\n" + quad + subsets + "}\n", r);
        check(ok && r.meshes.size() == 1, "a mesh with overlapping subsets imports");
        if (ok && r.meshes.size() == 1) {
            const fmt::OcMeshData& m = r.meshes[0];
            check(m.indices.size() == 6, "its two faces are built once each, not three times");
            check(m.submeshes.size() == 1 && m.submeshes[0].indexCount == 6,
                  "the first subset kept both; the second had nothing left and made no submesh");
            check(r.diagnostics.duplicateSubsetFaces == 1, "one duplicate face is counted");
            check(!r.diagnostics.duplicateSubsetMeshes.empty() && r.diagnostics.duplicateSubsetMeshes[0] == "/quad",
                  "and the mesh named");
            bool said = false;
            for (const std::string& u : r.unsupported) if (u.find("more than one GeomSubset") != std::string::npos) said = true;
            check(said, "and reported in `unsupported`");
        }
    }

    AVER_INFO("=== exact-coplanar duplicate triangles across slots ===");
    {
        const std::string body =
            "    int[] faceVertexCounts = [3, 3, 3]\n"
            "    int[] faceVertexIndices = [0, 1, 2, 2, 1, 0, 1, 3, 2]\n"
            "    point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)]\n"
            "    def GeomSubset \"SA\"\n    {\n        uniform token elementType = \"face\"\n"
            "        int[] indices = [0, 2]\n        rel material:binding = </Looks/M1>\n    }\n"
            "    def GeomSubset \"SB\"\n    {\n        uniform token elementType = \"face\"\n"
            "        int[] indices = [1]\n        rel material:binding = </Looks/M2>\n    }\n";
        const std::string doc = "def Mesh \"tile\"\n{\n" + body + "}\n";
        {
            fmt::UsdImportResult r;
            const bool ok = importDoc(doc, r);
            check(ok && r.meshes.size() == 1, "a mesh with a coplanar duplicate imports");
            if (ok && r.meshes.size() == 1) {
                check(r.meshes[0].indices.size() == 9, "Report keeps all three triangles");
                check(r.meshes[0].submeshes.size() == 2, "and both slots");
                check(r.diagnostics.coplanar.size() == 1 && r.diagnostics.coplanarTriangles == 1,
                      "one slot pair holds one shared triangle (the opposite winding counts)");
                if (!r.diagnostics.coplanar.empty()) {
                    const fmt::UsdCoplanarPair& p = r.diagnostics.coplanar[0];
                    check(p.mesh == "/tile" && p.slotA == "/tile/SA" && p.slotB == "/tile/SB",
                          "the pair names the mesh and both slots, earlier first");
                    check(!p.dropped, "and says nothing was dropped");
                }
                const std::string text = fmt::usdImportReportText(r);
                check(text.find("/tile/SA / /tile/SB x1") != std::string::npos, "the report text lists the pair");
            }
        }
        {
            fmt::UsdImportOptions opt;
            opt.resolveCoplanar = fmt::UsdCoplanarPolicy::DropLater;
            fmt::UsdImportResult r;
            const bool ok = importDoc(doc, r, opt);
            check(ok && r.meshes.size() == 1, "DropLater imports it");
            if (ok && r.meshes.size() == 1) {
                const fmt::OcMeshData& m = r.meshes[0];
                check(m.indices.size() == 6, "the later slot's copy is removed");
                check(m.submeshes.size() == 1 && m.submeshes[0].name == "/tile/SA" && m.submeshes[0].indexCount == 6,
                      "the emptied slot is gone and the earlier one keeps both of its triangles");
                check(!r.diagnostics.coplanar.empty() && r.diagnostics.coplanar[0].dropped, "and the pair says it was dropped");
            }
        }
        {
            const std::string distinct =
                "def Mesh \"ok\"\n{\n"
                "    int[] faceVertexCounts = [3, 3]\n"
                "    int[] faceVertexIndices = [0, 1, 2, 1, 3, 2]\n"
                "    point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)]\n"
                "    def GeomSubset \"SA\"\n    {\n        uniform token elementType = \"face\"\n"
                "        int[] indices = [0]\n        rel material:binding = </Looks/M1>\n    }\n"
                "    def GeomSubset \"SB\"\n    {\n        uniform token elementType = \"face\"\n"
                "        int[] indices = [1]\n        rel material:binding = </Looks/M2>\n    }\n"
                "    float[] primvars:displayOpacity = [0.5] (interpolation = \"constant\")\n"
                "    color3f[] primvars:displayColor = [(1, 0, 0)] (interpolation = \"constant\")\n"
                "}\n";
            fmt::UsdImportResult r;
            const bool ok = importDoc(distinct, r);
            check(ok && r.meshes.size() == 1 && r.meshes[0].submeshes.size() == 2, "two distinct subsets import as two slots");
            check(r.diagnostics.coplanar.empty() && r.diagnostics.coplanarTriangles == 0, "and nothing is called coplanar");
            check(r.diagnostics.duplicateSubsetFaces == 0, "nor duplicated");
            check(r.diagnostics.displayOpacityMeshes == 1 && r.diagnostics.displayOpacityNames.size() == 1 &&
                  r.diagnostics.displayOpacityNames[0] == "/ok", "primvars:displayOpacity is reported by mesh name");
            check(r.diagnostics.displayColorMeshes == 1, "and primvars:displayColor");
        }
    }

    AVER_INFO("=== tracePrims ===");
    {
        const std::string doc =
            prim("A", "    double3 xformOp:translate = (100, 0, 0)\n    uniform token[] xformOpOrder = [\"xformOp:translate\"]\n",
                 meshPrim("n") +
                 prim("B", "        uniform token[] xformOpOrder = [\"!resetXformStack!\"]\n", meshPrim("m")));
        fmt::UsdImportResult quiet;
        check(importDoc(doc, quiet) && quiet.diagnostics.trace.empty(), "without tracePrims nothing is traced");
        fmt::UsdImportOptions opt;
        opt.tracePrims = {"/A/B"};
        fmt::UsdImportResult r;
        check(importDoc(doc, r, opt), "a traced import succeeds");
        bool allUnder = !r.diagnostics.trace.empty(), sawReset = false;
        for (const std::string& t : r.diagnostics.trace) {
            if (t.rfind("/A/B", 0) != 0) allUnder = false;
            if (t.find("resetXformStack=1") != std::string::npos) sawReset = true;
        }
        check(allUnder, "only the traced prefix is logged");
        check(sawReset, "and the reset shows in it");
        const std::string text = fmt::usdImportReportText(r);
        check(text.find("reset-xform-stack: 1") != std::string::npos && text.find("Trace") != std::string::npos,
              "the report text carries the skip counts and the trace");
    }

    AVER_INFO("=== exact matrix on a placement ===");
    {
        using namespace aver::fmt::usd_detail;
        fmt::UsdImportOptions opt;
        Ctx c;
        c.opt = &opt;
        c.unitScale = 1.0f;
        c.yUp = false;

        const fmt::UsdPlacement plain = placementForMatrix(c, 0, mul(rotateAxis(2, 90.0f), translate(3, 0, 0)));
        check(!plain.exact, "a rotation and a translation is a plain TRS");

        const fmt::UsdPlacement uni = placementForMatrix(c, 0, mul(scaleM(2, 3, 4), mul(rotateAxis(2, 45.0f), translate(1, 2, 3))));
        check(!uni.exact, "scale then rotate (no shear) is still a plain TRS, however non-uniform");

        const fmt::UsdPlacement mirror = placementForMatrix(c, 7, mul(scaleM(-1, 1, 1), translate(3, 0, 0)));
        check(mirror.exact && mirror.meshIndex == 7, "a mirrored placement is exact");
        checkNear(mirror.matrix[0], -1.0f, 1e-5f, "  its basis keeps the reflection");
        checkNear(mirror.matrix[5], 1.0f, 1e-5f, "  and the other axes");
        checkNear(mirror.matrix[12], 3.0f, 1e-5f, "  the translation sits in matrix[12..14]");
        checkNear(mirror.matrix[15], 1.0f, 1e-6f, "  and w is 1");
        checkNear(mirror.position.x, 3.0f, 1e-5f, "  position agrees");

        M4 shear = M4::identity();
        shear.m[4] = 0.5f;
        const fmt::UsdPlacement sh = placementForMatrix(c, 0, shear);
        check(sh.exact, "a shear is exact");
        checkNear(sh.matrix[4], -0.5f, 1e-5f, "  in engine space (the Z-up flip negates the Y row's X)");

        const fmt::UsdPlacement skewed = placementForMatrix(c, 0, mul(rotateAxis(2, 45.0f), scaleM(2, 1, 1)));
        check(skewed.exact, "rotate THEN non-uniform scale shears, and is exact");

        c.yUp = true;
        const fmt::UsdPlacement yup = placementForMatrix(c, 0, mul(scaleM(1, 1, -1), translate(1, 2, 3)));
        check(yup.exact, "a Y-up mirror is exact too");
        checkNear(yup.position.x, -3.0f, 1e-5f, "  Y-up translation (1,2,3) -> engine (-z, x, y): x");
        checkNear(yup.position.y, 1.0f, 1e-5f, "  y");
        checkNear(yup.position.z, 2.0f, 1e-5f, "  z");
        checkNear(yup.matrix[14], 2.0f, 1e-5f, "  and matrix[14] agrees");
    }

    fs::remove_all(g_dir, ec);
    if (g_failures == 0) AVER_INFO("=== all USD stage import tests passed ===");
    else                 AVER_ERROR("=== {} FAILED ===", g_failures);
    return g_failures == 0 ? 0 : 1;
}
