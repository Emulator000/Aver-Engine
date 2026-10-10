// VideoEncoderTest -- the H.264 MP4 encoder behind the render queue.
//
// Encodes a short moving gradient and checks what a player would: the file exists, is not trivially
// small, opens with an MP4 `ftyp` box, and holds a `moov` box (a file whose sink writer was never
// finalised has no moov and plays nowhere). Exits 77 -- SKIPPED to ctest -- where the machine has no
// H.264 encoder, so a Windows N edition or a non-Windows runner is not reported as a failure.
#include "aver/core/Log.hpp"
#include "aver/media/VideoEncoder.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool contains(const std::vector<unsigned char>& b, const char* tag) {
    const size_t n = std::strlen(tag);
    for (size_t i = 0; i + n <= b.size(); ++i)
        if (std::memcmp(b.data() + i, tag, n) == 0) return true;
    return false;
}

int main() {
    // Refusals do not need an encoder, so they run everywhere.
    {
        std::unique_ptr<media::VideoEncoder> enc;
        std::string why;
        media::VideoEncoderDesc odd;
        odd.width = 101;
        odd.height = 64;
        check(media::VideoEncoder::open("unused.mp4", odd, enc, why) != media::OpenResult::Ok && !enc,
              "odd dimensions are not accepted");
        media::VideoEncoderDesc zero;
        check(media::VideoEncoder::open("unused.mp4", zero, enc, why) != media::OpenResult::Ok && !enc,
              "zero dimensions are not accepted");
    }

    const std::string dir = (std::filesystem::temp_directory_path() / "aver_video_test").string();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string path = dir + "/test.mp4";
    std::filesystem::remove(path, ec);

    media::VideoEncoderDesc d;
    d.width = 128;
    d.height = 96;
    d.fpsNum = 30;
    d.fpsDen = 1;
    d.bitrateKbps = 2000;
    std::unique_ptr<media::VideoEncoder> enc;
    std::string why;
    const media::OpenResult r = media::VideoEncoder::open(path, d, enc, why);
    if (r == media::OpenResult::Unsupported) {
        AVER_INFO("SKIP: no H.264 encoder here ({})", why);
        return g_failures ? 1 : 77;
    }
    check(r == media::OpenResult::Ok, "opens an encoder " + why);
    if (r != media::OpenResult::Ok) {
        AVER_ERROR("=== {} FAILED ===", g_failures);
        return 1;
    }

    std::vector<unsigned char> frame(static_cast<size_t>(d.width) * d.height * 4);
    bool wrote = true;
    for (u32 f = 0; f < 30; ++f) {
        for (u32 y = 0; y < d.height; ++y)
            for (u32 x = 0; x < d.width; ++x) {
                unsigned char* p = &frame[(static_cast<size_t>(y) * d.width + x) * 4];
                p[0] = static_cast<unsigned char>((x * 2 + f * 8) & 255);
                p[1] = static_cast<unsigned char>(y * 2);
                p[2] = static_cast<unsigned char>(f * 8);
                p[3] = 255;
            }
        wrote = enc->addFrame(frame.data()) && wrote;
    }
    check(wrote, "30 frames are accepted");
    check(enc->framesWritten() == 30, "the encoder counts them");
    check(enc->finish(why), "finishes " + why);
    enc.reset();

    std::vector<unsigned char> bytes;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        unsigned char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
        std::fclose(f);
    }
    check(bytes.size() > 1024, "the file has content");
    check(bytes.size() >= 12 && std::memcmp(bytes.data() + 4, "ftyp", 4) == 0, "opens with an MP4 ftyp box");
    check(contains(bytes, "moov"), "has a moov box (the writer was finalised)");
    check(contains(bytes, "avc1") || contains(bytes, "avcC"), "carries H.264 (avc1/avcC)");

    std::filesystem::remove_all(dir, ec);
    if (g_failures == 0) AVER_INFO("=== all video encoder tests passed ===");
    else                 AVER_ERROR("=== {} FAILED ===", g_failures);
    return g_failures == 0 ? 0 : 1;
}
