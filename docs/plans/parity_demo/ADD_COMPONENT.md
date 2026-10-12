# Add Component for the parity features (Skeletal Mesh, Animator, Root Motion, Motion Matching, IK Rig, Control Rig, AI Brain, Smart Object)

Owner request 2026-10-12: make the Unreal-parity features testable without writing code. This part designs the
**Details panel > Add Component** entries, their inspectors, undo, and (the hard part) how they are saved in a
`.ocworld` and rebuilt in Play and in the packaged game.

Conventions: `file:line` cites existing code read for this plan. **UNVERIFIED** marks anything not confirmed by
reading code. Nothing here was built or run.

**Status: FROZEN 2026-10-12 by the reviewer pass** (cross-checked against PLACE_CLASS.md and DEMO_CONTENT.md; the cites in sections 1-6 were
re-read against the tree at `f738c7a2`). Implementation order and file ownership are in `docs/plans/parity_demo/TASKS.md`; the
work-breakdown table in section 11 is superseded by it.

## 0.0 Names every document must use (authoritative)

One row per component. `PLACE_CLASS.md` and `DEMO_CONTENT.md` quote these spellings and no others.

| Menu label (Add / Remove Component) | `COMPONENT` record type | Registered component name | Asset keys and extensions |
|---|---|---|---|
| Skeletal Mesh | `SkeletalMesh` | built-in `kComponentSkeletalMesh` (9) | `skeleton` `.ocskel` |
| Animator | `Animator` | built-in `kComponentAnimator` (10) | `clip` `.ocanim` (not object clips) |
| Root Motion | `RootMotion` | `"CRootMotion"` | none |
| Motion Matching | `MotionMatcher` | `"CMotionMatcher"` | `database` `.ocmotion` |
| Control Rig | `ControlRig` | `"CControlRig"` | `rig` `.ocrig` |
| IK Rig | `IkRig` | `"CIkRig"` | `asset` `.ocikrig` |
| AI Brain | `Brain` | `"CSynapseBehavior"` | `asset` `.ocbt` or `.ocst` |
| Smart Object | `SmartObject` | `"CSmartObject"` | `asset` `.ocso` |
| (Soft Body, optional 4.9) | `SoftBody` | built-in `kComponentSoftBody` (13) | none |

Anything else the demo calls a "Motion Matcher" is this document's **Motion Matching** menu entry and `MotionMatcher` record. There is no
"Synapse Agent" entry (DEMO_CONTENT gap G1; out of scope here). Components attach to **level placements** (mesh entities in the level) only:
a graph-class actor (PLACE_CLASS.md) is never a host, so on its root and on its COMP parts every entry is disabled with the reason
"Not a level placement" (that is `AttachContext::levelOwned == false`, 7.2). A `COMPONENT` line after a `PLACE none ... class X` line
parses and is preserved but is never applied (class placements have no placement entity, LevelInstance.cpp:304); the applier logs one warning.

## 0. Decisions (read this first)

1. **One data model for everything:** `fmt::OcPlacedComponent { type, key/value list }` hung on `OcWorldPlacement`.
   It is simultaneously the `.ocworld` record, the undo payload, and the input to the shared applier. The live
   component is the source of truth in the editor; save = capture from the live component, load = apply the record.
   No editor side maps (the `entityAnim_` pattern is deliberately not repeated; it needs per-feature undo plumbing).
2. **New level record `COMPONENT <Type> key=value ...`**, written on its own indented line directly after the
   `PLACE`/`CHILD` line it belongs to. An old reader skips it as an unknown record (OcWorld.hpp:6), so older editors
   open the level and lose only these components.
3. **Components attach to level placements (mesh entities) only.** Light and Decal are *not* placement components:
   each is its own bare entity with its own `LIGHT`/`DECAL` record (section 1.2). That model does not fit a character.
4. **One applier shared by both hosts**, in `Runtime` (`aver/game/LevelComponents.hpp/.cpp`), called from
   `GameLevel::load` (GameLevel.cpp:517) and from the placement-streaming load (LevelStreaming.cpp:250). The editor
   and the packaged game therefore cannot disagree. No host code change is needed on the load side.
5. **Four small general engine changes** (section 6): a `CMotionMatcher` input-source field, a root-motion
   "only while Play is live" gate, a string-field fix in editor Copy/Paste/Undo snapshots, and a LevelPack rule.
   None is specific to PTTest or any level.
6. Skeleton binding is expressed as a `SkeletalMesh` component record, because `PLACE` has no skeleton field
   (OcWorld.hpp:21-127) and the only other route is a graph class (GraphComponentTree.cs:122-134).

## 1. What exists today (findings)

### 1.1 The Add / Remove Component menus

- `Add Component` is a button and popup in `SandboxPanels.cpp:2482-2529`. Its list is a hand-written static array
  `kAddable[]` of `{u32 id, name, tooltip}` with exactly three entries: Soft Body, Light, Decal
  (SandboxPanels.cpp:2490-2498). The comment at :2488 explains the allow-list: the registry knows nothing about
  whether attaching a type from a menu is safe (CWorld/CLocal would corrupt an entity).
- Creating a component: `w.addComponent(sel, id)` returns **zero-filled storage with no constructor run**, so the
  handler writes defaults (`*c = scene::CSoftBody{}`, `editor::makeNewLight`, `editor::makeNewDecal`)
  (SandboxPanels.cpp:2503-2515).
- **Add is not undoable**; it only calls `markLevelUnsaved()` (SandboxPanels.cpp:2517-2520).
- `Remove Component` mirrors it with `kRemovable[]` (SandboxPanels.cpp:2539-2544) and **is undoable** through
  `removeComponentFromSelection` -> `EditCmd::Kind::RemoveComponent`, a byte-exact copy of the component
  (SandboxSelection.cpp:492-506, undo at :1253, redo at :1322, `restoreComponent` at :362).
- The ids in `kAddable` are the fixed built-in ids (`kComponentSoftBody = 13`, `kComponentLight = 7`,
  `kComponentDecal = 16`, Components.hpp:10-26). **The components this plan needs are different:**
  `CIkRig`, `CControlRig`, `CMotionMatcher`, `CRootMotion`, `CSynapseBehavior`, `CSmartObject` are **registered
  dynamically by name** (IkRigSystem.hpp:7, ControlRig.hpp:11-13, MotionMatchSystem.hpp:3-4, AnimSystem.hpp:62-63,
  SynapseBt.hpp:31, SynapseSmartObject.hpp:23). Their ids are only known after registration (`World::componentId(name)`,
  World.hpp:66) and differ between processes. So the menu needs name-based entries, not constants. Only
  `CSkeletalMesh` (id 9) and `CAnimator` (id 10) are built-ins.
- **Skeletal Mesh + Animator on a placed actor is not possible today.** No Add entry exists, `OcWorldPlacement`
  has no skeleton field, and the only editor code that creates a `CSkeletalMesh` is the self-test
  (SkinSceneTest.cpp:119-121). The existing "Animation" section even excludes skeletal meshes and its `anim` token
  only drives *object* clips (SandboxPanels.cpp:1959-1964; SandboxSelection.cpp:429-432).

### 1.2 How Light, Decal and Soft Body persist today

| Component | Persistence today | Evidence |
|---|---|---|
| Light | A **separate bare entity** with a `LIGHT` record (`OcLight`). Save walks the whole `CLight` pool and writes one record per light; load spawns one bare entity per record (`game::spawnLevelLights`), in both hosts. | OcWorld.hpp:538-557; SandboxLevelEdit.cpp:556-568; SandboxLevelLoad.cpp:392-393; GameApp.cpp:1012 |
| Decal | Same model: bare entity, `DECAL` record (`OcDecal`). | OcWorld.hpp:564-583; DecalLevelIo.hpp:80-91; SandboxLevelEdit.cpp:581-583; SandboxLevelLoad.cpp:403; GameApp.cpp:1010 |
| Soft Body | **Not persisted at all.** `CSoftBody` is referenced by no level load/save file (grep of `kComponentSoftBody`/`CSoftBody`: panel, renderer, scene, docs only). It is lost on reload. | SandboxPanels.cpp:2386; the hit list contains no `SandboxLevel*`/`OcWorld*` |

Consequences:
- A placement-save skips an entity with no mesh renderer that carries a Light or Decal (SandboxLevelEdit.cpp:413-414,
  598-601, 646-647) so they are not written twice. **A Light added to a *mesh* entity through Add Component is written
  both as a `PLACE` and as a `LIGHT` (the pool walk at :558-568 does not check for a mesh), so it reloads as two
  entities.** By reading, not run. This is an existing bug outside this plan; it is the reason the new components do
  not reuse the bare-entity model (see decision 3).
- Soft Body can ride the new `COMPONENT` record at near-zero cost (section 4.9, optional).

### 1.3 Other facts the design depends on

- A placement becomes an entity in `world::instantiate`: `world.create(p.asset, parent, xf)`, then `CMeshRenderer`,
  then `CAnimator` only for an `anim` object clip (LevelInstance.cpp:393-425). The loop collects `out.entities` and
  `out.placementIndex` (LevelInstance.cpp:432-433), which is exactly the entity-to-placement map a post-pass needs.
  `Aver.World` must stay free of Framework/Anim/Synapse (LevelInstance.cpp:304-313), so the applier cannot live there.
- `GameLevel::load` calls `world::instantiate` at GameLevel.cpp:277, or `streaming_.begin` for a streamed level, and then
  `hooks_.afterInstantiate` at :517. Placement streaming batches placements through `world::instantiate(sub, opt_)`
  and reports `(entities, placementIndex)` to `hooks_.afterLoad` (LevelStreaming.cpp:223-250).
- The editor's save rebuilds every placement from live components (`fillPlacementFromEntity`,
  SandboxLevelEdit.cpp:407-469, called from the loop at :641-697) and writes the result with `writeOcworld`
  (:743). Streamed levels use `buildStreamedPlacements` (:640; SandboxLevelStream.cpp:242).
- `World::addComponent` zero-fills with no constructor. Each of the new component headers says to attach through the
  system (`attachIk`, `ControlRigSystem::attach`, `MotionMatchSystem::attach`, `AnimSystem::attach`, `BtSystem::setBrain`)
  and never raw: IkRig `weight` would read 0 and do nothing (IkRigSystem.hpp:9, ControlRig.hpp:14-17).
- Registration order is already right for load-time use: the editor registers Synapse/ControlRig/game systems at
  SandboxApp.cpp:607-640 before it opens a project (:678); the game does at GameApp.cpp:1840-1873 before `openProject`
  (:1893). `CSmartObject` is registered inside `aver_syn_ai_register()` (GameSystemsWiring.hpp:137; SynapseAi.cpp:390-394).
- Packaging copies `Content\` loose and ids are package-neutral (docs/PACKAGING.md:114,179), so the `.ocskel`,
  `.ocanim`, `.ocikrig`, `.ocrig`, `.ocmotion`, `.ocbt`, `.ocst`, `.ocso` a record names ship with the level by path.
  Asset-reference scans anchor on path boundaries and treat `=` as a delimiter (AssetRefScan.hpp:46-60), so Content
  Browser "who references this" and rename-rewrite will see `skeleton=Meshes/X.ocskel` (UNVERIFIED that they scan
  `.ocworld` text for these keys; they do for `PLACE` paths).
- String-field components are a trap for byte copies. `aver_scene_set_str` **writes into the slot the field already
  owns** (SceneAbi.cpp:306-329) and slots are never freed. `CSmartObject::asset` is such a field
  (SynapseSmartObject.cpp:44-45). `captureEntity`/`instantiateEntity` copy raw bytes (EditorEntitySnapshot.cpp:22-39, 41-106),
  so a pasted/duplicated entity shares the source's string slot and editing one path edits both. Fixed in 6.3.
- Root motion `Apply` writes `CLocal` unconditionally inside `AnimSystem::tick` (AnimSystem.cpp:504-510), and that tick
  runs **outside Play** in the editor (SandboxApp.cpp:2256; its comment at :2242 says previews must run). Left as is, a
  root-motion character would walk away from its placement while merely selected and the drifted pose would be saved.
  Fixed in 6.2.

## 2. Architecture

```
Details panel (sandbox)            Shared applier (Runtime, Game.Core)         Format (formats)
  PlacedComponentModel.hpp  --->   fmt::OcPlacedComponent  <--- writeOcworld   COMPONENT lines
  SandboxComponentDetails.cpp      game::applyPlacedComponent / capture...   <--- parseOcworld
  EditCmd::Kind::PlacedComponent         ^                ^
         (undo/redo)                     |                |
  saveLevel -> fillPlacementFromEntity --+                +-- GameLevel::load / LevelStreaming (both hosts)
```

New / changed files (details in sections 3-8):

| File | Change |
|---|---|
| `modules/formats/include/aver/formats/OcWorld.hpp` | add `OcPlacedComponent`; add `components` to `OcWorldPlacement` |
| `modules/formats/src/OcWorld.cpp` | parse `COMPONENT`; write it |
| `Runtime/include/aver/game/LevelComponents.hpp` + `Runtime/src/LevelComponents.cpp` | **new**: apply / remove / capture / `applyLevelComponents` |
| `Runtime/src/GameLevel.cpp`, `Runtime/src/LevelStreaming.cpp` | one call each |
| `Runtime/CMakeLists.txt` | add `LevelComponents.cpp`; link `Aver.Synapse.Scene` to `Game.Core` when the target exists |
| `modules/world/src/LevelPack.cpp` | placements with components are not packed |
| `modules/anim.scene/...` | `CMotionMatcher::inputSource`; `AnimSystem::setRootMotionRequiresLive` |
| `sandbox/src/PlacedComponentModel.hpp` | **new**, pure model (rules, defaults, asset listing) |
| `sandbox/src/SandboxComponentDetails.cpp` | **new TU**: sections, add/remove/commit, `drawAssetField` |
| `sandbox/src/SandboxApp.hpp`, `SandboxPanels.cpp`, `SandboxSelection.cpp`, `SandboxLevelEdit.cpp`, `SandboxPlay.cpp`, `SandboxApp.cpp`, `EditorEntitySnapshot.*`, `sandbox/CMakeLists.txt` | wiring, undo kind, save capture, Play baseline, string fix |
| tests (section 9), `docs/formats/FORMAT_SPECS.md` section 11, `docs/EDITOR.md` | tests and docs (`CHANGELOG.md` is release-only and is NOT edited: its header says every entry is a public release, and releases need the owner's approval) |

## 3. The format

### 3.1 In-memory (OcWorld.hpp)

Add before `OcWorldPlacement` (OcWorld.hpp:21):

```cpp
// One component attached to a PLACEMENT (the COMPONENT record). The format does not know what a type means:
// the host's applier (aver/game/LevelComponents.hpp) does, so a new component needs no format change. Keys are
// lower-case; values are stored decoded. Unknown types and keys parse and write back untouched.
struct OcPlacedComponent {
    std::string type;                                      // canonical case on write, case-insensitive on read
    std::vector<std::pair<std::string, std::string>> kv;   // file order
    const std::string* find(std::string_view key) const;   // nullptr when absent
    bool operator==(const OcPlacedComponent&) const = default;
};
```

In `OcWorldPlacement`: `std::vector<OcPlacedComponent> components;` with a comment in the house style: empty means
"an ordinary placement, byte-identical to a level written before this field existed". **Append it as the LAST member**, after
`boundsMax` and before `uniform()` (OcWorld.hpp:124-126): a grep for `OcWorldPlacement{` over `modules`, `Runtime`, `sandbox`, `tests`
found no positional initialiser, but the last position keeps any that exist valid. It costs 24 bytes per placement (empty vector) in
`LevelStreaming::records_` (a copy of every placement, LevelStreaming.cpp:42); accepted.
Include `<utility>` in OcWorld.hpp for `std::pair`.

### 3.2 Grammar

```
COMPONENT <Type> [key=value]...
```

- Position: any line after a `PLACE`/`PLACEG`/`CHILD`/`CHILDG`, before the next one. It attaches to the **most recent
  placement** (`lastPlacement`, OcWorld.cpp:238), exactly as `BEGIN` does (:713-719). The writer puts it immediately after the
  placement line and **before that placement's `BEGIN`**, indented one level deeper than the placement.
  A `COMPONENT` does not change `lastPlacement`.
- A `COMPONENT` with no placement before it, or with fewer than 2 tokens, is skipped (unknown-record rule).
- Values are `percentEncode`d (OcWorld.cpp:61; escapes whitespace, `%`, quotes, `#`, `;`), because the tokenizer has no
  quoting and `#`/`;` are stripped before tokenizing (OcWorld.cpp:272). Split each token at the **first** `=`; a value may
  contain further `=`. A token with no `=` is read as key with value `1`.
- Writer omits keys equal to their default (the "no override is the default" rule used by every optional token in this
  file), so a freshly added component usually writes one line with 0-3 keys (exceptions and read defaults: "Canonical writing" under the table in 3.4).
- **Number spelling.** `fmt::detail::num()` is in an anonymous namespace (OcWorld.cpp:25-29) and prints `%.6g`
  (so 100 prints `100`, 0.0047813 prints `0.0047813`, 1e-05 prints `1e-05`). The format layer never formats a component number
  (values are opaque strings). `LevelComponents.cpp` therefore carries its own two-line helpers: format with `std::snprintf("%.6g")`,
  parse with `std::from_chars` (locale-independent; do not use `strtod`). Capture-then-apply is stable because both sides use `%.6g`.
- **Lists** (`axes=x,y,yaw`, `sources=graph,montage`) are comma-joined lower-case words in the canonical order of the
  table in 3.4; the reader accepts any order and repeats.
- **Asset keys** (`skeleton`, `clip`, `database`, `rig`, `asset`) hold a **content-relative forward-slash path**, the same
  spelling `PLACE`'s asset column and `anim` use; the applier hashes it with `fnv1a64`
  (docs/PACKAGING.md:179; GraphComponentTree.cs:119-121 for the same rule on the C# side). If the editor cannot turn the
  stored id back into a path (asset removed from Content), it writes `<key>#=<16 hex digits>` instead; the applier accepts either.
  Brain and Smart Object have no hex form (they need a path to load); the editor skips them with a warning.
- Parse and write must round-trip a level with **no** `COMPONENT` line byte-identically (existing FormatTest cases pin this).

### 3.3 Parser and writer edits (OcWorld.cpp)

- Parse: new branch `else if (equalsCI(key, "COMPONENT") && t.size() >= 2)` placed **before** the `BEGIN` branch (:713). If
  `lastPlacement < 0` do nothing. Otherwise push an `OcPlacedComponent` onto `out.placements[lastPlacement].components`.
- Write: the `line` lambda (:1272-1314) gains a `usize depth` parameter; after its closing `s += "\n"` emit, for each
  component, `s.append((depth+1)*2, ' ') + "COMPONENT " + type`, then ` key=percentEncode(value)` per pair. The call sites at
  :1323-1324 pass `st.depth`. Nothing else in the emit loop changes, because the nesting is carried by `BEGIN`/`END`, not by
  line order (:1317-1319).
- `placementEmitSteps` renumbers placements (OcWorld.cpp:107-111, header comment OcWorld.hpp:107-115). Components travel *inside*
  the placement struct, so renumbering cannot detach them. This is the whole reason there is no "target index" on the record.

### 3.4 Record catalogue

`Type` names are stable strings, independent of the dynamic component ids. Canonical write order of keys is the order shown.
Booleans are `0`/`1`. Numbers use the file's existing `num()` formatting.

| Type | Keys (default = omitted) | Maps to |
|---|---|---|
| `SkeletalMesh` | `skeleton` (.ocskel, required) | `CSkeletalMesh::skeleton` = id (Components.hpp:152-156). `boneCount`/`dirty` are runtime-filled (AnimSystem.cpp:408-412). |
| `Animator` | `clip` (.ocanim), `speed` (1), `weight` (1), `once` (0), `paused` (0) | `CAnimator` (Components.hpp:168-174); `once` = `kAnimatorOnce`, `paused` = `kAnimatorPaused`. `time` is **not** persisted (starts at 0). |
| `RootMotion` | `mode` (`apply`; also `ignore`, `accumulate`), `axes` (`x,y,yaw`; any of `x,y,z,yaw`), `sources` (`graph,montage`), `scale` (1), `external` (0) | `CRootMotion` (AnimSystem.hpp:69-79); `mode`=`kRootMotionApply/Ignore/Accumulate`, `axes`=`kRootAxisX|Y|Z|Yaw` (RootMotion.hpp:13), `sources`=`kRootSrcGraph|Montage`, `external`=`kRootMotionExternal`. `accum*` runtime. A stored `scale` of 0 is written as 1. |
| `MotionMatcher` | `database` (.ocmotion), `input` (`script`; also `velocity`), `facing` (`velocity`; also `yaw`), `playrate` (1), `interval` (0), `velhalflife` (0), `turnhalflife` (0), `paused` (0) | `CMotionMatcher` (MotionMatchSystem.hpp:31-47); `facing=yaw` = `kMotionFaceYaw`; `input` -> new `inputSource` (6.1). Tag lists (`requireTag`/`excludeTag`) are stored as hashes and cannot be turned back into names, so v1 does not persist them (scripts can still set them). |
| `ControlRig` | `rig` (.ocrig), `weight` (1) | `CControlRig` (ControlRig.hpp:35-43). Rig inputs are runtime (`setInput`), not persisted. |
| `IkRig` | `asset` (.ocikrig), `weight` (1), `feet` (1), `goals` (1), `body` (1) | `CIkRig` (IkRigSystem.hpp:31-37); `feet/goals/body=0` set `kIkRigFeetOff/GoalsOff/BodyOff` (IkRigSolver.hpp:15). Goals and foot smoothing are transient by design (IkRigSystem.hpp:11-12). |
| `Brain` | `asset` (.ocbt or .ocst), `team` ("") | `CSynapseBehavior` via `btSystem().setBrain` (SynapseBt.hpp:100) and `setTeam` (:132). The id is `fnv1a64(path as written)` (SynapseBt.cpp:254, 262). |
| `SmartObject` | `asset` (.ocso), `enabled` (1) | `CSmartObject` (SynapseSmartObject.hpp:24-28); `asset` is a scene string field set with `aver_scene_set_str` (SynapseSmartObject.cpp:49-56). `slotsFree` is runtime. |
| `SoftBody` (optional, 4.9) | `maxdist` (0), `compliance` (0), `simulated` (1) | `CSoftBody` (Components.hpp:235-248); `simulated=0` sets `kSoftBodyDisabled`; `body` is process-local and never written. |

**Canonical writing (frozen; the golden strings of 9.1 and the demo level depend on it).**
- Always written when the component carries a value (never omitted as "default"): the asset key (`skeleton`, `clip`, `database`, `rig`, `asset`; omitted only when the
  id is 0 = none), `RootMotion.mode`, and `MotionMatcher.input`. Every other key is written only when it differs from the default in the table's parentheses.
- Defaults when a key is absent on READ: those parentheses, plus `RootMotion.mode` = `apply`, `MotionMatcher.input` = `script`, `MotionMatcher.facing` = `velocity`,
  `RootMotion.axes` = `x,y,yaw`, `RootMotion.sources` = `graph,montage`. (`input` is the one place the read default and the menu's creation default differ: a
  hand-written record without `input` keeps today's script-driven behaviour; the menu creates `input=velocity`, and capture always writes it.)
- Key order on write is the order in the table. Component order under a placement is the canonical order of 5.1.
- Example canonical line: `COMPONENT RootMotion mode=apply axes=x,y scale=6` (a record with only `mode=apply` is also canonical).

Example (the character demo placement; asset names are examples):

```
PLACE Meshes/KenneyChar.ocmesh 0 0 0 0 0 0 1 nocollide name KenneyChar
  COMPONENT SkeletalMesh skeleton=Meshes/KenneyChar.ocskel
  COMPONENT Animator clip=Meshes/KenneyChar_idle.ocanim
  COMPONENT RootMotion mode=apply
  COMPONENT MotionMatcher database=Motion/Locomotion.ocmotion input=velocity
  COMPONENT IkRig asset=Rigs/KenneyChar.ocikrig
  COMPONENT ControlRig rig=Rigs/KenneyChar.ocrig weight=0.5
  COMPONENT Brain asset=AI/Patrol.ocbt team=guards
```

## 4. Component by component

Shared rules for every entry below (implemented once, see 5.2 and 7):
- Menu section "Animation and AI" below a separator in the Add Component popup; entry hidden if the type is not registered in
  this build (`World::componentId(name) == 0`, or the module macro is off).
- Enabled only when `canAttach` is true (5.2): the entity is a level placement with a mesh renderer, the level is `.ocworld`
  (not legacy `.ocmap`, which cannot store it, same message style as SandboxPanels.cpp:1966), and the entity is not a prefab
  member or level-instance member (those are saved inside `PREFABINST`/`LEVELINST`, SandboxLevelEdit.cpp:415-417).
- Each section is a `CollapsingHeader(..., DefaultOpen)` after the Decal block (insert at SandboxPanels.cpp:2481).
- Every edit goes through `commitPlacedEdit` (7.3): live component changed in place, one undo entry per gesture, `markLevelUnsaved()`.
- Asset rows use `drawAssetField` (7.2): path text, a `Change...` button that opens the existing `assetPicker` popup filtered by
  extension, and a drag-drop target accepting `kCbMoveDragDropType`/`kAssetDragDropType` with the right extension. This is
  the Particle Emitter pattern verbatim (SandboxPanels.cpp:2293-2379).

### 4.1 Skeletal Mesh

- Type: built-in `scene::CSkeletalMesh` (id 9). Needs `CMeshRenderer`. The skinned renderer treats the component's *presence* as the intent
  (SkinnedScene.cpp:174-176) and warns if the mesh has no skin streams (SkinnedScene.cpp:95).
- Create: `addComponent(e, kComponentSkeletalMesh)` then `*c = CSkeletalMesh{}` and set `skeleton` after the user picks one.
  Menu creates it with `skeleton = 0` and the section shows "Pick a skeleton"; nothing is applied until chosen. If exactly one `.ocskel`
  sits next to the mesh with the same stem (`KenneyChar.ocmesh`/`KenneyChar.ocskel`), preselect it.
- Inspector rows: `Skeleton` asset field (.ocskel); `Bones` = `boneCount` ("resolving..." until the first `AnimSystem::tick`
  fills it, AnimSystem.cpp:409-411; "0 after 2 frames" -> red hint "skeleton did not load"); a hint row when the placement has `Collides` on:
  "A character should not have a static collision body: turn Collides off." (no automatic change).
- Disabled when the placement has an object animation (`entityAnim(e)` non-null): the two meanings of `CAnimator` conflict
  (SandboxSelection.cpp:429-432).
- Record: `COMPONENT SkeletalMesh skeleton=<path>`.
- Works in: edit mode and Play (AnimSystem poses every entity with an animator; SkinnedScene draws it).

### 4.2 Animator

- Type: built-in `scene::CAnimator` (id 10). Requires `CSkeletalMesh` on the entity (the pose loop skips entities without one,
  AnimSystem.cpp:~400-406). For a mesh *without* a skeleton the existing "Animation" (object clip) section stays the way to animate.
- Create: `*c = CAnimator{}` (speed 1, weight 1, flags 0 = playing, looping).
- Inspector: `Clip` asset field (.ocanim; candidates exclude object clips: `!(clip->flags & kOcAnimObject)`, the inverse of the filter
  at SandboxPanels.cpp:2021), `Speed` drag (-4..4), `Blend weight` slider, `Loop` checkbox (inverse of `kAnimatorOnce`), `Playing` checkbox
  (inverse of `kAnimatorPaused`), read-only `Time`. A note appears when a `MotionMatcher` with a database is present:
  "Pose comes from Motion Matching; this clip is ignored" (the matcher is the pose source, MotionMatchSystem.hpp:11-13).
- Record: `COMPONENT Animator clip=... speed=... weight=... once=1 paused=1`.
- Works in: edit mode (previews; AnimSystem.cpp comment "CALLED UNCONDITIONALLY") and Play.

### 4.3 Root Motion

- Type: dynamic `anim::CRootMotion` (name `"CRootMotion"`, AnimSystem.hpp:69). Create with
  `anim::AnimSystem::attach(world, e, kRootMotionApply, 0, 1.0f)` (AnimSystem.cpp:106-115): never raw.
- Needs `CSkeletalMesh` (`rootSpec` returns false without one, AnimSystem.cpp:120-130).
- Inspector: `Mode` combo (Ignore / Apply / Accumulate), `Axes` four checkboxes (X, Y, Z, Yaw), `Sources` two checkboxes (State machine,
  Montage), `Scale` drag, `External consumer` checkbox with tooltip "A physics character reads the accumulator; Apply leaves the transform alone"
  (AnimSystem.hpp:66-68), read-only `Accumulated` (x, y, z, yaw) in Play.
- Hint row: "Apply moves the entity during Play only." (6.2). When there is no `Animator`, no `MotionMatcher` and no state machine:
  "Nothing produces root motion on this entity."
- Record: `COMPONENT RootMotion mode=apply axes=x,y,z,yaw sources=montage scale=2 external=1`.
- Works in: Play (Apply is gated, 6.2). In edit mode the pose plays in place.

### 4.4 Motion Matching

- Type: dynamic `anim::CMotionMatcher` (name `"CMotionMatcher"`). Create with `MotionMatchSystem::attach(world, e, 0)`, then set
  `inputSource = velocity` (new creation default), and **also add `CAnimator` if missing** (the matcher does the same,
  MotionMatchSystem.cpp:319-320, but doing it at creation makes it visible and saveable). Needs `CSkeletalMesh`.
- Inspector: `Database` asset field (.ocmotion); `Input` combo: **Script** (a script/graph writes desired velocity) / **Entity velocity**
  (the matcher follows how the entity actually moves; the default); `Facing` combo (Velocity / Entity heading); `Play rate`;
  `Search interval` (0 = database default); `Velocity half-life`, `Turn half-life` (0 = database default); `Paused`.
  Read-only block from the component's OUT fields: playing `clip` (name via the content index), `cost`, `switches`, `row`
  (MotionMatchSystem.hpp:40-45); and `database state`: Loading / Ready / Failed / Does not fit skeleton using
  `motionMatchSystem().database(id)` and `pendingBuilds()` (MotionMatchSystem.hpp:70-77). A "Draw trajectory" checkbox is out of scope.
- Why "Entity velocity" is the default input: `TrajectoryPredictor` already measures the entity's own velocity every tick
  (`rt.traj.observe`, MotionMatchSystem.cpp:329; `velocity()` Trajectory.hpp:59). Today the intent comes only from `desiredVelX/Y` that a script
  writes, so a placed matcher with no script idles forever. Driving the intent from the measured velocity makes a character that is moved by *anything*
  (physics character, sequencer, AI agent, root motion of another system) play the matching locomotion with no code. A possessed pawn moved by player
  input is covered by the same path, because its input shows up as movement. 6.1 has the change.
- Database content: use the Content Browser create command that already exists (`cbCreateMotionDb`, SandboxApp.hpp:2738). A starter
  needs a skeleton and clips; not this plan's job.
- Record: `COMPONENT MotionMatcher database=... input=velocity facing=yaw playrate=1.5 interval=0.2 paused=1`.
- Works in: edit mode (the first ticks bake or load the database on a worker thread, `pendingBuilds`) and Play. Needs the skeleton to match the
  database (`"does not fit the skeleton"` warning, MotionMatchSystem.cpp:355).

### 4.5 Control Rig

- Type: dynamic `anim::CControlRig` (name `"CControlRig"`). Create with `anim::ControlRigSystem::attach(world, e, 0, 1.0f)` (ControlRig.hpp:80).
  Needs `CSkeletalMesh`.
- Inspector: `Rig` asset field (.ocrig), `Weight` slider 0..1 ("0 turns the whole rig off without detaching it", ControlRig.hpp:39-41),
  read-only line "applied to N entities last tick" from `controlRigSystem().appliedLastTick()` (ControlRig.hpp:100) so "no rig ran" and "a rig ran and did
  nothing" are distinguishable. Rig inputs are a script concern; a hint says an unset `@input` uses its default.
- Record: `COMPONENT ControlRig rig=... weight=...`.
- Works in: edit mode and Play (pose modifier inside `AnimSystem::tick`, runs first: ControlRig.hpp:19-21).

### 4.6 IK Rig (with foot IK weight)

- Type: dynamic `anim::CIkRig` (name `"CIkRig"`). Create with `controlRigSystem().attachIk(world, e, 0, 1.0f)` (ControlRig.hpp:82; weight must
  not be left 0, IkRigSystem.hpp:33). Needs `CSkeletalMesh`.
- Inspector: `IK Rig` asset field (.ocikrig); `IK weight` slider 0..1 (`CIkRig::weight`, the master); three checkboxes `Foot placement`, `Goals`, `Body`
  (inverse of the three `Off` flag bits); a button **Feet only** that sets Goals and Body off. **"Foot IK weight" is `IK weight` while Goals and Body are
  off** (the Feet-only preset): this reuses the one authored weight rather than adding a field that `IkRigRuntime::apply` would have to learn about. Per-foot
  and per-goal weights stay runtime (`setFootWeight`/`setGoalWeight`, IkRigSystem.hpp:94-99). Read-only diagnostics:
  `hasGroundProbe()` and `probesLastTick()` (IkRigSystem.hpp:74-75, 82), plus "No ground hit: feet need collision under them" when probes > 0 and a foot reports no plant.
- Record: `COMPONENT IkRig asset=... weight=0.8 goals=0 body=0`.
- Works in: Play. Foot placement raycasts through physics (`probeGround`, GameIkProbe.hpp:20-45); the header says outside Play the editor has no physics bodies so
  feet see no hit and the character animates unchanged (GameIkProbe.hpp:7-8; UNVERIFIED, the editor does build static bodies at level load per SandboxApp.cpp:597-599,
  so this may also work in edit mode on placed meshes with Collides on). Terrain needs the landscape collision that Play builds.

### 4.7 AI Brain

- Type: dynamic `synapse::CSynapseBehavior` (name `"CSynapseBehavior"`, `#if AVER_MODULE_SYNAPSE_SCENE`). Create/assign only through
  `synapse::btSystem().setBrain(world, e, path)` (SynapseBt.cpp:536-548), which attaches with real defaults when missing. The menu entry attaches with
  `btSystem().attach(world, e)` and leaves the tree empty ("no tree assigned yet" is tolerated: SynapseBt.hpp:156-158).
- Does not require a skeleton, only a mesh placement (any pawn/prop can have a brain).
- Inspector: `Brain` asset field accepting **.ocbt** (behaviour tree) and **.ocst** (state tree), path content-relative as written (SynapseBt.hpp:92-94). On
  pick: `setBrain`; if it returns false show red "could not load" (the loader already logs). `Team` text field -> `setTeam`. Read-only: kind
  (`brainKind`: none / behaviour tree / state tree), status (`brainStatus`), state-tree active path (`activeStatePath`). Buttons **Restart** / **Stop**
  (`brainRestart`/`brainStop`) enabled in Play. A line "Open in editor" may reuse the existing BT/state tree editors; optional.
- Capture the path back with `btSystem().findTree(id)->path` or `findStateTree(id)->path` (SynapseBt.hpp:125, 97; both runtimes keep `path`:
  BtAsset.hpp:48, StateTree.hpp:47-50). No new accessor.
- Remove: call `btSystem().brainStop(e)` first, then remove the component. Checked by the reviewer pass: `BtSystem::prune` (SynapseBt.cpp:491-523, run at the end of
  `BtSystem::tick`) already erases `running_` and aborts-and-erases the state-tree instance of an entity that no longer carries the component; only the blackboard of a still-valid
  entity is kept until the entity dies (:517-522), which is harmless (a re-added brain rebinds it, :478). `prune` runs only when `tickAi` does (Play), so the stop call is what
  keeps a removal in edit mode from leaving a running instance for the next Play to find.
- Record: `COMPONENT Brain asset=AI/Patrol.ocbt team=guards`.
- Works in: **Play only** (`game::tickAi` is gated, SandboxApp.cpp:2377-2381; GameApp.cpp:2162). A brain is inert without content: a tree of `Wait`/`MoveTo`
  leaves needs a nav grid for movement (GameTick.hpp:171-181).

### 4.8 Smart Object

- Type: dynamic `synapse::CSmartObject` (name `"CSmartObject"`). Create **without** the system instance (it lives inside the Synapse DLL, `aver_syn_ai_instance`):
  `addComponent(e, componentId("CSmartObject"))`, `*c = CSmartObject{}`, then `aver_scene_set_str(e, fieldId("CSmartObject.asset"), path)` exactly as
  `SmartObjectSystem::attach` does (SynapseSmartObject.cpp:49-56). `Game.Core` does not get `AVER_WITH_SYNAPSE_AI` (only `Aver.Runtime.Game` does,
  Runtime/CMakeLists.txt:204), so the shared applier must not use the DLL ABI.
- Inspector: `Smart Object` asset field (.ocso); `Enabled` checkbox; read-only `Slots` / `Free` (`slotsFree` mirror; the slot count via the DLL
  ABI is guarded `#if AVER_WITH_SYNAPSE_AI` in the *editor* only, `aver_syn_ai_instance()->smartObjects().slotCount`, SynapseAi.hpp:66).
- Capture the path back with `aver_scene_get_str` (as `SmartObjectSystem::sync` does, SynapseSmartObject.cpp:~101).
- Record: `COMPONENT SmartObject asset=AI/Bench.ocso enabled=0`.
- Works in: Play (`SmartObjectSystem::sync` registers each object every AI tick and `tick` runs claims, SynapseAi.cpp:395-402; via
  `aver_syn_ai_tick` in `tickAi`, GameTick.hpp:182-186). Slot gizmos are out of scope.

### 4.9 Soft Body (optional, same mechanism)

Not requested but trivially covered by the same record and fixes the "lost on reload" gap (1.2): `COMPONENT SoftBody maxdist=12 compliance=0.001 simulated=0`.
Applier: `*c = CSoftBody{}` then set fields. Capture skips `body`. The existing panel and Add/Remove entries stay as they are; only persistence is new.
Implement last; drop it if time is short. Do **not** add Light/Decal to this record (bare-entity model, 1.2).

## 5. The shared applier (Runtime)

### 5.1 API (`Runtime/include/aver/game/LevelComponents.hpp`)

```cpp
namespace aver::game {
// Canonical type order = apply order (dependencies first) = write order = the order of the table in 3.4 and of editor::kPlacedKinds (7.2).
inline constexpr std::string_view kPlacedComponentTypes[] = {
    "SkeletalMesh","Animator","RootMotion","MotionMatcher","ControlRig","IkRig","Brain","SmartObject","SoftBody"};

using PathOfIdFn = std::function<std::string(u64 id)>;   // content-relative path, "" when unknown

// Add-or-update one component on `e`. False (and a one-line AVER_WARN) when the type is unknown to this build, a required
// component is missing, an asset key is absent, or the system refuses. Never partially leaves a half-written component.
bool applyPlacedComponent(scene::World&, scene::Entity, const fmt::OcPlacedComponent&);
// Removes the component (Brain: stops the tree first). False when absent.
bool removePlacedComponent(scene::World&, scene::Entity, std::string_view type);
// The authored state of one component as a record; nullopt when the entity does not carry it. Runtime OUT fields are never read.
std::optional<fmt::OcPlacedComponent> capturePlacedComponent(scene::World&, scene::Entity, std::string_view type, const PathOfIdFn&);
std::vector<fmt::OcPlacedComponent> capturePlacedComponents(scene::World&, scene::Entity, const PathOfIdFn&);   // canonical order
// Applies p.components (sorted into canonical order). Returns the number applied.
u32 applyPlacedComponents(scene::World&, scene::Entity, const fmt::OcWorldPlacement&);
// The post-pass: for k in inst.entities, applies w.placements[inst.placementIndex[k]].components.
// Logs ONE `AVER_INFO("[Level] {} placed component(s) on {} placement(s)", applied, placementsTouched)` when applied > 0 (the line the demo and the
// acceptance checks grep for), and one AVER_WARN per refused record: "[Level] COMPONENT {Type} refused on placement {index}: {reason}".
void applyLevelComponents(scene::World&, const fmt::OcWorldData&, const world::LevelInstance&);
}
```

Rules every `apply*` follows (each is a trap this codebase already fell into):
- Attach through the system, then **write every authored field explicitly**, because storage is zero-filled (Components.hpp:158-162).
- Fixed per-type dependency check: Animator/RootMotion/MotionMatcher/ControlRig/IkRig require `CSkeletalMesh` *with a non-zero skeleton*; all require
  `CMeshRenderer`. Missing requirement -> warn once per type per load and skip.
- A `SkeletalMesh` record on a placement that also has an `anim` object clip is refused with a warning (the two meanings of `CAnimator`).
- Idempotent: applying twice updates; never adds a second component.
- Brain: `setBrain(world, e, path)` then `setTeam` if `team` is non-empty. SmartObject: as in 4.8.
- `MotionMatcher`: attach, then write `database`, `inputSource`, flags, rates. Write `database` **last** (MotionMatching.cs:44-45 explains the half-written-config race).
- `capture*` reads only authored fields (4.x tables). For asset ids it calls `pathOf`; on "" it writes the `#=` hex form (not for Brain/SmartObject).

### 5.2 Call sites

- `GameLevel.cpp`, immediately before `hooks_.afterInstantiate(...)` (:517): `game::applyLevelComponents(scene::World::instance(), w, inst);`
  Guard with `#if AVER_MODULE_SCENE`. Skip when `legacy`. For a streamed level `inst` is empty here (:276) and the streaming path covers it.
- `LevelStreaming.cpp`, after the loop that builds `loaded` (:225-243) and before `hooks_.afterLoad` (:250): for each `k`, apply
  `records_[loadedPlacement[k]].components` to `loaded[k]`. Reuse `applyPlacedComponents`; this site logs warnings only (an INFO line per streamed item would be noise on a hitch-sensitive path).
- Level instances (`LEVELINST`, `Runtime/src/LevelInstances.cpp`): **done 2026-10-12** (was a v1 non-goal). Members get their child level's
  records through `applyPlacedComponents` after the `created` hook, so an editor Commit captures them back instead of dropping them.
- `modules/world/src/LevelPack.cpp:56`: `const bool own = ... || !p.components.empty();`. Packed rows are bare transform rows with no entity, so a
  placement with components must stay an entity (same reason as className/vehicle/anim there).
- `Runtime/CMakeLists.txt`: add `src/LevelComponents.cpp` to `Aver.Runtime.Game.Core` (list at :~186-215) and, next to the Anim.Scene link at :134-136,
  `if(TARGET Aver.Synapse.Scene) target_link_libraries(Aver.Runtime.Game.Core PRIVATE Aver.Synapse.Scene) endif()`. **PRIVATE, not PUBLIC** (frozen): `AVER_MODULE_SYNAPSE_SCENE=1` is a PUBLIC definition on that target (modules/synapse.scene/CMakeLists.txt, last line), so a PUBLIC link would hand the macro to every Game.Core dependent (`InputBridgeTest`, `PlayMobilityTest` ...) and compile guarded paths in them that nobody has built there; PRIVATE gives it to Game.Core's own TUs only (none of which include the guarded `GameTick.hpp` / `GameSystemsWiring.hpp`: checked by grep). `LevelComponents.hpp` therefore must not name a Synapse type. `Aver.Synapse.Scene` depends only on
  Synapse, Scene, Core (modules/synapse.scene/CMakeLists.txt), so no cycle (it already links into `Aver.Runtime.Game`, Runtime/CMakeLists.txt:200-202).
  Anim code is under `#if AVER_MODULE_SCENE`, AI code under `#if AVER_MODULE_SYNAPSE_SCENE`, as GameSystemsWiring.hpp does.

## 6. Small engine changes (general-purpose)

### 6.1 `CMotionMatcher::inputSource`

- `MotionMatchSystem.hpp:45`: rename the padding word `u32 reserved = 0;` to `u32 inputSource = 0;` with `inline constexpr u32 kMotionInputScript = 0,
  kMotionInputEntityVelocity = 1;`. Size stays 136, so the `static_assert` holds. Zero = Script = today's behaviour for every existing entity and script.
- `MotionMatchSystem.cpp:78`: the field is registered read-only named `"reserved"`. Register it writable as `"inputSource"` (I32). Checked by the reviewer pass: the only other
  mention of the field is the design record `docs/plans/wave5/MOTION_MATCHING.md:376,380` (update both lines to say `inputSource`); no C#, test or overlay code reads `reserved` on this struct
  (`CAnimGraph` has its own unrelated `reserved`, AnimGraphSystem.cpp:42; leave it).
- `MotionMatchSystem.cpp:332-335` (the tick, right after `rt.traj.observe` at :329): before `setIntent`,
  ```cpp
  f32 vx = c->desiredVelX, vy = c->desiredVelY, yaw = c->desiredYaw;
  if (c->inputSource == kMotionInputEntityVelocity) {
      const Vec3 v = rt.traj.velocity();   // measured, already smoothed (Trajectory.hpp:59)
      vx = v.x; vy = v.y; yaw = rt.traj.yaw();
      c->desiredVelX = vx; c->desiredVelY = vy; c->desiredYaw = yaw;   // so the inspector and scripts can read what is used
  }
  rt.traj.setIntent(vx, vy, (c->flags & kMotionFaceYaw) != 0, yaw, defs->spring.faceSpeedMin);
  ```
- Optional C# sugar (`scripting/csharp/Aver.Framework/MotionMatching.cs`): `SetMotionMatching(..., MotionInput input = MotionInput.Script)`. Not required by this plan, so no
  `Aver.Graph.Tests` change.

### 6.2 Root motion `Apply` only while Play is live

- `AnimSystem.hpp`: `void setRootMotionRequiresLive(bool on) { rootNeedsLive_ = on; }` (default **false**, so every existing host and test is unchanged) and a
  private `bool rootNeedsLive_ = false;`.
- `AnimSystem.cpp:504-510`: today the block is `if (Apply && !External) { write CLocal; acc = {}; accumSeconds = 0; } else if (Apply && accumSeconds > 0.5f) { drop }`.
  Introduce `const bool applyNow = rm->mode == kRootMotionApply && !(rm->flags & kRootMotionExternal);` and
  `const bool mayWrite = !rootNeedsLive_ || objectLive_;`. Branch 1 becomes `if (applyNow && mayWrite)` (unchanged body). Branch 2 becomes
  `else if (applyNow || (rm->mode == kRootMotionApply && rm->accumSeconds > 0.5f))` and drops the accumulator (`acc = {}; accumSeconds = 0`), so a
  gated-off Apply plays the pose in place and the accumulator never grows. Accumulate and Ignore are untouched.
  `objectLive_` is already exactly "a Play session is live" (SandboxPlay.cpp:262 sets it true; :427, :463, :700 set it false; GameApp.cpp:1961).
- `SandboxApp.cpp:618` (beside `controlRigSystem().install`): `anim::animSystem().setRootMotionRequiresLive(true);`. The packaged game does not opt in (it is always live).

### 6.3 Editor snapshot: String fields

`EntitySnapshot::Comp` (EditorEntitySnapshot.hpp:56) gains `std::vector<std::pair<u32, std::string>> strings;` (field id, text).
- `captureEntity` (EditorEntitySnapshot.cpp:22-39): after the byte copy, for each field of the type with `kind == FieldKind::String`
  (`World::fieldCount/fieldAt/field`, World.hpp:91-95; `aver_scene_get_str(e, fieldId)`), record the text.
- `instantiateEntity` (:41-106): after `memcpy`, for each recorded string, zero the 8 bytes at the field offset and call `aver_scene_set_str(newEntity, fieldId, text)`,
  which allocates a **fresh** slot. Do it for both undo and paste (a restored entity must not alias either). Precedent for a per-component touch-up: the
  `CParticleEmitter` seed reset at :96.
- Why: 1.3, last-but-two bullet. It also protects any future String-field component.

### 6.4 Not changed

`world::instantiate` stays free of these components (the post-pass owns them). `CIkRig`'s layout is unchanged (no authored per-foot weight field).
`Components.hpp` and the scene ABI are untouched (dynamic registration is why).

## 7. Editor

### 7.1 Menu restructure (SandboxPanels.cpp:2482-2562)

- Keep `kAddable[]`/`kRemovable[]` and their loops for the three existing entries.
- After the loop add `ImGui::Separator(); ImGui::TextDisabled("Animation and AI");` and loop `editor::kPlacedKinds` (from `PlacedComponentModel.hpp`):
  for each kind, resolve the id (`kind.builtinId` or `w.componentId(kind.componentName)`); skip if 0; `present = w.hasComponent(sel, id)`;
  `canAttach(ctx, kind, &why)`; `BeginDisabled(present || !ok)`; `MenuItem(kind.menuLabel)` -> `addPlacedComponent(kind)`; tooltip = `why` when disabled
  (`AllowWhenDisabled`, same idiom as :2523-2526), else `kind.tip`.
- Remove popup: same loop, enabled when present and `canRemove` (a SkeletalMesh with dependents cannot be removed: "Remove Animator, Root Motion, Motion Matching, Control Rig
  and IK Rig first"; no cascade, so one undo entry stays one component) -> `removePlacedComponentFromSelection(kind)`.
- Menu labels (exactly the table in 0.0): `Skeletal Mesh`, `Animator`, `Root Motion`, `Motion Matching`, `Control Rig`, `IK Rig`, `AI Brain`, `Smart Object`. Tooltips one sentence each,
  e.g. IK Rig: "Plants the feet on the ground and drives IK goals from an .ocikrig. Needs a Skeletal Mesh."
- `levelOwned` is "`selEntity_` is in `levelEntities_`" (SandboxApp.hpp:4168, a `std::vector`, so a linear `std::find`). The Add/Remove popups are
  `BeginPopup` blocks whose bodies run only while open, so the scan is a user-action cost, not a per-frame one: compute `AttachContext`
  inside the popup body, never in the panel's per-frame path. A graph-class root or COMP part (PLACE_CLASS.md) is not in `levelEntities_`,
  which is exactly why every entry is disabled there (0.0).

### 7.2 New model header `sandbox/src/PlacedComponentModel.hpp` (pure, header-only, no ImGui, so tests/editor can drive it)

- `struct PlacedKind { const char* type; const char* menuLabel; const char* tip; const char* header; u32 builtinId; const char* componentName; std::initializer_list<const char*> assetExts; }`
  and `inline constexpr PlacedKind kPlacedKinds[]` in canonical order (the table in 3.4).
- `struct AttachContext { bool levelOwned, hasMesh, hasSkeleton /*CSkeletalMesh with skeleton!=0*/, hasObjectAnim, legacyOcmap, prefabLinked, instanceMember, streamedOnlyRecord; }`
  `bool canAttach(const AttachContext&, const PlacedKind&, std::string* why)` and `bool canRemove(bool hasAnimator, bool hasRoot, bool hasMatcher, bool hasControlRig, bool hasIk, const PlacedKind&, std::string* why)`.
  The rule table is exactly the "Needs" lines in 4.1-4.8.
- `std::vector<AssetChoice-like> listAssets(const std::unordered_map<u64,std::string>& contentIndex, exts, const std::function<std::string(const std::string&)>& toRelative)`:
  the pure core of the picker candidates (the loop at SandboxPanels.cpp:2330-2345, extracted), sorted by label.
- `fmt::OcPlacedComponent makeNewRecord(const PlacedKind&)`: creation defaults (Animator speed/weight 1; RootMotion `mode=apply`; MotionMatcher `input=velocity`; IkRig `weight=1`; others empty/default).
- `std::string presetSkeletonFor(meshPath, listing)`: the "same stem next to the mesh" preselect from 4.1.

### 7.3 `sandbox/src/SandboxComponentDetails.cpp` (new TU, add to `sandbox/CMakeLists.txt` sources)

SandboxApp members (declare near `assetPicker`, SandboxApp.hpp:2857-2870, inside `#if AVER_MODULE_SCENE`):

```cpp
void drawPlacedComponentSections();                                   // called once at SandboxPanels.cpp:2481
bool addPlacedComponent(const editor::PlacedKind&);                   // create + defaults + one undo entry + markLevelUnsaved
bool removePlacedComponentFromSelection(const editor::PlacedKind&);   // capture, remove (Brain: stop first), one undo entry
void commitPlacedEdit(const editor::PlacedKind&, std::optional<fmt::OcPlacedComponent> before);   // capture "after", push if different
bool drawAssetField(const char* id, const char* label, std::string& path, std::span<const char* const> exts);   // true when changed
std::string pathOfContentId(u64) const;                               // cbRelativeToContent(content_.pathFor(id)), "" if not indexed
```

- `drawAssetField`: shows `path` or "(none)" in `TextDisabled`, `Change...` button -> `ImGui::OpenPopup(id)`; the candidate list is built **only while the popup is open** and
  cached with `PickerCands` (SandboxApp.hpp:2864-2870; build/drop idiom at SandboxPanels.cpp:2323-2376, which warns that scanning the whole index every frame
  once cost real time); a `(None)` row first; drag-drop target accepting the two payload types and filtering by extension (SandboxPanels.cpp:2309-2325).
- Edit gesture pattern for each widget: `if (ImGui::IsItemActivated()) beginPlacedGesture()` (captures `before` into one member `placedBefore_`), then
  `if (ImGui::IsItemDeactivatedAfterEdit()) commitPlacedEdit(...)`. Combos/checkboxes/asset picks that finish in one frame call `before = capture; mutate; commit` in a row.
  This is the bracket the transform fields use (SandboxPanels.cpp:225-233, 1704-1707). Commit builds `after = capturePlacedComponent(...)`, pushes only if `before != after`
  (SandboxPanels.cpp:366 says a no-op entry makes Ctrl+Z "visibly do nothing, once").
- `EditCmd` (SandboxApp.hpp:2009-2010): add `PlacedComponent` to `Kind`; add members `std::string pcType; std::optional<fmt::OcPlacedComponent> pcBefore, pcAfter;`
  inside `#if AVER_MODULE_SCENE` next to `removedComponent` (:2061-2064). `nullopt` = component absent, so the same command covers Add (before nullopt), Edit, Remove (after nullopt).
- Undo/redo: `SandboxSelection.cpp` switch in `undo()` (:1235, add near :1253) and `redo()` (:1305, near :1322) call a new
  `applyPlacedComponentState(EditId id, const std::string& type, const std::optional<fmt::OcPlacedComponent>& st)` which resolves `entityForEdit(id)` and calls
  `game::applyPlacedComponent` or `game::removePlacedComponent`, then `markLevelUnsaved()`.
- Deleting the entity still works through `EntitySnapshot` (byte copy of dynamic components too: `captureEntity` walks every registered type, EditorEntitySnapshot.cpp:28-33),
  plus 6.3 for strings.
- Sections in `drawPlacedComponentSections()` follow the order of `kPlacedKinds`; each is `if (present) { if (CollapsingHeader(kind.header, DefaultOpen)) draw...(); }` with the rows
  listed in 4.1-4.8. Guard each by its module macro (`AVER_MODULE_SCENE` for anim, `AVER_MODULE_SYNAPSE_SCENE` for brain and smart object; smart object slot count also `AVER_WITH_SYNAPSE_AI`).
- A `Details` multi-selection edits only the primary selection (same as Soft Body today); not in scope.

### 7.4 Save and Play

- `SandboxLevelEdit.cpp:fillPlacementFromEntity` (:407-469): before `return true`, `p.components = game::capturePlacedComponents(world, e, [this](u64 id) { return pathOfContentId(id); });`.
  The placement loop (:641-697) and `buildStreamedPlacements` (:640) both go through it for resident entities, so streamed levels write them too (verified by the reviewer pass: `buildStreamedPlacements` calls `fillPlacementFromEntity` at SandboxLevelStream.cpp:252, and `refreshStreamRecord` at :64 does the same for a live edit, so no extra line in `SandboxLevelStream.cpp`). The level-instance commit path (SandboxLevelInstances.cpp:174) goes through the same function, so a child level saved from an instance edit keeps its components too. For a non-resident streamed record the record's own `components` ride through
  unchanged (LevelStreaming records_ copy the whole placement, LevelStreaming.cpp:42).
- **A save during Play must write the authored state, not Play's.** A script can change `CAnimator::speed/paused`, `CIkRig::weight`, etc. at runtime, and an autosave lands mid-Play
  (SandboxLevelEdit.cpp:609-612 documents the same hazard for transforms). At Play start (SandboxPlay.cpp, after the transform snapshot near :262) capture `placedBaseline_[entity]` =
  `capturePlacedComponents(...)` for each level-owned entity that has any; in `fillPlacementFromEntity` use the baseline while `playWorldCaptured_` is true. Clear it where the snapshot is
  cleared (SandboxPlay.cpp:427/463/700 neighbourhood).
- Load needs no editor code: `GameLevel::load` already applied the components before `afterInstantiate` (5.2), and the editor's `onLevelInstantiated` (SandboxLevelLoad.cpp:381)
  only builds its label/collide tables.

## 8. What makes each component start working

| Component | System that reads it | Tick site, editor / game | Outside Play (editor) |
|---|---|---|---|
| Skeletal Mesh + Animator | `AnimSystem::tick` poses; `SkinnedScene` draws | SandboxApp.cpp:2256 / GameApp.cpp:2125 (not gated: "previews") | Yes, animates in the viewport |
| Root Motion | `AnimSystem::tick` (AnimSystem.cpp:481-522) | same | **No** (6.2); Apply moves the entity in Play only |
| Motion Matching | `MotionMatchSystem::tick` inside `tickAnimGraphs` (GameSystemsWiring.hpp:166-173) | SandboxApp.cpp:2252 / GameApp.cpp:2121 | Yes (database loads on a worker thread first); entity velocity is 0 unless something moves it |
| Control Rig | pose modifier called in `AnimSystem::tick` (AnimSystem.cpp:531) | same as Animator | Yes |
| IK Rig | `IkRigRuntime::apply`, chained after the control rig ops (ControlRig.hpp:19-21) | same | Feet need physics bodies (see 4.6, UNVERIFIED in edit mode) |
| AI Brain | `btSystem().tick` in `game::tickAi` (GameTick.hpp:171-190) | gated on Play: SandboxApp.cpp:2377-2381 / GameApp.cpp:2162 | No |
| Smart Object | `SmartObjectSystem::sync/tick` through `aver_syn_ai_tick` (GameTick.hpp:182-186; SynapseAi.cpp:395-402) | same gate | No |

All the systems are registered and installed before a project opens in both hosts (section 1.3), so a record applied during load always finds its component type.

## 9. Tests

Names end in `Test`; the root CMake registers every such target with the repo's failure regex (see the comment in tests/formats/CMakeLists.txt), so use plain
`add_executable` and `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin`, and no hand-written `add_test`. Use the `check()` + `AVER_INFO/AVER_ERROR` shape of
tests/formats/src/LightFormatTest.cpp.

### 9.1 `tests/formats/src/OcWorldComponentsTest.cpp` (links `Aver.Formats Aver.Core Aver.Platform`)

1. Parse a literal level with `PLACE` + two `COMPONENT` lines: `components.size()==2`, types, kv order, decoded values.
2. `writeOcworld(parse(x))` equals a golden string, and parse(write(...)) equals the first parse (`operator==`).
3. A level with no `COMPONENT` lines writes byte-identically to the pre-change writer: assert against the existing FormatTest fixture text; also assert the writer emits no `COMPONENT` token.
4. `COMPONENT` after a `CHILD` inside `BEGIN`/`END` attaches to the child; after the `END` it still attaches to the last placement parsed.
5. `COMPONENT` before any placement: parse succeeds, nothing attached. `COMPONENT` alone (one token): skipped.
6. Values with space, `%`, `#`, `;`, `=`, `"` round-trip. Empty value (`key=`) round-trips. Bare token reads as `1`.
7. Placement list built by hand with the **child before its parent** (the case `placementEmitSteps` renumbers): each placement keeps its own components after write+parse. A `SEQTRACK` target still names the right placement.
8. Unknown type and unknown keys round-trip untouched. Mixed-case `component`/`SKELETALMESH` read; written in canonical case. `skeleton#=00ff...` round-trips.
9. Trailing-`;` and trailing-comment lines (`# ...`) after a `COMPONENT` do not corrupt it.

### 9.2 `tests/game/src/LevelComponentsTest.cpp` (links `Aver.Runtime.Game.Core Aver.Anim.Scene Aver.Scene Aver.Formats Aver.Core`, plus `Aver.Synapse.Scene` under `if(TARGET ...)` with `AVER_MODULE_SYNAPSE_SCENE` guarding the AI cases; add to tests/game/CMakeLists.txt in the style of LevelLightsTest, :46-52)

Real `scene::World`, no device. Register: `AnimSystem::registerComponents`, `ControlRigSystem::registerComponents`, `MotionMatchSystem::registerComponents`; for AI
`btSystem().registerComponents(world)` and a local `synapse::SmartObjectSystem` `registerComponents` (`registerComponents` works without `bind()`: it touches only the world and its own `type_` / `assetField_`, SynapseSmartObject.cpp:43-52).
1. For each type: apply a record to an entity that has `CMeshRenderer` (+ `CSkeletalMesh` where needed); assert the component fields (ids equal `fnv1a64(path)`; `CIkRig::weight==1` after a record with no `weight` key; `CRootMotion::mode` and
   `axes`; `CMotionMatcher::inputSource==1`; `CSmartObject::asset` string via `aver_scene_get_str`; `CSynapseBehavior::treeAssetId` equals the loaded id).
2. `capturePlacedComponent` returns a record `==` the canonical form of the input (defaults omitted), with a `PathOfIdFn` over a map; with a resolver returning "" the `#=` hex form is written and re-applies to identical bytes (not Brain/SmartObject: those return nullopt and warn).
3. Dependency order: records given in reverse order apply the same as canonical order; a `RootMotion` record on an entity with no skeleton is refused and leaves no component.
4. Idempotent: applying the same record twice leaves one component and the same bytes; applying a changed record updates.
5. `removePlacedComponent` removes; capture then returns nullopt; second remove returns false.
6. `applyLevelComponents` with a hand-built `world::LevelInstance` (entities + placementIndex, one class placement skipped): components land on the right entities; a placement with `animClip` and a `SkeletalMesh` record is refused.
7. Undo/redo equivalence used by the editor: `apply(before)` then `apply(after)` then `apply(before)` ends byte-identical to the start; `before=nullopt` -> `remove`.
8. Brain: write a minimal `.ocbt` into a temp content root (`synapse::setAiContentRoot`, modules/synapse/include/aver/synapse/AiPaths.hpp:9); build the asset with `btAssetStarter()` and write it with `writeBtAsset` (both in `aver/synapse/BtAsset.hpp`, used by tests/synapse_bt/src/BtAssetTest.cpp:24-70). Apply `Brain asset=AI/T.ocbt`; `brainKind==1`; capture returns the same path via `findTree(id)->path`. Same for a `.ocst`.

### 9.3 `tests/editor/src/PlacedComponentModelTest.cpp` (header-only, Core + Scene; mirror `LightDetailsTest`'s CMake block, tests/editor/CMakeLists.txt, `target_include_directories(... ${CMAKE_SOURCE_DIR}/sandbox/src ${CMAKE_SOURCE_DIR}/Runtime/include ${CMAKE_SOURCE_DIR}/modules/world/include)`)

- `canAttach` matrix: every row of 4.x "Needs" (no mesh, not level-owned, legacy ocmap, prefab-linked, instance member, has object anim vs SkeletalMesh, already present, Animator without skeleton, RootMotion without skeleton).
- `canRemove`: SkeletalMesh blocked while any dependent exists; message names the dependents; others always removable.
- `listAssets`: a fake content index with mixed extensions returns only the wanted ones, sorted, labels via the relative-path callback; `.ocbt` and `.ocst` both for Brain; object clips excluded for Animator (injectable predicate).
- `makeNewRecord` for every kind applies cleanly through `game::applyPlacedComponent` (the defaults are valid, not zero-filled traps) - this test also links `Aver.Runtime.Game.Core Aver.Anim.Scene`.
- `presetSkeletonFor`: `Meshes/KenneyChar.ocmesh` + listing containing `Meshes/KenneyChar.ocskel` and `Skeletons/Character.ocskel` picks the first; with only the second returns "".
- Extend `tests/editor/src/EditorEntitySnapshotTest.cpp`: register a test component with a `FieldKind::String` field; capture, `instantiateEntity` twice (undo mode and paste mode); assert the three entities have **distinct** pool slots and equal text, and that changing one does not change another (6.3). Also assert a `CIkRig`-style dynamic component round-trips byte-exact.

### 9.4 Engine tests

- `tests/anim.scene/src/MotionMatchSystemTest.cpp`: an entity whose `CLocal` is written at 200 cm/s for ~1 s with `inputSource=1`: `desiredVelX` ends within 10% of 200 and the matcher selects a moving clip; with `inputSource=0` and zero desired velocity it stays on the idle clip. A zero-filled `CMotionMatcher` still behaves as before (`inputSource` 0).
- `tests/anim.scene/src/RootMotionSystemTest.cpp`: with `setRootMotionRequiresLive(true)` and `setObjectAnimationLive(false)`, `Apply` leaves `CLocal` unchanged and `accumSeconds==0` while the pose still plays; with live true it moves exactly as the existing "apply" case (:253). Existing cases are unchanged (default false).
- `tests/levelinstance/src/LevelPackTest.cpp`: a placement with `components` is not packed (stays an entity), its children neither, a sibling without components still packs.
- `tests/world/src/LevelInstanceTest.cpp`: `world::instantiate` ignores `components` (no extra components on the entity, nothing thrown).

### 9.5 Not testable headless (owner verifies)

ImGui layout of the sections, the viewport drag-drop, and the actual look of IK feet and motion matching. Section 10 is the owner's script for them.

## 10. Acceptance (for the owner, on PTTest)

Preconditions: PTTest `Content/Meshes/KenneyChar.ocmesh` + `KenneyChar.ocskel` + `KenneyChar_idle.ocanim`/`_walk.ocanim` (all exist). `KenneyChar.ocmesh` carries skin
streams: **verified by the reviewer pass** by reading its `MHDR` (flags `0x24` = HasMeshlets + HasSkin, streams POSITION, TANGENT_FRAME, JOINTS, WEIGHTS; 3 LODs, 2 submeshes;
`Character.ocmesh` is `0x20` HasSkin, 1 LOD), and `KenneyChar.ocskel` lists exactly seven bones (`root`, `leg-left`, `leg-right`, `torso`, `arm-left`, `arm-right`, `head`), so the Bones
read-out should say 7. If the log shows the
"has no skin streams" warning (SkinnedScene.cpp:95) anyway, report it; do not re-import.

1. Drag `KenneyChar.ocmesh` into a level. Select it. Details > Collides off.
2. Add Component > **Skeletal Mesh**; Change... > `Meshes/KenneyChar.ocskel` (preselected). Bones shows the skeleton's bone count within two frames.
3. Add Component > **Animator**; clip `KenneyChar_idle.ocanim`. The character idles in the viewport without Play. Undo (Ctrl+Z) removes the clip; Redo restores it.
4. Ctrl+S, close and reopen the level. The Animator and Skeletal Mesh sections are back with the same values. The saved file contains
   `COMPONENT SkeletalMesh skeleton=Meshes/KenneyChar.ocskel` and `COMPONENT Animator clip=Meshes/KenneyChar_idle.ocanim` directly under the placement line.
5. Add **Root Motion** (Apply). Not in Play: the character does not drift. Play with an animator clip that has root motion: it moves, Stop restores the placement.
6. Add **Motion Matching** with a database (create via Content Browser). Database state turns Ready; in Play, move the entity (any means) and the `clip` read-out changes from idle to a moving clip.
7. Add **IK Rig** (create with the existing IK Rig editor, `cbCreateIkRig`, SandboxApp.hpp:2734) and press **Feet only**; in Play on terrain/collision, feet plant; diagnostics show probes > 0.
8. Add **Control Rig** with a `.ocrig`; the weight slider visibly changes the pose.
9. Add **AI Brain** with an `.ocbt`; Play: kind/status read-outs update. Add **Smart Object** with an `.ocso`; Play: `Free` mirrors the slot count.
10. Duplicate (Ctrl+D) an entity with a Smart Object; change the duplicate's `.ocso`; the original keeps its own (6.3).
11. Run the packaged game from the same level (`aver_package`/`AverEngineRuntime`): the same components exist and behave (this is the packaged-game proof of 5.2).
12. Run the new test targets (section 9) and `FormatTest`; all existing suites unchanged.

## 11. Work breakdown (SUPERSEDED by TASKS.md; kept for the item names it cites)

Mapping to `TASKS.md`: A1-A4 and the `LevelPack` test = T1; B1-B2 = T2; C1, C2, D1, D2 and the 6.3 snapshot fix (A5) = T3 (T3 owns every shared
`sandbox/src` file, including the PLACE_CLASS hunks in them); D3 docs = T1 (`FORMAT_SPECS.md`; no `CHANGELOG.md` edit) and T3 (`docs/EDITOR.md`). E1 (Soft Body, optional) is T2 only: its applier/capture cases live in
`LevelComponents.cpp`, and the Soft Body menu entry and panel already exist.

| Stage | Items | Depends on |
|---|---|---|
| A | A1 format (3.1-3.3) + 9.1; A2 `LevelPack.cpp:56` + test; A3 6.1 MM `inputSource` + test; A4 6.2 root motion gate + test; A5 6.3 snapshot strings + test | none |
| B | B1 `LevelComponents.hpp/.cpp` (5.1) + CMake; B2 call sites `GameLevel.cpp`, `LevelStreaming.cpp` (5.2) + 9.2 | A1, A3 |
| C | C1 `PlacedComponentModel.hpp` + 9.3; C2 `SandboxComponentDetails.cpp`, `SandboxApp.hpp`, `SandboxPanels.cpp`, undo/redo cases (7.1-7.3) | B1, C1 |
| D | D1 save capture + Play baseline (7.4); D2 `SandboxApp.cpp:618` gate call; D3 docs: `docs/formats/FORMAT_SPECS.md` section 11 (`COMPONENT` grammar, table 3.4), `docs/EDITOR.md` (no `CHANGELOG.md` edit, see section 2) | C2 |
| E | E1 optional Soft Body record (4.9) | B1 |

Per repo rules: lean comments (rationale only), `AVER_MODULE_*` guards exactly as the existing code uses them, no new third-party code, every engine change has its test (A2-A5, B2),
no C# change so no `Aver.Graph.Tests` change. Do not `-D` reconfigure the build tree; only the owner builds.

## 12. Risks and UNVERIFIED list

1. **Skinned character in the editor viewport (UNVERIFIED end to end).** `PlayMobility::objectAnimated` ignores skeletal entities (PlayMobility.cpp:16-24); whether a placed skeletal actor with a static `CLocal` is
   treated as static by the GI/RT scene (shadows not following the animation) was not checked. Owner check in step 3/5; fix would be in `PlayMobility` (general).
2. `BtSystem` runtime cleanup on component removal is covered by `prune` (4.7); `buildStreamedPlacements` writing components is verified (7.4). No open item remains in this row.
3. `MotionMatchSystem::tick` query timing: `traj.velocity()` after the first observe is 0 for one tick; the matcher idles for that tick. Acceptable.
4. IK feet in edit mode (4.6) and the ground probe (GameIkProbe.hpp) need collision bodies under the character; a level with `Collides` off on the floor gives no plant.
5. `.ocworld` written by the new editor opens in an old editor with the components silently dropped (old parser skips `COMPONENT`), and a re-save there deletes them. Same trade-off every added record has made; the owner mentions it in the release notes when this ships.
6. Packed/instanced levels: `LEVELINST` realisation does not apply components (non-goal below).
7. `CMotionMatcher.reserved` rename: no external user found (6.1); resolved.
8. Dynamic component ids differ per process; nothing in this design stores one (names only in the file, ids resolved at menu-draw and load).
9. A multi-selection Add/Remove affects the primary selection only.

### Non-goals

Light/Decal as placement components; fixing the existing Light-on-mesh double save (1.2) or Soft Body persistence beyond 4.9; level-instance (`LEVELINST`) realisation of components; persisting
motion tag lists, IK goals, rig inputs or per-foot weights; creating the assets (`.ocikrig`, `.ocmotion`, `.ocso`, `.ocbt`, `.ocst`, `.ocrig` authoring already has Content Browser create commands,
SandboxApp.hpp:2723-2739, and the content itself is another part of the demo); a graph node for any of this; multi-selection editing; slot/IK gizmos in the viewport.
