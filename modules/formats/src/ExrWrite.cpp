#include "aver/formats/ExrWrite.hpp"

#include "aver/platform/FileSystem.hpp"

#include <cstring>

namespace aver::fmt {

namespace {

// Little-endian appends; every supported host is little-endian.
void put(std::vector<u8>& b, const void* p, usize n) {
    const u8* s = static_cast<const u8*>(p);
    b.insert(b.end(), s, s + n);
}
void putI32(std::vector<u8>& b, i32 v) { put(b, &v, 4); }
void putF32(std::vector<u8>& b, f32 v) { put(b, &v, 4); }
void putStr(std::vector<u8>& b, const char* s) { put(b, s, std::strlen(s) + 1); }   // with the NUL

void attrHeader(std::vector<u8>& b, const char* name, const char* type, i32 size) {
    putStr(b, name);
    putStr(b, type);
    putI32(b, size);
}

// Channels are stored alphabetically: A, B, G, R. kSrc maps each to its slot in the RGBA input.
constexpr const char* kChannelName[4] = {"A", "B", "G", "R"};
constexpr int kSrc[4] = {3, 2, 1, 0};

constexpr u8 kMagic[4] = {0x76, 0x2f, 0x31, 0x01};

bool fail(std::string* why, const char* msg) {
    if (why) *why = msg;
    return false;
}

} // namespace

// floatToHalf is IesProfile.cpp's (one definition, declared in both headers).

f32 halfToFloat(u16 h) {
    const u32 sign = (static_cast<u32>(h) & 0x8000u) << 16;
    u32 exp = (h >> 10) & 0x1Fu;
    u32 mant = h & 0x3FFu;
    u32 x;
    if (exp == 0) {
        if (mant == 0) {
            x = sign;
        } else {                                  // subnormal: renormalise
            exp = 127 - 15 + 1;
            while (!(mant & 0x400u)) { mant <<= 1; --exp; }
            mant &= 0x3FFu;
            x = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        x = sign | 0x7F800000u | (mant << 13);
    } else {
        x = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    f32 f;
    std::memcpy(&f, &x, 4);
    return f;
}

std::vector<u8> encodeExr(u32 width, u32 height, const f32* rgba, ExrPixelType type) {
    std::vector<u8> out;
    if (width == 0 || height == 0 || !rgba) return out;
    const u32 bps = type == ExrPixelType::Half ? 2u : 4u;
    const u32 lineBytes = width * 4u * bps;

    put(out, kMagic, 4);
    const u32 version = 2;   // single part, scanline, short names
    put(out, &version, 4);

    {   // channels
        std::vector<u8> ch;
        for (int c = 0; c < 4; ++c) {
            putStr(ch, kChannelName[c]);
            putI32(ch, type == ExrPixelType::Half ? 1 : 2);   // HALF = 1, FLOAT = 2
            const u8 pLinear[4] = {static_cast<u8>(c == 0 ? 1 : 0), 0, 0, 0};   // + 3 reserved bytes
            put(ch, pLinear, 4);
            putI32(ch, 1);
            putI32(ch, 1);
        }
        ch.push_back(0);
        attrHeader(out, "channels", "chlist", static_cast<i32>(ch.size()));
        put(out, ch.data(), ch.size());
    }
    attrHeader(out, "compression", "compression", 1);
    out.push_back(0);   // NO_COMPRESSION
    const i32 box[4] = {0, 0, static_cast<i32>(width) - 1, static_cast<i32>(height) - 1};
    attrHeader(out, "dataWindow", "box2i", 16);
    put(out, box, 16);
    attrHeader(out, "displayWindow", "box2i", 16);
    put(out, box, 16);
    attrHeader(out, "lineOrder", "lineOrder", 1);
    out.push_back(0);   // INCREASING_Y
    attrHeader(out, "pixelAspectRatio", "float", 4);
    putF32(out, 1.0f);
    attrHeader(out, "screenWindowCenter", "v2f", 8);
    putF32(out, 0.0f);
    putF32(out, 0.0f);
    attrHeader(out, "screenWindowWidth", "float", 4);
    putF32(out, 1.0f);
    out.push_back(0);   // end of header

    // Offset table, then one block per scanline: y, byte count, the four channels one after another.
    const usize tableAt = out.size();
    out.resize(tableAt + static_cast<usize>(height) * 8);
    out.reserve(out.size() + static_cast<usize>(height) * (8u + lineBytes));
    for (u32 y = 0; y < height; ++y) {
        const u64 at = out.size();
        std::memcpy(out.data() + tableAt + static_cast<usize>(y) * 8, &at, 8);
        putI32(out, static_cast<i32>(y));
        putI32(out, static_cast<i32>(lineBytes));
        const f32* row = rgba + static_cast<usize>(y) * width * 4;
        for (int c = 0; c < 4; ++c) {
            const usize base = out.size();
            out.resize(base + static_cast<usize>(width) * bps);
            u8* dst = out.data() + base;
            for (u32 x = 0; x < width; ++x) {
                const f32 v = row[static_cast<usize>(x) * 4 + kSrc[c]];
                if (type == ExrPixelType::Half) {
                    const u16 h = floatToHalf(v);
                    std::memcpy(dst + static_cast<usize>(x) * 2, &h, 2);
                } else {
                    std::memcpy(dst + static_cast<usize>(x) * 4, &v, 4);
                }
            }
        }
    }
    return out;
}

bool writeExr(const std::string& path, u32 width, u32 height, const f32* rgba, ExrPixelType type,
              std::string* why) {
    const std::vector<u8> bytes = encodeExr(width, height, rgba, type);
    if (bytes.empty()) return fail(why, "empty image");
    const bool ok = writeFileBytes(path, bytes.data(), bytes.size());
    return ok ? true : fail(why, "could not write the file");
}

namespace {

struct Reader {
    const u8* p;
    usize n;
    usize at = 0;
    bool ok = true;
    bool take(void* dst, usize k) {
        if (!ok || k > n - at) { ok = false; return false; }
        std::memcpy(dst, p + at, k);
        at += k;
        return true;
    }
    i32 i32v() { i32 v = 0; take(&v, 4); return v; }
    bool cstr(std::string& s, usize cap = 256) {
        s.clear();
        while (ok) {
            if (at >= n || s.size() >= cap) { ok = false; return false; }
            const char c = static_cast<char>(p[at++]);
            if (c == 0) return true;
            s.push_back(c);
        }
        return false;
    }
};

} // namespace

bool decodeExr(const u8* data, usize size, ExrImage& out, std::string* why) {
    Reader r{data, size};
    u8 magic[4] = {};
    u32 version = 0;
    if (!r.take(magic, 4) || std::memcmp(magic, kMagic, 4) != 0) return fail(why, "not an OpenEXR file");
    if (!r.take(&version, 4) || (version & 0xFFu) != 2 || (version & ~0xFFu) != 0)
        return fail(why, "unsupported EXR version or flags (tiled, deep or multipart)");

    struct Chan { std::string name; i32 type = 0; };
    std::vector<Chan> chans;
    i32 box[4] = {0, 0, -1, -1};
    int compression = -1, lineOrder = 0;
    for (;;) {
        std::string name, type;
        if (!r.cstr(name)) return fail(why, "truncated header");
        if (name.empty()) break;
        if (!r.cstr(type)) return fail(why, "truncated header");
        const i32 sz = r.i32v();
        if (!r.ok || sz < 0 || static_cast<usize>(sz) > size - r.at) return fail(why, "bad attribute size");
        const usize end = r.at + static_cast<usize>(sz);
        if (name == "channels") {
            for (;;) {
                Chan c;
                if (!r.cstr(c.name, 32)) return fail(why, "bad channel list");
                if (c.name.empty()) break;
                c.type = r.i32v();
                u8 skip[4];
                r.take(skip, 4);
                const i32 xs = r.i32v(), ys = r.i32v();
                if (!r.ok) return fail(why, "bad channel list");
                if (xs != 1 || ys != 1) return fail(why, "subsampled channels are unsupported");
                chans.push_back(c);
            }
        } else if (name == "compression") {
            u8 c = 0xFF;
            r.take(&c, 1);
            compression = c;
        } else if (name == "dataWindow") {
            r.take(box, 16);
        } else if (name == "lineOrder") {
            u8 c = 0;
            r.take(&c, 1);
            lineOrder = c;
        }
        if (!r.ok) return fail(why, "truncated header");
        r.at = end;
    }
    if (compression != 0) return fail(why, "compressed EXR is unsupported");
    if (lineOrder != 0) return fail(why, "only increasing-Y line order is supported");
    if (box[2] < box[0] || box[3] < box[1]) return fail(why, "empty data window");
    const u64 w = static_cast<u64>(static_cast<i64>(box[2]) - box[0] + 1);
    const u64 h = static_cast<u64>(static_cast<i64>(box[3]) - box[1] + 1);
    if (w > 65536 || h > 65536) return fail(why, "image too large");
    if (chans.empty()) return fail(why, "no channels");

    usize lineBytes = 0;
    std::vector<usize> chanOffset;
    for (const Chan& c : chans) {
        if (c.type != 1 && c.type != 2) return fail(why, "only HALF and FLOAT channels are supported");
        chanOffset.push_back(lineBytes);
        lineBytes += static_cast<usize>(w) * (c.type == 1 ? 2u : 4u);
    }
    std::vector<u64> table(static_cast<usize>(h));
    if (!r.take(table.data(), table.size() * 8)) return fail(why, "truncated offset table");

    out.width = static_cast<u32>(w);
    out.height = static_cast<u32>(h);
    out.type = chans[0].type == 1 ? ExrPixelType::Half : ExrPixelType::Float;
    out.rgba.assign(static_cast<usize>(w) * h * 4, 0.0f);
    for (usize i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 1.0f;   // alpha defaults to opaque

    for (usize y = 0; y < table.size(); ++y) {
        if (table[y] > size || size - table[y] < 8) return fail(why, "bad scanline offset");
        Reader b{data, size, static_cast<usize>(table[y])};
        const i32 yy = b.i32v(), bytes = b.i32v();
        if (yy != box[1] + static_cast<i32>(y) || bytes < 0 || static_cast<usize>(bytes) != lineBytes ||
            lineBytes > size - b.at)
            return fail(why, "bad scanline block");
        const u8* line = data + b.at;
        for (usize c = 0; c < chans.size(); ++c) {
            const std::string& nm = chans[c].name;
            const int slot = nm == "R" ? 0 : nm == "G" ? 1 : nm == "B" ? 2 : nm == "A" ? 3 : -1;
            if (slot < 0) continue;
            const u8* src = line + chanOffset[c];
            f32* dst = out.rgba.data() + y * static_cast<usize>(w) * 4 + slot;
            for (usize x = 0; x < w; ++x) {
                if (chans[c].type == 1) {
                    u16 hv;
                    std::memcpy(&hv, src + x * 2, 2);
                    dst[x * 4] = halfToFloat(hv);
                } else {
                    std::memcpy(&dst[x * 4], src + x * 4, 4);
                }
            }
        }
    }
    return true;
}

} // namespace aver::fmt
