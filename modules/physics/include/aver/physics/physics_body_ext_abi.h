#pragma once
// Aver.Physics -- PER-BODY EXTRAS: locked axes, continuous collision, velocity caps, sleep permission and
// turning an existing body into a sensor. Setters on a body that is already in the world, like the
// material and damping ones in physics_abi.h; they share its handle convention (0 is invalid).
//
// UNITS AND AXES ARE THE ENGINE'S: centimetres, +X forward, +Y right, +Z up, left-handed. The velocity
// caps take DEGREES per second for the angular one (the angle unit of this sheet), unlike
// aver_phys_body_angular_velocity in physics_abi.h which is radians/second.
//
// DYNAMIC BODIES: every setter here except the sensor flag needs a body with motion properties, i.e. not
// a static one, and returns 0 on one (aver_phys_last_error reports Unsupported); the locked-axes call
// additionally needs a dynamic body with a shape that has mass. A body whose motion type is changed with
// aver_phys_body_set_motion_type from static to movable is REBUILT, and the rebuilt body has the defaults
// -- apply these after the switch.
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

// ---- Locked axes ---------------------------------------------------------------------------------------
//
// Which of a body's six degrees of freedom are FROZEN: a bit set means locked. Typical masks are a 2.5D
// character that must stay on a plane (lock translation Y and all rotation but Z-up spin), a body that
// must not tip over (lock rotation X and Y), a wheel hung on a fixed axis (lock all translation).
//
// WORLD AXES, NOT THE BODY'S OWN. A body rotated 90 degrees about Z and locked in translation X is still
// locked along the world's X, which is Jolt's model and the one used here. A body that must keep one of
// its own axes fixed as it turns wants a joint (aver_phys_joint_hinge / slider to the world) instead.
#define AVER_PHYS_LOCK_TRANSLATION_X 0x01u
#define AVER_PHYS_LOCK_TRANSLATION_Y 0x02u
#define AVER_PHYS_LOCK_TRANSLATION_Z 0x04u
#define AVER_PHYS_LOCK_ROTATION_X    0x08u
#define AVER_PHYS_LOCK_ROTATION_Y    0x10u
#define AVER_PHYS_LOCK_ROTATION_Z    0x20u
#define AVER_PHYS_LOCK_ALL_TRANSLATION 0x07u
#define AVER_PHYS_LOCK_ALL_ROTATION    0x38u

// Sets the locked set (0 unlocks everything). The body KEEPS ITS MASS and the inertia its shape implies:
// the call rebuilds the body's mass properties, and freezing all three translations (inverse mass 0 in
// Jolt) remembers the mass so unlocking restores it. Velocity already along a locked axis is zeroed.
// Locking all six is refused: that is a static body (aver_phys_body_set_motion_type). Returns 0 for a dead
// handle, a non-dynamic body, a shape with no mass, unknown bits, or all six locked.
AVER_PHYS_API int32_t aver_phys_body_set_locked_axes(int32_t body, uint32_t mask);
// The current mask, written to `outMask`. 0 for a dead handle or a body with no motion properties.
AVER_PHYS_API int32_t aver_phys_body_locked_axes(int32_t body, uint32_t* outMask);

// ---- Continuous collision ------------------------------------------------------------------------------
//
// A discrete body is tested where it is at the end of each fixed step, so a fast small one can cross a
// thin wall between two steps without ever overlapping it -- a bullet, a thrown dart, a ball hit hard.
// CCD sweeps the body along its motion and stops it at the first thing in the way. It costs extra per
// step while the body is fast, so it is off by default and meant for the few bodies that need it. This is
// Jolt's LinearCast motion quality; it matters for dynamic bodies (a kinematic body is moved by the
// caller, who owns its path).
AVER_PHYS_API int32_t aver_phys_body_set_ccd(int32_t body, int32_t on);
AVER_PHYS_API int32_t aver_phys_body_ccd(int32_t body, int32_t* outOn);

// ---- Velocity caps -------------------------------------------------------------------------------------
//
// The most a body may move or spin; the solver clamps to it every step and a velocity set above it is
// clamped now. Jolt's defaults are 50000 cm/s (500 m/s) and ~2700 deg/s (47 rad/s). A cap is what keeps
// a ball from being flung to a speed where nothing can stop it, or a stack from exploding after a bad
// overlap. Negative or non-finite values are refused.
AVER_PHYS_API int32_t aver_phys_body_set_max_linear_velocity(int32_t body, float cmPerSecond);
AVER_PHYS_API int32_t aver_phys_body_max_linear_velocity(int32_t body, float* outCmPerSecond);
AVER_PHYS_API int32_t aver_phys_body_set_max_angular_velocity(int32_t body, float degPerSecond);
AVER_PHYS_API int32_t aver_phys_body_max_angular_velocity(int32_t body, float* outDegPerSecond);

// ---- Sleeping ------------------------------------------------------------------------------------------
//
// Whether the body may fall asleep when it comes to rest. Off for a body the game reads every frame and
// must never go stale (a driven prop, a body a joint motor keeps working); switching it off wakes the body.
// On is the default.
AVER_PHYS_API int32_t aver_phys_body_set_allow_sleeping(int32_t body, int32_t on);
AVER_PHYS_API int32_t aver_phys_body_allow_sleeping(int32_t body, int32_t* outOn);

// ---- Sensor --------------------------------------------------------------------------------------------
//
// Turns an existing body into a sensor (a trigger volume: detects overlap, pushes nothing, reports through
// the aver_phys_overlap_* queue) or back into a solid. Works on any body including static ones; the body
// moves between the moving and non-moving broad-phase halves as needed, placed exactly as
// aver_phys_add_sensor_* bodies are. Overlaps already in progress when it becomes a sensor are not
// reported as "entered".
AVER_PHYS_API int32_t aver_phys_body_set_sensor(int32_t body, int32_t on);
AVER_PHYS_API int32_t aver_phys_body_sensor(int32_t body, int32_t* outOn);

#ifdef __cplusplus
}
#endif
