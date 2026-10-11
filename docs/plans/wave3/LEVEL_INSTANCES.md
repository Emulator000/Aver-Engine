# Wave 3, feature #12: Level Instances and Packed Level Actors (design contract)

Status: DESIGN, frozen for the implementing agent. 2026-10-11. Scope: a placed record that instances another `.ocworld` (transform, overrides), edited in place or opened as its own level, nested with cycle detection, streamed as a unit, with an opt-in "packed" mode that bakes the instance's static meshes into instance tables, working in the editor, the packaged game, physics, lights and actors, plus the C ABI, C# API and graph nodes. Not in scope: vehicles, landscape, water, post volumes, sequences or PCG inside an instance (section 2.3), per-component overrides, per-placement streaming inside an instance, merged-mesh packing (section 6).

## 0. Decisions in one screen

1. **An instance is a record in the host `.ocworld`: `LEVELINST`.** Same shape as `PREFABINST` (`OcPrefab.hpp:100`): asset reference, transform, overrides, written before the placements. The child is an ordinary `.ocworld`. There is no new asset type and no "instanceable" flag; any level can be instanced.
2. **Live link, not flatten.** An instance is realised as a root entity plus the child's entities parented under it, every one carrying a `CLevelInstanceLink`. The level save skips linked entities and writes the `LEVELINST` record instead, exactly as `prefabSys_.isLinked(e)` does for prefabs (`SandboxLevelEdit.cpp:415, 594, 639`). The packaged game uses the same code (`game::LevelInstances`, in `Aver.Runtime.Game.Core`, shared by both hosts, as `LevelStreaming` is).
3. **Naming trap.** `world::LevelInstance` (`LevelInstance.hpp:212`) is the RESULT of `world::instantiate()`. It has nothing to do with this feature and is not renamed. New C++ names: `fmt::OcLevelInstance` (the record), `world::LevelAssetLibrary`, `game::LevelInstances` (the realiser), `scene::CLevelInstanceLink`. The user-facing term stays "Level Instance".
4. **Overrides reuse the prefab override type.** `fmt::OcPrefabOverride` with the pseudo component `@Placement` (like `@Node`, `OcPrefab.hpp:50`) and the child placement's stable `id=` as the path. One parser and writer for both.
5. **The streaming unit is the instance.** `world::PlacementStreamer` is reused unchanged over instance world AABBs. No second residency policy.
6. **Packed is opt-in per instance.** Bake output is an `.ocinst` (child-local rows) plus an `.ocpack` sidecar under `Binaries/LevelPacks/`. It renders through the existing `VoxiRenderer::setFoliage` path, so it inherits the foliage contract: ray-driven primary visibility only, no voxel-GI injection. When the renderer cannot show foliage, or the pack is stale, or the instance has overrides, the instance realises unpacked. Nothing disappears.
7. **Prerequisite found by reading the code: the packaged game spawns no `LIGHT` records and feeds no scene lights.** Only the editor does (`SandboxLevelLoad.cpp:386-394`, `SceneLightFeed` in `sandbox/src`). "Lights inside an instance work in the packaged game" is false for every level today. Step P1 (section 2.9) moves the loader and the feed to Runtime. It fixes plain levels too.
8. **Edit in place edits the child asset.** `Edit Instance` realises the raw asset (overrides set aside), the author edits ordinary entities, `Commit` writes the child `.ocworld` and rebuilds every other instance of it. `Open as Level` is the existing `requestOpenLevel` plus a return breadcrumb.
9. **Child placements need stable ids.** Overrides are keyed by them (section 2.2.3). Non-streamed editor saves drop `id=` today; they will keep it.

## 1. What exists today, and what is missing

| Need | Exists | Missing |
|---|---|---|
| A record that places another asset | `PREFABINST` + `POVERRIDE` (`OcPrefab.cpp:349-405`), `OcWorldData::prefabInstances` (`OcWorld.hpp:745`), parse hook `OcWorld.cpp:292`, writer hook `OcWorld.cpp:1254`. Unknown records are skipped (`OcWorld.cpp:231`) so an older build loses them, never misplaces | A `.ocworld`-instancing record |
| Override value type, text syntax | `OcPrefabOverride` (`OcPrefab.hpp:60`), `parseOverride`/`appendOverrideBody` (`OcPrefab.cpp:110, 130`, file-local) | Export of those two helpers |
| Hierarchy inside a level | `BEGIN/END`, `CHILD`; `OcWorldPlacement::parent` (`OcWorld.hpp`, flat vector + index), `instantiate()` creates parents first (`LevelInstance.cpp:167-300`) | A way to hang a whole world under one entity and compose world transforms for bodies |
| Stable placement identity | `id=<16 hex>` token, `OcWorldPlacement::placementId` (`OcWorld.hpp:120`), `assignPlacementIds` (`OcStream.hpp:58`) | Persistence of ids across a non-streamed editor save: `fillPlacementFromEntity` (`SandboxLevelEdit.cpp:408-462`) never writes one |
| Level bounds | `GameLevel::placementBounds` computes them per load (`GameLevel.cpp:651`); per-placement `aabb=` for streamed levels | A bounds record on the level file itself that another level can read without loading meshes |
| Residency | `world::PlacementStreamer` (generic over AABBs, `PlacementStreamer.hpp:23-46`: build/add/pin/update/markLoaded), `game::LevelStreaming` for placements (`LevelStreaming.hpp`) | Streaming for anything but placements and foliage cells |
| Instance tables | `.ocinst` (`OcInstances.hpp`: groups + 12-float row-vector rows, `cellOcInstances`), loaded by `game::LevelFoliage` into `VoxiRenderer::setFoliage` (`VoxiRenderer.hpp:196-208`; "TRACED but never rasterised/voxelised/path-traced"), `ActorPreview::setInstanceBatches` previews tables (`ActorPreview.hpp:132`), `InstanceSetEditor` edits them | Tables placed with a transform, per-group material, collision for rows |
| Instanced draws | Only the shadow and GI-shadow passes group by mesh (`VoxiRenderer.cpp:4876-5042`, `drawMeshInstanced`). The scene pass is one draw per entity, capped at `kMaxDraws` | No instanced raster scene path (not built here, section 6) |
| Prefabs | `Aver.Prefab`: `CPrefabLink` (`PrefabLink.hpp`), `PrefabSystem` (instantiate/destroy/overrides diff/apply/revert/undo state), editor model/UI, ABI `prefab_abi.h`, C# `Aver.Prefab`, `GraphPrefabs`, 5 graph nodes | Everything is prefab-asset based; none of it can be reused for an `.ocworld` because a world also carries static bodies, classes, lights, decals, foliage, animation |
| Editor level bookkeeping | `levelEntities_`, `entityLabels_`, `entityBodies_`, `entityCollide_` (`SandboxSelection.cpp:521-557` shows the prefab hook shape), `requestOpenLevel` (`SandboxLevelEdit.cpp:300`), content drop (`SandboxViewport.cpp:1106` for `.ocprefab`), pick (`SandboxViewport.cpp:2402`), `entityWorldBounds` (`SandboxLevelStream.cpp:264`) | Instance-aware equivalents |
| Class placements | `OcWorldPlacement::className`, spawned late by `GameLevel::spawnClassPlacements` (`GameLevel.cpp:505`) because scripting declares classes after the level loads | The same two-step for classes inside an instance |
| Physics | `world::instantiate` makes static mesh/box bodies with a shared mesh-shape cache (`LevelInstance.cpp:340-393`) | That body branch is inline; packed rows and attached placements need it as a function |
| Lights, decals at runtime | Decals: `game::spawnLevelDecals` + `SceneDecalFeed` (Runtime, `GameApp.cpp:1003, 2145`). Lights: editor only | Lights in the packaged game (P1) |
| Packaging | `stage-game.ps1:391-394` copies `Binaries/` recursively; `Content/` is copied whole | Nothing for nested `.ocworld` (they sit in Content). Pack data under Binaries is staged already |

## 2. Design

### 2.1 Where each piece lives

| Piece | Where | Why |
|---|---|---|
| `LEVELINST`, `EXTENT`, `.ocpack`, row transform helper | `Aver.Formats` (new `OcLevelInst.*`, `OcPack.*`) | Text/binary formats; no scene |
| `LevelAssetLibrary` (parse cache, cycle and depth check, ids, closure hash, override resolution, instance bounds), `LevelPack` (bake) | `Aver.World` (new files; pure, compile in a scene-less tree) | World already owns "level to entities" and is required by both hosts |
| `InstantiateOptions::attach`, `addStaticPlacementBody` extraction | `Aver.World` `LevelInstance.*` | Bodies must be composed in world space |
| `CLevelInstanceLink` | `Aver.Scene` header-only, registered at runtime (copy of `PrefabLink.hpp`) | No built-in id, no Scene CMake edit |
| `game::LevelInstances` (realise, unrealise, stream, edit, overrides, pack feed), `LevelLights`, `SceneLightFeed` | `Aver.Runtime.Game.Core` (`Runtime/`) | Shared by editor and game, like `LevelStreaming` and `LevelDecals` |
| C ABI | new module `Aver.LevelInstance.Abi` (`modules/levelinstance.abi`), stateless host-pointer DLL | Same shape as `Aver.Prefab.Abi` |
| C# API + graph seam | new `scripting/csharp/Aver.LevelInstance` (leaf project, copy of `Aver.Prefab`) | Same wiring route as prefabs |
| Editor | `sandbox/src/LevelInstanceEditorModel.*` (no UI, testable) and `LevelInstanceEditorUi.*` | Same split as `PrefabEditorModel/Ui` |

`game::LevelInstances` depends on no framework, prefab, GPU or GameContent type. Everything host-specific arrives through the `Hooks` and `ContentPort` structs below, so `LevelInstancesTest` runs with a fake host.

### 2.2 Formats

#### 2.2.1 `LEVELINST` (in `.ocworld`, new `OcLevelInst.hpp`)

```
LEVELINST Levels/Room.ocworld pos 1200 0 0 rot 90 0 0 scale 1 1 1 name Room%20A id=00000000000000a1 packed stream 1
  LIOVERRIDE set 00000000000000c3 @Placement material string M_Red
  LIOVERRIDE set 00000000000000c4 @Placement removed bool 1
  LIOVERRIDE set 00000000000000b2/00000000000000c9 @Placement position vec3 0 0 40
ENDLEVELINST
EXTENT -500 -500 0 500 500 400
```

```cpp
struct OcLevelInstance {
    std::string asset;                 // content-relative .ocworld, forward slashes, pctEncode'd on disk
    std::string name;                  // outliner label; empty = the asset's file stem
    f64 x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0, sx = 1, sy = 1, sz = 1;
    u64 id = 0;                        // instance id, hex `id=`; 0 = unassigned (the editor assigns)
    bool packed = false;               // bare token `packed`
    i32 stream = -1;                   // `stream 0|1`; -1 = follow the host level (absent on disk)
    std::vector<OcPrefabOverride> overrides;
};
// OcWorldData gains (delimited block): std::vector<OcLevelInstance> levelInstances;
//   bool hasExtent = false; f64 extentMin[3] = {0,0,0}, extentMax[3] = {0,0,0};
bool parseOcLevelInstanceLine(const std::vector<std::string_view>& tokens,
                              std::vector<OcLevelInstance>& out, i32& open);   // LEVELINST, LIOVERRIDE, ENDLEVELINST
void appendOcLevelInstances(std::string& out, const std::vector<OcLevelInstance>& v);   // nothing when empty
```

- Parse hook: one line in `parseOcworld` beside `parseOcPrefabInstanceLine(...)` (`OcWorld.cpp:292`). Writer: one line beside `appendOcPrefabInstances` (`OcWorld.cpp:1254`), after it. A level with no instances and no extent writes no new bytes.
- Tokens after the asset are keyword/value like `PREFABINST`: `pos x y z`, `rot yaw pitch roll`, `scale sx sy sz`, `name <pctEncode>`, `id=<hex>`, bare `packed`, `stream 0|1`. Unknown tokens are skipped. Numbers are written with the shortest round-trip form (`numAuto`, `OcPrefab.cpp:29`).
- `LIOVERRIDE` is `POVERRIDE` with another keyword; only the `set` op is meaningful (`addcomp`/`rmcomp` parse and are ignored with a warning, reserved for per-component overrides). `OcPrefab.cpp` exports `parseOverride` and `appendOverrideBody` as `fmt::parseOcOverrideBody(t, from, ov)` and `fmt::appendOcOverrideBody(s, ov)` (declared in `OcPrefab.hpp`, a delimited addition).
- **Override addressing.** `path` is the child placement's `id=` as 16 hex digits (leading zeros optional on read, always written). A placement inside a nested instance is `<nested instance id>/<placement id>`; join/split with `ocPrefabJoinPath/ocPrefabSplitPath` (`OcPrefab.hpp`). `component` is always `@Placement`. Fields (`value.name`, kind):

| Field | Kind | Meaning |
|---|---|---|
| `position` | vec3 | placement position, cm, in the child's own frame (replaces, not a delta) |
| `rotation` | vec3 | yaw pitch roll degrees, same order as `rot` |
| `scale` | vec3 | scale |
| `material` | string | surface name; empty string = the mesh's own |
| `visible` | bool | the `hidden` token's inverse |
| `collide` | bool | the `nocollide` token's inverse |
| `removed` | bool | 1 drops the placement and its children |
| `name` | string | outliner label |

- Old build: `LEVELINST/LIOVERRIDE/ENDLEVELINST/EXTENT` are unknown records and skipped; the instance vanishes visibly, nothing is misplaced. An old build that then saves drops them (same class as `PREFABINST`; the `OcWorld.cpp` comment above `parseOcworld`, lines 231-239, states the rule). The header stays `OCWORLD 1`.
- `docs/formats/FORMAT_SPECS.md`: append a paragraph to section 11 (`LEVELINST`, `EXTENT`) and a new section `11e. .ocpack`. Append only; the landscape feature also appends to section 11, so each addition is a delimited block.

#### 2.2.2 `EXTENT`

`EXTENT minx miny minz maxx maxy maxz` (cm, the level's own frame): the union of its placements' world AABBs. Written by the editor on every save (`entityWorldBounds`, `SandboxLevelStream.cpp:264`, unioned over `levelEntities_`; for a streamed level the union of the placement `aabb=`s). Absent = unknown. `LevelAssetLibrary::instanceBounds` derives an instance's world AABB from it (eight corners through the instance TRS); without it a child's extent is the union of its nested instances' bounds, else unknown, and an instance with unknown bounds is always loaded (the rule `LevelStreaming` already applies to a placement with no bounds, `LEVEL_STREAMING.md` section 1).

#### 2.2.3 Ids

- `OcWorldPlacement::placementId` is the key. `OcLevelInstance::id` is the key of a nested instance.
- **Keep ids on save.** The editor stores a side map `entityPlaceId_` (entity to id), filled in `onLevelInstantiated` from `p.placementId`, and `fillPlacementFromEntity` writes it back. **Rule: if any placement of a level carries an id, `saveLevel` assigns an id to every placement that has none** (`fmt::assignPlacementIds`). A level nobody ever instanced and that never had an id round-trips byte for byte.
- **First use creates ids.** Placing a level as an instance, or authoring an override, calls `LevelAssetLibrary::ensureIds(path)`: it assigns ids to the asset's placements and nested instances in memory, and if any were missing rewrites the child file once (temp file + rename; refused with a clear message while that level is the open, dirty one in the editor: "save it first").
- In memory only, `LevelAssetLibrary::get` fills any still-missing id with `fnv1a64(asset + "#" + index in file order)` so a never-saved child still instantiates. Overrides cannot target such a placement (the editor does not offer it).

### 2.3 What an instance takes from its child

| Child record | In an instance |
|---|---|
| `PLACE/PLACEG/CHILD` mesh placements | Entities under the instance root; static bodies (mesh shape or box, shared shape cache); `hidden`, `nocollide`, object animation clip plus kinematic body (aggregated into `animatedBodies()`) |
| Class placements (`class <name>`) | Spawned by `Hooks::spawnClass` in the late class pass (2.5.4) |
| `vehicle <preset>` | Not driven: realised as a plain mesh, one warning per instance |
| `snap` | Ignored, one warning (terrain height is a host-level quantity) |
| `LIGHT`, `DECAL` | Entities with `CLight`/`CDecal` under the root (P1 makes `LIGHT` work in the game) |
| `PREFABINST` | `Hooks::spawnPrefab` with the composed local transform, parent = instance root |
| `FOLIAGE` tables | Extra tables with the instance matrix applied (2.5.5) |
| `LEVELINST` | Nested instance, recursively (depth at most 8, cycles refused) |
| `SUN SKY FOG CLOUDS GAMEMODE DEFAULTPAWN SPAWN CAMERA STREAM NAME ID BUILD ALGO` | Ignored: the host level's own. One log line per instance names what was ignored |
| `LANDSCAPE WATER WAVE PCGVOLUME SCATTER POSTVOLUME SEQUENCE` | Ignored in v1, counted in the same log line. Terrain and water belong to feature #13; its instancing seam would be a new row here, not a new system |

### 2.4 `world::LevelAssetLibrary` (new `modules/world/include/aver/world/LevelAssetLibrary.hpp`, `src/LevelAssetLibrary.cpp`)

Pure: Core + Formats only, compiles in a scene-less tree.

```cpp
namespace aver::world {
struct LevelAsset {
    std::string key;                       // content-relative path as authored
    fmt::OcWorldData data;                 // ids filled in memory (2.2.3)
    u64 contentHash = 0;                   // FNV-1a over the instanced records: placements, lights, decals, prefabs, foliage names, nested records
    std::vector<std::string> childAssets;  // distinct LEVELINST assets, file order
};
struct ResolvedContents {                  // one instance's child with its overrides applied, in the instance's own frame
    fmt::OcWorldData world;                // placements/lights/decals/prefabs/foliage/levelInstances only (2.3); removed placements and their subtrees dropped
    std::vector<u64> placementIds;         // parallel to world.placements
    u32 applied = 0, unmatched = 0;        // unmatched overrides are counted, never fatal
};
class LevelAssetLibrary {
public:
    static constexpr u32 kMaxDepth = 8;                         // PrefabLibrary::resolve's cap
    using Loader = std::function<bool(const std::string& path, fmt::OcWorldData& out, std::string* why)>;
    void setLoader(Loader l);                                   // host: content index -> loadOcworld
    const LevelAsset* get(const std::string& path, std::string* why = nullptr);   // parses once
    void put(const std::string& path, fmt::OcWorldData data);   // a commit or an external reload
    void invalidate(const std::string& path);
    bool ensureIds(const std::string& path, const std::string& absoluteFile, std::string* why);   // 2.2.3
    // Depth-first over LEVELINST. False with `chain` = "A -> B -> A" on a cycle, or "depth > 8".
    bool closure(const std::string& root, std::vector<std::string>& order, std::string* chain);
    // True when instancing `child` inside `container` would create a cycle (child is, or transitively contains, container).
    bool wouldCycle(const std::string& container, const std::string& child, std::string* chain = nullptr);
    u64 closureHash(const std::string& root);                   // asset hash combined with every nested asset's; pack staleness key
    std::vector<std::string> usersOf(const std::string& path) const;   // cached assets that directly instance it
    bool resolve(const fmt::OcLevelInstance& rec, ResolvedContents& out, std::string* why = nullptr);
    // World AABB of the instance (EXTENT through the TRS; nested fallback). False = unknown.
    bool instanceBounds(const fmt::OcLevelInstance& rec, AABB& out);
};
}
```

- Keys compare case-folded with `\` mapped to `/`; the stored key keeps the authored spelling (ObjectIds hash the authored forward-slash path, `PACKAGING.md:114`).
- `resolve` applies overrides to a copy: `removed` drops the placement and, transitively, its children (renumbering `parent`); `position/rotation/scale/material/visible/collide/name` overwrite the fields; an override whose path has a `/` is moved onto the nested `LEVELINST` record it names (its `overrides` list gains the path with the head stripped), so nesting needs no special case later. `resolve` never recurses into nested assets; the realiser does.

### 2.5 Realisation

#### 2.5.1 `world::instantiate` additions (`LevelInstance.hpp:120`, `LevelInstance.cpp:167`)

```cpp
// InstantiateOptions (delimited addition)
struct Attach { scene::Entity parent = scene::kInvalidEntity; Transform parentWorld; };
const Attach* attach = nullptr;   // roots (parent < 0) are created under attach->parent; worldXf of roots starts at parentWorld
// LevelInstance (the result struct) gains: nothing.
```

In the loop, a root placement (`!isChild`) uses `attach->parent` as `parentEnt` and composes `worldXf[i]` from `attach->parentWorld` exactly as the existing child branch does (`LevelInstance.cpp:279-289`). `snapToGround` on a root is ignored when `attach` is set (warn once). With `attach == nullptr` nothing changes (bit-exact; the render gates must stay identical).

Extract the body branch (`LevelInstance.cpp:340-393`: triangle shape via the shared cache, else box, set entity) into

```cpp
#if AVER_MODULE_PHYSICS
// 0 when nothing was made. `moving`/kinematic handling stays in instantiate(); this is the static path.
i32 addStaticPlacementBody(const InstantiateOptions& opt, std::unordered_map<u64, i32>& shapeCache,
                           u64 meshId, const Transform& worldXf, u32* outTriangles, bool* outUsedMesh);
#endif
```

`instantiate` calls it in the same order with the same arguments (a behaviour-preserving refactor); `game::LevelInstances` calls it for packed rows.

#### 2.5.2 `scene::CLevelInstanceLink` (`modules/scene/include/aver/scene/LevelInstanceLink.hpp`, header-only)

```cpp
struct CLevelInstanceLink {
    u64    assetId     = 0;              // fnv1a64 of the instance's asset path
    u64    placementId = 0;              // child placement id, 0 for the root, a light, a decal
    Entity root        = kInvalidEntity; // the instance root; a root links to itself
    u32    instanceId  = 0;              // run-time id, stable across delete + undo
    u32    depth       = 0;              // 0 = placed in the host level
    u32    flags       = 0;              // bit0 kLinkEditing
};
static_assert(sizeof(CLevelInstanceLink) == 32, "padding-free");
inline constexpr const char* kLevelInstanceLinkComponentName = "CLevelInstanceLink";
inline u32 registerLevelInstanceLink(World& world);   // idempotent, field table as CPrefabLink
```

`aver::save` captures it by reflection, so a restored world still knows its instances; `LevelInstances::rescan()` rebuilds its table from the links (2.10).

#### 2.5.3 `game::LevelInstances` (`Runtime/include/aver/game/LevelInstances.hpp`, `Runtime/src/LevelInstances.cpp`)

```cpp
namespace aver::game {
struct ContentPort {                     // GameContent / the editor's mesh cache, behind std::function so tests fake it
    std::function<bool(u64 mesh)> acquire;  std::function<void(u64 mesh)> release;
    std::function<void(u64 mesh)> prefetch; std::function<bool(u64 mesh)> ready;
};
class LevelInstances {
public:
    struct Hooks {
        std::function<void(scene::Entity, i32 body, const fmt::OcWorldPlacement*)> created;   // editor bookkeeping; null placement for a light/decal/prefab root
        std::function<void(scene::Entity)> destroying;
        std::function<scene::Entity(const fmt::OcPrefabInstance&, const Transform& local, scene::Entity parent)> spawnPrefab;
        std::function<scene::Entity(const fmt::OcWorldPlacement&, const Transform& world, scene::Entity parent)> spawnClass;
        std::function<void(scene::Entity)> destroyClass;                 // the aver_fw_destroy path, so OnEndPlay runs
        std::function<bool(scene::Entity, fmt::OcWorldData& into, i32 parentIndex)> captureEntity;   // editor: placement/light/decal record from a live entity (commit)
        std::function<std::vector<fmt::OcPrefabInstance>(scene::Entity root)> capturePrefabs;
    };
    struct PackedSink {                  // game::LevelFoliage in the hosts; a fake in tests
        std::function<void(u64 owner, fmt::OcInstanceData rowsInWorldSpace, std::vector<std::string> groupMaterial)> set;
        std::function<void(u64 owner)> clear;
    };

    // Residency callback: (root, loaded, entity count). The host relays it to aver_fw_event_post (2.5.3 step 11).
    std::function<void(scene::Entity, bool, u32)> onResidency;
    struct LevelInstanceState;           // value type, mirrors prefab::PrefabState: instance records + parent + local transform, by instanceId
    void init(world::LevelAssetLibrary& lib, scene::World& w);
    // Realises every LEVELINST of `level` (eager, or as streamed units, 2.6). `opt` is what a full load passes to
    // world::instantiate (materials, bounds, triangles, meshShapes); progress is ignored.
    void begin(const fmt::OcWorldData& level, const ContentPort&, const world::InstantiateOptions& opt,
               const world::PlacementStreamSettings* levelStream, Hooks, PackedSink);
    void end();                                                  // unrealise everything, release meshes and bodies
    void tick(const std::vector<Vec3>& viewers);                 // streaming, deferred rebuilds, once a frame
    void spawnPendingClasses();                                  // the late class pass (GameLevel::spawnClassPlacements calls it)
    void rescan();                                               // rebuild the table from CLevelInstanceLink after a save restore

    // ---- run time / script ----
    scene::Entity spawn(const fmt::OcLevelInstance&, scene::Entity parent = scene::kInvalidEntity, bool runtimeOnly = true);
    bool destroy(scene::Entity root);
    scene::Entity rootOf(scene::Entity e) const;                 // kInvalidEntity when not a member
    bool isMember(scene::Entity e) const;
    scene::Entity find(scene::Entity root, std::string_view idPath) const;
    bool setLoaded(scene::Entity root, bool loaded);             // manual residency; pins while loaded by hand
    bool loaded(scene::Entity root) const;
    bool reload(scene::Entity root);                             // re-realise from the library, overrides kept
    bool reloadAsset(const std::string& path);                   // every instance of it, nested ones too
    bool setPacked(scene::Entity root, bool packed);
    bool bounds(scene::Entity root, AABB& out) const;
    u32 count() const;  scene::Entity at(u32 i) const;
    // ---- editor ----
    bool beginEdit(scene::Entity root);  bool commitEdit(scene::Entity root, std::string* why);  bool discardEdit(scene::Entity root);
    bool editing(scene::Entity root) const;  bool anyDirtyEdit() const;
    bool computeOverrides(scene::Entity root, std::vector<fmt::OcPrefabOverride>& out) const;   // live fields vs the child's records
    bool setOverrides(scene::Entity root, const std::vector<fmt::OcPrefabOverride>& ov);
    bool revertOverride(scene::Entity root, const fmt::OcPrefabOverride& which);
    bool revertAll(scene::Entity root);
    std::vector<fmt::OcLevelInstance> captureRecords(const std::function<bool(scene::Entity)>& include = {}) const;   // saveLevel
    scene::Entity createFromRecord(const fmt::OcLevelInstance&, scene::Entity parent, std::string* why);
    bool breakInstance(scene::Entity root);                      // members become ordinary entities, reparented to root's parent at their world pose
    LevelInstanceState captureState(const std::vector<u32>& instanceIds);   // undo
    bool restoreState(const LevelInstanceState&);
    void pin(scene::Entity root, bool on);
    const std::vector<world::AnimatedBody>& animatedBodies() const;
    bool extentBounds(Vec3& lo, Vec3& hi) const;                 // union of known instance bounds, for GameLevel::placementBounds
    void setPackedRenderOk(bool ok);                             // host: rtRenderMode is ray-driven primary (2.8.4)
    struct Stats { u32 instances, loaded, entities, bodies, packedRows; };  Stats stats() const;
};
}
```

Per-instance state: `{u32 id; fmt::OcLevelInstance rec; LevelAsset* asset; scene::Entity root, containerRoot; u32 depth; bool loaded, packedActive, editing, runtimeOnly, pinned; Transform world; AABB bounds; bool hasBounds; u32 streamItem; members[] {entity, placementId, body, collide}; bodies[]; classEntities[]; meshes[]; packedBodies[]; std::vector<u32> childIds}`.

**Realise** (one instance, `realise(inst, stack)`):
1. `stack` holds the case-folded asset keys of the containers. If the asset is in it, or depth > 8: `AVER_ERROR("[LevelInstance] cycle A -> B -> A: instance skipped")`, no entity beyond the (empty) root is made, siblings are unaffected.
2. `lib.resolve(rec)`. Create the root: `world.create(name, containerRoot_or_parent, rec transform)` + `CLevelInstanceLink` (flags 0). The root exists for the whole life of the instance, loaded or not, so script handles stay valid across streaming.
3. Packed? (2.8.4) If active, the `PackedSink`/body path below, minus the placements the pack carries.
4. `world::instantiate(resolved.world, opt + attach{root, instance world transform})`. Per returned entity: add `CLevelInstanceLink{assetId, placementIds[k], root, id, depth}`, record the member, call `Hooks::created(e, body, &placement)`. Animated bodies are appended to `animatedBodies()`.
5. Class placements (`className` non-empty): remembered as pending; spawned by `spawnPendingClasses()` once scripting is up, or immediately if it already is (`Hooks::spawnClass` set and `aver_fw_class_find` succeeds, decided by the hook). Their composed world transform comes from the resolved placement chain. Destroyed with `Hooks::destroyClass`.
6. Lights and decals: `game::spawnLevelLights` / `spawnLevelDecals` (the latter exists, `LevelDecals.hpp:60`), then reparented under the root with `setParent(e, root, keepWorld=false)` after being created at local pose (records are in the child's frame, which is the root's local frame).
7. `PREFABINST`: `Hooks::spawnPrefab(rec, local, root)`.
8. Child `FOLIAGE` tables: loaded from the content dir, rows multiplied by the instance world matrix (`fmt::transformOcInstanceRows`, 2.2), owner key `instanceId`, pushed through `PackedSink::set` (cell-less, so resident while the instance is).
9. Nested `LEVELINST` records of the resolved world: `realise` recursively with `containerRoot = root`, `depth + 1`, `stack + key`; the nested world transform is `instance world * record local`.
10. Mesh acquisition: every mesh id in the resolved placements through `ContentPort::acquire` (released at unrealise).
11. Call `onResidency(root, true, entityCount)`. GameApp and the editor relay it to `aver_fw_event_post("LevelInstance.Loaded" or "LevelInstance.Unloaded", root, root)` with the count as arg0 (`framework_timers_abi.h`: push_int then post); `LevelInstances` itself stays framework-free.

**Unrealise** (streaming eviction, `setLoaded(false)`, destroy, `end`): `Hooks::destroying` for every member, `destroyClass` for classes, remove bodies (`aver_phys_remove_body`), `PackedSink::clear(owner)`, release meshes, destroy the member entities (depth first; the root stays unless the instance itself is destroyed), nested instances unrealised first. Not during an edit (2.7).

**Overrides on the diff, not on the entity.** As with prefabs, overrides are not stored on entities. `computeOverrides(root)` compares each member with the baseline the child record would produce (record -> expected `CLocal`, material name, visible bit, collide flag from the member table, name) and emits `@Placement` overrides for the difference, plus `removed` for a baseline placement with no live member. Members created in the editor under an instance are not overrides (v1: they are not saved; the editor refuses to create entities under a locked instance, 2.7).

#### 2.5.4 `GameLevel` and host wiring

`GameLevel.hpp/.cpp` (owned by this feature; delimited hunks marked `// [wave3/level-instances]`):
- member `LevelInstances instances_; world::LevelAssetLibrary assets_;` and `LevelInstances& instances()`, `LevelAssetLibrary& assets()`.
- `load()`: after `world::instantiate` / `streaming_.begin` (`GameLevel.cpp:252-253`): `instances_.begin(w, port, opt, w.stream.enabled ? &streamSettings : nullptr, instanceHooks_, packedSink_)`; `setInstanceHooks(Hooks)` / `setPackedSink` mirror `setStreamHooks`.
- `unload()` (`:680`): `instances_.end()` before the level entities are destroyed.
- `tickStreaming()` (`:672`): `instances_.tick(viewers)` after `streaming_.tick`.
- `spawnClassPlacements()` (`:505`): ends with `instances_.spawnPendingClasses()`.
- `animatedBodies()`: returns `streaming_.animatedBodies()` / `animatedBodies_` plus `instances_.animatedBodies()` through a merged vector rebuilt when either changes (the hosts pass it to `world::driveKinematicBodies`, `GameApp.cpp:2044`).
- `entityCount()` adds `instances_.stats().entities`; `placementBounds()` (`:651`) unions `instances_.extentBounds()`, so the GI volume auto-fit (`GameApp.cpp` `fitGiVolumeToLevel`) covers instances.
- `LoadedLevel` gains nothing; the editor learns about instance entities through `Hooks::created`.

`GameApp.cpp` (`installLevelHooks`, `GameApp.cpp:965-1030`, and `onInit` near `:1806`): build `ContentPort` from `content_`, install `Hooks` (`spawnPrefab` -> `prefabSys_->instantiate(path, local, parent, overrides, name)`, `spawnClass` -> `aver_fw_class_find/aver_fw_spawn` as `GameLevel::spawnClassPlacements` does, `destroyClass` -> `aver_fw_destroy`), `PackedSink` -> `levelFoliage_` (2.5.5), `onResidency` -> `aver_fw_event_post`, `setPackedRenderOk(voxiAttached_ && settings.rtRenderMode != 0)` every frame it can change (the implementer confirms which `rtRenderMode` value means ray-driven primary; `ProjectRenderApply.hpp:103`), `aver_levelinst_set_host(&levelInstHost_)`. The same in `SandboxApp` (`installLevelStreamHooks` area, `SandboxLevelLoad.cpp`), with the editor hooks of 2.7.

#### 2.5.5 Foliage and packed tables (`GameFoliage.hpp/.cpp`, delimited additions)

```cpp
// LevelFoliage
void setExtraTable(u64 owner, fmt::OcInstanceData rowsInWorldSpace, std::vector<std::string> groupMaterial);
void clearExtraTable(u64 owner);
```

`rebuild()` (`GameFoliage.cpp:318`) emits extra tables after the cell files, as cell-less files (always resident), sharing the `kMaxFoliageInstances` budget (over-budget rows are dropped with the existing warning). A non-empty `groupMaterial[g]` replaces the prototype's part materials: token `aver_scene_material(0, name)`, bound with `ensureFoliageMaterialBound`, looked up with `resolveFoliagePartLook` (`GameFoliage.cpp:46, 65`); a translucent override drops the group's parts as today (`droppedParts`). Prototype meshes of extra tables are acquired and released like any other group (`File.use/held`). `clear()` drops extras. Setting or clearing an extra table schedules a rebuild at the existing `kCheckSeconds` throttle (0.25 s).

### 2.6 Streaming interplay

- **Unit = instance.** `LevelInstances::begin` builds a `world::PlacementStreamer` (`build(bounds, settings)`) over the world AABBs of the top-level instances that `streams` (below). Items with unknown bounds are not given to the streamer: they stay loaded. `tick` calls `update(viewers, toLoad, toEvict)`, realises and unrealises units, `markLoaded/markEvicted`, exactly as `LevelStreaming::tick` does for placements (`LevelStreaming.hpp`; `LEVEL_STREAMING.md` sections 2 and 6).
- **Which instances stream.** `rec.stream == 1`, or `rec.stream == -1` and the host level has `STREAM` enabled with a device (the condition `GameLevel.cpp:130` uses). `stream 0` is always resident. Settings are the level's `STREAM` cell/load/evict when it has them, else `PlacementStreamSettings{}` defaults.
- **Nested instances** load and unload with their top-level unit and never stream on their own; the child's own `STREAM` record is ignored.
- **Budget.** A unit realises atomically, so a hitch is bounded by unit size. To keep it small: (a) mesh prefetch: for units within 1.3 x `loadCm` the unit's meshes are requested with `ContentPort::prefetch` (the 1.3 factor of `LevelFoliage::kPrefetchFactor`), and a unit is loaded only when `ready()` is true for all of them (pinned units and units with no device load at once); (b) per tick one unit within 12 ms near / 3 ms far, evictions 2 ms (the `LevelStreaming` constants, `LevelStreaming.hpp`). The guideline for authors is a unit of at most a few thousand placements; the Details panel shows the child's placement count and warns above 5000.
- **Pinned** (never evicted): an instance being edited, selected in the editor, with a dirty override, or loaded by hand with `setLoaded(root, true)`. `pin(root, bool)`.
- **Editor.** The editor streams instances only when the level streams (same rule). Before a unit is evicted its overrides are computed (`computeOverrides`) and stored on the instance record, so the unit reloads with them; nothing else on a member is state that cannot be derived from the child asset.
- Foliage: instance foliage and packed tables live and die with the unit (`PackedSink::clear` on evict).
- `hashPlacements`, `.ocstream` and `bakeOcStream` are unaffected: instances are not placements.

### 2.7 Authoring workflows (editor behaviour contract)

The editor logic lives in `LevelInstanceEditorModel` (no UI) so it is testable; the UI file only draws.

1. **Place.** Content Browser drag of a `.ocworld` onto the viewport: new branch in `spawnFromAssetDrop` (`SandboxViewport.cpp:1106` is the `.ocprefab` precedent) -> `model.place(asset, xf)`. Refuses (status text, no entity) when `lib.wouldCycle(openLevelAsset, asset)` or the asset does not parse; calls `ensureIds`; assigns the instance `id`; creates the instance with `createFromRecord`. Undo entry `EditCmd::Kind::LevelInstance` carrying a `LevelInstanceState` before/after (the `Kind::Prefab` shape, `SandboxSelection.cpp:1256, 1311`).
2. **Locked by default.** A member is not individually pickable: the pick result at `SandboxViewport.cpp:2402` is redirected through `model.pickTarget(entity, altHeld)`: root of the instance for a plain click; the member itself for Alt+click (override mode) or when its instance is being edited. Gizmos, Details and delete work on the picked entity as usual. In override mode an edit to a member is an override (computed by diff at save, undo and Details time); deleting a member becomes `removed`. Creating, reparenting or deleting structure under a locked instance is refused with a status line.
3. **Edit in place.** Outliner/Details `Edit Instance` -> `beginEdit(root)`: the instance is rebuilt from the raw asset (current overrides set aside on the record), flag `kLinkEditing` on the members, pinned, the root row shows `[editing]`. Only one instance is edited at a time. Members are ordinary editable entities. `Commit` -> `commitEdit`: the editor's `captureEntity` hook (a thin wrapper over `fillPlacementFromEntity` + `recordFromLight` + `recordsFromDecalEntities`) rebuilds the child `OcWorldData` from the library copy (header records and everything an instance ignores ride through untouched, the `levelHeader_` rule), with placements in hierarchy order, ids from the links (new entities get fresh ids), `EXTENT` recomputed, nested instance records from `captureRecords`, prefab records from `capturePrefabs`; `saveOcworld` writes it; `lib.put` replaces the asset; **every other instance of that asset, nested ones included, is rebuilt in place** keeping its overrides (`reloadAsset`); this instance is rebuilt with its overrides re-applied and leaves edit mode. `Discard` rebuilds from the asset. `saveLevel` and the exit prompt treat `anyDirtyEdit()` as unsaved (`levelHasUnsavedEdits`, used by `applyPendingOpen`, `SandboxLevelEdit.cpp:333`): "Commit or discard the instance edit first".
4. **Open as its own level.** `Open Level` -> `requestOpenLevel(fullPath)` (`SandboxLevelEdit.cpp:300`; the unsaved-changes prompt in `applyPendingOpen` applies) after pushing `{parentPath, camera}` on `levelReturnStack_`; a `Back to <parent>` button in the level bar pops it. Saving the child standalone follows the id-keeping rule (2.2.3) and writes `EXTENT`. On return, the parent's instances of that asset are `reloadAsset`ed (the library is invalidated on every save of an open level whose path it holds).
5. **Create from selection.** Outliner context menu `Create Level Instance...`: the selected roots (and subtrees, lights, decals) are re-origined to their pivot (the first selected entity's position, yaw only), written as a new `.ocworld` by the same capture path, and replaced by an instance at that pivot, as one undo entry (`PrefabSystem::captureAsPrefab/convertToInstance` is the precedent).
6. **Break.** `breakInstance(root)`: members are reparented to the root's parent keeping world pose, links removed, root destroyed; they become ordinary placements on the next save. Overrides are baked in (they were applied already). Undo restores the instance from its `LevelInstanceState`.
7. **Reload / Packed / Stream** toggles in Details; `Reload` also runs after an external edit of the child file.
8. **Save.** `saveLevel` (`SandboxLevelEdit.cpp:466-650`): `w.levelInstances = instances.captureRecords()`; `levelHeader_.levelInstances.clear()` in the load hook beside `levelHeader_.prefabInstances.clear()` (`SandboxLevelLoad.cpp:283`); skip linked entities at the three `isLinked` sites and in the light and decal loops (`SandboxLevelEdit.cpp:553-562`, `recordsFromDecalEntities`); write `EXTENT`; keep ids (2.2.3). `fillPlacementFromEntity` gets `if (levelInstances.isMember(e)) return false;` beside `:415`.
9. **Other consumers of "is this entity a normal placement"**: the sequencer's `canKey` (`SandboxPanels.cpp:537`) returns false for members; Select All and marquee skip members unless their instance is being edited.
10. **Previews.** The Content Browser thumbnail/hover for a `.ocworld` is not in v1. A packed `.ocinst` opens in `InstanceSetEditor` read-only (a `readOnly` flag on `InstanceSetModel`), so the baked table can be inspected; `ActorPreview::setInstanceBatches` is the reuse if a level preview is added later.

### 2.8 Packed mode

#### 2.8.1 What it is

A per-instance flag (`packed` token, Details checkbox, `SetPacked`). For an instance of asset `A`, a pre-baked pack of `A` (child-local rows) replaces the entities of every statically packable mesh placement of `A` and of its nested instances, rendered through `setFoliage`, with static bodies kept for rows that collide.

#### 2.8.2 Bake (`world::bakeLevelPack`, new `LevelPack.hpp/.cpp`, pure; host wrapper `game::LevelPackBaker`)

```cpp
struct PackBakeOptions {
    std::function<bool(const std::string& asset, const std::string& material, std::string* whyNot)> meshPackable;
        // host: mesh loads, <= 16 parts, no translucent part (pbr::isTranslucent, GameFoliage.cpp:46-65). Absent = everything packable.
    u32 maxRows = 1000000;
};
struct PackBakeReport { u32 rows = 0, groups = 0, kept = 0, notPackable = 0; };
bool bakeLevelPack(LevelAssetLibrary&, const std::string& levelAsset, const std::string& contentDir,
                   const std::string& projectDir, const PackBakeOptions&, PackBakeReport&, std::string* why);
```

- A placement is packable iff: mesh placement (`className`, `vehiclePreset`, `animClip` empty), `visible`, not `snap`, `asset` non-empty, `meshPackable` true, none of its ancestors is a class, animated or vehicle placement, and no override touches it (bake runs on the asset with NO overrides; an instance with any override is not packed, 2.8.4). Everything else is KEPT.
- Rows: the placement's matrix in the child's frame (`Transform::toMatrix()` composed down the `parent` chain, nested instances composed through their record transform) in the `.ocinst` row-vector layout (`OcInstances.hpp` header comment). Groups are keyed by `(asset, material, collide)`; one `OcInstanceGroup` each, `flags = kOcInstanceFlagCastShadow`.
- Output under `<projectDir>/Binaries/LevelPacks/`: `<flat>.ocinst` and `<flat>.ocpack`, `<flat>` = the content-relative path with `/` and `\` replaced by `_` and the extension dropped (`Levels/Room.ocworld` -> `Levels_Room`), the same flattening `LEVEL_STREAMING.md` section 1 uses.

`.ocpack` (text, `fmt::OcPack`, new `OcPack.hpp/.cpp`):

```
OCPACK 1
SOURCE 3f9a1c0e2b7d4455                     closureHash of the asset when baked; a mismatch means stale
TABLE Binaries/LevelPacks/Levels_Room.ocinst
GROUP 0 collide 1 material M_Wood           one per OcInstanceGroup, same order; `material -` = the mesh's own
GROUP 1 collide 0 material -
KEEP 00000000000000c3                       every placement NOT in the table (path ids, `b2/c9` inside nested instances)
```

`fmt::transformOcInstanceRows(const f32* rows, usize count, const Mat4& m, f32* out)` multiplies each 12-float row by an instance matrix (row-vector convention, `v*M`); it is also what 2.5.3 step 8 uses.

#### 2.8.3 Runtime

For a packed, valid instance: load `.ocpack` + `.ocinst` once per asset (cached in the library), realise the entities of the KEEP set only (the resolved world is filtered by KEEP path ids; nested instance roots are still created so edits and events keep working), multiply the rows by the instance matrix, `PackedSink::set(instanceId, rows, groupMaterial)`. For each row of a `collide` group: `addStaticPlacementBody(opt, opt.meshShapes, meshId, rowTransform, ...)` using the same shape cache as the entities (so a mesh used packed and unpacked shares one shape); bodies are kept in `packedBodies[]` and removed at unrealise; they have no entity (`aver_phys_set_entity` is not called), so a ray hit reports entity 0.

#### 2.8.4 When an instance realises unpacked (all silent, logged once)

1. `packedRenderOk == false` (no Voxi, or the renderer is not in ray-driven primary mode: foliage is invisible there today, `ASSET_IMPORT.md:318-330`). `setPackedRenderOk` flipping re-realises the packed instances next `tick`, throttled to one per frame.
2. No pack, `SOURCE` mismatch with `closureHash`, or the pack fails to parse.
3. The instance has any override, or is being edited.

The editor shows the state (`packed`, `pack stale`, `no pack`, `unpacked: renderer`) and a `Bake Pack` button (runs `game::LevelPackBaker`, which wraps `bakeLevelPack` with `meshPackable` built from `GameContent::partsFor` and `pbr::isTranslucent`); `--bake-level-pack <content-relative level>` on `Sandbox.exe` does the same headlessly.

#### 2.8.5 Limits to document (docs/LEVEL_INSTANCES.md)

Packed rows are ray-traced geometry only: they cast and receive RT shadows and appear in ray-driven primary visibility and reflections; they are not rasterised, not injected into the voxel GI volume (`VoxiRenderer.hpp:196`) and not in the reference path tracer. They have collision but no entity: no scripts, no per-instance hide, no sequence tracks, no outline in the editor. The win is the entity, draw-list and per-frame cost of thousands of static meshes (`kMaxDraws`, `docs/LEVEL_STREAMING.md` hitch notes). Choose packed for repeated static dressing (props, bricks, rubble); do not pack hero geometry.

### 2.9 P1: scene lights in the packaged game (prerequisite)

Verified: `Runtime/` has `SceneDecalFeed.hpp` but no light equivalent; `grep SceneLight Runtime` is empty; `LIGHT` records are turned into entities only in `SandboxLevelLoad.cpp:386-394` and fed to Voxi only by `sandbox/src/SceneLightFeed.hpp` (`SandboxRender.cpp:241`). A packaged game therefore draws no authored light, with or without level instances.

- New `Runtime/include/aver/game/LevelLights.hpp` (header-only, like `LevelDecals.hpp`): `lightAssetId`, `lightConeCos`, `lightConeDegrees`, `lightHasExt`, `listLightAssets` and `LightAssetChoice` (the pure half of `sandbox/src/LightDetails.hpp:24-75`), `lightFromRecord`, `transformFromLightRecord`, `spawnLevelLights(world, records)` returning the entities. `sandbox/src/LightDetails.hpp` and `LightLevelIo.hpp` keep their editor-only parts and re-export the moved names with `using` (the `EditorEuler.hpp` precedent), so no editor call site changes.
- New `Runtime/include/aver/game/SceneLightFeed.hpp`: `game::SceneLightFeed`, the body of `sandbox/src/SceneLightFeed.hpp` moved (the `pt` pointer stays optional); `sandbox/src/SceneLightFeed.hpp` becomes `using editor::SceneLightFeed = game::SceneLightFeed;`.
- `GameApp`: member `SceneLightFeed sceneLightFeed_` (beside `sceneDecalFeed_`, `GameApp.hpp:346`); `update(world, voxiRenderer_, nullptr, contentDir)` before the frame's draws, in the same `AVER_MODULE_VOXI` block as the decal feed (`GameApp.cpp:2136-2152`); `game::spawnLevelLights(world, loaded.world.lights)` in `afterInstantiate` beside `spawnLevelDecals` (`GameApp.cpp:1003`).
- The editor switches `SandboxLevelLoad.cpp:386-394` to `spawnLevelLights` (labels kept).
- Verification: a headed packaged-game run of a level with two `LIGHT` records shows them (before: no light).

### 2.10 Packaged game, save/load, events

- `Content/` carries every child `.ocworld`; `Binaries/LevelPacks/` is staged by the recursive Binaries copy (`stage-game.ps1:391-394`). `verify-game.ps1`'s objectId check is unchanged (child assets are loaded through the content index, `content_.pathFor(fnv1a64(path))`, as the prefab loader does, `GameApp.cpp:1798-1805`). Nothing in packaging changes.
- `LevelAssetLibrary::Loader` in the hosts: content index to `fmt::loadOcworld`. The editor's loader prefers an unsaved in-memory edit (the open dirty level) over disk for the level being edited, and refuses cycles through it.
- **Save games.** `aver::save::capture/restore` (`SaveWorld.hpp`) rebuilds the whole world from components, including `CLevelInstanceLink` members as plain entities; static bodies are not part of the save (same as ordinary placements, `GameApp.cpp:211-232`). After a restore the host calls `instances.rescan()`: the table is rebuilt from the links (roots, asset ids, instance ids, members by `placementId`); runtime-only instances stay. Bodies survive a restore because `LevelInstances` owns them by id, not through entities; the `aver_phys_set_entity` stamps on them go stale exactly as for ordinary level bodies today (no worse).
- **Events** on the existing bus (no new bus): `LevelInstance.Loaded` / `LevelInstance.Unloaded`, `sender = target = root`, arg0 = entity count. Hooked with `AN_OnEvent` on an entity-owner subscription (`GAS.md` section 2.7 convention).
- **Play in the editor.** Members are in `levelEntities_` through `Hooks::created`, so the Play snapshot/restore treats them like placements; streaming-in during Play is the same as `LevelStreaming` (what streams in during Play reloads on Stop: `reloadAsset`-free, the editor calls `end(); begin()` on Stop for instances that were not resident at Play start, mirroring `LevelStreaming::reload`).

### 2.11 Rendering on both backends

No new shader, pipeline or RHI call. Unpacked instances are ordinary entities. Packed instances use `VoxiRenderer::setFoliage`, which builds BLAS/TLAS through `IDevice` for D3D12 and Vulkan alike (`docs/VULKAN.md:111-117`); the parity of packed rows is the parity of `FOLIAGE`. Nothing here may add a backend-specific branch. The `packedRenderOk` predicate is backend-neutral (a settings value).

### 2.12 C ABI (`modules/levelinstance.abi/include/aver/levelinstance/level_instance_abi.h`, `abi/LevelInstanceAbi.cpp`)

Stateless, host-pointer DLL `Aver.LevelInstance.Abi`, the shape of `prefab_abi.h`. Own version, no existing ABI is touched.

```c
#define AVER_LEVELINST_ABI_VERSION_MAJOR 1
#define AVER_LEVELINST_ABI_VERSION_MINOR 0
#define AVER_LEVELINST_ABI_VERSION ((AVER_LEVELINST_ABI_VERSION_MAJOR << 16) | AVER_LEVELINST_ABI_VERSION_MINOR)

typedef struct AverLevelInstanceHost {
    void* user;
    int32_t (*spawn)(void* user, const char* path, int32_t parent, const float* xyz,
                     const float* yawPitchRoll, float scale, int32_t packed);   /* root, 0 on failure */
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

AVER_LEVELINST_API int32_t aver_levelinst_abi_version(void);
AVER_LEVELINST_API int32_t aver_levelinst_last_error(void);          /* aver::AbiError, thread-local, set on success too (prefab_abi.h rule) */
AVER_LEVELINST_API void    aver_levelinst_set_host(const AverLevelInstanceHost* host);
AVER_LEVELINST_API int32_t aver_levelinst_spawn(const char* path, int32_t parent, float x, float y, float z,
                                                float yaw, float pitch, float roll, float scale, int32_t packed);
AVER_LEVELINST_API int32_t aver_levelinst_destroy(int32_t root);
AVER_LEVELINST_API int32_t aver_levelinst_root_of(int32_t entity);
AVER_LEVELINST_API int32_t aver_levelinst_is_member(int32_t entity);
AVER_LEVELINST_API int32_t aver_levelinst_find(int32_t root, const char* idPath);
AVER_LEVELINST_API int32_t aver_levelinst_set_loaded(int32_t root, int32_t loaded);
AVER_LEVELINST_API int32_t aver_levelinst_is_loaded(int32_t root);
AVER_LEVELINST_API int32_t aver_levelinst_reload(int32_t root);
AVER_LEVELINST_API int32_t aver_levelinst_set_packed(int32_t root, int32_t packed);
AVER_LEVELINST_API int32_t aver_levelinst_bounds(int32_t root, float* minXyz, float* maxXyz);
AVER_LEVELINST_API int32_t aver_levelinst_count(void);
AVER_LEVELINST_API int32_t aver_levelinst_at(int32_t index);
AVER_LEVELINST_API int32_t aver_levelinst_asset_id(int32_t root, int64_t* outId);
```

Error mapping like `PrefabAbi.cpp`: no host -> `NotInitialised` (-3), null function -> `Unsupported` (-5), null/empty path or null out-pointer -> `InvalidArgument` (-6)/`NullPointer` (-2), host refusal of a root -> `BadHandle` (-1). `Spawn` creates a runtime-only instance (not written to the level) that is destroyed with the level. Host adaptor: `Runtime/include/aver/game/LevelInstancesAbiHost.hpp` `AverLevelInstanceHost makeAbiHost(LevelInstances&)` (the `PrefabAbiHost.hpp` shape). Module: `module.json` name `Aver.LevelInstance.Abi`, dir `levelinstance.abi`, category `Gameplay`, `dependsOn ["Aver.Core","Aver.Scene"]`, `derivedFrom ["Aver.Scene"]`, consequence "Scripts cannot place, stream or query level instances."; then `python scripts/module-catalog.py --write` and the module count in `docs/MODULES.md` (66 -> 67 at the time of writing; the integrator counts).

### 2.13 C# API (`scripting/csharp/Aver.LevelInstance`, namespace `Aver.LevelInstance`)

Leaf project, copy of `Aver.Prefab.csproj` (net10.0, nullable, version 1.0.0). Files: `Aver.LevelInstance.csproj`, `Native.cs` (`[DllImport("Aver.LevelInstance.Abi")]` one extern per export, `LevelInstanceError` enum mirroring `AbiError` as `PrefabError` does), `LevelInstances.cs`.

```csharp
public static class LevelInstances {
    public static int AbiVersion { get; }
    public static int Spawn(string level, float x, float y, float z, float yaw = 0, float pitch = 0, float roll = 0,
                            float scale = 1, int parent = 0, bool packed = false);   // root, 0 on failure
    public static bool Destroy(int root);
    public static int  RootOf(int entity);          // 0 when not a member
    public static bool IsMember(int entity);
    public static int  Find(int root, string idPath);   // "c3" or "b2/c9", hex ids, leading zeros optional
    public static bool SetLoaded(int root, bool loaded);
    public static bool IsLoaded(int root);
    public static bool Reload(int root);
    public static bool SetPacked(int root, bool packed);
    public static bool TryGetBounds(int root, out (float X, float Y, float Z) min, out (float X, float Y, float Z) max);
    public static int  Count { get; }
    public static int  At(int index);
    public static long AssetId(int root);
}
public static class LevelInstanceAbi { public static LevelInstanceError LastError { get; } public static bool LastCallFailed { get; } }
public static class GraphLevelInstances { /* section 3 */ }
```

Every call returns 0/false until a host is installed (`aver_levelinst_set_host` in `GameApp` and `SandboxApp` start-up, beside `aver_prefab_set_host`, `GameApp.cpp:1808`).

## 3. Graph nodes (Blueprint parity)

Owner class `GraphLevelInstances` (public static, `scripting/csharp/Aver.LevelInstance/LevelInstances.cs`; written FIRST so the graph agent has real methods). Rows in a new `scripting/csharp/Aver.Graph/GraphLevelInstanceNodeTable.cs` (`private static GenericNodeSpec[] LevelInstanceTable()` in `partial class GameSystemNodes`); `Table()` gets one more `.Concat(LevelInstanceTable())` (`GraphGameSystemNodeTable.cs:120` already ends `.Concat(GasTable())`). Palette mirror in new `sandbox/src/GraphNodeDefsLevelInstances.hpp` (`appendLevelInstanceNodeDefs(t)`), one call added after `appendGasNodeDefs(t)` (`GraphNodeDefs.hpp:1088`), category `Level Instance`. Pins are float/int/bool only, so the level path and the id path are NODE attributes (`level=`, `path=`), as `prefab=`/`node=` are.

| Type | Kind | `GraphLevelInstances` method | Inputs (defaults) | Outputs | Attribute |
|---|---|---|---|---|---|
| `AN_SpawnLevelInstance` | Exec | `int SpawnForGraph(int parent, float x, float y, float z, float yaw, float scale, bool packed, string level)` | parent, x, y, z, yaw, scale=1, packed | `entity:int` (root, 0 on failure) | `level` (required) |
| `AN_DestroyLevelInstance` | Exec | `bool DestroyForGraph(int root)` | root | `success:bool` | none |
| `AN_GetLevelInstanceRoot` | Pure | `int RootOfForGraph(int entity)` | entity | `root:int` (0 = not a member) | none |
| `AN_FindLevelInstanceEntity` | Pure | `int FindForGraph(int root, string path)` | root | `entity:int` | `path?` (empty = the root) |
| `AN_SetLevelInstanceLoaded` | Exec | `bool SetLoadedForGraph(int root, bool loaded)` | root, loaded=true | `success:bool` | none |
| `AN_IsLevelInstanceLoaded` | Pure | `bool IsLoadedForGraph(int root)` | root | `loaded:bool` | none |
| `AN_ReloadLevelInstance` | Exec | `bool ReloadForGraph(int root)` | root | `success:bool` | none |
| `AN_SetLevelInstancePacked` | Exec | `bool SetPackedForGraph(int root, bool packed)` | root, packed | `success:bool` (true = effective state matches) | none |
| `AN_GetLevelInstanceBounds` | Pure | `bool BoundsForGraph(int root, out float minX, out float minY, out float minZ, out float maxX, out float maxY, out float maxZ)` | root | `minX minY minZ maxX maxY maxZ:float, success:bool` | none |
| `AN_GetLevelInstanceCount` | Pure | `int CountForGraph()` | none | `count:int` | none |
| `AN_GetLevelInstanceAt` | Pure | `int AtForGraph(int index)` | index | `root:int` | none |

Row strings follow the existing grammar, for example
`Reg("an_spawnlevelinstance", GenericKind.Exec, typeof(GraphLevelInstances), "SpawnForGraph", "parent:int,x:float,y:float,z:float,yaw:float,scale:float=1,packed:bool", "entity:int", "p:parent,p:x,p:y,p:z,p:yaw,p:scale,p:packed,@level", "entity")` and
`Reg("an_getlevelinstancebounds", GenericKind.Pure, typeof(GraphLevelInstances), "BoundsForGraph", "root:int", "minX:float,minY:float,minZ:float,maxX:float,maxY:float,maxZ:float,success:bool", "p:root,>minX,>minY,>minZ,>maxX,>maxY,>maxZ", "success")`.

**Entries (no new node).** `LevelInstance.Loaded` / `LevelInstance.Unloaded` through `AN_OnEvent` with the root entity as owner; `AN_EventPayload` index 0 = entity count.

**Parity table.** Spawn/destroy/root/find/loaded/reload/packed/bounds/count/at: node each. `LevelInstanceAbi.LastError`, `AbiVersion`, `AssetId` (an int64 hash a graph pin cannot hold): C#-only. Residency changes: events. Editor-only operations (edit, commit, create, break, bake): not runtime features, no node.

## 4. Tests

Names are exact. All C++ targets end in `Test` (auto-registered by `aver_register_ctest_in`, root `CMakeLists.txt`). New directory `tests/levelinstance/` (`add_subdirectory(tests/levelinstance)` inside the `AVER_MODULE_SCENE` block next to `tests/prefab`, root `CMakeLists.txt:509`), plus additions to `tests/world` and `tests/game`.

| Test (file) | Layer | Asserts |
|---|---|---|
| `OcLevelInstTest` (`tests/levelinstance/src/OcLevelInstTest.cpp`) | Formats only | Round trip of `LEVELINST` with every token, three override kinds, a nested path `b2/c9`; `EXTENT` round trip; percent-encoded asset and name; `id` leading zeros read, always written 16 digits; a level with no instances and no extent is byte-identical to before; stray `LIOVERRIDE`/`ENDLEVELINST` outside a block are dropped; unknown token skipped; `addcomp` parsed and ignored with a warning; an `.ocworld` with all three of `PREFABINST`, `LEVELINST`, placements keeps each list intact; a pre-feature parser model (the unknown-record rule of `OcWorld.cpp:231`) leaves placements untouched. |
| `OcPackTest` (same dir) | Formats only | `.ocpack` round trip, `KEEP` path ids, bad header refused, `TABLE`/`GROUP` count mismatch refused; `transformOcInstanceRows` equals `(child * instance)` computed with `Transform::toMatrix` to 1e-4 for yaw 90 + scale 2 + translation; identity matrix is the identity. |
| `LevelAssetLibraryTest` (`tests/levelinstance/src/`, links Aver.World) | World, no scene | Parses once (loader call count 1 for two users); `closure` finds A->B->A, A->A, and a depth-9 chain, accepts depth 8, reports the chain text; `wouldCycle(A, B)` true when B contains A transitively, false otherwise; `closureHash` changes when a nested asset's record changes and not when an ignored record (SUN) changes; `ensureIds` assigns unique non-zero ids, is idempotent and rewrites the file only when something was missing; `resolve` applies `removed` (children dropped, `parent` renumbered), `material`, `position`, `visible`, `collide`, `name`; a path with `/` lands on the nested record; unmatched counted not fatal; `instanceBounds` of EXTENT (-100,-100,0)..(100,100,200) under yaw 90, scale 2, pos (1000,0,0) equals the hand-computed AABB; unknown extent returns false; case/backslash variants of one path are one key. |
| `LevelPackTest` (same dir) | World + Formats | Packable predicate for each exclusion (class, vehicle, anim, hidden, snap, translucent via fake `meshPackable`, animated ancestor); groups keyed by (asset, material, collide); rows equal hand-composed matrices including a two-deep parent chain and a nested instance; `KEEP` complete (every non-table placement, including nested path ids); `.ocinst` passes `valid()`; `maxRows` cap reported; `SOURCE` equals `closureHash` and goes stale after a child edit; baking twice is byte-identical. |
| `LevelInstancesTest` (`tests/levelinstance/src/LevelInstancesTest.cpp`, links Aver.Runtime.Game.Core, Aver.Scene, Aver.World, Aver.Physics when on; fake `ContentPort`, `Hooks`, `PackedSink`) | Runtime, no GPU | Realise makes a root and members parented to it with `CLevelInstanceLink` (assetId, placementId, instanceId, depth); member world position = instance TRS x local (yaw 90, scale 2); a nested instance composes; cycle A->B->A logs and skips only that branch, siblings realise; overrides applied (material, removed, position); unrealise destroys every member (invalid after flush), removes every body (live-body count returns to the baseline) and releases every acquired mesh (acquire count == release count); streaming: viewer far = not loaded, near = loaded after `ready()`, far again = evicted, pin keeps it, unknown-bounds instance always loaded, `stream 0` never evicted, an evicted-and-reloaded unit keeps its overrides; two instances of one asset parse it once; `reloadAsset` keeps root handles and overrides and picks up an added placement; `beginEdit` shows the raw asset, `commitEdit` (fake `captureEntity`) writes the asset, updates the library and rebuilds the other instance, `discardEdit` restores; `computeOverrides` -> `setOverrides` round trip is the identity (moved, removed, re-materialled member); `breakInstance` leaves plain entities at the right world poses with no link; class placements stay pending until `spawnPendingClasses` and are destroyed through `destroyClass`; a light and a decal in the child exist under the root; `captureRecords` returns the records (not the members); save `capture` + `restore` + `rescan` rebuilds the table; `animatedBodies()` includes an animated child placement's kinematic body; `spawn`/`destroy` runtime-only instances are destroyed by `end()`; `Loaded` residency callback fires once per change. |
| `LevelInstancesPackedTest` (same dir) | Runtime, no GPU | Packed realise: `PackedSink::set` receives rows = pack rows x instance matrix; KEEP placements are entities, packed ones are not; colliding groups make one body per row from the shared shape cache (shape count == distinct meshes); `packedRenderOk=false`, stale pack, any override, and an edit each realise unpacked with the same entity count as the unpacked baseline and clear the sink; toggling `setPackedRenderOk` flips without leaking bodies, rows or meshes. |
| `LevelInstanceAbiTest` (same dir) | ABI | No host: every call 0 with `NotInitialised`; fake host receives exact args (path, xyz, ypr, scale, packed); null function pointer -> `Unsupported`; null/empty path -> `InvalidArgument`; null out pointer -> `NullPointer`; version equals the macro; `aver_levelinst_last_error` is 0 after a success. |
| `LevelInstanceEditorModelTest` (`tests/levelinstance/src/`, compiles `sandbox/src/LevelInstanceEditorModel.cpp` directly like `PrefabEditorModelTest`) | Editor model, no UI | `place` refuses A into A and A into B when B contains A, with status text and no entity; `place` assigns ids and an instance `id`; `createFromSelection` writes an asset with ids, `EXTENT` and re-origined transforms and replaces the selection with an instance at the same world poses; `pickTarget` returns the root for a plain click, the member for Alt or an edited instance; edits under a locked instance are refused; commit/discard dirty tracking drives `anyDirtyEdit`; return stack push/pop; save-prompt state. |
| `LevelInstanceTest` (`tests/world/src/LevelInstanceTest.cpp`, new cases) | World | `attach` composes the root's body pose with `parentWorld` (compare `aver_phys_body` position to the hand composition for a rotated, scaled attach); a level instantiated with `attach == nullptr` produces byte-identical entities, ids and body count to a recorded baseline (the extraction of `addStaticPlacementBody` changes nothing); `addStaticPlacementBody` shares one shape across rows. |
| `LevelLightsTest` (`tests/game/src/LevelLightsTest.cpp`, links Aver.Runtime.Game.Core) | Runtime | `spawnLevelLights` of a point, a spot and a rect record, then `scene::gatherLights`, returns the same count and fields; `lightFromRecord(recordFromLight(c))` round-trips (the existing editor helper through the re-export); `lightAssetId` equals the pre-move values for fixture paths (pinned literal hashes); `lightConeCos/Degrees` pinned. |
| `LevelInstanceNodeTests.cs` (`scripting/csharp/Aver.Graph.Tests/`) | Managed | `GameSystemNodes.Validate()` returns no problems for the new rows (every method exists, parameter types match pins, every input pin feeds a parameter); a bare `NODE` gets default pins and defaults (`scale=1`, `loaded=true`); `AN_SpawnLevelInstance` without `level=` fails to compile naming `level=`; `AN_FindLevelInstanceEntity` without `path=` compiles; a pure node (`AN_GetLevelInstanceRoot`) feeds an exec node under both compilers. One line in `Program.cs` beside `GameSystemNodeTests.RunAll()`. |
| `GraphEditorGeometryTest` (existing, `tests/editor`, `testNodeCatalogCoversTheCompiler`) | Editor | Must stay green: the hand-maintained palette (`GraphNodeDefsLevelInstances.hpp`) and the compiler's node set agree for every `AN_*LevelInstance*` type; add the type names to whatever list that test reads if it needs one. |

Verification the implementer must do and report, not claim: build; `ctest` for every test above; `dotnet run --project scripting/csharp/Aver.Graph.Tests`; `scripts/gates.ps1` shows the render gates unchanged against `gates.baseline.txt` (the `instantiate` refactor must be bit-exact); then headed runs with a sample project (two small child levels, one nested in the other, a mesh, a collider, a point light, a class placement with a graph): (1) editor: drop an instance, Alt+click override a material, Edit Instance / Commit and see a second instance update, Open Level / Back, Create from selection, Break, save and reopen with ids preserved; (2) Play: the class runs, the player collides with the instance's wall, the light lights; (3) packaged game via `stage-game.ps1` + `verify-game.ps1`: the same scene, the light present (P1), a packed instance of 2000 props showing in ray-driven mode and unpacked (and still visible) with `--rt-render-mode 0`; (4) a streamed level with 40 instances: units load and unload around the camera with an `[Hitch]` line check; (5) 1440p screenshots and a ping per the standing rule.

## 5. Files to create or modify, and ownership

Owner of every file below is this feature (the level-instances agents) unless stated. Other wave-3 features: **AI** (modules/synapse*, AI C#), **Landscape** (modules/landscape, its editor tools, fluids/water for rivers). Shared files are named with the exact delimited hunk; every hunk is bracketed `// [wave3/level-instances begin]` ... `// [wave3/level-instances end]` (`#` or `<!-- -->` in other file types); whoever merges second keeps both.

**Created**
- Formats: `modules/formats/include/aver/formats/OcLevelInst.hpp`, `OcPack.hpp`; `modules/formats/src/OcLevelInst.cpp`, `OcPack.cpp`
- World: `modules/world/include/aver/world/LevelAssetLibrary.hpp`, `LevelPack.hpp`; `modules/world/src/LevelAssetLibrary.cpp`, `LevelPack.cpp`
- Scene: `modules/scene/include/aver/scene/LevelInstanceLink.hpp`
- Runtime: `Runtime/include/aver/game/LevelInstances.hpp`, `LevelInstancesAbiHost.hpp`, `LevelLights.hpp`, `SceneLightFeed.hpp`, `LevelPackBaker.hpp`; `Runtime/src/LevelInstances.cpp`, `LevelInstancesAbiHost.cpp`, `LevelPackBaker.cpp`
- ABI module: `modules/levelinstance.abi/{module.json, CMakeLists.txt, README.md, include/aver/levelinstance/level_instance_abi.h, abi/LevelInstanceAbi.cpp}`
- C#: `scripting/csharp/Aver.LevelInstance/{Aver.LevelInstance.csproj, Native.cs, LevelInstances.cs}`; `scripting/csharp/Aver.Graph/GraphLevelInstanceNodeTable.cs`; `scripting/csharp/Aver.Graph.Tests/LevelInstanceNodeTests.cs`
- Editor: `sandbox/src/LevelInstanceEditorModel.{hpp,cpp}`, `LevelInstanceEditorUi.{hpp,cpp}`, `GraphNodeDefsLevelInstances.hpp`
- Tests: `tests/levelinstance/CMakeLists.txt` and the sources in section 4; `tests/game/src/LevelLightsTest.cpp`
- Docs: `docs/LEVEL_INSTANCES.md` (user guide: format, workflows, packed limits, node and event list, API)

**Modified (this feature's edits)**
- `modules/formats/include/aver/formats/OcWorld.hpp` + `src/OcWorld.cpp`: `#include "aver/formats/OcLevelInst.hpp"`; members `levelInstances`, `hasExtent`, `extentMin/Max` after `prefabInstances` (`OcWorld.hpp:745`); parse hook after `:292`; `EXTENT` parse branch in the record chain; writer calls after `:1254` and the `EXTENT` line. **Shared with Landscape** (it may add `LANDSCAPE`/`WATER` fields in the same files): this feature's hunks are the only ones in the prefab/instance area; Landscape's stay inside the `OcLandscapePlacement`/`OcWaterPlacement` blocks.
- `modules/formats/include/aver/formats/OcPrefab.hpp` + `src/OcPrefab.cpp`: export `parseOcOverrideBody` / `appendOcOverrideBody` (the two file-local helpers become thin public wrappers; no behaviour change)
- `modules/formats/CMakeLists.txt`: the four new sources
- `modules/world/include/aver/world/LevelInstance.hpp` + `src/LevelInstance.cpp`: `Attach`, `addStaticPlacementBody` (2.5.1); `modules/world/CMakeLists.txt`: the new sources
- `Runtime/include/aver/game/GameLevel.hpp` + `src/GameLevel.cpp`: 2.5.4 hunks. **Owned by this feature** (level load/streaming)
- `Runtime/include/aver/game/GameFoliage.hpp` + `src/GameFoliage.cpp`: 2.5.5
- `Runtime/src/GameApp.cpp` + `GameApp.hpp`: level-instance wiring (`installLevelHooks`, `onInit` host install, `sceneLightFeed_`, `spawnLevelLights`, `setPackedRenderOk`). **Shared with AI** (it touches `GameApp` for crowds/nav): separate hunks
- `Runtime/CMakeLists.txt`: link `Aver.LevelInstance.Abi` beside `Aver.Prefab.Abi` (`:197-198`); sources
- Editor: `sandbox/src/SandboxApp.hpp` (members `levelInstances` access, `entityPlaceId_`, `levelReturnStack_`, `levelInstanceModel_`, `levelInstanceDlg_`; one delimited block; **shared with Landscape**), `SandboxLevelLoad.cpp` (`:283` clear, `:386-394` lights via `spawnLevelLights`, instance hooks, `entityPlaceId_` fill), `SandboxLevelEdit.cpp` (`:415, 553-562, 567, 594, 639` skips and capture, id rule, `EXTENT`, `levelHasUnsavedEdits`), `SandboxSelection.cpp` (instance hooks beside `installPrefabHooks`, undo `Kind::LevelInstance` at `:1256/:1311`), `SandboxViewport.cpp` (`.ocworld` drop at `:1106`, pick redirect at `:2402`), `SandboxPanels.cpp` (`:537` `canKey`, Details section beside `:1690`, outliner row/context menu), `SandboxShell.cpp` (dialog draw beside `:2417`), `SandboxRender.cpp` (`:241` type only), `LightDetails.hpp`, `LightLevelIo.hpp`, `SceneLightFeed.hpp` (re-exports), `DecalLevelIo.hpp` (skip members), `InstanceSetModel.*`/`InstanceSetEditor.*` (`readOnly`), `sandbox/CMakeLists.txt` (sources; link beside `:313-315`)
- `tools`: none. `scripts/stage-game.ps1`: none (Binaries already recursive)
- Scripting wiring (the prefab route): `modules/scripting/CMakeLists.txt` (glob `Aver.LevelInstance/*.cs|*.csproj` beside `:126`, output `${AVER_BRIDGE_DIR}/Aver.LevelInstance.dll` beside `:160`), `scripting/csharp/Aver.Graph/Aver.Graph.csproj` and `Aver.Scripting.Bridge.csproj` (a `ProjectReference`, beside the `Aver.Prefab` lines), `scripting/csharp/Aver.Graph/GraphGameSystemNodeTable.cs` (`.Concat(LevelInstanceTable())` at `:120`), `scripting/csharp/Aver.Graph.Tests/Program.cs` (one line), `sandbox/src/GraphNodeDefs.hpp` (include + call, `:1088`). **Shared with AI** (Synapse binding appears in the same files): one-line additions each
- Root `CMakeLists.txt`: `add_subdirectory(modules/levelinstance.abi)` after `modules/prefab` (`:267`), `add_subdirectory(tests/levelinstance)` after `tests/prefab` (`:509`). **Shared with all wave-3 features**
- `cmake/modules.catalog.json` (regenerated), `docs/MODULES.md` (count)
- Docs, append-only: `docs/formats/FORMAT_SPECS.md` (section 11 paragraph, new 11e), `docs/AVER_NODE_NODES.md` (node family), `docs/LEVEL_STREAMING.md` (new section 7, instance units), `docs/ARCHITECTURE.md` (one paragraph), `modules/world/README.md`

**Not touched (partition)**: `modules/synapse*`, AI C#, `modules/landscape`, `modules/fluids`, `LandscapeEditor*`, `GameLandscape.*`, `GameWater.*`, `modules/prefab` (reused, not edited), `framework_abi.h`, `Aver.Framework`, `modules/save` (links ride through reflection), `VoxiRenderer.*` and every shader (no render change), `LevelStreaming.*` (units are a separate streamer over the same class), `scripts/*.ps1`.

**Implementation order** (each step builds and tests): P1 lights (independent, can run first or in parallel) -> 1 formats + `LevelAssetLibrary` + `resolve` + tests -> 2 `instantiate` attach/extraction + `CLevelInstanceLink` + `LevelInstances` eager realise + `GameLevel`/`GameApp` wiring + `LevelInstancesTest` + headed packaged-game check -> 3 ABI + C# + graph nodes + node tests -> 4 editor (place, lock/pick, overrides, edit/commit, open, create, break, save/ids) -> 5 streaming units -> 6 packed (bake, `.ocpack`, `setExtraTable`, bodies, fallbacks, bake button/flag). Parallel agents with disjoint files: A (steps 1), B (step 2 and 5), C (step 3), D (step 4, needs 2's header), E (step 6, needs 1 and 2), L (P1).

## 6. Risks and open questions

1. **Packed rows are ray-traced geometry only** (the foliage contract). A packed instance has no voxel-GI contribution and is absent from raster and the reference path tracer; the fallback makes this invisible only when the renderer is not ray-driven. Owner: is that acceptable for v1, or should packed wait for an instanced raster scene path (a real renderer feature: `drawMeshInstanced` exists only for shadow passes, `VoxiRenderer.cpp:4876`, and the scene pass is per entity)?
2. **Merged-mesh packing is not built.** UE's packed level actor merges into ISM/HISM; the instance-table route matches that. Merging unique meshes into one `.ocmesh` (LOD, cluster data, collision cache, material slots) is a separate project. Decide whether it is wanted.
3. **P1 is a real gap in the shipped game** (no authored lights at all). It is cheap and in this design, but it changes what every packaged level looks like once lights appear. Confirm.
4. **Ids on save.** The "if any placement has an id, give every placement one" rule adds about 21 bytes per placement to a level the first time it is instanced. Alternative: key overrides by an index path (breaks on reorder). Chosen: ids.
5. **Unit-atomic streaming.** A 5000-placement instance realises in one go (bounded by prefetch, not sliced). If hitches show, the next step is to slice `realise` over ticks the way `LevelStreaming` slices roots; not designed here.
6. **Vehicles, landscape, water, post volumes, sequences in a child are ignored.** A room with its own post volume or a lane network is a plausible request. Post volumes are world-axis boxes and do not survive a rotated instance; vehicles need the level's `.oclanes`.
7. **Non-uniform instance scale** with rotated children makes body composition approximate (the same limit as nested non-uniform placements, `LevelInstance.cpp:279-289`). The editor's scale gizmo on an instance is uniform-only.
8. **Overrides are record-level only** (material, visible, collide, removed, transform, name). Generic component overrides would reuse `OcPrefabOverride` and `PrefabFields` (`modules/prefab/src/PrefabFields.hpp` is internal to Aver.Prefab and would have to be made public); deferred.
9. **Editing the open level's own instance of the open level is impossible by construction** (cycle). Editing a child that the open level also contains as the host: `beginEdit` is allowed, commit rewrites the child on disk and the library, never the open level.
10. **`aver_fw_class_find` timing** for class placements inside streamed-in units after scripting is ready is assumed immediate (`Hooks::spawnClass` returns an entity or null). A class that fails to bind leaves a live entity with no behaviour, exactly as `GameLevel::spawnClassPlacements` reports today.
11. **`rtRenderMode` semantics** for `setPackedRenderOk` must be confirmed against `RenderSettingsResolver.hpp` (which value means ray-driven primary visibility).
12. **Hot reload from disk** (a child edited outside the editor) is manual (`Reload`); the prefab system has no file watcher either (`grep ocprefab sandbox/src` finds none).
13. **Level-instance thumbnails in the Content Browser** are not in v1 (the `ActorPreview` instance-batch API makes them easy later).
