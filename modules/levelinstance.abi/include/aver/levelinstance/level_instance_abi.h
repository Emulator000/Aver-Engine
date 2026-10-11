#ifndef AVER_LEVEL_INSTANCE_ABI_H
#define AVER_LEVEL_INSTANCE_ABI_H

/* Aver.LevelInstance.Abi -- the plain-C seam a game script places, streams and queries level instances through
 * (docs/LEVEL_INSTANCES.md).
 *
 * Entities cross as int32_t, exactly as in scene_abi.h. The DLL holds NO level-instance state: the host (the editor,
 * the runtime) owns the game::LevelInstances and installs function pointers with aver_levelinst_set_host, the same
 * shape aver_prefab_set_host has. Until a host is installed every call returns 0 and records NotInitialised.
 * Setters and queries return 0 for failure and a positive value (or the entity) for success. */

#include <stdint.h>

#if defined(_WIN32)
#  if defined(AVER_LEVELINST_ABI_BUILD)
#    define AVER_LEVELINST_API __declspec(dllexport)
#  else
#    define AVER_LEVELINST_API __declspec(dllimport)
#  endif
#else
#  define AVER_LEVELINST_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define AVER_LEVELINST_ABI_VERSION_MAJOR 1
#define AVER_LEVELINST_ABI_VERSION_MINOR 0
#define AVER_LEVELINST_ABI_VERSION \
    ((AVER_LEVELINST_ABI_VERSION_MAJOR << 16) | AVER_LEVELINST_ABI_VERSION_MINOR)

/* What the host provides. Every member may be NULL; the matching call then returns 0 with Unsupported. */
typedef struct AverLevelInstanceHost {
    void* user;
    /* Spawns a RUNTIME-ONLY instance of the level at `path` (content-relative .ocworld), parented to `parent`
     * (0 = none), at xyz centimetres, yaw/pitch/roll degrees and a uniform scale. It is not written to the level and
     * is destroyed with it. Returns the instance root, 0 on failure. */
    int32_t (*spawn)(void* user, const char* path, int32_t parent, const float* xyz,
                     const float* yawPitchRoll, float scale, int32_t packed);
    int32_t (*destroy)(void* user, int32_t root);                                /* 1 */
    int32_t (*root_of)(void* user, int32_t entity);                              /* 0 = not a member */
    int32_t (*find)(void* user, int32_t root, const char* idPath);               /* entity, 0 */
    int32_t (*set_loaded)(void* user, int32_t root, int32_t loaded);             /* 1 */
    int32_t (*is_loaded)(void* user, int32_t root);                              /* 1, 0, -1 bad root */
    int32_t (*reload)(void* user, int32_t root);                                 /* 1 */
    int32_t (*set_packed)(void* user, int32_t root, int32_t packed);             /* 1 = the effective state now equals the request */
    int32_t (*bounds)(void* user, int32_t root, float* minXyz, float* maxXyz);   /* 1 when known */
    int32_t (*count)(void* user);
    int32_t (*at)(void* user, int32_t index);                                    /* root, 0 */
    int32_t (*asset_id)(void* user, int32_t root, int64_t* outId);               /* fnv1a64 of the asset path */
} AverLevelInstanceHost;

/* The version this DLL was built with, so a caller can catch a stale binary. */
AVER_LEVELINST_API int32_t aver_levelinst_abi_version(void);

/* WHY the last call failed, as an aver::AbiError (modules/core/include/aver/core/ErrorCodes.hpp):
   0 ok, -1 bad handle, -2 null pointer, -3 not initialised (no host installed), -5 unsupported (the host lacks that
   function), -6 invalid argument (null or empty path, or a spawn the host refused).
   THREAD-LOCAL, per DLL, and SET ON SUCCESS TOO (to 0), like aver_prefab_last_error. */
AVER_LEVELINST_API int32_t aver_levelinst_last_error(void);

/* Installs (or, with NULL, removes) the host. The struct is copied. */
AVER_LEVELINST_API void aver_levelinst_set_host(const AverLevelInstanceHost* host);

AVER_LEVELINST_API int32_t aver_levelinst_spawn(const char* path, int32_t parent, float x, float y, float z,
                                                float yaw, float pitch, float roll, float scale, int32_t packed);
AVER_LEVELINST_API int32_t aver_levelinst_destroy(int32_t root);
/* The innermost instance root `entity` belongs to; 0 when it is not part of an instance. */
AVER_LEVELINST_API int32_t aver_levelinst_root_of(int32_t entity);
/* 1 when `entity` is part of a level instance, 0 otherwise. */
AVER_LEVELINST_API int32_t aver_levelinst_is_member(int32_t entity);
/* The entity at `idPath` inside `root`'s instance: a child placement id in hex ("c3"), or "b2/c9" through a nested
   instance. "" is the root itself. 0 if absent. */
AVER_LEVELINST_API int32_t aver_levelinst_find(int32_t root, const char* idPath);
AVER_LEVELINST_API int32_t aver_levelinst_set_loaded(int32_t root, int32_t loaded);
/* 1 loaded, 0 not loaded, -1 not an instance root. */
AVER_LEVELINST_API int32_t aver_levelinst_is_loaded(int32_t root);
AVER_LEVELINST_API int32_t aver_levelinst_reload(int32_t root);
AVER_LEVELINST_API int32_t aver_levelinst_set_packed(int32_t root, int32_t packed);
/* World AABB in centimetres; 1 when known, 0 otherwise (the out arrays are then untouched). */
AVER_LEVELINST_API int32_t aver_levelinst_bounds(int32_t root, float* minXyz, float* maxXyz);
AVER_LEVELINST_API int32_t aver_levelinst_count(void);
AVER_LEVELINST_API int32_t aver_levelinst_at(int32_t index);
/* fnv1a64 of the instance's asset path (the id CLevelInstanceLink stores); 1 on success. */
AVER_LEVELINST_API int32_t aver_levelinst_asset_id(int32_t root, int64_t* outId);

#ifdef __cplusplus
}
#endif

#endif /* AVER_LEVEL_INSTANCE_ABI_H */
