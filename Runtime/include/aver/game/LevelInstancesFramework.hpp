#pragma once
// The framework half of LevelInstances' hooks, shared by the packaged game and the editor: spawn and destroy a class
// placement through the framework (aver_fw_*, so OnStart/OnEndPlay run), and relay a unit's residency to the event
// bus ("LevelInstance.Loaded" / "LevelInstance.Unloaded", sender = target = the instance root, arg0 = entity count).
// LevelInstances itself stays framework-free. docs/LEVEL_INSTANCES.md 2.5.3.
#if AVER_MODULE_SCENE && AVER_MODULE_FRAMEWORK

#include "aver/core/Log.hpp"
#include "aver/formats/OcWorld.hpp"
#include "aver/scene/Entity.hpp"
#include "aver/world/LevelTransform.hpp"

#if AVER_MODULE_FRAMEWORK
#  include "aver/framework/framework_abi.h"
#  include "aver/framework/framework_timers_abi.h"

namespace aver::game {

// The class a placement names, spawned at its world pose (aver_fw_spawn: BeginPlay runs at once). kInvalidEntity
// when the class is not declared or the spawn failed.
inline scene::Entity spawnInstanceClass(const fmt::OcWorldPlacement& p, const Transform& world) {
    const int32_t c = aver_fw_class_find(p.className.c_str());
    if (c == 0) {
        AVER_WARN("[LevelInstance] placement names class '{}', which is not declared -- skipped", p.className);
        return scene::kInvalidEntity;
    }
    const f32 pos3[3] = {world.position.x, world.position.y, world.position.z};
    const f32 quat4[4] = {world.rotation.x, world.rotation.y, world.rotation.z, world.rotation.w};
    const f32 scale3[3] = {world.scale.x, world.scale.y, world.scale.z};
    const int32_t e = aver_fw_spawn(c, p.className.c_str(), pos3, quat4, scale3);
    if (e == 0) {
        AVER_WARN("[LevelInstance] class '{}' failed to spawn at ({:.0f}, {:.0f}, {:.0f})", p.className, pos3[0], pos3[1], pos3[2]);
        return scene::kInvalidEntity;
    }
    return static_cast<scene::Entity>(e);
}

inline void destroyInstanceClass(scene::Entity e) { aver_fw_destroy(static_cast<int32_t>(e)); }

inline void relayInstanceResidency(scene::Entity root, bool loaded, u32 entityCount) {
    aver_fw_event_payload_clear();
    aver_fw_event_push_int(static_cast<int64_t>(entityCount));
    aver_fw_event_post(loaded ? "LevelInstance.Loaded" : "LevelInstance.Unloaded", static_cast<int32_t>(root),
                       static_cast<int32_t>(root));
}

} // namespace aver::game

#endif // AVER_MODULE_FRAMEWORK

#endif // AVER_MODULE_SCENE && AVER_MODULE_FRAMEWORK
