# Movie Render Queue

Renders a level's camera sequence to a PNG sequence, an OpenEXR sequence or an H.264 MP4, offline,
at a resolution and frame rate of your choosing, with sub-frame temporal samples for motion blur and
anti-aliasing. It upgrades the `--sequence-play` / `--sequence-capture` run (docs/EDITOR.md, Phase 15):
same run mode, same capture path, now driven by a job.

Two ways in:

- **Window > Render Queue** in the editor: set up a job, press Render. The window is resized to the output,
  the editor UI is hidden while it renders (the scene fills the window), Esc or Cancel stops it, and the
  editor returns when it ends.
- **Command line**, headless-friendly (bounded run, exits 0 on success and 1 on failure):

  ```
  Sandbox.exe --project <p>.ocproject --render-queue shot01.ocrender
  ```

  Any other flag applies as usual (`--denoiser`, `--render-scale`, `--no-vsync`, `--set ...`). Run it
  **headed**: it needs a window to render into.

## The job (`.ocrender`)

JSON. Every key is optional; unknown keys are ignored. Values are clamped into range on load.
`modules/formats/include/aver/formats/RenderJob.hpp` is the reader and writer.

```json
{
  "level": "Content/Levels/Intro.ocworld",
  "sequence": "Intro",
  "frameStart": 0,
  "frameEnd": -1,
  "width": 1920,
  "height": 1080,
  "fps": 24,
  "warmupFrames": 120,
  "settleFrames": 0,
  "temporalSamples": 8,
  "shutter": 0.5,
  "ptSamples": 64,
  "output": { "kind": "exr", "dir": "Renders/intro", "name": "intro", "exrFloat": false, "bitrateKbps": 20000 }
}
```

| Key | Default | Meaning |
|---|---|---|
| `level` | open level | Level to open first. Relative paths are tried as given, then next to the job file, then in the project. |
| `sequence` | the level's | The sequence's name; the run refuses if the level's sequence has another name. A level has one sequence today. |
| `frameStart`, `frameEnd` | 0, -1 | Output frames to render, inclusive. `frameEnd` < 0 = through the end of the sequence. |
| `width`, `height` | 1920 x 1080 | Output size (16..16384). MP4 sizes are rounded down to even. |
| `fps` | 0 | Output frame rate. 0 = the sequence's own. Frame `n` is at `n / fps` seconds; the count is `ceil(length * fps)`. |
| `warmupFrames` | 120 | Engine frames spent on the first pose before frame 0 so GI, TAA and the denoiser settle (at least 4). |
| `settleFrames` | 0 | Extra engine frames each temporal sample holds its pose before it is read. Raise it to let a denoiser converge per sample. |
| `temporalSamples` | 1 | Renders averaged into each output frame (1..64). |
| `shutter` | 0.5 | Fraction of the frame interval the shutter is open (0..1; 0.5 = a 180 degree shutter). 0 = no motion blur. |
| `ptSamples` | 64 | Path tracer samples per pixel each temporal sample waits for. Used only while the Path Tracing view is on (1..1600). |
| `output.kind` | `png` | `png`, `exr` or `mp4`. |
| `output.dir` | `Renders` | Output folder, created. Relative paths are relative to the job file (the project folder for a job started from the panel). |
| `output.name` | `seq` | File name stem: `<name>_00012.png` / `.exr`, or `<name>.mp4`. The number is the output frame number. |
| `output.exrFloat` | false | EXR as 32-bit float instead of 16-bit half. |
| `output.bitrateKbps` | 20000 | MP4 video bitrate. |

## What a frame is

Output frame `n` is built from `temporalSamples` renders. Sample `k` of `S` shows the sequence at

```
t = (n + ((k + 0.5) / S - 0.5) * shutter) / fps
```

so the shutter is centred on the frame's time; one sample, or a closed shutter, lands exactly on `n / fps`.
Each sample is one engine frame (plus `settleFrames`; plus however long the path tracer needs to reach
`ptSamples`), read back the next frame. The samples are averaged **in linear light** and written once.

- `temporalSamples > 1`, `shutter > 0`: motion blur (camera, and every object or light a track drives).
- `temporalSamples > 1`, `shutter = 0`: the TAA jitter of each engine frame averages into anti-aliasing.
- `temporalSamples = 1`: the captured pixels are written untouched, so a PNG job is the plain capture.

Cost is `frames x temporalSamples` renders. `RenderQueuePanel` shows the count under the Render button.

## Output

- **PNG**: 8-bit RGBA, display-referred (the same pixels `--sequence-capture` writes).
- **EXR**: uncompressed scanline OpenEXR, RGBA, half or float, no third-party code (`Aver.Formats`,
  `ExrWrite.hpp`). The values are **linear light recovered from the 8-bit tonemapped frame**: they are
  display-referred (nothing above 1.0) and carry 8-bit precision, but are correct to composite and grade
  against. A scene-linear HDR readback would need a new RHI capture call; the file side is ready for it.
- **MP4**: H.264 through Windows Media Foundation's sink writer (`Aver.Media`, no third-party code). Needs
  an H.264 encoder (not present on Windows N editions without the Media Feature Pack); where there is
  none, or off Windows, the job refuses with a message and you render PNG or EXR instead. The software
  encoder limits the size to roughly 4096 x 2304.

## Resolution

The window is resized to `width x height` and the scene is rendered into exactly that rect, so the output
is the swapchain's native resolution, not a scaled screenshot. A window cannot be larger than the OS
allows (about the monitor size plus its border); when the output is bigger, the frame renders at the
largest size of the same aspect that fits and is scaled up (bilinear) when written, with a log line. For
a 4K output use a 4K monitor, or accept the scaled result.

## Notes and limits

- The editor UI is hidden while a job renders, so the window shows only the render. Esc (with the window
  focused) cancels.
- A level has one sequence today (`SequenceEditor` loads the first); `sequence` is a name check.
- The camera track must exist: the job fails with "the level has no camera path" otherwise.
- Temporal denoisers and TAA see the pose change between samples. That is what a moving shutter is, but a
  large `shutter` with few samples looks stepped; raise `temporalSamples`, not `shutter`.
- A lost capture (the window still resizing, a readback that never arrives) is rendered again, up to 300
  times in a row, then the job fails.

## Code

| Piece | File |
|---|---|
| Job format | `modules/formats/{include/aver/formats/RenderJob.hpp, src/RenderJob.cpp}` |
| EXR writer/reader | `modules/formats/{include/aver/formats/ExrWrite.hpp, src/ExrWrite.cpp}`; test `tests/formats/src/ExrWriteTest.cpp` |
| MP4 encoder | `modules/media` (`Aver.Media`, `AVER_MODULE_MEDIA`); test `tests/media/src/VideoEncoderTest.cpp` (skips, exit 77, without an encoder) |
| Frame plan, accumulation, PNG/EXR/MP4 writing | `sandbox/src/RenderQueueOutput.{hpp,cpp}` (`RenderRun`) |
| Engine driver | `sandbox/src/SandboxSequenceRun.cpp` (`renderJobStep`, `serviceRenderCapture`) |
| Panel | `sandbox/src/RenderQueuePanel.{hpp,cpp}` |
| Pose at any time | `SequenceEditor::runAt` |
