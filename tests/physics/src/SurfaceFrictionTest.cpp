// Per-surface friction (physics_water_abi.h): a box sliding on a heightfield that is flagged
// surface-sampled travels by the sampler's friction; an unflagged heightfield ignores the sampler; and the
// result is the same run to run although the sampler is called from physics worker threads.
// Links Aver.Physics + Aver.Core only (the ABI).
#include "aver/physics/physics_abi.h"
#include "aver/physics/physics_water_abi.h"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"

#include <atomic>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

struct Cfg {
    float friction = 0.0f;
    std::atomic<int> calls{0};
    std::atomic<int> offMain{0};
    std::thread::id mainId;
};

static int32_t surfaceSampler(float, float, float* fr, float* re, void* user) {
    Cfg* c = static_cast<Cfg*>(user);
    ++c->calls;
    if (std::this_thread::get_id() != c->mainId) ++c->offMain;
    *fr = c->friction;
    *re = 0.0f;
    return 1;
}

// Slides a box across a flat heightfield and returns how far along +X it got in `seconds`.
static float slide(bool flagged, float friction, Cfg& cfg) {
    aver_phys_init();
    aver_phys_set_gravity(0, 0, -981);
    const int n = 9;
    std::vector<float> samples(static_cast<size_t>(n) * n, 0.0f);
    const int32_t field = aver_phys_add_heightfield(samples.data(), n, 200.0f, 800.0f, -800.0f, 0.0f);
    if (flagged) aver_phys_body_set_surface_sampled(field, 1);
    cfg.friction = friction;
    aver_phys_set_surface_sampler(&surfaceSampler, &cfg);
    const int32_t box = aver_phys_add_dynamic_box(-700, 0, 50.5f, 50, 50, 50, 0.0f);
    aver_phys_body_set_friction(box, 0.5f);
    aver_phys_body_set_velocity(box, 400.0f, 0, 0);
    const float fixed = aver_phys_fixed_step();
    for (float t = 0.0f; t < 1.0f; t += fixed) aver_phys_step(fixed);
    float p[3] = {0, 0, 0};
    aver_phys_body_position(box, p);
    aver_phys_shutdown();
    return p[0] + 700.0f;
}

int main() {
    Cfg cfg;
    cfg.mainId = std::this_thread::get_id();

    const float slick = slide(true, 0.0f, cfg);
    const float grippy = slide(true, 1.0f, cfg);
    check(cfg.calls > 0, "the sampler was called (" + std::to_string(cfg.calls.load()) + " calls)");
    check(slick > grippy + 50.0f, "a box slides further on friction 0 (" + std::to_string(slick) + " cm) than on friction 1 (" + std::to_string(grippy) + " cm)");

    const int before = cfg.calls;
    const float unflaggedA = slide(false, 0.0f, cfg);
    const float unflaggedB = slide(false, 1.0f, cfg);
    check(cfg.calls == before, "an unflagged heightfield never asks the sampler");
    check(std::fabs(unflaggedA - unflaggedB) < 0.01f, "...so the sampled value changes nothing (" + std::to_string(unflaggedA) + " vs " + std::to_string(unflaggedB) + ")");

    const float r1 = slide(true, 0.3f, cfg), r2 = slide(true, 0.3f, cfg), r3 = slide(true, 0.3f, cfg);
    check(r1 == r2 && r2 == r3, "identical result run to run (" + std::to_string(r1) + ", " + std::to_string(r2) + ", " + std::to_string(r3) + ")");
    AVER_INFO("  sampler calls off the main thread: {} of {}", cfg.offMain.load(), cfg.calls.load());

    check(aver_phys_body_set_surface_sampled(0, 1) == 0, "a dead handle is refused");

    AVER_INFO("SurfaceFriction tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
