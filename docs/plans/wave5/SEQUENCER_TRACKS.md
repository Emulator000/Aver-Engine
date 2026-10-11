# Feature #27: Sequencer (property, event, audio, animation, visibility tracks; camera cuts; sub-sequences; tangent curves)

Design contract for the implementing agent. Parity plan: `docs/plans/UE_FEATURE_PARITY_PLAN.md` row 27, wave 5. Nothing here is built. Rules in force: upgrade the existing systems (level sequences in the `.ocworld`, `game::SequencePlayer`, `editor::SequenceEditor`, `AnimSystem`'s montage players), module boundaries hold (`docs/MODULES.md`), every C# capability gets graph nodes, no new third-party code, lean comments, D3D12/Vulkan parity (nothing here touches a GPU API; it writes components and cameras that both backends already consume). Patent check: done live (2026-10-11 search for cinematic sequencer / keyframe track / Hermite tangent / curve editor claims returned only product documentation, no patents). Timeline tracks, keyframes and cubic Hermite tangents are decades-old public art; nothing here is a known patented technique, so there is nothing to design around. Style matches `docs/plans/wave2/MONTAGES_ROOT_MOTION.md`, whose montages this feature plays.

## 0. Decisions in one screen

1. **Same container, more track kinds.** The level sequence stays `SEQUENCE / SEQTRACK / SEQKEY / ENDSEQUENCE` in the `.ocworld`. `OcSeqTrackKind` gains `Property, Event, Audio, Animation, Visibility, CameraCut, SubSequence` (appended; existing values keep their numbers). A level written without the new tracks is **byte-identical** to today; older builds skip the new tracks (their parser already ignores unknown track kinds and everything under them), so a file stays loadable, and re-saving it in an old build drops them (risk 1).
2. **Two families of track.** *Sampled* tracks (transform, camera, material, property, visibility, animation, cuts, sub-sequence) are pure functions of time: scrub, jump, reverse-jump and the render queue's `runAt` are exact. *Triggered* tracks (event, audio) fire on **forward crossings** of the clock only; a jump/scrub fires nothing.
3. **Three playback modes**: `Preview` (Animate mode: sampled tracks only, plus an opt-in audio audition), `Live` (Play, packaged game: everything), `Offline` (render queue: sampled tracks only, nothing triggered, nothing audible).
4. **Property tracks use the scene field registry** (`World::fieldId("CLight.intensityLux")`, `FieldDesc::offset`, `World::getComponent`), the same bytes `aver_scene_set_*` writes; no new scene API, no `Components.hpp` change. Float, Vec3, Quat, I32 and Bool fields; strings, I64, entity refs and Mat4 are refused.
5. **Animation tracks play through the montage system** as *driven* players: a new `MontageSystem::driveClip` / `driveMontage` puts a player at an exact position with an exact weight every evaluate, with no clock, no events and no auto-blend-out. Scrub-correct by construction, works over a `CAnimGraph` state machine (DefaultSlot is applied full-body after the pipeline, `ANIM_MONTAGES.md`) and over a bare `CAnimator`. `CAnimator` itself is never written.
6. **Evaluation is split in two phases around `AnimSystem::tick`.** `Pre` (before `tickAnimGraphs`): property, visibility, animation, event, audio, sub-sequence recursion. `Post` (where the sequence evaluates today, after the animation tick): transform, camera, material, cuts. This is what makes a driven pose land on the same frame it was posed (the render queue captures the frame it just posed; today's single post-tick evaluate would be one frame stale for animation).
7. **Every write is undoable by a base store**, owned by the player: first write of an (entity, component, offset) records the authored bytes; `restore()` puts them back (Animate leave, Play stop, save, a track removed). The editor's `bases_` (transform only) folds into it. Play stop's world snapshot records only transform and visibility (`SandboxPlay.cpp:99-121`), so a property write would otherwise survive Play.
8. **Several sequences per level are finally real.** The editor keeps only the first and drops the rest on save today (`SequenceEditor.cpp:78-80`). It will hold all of them (a current one plus an `others_` list), because sub-sequences and a cutscene-per-sequence workflow need that, and because the drop is silent data loss. At runtime a **`SequenceDirector`** owns one `SequencePlayer` per sequence; sequences with `autoplay 1` start with the session (a single-sequence level behaves exactly as now).
9. **Sub-sequence** = a section on a `SubSequence` track naming another level sequence (`name`) or a standalone **`.ocseq`** file (new tiny format: the same records, actors bound **by name**). Time = `offset + (t - start) * scale`, optional loop, optional "its camera is live while it plays".
10. **Camera cuts**: camera tracks become *named* and many per sequence; a `CameraCut` track's keys pick one by name with an optional blend time. A camera track may instead be bound to an actor (pose = that actor's world transform), so a camera can ride a crane that a transform track moves.
11. **Tangents**: a fourth interpolation `Cubic` with optional per-key arrive/leave slopes per channel (cubic Hermite); `Smooth` stays exactly the current Catmull-Rom so existing sequences render identically. A **Curves** view in the Sequencer window edits keys and tangent handles; every curve it draws comes from the same sampler the runtime uses.
12. **No `scene_abi.h`, `Components.hpp`, `framework_abi.h`, or `audio_abi.h` change.** The control surface is a new header `framework_sequence_abi.h` with its own version (the `framework_anim_abi.h` arrangement); audio and events reach the engine through host hooks, so `Aver.Runtime.Game.Core` gains no new module edge.

## 1. What exists today

| Piece | Where | State |
|---|---|---|
| `OcSeqTrackKind { Transform, Camera, Material }`, `OcSeqInterp { Smooth, Linear, Step }`, `OcSeqKey { t, v[9], interp }`, `OcSeqTrack { kind, target, keys }`, `OcSequence { name, length, fps, loop, autoplay, camera, tracks }` | `modules/formats/include/aver/formats/OcWorld.hpp:497-526`; `OcWorldData::sequences` at `:801` | Complete for three kinds. No string payload, no section length, no tangents, no track name. |
| Parse: `SEQUENCE` `OcWorld.cpp:723`, `SEQTRACK` `:736` (unknown kind skipped, camera target forced `-1` at `:746`), `SEQKEY` `:750` (reads exactly `seqKeyValueCount` values, 9/5/4), sort `:866`; write `:1307-1347` (a transform/material track whose target is out of range is dropped on write). Number spelling `num()` = `%.6g` (`:24`) | `modules/formats/src/OcWorld.cpp` | Values after the fixed count are ignored: new trailing tokens are forward compatible. `%.6g` loses a millisecond past t = 1000 s. |
| Spec | `docs/formats/FORMAT_SPECS.md:973-1010` | |
| Samplers `sampleSeqTransform` `LevelSequence.cpp:186`, `sampleSeqCamera` `:204` (centripetal Catmull-Rom position + squad on quaternions), `sampleSeqMaterial` `:240`; `findSeg` `:30`; `sampleChannel` `:57` (Smooth = uniform Catmull-Rom, not time-weighted) | `Runtime/src/LevelSequence.cpp` | Correct and tested (`tests/game/src/LevelSequenceTest.cpp`). No tangents, no per-channel public sampler. |
| `SequencePlayer`: `setSequence/bind(placementEntities)/play/pause/setTime/advance/evaluate/camera/emissiveScale` | `Runtime/include/aver/game/LevelSequence.hpp:53-120`; `.cpp:287` advance, `:298` evaluate | One camera track, first wins (`hasCamera_`, `.cpp:309`). Transform written with `world.setLocalTransform`. Binds by placement index only. |
| Runtime host: one `sequencePlayer_`, built from `sequences.front()` (`GameApp.cpp:1013-1024`), `seedMovable` (`:1937`), `advance`+`evaluate` after `animSystem().tick` (`:2098-2101`), camera (`:2146-2152`), emissive sink (`:588`, `:2245`) | `Runtime/src/GameApp.cpp`, member `GameApp.hpp:300` | Plays the first sequence only. |
| Editor: `SequenceEditor` (1249 lines): `load` keeps the first sequence (`SequenceEditor.cpp:73-105`), one camera track (`:88-92`), tracks hold `scene::Entity` in `target` while editing and `save` converts through `slotOf` (`:107`), `bases_` for transforms (`:165-197`), `syncPlayer` dense-binds a copy to the player (`:365`), `addTrack` (`:498`), Mode panel (`:728`), Sequencer window with ruler, zoom, lanes, key select/drag/interp menu (`:888`) | `sandbox/src/SequenceEditor.{hpp,cpp}` | Dope sheet only: **no curve view**, no value display beyond text. Undo = whole-sequence snapshot (`SnapshotUndo<OcSequence>`). |
| Host glue: `sequenceHost()` (`SandboxPanels.cpp:520-545`, `canKey` excludes lights, decals and prefab members), load (`SandboxLevelLoad.cpp:451-469`, streamed levels keep `target >= 0` tracks aside), save (`SandboxLevelEdit.cpp:707`; `restoreBases()` before the save at `:477`), Play (`SandboxPlay.cpp:245-250` restore + `beginPlay`, `:697` `endPlay`, `:905-914` `tickSequence`), frame order (`SandboxApp.cpp:2222-2230`: `tickAnimGraphs`, ragdoll, `animSystem().tick`, **then** `tickSequence`) | | Sequence evaluates after the animation tick in both hosts. |
| Render queue: `runAt(seconds)` poses the sequence at any time (`SequenceEditor.cpp:355`); the job's `sequence` key is a name check against the level's only sequence (`SandboxSequenceRun.cpp:385`); `hasCameraKeys()` gates `--sequence-play` and jobs (`:85`, `:207`, `:375`); `renderJobStep` runs from `tickSequence` (`SandboxPlay.cpp:906`, i.e. after the animation tick); capture is requested the frame after a pose (`serviceRenderCapture`, `hold`) | `docs/RENDER_QUEUE.md`, `sandbox/src/SandboxSequenceRun.cpp` | Works for camera/transform/material. An animation driven after the tick would be captured one frame stale. |
| Scene fields: `World::fieldId/field/getComponent/touchLocal` (`World.hpp:87-93`, `:102`), the ABI setters funnel through `fieldAddr` + `noteWrite` (`SceneAbi.cpp:57-84`: bytes + `touchLocal` for CLocal, nothing else) | `modules/scene` | Script-declared components register in the same table. String fields live in a DLL-local intern pool (`SceneAbi.cpp:90`), so C++ cannot set them. |
| Animation: montages (`MontageSystem`, `MontagePlayer`: `bind/start/stop/advance/evaluate`, no way to place a player at a position) | `modules/anim/include/aver/anim/Montage.hpp`, `modules/anim.scene/include/aver/anim/AnimMontages.hpp` | `play` requires a skeleton, adds `CAnimator`, DefaultSlot is applied full-body after the pipeline (`AnimSystem.cpp:451-466`). `CAnimator.speed 0` reads as 1 (`AnimSystem.cpp:335`), so a clock cannot be frozen by speed; `kAnimatorPaused` freezes it but a state machine (`poseSrc_`) outranks the clip (`:417`). |
| Events: bus `aver_fw_event_push_* / post / dispatch(name, sender, target)` (`framework_timers_abi.h:74-117`), `AN_OnEvent` entries receive bus events by name (`docs/GAME_TIMERS_EVENTS.md:129`); entity graph events `ScriptHost::graphFire(entity, name)` (`GameApp.cpp:1356`, `SandboxPlay.cpp:1014`) | | The two delivery routes event keys will use. |
| Audio: `aver_audio_load/play/play_at/stop/fade_voice/set_voice_position` (`audio_abi.h`); `Aver.Runtime.Game` links `Aver.Audio.Abi` and defines `AVER_WITH_AUDIO_ABI` (`Runtime/CMakeLists.txt:217-219`), Sandbox likewise (`sandbox/CMakeLists.txt:201-203`) | | No seek/offset on a voice (risk 3). |
| Visibility: `CMeshRenderer.flags` bit 0 (`Components.hpp:70-75`); `OcWorldPlacement::visible` is the authored value | | |
| Camera FOV is a constant 60 degrees at every view build (`SandboxRender.cpp:388,1060,1207,1341`, `GameApp.cpp:1647`) | | No FOV track (risk 5). |

Missing: every new track kind, multiple sequences in the editor, sections, sub-sequences, named/multiple cameras, cuts, tangents, a curve view, a base store for non-transform writes, a pre-animation phase, a control ABI, graph nodes, an external sequence file.

## 2. Frame order and data flow (contract)

```
GameApp / SandboxApp frame (one director per host):
  tickGameplay                      C# may call Sequences.Play/Pause/Stop/SetTime (ABI, section 6)
  director.advance(dt)              one clock step per player (not while Play is paused); queues crossings
  director.evaluate(Pre)            property, visibility, animation (driven montages), event, audio, sub-sequence Pre
  tickAnimGraphs(dt); ragdoll.tick(dt)
  animSystem().tick(dt)             montages_.tick: driven players are NOT advanced, only evaluated at their placed position
  director.evaluate(Post)           transform, material, camera tracks; cuts choose/blend the live camera; sub-sequence Post
  World::flush                      (as today)
```
- The editor's frame (`SandboxApp.cpp:2222-2230`) splits `tickSequence` into `tickSequencePre(e, dt)` (before `tickAnimGraphs`: `sequenceRunStep(e)` and so `renderJobStep`, then `seqEditor_.tickPre(ctx)`) and `tickSequencePost()` (where `tickSequence` is now). Because `runAt` and the pre-phase happen before the animation tick, a pose chosen by the render queue is on screen in the same engine frame, and `hold`/`settleFrames` need no change.
- Paused Play (`objectsHeld`): `advance` is skipped, `evaluate` still runs (idempotent), nothing triggered re-fires.
- Events posted from the Pre phase go through `aver_fw_event_post` and are delivered at the next frame's `aver_fw_events_flush` (one frame of latency, accepted; keeps handlers out of the middle of the evaluate).
- Conflicts: two tracks writing the same thing resolve **last track in file order wins**. Post-phase transform writes beat Pre-phase property writes to `CLocal.*`. A sub-sequence's tracks apply at the sub-sequence track's position.

## 3. Data

### 3.1 Records

```
SEQUENCE name Intro length 20 fps 30 loop 0 autoplay 1 camera 1        # unchanged
  SEQTRACK transform target 3                                            # unchanged
    SEQKEY 0 smooth 0 0 100 0 0 0 1 1 1
    SEQKEY 4.5 cubic 400 0 100 90 0 0 1 1 1 tan 0 0 80 80 0 0 0 0 0 0 0 0 0 0 0 0 0 0   # arrive,leave per channel
  SEQTRACK camera name Wide                                              # named camera (free keys)
    SEQKEY 0 smooth -500 0 150 0 -10
  SEQTRACK camera name Crane target 7                                    # actor camera: pose = actor's world transform, no keys
  SEQTRACK cut                                                           # at most one
    SEQKEY 0 step 0 text Wide
    SEQKEY 6 step 1.5 text Crane                                         # v0 = blend seconds INTO this camera
  SEQTRACK property bind Lamp%201 prop CLight.intensityLux
    SEQKEY 0 linear 2000
    SEQKEY 3 cubic 9000
  SEQTRACK property target 5 prop CLight.colour
    SEQKEY 0 smooth 1 0.8 0.6
  SEQTRACK visibility target 9
    SEQKEY 2 step 0 1                                                    # v0 visible(0/1), v1 include children (default 1)
  SEQTRACK event                                                         # no target = bus broadcast
    SEQKEY 4 step 0 1 0.5 text Door_Open arg lever_a                     # v0 route(0 bus,1 graph,2 both), v1 n numeric args, v2.. the args
  SEQTRACK audio target 9                                                # target = positional source, none = flat
    SEQKEY 1 step 0 0 0 0 0 0 1 1 dur 3.2 text Audio/door_creak.wav      # see 3.3 for channels
  SEQTRACK animation target 12
    SEQKEY 2 step 0 0.2 0.2 0 0 0 1 1 dur 4 text Anims/wave.ocanim
  SEQTRACK sub name Explosion
    SEQKEY 10 step 0 0 0 0 0 0 1 dur 3 text Explosion                    # level sequence "Explosion", or Content/Seq/boom.ocseq
ENDSEQUENCE
```
Grammar (all case-insensitive keywords, percent-encoded strings, one record per line):
- `SEQTRACK <kind> [target <n>] [bind <name>] [name <name>] [prop <Component.field>] [mute 0|1]`. `kind` = `transform camera material property event audio animation visibility cut sub`. `target` is a placement index (renumbered on write exactly as today); `bind` names an actor instead (placement `name`, else `World::find`, so lights named in their `LIGHT` record, class actors and other non-placements are addressable); both present: `bind` wins and `target` is a fallback. A **camera** track keeps `target` now (actor camera); old files have none. `name` is the camera's name (default `Camera`, `Camera 2` ... assigned by the editor when missing) and any track's display name. `mute 1` skips the track. An unknown kind and every `SEQKEY` under it is ignored (as today).
- `SEQKEY <t> <smooth|linear|step|cubic> <numbers...> [dur <s>] [text <str>] [arg <str>] [tan <numbers...>]`. After the interpolation word the parser reads a **numeric run** (up to 9 values, missing ones keep the `OcSeqKey` defaults) until the first non-number token, then reads the tags. Existing three kinds write exactly 9/5/4 values and no tags unless one is set. Old parsers read the first 9/5/4 values and ignore the rest of the line, so `cubic` (unknown word -> Smooth), tags and extra values degrade without breaking a load.
- Number spelling: values and times keep `%.6g`; if that is off by more than `1e-6 * max(1, |v|)` the writer uses `%.9g` (so `t = 1234.567` survives; every value an existing file can contain still writes identically). `dur` and `tan` always use that rule.
- `SEQUENCE`'s own line is unchanged. A sequence with only non-placement tracks is kept (the editor's "no tracks writes nothing" rule is about zero tracks).

```cpp
// modules/formats/include/aver/formats/OcWorld.hpp (structs stay here)
enum class OcSeqTrackKind : u8 { Transform, Camera, Material, Property, Event, Audio, Animation, Visibility, CameraCut, SubSequence };
enum class OcSeqInterp : u8 { Smooth, Linear, Step, Cubic };
struct OcSeqKey {
    f64 t = 0;
    f64 v[9] = {0,0,0,0,0,0,1,1,1};   // per-kind meaning in 3.3; defaults chosen so a missing value is neutral
    OcSeqInterp interp = OcSeqInterp::Smooth;
    f64 dur = 0;                        // section length (audio, animation, sub-sequence); 0 for point keys
    std::string text, arg;              // event name / asset path / sequence name / camera name;  event string argument
    std::vector<f64> tan;               // Cubic only: [arrive0, leave0, arrive1, leave1, ...] slopes in value/second; empty = automatic
};
struct OcSeqTrack {
    OcSeqTrackKind kind = OcSeqTrackKind::Transform;
    i32 target = -1;                    // placement index (editor: scene::Entity, as today); camera: actor camera
    std::string bind, name, property;
    bool mute = false;
    std::vector<OcSeqKey> keys;         // sorted by t
};
```
Sections (`dur > 0`) and point keys share `keys`, so the existing select/drag/sort code and the "keys sorted by t" invariant carry over. Sections on one track must not overlap: the editor trims on drop and refuses overlapping drags; if a file overlaps, the **later start wins** and the earlier section is cut there. `OcSequence` gains nothing.

### 3.2 Shared record code and the `.ocseq` file

New `modules/formats/{include/aver/formats/OcSeq.hpp, src/OcSeq.cpp}` (the `OcLevelInst.cpp` arrangement: `detail/TextEscape.hpp`, `detail/TextScan.hpp`):
```cpp
struct SeqParseState { i32 seqOpen = -1, trackOpen = -1; };
// True when the line was a SEQUENCE/SEQTRACK/SEQKEY/ENDSEQUENCE record (consumed).
bool parseSeqRecord(std::string_view key, const std::vector<std::string_view>& tokens, std::vector<OcSequence>& out, SeqParseState& st);
void sortSeqKeys(std::vector<OcSequence>& seqs);                         // stable by t
// remapTarget: placement index -> emitted index, or -1 to drop the track (OcWorld's emitIndex); null = write targets as they are.
void appendSeqRecords(std::string& out, const std::vector<OcSequence>& seqs, const std::function<i32(i32)>& remapTarget);

struct OcSeqFile { std::vector<OcSequence> sequences; };                  // "OCSEQ 1" + records
bool parseOcseq(std::string_view text, OcSeqFile& out, std::string* err);
std::string writeOcseq(const OcSeqFile& f);
const char* seqTrackKindName(OcSeqTrackKind);                            // for the editor and logs
```
`OcWorld.cpp` replaces its inline branches (`:723-763`, `:866-870`, `:1307-1347`) by calls into these, nothing else changes. `.ocseq` rules: header `OCSEQ 1` (a newer version is refused); same records; **`target` is ignored on read and never written**; every non-camera, non-event, non-cut, non-sub track must carry `bind` (a track without one is dropped on load with one warning). Camera actor tracks use `bind`. It is ordinary Content (no registration: extension-agnostic index, id `fnv1a64(path)`).

### 3.3 Track kinds: key meaning (normative)

`v[]` defaults make a missing value neutral (rate and weight sit in slots 6-8 whose default is 1).

| Kind | Binding | Key | `v[]` | `text` / `arg` |
|---|---|---|---|---|
| Transform, Material | target/bind | point | as today (9 / 4) | |
| Camera | none (free) or target/bind (actor) | point (free only) | as today (5): x y z yaw pitch | |
| Property | target/bind + `prop` | point | channels = field arity: F32 1, Vec3 3 (colours too), Quat 4 (raw x y z w), I32/Bool 1 (forced Step, rounded) | |
| Visibility | target/bind | point, step | `v0` visible 0/1, `v1` apply to descendants (default 1) | |
| Event | optional target/bind (target filter) | point | `v0` route 0 bus / 1 graph / 2 both, `v1` n numeric args (0..4), `v2..v5` args (pushed as floats) | `text` event name, `arg` optional string argument |
| Audio | optional target/bind (positional) | section | `v0` fade in s, `v1` fade out s, `v2` loop 0/1, `v3` spatial 0/1 (needs a binding), `v4` inner cm (0 = 300), `v5` outer cm (0 = 3000), `v6` volume (1), `v7` pitch (1) | `text` content-relative sound path |
| Animation | target/bind, skeletal entity | section | `v0` start offset (clip s / montage s), `v1` blend in s, `v2` blend out s, `v3` loop (clip only), `v4` after: 0 release, 1 hold last pose, `v6` rate (1), `v7` weight (1) | `text` `.ocanim` or `.ocmontage` path |
| CameraCut | none | point, step | `v0` blend seconds into this camera (0 = hard cut) | `text` camera track name |
| SubSequence | none | section | `v0` start offset in the sub-sequence s, `v1` loop 0/1, `v2` its camera is live in this section 0/1, `v6` time scale (1, > 0) | `text` level sequence name, or `.ocseq` path (optionally `path#Name`) |

### 3.4 Curves and tangents

- `Smooth` = unchanged (`LevelSequence.cpp:57-64`, uniform Catmull-Rom; rotation slerp with a smoothstep; camera centripetal + squad). `Linear`, `Step` unchanged.
- `Cubic` (new): cubic Hermite per channel over the segment `k1 -> k2` (`k1.interp` decides, as today): `p(u) = h00 p1 + h10 dt m1 + h01 p2 + h11 dt m2`, `dt = t2 - t1`, `u = (t - t1)/dt`. `m1` = leave slope of `k1`, `m2` = arrive slope of `k2`, channel `c`: `k.tan[2c+1]` / `k.tan[2c]`. **Empty `tan` = automatic**: the non-uniform finite difference `(p_next - p_prev) / (t_next - t_prev)` (one-sided at the ends), arrive = leave. A key with `tan` has it for all its channels (editing one channel's handle materialises the others' automatic values first). Tangent presets are edit-time operations that fill `tan`: **Linear** (slope toward the neighbour on that side), **Flat** (0), **Auto** (clear). *Break* = arrive != leave.
- Channels under `Cubic`: Transform and Camera are all-Hermite on the authored numbers (position, scale, **and the Euler degrees / yaw-pitch**, no quaternion path, no short-arc: unwrap yaw yourself, e.g. 350 -> 370). Material: r g b intensity. Property: per channel; Quat fields treat Cubic as Smooth. A segment may mix: a `Smooth` key followed by a `Cubic` key uses the first key's mode for the segment (as today).
- Public samplers (pure, used by the player AND the editor's curve drawing):
```cpp
// Runtime/include/aver/game/LevelSequence.hpp additions
u32  seqChannelCount(const fmt::OcSeqTrack&, u32 propertyArity = 1);        // transform 9, camera 5, material 4, property arity, others 0
f64  sampleSeqChannel(const fmt::OcSeqTrack&, u32 channel, f64 t);          // the curve as the player evaluates it (rotation channels as authored Euler)
void seqAutoTangent(const std::vector<fmt::OcSeqKey>&, usize key, u32 channel, f64& arrive, f64& leave);
bool sampleSeqProperty(const fmt::OcSeqTrack&, f64 t, bool quat, bool stepOnly, f64 out[4]);
// Sections and points
const fmt::OcSeqKey* seqActiveSection(const fmt::OcSeqTrack&, f64 t, bool holdAfter, f64& local);   // later start wins; local = t - start
f32  seqSectionWeight(const fmt::OcSeqKey&, f64 local, f64 fadeIn, f64 fadeOut);                      // smoothstep ramps, 1 inside
usize seqCrossedKeys(const fmt::OcSeqTrack&, f64 t0, f64 t1, bool inclusiveStart, f64 length, bool loop, std::vector<u32>& out);   // key indices passed, wrap-aware
bool sampleSeqCut(const fmt::OcSeqTrack& cuts, f64 t, std::string& camera, std::string& prevCamera, f32& blendAlpha);
```

### 3.5 Properties of the model the implementer must keep

- Property key **base value** semantics are not stored: before the first key the first key holds (as every track); the authored value returns only when the track is removed/muted or the base store restores.
- Visibility before its first key shows the authored value (no write until `t >= first key`).
- Animation/audio/sub outside every section: nothing driven, nothing playing (animation `after = 1` holds the last pose until another section starts or the sequence stops/wraps).
- Sub-sequence depth is limited to 4 and a name already on the stack is refused (one warning, section skipped).

## 4. Runtime (`Runtime/`, `Aver.Runtime.Game.Core`)

### 4.1 `SequencePlayer` changes (`LevelSequence.hpp/.cpp`)

```cpp
enum class SeqMode : u8 { Preview, Live, Offline };
enum class SeqPhase : u8 { Pre, Post };

struct SeqBindings {                                     // what the host knows about the level's actors
    std::vector<scene::Entity> byPlacement;              // placement index -> entity (as bind() takes today)
    std::unordered_map<std::string, scene::Entity> byName;   // placement `name` (first wins), light names, class actors
    std::function<scene::Entity(const std::string&)> find;   // fallback; hosts pass World::find
    std::unordered_map<std::string, scene::Entity> overrides; // runtime Bind (section 6): wins over everything
};
struct SeqVoice { f32 volume = 1, pitch = 1; bool loop = false, spatial = false; Vec3 pos{0,0,0}; f32 innerCm = 300, outerCm = 3000, fadeIn = 0; };
struct SeqEventCall { std::string name, arg; scene::Entity sender = scene::kInvalidEntity, target = scene::kInvalidEntity; u8 route = 0, argc = 0; f32 args[4] = {}; };
struct SequenceHooks {                                   // installed by the host; null = that track kind is silent
    std::function<i32(const std::string& contentPath)> loadSound;            // handle or 0 (aver_audio_load is path-cached)
    std::function<i32(i32 sound, const SeqVoice&)> playVoice;                // voice or 0
    std::function<void(i32 voice, const Vec3* pos, f32 volume)> updateVoice;
    std::function<void(i32 voice, f32 fadeSeconds)> stopVoice;
    std::function<void(const SeqEventCall&)> fireEvent;
    std::function<std::string(const std::string& contentPath)> readFile;     // for .ocseq; null = read relative to the working dir
};

class SeqLibrary {                                       // name -> sequence for sub-sequences (level sequences + loaded .ocseq, cached by path)
public:
    void setLevel(const std::vector<fmt::OcSequence>*);  // borrowed; editor passes model_ + others_
    const fmt::OcSequence* find(const std::string& nameOrPath, const SequenceHooks&);   // null (cached) on failure
    void clear();
};
```
`SequencePlayer` (one sequence) additions: `setLibrary(SeqLibrary*)`, `setBindings(SeqBindings)` (the existing `bind(vector)` stays as a thin wrapper), `setHooks(const SequenceHooks*)`, `setMode(SeqMode)`, `setRate(f64 >= 0)` / `rate()`, `setBaseStore(SeqBaseStore*)`, `advance(dt)` as today plus: queue the crossed span (`prev, cur`, loop-aware) for the triggered tracks, and an `onFinished` flag read once by the director; `setTime` marks a **discontinuity** (next advance fires nothing for the skipped span; the first advance after `play()` from 0 is inclusive of t = 0); `evaluate(World&, SeqPhase)` (the old one-arg `evaluate` = Post, kept for the test and for compatibility); `camera(SeqCameraPose&)` now answers the **cut-resolved** pose; `cameraName()`; `boundEntities()` recurses into sub-sequences and covers every track kind that has a binding; `restore(World&)`.

Per-kind evaluate (all skip `mute`, unresolved bindings, dead entities):

- **Property** (Pre): resolve `world.fieldId(track.property)` once per (track, world revision) and cache; unknown field, read-only field, unsupported kind -> one warning per track, skipped. Entity lacking the component -> skipped (no component is ever added). `sampleSeqProperty` -> write `FieldDesc::offset` bytes with the field kind (F32/Vec3 floats; Quat normalised; I32/Bool rounded) through `World::getComponent`; `touchLocal` when the component is `CLocal`. Base captured on first write.
- **Visibility** (Pre): value from the last key with `t <= now`; sets bit 0 of `CMeshRenderer.flags` on the entity and, if `v1`, its descendants (CHierarchy walk, cached). Base = the flags word.
- **Animation** (Pre): `seqActiveSection`; local clip/montage position `pos = v0 + local * v6` (clip: wrap by clip duration when `v3` loop, else clamp to the end; montage: clamp to its length); weight = `seqSectionWeight(key, local, v1, v2) * v7`; call `anim::animSystem().montages().driveClip(...)` / `driveMontage(...)` (section 5). Leaving a section (or `after == 0` past its end, mute, track removed, `restore`) calls `releaseDriven`. Offline and Preview use exactly the same calls (they are what makes scrubbing exact). Entities without a skeleton are skipped with one warning (object animation clips are covered by transform tracks).
- **Event** (Pre, Live only): `seqCrossedKeys` over the queued span -> `hooks.fireEvent` in key order. Route 0 bus (`aver_fw_event_push_* ; aver_fw_event_post(name, sender = 0, target)`: with a binding the event is addressed to that entity so only its `AN_OnEvent` / C# subscribers owning it run; without, broadcast); route 1 `graphFire(entity, name)` on the bound entity; 2 both. Numeric args are pushed as floats in order, then the string `arg` last. Lifecycle events from the director: `<Sequence>_Started`, `<Sequence>_Finished` (bus, broadcast).
- **Audio** (Pre, Live; Preview only when the editor's Audition toggle is on and the clock is playing forward): a section starts when the clock crosses `start` forward (`loadSound(text)` then `playVoice`; `spatial` with a binding uses the entity's world position, refreshed every tick through `updateVoice`; volume ramps by `seqSectionWeight(fadeIn, fadeOut)` applied as `volume * ramp`); it stops at `start + dur` (`stopVoice(fadeOut)`), on pause, on a discontinuity, on sequence stop/wrap and in `restore`. **A section entered mid-way (jump, resume, play from t) stays silent**: the audio ABI has no seek (risk 3). `dur == 0` plays the sound once and lets it finish.
- **Transform / Material** (Post): as today (`setLocalTransform`, emissive cache) but base-store backed.
- **Camera / CameraCut** (Post): every camera track is sampled (free: `sampleSeqCamera`, now `Cubic` aware; actor: world matrix -> yaw/pitch with `game::cameraForward`'s inverse). The live camera: with a cut track, `sampleSeqCut` names the camera and the previous one + blend alpha (`smoothstep((t - key.t)/blend)`): position lerp, yaw/pitch shortest-arc lerp; before the first cut key the first camera track; without a cut track the first camera track (today). A sub-sequence section with `v2` on overrides the parent's camera for its span (that sub-sequence's own resolved camera).
- **SubSequence** (Pre and Post, recursion): `local = v0 + (t - start) * v6`; `v1` wraps by the sub-sequence's length, else clamps; the sub's own `SequencePlayer` (created lazily per (track, section), same world, same base store, same bindings, parent mode) is positioned with `setTime(local)` (a discontinuity unless the parent advanced from the previous frame, tracked by `prevLocal`) so its event/audio crossings are computed in its own time. `restore()` recurses.

`SeqBaseStore` (new, `LevelSequence.hpp`): `struct Entry { scene::Entity e; u32 comp; u16 off; u16 size; u8 bytes[16]; bool added; }`; `captureProperty(world, e, desc)` (first write only), `captureComponentAdded(...)`, `restore(World&)`, `release(entity)`, `releaseUndriven(drivenSet)`; plus the transform entry the editor owned (`bases_`). It stores only entities that still exist at restore (stale handles dropped). One store per director; the editor's single-sequence `player_` owns its own. Muting a track or deleting one (editor `releaseUndrivenBases` generalised) calls `release` for what no remaining track drives.

### 4.2 `SequenceDirector` (new `SequenceDirector.hpp/.cpp`)

```cpp
class SequenceDirector {
public:
    void setSequences(std::vector<fmt::OcSequence> seqs);       // copies; keeps player state of sequences whose name survives
    void setBindings(SeqBindings b);  void setHooks(SequenceHooks h);  void setMode(SeqMode m);
    i32  find(u64 nameHash) const;                               // -1; name = fnv1a64 of the sequence name
    // Control (what the ABI provider and the host call). All return false for an unknown sequence.
    bool play(i32 seq, f64 startTime /*<0: continue; stopped -> 0*/, f64 rate /*0 keeps*/);
    bool pause(i32 seq);  bool stop(i32 seq /*-1 all*/);  bool setTime(i32 seq, f64 t);  bool setRate(i32 seq, f64 r);
    bool state(i32 seq, f64& time, f64& length, f64& rate, bool& playing) const;
    bool bindOverride(i32 seq, u64 nameHash, scene::Entity e);  // replaces the `bind` name's entity for that sequence (and its subs); 0 entity clears
    void startAutoplay();                                        // every sequence with autoplay 1, from 0, at session start
    void advance(f64 dt);  void evaluate(scene::World&, SeqPhase);
    bool camera(SeqCameraPose& out) const;                      // the live camera: most recently started playing sequence with `camera 1` that has one
    bool emissiveScale(scene::Entity e, f32 out[3]) const;
    std::vector<scene::Entity> boundEntities() const;           // for PlayMobility::seedMovable (transform-driven ones)
    bool empty() const;  void restore(scene::World&);  void clear();
};
```
`stop(seq)` = pause, time 0, `restore` of what only that sequence drove, stop its voices. Finishing a non-looping sequence raises `<Name>_Finished` once and leaves the final pose in place (as the camera/transform does today). `play` of a sequence that is a sub-sequence target is allowed (it just also plays standalone).

### 4.3 Host helpers (`Runtime/include/aver/game/SequenceHost.hpp`, header-only inline, the `LevelLights.hpp` arrangement, so each host TU supplies its own `AVER_WITH_AUDIO_ABI` / scripting definitions)

- `SequenceHooks makeSequenceHooks(const std::string& contentDir, std::function<bool(i32, const std::string&)> graphFire)`: `loadSound` = `aver_audio_load(contentDir/path)`; `playVoice` = `aver_audio_play` / `aver_audio_play_at` (bus SFX) then `aver_audio_fade_voice` for the fade-in; `updateVoice` = `aver_audio_set_voice_position/volume`; `stopVoice` = fade out with `stopWhenDone` or `aver_audio_stop`; `fireEvent` = the `aver_fw_event_*` push/post sequence (inside `#if AVER_MODULE_FRAMEWORK`) and `graphFire` for route 1. Every body compiles to nothing without its macro, so a scene-only build links.
- `i32 sequenceProvider(i32 op, i32 seqArg..., void* user)` for the ABI relay (section 6), `user` = the `SequenceDirector*`. Declared here so the framework never links the runtime.
- `SeqBindings makeLevelBindings(const fmt::OcWorldData&, const std::vector<scene::Entity>& byPlacement, const std::vector<scene::Entity>& lights)`: fills `byName` from `OcWorldPlacement::name` (non-empty, first wins, a duplicate is logged once) and the light names (`spawnLevelLights` returns entities in record order, `LevelLights.hpp:106`; a refused record shifts the tail, accepted: it fails only on entity exhaustion).

## 5. Anim-side additions (small; `Montage.*` and `AnimMontages.*` are this feature's files in wave 5)

`modules/anim` `Montage.hpp/.cpp`:
```cpp
// Places the player at `position` montage seconds with weight `w`, state Hold, no clock, no events, no root motion,
// no auto blend-out. Sections/notifies are not traversed. Safe to call every tick.
void MontagePlayer::drive(f32 position, f32 w);
bool MontagePlayer::driven() const;
```
`modules/anim.scene` `AnimMontages.hpp/.cpp` (`MontageSystem`):
```cpp
// Driven players: owner is the sequencer. Returns false for an entity without a skeleton or an unloadable asset.
bool driveMontage(AnimSystem&, scene::World&, scene::Entity e, u64 montageId, f32 position, f32 weight, u64 owner);
// Plays clip `clipId` (an .ocanim ObjectId) as a synthesised one-segment montage {segment [0,-1], rate 1, blend 0, DefaultSlot},
// cached by clip id, at `clipTime` clip seconds.
bool driveClip(AnimSystem&, scene::World&, scene::Entity e, u64 clipId, f32 clipTime, f32 weight, u64 owner);
void releaseDriven(scene::Entity e, u64 owner, u64 assetId /*0 = all of this owner's*/);   // silent: no Ended, no notifies
```
- `tick`: a driven player is **not advanced** (`MontageSystem::tick` skips `advance`, only counts a `staleTicks` that `drive*` zeroes); more than 2 stale ticks (the sequencer died without releasing) drops it silently.
- A driven player joins its slot like any other: `evaluateSlot` blends by weight, newest wins coverage rules unchanged. Sequencer players use the montage's authored slot (a synthesised clip montage = DefaultSlot).
- `driveMontage/driveClip` add `CAnimator` if absent (`play` does the same); the sequencer's base store records "added" so `restore` removes it. They never write `CAnimator.clip/time/flags`.
- Cost: one hash probe per driven entity per tick; nothing when no sequence drives animation (`players_` empty path is today's).

## 6. C ABI: `modules/framework/include/aver/framework/framework_sequence_abi.h` + `src/SequenceAbi.cpp`

Conventions of `framework_anim_abi.h`: `int32_t/int64_t/float` only, 1 success / 0 refusal, own version, provider relay (Aver.Framework does not link the runtime), `AVER_FW_ABI_VERSION_MINOR` untouched, `aver_fw_*` hashes are `fnv1a64` of the name (`Assets.ObjectIdOf` in C#).
```c
#define AVER_FW_SEQUENCE_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_sequence_abi_version(void);

#define AVER_FW_SEQ_OP_PLAY      1   /* h0 sequence, f0 start seconds (<0 continue, a stopped one starts at 0), f1 rate (0 keeps) */
#define AVER_FW_SEQ_OP_PAUSE     2   /* h0 sequence */
#define AVER_FW_SEQ_OP_STOP      3   /* h0 sequence (0 = every sequence): pause, time 0, restore, stop voices */
#define AVER_FW_SEQ_OP_SET_TIME  4   /* h0 sequence, f0 seconds (a jump: fires nothing for the skipped span) */
#define AVER_FW_SEQ_OP_SET_RATE  5   /* h0 sequence, f0 rate >= 0 */
#define AVER_FW_SEQ_OP_PLAYING   6   /* h0 sequence (0 = any): 1 when playing */
#define AVER_FW_SEQ_OP_STATE     7   /* h0 sequence; outF[0..2] = time, length, rate; outI[0] = playing */
#define AVER_FW_SEQ_OP_BIND      8   /* h0 sequence, h1 binding-name hash, entity = the actor (0 clears the override) */

typedef int32_t (AVER_FW_CALL* aver_fw_sequence_fn)(int32_t op, int32_t entity, int64_t h0, int64_t h1,
                                                    float f0, float f1, float* outF, int64_t* outI, void* user);
AVER_FW_ABI int32_t aver_fw_set_sequence_provider(aver_fw_sequence_fn fn, void* user);   /* null clears; always 1 */

AVER_FW_ABI int32_t aver_fw_sequence_play(int64_t sequence, float startSeconds, float rate);
AVER_FW_ABI int32_t aver_fw_sequence_pause(int64_t sequence);
AVER_FW_ABI int32_t aver_fw_sequence_stop(int64_t sequence);
AVER_FW_ABI int32_t aver_fw_sequence_set_time(int64_t sequence, float seconds);
AVER_FW_ABI int32_t aver_fw_sequence_set_rate(int64_t sequence, float rate);
AVER_FW_ABI int32_t aver_fw_sequence_is_playing(int64_t sequence);
AVER_FW_ABI int32_t aver_fw_sequence_state(int64_t sequence, float* outTimeLengthRate /*3 floats*/, int32_t* outPlaying);
AVER_FW_ABI int32_t aver_fw_sequence_bind(int64_t sequence, int64_t bindingName, int32_t entity);
```
No provider: every call returns 0 and `state` leaves its outputs untouched (an entity-less, sequence-less answer). `SequenceAbi.cpp` is one line in the `modules/framework/CMakeLists.txt` source list.

## 7. C# API (`scripting/csharp/Aver.Framework`)

New files only (no edits to `Native.cs`, `Entity.cs`, `Animation.cs`, `Events.cs`): **`SequenceNative.cs`** (`internal static class FwSequence`, nine `DllImport`s incl. `aver_fw_sequence_abi_version`, pattern of `AnimNative.cs`), **`LevelSequence.cs`**:
```csharp
public static class LevelSequences
{
    public static bool Play(string sequence, float startSeconds = -1f, float rate = 0f);   // < 0 continues
    public static bool Pause(string sequence);
    public static bool Stop(string? sequence = null);                                      // null = all
    public static bool SetTime(string sequence, float seconds);
    public static bool SetRate(string sequence, float rate);
    public static bool IsPlaying(string? sequence = null);
    public static bool TryGetState(string sequence, out float time, out float length, out float rate, out bool playing);
    public static bool Bind(string sequence, string bindingName, Entity actor);            // Entity.None clears
}
internal static class SequenceGraphInterop      // int-entity / primitive signatures for GraphCompiler (MontageGraphInterop pattern)
{
    internal static bool PlaySequenceForGraph(string sequence, float start, float rate);
    internal static bool PauseSequenceForGraph(string sequence);
    internal static bool StopSequenceForGraph(string sequence);
    internal static bool SetSequenceTimeForGraph(string sequence, float seconds);
    internal static bool SetSequenceRateForGraph(string sequence, float rate);
    internal static bool BindSequenceActorForGraph(string sequence, string bindingName, int entity);
    internal static bool IsSequencePlayingForGraph(string sequence);
    internal static bool GetSequenceStateForGraph(string sequence, out float time, out float length, out float rate);
}
```
Events: `Events.Subscribe("<Sequence>_Finished", ...)` / `AN_OnEvent` entry `<Sequence>_Finished`; sequence event keys arrive as the names authored.

## 8. Runtime and packaged-game wiring, save/load

- `GameApp.hpp:300` `sequencePlayer_` -> `game::SequenceDirector sequenceDirector_` (+ `SequenceHooks` built by `makeSequenceHooks`). Edits, all in the sequence blocks only: `:588` sink, `:982` and `:1013-1024` (`setSequences(loaded.world.sequences)`, `setBindings(makeLevelBindings(...))`, `setMode(Live)`, `startAutoplay()`; the AVER_INFO line lists each sequence), `:1937` (`seedMovable(sequenceDirector_.boundEntities())`), `:2098-2101` (split: `advance` + `evaluate(Pre)` before `tickAnimGraphs`, `evaluate(Post)` where it is now), `:2146-2152` (camera), `:2245` (emissive), the `afterUnload` reset (`:1013` area: `restore` + `clear`), and the provider install next to the anim provider (`GameApp.cpp:1847`): `aver_fw_set_sequence_provider(&game::sequenceProvider, &sequenceDirector_)` (`#if AVER_MODULE_SCENE && AVER_MODULE_FRAMEWORK`). Level streaming (`LevelStreaming`) already keeps authored levels whole in the shipped game; a streamed level's sequences are the editor's concern (below).
- `SandboxApp`: `seqEditor_` owns a `SequenceDirector` for Play (same wiring as the game, `SeqMode::Live`) and a single `SequencePlayer` for Animate preview (`Preview`) and run mode (`Offline`). `SandboxApp.cpp:2222-2230` and `SandboxPlay.cpp:905` get `tickSequencePre/Post` (section 2). The provider is installed once at init beside the anim provider (`SandboxApp.cpp:615`).
- `resetGameSystems()` (`GameSystemsWiring.hpp`): `sequenceDirector.stop(-1)` is **not** added there (the director lives in the host); each host calls `director.restore()+clear()` on level unload and on `endPlay`.
- Save/load: sequences persist only in the `.ocworld`/`.ocseq`. **Runtime sequence state (clock, voices, driven players) is transient and not saved**: a save/load mid-sequence resumes at the level's autoplay state (same as montage players and ragdolls). Property writes of a *running* sequence are not part of the save (the save host seams capture component bytes; a save taken mid-sequence would capture the sequenced value; risk 6).
- Editor outside Animate/Play: nothing evaluates; the director and driven-player maps are empty (one `empty()` check per frame).
- D3D12/Vulkan parity: n/a (CPU data, components, and the existing camera/skinning paths).

## 9. Editor (extend `SequenceEditor`; Animate mode and the Sequencer window)

Existing pattern kept: `SequenceHost` callbacks, `SnapshotUndo<fmt::OcSequence>` (per current sequence), `editRev_`, `status_`, headless-compilable with `AVER_WITH_IMGUI` undefined. New headless units so the logic is testable without ImGui: `SequenceTrackOps.{hpp,cpp}` and `SequenceCurveOps.{hpp,cpp}`; the draw code stays behind `AVER_WITH_IMGUI`.

1. **Multiple sequences** (`SequenceEditor`): `model_` stays the *current* sequence; new `std::vector<fmt::OcSequence> others_` holds the rest, written back on save unchanged except for target conversion; `load` no longer warns/drops. Sequencer title bar: combo of the level's sequences (+ `New`, `Duplicate`, `Rename`, `Delete`, `Import .ocseq...`, `Export .ocseq...`), switching clears selection/undo and re-syncs the player. The "no tracks writes nothing" rule applies per sequence. Play uses the director over `model_` + `others_` (so autoplay sequences all start, as in the game).
2. **Track menu** (`+ Track` popup in the Mode panel and the lane header): Transform, Emissive (existing), **Property...** (pick actor from the selection, then a field picker built from the registry: components via `World::fieldCount/fieldAt`, readable non-readonly F32/Vec3/Quat/I32/Bool fields only, text filter, shows kind and current value; creates the track seeded with a key at the playhead from the field's current value), **Visibility**, **Event** (name field; "Fire at playhead" key button), **Audio...** (drop a sound from the Content Browser or type a path; optional target for a positional source), **Animation...** (drop `.ocanim`/`.ocmontage`; selected skeletal actor), **Camera** (named; "from selection" makes an actor camera), **Camera Cuts** (one), **Sub-sequence...** (combo of the other sequences, or pick an `.ocseq`). Track order is draggable; each track has mute, rename, and a binding badge (placement / name / unbound in red).
3. **Section lanes** (dope sheet): audio/animation/sub sections are bars `[start, start+dur]` with the asset's file name inside (animation draws blend ramps, sub shows its offset/scale); drag the body to move, drag an edge to trim (animation/montage trims `v0`, sub trims `dur`), drop a Content Browser asset on a lane at the mouse time to create one (snap 1/fps; overlapping neighbours are trimmed, never overlapped); event keys are flags with their name; visibility is a shaded on/off span; cut keys are colour-coded to the camera. Section details (Mode panel `KEY`): all fields of 3.3 by name.
4. **Preview semantics**: sampled tracks always. Scrub/step/play use `SeqMode::Preview`; **Audition audio** (checkbox in the transport row) lets audio sections play while the clock moves forward; events never fire in Preview (shown as ticks on the ruler passing the playhead). Everything the preview wrote is restored by the base store on `leave()`, on save (`SandboxLevelEdit.cpp:477` already calls `restoreBases()`), on Play start (`SandboxPlay.cpp:245`) and when a track is removed/muted. Animation scrubbing is exact because driven players are placed, not played.
5. **Curves view**: a `Dope sheet | Curves` toggle in the Sequencer transport row. Curves: a left list of the selected track(s) with per-channel colour swatches and visibility checkboxes (X Y Z / yaw pitch roll / scale / R G B I / field channels), a time x value plot sharing the dope sheet's time axis, zoom and scroll (Ctrl+wheel time, Alt+wheel value, middle-drag pan, `F` frames the selected keys / all, auto-fit value range). Each channel is drawn by sampling `sampleSeqChannel` (4 px steps) so it is exactly the runtime curve. Keys are squares; the selected key shows its arrive and leave handles (length 1/3 of the neighbouring span, drawn from the effective tangents `seqAutoTangent` when automatic); drag a key = time + value (clamped inside neighbours' order), drag a handle = slope (broken when it moves alone, mirrored when both unbroken; Alt toggles), double-click the curve = key at that time with the sampled value, box select, `Del`. Toolbar for the selection: **Auto, Linear, Flat, Step, Smooth, Cubic (user), Break**. Editing writes `v[c]`, `t`, `interp`, `tan` only. Headless `SequenceCurveOps`:
   ```cpp
   enum class SeqTangentMode { Auto, Linear, Flat, User, Broken };
   void seqSetKeyInterp(fmt::OcSeqTrack&, usize key, fmt::OcSeqInterp);
   void seqSetTangentMode(fmt::OcSeqTrack&, usize key, SeqTangentMode);                     // fills/clears tan
   void seqSetTangent(fmt::OcSeqTrack&, usize key, u32 channel, f64 arrive, f64 leave, bool broken);
   usize seqMoveKey(fmt::OcSeqTrack&, usize key, f64 t, u32 channel, f64 value, f64 sequenceLength);   // returns the new index (re-sorted)
   usize seqInsertKeyAt(fmt::OcSeqTrack&, f64 t, u32 propertyArity);                         // value = sampleSeqChannel for every channel
   void seqCurveBounds(const fmt::OcSeqTrack&, u32 channelMask, u32 propertyArity, f64& t0, f64& t1, f64& v0, f64& v1);
   SeqTangentMode seqKeyTangentMode(const fmt::OcSeqKey&, u32 channel);
   ```
6. **Camera path overlay** (`pathLines`): draws every free camera track (selected one bright), cut boundaries as markers, the live camera's playhead pose; `pathStamp` includes the cut blend. **Lock viewport** and **K** key the *selected* camera track (K with none selected creates `Camera`); `hasCameraKeys()` becomes `hasCamera()` (any free camera track with keys, or an actor camera).
7. **Details of bindings**: `SequenceHost` gains `std::function<scene::Entity(const std::string&)> findByName;` and `std::function<std::string(scene::Entity)> bindName` (placement `name` if set, else the light/actor name when unique in the level, else empty). Keying a light or class actor requires a unique name (status line "Rename the light first"), so `canKey`'s exclusion list shrinks to decals and prefab members for non-transform kinds; **transform tracks on lights stay disallowed** (lights are saved as records, not placements). Save converts a track's entity to `target` through `slotOf`, or to `bind` when the entity is not a placement; tracks whose entity is gone or unnamed are dropped with one log line (as today's dropped tracks).
8. **Streamed levels** keep the rule at `SandboxLevelLoad.cpp:456-468`: tracks with `target >= 0` for non-resident objects are kept aside and written back; tracks with `bind` or no binding are edited normally and resolve if loaded. `trackEntities()` (`SandboxLevelStream.cpp:194`) returns every bound entity, including property/visibility/animation/audio targets, so they stay pinned.
9. **Render queue**: `--sequence-play` and `.ocrender` accept any level that `hasCamera()`. The job's `sequence` key now **selects** a level sequence by name (empty = the first with a camera); not found fails with the list of names. `runAt` positions the player in `SeqMode::Offline` (sampled tracks exact, sub-sequences recursive, audio/events silent) and the frame order of section 2 makes the pose current in the same frame. `.ocrender` keeps its format (no new keys): offline events are a decision for the owner (risk 4). `docs/RENDER_QUEUE.md` loses the "one sequence" limits and gains a "what renders" line listing the tracks.
10. **`.ocseq` in the editor**: Content Browser create entry `cbCreateSequence()` (empty `OCSEQ 1` with one sequence) and a type label `{".ocseq", "Level Sequence Asset"}` (append-only edits to `SandboxContentBrowser.cpp` / `SandboxApp.hpp`); double-click opens it as the Sequencer's current sequence with its own dirty flag and Ctrl+S in the Sequencer window (bindings resolve by name against the open level, unresolved ones shown red). `Export .ocseq` converts entity targets to names (refuses with the offending list when names are missing or not unique); `Import .ocseq` copies its sequences into the level.

## 10. Graph nodes (for the graph-node agent)

Category `"Sequencer"` in `GraphNodeDefsGameSystems.hpp` + `GraphGameSystemNodeTable.cs` (table pattern of `PlayMontage`, `GraphGameSystemNodeTable.cs:109`). Exec nodes: `exec`, outputs `then`, `success:bool`. String inputs are NODE-line attributes. The wrapped methods are `SequenceGraphInterop` (section 7).

| Node | Pins (besides exec/then/success) | Attrs | Wraps |
|---|---|---|---|
| `PlaySequence` | in `start:float=-1`, `rate:float=0` | `sequence` | `PlaySequenceForGraph` |
| `PauseSequence` | | `sequence` | `PauseSequenceForGraph` |
| `StopSequence` | | `sequence` (empty = all) | `StopSequenceForGraph` |
| `SetSequenceTime` | in `seconds:float` | `sequence` | `SetSequenceTimeForGraph` |
| `SetSequenceRate` | in `rate:float` | `sequence` | `SetSequenceRateForGraph` |
| `BindSequenceActor` | in `entity:int` | `sequence`, `binding` | `BindSequenceActorForGraph` |
| `IsSequencePlaying` (pure) | out `result:bool` | `sequence` (empty = any) | `IsSequencePlayingForGraph` |
| `GetSequenceState` (pure) | out `found:bool`, `time:float`, `length:float`, `rate:float` | `sequence` | `GetSequenceStateForGraph` |

Sequence lifecycle and event-track events need no node: they arrive as `AN_OnEvent` entries (`<Sequence>_Started`, `<Sequence>_Finished`, and every authored event name) or `CustomEvent` (route 1). Property/visibility/animation tracks are authored, not scripted; scripting them is `SetField`/`SetVisible`/`PlayMontage` as before.

## 11. Tests (auto-registered: any `*Test` executable under `tests/` is picked up by the walk at the bottom of the root `CMakeLists.txt`; add the `add_executable`)

**`tests/formats`** (extend `OcSequenceTest.cpp`; new `OcSeqFileTest.cpp`, links `Aver.Formats Aver.Core`)
- Old levels byte-identical write->parse->write (golden = the pre-change text of an existing test level); every new kind round-trips with tags (`dur text arg tan`); `cubic` + `tan` round trip; the numeric-run parser stops at the first tag; unknown word after the key time -> Smooth; a track of an unknown kind and its keys ignored; `bind` percent-encoding round trips a name with spaces and `%`; target renumbering still correct with `bind` tracks interleaved; a camera track keeps `target`; `t = 1234.567` survives and `t = 0.1` still writes `0.1`; duplicate camera names kept as authored.
- `.ocseq`: header gate (newer version refused), `target` ignored on read and never written, a track without `bind` dropped with one message, several sequences, `path#Name` is a game-side concern (not parsed here).

**`tests/game`** (extend `LevelSequenceTest.cpp`; new `SequenceTracksTest.cpp`, `SequenceDirectorTest.cpp`; link as `LevelSequenceTest` does)
- Samplers: `Cubic` Hermite equals the closed form for a two-key curve with explicit tangents; automatic tangents equal the finite difference; `Smooth` outputs are bit-identical to the pre-change samplers on the existing fixtures; `seqActiveSection` (later start wins, hold-after), `seqSectionWeight` ramps, `seqCrossedKeys` across a loop seam, inclusive start, a zero-length span; `sampleSeqCut` blend alpha and before-first-key camera.
- `SequenceTracksTest` (World, no GPU): property F32/Vec3/Quat/I32/Bool on `CLight`/`CMeshRenderer`/a script-style registered component writes the bytes, read-only/String/Mat4 refused with one warning, missing component skipped, `touchLocal` for `CLocal`; visibility with and without descendants; base store restores everything incl. an added component and a destroyed entity; mute releases; transform track via the store matches the old behaviour; actor camera reads the world matrix; two camera tracks + cuts select and blend; sub-sequence time map (offset/scale/loop/clamp), nested depth 4, cycle refused, `.ocseq` loaded through `readFile`; triggered tracks: events fire once per forward crossing, never on `setTime`, never in Preview/Offline, once at t = 0 on first play, route 0/1/2 reach a recording hook with args in order; audio starts at a forward crossing, not mid-section, stops on pause/wrap; hook-less build stays silent.
- `SequenceDirectorTest`: two sequences, autoplay only the flagged; `play/pause/stop/setTime/setRate/state/bindOverride` (override redirects a `bind` track, clearing restores); `_Started`/`_Finished` once; camera = most recently started playing sequence with a camera; `boundEntities` recursion; `restore` after `stop`.

**`tests/anim.scene`** (new `MontageDriveTest.cpp`, link set of `MontageSystemTest`)
- `driveClip` at t shows exactly `sampleAnimation(clip, t)` over rest in `pose()`; a second call at t2 moves it with no state left over (scrub back and forth is exact); weight 0.5 blends; unweighted slot coverage unchanged for ordinary montages; no events and no `notifiesFired()` change; not advanced by `tick`; stale for 3 ticks drops it silently; `releaseDriven` leaves the entity at its state-machine pose; works over a `CAnimGraph` entity and a bare `CAnimator`; entity without a skeleton returns false; `CAnimator` bytes unchanged.

**`tests/framework_sequence`** (new directory; one `add_subdirectory` line in the root `CMakeLists.txt` beside `framework_anim`; links `Aver.Framework Aver.Core`): `FrameworkSequenceAbiTest`: version 1; no provider -> every call 0 and `state` leaves outputs; a fake provider receives the exact op/hashes/floats for each of the nine functions and its returns and outs are relayed; clearing restores zeros.

**`tests/editor`** (compile the editor sources directly with `AVER_WITH_IMGUI` undefined, the `AnimGraphEditorTest` arrangement; link `Aver.Runtime.Game.Core Aver.Scene Aver.Formats Aver.Core Aver.Platform`)
- `SequenceCurveOpsTest`: tangent mode transitions (Auto -> Linear -> Flat -> User -> Broken and back), materialising the other channels' automatic values, `seqMoveKey` keeps order and returns the new index, `seqInsertKeyAt` samples the existing curve (value unchanged by the insert on a Cubic curve), bounds.
- `SequenceTrackOpsTest`: add-track per kind (defaults, naming `Camera 2`, one cut track, camera rename repairs cut keys), property field picker list (kinds filtered, readonly excluded), section add/trim/move without overlap, the asset-drop rule, `bind` vs `target` choice on save, `.ocseq` export refusal on duplicate/missing names, mute/release.
- `SequenceEditorTest` (compiles `SequenceEditor.cpp`): load keeps **all** sequences and save writes them back (the old drop is gone), switching current sequence keeps undo separate, streamed-level aside rule for `target >= 0`, `restoreBases` restores property/visibility/animation writes, `hasCamera`, render `runAt` in Offline fires nothing.

**C#** (`scripting/csharp/Aver.Graph.Tests`, for the graph-node agent): `SequenceNodeTests.cs` in the `MontageNodeTests` mould (each table row names a real `SequenceGraphInterop` method matching its pins, defaults, emitted IL valid, exec node refused by the pure compiler, pure reads work, missing attribute fails by name), registered in `Program.cs`.

Verification the main loop runs (not the implementer's tests): headed PIE and packaged game on a level with a 20 s cutscene: two camera tracks and cuts (one blended), a lamp's `CLight.intensityLux` and `colour` property tracks, a door that becomes invisible, a character playing a clip section then a montage section (scrub in Animate, no pop at section edges, no stale frame), a positional audio section, an event key that an `AN_OnEvent` graph logs, and a sub-sequence of the same level; then a 48-frame render-queue job of it (frames N and N+1 differ in the animation, the camera cut lands on its frame) and Play/Stop leaving the lamp, door and character exactly as authored.

## 12. Files, partitioned

**Create**
- `modules/formats/include/aver/formats/OcSeq.hpp`, `modules/formats/src/OcSeq.cpp`
- `Runtime/include/aver/game/{SequenceDirector,SequenceHost}.hpp`, `Runtime/src/SequenceDirector.cpp` (`SequenceTracks` appliers may live in `Runtime/src/SequenceTracks.cpp` with a private header)
- `modules/framework/include/aver/framework/framework_sequence_abi.h`, `modules/framework/src/SequenceAbi.cpp`
- `scripting/csharp/Aver.Framework/{SequenceNative,LevelSequence}.cs`
- `sandbox/src/{SequenceTrackOps,SequenceCurveOps}.{hpp,cpp}`, `sandbox/src/SequenceCurveView.cpp` (ImGui draw only, `AVER_WITH_IMGUI` guard)
- `docs/SEQUENCER.md` (user guide: tracks, modes, `.ocseq`, curves, render queue, node and event list, limits)
- `tests/formats/src/OcSeqFileTest.cpp`; `tests/game/src/{SequenceTracksTest,SequenceDirectorTest}.cpp`; `tests/anim.scene/src/MontageDriveTest.cpp`; `tests/framework_sequence/{CMakeLists.txt,src/FrameworkSequenceAbiTest.cpp}`; `tests/editor/src/{SequenceCurveOpsTest,SequenceTrackOpsTest,SequenceEditorTest}.cpp`

**Modify (this feature owns these)**
- `modules/formats/include/aver/formats/OcWorld.hpp` (the sequence structs only, `:497-526`), `modules/formats/src/OcWorld.cpp` (the three sequence hunks only), `modules/formats/CMakeLists.txt` (one source), `docs/formats/FORMAT_SPECS.md` (the SEQUENCE paragraph `:973-1010` plus a new subsection for `.ocseq`)
- `Runtime/include/aver/game/LevelSequence.hpp`, `Runtime/src/LevelSequence.cpp`, `Runtime/CMakeLists.txt` (sources)
- `modules/anim/include/aver/anim/Montage.hpp`, `modules/anim/src/Montage.cpp` (`drive`, `driven`); `modules/anim.scene/include/aver/anim/AnimMontages.hpp`, `modules/anim.scene/src/AnimMontages.cpp` (driven players)
- `sandbox/src/SequenceEditor.{hpp,cpp}`, `sandbox/src/SandboxSequenceRun.cpp`, `sandbox/src/SandboxPlay.cpp` (`tickSequence` -> Pre/Post), `sandbox/src/SandboxPanels.cpp` (`sequenceHost()` additions), `sandbox/src/SandboxLevelLoad.cpp` / `SandboxLevelEdit.cpp` / `SandboxLevelStream.cpp` (load/save/pin of the new tracks, multi-sequence), `sandbox/src/SandboxApp.hpp` (members, `tickSequencePre/Post`, `cbCreateSequence` decl)
- `docs/RENDER_QUEUE.md`, `docs/EDITOR.md` (Phase 15 limits line), `docs/formats/FORMAT_SPECS.md`
- `tests/formats/src/OcSequenceTest.cpp`, `tests/game/src/LevelSequenceTest.cpp`, their `CMakeLists.txt`, `tests/anim.scene/CMakeLists.txt`, `tests/editor/CMakeLists.txt`, root `CMakeLists.txt` (one `add_subdirectory`)

**Shared files, append-only (other wave-5 features may also touch; the second to land keeps both blocks)**
- `Runtime/src/GameApp.cpp` and `Runtime/include/aver/game/GameApp.hpp`: only the hunks listed in section 8 (other wave-5 agents may add provider installs next to the anim provider: keep both lines).
- `sandbox/src/SandboxApp.cpp`: the `tickSequence` split (`:2222-2230`) and one provider-install line next to the anim provider.
- `sandbox/CMakeLists.txt` (sources), `modules/framework/CMakeLists.txt` (source list), root `CMakeLists.txt`, `sandbox/src/SandboxContentBrowser.cpp` (create entry + type label), `docs/formats/FORMAT_SPECS.md` outside the SEQUENCE paragraph (IK/retarget and motion matching may append their own format subsections: separate subsections, no overlap).

**Owned by the other wave-5 features (do not edit)**: IK/retarget: `modules/anim` `Ik*`, ControlRig, the retarget asset, `AnimEditor.cpp` and the rig/retarget panels. Motion matching: `modules/anim` pose database, the `anim.scene` pose source, `AnimGraphSystem.*`, `AnimSystem.*` (this feature needs neither: it adds nothing to `AnimSystem.hpp/.cpp`; the `Montage*`/`AnimMontages*` files above are unclaimed by them), the C# character files. Not edited by anyone here: `framework_abi.h`, `scene_abi.h`, `audio_abi.h`, `Components.hpp`, `modules/rhi.*`, `modules/ragdoll`.

`modules/framework/module.json` needs no change (its optional/runtime relay arrangement is the anim ABI's); `Aver.Runtime.Game.Core` gains no module edge (audio and events go through hooks; `SequenceHost.hpp` is header-only and included only by the hosts). Run `scripts/module-guard-audit.py` after.

## 13. Risks and open questions for the owner

1. **Old builds drop the new tracks silently** on load+save (unknown kinds are skipped, and the re-save writes what was kept). Same-binary editor/runtime makes this a non-issue in practice; say if you want a `SEQUENCE ... ver 2` stamp and a refusal instead.
2. **Cubic is "you own the curves"**: Euler/yaw-pitch channels are Hermite on the raw degrees (no short arc, no quaternion), unlike `Smooth`/`Linear`, which stay slerp/squad. An authored 350 -> 10 under Cubic spins the long way until you key 370. UE behaves the same; the alternative (tangents on quaternions) has no sane handle UI.
3. **Audio cannot start mid-section** (the audio ABI has no seek/offset), so resuming or jumping into the middle of a sound is silent, and pause stops the voice. An `aver_audio_play_from(sound, offsetSeconds, ...)` in `audio.abi` fixes it (outside this feature's files): approve if wanted.
4. **Events never fire in the render queue.** A render that needs gameplay events (a door that must open for the shot) would have to be Play-driven; making `Offline` fire events would run game logic during a deterministic render. Keep silent, or add an `.ocrender` flag later?
5. **No FOV (and no fade/letterbox) track.** The view FOV is a constant 60 degrees at six build sites (`SandboxRender.cpp`, `GameApp.cpp:1647`); a camera FOV needs one `viewFovOverride` seam first. `CCamera.fovYRad` exists and an actor camera could carry it once that seam exists.
6. **A save during a running sequence captures sequenced values** (property writes are not restored by Play's snapshot, only by the sequencer's base store). Fine for the editor (Play stop restores); a packaged-game save mid-cutscene stores the sequenced lamp intensity and the sequence restarts from its autoplay state on load. Saving the sequence clock needs the save host seams (like montages, deferred).
7. **Bind by name is as unique as your names.** Placements without an authored `name` are addressed by index (stable within a level, not across `.ocseq` files). Duplicate names resolve to the first and warn once; the editor refuses to key an unnamed/duplicate light. Lights are `LIGHT` records, so transform tracks on lights stay unsupported.
8. **Animation tracks drive montages, so notifies inside driven clips do not fire** (use event tracks), root motion is off, and clip sections need a skeleton (object-animation clips are what transform tracks are for). Blends between two overlapping animation sections on one track are not crossfaded in v1 (later start wins); use two tracks for a true overlap.
9. **Evaluate order change** (Pre before the animation tick) means property writes land before gameplay-driven animation graphs read parameters this frame; a sequence that sets a field a graph reads sees the new value one frame earlier than before. Intended.
10. **Not covered**: reverse playback, spawnables/possessables beyond named binding, take/shot hierarchy and shot rendering by track, sequence recorder, camera shake, per-section easing curves, weighted Bezier handles (slopes only), `.ocseq` sub-sequences of an instanced level's own sequences (an instance's `SEQUENCE` records stay ignored, `LEVEL_INSTANCES.md`).
11. `GameApp.cpp`, `SandboxApp.cpp`, `sandbox/CMakeLists.txt` and the root `CMakeLists.txt` are touched by several wave-5 features: serialise the merges (every edit here is one delimited hunk or one appended line).
