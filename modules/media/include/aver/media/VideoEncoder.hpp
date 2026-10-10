#pragma once
// Aver.Media -- video file output for the render queue: H.264 in an MP4 container.
//
// On Windows this is Media Foundation's IMFSinkWriter, which ships with the OS and brings no
// third-party code. Elsewhere, and on a Windows without an H.264 encoder (the N editions), open()
// reports Unsupported and the caller falls back to an image sequence.
//
// One encoder is one file, written on the calling thread. Frames arrive in order, top row first,
// RGBA8 -- the layout the editor's frame capture already has.
#include "aver/core/Types.hpp"

#include <memory>
#include <string>

namespace aver::media {

struct VideoEncoderDesc {
    u32 width = 0, height = 0;   // pixels; H.264 needs both even
    u32 fpsNum = 30, fpsDen = 1; // frame rate as a fraction (29.97 = 30000 / 1001)
    u32 bitrateKbps = 20000;
};

enum class OpenResult : u8 {
    Ok,
    Unsupported,   // no encoder on this machine or platform: not an error worth failing a build over
    Failed,        // bad arguments or an I/O error; `why` says which
};

class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;

    // Creates `path` (UTF-8). On anything but Ok, `out` is null and `why` is set.
    static OpenResult open(const std::string& path, const VideoEncoderDesc& desc,
                           std::unique_ptr<VideoEncoder>& out, std::string& why);

    // Appends one frame: width * height RGBA8 pixels, rows `strideBytes` apart (0 = tight).
    virtual bool addFrame(const u8* rgba, u32 strideBytes = 0) = 0;
    // Flushes and closes the file. The encoder is unusable afterwards. False if the file is incomplete.
    virtual bool finish(std::string& why) = 0;
    virtual u32 framesWritten() const = 0;
};

} // namespace aver::media
