#pragma once
// CLevelInstanceLink -- the link from an entity back to the level instance it belongs to.
//
// Header-only and registered at RUNTIME (a copy of PrefabLink.hpp's route): no id in the fixed built-in table and
// no source in Aver.Scene's CMake. game::LevelInstances registers it; a world that never realises an instance has
// no such component. aver::save captures it by reflection, so a restored world still knows its instances and
// LevelInstances::rescan() rebuilds its table from the links (docs/LEVEL_INSTANCES.md 2.5.2).
#include "aver/core/Types.hpp"
#include "aver/scene/Entity.hpp"
#include "aver/scene/World.hpp"

#include <cstddef>

namespace aver::scene {

inline constexpr u32 kLinkEditing = 1u;   // the instance is being edited in place (members are ordinary entities)

struct CLevelInstanceLink {
    u64    assetId     = 0;              // fnv1a64 of the instance's asset path
    u64    placementId = 0;              // child placement id, 0 for the root, a light, a decal
    Entity root        = kInvalidEntity; // the instance root; a root links to itself
    u32    instanceId  = 0;              // run-time id, stable across delete + undo
    u32    depth       = 0;              // 0 = placed in the host level
    u32    flags       = 0;              // bit0 kLinkEditing
};

static_assert(sizeof(CLevelInstanceLink) == 32, "CLevelInstanceLink must be padding-free");

inline constexpr const char* kLevelInstanceLinkComponentName = "CLevelInstanceLink";

// Registers the component and its field table, idempotently. Returns its type id, 0 on failure.
inline u32 registerLevelInstanceLink(World& world) {
    if (const u32 existing = world.componentId(kLevelInstanceLinkComponentName)) return existing;
    ComponentBuilder b = world.registerComponent<CLevelInstanceLink>(kLevelInstanceLinkComponentName);
    b.field("assetId",     FieldKind::I64,    static_cast<u16>(offsetof(CLevelInstanceLink, assetId)))
     .field("placementId", FieldKind::I64,    static_cast<u16>(offsetof(CLevelInstanceLink, placementId)))
     .field("root",        FieldKind::Entity, static_cast<u16>(offsetof(CLevelInstanceLink, root)))
     .field("instanceId",  FieldKind::I32,    static_cast<u16>(offsetof(CLevelInstanceLink, instanceId)))
     .field("depth",       FieldKind::I32,    static_cast<u16>(offsetof(CLevelInstanceLink, depth)))
     .field("flags",       FieldKind::I32,    static_cast<u16>(offsetof(CLevelInstanceLink, flags)));
    return b.verify(sizeof(CLevelInstanceLink)) ? b.typeId() : 0;
}

} // namespace aver::scene
