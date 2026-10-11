#include "aver/voxi/NeuRaCNet.hpp"

#include "aver/core/Log.hpp"
#include "aver/rhi/ShaderFiles.hpp"
#include "aver/voxi/NeuRaCLayout.hpp"
#include "aver/voxi/NeuRaCNetReference.hpp"

#include <algorithm>
#include <initializer_list>
#include <string>

namespace aver::voxi {

namespace rc  = neurac;
namespace net = neurac::net;

namespace {

constexpr const char* kShaderName = "voxi_neurac_net.hlsl";
constexpr const char* kEntry[4]   = {"CSRcNetReset", "CSRcNetFeatures", "CSRcNetFinish", "CSRcNetApply"};
enum Kernel : u32 { kReset = 0, kFeatures = 1, kFinish = 2, kApply = 3 };

constexpr rhi::ResourceState kCommon = rhi::ResourceState::Common;
constexpr rhi::ResourceState kWrite  = rhi::ResourceState::UnorderedAccess;

constexpr u32 kStatsBytes = net::kStatCount * sizeof(u32);

void moveBuffers(rhi::IRenderContext& ctx, std::initializer_list<rhi::BufferHandle> list, rhi::ResourceState from,
                 rhi::ResourceState to) {
    for (rhi::BufferHandle b : list) if (b) ctx.bufferBarrier(b, from, to);
}

}  // namespace

NeuRaCNet::~NeuRaCNet() { destroy(); }

bool NeuRaCNet::create(rhi::IDevice& dev) {
    destroy();
    dev_ = &dev;
    res_ = dev.resources();
    if (!res_) { dev_ = nullptr; return false; }

    auto fail = [&](const char* why) {
        AVER_WARN("[Voxi] NeuRaC net: {}; the cache keeps its stage-1 weights", why);
        destroy();
        return false;
    };

    const std::string& source = rhi::shaderFile(kShaderName);
    if (source.empty()) return fail("voxi_neurac_net.hlsl is not deployed beside the executable");
    for (u32 k = 0; k < 4; ++k) {
        // SM 6.2 selects DXC, which voxi_neurac.hlsli's f16 unpacking is written for (as the resolve shader).
        rhi::ShaderDesc sd{};
        sd.source = source.c_str();
        sd.entry  = kEntry[k];
        sd.stage  = rhi::ShaderStage::Compute;
        sd.minShaderModel = 62;
        const rhi::ShaderHandle cs = res_->createShader(sd);
        if (!cs) return fail("a net shader would not compile");
        // t0 RcInfo; u0 accum, u1 cells, u2 infRec, u3 trainRec, u4 trainTgt, u5 infCnt, u6 trainCnt, u7 recCell,
        // u8 recLabel, u9 infOut, u10 state, u11 stats. No constants: every bound is a shader #define.
        rhi::ComputePipelineDesc pd{};
        pd.cs = cs;
        pd.layout.srvCount = kSrvCount;
        pd.layout.uavCount = kUavCount;
        pd.layout.slotKindsDeclared = true;
        pd.layout.srvKinds[0] = rhi::SlotKind::StructuredBuffer;
        for (u32 i = 0; i < kUavCount; ++i) pd.layout.uavKinds[i] = rhi::SlotKind::StructuredBuffer;
        pipelines_[k] = res_->createComputePipeline(pd);
        res_->destroyShader(cs);   // the pipeline owns the bytecode now
        if (!pipelines_[k]) return fail("a net pipeline would not build");
    }

    auto make = [&](u64 bytes, rhi::BufferKind kind, bool uav, const char* name) {
        rhi::BufferDesc bd{};
        bd.bytes = bytes;
        bd.kind = kind;
        bd.allowUnorderedAccess = uav;
        bd.debugName = name;
        return res_->createBuffer(bd);
    };
    constexpr u32 f = sizeof(f32), u = sizeof(u32);
    infRec_   = make(static_cast<u64>(net::kMaxInfer) * net::kInputs * f, rhi::BufferKind::Default, true, "Voxi NeuRaC net inference records");
    trainRec_ = make(static_cast<u64>(net::kMaxTrain) * net::kInputs * f, rhi::BufferKind::Default, true, "Voxi NeuRaC net training records");
    trainTgt_ = make(static_cast<u64>(net::kMaxTrain) * f, rhi::BufferKind::Default, true, "Voxi NeuRaC net training targets");
    infCnt_   = make(2 * u, rhi::BufferKind::Default, true, "Voxi NeuRaC net inference count");
    trainCnt_ = make(2 * u, rhi::BufferKind::Default, true, "Voxi NeuRaC net training count");
    recCell_  = make(static_cast<u64>(net::kMaxInfer) * 2 * u, rhi::BufferKind::Default, true, "Voxi NeuRaC net record cells");
    recLabel_ = make(static_cast<u64>(net::kMaxInfer) * f, rhi::BufferKind::Default, true, "Voxi NeuRaC net record labels");
    infOut_   = make(static_cast<u64>(net::kMaxInfer) * f, rhi::BufferKind::Default, true, "Voxi NeuRaC net predictions");
    state_    = make(4 * u, rhi::BufferKind::Default, true, "Voxi NeuRaC net state");
    stats_    = make(kStatsBytes, rhi::BufferKind::Default, true, "Voxi NeuRaC net stats");
    if (!infRec_ || !trainRec_ || !trainTgt_ || !infCnt_ || !trainCnt_ || !recCell_ || !recLabel_ || !infOut_ ||
        !state_ || !stats_)
        return fail("the record lists could not be allocated");
    // The log line is optional: without the readback the net still runs.
    statsReadback_ = make(kStatsBytes, rhi::BufferKind::Readback, false, "Voxi NeuRaC net stats readback");

    const render::neural::MlpDesc desc = net::netDesc();
    if (!mlp_.create(dev, desc, net::netOptimiser())) return fail("the network would not build");
    if (mlp_.safeBatchLimit() < net::kMaxTrain) return fail("the training batch is above the network's safe limit");
    if (!mlp_.uploadWeights(net::identityWeights(desc))) return fail("the identity weights would not upload");

    AVER_INFO("[Voxi] NeuRaC net: {} weights, up to {} inference and {} training records a frame", mlp_.weightCount(),
              net::kMaxInfer, net::kMaxTrain);
    return true;
}

void NeuRaCNet::destroy() {
    invalidateBindings();
    mlp_.destroy();
    if (res_) {
        for (rhi::PipelineHandle& p : pipelines_) if (p) res_->destroyPipeline(p);
        for (rhi::BufferHandle* b : {&infRec_, &trainRec_, &trainTgt_, &infCnt_, &trainCnt_, &recCell_, &recLabel_,
                                     &infOut_, &state_, &stats_, &statsReadback_})
            if (*b) res_->destroyBuffer(*b);
    }
    for (rhi::PipelineHandle& p : pipelines_) p = 0;
    infRec_ = trainRec_ = trainTgt_ = infCnt_ = trainCnt_ = recCell_ = recLabel_ = infOut_ = state_ = stats_ =
        statsReadback_ = 0;
    framesActive_ = trainSteps_ = frame_ = pendingSince_ = 0;
    dev_ = nullptr;
    res_ = nullptr;
}

void NeuRaCNet::invalidateBindings() {
    if (res_) for (SetEntry& e : sets_) if (e.set) res_->destroyBindingSet(e.set);
    sets_.clear();
    recycleNext_ = 0;
    mlp_.invalidateBindings();
}

rhi::BindingSetHandle NeuRaCNet::setFor(const Frame& f) {
    for (const SetEntry& e : sets_)
        if (e.info == f.info && e.accum == f.accum && e.cells == f.cells) return e.set;

    SetEntry* slot = nullptr;
    if (sets_.size() < kMaxSets) {
        rhi::BindingSetDesc bd{};
        bd.srvCount = kSrvCount;
        bd.uavCount = kUavCount;
        bd.srvKinds[0] = rhi::SlotKind::StructuredBuffer;
        for (u32 i = 0; i < kUavCount; ++i) bd.uavKinds[i] = rhi::SlotKind::StructuredBuffer;
        const rhi::BindingSetHandle set = res_->createBindingSet(bd);
        if (!set) { AVER_WARN("[Voxi] NeuRaC net: createBindingSet failed"); return 0; }
        sets_.push_back({});
        sets_.back().set = set;
        slot = &sets_.back();
    } else {
        // More distinct buffer triples than the info ring explains: the caller is rebuilding the cache's buffers
        // without invalidateBindings.
        slot = &sets_[recycleNext_];
        recycleNext_ = (recycleNext_ + 1) % kMaxSets;
    }
    slot->info = f.info; slot->accum = f.accum; slot->cells = f.cells;

    constexpr u32 fl = sizeof(f32), un = sizeof(u32);
    const rhi::BindingSetHandle set = slot->set;
    res_->setSrvBuffer(set, 0, f.info, rc::kInfoStride, 1, 0);
    res_->setUavBuffer(set, 0, f.accum, un, rc::kAccumInts, 0);
    res_->setUavBuffer(set, 1, f.cells, rc::kCellStride, rc::kCells, 0);
    res_->setUavBuffer(set, 2, infRec_, fl, net::kMaxInfer * net::kInputs, 0);
    res_->setUavBuffer(set, 3, trainRec_, fl, net::kMaxTrain * net::kInputs, 0);
    res_->setUavBuffer(set, 4, trainTgt_, fl, net::kMaxTrain, 0);
    res_->setUavBuffer(set, 5, infCnt_, un, 2, 0);
    res_->setUavBuffer(set, 6, trainCnt_, un, 2, 0);
    res_->setUavBuffer(set, 7, recCell_, 2 * un, net::kMaxInfer, 0);
    res_->setUavBuffer(set, 8, recLabel_, fl, net::kMaxInfer, 0);
    res_->setUavBuffer(set, 9, infOut_, fl, net::kMaxInfer, 0);
    res_->setUavBuffer(set, 10, state_, un, 4, 0);
    res_->setUavBuffer(set, 11, stats_, un, net::kStatCount, 0);
    return set;
}

void NeuRaCNet::recordPre(rhi::IRenderContext& ctx, const Frame& f) {
    if (!valid() || !f.info || !f.accum || !f.cells) return;
    const rhi::BindingSetHandle set = setFor(f);
    if (!set) return;
    framesActive_ = std::min(framesActive_ + 1u, 1u << 30);

    rhi::ScopedGpuStat stat(ctx, "Voxi NeuRaC net");
    const auto lists = {infRec_, trainRec_, trainTgt_, infCnt_, trainCnt_, recCell_, recLabel_, state_, stats_};
    moveBuffers(ctx, lists, kCommon, kWrite);
    auto run = [&](Kernel k, u32 groups) {
        ctx.setPipeline(pipelines_[k]);
        ctx.setBindingSet(set);
        ctx.dispatch(groups, 1, 1);
    };

    run(kReset, 1);
    ctx.uavBarrierBuffer(infCnt_);
    ctx.uavBarrierBuffer(trainCnt_);
    ctx.uavBarrierBuffer(stats_);

    run(kFeatures, rc::kResolveGroups);   // one thread per cell, the resolve's grouping
    ctx.uavBarrierBuffer(infCnt_);
    ctx.uavBarrierBuffer(trainCnt_);
    ctx.uavBarrierBuffer(state_);

    run(kFinish, 1);
    // The resolve writes the accumulator and cells this pass only read.
    ctx.uavBarrierBuffer(f.accum);
    ctx.uavBarrierBuffer(f.cells);
    moveBuffers(ctx, lists, kWrite, kCommon);

    mlp_.recordTrain(ctx, trainRec_, trainTgt_, trainCnt_, net::kMaxTrain);
    ++trainSteps_;
    mlp_.recordInfer(ctx, infRec_, infOut_, infCnt_, net::kMaxInfer);
}

void NeuRaCNet::recordApply(rhi::IRenderContext& ctx, const Frame& f) {
    if (!valid() || !f.info || !f.accum || !f.cells) return;
    const rhi::BindingSetHandle set = setFor(f);
    if (!set) return;
    ++frame_;
    if (pendingSince_ && frame_ >= pendingSince_ + kReadDelayFrames) logStats();

    rhi::ScopedGpuStat stat(ctx, "Voxi NeuRaC net");
    const auto lists = {infCnt_, recCell_, recLabel_, infOut_, stats_};
    moveBuffers(ctx, lists, kCommon, kWrite);
    ctx.setPipeline(pipelines_[kApply]);
    ctx.setBindingSet(set);
    ctx.dispatch(net::kMaxInfer / 64u, 1, 1);   // threads past the live count return
    moveBuffers(ctx, lists, kWrite, kCommon);

    if (statsReadback_ && !pendingSince_ && frame_ % net::kLogEveryFrames == 0) {
        ctx.bufferBarrier(stats_, kCommon, rhi::ResourceState::CopySource);
        ctx.copyBuffer(statsReadback_, stats_, kStatsBytes);
        ctx.bufferBarrier(stats_, rhi::ResourceState::CopySource, kCommon);
        pendingSince_ = frame_;
    }
}

f32 NeuRaCNet::warmStrength(f32 user) const {
    const f32 ramp = std::min(1.0f, static_cast<f32>(framesActive_) / static_cast<f32>(net::kWarmupFrames));
    return user * ramp;
}

void NeuRaCNet::logStats() {
    pendingSince_ = 0;
    u32 st[net::kStatCount] = {};
    if (!res_->readBuffer(statsReadback_, st, kStatsBytes, 0)) return;
    const f64 n = static_cast<f64>(st[0]), scale = static_cast<f64>(net::kStatScale);
    const f64 meanLabel = n > 0.0 ? st[1] / (scale * n) : 0.0, meanPred = n > 0.0 ? st[2] / (scale * n) : 0.0,
              loss = n > 0.0 ? st[3] / (scale * n) : 0.0,
              meanTrust = st[4] ? st[5] / (static_cast<f64>(rc::kTrustMax) * st[4]) : 0.0;
    AVER_INFO("[Voxi] NeuRaC net: step {}, eligible {}, mean label {:.3f}, mean pred {:.3f}, loss {:.4f}, mean trust {:.3f}",
              trainSteps_, st[0], meanLabel, meanPred, loss, meanTrust);
}

}  // namespace aver::voxi
