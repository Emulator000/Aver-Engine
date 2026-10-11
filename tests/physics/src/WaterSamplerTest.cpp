// The host water sampler (physics_water_abi.h), through the plain-C ABI only: a flat sampler matches the
// plane, "no water here" lets a body fall, a tilted surface tilts a floating box, a current carries it, -1
// falls back to the plane, a per-body override still wins, and clearing restores the plane.
// Links Aver.Physics + Aver.Core only, like BuoyancyTest, for the same reason.
#include "aver/physics/physics_abi.h"
#include "aver/physics/physics_water_abi.h"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <string>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static void step(float seconds) {
    const float fixed = aver_phys_fixed_step();
    for (float t = 0.0f; t < seconds; t += fixed) aver_phys_step(fixed);
}
static float bodyZ(int32_t b) { float p[3] = {0, 0, 0}; aver_phys_body_position(b, p); return p[2]; }
static float bodyX(int32_t b) { float p[3] = {0, 0, 0}; aver_phys_body_position(b, p); return p[0]; }

struct Cfg {
    int mode = 0;            // 0 flat, 1 water only for |x| < 500, 2 no opinion
    float surfaceZ = 0.0f;
    float normal[3] = {0, 0, 1};
    float current[3] = {0, 0, 0};
    float buoyancy = 1.4f;
    int calls = 0;
};

static int32_t sampler(float x, float, float, AverPhysWaterSample* out, void* user) {
    Cfg* c = static_cast<Cfg*>(user);
    ++c->calls;
    if (c->mode == 2) return -1;
    if (c->mode == 1 && std::fabs(x) >= 500.0f) return 0;
    out->surfaceZCm = c->surfaceZ;
    for (int i = 0; i < 3; ++i) { out->normal[i] = c->normal[i]; out->currentCmS[i] = c->current[i]; }
    out->buoyancy = c->buoyancy;
    out->linearDrag = 0.5f;
    out->angularDrag = 0.01f;
    return 1;
}

static void startWorld() {
    check(aver_phys_init() == 1, "physics started");
    aver_phys_set_gravity(0, 0, -981);
}

static int32_t addBox(float x, float z) { return aver_phys_add_dynamic_box(x, 0, z, 50, 50, 50, 0.0f); }

int main() {
    const float up[3] = {0, 0, 1}, still[3] = {0, 0, 0};

    AVER_INFO("-- a flat sampler settles where the plane does --");
    float planeZ = 0, sampledZ = 0;
    {
        startWorld();
        const int32_t b = addBox(0, 300);
        aver_phys_set_water_plane(0.0f, up, 1.4f, 0.5f, 0.01f, still);
        step(6.0f);
        planeZ = bodyZ(b);
        aver_phys_shutdown();
    }
    {
        startWorld();
        Cfg cfg;
        check(aver_phys_set_water_sampler(&sampler, &cfg) == 1 && aver_phys_has_water_sampler() == 1, "sampler installed");
        const int32_t b = addBox(0, 300);
        step(6.0f);
        sampledZ = bodyZ(b);
        check(cfg.calls > 0, "the step loop called the sampler (" + std::to_string(cfg.calls) + " calls)");
        check(aver_phys_buoyant_body_count() >= 1, "a sampled body counts as buoyant");
        aver_phys_shutdown();
        check(aver_phys_has_water_sampler() == 0, "shutdown clears the sampler");
    }
    check(std::fabs(planeZ - sampledZ) < 1.0f && planeZ > -150.0f && planeZ < 150.0f,
          "same resting height as the plane (plane " + std::to_string(planeZ) + " cm, sampler " + std::to_string(sampledZ) + " cm)");

    AVER_INFO("-- no water outside a rectangle lets a body fall --");
    {
        startWorld();
        Cfg cfg;
        cfg.mode = 1;
        aver_phys_set_water_sampler(&sampler, &cfg);
        const int32_t inside = addBox(0, 300);
        const int32_t outside = addBox(3000, 300);
        step(4.0f);
        check(bodyZ(inside) > -200.0f, "inside the rectangle it floats (z " + std::to_string(bodyZ(inside)) + ")");
        check(bodyZ(outside) < -1000.0f, "outside it falls straight through (z " + std::to_string(bodyZ(outside)) + ")");
        aver_phys_shutdown();
    }

    AVER_INFO("-- a tilted surface tilts a floating box --");
    float flatTilt = 0, slopeTilt = 0;
    for (int slope = 0; slope < 2; ++slope) {
        startWorld();
        Cfg cfg;
        if (slope) { cfg.normal[0] = 0.5f; cfg.normal[2] = std::sqrt(1.0f - 0.25f); }
        aver_phys_set_water_sampler(&sampler, &cfg);
        const int32_t b = addBox(0, 40);
        step(3.0f);
        float q[4] = {0, 0, 0, 1};
        aver_phys_body_rotation(b, q);
        const float tilt = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
        (slope ? slopeTilt : flatTilt) = tilt;
        aver_phys_shutdown();
    }
    check(slopeTilt > flatTilt + 0.02f, "a sloped surface rolls the box (flat " + std::to_string(flatTilt) + ", sloped " + std::to_string(slopeTilt) + ")");

    AVER_INFO("-- a current carries a floating box --");
    float vxCurrent = 0, vxStill = 0;
    for (int run = 0; run < 2; ++run) {
        startWorld();
        Cfg cfg;
        if (run) cfg.current[0] = 300.0f;
        aver_phys_set_water_sampler(&sampler, &cfg);
        const int32_t b = addBox(0, 40);
        step(4.0f);
        float v[3] = {0, 0, 0};
        aver_phys_body_velocity(b, v);
        (run ? vxCurrent : vxStill) = v[0];
        const float x = bodyX(b);
        if (run) check(x > 100.0f, "it moved downstream (x " + std::to_string(x) + ")");
        aver_phys_shutdown();
    }
    check(vxCurrent > 30.0f && std::fabs(vxStill) < 5.0f, "downstream velocity with a current (" + std::to_string(vxCurrent) + "), none without (" + std::to_string(vxStill) + ")");

    AVER_INFO("-- -1 falls back to the plane; a per-body override still wins; clearing restores the plane --");
    {
        startWorld();
        Cfg cfg;
        cfg.mode = 2;
        aver_phys_set_water_sampler(&sampler, &cfg);
        aver_phys_set_water_plane(0.0f, up, 1.4f, 0.5f, 0.01f, still);
        const int32_t viaPlane = addBox(0, 300);
        step(5.0f);
        check(bodyZ(viaPlane) > -200.0f, "no opinion: the plane holds the box (z " + std::to_string(bodyZ(viaPlane)) + ")");
        aver_phys_shutdown();
    }
    {
        startWorld();
        Cfg cfg;   // flat water, buoyancy 1.4
        aver_phys_set_water_sampler(&sampler, &cfg);
        const int32_t sinker = addBox(-2000, 300);
        const int32_t floater = addBox(2000, 300);
        const float surf[3] = {-2000, 0, 0};
        aver_phys_set_water_volume(sinker, surf, up, 0.4f, 0.5f, 0.01f, still);
        step(5.0f);
        check(bodyZ(sinker) < bodyZ(floater) - 50.0f, "the per-body override wins over the sampler (override z " + std::to_string(bodyZ(sinker)) +
                                                           ", sampled z " + std::to_string(bodyZ(floater)) + ")");
        aver_phys_shutdown();
    }
    {
        startWorld();
        Cfg cfg;
        cfg.mode = 1;   // water nowhere near the body
        aver_phys_set_water_sampler(&sampler, &cfg);
        aver_phys_set_water_plane(0.0f, up, 1.4f, 0.5f, 0.01f, still);
        const int32_t b = addBox(3000, 300);
        step(3.0f);
        check(bodyZ(b) < -500.0f, "a sampler's 'no water' beats the plane for that body (z " + std::to_string(bodyZ(b)) + ")");
        aver_phys_clear_water_sampler();
        check(aver_phys_has_water_sampler() == 0, "cleared");
        aver_phys_body_set_position(b, 3000, 0, 300);
        aver_phys_body_set_velocity(b, 0, 0, 0);
        step(5.0f);
        check(bodyZ(b) > -200.0f, "after clearing, the plane holds it again (z " + std::to_string(bodyZ(b)) + ")");
        aver_phys_shutdown();
    }

    AVER_INFO("WaterSampler tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
