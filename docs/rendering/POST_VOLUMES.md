# Post-process volumes and grading LUTs

A post-process volume overrides camera post-processing (grade, bloom, vignette, depth of field, exposure...) while the
view is inside a box, or everywhere, and fades in and out as the camera moves through its edge. It is a level record
(`POSTVOLUME`), not an entity: it has no mesh, transform or physics, only a box and a list of overridden fields. A
volume can also carry a `.cube` 3D LUT, which the post pass applies after the tonemap.

The fields it can override are the `rhi::PostSettings` ones (`modules/rhi/include/aver/rhi/RHI.hpp`). The base values
come from the project (`RENDER.*` keys) and the editor's Post Process panel; volumes blend on top of those.

## The record

`POSTVOLUME` in `.ocworld` (grammar and key list: [FORMAT_SPECS.md](../formats/FORMAT_SPECS.md), section 11). Data
model: `fmt::OcPostVolume` in `modules/formats/include/aver/formats/OcWorld.hpp`.

```
POSTVOLUME name Cave%20Mouth bounds -100 -200 0 1000 500 300 blend 150 priority 3 weight 0.75 saturation 0.4 vignette 0.6 lut Grades/teal.cube
POSTVOLUME infinite autoexposure 0 bloom 0.3
```

Fields are **sparse**: a volume stores only what it overrides, as `key value` pairs from the table `kOcPostKeys`. That
table (name, label, value count, slider range) is the single list the parser, writer, editor and blender share; a
`PostSettings` field that volumes should reach needs one row there and one line in `applyPostVolumeFields` /
`readPostField` (`Runtime/src/GamePostVolumes.cpp`), and no format change. An old level has no record and loads and
writes exactly as before.

## Blending

`game::blendPostVolumes(volumes, base, eye, luts)` (`Runtime/include/aver/game/GamePostVolumes.hpp`), called by both
hosts right before `setPostProcess`:

1. **Weight per volume at the camera.** Inside the box: its `weight` (0..1). Outside: ramps to zero over `blendCm`
   with a smoothstep, measured as the Euclidean distance to the box; `blendCm` 0 is a hard edge. `infinite` is
   `weight` everywhere.
2. **Apply in ascending `priority`** (file order among equals), each onto the running result, for every field it
   states. Floats lerp by the weight; exposure-like multipliers (`exposure`, `exposuremin/max`, `doffocus`) lerp in
   stops; switches and choices (`autoexposure`, `dof`, `tonemap`) flip once the weight reaches 0.5. Values are held
   to the key's slider range, so a hand-edited 50 cannot blow the grade up. Both ends are exact: no volume (or weight
   0) gives the base settings field for field, full weight gives precisely the volume's value.
3. **The LUT.** A texture cannot be lerped, so the volume with the highest weight (the later wins a tie) supplies the
   LUT and its strength crossfades by that weight through `gradingLutIntensity`: from nothing when the base has no
   LUT, from the base's intensity when the base uses the same LUT. Against a *different* base LUT the volume's takes
   over once its weight reaches 0.5. `lutintensity` sets the strength at full weight (default 1). Volumes whose LUT
   fails to load are skipped.

With no volume covering the camera nothing is allocated and `setPostProcess` receives `post_` unchanged, so a level
without volumes renders as it always did.

## Grading LUTs

`fmt::parseCubeLut` / `loadCubeLut` (`modules/formats/include/aver/formats/CubeLut.hpp`) read Adobe/Resolve `.cube`
3D LUTs (`LUT_3D_SIZE` 2..129, `DOMAIN_MIN/MAX`, Resolve's `LUT_3D_INPUT_RANGE`) into an N^3 RGBA16F array, red
fastest. A file whose domain is not 0..1 is **resampled onto 0..1** at load, so the shader samples with the display
colour directly. 1D LUTs, a wrong row count and non-numeric data are refused with a reason. `game::PostLutCache`
turns the array into a 3D texture through `IDevice::resources()->createTexture` (`Tex3D`, `RGBA16F`, one upload, both
backends), once per path; a path that fails stays failed (one warning) until forgotten.

LUT paths are relative to the project's content folder. The game preloads a level's LUTs at load, so entering a
volume never stalls on a file read; the editor loads on first use (picking one in the panel, or opening a level).
Rescan in the panel re-reads the file.

## Using it

**In the editor**: Add > Post Process Volume drops a 10 m box with a 2 m blend around the camera and selects it; the
Outliner's "Post Process Volumes" entry opens the volume list and the selected volume's Details. There you set the
box (min/max in cm), blend distance, priority and weight, pick a `.cube` from the content folder, and tick the fields
to override: a ticked field starts at the value it replaces (the level's base) and takes a slider. The selected
volume's box is drawn bright with a dim shell at the blend distance; Show > Post Volumes draws every box. The
viewport blends volumes at the camera exactly as the game does, so flying into a box shows the grade; Delete in the
panel removes a volume. Volumes save with the level (they live in `levelHeader_.postVolumes` and ride through
`saveLevel` like water does).

**In a game**: nothing to call. `GameApp` copies the level's records at load, preloads their LUTs, and blends at the
camera each frame.

## Wiring

| What | Where |
|---|---|
| Record, key table, writer/parser | `modules/formats/include/aver/formats/OcWorld.hpp`, `src/OcWorld.cpp` |
| `.cube` reader | `modules/formats/include/aver/formats/CubeLut.hpp`, `src/CubeLut.cpp` |
| Weights, blend, LUT texture cache | `Runtime/include/aver/game/GamePostVolumes.hpp`, `src/GamePostVolumes.cpp` |
| Game call site | `GameApp::pushFrame` (blend), `installLevelHooks` (load, preload, unload) |
| Editor tools (Add, boxes, Details, preview blend) | `sandbox/src/PostVolumeTools.hpp/.cpp`; hooks in `SandboxShell.cpp` (Add, Show), `SandboxPanels.cpp` (Outliner entry, Details), `SandboxRender.cpp` (boxes), `SandboxApp.cpp` (blend), `SandboxLevelLoad.cpp` (reset) |
| Tests | `tests/formats/src/CubeLutTest.cpp` (`.cube`, POSTVOLUME round trip), `tests/game/src/PostVolumesTest.cpp` (weights, blend, LUT) |

## Not done / edges

* **Volumes are not entities**: no viewport click-pick, gizmo handles or undo entry yet; select in the Outliner list
  and edit numbers. (Delete is not undoable.)
* **Inside a volume the editor's Post Process panel still edits the base**, not the blended value; the panel shows what
  the level uses outside any volume. A console `post.*` change made while inside a volume is adopted into the base
  with the volume's contribution baked in.
* **One camera**: the blend is evaluated at the view position; split-screen or a second render view would need its own.
* **Streamed levels**: records are level-header data and load with the level, not per cell.
* **No spatial index**: every volume is tested every frame, which is fine for tens of volumes.
* The shader side (what `vignette`, `grain`, `dof` etc. do, and sampling the LUT) is the post pass's, not this page's.
