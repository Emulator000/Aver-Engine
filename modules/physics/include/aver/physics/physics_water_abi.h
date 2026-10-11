#pragma once
// Aver.Physics -- WATER AND SURFACE SAMPLERS: buoyancy that asks the host where the water is, and
// per-surface friction for a terrain body.
//
// WHY CALLBACKS. aver_phys_set_water_plane gives every body one infinite flat plane. A river has a
// current, a lake has a bank, an ocean has waves, and a bounded WATER record has edges; the host knows
// all four and the physics module should link none of them. So the host installs a sampler, the step
// loop calls it for every dynamic body each fixed substep, and physics stays free of fluids, framework
// and landscape (the aver_fw_set_fluid_spawn_provider pattern).
//
// Engine units and axes throughout: centimetres, +Z up. The water sampler runs on the thread that
// calls aver_phys_step. THE SURFACE SAMPLER RUNS ON PHYSICS WORKER THREADS (the contact listener), so it
// must be a pure read of data that does not change while the world steps.
#include <stdint.h>

#if defined(_WIN32)
#  if defined(AVER_PHYS_BUILD)
#    define AVER_PHYS_API __declspec(dllexport)
#  else
#    define AVER_PHYS_API __declspec(dllimport)
#  endif
#else
#  define AVER_PHYS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AverPhysWaterSample {
    float surfaceZCm;      /* world Z of the surface at (x, y) */
    float normal[3];       /* unit, engine axes */
    float currentCmS[3];   /* water velocity, cm/s */
    float buoyancy;        /* as aver_phys_set_water_plane; <= 0 = none here */
    float linearDrag, angularDrag;
} AverPhysWaterSample;

/* Returns 1 = water here (use `out`); 0 = no water here (the body gets no buoyancy);
   -1 = no opinion (fall back to the per-body override or the plane). */
typedef int32_t (*aver_phys_water_sampler_fn)(float xCm, float yCm, float zCm, AverPhysWaterSample* out, void* user);

/* Installs the sampler, replacing any other. Called per rigid dynamic body per fixed substep, for bodies
   with no per-body override (aver_phys_set_water_volume still wins). For a body whose horizontal extent
   exceeds 300 cm the centre and the four corners of its bounds are sampled and averaged (height,
   current, renormalised normal). Always returns 1. */
AVER_PHYS_API int32_t aver_phys_set_water_sampler(aver_phys_water_sampler_fn fn, void* user);
AVER_PHYS_API void    aver_phys_clear_water_sampler(void);
AVER_PHYS_API int32_t aver_phys_has_water_sampler(void);

/* Friction and restitution of a terrain surface at a world XY. 1 = valid, 0 = use the body's own. */
typedef int32_t (*aver_phys_surface_sampler_fn)(float xCm, float yCm, float* outFriction, float* outRestitution, void* user);
AVER_PHYS_API int32_t aver_phys_set_surface_sampler(aver_phys_surface_sampler_fn fn, void* user);
AVER_PHYS_API void    aver_phys_clear_surface_sampler(void);

/* Marks a body (normally the terrain heightfield) as surface-sampled: in every contact with it the OTHER
   body's friction is combined with the sampled value (sqrt of the product, Jolt's default rule) and its
   restitution with the larger of the two. Returns 0 for a dead handle. */
AVER_PHYS_API int32_t aver_phys_body_set_surface_sampled(int32_t body, int32_t on);

#ifdef __cplusplus
}
#endif
