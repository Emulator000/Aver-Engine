// RigPreviewTest -- the orbit camera, projection, ray and picking math the rig editors' skeleton view uses. No ImGui:
// this is the headless half of RigPreview.
#include "RigPreview.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <string>

using namespace aver;
using namespace aver::editor;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static std::string f2s(f32 v) { char b[64]; std::snprintf(b, sizeof b, "%.3f", v); return b; }

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AVER_INFO("rig preview camera");

    RigView v;
    v.target = Vec3{0, 0, 100};
    v.yawDeg = 0.0f;
    v.pitchDeg = 0.0f;
    v.distance = 300.0f;
    const f32 W = 800.0f, H = 600.0f;

    AVER_INFO("=== the basis ===");
    {
        const Vec3 eye = rigEye(v);
        check(std::fabs(eye.x - 300.0f) < 1e-3f && std::fabs(eye.z - 100.0f) < 1e-3f, "yaw 0 pitch 0 puts the eye on +X at the target's height");
        const Vec3 f = rigForward(v), r = rigRight(v), u = rigCamUp(v);
        check(std::fabs(f.x + 1.0f) < 1e-4f, "looking back along -X");
        check(dot(f, r) < 1e-4f && dot(f, u) < 1e-4f && dot(r, u) < 1e-4f, "forward, right and up are orthogonal");
        check(u.z > 0.99f, "camera up is +Z");
    }

    AVER_INFO("=== projection ===");
    {
        f32 sx = 0, sy = 0, depth = 0;
        check(rigProject(v, W, H, v.target, sx, sy, &depth) && std::fabs(sx - W * 0.5f) < 1e-2f && std::fabs(sy - H * 0.5f) < 1e-2f &&
                  std::fabs(depth - 300.0f) < 1e-2f, "the target projects to the middle of the view at the orbit distance");
        f32 hx, hy, ux, uy;
        rigProject(v, W, H, v.target + rigCamUp(v) * 30.0f, ux, uy);
        rigProject(v, W, H, v.target + rigRight(v) * 30.0f, hx, hy);
        check(uy < H * 0.5f - 1.0f && std::fabs(ux - W * 0.5f) < 0.01f, "a point above the target is above the middle of the screen");
        check(hx > W * 0.5f + 1.0f && std::fabs(hy - H * 0.5f) < 0.01f, "a point to the camera's right is right of the middle");
        check(!rigProject(v, W, H, rigEye(v) + Vec3{50, 0, 0}, sx, sy), "a point behind the camera does not project");
    }

    AVER_INFO("=== a ray through a projected pixel hits the point ===");
    {
        RigView o;
        o.target = Vec3{10, -20, 80};
        o.yawDeg = 57.0f;
        o.pitchDeg = 25.0f;
        o.distance = 450.0f;
        const Vec3 p{35, 12, 140};
        f32 sx, sy;
        check(rigProject(o, W, H, p, sx, sy), "(the point is in front)");
        Vec3 origin, dir;
        rigRay(o, W, H, sx, sy, origin, dir);
        const Vec3 toP = p - origin;
        const f32 along = dot(toP, dir);
        const f32 miss = (toP - dir * along).size();
        check(miss < 0.05f && std::fabs(dir.size() - 1.0f) < 1e-4f, "the ray passes through it, missing by " + f2s(miss) + " cm");

        Vec3 back;
        check(rigPlanePoint(o, W, H, sx, sy, p, back) && (back - p).size() < 0.05f, "and the camera-facing plane through it returns it exactly");
        // Dragging 20 pixels right moves the point along the camera's right axis only.
        Vec3 moved;
        rigPlanePoint(o, W, H, sx + 20.0f, sy, p, moved);
        const Vec3 d = moved - p;
        check(std::fabs(dot(d, rigForward(o))) < 0.05f && dot(d, rigRight(o)) > 0.5f && std::fabs(dot(d, rigCamUp(o))) < 0.05f,
              "a horizontal drag stays on the plane and moves along the camera's right");
    }

    AVER_INFO("=== orbit, pan, zoom, frame ===");
    {
        RigView o;
        rigOrbit(o, 100.0f, 10000.0f);
        check(o.pitchDeg <= 85.0f && o.pitchDeg >= -85.0f, "pitch is clamped, " + f2s(o.pitchDeg));
        rigOrbit(o, 5000.0f, 0.0f);
        check(o.yawDeg >= -180.0f && o.yawDeg <= 180.0f, "yaw wraps");
        const f32 d0 = o.distance;
        rigZoom(o, 3.0f);
        check(o.distance < d0, "positive wheel zooms in");
        for (int i = 0; i < 100; ++i) rigZoom(o, -5.0f);
        check(o.distance <= 4000.0f + 1e-3f, "and the distance is clamped");
        RigView p;
        const Vec3 t0 = p.target;
        rigPan(p, W, H, 50.0f, 0.0f);
        check(dot(p.target - t0, rigRight(p)) < -1.0f, "dragging right moves the target left (the scene follows the cursor)");
        RigView fr;
        rigFrame(fr, Vec3{0, 0, 90}, 100.0f);
        f32 sx, sy;
        check(rigProject(fr, W, H, Vec3{0, 0, 190}, sx, sy) && sy > -50.0f && sy < H + 50.0f, "framing a sphere keeps its top inside the view");
        std::vector<Mat4> pts(2);
        pts[0].m[3][2] = 0.0f;
        pts[1].m[3][2] = 180.0f;
        RigView fp;
        rigFramePose(fp, pts);
        check(std::fabs(fp.target.z - 90.0f) < 1e-3f, "framing a pose centres its bounds");
    }

    AVER_INFO("=== picking ===");
    {
        std::vector<Vec3> pts = {Vec3{0, -30, 100}, Vec3{0, 0, 100}, Vec3{0, 30, 100}, rigEye(v) + Vec3{40, 0, 0}};
        f32 sx, sy;
        rigProject(v, W, H, pts[2], sx, sy);
        check(rigPick(v, W, H, pts, sx + 3.0f, sy - 2.0f, 8.0f) == 2, "the nearest joint within the radius is picked");
        check(rigPick(v, W, H, pts, sx + 60.0f, sy, 8.0f) == -1, "none within the radius is -1");
        rigProject(v, W, H, pts[1], sx, sy);
        check(rigPick(v, W, H, pts, sx, sy, 400.0f) == 1, "the closest wins when several are in range");
        check(rigPick(v, W, H, pts, 0.0f, 0.0f, 2.0f) == -1, "and a point behind the camera is never picked");
    }

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
