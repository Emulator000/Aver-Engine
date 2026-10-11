// `.ocland` paint layers (LLYR/LWGT): round trip, legacy files, fail-soft parsing, writer refusals.
// CPU only, no device. Exit code = failure count.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcLand.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static fmt::OcLandData makeGrid(u32 n) {
    fmt::OcLandData d;
    d.sampleCount = n;
    d.spacingCm = 100.0f;
    d.heights.resize(static_cast<usize>(n) * n);
    for (u32 iy = 0; iy < n; ++iy)
        for (u32 ix = 0; ix < n; ++ix) d.heights[static_cast<usize>(iy) * n + ix] = static_cast<f32>(iy * 7 + ix);
    return d;
}

static fmt::OcLandData makePainted(u32 n) {
    fmt::OcLandData d = makeGrid(n);
    d.layerNames = {"Grass", "Rock", "Sand_2"};
    const usize nn = static_cast<usize>(n) * n;
    d.weights.assign(nn * 2, 0);
    for (usize i = 0; i < nn; ++i) {
        d.weights[i] = static_cast<u8>(i % 100);            // Rock
        d.weights[nn + i] = static_cast<u8>((i * 3) % 100); // Sand_2
    }
    return d;
}

// Finds a chunk in written bytes and rewrites it via the AVR1 container, so tests can corrupt one chunk.
static std::vector<u8> mutateChunk(const std::vector<u8>& bytes, u32 id, std::vector<u8> payload, bool drop) {
    fmt::Avr1File f;
    std::string why;
    fmt::parseAvr1(bytes.data(), bytes.size(), f, &why);
    fmt::Avr1File g;
    g.subtype = f.subtype;
    for (const fmt::AvrChunk& c : f.chunks) {
        if (c.id == id) {
            if (!drop) g.add(id, payload, c.flags);
        } else {
            g.add(c.id, c.data, c.flags);
        }
    }
    std::vector<u8> out;
    fmt::writeAvr1(g, out, &why);
    return out;
}

int main() {
    AVER_INFO("=== round trip ===");
    {
        const fmt::OcLandData src = makePainted(9);
        check(src.layersValid(), "fixture is valid");
        std::vector<u8> bytes;
        std::string why;
        check(fmt::writeOcLand(src, bytes, &why), "writes (" + why + ")");
        fmt::OcLandData back;
        check(fmt::parseOcLand(bytes.data(), bytes.size(), back, &why), "parses (" + why + ")");
        check(back.layerNames == src.layerNames, "names survive");
        check(back.weights == src.weights, "planes survive");
        check(back.warning.empty(), "no warning");
        check(back.weightAt(1, 3, 2) == src.weights[2 * 9 + 3], "weightAt layer 1");
        check(back.weightAt(2, 3, 2) == src.weights[81 + 2 * 9 + 3], "weightAt layer 2");
        const u32 i = 2 * 9 + 3;
        check(back.weightAt(0, 3, 2) == 255 - (src.weights[i] + src.weights[81 + i]), "weightAt layer 0 = 255 - sum");
        check(back.weightAt(3, 0, 0) == 0 && back.weightAt(0, 99, 0) == 0, "missing layer / off-grid = 0");
    }

    AVER_INFO("=== legacy file ===");
    {
        const fmt::OcLandData plain = makeGrid(9);
        std::vector<u8> a;
        std::string why;
        check(fmt::writeOcLand(plain, a, &why), "writes unpainted");
        fmt::OcLandData back;
        check(fmt::parseOcLand(a.data(), a.size(), back, &why), "parses");
        check(!back.hasLayers() && back.weights.empty() && back.warning.empty(), "unpainted has no layers");
        check(back.weightAt(0, 1, 1) == 255 && back.weightAt(1, 1, 1) == 0, "unpainted is layer 0 everywhere");
        std::vector<u8> b;
        check(fmt::writeOcLand(back, b, &why), "re-saves");
        check(a == b, "re-save is byte-identical (heights and quantisation unchanged)");
        // Painting does not move the heights.
        fmt::OcLandData painted = back;
        painted.layerNames = {"A", "B"};
        painted.weights.assign(81, 10);
        std::vector<u8> c;
        check(fmt::writeOcLand(painted, c, &why), "writes painted");
        fmt::OcLandData cb;
        check(fmt::parseOcLand(c.data(), c.size(), cb, &why) && cb.heights == back.heights, "heights identical with paint");
    }

    AVER_INFO("=== fail soft ===");
    {
        const fmt::OcLandData src = makePainted(9);
        std::vector<u8> bytes;
        std::string why;
        fmt::writeOcLand(src, bytes, &why);

        // Short LWGT
        {
            auto bad = mutateChunk(bytes, fmt::kOcLandChunkWeights, std::vector<u8>(10, 0), false);
            fmt::OcLandData d;
            check(fmt::parseOcLand(bad.data(), bad.size(), d, &why), "short LWGT still loads");
            check(!d.hasLayers() && !d.warning.empty() && d.heights.size() == 81, "layers dropped, warning set, heights kept");
        }
        // Missing LWGT
        {
            auto bad = mutateChunk(bytes, fmt::kOcLandChunkWeights, {}, true);
            fmt::OcLandData d;
            check(fmt::parseOcLand(bad.data(), bad.size(), d, &why) && !d.hasLayers() && !d.warning.empty(), "missing LWGT drops layers");
        }
        // Nine layers
        {
            std::vector<u8> ly{9};
            for (int i = 0; i < 9; ++i) { ly.push_back(2); ly.push_back('L'); ly.push_back(static_cast<u8>('0' + i)); }
            auto bad = mutateChunk(bytes, fmt::kOcLandChunkLayers, ly, false);
            fmt::OcLandData d;
            check(fmt::parseOcLand(bad.data(), bad.size(), d, &why) && !d.hasLayers() && !d.warning.empty(), "9 layers dropped");
        }
        // Bad name
        {
            std::vector<u8> ly{2, 1, 'A', 3, 'a', ' ', 'b'};
            auto bad = mutateChunk(bytes, fmt::kOcLandChunkLayers, ly, false);
            fmt::OcLandData d;
            check(fmt::parseOcLand(bad.data(), bad.size(), d, &why) && !d.hasLayers() && !d.warning.empty(), "bad name dropped");
        }
    }

    AVER_INFO("=== writer refusals ===");
    {
        std::vector<u8> out;
        std::string why;
        fmt::OcLandData over = makePainted(5);
        over.weights[3] = 200;
        over.weights[25 + 3] = 100;
        check(!fmt::writeOcLand(over, out, &why), "sum > 255 refused");
        fmt::OcLandData dup = makePainted(5);
        dup.layerNames[2] = "Grass";
        check(!dup.layersValid() && !fmt::writeOcLand(dup, out, &why), "duplicate name refused");
        fmt::OcLandData badName = makePainted(5);
        badName.layerNames[1] = "has space";
        check(!fmt::writeOcLand(badName, out, &why), "bad name refused");
        fmt::OcLandData wrongSize = makePainted(5);
        wrongSize.weights.pop_back();
        check(!fmt::writeOcLand(wrongSize, out, &why), "wrong plane size refused");
        fmt::OcLandData nine = makeGrid(5);
        for (int i = 0; i < 9; ++i) nine.layerNames.push_back("L" + std::to_string(i));
        nine.weights.assign(8 * 25, 0);
        check(!nine.layersValid() && !fmt::writeOcLand(nine, out, &why), "9 layers refused");
        fmt::OcLandData single = makeGrid(5);
        single.layerNames = {"Only"};
        std::vector<u8> bytes;
        fmt::OcLandData back;
        check(fmt::writeOcLand(single, bytes, &why) && fmt::parseOcLand(bytes.data(), bytes.size(), back, &why) &&
                  back.layerNames.size() == 1 && back.weights.empty(),
              "single-layer table with no weights round trips");
    }

    AVER_INFO("OcLandLayers tests done: {} failure(s)", g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
