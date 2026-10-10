// ExrWriteTest -- the render queue's file side: the OpenEXR writer (header bytes, half and float round
// trips, bad input) and the .ocrender job (round trip, defaults, normalisation, refusals).
#include "aver/core/Log.hpp"
#include "aver/formats/ExrWrite.hpp"
#include "aver/formats/RenderJob.hpp"
#include "aver/platform/FileSystem.hpp"

#include <cmath>
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

// A gradient with values past 1 (HDR), a negative and an exact power of two, so a clamp or a
// mis-ordered channel shows.
static std::vector<f32> testImage(u32 w, u32 h) {
    std::vector<f32> px(static_cast<size_t>(w) * h * 4);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            f32* p = &px[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = static_cast<f32>(x) * 0.5f;          // R: 0 .. well above 1
            p[1] = static_cast<f32>(y) * 0.25f - 1.0f;  // G: includes negatives
            p[2] = 0.125f;                              // B: exactly representable in half
            p[3] = (x + y) % 2 ? 1.0f : 0.5f;           // A
        }
    return px;
}

int main() {
    AVER_INFO("=== half <-> float ===");
    {
        check(fmt::floatToHalf(0.0f) == 0x0000, "0");
        check(fmt::floatToHalf(1.0f) == 0x3C00, "1");
        check(fmt::floatToHalf(-2.0f) == 0xC000, "-2");
        check(fmt::floatToHalf(65504.0f) == 0x7BFF, "largest finite half");
        check(fmt::floatToHalf(1e9f) == 0x7C00, "overflow -> +inf");
        check(fmt::floatToHalf(5.9604645e-8f) == 0x0001, "smallest subnormal");
        check(fmt::halfToFloat(0x3C00) == 1.0f && fmt::halfToFloat(0x0001) == 5.9604645e-8f, "reads back");
        bool exact = true;   // every half converts to float and back to itself (NaNs aside)
        for (u32 h = 0; h < 0x10000; ++h) {
            const u16 hv = static_cast<u16>(h);
            if ((hv & 0x7C00) == 0x7C00 && (hv & 0x3FF)) continue;
            if (fmt::floatToHalf(fmt::halfToFloat(hv)) != hv) { exact = false; break; }
        }
        check(exact, "all 63k finite halves round-trip exactly");
        check(std::isnan(fmt::halfToFloat(fmt::floatToHalf(NAN))), "NaN stays NaN");
        check(fmt::floatToHalf(1.0f + 1.0f / 2048.0f) == 0x3C00, "ties round to even (down)");
        check(fmt::floatToHalf(1.0f + 3.0f / 2048.0f) == 0x3C02, "ties round to even (up)");
    }

    AVER_INFO("=== EXR header ===");
    {
        const std::vector<f32> px = testImage(5, 3);
        const std::vector<u8> bytes = fmt::encodeExr(5, 3, px.data(), fmt::ExrPixelType::Half);
        check(bytes.size() > 8, "encodes");
        const u8 magic[4] = {0x76, 0x2f, 0x31, 0x01};
        check(bytes.size() >= 8 && std::memcmp(bytes.data(), magic, 4) == 0, "magic number 20000630");
        u32 ver = 0;
        if (bytes.size() >= 8) std::memcpy(&ver, bytes.data() + 4, 4);
        check(ver == 2, "version 2, no tiled/long-name/deep/multipart flags");
        const std::string s(bytes.begin(), bytes.end());
        check(s.find(std::string("channels\0chlist\0", 16)) != std::string::npos, "channel list present");
        check(s.find(std::string("compression\0compression\0", 24)) != std::string::npos, "compression attribute");
        check(s.find(std::string("dataWindow\0box2i\0", 17)) != std::string::npos, "data window");
        check(s.find(std::string("displayWindow\0box2i\0", 20)) != std::string::npos, "display window");
        check(s.find(std::string("lineOrder\0lineOrder\0", 20)) != std::string::npos, "line order");
        // 4 channels * 5 px * 2 bytes + 8 byte block header per line, plus the offset table.
        check(bytes.size() > 3u * (8 + 5 * 4 * 2) + 3 * 8, "one block per scanline");
        check(fmt::encodeExr(0, 3, px.data(), fmt::ExrPixelType::Half).empty(), "zero width -> empty");
        check(fmt::encodeExr(5, 3, nullptr, fmt::ExrPixelType::Half).empty(), "null pixels -> empty");
    }

    AVER_INFO("=== round trips ===");
    {
        const u32 w = 37, h = 19;
        const std::vector<f32> px = testImage(w, h);
        for (const fmt::ExrPixelType t : {fmt::ExrPixelType::Half, fmt::ExrPixelType::Float}) {
            const bool half = t == fmt::ExrPixelType::Half;
            const std::vector<u8> bytes = fmt::encodeExr(w, h, px.data(), t);
            fmt::ExrImage img;
            std::string why;
            const bool ok = fmt::decodeExr(bytes.data(), bytes.size(), img, &why);
            check(ok, std::string(half ? "half" : "float") + " decodes " + why);
            if (!ok) continue;
            check(img.width == w && img.height == h && img.type == t, "size and type survive");
            bool same = img.rgba.size() == px.size();
            for (size_t i = 0; same && i < px.size(); ++i) {
                const f32 want = half ? fmt::halfToFloat(fmt::floatToHalf(px[i])) : px[i];
                if (img.rgba[i] != want) same = false;
            }
            check(same, std::string(half ? "half" : "float") + " pixels (values past 1 and negatives kept)");
        }
        // Truncation and corruption must fail cleanly, not read out of bounds.
        const std::vector<u8> bytes = fmt::encodeExr(w, h, px.data(), fmt::ExrPixelType::Half);
        fmt::ExrImage img;
        std::string why;
        check(!fmt::decodeExr(bytes.data(), bytes.size() / 2, img, &why), "a truncated file is refused");
        std::vector<u8> bad = bytes;
        bad[0] = 0;
        check(!fmt::decodeExr(bad.data(), bad.size(), img, &why), "bad magic is refused");
        check(!fmt::decodeExr(bytes.data(), 3, img, &why), "three bytes is refused");
    }

    AVER_INFO("=== file ===");
    {
        const std::string dir = (std::filesystem::temp_directory_path() / "aver_exr_test").string();
        createDirectories(dir);
        const std::string path = dir + "/frame.exr";
        const std::vector<f32> px = testImage(8, 4);
        check(fmt::writeExr(path, 8, 4, px.data(), fmt::ExrPixelType::Float), "writes a file");
        std::vector<u8> back;
        check(readFileBytes(path, back) && !back.empty(), "reads it back");
        fmt::ExrImage img;
        check(fmt::decodeExr(back.data(), back.size(), img) && img.rgba == px, "the file decodes to the input");
        std::string why;
        check(!fmt::writeExr(dir + "/no/such/dir/x.exr", 8, 4, px.data(), fmt::ExrPixelType::Half, &why) && !why.empty(),
              "an unwritable path reports a reason");
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    AVER_INFO("=== .ocrender job ===");
    {
        fmt::RenderJob d;
        check(d.width == 1920 && d.height == 1080 && d.temporalSamples == 1 && d.output == fmt::RenderOutputKind::Png,
              "defaults: 1080p, one sample, PNG");

        fmt::RenderJob j;
        j.level = "Content/Levels/A \"quoted\" \\ level.ocworld";
        j.sequence = "Intro";
        j.frameStart = 10;
        j.frameEnd = 99;
        j.width = 1280;
        j.height = 720;
        j.fps = 24.0f;
        j.warmupFrames = 30;
        j.settleFrames = 2;
        j.temporalSamples = 8;
        j.shutter = 0.25f;
        j.ptSamples = 128;
        j.output = fmt::RenderOutputKind::Exr;
        j.dir = "out/shot 01";
        j.name = "shot";
        j.exrFloat = true;
        j.bitrateKbps = 8000;
        fmt::RenderJob r;
        std::string why;
        check(fmt::parseRenderJob(fmt::serialiseRenderJob(j), r, &why), "the written job parses " + why);
        check(r.level == j.level && r.sequence == j.sequence && r.dir == j.dir && r.name == j.name, "strings survive, escapes included");
        check(r.frameStart == 10 && r.frameEnd == 99 && r.width == 1280 && r.height == 720 && r.fps == 24.0f, "range, size, rate");
        check(r.warmupFrames == 30 && r.settleFrames == 2 && r.temporalSamples == 8 && r.shutter == 0.25f && r.ptSamples == 128,
              "sampling");
        check(r.output == fmt::RenderOutputKind::Exr && r.exrFloat && r.bitrateKbps == 8000, "output");
        check(fmt::serialiseRenderJob(r) == fmt::serialiseRenderJob(j), "byte-stable");

        fmt::RenderJob sparse;
        check(fmt::parseRenderJob("{\"width\": 640, \"unknown\": [1,2,3]}", sparse, &why), "a sparse job with an unknown key parses");
        check(sparse.width == 640 && sparse.height == 1080, "absent keys keep their defaults");

        fmt::RenderJob mp4;
        check(fmt::parseRenderJob("{\"width\": 1281, \"height\": 721, \"output\": {\"kind\": \"mp4\"}}", mp4), "mp4 job");
        check(mp4.width == 1280 && mp4.height == 720, "MP4 dimensions are made even");

        fmt::RenderJob wild;
        check(fmt::parseRenderJob("{\"temporalSamples\": 9999, \"shutter\": 7, \"frameStart\": -5, \"frameEnd\": 2, "
                                  "\"width\": 1, \"ptSamples\": 0}", wild), "out-of-range values parse");
        check(wild.temporalSamples == 64 && wild.shutter == 1.0f && wild.frameStart == 0 && wild.width == 16 && wild.ptSamples == 1,
              "...and are clamped");
        fmt::RenderJob swapped;
        fmt::parseRenderJob("{\"frameStart\": 30, \"frameEnd\": 5}", swapped);
        check(swapped.frameEnd == 30, "an end before the start is raised to the start");

        fmt::RenderJob bad;
        check(!fmt::parseRenderJob("[1,2]", bad, &why), "a non-object is refused");
        check(!fmt::parseRenderJob("{\"output\": {\"kind\": \"gif\"}}", bad, &why), "an unknown output kind is refused");
        check(!fmt::parseRenderJob("{\"width\": ", bad, &why), "malformed JSON is refused");

        const std::string dir = (std::filesystem::temp_directory_path() / "aver_job_test").string();
        createDirectories(dir);
        const std::string path = dir + "/a.ocrender";
        check(fmt::saveRenderJob(path, j), "saves");
        fmt::RenderJob loaded;
        check(fmt::loadRenderJob(path, loaded, &why) && loaded.name == "shot", "loads");
        check(!fmt::loadRenderJob(dir + "/missing.ocrender", loaded, &why), "a missing file is refused");
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    if (g_failures == 0) AVER_INFO("=== all EXR / render job tests passed ===");
    else                 AVER_ERROR("=== {} FAILED ===", g_failures);
    return g_failures == 0 ? 0 : 1;
}
