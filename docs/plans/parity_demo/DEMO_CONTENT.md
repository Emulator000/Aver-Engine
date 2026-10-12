# Parity demo content for PTTest (`Content/Demo`)

Part: **demo-content**. Owner request 2026-10-12: make the recently added Unreal-parity features testable without
writing code. This file specifies every asset of a demo set that lives in the owner's test project
`C:\Users\User\Documents\Aver Projects\PTTest` (called PTTest below), in full text, so an implementer who cannot
build or run anything can copy it verbatim. It changes no engine code.

Conventions used throughout:

- `U<n>` points at an entry of the UNVERIFIED register in section 9: a claim nobody has run. Everything not tagged
  was read in the source and is cited as `file:line`.
- "Model units" means the units of a skeleton file; "cm" means world centimetres. +X forward, +Y right, +Z up,
  left-handed (`docs/physics/QUERIES.md`).
- **Add Component** is the Details panel feature specified in ADD_COMPONENT.md (today `SandboxPanels.cpp:2491-2498` offers only Soft Body, Light and
  Decal). **Place Class** is the drag-a-graph / `Add > Actor From Graph` feature specified in PLACE_CLASS.md (today a hand edit of a
  `PLACE none ... class AN_Foo` line, `docs/VISUAL_SCRIPTING.md` section 6). Section 3 says exactly what this content needs from them
  (nothing mandatory for the five graph levels; `Demo_Components` needs Add Component's `COMPONENT` record to be read).

**Status: FROZEN 2026-10-12 by the reviewer pass.** Cross-checked against ADD_COMPONENT.md and PLACE_CLASS.md: component names, `COMPONENT`
record syntax and the placement mechanism below are exactly theirs (ADD_COMPONENT.md 0.0 is the authoritative name table). The pass added a sixth level,
`Demo_Components` (6.9), which uses the new `COMPONENT` records on ordinary placements; the five graph-driven levels are unchanged apart from the
corrections listed in section 13. Implementation order and file ownership are in `docs/plans/parity_demo/TASKS.md` (task T5 is this file's content).

What was and was not checked: every structured asset was compared by reading against its parser and writer (field names, grammar, `valid()`
rules) and every graph was checked mechanically for node types, pin names and directions, pin types, one link per exec output,
reachability from an `ENTRY` and required attributes (section 8). Nothing was built or run, so behaviour is as certain as the UNVERIFIED register
says and no more.

## 1. What the six levels prove

| Level | Pawn | Parity features exercised (plan feature numbers from `docs/plans/UE_FEATURE_PARITY_PLAN.md`) |
|---|---|---|
| `Demo_Animation` | `AN_DemoHero` (Kenney figure, third person) | #25 motion matching (`.ocmotion`, tags, search request, matcher state), #26 montages with sections and notifies (`.ocmontage`), root motion Apply and Accumulate, #24 IK rig (`.ocikrig` CCD chain, goal bound to an entity, goal blend), control rig v2 with named inputs (`.ocrig`), retargeting (`.ocretarget`, runtime bake), foot IK with pelvis solve and a twobone hand reach on the 45-bone mannequin, #28 ragdoll (`.ocragdoll` override and the automatic builder) |
| `Demo_Gameplay` | `AN_DemoViewer` | #18 gameplay ability system (`.ocgas`: attribute set with a meta attribute, instant / duration / periodic / infinite effects, stacking, SetByCaller, abilities with cost and cooldown, graph-authored ability bodies, attribute and tag events), #19 enhanced input (`.ocinput`: tap, hold, combo triggers), data tables (`.csv`), localisation (`.ocstrings`, two cultures, runtime switch) |
| `Demo_AI` | `AN_DemoViewer` | #21 StateTree (`.ocst`), smart objects (`.ocso`, claim / use / release, a slot `USE event`), EQS (`.oceqs`, two queries: one run by a StateTree task, one run by a graph node), brain / smart-object / EQS graph nodes |
| `Demo_Physics` | `AN_DemoViewer` | physics wave 1: box cast and capsule cast, breakable hinge joint, locked/continuous collision (CCD) against none, #28 ragdoll with a shove |
| `Demo_Sequencer` | `AN_DemoViewer` | #27 sequencer: transform, property (float and colour), visibility, event, audio (plain and positional), camera, camera-cut and sub-sequence tracks, a standalone `.ocseq`, autoplay, lifecycle events, the eight sequencer graph nodes |
| `Demo_Components` | `AN_DemoViewer` | Add Component records on ordinary placements, no graph: Skeletal Mesh + Animator, Root Motion (Apply), Motion Matching, Control Rig, IK Rig (foot placement on the mannequin), AI Brain (state tree) and Smart Object, and their save / reload round trip (ADD_COMPONENT.md). Needs the Add Component engine work; without it the level opens with static meshes |

Not covered here because the request did not ask for them: level instances, landscape layers, many-light sampling,
material instances, texture streaming, virtualized geometry, post volumes.

## 2. Facts the design stands on

Measured facts about the PTTest content were read directly from the binary files (AVR1 chunks, `docs/formats/FORMAT_SPECS.md`
sections 3, 5, 8, 9) with a throw-away script; code facts are cited.

| # | Fact | Where it comes from | Consequence for the content |
|---|---|---|---|
| F1 | `Meshes/KenneyChar.ocmesh` is a skinned mesh: MHDR flags `0x24` = HasMeshlets + HasSkin, 3 LODs, 2 submeshes, JOINTS and WEIGHTS streams; bounds (-16.8,-38.4,0) to (17.2,38.4,67.1) | file bytes | It can be a `COMP ... SkeletalMesh`. It is 67 units tall, so the child needs scale 2.6 (the owner's `Default.ocmap` places it at scale 2.6) |
| F2 | `Meshes/KenneyChar.ocskel` has 7 bones: `root`(0), `leg-left`(1), `leg-right`(2) and `torso`(3) under `root`, `arm-left`(4), `arm-right`(5) and `head`(6) under `torso`. Every bind rotation is identity, scale 1. Left bones sit at +Y | file bytes | The legs are single leaf bones: no knee, no ankle. A leg IK chain cannot exist (F13). Head, arms and torso can be IK / look-at targets |
| F3 | The 32 `KenneyChar_*.ocanim` clips all have header flags 0 (not looping, not additive, not root-motion) and no notify or curve chunk. `walk` (0.667 s) moves only the `root` bone, in Z, 0 to 5 units; `sprint` (0.5 s) Z 0 to 20; `idle` (1.333 s) has no root track. `attack-melee-right` (0.417 s) root X -9.96 to +4.98, `attack-kick-right` (0.533 s) X -9.87 to +10; both return to 0. No clip travels | file bytes | Motion matching has no trajectory signal, so tags select the clip family (section 6.2). Looping is set per entry in the `.ocmotion`. Root motion needs the Root-motion flag set once in the Anim tab, and a lunge has zero net travel, so Accumulate is sampled mid-clip |
| F4 | `Skeletons/Character.ocskel` has 45 bones (`Hips`, `Spine`, `Chest`, `UpperChest`, `Neck`, `Head`, `LeftShoulder` ... `RightToes`, plus control bones). `Character.ocmesh` is skinned and 37,654 units tall; the First Person template scales it 0.0047813 for 180 cm (`templates/FirstPerson/Content/Scripts/AN_FPCharacter.ocgraph` COMP record), so **1 cm = 209.2 model units**. Rest pose in model units: Hips z=12,422, Head z=26,985, feet z=1,932, toes z=131; left bones at +Y; each toe lies at smaller X than its ankle, so the figure faces -X | file bytes, rest pose composed locally | Every IK number for the mannequin is in model units (F13). The COMP child needs yaw 180 to face +X |
| F5 | Kenney figure facing: glTF forward -Z becomes engine +X (`docs/ASSET_IMPORT.md:15-17`), Kenney models face glTF +Z, so they face -X; the left-bone sides in F2 agree | doc + F2 | `rot=180,0,0` on every skinned COMP (U1) |
| F6 | `Meshes/cube.ocmesh` is +-1, so `PLACEG` scale is the half extent (`Runtime/src/GameContent.cpp:253`). `Meshes/sphere.ocmesh` has radius 1. The only runtime built-in meshes are cube, sphere and drone (`GameContent.cpp:261-280`); a cylinder exists only in the editor preview (`modules/render.actorpreview/src/PreviewMeshCache.cpp:247`) | code | Every visible primitive below is a cube or a sphere |
| F7 | A material name resolves to `Binaries/Materials/<n>.ocmat`, `Content/Materials/<n>.ocmat` or `Content/<n>` (`GameContent.cpp:1125-1129`). `KenneyChar_colormap` is in PTTest `Binaries/Materials`; the `Caldera_*` materials are in `Content/Materials` | code + listing | The new mannequin material must go in `Content/Materials` (one of the files outside `Content/Demo`, section 4) |
| F8 | Rotated static level boxes collide with their rotation (`modules/world/src/LevelInstance.cpp:96-107`); `docs/SYNAPSE.md` section 1 saying otherwise is stale | code | A pitched `PLACEG` is a valid ramp collider (sign of the pitch: U13) |
| F9 | `.ocgraph`: only whole-line `#` comments (`scripting/csharp/Aver.Graph/OcGraphParser.cs:36-48`), tokens split on whitespace with no quoting (`:2617`), so attribute values (`text=`, `table=`, `asset=`) cannot hold spaces. An exec output may drive at most one link (`Graph.cs:710-733`, `GraphCompiler.cs:2907-2913`) while an exec input may take many; link pin types must match exactly (`Graph.cs:505-511`); an unwired data input reads 0 / false (`GraphCompiler.cs:4113-4115`); any `PIN` record on a node removes all its default pins (`OcGraphParser.cs:814`); two `ENTRY` records may not share an event name (`Graph.cs:568`) | code | Every graph below keeps to those rules, and Sequence nodes with declared `then0..thenN` pins stand in for fan-out |
| F10 | An event entry (`AN_OnEvent`, `CustomEvent`, `OnInputAction`) runs with only `entity`, `time` and `deltaTime` (always 0) available as PARAMs (`GraphHost.cs:539-573`) | code | No `dt`-driven motion in an event chain; event graphs declare no other PARAM |
| F11 | `Print`, `PrintInt`, `PrintString` log `[Graph] <node id> = <value>` / `<node id>: <text>` (`scripting/csharp/Aver.Framework/GraphInterop.cs:333-359`); the editor viewport shows the last 12 such lines, collapsing consecutive duplicates (`sandbox/src/SandboxApp.cpp:94-104`, `SandboxApp.hpp:990`) | code | The node id is the on-screen label, so Print nodes are named for what they show, and nothing prints every tick |
| F12 | `AverCharacter.MoveSpeed` is 350 (`Character.cs:47`), the wish vector is normalised (`:244`), and it feeds a `CMotionMatcher` only if it finds one on its own entity (`:270-280`) | code | Walk and run differ by clip only; the Kenney body is a scaled child, so the graph feeds its matcher itself |
| F13 | IK rig thresholds are in the skeleton's model units: ground probes are cast `distance * entityWorldScale` cm (`modules/anim.scene/src/IkRigSystem.cpp:146,191`). A chain needs `start != end` (`modules/anim/src/IkSolvers.cpp:304`) and at least two unclaimed bones (`IkRigSolver.cpp:307`) | code | No foot chain on Kenney. Mannequin numbers below are cm x 209 |
| F14 | Synapse advises, it does not move (`docs/SYNAPSE.md` section 2). The `MoveTo` / `MoveToSmartObject` leaves need a `CSynapseAgent` already on the entity (`modules/synapse.scene/src/SynapseAi.cpp:22-31`); no graph node attaches one (grep over `sandbox/src/GraphNodeDefs*.hpp`, `scripting/csharp/Aver.Graph/*NodeTable.cs`) | code | The Patron is an Actor moved by the graph (SetLocalPosition + LookAt); the tree only decides. Gap G1 |
| F15 | A smart-object `USE event <Name>` raises that name on the user through the graph event seam; `UseSmartObject` is Running while the slot is Occupied; leaving the state aborts it and releases the claim (`SynapseAi.cpp:331-362`, `docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md` sections 2, 4). In a StateTree tasks of one state run in parallel, siblings run in order, and the default `COMPLETE` is `all` when a state has tasks (`modules/synapse/src/StateTreeAsset.cpp:158-161`) | code | The Patron's tree is a chain of root states, not one state with ordered tasks |
| F16 | `UiSystem.OpenLayout(path)` does `File.ReadAllText(path)` on the path as given (`scripting/csharp/Aver.UI/Widgets.cs:367-377`), i.e. relative to the process's working directory, not the content root | code | No `.ocui` HUD file; `AN_CreateUiWidget` makes the one text widget (Gap G2) |
| F17 | Retarget bakes are looked up by (source file stem, target skeleton id) from every `.ocretarget` in the content index (`modules/anim.scene/include/aver/anim/AnimRetarget.hpp:15-34`); the index is registered at project open (`sandbox/src/SandboxProject.cpp:167-168`) | code | New `.ocretarget` / `.ocgas` / graphs need the project reopened |
| F18 | A ragdoll part exists for a bone with a child (capsule toward the longest child), none for a leaf unless forced by `<skeleton>.ocragdoll`; the file sits beside the skeleton (`modules/ragdoll/src/RagdollBuilder.cpp:61-121`, `docs/physics/RAGDOLL_ANIMATION.md`) | code | `Content/Meshes/KenneyChar.ocragdoll` (outside `Content/Demo`) |
| F19 | A COMP child entity is named by its COMP id (`GraphComponentTree.cs:52`, `Entity.cs:31-36`); `FindEntity` returns the first live entity of that name (`GraphInterop.cs:699-704`) | code | Each graph class uses unique COMP ids and a level places each such class once |
| F20 | `.ocworld` lines are cut at the first `#` (`modules/formats/src/OcWorld.cpp:272`); sequence text fields are percent-encoded (`modules/formats/src/OcSeq.cpp:101-166`) | code | A sub-sequence reference `file.ocseq#Name` is written `file.ocseq%23Name` |
| F21 | Enhanced-input trigger semantics (tap, hold with `once`, combo) are `modules/framework/src/ActionEngine.cpp:92-210`; input events reach a graph as the bus events `Input.<action>.<event>` (`docs/AVER_NODE_NODES.md` "Enhanced Input") | code | `AN_DemoInputProbe` has only event entries |
| F22 | PLACE grammar: `PLACE <asset> x y z yaw pitch roll scale [tokens]`, `PLACEG` takes three scales; tokens `class <Name>`, `name <n>`, `nocollide` precede the material fallback (`OcWorld.cpp:726-811`). A level `PLACE` has no skeleton field | code | Skinned actors exist as graph classes with a `COMP ... SkeletalMesh` (`GraphComponentTree.cs:121-136`) or, once ADD_COMPONENT.md lands, as a placement plus `COMPONENT SkeletalMesh skeleton=...` (`Demo_Components`) |
| F23 | Editor Play reads the level's `GAMEMODE` and `DEFAULTPAWN` (a class must carry the Pawn flag, which a `Character` graph class inherits) and spawns the possessed pawn at the Player Start, which is the level's `SPAWN` record, unless the Play toolbar's spawn-at option is Camera Location (`sandbox/src/SandboxPlay.cpp:341-361, 440-466`, `sandbox/src/SandboxViewport.cpp:710-724`) | code | `Demo_Animation` names `DEFAULTPAWN AN_DemoHero`; every level carries `SPAWN` (and a `CAMERA` near it, so the Camera Location option also works) |

## 3. The two editor features, by name

**None of the five graph levels requires Add Component or Place Class.** Both are conveniences over things the files already do:

- **Place Class** (PLACE_CLASS.md): the five graph levels already contain the `PLACE none x y z yaw pitch roll 1 class AN_Demo...` lines
  (parsed at `OcWorld.cpp:765`). With the feature an author can drag any `Content/Demo/Scripts/*.ocgraph` that declares a `CLASS` into a level, or use
  `Add > Actor From Graph`, and the editor writes the same record (`makeClassPlacement`, PLACE_CLASS.md Step 2). Two rules carry over: (1) a class
  placement's `name <n>` token is kept in the file but does not rename the spawned actor (the spawner passes the CLASS name, `SandboxLevelLoad.cpp:690`,
  `GameLevel.cpp:597`), so a sequencer track cannot rely on that token for a class actor (it binds by Outliner label, `SandboxLevelStream.cpp:275-281`); Demo_Sequencer binds only mesh placements and a light; (2) after Play then Stop the editor destroys
  every class actor in the level (`FrameworkAbi.cpp:1017-1040`) and only PLACE_CLASS Step 9 respawns them, so until that lands the owner reopens the level
  after Stop (the README says so).
- **Add Component** (ADD_COMPONENT.md): every component the five graph levels need is attached by a graph node at run time instead
  (`SetIkRig` adds `CIkRig` and `CAnimator`, `Ik.cs:33-40`; `SetMotionMatching` adds `CMotionMatcher`, `MotionMatching.cs:46-57`; `SetRootMotion` adds
  `CRootMotion`, `RootMotion.cs:46-62`; `SetControlRig` adds `CControlRig`; `AN_BrainSet` adds the AI brain; `AN_SoAttach` adds `CSmartObject`).

**Add Component does replace the graph for the plain case**, which this file's first draft said it could not: ADD_COMPONENT.md puts a skeleton on an ordinary
placement through `COMPONENT SkeletalMesh skeleton=...` (a `PLACE` line itself still has no skeleton field, F22), so a Kenney figure can be placed, selected,
given an Animator and inspected before Play. `Demo_Components` (6.9) is that case. It cannot replace the graphs for gameplay (keys, abilities, the Patron's
movement, ragdoll triggers, goals bound to the orbiting ball): those need the graph, so the other five levels stay graph-driven.

The names this content uses, copied from ADD_COMPONENT.md 0.0 (do not respell them): menu labels `Skeletal Mesh`, `Animator`, `Root Motion`, `Motion Matching`,
`Control Rig`, `IK Rig`, `AI Brain`, `Smart Object`; record types `SkeletalMesh`, `Animator`, `RootMotion`, `MotionMatcher`, `ControlRig`, `IkRig`, `Brain`,
`SmartObject`; record syntax `COMPONENT <Type> key=value ...` on its own indented line directly under the `PLACE` line it belongs to. One entry this content
would use if the menu offered it: `Synapse Agent` (`CSynapseAgent`), which no node and no menu attaches today (F14, G1); with it the Patron could use the
`MoveToSmartObject` leaf over a baked nav grid instead of walking itself. It is not in ADD_COMPONENT.md's scope.

## 4. Layout

Everything is under `C:\Users\User\Documents\Aver Projects\PTTest`. Paths inside files are content-relative
(`Meshes/...`, `Demo/...`), never starting with `Content/` (the id is a hash of the string, `GraphComponentTree.cs:108-113`).

```text
Content/
  Demo/
    README.md                     section 6.8   what to press
    Maps/    Demo_Animation.ocworld  Demo_Gameplay.ocworld  Demo_AI.ocworld  Demo_Physics.ocworld  Demo_Sequencer.ocworld
             Demo_Components.ocworld   (6.9: COMPONENT records, no graph class except the shared player)
    Scripts/ AN_DemoController  AN_DemoRules  AN_DemoViewer  AN_DemoOrbiter  AN_DemoHero  AN_DemoMannequin
             AN_DemoMannequinIk  AN_DemoCaster  AN_DemoInputProbe  AN_DemoBench  AN_DemoPatron  AN_DemoEqsProbe
             AN_DemoCastRig  AN_DemoHinge  AN_DemoCcd  AN_DemoSeqListener          (all .ocgraph)
    Anim/    KenneyLocomotion.ocmotion  Strike.ocmontage  Lunge.ocmontage  KenneyReach.ocikrig  MannequinIk.ocikrig
             KenneyLook.ocrig  KenneyLookStatic.ocrig  KenneyToMannequin.ocretarget
    Gameplay/ Demo.ocgas      Input/ Demo.ocinput      Data/ DemoSpells.csv
    AI/      Bench.ocso  Patron.ocst  FindSpot.oceqs  FindNearPlayer.oceqs
    Seq/     DemoPulse.ocseq
    Audio/   chime.wav        (generated by Tools/make_demo_chime.py, not committed text)
  Materials/M_DemoMannequin.ocmat           outside Content/Demo: F7
  Localization/en/DemoUI.ocstrings          outside Content/Demo: the table path is fixed by the loader
  Localization/de/DemoUI.ocstrings
  Meshes/KenneyChar.ocragdoll               outside Content/Demo: it must sit beside the skeleton (F18)
Tools/make_demo_chime.py
```

Four locations lie outside `Content/Demo`: the material, the ragdoll override, the localisation pair and the helper script. Derived files the editor will write next to the assets
(`Demo/Anim/KenneyLocomotion.ocmbake`, content-index caches) are generated, not authored; do not commit `*.ocmbake`.

## 5. Steps for the implementer

**Who does what.** The implementer (cannot build or run) does step 1 only: creates every file of section 6 and adds nothing else. Steps 2-5 need the editor or
Python and are the owner's, in that order; they are repeated in `Content/Demo/README.md`. Do not run `Tools/make_demo_chime.py` or open the editor.

1. Create the files of section 6 exactly as written (LF line endings, ASCII only). Do not "tidy" node ids: the Print
   nodes' ids are the labels the acceptance checks in section 7 look for.
2. `Tools/make_demo_chime.py` (section 6.7) writes `Content/Demo/Audio/chime.wav`; run it once.
3. Editor-only steps, no code, both are Content Browser / Settings gestures:
   a. Double-click `Meshes/KenneyChar_attack-kick-right.ocanim`, tick **Root motion** in the Anim tab, save (the Lunge
      montage and the Apply / Accumulate keys need it; the Anim tab labels it at `docs/ANIM_MONTAGES.md` section 3).
   b. Project Settings, project page (Name, Author, Start map ...), **Input Scheme** combo > `Demo/Input/Demo.ocinput`, save the
      project (sets `INPUT.SCHEME`; the combo is `sandbox/src/SandboxSettings.cpp:775-817`). Only Demo_Gameplay's T / Y / C,V probe needs it.
4. Reopen the project (F17), check the Output Log lists sixteen `[Graph] declared class 'AN_Demo...'` lines and no
   `[GAS] Content/Demo/Gameplay/Demo.ocgas:<line>` errors, open each level, Play, and walk section 7.
5. Do not commit the generated sidecar files; commit the sources only.

## 6. The files

### 6.1 Shared graphs (every level)

`Content/Demo/Scripts/AN_DemoController.ocgraph` - the concrete controller `GameMode.controller=` can name (same as
`templates/FirstPerson/Content/Scripts/AN_FPController.ocgraph`):

```text
OCGRAPH 1
DOMAIN gameplay
# The concrete player controller every Demo level possesses its pawn with. PlayerController is abstract and
# unspawnable, ABSTRACT is not inherited, so this empty subclass is what GameMode.controller= can name
# (same file as templates/FirstPerson AN_FPController.ocgraph).
NAME DemoController
DESCRIPTION The player controller for the parity demo levels. Empty on purpose.

CLASS AN_DemoController PlayerController
```

`Content/Demo/Scripts/AN_DemoRules.ocgraph` - the game mode. Each level names it with `GAMEMODE AN_DemoRules`:

```text
OCGRAPH 1
DOMAIN gameplay
# The game mode every Demo level names with `GAMEMODE AN_DemoRules`. It only says who the player is.
# A level that wants a different pawn writes `DEFAULTPAWN <class>` (Demo_Animation uses AN_DemoHero).
# No ENTRY on purpose: a class graph with no entry is legal (AN_FPController is one) and ticks nothing.
NAME DemoRules
DESCRIPTION Parity demo game mode: first-person walker pawn, empty controller.

CLASS AN_DemoRules GameMode pawn=AN_DemoViewer controller=AN_DemoController
```

`Content/Demo/Scripts/AN_DemoViewer.ocgraph` - the default first-person walker (the First Person template's character without the gun):

```text
OCGRAPH 1
DOMAIN gameplay
# The default player of every Demo level: walk (WASD), look (mouse), jump (Space), first person, no body.
# Same shape as templates/FirstPerson AN_FPCharacter.ocgraph minus the gun. Nothing demo-specific lives
# here: every demo feature is driven by a class actor placed in the level, which reads keys itself.
NAME DemoViewer
DESCRIPTION First-person walker used as the pawn in the parity demo levels.

CLASS AN_DemoViewer Character view=firstperson

PARAM entity int
PARAM deltaTime float

NODE ent Param param=entity
NODE dt Param param=deltaTime

ENTRY tick OnTick
NODE tick OnTick

# MoveAxis and MouseDelta cache their read to the exec visit, so they sit on the chain.
NODE move MoveAxis
NODE mouse MouseDelta
LINK tick.exec move.exec
LINK move.then mouse.exec

# Degrees per mouse unit. Pitch is negated so pushing the mouse forward looks up.
NODE sensYaw ConstFloat value=0.15
NODE sensPitch ConstFloat value=-0.15
NODE yaw Multiply
LINK mouse.deltaX yaw.a
LINK sensYaw.value yaw.b
NODE pitch Multiply
LINK mouse.deltaY pitch.a
LINK sensPitch.value pitch.b

NODE drive CharacterMove
LINK mouse.then drive.exec
LINK ent.value drive.entity
LINK dt.value drive.dt
LINK move.forward drive.forward
LINK move.right drive.right
LINK yaw.result drive.yawDelta
LINK pitch.result drive.pitchDelta

# Space = Key 36. Jump declines in mid-air by itself.
NODE jumpKey ConstInt value=36
NODE jumping InputKey
LINK jumpKey.value jumping.key
NODE jumpGate Branch
LINK drive.then jumpGate.exec
LINK jumping.down jumpGate.cond
NODE doJump Jump
LINK jumpGate.true doJump.exec
LINK ent.value doJump.entity
```

`Content/Demo/Scripts/AN_DemoOrbiter.ocgraph` - a ball that circles its placement point and names itself `IkBall`:

```text
OCGRAPH 1
DOMAIN gameplay
# A ball that circles the point it was placed at. It names itself IkBall so the animation graphs can
# `FindEntity name=IkBall` and bind IK goals / look-at targets to it. Shape follows test-content/GraphDemo
# IdleMotion.ocgraph (remember the home point at OnStart, then orbit it every tick).
NAME DemoOrbiter
DESCRIPTION Circles its placement point; findable by the name IkBall.

CLASS AN_DemoOrbiter Actor mesh=Meshes/sphere.ocmesh material=Caldera_metal

PARAM entity int
PARAM time float

VAR homeX float 0
VAR homeY float 0
VAR homeZ float 0

NODE ent Param param=entity
NODE t Param param=time

# ---- OnStart: remember home, name the entity, size the 1 cm built-in sphere up to a 14 cm ball ----
ENTRY seed OnStart
NODE seed OnStart
NODE here GetFieldVec3 field=CLocal.position
LINK ent.value here.entity
NODE sx SetVar var=homeX
NODE sy SetVar var=homeY
NODE sz SetVar var=homeZ
LINK seed.exec sx.exec
LINK here.x sx.value
LINK sx.then sy.exec
LINK here.y sy.value
LINK sy.then sz.exec
LINK here.z sz.value

# SetName and SetFieldVec3 have no exec pins by default, so every pin is declared by hand (any PIN
# record suppresses all of that node's defaults).
NODE nm SetName name=IkBall
PIN nm exec in exec
PIN nm entity in int
PIN nm then out exec
PIN nm success out bool
LINK sz.then nm.exec
LINK ent.value nm.entity

NODE ballSize ConstFloat value=14.0
NODE grow SetFieldVec3 field=CLocal.scale
PIN grow exec in exec
PIN grow entity in int
PIN grow x in float
PIN grow y in float
PIN grow z in float
PIN grow then out exec
PIN grow success out bool
LINK nm.then grow.exec
LINK ent.value grow.entity
LINK ballSize.value grow.x
LINK ballSize.value grow.y
LINK ballSize.value grow.z

# ---- OnTick: circle radius 160 cm at 1.2 rad/s, bobbing 40 cm up and down at half that rate ----
ENTRY tick OnTick
NODE tick OnTick
NODE omega ConstFloat value=1.2
NODE phase Multiply
LINK t.value phase.a
LINK omega.value phase.b
NODE cs Cos
LINK phase.result cs.a
NODE sn Sin
LINK phase.result sn.a
NODE radius ConstFloat value=160.0
NODE dx Multiply
LINK cs.result dx.a
LINK radius.value dx.b
NODE dy Multiply
LINK sn.result dy.a
LINK radius.value dy.b

NODE halfRate ConstFloat value=0.5
NODE phase2 Multiply
LINK phase.result phase2.a
LINK halfRate.value phase2.b
NODE sn2 Sin
LINK phase2.result sn2.a
NODE bobAmp ConstFloat value=40.0
NODE dz Multiply
LINK sn2.result dz.a
LINK bobAmp.value dz.b

NODE hx GetVar var=homeX
NODE hy GetVar var=homeY
NODE hz GetVar var=homeZ
NODE px Add
LINK hx.value px.a
LINK dx.result px.b
NODE py Add
LINK hy.value py.a
LINK dy.result py.b
NODE pz Add
LINK hz.value pz.a
LINK dz.result pz.b

NODE place SetLocalPosition
LINK tick.exec place.exec
LINK ent.value place.entity
LINK px.result place.x
LINK py.result place.y
LINK pz.result place.z
```

### 6.2 Demo_Animation

What each piece shows, and what IK can and cannot show on these two rigs:

- **Motion matching** (`KenneyLocomotion.ocmotion`). Three looping entries tagged `idle`, `walk`, `run`. Feature bones are
  `head`, `arm-left`, `arm-right`, `torso`: the leg bones are leaves with fixed joint origins, so their positions carry no
  information (F2). The root-velocity and trajectory terms are flat because no clip travels (F3). `switchMargin` is 0 so a tag
  change switches at the next search. The graph sends the wish velocity every tick and, when the idle / walk / run family changes,
  the required tag and a search request. What you see is real matcher behaviour (inertialized switches, a cost, a switch count),
  but not trajectory matching; that needs clips with root travel (G6). U3.
- **Montages.** `Strike` chains melee and kick with three sections and two notifies; `Lunge` is the kick alone. Both use the
  default slot, which is applied over the finished pose when no state-machine node consumes it (`docs/ANIM_MONTAGES.md` section 1).
- **Root motion.** `G` sets Apply (scale 6) and plays Lunge; `H` sets Accumulate and samples the accumulator 0.22 s in. After the
  montage ends the graph sets Ignore again. The body is a child of the capsule, so Apply moves the child inside the capsule
  (the animation system moves a plain entity itself; only a physics character sets the External bit, `Character.cs:308-334`).
  U6, U7.
- **IK on the Kenney rig** (`KenneyReach.ocikrig`): one CCD chain `torso -> head` with limits and the goal `Reach`. Bound to the
  orbiting ball it leans the torso toward it. A second chain sharing `torso` would be skipped and reported (a bone a chain has rotated is
  claimed, `docs/ANIM_IK_RETARGET.md` "IK Rig"), so there is only one. There is **no foot placement on this rig**: a leg chain
  would need start != end (F13) and the legs are leaves (F2). U8.
- **Control rig** (`KenneyLook.ocrig`, OCRIG 2): `OP aim` on `head` with a world-space input `LookAt` and the weight as the float input
  `Look`. The graph feeds `LookAt` from the ball every tick while `Look` is 1. U9.
- **Retarget** (`KenneyToMannequin.ocretarget`): six FK chains, each source a single Kenney bone mapped onto a chain of the
  mannequin (the delta of the one source bone is applied along the whole target chain). The root bob (`root` bone) is not mapped
  (empty `rootMotion`). Nothing in the graph mentions retargeting. U10.
- **Foot IK on the mannequin** (`MannequinIk.ocikrig`): twobone chains for both legs with `feet` entries, a pelvis (`Hips`) solve,
  and a twobone right-arm chain with the goal `Reach`; every length is the centimetre value x 209 (F4, F13). The mannequin stands with
  its origin on the edge of a 20 cm block. U11.
- **Ragdoll.** The hero uses the override file (`Content/Meshes/KenneyChar.ocragdoll`) to give `head`, `arm-left` and
  `arm-right` parts besides the two the automatic rule builds (`root`, `torso`); the legs ride the pelvis part. The mannequin uses the
  automatic builder only. U12.

`Content/Demo/Scripts/AN_DemoHero.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# THE ANIMATION DEMO PLAYER: a third-person Kenney character driven by motion matching, with a montage,
# root motion, IK, a control-rig look-at and a ragdoll on keys. Level: Demo_Animation (DEFAULTPAWN).
#
#   W A S D / mouse   walk and look          Shift   run tag instead of walk tag      Space   jump
#   F   montage Strike          G   montage Lunge with root motion APPLIED (the body lunges)
#   H   montage Lunge with root motion ACCUMULATED (the displacement is printed, the body stays)
#   E   IK goal Reach on/off (torso leans toward the orbiting IkBall)
#   Q   control-rig head look-at on/off (head follows IkBall)
#   X   ragdoll on (with a shove) / off
#   M   print the matcher state: cost, switches so far, whether the walk clip is the one playing
#
# WHY THE SKINNED MESH IS A CHILD. AverCharacter feeds a CMotionMatcher found on ITS OWN entity, but its
# capsule/camera must stay unscaled, and this 67 cm figure needs x2.6. So the body is a COMP child (scale 2.6,
# yaw 180: the Kenney rig faces -X) and this graph feeds the child instead of relying on Character.cs.
# Character.cs normalises the wish vector, so body speed is always 350 cm/s; walk vs run is the clip only.
NAME DemoHero
DESCRIPTION Third-person Kenney character: motion matching, montage, root motion, IK, control-rig look, ragdoll.

CLASS AN_DemoHero Character view=thirdperson

COMP kBody SkeletalMesh mesh=Meshes/KenneyChar.ocmesh material=KenneyChar_colormap skeleton=Meshes/KenneyChar.ocskel rot=180,0,0 scale=2.6,2.6,2.6

PARAM entity int
PARAM deltaTime float

# body = handle of the kBody child. lastMode = last motion tag family sent (-1 none).
VAR body int 0
VAR lastMode float -1
VAR ik bool false
VAR look bool false
VAR rag bool false
VAR lunge bool false
VAR acc bool false
VAR lungeT float 0
VAR sampled bool false

NODE ent Param param=entity
NODE dt Param param=deltaTime
NODE bodyV GetVar var=body
NODE zero ConstFloat value=0.0
NODE oneF ConstFloat value=1.0
NODE twoF ConstFloat value=2.0
NODE tTrue ConstBool value=true
NODE tFalse ConstBool value=false

# ======================================================================================================
# ON START: find the child, then give it the matcher, the IK rig, the control rig and root-motion Ignore.
# ======================================================================================================
ENTRY seed OnStart
NODE seed OnStart
NODE findBody FindEntity name=kBody
NODE saveBody SetVar var=body
LINK seed.exec saveBody.exec
LINK findBody.entity saveBody.value

NODE mm SetMotionMatching database=Demo/Anim/KenneyLocomotion.ocmotion
LINK saveBody.then mm.exec
LINK bodyV.value mm.entity

NODE rig SetIkRig rig=Demo/Anim/KenneyReach.ocikrig
LINK mm.then rig.exec
LINK bodyV.value rig.entity

# SetControlRig has no exec pins by default: every pin declared by hand. Unwired weight reads as 1.
NODE crig SetControlRig rig=Demo/Anim/KenneyLook.ocrig
PIN crig exec in exec
PIN crig entity in int
PIN crig weight in float
PIN crig then out exec
PIN crig success out bool
LINK rig.then crig.exec
LINK bodyV.value crig.entity

NODE rmIgnore0 SetRootMotion
NODE rmModeIgnore ConstInt value=1
NODE rmScale ConstFloat value=6.0
LINK crig.then rmIgnore0.exec
LINK bodyV.value rmIgnore0.entity
LINK rmModeIgnore.value rmIgnore0.mode
LINK rmScale.value rmIgnore0.scale

# ======================================================================================================
# ON TICK: move, then fan out to one small arm per behaviour.
# ======================================================================================================
ENTRY tick OnTick
NODE tick OnTick
NODE move MoveAxis
NODE mouse MouseDelta
LINK tick.exec move.exec
LINK move.then mouse.exec
NODE sensYaw ConstFloat value=0.15
NODE sensPitch ConstFloat value=-0.15
NODE yaw Multiply
LINK mouse.deltaX yaw.a
LINK sensYaw.value yaw.b
NODE pitch Multiply
LINK mouse.deltaY pitch.a
LINK sensPitch.value pitch.b
NODE drive CharacterMove
LINK mouse.then drive.exec
LINK ent.value drive.entity
LINK dt.value drive.dt
LINK move.forward drive.forward
LINK move.right drive.right
LINK yaw.result drive.yawDelta
LINK pitch.result drive.pitchDelta

NODE fan Sequence
PIN fan exec in exec
PIN fan then0 out exec
PIN fan then1 out exec
PIN fan then2 out exec
PIN fan then3 out exec
PIN fan then4 out exec
PIN fan then5 out exec
PIN fan then6 out exec
PIN fan then7 out exec
PIN fan then8 out exec
PIN fan then9 out exec
PIN fan then10 out exec
LINK drive.then fan.exec

# ---- arm 0: jump (Space = Key 36) ---------------------------------------------------------------------
NODE jumpKey ConstInt value=36
NODE jumping InputKey
LINK jumpKey.value jumping.key
NODE jumpGate Branch
LINK fan.then0 jumpGate.exec
LINK jumping.down jumpGate.cond
NODE doJump Jump
LINK jumpGate.true doJump.exec
LINK ent.value doJump.entity

# ---- arm 1: tell the matcher where we want to go, and which tag family to search in ----------------------
# Planar look direction p = normalise(look.xy); wish = p*forward + right-of-p*right; velocity = wish * 350 or 0.
NODE aim GetForward
LINK ent.value aim.entity
NODE pb VecNormalize
LINK aim.x pb.ax
LINK aim.y pb.ay
LINK zero.value pb.az
NODE m1 Multiply
LINK pb.x m1.a
LINK move.forward m1.b
NODE m2 Multiply
LINK pb.y m2.a
LINK move.right m2.b
NODE wx Subtract
LINK m1.result wx.a
LINK m2.result wx.b
NODE m3 Multiply
LINK pb.y m3.a
LINK move.forward m3.b
NODE m4 Multiply
LINK pb.x m4.a
LINK move.right m4.b
NODE wy Add
LINK m3.result wy.a
LINK m4.result wy.b
NODE wn VecNormalize
LINK wx.result wn.ax
LINK wy.result wn.ay
LINK zero.value wn.az

NODE inLen VecLength
LINK move.forward inLen.ax
LINK move.right inLen.ay
LINK zero.value inLen.az
NODE inThresh ConstFloat value=0.1
NODE has Greater
LINK inLen.result has.a
LINK inThresh.value has.b
NODE speedC ConstFloat value=350.0
NODE spd Select
LINK has.result spd.cond
LINK speedC.value spd.ifTrue
LINK zero.value spd.ifFalse
NODE vx Multiply
LINK wn.x vx.a
LINK spd.result vx.b
NODE vy Multiply
LINK wn.y vy.a
LINK spd.result vy.b

NODE feed SetMotionInput
LINK fan.then1 feed.exec
LINK bodyV.value feed.entity
LINK vx.result feed.velocityX
LINK vy.result feed.velocityY

# mode 0 idle, 1 walk, 2 run (LeftShift = Key 37)
NODE shiftKey ConstInt value=37
NODE shift InputKey
LINK shiftKey.value shift.key
NODE runSel Select
LINK shift.down runSel.cond
LINK twoF.value runSel.ifTrue
LINK oneF.value runSel.ifFalse
NODE modeF Select
LINK has.result modeF.cond
LINK runSel.result modeF.ifTrue
LINK zero.value modeF.ifFalse
NODE lastModeV GetVar var=lastMode
NODE changed NotEqual
LINK modeF.result changed.a
LINK lastModeV.value changed.b
NODE chg Branch
LINK feed.then chg.exec
LINK changed.result chg.cond
NODE modeI FloatToInt
LINK modeF.result modeI.a
NODE modeSw SwitchInt
LINK chg.true modeSw.exec
LINK modeI.result modeSw.selector
NODE tagIdle SetMotionTags required=idle
LINK modeSw.case0 tagIdle.exec
LINK bodyV.value tagIdle.entity
NODE tagWalk SetMotionTags required=walk
LINK modeSw.case1 tagWalk.exec
LINK bodyV.value tagWalk.entity
NODE tagRun SetMotionTags required=run
LINK modeSw.case2 tagRun.exec
LINK bodyV.value tagRun.entity
NODE search RequestMotionSearch
LINK tagIdle.then search.exec
LINK tagWalk.then search.exec
LINK tagRun.then search.exec
LINK bodyV.value search.entity
NODE saveMode SetVar var=lastMode
LINK search.then saveMode.exec
LINK modeF.result saveMode.value
NODE modeShown PrintInt
LINK saveMode.then modeShown.exec
LINK modeI.result modeShown.value

# ---- arm 2: F = montage Strike -----------------------------------------------------------------------
NODE fKey ConstInt value=5
NODE fPressed InputKeyPressed
LINK fKey.value fPressed.key
NODE fGate Branch
LINK fan.then2 fGate.exec
LINK fPressed.triggered fGate.cond
NODE strike PlayMontage montage=Demo/Anim/Strike.ocmontage
LINK fGate.true strike.exec
LINK bodyV.value strike.entity
NODE strikeMsg PrintString text=Montage_Strike_played
LINK strike.then strikeMsg.exec

# ---- arm 3: G = Lunge with root motion Apply (mode 2) --------------------------------------------------
NODE gKey ConstInt value=6
NODE gPressed InputKeyPressed
LINK gKey.value gPressed.key
NODE gGate Branch
LINK fan.then3 gGate.exec
LINK gPressed.triggered gGate.cond
NODE rmApplyMode ConstInt value=2
NODE rmApply SetRootMotion
LINK gGate.true rmApply.exec
LINK bodyV.value rmApply.entity
LINK rmApplyMode.value rmApply.mode
LINK rmScale.value rmApply.scale
NODE lungeG PlayMontage montage=Demo/Anim/Lunge.ocmontage
LINK rmApply.then lungeG.exec
LINK bodyV.value lungeG.entity
NODE lungeOnG SetVar var=lunge
LINK lungeG.then lungeOnG.exec
LINK tTrue.value lungeOnG.value
NODE lungeMsgG PrintString text=Lunge_root_motion_APPLY
LINK lungeOnG.then lungeMsgG.exec

# ---- arm 4: H = Lunge with root motion Accumulate (mode 3) ---------------------------------------------
NODE hKey ConstInt value=7
NODE hPressed InputKeyPressed
LINK hKey.value hPressed.key
NODE hGate Branch
LINK fan.then4 hGate.exec
LINK hPressed.triggered hGate.cond
NODE rmAccMode ConstInt value=3
NODE rmAcc SetRootMotion
LINK hGate.true rmAcc.exec
LINK bodyV.value rmAcc.entity
LINK rmAccMode.value rmAcc.mode
LINK rmScale.value rmAcc.scale
NODE lungeH PlayMontage montage=Demo/Anim/Lunge.ocmontage
LINK rmAcc.then lungeH.exec
LINK bodyV.value lungeH.entity
NODE lungeOnH SetVar var=lunge
LINK lungeH.then lungeOnH.exec
LINK tTrue.value lungeOnH.value
NODE accOnH SetVar var=acc
LINK lungeOnH.then accOnH.exec
LINK tTrue.value accOnH.value
NODE lungeMsgH PrintString text=Lunge_root_motion_ACCUMULATE
LINK accOnH.then lungeMsgH.exec

# ---- arm 5: watch the lunge montage. H (Accumulate) is sampled once, 0.22 s in, because the lunge goes out and
# comes back (net travel 0): at the end the accumulator would read ~0. When the montage has finished, root motion
# goes back to Ignore. lungeT ages the lunge so nothing can fire in the tick PlayMontage was called in.
NODE lungeV GetVar var=lunge
NODE accV GetVar var=acc
NODE sampledV GetVar var=sampled
NODE ltV GetVar var=lungeT
NODE ltAdd Add
LINK ltV.value ltAdd.a
LINK dt.value ltAdd.b
NODE ltSel Select
LINK lungeV.value ltSel.cond
LINK ltAdd.result ltSel.ifTrue
LINK zero.value ltSel.ifFalse
NODE ltSet SetVar var=lungeT
LINK fan.then5 ltSet.exec
LINK ltSel.result ltSet.value

NODE sampleAge ConstFloat value=0.22
NODE sampleAged Greater
LINK ltSel.result sampleAged.a
LINK sampleAge.value sampleAged.b
NODE notSampled Not
LINK sampledV.value notSampled.a
NODE sampleA And
LINK accV.value sampleA.a
LINK sampleAged.result sampleA.b
NODE sampleDue And
LINK sampleA.result sampleDue.a
LINK notSampled.result sampleDue.b
NODE sampleGate Branch
LINK ltSet.then sampleGate.exec
LINK sampleDue.result sampleGate.cond
NODE consume ConsumeRootMotion
LINK sampleGate.true consume.exec
LINK bodyV.value consume.entity
NODE acc_x_cm Print
LINK consume.then acc_x_cm.exec
LINK consume.x acc_x_cm.value
NODE acc_y_cm Print
LINK acc_x_cm.then acc_y_cm.exec
LINK consume.y acc_y_cm.value
NODE acc_seconds Print
LINK acc_y_cm.then acc_seconds.exec
LINK consume.seconds acc_seconds.value
NODE markSampled SetVar var=sampled
LINK acc_seconds.then markSampled.exec
LINK tTrue.value markSampled.value

NODE minAge ConstFloat value=0.25
NODE aged Greater
LINK ltSel.result aged.a
LINK minAge.value aged.b
NODE lungeNow IsMontagePlaying montage=Demo/Anim/Lunge.ocmontage
LINK bodyV.value lungeNow.entity
NODE lungeDone Not
LINK lungeNow.result lungeDone.a
NODE lungeAged And
LINK lungeV.value lungeAged.a
LINK aged.result lungeAged.b
NODE lungeEnded And
LINK lungeAged.result lungeEnded.a
LINK lungeDone.result lungeEnded.b
NODE endGate Branch
LINK sampleGate.false endGate.exec
LINK markSampled.then endGate.exec
LINK lungeEnded.result endGate.cond
NODE rmOff SetRootMotion
LINK endGate.true rmOff.exec
LINK bodyV.value rmOff.entity
LINK rmModeIgnore.value rmOff.mode
LINK rmScale.value rmOff.scale
NODE lungeOff SetVar var=lunge
LINK rmOff.then lungeOff.exec
LINK tFalse.value lungeOff.value
NODE accOff SetVar var=acc
LINK lungeOff.then accOff.exec
LINK tFalse.value accOff.value
NODE sampledOff SetVar var=sampled
LINK accOff.then sampledOff.exec
LINK tFalse.value sampledOff.value

# ---- arm 6: E = IK goal Reach on/off -------------------------------------------------------------------
NODE eKey ConstInt value=4
NODE ePressed InputKeyPressed
LINK eKey.value ePressed.key
NODE eGate Branch
LINK fan.then6 eGate.exec
LINK ePressed.triggered eGate.cond
NODE ikV GetVar var=ik
NODE ikNext Not
LINK ikV.value ikNext.a
NODE ikSet SetVar var=ik
LINK eGate.true ikSet.exec
LINK ikNext.result ikSet.value
NODE ikBranch Branch
LINK ikSet.then ikBranch.exec
LINK ikNext.result ikBranch.cond
NODE ikBall FindEntity name=IkBall
NODE ikFound Branch
LINK ikBranch.true ikFound.exec
LINK ikBall.found ikFound.cond
NODE ikBind BindIkGoal goal=Reach
LINK ikFound.true ikBind.exec
LINK bodyV.value ikBind.entity
LINK ikBall.entity ikBind.target
NODE ikBlend ConstFloat value=0.3
NODE ikWeight SetIkGoalWeight goal=Reach
LINK ikBind.then ikWeight.exec
LINK bodyV.value ikWeight.entity
LINK oneF.value ikWeight.weight
LINK ikBlend.value ikWeight.blendSeconds
NODE ikOnMsg PrintString text=IK_Reach_ON
LINK ikWeight.then ikOnMsg.exec
NODE ikClear ClearIkGoal goal=Reach
LINK ikBranch.false ikClear.exec
LINK bodyV.value ikClear.entity
NODE ikOffMsg PrintString text=IK_Reach_OFF
LINK ikClear.then ikOffMsg.exec

# ---- arm 7: Q = control-rig look-at weight on/off ------------------------------------------------------
NODE qKey ConstInt value=16
NODE qPressed InputKeyPressed
LINK qKey.value qPressed.key
NODE qGate Branch
LINK fan.then7 qGate.exec
LINK qPressed.triggered qGate.cond
NODE lookV GetVar var=look
NODE lookNext Not
LINK lookV.value lookNext.a
NODE lookSet SetVar var=look
LINK qGate.true lookSet.exec
LINK lookNext.result lookSet.value
NODE lookW Select
LINK lookNext.result lookW.cond
LINK oneF.value lookW.ifTrue
LINK zero.value lookW.ifFalse
NODE lookWeight SetRigInputFloat input=Look
LINK lookSet.then lookWeight.exec
LINK bodyV.value lookWeight.entity
LINK lookW.result lookWeight.value
NODE lookMsg PrintString text=Look_toggled
LINK lookWeight.then lookMsg.exec

# ---- arm 8: while Look is on, hand the rig the ball's world position every tick --------------------------
NODE lookGate Branch
LINK fan.then8 lookGate.exec
LINK lookV.value lookGate.cond
NODE lookBall FindEntity name=IkBall
NODE ballPos GetWorldPosition
LINK lookBall.entity ballPos.entity
NODE lookTarget SetRigInputVec3 input=LookAt
LINK lookGate.true lookTarget.exec
LINK bodyV.value lookTarget.entity
LINK ballPos.x lookTarget.x
LINK ballPos.y lookTarget.y
LINK ballPos.z lookTarget.z

# ---- arm 9: X = ragdoll on (limp, with a shove along the look direction) / off ---------------------------
NODE xKey ConstInt value=23
NODE xPressed InputKeyPressed
LINK xKey.value xPressed.key
NODE xGate Branch
LINK fan.then9 xGate.exec
LINK xPressed.triggered xGate.cond
NODE ragV GetVar var=rag
NODE ragBranch Branch
LINK xGate.true ragBranch.exec
LINK ragV.value ragBranch.cond
NODE ragLimp ConstInt value=1
NODE ragBlend ConstFloat value=0.1
NODE ragStart RagdollStart
LINK ragBranch.false ragStart.exec
LINK bodyV.value ragStart.entity
LINK ragLimp.value ragStart.mode
LINK ragBlend.value ragStart.blend
NODE shove ConstFloat value=25000.0
NODE lift ConstFloat value=15000.0
NODE shoveX Multiply
LINK pb.x shoveX.a
LINK shove.value shoveX.b
NODE shoveY Multiply
LINK pb.y shoveY.a
LINK shove.value shoveY.b
NODE ragPush RagdollImpulse
LINK ragStart.then ragPush.exec
LINK bodyV.value ragPush.entity
LINK shoveX.result ragPush.x
LINK shoveY.result ragPush.y
LINK lift.value ragPush.z
NODE ragOnSet SetVar var=rag
LINK ragPush.then ragOnSet.exec
LINK tTrue.value ragOnSet.value
NODE ragOnMsg PrintString text=Ragdoll_ON
LINK ragOnSet.then ragOnMsg.exec
NODE ragBack ConstFloat value=0.5
NODE ragStop RagdollStop
LINK ragBranch.true ragStop.exec
LINK bodyV.value ragStop.entity
LINK ragBack.value ragStop.blend
NODE ragOffSet SetVar var=rag
LINK ragStop.then ragOffSet.exec
LINK tFalse.value ragOffSet.value
NODE ragOffMsg PrintString text=Ragdoll_OFF
LINK ragOffSet.then ragOffMsg.exec

# ---- arm 10: M = ask the matcher what it is doing -----------------------------------------------------------
NODE mKey ConstInt value=12
NODE mPressed InputKeyPressed
LINK mKey.value mPressed.key
NODE mGate Branch
LINK fan.then10 mGate.exec
LINK mPressed.triggered mGate.cond
NODE mmState GetMotionState
LINK bodyV.value mmState.entity
NODE walkNow IsMotionClip clip=Meshes/KenneyChar_walk.ocanim
LINK bodyV.value walkNow.entity
NODE walkNowF BoolToFloat
LINK walkNow.result walkNowF.a
NODE motion_cost Print
LINK mGate.true motion_cost.exec
LINK mmState.cost motion_cost.value
NODE motion_switches PrintInt
LINK motion_cost.then motion_switches.exec
LINK mmState.switches motion_switches.value
NODE motion_is_walk_clip Print
LINK motion_switches.then motion_is_walk_clip.exec
LINK walkNowF.result motion_is_walk_clip.value
```

`Content/Demo/Scripts/AN_DemoMannequin.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# THE RETARGET + RAGDOLL DUMMY. A Character.ocskel (45 bones) figure that has no clips of its own plays the
# KENNEY walk clip: AnimSystem finds Demo/Anim/KenneyToMannequin.ocretarget (source KenneyChar -> target
# Skeletons/Character.ocskel) and bakes the clip on first use. Nothing here mentions retargeting.
# It walks a circle of radius 300 cm around its placement point. Z toggles a ragdoll (the circle freezes).
#
# The mesh is ~209x larger than a centimetre in this rig's units, hence scale 0.0047813 (the number
# templates/FirstPerson AN_FPCharacter.ocgraph measured); rot=180 because the rig faces -X.
NAME DemoMannequin
DESCRIPTION Walks a circle playing a retargeted Kenney clip; Z toggles ragdoll.

CLASS AN_DemoMannequin Actor

COMP mBody SkeletalMesh mesh=Meshes/Character.ocmesh material=M_DemoMannequin skeleton=Skeletons/Character.ocskel rot=180,0,0 scale=0.0047813,0.0047813,0.0047813

PARAM entity int
PARAM time float
PARAM deltaTime float

VAR body int 0
VAR homeX float 0
VAR homeY float 0
VAR homeZ float 0
VAR theta float 0
VAR rag bool false

NODE ent Param param=entity
NODE dt Param param=deltaTime
NODE bodyV GetVar var=body
NODE zero ConstFloat value=0.0
NODE tTrue ConstBool value=true
NODE tFalse ConstBool value=false

# ---- ON START -------------------------------------------------------------------------------------------
ENTRY seed OnStart
NODE seed OnStart
NODE findBody FindEntity name=mBody
NODE saveBody SetVar var=body
LINK seed.exec saveBody.exec
LINK findBody.entity saveBody.value
NODE here GetFieldVec3 field=CLocal.position
LINK ent.value here.entity
NODE sx SetVar var=homeX
NODE sy SetVar var=homeY
NODE sz SetVar var=homeZ
LINK saveBody.then sx.exec
LINK here.x sx.value
LINK sx.then sy.exec
LINK here.y sy.value
LINK sy.then sz.exec
LINK here.z sz.value

# PlayAnimation has no exec pins by default; declare all five. Unwired loop reads false, so wire true.
NODE walk PlayAnimation clip=Meshes/KenneyChar_walk.ocanim
PIN walk exec in exec
PIN walk entity in int
PIN walk loop in bool
PIN walk then out exec
PIN walk success out bool
LINK sz.then walk.exec
LINK bodyV.value walk.entity
LINK tTrue.value walk.loop

# ---- ON TICK: advance the angle unless ragdolling, then place and face along the circle -----------------
ENTRY tick OnTick
NODE tick OnTick
NODE ragV GetVar var=rag
NODE thetaV GetVar var=theta
NODE omega ConstFloat value=0.4
NODE stepA Multiply
LINK dt.value stepA.a
LINK omega.value stepA.b
NODE thetaNext Add
LINK thetaV.value thetaNext.a
LINK stepA.result thetaNext.b
NODE thetaPick Select
LINK ragV.value thetaPick.cond
LINK thetaV.value thetaPick.ifTrue
LINK thetaNext.result thetaPick.ifFalse
NODE thetaSet SetVar var=theta
LINK tick.exec thetaSet.exec
LINK thetaPick.result thetaSet.value

NODE cs Cos
LINK thetaPick.result cs.a
NODE sn Sin
LINK thetaPick.result sn.a
NODE radius ConstFloat value=300.0
NODE dx Multiply
LINK cs.result dx.a
LINK radius.value dx.b
NODE dy Multiply
LINK sn.result dy.a
LINK radius.value dy.b
NODE hx GetVar var=homeX
NODE hy GetVar var=homeY
NODE hz GetVar var=homeZ
NODE px Add
LINK hx.value px.a
LINK dx.result px.b
NODE py Add
LINK hy.value py.a
LINK dy.result py.b
NODE place SetLocalPosition
LINK thetaSet.then place.exec
LINK ent.value place.entity
LINK px.result place.x
LINK py.result place.y
LINK hz.value place.z

# yaw (degrees) = theta in degrees + 90: the tangent of a counter-clockwise circle.
NODE radToDeg ConstFloat value=57.29578
NODE ninety ConstFloat value=90.0
NODE thetaDeg Multiply
LINK thetaPick.result thetaDeg.a
LINK radToDeg.value thetaDeg.b
NODE yawDeg Add
LINK thetaDeg.result yawDeg.a
LINK ninety.value yawDeg.b
NODE face SetLocalRotation
LINK place.then face.exec
LINK ent.value face.entity
LINK yawDeg.result face.yaw

# ---- Z (Key 25) toggles the ragdoll: limp, with a shove (70 kg: 28000 kg*cm/s = 4 m/s) ------------------
NODE zKey ConstInt value=25
NODE zPressed InputKeyPressed
LINK zKey.value zPressed.key
NODE zGate Branch
LINK face.then zGate.exec
LINK zPressed.triggered zGate.cond
NODE ragBranch Branch
LINK zGate.true ragBranch.exec
LINK ragV.value ragBranch.cond
NODE limp ConstInt value=1
NODE blendIn ConstFloat value=0.1
NODE ragStart RagdollStart
LINK ragBranch.false ragStart.exec
LINK bodyV.value ragStart.entity
LINK limp.value ragStart.mode
LINK blendIn.value ragStart.blend
NODE shoveX ConstFloat value=-28000.0
NODE shoveUp ConstFloat value=20000.0
NODE ragPush RagdollImpulse
LINK ragStart.then ragPush.exec
LINK bodyV.value ragPush.entity
LINK shoveX.value ragPush.x
LINK zero.value ragPush.y
LINK shoveUp.value ragPush.z
NODE ragOn SetVar var=rag
LINK ragPush.then ragOn.exec
LINK tTrue.value ragOn.value
NODE ragOnMsg PrintString text=Mannequin_ragdoll_ON
LINK ragOn.then ragOnMsg.exec
NODE blendOut ConstFloat value=0.5
NODE ragStop RagdollStop
LINK ragBranch.true ragStop.exec
LINK bodyV.value ragStop.entity
LINK blendOut.value ragStop.blend
NODE ragOff SetVar var=rag
LINK ragStop.then ragOff.exec
LINK tFalse.value ragOff.value
NODE ragOffMsg PrintString text=Mannequin_ragdoll_OFF
LINK ragOff.then ragOffMsg.exec
```

`Content/Demo/Scripts/AN_DemoMannequinIk.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# THE REAL FOOT-IK DUMMY. The Kenney rig has no knees, so foot placement is shown on the 45-bone mannequin.
# It stands (Kenney idle clip, retargeted) with its origin on a 20 cm high block whose edge runs through
# the middle of its stance: the right foot is on the block, the left foot hangs over the lower floor.
# Foot IK pulls the left foot down, drops the pelvis and bends the right knee. The right hand also chases a
# point that bobs in front of it (SetIkGoal every tick). Rig: Demo/Anim/MannequinIk.ocikrig (model units).
#
#   I   print the foot / pelvis IK offsets (model units; divide by 209 for centimetres)
#   U   feet IK on / off (blends over 0.2 s)
NAME DemoMannequinIk
DESCRIPTION Mannequin straddling a step edge; foot placement, pelvis drop and a hand reach.

CLASS AN_DemoMannequinIk Actor

COMP iBody SkeletalMesh mesh=Meshes/Character.ocmesh material=M_DemoMannequin skeleton=Skeletons/Character.ocskel rot=180,0,0 scale=0.0047813,0.0047813,0.0047813

PARAM entity int
PARAM time float
PARAM deltaTime float

VAR body int 0
VAR homeX float 0
VAR homeY float 0
VAR homeZ float 0
VAR feetOn bool true

NODE ent Param param=entity
NODE t Param param=time
NODE bodyV GetVar var=body
NODE zero ConstFloat value=0.0
NODE oneF ConstFloat value=1.0
NODE tTrue ConstBool value=true

# ---- ON START: find the child, remember home, play the retargeted idle, put the IK rig on ----------------
ENTRY seed OnStart
NODE seed OnStart
NODE findBody FindEntity name=iBody
NODE saveBody SetVar var=body
LINK seed.exec saveBody.exec
LINK findBody.entity saveBody.value
NODE here GetFieldVec3 field=CLocal.position
LINK ent.value here.entity
NODE sx SetVar var=homeX
NODE sy SetVar var=homeY
NODE sz SetVar var=homeZ
LINK saveBody.then sx.exec
LINK here.x sx.value
LINK sx.then sy.exec
LINK here.y sy.value
LINK sy.then sz.exec
LINK here.z sz.value

NODE idle PlayAnimation clip=Meshes/KenneyChar_idle.ocanim
PIN idle exec in exec
PIN idle entity in int
PIN idle loop in bool
PIN idle then out exec
PIN idle success out bool
LINK sz.then idle.exec
LINK bodyV.value idle.entity
LINK tTrue.value idle.loop

NODE rig SetIkRig rig=Demo/Anim/MannequinIk.ocikrig
LINK idle.then rig.exec
LINK bodyV.value rig.entity

# ---- ON TICK: the reach goal. World target = right shoulder + (30, 31, -10 + 12 sin(1.5 t)) cm ----------
ENTRY tick OnTick
NODE tick OnTick
NODE hx GetVar var=homeX
NODE hy GetVar var=homeY
NODE hz GetVar var=homeZ
NODE reachX ConstFloat value=30.0
NODE reachY ConstFloat value=31.0
NODE reachZ ConstFloat value=103.0
NODE bobAmp ConstFloat value=12.0
NODE bobRate ConstFloat value=1.5
NODE bobPhase Multiply
LINK t.value bobPhase.a
LINK bobRate.value bobPhase.b
NODE bobSin Sin
LINK bobPhase.result bobSin.a
NODE bob Multiply
LINK bobSin.result bob.a
LINK bobAmp.value bob.b
NODE gx Add
LINK hx.value gx.a
LINK reachX.value gx.b
NODE gy Add
LINK hy.value gy.a
LINK reachY.value gy.b
NODE gz0 Add
LINK hz.value gz0.a
LINK reachZ.value gz0.b
NODE gz Add
LINK gz0.result gz.a
LINK bob.result gz.b
NODE goal SetIkGoal goal=Reach
LINK tick.exec goal.exec
LINK bodyV.value goal.entity
LINK gx.result goal.x
LINK gy.result goal.y
LINK gz.result goal.z

# ---- I (Key 8): print the IK offsets --------------------------------------------------------------------
NODE iKey ConstInt value=8
NODE iPressed InputKeyPressed
LINK iKey.value iPressed.key
NODE iGate Branch
LINK goal.then iGate.exec
LINK iPressed.triggered iGate.cond
NODE footL GetFootIkState foot=LegL
LINK bodyV.value footL.entity
NODE footR GetFootIkState foot=LegR
LINK bodyV.value footR.entity
NODE pelvis GetFootIkState
LINK bodyV.value pelvis.entity
NODE printL Print
LINK iGate.true printL.exec
LINK footL.offsetCm printL.value
NODE printR Print
LINK printL.then printR.exec
LINK footR.offsetCm printR.value
NODE printP Print
LINK printR.then printP.exec
LINK pelvis.offsetCm printP.value

# ---- U (Key 20): feet IK weight on/off ------------------------------------------------------------------
NODE uKey ConstInt value=20
NODE uPressed InputKeyPressed
LINK uKey.value uPressed.key
NODE uGate Branch
LINK printP.then uGate.exec
LINK uPressed.triggered uGate.cond
NODE feetV GetVar var=feetOn
NODE feetNext Not
LINK feetV.value feetNext.a
NODE feetSet SetVar var=feetOn
LINK uGate.true feetSet.exec
LINK feetNext.result feetSet.value
NODE feetW Select
LINK feetNext.result feetW.cond
LINK oneF.value feetW.ifTrue
LINK zero.value feetW.ifFalse
NODE feetBlend ConstFloat value=0.2
NODE feetWeight SetFootIkWeight
LINK feetSet.then feetWeight.exec
LINK bodyV.value feetWeight.entity
LINK feetW.result feetWeight.weight
LINK feetBlend.value feetWeight.blendSeconds
NODE feetMsg PrintString text=Foot_IK_toggled
LINK feetWeight.then feetMsg.exec
```

`Content/Demo/Anim/KenneyLocomotion.ocmotion` (format: `modules/anim/src/MotionAsset.cpp` writer, `docs/ANIM_MOTION_MATCHING.md` section 2):

```json
{
  "format": "ocmotion",
  "version": 1,
  "name": "KenneyLocomotion",
  "skeleton": "Meshes/KenneyChar.ocskel",
  "rootBone": "",
  "sampleRate": 30,
  "tags": ["idle", "walk", "run"],
  "features": {
    "bones": [
      {"bone": "head", "pos": 1, "vel": 0.8},
      {"bone": "arm-left", "pos": 1, "vel": 0.8},
      {"bone": "arm-right", "pos": 1, "vel": 0.8},
      {"bone": "torso", "pos": 0.5, "vel": 0.5}
    ],
    "rootVelocity": 1,
    "future": {"times": [0.2, 0.4], "pos": 1, "dir": 0.8},
    "past": {"times": [-0.2], "pos": 0.5}
  },
  "search": {"interval": 0.1, "switchMargin": 0, "acceptCost": -1, "continuityWindow": 0.25, "accel": "auto", "kdMinRows": 40000},
  "trajectory": {"velocityHalfLife": 0.2, "turnHalfLife": 0.15, "historySeconds": 1, "faceSpeedMin": 5},
  "inertialization": {"halfLife": 0.1},
  "clips": [
    {"clip": "Meshes/KenneyChar_idle.ocanim", "start": 0, "end": -1, "loop": true, "bias": 0, "noSearch": false, "tags": ["idle"], "ranges": []},
    {"clip": "Meshes/KenneyChar_walk.ocanim", "start": 0, "end": -1, "loop": true, "bias": 0, "noSearch": false, "tags": ["walk"], "ranges": []},
    {"clip": "Meshes/KenneyChar_sprint.ocanim", "start": 0, "end": -1, "loop": true, "bias": 0, "noSearch": false, "tags": ["run"], "ranges": []}
  ]
}
```

`Content/Demo/Anim/Strike.ocmontage` and `Lunge.ocmontage` (format: `modules/anim/src/AnimGraphAsset.cpp:406-496`):

```json
{
  "format": "ocmontage",
  "version": 1,
  "name": "Strike",
  "slot": "DefaultSlot",
  "blendIn": 0.1,
  "blendOut": 0.2,
  "blendInCurve": "smooth",
  "blendOutCurve": "smooth",
  "blendOutTrigger": -1,
  "rate": 1,
  "additive": "auto",
  "segments": [
    {"clip": "Meshes/KenneyChar_attack-melee-right.ocanim", "start": 0, "end": -1, "rate": 1},
    {"clip": "Meshes/KenneyChar_attack-kick-right.ocanim", "start": 0, "end": -1, "rate": 1}
  ],
  "sections": [
    {"name": "Melee", "start": 0},
    {"name": "Kick", "start": 0.417},
    {"name": "Recover", "start": 0.8, "next": "@end"}
  ],
  "notifies": [
    {"time": 0.15, "name": "HitStart", "duration": 0.1},
    {"time": 0.6, "name": "Whoosh"}
  ]
}
```

```json
{
  "format": "ocmontage",
  "version": 1,
  "name": "Lunge",
  "slot": "DefaultSlot",
  "blendIn": 0.05,
  "blendOut": 0.15,
  "blendInCurve": "smooth",
  "blendOutCurve": "smooth",
  "blendOutTrigger": -1,
  "rate": 1,
  "additive": "auto",
  "segments": [
    {"clip": "Meshes/KenneyChar_attack-kick-right.ocanim", "start": 0, "end": -1, "rate": 1}
  ],
  "sections": [
    {"name": "Lunge", "start": 0, "next": "@end"}
  ],
  "notifies": []
}
```

`Content/Demo/Anim/KenneyReach.ocikrig` and `MannequinIk.ocikrig` (format: `modules/anim/src/IkRigAsset.cpp:86-190`):

```json
{
  "format": "ocikrig",
  "version": 1,
  "name": "KenneyReach",
  "skeleton": "Meshes/KenneyChar.ocskel",
  "body": {"bone": "", "goal": "", "maxOffsetCm": 40},
  "chains": [
    {"name": "Lean", "start": "torso", "end": "head", "solver": "ccd", "priority": 0, "goal": "Reach", "iterations": 12, "toleranceCm": 0.5, "maxStepDeg": 20, "limits": true, "tipRotation": false, "pole": {"mode": "animated"}}
  ],
  "limits": [
    {"bone": "torso", "swing1": 35, "swing2": 35, "twist": 25}
  ],
  "feet": [],
  "foot": {"pelvisMode": "lowest", "smoothing": 14, "maxPelvisDropCm": 40, "maxFootOffsetCm": 45, "layerMask": 4294967295}
}
```

```json
{
  "format": "ocikrig",
  "version": 1,
  "name": "MannequinIk",
  "skeleton": "Skeletons/Character.ocskel",
  "body": {"bone": "Hips", "goal": "", "maxOffsetCm": 8366},
  "chains": [
    {"name": "LegL", "start": "LeftUpLeg", "end": "LeftFoot", "solver": "twobone", "priority": 1, "goal": "", "iterations": 10, "toleranceCm": 21, "maxStepDeg": 30, "limits": false, "tipRotation": false, "pole": {"mode": "animated"}},
    {"name": "LegR", "start": "RightUpLeg", "end": "RightFoot", "solver": "twobone", "priority": 1, "goal": "", "iterations": 10, "toleranceCm": 21, "maxStepDeg": 30, "limits": false, "tipRotation": false, "pole": {"mode": "animated"}},
    {"name": "ArmR", "start": "RightArm", "end": "RightHand", "solver": "twobone", "priority": 0, "goal": "Reach", "iterations": 10, "toleranceCm": 21, "maxStepDeg": 30, "limits": false, "tipRotation": false, "pole": {"mode": "vector", "dirModel": [0, -0.5, -1]}}
  ],
  "limits": [],
  "feet": [
    {"chain": "LegL", "toe": "LeftToes", "soleOffsetCm": -1, "plantCurve": "", "plantLowCm": 418, "plantHighCm": 2092, "probeUpCm": 6275, "probeDownCm": 12550, "alignToNormal": true, "maxAlignDeg": 30},
    {"chain": "LegR", "toe": "RightToes", "soleOffsetCm": -1, "plantCurve": "", "plantLowCm": 418, "plantHighCm": 2092, "probeUpCm": 6275, "probeDownCm": 12550, "alignToNormal": true, "maxAlignDeg": 30}
  ],
  "foot": {"pelvisMode": "lowest", "smoothing": 14, "maxPelvisDropCm": 8366, "maxFootOffsetCm": 9412, "layerMask": 4294967295}
}
```

`Content/Demo/Anim/KenneyLook.ocrig` (format: `modules/formats/src/OcRig.cpp:233-390`):

```text
OCRIG 2
# Head look-at for the Kenney figure, driven from AN_DemoHero.ocgraph (arm 7 and 8):
#   Look   is the op weight, 0 = clip untouched, 1 = head fully on the target (Q toggles it)
#   LookAt is the world position to face (the IkBall's position, set every tick while Look is on)
# axis=-1,0,0 is the head bone's OWN forward: the Kenney rig faces -X in model space and every bone has an
# identity rest rotation, so the bone-local forward is -X as well (UNVERIFIED by running).
NAME KenneyLook
INPUT LookAt vec3 space=world default=0,0,0
INPUT Look float default=0
OP aim bone=head at=@LookAt axis=-1,0,0 weight=@Look
```

`Content/Demo/Anim/KenneyToMannequin.ocretarget` (format: `modules/anim/src/RetargetAsset.cpp:103-175`; `valid(src, dst)` demands that
`root.dstBone` `Hips` is an ancestor of every target chain start, which holds, and that no bone is in two chains, which holds):

```json
{
  "format": "ocretarget",
  "version": 1,
  "name": "KenneyToMannequin",
  "source": "Meshes/KenneyChar.ocskel",
  "target": "Skeletons/Character.ocskel",
  "root": {"srcBone": "torso", "dstBone": "Hips", "scaleH": "auto", "scaleV": "auto", "offsetCm": [0, 0, 0]},
  "rootMotion": {"srcBone": "", "dstBone": "", "scale": "auto"},
  "unmapped": "rest",
  "chains": [
    {"name": "Spine", "src": ["torso", "torso"], "dst": ["Spine", "UpperChest"], "mode": "fk", "rotation": "interpolate", "translation": "none", "anchor": "auto", "scale": "auto", "pole": "fk", "tipRotation": false},
    {"name": "Head", "src": ["head", "head"], "dst": ["Neck", "Head"], "mode": "fk", "rotation": "interpolate", "translation": "none", "anchor": "auto", "scale": "auto", "pole": "fk", "tipRotation": false},
    {"name": "ArmL", "src": ["arm-left", "arm-left"], "dst": ["LeftShoulder", "LeftHand"], "mode": "fk", "rotation": "interpolate", "translation": "none", "anchor": "auto", "scale": "auto", "pole": "fk", "tipRotation": false},
    {"name": "ArmR", "src": ["arm-right", "arm-right"], "dst": ["RightShoulder", "RightHand"], "mode": "fk", "rotation": "interpolate", "translation": "none", "anchor": "auto", "scale": "auto", "pole": "fk", "tipRotation": false},
    {"name": "LegL", "src": ["leg-left", "leg-left"], "dst": ["LeftUpLeg", "LeftToes"], "mode": "fk", "rotation": "interpolate", "translation": "none", "anchor": "auto", "scale": "auto", "pole": "fk", "tipRotation": false},
    {"name": "LegR", "src": ["leg-right", "leg-right"], "dst": ["RightUpLeg", "RightToes"], "mode": "fk", "rotation": "interpolate", "translation": "none", "anchor": "auto", "scale": "auto", "pole": "fk", "tipRotation": false}
  ]
}
```

`Content/Meshes/KenneyChar.ocragdoll` (format: `modules/ragdoll/src/RagdollBuilder.cpp:61-111`; the only file of this set that goes
beside an imported asset):

```text
# Ragdoll overrides for Meshes/KenneyChar.ocskel (read by the ragdoll builder when it first sees the skeleton).
# KenneyChar has 7 bones and five of them are leaves, which the automatic rules give no physics part.
# `on` forces a part; a leaf needs `length` because it has no child to point at. Radii and lengths are in the
# skeleton's own units (the mesh is 67 units tall); the entity's scale (2.6) multiplies them.
# root and torso get their parts from the automatic rule; the legs stay without parts and ride the root part.
mass 40
bone head on length 10 radius 5
bone arm-left on length 14 radius 3
bone arm-right on length 14 radius 3
```

`Content/Materials/M_DemoMannequin.ocmat` (a copy of the First Person template's `M_Character.ocmat`; `Textures/Character_Skin.png` is already in PTTest):

```text
OCMAT 1
# The 45-bone mannequin's skin. Copy of templates/FirstPerson/Content/Materials/M_Character.ocmat; the texture
# Textures/Character_Skin.png already sits in PTTest/Content/Textures.
NAME M_DemoMannequin
SHADER standard
BLEND opaque
CULL back
FLAGS twosided=0 castshadow=1

PARAM baseColorFactor 1.0 1.0 1.0 1.0
PARAM metallicFactor 0.0
PARAM roughnessFactor 0.75
PARAM emissiveFactor 0.0 0.0 0.0
PARAM normalScale 1.0
PARAM occlusionStrength 1.0

TEX baseColor {path:Textures/Character_Skin.png} uv0 sRGB
```

`Content/Demo/Maps/Demo_Animation.ocworld`:

```text
OCWORLD 1
# Demo_Animation: motion matching, montages, root motion, IK, retargeting, ragdoll, control-rig look-at.
# Centimetres, +X forward, +Y right, +Z up. Everything interesting is a graph class placed with `class`.
# Read Content/Demo/README.md for the keys. Player: AN_DemoHero (DEFAULTPAWN), spawned at SPAWN.
NAME Demo_Animation
BUILD 0
ALGO 3
GAMEMODE AN_DemoRules
DEFAULTPAWN AN_DemoHero
SPAWN 0 0 10 0
CAMERA -600 -500 450 35 -20 800
SUN elev 55 azim 40 color 1 1 1 lux 100000
SKY model physical
FOG exp density 0.00002 color 0.7 0.78 0.88

# ---- the floor: 10 x 10 m x 100, top surface at z = 0 (cube.ocmesh is +-1, so scale is HALF extents) ----------
PLACEG Meshes/cube.ocmesh 0 0 -10 0 0 0 5000 5000 10 Caldera_concrete

# ---- station A, the hero at the origin: a ball that circles (350, 0, 120) and is found by the name IkBall ------
PLACE none 350 0 120 0 0 0 1 class AN_DemoOrbiter

# ---- station B, steps and a ramp for the hero to walk over (foot IK on the Kenney rig is the pelvis only) -----
# Four steps 120 deep, 400 wide, rising 20 cm each: tops at 20, 40, 60, 80.
PLACEG Meshes/cube.ocmesh 1260 0 10 0 0 0 60 200 10 Caldera_wood
PLACEG Meshes/cube.ocmesh 1380 0 20 0 0 0 60 200 20 Caldera_wood
PLACEG Meshes/cube.ocmesh 1500 0 30 0 0 0 60 200 30 Caldera_wood
PLACEG Meshes/cube.ocmesh 1620 0 40 0 0 0 60 200 40 Caldera_wood
# A ramp: 8 m long, 4 m wide, 12 cm thick, pitched. If it slopes DOWN away from the origin, flip the sign of
# the pitch (the second rotation number, -12 -> 12). Rotated static boxes collide with their rotation.
PLACEG Meshes/cube.ocmesh 1700 700 60 0 -12 0 400 200 6 Caldera_pavers

# ---- station C, the retarget dummy: walks a 3 m circle around this point playing a Kenney clip --------------------
PLACE none -900 900 0 0 0 0 1 class AN_DemoMannequin

# ---- station D, the foot-IK dummy: origin ON THE EDGE of a 20 cm block (the block covers y from 1500 to 2000) ------
PLACEG Meshes/cube.ocmesh 0 1750 10 0 0 0 250 250 10 Caldera_concrete
PLACE none 0 1500 20 0 0 0 1 class AN_DemoMannequinIk
```

### 6.3 Demo_Gameplay

The ability system, its events, a data table row and a localised string are all driven from `AN_DemoCaster`. The attribute set has a
meta attribute `Damage` that forwards to `Health` with scale -1, so "damage" effects are written as `MOD Damage add N`
(`docs/GAMEPLAY_ABILITIES.md` "Attributes"). Fireball and Heal both cost mana and then block themselves with a cooldown tag for 3 and 5 s;
their bodies are graph entries on `GAS.AbilityActivated.<ability>`. U14, U15.

`Content/Demo/Gameplay/Demo.ocgas` (format: `scripting/csharp/Aver.Framework/Gas/OcGasParser.cs`, `docs/GAMEPLAY_ABILITIES.md`):

```text
OCGAS 1
# Gameplay definitions for the Demo_Gameplay level. Loaded at every Play start (any *.ocgas under Content).
# Owner: AN_DemoCaster.ocgraph. Errors are logged as "[GAS] Content/Demo/Gameplay/Demo.ocgas:<line>: ...".

TAG State.Poisoned Cooldown.Fireball Cooldown.Heal Data.Damage

# Health and Mana are clamped to their Max attributes. Damage is a META attribute: an instant Add to it never
# changes Damage itself, it is forwarded to Health with scale -1 (so "Damage +18" is "Health -18").
ATTRSET DemoHero
  ATTR Health 100 min=0 max=MaxHealth
  ATTR MaxHealth 100 min=1
  ATTR Mana 60 min=0 max=MaxMana
  ATTR MaxMana 60 min=1
  ATTR Damage 0 meta forward=Health scale=-1

# ---- Fireball: 20 mana up front, then 3 s during which the cooldown tag blocks it ----------------------------
EFFECT Effect.FireballCost
  POLICY instant
  MOD Mana add -20

EFFECT Effect.FireballCooldown
  POLICY duration 3
  GRANTS Cooldown.Fireball

ABILITY Ability.Fireball
  TAGS Ability.Fireball
  COST Effect.FireballCost
  COOLDOWN Effect.FireballCooldown
  DURATION 0.5

# ---- Heal: 15 mana, 5 s cooldown, +30 health ------------------------------------------------------------------
EFFECT Effect.Heal
  POLICY instant
  MOD Health add 30

EFFECT Effect.HealCost
  POLICY instant
  MOD Mana add -15

EFFECT Effect.HealCooldown
  POLICY duration 5
  GRANTS Cooldown.Heal

ABILITY Ability.Heal
  TAGS Ability.Heal
  COST Effect.HealCost
  COOLDOWN Effect.HealCooldown
  DURATION 0.3

# ---- passive and over-time effects ------------------------------------------------------------------------------
# +3 mana every second, forever (the graph applies it once at OnStart).
EFFECT Effect.ManaRegen
  POLICY infinite
  PERIOD 1
  MOD Mana add 3

# 4 damage on application and every second for 6 s; reapplying stacks up to 3 and refreshes the timer.
EFFECT Effect.Poison
  POLICY duration 6
  PERIOD 1 apply
  MOD Damage add 4
  STACK target 3 refresh
  GRANTS State.Poisoned

# A one-shot hit whose size the caller supplies (the 4 key passes 18).
EFFECT Effect.Bolt
  POLICY instant
  MOD Damage add caller=Data.Damage

# An ability carries tags, cost, cooldown and a duration; what it DOES is its body. Ability.Heal's body is the
# graph entry GAS.AbilityActivated.Ability.Heal in AN_DemoCaster, which applies Effect.Heal to its owner.

# Give the placed class actor its attribute set and abilities at begin play. (The graph also checks and adds
# them by node, so the demo runs even if class defaults are not applied to placed class actors.)
ACTORDEFAULTS AN_DemoCaster
  ATTRSET DemoHero
  GRANT Ability.Fireball
  GRANT Ability.Heal
```

`Content/Demo/Data/DemoSpells.csv` (columns and type hints: `docs/DATA_TABLES.md`):

```csv
Name,Damage:int,ManaCost:float,Tint:vec3,DisplayName:text
fireball,35,20,1 0.4 0.1,@DemoUI:spell.fireball=Fireball
heal,-30,15,0.2 1 0.3,@DemoUI:spell.heal=Heal
```

`Content/Localization/en/DemoUI.ocstrings` and `Content/Localization/de/DemoUI.ocstrings` (format: `docs/LOCALIZATION.md`):

```text
OCSTRINGS 1
CULTURE en
# Table DemoUI, default culture. Used by AN_DemoCaster (hud.title) and by the DisplayName column of
# Demo/Data/DemoSpells.csv (spell.*). Press L in Demo_Gameplay to switch to the German table.
ENTRY hud.title "Gameplay Demo: 1 Fireball, 2 Heal, 3 Poison, 4 Hit, L Language"
ENTRY spell.fireball "Fireball"
ENTRY spell.heal "Heal"
```

```text
OCSTRINGS 1
CULTURE de
ENTRY hud.title "Spiel-Demo: 1 Feuerball, 2 Heilen, 3 Gift, 4 Treffer, L Sprache"
ENTRY spell.fireball "Feuerball"
ENTRY spell.heal "Heilung"
```

`Content/Demo/Input/Demo.ocinput` (grammar: `modules/formats/src/OcInput.cpp` header comment; trigger semantics F21). The combo
action `DemoCombo` has no binding of its own: its trigger is driven by the other two actions rising (U16):

```text
OCINPUT 1
# Demo_Gameplay input probe. Set it as the project's input scheme: Project Settings > Input Scheme, or the
# project key INPUT.SCHEME Demo/Input/Demo.ocinput. Read by AN_DemoInputProbe.ocgraph (Input.<action>.<event>).
NAME DemoControls

# T: Triggered on RELEASE if the key was down 0.2 s or less (a tap).
ACTION DemoTap digital trigger tap
# Y: Started on press, Triggered ONCE when held 0.6 s, Completed on release afterwards, Canceled if released early.
ACTION DemoHold digital trigger hold:0.6:once
# C then V within 0.6 s of each other: DemoCombo Triggered. The step actions are ordinary key actions.
ACTION DemoComboC digital
ACTION DemoComboV digital
ACTION DemoCombo digital trigger combo:0.6:DemoComboC,DemoComboV

BIND DemoTap key T
BIND DemoHold key Y
BIND DemoComboC key C
BIND DemoComboV key V

CONTEXT DemoControls priority 0
```

`Content/Demo/Scripts/AN_DemoCaster.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# THE GAMEPLAY DEMO TOTEM: owns a Gameplay Ability System (attribute set DemoHero, abilities Fireball and
# Heal, a mana regen effect) defined in Demo/Gameplay/Demo.ocgas, and reacts to its bus events. Keys:
#   1 Fireball (20 mana, 3 s cooldown)   2 Heal (15 mana, 5 s cooldown)   3 Poison self (6 s, stacks to 3)
#   4 Take an 18 point hit (SetByCaller magnitude)   5 print the Fireball cooldown   L switch en <-> de
# Health and Mana print as "[Graph] Health_now = 87" lines (editor viewport overlay + Output Log).
# The same ability / data-table / localisation chain also fills one on-screen text widget.
#
# Events arrive on the OWNER, so the handlers live in the same graph. AN_EventPayload index 1 is the new value
# of GAS.AttributeChanged. An event entry runs with deltaTime 0, so no PARAM deltaTime is declared.
NAME DemoCaster
DESCRIPTION Gameplay Ability System demo: abilities on keys, attribute events, data table row, localised text.

CLASS AN_DemoCaster Actor

PARAM entity int

VAR hud int 0
VAR german bool false

NODE ent Param param=entity
NODE hudV GetVar var=hud
NODE idx1 ConstInt value=1
NODE tTrue ConstBool value=true
NODE tFalse ConstBool value=false

# ======================================================================================================
# ON START. ACTORDEFAULTS in the .ocgas normally gives this actor its set and abilities at begin play; the
# Health read below is the guard that does it by node if that did not happen (UNVERIFIED for placed
# class actors), so the demo works either way. Then regen, then the one text widget.
# ======================================================================================================
ENTRY seed OnStart
NODE seed OnStart
NODE hasHealth AN_GasGetAttribute attribute=Health
LINK ent.value hasHealth.entity
NODE missing Not
LINK hasHealth.success missing.a
NODE setGate Branch
LINK seed.exec setGate.exec
LINK missing.result setGate.cond
NODE addSet AN_GasAddAttributeSet set=DemoHero
LINK setGate.true addSet.exec
LINK ent.value addSet.entity
NODE grantF AN_GasGrantAbility ability=Ability.Fireball
LINK addSet.then grantF.exec
LINK ent.value grantF.entity
NODE grantH AN_GasGrantAbility ability=Ability.Heal
LINK grantF.then grantH.exec
LINK ent.value grantH.entity

NODE regen AN_GasApplyEffect effect=Effect.ManaRegen
LINK setGate.false regen.exec
LINK grantH.then regen.exec
LINK ent.value regen.target
LINK ent.value regen.source

# Kind 1 = text widget. A widget with no parent is a new root, drawn top-left.
NODE zeroI ConstInt value=0
NODE textKind ConstInt value=1
NODE mkText AN_CreateUiWidget name=HudText
LINK regen.then mkText.exec
LINK zeroI.value mkText.parent
LINK textKind.value mkText.kind
NODE saveHud SetVar var=hud
LINK mkText.then saveHud.exec
LINK mkText.widget saveHud.value
NODE title AN_SetUiText text=@DemoUI:hud.title=Gameplay_Demo
LINK saveHud.then title.exec
LINK mkText.widget title.widget

# ======================================================================================================
# ON TICK: one arm per key.
# ======================================================================================================
ENTRY tick OnTick
NODE tick OnTick
NODE fan Sequence
PIN fan exec in exec
PIN fan then0 out exec
PIN fan then1 out exec
PIN fan then2 out exec
PIN fan then3 out exec
PIN fan then4 out exec
PIN fan then5 out exec
LINK tick.exec fan.exec

# ---- key 1 (D1 = Key 27): Fireball ----------------------------------------------------------------------
NODE k1 ConstInt value=27
NODE p1 InputKeyPressed
LINK k1.value p1.key
NODE g1 Branch
LINK fan.then0 g1.exec
LINK p1.triggered g1.cond
NODE fire AN_GasTryActivate ability=Ability.Fireball
LINK g1.true fire.exec
LINK ent.value fire.entity
NODE fireOk Branch
LINK fire.then fireOk.exec
LINK fire.success fireOk.cond
NODE fireRefused PrintInt
LINK fireOk.false fireRefused.exec
LINK fire.reason fireRefused.value

# ---- key 2 (D2 = Key 28): Heal ----------------------------------------------------------------------------
NODE k2 ConstInt value=28
NODE p2 InputKeyPressed
LINK k2.value p2.key
NODE g2 Branch
LINK fan.then1 g2.exec
LINK p2.triggered g2.cond
NODE heal AN_GasTryActivate ability=Ability.Heal
LINK g2.true heal.exec
LINK ent.value heal.entity
NODE healOk Branch
LINK heal.then healOk.exec
LINK heal.success healOk.cond
NODE healRefused PrintInt
LINK healOk.false healRefused.exec
LINK heal.reason healRefused.value

# ---- key 3 (D3 = Key 29): Poison, a periodic damage-over-time effect -------------------------------------
NODE k3 ConstInt value=29
NODE p3 InputKeyPressed
LINK k3.value p3.key
NODE g3 Branch
LINK fan.then2 g3.exec
LINK p3.triggered g3.cond
NODE poison AN_GasApplyEffect effect=Effect.Poison
LINK g3.true poison.exec
LINK ent.value poison.target
LINK ent.value poison.source
NODE poisonHandle PrintInt
LINK poison.then poisonHandle.exec
LINK poison.handle poisonHandle.value

# ---- key 4 (D4 = Key 30): an 18 point hit through a SetByCaller magnitude -------------------------------
NODE k4 ConstInt value=30
NODE p4 InputKeyPressed
LINK k4.value p4.key
NODE g4 Branch
LINK fan.then3 g4.exec
LINK p4.triggered g4.cond
NODE hitDamage ConstFloat value=18.0
NODE hit AN_GasApplyEffect effect=Effect.Bolt callertag=Data.Damage
LINK g4.true hit.exec
LINK ent.value hit.target
LINK ent.value hit.source
LINK hitDamage.value hit.callerValue

# ---- key 5 (D5 = Key 31): print the seconds left on Fireball's cooldown ------------------------------------
NODE k5 ConstInt value=31
NODE p5 InputKeyPressed
LINK k5.value p5.key
NODE g5 Branch
LINK fan.then4 g5.exec
LINK p5.triggered g5.cond
NODE cd AN_GasCooldown ability=Ability.Fireball
LINK ent.value cd.entity
NODE fireballCooldownSeconds Print
LINK g5.true fireballCooldownSeconds.exec
LINK cd.remaining fireballCooldownSeconds.value

# ---- key L (Key 11): switch the language; the text widget re-resolves its @DemoUI reference ----------------
NODE kL ConstInt value=11
NODE pL InputKeyPressed
LINK kL.value pL.key
NODE gL Branch
LINK fan.then5 gL.exec
LINK pL.triggered gL.cond
NODE deV GetVar var=german
NODE deNext Not
LINK deV.value deNext.a
NODE deSet SetVar var=german
LINK gL.true deSet.exec
LINK deNext.result deSet.value
NODE langBranch Branch
LINK deSet.then langBranch.exec
LINK deNext.result langBranch.cond
NODE toDe AN_SetCulture culture=de
LINK langBranch.true toDe.exec
NODE toEn AN_SetCulture culture=en
LINK langBranch.false toEn.exec

# ======================================================================================================
# EVENTS from the ability system. The entry name IS the bus event name.
# ======================================================================================================
# Health changed. Payload 0 = attribute id, 1 = new value, 2 = old value.
ENTRY hpEvent GAS.AttributeChanged.Health
NODE hpEvent AN_OnEvent
NODE hpPay AN_EventPayload
LINK hpEvent.exec hpPay.exec
LINK idx1.value hpPay.index
NODE Health_now Print
LINK hpPay.then Health_now.exec
LINK hpPay.f Health_now.value

ENTRY mpEvent GAS.AttributeChanged.Mana
NODE mpEvent AN_OnEvent
NODE mpPay AN_EventPayload
LINK mpEvent.exec mpPay.exec
LINK idx1.value mpPay.index
NODE Mana_now Print
LINK mpPay.then Mana_now.exec
LINK mpPay.f Mana_now.value

# Poison tag came or went (payload 1 = present 0/1).
ENTRY poisonTag GAS.TagChanged.State.Poisoned
NODE poisonTag AN_OnEvent
NODE poisonPay AN_EventPayload
LINK poisonTag.exec poisonPay.exec
LINK idx1.value poisonPay.index
NODE State_Poisoned_present PrintInt
LINK poisonPay.then State_Poisoned_present.exec
LINK poisonPay.i State_Poisoned_present.value

# Graph-authored ability bodies: activation raises GAS.AbilityActivated.<ability>. Each looks its row up in
# the data table, prints cost and damage, and shows the LOCALISED spell name in the text widget.
ENTRY fireballBody GAS.AbilityActivated.Ability.Fireball
NODE fireballBody AN_OnEvent
NODE rowF AN_FindDataTableRow table=Demo/Data/DemoSpells.csv row=fireball
NODE costF AN_GetDataTableFloat table=Demo/Data/DemoSpells.csv column=ManaCost
LINK rowF.row costF.row
NODE dmgF AN_GetDataTableInt table=Demo/Data/DemoSpells.csv column=Damage
LINK rowF.row dmgF.row
NODE table_fireball_ManaCost Print
LINK fireballBody.exec table_fireball_ManaCost.exec
LINK costF.value table_fireball_ManaCost.value
NODE table_fireball_Damage PrintInt
LINK table_fireball_ManaCost.then table_fireball_Damage.exec
LINK dmgF.value table_fireball_Damage.value
NODE nameF AN_SetUiTextFromTable table=Demo/Data/DemoSpells.csv column=DisplayName
LINK table_fireball_Damage.then nameF.exec
LINK hudV.value nameF.widget
LINK rowF.row nameF.row

ENTRY healBody GAS.AbilityActivated.Ability.Heal
NODE healBody AN_OnEvent
NODE rowH AN_FindDataTableRow table=Demo/Data/DemoSpells.csv row=heal
NODE costH AN_GetDataTableFloat table=Demo/Data/DemoSpells.csv column=ManaCost
LINK rowH.row costH.row
NODE table_heal_ManaCost Print
LINK healBody.exec table_heal_ManaCost.exec
LINK costH.value table_heal_ManaCost.value
NODE applyHeal AN_GasApplyEffect effect=Effect.Heal
LINK table_heal_ManaCost.then applyHeal.exec
LINK ent.value applyHeal.target
LINK ent.value applyHeal.source
NODE nameH AN_SetUiTextFromTable table=Demo/Data/DemoSpells.csv column=DisplayName
LINK applyHeal.then nameH.exec
LINK hudV.value nameH.widget
LINK rowH.row nameH.row
```

`Content/Demo/Scripts/AN_DemoInputProbe.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# ENHANCED INPUT PROBE. Demo/Input/Demo.ocinput (project key INPUT.SCHEME) declares four actions with triggers;
# each event the input system raises reaches this graph as the bus event Input.<action>.<event>.
#   T  tap    -> DemoTap Triggered on RELEASE if the key was held 0.2 s or less
#   Y  hold   -> DemoHold Started on press, Triggered once after 0.6 s held, Completed on release after that,
#                Canceled if released early
#   C then V within 0.6 s -> DemoCombo Triggered
# Payload for input events: index 0 = x, 1 = y, 2 = seconds held, 3 = seconds since triggered.
NAME DemoInputProbe
DESCRIPTION Prints the tap / hold / combo input events defined in Demo.ocinput.

CLASS AN_DemoInputProbe Actor

NODE idx2 ConstInt value=2

ENTRY tapEvt Input.DemoTap.Triggered
NODE tapEvt AN_OnEvent
NODE TAP_triggered PrintString text=Tap_released_quickly
LINK tapEvt.exec TAP_triggered.exec

ENTRY holdStart Input.DemoHold.Started
NODE holdStart AN_OnEvent
NODE HOLD_started PrintString text=Hold_pressed
LINK holdStart.exec HOLD_started.exec

ENTRY holdTrig Input.DemoHold.Triggered
NODE holdTrig AN_OnEvent
NODE holdPay AN_EventPayload
LINK holdTrig.exec holdPay.exec
LINK idx2.value holdPay.index
NODE HOLD_triggered_seconds_held Print
LINK holdPay.then HOLD_triggered_seconds_held.exec
LINK holdPay.f HOLD_triggered_seconds_held.value

ENTRY holdDone Input.DemoHold.Completed
NODE holdDone AN_OnEvent
NODE HOLD_completed PrintString text=Hold_released_after_trigger
LINK holdDone.exec HOLD_completed.exec

ENTRY holdCancel Input.DemoHold.Canceled
NODE holdCancel AN_OnEvent
NODE HOLD_canceled PrintString text=Hold_released_too_early
LINK holdCancel.exec HOLD_canceled.exec

ENTRY comboEvt Input.DemoCombo.Triggered
NODE comboEvt AN_OnEvent
NODE COMBO_triggered PrintString text=Combo_C_then_V
LINK comboEvt.exec COMBO_triggered.exec
```

`Content/Demo/Maps/Demo_Gameplay.ocworld`:

```text
OCWORLD 1
# Demo_Gameplay: gameplay abilities (GAS), enhanced input triggers, a data table, localised text.
# Needs the project's input scheme set to Demo/Input/Demo.ocinput for the T / Y / C+V probe (README step 3).
NAME Demo_Gameplay
BUILD 0
ALGO 3
GAMEMODE AN_DemoRules
SPAWN -600 0 10 0
CAMERA -1200 -400 400 20 -15 800
SUN elev 50 azim 60 color 1 1 1 lux 100000
SKY model physical
FOG exp density 0.00002 color 0.7 0.78 0.88

PLACEG Meshes/cube.ocmesh 0 0 -10 0 0 0 2500 2500 10 Caldera_pavers

# The visible totem is a plain pillar; the logic actors below are invisible and sit on it.
PLACEG Meshes/cube.ocmesh 500 0 100 0 0 0 40 40 100 Caldera_metal
PLACE none 500 0 0 0 0 0 1 class AN_DemoCaster name Caster
PLACE none 0 0 0 0 0 0 1 class AN_DemoInputProbe name Probe
```

### 6.4 Demo_AI

How it fits together: the Patron's tree is `Seek -> Approach -> Sit -> Rest -> Seek`. `Seek` claims a seat (`ClaimSmartObject`); `Approach`
waits while the graph walks the body to the slot's entry point and then sends the brain the event `Arrived` (`AN_BrainSendEvent`, seen by
the tree's `ON event:Arrived` at its next update); `Sit` runs `UseSmartObject` (the slot raises `SatDown`, the graph snaps the body onto
the seat) with a 7 s `Wait`, and leaving the state releases the claim; `Rest` runs the environment query `FindSpot.oceqs`, which writes the
board key `WanderPos` that the graph walks to while it holds no claim. The probe runs `FindNearPlayer.oceqs` against the player once a second
(`AN_EqsRun`) and parks a ball on the winner; **Show > AI** (`docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md` section 5) draws every item,
the slot states and the tree. Nothing needs a nav grid. U17, U18.

`Content/Demo/AI/Bench.ocso` (grammar: `docs/formats/FORMAT_SPECS.md` 11h, parser `modules/synapse/src/SmartObjectAsset.cpp`):

```text
OCSO 1
# Two-seat park bench. Positions are local to the owning entity, centimetres, +X forward, +Y right, +Z up.
# The bench faces +X: the seats are behind (-X) the entry points, so a user walks up from the front.
DESC "Park bench: two seats"
TAGS Furniture.Seat Furniture.Outdoor

SLOT sit_left
  TAGS Furniture.Seat
  POS -40 -30 45
  YAW 0
  ENTRY 90 -30 0 180
  COOLDOWN 4
  CLAIMTTL 40
  USE event SatDown
  RELEASE auto
END

SLOT sit_right
  TAGS Furniture.Seat
  POS -40 30 45
  YAW 0
  ENTRY 90 30 0 180
  COOLDOWN 4
  CLAIMTTL 40
  USE event SatDown
  RELEASE auto
END
```

`Content/Demo/AI/Patron.ocst` (grammar: 11i, parser `modules/synapse/src/StateTreeAsset.cpp:158-330`; leaf names are the built-ins registered at
`modules/synapse.scene/src/SynapseAi.cpp:449-467`):

```text
OCST 1
# The Patron's brain. Loop: Seek (claim a free seat) -> Approach (the graph walks the body and sends the event
# "Arrived") -> Sit (use the slot for 7 s) -> Rest (run an environment query, wait) -> Seek again.
# Every state's tasks are built-in leaves: ClaimSmartObject, Wait, UseSmartObject, RunEqsQuery.
DESC "Patron: claim a free seat, wait to arrive, sit, then pick a spot to wander to"

# RunEqsQuery writes these keys (see the OUTPUT line of FindSpot.oceqs); AN_DemoPatron reads WanderPos.
KEY WanderPos vec3 agent
KEY WanderScore float agent
KEY WanderCount int agent

STATE Seek
  # nearest free slot tagged Furniture.Seat within 40 m (p0), claim held at most 30 s (p1)
  TASK ClaimSmartObject p0=4000 p1=30 s="Furniture.Seat"
  ON completed TO Approach
  ON failed TO Rest
END

STATE Approach
  # nothing to do here but wait: the body is walking. The graph sends "Arrived" at the slot's entry point.
  TASK Wait p0=60
  ON event:Arrived TO Sit
  ON completed TO Seek
  ON failed TO Rest
END

STATE Sit
  # complete when ANY task is done: the 7 s Wait ends the visit; leaving the state aborts UseSmartObject,
  # which releases the claim (the slot then cools down for 4 s).
  COMPLETE any
  TASK UseSmartObject
  TASK Wait p0=7
  ON completed TO Rest
  ON failed TO Rest
END

STATE Rest
  # p0 = give up on the query after 2 s
  TASK RunEqsQuery p0=2 s="Demo/AI/FindSpot.oceqs"
  TASK Wait p0=8
  ON completed TO Seek
  ON failed TO Seek
END
```

`Content/Demo/AI/FindSpot.oceqs` and `FindNearPlayer.oceqs` (grammar: 11g, parser `modules/synapse/src/EqsAsset.cpp:114-360`):

```text
OCEQS 1
# Used by Patron.ocst (RunEqsQuery). Needs no navigation grid and no target: every test is about the querier.
DESC "A spot 3 to 8 m from the querier: prefer the far ring, with a little randomness"
OUTPUT pos=WanderPos score=WanderScore count=WanderCount
RESULT best
RUN firstvalid
OPTION
  GENERATOR ring centre=querier inner=300 outer=800 rings=3 count=12 project=none
  TEST distance to=querier curve=linear weight=1
  TEST random weight=0.5
```

```text
OCEQS 1
# Used by AN_DemoEqsProbe.ocgraph (AN_EqsRun). The querier is the probe, the target is the player.
# Show > AI draws every item with its score; the winner is the one the red ball sits on.
DESC "Spots 4 to 9 m from the probe: at least 3 m from the player, close to the player, hidden from him if possible"
OUTPUT pos=ProbePos score=ProbeScore count=ProbeCount
RESULT best
RUN firstvalid
OPTION
  GENERATOR ring centre=querier inner=400 outer=900 rings=3 count=16 project=none
  TEST distance to=target purpose=filter min=300
  TEST distance to=target curve=inverse weight=2
  TEST trace from=target fromz=160 toz=60 want=0 purpose=score weight=1
```

`Content/Demo/Scripts/AN_DemoBench.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# A BENCH THAT IS A SMART OBJECT. The visible bench is two COMP boxes; AN_SoAttach gives the entity the slots
# defined in Demo/AI/Bench.ocso. Placed at yaw 0 the bench faces +X (the slots, the entry points and the
# seat boxes are in its local frame). Placed anywhere else, the slots follow it.
NAME DemoBench
DESCRIPTION Two-seat bench; attaches Demo/AI/Bench.ocso at start and reports how many slots it has.

CLASS AN_DemoBench Actor

# Cube is +-1, so scale is HALF extents: seat 44 x 120 x 44 cm, back 8 x 120 x 44 cm.
COMP seat Mesh mesh=Meshes/cube.ocmesh material=Caldera_wood pos=-40,0,22 scale=22,60,22
COMP back Mesh mesh=Meshes/cube.ocmesh material=Caldera_wood pos=-62,0,66 scale=4,60,22

PARAM entity int

NODE ent Param param=entity

ENTRY seed OnStart
NODE seed OnStart
NODE attach AN_SoAttach asset=Demo/AI/Bench.ocso
LINK seed.exec attach.exec
LINK ent.value attach.entity
NODE slots AN_SoSlotCount
LINK ent.value slots.entity
NODE bench_slot_count PrintInt
LINK attach.then bench_slot_count.exec
LINK slots.count bench_slot_count.value
```

`Content/Demo/Scripts/AN_DemoPatron.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# THE PATRON: an AI whose BRAIN is a StateTree (Demo/AI/Patron.ocst) that claims a bench slot, waits until
# this graph reports arrival, sits, then runs an environment query to choose where to wander.
#
# SYNAPSE ADVISES, THE GRAPH MOVES (docs/SYNAPSE.md section 2): the tree decides, this graph walks the body.
# The Patron is an Actor (not a Character) moved with SetLocalPosition/LookAt on flat ground, so it needs
# no nav grid and, unlike a Character, never touches the player's camera.
#   claimed slot    -> walk to the slot's ENTRY point; on arrival send the brain the event "Arrived"
#   occupied slot   -> stay (the slot's USE event "SatDown" snaps the body onto the seat and faces it)
#   no claim        -> walk to the board key WanderPos (written by the tree's RunEqsQuery task)
# Animation follows the mode: 0 idle, 1 walk, 2 sit (the Kenney clips), switched only when the mode changes.
NAME DemoPatron
DESCRIPTION StateTree-driven patron: claims a smart-object bench slot, sits, wanders to an EQS-chosen spot.

CLASS AN_DemoPatron Actor

COMP pBody SkeletalMesh mesh=Meshes/KenneyChar.ocmesh material=KenneyChar_colormap skeleton=Meshes/KenneyChar.ocskel rot=180,0,0 scale=2.6,2.6,2.6

PARAM entity int
PARAM deltaTime float

VAR body int 0
VAR anim float -1
VAR sent bool false

NODE ent Param param=entity
NODE dt Param param=deltaTime
NODE bodyV GetVar var=body
NODE animV GetVar var=anim
NODE sentV GetVar var=sent
NODE zero ConstFloat value=0.0
NODE oneF ConstFloat value=1.0
NODE twoF ConstFloat value=2.0
NODE tTrue ConstBool value=true

# ---- the world as this tick sees it (all pure reads) ----------------------------------------------------
NODE claim AN_SoGetClaim
LINK ent.value claim.user
NODE pos GetWorldPosition
LINK ent.value pos.entity
NODE wander GetBlackboardVec3 key=WanderPos
LINK ent.value wander.entity

# claim.state: 1 claimed, 2 occupied.
NODE stateF IntToFloat
LINK claim.state stateF.a
NODE isClaimedState Equal
LINK stateF.result isClaimedState.a
LINK oneF.value isClaimedState.b
NODE isOccupiedState Equal
LINK stateF.result isOccupiedState.a
LINK twoF.value isOccupiedState.b
NODE claimed And
LINK claim.has claimed.a
LINK isClaimedState.result claimed.b
NODE occupied And
LINK claim.has occupied.a
LINK isOccupiedState.result occupied.b

# target (tx, ty): the slot entry while claimed, else the wander point, else stay put.
NODE txW Select
LINK wander.success txW.cond
LINK wander.x txW.ifTrue
LINK pos.x txW.ifFalse
NODE tyW Select
LINK wander.success tyW.cond
LINK wander.y tyW.ifTrue
LINK pos.y tyW.ifFalse
NODE tx Select
LINK claimed.result tx.cond
LINK claim.ex tx.ifTrue
LINK txW.result tx.ifFalse
NODE ty Select
LINK claimed.result ty.cond
LINK claim.ey ty.ifTrue
LINK tyW.result ty.ifFalse

NODE dx Subtract
LINK tx.result dx.a
LINK pos.x dx.b
NODE dy Subtract
LINK ty.result dy.a
LINK pos.y dy.b
NODE dist VecLength
LINK dx.result dist.ax
LINK dy.result dist.ay
LINK zero.value dist.az
NODE arriveR ConstFloat value=40.0
NODE far Greater
LINK dist.result far.a
LINK arriveR.value far.b
NODE notOccupied Not
LINK occupied.result notOccupied.a
NODE moving And
LINK far.result moving.a
LINK notOccupied.result moving.b

NODE speed ConstFloat value=160.0
NODE stepMax Multiply
LINK speed.value stepMax.a
LINK dt.value stepMax.b
NODE step Min
LINK stepMax.result step.a
LINK dist.result step.b
NODE dir VecNormalize
LINK dx.result dir.ax
LINK dy.result dir.ay
LINK zero.value dir.az
NODE stepX Multiply
LINK dir.x stepX.a
LINK step.result stepX.b
NODE stepY Multiply
LINK dir.y stepY.a
LINK step.result stepY.b
NODE nx Add
LINK pos.x nx.a
LINK stepX.result nx.b
NODE ny Add
LINK pos.y ny.a
LINK stepY.result ny.b

# ======================================================================================================
# ON START: find the body, give the entity its brain.
# ======================================================================================================
ENTRY seed OnStart
NODE seed OnStart
NODE findBody FindEntity name=pBody
NODE saveBody SetVar var=body
LINK seed.exec saveBody.exec
LINK findBody.entity saveBody.value
NODE brain AN_BrainSet asset=Demo/AI/Patron.ocst
LINK saveBody.then brain.exec
LINK ent.value brain.entity
NODE brainOk BoolToFloat
LINK brain.success brainOk.a
NODE Patron_brain_loaded Print
LINK brain.then Patron_brain_loaded.exec
LINK brainOk.result Patron_brain_loaded.value

# ======================================================================================================
# ON TICK: four small arms.
# ======================================================================================================
ENTRY tick OnTick
NODE tick OnTick
NODE fan Sequence
PIN fan exec in exec
PIN fan then0 out exec
PIN fan then1 out exec
PIN fan then2 out exec
PIN fan then3 out exec
LINK tick.exec fan.exec

# ---- arm 0: forget the "Arrived" latch as soon as there is no claim ---------------------------------------
NODE sentKeep And
LINK sentV.value sentKeep.a
LINK claim.has sentKeep.b
NODE sentSet SetVar var=sent
LINK fan.then0 sentSet.exec
LINK sentKeep.result sentSet.value

# ---- arm 1: walk ----------------------------------------------------------------------------------------
NODE moveGate Branch
LINK fan.then1 moveGate.exec
LINK moving.result moveGate.cond
NODE place SetLocalPosition
LINK moveGate.true place.exec
LINK ent.value place.entity
LINK nx.result place.x
LINK ny.result place.y
LINK pos.z place.z
NODE face LookAt
LINK place.then face.exec
LINK ent.value face.entity
LINK tx.result face.x
LINK ty.result face.y
LINK pos.z face.z

# ---- arm 2: animation follows the mode (0 idle, 1 walk, 2 sit) --------------------------------------------
NODE walkOrIdle Select
LINK moving.result walkOrIdle.cond
LINK oneF.value walkOrIdle.ifTrue
LINK zero.value walkOrIdle.ifFalse
NODE modeF Select
LINK occupied.result modeF.cond
LINK twoF.value modeF.ifTrue
LINK walkOrIdle.result modeF.ifFalse
NODE changed NotEqual
LINK modeF.result changed.a
LINK animV.value changed.b
NODE animGate Branch
LINK fan.then2 animGate.exec
LINK changed.result animGate.cond
NODE modeI FloatToInt
LINK modeF.result modeI.a
NODE modeSw SwitchInt
LINK animGate.true modeSw.exec
LINK modeI.result modeSw.selector

NODE playIdle PlayAnimation clip=Meshes/KenneyChar_idle.ocanim
PIN playIdle exec in exec
PIN playIdle entity in int
PIN playIdle loop in bool
PIN playIdle then out exec
PIN playIdle success out bool
LINK modeSw.case0 playIdle.exec
LINK bodyV.value playIdle.entity
LINK tTrue.value playIdle.loop
NODE playWalk PlayAnimation clip=Meshes/KenneyChar_walk.ocanim
PIN playWalk exec in exec
PIN playWalk entity in int
PIN playWalk loop in bool
PIN playWalk then out exec
PIN playWalk success out bool
LINK modeSw.case1 playWalk.exec
LINK bodyV.value playWalk.entity
LINK tTrue.value playWalk.loop
NODE playSit PlayAnimation clip=Meshes/KenneyChar_sit.ocanim
PIN playSit exec in exec
PIN playSit entity in int
PIN playSit loop in bool
PIN playSit then out exec
PIN playSit success out bool
LINK modeSw.case2 playSit.exec
LINK bodyV.value playSit.entity
LINK tTrue.value playSit.loop

NODE animSave SetVar var=anim
LINK playIdle.then animSave.exec
LINK playWalk.then animSave.exec
LINK playSit.then animSave.exec
LINK modeF.result animSave.value
NODE Patron_mode PrintInt
LINK animSave.then Patron_mode.exec
LINK modeI.result Patron_mode.value

# ---- arm 3: tell the brain once when we are standing at the claimed slot's entry point -----------------------
NODE inReach LessEqual
LINK dist.result inReach.a
LINK arriveR.value inReach.b
NODE arrivedNow And
LINK claimed.result arrivedNow.a
LINK inReach.result arrivedNow.b
NODE notSent Not
LINK sentKeep.result notSent.a
NODE needSend And
LINK arrivedNow.result needSend.a
LINK notSent.result needSend.b
NODE sendGate Branch
LINK fan.then3 sendGate.exec
LINK needSend.result sendGate.cond
NODE arrived AN_BrainSendEvent event=Arrived
LINK sendGate.true arrived.exec
LINK ent.value arrived.entity
NODE sentOn SetVar var=sent
LINK arrived.then sentOn.exec
LINK tTrue.value sentOn.value
NODE Patron_arrived PrintString text=Patron_arrived_at_slot_entry
LINK sentOn.then Patron_arrived.exec

# ======================================================================================================
# The slot's `USE event SatDown` lands here when the tree's UseSmartObject task starts the slot:
# put the body on the seat (slot x, y) facing the slot's yaw.
# ======================================================================================================
ENTRY satEvent SatDown
NODE satEvent CustomEvent
NODE snapPos SetLocalPosition
LINK satEvent.exec snapPos.exec
LINK ent.value snapPos.entity
LINK claim.x snapPos.x
LINK claim.y snapPos.y
LINK pos.z snapPos.z
NODE snapRot SetLocalRotation
LINK snapPos.then snapRot.exec
LINK ent.value snapRot.entity
LINK claim.yaw snapRot.yaw
NODE Patron_sat PrintString text=Patron_sat_down
LINK snapRot.then Patron_sat.exec
```

`Content/Demo/Scripts/AN_DemoEqsProbe.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# EQS PROBE: once a second runs Demo/AI/FindNearPlayer.oceqs with this actor as the querier and the player
# as the target, and parks a red ball (the COMP eqsMarker) on the best item. The editor's Show > AI overlay
# draws EVERY scored item of the run, so the ball is the winner and the overlay is the field it won.
# MUST BE PLACED AT 0 0 0 with no rotation: the marker is a child, and its position is set in world cm.
NAME DemoEqsProbe
DESCRIPTION Runs an environment query every second and shows the best item with a ball.

CLASS AN_DemoEqsProbe Actor

COMP eqsMarker Mesh mesh=Meshes/sphere.ocmesh material=Caldera_plastic scale=18,18,18

PARAM entity int
PARAM deltaTime float

VAR marker int 0
VAR clock float 0

NODE ent Param param=entity
NODE dt Param param=deltaTime
NODE markerV GetVar var=marker
NODE clockV GetVar var=clock
NODE zero ConstFloat value=0.0

ENTRY seed OnStart
NODE seed OnStart
NODE findMarker FindEntity name=eqsMarker
NODE saveMarker SetVar var=marker
LINK seed.exec saveMarker.exec
LINK findMarker.entity saveMarker.value

ENTRY tick OnTick
NODE tick OnTick
NODE clockAdd Add
LINK clockV.value clockAdd.a
LINK dt.value clockAdd.b
NODE period ConstFloat value=1.0
NODE due GreaterEqual
LINK clockAdd.result due.a
LINK period.value due.b
NODE clockPick Select
LINK due.result clockPick.cond
LINK zero.value clockPick.ifTrue
LINK clockAdd.result clockPick.ifFalse
NODE clockSet SetVar var=clock
LINK tick.exec clockSet.exec
LINK clockPick.result clockSet.value
NODE dueGate Branch
LINK clockSet.then dueGate.exec
LINK due.result dueGate.cond

NODE player GetPlayerPawn
NODE run AN_EqsRun query=Demo/AI/FindNearPlayer.oceqs
LINK dueGate.true run.exec
LINK ent.value run.querier
LINK player.entity run.target
NODE foundGate Branch
LINK run.then foundGate.exec
LINK run.found foundGate.cond
NODE place SetLocalPosition
LINK foundGate.true place.exec
LINK markerV.value place.entity
LINK run.x place.x
LINK run.y place.y
LINK run.z place.z
NODE eqs_item_count PrintInt
LINK place.then eqs_item_count.exec
LINK run.count eqs_item_count.value
NODE eqs_best_score Print
LINK eqs_item_count.then eqs_best_score.exec
LINK run.score eqs_best_score.value
```

`Content/Demo/Maps/Demo_AI.ocworld`:

```text
OCWORLD 1
# Demo_AI: a StateTree brain, a smart-object bench, an environment query. Nothing here needs a nav grid.
# The Patron (AN_DemoPatron) claims a bench seat, walks to it, sits for 7 s, wanders to an EQS-chosen spot,
# and repeats. The probe (AN_DemoEqsProbe, at 0 0 0) runs a second query every second against YOU.
# Turn on Show > AI in the editor to see the EQS items, the smart-object slot states and the state tree.
NAME Demo_AI
BUILD 0
ALGO 3
GAMEMODE AN_DemoRules
SPAWN -600 0 10 0
CAMERA -1300 -300 600 15 -22 900
SUN elev 55 azim 30 color 1 1 1 lux 100000
SKY model physical
FOG exp density 0.00002 color 0.7 0.78 0.88

PLACEG Meshes/cube.ocmesh 0 0 -10 0 0 0 3000 3000 10 Caldera_pavers

# Pillars: occluders for the probe query's `trace` test (static boxes, 80 x 80 x 300).
PLACEG Meshes/cube.ocmesh 700 400 150 0 0 0 40 40 150 Caldera_brick
PLACEG Meshes/cube.ocmesh 700 -400 150 0 0 0 40 40 150 Caldera_brick
PLACEG Meshes/cube.ocmesh -700 600 150 0 0 0 40 40 150 Caldera_brick
PLACEG Meshes/cube.ocmesh 300 -900 150 0 0 0 40 40 150 Caldera_brick

# Two benches at yaw 180, so they face -X, toward the plaza. Each has two seats.
PLACE none 1300 -500 0 180 0 0 1 class AN_DemoBench name BenchA
PLACE none 1300 500 0 180 0 0 1 class AN_DemoBench name BenchB

PLACE none 200 0 0 0 0 0 1 class AN_DemoPatron name Patron
PLACE none 0 0 0 0 0 0 1 class AN_DemoEqsProbe name EqsProbe
```

### 6.5 Demo_Physics

Shape casts, a breakable joint, CCD and a ragdoll, each a self-contained station. Bodies the graph creates are not entities and have no mesh, so
the graphs copy body positions onto COMP meshes every tick (`docs/AVER_NODE_NODES.md` "A body is not an entity"); there is no node that reads a
body's rotation, so the door mesh is aimed with `LookAt` from the hinge (G4). The cast "drawing" is a ghost mesh parked at the cast's stop point;
there is no debug-draw node (grep over the node tables). U19.

`Content/Demo/Scripts/AN_DemoCastRig.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# SHAPE CASTS, DRAWN. Every tick a 60 cm box is swept 700 cm in a direction that turns at 0.6 rad/s, and a
# 50 x 120 cm capsule is swept in the OPPOSITE direction. Where each sweep stops (first contact, or 700 cm
# when nothing is hit) a ghost is parked: a plastic cube for the box, a tall metal block for the capsule (the runtime has no cylinder mesh). Obstacles are
# static level boxes around the rig, so the ghosts jump in and out as the sweeps turn past them.
# Every 0.5 s the two travelled distances are printed. Place the rig at the middle of the obstacle ring.
# Shape-cast pins are (centre, half extents or radius/height, yaw pitch roll, direction, maxDist) and the
# filter pins (layerMask 0 = all, ignoreBody 0, includeSensors false) are left unwired on purpose.
NAME DemoCastRig
DESCRIPTION Sweeps a box and a capsule through the level and parks ghosts where they stop.

CLASS AN_DemoCastRig Actor

COMP boxGhost Mesh mesh=Meshes/cube.ocmesh material=Caldera_plastic scale=30,30,30
COMP capGhost Mesh mesh=Meshes/cube.ocmesh material=Caldera_metal scale=25,25,60

PARAM entity int
PARAM deltaTime float

VAR boxG int 0
VAR capG int 0
VAR homeX float 0
VAR homeY float 0
VAR homeZ float 0
VAR phi float 0
VAR clock float 0

NODE ent Param param=entity
NODE dt Param param=deltaTime
NODE boxGV GetVar var=boxG
NODE capGV GetVar var=capG
NODE zero ConstFloat value=0.0
NODE reach ConstFloat value=700.0

# ---- ON START: ghosts and home ------------------------------------------------------------------------
ENTRY seed OnStart
NODE seed OnStart
NODE findBox FindEntity name=boxGhost
NODE saveBox SetVar var=boxG
LINK seed.exec saveBox.exec
LINK findBox.entity saveBox.value
NODE findCap FindEntity name=capGhost
NODE saveCap SetVar var=capG
LINK saveBox.then saveCap.exec
LINK findCap.entity saveCap.value
NODE here GetFieldVec3 field=CLocal.position
LINK ent.value here.entity
NODE sx SetVar var=homeX
NODE sy SetVar var=homeY
NODE sz SetVar var=homeZ
LINK saveCap.then sx.exec
LINK here.x sx.value
LINK sx.then sy.exec
LINK here.y sy.value
LINK sy.then sz.exec
LINK here.z sz.value

# ---- ON TICK -----------------------------------------------------------------------------------------------
ENTRY tick OnTick
NODE tick OnTick
NODE turnRate ConstFloat value=0.6
NODE phiV GetVar var=phi
NODE phiStep Multiply
LINK dt.value phiStep.a
LINK turnRate.value phiStep.b
NODE phiNext Add
LINK phiV.value phiNext.a
LINK phiStep.result phiNext.b
NODE phiSet SetVar var=phi
LINK tick.exec phiSet.exec
LINK phiNext.result phiSet.value

NODE dirX Cos
LINK phiNext.result dirX.a
NODE dirY Sin
LINK phiNext.result dirY.a
NODE negDirX Negate
LINK dirX.result negDirX.a
NODE negDirY Negate
LINK dirY.result negDirY.a
NODE radToDeg ConstFloat value=57.29578
NODE yawDeg Multiply
LINK phiNext.result yawDeg.a
LINK radToDeg.value yawDeg.b

NODE hx GetVar var=homeX
NODE hy GetVar var=homeY
NODE hz GetVar var=homeZ
NODE lift ConstFloat value=60.0
NODE oz Add
LINK hz.value oz.a
LINK lift.value oz.b

# ---- the box sweep (half extents 30 x 30 x 30, turning with the sweep) ---------------------------------------
NODE half ConstFloat value=30.0
NODE boxCast BoxCast
LINK phiSet.then boxCast.exec
LINK hx.value boxCast.cx
LINK hy.value boxCast.cy
LINK oz.result boxCast.cz
LINK half.value boxCast.hx
LINK half.value boxCast.hy
LINK half.value boxCast.hz
LINK yawDeg.result boxCast.yaw
LINK dirX.result boxCast.dirX
LINK dirY.result boxCast.dirY
LINK reach.value boxCast.maxDist
NODE boxDist Select
LINK boxCast.hit boxDist.cond
LINK boxCast.distance boxDist.ifTrue
LINK reach.value boxDist.ifFalse
NODE boxOffX Multiply
LINK dirX.result boxOffX.a
LINK boxDist.result boxOffX.b
NODE boxOffY Multiply
LINK dirY.result boxOffY.a
LINK boxDist.result boxOffY.b
NODE boxGX Add
LINK hx.value boxGX.a
LINK boxOffX.result boxGX.b
NODE boxGY Add
LINK hy.value boxGY.a
LINK boxOffY.result boxGY.b
NODE boxPlace SetLocalPosition
LINK boxCast.then boxPlace.exec
LINK boxGV.value boxPlace.entity
LINK boxGX.result boxPlace.x
LINK boxGY.result boxPlace.y
LINK oz.result boxPlace.z
NODE boxTurn SetLocalRotation
LINK boxPlace.then boxTurn.exec
LINK boxGV.value boxTurn.entity
LINK yawDeg.result boxTurn.yaw

# ---- the capsule sweep, the other way (radius 25, total height 120) --------------------------------------------
NODE capRadius ConstFloat value=25.0
NODE capHeight ConstFloat value=120.0
NODE capCast CapsuleCast
LINK boxTurn.then capCast.exec
LINK hx.value capCast.cx
LINK hy.value capCast.cy
LINK oz.result capCast.cz
LINK capRadius.value capCast.radius
LINK capHeight.value capCast.height
LINK negDirX.result capCast.dirX
LINK negDirY.result capCast.dirY
LINK reach.value capCast.maxDist
NODE capDist Select
LINK capCast.hit capDist.cond
LINK capCast.distance capDist.ifTrue
LINK reach.value capDist.ifFalse
NODE capOffX Multiply
LINK negDirX.result capOffX.a
LINK capDist.result capOffX.b
NODE capOffY Multiply
LINK negDirY.result capOffY.a
LINK capDist.result capOffY.b
NODE capGX Add
LINK hx.value capGX.a
LINK capOffX.result capGX.b
NODE capGY Add
LINK hy.value capGY.a
LINK capOffY.result capGY.b
NODE capPlace SetLocalPosition
LINK capCast.then capPlace.exec
LINK capGV.value capPlace.entity
LINK capGX.result capPlace.x
LINK capGY.result capPlace.y
LINK oz.result capPlace.z

# ---- every 0.5 s print both distances ---------------------------------------------------------------------------
NODE clockV GetVar var=clock
NODE clockAdd Add
LINK clockV.value clockAdd.a
LINK dt.value clockAdd.b
NODE period ConstFloat value=0.5
NODE due GreaterEqual
LINK clockAdd.result due.a
LINK period.value due.b
NODE clockPick Select
LINK due.result clockPick.cond
LINK zero.value clockPick.ifTrue
LINK clockAdd.result clockPick.ifFalse
NODE clockSet SetVar var=clock
LINK capPlace.then clockSet.exec
LINK clockPick.result clockSet.value
NODE printGate Branch
LINK clockSet.then printGate.exec
LINK due.result printGate.cond
NODE box_cast_distance Print
LINK printGate.true box_cast_distance.exec
LINK boxDist.result box_cast_distance.value
NODE capsule_cast_distance Print
LINK box_cast_distance.then capsule_cast_distance.exec
LINK capDist.result capsule_cast_distance.value
```

`Content/Demo/Scripts/AN_DemoHinge.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# A BREAKABLE HINGE. A static post and a 20 kg door are physics bodies the graph builds at OnStart, joined by
# a vertical hinge (limits +-1.5 rad) that is armed to break at 6000 N. The door and post you SEE are COMP
# meshes; the graph copies the door body's position onto its mesh every tick and aims the mesh with LookAt
# (there is no body-rotation read node, so the mesh long axis, local X, is pointed from the door centre
# away from the hinge). Once the hinge breaks the door flies free and the mesh just follows the position.
#   B  gentle push (2500 kg*cm/s)    V  hard hit (60000 kg*cm/s)    J  print the hinge angle (radians)
# Place the actor on flat ground; everything is built relative to its position. Hinge-pivot = home + (0, 10, 100).
NAME DemoHinge
DESCRIPTION Static post + dynamic door on a hinge joint with a break threshold; keys push the door.

CLASS AN_DemoHinge Actor

COMP postMesh Mesh mesh=Meshes/cube.ocmesh material=Caldera_metal pos=0,0,100 scale=10,10,100
COMP doorMesh Mesh mesh=Meshes/cube.ocmesh material=Caldera_wood pos=0,70,100 scale=60,4,90

PARAM entity int

VAR door int 0
VAR joint int 0
VAR doorE int 0
VAR homeX float 0
VAR homeY float 0
VAR homeZ float 0
VAR broken bool false

NODE ent Param param=entity
NODE doorV GetVar var=door
NODE jointV GetVar var=joint
NODE doorEV GetVar var=doorE
NODE brokenV GetVar var=broken
NODE hx GetVar var=homeX
NODE hy GetVar var=homeY
NODE hz GetVar var=homeZ
NODE zero ConstFloat value=0.0
NODE oneF ConstFloat value=1.0
NODE tTrue ConstBool value=true

# pivot = home + (0, 10, 100): the post's +Y face at mid height.
NODE ten ConstFloat value=10.0
NODE hundred ConstFloat value=100.0
NODE pivotY Add
LINK hy.value pivotY.a
LINK ten.value pivotY.b
NODE pivotZ Add
LINK hz.value pivotZ.a
LINK hundred.value pivotZ.b

# ======================================================================================================
# ON START: remember home, then build post, door, hinge and the break threshold.
# ======================================================================================================
ENTRY seed OnStart
NODE seed OnStart
NODE here GetFieldVec3 field=CLocal.position
LINK ent.value here.entity
NODE sx SetVar var=homeX
NODE sy SetVar var=homeY
NODE sz SetVar var=homeZ
LINK seed.exec sx.exec
LINK here.x sx.value
LINK sx.then sy.exec
LINK here.y sy.value
LINK sy.then sz.exec
LINK here.z sz.value
NODE findDoor FindEntity name=doorMesh
NODE saveDoorE SetVar var=doorE
LINK sz.then saveDoorE.exec
LINK findDoor.entity saveDoorE.value

# post: static box, centre (home.x, home.y, home.z+100), half extents 10 x 10 x 100.
NODE postHalfXY ConstFloat value=10.0
NODE postHalfZ ConstFloat value=100.0
NODE post AddStaticBox
LINK saveDoorE.then post.exec
LINK hx.value post.cx
LINK hy.value post.cy
LINK pivotZ.result post.cz
LINK postHalfXY.value post.hx
LINK postHalfXY.value post.hy
LINK postHalfZ.value post.hz

# door: dynamic box, centre (home.x, home.y+70, home.z+100), half extents 4 x 60 x 90, 20 kg.
NODE seventy ConstFloat value=70.0
NODE doorCY Add
LINK hy.value doorCY.a
LINK seventy.value doorCY.b
NODE doorHalfX ConstFloat value=4.0
NODE doorHalfY ConstFloat value=60.0
NODE doorHalfZ ConstFloat value=90.0
NODE doorMass ConstFloat value=20.0
NODE doorBox AddDynamicBox
LINK post.then doorBox.exec
LINK hx.value doorBox.cx
LINK doorCY.result doorBox.cy
LINK pivotZ.result doorBox.cz
LINK doorHalfX.value doorBox.hx
LINK doorHalfY.value doorBox.hy
LINK doorHalfZ.value doorBox.hz
LINK doorMass.value doorBox.mass
NODE saveDoor SetVar var=door
LINK doorBox.then saveDoor.exec
LINK doorBox.body saveDoor.value

# hinge: pivot at the post face, axis +Z, zero-angle reference +X, limits -1.5 .. +1.5 rad.
NODE axisX ConstFloat value=1.0
NODE limitLo ConstFloat value=-1.5
NODE limitHi ConstFloat value=1.5
NODE hinge JointHinge
LINK saveDoor.then hinge.exec
LINK post.body hinge.bodyA
LINK doorBox.body hinge.bodyB
LINK hx.value hinge.px
LINK pivotY.result hinge.py
LINK pivotZ.result hinge.pz
LINK zero.value hinge.hx
LINK zero.value hinge.hy
LINK oneF.value hinge.hz
LINK axisX.value hinge.nx
LINK zero.value hinge.ny
LINK zero.value hinge.nz
LINK limitLo.value hinge.minAngleRad
LINK limitHi.value hinge.maxAngleRad
NODE saveJoint SetVar var=joint
LINK hinge.then saveJoint.exec
LINK hinge.joint saveJoint.value

# breakable: 6000 N force threshold (SI units), torque threshold 0 = never check the torque.
NODE breakForce ConstFloat value=6000.0
NODE arm SetJointBreakable
LINK saveJoint.then arm.exec
LINK hinge.joint arm.joint
LINK breakForce.value arm.maxForceN
LINK zero.value arm.maxTorqueNm
NODE hinge_armed PrintString text=Hinge_armed_6000N
LINK arm.then hinge_armed.exec

# ======================================================================================================
# ON TICK: copy the body onto the mesh, aim it, detect the break, read the keys.
# ======================================================================================================
ENTRY tick OnTick
NODE tick OnTick
NODE doorPos GetBodyPosition
LINK doorV.value doorPos.body
NODE localX Subtract
LINK doorPos.x localX.a
LINK hx.value localX.b
NODE localY Subtract
LINK doorPos.y localY.a
LINK hy.value localY.b
NODE localZ Subtract
LINK doorPos.z localZ.a
LINK hz.value localZ.b
NODE follow SetLocalPosition
LINK tick.exec follow.exec
LINK doorEV.value follow.entity
LINK localX.result follow.x
LINK localY.result follow.y
LINK localZ.result follow.z

# far end of the door = centre + (centre - pivot): aim local +X at it while still attached.
NODE farX Subtract
NODE twiceX Multiply
NODE two ConstFloat value=2.0
LINK doorPos.x twiceX.a
LINK two.value twiceX.b
LINK twiceX.result farX.a
LINK hx.value farX.b
NODE farY Subtract
NODE twiceY Multiply
LINK doorPos.y twiceY.a
LINK two.value twiceY.b
LINK twiceY.result farY.a
LINK pivotY.result farY.b
NODE attachedGate Branch
LINK follow.then attachedGate.exec
NODE attached Not
LINK brokenV.value attached.a
LINK attached.result attachedGate.cond
NODE aim LookAt
LINK attachedGate.true aim.exec
LINK doorEV.value aim.entity
LINK farX.result aim.x
LINK farY.result aim.y
LINK doorPos.z aim.z

# broken <=> the door centre is more than 90 cm from the pivot (attached it is exactly 60).
NODE pivotDist VecDistance
LINK doorPos.x pivotDist.ax
LINK doorPos.y pivotDist.ay
LINK doorPos.z pivotDist.az
LINK hx.value pivotDist.bx
LINK pivotY.result pivotDist.by
LINK pivotZ.result pivotDist.bz
NODE ninety ConstFloat value=90.0
NODE farAway Greater
LINK pivotDist.result farAway.a
LINK ninety.value farAway.b
NODE justBroke And
LINK farAway.result justBroke.a
LINK attached.result justBroke.b
NODE breakGate Branch
LINK attachedGate.false breakGate.exec
LINK aim.then breakGate.exec
LINK justBroke.result breakGate.cond
NODE markBroken SetVar var=broken
LINK breakGate.true markBroken.exec
LINK tTrue.value markBroken.value
NODE hinge_BROKEN PrintString text=Hinge_BROKEN
LINK markBroken.then hinge_BROKEN.exec

# keys: B = soft push (Key 1), V = hard hit (Key 21), J = print the hinge angle (Key 9). All push along +X.
NODE keys Sequence
PIN keys exec in exec
PIN keys then0 out exec
PIN keys then1 out exec
PIN keys then2 out exec
LINK breakGate.false keys.exec
LINK hinge_BROKEN.then keys.exec

NODE bKey ConstInt value=1
NODE bPressed InputKeyPressed
LINK bKey.value bPressed.key
NODE bGate Branch
LINK keys.then0 bGate.exec
LINK bPressed.triggered bGate.cond
NODE softPush ConstFloat value=2500.0
NODE soft AddImpulse
LINK bGate.true soft.exec
LINK doorV.value soft.body
LINK softPush.value soft.x
NODE hinge_soft_push PrintString text=Soft_push_2500
LINK soft.then hinge_soft_push.exec

NODE vKey ConstInt value=21
NODE vPressed InputKeyPressed
LINK vKey.value vPressed.key
NODE vGate Branch
LINK keys.then1 vGate.exec
LINK vPressed.triggered vGate.cond
NODE hardPush ConstFloat value=60000.0
NODE hard AddImpulse
LINK vGate.true hard.exec
LINK doorV.value hard.body
LINK hardPush.value hard.x
NODE hinge_hard_hit PrintString text=Hard_hit_60000
LINK hard.then hinge_hard_hit.exec

NODE jKey ConstInt value=9
NODE jPressed InputKeyPressed
LINK jKey.value jPressed.key
NODE jGate Branch
LINK keys.then2 jGate.exec
LINK jPressed.triggered jGate.cond
NODE angle GetJointValue
LINK jointV.value angle.joint
NODE hinge_angle_rad Print
LINK jGate.true hinge_angle_rad.exec
LINK angle.value hinge_angle_rad.value
```

`Content/Demo/Scripts/AN_DemoCcd.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# CCD VS NO CCD. Two 8 cm balls with gravity switched off are fired at 60 m/s (100 cm per 1/60 s physics step)
# at a 2 cm thick static wall that the level places 900 cm in front of the launcher (x = home.x + 900).
# Ball A (red) has continuous collision OFF and is expected to tunnel through the wall; ball B (green) has
# SetCcd on and is expected to stop against it. One second after each launch the graph prints how far each
# ball is past the wall: a number near +5000 means it went through, about -9 means it stopped.
#   C  fire again (the old balls are destroyed first)
# The launcher is the actor's own position; the wall is the level's `Wall` placement 900 cm along +X.
NAME DemoCcd
DESCRIPTION Fast projectile with and without continuous collision against a thin wall.

CLASS AN_DemoCcd Actor

COMP ballAMesh Mesh mesh=Meshes/sphere.ocmesh material=Caldera_plastic scale=8,8,8
COMP ballBMesh Mesh mesh=Meshes/sphere.ocmesh material=Caldera_metal scale=8,8,8

PARAM entity int
PARAM deltaTime float

VAR bodyA int 0
VAR bodyB int 0
VAR meshA int 0
VAR meshB int 0
VAR homeX float 0
VAR homeY float 0
VAR homeZ float 0
VAR age float 0
VAR reported bool true

NODE ent Param param=entity
NODE dt Param param=deltaTime
NODE bodyAV GetVar var=bodyA
NODE bodyBV GetVar var=bodyB
NODE meshAV GetVar var=meshA
NODE meshBV GetVar var=meshB
NODE hx GetVar var=homeX
NODE hy GetVar var=homeY
NODE hz GetVar var=homeZ
NODE zero ConstFloat value=0.0
NODE tTrue ConstBool value=true
NODE tFalse ConstBool value=false

# ======================================================================================================
# ON START: remember home and the two meshes, then fire.
# ======================================================================================================
ENTRY seed OnStart
NODE seed OnStart
NODE here GetFieldVec3 field=CLocal.position
LINK ent.value here.entity
NODE sx SetVar var=homeX
NODE sy SetVar var=homeY
NODE sz SetVar var=homeZ
LINK seed.exec sx.exec
LINK here.x sx.value
LINK sx.then sy.exec
LINK here.y sy.value
LINK sy.then sz.exec
LINK here.z sz.value
NODE findA FindEntity name=ballAMesh
NODE saveMeshA SetVar var=meshA
LINK sz.then saveMeshA.exec
LINK findA.entity saveMeshA.value
NODE findB FindEntity name=ballBMesh
NODE saveMeshB SetVar var=meshB
LINK saveMeshA.then saveMeshB.exec
LINK findB.entity saveMeshB.value

# ======================================================================================================
# FIRE (shared by OnStart and the C key). Destroying handle 0 is a harmless no-op the first time.
# ======================================================================================================
NODE killA DestroyBody
LINK saveMeshB.then killA.exec
LINK bodyAV.value killA.body
NODE killB DestroyBody
LINK killA.then killB.exec
LINK bodyBV.value killB.body

NODE ballRadius ConstFloat value=8.0
NODE ballMass ConstFloat value=1.0
NODE offY ConstFloat value=100.0
NODE ballAY Subtract
LINK hy.value ballAY.a
LINK offY.value ballAY.b
NODE ballBY Add
LINK hy.value ballBY.a
LINK offY.value ballBY.b
NODE lift ConstFloat value=150.0
NODE ballZ Add
LINK hz.value ballZ.a
LINK lift.value ballZ.b

NODE makeA AddDynamicSphere
LINK killB.then makeA.exec
LINK hx.value makeA.cx
LINK ballAY.result makeA.cy
LINK ballZ.result makeA.cz
LINK ballRadius.value makeA.radius
LINK ballMass.value makeA.mass
NODE saveA SetVar var=bodyA
LINK makeA.then saveA.exec
LINK makeA.body saveA.value
NODE makeB AddDynamicSphere
LINK saveA.then makeB.exec
LINK hx.value makeB.cx
LINK ballBY.result makeB.cy
LINK ballZ.result makeB.cz
LINK ballRadius.value makeB.radius
LINK ballMass.value makeB.mass
NODE saveB SetVar var=bodyB
LINK makeB.then saveB.exec
LINK makeB.body saveB.value

NODE gravA SetBodyGravityFactor
LINK saveB.then gravA.exec
LINK makeA.body gravA.body
LINK zero.value gravA.factor
NODE gravB SetBodyGravityFactor
LINK gravA.then gravB.exec
LINK makeB.body gravB.body
LINK zero.value gravB.factor
NODE ccdA SetCcd
LINK gravB.then ccdA.exec
LINK makeA.body ccdA.body
LINK tFalse.value ccdA.enabled
NODE ccdB SetCcd
LINK ccdA.then ccdB.exec
LINK makeB.body ccdB.body
LINK tTrue.value ccdB.enabled

NODE launchSpeed ConstFloat value=6000.0
NODE velA SetBodyVelocity
LINK ccdB.then velA.exec
LINK makeA.body velA.body
LINK launchSpeed.value velA.x
NODE velB SetBodyVelocity
LINK velA.then velB.exec
LINK makeB.body velB.body
LINK launchSpeed.value velB.x
NODE ageReset SetVar var=age
LINK velB.then ageReset.exec
LINK zero.value ageReset.value
NODE reportedOff SetVar var=reported
LINK ageReset.then reportedOff.exec
LINK tFalse.value reportedOff.value
NODE ccd_fired PrintString text=Fired_A_noCCD_B_CCD
LINK reportedOff.then ccd_fired.exec

# ======================================================================================================
# ON TICK: mirror the bodies onto the meshes, age the shot, report once at one second, read C.
# ======================================================================================================
ENTRY tick OnTick
NODE tick OnTick
NODE posA GetBodyPosition
LINK bodyAV.value posA.body
NODE posB GetBodyPosition
LINK bodyBV.value posB.body
NODE localAX Subtract
LINK posA.x localAX.a
LINK hx.value localAX.b
NODE localAY Subtract
LINK posA.y localAY.a
LINK hy.value localAY.b
NODE localAZ Subtract
LINK posA.z localAZ.a
LINK hz.value localAZ.b
NODE followA SetLocalPosition
LINK tick.exec followA.exec
LINK meshAV.value followA.entity
LINK localAX.result followA.x
LINK localAY.result followA.y
LINK localAZ.result followA.z
NODE localBX Subtract
LINK posB.x localBX.a
LINK hx.value localBX.b
NODE localBY Subtract
LINK posB.y localBY.a
LINK hy.value localBY.b
NODE localBZ Subtract
LINK posB.z localBZ.a
LINK hz.value localBZ.b
NODE followB SetLocalPosition
LINK followA.then followB.exec
LINK meshBV.value followB.entity
LINK localBX.result followB.x
LINK localBY.result followB.y
LINK localBZ.result followB.z

NODE ageV GetVar var=age
NODE ageNext Add
LINK ageV.value ageNext.a
LINK dt.value ageNext.b
NODE ageSet SetVar var=age
LINK followB.then ageSet.exec
LINK ageNext.result ageSet.value
NODE reportedV GetVar var=reported
NODE notReported Not
LINK reportedV.value notReported.a
NODE oneSecond ConstFloat value=1.0
NODE oldEnough GreaterEqual
LINK ageNext.result oldEnough.a
LINK oneSecond.value oldEnough.b
NODE reportDue And
LINK notReported.result reportDue.a
LINK oldEnough.result reportDue.b
NODE reportGate Branch
LINK ageSet.then reportGate.exec
LINK reportDue.result reportGate.cond

# x past the wall = ball x - (home.x + 900), measured from the physics body.
NODE wallDist ConstFloat value=900.0
NODE wallX Add
LINK hx.value wallX.a
LINK wallDist.value wallX.b
NODE pastA Subtract
LINK posA.x pastA.a
LINK wallX.result pastA.b
NODE pastB Subtract
LINK posB.x pastB.a
LINK wallX.result pastB.b
NODE ccd_OFF_past_wall_cm Print
LINK reportGate.true ccd_OFF_past_wall_cm.exec
LINK pastA.result ccd_OFF_past_wall_cm.value
NODE ccd_ON_past_wall_cm Print
LINK ccd_OFF_past_wall_cm.then ccd_ON_past_wall_cm.exec
LINK pastB.result ccd_ON_past_wall_cm.value
NODE reportedOn SetVar var=reported
LINK ccd_ON_past_wall_cm.then reportedOn.exec
LINK tTrue.value reportedOn.value

# C (Key 2): fire again.
NODE cKey ConstInt value=2
NODE cPressed InputKeyPressed
LINK cKey.value cPressed.key
NODE cGate Branch
LINK reportGate.false cGate.exec
LINK reportedOn.then cGate.exec
LINK cPressed.triggered cGate.cond
LINK cGate.true killA.exec
```

The ragdoll station reuses `AN_DemoMannequin` (section 6.2): placed once in the level, key `Z`.

`Content/Demo/Maps/Demo_Physics.ocworld`:

```text
OCWORLD 1
# Demo_Physics: box and capsule casts drawn, a breakable hinge, CCD against no CCD, a ragdoll.
# Stations (all relative to the origin): cast rig at 0 0 0; hinge door at 1500 -300; CCD launcher at -1500 -500;
# ragdoll dummy circling 600 1200. The player starts south of the rig, 18 m away.
NAME Demo_Physics
BUILD 0
ALGO 3
GAMEMODE AN_DemoRules
SPAWN 0 -1800 10 90
CAMERA -300 -2200 500 90 -15 900
SUN elev 55 azim 40 color 1 1 1 lux 100000
SKY model physical
FOG exp density 0.00002 color 0.7 0.78 0.88

PLACEG Meshes/cube.ocmesh 0 0 -10 0 0 0 4000 4000 10 Caldera_concrete

# ---- station 1: the cast rig and a ring of obstacles for it to find (cube is +-1: scale = half extents) ----------------
PLACE none 0 0 0 0 0 0 1 class AN_DemoCastRig
PLACEG Meshes/cube.ocmesh 350 0 60 0 0 0 40 120 60 Caldera_brick
PLACEG Meshes/cube.ocmesh 0 420 60 0 0 0 100 40 60 Caldera_brick
PLACEG Meshes/cube.ocmesh -500 100 60 0 0 0 50 50 60 Caldera_brick
PLACEG Meshes/cube.ocmesh 0 -600 60 0 0 0 80 40 60 Caldera_brick
PLACEG Meshes/cube.ocmesh 300 300 60 0 0 0 40 40 60 Caldera_brick
PLACEG Meshes/cube.ocmesh -300 -350 60 0 0 0 60 60 60 Caldera_brick
PLACEG Meshes/cube.ocmesh 600 -200 60 0 0 0 40 40 60 Caldera_brick
PLACEG Meshes/cube.ocmesh -200 520 60 0 0 0 70 30 60 Caldera_brick

# ---- station 2: the breakable hinge door --------------------------------------------------------------------------------
PLACE none 1500 -300 0 0 0 0 1 class AN_DemoHinge

# ---- station 3: CCD. The launcher actor sits at x = -1500; the wall is 900 cm ahead of it, 2 cm thick --------------------
PLACE none -1500 -500 0 0 0 0 1 class AN_DemoCcd
PLACEG Meshes/cube.ocmesh -600 -500 150 0 0 0 1 300 150 Caldera_metal name Wall

# ---- station 4: the ragdoll dummy (Z toggles its ragdoll) ----------------------------------------------------------------
PLACE none 600 1200 0 0 0 0 1 class AN_DemoMannequin
```

### 6.6 Demo_Sequencer

One level sequence `Show` (autoplay) with a camera, plus a standalone `.ocseq` it plays as a sub-sequence. All tracks bind by name, so the same records
also work in a `.ocseq` (an `.ocseq` track that needs an actor and has no `bind` is dropped on read, `OcSeq.cpp:254-268`). Time budget: 0 to 24 s. U20.

| t (s) | What happens |
|---|---|
| 0 | `Show_Started` event; camera `Wide` live; Prop at (-500,0,100) |
| 2 to 5 | chime (non-positional) |
| 4 | lamp swells to 60,000 cd; sub-sequence `Pulse` starts (3 pulses of the small cube, 3 `Pulse` events, until t=10) |
| 6 | Prop has risen to 320 cm and yawed 90 degrees |
| 8 | Door vanishes; `Door_Open` event (argument 0.5); positional chime from the door |
| 12 | cut to `Crane` (blend 1.5 s): the camera rides the crane placement; Prop at (500,0,320), scale 150 |
| 14 | Door returns; `Door_Close` event |
| 20 | cut back to `Wide` (blend 0.5 s) |
| 24 | `Show_Finished` event |

`Content/Demo/Maps/Demo_Sequencer.ocworld`:

```text
OCWORLD 1
# Demo_Sequencer: a low evening sun so the lamp's property track is visible; one autoplaying level sequence "Show" with transform, property, visibility, event, audio,
# camera, camera-cut and sub-sequence tracks. Every track binds its actor BY NAME (bind), so the same records
# also work in a standalone .ocseq. Press Play and watch; P restarts it, O pauses, K prints the clock.
# In the editor's Animate mode (Sequencer window) the same sequence scrubs; events and audio only run in Play.
NAME Demo_Sequencer
BUILD 0
ALGO 3
GAMEMODE AN_DemoRules
SPAWN -2200 0 10 0
CAMERA -1800 0 400 0 -10 900
SUN elev 6 azim 35 color 1 0.85 0.7 lux 6000
SKY model physical
FOG exp density 0.00002 color 0.7 0.78 0.88

# ---- the stage ----------------------------------------------------------------------------------------------------------
PLACEG Meshes/cube.ocmesh 0 0 -10 0 0 0 3000 3000 10 Caldera_pavers
PLACE Meshes/cube.ocmesh -500 0 100 0 0 0 100 Caldera_plastic name Prop
PLACEG Meshes/cube.ocmesh 500 0 100 0 0 0 20 150 100 Caldera_brick name Door
PLACE Meshes/cube.ocmesh 0 -600 40 0 0 0 40 Caldera_plastic name Beacon
PLACE Meshes/cube.ocmesh -1200 -1500 500 0 -10 0 20 Caldera_metal name CraneArm
LIGHT name Lamp kind point pos 0 0 300 colour 1 0.8 0.5 intensity 4000 range 2500 radius 3
PLACE none 0 -2000 0 0 0 0 1 class AN_DemoSeqListener name Listener

# ---- the sequence --------------------------------------------------------------------------------------------------------
# Transform keys are: t interp x y z yaw pitch roll sx sy sz (scale values are absolute: Prop is placed at 100).
SEQUENCE name Show length 24 fps 30 loop 0 autoplay 1 camera 1
  SEQTRACK transform name PropMove bind Prop
    SEQKEY 0 smooth -500 0 100 0 0 0 100 100 100
    SEQKEY 6 smooth -500 0 320 90 0 0 100 100 100
    SEQKEY 12 smooth 500 0 320 180 0 0 150 150 150
    SEQKEY 18 smooth 0 -300 100 360 0 0 100 100 100
  SEQTRACK transform name CraneMove bind CraneArm
    SEQKEY 0 smooth -1200 -1500 500 0 -10 0 20 20 20
    SEQKEY 12 smooth -400 -1500 700 60 -10 0 20 20 20
    SEQKEY 24 smooth 600 -1500 500 120 -10 0 20 20 20
  SEQTRACK property name LampIntensity bind Lamp prop CLight.intensityLux
    SEQKEY 0 linear 4000
    SEQKEY 4 cubic 60000
    SEQKEY 8 smooth 4000
    SEQKEY 16 smooth 40000
    SEQKEY 22 linear 4000
  SEQTRACK property name LampColour bind Lamp prop CLight.colour
    SEQKEY 0 linear 1 0.8 0.5
    SEQKEY 12 linear 0.4 0.6 1
    SEQKEY 22 linear 1 0.8 0.5
  SEQTRACK visibility name DoorVisible bind Door
    SEQKEY 0 step 1 1
    SEQKEY 8 step 0 1
    SEQKEY 14 step 1 1
  SEQTRACK event name Events
    SEQKEY 8 step 0 1 0.5 text Door_Open arg lever_a
    SEQKEY 14 step 0 0 text Door_Close
  SEQTRACK audio name Chime
    SEQKEY 2 step 0.1 0.3 0 0 0 0 0.8 1 dur 3 text Demo/Audio/chime.wav
  SEQTRACK audio name DoorChime bind Door
    SEQKEY 8 step 0 0.2 0 1 300 3000 1 1.3 dur 2 text Demo/Audio/chime.wav
  SEQTRACK camera name Wide
    SEQKEY 0 smooth -1800 0 400 0 -10
    SEQKEY 12 smooth -1400 900 420 -25 -12
  SEQTRACK camera name Crane bind CraneArm
  SEQTRACK cut name Cuts
    SEQKEY 0 step 0 text Wide
    SEQKEY 12 step 1.5 text Crane
    SEQKEY 20 step 0.5 text Wide
  SEQTRACK sub name PulseSub
    SEQKEY 4 step 0 0 0 dur 6 text Demo/Seq/DemoPulse.ocseq%23Pulse
ENDSEQUENCE
```

`Content/Demo/Seq/DemoPulse.ocseq`:

```text
OCSEQ 1
# The sub-sequence the Show sequence plays from t = 4 s for 6 s (a `sub` track naming
# Demo/Seq/DemoPulse.ocseq%23Pulse; %23 is '#', which a level file would otherwise read as a comment).
# Three pulses of the Beacon cube and a "Pulse" event on the bus with each. Actors are bound by NAME only.
SEQUENCE name Pulse length 6 fps 30 loop 0 autoplay 0 camera 0
  SEQTRACK transform name BeaconPulse bind Beacon
    SEQKEY 0 smooth 0 -600 40 0 0 0 40 40 40
    SEQKEY 0.5 smooth 0 -600 40 0 0 0 70 70 70
    SEQKEY 1 smooth 0 -600 40 0 0 0 40 40 40
    SEQKEY 2.5 smooth 0 -600 40 0 0 0 70 70 70
    SEQKEY 3 smooth 0 -600 40 0 0 0 40 40 40
    SEQKEY 4.5 smooth 0 -600 40 0 0 0 70 70 70
    SEQKEY 5 smooth 0 -600 40 0 0 0 40 40 40
  SEQTRACK event name PulseEvents
    SEQKEY 0.5 step 0 0 text Pulse
    SEQKEY 2.5 step 0 0 text Pulse
    SEQKEY 4.5 step 0 0 text Pulse
ENDSEQUENCE
```

`Content/Demo/Scripts/AN_DemoSeqListener.ocgraph`:

```text
OCGRAPH 1
DOMAIN gameplay
# SEQUENCER LISTENER AND REMOTE. The level sequence "Show" (autoplay) raises Show_Started and Show_Finished
# on the event bus by itself, and its event track raises Door_Open (one numeric argument, 0.5) and Pulse.
# This graph prints them, and gives you the sequencer script nodes on keys:
#   P  restart the sequence from 0     O  pause / stop it     K  print time, length and rate
# Event entries run with deltaTime 0 and need no PARAM.
NAME DemoSeqListener
DESCRIPTION Prints the events the Show sequence raises and offers play/pause/state on keys.

CLASS AN_DemoSeqListener Actor

NODE idx0 ConstInt value=0
NODE zero ConstFloat value=0.0

ENTRY startedEvt Show_Started
NODE startedEvt AN_OnEvent
NODE Show_Started_event PrintString text=Sequence_Show_started
LINK startedEvt.exec Show_Started_event.exec

ENTRY finishedEvt Show_Finished
NODE finishedEvt AN_OnEvent
NODE Show_Finished_event PrintString text=Sequence_Show_finished
LINK finishedEvt.exec Show_Finished_event.exec

ENTRY doorEvt Door_Open
NODE doorEvt AN_OnEvent
NODE doorPay AN_EventPayload
LINK doorEvt.exec doorPay.exec
LINK idx0.value doorPay.index
NODE Door_Open_event_arg0 Print
LINK doorPay.then Door_Open_event_arg0.exec
LINK doorPay.f Door_Open_event_arg0.value

ENTRY pulseEvt Pulse
NODE pulseEvt AN_OnEvent
NODE Pulse_event PrintString text=Pulse_event_from_the_sub_sequence
LINK pulseEvt.exec Pulse_event.exec

ENTRY tick OnTick
NODE tick OnTick
NODE fan Sequence
PIN fan exec in exec
PIN fan then0 out exec
PIN fan then1 out exec
PIN fan then2 out exec
LINK tick.exec fan.exec

# P (Key 15): stop (back to 0, authored values restored) then play from 0.
NODE pKey ConstInt value=15
NODE pPressed InputKeyPressed
LINK pKey.value pPressed.key
NODE pGate Branch
LINK fan.then0 pGate.exec
LINK pPressed.triggered pGate.cond
NODE stopShow StopSequence sequence=Show
LINK pGate.true stopShow.exec
NODE playShow PlaySequence sequence=Show
LINK stopShow.then playShow.exec
LINK zero.value playShow.start

# O (Key 14): pause.
NODE oKey ConstInt value=14
NODE oPressed InputKeyPressed
LINK oKey.value oPressed.key
NODE oGate Branch
LINK fan.then1 oGate.exec
LINK oPressed.triggered oGate.cond
NODE pauseShow PauseSequence sequence=Show
LINK oGate.true pauseShow.exec

# K (Key 10): print the sequence clock.
NODE kKey ConstInt value=10
NODE kPressed InputKeyPressed
LINK kKey.value kPressed.key
NODE kGate Branch
LINK fan.then2 kGate.exec
LINK kPressed.triggered kGate.cond
NODE state GetSequenceState sequence=Show
NODE Show_time_s Print
LINK kGate.true Show_time_s.exec
LINK state.time Show_time_s.value
NODE Show_length_s Print
LINK Show_time_s.then Show_length_s.exec
LINK state.length Show_length_s.value
```

### 6.7 The chime generator

`Tools/make_demo_chime.py` (Python standard library only; no audio file exists anywhere in the repository or in PTTest):

```python
"""Writes Content/Demo/Audio/chime.wav for the Demo_Sequencer level (standard library only).

A 1.2 s mono 16-bit 22050 Hz tone: 660 Hz plus a quieter 990 Hz, decaying. Run from anywhere:
    python Tools/make_demo_chime.py
"""
import math
import struct
import wave
from pathlib import Path

RATE = 22050
SECONDS = 1.2

out = Path(__file__).resolve().parent.parent / "Content" / "Demo" / "Audio" / "chime.wav"
out.parent.mkdir(parents=True, exist_ok=True)

frames = bytearray()
for i in range(int(RATE * SECONDS)):
    t = i / RATE
    envelope = math.exp(-3.0 * t)
    sample = 0.6 * envelope * (math.sin(2 * math.pi * 660 * t) + 0.5 * math.sin(2 * math.pi * 990 * t))
    frames += struct.pack("<h", int(max(-1.0, min(1.0, sample)) * 32767))

with wave.open(str(out), "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(bytes(frames))

print("wrote", out, out.stat().st_size, "bytes")
```

### 6.8 `Content/Demo/README.md`

The README the owner reads (what to press). Copy verbatim:

````markdown
# Parity demo content

Six small levels, one per group of the recently added Unreal-parity features. Nothing here is code: the
behaviour is Aver Node graphs (`Scripts/`), the features' own asset files, and level text. Press Play, press
the keys below, read the numbers that appear bottom-left of the viewport (graph `Print` nodes; the same lines
are in the Output Log, prefixed `[Graph]`).

Two editor behaviours to know before you start:

- **Placed graph classes start when the level opens, not at Play.** Their `OnStart` and `OnTick` run as soon as the level loads (a class instance ticks
  in the editor), so setup prints such as `bench_slot_count` appear when you open the level. Play adds only the player.
- **Stop removes every graph-class actor in the level** until the Place Class work lands (`docs/plans/parity_demo/PLACE_CLASS.md`, Step 9). If a level looks empty
  or a second Play does nothing, reopen the level (File > Open Level) before pressing Play again.

## Once, before the first run

1. **Reopen the project** after the files are copied in: graphs, retargeters and gameplay definitions are
   indexed at project open.
2. **Make the sound.** The sequencer demo plays `Content/Demo/Audio/chime.wav`, which is not in the repo
   (no audio file ships anywhere). Run `python Tools/make_demo_chime.py` from the project folder; it writes a
   1.2 s mono 16-bit tone. Any other short `.wav` with that file name works too.
3. **Mark one clip as root-motion.** In the Content Browser double-click `Meshes/KenneyChar_attack-kick-right.ocanim`,
   tick **Root motion** in the Anim tab, save. The Kenney clips ship with no flags at all, and the Lunge montage
   (keys G and H in Demo_Animation) is dead without it.
4. **Pick the input scheme.** Project Settings (the project page with Name, Author, Start map), **Input Scheme**
   combo > `Demo/Input/Demo.ocinput`, save the project (this is the project key `INPUT.SCHEME`). Only Demo_Gameplay's T / Y / C+V probe needs it.
5. **Check the classes loaded.** At project open the Output Log lists sixteen lines
   `[Graph] declared class 'AN_Demo...'`. A graph that fails to compile is missing from that list and is
   reported with the node it choked on.

Open a level with File > Open Level (`Content/Demo/Maps/Demo_*.ocworld`), then Play (the toolbar's Stop button leaves it).

## Demo_Animation   (pawn: the Kenney character, third person)

| Key | What happens | Feature |
|---|---|---|
| W A S D, mouse, Space | walk, look, jump | |
| (stand still / walk / hold Shift) | `modeShown = 0 / 1 / 2`; the figure swaps idle / walk / run clips | motion matching, tags, inertialization |
| M | `motion_cost`, `motion_switches`, `motion_is_walk_clip` | matcher state |
| F | melee, then kick (`Montage_Strike_played`) | montage, sections |
| G | the kick lunges the body forward and back about 1.5 m (needs setup step 2) | root motion, Apply |
| H | prints `acc_x_cm`, `acc_y_cm`, `acc_seconds` 0.22 s into the lunge; the body stays | root motion, Accumulate |
| E | the torso leans toward the orbiting grey ball; E again lets go | IK chain, goal, blend |
| Q | the head turns to watch the ball; Q again releases | control rig v2 input |
| X | ragdoll with a shove; X again blends back | ragdoll, override file |

Elsewhere in the level:
- **Circle walker** around (-900, 900): a 45-bone mannequin with no clips of its own plays the Kenney walk clip
  through `Anim/KenneyToMannequin.ocretarget`. **Z** toggles its ragdoll.
- **Foot-IK mannequin** at (0, 1500): it stands with its left foot over the edge of a 20 cm
  block. With foot IK on, the left foot drops to the floor, the pelvis lowers and the right knee bends. Its
  right hand follows a bobbing point. **U** toggles foot IK, **I** prints the offsets (model units, divide by 209
  for cm).
- **Steps and a ramp** for the Kenney hero. The Kenney legs are single bones, so its IK rig has no foot
  placement; the rig shows a CCD chain and a goal instead. The mannequin is where foot IK is visible.

## Demo_Gameplay   (pawn: first-person walker)

| Key | What happens |
|---|---|
| 1 | Fireball: costs 20 mana, 3 s cooldown. Refusal prints `fireRefused = 7` (cooldown) or `8` (cannot afford) |
| 2 | Heal: 15 mana, 5 s cooldown, +30 health |
| 3 | Poison: 4 damage now and every second for 6 s, stacks to 3 |
| 4 | An 18 point hit (a SetByCaller magnitude) |
| 5 | prints the seconds left on the Fireball cooldown |
| L | switch the text widget between English and German |
| T | tap: `Tap_released_quickly` when released within 0.2 s |
| Y | hold: `Hold_pressed`, then `HOLD_triggered_seconds_held` at 0.6 s, `Hold_released_after_trigger` on release; release early gives `Hold_released_too_early` |
| C then V within 0.6 s | `Combo_C_then_V` |

`Health_now` and `Mana_now` lines come from the ability system's attribute events. Mana regenerates 3 per second.
Fireball and Heal also look up their row in `Data/DemoSpells.csv` (`table_fireball_ManaCost` ...) and show the
localised spell name in the text widget at the top-left.

## Demo_AI   (pawn: first-person walker)

A Patron (Kenney figure, a StateTree brain) claims a free bench seat, walks to it, sits for 7 s, picks a spot
with an environment query, walks there and repeats. A red ball sits on the best result of a second query that
runs against YOU every second. Turn on **Show > AI** to see the query items with their scores, the bench slots
(green free, yellow claimed, red occupied) and the state tree. Lines: `Patron_mode` (0 idle, 1 walk, 2 sit),
`Patron_arrived_at_slot_entry`, `Patron_sat_down`, `eqs_item_count`, `eqs_best_score`.

## Demo_Physics   (pawn: first-person walker, starts 18 m south of the rig)

| Station | What to look at | Keys |
|---|---|---|
| Cast rig at the origin | a cube and a tall block sweep through a ring of obstacles and stop where the box / capsule cast hits | prints `box_cast_distance`, `capsule_cast_distance` every 0.5 s |
| Hinge door at (1500, -300) | `Hinge_armed_6000N`; the door swings on a hinge | B soft push (hinge holds), V hard hit (`Hinge_BROKEN`, the door flies off), J hinge angle |
| CCD launcher at (-1500, -500) | two balls at 60 m/s toward a 2 cm wall: the plastic one (no CCD) passes through, the metal one (CCD) stops | C fire again; at 1 s prints `ccd_OFF_past_wall_cm` (about +5000) and `ccd_ON_past_wall_cm` (about -9) |
| Ragdoll dummy around (600, 1200) | the mannequin circles; Z drops it limp with a shove | Z |

## Demo_Components   (pawn: first-person walker; needs the Add Component work, no graph)

Seven placements carry `COMPONENT` records (Output Log on open: `[Level] 18 placed component(s) on 7 placement(s)`). Select one in the Outliner and read its
Details sections; use **Add Component** / **Remove Component** to change them, Ctrl+Z to undo, `File > Save As` to write a copy and compare.

| Placement | Components | What to look at |
|---|---|---|
| IdleKenney | Skeletal Mesh, Animator | idles in the viewport without Play; Bones shows 7 |
| LungeKenney | + Root Motion (Apply, x y, scale 6) | in Play the body lunges and returns; outside Play it stays put (setup step 3 first) |
| MatcherKenney | + Motion Matching (`KenneyLocomotion.ocmotion`) | Database state Ready; playing clip, cost, switches (the figure does not move, so it does not switch) |
| LookKenney | + Control Rig (`KenneyLookStatic.ocrig`, weight 0.6) | drag Weight 0 to 1: the head turns toward a fixed point |
| FootIkMannequin | Skeletal Mesh, Animator, IK Rig (`MannequinIk.ocikrig`, goals off) | on a 20 cm block edge; in Play the overhanging foot drops, pelvis lowers |
| BenchObject + two boxes | Smart Object (`Bench.ocso`) | in Play, Show > AI draws two slots |
| BrainKenney | Skeletal Mesh, Animator, AI Brain (`Patron.ocst`) | in Play: kind state tree, active state `Seek` / `Approach`; one slot turns yellow |

## Demo_Sequencer   (autoplays)

The 24 s sequence `Show` starts with Play: a cube moves and spins (transform), the lamp swells and changes colour
(property), the door vanishes and returns (visibility), `Door_Open` / `Door_Close` events print, a chime plays (audio,
plus a positional one on the door), the camera cuts Wide, Crane, Wide (camera and cut tracks), and a sub-sequence
pulses the small cube three times and raises `Pulse` events. **P** restarts it, **O** pauses it, **K** prints the clock.
`Sequence_Show_started` and `Sequence_Show_finished` come from the sequencer itself.
````

### 6.9 Demo_Components

The only level that uses the new `COMPONENT` record (ADD_COMPONENT.md 3.2, names in its 0.0) and the only one with no graph class except the shared player.
It exists so each Add Component entry has a ready instance to select, read, edit (Ctrl+Z), save and reload, and so the packaged-game claim ("the same
components exist in the runtime") has a level to run. Without the Add Component engine work the `COMPONENT` lines are skipped as unknown records and the
placements are static, unposed meshes: nothing else in the level depends on it.

Every `COMPONENT` line below is already in the canonical form ADD_COMPONENT.md 3.4 writes (asset keys always, `mode` and `input` always, other keys only off
default, canonical key and component order), so `File > Save As` of this level must reproduce each `PLACE` and `COMPONENT` line byte for byte (acceptance row 4).

What each station shows, and what it honestly cannot:

- **Idle / Lunge / Matcher / Look Kenney figures** (row A, x = 300, y = -750, -500, -250, 0). Each is `PLACE Meshes/KenneyChar.ocmesh` scale 2.6 with `nocollide`
  (a character carries no static body, ADD_COMPONENT.md 4.1), yaw 0 (the Kenney rig faces -X, F5, so the figures face the player's start at x = -700).
  *Lunge* plays the kick clip looped with Root Motion Apply on x and y at scale 6: in Play the body slides about 1.5 m and back (needs setup step 3a, the
  root-motion flag; U6); outside Play it plays in place (ADD_COMPONENT.md 6.2). *Matcher* carries Motion Matching with `input=velocity`: the entity never
  moves, so it settles on one clip and the inspector's playing-clip, cost and switch read-outs are what to look at. **Entity-velocity input cannot be shown with
  this database**: no Kenney clip travels (F3), so the trajectory terms are flat and velocity does not change the choice (G6); the test for that input is
  `MotionMatchSystemTest`. *Look* has Control Rig `KenneyLookStatic.ocrig` at weight 0.6: the rig's inputs default to "look at a fixed point, fully on", so the
  Weight slider blends the head toward the point with no script (U9 applies to the axis).
- **Foot-IK mannequin** (row B, x = 1000, y = 0): the 45-bone figure, scale 0.0047813 (1 cm = 209.2 model units, F4), standing with its origin on the edge of a
  20 cm block, IK Rig `MannequinIk.ocikrig` with `goals=0` (the arm goal `Reach` is bound by script in Demo_Animation; here it would be unbound). Play: the foot
  over the floor drops 20 cm, the pelvis lowers, the opposite knee bends (U11; needs the floor and the block to collide, which they do). The Animator plays the
  Kenney idle clip through the retarget asset (U10); if that fails the figure stands in the rest pose and the foot offsets still read.
- **Bench and Brain** (row B, x = 1000, y = 600 and 900): a marker sphere carries Smart Object `Bench.ocso` (scale 1 on purpose: slot positions are transformed by the
  owner's full world matrix including scale, `SynapseSmartObject.cpp:88-99`), two wooden boxes make the seat and back at the same offsets as `AN_DemoBench`, and a Kenney
  figure carries AI Brain `Patron.ocst`. No graph moves it and nothing sends the `Arrived` event, so the tree can only claim a seat and wait: Play shows the
  state path alternating `Seek` and `Approach`, a yellow (claimed) slot with **Show > AI**, and the Details read-outs (kind: state tree, status, active
  path). That is the smoke test of the Brain and Smart Object components; the full loop is Demo_AI (U26).

New asset `Content/Demo/Anim/KenneyLookStatic.ocrig` (same grammar as `KenneyLook.ocrig` in 6.2; only the two `default=` values differ):

```text
OCRIG 2
# KenneyLook with its inputs defaulted ON, for Demo_Components where no script feeds the rig: the head aims at the fixed
# world point below, fully, whenever the Control Rig component's Weight is above 0. Look is the op weight, LookAt the point.
# axis=-1,0,0 is the head bone's own forward (see KenneyLook.ocrig; U9).
NAME KenneyLookStatic
INPUT LookAt vec3 space=world default=300,900,160
INPUT Look float default=1
OP aim bone=head at=@LookAt axis=-1,0,0 weight=@Look
```

`Content/Demo/Maps/Demo_Components.ocworld`:

```text
OCWORLD 1
# Demo_Components: the Add Component record set on ordinary placements. No graph class but the shared player (GAMEMODE).
# Each `COMPONENT <Type> key=value` line belongs to the PLACE line above it (ADD_COMPONENT.md 3.2). Centimetres, +X forward, +Y right, +Z up.
# Row A (x=300): Skeletal Mesh + Animator, Root Motion, Motion Matching, Control Rig. Row B (x=1000): IK Rig on the mannequin, Smart Object, AI Brain.
NAME Demo_Components
BUILD 0
ALGO 3
GAMEMODE AN_DemoRules
SPAWN -700 0 10 0
CAMERA -300 -1200 500 54 -20 800
SUN elev 55 azim 40 color 1 1 1 lux 100000
SKY model physical
FOG exp density 0.00002 color 0.7 0.78 0.88

PLACEG Meshes/cube.ocmesh 0 0 -10 0 0 0 4000 4000 10 Caldera_concrete

# ---- row A: the Kenney figure is 67 units tall, so scale 2.6 (as the owner's Default.ocmap places it) --------------------------------
PLACE Meshes/KenneyChar.ocmesh 300 -750 0 0 0 0 2.6 KenneyChar_colormap nocollide name IdleKenney
  COMPONENT SkeletalMesh skeleton=Meshes/KenneyChar.ocskel
  COMPONENT Animator clip=Meshes/KenneyChar_idle.ocanim
PLACE Meshes/KenneyChar.ocmesh 300 -500 0 0 0 0 2.6 KenneyChar_colormap nocollide name LungeKenney
  COMPONENT SkeletalMesh skeleton=Meshes/KenneyChar.ocskel
  COMPONENT Animator clip=Meshes/KenneyChar_attack-kick-right.ocanim
  COMPONENT RootMotion mode=apply axes=x,y scale=6
PLACE Meshes/KenneyChar.ocmesh 300 -250 0 0 0 0 2.6 KenneyChar_colormap nocollide name MatcherKenney
  COMPONENT SkeletalMesh skeleton=Meshes/KenneyChar.ocskel
  COMPONENT Animator clip=Meshes/KenneyChar_idle.ocanim
  COMPONENT MotionMatcher database=Demo/Anim/KenneyLocomotion.ocmotion input=velocity
PLACE Meshes/KenneyChar.ocmesh 300 0 0 0 0 0 2.6 KenneyChar_colormap nocollide name LookKenney
  COMPONENT SkeletalMesh skeleton=Meshes/KenneyChar.ocskel
  COMPONENT Animator clip=Meshes/KenneyChar_idle.ocanim
  COMPONENT ControlRig rig=Demo/Anim/KenneyLookStatic.ocrig weight=0.6

# ---- row B: the mannequin on the edge of a 20 cm block (the block covers y from -500 to 0; the figure's left side is +Y, so its left foot hangs over the floor) ----
PLACEG Meshes/cube.ocmesh 1000 -250 10 0 0 0 250 250 10 Caldera_concrete
PLACE Meshes/Character.ocmesh 1000 0 20 0 0 0 0.0047813 M_DemoMannequin nocollide name FootIkMannequin
  COMPONENT SkeletalMesh skeleton=Skeletons/Character.ocskel
  COMPONENT Animator clip=Meshes/KenneyChar_idle.ocanim
  COMPONENT IkRig asset=Demo/Anim/MannequinIk.ocikrig goals=0

# ---- row B: the bench (owner scale 1: slot offsets are in the owner's frame) and a figure whose brain claims it ----------------------------------------------------
PLACE Meshes/sphere.ocmesh 1000 600 0 0 0 0 1 Caldera_plastic nocollide name BenchObject
  COMPONENT SmartObject asset=Demo/AI/Bench.ocso
PLACEG Meshes/cube.ocmesh 960 600 22 0 0 0 22 60 22 Caldera_wood
PLACEG Meshes/cube.ocmesh 938 600 66 0 0 0 4 60 22 Caldera_wood
PLACE Meshes/KenneyChar.ocmesh 1000 900 0 0 0 0 2.6 KenneyChar_colormap nocollide name BrainKenney
  COMPONENT SkeletalMesh skeleton=Meshes/KenneyChar.ocskel
  COMPONENT Animator clip=Meshes/KenneyChar_idle.ocanim
  COMPONENT Brain asset=Demo/AI/Patron.ocst team=guests
```

The open-level log line to expect once the Add Component work is in: `[Level] 18 placed component(s) on 7 placement(s)` (2 + 3 + 3 + 3 on row A, 3 on the
mannequin, 1 on the bench object, 3 on the brain figure; ADD_COMPONENT.md 5.1).

## 7. Acceptance checks

Run these in a normal (headed) editor session, one level at a time. "Overlay" is the bottom-left viewport print feed; every line is also in the Output Log with the `[Graph] ` prefix.

**All levels, at Play.** Output Log: `[Sandbox] Play: begin_play GameMode='AN_DemoRules'` and `Play: AN_DemoRules spawned at the level's Player Start (...)` (`SandboxPlay.cpp:425-466`); if the toolbar says Camera Location it says so instead.

**All levels, at project open.** Output Log: sixteen `[Graph] declared class 'AN_Demo...'` lines (`AN_DemoRules`, `AN_DemoController`, `AN_DemoInputProbe` and `AN_DemoBench`... all of them; the ones with an OnStart / OnTick entry end `, ticks`). No `[GAS]`, `[Graph]` error or "was not found" lines mentioning `Demo/`.

**Demo_Animation.**

| Do | Expect | If not |
|---|---|---|
| Play, stand still | the Kenney figure idles; overlay `modeShown = 0` appears at the first change | `FindEntity name=kBody` failed: log `[Graph]` warns, check U4 |
| hold W | `modeShown = 1`, figure plays walk; add Shift: `modeShown = 2`, sprint clip | U3: press M, `motion_switches` must rise on each change; if it does not, set `switchMargin` 0 already, then lower `interval` |
| M | `motion_cost`, `motion_switches`, `motion_is_walk_clip` (1 while walking, else 0) | |
| F | `Montage_Strike_played`; melee then kick over about 0.95 s | U5 |
| G (after setup step 3a) | `Lunge_root_motion_APPLY`; the body slides forward / back about 1.5 m while the clip plays | U6; without the flag nothing moves |
| H | `Lunge_root_motion_ACCUMULATE`, then `acc_x_cm`, `acc_y_cm`, `acc_seconds` (non-zero, seconds about 0.22); the body stays put | U7 |
| E | `IK_Reach_ON`; the torso leans toward the ball; E again: `IK_Reach_OFF` | U8; check the log for `[IkRig]` binding messages |
| Q | `Look_toggled`; the head turns toward the ball | U9 |
| X | `Ragdoll_ON`; the figure goes limp and tumbles; X again: `Ragdoll_OFF` | U12 |
| look at the walker around (-900, 900) | a mannequin walks a circle, playing the Kenney walk (arms and legs swing, no bob) | U10; open `KenneyToMannequin.ocretarget` in the Retargeter tab to preview |
| look at (0, 1500) | mannequin straddling the block edge: left leg extended down, pelvis lowered, right knee bent, right hand tracing a small arc; `U` pops the pose to the unadjusted one over 0.2 s; `I` prints the offsets (left foot about -4180 model units = -20 cm) | U11 |
| Z | `Mannequin_ragdoll_ON`: the circle walker freezes and falls limp with a shove; Z again blends it back up | U12 |

**Demo_Gameplay.**

| Do | Expect |
|---|---|
| Play | a text widget at the top-left with the English title (U15). Mana starts full (60) so nothing prints until you spend some |
| 1 | `Mana_now = 40`, `table_fireball_ManaCost = 20`, `table_fireball_Damage = 35`, the widget shows `Fireball`, then `Mana_now = 43`, `46` ... as the regeneration ticks; a second press inside 3 s prints `fireRefused = 7`; with mana under 20, `= 8` |
| 2 | `Mana_now` drops 15, widget `Heal`; at full health no `Health_now` line appears (the +30 is clamped away), so press 4 first (`Health_now = 82`), then 2 (`Health_now = 100`) |
| 3 | `State_Poisoned_present = 1`, `Health_now` falls 4 at once and every second for 6 s, then `State_Poisoned_present = 0`; pressing 3 twice more stacks the damage per tick |
| 4 | `Health_now` falls exactly 18 |
| 5 | `fireballCooldownSeconds` shows the seconds left |
| L | the widget text switches to German and back |
| T / Y / C then V | the lines listed in the README (setup step 3b must be done) |

**Demo_AI.**

| Do | Expect |
|---|---|
| Play | `bench_slot_count = 2  (x2)` (two benches); `Patron_brain_loaded = 1`; the Patron walks to a bench (`Patron_mode = 1`), then `Patron_arrived_at_slot_entry`, `Patron_sat_down`, `Patron_mode = 2` and the sit clip for about 7 s, then it stands and walks to a spot (`Patron_mode = 1`, then `0`) and repeats |
| Show > AI | slot arrows: green free, yellow claimed, red occupied; the state tree labels `Seek`, `Approach`, `Sit`, `Rest`; EQS items with scores |
| walk around | every second `eqs_item_count` and `eqs_best_score`; the red ball moves to a spot 3 m or more from you, close to you, behind a pillar when one is in the way |

**Demo_Physics.**

| Do | Expect |
|---|---|
| Play | `hinge_armed: Hinge_armed_6000N`, `ccd_fired: Fired_A_noCCD_B_CCD`; every 0.5 s `box_cast_distance` and `capsule_cast_distance` with numbers between 0 and 700; the two ghosts sweep around the origin and jump inward when they meet an obstacle |
| 1 s after Play | `ccd_OFF_past_wall_cm` about +5000 (the plastic ball went through the wall), `ccd_ON_past_wall_cm` about -9 (the metal ball stopped) |
| C | both balls respawn and fire again |
| B | `Soft_push_2500`; the door swings and settles within +-1.5 rad; J prints the angle |
| V | `Hard_hit_60000` then `Hinge_BROKEN`; the door flies off its post |
| Z | the circling mannequin freezes and ragdolls with a shove, Z again stands it up |

**Demo_Sequencer.**

| Do | Expect |
|---|---|
| Play | `Sequence_Show_started`; the chime at 2 s; the lamp brightens at 4 s; the cube spins and rises; the door disappears at 8 s with `Door_Open_event_arg0 = 0.5`; `Pulse_event_from_the_sub_sequence` three times between 4.5 and 8.5 s; the view cuts to the crane at 12 s and back at 20 s; `Sequence_Show_finished` at 24 s |
| P / O / K | restart / pause / prints `Show_time_s`, `Show_length_s` (24) |
| Animate mode | the same sequence scrubs in the Sequencer window (events and audio do not run there) |

**Demo_Components** (needs the Add Component engine work; every row is by reading the Details panel and the log, nothing is scripted).

| Do | Expect | If not |
|---|---|---|
| Open the level | Output Log `[Level] 18 placed component(s) on 7 placement(s)`; no `[Level] COMPONENT ... refused` lines | refused lines name the placement and reason; `could not load` lines name the missing asset path (U23) |
| Click IdleKenney (Outliner) | Details has Skeletal Mesh (Skeleton `Meshes/KenneyChar.ocskel`, Bones 7) and Animator (Clip idle); the figure idles in the viewport without Play | static figure: the figure is not skinned (log `has no skin streams`, ADD_COMPONENT.md 10) or the Add Component work is missing |
| LungeKenney: Play | the body slides about 1.5 m along X and back, repeating; Stop restores the placement; not in Play: no drift | no motion: setup step 3a not done (U6) |
| MatcherKenney | Motion Matching section: Database state `Ready` within a few seconds, a playing clip name, cost, switch count | `Failed` / `Does not fit skeleton`: log names the database and skeleton (U24) |
| LookKenney: drag Control Rig Weight 0 -> 1 | the head turns toward a point about 9 m to the side; Ctrl+Z restores the previous weight in one step | head turns the wrong way: U9 (flip `axis=`) |
| FootIkMannequin: Play | diagnostics show probes > 0; the overhanging foot is about 20 cm lower than the supported one and the pelvis drops | U11, U25 |
| BenchObject: Play, Show > AI | two slot arrows at the bench (smart objects register on the AI tick, which runs in Play only, SandboxApp.cpp:2377-2381) | the `.ocso` did not load: log `could not load smart object` |
| BrainKenney: Play | AI Brain section: kind state tree, status running, active state `Seek` then `Approach`; Show > AI: one slot yellow | U26 |
| `File > Save As` `Demo_Components_copy.ocworld`, then diff against the original | every `PLACE` and `COMPONENT` line is byte-identical (the file otherwise gains the editor's banner and `ID` line) | a line differs: the writer is not canonical per ADD_COMPONENT.md 3.4, or a value did not round-trip: that is a bug to report |
| Reopen the copy, then undo/redo an Add and a Remove Component | state returns each time; the level is marked unsaved after Add | |

**Editor features, using this content** (PLACE_CLASS.md 6.3 has the full list). Drag `Content/Demo/Scripts/AN_DemoOrbiter.ocgraph` onto the floor of Demo_Components: a grey
ball appears under the cursor, is named `AN_DemoOrbiter` in the Outliner, orbits its drop point, and `File > Save As` writes `PLACE none ... class AN_DemoOrbiter`. Select it,
Delete, Ctrl+Z: it returns and keeps orbiting. Play then Stop: it is still there.

## 8. Node confirmation table

Every node type the sixteen graphs use, with the title and category the editor palette shows (`sandbox/src/GraphNodeDefs*.hpp`) and where its pins are defined
for the compiler (`OcGraphParser.cs` `AddDefaultPins` case line, or the row of a `Graph*NodeTable.cs`). Pin names and types in the graphs were checked mechanically against those
definitions (direction, type, one exec link per exec output, every exec node reachable from an ENTRY, required attributes present). That proves names and wiring, not behaviour.
Where the palette's own pin list could not be compared line by line (the shape casts are built by a helper; math nodes also list the material float3 overload) the Note column says so; the compiler's definition is the one that runs.

| Node type | Palette title | Category | Pins defined at | Used by | Note |
|---|---|---|---|---|---|
| `Add` | Add | Math | `OcGraphParser.cs:971` | CastRig, Ccd, EqsProbe, Hero, Hinge, Mannequin, MannequinIk, Orbiter, Patron | palette also lists the material float3 overload |
| `AddDynamicBox` | Add Dynamic Box | Physics | `OcGraphParser.cs:1461` | Hinge |  |
| `AddDynamicSphere` | Add Dynamic Sphere | Physics | `OcGraphParser.cs:1474` | Ccd |  |
| `AddImpulse` | Add Impulse | Physics | `OcGraphParser.cs:1528` | Hinge |  |
| `AddStaticBox` | Add Static Box | Physics | `OcGraphParser.cs:1449` | Hinge |  |
| `AN_BrainSendEvent` | Send Brain Event | AI | `GraphAiQueryNodeTable.cs:37` | Patron |  |
| `AN_BrainSet` | Set Brain | AI | `GraphAiQueryNodeTable.cs:36` | Patron |  |
| `AN_CreateUiWidget` | Create UI Widget | UI | `GraphGameSystemNodeTable.cs:53` | Caster |  |
| `AN_EqsRun` | Run EQS Query | AI | `GraphAiQueryNodeTable.cs:19` | EqsProbe |  |
| `AN_EventPayload` | Event Payload | Events | `GraphGameSystemNodeTable.cs:23` | Caster, InputProbe, SeqListener |  |
| `AN_FindDataTableRow` | Find Data Table Row | Data | `GraphGameSystemNodeTable.cs:94` | Caster |  |
| `AN_GasAddAttributeSet` | Add Attribute Set | Abilities | `GraphGasNodeTable.cs:28` | Caster |  |
| `AN_GasApplyEffect` | Apply Gameplay Effect | Abilities | `GraphGasNodeTable.cs:29` | Caster |  |
| `AN_GasCooldown` | Ability Cooldown | Abilities | `GraphGasNodeTable.cs:43` | Caster |  |
| `AN_GasGetAttribute` | Get Attribute | Abilities | `GraphGasNodeTable.cs:26` | Caster |  |
| `AN_GasGrantAbility` | Grant Ability | Abilities | `GraphGasNodeTable.cs:34` | Caster |  |
| `AN_GasTryActivate` | Try Activate Ability | Abilities | `GraphGasNodeTable.cs:36` | Caster |  |
| `AN_GetDataTableFloat` | Get Data Table Float | Data | `GraphGameSystemNodeTable.cs:98` | Caster |  |
| `AN_GetDataTableInt` | Get Data Table Int | Data | `GraphGameSystemNodeTable.cs:97` | Caster |  |
| `AN_OnEvent` | On Event | Events | `GraphGameSystemNodeTable.cs:19` | Caster, InputProbe, SeqListener |  |
| `AN_SetCulture` | Set Culture | Localization | `GraphGameSystemNodeTable.cs:102` | Caster |  |
| `AN_SetUiText` | Set UI Text | UI | `GraphGameSystemNodeTable.cs:54` | Caster |  |
| `AN_SetUiTextFromTable` | Set UI Text From Table | Data | `GraphGameSystemNodeTable.cs:101` | Caster |  |
| `AN_SoAttach` | Attach Smart Object | AI | `GraphAiQueryNodeTable.cs:26` | Bench |  |
| `AN_SoGetClaim` | Get Smart Object Claim | AI | `GraphAiQueryNodeTable.cs:31` | Patron |  |
| `AN_SoSlotCount` | Smart Object Slot Count | AI | `GraphAiQueryNodeTable.cs:33` | Bench |  |
| `And` | AND | Logic | `OcGraphParser.cs:836` | Ccd, Hero, Hinge, Patron |  |
| `BindIkGoal` | Bind IK Goal | Animation | `GraphIkNodeTable.cs:19` | Hero |  |
| `BoolToFloat` | Bool To Float | Convert | `OcGraphParser.cs:1210` | Hero, Patron |  |
| `BoxCast` | Box Cast | Physics | `OcGraphParser.cs:2536` | CastRig | palette entry built by a helper (`GraphNodeDefs.hpp:329`); same pins |
| `Branch` | Branch | Flow | `OcGraphParser.cs:1942` | CastRig, Caster, Ccd, EqsProbe, Hero, Hinge, Mannequin, MannequinIk, Patron, SeqListener, Viewer |  |
| `CapsuleCast` | Capsule Cast | Physics | `OcGraphParser.cs:2542` | CastRig | palette entry built by a helper (`GraphNodeDefs.hpp:330`); same pins |
| `CharacterMove` | Character Move | Actor | `OcGraphParser.cs:2214` | Hero, Viewer |  |
| `ClearIkGoal` | Clear IK Goal | Animation | `GraphIkNodeTable.cs:21` | Hero |  |
| `ConstBool` | Const Bool | Const | `OcGraphParser.cs:828` | Caster, Ccd, Hero, Hinge, Mannequin, MannequinIk, Patron |  |
| `ConstFloat` | Const Float | Const | `OcGraphParser.cs:818` | CastRig, Caster, Ccd, EqsProbe, Hero, Hinge, Mannequin, MannequinIk, Orbiter, Patron, SeqListener, Viewer |  |
| `ConstInt` | Const Int | Const | `OcGraphParser.cs:823` | Caster, Ccd, Hero, Hinge, InputProbe, Mannequin, MannequinIk, SeqListener, Viewer |  |
| `ConsumeRootMotion` | Consume Root Motion | Animation | `GraphGameSystemNodeTable.cs:119` | Hero |  |
| `Cos` | Cos | Math | `OcGraphParser.cs:1916` | CastRig, Mannequin, Orbiter | palette also lists the material float3 overload |
| `CustomEvent` | Custom Event | Event | `OcGraphParser.cs:2028` | Patron |  |
| `DestroyBody` | Destroy Body | Physics | `OcGraphParser.cs:1425` | Ccd |  |
| `Equal` | a == b | Logic | `OcGraphParser.cs:885` | Patron |  |
| `FindEntity` | Find Entity | Scene | `OcGraphParser.cs:1379` | CastRig, Ccd, EqsProbe, Hero, Hinge, Mannequin, MannequinIk, Patron |  |
| `FloatToInt` | Float To Int | Convert | `OcGraphParser.cs:1215` | Hero, Patron |  |
| `GetBlackboardVec3` | Get Blackboard Vec3 | Blackboard | `GraphGameSystemNodeTable.cs:32` | Patron |  |
| `GetBodyPosition` | Get Body Position | Physics | `OcGraphParser.cs:1335` | Ccd, Hinge |  |
| `GetFieldVec3` | Get Field (Vec3) | Scene | `OcGraphParser.cs:1866` | CastRig, Ccd, Hinge, Mannequin, MannequinIk, Orbiter |  |
| `GetFootIkState` | Get Foot IK State | Animation | `GraphIkNodeTable.cs:24` | MannequinIk |  |
| `GetForward` | Get Forward (Look) | Scene | `OcGraphParser.cs:1895` | Hero |  |
| `GetJointValue` | Get Joint Value | Physics | `OcGraphParser.cs:1759` | Hinge |  |
| `GetMotionState` | Get Motion State | Animation | `GraphMotionMatchNodeTable.cs:21` | Hero |  |
| `GetPlayerPawn` | Get Player Pawn | Game | `OcGraphParser.cs:1117` | EqsProbe |  |
| `GetSequenceState` | Get Sequence State | Sequencer | `GraphSequenceNodeTable.cs:24` | SeqListener |  |
| `GetVar` | Get Var | Var | `OcGraphParser.cs:2401` | CastRig, Caster, Ccd, EqsProbe, Hero, Hinge, Mannequin, MannequinIk, Orbiter, Patron |  |
| `GetWorldPosition` | Get World Position | Transform | `OcGraphParser.cs:1221` | Hero, Patron |  |
| `Greater` | a > b | Logic | `OcGraphParser.cs:861` | Hero, Hinge, Patron |  |
| `GreaterEqual` | a >= b | Logic | `OcGraphParser.cs:867` | CastRig, Ccd, EqsProbe |  |
| `InputKey` | Input Key | Input | `OcGraphParser.cs:2072` | Hero, Viewer |  |
| `InputKeyPressed` | Input Key Pressed | Input | `OcGraphParser.cs:2086` | Caster, Ccd, Hero, Hinge, Mannequin, MannequinIk, SeqListener |  |
| `IntToFloat` | Int To Float | Convert | `OcGraphParser.cs:1205` | Patron |  |
| `IsMontagePlaying` | Is Montage Playing | Animation | `GraphGameSystemNodeTable.cs:115` | Hero |  |
| `IsMotionClip` | Is Motion Clip | Animation | `GraphMotionMatchNodeTable.cs:22` | Hero |  |
| `JointHinge` | Joint: Hinge | Physics | `OcGraphParser.cs:1697` | Hinge |  |
| `Jump` | Jump | Actor | `OcGraphParser.cs:1879` | Hero, Viewer |  |
| `LessEqual` | a <= b | Logic | `OcGraphParser.cs:879` | Patron |  |
| `LookAt` | Look At | Transform | `OcGraphParser.cs:1307` | Hinge, Patron |  |
| `Min` | Min | Math | `OcGraphParser.cs:898` | Patron | palette also lists the material float3 overload |
| `MouseDelta` | Mouse Delta | Input | `OcGraphParser.cs:2165` | Hero, Viewer |  |
| `MoveAxis` | Move Axis | Input | `OcGraphParser.cs:2175` | Hero, Viewer |  |
| `Multiply` | Multiply | Math | `OcGraphParser.cs:977` | CastRig, Hero, Hinge, Mannequin, MannequinIk, Orbiter, Patron, Viewer | palette also lists the material float3 overload |
| `Negate` | Negate | Math | `OcGraphParser.cs:927` | CastRig |  |
| `Not` | NOT | Logic | `OcGraphParser.cs:854` | Caster, Ccd, Hero, Hinge, MannequinIk, Patron |  |
| `NotEqual` | a != b | Logic | `OcGraphParser.cs:891` | Hero, Patron |  |
| `OnStart` | On Start | Event | `OcGraphParser.cs:2015` | Bench, CastRig, Caster, Ccd, EqsProbe, Hero, Hinge, Mannequin, MannequinIk, Orbiter, Patron |  |
| `OnTick` | On Tick | Event | `OcGraphParser.cs:2021` | CastRig, Caster, Ccd, EqsProbe, Hero, Hinge, Mannequin, MannequinIk, Orbiter, Patron, SeqListener, Viewer |  |
| `Param` | Param | Param | `OcGraphParser.cs:2046` | Bench, CastRig, Caster, Ccd, EqsProbe, Hero, Hinge, Mannequin, MannequinIk, Orbiter, Patron, Viewer |  |
| `PauseSequence` | Pause Sequence | Sequencer | `GraphSequenceNodeTable.cs:18` | SeqListener |  |
| `PlayAnimation` | Play Animation | Scene | `OcGraphParser.cs:2504` | Mannequin, MannequinIk, Patron |  |
| `PlayMontage` | Play Montage | Animation | `GraphGameSystemNodeTable.cs:109` | Hero |  |
| `PlaySequence` | Play Sequence | Sequencer | `GraphSequenceNodeTable.cs:17` | SeqListener |  |
| `Print` | Print | Debug | `OcGraphParser.cs:1830` | CastRig, Caster, Ccd, EqsProbe, Hero, Hinge, InputProbe, MannequinIk, Patron, SeqListener |  |
| `PrintInt` | Print Int | Debug | `OcGraphParser.cs:1824` | Bench, Caster, EqsProbe, Hero, Patron |  |
| `PrintString` | Print String | Debug | `OcGraphParser.cs:1839` | Ccd, Hero, Hinge, InputProbe, Mannequin, MannequinIk, Patron, SeqListener |  |
| `RagdollImpulse` | Ragdoll Impulse | Scene | `OcGraphParser.cs:2532` | Hero, Mannequin |  |
| `RagdollStart` | Ragdoll Start | Scene | `OcGraphParser.cs:2522` | Hero, Mannequin |  |
| `RagdollStop` | Ragdoll Stop | Scene | `OcGraphParser.cs:2525` | Hero, Mannequin |  |
| `RequestMotionSearch` | Request Motion Search | Animation | `GraphMotionMatchNodeTable.cs:20` | Hero |  |
| `Select` | Select | Logic | `OcGraphParser.cs:2063` | CastRig, EqsProbe, Hero, Mannequin, MannequinIk, Patron |  |
| `Sequence` | Sequence | Flow | `OcGraphParser.cs:1953` | Caster, Hero, Hinge, Patron, SeqListener |  |
| `SetBodyGravityFactor` | Set Body Gravity Factor | Physics | `OcGraphParser.cs:1577` | Ccd |  |
| `SetBodyVelocity` | Set Body Velocity | Physics | `OcGraphParser.cs:1405` | Ccd |  |
| `SetCcd` | Set CCD | Physics | `OcGraphParser.cs:2569` | Ccd |  |
| `SetControlRig` | Set Control Rig | Scene | `OcGraphParser.cs:2513` | Hero |  |
| `SetFieldVec3` | Set Field (Vec3) | Scene | `OcGraphParser.cs:1907` | Orbiter |  |
| `SetFootIkWeight` | Set Foot IK Weight | Animation | `GraphIkNodeTable.cs:23` | MannequinIk |  |
| `SetIkGoal` | Set IK Goal | Animation | `GraphIkNodeTable.cs:18` | MannequinIk |  |
| `SetIkGoalWeight` | Set IK Goal Weight | Animation | `GraphIkNodeTable.cs:20` | Hero |  |
| `SetIkRig` | Set IK Rig | Animation | `GraphIkNodeTable.cs:17` | Hero, MannequinIk |  |
| `SetJointBreakable` | Set Joint Breakable | Physics | `OcGraphParser.cs:2562` | Hinge |  |
| `SetLocalPosition` | Set Local Position | Transform | `OcGraphParser.cs:1283` | CastRig, Ccd, EqsProbe, Hinge, Mannequin, Orbiter, Patron |  |
| `SetLocalRotation` | Set Local Rotation | Transform | `OcGraphParser.cs:1295` | CastRig, Mannequin, Patron |  |
| `SetMotionInput` | Set Motion Input | Animation | `GraphMotionMatchNodeTable.cs:18` | Hero |  |
| `SetMotionMatching` | Set Motion Matching | Animation | `GraphMotionMatchNodeTable.cs:17` | Hero |  |
| `SetMotionTags` | Set Motion Tags | Animation | `GraphMotionMatchNodeTable.cs:19` | Hero |  |
| `SetName` | Set Name | Scene | `OcGraphParser.cs:2450` | Orbiter |  |
| `SetRigInputFloat` | Set Rig Input (Float) | Animation | `GraphIkNodeTable.cs:25` | Hero |  |
| `SetRigInputVec3` | Set Rig Input (Vector) | Animation | `GraphIkNodeTable.cs:26` | Hero |  |
| `SetRootMotion` | Set Root Motion | Animation | `GraphGameSystemNodeTable.cs:118` | Hero |  |
| `SetVar` | Set Var | Var | `OcGraphParser.cs:2415` | CastRig, Caster, Ccd, EqsProbe, Hero, Hinge, Mannequin, MannequinIk, Orbiter, Patron |  |
| `Sin` | Sin | Math | `OcGraphParser.cs:1915` | CastRig, Mannequin, MannequinIk, Orbiter | palette also lists the material float3 overload |
| `StopSequence` | Stop Sequence | Sequencer | `GraphSequenceNodeTable.cs:19` | SeqListener |  |
| `Subtract` | Subtract | Math | `OcGraphParser.cs:1921` | Ccd, Hero, Hinge, Patron | palette also lists the material float3 overload |
| `SwitchInt` | Switch on Int | Flow | `OcGraphParser.cs:1813` | Hero, Patron |  |
| `VecDistance` | Vec Distance | Vector | `OcGraphParser.cs:1072` | Hinge |  |
| `VecLength` | Vec Length | Vector | `OcGraphParser.cs:1065` | Hero, Patron |  |
| `VecNormalize` | Vec Normalize | Vector | `OcGraphParser.cs:1033` | Hero, Patron |  |

## 9. UNVERIFIED register

Nothing below was run. For each: the claim, what is known, how to settle it, and the cheapest fallback.

| # | Claim | What is known | Settle it by | Fallback |
|---|---|---|---|---|
| U1 | The Kenney figure and the mannequin face -X, so `rot=180,0,0` makes them face +X | F2, F4, F5 | Play Demo_Animation, walk forward: the hero should walk face-first | change the yaw in the `COMP` line of `AN_DemoHero`, `AN_DemoPatron`, `AN_DemoMannequin`, `AN_DemoMannequinIk` (0 if they walk backwards, 90 / -90 if sideways) |
| U2 | `SetControlRig`, `PlayAnimation`, `SetName` and `SetFieldVec3` written with every pin declared by hand compile in the push compiler | `AN_FPCharacter` does the same for `SetParent` / `SetFieldVec3` (`OcGraphParser.cs:814`); the emitters are `GraphCompiler.cs:3937-3990` | the log at project open | n/a |
| U3 | Tag-gated matching switches clips on a tag change | `docs/ANIM_MOTION_MATCHING.md` section 3; a current row without the required tag is not a candidate (not read) | Demo_Animation, key M | `RequestMotionSearch` already follows every `SetMotionTags`; lower `search.interval` to 0.05, or give each tag family its own `.ocmotion` |
| U4 | `FindEntity name=kBody` returns the COMP child at `OnStart` | F19; COMPs are built at bind, before `OnStart` (`GraphComponentTree.cs:44-67`) | the figure appears and animates | none needed; if two graph classes of the same COMP id are placed the first wins |
| U5 | A `DefaultSlot` montage plays over a matched entity that has no state machine | `docs/ANIM_MONTAGES.md` sections 1, 4 | key F | add a one-state `.ocasm` |
| U6 | Apply moves the scaled child inside its parent; the clip flag is required; the visible excursion is about 156 cm (10 units x 2.6 x scale 6) | `docs/ANIM_MONTAGES.md` section 4 | key G | lower `rmScale` (6.0) in the hero graph |
| U7 | Accumulate collects the lunge and `ConsumeRootMotion` reads it in the entity's frame | same | key H | |
| U8 | The CCD lean works on a two-bone chain and `BindIkGoal` engages (the graph also calls `SetIkGoalWeight` 1 over 0.3 s) | `docs/ANIM_IK_RETARGET.md` "Driving it from a script" | key E | |
| U9 | `axis=-1,0,0` is the head's forward in bone space | identity bind rotations, F2, F5 | key Q: if the head looks away from the ball use `axis=1,0,0` | |
| U10 | The six single-bone-source chains retarget plausibly and the baked clip plays on the mannequin | `docs/ANIM_IK_RETARGET.md` "Retargeting"; the mannequin's rest pose is unusual (bones with 180 degree rotations, three roots with scale 100) | the Retargeter tab's preview, then the walker | Auto-map in the Retargeter tab and keep its chains |
| U11 | Foot IK numbers (model units), the arm's `vector` pole `[0,-0.5,-1]`, and that the left foot over the edge is pulled down while the right stays | F4, F13, `docs/ANIM_IK_RETARGET.md` "Foot placement" | keys I and U at the block edge | scale every `*Cm` number in `MannequinIk.ocikrig` by the factor the log suggests |
| U12 | The ragdoll builds on a 7-bone rig with the override, and on the mannequin | F18 | key X / Z | delete `KenneyChar.ocragdoll` to see the automatic 2-part version |
| U13 | `pitch -12` slopes the ramp up toward +X | rotation sign conventions unread | look at it | flip the sign |
| U14 | `ACTORDEFAULTS AN_DemoCaster` applies to a placed class actor (the graph adds the set and abilities itself if not); periodic damage through a meta attribute; `PERIOD 1 apply` | `docs/GAMEPLAY_ABILITIES.md` | keys 1 to 4 | none needed |
| U15 | `AN_CreateUiWidget kind=1` makes a visible text root at the top-left, `AN_SetUiText` / `AN_SetUiTextFromTable` take `@Table:Key=Default` references and follow `AN_SetCulture`; the culture choice may persist as the player setting `locale.culture` (`docs/LOCALIZATION.md` "Cultures") | `docs/GAME_UI.md` "Visual-script nodes", F16 | key L | the `Print` lines already carry every number; press L again to leave the German culture |
| U16 | An action with only a `combo:` trigger evaluates without a binding | `ActionEngine.cpp:92-210` reads other actions' rises, not its own value | keys C then V | bind `DemoCombo` to a spare key |
| U17 | The StateTree chain of root states, `ON event:Arrived`, `COMPLETE any`, the abort releasing the claim, and `SatDown` reaching the graph's `CustomEvent` entry | F15 | the Patron loop | send `AN_SoUse` from the graph on arrival instead |
| U18 | `ring` + `distance` + `random` + `trace` parse and run; `project=none` needs no grid; `RunEqsQuery` writes `WanderPos` and `GetBlackboardVec3` reads it | `docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md` section 1 | `eqs_item_count`, the overlay | remove the `trace` test |
| U19 | The soft push does not break the hinge (about 1500 N reaction) and the hard hit does; `SetCcd` makes ball B stop; impulse units are kg x cm / s | `docs/physics/GRAPH_NODES.md` | keys B, V, C | change `softPush`, `hardPush`, `breakForce` |
| U20 | Transform key scale values are absolute; `bind` works on a camera track; the sub-sequence path with `%23`; light property names; audio paths | `docs/SEQUENCER.md`, `OcSeq.cpp:101-166` | Play the level | camera: `SEQTRACK camera name Crane target 4` (CraneArm is placement 4 in file order); sub-sequence: embed the Pulse tracks in `Show` |
| U21 | A mixed OnStart / OnTick / event graph with 11-way `Sequence` fan-out and fan-in diamonds compiles | `GraphCompiler.cs:2868-2903` | project open | split arms into a second class |
| U22 | Graph classes run `OnStart` at level load, so setup (ability grants, smart-object attach, `SetName`) happens before Play and is not redone by Play; Play does not reset GAS or the smart-object registry | `docs/VISUAL_SCRIPTING.md:276-283`, `FrameworkAbi.cpp:981-1011` (Play spawns only the GameInstance, GameMode, controller, pawn); whether Play start resets GAS / Synapse state was not read | the Output Log at level open, then Play and key 1 / the Patron | reopen the level, or press Play only once per open; if Play does reset, move the setup to an `AN_OnEvent` entry raised by the mode |
| U23 | Every asset path in `Demo_Components` resolves: the applier hashes content-relative paths exactly as the `PLACE` column does | ADD_COMPONENT.md 3.2; paths here match 6.1-6.8 and the PTTest listing | the open-level log | fix the path in the record |
| U24 | A `.ocmotion` baked for `KenneyChar.ocskel` binds to a placed (not graph-child) entity and the matcher's first search runs while the entity is static | 6.2's graph does the same on a child; MotionMatchSystem.cpp:300-345 | Details read-outs | none needed |
| U25 | Foot IK on a placed mannequin finds the floor and block: the editor starts physics before the level loads (`SandboxApp.cpp:597-599`) and both boxes collide | ADD_COMPONENT.md 4.6 | Play, diagnostics probes | check Collides is ticked on the floor and the block in Details |
| U26 | `Patron.ocst` runs standalone on a placed entity: `ClaimSmartObject` needs only the subject entity (`SynapseAi.cpp:292-309`), nothing needs a `CSynapseAgent` until `MoveTo` | read | Play, Details active state | none needed; the full loop is Demo_AI |
| U27 | The packaged game applies the same records (`GameLevel::load` calls the shared applier before `afterInstantiate`) | ADD_COMPONENT.md 5.2 | `aver_package` the project, run the level | n/a |

The montage files also carry sections and notifies that no graph listens to; they are there for the Montage tab's simulator and for `<Montage>_Section_<Name>` / notify events if someone wires an `AN_OnEvent` entry to them.

## 10. Engine gaps found while writing this (for the freezer)

Each is a limit of the engine as it stands, worked around in the content, and worth a line in the other plans.

- **G1** No node attaches `CSynapseAgent` (F14), so a graph NPC cannot use `MoveTo` / `MoveToSmartObject`. The Patron is moved by the graph. A `Synapse Agent` entry in the Add Component menu (not in ADD_COMPONENT.md's scope), or `AN_BrainSet` attaching the agent, would let the tree own movement.
- **G2** `AN_OpenUiLayout` reads a working-directory-relative path (F16); a `.ocui` HUD is not reliable. No node sets a widget's placement, so one created widget is all a graph can show. The Gameplay demo's numbers go to the Print overlay instead.
- **G3** `Character.cs:244` normalises the wish vector: no walk-versus-run speed from `CharacterMove`.
- **G4** No node reads a body's rotation (`GetBodyPosition` only): the hinge door mesh is aimed with `LookAt`.
- **G5** No debug-draw node for casts; the demo parks ghost meshes.
- **G6** The Kenney clips are shipped without loop or root-motion flags and without travel (F3). A batch "set clip flags" tool, or re-export, would make motion matching show trajectory behaviour.
- **G7** Documentation drift: `docs/formats/FORMAT_SPECS.md` 9.2 says a track descriptor is 24 bytes, the reader consumes 20 (`modules/formats/src/OcAnim.cpp:622-631`); `docs/SYNAPSE.md` section 1 says level colliders ignore rotation, they do not (F8).
- **G8** A graph cannot find its own COMP child (F19); `FindEntity` by name makes two instances of one class ambiguous. A "Get Component By Name" node would remove the limit.
- **G9** `Meshes/cylinder.ocmesh` is an editor-preview mesh only (F6); a runtime cylinder (the capsule ghost's natural shape) does not exist.
- **G10** The Print overlay holds 12 lines (F11): per-tick prints are unusable, hence the event-driven prints everywhere.
- **G11** The material lookup only searches `Binaries/Materials`, `Content/Materials` and `Content/<name>` (`GameContent.cpp:1125-1129`); a feature folder can own a material only by referencing it with its full content-relative file name (inferred from the third candidate, not exercised).

## 11. Checks worth adding when someone can build

No engine code changes, so no test is required by the repo rules. Cheap safety nets for the integrator:

- **Parse every demo asset with its real reader** (a table-driven test in `tests/formats` or next to each parser's suite): `parseOcworld`, `parseOcseq`, `parseOcinput`, `parseOcRig`, `parseIkRig`, `parseRetarget`, `parseMotionAsset` (+ `valid()`), `parseMontage` (+ `valid()`), `parseStateTree`, EQS and smart-object parsers, `parseRagdollOverrides`, `OcGasParser.Parse` (through `Aver.Framework.Tests`), and `RetargetAsset::valid(src, dst)` against the two real skeleton files. The first line of each file names the reader.
- **Compile every graph**: in `scripting/csharp/Aver.Graph.Tests` add a test that loads each `Content/Demo/Scripts/*.ocgraph` with `OcGraphParser.Parse` and runs `NodeRowKit.CompileAndRun`-style `CompileEntryPoint` for every ENTRY (the pattern is `IkNodeTests.TestGoalChain`).
- **Smoke run** (flags per `sandbox/src/SandboxMain.cpp`: `--project`, `--open-level`, `--play-test`, `--frames`; use the headed rules the owner set): open each level for a few hundred frames and grep the log for the declared-class lines and for `[Graph]` errors.

## 12. Sources read

`docs/VISUAL_SCRIPTING.md`, `docs/AVER_NODE_NODES.md`, `docs/formats/FORMAT_SPECS.md` (3, 5, 8, 9, 10a, 11, 11d, 11g-i), `docs/ANIM_IK_RETARGET.md`, `docs/ANIM_MOTION_MATCHING.md`, `docs/ANIM_MONTAGES.md`,
`docs/GAMEPLAY_ABILITIES.md`, `docs/DATA_TABLES.md`, `docs/LOCALIZATION.md`, `docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md`, `docs/SYNAPSE.md`, `docs/SEQUENCER.md`, `docs/GAME_TIMERS_EVENTS.md`,
`docs/GAME_UI.md`, `docs/physics/{QUERIES,GRAPH_NODES,RAGDOLL,RAGDOLL_ANIMATION}.md`, `docs/plans/UE_FEATURE_PARITY_PLAN.md`, and the two sibling plans `ADD_COMPONENT.md`, `PLACE_CLASS.md`; the parsers and compilers cited inline; `test-content/` and `templates/FirstPerson`;
the PTTest files under `Content/Meshes`, `Content/Skeletons`, `Content/Maps`, `Content/Materials`, `Binaries/Materials`.

## 13. Reconciliation changes made by the freezer pass (2026-10-12)

What the review changed in this file, so a reader of the first draft can see the delta:

1. Section 3 rewritten: the draft said Add Component "could not replace the graphs" because a `PLACE` has no skeleton field. ADD_COMPONENT.md's `COMPONENT SkeletalMesh`
   record answers that; the five graph levels stay graph-driven (they need keys, abilities, movement), and a sixth level (6.9) uses the records.
2. `EF1` / `EF2` replaced by the real names; "Motion Matcher" menu label corrected to **Motion Matching** (record type `MotionMatcher`); "the placement must keep `name`
   because the sequencer binds by name" corrected: the spawner ignores the `name` token on class placements (SandboxLevelLoad.cpp:690); Demo_Sequencer only binds mesh placements and a light, so nothing in it depends on that.
3. Added: `Demo_Components.ocworld`, `KenneyLookStatic.ocrig`, README rows, acceptance rows, U22-U27.
4. Added to the README the two editor behaviours that change how the five graph levels feel: class graphs start at level load, and Stop destroys class actors until
   PLACE_CLASS Step 9 lands.
5. Section 5 now says what the implementer does (write the files) and what is the owner's (run the script, set the clip flag, pick the input scheme, reopen).
6. Not changed: every graph, every asset body and every node-table row of the first draft. Their mechanical checks were not repeated; the reviewer spot-checked the PLACE
   grammar, the `LIGHT` / `SKY` / `FOG` / `SUN` records, the `GAMEMODE` / `DEFAULTPAWN` path (SandboxPlay.cpp:341-361), the skeleton bone lists (file bytes), the built-in meshes
   (GameContent.cpp:253-280) and the material lookup (GameContent.cpp:1125-1129) against the tree at `f738c7a2`; all held.
