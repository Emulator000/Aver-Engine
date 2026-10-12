# Parity demo: implementation tasks (frozen 2026-10-12)

Source documents, all frozen by the same reviewer pass and cross-checked against each other and against the tree at `f738c7a2` (branch `0.7-main-nrdneuraa`):

- `ADD_COMPONENT.md` (Details > Add Component for Skeletal Mesh, Animator, Root Motion, Motion Matching, Control Rig, IK Rig, AI Brain, Smart Object; the `COMPONENT` level record)
- `PLACE_CLASS.md` (drag an `.ocgraph` CLASS into the viewport or `Add > Actor From Graph`; the Delete / Undo / Stop fixes it needs)
- `DEMO_CONTENT.md` (six levels and their assets for the owner's `PTTest` project)

Five tasks, each owning a disjoint set of files (section 2). Nothing here is built or run by an implementer: implementers cannot build, the owner builds and verifies
(standing rule: user verifies, I write code). Section 5 is the owner's integration run.

## 0. Rules for every task

1. **Read first**, in this order: this file; the task's source sections (listed per task); then re-open every file you will edit and confirm the cited line still shows what the
   document says. **If a cite does not match, stop and report it** (file, line, what you found); do not improvise a different design.
2. **Touch only the files your task owns** (section 2). If you need a change in another task's file, write it as a note in your final message instead; do not make it.
   Where a document says a hunk belongs to a file another task owns, that task applies it (section 3 is the contract).
3. Engine rules: general-purpose changes only (no PTTest- or level-specific engine code); module boundaries exactly as the neighbouring code does them (`AVER_MODULE_SCENE`,
   `AVER_MODULE_FRAMEWORK`, `AVER_MODULE_SYNAPSE_SCENE`, `AVER_WITH_SYNAPSE_AI`, `AVER_MODULE_SCRIPTING`); lean comments that give the reason, not a narration; **no new third-party code**;
   every engine change ships with its test where the repo has a matching suite (the task lists them); C# graph changes need `Aver.Graph.Tests` (no task changes `Aver.Graph`).
4. **No shader, pipeline or renderer changes.** No task touches `*.hlsl`, `modules/render*/**` or a draw path, so the RX 7800 XT register-pressure / TDR rule for shader changes
   (memory: `aver-raster-rt-tdr-1011`, `aver-rect-light-tdr-and-coarse-cap`, `aver-gpu-tdr`) is not engaged. If you find you need a shader edit, stop and ask.
5. Do not run, build, reconfigure (`-D`), or delete anything under `build\` (it is the owner's Visual Studio tree). Do not commit unless the owner says so. Do not edit `CHANGELOG.md`
   (release-only). Do not touch the owner's running editor or any existing PTTest file.
6. Test targets: plain `add_executable` + `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin`, name ends in `Test`, **no hand-written `add_test`** (the root CMake registers every `*Test` target
   with the failure regex; see the comment in `tests/formats/CMakeLists.txt`). Use the `check()` + `AVER_INFO` / `AVER_ERROR` shape of the neighbouring test.
7. Final message of each task: files changed, tests added (names), every place you deviated from the document and why, every UNVERIFIED item you hit, and any cite that did not match.

## 1. Order

```
T1  format + engine gates + format tests        (no dependency)
T2  Runtime applier + tests            -> needs T1's OcPlacedComponent and CMotionMatcher::inputSource
T3  editor: Add Component + shared plumbing   -> needs T2's API (ADD_COMPONENT.md 5.1 is the contract; may start in parallel against the header text)
T4  editor: Place Class                       -> needs T3's SandboxApp.hpp declarations (section 3 is the contract; may start in parallel)
T5  PTTest demo content                       -> no code dependency
```

Merge order for the owner's first build: T1, T2, T3, T4 (T3's `SandboxApp.hpp` includes T4's `GraphClassPlacement.hpp`, so those two must be in the tree together before the editor compiles); T5 any time.
A build with only T1 + T2 is valid and green (the editor is untouched). T5's five graph levels need none of T1-T4 (they need T4's Step 9 only to survive a second Play without reopening).

## 2. File ownership (every file appears under exactly one task)

**T1** (format, anim.scene, world; engine tests)
- `modules/formats/include/aver/formats/OcWorld.hpp`, `modules/formats/src/OcWorld.cpp`
- `modules/world/src/LevelPack.cpp`
- `modules/anim.scene/include/aver/anim/MotionMatchSystem.hpp`, `modules/anim.scene/src/MotionMatchSystem.cpp`
- `modules/anim.scene/include/aver/anim/AnimSystem.hpp`, `modules/anim.scene/src/AnimSystem.cpp`
- `tests/formats/src/OcWorldComponentsTest.cpp` (new), `tests/formats/CMakeLists.txt`
- `tests/anim.scene/src/MotionMatchSystemTest.cpp`, `tests/anim.scene/src/RootMotionSystemTest.cpp`
- `tests/levelinstance/src/LevelPackTest.cpp`, `tests/world/src/LevelInstanceTest.cpp`
- `docs/formats/FORMAT_SPECS.md` (section 11), `docs/ANIM_MOTION_MATCHING.md`, `docs/plans/wave5/MOTION_MATCHING.md` (the two `reserved` lines)

**T2** (Runtime)
- `Runtime/include/aver/game/LevelComponents.hpp` (new), `Runtime/src/LevelComponents.cpp` (new)
- `Runtime/src/GameLevel.cpp`, `Runtime/src/LevelStreaming.cpp`, `Runtime/src/LevelInstances.cpp` (the one warning, ADD 5.2), `Runtime/CMakeLists.txt`
- `tests/game/src/LevelComponentsTest.cpp` (new), `tests/game/CMakeLists.txt`

**T3** (editor: Add Component, and every `sandbox/src` file that both editor features edit)
- `sandbox/src/PlacedComponentModel.hpp` (new), `sandbox/src/SandboxComponentDetails.cpp` (new), `sandbox/CMakeLists.txt`
- `sandbox/src/EditorEntitySnapshot.hpp`, `sandbox/src/EditorEntitySnapshot.cpp`
- `sandbox/src/SandboxApp.hpp`, `sandbox/src/SandboxApp.cpp`, `sandbox/src/SandboxPanels.cpp`, `sandbox/src/SandboxSelection.cpp`, `sandbox/src/SandboxLevelEdit.cpp`, `sandbox/src/SandboxPlay.cpp`
- `tests/editor/CMakeLists.txt`, `tests/editor/src/PlacedComponentModelTest.cpp` (new), `tests/editor/src/EditorEntitySnapshotTest.cpp`
- `docs/EDITOR.md`

**T4** (editor: Place Class, its own files)
- `sandbox/src/LevelClassSave.hpp`, `sandbox/src/GraphClassPlacement.hpp` (new)
- `sandbox/src/SandboxLevelLoad.cpp`, `sandbox/src/SandboxContentBrowser.cpp`, `sandbox/src/SandboxViewport.cpp`, `sandbox/src/SandboxShell.cpp`, `sandbox/src/SandboxRender.cpp`, `sandbox/src/SandboxMcp.cpp`
- `sandbox/src/SandboxMain.cpp`, `sandbox/src/SandboxSelfTests.cpp`
- `scripting/csharp/Aver.Scripting.Bridge/HostBridge.cs`
- `tests/editor/src/GraphClassPlacementTest.cpp` (new), `tests/editor/src/GraphEditorLoadSaveTest.cpp`
- `test-content/ClassPlace/**` (new), `docs/VISUAL_SCRIPTING.md`

**T5** (the owner's test project, outside the repo; add-only)
- Under `C:\Users\User\Documents\Aver Projects\PTTest`: `Content\Demo\**`, `Content\Materials\M_DemoMannequin.ocmat`, `Content\Localization\en\DemoUI.ocstrings`, `Content\Localization\de\DemoUI.ocstrings`,
  `Content\Meshes\KenneyChar.ocragdoll`, `Tools\make_demo_chime.py`. Nothing that exists today is modified (check each target path is absent before writing; stop if one exists).

Disjointness check: `tests/editor/CMakeLists.txt` is T3's although it carries T4's test block (T4's `GraphClassPlacementTest.cpp` must exist with that exact name); `SandboxApp.hpp` is T3's although it declares T4's members.
No other file is named in two places.

## 3. The T3 / T4 contract

T3 applies these hunks in files it owns, copying from `PLACE_CLASS.md` verbatim; T4 implements the bodies. Signatures are exactly those of PLACE_CLASS.md Step 2 and Step 3.

| T3 edits (file: hunk) | Source | T4 provides |
|---|---|---|
| `SandboxApp.hpp`: `#include "GraphClassPlacement.hpp"`; the members and methods of Step 3 (`classAuthored_`, `PlayClassSaved`, `playClassSnapshot_`, `classPickList_`, `classPickDirty_`, `spawnClassSlot`, `placeClassInstance`, `dropGraphClass`, `drawGraphActorMenu`, `classSlotOfEntity`, `classRootOf`, `tagClassEdit`, `respawnClassFromEdit`, `respawnLostClassInstances`, `classPartsBounds`); `EditCmd::className`, `EditCmd::classSlot`; `classMarkerMesh_`; `setClassPlaceTestAuto`, `classPlaceTestAutoFrames_`, `runClassPlaceTest(Engine&)` beside the undo-test ones (`SandboxApp.hpp:1438,1876,2547`) | PLACE_CLASS Step 3, Step 6 (self-test wiring) | definitions in `SandboxLevelLoad.cpp` / `SandboxViewport.cpp` / `SandboxSelfTests.cpp` |
| `SandboxLevelEdit.cpp:728-740`: pass `classAuthored_` to `appendClassPlacements` | Step 5 | `LevelClassInstance::removed` |
| `SandboxSelection.cpp`: `destroyEntity` (class-aware destroy + tombstone), `recreateFrom` (first lines), `selectedRadius` (`classPartsBounds`) | Step 7 | `classSlotOfEntity`, `respawnClassFromEdit`, `classPartsBounds` |
| `SandboxPanels.cpp` (~`:1746`): the "Graph class" line and Open Graph button | Step 6, last bullet | `classSlotOfEntity`, `scanGraphClasses` |
| `SandboxPlay.cpp`: `capturePlayWorld` additions; the one call `respawnLostClassInstances();` after `restorePlayWorld();` in `stopPlay` | Step 9 | `respawnLostClassInstances` body |
| `SandboxApp.cpp`: `if (classPlaceTestAutoFrames_ > 0 && --classPlaceTestAutoFrames_ == 0) runClassPlaceTest(e);` beside `:2294` | Step 6 / 6.2 | `runClassPlaceTest` |
| `tests/editor/CMakeLists.txt`: the `GraphClassPlacementTest` block of PLACE_CLASS 6.1 | 6.1 | `tests/editor/src/GraphClassPlacementTest.cpp` |

T3's own `ADD_COMPONENT.md` hunks in these same files are listed in its task below. Where both documents add a declaration to `EditCmd` or `Kind`, append: `Kind::PlacedComponent` goes after `WaterBodies` (`SandboxApp.hpp:2010`).

---

## T1. Format, load-time gates, engine tests

**Source:** ADD_COMPONENT.md 0.0, 3 (all), 6.1, 6.2, 9.1, 9.4; `docs/formats/FORMAT_SPECS.md` section 11.

**Steps**
1. `OcWorld.hpp`: add `OcPlacedComponent` and `std::vector<OcPlacedComponent> components` as the **last member** of `OcWorldPlacement` (3.1); include `<utility>`. `OcWorld.cpp`: `find`, the `COMPONENT` parse
   branch placed **before** the `BEGIN` branch (`OcWorld.cpp:713`), and the writer (`line` lambda gets `usize depth`; call sites `:1323-1324` pass `st.depth`) per 3.3. Follow 3.2 exactly, including **Canonical writing**
   (asset keys, `mode`, `input` always written; others only off default; list syntax; `%.6g` is the caller's concern, values are opaque strings here).
2. `LevelPack.cpp:56`: `own = ... || !p.components.empty()`.
3. `MotionMatchSystem.hpp/.cpp`: `reserved` -> `inputSource` (6.1), registered writable `"inputSource"` I32, tick change at `:329-335`. Update the two lines of `docs/plans/wave5/MOTION_MATCHING.md` (`:376,380`) and add the field to `docs/ANIM_MOTION_MATCHING.md`.
4. `AnimSystem.hpp/.cpp`: `setRootMotionRequiresLive` and the `applyNow` / `mayWrite` restructure of `AnimSystem.cpp:504-510` exactly as 6.2 now words it. Default off.
5. `docs/formats/FORMAT_SPECS.md` section 11: document `COMPONENT` (grammar, the type table of 3.4, canonical writing, unknown types preserved).
6. Tests (ADD 9.1, 9.4): new `OcWorldComponentsTest` (9 cases) + CMake entry in `tests/formats/CMakeLists.txt` (links `Aver.Formats Aver.Core Aver.Platform`); `MotionMatchSystemTest` (entity-velocity input, script input unchanged,
   zero-filled matcher unchanged); `RootMotionSystemTest` (gate off in edit, on in Play, default unchanged); `LevelPackTest` (a placement with `components` is not packed, nor its children, a sibling still packs);
   `LevelInstanceTest` (`world::instantiate` ignores `components`). Do **not** edit `FormatTest.cpp`; case 3 of 9.1 reads its existing fixture text.

**Acceptance (owner)**
- Build, then green: `OcWorldComponentsTest`, `FormatTest` (its three unknown-record cases untouched), `OcLevelInstTest`, `OcSequenceTest`, `MotionMatchSystemTest`, `RootMotionSystemTest`, `AnimSystemTest`, `LevelPackTest`, `LevelInstanceTest`; the rest of ctest unchanged.
- Headed regression, on a COPY of any existing level (e.g. `NewSponza_Night.ocworld`): open, `File > Save As`, diff against the original: no `COMPONENT` line appears and nothing else differs from what the editor wrote before this change.
- Nothing visible changes in the editor from T1 alone.

---

## T2. Runtime applier

**Source:** ADD_COMPONENT.md 4.1-4.9 (rules per component), 5, 9.2.

**Steps**
1. `Runtime/include/aver/game/LevelComponents.hpp` + `Runtime/src/LevelComponents.cpp`: the API of 5.1 (`applyPlacedComponent`, `removePlacedComponent`, `capturePlacedComponent(s)`, `applyPlacedComponents`, `applyLevelComponents`) and `kPlacedComponentTypes` in the **table order**
   of 3.4 (SoftBody last). Honour every rule of 5.1 (attach through the system and write every authored field; dependency checks; `database` written last for the matcher; idempotent). `%.6g` / `std::from_chars` helpers local to the TU (3.2, Number spelling).
   Anim code under `AVER_MODULE_SCENE`, Brain / Smart Object under `AVER_MODULE_SYNAPSE_SCENE`; the header names no Synapse type. Smart Object goes through the scene string-field ABI (4.8), never the Synapse DLL ABI.
   Log the one INFO line and per-record warnings exactly as the header comment in 5.1 says (the demo greps `[Level] 18 placed component(s) on 7 placement(s)`).
2. Call sites (5.2): `GameLevel.cpp` immediately before `hooks_.afterInstantiate(...)` (`:516-517`), `#if AVER_MODULE_SCENE`, skipped for legacy; `LevelStreaming.cpp` between the `loaded` loop and `hooks_.afterLoad` (`:243-250`), warnings only;
   `LevelInstances.cpp`: one `AVER_WARN` per realised instance whose child level has placements with components.
3. `Runtime/CMakeLists.txt`: add `src/LevelComponents.cpp` to `Aver.Runtime.Game.Core` sources; `if(TARGET Aver.Synapse.Scene) target_link_libraries(Aver.Runtime.Game.Core PRIVATE Aver.Synapse.Scene) endif()` (**PRIVATE**, 5.2 explains why).
4. Optional last item, droppable: Soft Body (4.9): apply / capture cases only.
5. Tests: `tests/game/src/LevelComponentsTest.cpp` (9.2, eight groups) + CMake entry in `tests/game/CMakeLists.txt` in the style of `LevelLightsTest`, linking `Aver.Runtime.Game.Core Aver.Anim.Scene Aver.Scene Aver.Formats Aver.Core` and, under `if(TARGET Aver.Synapse.Scene)`, `Aver.Synapse.Scene`
   (AI cases guarded by `AVER_MODULE_SYNAPSE_SCENE`).

**Acceptance (owner)**
- Build both hosts (`Aver.Runtime.Game` and the editor must still link: `Aver.Synapse.Scene` is PRIVATE to `Game.Core`).
- Green: `LevelComponentsTest`, `LevelLightsTest`, `PlayMobilityTest`, `InputBridgeTest`, `LevelSequenceTest` and the rest of `tests/game`.
- Headed: none on its own. After T3, opening `Demo_Components.ocworld` logs `[Level] 18 placed component(s) on 7 placement(s)`.

---

## T3. Editor: Add Component, plus the shared `sandbox/src` plumbing

**Source:** ADD_COMPONENT.md 4, 6.2 (the `SandboxApp.cpp:618` call), 6.3, 7, 9.3, 10; PLACE_CLASS.md hunks listed in section 3 above.

**Steps**
1. `PlacedComponentModel.hpp` (7.2): `PlacedKind` table in the 3.4 order with the menu labels of 0.0, `AttachContext`, `canAttach`, `canRemove`, `listAssets`, `makeNewRecord`, `presetSkeletonFor`. Header-only, no ImGui.
2. `EditorEntitySnapshot.hpp/.cpp` (6.3): record and re-set `FieldKind::String` fields on capture / instantiate.
3. `SandboxApp.hpp`: the ADD 7.3 declarations (`drawPlacedComponentSections`, `addPlacedComponent`, `removePlacedComponentFromSelection`, `commitPlacedEdit`, `drawAssetField`, `pathOfContentId`, `placedBefore_`, `placedBaseline_`, `applyPlacedComponentState`), `EditCmd::Kind::PlacedComponent` and its payload members; **plus** the PLACE_CLASS declarations of section 3.
4. `SandboxComponentDetails.cpp` + `sandbox/CMakeLists.txt` (add the source): `drawPlacedComponentSections`, the per-kind sections with the rows of 4.1-4.8, add / remove / commit with one undo entry per gesture, `drawAssetField`, `applyPlacedComponentState`.
5. `SandboxPanels.cpp`: the section call at `:2481`, the "Animation and AI" separator and loop in both popups (7.1), `levelOwned` computed only inside the popup body; **plus** the PLACE_CLASS Details class line.
6. `SandboxSelection.cpp`: undo / redo cases (`:1235`, `:1305`) calling `applyPlacedComponentState`; **plus** the three PLACE_CLASS Step 7 hunks (`destroyEntity`, `recreateFrom`, `selectedRadius`).
7. `SandboxLevelEdit.cpp`: `p.components = game::capturePlacedComponents(...)` before `return true` in `fillPlacementFromEntity` and the baseline use while `playWorldCaptured_` is true (7.4); **plus** PLACE_CLASS Step 5.
8. `SandboxPlay.cpp`: `placedBaseline_` capture / clear (7.4, near `:262`, `:427`, `:463`, `:700`); **plus** PLACE_CLASS Step 9 (`capturePlayWorld`, the one call in `stopPlay`).
9. `SandboxApp.cpp`: `anim::animSystem().setRootMotionRequiresLive(true);` beside `controlRigSystem().install` (`:618`, inside `AVER_MODULE_SCENE`); **plus** the `--class-place-test` tick line.
10. Tests: `PlacedComponentModelTest` (9.3) and the `EditorEntitySnapshotTest` extension (string fields, undo mode and paste mode, distinct slots) in `tests/editor`; CMake blocks for `PlacedComponentModelTest` **and** `GraphClassPlacementTest` (PLACE_CLASS 6.1, verbatim). `docs/EDITOR.md`: one section on the Add Component entries.

**Acceptance (owner)**
- Green: `PlacedComponentModelTest`, `EditorEntitySnapshotTest`, `SnapshotUndoTest`, `LightDetailsTest`, `DropPlacementTest`, `LevelComponentsTest`; the existing `Sandbox.exe --project ... --undo-test` still exits 0.
- Headed (visible window, never `--headless`), on a COPY of a PTTest level, ADD_COMPONENT.md section 10 steps 1-12, and `Demo_Components.ocworld` once T5 has landed: the acceptance rows in DEMO_CONTENT.md section 7 ("Demo_Components").
  In particular: Add Component > Skeletal Mesh preselects `Meshes/KenneyChar.ocskel` and Bones reads 7; Animator idles the figure without Play; Ctrl+Z and redo step each Add, Remove and edit; Ctrl+S then reopen restores every section; the saved text contains
  `COMPONENT SkeletalMesh skeleton=Meshes/KenneyChar.ocskel` under the placement line; Root Motion does not drift outside Play; every entry is disabled with a reason on a graph-class actor and on a prefab member.
- A duplicate (Ctrl+D) of an entity with a Smart Object edits independently (the string-slot fix).

---

## T4. Editor: Place Class

**Source:** PLACE_CLASS.md Steps 1, 2, 4, 6 (entry points), 7 (pick, `deleteSelection`, duplicate guard, `selectionBounds`), 8, 10, 11, 12; 6.1, 6.2, 6.3; section 3 above.

**Steps**
1. `LevelClassSave.hpp`: `removed`, the skip in `appendClassPlacements`, `classRootOf` (Step 1). `GraphClassPlacement.hpp` (Step 2): `parseGraphClassDecl`, `scanGraphClasses`, `makeClassPlacement`, `classPlaceRefusal`, `isGraphAssetExt`, `classMarkerSegments`; header-only, Core + Formats only.
2. `SandboxLevelLoad.cpp` (Step 4): `onLevelInstantiated` fill, `spawnClassSlot`, `placeClassInstance`, `classSlotOfEntity`, `classRootOf`, `tagClassEdit`, `respawnClassFromEdit`, `respawnLostClassInstances` (body in Step 9), `classPartsBounds`, `unloadLevel` additions.
3. Entry points (Step 6): `SandboxContentBrowser.cpp` `isPlaceableAssetExt`; `SandboxViewport.cpp` `spawnFromAssetDrop` branch, `dropGraphClass`, `drawGraphActorMenu`; `SandboxShell.cpp` Add submenu and wording.
4. `SandboxViewport.cpp` (Step 7): `pick()` class-root redirect and marker hit test, `deleteSelection` tagging, the Copy / Duplicate guard, `selectionBounds`. `SandboxRender.cpp` (Step 8): marker lines.
5. `SandboxMcp.cpp` (Step 10): accept class lines. `HostBridge.cs` (Step 11): silent same-path redeclare. `SandboxMain.cpp` + `SandboxSelfTests.cpp`: `--class-place-test` parse and `runClassPlaceTest` (6.2).
6. `docs/VISUAL_SCRIPTING.md` (Step 12). `test-content/ClassPlace/**` fixture with `ENGINE Aver 0.6.0-1` / `CREATEDWITH 0.6.0-1` (PLACE_CLASS 6.2 now says why).
7. Tests: `GraphClassPlacementTest.cpp` (nine groups, 6.1) and the `GraphEditorLoadSaveTest.cpp` additions after `:1523`.

**Acceptance (owner)**
- Green: `GraphClassPlacementTest`, `GraphEditorLoadSaveTest`, `DropPlacementTest`, `GraphAssetPresentationTest`, `Aver.Graph.Tests` (untouched, must still pass).
- Headed: `Sandbox.exe --project test-content/ClassPlace/Game.ocproject --class-place-test` exits 0 with every check passing (it never runs `--headless`).
- Headed manual, PLACE_CLASS.md 6.3 steps 1-10 on a COPY of a PTTest level: drag `AN_FPTarget.ocgraph` (ball appears, status "Placed AN_FPTarget"); drag `AN_FPCharacter.ocgraph` (body visible at rest pose; if it is invisible that is the finding to report);
  click selects the class root; Delete, Ctrl+Z, redo round-trip; Save writes `PLACE none ... class AN_FPCharacter`; Play then Stop leaves both actors in place; `Add > Actor From Graph` lists the five FP classes with `AN_FPRules` and `AN_FPController` disabled;
  Ctrl+D on a placed actor refuses with the status text; the log has no repeated "declared by more than one graph" line.

---

## T5. PTTest demo content

**Source:** DEMO_CONTENT.md sections 4, 5 (step 1 only), 6.1-6.9. Copy every file body exactly as written (LF line endings, ASCII only, node ids untouched, class names `AN_Demo*` unchanged).

**Steps**
1. Create, under `PTTest\Content\Demo`: `Scripts\` (sixteen `.ocgraph`), `Anim\` (`KenneyLocomotion.ocmotion`, `Strike.ocmontage`, `Lunge.ocmontage`, `KenneyReach.ocikrig`, `MannequinIk.ocikrig`, `KenneyLook.ocrig`, `KenneyLookStatic.ocrig`, `KenneyToMannequin.ocretarget`),
   `Gameplay\Demo.ocgas`, `Input\Demo.ocinput`, `Data\DemoSpells.csv`, `AI\` (`Bench.ocso`, `Patron.ocst`, `FindSpot.oceqs`, `FindNearPlayer.oceqs`), `Seq\DemoPulse.ocseq`, `Maps\` (six `.ocworld`), `README.md` (the 6.8 body, including the Demo_Components section).
2. Create the four files outside `Content\Demo` (DEMO 4): `Content\Materials\M_DemoMannequin.ocmat`, `Content\Localization\en\DemoUI.ocstrings`, `...\de\DemoUI.ocstrings`, `Content\Meshes\KenneyChar.ocragdoll`, and `Tools\make_demo_chime.py` (6.7). Do not create `Audio\chime.wav`: the owner runs the script.
3. Self-check without running anything: every path a record names exists in section 4's layout or in the PTTest listing; every `COMPONENT` line of `Demo_Components.ocworld` is character-for-character the 6.9 text (it is the canonical form ADD_COMPONENT.md 3.4 writes); the level headers match the other five (`GAMEMODE AN_DemoRules`, `SPAWN`, `CAMERA`).

**Acceptance (owner)** (the owner steps of DEMO_CONTENT.md section 5, items 2-5, come first: run the chime script, set the kick clip's Root motion flag, pick the input scheme, reopen the project)
- Output Log at project open: sixteen `[Graph] declared class 'AN_Demo...'` lines, no `[GAS]` error mentioning `Demo/`, no "was not found" for a `Demo/` path.
- DEMO_CONTENT.md section 7, level by level. Headed. Needs T1-T3 for `Demo_Components` (without them the level opens with static meshes and no `COMPONENT` effect, by design) and T4's Step 9 for a second Play without reopening.
- Optional follow-ups the owner may ask for later (DEMO 11): a table-driven test that parses every demo asset with its real reader, and an `Aver.Graph.Tests` case that compiles each demo graph.

---

## 5. Owner integration run (after T1-T5)

1. Build everything once (`aver_build`, Debug tree, the owner's usual routine). Expect no warnings-as-errors from the new files; report any.
2. `ctest`: every target named in the tasks above, then the whole suite; compare with the pre-change baseline.
3. Headed passes, in this order, each on copies of owner levels: T1 regression save; T3 (ADD section 10); T4 (`--class-place-test`, then PLACE 6.3); T5 (DEMO README steps, then section 7 level by level).
4. Packaged-game proof (ADD 10 step 11, DEMO U27): package the project with `Demo_Components` as the start map and confirm the same components exist (`aver_package`, then `AverEngineRuntime`).
5. The things most likely to fail first (each is an UNVERIFIED item below): skinned placement looks wrong in edit mode; the retarget bake on the mannequin; Stop/respawn of class actors; foot IK finding the block.

## 6. Open questions for the owner

1. **T5 writes into `PTTest` directly** (your test project, add-only, nothing existing modified). Fine, and with the editor closed so the content index is not hot-reloaded half way? The alternative is a staging copy inside the repo that you copy over yourself.
2. **Soft Body persistence** (ADD 4.9) comes for free with the same record; included as T2's last, droppable item. Keep it?
3. The existing bug that a **Light or Decal added to a mesh entity is saved twice** (ADD 1.2) is left alone. Want it fixed as a separate task?
4. **Level instances** (`LEVELINST`) apply `COMPONENT` records to their members (done 2026-10-12; was a v1 warning).
5. PLACE_CLASS side finding: `spawnFromAssetDrop`'s `.ocprefab` and `.ocworld` branches look unreachable from a viewport drop (`isPlaceableAssetExt` refuses both). Separate fix, not in these tasks.
6. The demo needs four of your own steps before it runs (chime script, kick-clip Root motion flag, input scheme, reopen). OK, or should an importer/tool step replace the flag one?
7. DEMO levels need Stop/respawn (T4 Step 9) to survive a second Play; until it lands the README says to reopen the level.

## 7. UNVERIFIED, consolidated

Settled by the reviewer pass (no longer open): `buildStreamedPlacements` calls `fillPlacementFromEntity` (SandboxLevelStream.cpp:252); `BtSystem::prune` drops a removed brain's runtime; `registerComponents` of the smart-object system works without `bind()`;
`CMotionMatcher.reserved` has no other user; `KenneyChar.ocmesh` is skinned (MHDR flags `0x24`) and `KenneyChar.ocskel` has 7 bones; F7 (Stop destroys class actors) is confirmed from code (`FrameworkAbi.cpp:1017-1040`); the repo version is `0.6.0-1`, not `0.7.0`.

Still unverified (nobody built or ran anything):
1. A skinned placement (and a graph-class `COMP SkeletalMesh`) draws correctly in the edit-mode viewport, and `PlayMobility` treats it correctly for GI / RT (ADD 12.1; PLACE 6.3 step 2).
2. F7 at runtime: that Stop really removes class actors in the editor (PLACE 7). Step 9 is guarded by `valid()` and is a no-op if the sweep spares them.
3. A CLASS + COMP graph with no nodes loads in `GraphHost` (the `ClassPlace` fixture; PLACE 6.2). Fallback is in the document.
4. IK feet in edit mode and in Play over a placed mannequin (ADD 4.6, DEMO U11, U25); the Kenney-to-mannequin retarget bake through a plain `Animator clip=` (DEMO U10).
5. Tag-gated motion matching, montage-over-matcher, root-motion lunge, CCD lean, head-aim axis, hinge break forces, EQS ring parse, state-tree event chain, sequencer sub-sequence path, `AN_CreateUiWidget`, combo trigger without a binding (DEMO U3-U21).
6. Whether Play start resets GAS / Synapse state set up by `OnStart` at level load (DEMO U22).
7. `DeclareGraphClasses` cost on a large Content tree (PLACE 7), and `HostBridge` has no C# test project, so Step 11 is verified by log only.
8. That asset-reference scans (Content Browser "who references this", rename rewrite) see `skeleton=` / `clip=` keys inside `.ocworld` text (ADD 1.3).
