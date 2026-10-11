// NeuRaCGuide -- the radiance cache read at every primary surface pixel, handed to NRD2 as the anchor's
// guide (docs/rendering/NRD2.md, docs/rendering/NEURAC.md). A standalone compute pass with its own small
// layout (t0 RcInfo, t1 view Z, t2 normal-roughness, u0 cells, u1 guide, u2 coverage counters, b3), so
// table 0 and the trace twins are untouched.
//
// Frame order: after Stage B's draw, before Nrd2::record. The G-buffer is read after the draw, so
// record() barriers it from `gbufferState` and back exactly as Nrd2::record does.
// Output: RGBA16F at the staged size, resting in NonPixelShaderResource; rgb is the cached
// irradiance / PI in D units, a the confidence (1 - the fraction the cache could not fill).
#pragma once

#include "aver/core/Types.hpp"
#include "aver/rhi/RHI.hpp"
#include "aver/rhi/RHIResources.hpp"

namespace aver::voxi {

class NeuRaCGuide {
public:
    struct Frame {
        rhi::BufferHandle info = 0, cells = 0;   // NeuRaC::infoBuffer() and cellsBuffer(), this frame
        const f32* eyeCm = nullptr;              // the eye NeuRaC::beginFrame was given (world cm)
        rhi::TextureHandle viewZ = 0, normalRoughness = 0;
        rhi::ResourceState gbufferState = rhi::ResourceState::RenderTarget;
        u32 viewport[4] = {};                    // scene viewport x y w h, render-target pixels
        u32 width = 0, height = 0;               // the staged target size (the guide texture's size)
    };

    NeuRaCGuide() = default;
    ~NeuRaCGuide();
    NeuRaCGuide(const NeuRaCGuide&)            = delete;
    NeuRaCGuide& operator=(const NeuRaCGuide&) = delete;

    // Compiles voxi_neurac_guide.hlsl and builds the pipeline. False (said once) when it cannot.
    bool create(rhi::IDevice& dev);
    void destroy();
    [[nodiscard]] bool valid() const { return pipeline_ != 0; }

    // Writes the guide for this frame. False: no guide this frame (invalid, bad input, or a failed resize);
    // the texture is then not to be read.
    bool record(rhi::IRenderContext& ctx, const Frame& f);

    // RGBA16F, NonPixelShaderResource at rest. Changes handle when the staged size changes.
    [[nodiscard]] rhi::TextureHandle guide() const { return guide_; }

    // Forget the cached descriptor handles: the next record() rewrites its binding set. Call when the
    // buffers or G-buffer textures it was given are about to be destroyed or replaced.
    void invalidateBindings();

private:
    bool resize(u32 width, u32 height);
    void releaseGuide();
    void logCoverage();

    rhi::IDevice*          dev_ = nullptr;
    rhi::IResourceFactory* res_ = nullptr;
    rhi::PipelineHandle    pipeline_ = 0;
    rhi::BindingSetHandle  set_ = 0;
    rhi::TextureHandle     guide_ = 0;
    u32 width_ = 0, height_ = 0;
    u32 failedWidth_ = 0, failedHeight_ = 0;

    // What set_ was last written with (the info buffer rotates through the NeuRaC ring every frame).
    rhi::BufferHandle  boundInfo_ = 0, boundCells_ = 0;
    rhi::TextureHandle boundViewZ_ = 0, boundNormal_ = 0, boundGuide_ = 0;

    // Coverage: two running totals the shader adds to (never cleared; the log reads differences, so the
    // first reading is only a baseline). A readback is copied every kLogEveryFrames and read
    // kReadDelayFrames later, once the GPU is done with it.
    static constexpr u32 kLogEveryFrames = 120;
    static constexpr u32 kReadDelayFrames = 6;
    rhi::BufferHandle stats_ = 0, statsReadback_ = 0;
    u32 frame_ = 0;
    u32 pendingSince_ = 0;      // frame_ at which the copy was recorded; 0 = none in flight
    bool haveBaseline_ = false;
    u32 lastSurf_ = 0, lastCov_ = 0;
};

}  // namespace aver::voxi
