#include "RenderQueueOutput.hpp"

#include "aver/core/Log.hpp"
#include "aver/formats/ExrWrite.hpp"
#include "aver/platform/FileSystem.hpp"

#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace aver::editor {

namespace {

const f32* srgbToLinearTable() {
    static const std::vector<f32> t = [] {
        std::vector<f32> v(256);
        for (int i = 0; i < 256; ++i) {
            const f64 s = i / 255.0;
            v[static_cast<size_t>(i)] = static_cast<f32>(s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4));
        }
        return v;
    }();
    return t.data();
}

u8 linearToSrgb8(f32 lin) {
    static constexpr int kSteps = 16384;   // finer than 8 bits can show even in the darks
    static const std::vector<u8> t = [] {
        std::vector<u8> v(kSteps + 1);
        for (int i = 0; i <= kSteps; ++i) {
            const f64 l = static_cast<f64>(i) / kSteps;
            const f64 s = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
            v[static_cast<size_t>(i)] = static_cast<u8>(std::lround(std::clamp(s, 0.0, 1.0) * 255.0));
        }
        return v;
    }();
    if (!(lin > 0.0f)) return 0;   // also catches NaN
    if (lin >= 1.0f) return 255;
    return t[static_cast<size_t>(lin * kSteps + 0.5f)];
}

void rgbaToLinear(const u8* rgba, size_t pixels, f32* dst) {
    const f32* lut = srgbToLinearTable();
    for (size_t i = 0; i < pixels; ++i) {
        dst[i * 3 + 0] = lut[rgba[i * 4 + 0]];
        dst[i * 3 + 1] = lut[rgba[i * 4 + 1]];
        dst[i * 3 + 2] = lut[rgba[i * 4 + 2]];
    }
}

// Bilinear, pixel centres aligned. Only the fallback when the window cannot be as large as the output.
void resizeLinear(const std::vector<f32>& src, u32 sw, u32 sh, std::vector<f32>& dst, u32 dw, u32 dh) {
    dst.resize(static_cast<size_t>(dw) * dh * 3);
    for (u32 y = 0; y < dh; ++y) {
        const f32 fy = std::clamp((static_cast<f32>(y) + 0.5f) * sh / dh - 0.5f, 0.0f, static_cast<f32>(sh - 1));
        const u32 y0 = static_cast<u32>(fy), y1 = std::min(y0 + 1, sh - 1);
        const f32 ty = fy - static_cast<f32>(y0);
        for (u32 x = 0; x < dw; ++x) {
            const f32 fx = std::clamp((static_cast<f32>(x) + 0.5f) * sw / dw - 0.5f, 0.0f, static_cast<f32>(sw - 1));
            const u32 x0 = static_cast<u32>(fx), x1 = std::min(x0 + 1, sw - 1);
            const f32 tx = fx - static_cast<f32>(x0);
            for (int c = 0; c < 3; ++c) {
                const auto at = [&](u32 xx, u32 yy) { return src[(static_cast<size_t>(yy) * sw + xx) * 3 + c]; };
                const f32 top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
                const f32 bot = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
                dst[(static_cast<size_t>(y) * dw + x) * 3 + c] = top + (bot - top) * ty;
            }
        }
    }
}

#if AVER_MODULE_MEDIA
// 30000/1001-style rates stay exact; anything else is a thousandths fraction.
void rateFraction(f64 fps, u32& num, u32& den) {
    for (const u32 r : {24u, 30u, 48u, 60u, 120u}) {
        if (std::fabs(fps - r * 1000.0 / 1001.0) < 0.005) { num = r * 1000; den = 1001; return; }
    }
    if (std::fabs(fps - std::round(fps)) < 0.001) { num = static_cast<u32>(std::lround(fps)); den = 1; return; }
    num = static_cast<u32>(std::lround(fps * 1000.0));
    den = 1000;
}
#endif

} // namespace

RenderRun::RenderRun() = default;
RenderRun::~RenderRun() {
    std::string ignored;
    end(ignored);
}

bool RenderRun::begin(const fmt::RenderJob& job, const std::string& baseDir, f64 length, f64 seqFps, std::string& why) {
    job_ = job;
    job_.normalise();
    fps_ = job_.fps > 0.0f ? static_cast<f64>(job_.fps) : seqFps;
    if (fps_ <= 0.0) fps_ = 60.0;
    const i64 total = std::max<i64>(1, static_cast<i64>(std::ceil(length * fps_ - 1e-6)));
    if (job_.frameStart >= total) {
        why = "the frame range starts at " + std::to_string(job_.frameStart) + " but the sequence has only " +
              std::to_string(total) + " frame(s) at " + std::to_string(static_cast<int>(std::lround(fps_))) + " fps";
        return false;
    }
    first_ = job_.frameStart;
    last_ = job_.frameEnd < 0 ? total - 1 : std::min<i64>(job_.frameEnd, total - 1);

    std::filesystem::path d = job_.dir.empty() ? std::filesystem::path(baseDir) / "Renders" : std::filesystem::path(job_.dir);
    if (d.is_relative() && !baseDir.empty()) d = std::filesystem::path(baseDir) / d;
    dir_ = d.string();
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    if (!std::filesystem::is_directory(d, ec)) {
        why = "could not create the output directory " + dir_;
        return false;
    }

    written_ = 0;
    acc_.clear();
    raw_.clear();
    count_ = 0;
    encoder_.reset();
    if (job_.output == fmt::RenderOutputKind::Mp4) {
#if AVER_MODULE_MEDIA
        media::VideoEncoderDesc desc;
        desc.width = job_.width;
        desc.height = job_.height;
        desc.bitrateKbps = static_cast<u32>(job_.bitrateKbps);
        rateFraction(fps_, desc.fpsNum, desc.fpsDen);
        const std::string path = (d / (job_.name + ".mp4")).string();
        const media::OpenResult r = media::VideoEncoder::open(path, desc, encoder_, why);
        if (r == media::OpenResult::Unsupported) why += " (this machine cannot encode H.264; render PNG or EXR instead)";
        if (r != media::OpenResult::Ok) return false;
#else
        why = "this build has no video encoder (AVER_MODULE_MEDIA is off); render PNG or EXR instead";
        return false;
#endif
    }
    return true;
}

f64 RenderRun::sampleTime(i64 frame, i32 k) const {
    const i32 s = std::max(1, job_.temporalSamples);
    const f64 shutterOffset = ((static_cast<f64>(k) + 0.5) / s - 0.5) * static_cast<f64>(job_.shutter);
    return (static_cast<f64>(frame) + shutterOffset) / fps_;
}

std::string RenderRun::framePath(i64 frame) const {
    char name[256];
    std::snprintf(name, sizeof name, "%s_%05lld.%s", job_.name.c_str(), static_cast<long long>(frame),
                  fmt::renderOutputName(job_.output));
    return (std::filesystem::path(dir_) / name).string();
}

void RenderRun::addSample(const u8* rgba, u32 w, u32 h) {
    if (!rgba || !w || !h) return;
    const size_t px = static_cast<size_t>(w) * h;
    if (count_ == 0) {
        raw_.assign(rgba, rgba + px * 4);
        accW_ = w;
        accH_ = h;
        count_ = 1;
        return;
    }
    if (w != accW_ || h != accH_) return;   // the window changed size mid-frame: keep what matches the first
    if (acc_.empty()) {
        acc_.resize(px * 3);
        rgbaToLinear(raw_.data(), px, acc_.data());
    }
    const f32* lut = srgbToLinearTable();
    for (size_t i = 0; i < px; ++i) {
        acc_[i * 3 + 0] += lut[rgba[i * 4 + 0]];
        acc_[i * 3 + 1] += lut[rgba[i * 4 + 1]];
        acc_[i * 3 + 2] += lut[rgba[i * 4 + 2]];
    }
    ++count_;
}

bool RenderRun::emit(i64 frame, const u8* rgba8, const std::vector<f32>* lin, u32 w, u32 h, std::string& why) {
    const size_t px = static_cast<size_t>(w) * h;
    const std::string path = framePath(frame);
    switch (job_.output) {
    case fmt::RenderOutputKind::Png: {
        std::vector<u8> tmp;
        if (!rgba8) {
            tmp.resize(px * 4);
            for (size_t i = 0; i < px; ++i) {
                tmp[i * 4 + 0] = linearToSrgb8((*lin)[i * 3 + 0]);
                tmp[i * 4 + 1] = linearToSrgb8((*lin)[i * 3 + 1]);
                tmp[i * 4 + 2] = linearToSrgb8((*lin)[i * 3 + 2]);
                tmp[i * 4 + 3] = 255;
            }
            rgba8 = tmp.data();
        }
        if (!stbi_write_png(path.c_str(), static_cast<int>(w), static_cast<int>(h), 4, rgba8, static_cast<int>(w) * 4)) {
            why = "could not write " + path;
            return false;
        }
        return true;
    }
    case fmt::RenderOutputKind::Exr: {
        std::vector<f32> rgba(px * 4);
        std::vector<f32> tmp;
        if (!lin) {
            tmp.resize(px * 3);
            rgbaToLinear(rgba8, px, tmp.data());
            lin = &tmp;
        }
        for (size_t i = 0; i < px; ++i) {
            rgba[i * 4 + 0] = (*lin)[i * 3 + 0];
            rgba[i * 4 + 1] = (*lin)[i * 3 + 1];
            rgba[i * 4 + 2] = (*lin)[i * 3 + 2];
            rgba[i * 4 + 3] = 1.0f;
        }
        return fmt::writeExr(path, w, h, rgba.data(),
                             job_.exrFloat ? fmt::ExrPixelType::Float : fmt::ExrPixelType::Half, &why);
    }
    case fmt::RenderOutputKind::Mp4: {
#if AVER_MODULE_MEDIA
        std::vector<u8> tmp;
        if (!rgba8) {
            tmp.resize(px * 4);
            for (size_t i = 0; i < px; ++i) {
                tmp[i * 4 + 0] = linearToSrgb8((*lin)[i * 3 + 0]);
                tmp[i * 4 + 1] = linearToSrgb8((*lin)[i * 3 + 1]);
                tmp[i * 4 + 2] = linearToSrgb8((*lin)[i * 3 + 2]);
                tmp[i * 4 + 3] = 255;
            }
            rgba8 = tmp.data();
        }
        if (!encoder_ || !encoder_->addFrame(rgba8)) {
            why = "the video encoder refused frame " + std::to_string(frame);
            return false;
        }
        return true;
#else
        why = "no video encoder in this build";
        return false;
#endif
    }
    }
    return false;
}

bool RenderRun::finishFrame(i64 frame, std::string& why) {
    if (count_ == 0) {
        why = "no image was captured for frame " + std::to_string(frame);
        return false;
    }
    const u32 ow = job_.width, oh = job_.height;
    const bool same = accW_ == ow && accH_ == oh;
    bool ok;
    if (count_ == 1 && same && job_.output != fmt::RenderOutputKind::Exr) {
        ok = emit(frame, raw_.data(), nullptr, ow, oh, why);   // one sample: the captured bytes, untouched
    } else {
        const size_t px = static_cast<size_t>(accW_) * accH_;
        std::vector<f32> lin;
        if (count_ == 1) {
            lin.resize(px * 3);
            rgbaToLinear(raw_.data(), px, lin.data());
        } else {
            lin = acc_;
            const f32 inv = 1.0f / static_cast<f32>(count_);
            for (f32& v : lin) v *= inv;
        }
        if (!same) {
            std::vector<f32> scaled;
            resizeLinear(lin, accW_, accH_, scaled, ow, oh);
            lin.swap(scaled);
        }
        ok = emit(frame, nullptr, &lin, ow, oh, why);
    }
    count_ = 0;
    acc_.clear();
    if (ok) ++written_;
    return ok;
}

bool RenderRun::end(std::string& why) {
#if AVER_MODULE_MEDIA
    if (encoder_) {
        const bool ok = encoder_->finish(why);
        encoder_.reset();
        return ok;
    }
#endif
    (void)why;
    return true;
}

} // namespace aver::editor
