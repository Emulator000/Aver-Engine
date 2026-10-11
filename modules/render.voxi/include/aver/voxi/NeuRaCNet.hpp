// NeuRaCNet -- stage 2a of the radiance cache: a tiny MLP (10-8-8-1, Aver.Render.Neural, unmodified) learns a per-cell
// trust in [0,1] from traced samples and bakes it into the cell's 6 trust bits, where the lookup multiplies it into each
// corner weight (docs/plans/neurac_nrd2/DESIGN.md section 5, docs/rendering/NEURAC.md).
// Standalone compute passes with their own layout (voxi_neurac_net.hlsl); table 0 and the trace twins are untouched.
//
// Frame order, inside the cache's resolve block:
//   scatter -> uavBarrier(accum) -> recordPre -> rc.recordResolve -> uavBarrier(accum, cells) -> recordApply
//   -> uavBarrier(cells)
// Not thread-safe. Off while Path Tracing is wanted (the caller keeps netStrength 0 then).
#pragma once

#include "aver/core/Types.hpp"
#include "aver/render/neural/Mlp.hpp"
#include "aver/rhi/RHI.hpp"
#include "aver/rhi/RHIResources.hpp"

#include <vector>

namespace aver::voxi {

class NeuRaCNet {
public:
    struct Frame {
        rhi::BufferHandle info = 0;    // NeuRaC::infoBuffer(), this frame's RcInfo
        rhi::BufferHandle accum = 0;   // NeuRaC::accumBuffer()
        rhi::BufferHandle cells = 0;   // NeuRaC::cellsBuffer()
    };

    NeuRaCNet() = default;
    ~NeuRaCNet();
    NeuRaCNet(const NeuRaCNet&)            = delete;
    NeuRaCNet& operator=(const NeuRaCNet&) = delete;

    // Builds the Mlp (identity init, trust starts at 1), the four pipelines and ~8 MB of lists. False (said once) when
    // any of it fails; the caller then keeps netStrength 0, which is stage 1 exactly.
    bool create(rhi::IDevice& dev);
    void destroy();
    [[nodiscard]] bool valid() const { return mlp_.valid() && pipelines_[0] != 0; }

    // Forget the cached binding sets (and the Mlp's): call before the cache's or the lists' buffers are destroyed or
    // replaced. The next record* rebuilds them.
    void invalidateBindings();

    // After the scatter and its accum UAV barrier, before NeuRaC::recordResolve: reset the counters, build features from
    // the old state and the fresh accumulator, clamp, train one step, infer. Leaves its buffers resting in Common.
    void recordPre(rhi::IRenderContext& ctx, const Frame& f);

    // After recordResolve and the accum + cells UAV barriers: write the predicted trust into every touched cell the
    // resolve kept valid and gather stats (a readback and log line every 120 frames). The caller adds a cells UAV
    // barrier after.
    void recordApply(rhi::IRenderContext& ctx, const Frame& f);

    // The strength to give NeuRaC::Params::netStrength: user * saturate(framesActive / 900).
    [[nodiscard]] f32 warmStrength(f32 user) const;
    // Cache reset: the net's lists start over, so the strength warms in again. The weights are kept.
    void resetWarmup() { framesActive_ = 0; }

private:
    static constexpr u32 kSrvCount = 1, kUavCount = 12;
    static constexpr u32 kMaxSets = 8;

    rhi::BindingSetHandle setFor(const Frame& f);
    void logStats();

    rhi::IDevice*          dev_ = nullptr;
    rhi::IResourceFactory* res_ = nullptr;
    render::neural::Mlp    mlp_;

    // CSRcNetReset, Features, Finish, Apply.
    rhi::PipelineHandle pipelines_[4] = {};

    // Lists, counters and per-record side arrays; all rest in Common (see recordPre / recordApply).
    rhi::BufferHandle infRec_ = 0, trainRec_ = 0, trainTgt_ = 0, infCnt_ = 0, trainCnt_ = 0, recCell_ = 0, recLabel_ = 0,
                      infOut_ = 0, state_ = 0, stats_ = 0, statsReadback_ = 0;

    // One binding set per (info, accum, cells): the info buffer rotates through NeuRaC's ring. A set is never
    // rewritten once bound (Vulkan constraint); a full cache recycles round-robin.
    struct SetEntry { rhi::BufferHandle info = 0, accum = 0, cells = 0; rhi::BindingSetHandle set = 0; };
    std::vector<SetEntry> sets_;
    u32 recycleNext_ = 0;

    u32 framesActive_ = 0;
    u32 trainSteps_   = 0;      // optimiser steps dispatched (a step with no records only clears the accumulator)
    u32 frame_        = 0;
    u32 pendingSince_ = 0;      // frame_ the stats copy was recorded at; 0 = none in flight
    static constexpr u32 kReadDelayFrames = 6;
};

}  // namespace aver::voxi
