// Decodes an image file and creates the GPU texture for it.
#include "aver/assets/TextureUpload.hpp"

#include "TextureUploadDetail.hpp"
#include "aver/assets/TextureCache.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/Bc7.hpp"
#include "aver/formats/Texture.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_set>

namespace aver::assets {

namespace {

// The alpha-test reference coverage is preserved AT, shared with the BC7 encoder's own cutout
// weighting below (Bc7Options::cutoff defaults to 128, which IS 0.5 in the u8 scale
// generateMipChain's preserveCoverageAt works in) -- one number, so the mip chain and the block
// encoder agree on where "the leaf's edge" actually is.
constexpr f32 kCoverageReferenceAlpha = 0.5f;

// The last level is 1x1 (generateMipChain loops until both extents are 1). Its texel is stored in
// the texture's OWN encoding, so an sRGB texture needs decoding back to linear -- downsample()
// averaged in linear and then re-encoded, and handing the encoded byte straight out would report a
// colour roughly twice as bright as the texture really is. Shared by every upload path (RGBA8 and
// BC7 alike): the mean is read off `tex`'s own decoded chain before either path throws it away.
void averageLinearFromLastMip(const fmt::TextureData& tex, f32 out[3]) {
    const ImageData& last = tex.levels.back();
    if (last.pixels.size() < 4) return;
    for (int c = 0; c < 3; ++c) {
        const f32 v = static_cast<f32>(last.pixels[c]) / 255.0f;
        out[c] = tex.srgb ? (v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f)) : v;
    }
}

// "Any failure falls back to today's RGBA8 path, logged once per file": a texture that fails to
// compress on every resolve (a material preview re-requesting the same fallback icon, say) must
// not spam the log once per resolve. Keyed on the raw path string, which is exactly what the
// caller already deduplicates ON when a hit skips this file entirely next time.
std::mutex g_loggedFailuresMutex;
std::unordered_set<std::string> g_loggedFailures;
void logCompressionFailureOnce(const std::string& path, std::string_view why) {
    {
        std::lock_guard<std::mutex> lock(g_loggedFailuresMutex);
        if (!g_loggedFailures.insert(path).second) return;
    }
    AVER_WARN("[Texture] {} will not compress ({}); using uncompressed RGBA8", path, why);
}

// Uploads an already-built BC7 chain (either just baked or read straight from the cache) as a
// texture. 0 on failure, with nothing to undo either way: the caller still holds whatever it needs
// to fall back.
rhi::TextureHandle uploadCachedTexture(rhi::IResourceFactory& res, const std::string& path,
                                       const CachedTexture& cached, TextureUploadInfo* info) {
    std::vector<const void*> levels;
    levels.reserve(cached.mipBlocks.size());
    usize bytes = 0;
    for (const std::vector<u8>& mip : cached.mipBlocks) {
        levels.push_back(mip.data());
        bytes += mip.size();
    }

    rhi::TextureDesc d;
    d.width  = cached.width;
    d.height = cached.height;
    d.mips   = cached.mips;
    d.format = cached.format;
    d.bind   = rhi::ResourceBind::ShaderResource;
    d.initialState = rhi::ResourceState::ShaderResource;
    d.initialData      = levels.data();
    d.initialDataCount = static_cast<u32>(levels.size());
    d.initialRowPitch  = 0;   // every level is tightly packed BC7 blocks; see packedRowPitch/Vk
    d.debugName = path.c_str();

    const rhi::TextureHandle h = res.createTexture(d);
    if (!h) return 0;

    if (info) {
        *info = TextureUploadInfo{cached.width, cached.height, cached.mips, bytes};
        for (int c = 0; c < 3; ++c) info->averageLinear[c] = cached.averageLinear[c];
    }
    return h;
}

// The miss path: `tex` already holds a full decoded RGBA8 mip chain (coverage-preserved already,
// if `cutout` asked for it). Encodes every level to BC7, writes the cache entry, then uploads.
// 0 on failure -- the caller falls back to the RGBA8 chain it already decoded, so nothing here
// needs to be undone.
rhi::TextureHandle uploadCompressed(rhi::IResourceFactory& res, const std::string& path,
                                    TextureUsage usage, const fmt::TextureData& tex, bool cutout,
                                    TextureUploadInfo* info) {
    const auto t0 = std::chrono::steady_clock::now();

    fmt::Bc7Options bc7opt;
    bc7opt.cutout = cutout;   // default cutoff (128) IS kCoverageReferenceAlpha in the u8 scale

    CachedTexture cached;
    cached.format = tex.srgb ? rhi::Format::BC7UnormSrgb : rhi::Format::BC7Unorm;
    cached.width  = tex.width;
    cached.height = tex.height;
    cached.mips   = static_cast<u32>(tex.levels.size());
    cached.coveragePreserved = cutout;
    averageLinearFromLastMip(tex, cached.averageLinear);

    cached.mipBlocks.resize(tex.levels.size());
    for (usize m = 0; m < tex.levels.size(); ++m)
        fmt::encodeBc7Image(tex.levels[m], cached.mipBlocks[m], bc7opt);

    const auto t1 = std::chrono::steady_clock::now();
    usize totalBytes = 0;
    for (const std::vector<u8>& mip : cached.mipBlocks) totalBytes += mip.size();
    AVER_INFO("[Texture] compressed {} to BC7: {} KiB ({} ms)", path, totalBytes / 1024,
              std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    if (!saveCachedTexture(path, usage, cached))
        AVER_WARN("[Texture] {} compressed but could not be written to the derived-data cache", path);

    return uploadCachedTexture(res, path, cached, info);
}

} // namespace

// ---- the decode / resize / mip helpers shared with the atlas builder (TextureUploadDetail.hpp) ----
namespace detail {

bool decodeTexture(const std::string& path, TextureUsage usage, fmt::TextureData& out, std::string* err) {
    fmt::TextureLoadOptions opt;
    opt.srgb        = usage == TextureUsage::Colour;
    opt.normalMap   = usage == TextureUsage::NormalMap;
    opt.generateMips = false;   // the caller decides how mips are built
    return fmt::loadTexture(path, out, opt, err);
}

void buildMips(fmt::TextureData& t, TextureUsage usage) {
    fmt::generateMipChain(t, usage == TextureUsage::NormalMap);
}

namespace {
// sRGB <-> linear, exact piecewise curve; a 256-entry table for the decode.
const f32* srgbTable() {
    static const std::array<f32, 256> t = [] {
        std::array<f32, 256> a{};
        for (int i = 0; i < 256; ++i) {
            const f32 c = static_cast<f32>(i) / 255.0f;
            a[static_cast<usize>(i)] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return a;
    }();
    return t.data();
}
u8 encodeSrgb(f32 c) {
    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    const f32 s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<u8>(s * 255.0f + 0.5f);
}
} // namespace

void resizeBox(const ImageData& src, u32 w, u32 h, TextureUsage usage, ImageData& out) {
    out = ImageData{};
    out.width = w; out.height = h; out.channels = src.channels; out.srgb = src.srgb;
    out.pixels.assign(static_cast<usize>(w) * h * 4, 255);
    if (!src.valid() || w == 0 || h == 0) return;
    const bool lin = usage == TextureUsage::Colour;
    const f32* tab = srgbTable();
    const auto dec = [&](u8 v) { return lin ? tab[v] : static_cast<f32>(v) * (1.0f / 255.0f); };
    const auto enc = [&](f32 v) {
        if (lin) return encodeSrgb(v);
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return static_cast<u8>(v * 255.0f + 0.5f);
    };
    const auto px = [&](i64 x, i64 y) {
        const i64 sx = ((x % src.width) + src.width) % src.width, sy = ((y % src.height) + src.height) % src.height;
        return &src.pixels[(static_cast<usize>(sy) * src.width + static_cast<usize>(sx)) * 4];
    };
    const f32 kx = static_cast<f32>(src.width) / static_cast<f32>(w);
    const f32 ky = static_cast<f32>(src.height) / static_cast<f32>(h);
    for (u32 y = 0; y < h; ++y) {
        for (u32 x = 0; x < w; ++x) {
            f32 acc[4] = {0, 0, 0, 0};
            if (kx >= 1.0f && ky >= 1.0f) {   // shrinking: area average with fractional edge weights
                const f32 x0 = static_cast<f32>(x) * kx, x1 = x0 + kx, y0 = static_cast<f32>(y) * ky, y1 = y0 + ky;
                f32 wsum = 0.0f;
                for (i64 sy = static_cast<i64>(std::floor(y0)); sy < static_cast<i64>(std::ceil(y1)); ++sy) {
                    const f32 wy = std::min(static_cast<f32>(sy + 1), y1) - std::max(static_cast<f32>(sy), y0);
                    for (i64 sx = static_cast<i64>(std::floor(x0)); sx < static_cast<i64>(std::ceil(x1)); ++sx) {
                        const f32 wx = std::min(static_cast<f32>(sx + 1), x1) - std::max(static_cast<f32>(sx), x0);
                        const f32 wt = wx * wy;
                        const u8* p = px(sx, sy);
                        for (int c = 0; c < 3; ++c) acc[c] += dec(p[c]) * wt;
                        acc[3] += static_cast<f32>(p[3]) * (1.0f / 255.0f) * wt;
                        wsum += wt;
                    }
                }
                if (wsum > 0.0f) for (f32& a : acc) a /= wsum;
            } else {                          // growing: bilinear, wrapping
                const f32 fx = (static_cast<f32>(x) + 0.5f) * kx - 0.5f, fy = (static_cast<f32>(y) + 0.5f) * ky - 0.5f;
                const i64 ix = static_cast<i64>(std::floor(fx)), iy = static_cast<i64>(std::floor(fy));
                const f32 tx = fx - static_cast<f32>(ix), ty = fy - static_cast<f32>(iy);
                const f32 wts[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
                const u8* p[4] = {px(ix, iy), px(ix + 1, iy), px(ix, iy + 1), px(ix + 1, iy + 1)};
                for (int t = 0; t < 4; ++t) {
                    for (int c = 0; c < 3; ++c) acc[c] += dec(p[t][c]) * wts[t];
                    acc[3] += static_cast<f32>(p[t][3]) * (1.0f / 255.0f) * wts[t];
                }
            }
            u8* o = &out.pixels[(static_cast<usize>(y) * w + x) * 4];
            for (int c = 0; c < 3; ++c) o[c] = enc(acc[c]);
            o[3] = static_cast<u8>(std::clamp(acc[3], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

} // namespace detail

// Decodes `path` and uploads it. Returns 0 on failure, with the reason in `err`.
rhi::TextureHandle uploadTexture(rhi::IResourceFactory& res, const std::string& path,
                                 TextureUsage usage, std::string* err, TextureUploadInfo* info) {
    if (path.empty()) {
        if (err) *err = "empty texture path";
        return 0;
    }

    // Both gates read once: a project must have pointed the cache somewhere (setTextureCacheDir)
    // AND the A/B switch must be on, or nothing below this file even looks for an entry -- the
    // legacy, always-uncompressed behaviour for a host that never calls setTextureCacheDir at all.
    const bool cacheReady = textureCompressionEnabled() && !textureCacheDir().empty();

    // ---- CACHE HIT: no image decode at all -- see TextureCache.hpp for the whole point of this. ----
    if (cacheReady) {
        CachedTexture cached;
        if (loadCachedTexture(path, usage, cached)) {
            if (const rhi::TextureHandle h = uploadCachedTexture(res, path, cached, info)) return h;
            // A cached entry the RHI refuses (a stale format from an older build, say) is not
            // repaired here -- fall through and decode as if it had been a miss.
            logCompressionFailureOnce(path, "the cached BC7 chain was rejected by the GPU");
        }
    }

    // Decoded with mips OFF, deliberately: this function decides mip generation itself, below, so it can
    // pass alphaLooksLikeCutout's verdict into generateMipChain -- loadTexture's own generateMips path
    // has no way to ask for that.
    const bool normalMap = usage == TextureUsage::NormalMap;

    fmt::TextureData tex;
    if (!detail::decodeTexture(path, usage, tex, err)) return 0;
    if (!tex.valid()) {
        if (err) *err = "decoded texture is empty: " + path;
        return 0;
    }

    // ELIGIBILITY IS DECIDED BEFORE A SINGLE MIP IS BUILT, deliberately: with compression off or
    // this texture ineligible (a small UI icon), everything past here -- including whether
    // coverage-preserving alpha runs at all -- must stay BYTE-IDENTICAL to the plain RGBA8 path
    // this function always had, or AVER_TEXTURE_COMPRESSION=0 would not be the clean A/B switch it
    // claims to be: it would also be silently comparing two different mip chains.
    const bool willCompress = cacheReady && textureCompressionEligible(tex.width, tex.height);

    // COVERAGE-PRESERVING MIPS, COLOUR CUTOUTS ONLY, AND ONLY WHEN THIS TEXTURE WILL ACTUALLY BE
    // COMPRESSED: a normal map's alpha is not a coverage channel (generateMipChain never treats it
    // as one) and Data usage (ORM, height, masks) is sampled directly rather than alpha-tested, so
    // neither has a coverage fraction worth preserving either way.
    const bool cutout = willCompress && usage == TextureUsage::Colour &&
                        fmt::alphaLooksLikeCutout(tex.levels.front());
    fmt::generateMipChain(tex, normalMap, cutout ? kCoverageReferenceAlpha : -1.0f);

    if (willCompress) {
        if (const rhi::TextureHandle h = uploadCompressed(res, path, usage, tex, cutout, info))
            return h;
        logCompressionFailureOnce(path, "the compressed GPU texture could not be created");
    }

    // ---- the RGBA8 path: unchanged from before this feature existed. ----
    // One CPU pointer per mip, in level order, which is the subresource order TextureDesc documents.
    std::vector<const void*> levels;
    levels.reserve(tex.levels.size());
    usize bytes = 0;
    for (const ImageData& lvl : tex.levels) {
        levels.push_back(lvl.pixels.data());
        bytes += lvl.pixels.size();
    }

    rhi::TextureDesc d;
    d.width  = tex.width;
    d.height = tex.height;
    d.mips   = static_cast<u32>(levels.size());
    d.format = tex.srgb ? rhi::Format::RGBA8UnormSrgb : rhi::Format::RGBA8Unorm;
    d.bind   = rhi::ResourceBind::ShaderResource;
    d.initialState = rhi::ResourceState::ShaderResource;
    d.initialData      = levels.data();
    d.initialDataCount = static_cast<u32>(levels.size());
    d.initialRowPitch  = tex.levels.front().rowPitch();
    d.debugName = path.c_str();

    const rhi::TextureHandle h = res.createTexture(d);
    if (!h) {
        if (err) *err = "the GPU texture could not be created: " + path;
        return 0;
    }
    if (info) {
        *info = TextureUploadInfo{tex.width, tex.height, d.mips, bytes};
        averageLinearFromLastMip(tex, info->averageLinear);
    }
    return h;
}

} // namespace aver::assets
