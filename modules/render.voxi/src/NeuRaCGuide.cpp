#include "aver/voxi/NeuRaCGuide.hpp"

#include "aver/core/Log.hpp"
#include "aver/rhi/ShaderFiles.hpp"
#include "aver/voxi/NeuRaCLayout.hpp"

#include <cmath>
#include <cstring>
#include <string>

namespace aver::voxi {

namespace rc = neurac;

namespace {

constexpr const char* kShaderName = "voxi_neurac_guide.hlsl";
constexpr const char* kEntry      = "CSNeuracGuide";
constexpr u32 kConstantSlot = 3;   // b3, as Nrd2 (modules/render.neural/README.md: why not b1)

constexpr u32 kSrvCount = 3, kUavCount = 3;
constexpr u32 kStatsBytes = 2 * sizeof(u32);

constexpr rhi::ResourceState kRead   = rhi::ResourceState::NonPixelShaderResource;
constexpr rhi::ResourceState kWrite  = rhi::ResourceState::UnorderedAccess;
constexpr rhi::ResourceState kCommon = rhi::ResourceState::Common;

// voxi_neurac_guide.hlsl's GuideCB, byte for byte.
struct Constants {
    u32 rect[4];
    f32 view[12];      // right, up, forward (xyz); w of right and up: tan of the half FOV
    f32 eyeCell[12];   // per cascade xyz (+ pad): eye / size - originCell
};
static_assert(sizeof(Constants) == 16 + 48 + 48, "GuideCB: a uint4 and six float4s");

// The view basis from the camera's camera-relative inverse view-projection (row vectors): the eye is the
// origin, so the unprojected viewport centre is forward and the edges give right and up. The w of right
// and up carry tan of the half FOV (a symmetric perspective frustum). Same maths as Nrd2.cpp's viewBasis,
// duplicated so the denoiser needs no new public API. False: no usable camera.
bool viewBasis(rhi::IDevice* dev, f32 out[12]) {
    std::memset(out, 0, 12 * sizeof(f32));
    f32 inv[16] = {};
    if (!dev || !dev->camera(nullptr, inv, nullptr)) return false;
    auto unproject = [&](f32 x, f32 y, f32 r[3]) {
        const f32 v[4] = {x, y, 0.5f, 1.0f};
        f32 h[4] = {};
        for (u32 c = 0; c < 4; ++c) for (u32 k = 0; k < 4; ++k) h[c] += v[k] * inv[k * 4 + c];
        const f32 w = std::fabs(h[3]) > 1e-20f ? h[3] : 1e-20f;
        for (u32 c = 0; c < 3; ++c) r[c] = h[c] / w;
    };
    f32 c[3], xp[3], xm[3], yp[3], ym[3];
    unproject(0, 0, c); unproject(1, 0, xp); unproject(-1, 0, xm); unproject(0, 1, yp); unproject(0, -1, ym);
    const f32 axes[3][3] = {{xp[0] - xm[0], xp[1] - xm[1], xp[2] - xm[2]},
                            {yp[0] - ym[0], yp[1] - ym[1], yp[2] - ym[2]},
                            {c[0], c[1], c[2]}};
    f32 basis[12] = {}, len[3] = {};
    for (u32 a = 0; a < 3; ++a) {
        const f32 l = std::sqrt(axes[a][0] * axes[a][0] + axes[a][1] * axes[a][1] + axes[a][2] * axes[a][2]);
        if (!(l > 1e-20f) || !std::isfinite(l)) return false;
        for (u32 k = 0; k < 3; ++k) basis[a * 4 + k] = axes[a][k] / l;
        len[a] = l;
    }
    // Both edge points sit at the centre's view depth, so half their separation over it is the tangent.
    basis[3] = 0.5f * len[0] / len[2];
    basis[7] = 0.5f * len[1] / len[2];
    std::memcpy(out, basis, sizeof(basis));
    return true;
}

}  // namespace

NeuRaCGuide::~NeuRaCGuide() { destroy(); }

bool NeuRaCGuide::create(rhi::IDevice& dev) {
    destroy();
    dev_ = &dev;
    res_ = dev.resources();
    if (!res_) { dev_ = nullptr; return false; }

    auto fail = [&](const char* why) {
        AVER_WARN("[Voxi] NeuRaC guide: {}; NRD2 keeps its tile anchor", why);
        destroy();
        return false;
    };

    const std::string& source = rhi::shaderFile(kShaderName);
    if (source.empty()) return fail("voxi_neurac_guide.hlsl is not deployed beside the executable");
    // SM 6.2 selects DXC, which voxi_neurac.hlsli's f16 unpacking is written for (as the resolve shader).
    rhi::ShaderDesc sd{};
    sd.source = source.c_str();
    sd.entry  = kEntry;
    sd.stage  = rhi::ShaderStage::Compute;
    sd.minShaderModel = 62;
    const rhi::ShaderHandle cs = res_->createShader(sd);
    if (!cs) return fail("the guide shader would not compile");

    // t0 RcInfo, t1 view Z, t2 normal-roughness; u0 cells, u1 guide, u2 coverage counters; b3 root CBV.
    rhi::ComputePipelineDesc pd{};
    pd.cs = cs;
    pd.layout.srvCount = kSrvCount;
    pd.layout.uavCount = kUavCount;
    pd.layout.slotKindsDeclared = true;
    pd.layout.srvKinds[0] = rhi::SlotKind::StructuredBuffer;
    pd.layout.uavKinds[0] = rhi::SlotKind::StructuredBuffer;
    pd.layout.uavKinds[2] = rhi::SlotKind::StructuredBuffer;
    pd.layout.constantDwords[kConstantSlot] = 0;   // root CBV
    pipeline_ = res_->createComputePipeline(pd);
    res_->destroyShader(cs);   // the pipeline owns the bytecode now
    if (!pipeline_) return fail("the guide pipeline would not build");

    rhi::BindingSetDesc bsd{};
    bsd.srvCount = kSrvCount; bsd.uavCount = kUavCount;
    bsd.srvKinds[0] = rhi::SlotKind::StructuredBuffer;
    bsd.uavKinds[0] = rhi::SlotKind::StructuredBuffer;
    bsd.uavKinds[2] = rhi::SlotKind::StructuredBuffer;
    set_ = res_->createBindingSet(bsd);
    if (!set_) return fail("the guide binding set could not be created");

    rhi::BufferDesc bd{};
    bd.bytes = kStatsBytes;
    bd.kind  = rhi::BufferKind::Default;
    bd.allowUnorderedAccess = true;
    bd.debugName = "Voxi NeuRaC guide counters";
    stats_ = res_->createBuffer(bd);
    if (!stats_) return fail("the coverage counters could not be created");
    // The log is optional: without the readback the pass still runs.
    bd.kind = rhi::BufferKind::Readback;
    bd.allowUnorderedAccess = false;
    bd.debugName = "Voxi NeuRaC guide readback";
    statsReadback_ = res_->createBuffer(bd);
    return true;
}

void NeuRaCGuide::destroy() {
    if (res_) {
        if (guide_) res_->destroyTexture(guide_);
        if (set_) res_->destroyBindingSet(set_);
        if (pipeline_) res_->destroyPipeline(pipeline_);
        if (stats_) res_->destroyBuffer(stats_);
        if (statsReadback_) res_->destroyBuffer(statsReadback_);
    }
    guide_ = 0; set_ = 0; pipeline_ = 0; stats_ = 0; statsReadback_ = 0;
    width_ = height_ = failedWidth_ = failedHeight_ = 0;
    frame_ = pendingSince_ = 0;
    haveBaseline_ = false;
    lastSurf_ = lastCov_ = 0;
    invalidateBindings();
    dev_ = nullptr;
    res_ = nullptr;
}

void NeuRaCGuide::invalidateBindings() {
    boundInfo_ = boundCells_ = 0;
    boundViewZ_ = boundNormal_ = boundGuide_ = 0;
}

void NeuRaCGuide::releaseGuide() {
    if (res_ && guide_) res_->destroyTexture(guide_);
    guide_ = 0;
    width_ = height_ = 0;
    invalidateBindings();
}

bool NeuRaCGuide::resize(u32 width, u32 height) {
    if (guide_ && width == width_ && height == height_) return true;
    if (width == failedWidth_ && height == failedHeight_) return false;
    releaseGuide();
    rhi::TextureDesc d{};
    d.width = width; d.height = height;
    d.format = rhi::Format::RGBA16F;
    d.bind = static_cast<rhi::ResourceBind>(static_cast<u32>(rhi::ResourceBind::ShaderResource) |
                                            static_cast<u32>(rhi::ResourceBind::UnorderedAccess));
    d.initialState = kRead;
    d.debugName = "Voxi NeuRaC guide";
    guide_ = res_->createTexture(d);
    if (!guide_) {
        AVER_WARN("[Voxi] NeuRaC guide: the {}x{} texture could not be allocated", width, height);
        failedWidth_ = width; failedHeight_ = height;
        return false;
    }
    failedWidth_ = failedHeight_ = 0;
    width_ = width; height_ = height;
    AVER_INFO("[Voxi] NeuRaC guide: {}x{} RGBA16F, {:.1f} MiB", width, height,
              static_cast<f64>(width) * height * 8.0 / (1024.0 * 1024.0));
    return true;
}

bool NeuRaCGuide::record(rhi::IRenderContext& ctx, const Frame& f) {
    if (!valid() || !f.info || !f.cells || !f.eyeCm || !f.viewZ || !f.normalRoughness) return false;
    const u32 vx = f.viewport[0], vy = f.viewport[1], vw = f.viewport[2], vh = f.viewport[3];
    if (!vw || !vh || vx + vw > f.width || vy + vh > f.height) return false;
    if (!std::isfinite(f.eyeCm[0]) || !std::isfinite(f.eyeCm[1]) || !std::isfinite(f.eyeCm[2])) return false;
    if (!resize(f.width, f.height)) return false;

    Constants cb{};
    if (!viewBasis(dev_, cb.view)) return false;   // an identity basis would put every surface in the wrong cell
    cb.rect[0] = vx; cb.rect[1] = vy; cb.rect[2] = vw; cb.rect[3] = vh;
    // Eye in each cascade's window cells, in double: the same origin NeuRaC::beginFrame snapped, so
    // eye / size - origin is in [32, 33) and the shader adds only the small camera-relative offset to it.
    for (u32 c = 0; c < rc::kCascades; ++c) {
        const f64 size = static_cast<f64>(rc::kCellSizeCm[c]);
        for (u32 a = 0; a < 3; ++a) {
            const i32 origin = rc::snapOrigin(f.eyeCm[a], rc::kCellSizeCm[c]);
            cb.eyeCell[c * 4 + a] = static_cast<f32>(static_cast<f64>(f.eyeCm[a]) / size - static_cast<f64>(origin));
        }
    }

    ++frame_;
    if (pendingSince_ && frame_ >= pendingSince_ + kReadDelayFrames) logCoverage();

    if (f.info != boundInfo_ || f.cells != boundCells_ || f.viewZ != boundViewZ_ ||
        f.normalRoughness != boundNormal_ || guide_ != boundGuide_) {
        res_->setSrvBuffer(set_, 0, f.info, rc::kInfoStride, 1, 0);
        res_->setSrv(set_, 1, f.viewZ);
        res_->setSrv(set_, 2, f.normalRoughness);
        res_->setUavBuffer(set_, 0, f.cells, rc::kCellStride, rc::kCells, 0);
        res_->setUav(set_, 1, guide_, 0);
        res_->setUavBuffer(set_, 2, stats_, sizeof(u32), 2, 0);
        boundInfo_ = f.info; boundCells_ = f.cells; boundViewZ_ = f.viewZ;
        boundNormal_ = f.normalRoughness; boundGuide_ = guide_;
    }

    rhi::ScopedGpuStat stat(ctx, "Voxi NeuRaC guide");
    ctx.textureBarrier(f.viewZ, f.gbufferState, kRead);
    ctx.textureBarrier(f.normalRoughness, f.gbufferState, kRead);
    ctx.textureBarrier(guide_, kRead, kWrite);
    ctx.bufferBarrier(stats_, kCommon, kWrite);
    ctx.uavBarrierBuffer(f.cells);   // the resolve's writes, whatever recorded last
    ctx.setPipeline(pipeline_);
    ctx.setBindingSet(set_);
    ctx.setConstantBuffer(kConstantSlot, &cb, sizeof(cb));
    ctx.dispatch((vw + 7u) / 8u, (vh + 7u) / 8u, 1);
    ctx.textureBarrier(guide_, kWrite, kRead);
    ctx.bufferBarrier(stats_, kWrite, kCommon);

    if (statsReadback_ && !pendingSince_ && frame_ % kLogEveryFrames == 0) {
        ctx.bufferBarrier(stats_, kCommon, rhi::ResourceState::CopySource);
        ctx.copyBuffer(statsReadback_, stats_, kStatsBytes);
        ctx.bufferBarrier(stats_, rhi::ResourceState::CopySource, kCommon);
        pendingSince_ = frame_;
    }

    ctx.textureBarrier(f.viewZ, kRead, f.gbufferState);
    ctx.textureBarrier(f.normalRoughness, kRead, f.gbufferState);
    return true;
}

// The totals only ever grow (mod 2^32), so the window since the last reading is a difference.
void NeuRaCGuide::logCoverage() {
    pendingSince_ = 0;
    u32 totals[2] = {};
    if (!res_->readBuffer(statsReadback_, totals, kStatsBytes, 0)) return;
    if (haveBaseline_) {
        const u32 surf = totals[0] - lastSurf_, cov = totals[1] - lastCov_;
        if (surf) AVER_INFO("[Voxi] NeuRaC guide: {:.1f}% covered", 100.0 * static_cast<f64>(cov) / static_cast<f64>(surf));
    }
    lastSurf_ = totals[0]; lastCov_ = totals[1];
    haveBaseline_ = true;
}

}  // namespace aver::voxi
