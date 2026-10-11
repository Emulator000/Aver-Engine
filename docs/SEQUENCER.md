# Sequencer

The level sequencer is the timeline built into every level: transform, camera and emissive tracks (Animate mode, `docs/EDITOR.md` Phase 15) plus **property**, **visibility**, **event**, **audio**, **animation**, **camera-cut** and **sub-sequence** tracks, several named cameras, several sequences per level, tangent curves, and a standalone `.ocseq` file. The data is `SEQUENCE` / `SEQTRACK` / `SEQKEY` records in the `.ocworld` (`docs/formats/FORMAT_SPECS.md`, section 11); the editor is `sandbox/src/SequenceEditor.cpp`, the player `Runtime/src/SequenceTracks.cpp` and `Runtime/src/LevelSequence.cpp`, the per-session director `Runtime/src/SequenceDirector.cpp`. Design record: `docs/plans/wave5/SEQUENCER_TRACKS.md`.

## Tracks

Every track has a binding (an actor) where it needs one, a mute switch and a display name. A level written without the new tracks is byte-identical to before.

| Track | Binds | Key | What it does |
|---|---|---|---|
| Transform | an actor | point | The actor's local transform (position, yaw / pitch / roll, scale). |
| Emissive | an actor | point | Multiplies the actor's emissive by `rgb * intensity`. |
| Property | an actor and `Component.field` | point | Any readable, writable float, vec3 (colours too), quaternion, int or bool field of any component the actor already has: `CLight.intensityLux`, `CLight.colour`, `CLocal.position`, a script component's field. Strings, 64-bit values, entity references and matrices are refused. Integer and bool fields hold their key. A track never adds a component. |
| Visibility | an actor | step | Shows or hides the actor's mesh and, optionally, its descendants. Before the first key the authored value stands. |
| Event | optional actor (the target) | point | Fires a named event with up to four numbers and a string. Delivery: the event bus (an `AN_OnEvent` entry or `Events.Subscribe`), the actor's entity graph (`CustomEvent`), or both. |
| Audio | optional actor | section | Plays a sound for the section's length, with fade in and out, loop, volume, pitch, and (with an actor) positional attenuation. |
| Animation | a skeletal actor | section | Plays an `.ocanim` clip or an `.ocmontage` over whatever the actor's state machine or animator is doing, with blend in and out, a start offset, rate, weight and "hold last pose". |
| Camera | none (free), or an actor | point | A named camera. A free one has keys (position, yaw, pitch); an **actor camera** looks along its actor's +X, so a camera can ride a crane that a transform track moves. |
| Camera Cuts | none | step | At most one. Each key names a camera and an optional blend (seconds) into it; before the first key the first camera is live. |
| Sub-sequence | none | section | Plays another level sequence, or a `.ocseq` file, inside a section: time is `offset + (t - start) * scale`, optionally looped, optionally with its camera live. Depth is limited to 4 and a sequence already playing above it is refused. |

Conflicts resolve **last track in file order wins**. Sections on one track never overlap.

## How it plays

Sampled tracks (transform, emissive, camera, cuts, property, visibility, animation, sub-sequences) are pure functions of time: scrubbing, jumping and the render queue's pose-at-time are exact. **Triggered** tracks (events and audio) fire on **forward crossings** of the clock only: a jump or scrub fires nothing, and a sound section entered mid-way stays silent (the audio ABI has no seek).

| Mode | Where | Sampled | Events | Audio |
|---|---|---|---|---|
| Preview | Animate mode | yes | never (shown as ticks on the ruler) | only with **Audition** on, while the clock moves forward |
| Live | Play and the packaged game | yes | yes | yes |
| Offline | the render queue, `--sequence-play` | yes | no | no |

A frame evaluates in two phases around the animation tick: **Pre** (property, visibility, animation, events, audio, sub-sequence recursion) before the animation graphs and `AnimSystem::tick`, **Post** (transform, emissive, cameras and cuts) after it. That is what makes a driven pose land on the frame it was posed, in the render queue and in the editor.

**Everything a sequence writes is undoable.** The first write of each (actor, component, field) records the authored bytes in a base store; leaving Animate mode, saving, starting Play, muting or deleting a track, and Play stopping put them back. An actor that no unmuted track drives goes back to where it was authored.

Animation tracks play through the montage system as **driven** players: placed at an exact position with an exact weight every tick, never advanced by a clock, raising no events (put a notify's job on an event track). They work over a state machine (the montage's slot is applied after the pipeline) and over a bare animator, never write `CAnimator`, and are invisible to `PlayMontage` / `StopMontage`.

## Several sequences

A level holds any number of sequences, each with a unique name. The Sequencer's title bar has a combo of them plus **New**, **Duplicate**, **Rename** (sub-sequence sections that named it follow), **Delete**, **Import .ocseq** and **Export .ocseq**. At run time a `SequenceDirector` owns one player per sequence: every sequence with `autoplay` starts with the session; the camera belongs to the most recently started one that has a camera. A sequence raises `<Name>_Started` and `<Name>_Finished` (non-looping, once) on the event bus. A sequence nobody started is not evaluated.

## Scripts

`LevelSequences` (`scripting/csharp/Aver.Framework/LevelSequence.cs`, over `framework_sequence_abi.h`):

```csharp
LevelSequences.Play("Intro", startSeconds: -1f, rate: 0f);   // < 0 continues; a stopped one starts at 0; rate 0 keeps the rate
LevelSequences.Pause("Intro");
LevelSequences.Stop("Intro");                // null = every sequence: back to 0, authored values restored, sounds stop
LevelSequences.SetTime("Intro", 4.5f);       // a jump: fires nothing for the skipped span
LevelSequences.SetRate("Intro", 0.5f);
LevelSequences.IsPlaying("Intro");           // null = any
LevelSequences.TryGetState("Intro", out var time, out var length, out var rate, out var playing);
LevelSequences.Bind("Intro", "Hero", someActor);   // tracks bound by the name "Hero" now drive someActor (Entity.None clears)
```

Graph nodes (category `Sequencer`): `PlaySequence`, `PauseSequence`, `StopSequence`, `SetSequenceTime`, `SetSequenceRate`, `BindSequenceActor`, `IsSequencePlaying`, `GetSequenceState`. Lifecycle events and every event key arrive as `AN_OnEvent` entries (`<Sequence>_Started`, `<Sequence>_Finished`, the authored names) or `CustomEvent` (graph delivery); there is no node for them.

## Bindings

A track binds an actor by **placement index** (the common case: the actor is a placed object) or by **name**: a placement's `name`, a light's name, a class actor's name. With both, the name wins and the index is the fallback. The editor saves a placement by index and any other actor by name; an unnamed or deleted actor drops its track with a log line, and keying a light or class actor needs a name that is unique in the level ("rename it first"). An unresolved name stays in the file and shows red in the lane header. Transform tracks on lights stay unsupported (lights are `LIGHT` records, not placements).

## `.ocseq`

A standalone sequence file: `OCSEQ 1` and the same records, with actors bound by **name only** (the placement index means nothing outside a level). **Export** refuses, listing them, when a bound actor has no name or its name is shared. **Import** copies the file's sequences into the level, resolving names against the open level (a name nothing answers to stays unresolved). Create one from the Content Browser (**New Level Sequence**); double-clicking one imports it and opens the Sequencer; Ctrl+S in the Sequencer window re-exports the linked file. A sub-sequence section can name `Seq/boom.ocseq` or `Seq/boom.ocseq#Boom`.

## Curves

The Sequencer's **Curves** toggle shows the selected transform, camera, emissive or property track as curves: a channel list with colours and visibility checkboxes, and a time x value plot on the same time axis as the dope sheet (Ctrl+wheel zooms time, Alt+wheel zooms value, middle-drag pans, **F** frames). Every curve is drawn by sampling the same function the runtime uses, so what you see is what plays. Drag a key for time and value, double-click the curve to add a key (on a Cubic curve the curve does not change), box-select, `Del`. The toolbar sets the selected keys: **Auto / Linear / Flat** tangents, **Step / Smooth / Cubic (user)**, **Break**; a Cubic key shows arrive and leave **handles** (drag one for its slope; Alt breaks the pair).

`Smooth` is the classic Catmull-Rom (a smooth camera is a centripetal spline plus quaternion squad). `Cubic` is a cubic Hermite per channel on the authored numbers, **Euler degrees included**: there is no quaternion path and no short arc, so key 350 -> 370 to turn the long way, as in Unreal.

## Render queue

`--sequence-play` and `.ocrender` accept any level that has a camera. The job's `sequence` key selects a level sequence by name (empty = the first with a camera); an unknown name fails and lists the level's sequences. A pose at time `t` is exact for every sampled track including animation and sub-sequences; events and audio never run in a render. `docs/RENDER_QUEUE.md`.

## Limits

- Audio cannot start mid-section (no seek in `audio_abi.h`): resuming or jumping into the middle of a sound is silent, and pause stops the voice.
- Events never fire in the render queue; a shot that needs a gameplay event has to be played.
- There is no field-of-view, fade or letterbox track: the view FOV is a constant 60 degrees.
- A save taken during a running sequence stores the sequenced values (Play's world snapshot records transforms and visibility only; the sequencer's base store puts the rest back when Play stops). Runtime sequence state is not saved.
- Notifies inside a driven clip do not fire and root motion is off; two overlapping animation sections on one track do not crossfade (the later start wins; use two tracks).
- Not covered: reverse playback, spawnables, take / shot hierarchies, a sequence recorder, camera shake, per-section easing curves, weighted Bezier handles (slopes only), `.ocseq` of an instanced level's own sequences.
- An older build that loads a level with the new tracks skips them, and drops them if it saves the level again.
