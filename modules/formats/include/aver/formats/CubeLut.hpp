#pragma once
// Adobe/Resolve .cube 3D colour LUTs, parsed into an N^3 RGBA16F array ready to upload as a 3D texture.
// Text and arithmetic only: no device. The runtime turns the array into a texture
// (Runtime/include/aver/game/GamePostVolumes.hpp); the post pass samples it after the tonemap, so the
// array always covers an input range of exactly [0, 1] -- a file with another DOMAIN is resampled.
// Texel order: red fastest, then green, then blue (index = r + N * (g + N * b)), which is the .cube
// data order and the order a Tex3D upload reads (x fastest, z slowest).
#include "aver/core/Types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::fmt {

constexpr u32 kCubeLutMinSize = 2;
constexpr u32 kCubeLutMaxSize = 129;   // 129^3 RGBA16F is 17 MB; real grades are 17, 33 or 65

struct CubeLut {
    std::string title;
    u32 size = 0;                      // N: the cube is N x N x N
    f32 domainMin[3] = {0, 0, 0};      // as the file stated them (informational: the data below is resampled to 0..1)
    f32 domainMax[3] = {1, 1, 1};
    std::vector<u16> rgba16f;          // size^3 * 4 IEEE halves, alpha 1

    bool valid() const {
        return size >= kCubeLutMinSize && rgba16f.size() == static_cast<usize>(size) * size * size * 4;
    }
};

// Parses .cube text. Refuses (false, reason in *err) 1D LUTs, a missing or out-of-range LUT_3D_SIZE, a data
// count that is not size^3, and non-numeric data. Unknown keywords are skipped.
bool parseCubeLut(std::string_view text, CubeLut& out, std::string* err = nullptr);

// Reads a file and parses it.
bool loadCubeLut(const std::string& path, CubeLut& out, std::string* err = nullptr);

// The identity LUT of the given size as .cube text (a no-op grade); for tests and as a starting point to grade from.
std::string writeIdentityCube(u32 size, const char* title = "identity");

} // namespace aver::fmt
