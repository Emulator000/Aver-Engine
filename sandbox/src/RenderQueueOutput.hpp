#pragma once
// The Movie Render Queue's frame plan and output side (docs/RENDER_QUEUE.md): which sequence times a
// job samples, the accumulation of sub-frame captures into one output frame, and the PNG / EXR / MP4
// writers. No device and no scene in here; SandboxSequenceRun.cpp drives the engine and hands it the
// captured frames.
#include "aver/core/Types.hpp"
#include "aver/formats/RenderJob.hpp"

#include <memory>
#include <string>
#include <vector>

// AVER_MODULE_MEDIA arrives through the link line (Aver.Media's PUBLIC_DEFS). Without it the encoder
// member is a never-created stub, so the class has the same shape in both builds.
#if AVER_MODULE_MEDIA
#include "aver/media/VideoEncoder.hpp"
#else
namespace aver::media { class VideoEncoder {}; }
#endif

namespace aver::editor {

class RenderRun {
public:
    RenderRun();
    ~RenderRun();
    RenderRun(const RenderRun&) = delete;
    RenderRun& operator=(const RenderRun&) = delete;

    // Plans `job` against a sequence `length` seconds long that authors `seqFps`. Relative job.dir is
    // taken from baseDir (an empty dir means baseDir/Renders). Creates the directory and, for MP4,
    // the encoder. False with `why` when the job cannot run.
    bool begin(const fmt::RenderJob& job, const std::string& baseDir, f64 length, f64 seqFps, std::string& why);

    const fmt::RenderJob& job() const { return job_; }
    f64 fps() const { return fps_; }
    i64 firstFrame() const { return first_; }
    i64 lastFrame() const { return last_; }                 // inclusive
    i64 frameCount() const { return last_ - first_ + 1; }
    i32 samples() const { return job_.temporalSamples; }
    // Seconds on the sequence timeline for temporal sample k of output frame n: the shutter is centred
    // on the frame's time, so one sample (or a closed shutter) lands exactly on n / fps.
    f64 sampleTime(i64 frame, i32 k) const;

    // One captured RGBA8 frame (top row first, sRGB). Samples of a frame add up in linear light.
    void addSample(const u8* rgba, u32 w, u32 h);
    // Averages, resizes to the job's size and writes the frame. Clears the accumulator.
    bool finishFrame(i64 frame, std::string& why);
    // Closes the MP4, if any. Safe to call twice.
    bool end(std::string& why);

    u32 written() const { return written_; }
    std::string framePath(i64 frame) const;
    const std::string& dir() const { return dir_; }

private:
    bool emit(i64 frame, const u8* rgba8, const std::vector<f32>* linearRgb, u32 w, u32 h, std::string& why);

    fmt::RenderJob job_;
    f64 fps_ = 60;
    i64 first_ = 0, last_ = 0;
    std::string dir_;
    std::unique_ptr<media::VideoEncoder> encoder_;
    std::vector<f32> acc_;   // linear RGB sum, capture size
    std::vector<u8> raw_;    // the single sample, kept as captured when no averaging is needed
    u32 accW_ = 0, accH_ = 0;
    i32 count_ = 0;
    u32 written_ = 0;
};

} // namespace aver::editor
