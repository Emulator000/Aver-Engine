# PLACE_CLASS: put a graph CLASS actor in a level without editing a file

Owner request 2026-10-12. Design only: nothing here is implemented. Every claim about existing code carries a
file:line; anything not confirmed by reading code is marked UNVERIFIED. Written for implementers who cannot build,
so each step names the file, function and exact behaviour, and ends in checks (section 6) that a person with a
build can run.

**Status: FROZEN 2026-10-12 by the reviewer pass** (cross-checked against ADD_COMPONENT.md and DEMO_CONTENT.md; F5, F7, F9, the `LevelClassSave.hpp`
text, `isPlaceableAssetExt`, `destroyEntity` and the `spawnClassPlacements` pair were re-read at `f738c7a2`). Implementation order and file
ownership are in `docs/plans/parity_demo/TASKS.md`; where this document edits a file that ADD_COMPONENT.md also edits, TASKS.md says which task
owns the file and applies both documents' hunks.

## 0. Reconciliation with the other two documents

* **Placement mechanism the demo levels use.** DEMO_CONTENT.md hand-writes `PLACE none x y z yaw pitch roll 1 class AN_DemoXxx` lines, which
  `parseOcworld` reads (OcWorld.cpp:765) and `GameLevel::spawnClassPlacements` / `SandboxApp::spawnClassPlacements` spawn
  (GameLevel.cpp:570-612; SandboxLevelLoad.cpp:664-703). This document's drop / `Add > Actor From Graph` writes the same record via
  `makeClassPlacement`, so a level the editor saves and a level the demo ships are interchangeable. Dragging `Content/Demo/Scripts/AN_DemoOrbiter.ocgraph`
  (a `CLASS ... Actor`) is the one-gesture check that the two agree.
* **`name` on a class placement.** The spawner passes the CLASS name, not `OcWorldPlacement::name`, to `aver_fw_spawn` (SandboxLevelLoad.cpp:690,
  GameLevel.cpp:597), so a `name <n>` token on a class line is preserved in the file by `appendClassPlacements` (it copies the whole authored
  placement, LevelClassSave.hpp:54) but does not rename the spawned actor. `makeClassPlacement` leaves `name` empty. A sequencer track binds an actor by its Outliner label
  (`sequenceBindName`, SandboxLevelStream.cpp:275-281; `docs/SEQUENCER.md:63` says a class actor can be bound by name), and how a class actor gets that label is UNVERIFIED; DEMO_CONTENT's
  Demo_Sequencer therefore binds only mesh placements and a light, never a class actor.
* **Class actors are not Add Component hosts.** Their root and COMP parts are not in `levelEntities_`, so ADD_COMPONENT.md's menu entries are disabled there
  (its 0.0). Skinned content on a plain placement is ADD_COMPONENT.md's job (`COMPONENT SkeletalMesh`); skinned content in a class is this
  document's `COMP ... SkeletalMesh` path. Both stay.
* **Stop destroys class actors (F7) and the demo depends on the fix.** `aver_fw_end_play` sweeps every class-bearing entity
  (FrameworkAbi.cpp:1017-1040, re-read: confirmed from code) and `aver_fw_begin_play_with_pawn` spawns only the GameInstance, GameMode,
  controller and pawn (FrameworkAbi.cpp:981-1011), so without Step 9 the second Play of any demo level has none of its placed actors. Until Step 9
  lands, the demo README tells the owner to reopen the level after Stop.
* **Class graphs run `OnStart` at level load, not at Play** (`docs/VISUAL_SCRIPTING.md:276-283`; SandboxLevelLoad.cpp:660-663). The demo acceptance
  accounts for it (DEMO_CONTENT U22).

Goal: drag an `.ocgraph` that declares a `CLASS` from the Content Browser into the viewport, or use
`Add > Actor From Graph`, and get the same `PLACE none ... class <Name>` record the loader already reads
(`docs/VISUAL_SCRIPTING.md:301-306`). The placed actor must be visible before Play (its `COMP SkeletalMesh`
parts included), selectable, movable with the gizmo, deletable, undoable, saved, reloaded, and running its graph in
Play.

## 1. Result in one screen

1. **The edit-mode preview needs no engine change.** The editor already spawns class placements for real on level
   load (`aver_fw_spawn`, `SandboxLevelLoad.cpp:664-703`), and the component tree is built inside the managed
   bind, not at Play (`HostBridge.cs:1394-1398`, `GraphComponentTree.Build` `GraphComponentTree.cs:39-73`). Skinned
   entities are updated every frame regardless of play state (`SandboxApp.cpp:2278-2283`), at rest when no
   `CAnimator` shares the entity (`SkinnedScene.cpp:174-176,197-203`; `test-content/ControlRigDemo/Game.ocproject`
   header). So a placed class with a `SkeletalMesh` COMP already draws; what is missing is creating the placement
   and everything around it.
2. **Placing is a new `placeClassInstance()`** that appends to an editor-owned placement list and spawns through the
   same code the load path uses. Entry points: drop on viewport, `Add > Actor From Graph`, and the MCP `level place`
   tool (which today refuses class lines, `SandboxMcp.cpp:683-690`).
3. **Today's class support has five real holes that make "deletable, undoable, saved" false**, found by reading the
   code (table in section 2). The biggest: pressing Stop destroys every class instance in the level
   (`FrameworkAbi.cpp:1030-1036`) and nothing respawns them. These are fixed as part of this design because a
   placement feature on top of them would not survive a Play/Stop cycle.
4. **Picking gets redirected from a component part to the class root**, because the root has no
   `CMeshRenderer` and the part does (`SandboxViewport.cpp:2369-2371`). Mesh-less classes get a small viewport
   marker so they can be seen and clicked.
5. **Tests:** one new headless suite (`GraphClassPlacementTest`), additions to `GraphEditorLoadSaveTest`, and an
   in-process `--class-place-test` (modelled on `--undo-test`) against a new `test-content/ClassPlace` project.

## 2. What exists, what does not (all read from code)

| # | Fact | Evidence | Consequence |
|---|------|----------|-------------|
| F1 | Class placements are collected at load and spawned for real once scripting is ready | `GameLevel.cpp:315-317`, `SandboxLevelLoad.cpp:664-703`, called from `SandboxProject.cpp:265-268`, `SandboxApp.cpp:1055-1062`, `SandboxLevelEdit.cpp:388-393` | Preview in edit mode works for loaded placements |
| F2 | Graph class instances tick every frame, ungated on play state | `docs/VISUAL_SCRIPTING.md:276-283`; `HostBridge.cs:1439` registers the instance in `s_graphInstances` at bind | `aver_fw_spawn_preview` would not stop ticking either, so previewing "inert until Play" is not a real option (keep `aver_fw_spawn`) |
| F3 | COMP parts are child entities parented to the class root | `GraphComponentTree.cs:64-70`; `SkeletalMesh` case `:121-136` | Picking hits the part, not the root |
| F4 | The editor save writes class placements from `level_.classPlacements()` paired with `levelClassInstances_` by recorded index; an instance with no live entity is written as read from disk | `SandboxLevelEdit.cpp:728-740`, `LevelClassSave.hpp:44-67` | A new placement has nowhere to live; a deleted one is written back |
| F5 | **Delete leaves the placement in the file.** `destroyEntity` calls `w.destroy(e)` (`SandboxSelection.cpp:1128`), not `aver_fw_destroy`, and never touches `levelClassInstances_`; the next save sees a dead entity and falls back to the on-disk copy | `SandboxSelection.cpp:1118-1156`, `LevelClassSave.hpp:53-57`, `SandboxLevelEdit.cpp:731-737` | Deleted class actor reappears after save/reload. Also the managed unbind (`GraphHost` release) never runs, so the graph likely keeps ticking a dead handle (UNVERIFIED which of those two the runtime shows) |
| F6 | **Undo of Delete rebuilds a bare entity from a snapshot**, not a class instance | `undo()` `SandboxSelection.cpp:1245`, `recreateFrom` `:795-820`, `spawnEntityFrom` `:759-790` | Ctrl+Z gives a mesh-less ghost with no graph, not saved |
| F7 | **Stop destroys every class-bearing entity**: `aver_fw_end_play` sweeps all entities with a class, not only ones the session spawned; `stopPlay` calls it and respawns nothing | `FrameworkAbi.cpp:1028-1036`, `SandboxPlay.cpp:690-693`, grep of `SandboxPlay.cpp` finds no `spawnClassPlacements`/`levelClassInstances_` | After the first Play/Stop the placed actors are gone from the viewport; unsaved moves made before Play are lost on save (falls back to disk copy). Read from code, not observed |
| F8 | Play snapshot covers `levelEntities_` only | `SandboxPlay.cpp:107-119` | A graph that moves its actor (OrbitDemo) leaves it moved after Stop |
| F9 | Viewport drop accepts only `.ocmesh`/`.ocparticle` | gate `SandboxContentBrowser.cpp:2256-2262`, used at `SandboxShell.cpp:2392`; drag source already carries any file `SandboxContentBrowser.cpp:2424-2431,2641-2646` | `.ocgraph` is dragged but refused |
| F10 | Copy/Paste/Duplicate rebuild a plain entity from a snapshot | `SandboxViewport.cpp:2047,2105,2167` | A duplicated class actor is an unsaved husk |
| F11 | `mcpLevelPlace` refuses class lines | `SandboxMcp.cpp:683-690` | Agents cannot place one either |
| F12 | `CLASS` is not modelled by the C++ graph reader (`OcGraphData` has no field) | `GraphEditor.hpp:632-635`, `GraphEditor.cpp:3503-3506` | The editor needs its own light scan; `GameApp.cpp:318-353` already has one for the runtime |
| F13 | Re-running `DeclareGraphClasses` is idempotent for the framework but warns "declared by more than one graph" for every class already known, even when it is the same file | `HostBridge.cs:741-746` | Needed for fresh COMP data (see 4.4) so the warning must compare paths |

Side finding, not part of this task (report to the orchestrator): `spawnFromAssetDrop` has `.ocprefab`
(`SandboxViewport.cpp:1114`) and `.ocworld/.ocmap` (`:1146`) branches, but the only caller filters with
`isPlaceableAssetExt` (`SandboxShell.cpp:2392`), which excludes both (`SandboxContentBrowser.cpp:2256-2262`).
By grep those branches are unreachable from a viewport drop. UNVERIFIED at runtime.

## 3. Design

### 3.1 Data model

Keep the existing pairing rule (index, never position; `LevelClassSave.hpp:11-27`). Two changes:

* `SandboxApp::classAuthored_` (new, `std::vector<fmt::OcWorldPlacement>`): every class placement of the open level,
  loaded ones first, session-added ones appended. It replaces `level_.classPlacements()` as the list the save and
  `ClassInstance::placement` index into. Filled in `onLevelInstantiated` (not in `spawnClassPlacements`), so a level
  opened before scripting is ready still saves its class lines.
* `LevelClassInstance` gains `bool removed = false` (tombstone). A removed instance is not written; an authored
  placement with **no** instance entry (class not resolvable) is still written as read, as today.

Add and delete are then: push to `classAuthored_` + spawn; or tombstone + `aver_fw_destroy`. Undo/redo flip the
tombstone and respawn.

### 3.2 Flows

| Flow | Path |
|------|------|
| Drop `.ocgraph` on viewport | `isPlaceableAssetExt` accepts it, `spawnFromAssetDrop` calls `dropGraphClass` (reads the file, `parseGraphClassDecl`, `dropWorldPoint`, snap) which calls `placeClassInstance` |
| `Add > Actor From Graph` | Submenu fed by `scanGraphClasses(contentDir)` (cached per popup open), places at `camPos_ + camForward()*kAddDistance` like `spawnLightAtCamera` (`SandboxViewport.cpp:807-830`) |
| `placeClassInstance` | refuse during Play or with no level; `declareGraphClasses` (fresh COMP data); `aver_fw_class_find`; role refusal; push to `classAuthored_`; `spawnClassSlot`; select; `Create` edit with `className`/`classSlot`; `markLevelUnsaved` |
| Select | `pick()` maps a hit part to its class root (`classRootOf`), marker sphere for roots without parts |
| Move | unchanged: gizmo on the root's `CLocal`; `Transform` edit; save writes the live pose (`LevelClassSave.hpp:56-65`) |
| Delete | `deleteSelection` tags the edit (`className`, `classSlot`, empty subtree); `destroyEntity` goes through `aver_fw_destroy` and tombstones |
| Undo/redo | `Create` undo = `destroyEntity` (class aware); `Destroy` undo and `Create` redo = `recreateFrom` -> `respawnClassFromEdit` (spawns from the class, rebinds the `EditId`) |
| Save/reload | `appendClassPlacements` over `classAuthored_`; tombstones skipped; new placements written with live pose |
| Play | instances are already live; `capturePlayWorld` also records class roots |
| Stop | `respawnLostClassInstances()` respawns any root `end_play` destroyed, at its pre-Play pose, rebinding `EditId`s |

Rejected alternatives: spawning with `aver_fw_spawn_preview` (F2: still ticks, and would diverge from the runtime);
mutating `GameLevel::classPlacements_` from the editor (Runtime module shared with the shipped game, and its
`classPlacements()` is a const view, `GameLevel.hpp:205-209`).

### 3.3 Known limits kept (state them in the status text or doc, do not hide them)

* Graph `VAR` state is not preserved across delete/undo or Play/Stop: the respawned instance starts fresh.
* A COMP child can still be selected from the Outliner and moved; that is not saved (parts are rebuilt from the
  graph). Pick redirects clicks to the root; the Outliner is left alone.
* Duplicate/Copy of a class actor is refused with a status line (F10) rather than producing an unsaved husk.
* Adding components to a class root in Details is not persisted (existing behaviour).
* GameMode, PlayerController and GameInstance classes are refused (they are spawned by Play; the level picks the
  GameMode in World Settings, `SandboxPlay.cpp:357`), as are abstract classes (`FrameworkAbi.cpp:550`).

## 4. Implementation steps

Module rules: everything touching `aver_fw_*` sits in `#if AVER_MODULE_FRAMEWORK` (FRAMEWORK implies SCENE;
`scripts/module-guard-audit.py:79,94,115`), `scripts_` in `#if AVER_MODULE_SCRIPTING`. The two new header
functions below use neither, so they are unguarded (that is what lets the tests include them). Comments stay brief
and give the reason, per the repo rule.

### Step 1. `sandbox/src/LevelClassSave.hpp`

```cpp
struct LevelClassInstance {
    std::size_t placement = 0;
    int32_t     entity    = 0;
    bool        removed   = false;   // deleted in the editor: not written, not pickable
};
```
`{1, 41}` still aggregate-initialises (existing test `GraphEditorLoadSaveTest.cpp:1493`). In
`appendClassPlacements`, after the `byPlacement` fill, at the top of the main loop:
`if (byPlacement[i] && byPlacement[i]->removed) continue;`. Add, below it:

```cpp
// The live class root `hit` belongs to: itself, or its nearest ancestor that a non-removed instance owns; 0 if none.
// parentOf(entity) -> parent or 0. Bounded, so a corrupt hierarchy cannot loop a click.
template <typename ParentOf>
int32_t classRootOf(int32_t hit, ParentOf&& parentOf,
                    const std::vector<LevelClassInstance>& live, std::size_t maxDepth = 64) {
    for (std::size_t d = 0; hit != 0 && d <= maxDepth; ++d) {
        for (const LevelClassInstance& li : live)
            if (!li.removed && li.entity == hit) return hit;
        hit = parentOf(hit);
    }
    return 0;
}
```

### Step 2. New `sandbox/src/GraphClassPlacement.hpp` (header-only; Core + Formats only)

Namespace `aver::editor`. Contents:

```cpp
struct GraphClassDecl {
    std::string className;
    std::string parent = "Actor";   // CLASS slot 2 unless it is key=value (OcGraphParser.cs:329-334)
    std::string path;               // as enumerated
    std::string relPath;            // content-relative, '/' separators
    u32  componentCount = 0;        // COMP records, for the picker tooltip
    bool duplicate = false;         // another file declares the same className
};

// Top-level record scan, no full parse (CLASS is unmodelled in C++, F12). Mirrors GameApp.cpp:318-353 and the
// C# rules: lines trimmed, '#' lines skipped, record names case-insensitive, DOMAIN absent or "gameplay" only,
// a second CLASS record or a CLASS with no name -> false (the C# parser rejects both, OcGraphParser.cs:289-298).
bool parseGraphClassDecl(std::string_view text, GraphClassDecl& out);

// Every *.ocgraph under contentDir (extension compared case-insensitively) that parseGraphClassDecl accepts,
// sorted by className then path; `duplicate` set on all but the first of a repeated name.
std::vector<GraphClassDecl> scanGraphClasses(const std::string& contentDir);

// asset "none" (never read for a class placement, docs/VISUAL_SCRIPTING.md:308-311), className set, scale 1,
// collide true, snapToGround false.
fmt::OcWorldPlacement makeClassPlacement(const std::string& className, const Vec3& pos, f64 yawDeg);

struct ClassRoleBits { bool gameMode = false, controller = false, gameInstance = false, abstractClass = false; };
// nullptr when placeable, else the status text to show.
const char* classPlaceRefusal(const ClassRoleBits& b);

inline bool isGraphAssetExt(std::string_view lowerExt) { return lowerExt == ".ocgraph"; }

// Editor marker for a class root: three world-axis segments through `c`, +-half long. Colour differs when selected.
struct MarkerSeg { Vec3 a, b; f32 rgb[3]; };
void classMarkerSegments(const Vec3& c, f32 half, bool selected, std::vector<MarkerSeg>& out);
```
`classPlaceRefusal` texts: GameMode "GameMode classes are chosen in World Settings, not placed", controller "a
PlayerController is spawned by Play, not placed", gameInstance likewise, abstract "'X' is abstract and cannot be
placed" (caller formats the name).

### Step 3. `sandbox/src/SandboxApp.hpp`

Near the `ClassInstance` alias (`:4175-4180`), inside `#if AVER_MODULE_FRAMEWORK`:

```cpp
std::vector<fmt::OcWorldPlacement> classAuthored_;      // every class placement of the open level (3.1)
struct PlayClassSaved { usize slot; EditId id; Transform xf; };
std::vector<PlayClassSaved> playClassSnapshot_;        // class roots at Play start, for the Stop respawn
std::vector<editor::GraphClassDecl> classPickList_;
bool classPickDirty_ = true;                           // rescan when the Add popup opens
```
Methods (declare beside `spawnClassPlacements`, `:4070-4072`; same guard):

```cpp
scene::Entity spawnClassSlot(usize slot, const Transform* worldXf);
scene::Entity placeClassInstance(fmt::OcWorldPlacement p, std::string* why);
void dropGraphClass(const std::string& fullPath, f32 screenX, f32 screenY);
void drawGraphActorMenu();                 // body of the Add submenu
int  classSlotOfEntity(scene::Entity e) const;      // index into levelClassInstances_, -1 if not a live root
scene::Entity classRootOf(scene::Entity hit) const; // hit's class root or kInvalidEntity
void tagClassEdit(EditCmd& c, scene::Entity e);
void respawnClassFromEdit(const EditCmd& c);
void respawnLostClassInstances();
bool classPartsBounds(scene::Entity root, Vec3& lo, Vec3& hi) const;  // world box of the root's mesh parts
```
`EditCmd` (`:2009-2108`), outside any module guard (plain types): `std::string className;` and `i32 classSlot = -1;`
with the comment "non-empty className: Create/Destroy of a graph-class actor, replayed by respawning the class, not
rebuilt from `snap`". Add `rhi::LineHandle classMarkerMesh_ = 0;` next to `motionDebugMesh_` (`:3144`).

### Step 4. `sandbox/src/SandboxLevelLoad.cpp`

a. **`onLevelInstantiated`** (`:381`, at the comment `:487-489`): `classAuthored_.clear(); for (const auto& p :
   loaded.world.placements) if (!p.className.empty()) classAuthored_.push_back(p);` (same filter as
   `GameLevel.cpp:316-317`).

b. **Extract `spawnClassSlot`** from the loop body of `spawnClassPlacements` (`:667-697`), unchanged apart from
   reading `classAuthored_[slot]` and the optional override:
   * `worldXf` null: existing computation including `snapToGround` via `landscape_.groundHeightAt` (`:674-682`).
     If an entry for `slot` already exists with a valid, non-pending entity and `!removed`, return it (makes the
     retry call at `SandboxApp.cpp:1061` idempotent).
   * `worldXf` set: `pos3/quat4/scale3` straight from it, no ground query (undo and respawn already hold a
     resolved pose; adding ground again would climb).
   * On success: reuse the `levelClassInstances_` entry whose `placement == slot` (set `entity`, `removed=false`)
     or push `{slot, e}`.
   `spawnClassPlacements` becomes `for slot in 0..classAuthored_.size(): spawnClassSlot(slot, nullptr)` plus the
   existing summary log (`:698-702`).

c. **`placeClassInstance`**, in this order (each refusal sets `cbStatus_`, warns, returns `kInvalidEntity`):
   1. `anyPlayActive()` -> "Stop Play before placing an actor".
   2. `!(hideEditorScene_ || !levelPath_.empty())` -> the existing text "Load a level (or hide the editor scene)
      before dropping assets" (`SandboxViewport.cpp:1147,1189`).
   3. `#if AVER_MODULE_SCRIPTING`: `!scripts_.ready()` -> "Scripting is not ready; use Tools > Reload Scripts"
      (wording of `SandboxProject.cpp:243`); else `scripts_.declareGraphClasses(project_.contentDir())`. Re-declaring
      here is what makes a graph written or edited after project open (the starter graph, `GraphEditor.cpp:3873-3881`)
      placeable and keeps COMP data in step with the graph file `DispBind` reloads (`HostBridge.cs:1386-1398`).
   4. `c = aver_fw_class_find(name)`; 0 -> "class 'X' is not declared: its graph did not parse, or no .ocgraph under
      Content declares it (see the log)".
   5. `ClassRoleBits` from `aver_fw_class_get_flags(c)` (`AVER_FW_CLASS_GAME_MODE/CONTROLLER/GAME_INSTANCE/ABSTRACT`,
      `framework_abi.h:87-94`); `classPlaceRefusal`.
   6. `slot = classAuthored_.size(); classAuthored_.push_back(p); e = spawnClassSlot(slot, nullptr)`; on failure
      `pop_back` and refuse.
   7. `multiSetSingle(e); sel_ = kSelScene; selEntity_ = e;`
   8. `EditCmd c2 = describeEntity(e); c2.kind = Create; c2.className = p.className; c2.classSlot = slot;
      pushEdit(std::move(c2)); markLevelUnsaved(); cbStatus_ = "Placed " + p.className;` plus one `AVER_INFO`.
   The label is not set: loaded class instances show their class name in the Outliner (the entity is named with it,
   `:690`), so new ones match.

d. **Helpers:** `classSlotOfEntity` (linear find over non-removed entries), `classRootOf(hit)` (calls the Step 1
   template with `parentOf = [&](int32_t x){ return int32_t(w.parent(scene::Entity(x))); }`), `tagClassEdit`:
   ```cpp
   const int i = classSlotOfEntity(e); if (i < 0) return;
   c.className = classAuthored_[levelClassInstances_[i].placement].className;
   c.classSlot = static_cast<i32>(levelClassInstances_[i].placement);
   c.subtree.clear();   // the class spawn rebuilds its parts; replaying captured ones would double them
   ```
   `respawnClassFromEdit(c)`: bounds-check `classSlot`; build a `Transform` from `c.after` (`pos`, `quatFromEulerDeg(rotDeg)`,
   `scale`); `e = spawnClassSlot(slot, &xf)`; on success `rebindEdit(c.id, e)` (`SandboxSelection.cpp:300-305`),
   `multiSetSingle(e)`, `sel_ = kSelScene; selEntity_ = e`.

e. **`unloadLevel`** (`:893-909`): next to `levelClassInstances_.clear()` also `classAuthored_.clear(); playClassSnapshot_.clear();`.

### Step 5. `sandbox/src/SandboxLevelEdit.cpp:728-740`

Pass `classAuthored_` instead of `level_.classPlacements()` to `editor::appendClassPlacements`. Nothing else changes:
a session-added placement has an instance entry, so it takes the live pose branch; the snap rule
(`LevelClassSave.hpp:56-60`) still applies to placements that carry `snap`.

### Step 6. Entry points

* `SandboxContentBrowser.cpp:2256-2262` `isPlaceableAssetExt`: `#if AVER_MODULE_FRAMEWORK if
  (editor::isGraphAssetExt(ext)) return true; #endif`. Update the wording at `SandboxApp.hpp:843`,
  `SandboxShell.cpp:2397` and `SandboxViewport.cpp:1181-1185` to include `.ocgraph`.
* `SandboxViewport.cpp:1109` `spawnFromAssetDrop`: before the `.ocworld` branch, `#if AVER_MODULE_FRAMEWORK if
  (isGraphAssetExt(ext)) { dropGraphClass(full, screenX, screenY); return; } #endif`. `dropGraphClass` (same file):
  read the file; `parseGraphClassDecl` false -> `cbStatus_ = "'<file>' declares no CLASS (or is not a gameplay
  graph), so it cannot be placed. Add a 'CLASS AN_Name Actor' line."`; `at = dropWorldPoint(...)` with the
  non-finite check and `snapMove_` snap copied from the `.ocworld` branch (`:1155-1160`); then
  `placeClassInstance(makeClassPlacement(d.className, at, 0.0), &why)`. No rest-lift: bounds of a class are not known
  before it spawns, and the origin of a character is its feet.
* `SandboxShell.cpp:2250-2279` Add popup: set `classPickDirty_ = true` when the Add button opens it (`:2250`); after
  the `Decal` item (`:2269-2270`) and before `Post Process Volume`:
  ```cpp
  #if AVER_MODULE_FRAMEWORK
  if (ImGui::BeginMenu("Actor From Graph")) { drawGraphActorMenu(); ImGui::EndMenu(); }
  uiReg_.track("toolbar.add.actorFromGraph");
  #endif
  ```
  `drawGraphActorMenu` (put it in `SandboxViewport.cpp` beside `spawnLightAtCamera`): rescan if dirty
  (`classPickList_ = editor::scanGraphClasses(project_.contentDir())`); one `MenuItem(className)` per entry with the
  parent dimmed on the right and a tooltip (relative path, parent, N components, "declared by more than one graph"
  when `duplicate`); entries disabled with a tooltip when Play is active, no level is open, or the class is already
  declared as GameMode/Controller/GameInstance/abstract (`classPlaceRefusal`); an empty list shows
  `TextDisabled("No graph under Content declares a CLASS")`; a final `Separator` + `MenuItem("Refresh")`. A click
  places at `camPos_ + camForward() * kAddDistance` (snap as `spawnLightAtCamera`, `:809-810`).
* `SandboxPanels.cpp:1746`, after `ImGui::TextDisabled("#%u  %s", ...)`: when `classSlotOfEntity(selEntity_) >= 0`
  show `TextDisabled("Graph class: %s", className)` and a `SmallButton("Open Graph")`
  (`uiReg_.track("details.graphClass.open")`) that finds the file via `scanGraphClasses` and calls
  `assetEditors_.open(path)` (as `SandboxSelection.cpp:565`).

### Step 7. Selection and edit operations

* `pick()` (`SandboxViewport.cpp:2331`): just before the level-instance redirect (`:2470`), map the winner to its
  class root: `if (bestEnt != kInvalidId) if (auto r = classRootOf(scene::Entity(bestEnt)); r != scene::kInvalidEntity) bestEnt = r;`.
  Also, only when `!noEditorChrome_ && !anyPlayActive()` (the same gate the marker is drawn under), test each live
  root as a box of half-size `kClassMarkerHalf` (new constant, 25 cm) centred on its world position with the
  existing `rayAabb(ro - pos, rd, {-h,-h,-h}, {h,h,h}, t)` (call shape at `:2405-2408`) and keep it when `t > 0 && t < bestT`
  (same `best = -1`, `bestEnt = root` assignment as the mesh path `:2433-2434`).
* `destroyEntity()` (`SandboxSelection.cpp:1118`): compute `classIdx = classSlotOfEntity(e)` before `collectSubtree`;
  replace the bare `w.destroy(e);` (`:1128`) with
  ```cpp
  #if AVER_MODULE_FRAMEWORK
  if (classIdx >= 0) {
      aver_fw_destroy(static_cast<int32_t>(e));   // managed unbind frees the GraphHost; destroys the subtree like w.destroy
      levelClassInstances_[classIdx].entity  = 0;
      levelClassInstances_[classIdx].removed = true;
  } else
  #endif
      w.destroy(e);
  ```
  This one change serves Delete, undo of Create, redo of Destroy and the MCP undo.
* `deleteSelection()` (`SandboxViewport.cpp:1963`): call `tagClassEdit(c, v)` right after each `captureSubtree`
  (`:1988` multi branch, `:2021` single branch).
* `recreateFrom()` (`SandboxSelection.cpp:795`): first lines
  `#if AVER_MODULE_FRAMEWORK if (!c.className.empty()) { respawnClassFromEdit(c); return; } #endif`. `undo()`/`redo()`
  need no change: their `Create`/`Destroy` cases already route through `destroyEntity`/`recreateFrom`
  (`:1243-1245`, `:1312-1313`).
* `copySelection`, `pasteClipboard` is untouched (it cannot receive a class root), `duplicateSelection` and the
  copy path: when any selected entity has `classSlotOfEntity >= 0`, set `cbStatus_ = "Duplicate/Copy is not supported for
  graph-class actors yet: drag the graph in again"` and skip that entity (`SandboxViewport.cpp:2047`, `:2167`; both
  already skip entities in loops, add the test beside the `ancestorSelected` checks `:2062-2064`, `:2184-2187`).
* Framing: `selectedRadius()` (`SandboxSelection.cpp:1387-1416`): when the root has no `CMeshRenderer`, use
  `classPartsBounds` -> `max(1, dist(lo,hi) * 0.5)` before the `max(1, s)` fallback (`:1415`). `selectionBounds()`
  (`SandboxViewport.cpp:2497-2528`) uses the same union box instead of the scale fallback. `classPartsBounds`:
  `collectSubtree(w, root, v)`, skip index 0, for every part with a visible `CMeshRenderer` and a valid box, transform
  the 8 corners by `w.worldMatrix(part)` (pattern `SandboxRender.cpp:582-588`) and union; false if none.

### Step 8. Marker (`SandboxRender.cpp`, beside the motion overlay `:1975-1984`)

Per frame: destroy `classMarkerMesh_`; if `!noEditorChrome_ && !anyPlayActive()` collect
`classMarkerSegments(worldPos, kClassMarkerHalf, selected, segs)` for each live root, convert to `rhi::LineVertex`
(`RHI.hpp:60`: `px,py,pz,r,g,b`), `createLineMesh`, `drawLines(mesh, identity)` exactly as the motion block does.
World-space vertices, so identity transform. No toggle.

### Step 9. Play and Stop (`SandboxPlay.cpp`)

* `capturePlayWorld()` (`:101-141`), after the `levelEntities_` loop: for each non-removed instance with a valid
  entity push `{placement, editIdFor(e), w.localTransform(e)}` to `playClassSnapshot_`, and also push
  `{e, localTransform, true}` to `playWorldSnapshot_` so a surviving root is put back like any placement (F8).
* `stopPlay()` (`:624`): immediately after `restorePlayWorld();` (`:702`) call `respawnLostClassInstances();` (the call is the only edit in `SandboxPlay.cpp`; the body below is
  defined in `SandboxLevelLoad.cpp` with the other class helpers):
  ```cpp
  for (const PlayClassSaved& s : playClassSnapshot_) {
      auto it = find_if(levelClassInstances_, [&](auto& ci){ return ci.placement == s.slot && !ci.removed; });
      if (it == end) continue;
      const auto old = scene::Entity(it->entity);
      if (w.valid(old) && !w.destroyPending(old)) continue;     // stand-in Play never ran end_play: it survived
      const Transform xf = s.xf;
      if (auto e = spawnClassSlot(s.slot, &xf); e != kInvalidEntity) {
          rebindEdit(s.id, e);
          if (selEntity_ == old) selEntity_ = e;
      }
  }
  playClassSnapshot_.clear();
  ```
  `destroyPending` check matters: `aver_fw_end_play` destroys with a pending bit that `valid()` does not reflect
  until a flush (the same trap `SandboxSelfTests.cpp:1702-1704` documents).
* `docs/VISUAL_SCRIPTING.md:276-283` stays true; add one line that Stop recreates class instances at their pre-Play pose.

### Step 10. MCP `level place` (`SandboxMcp.cpp:651-845`)

Recommended, because it lets an agent place without the GUI and lets the in-process test exercise the same path.
* `:683-690`: replace the refusal with a pass-through (class placements stay in `doc.placements`;
  `world::instantiate` makes no entity for them, `LevelInstance.cpp:304`).
* `:710-711` `unloaded()`: skip placements with a `className` (their asset is `none`, which would be reported as a
  missing mesh); likewise skip them in the material-binding loop `:765-767`.
* `:788-792`: do not fail "created no entity" when `doc` held class placements.
* After the bookkeeping loop `:799-820`, for each placement with a `className`: `e = placeClassInstance(p, &why)`;
  on failure return false with `why` (earlier mesh placements remain placed and undoable; say so in the message);
  on success `idFor[i] = e`. `placed` in the result JSON counts them; the `Create` edits are pushed by
  `placeClassInstance`, so the `:826-830` loop must not push for them (it already iterates only `inst.entities`).

### Step 11. `scripting/csharp/Aver.Scripting.Bridge/HostBridge.cs:742-746`

Warn only when the superseded file differs:
`if (s_graphClasses.TryGetValue(classKey, out GraphClassInfo prior) && !string.Equals(prior.Path, path, StringComparison.OrdinalIgnoreCase))`.
Without this, every placement (Step 4c.3) logs a false "declared by more than one graph" per class. No C# test
project covers `HostBridge` (UNVERIFIED: none listed under `scripting/csharp/*.Tests` references it); verify by
log (check 6.3-9). `Aver.Graph.Tests` is not affected: no `Aver.Graph` file changes.

### Step 12. Docs

`docs/VISUAL_SCRIPTING.md:332-336`: replace "Authoring one today is a text edit" with the drop / `Add > Actor From
Graph` description and the limits in 3.3. Keep the §8 paragraph at `:458-470` and append the delete and Stop fixes.

## 5. Behavioural details the implementer must not get wrong

* **Pairing is by recorded index**; `classAuthored_` indices are never reused or compacted during a level's life
  (tombstones stay), so `EditCmd::classSlot` stays valid for the whole undo history.
* **World pose for respawn comes from the edit** (`c.after`) or the Play snapshot, never from the authored
  placement, or a `snap` placement would add the ground height twice (`LevelClassSave.hpp:18-21`).
* **`aver_fw_destroy` is required** for class roots (`FrameworkAbi.cpp:593-623`): it is the only path that runs the
  managed unbind. `world().destroy` leaves the graph registered.
* **Re-declaring while instances live is safe by design** (`framework_abi.h:102-104`: redeclare rewrites the row in
  place; instances keep what they were spawned with), but is refused during Play (Step 4c.1).
* **No undo entry from `respawnLostClassInstances`**: Stop is not a user edit.
* **Dirty state**: `pushEdit` + `markLevelUnsaved()` like `spawnLightAtCamera` (`SandboxViewport.cpp:823-828`).

## 6. Tests and acceptance

### 6.1 New `tests/editor/src/GraphClassPlacementTest.cpp`

Style of `DropPlacementTest.cpp` (`check()` helper, `exitCode(ExitCode::...)`). Registered in
`tests/editor/CMakeLists.txt` after the `GraphAssetPresentationTest` block (`:189-198`):

```cmake
# GraphClassPlacementTest -- sandbox/src/GraphClassPlacement.hpp + the class-root/tombstone rules in
# LevelClassSave.hpp. Header-only; the ImGui menu, drop and gizmo are covered by --class-place-test.
add_executable(GraphClassPlacementTest src/GraphClassPlacementTest.cpp)
target_include_directories(GraphClassPlacementTest PRIVATE ${CMAKE_SOURCE_DIR}/sandbox/src)
target_compile_definitions(GraphClassPlacementTest PRIVATE AVER_REPO_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(GraphClassPlacementTest PRIVATE Aver.Formats Aver.World Aver.Core Aver.Platform)
set_target_properties(GraphClassPlacementTest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
```
(`Aver.World` for the `LevelTransform.hpp` include, as `GraphEditorLoadSaveTest` does, `CMakeLists.txt:182-186`.)

Assertions (each with a message saying what bug it catches):
1. `parseGraphClassDecl`: `CLASS AN_Foo` -> parent `Actor`; `CLASS AN_Foo Character view=firstperson` -> `Character`;
   `CLASS AN_Foo mesh=Meshes/x.ocmesh` -> parent stays `Actor` (key=value in slot 2 is not a parent); lowercase
   `class`; CRLF input; a `# CLASS AN_Nope` comment is ignored; leading spaces; no CLASS -> false; `CLASS` alone -> false;
   two CLASS records -> false; `DOMAIN material` + CLASS -> false; `DOMAIN gameplay` -> true; `DOMAIN vfx` -> false;
   three `COMP` lines -> `componentCount == 3`.
2. `scanGraphClasses` over a temp tree: nested folder, `.OCGRAPH` upper-case extension, a material graph, a graph
   with no CLASS, two files declaring one name (`duplicate` on the second only), result sorted by name, `relPath`
   uses `/`. A missing directory returns empty, no throw.
3. Real files: `AVER_REPO_ROOT/templates/FirstPerson/Content/Scripts/AN_FPCharacter.ocgraph` -> `AN_FPCharacter`,
   parent `Character`, `componentCount >= 1`; `AN_FPRules.ocgraph` -> parent `GameMode` (the picker must then be
   able to refuse it).
4. `makeClassPlacement` through `fmt::writeOcworld` / `fmt::parseOcworld` (`OcWorld.cpp:880,229`): the text contains
   `PLACE none` and `class AN_Foo` (assert tokens, not number spelling; `num()` is `%.6g`, so 100 prints `100`, OcWorld.cpp:25-29), and the
   parsed placement has the same className, position, yaw and scale 1.
5. `classPlaceRefusal`: each role bit refused with non-empty text, none -> `nullptr`.
6. `isGraphAssetExt`: `.ocgraph` true; `.ocmesh`, `.ocgraph.bak`, empty false.
7. `classRootOf` with a fake parent map: part -> root; root -> itself; grandchild -> root; unrelated -> 0; a removed
   instance is not matched (its parts return 0); a parent cycle terminates; `hit == 0` -> 0.
8. `appendClassPlacements` (extend `GraphEditorLoadSaveTest.cpp` after `:1523`, or here): a `removed` instance is
   omitted while an authored placement with no instance is still written as read; a session-added placement (index
   beyond the loaded ones) is written at its live pose; removed + `snap` neighbour keeps the z-offset rule.
9. `classMarkerSegments`: three segments, endpoints at `c +- half` on each axis, selected colour differs.

### 6.2 In-process `--class-place-test` against `test-content/ClassPlace`

Wiring copies `--undo-test`: parse `SandboxMain.cpp:686-689` (declare the local with the others at `:318`, apply at
`:1077`), `setClassPlaceTestAuto` (`SandboxApp.hpp:1438`, member `:1876`, definition `SandboxSelfTests.cpp:107`),
tick at `SandboxApp.cpp:2292-2294`, body `runClassPlaceTest(Engine&)` in `SandboxSelfTests.cpp` ending
`std::exit(failures == 0 ? 0 : 1)` like `:2180-2181`. Headed, never `--headless` (it lives in the ImGui block with the
undo test). Run: `Sandbox.exe --project test-content/ClassPlace/Game.ocproject --class-place-test`.

New fixture (content only, no engine feature):
* `test-content/ClassPlace/Game.ocproject`: `OCPROJECT 1`, `NAME ClassPlace`, `ENGINE Aver 0.6.0-1` (the engine
  version is `project(AverEngine VERSION 0.6.0)` plus `AVER_VERSION_SUFFIX "-1"`, CMakeLists.txt:2,11, and every `CREATEDWITH` stamp is the full
  `0.6.0-1`, as in the owner's `PTTest.ocproject`; the sibling `test-content` projects carry older strings, and a project from an older series raises an
  upgrade prompt at open that nothing can answer under a frame limit, SandboxApp.cpp:677-690), `CREATEDWITH 0.6.0-1`, `CONTENT Content`,
  `STARTMAP Maps/ClassPlace.ocworld`. (If the branch's version moves before this lands, copy the two stamps from a project the editor wrote.)
* `Content/Scripts/CompProbe.ocgraph`:
  ```
  OCGRAPH 1
  DOMAIN gameplay
  NAME CompProbe
  CLASS AN_CompProbe Actor
  COMP body Mesh mesh=Meshes/cube.ocmesh scale=50,50,50
  COMP tip Scene parent=body pos=0,0,1
  ```
  (`Meshes/cube.ocmesh` is the editor's built-in unit cube, scale = half-extent in cm, `SandboxViewport.cpp:745,871,906`.
  No `ENTRY`, so it does not tick. UNVERIFIED that a graph with only CLASS/COMP and no nodes loads in
  `GraphHost.Load`; if it does not, add the smallest valid node block copied from `IdleMotion.ocgraph`.)
* `Content/Maps/ClassPlace.ocworld`: `OCWORLD 1`, `NAME ClassPlace`, one line `PLACE none 0 0 0 0 0 0 1 class AN_CompProbe`.

Checks, in order (each `check(cond, "...")` as `runUndoTest`):
1. After load: one live instance; its root has two children; the `body` child has a visible `CMeshRenderer` whose
   `mesh == fnv1a64("Meshes/cube.ocmesh")` (**the edit-mode preview**).
2. `classRootOf(body) == root`, `classRootOf(root) == root`, `classRootOf(<plain cube>) == invalid`.
3. `selEntity_ = root`: `selectedRadius() > 40` (not the 1-cm fallback).
4. `placeClassInstance(makeClassPlacement("AN_CompProbe", {500,0,0}, 90))`: valid entity, 2 live instances,
   `classAuthored_.size() == 2`, top of `undoStack_` is `Create` with `className` set, selection is the new root,
   its parts exist.
5. Move the new root with `setLocalTransform`, `saveLevel(tmp)`, `fmt::loadOcworld(tmp)`: two class placements, the
   second at the moved position and yaw ~90.
6. `selEntity_ = e2; deleteSelection(); w.flush()`: `!valid(e2)`, its parts gone, `instances` entry `removed`;
   save -> one class placement. `undo(); w.flush()`: a valid root with two children again, `entityForEdit(id)` is the
   new root, save -> two. `redo()`: gone, save -> one. (This is F5 and F6.)
7. Place a third, `undo()`: gone, save -> one; `redo()`: back, save -> two (Create undo/redo).
8. Stop respawn: `capturePlayWorld(); aver_fw_destroy(rootEntity); w.flush(); restorePlayWorld();
   respawnLostClassInstances();` (emulates the sweep at `FrameworkAbi.cpp:1030-1036`): the instance is valid again
   at its captured pose, same slot, and a `Transform` edit made before still resolves through `entityForEdit`.
9. `selEntity_ = root; duplicateSelection()`: entity count unchanged and `cbStatus_` mentions graph-class actors.
10. `mcpLevelPlace("PLACE none 100 0 0 0 0 0 1 class AN_CompProbe", r, w)` returns true and one more instance exists;
    with `class AN_DoesNotExist` it returns false with the "not declared" text and places nothing.
11. Role refusal: declare nothing extra; call `classPlaceRefusal` through `placeClassInstance` with a name whose
    class has the GameMode flag only if the fixture can declare one cheaply; otherwise this is covered by 6.1-5.

### 6.3 Manual acceptance (owner, PTTest, on a COPY of a level)

Use `File > Save As` to make `PlaceClassTest.ocworld` first; do not touch the owner's levels.
1. Open it. Content Browser -> `Scripts` -> drag `AN_FPTarget.ocgraph` onto the ground: a sphere appears under the
   cursor (CLASS `mesh=Meshes/sphere.ocmesh`). Outliner shows `AN_FPTarget`. Status bar says "Placed AN_FPTarget".
2. Drag `AN_FPCharacter.ocgraph`: the Character body mesh (COMP `body`, `AN_FPCharacter.ocgraph:116`) is visible at
   rest pose, feet at the cursor. **If it is invisible this is the finding to report**, UNVERIFIED either way; first
   suspects: mesh not in `content_` (pick also needs `meshFor`, `SandboxViewport.cpp:2371`) or the skeleton id not in
   the content index (`GameContent.cpp:137-140`), though an unresolved skeleton still draws at rest
   (`SkinnedScene.cpp:174-176`).
3. Click the body: the root (`AN_FPCharacter`) is selected, not the mesh part; Details shows "Graph class:
   AN_FPCharacter" and Open Graph opens the graph tab; the gizmo sits at the feet. Press `F`: the camera frames
   the whole body, not a point at its feet.
4. Move it with the gizmo; `Ctrl+S`; open the `.ocworld` in a text editor: a line
   `PLACE none <x> <y> <z> ... class AN_FPCharacter` at the new position.
5. `Delete`: gone. `Ctrl+Z`: back, body visible, same place. `Ctrl+Shift+Z` (or the repo's redo chord): gone. Save:
   the line is absent. Undo once more and save: present.
6. `File > Open Level` the saved file: both placed actors present, at the moved positions.
7. Press Play, then Stop: both actors still present at their pre-Play pose (F7), and a second Play still runs.
8. `Add > Actor From Graph`: lists `AN_FPCharacter`, `AN_FPController`, `AN_FPGun`, `AN_FPRules`, `AN_FPTarget`;
   `AN_FPRules` (GameMode) and `AN_FPController` are disabled with a tooltip; `AN_FPGun` places at the camera front.
9. Check the log after several placements: no repeated "declared by more than one graph" lines (Step 11).
10. Group-select a placed actor and a cube, `Ctrl+D`: the cube duplicates, the actor does not, status explains.

## 7. Risks and UNVERIFIED list

* **UNVERIFIED, high value:** the whole of F7 (Stop destroys level class instances) is from reading
  `FrameworkAbi.cpp:1017-1040`; no run was made. Acceptance 6.3-7 settles it. If it turns out the sweep spares them,
  Step 9 becomes a no-op guarded by `valid()`; nothing else depends on it.
* UNVERIFIED: the edit-mode appearance of a `SkeletalMesh` COMP on the real PTTest assets (6.3-2).
* UNVERIFIED: `DeclareGraphClasses` cost over a large Content tree (it walks every `.ocgraph`, `HostBridge.cs:672`);
  PTTest has five graphs under `Content/Scripts`, so expected to be small, but Caldera/Sponza folders are large.
  Only placement triggers it, not the menu.
* Placing a pawn-class actor (`AN_FPCharacter`) means Play has two characters when the GameMode also spawns one;
  that is correct behaviour, but it will look odd to a first-time user.
* A placed `Character` class runs its graph in edit mode (F2); documented behaviour, not new.
* Sequences or other systems holding raw entity handles of class instances do not follow a Stop respawn
  (`seqEditor_` binds by entity, `SandboxPlay.cpp:705`); out of scope.
* Class placements inside a level instance (`LevelInstances`) are the class system's and untouched
  (`SandboxLevelInstances.cpp:115`).
* Not done here, on purpose: preserving `VAR` state across respawn, duplicating class actors, a `Show` toggle for the
  marker, editing COMP parts in place, an Animator on the same entity as a SkeletalMesh (a COMP-format decision the
  `test-content/ControlRigDemo/Game.ocproject` header already records as open; the Add/drop flow places whatever the
  graph declares, so it benefits automatically when that lands).

## 8. File checklist

`Task` is the owner in `TASKS.md` (T3 = Add Component plus every shared `sandbox/src` file; T4 = this feature's own files). Where T3 owns a file this
document edits, T3 applies the hunk named here, verbatim, so the file has one author; T4 implements the bodies the hunk calls.

| File | Change | Task |
|------|--------|------|
| `sandbox/src/LevelClassSave.hpp` | `removed`, skip in `appendClassPlacements`, `classRootOf` | T4 |
| `sandbox/src/GraphClassPlacement.hpp` | new | T4 |
| `sandbox/src/SandboxApp.hpp` | members, methods, `EditCmd::className/classSlot`, `classMarkerMesh_`, self-test hooks (Step 3, declarations only; the bodies are T4's) | T3 |
| `sandbox/src/SandboxLevelLoad.cpp` | `onLevelInstantiated`, `spawnClassSlot`, `placeClassInstance`, helpers incl. `respawnLostClassInstances`, `respawnClassFromEdit`, `classPartsBounds`, `unloadLevel` | T4 |
| `sandbox/src/SandboxLevelEdit.cpp` | pass `classAuthored_` to the save (Step 5) | T3 |
| `sandbox/src/SandboxContentBrowser.cpp` | `isPlaceableAssetExt` | T4 |
| `sandbox/src/SandboxViewport.cpp` | drop branch, `dropGraphClass`, `drawGraphActorMenu`, `pick`, `deleteSelection`, copy/duplicate guard, `selectionBounds` | T4 |
| `sandbox/src/SandboxShell.cpp` | Add submenu, dirty flag, message text | T4 |
| `sandbox/src/SandboxSelection.cpp` | `destroyEntity`, `recreateFrom`, `selectedRadius` (Step 7 hunks) | T3 |
| `sandbox/src/SandboxPanels.cpp` | Details class line (Step 6, last bullet) | T3 |
| `sandbox/src/SandboxRender.cpp` | marker draw | T4 |
| `sandbox/src/SandboxPlay.cpp` | capture + the `respawnLostClassInstances()` call after Stop (Step 9) | T3 |
| `sandbox/src/SandboxMcp.cpp` | accept class lines | T4 |
| `sandbox/src/SandboxMain.cpp`, `SandboxSelfTests.cpp` | `--class-place-test` parse and `runClassPlaceTest` body | T4 |
| `sandbox/src/SandboxApp.cpp` | the one-line `--class-place-test` tick beside `:2294` | T3 |
| `scripting/csharp/Aver.Scripting.Bridge/HostBridge.cs` | same-path redeclare is silent | T4 |
| `tests/editor/src/GraphClassPlacementTest.cpp`, `GraphEditorLoadSaveTest.cpp` | tests | T4 |
| `tests/editor/CMakeLists.txt` | the `GraphClassPlacementTest` block of 6.1 (T3 adds it verbatim; the source file is T4's) | T3 |
| `test-content/ClassPlace/**` | fixture | T4 |
| `docs/VISUAL_SCRIPTING.md` | sections 5, 6, 8 | T4 |

No new `.cpp` in `sandbox/` (sources are listed by hand in `sandbox/CMakeLists.txt`); all new behaviour lives in
existing files. No engine module, Runtime or third-party change. No shader or `modules/render*` file is touched, so the RX 7800 XT
register-pressure/TDR rule (shader changes) does not apply to this feature.
