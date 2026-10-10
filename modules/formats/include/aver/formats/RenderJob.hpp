#pragma once
// .ocrender -- one Movie Render Queue job: which level sequence to render, over which frames, at
// what size and frame rate, with how many temporal samples, into which files.
//
// JSON (read with fmt::parseJson, written by hand here), because a job is a handful of named values
// a person edits or a script generates. Unknown keys are ignored on read and dropped on write; every
// key is optional and falls back to the default below. docs/RENDER_QUEUE.md has the field list.
#include "aver/core/Types.hpp"

#include <string>
#include <string_view>

namespace aver::fmt {

enum class RenderOutputKind : u8 { Png, Exr, Mp4 };

struct RenderJob {
    std::string level;          // level to open; empty = the level already open
    std::string sequence;       // the sequence's name; empty = the level's sequence
    i32 frameStart = 0;         // first output frame
    i32 frameEnd = -1;          // last output frame, inclusive; < 0 = through the end of the sequence
    u32 width = 1920, height = 1080;
    f32 fps = 0;                // output frame rate; 0 = the sequence's own
    i32 warmupFrames = 120;     // engine frames to wait after the level is ready, on frame 0's pose
    i32 settleFrames = 0;       // extra engine frames each temporal sample holds its pose before it is read
    i32 temporalSamples = 1;    // sub-frame renders averaged into one output frame (1..64)
    f32 shutter = 0.5f;         // fraction of the frame interval the shutter is open (0 = no motion blur)
    i32 ptSamples = 64;         // path-tracer samples per pixel each temporal sample waits for (PT view only)
    RenderOutputKind output = RenderOutputKind::Png;
    std::string dir;            // output directory (created); relative paths are relative to the job file
    std::string name = "seq";   // file name stem: <name>_00012.png / .exr, <name>.mp4
    bool exrFloat = false;      // EXR as 32-bit float; false = 16-bit half
    i32 bitrateKbps = 20000;    // MP4 video bitrate

    // Clamps every field into its valid range; MP4 needs even dimensions. Returns the job.
    RenderJob& normalise();
};

// Parses JSON text. False with `why` set on malformed JSON; absent keys keep their defaults.
bool parseRenderJob(std::string_view text, RenderJob& out, std::string* why = nullptr);
bool loadRenderJob(const std::string& path, RenderJob& out, std::string* why = nullptr);
std::string serialiseRenderJob(const RenderJob& job);
bool saveRenderJob(const std::string& path, const RenderJob& job, std::string* why = nullptr);

const char* renderOutputName(RenderOutputKind k);   // "png" | "exr" | "mp4"

} // namespace aver::fmt
