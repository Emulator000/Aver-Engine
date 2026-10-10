#pragma once
// OpenEXR output for the render queue: single-part scanline files, no compression, RGBA as half or
// float. Uncompressed on purpose -- every EXR reader opens it, the writer is a page of code with no
// third-party dependency, and a render queue's EXRs are intermediates a compositor re-saves.
//
// decodeExr reads back exactly what encodeExr writes (uncompressed, scanline, A/B/G/R half or float).
// It is for the round-trip test and for tools checking a render, not a general EXR reader.
#include "aver/core/Types.hpp"

#include <string>
#include <vector>

namespace aver::fmt {

enum class ExrPixelType : u8 { Half, Float };

struct ExrImage {
    u32 width = 0, height = 0;
    ExrPixelType type = ExrPixelType::Half;
    std::vector<f32> rgba;   // width * height * 4, top row first, linear
};

// IEEE binary16 <-> binary32, round-to-nearest-even; overflow goes to infinity, NaN stays NaN.
u16 floatToHalf(f32 v);
f32 halfToFloat(u16 h);

// `rgba` is width*height*4 floats, top row first. Empty on bad arguments (zero size, null data).
std::vector<u8> encodeExr(u32 width, u32 height, const f32* rgba, ExrPixelType type);
bool writeExr(const std::string& path, u32 width, u32 height, const f32* rgba, ExrPixelType type,
              std::string* why = nullptr);

bool decodeExr(const u8* data, usize size, ExrImage& out, std::string* why = nullptr);

} // namespace aver::fmt
