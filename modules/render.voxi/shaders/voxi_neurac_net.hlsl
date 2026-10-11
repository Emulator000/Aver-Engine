// NeuRaC stage 2a: the cell-level passes around the trust network (NeuRaCNet.cpp, docs/plans/neurac_nrd2/DESIGN.md 5).
// Mlp trains and infers between CSRcNetFinish and the cache resolve; these four kernels feed it and bake its answer.
//   CSRcNetReset    1 thread:  zero the counters and this frame's stats
//   CSRcNetFeatures 1 per cell: cells that took samples and still hold a valid old state append an inference
//                              record; eligible ones (and a hash-thinned subset) also append a training record
//   CSRcNetFinish   1 thread:  keep last frame's raw totals for the thinning rates, clamp the live counts
//   CSRcNetApply    1 per inference record: write the predicted trust into the cell's 6 trust bits, if the cell
//                              survived the resolve; accumulate label / prediction / loss stats
// Own layout, shared by all four entry points; table 0 and the trace twins are not involved. Mirrored on the CPU by
// NeuRaCNetReference.hpp, which the tests hold to this file.
#include "voxi_neurac.hlsli"

#define RCN_INPUTS        10
#define RCN_MAX_INFER     131072u     // inference records a frame (Mlp maxCount)
#define RCN_MAX_TRAIN     16383u      // training records a frame: the Mlp's safeBatchLimit at gradClamp 2
#define RCN_INFER_TARGET  98304u      // expected inference records; cells beyond it are thinned by hash
#define RCN_TRAIN_TARGET  12288u      // expected training records
#define RCN_MIN_FRESH     8u          // fewest fresh samples a cell's label may come from
#define RCN_MIN_NEFF      2u
#define RCN_EPS           0.001
#define RCN_TRUST_DIV     0.85        // trust = saturate(prediction / 0.85)
#define RCN_STAT_SCALE    16384.0     // fixed point of the stats: a frame's 131072 records of <= 1 stay under 2^32
#define RCN_STATS         8u

StructuredBuffer<RcInfo>      gRcInfo    : register(t0);
RWStructuredBuffer<int>       gAccum     : register(u0);   // read
RWStructuredBuffer<RcCell>    gCells     : register(u1);   // read; Apply writes the trust bits
RWStructuredBuffer<float>     gInfRec    : register(u2);   // RCN_MAX_INFER x RCN_INPUTS
RWStructuredBuffer<float>     gTrainRec  : register(u3);   // RCN_MAX_TRAIN x RCN_INPUTS
RWStructuredBuffer<float>     gTrainTgt  : register(u4);   // RCN_MAX_TRAIN
RWStructuredBuffer<uint>      gInfCnt    : register(u5);   // [0] live count (Mlp reads it), [1] touched cells before thinning
RWStructuredBuffer<uint>      gTrainCnt  : register(u6);   // [0] live count, [1] eligible records before thinning
RWStructuredBuffer<uint2>     gRecCell   : register(u7);   // per inference record: cell index, expected tag
RWStructuredBuffer<float>     gRecLabel  : register(u8);   // per inference record: label, or -1 when not eligible
RWStructuredBuffer<float>     gInfOut    : register(u9);   // Mlp's answer per inference record
RWStructuredBuffer<uint>      gState     : register(u10);  // last frame's raw totals: touched, eligible, processed, appended
RWStructuredBuffer<uint>      gStats     : register(u11);  // eligible, sum label, sum pred, sum sq err, applied, sum trust6

// ---- shared with the CPU reference -------------------------------------------------------------------------
uint rcnHash16(uint a, uint b, uint salt)
{
    uint h = 0x9E3779B9u * (salt + 1u);
    h = (h ^ a) * 0x85EBCA6Bu;
    h = (h ^ b) * 0xC2B2AE35u;
    h ^= h >> 16;  h *= 0x85EBCA6Bu;
    h ^= h >> 13;  h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h >> 16;
}
float rcnRate(uint prev, uint target) { return prev <= target ? 1.0 : (float)target / (float)prev; }
bool  rcnKeep(float rate, uint cell, uint frame, uint salt)
{
    return rate >= 1.0 || rcnHash16(cell, frame, salt) < (uint)(rate * 65536.0);
}
float rcnLum(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
// A cell's state counts when it has samples, the right tag and has not aged out (the lookup's rule).
bool  rcnValid(uint meta, uint tag)
{
    return ((meta >> 24) & 15u) != 0u && (meta & 0xFFFFFFu) == tag && (meta >> 28) < (uint)AVER_RC_AGE_MAX;
}
uint  rcnTrust6(float pred)
{
    return (uint)(saturate(pred / RCN_TRUST_DIV) * (float)AVER_RC_TRUST_MAX + 0.5);
}

static const int3 kRcnDir[6] = { int3(1, 0, 0), int3(-1, 0, 0), int3(0, 1, 0), int3(0, -1, 0), int3(0, 0, 1), int3(0, 0, -1) };

[numthreads(1, 1, 1)]
void CSRcNetReset()
{
    gInfCnt[0] = 0u;  gInfCnt[1] = 0u;
    gTrainCnt[0] = 0u;  gTrainCnt[1] = 0u;
    [unroll] for (uint i = 0u; i < RCN_STATS; ++i) gStats[i] = 0u;
}

[numthreads(64, 1, 1)]
void CSRcNetFeatures(uint3 dtid : SV_DispatchThreadID)
{
    const uint cell = dtid.x;
    if (cell >= (uint)AVER_RC_CELLS) return;

    const RcInfo info = gRcInfo[0];
    if ((info.hdr0.x & 1u) != 0u) return;   // the resolve clears every cell this frame: nothing to learn from
    const uint cascade = cell / (uint)AVER_RC_CELLS_PER_CASC;
    // This frame's accepted samples, capped as the resolve will: a cell without any is not touched.
    const uint n = min((uint)gAccum[cell] & 0xFFFFu, rcCapFor(info, cascade));
    if (n == 0u) return;

    const uint local = cell - cascade * (uint)AVER_RC_CELLS_PER_CASC;
    const int3 texel = int3(local & 63u, (local >> 6) & 63u, local >> 12);
    const int3 org   = info.cas[cascade].xyz;
    const int3 world = org + ((texel - org) & 63);
    const uint tag   = rcTagPack(world);

    const RcCell stored = gCells[cell];
    if (!rcnValid(stored.b.w, tag)) return;   // a new or replaced cell has nothing to predict from; the resolve starts it at trust 1
    InterlockedAdd(gInfCnt[1], 1u);
    const uint frame = info.hdr0.y;
    if (!rcnKeep(rcnRate(gState[0], RCN_INFER_TARGET), cell, frame, 0u)) return;

    // Old state.
    const uint nEff = (stored.b.w >> 24) & 15u;
    const uint age  = stored.b.w >> 28;
    float len;
    const float3 nu = rcUnpackNormal(stored.b.z, len);
    float3 old[4];
    rcUnpackSh(stored.a, stored.b.xy, old);

    // Fresh mean, as the resolve forms it.
    const uint pb = (uint)AVER_RC_PAYLOAD_BASE + cell * (uint)AVER_RC_PAYLOAD_INTS;
    const float shInv = 1.0 / ((float)n * (float)AVER_RC_SH_SCALE);
    float3 fresh[4];
    [unroll] for (uint j = 0u; j < 4u; ++j)
        fresh[j] = float3((float)gAccum[pb + j * 3u], (float)gAccum[pb + j * 3u + 1u], (float)gAccum[pb + j * 3u + 2u]) * shInv;

    const float Eo = rcnLum(rcShIrradianceOverPi(old, nu));
    const float Ef = rcnLum(rcShIrradianceOverPi(fresh, nu));

    // The six face neighbours at this cascade, old state, weighted by how well their normal agrees with ours.
    float sumW = 0.0, sumWE = 0.0;
    float wk[6], ek[6];
    [unroll] for (int k = 0; k < 6; ++k)
    {
        wk[k] = 0.0;
        ek[k] = 0.0;
        const int3 nw = world + kRcnDir[k];
        const RcCell nb = gCells[rcCellIndex(cascade, nw)];
        if (rcnValid(nb.b.w, rcTagPack(nw)))
        {
            float nl;
            const float3 nn = rcUnpackNormal(nb.b.z, nl);
            const float w = saturate(2.0 * dot(nn, nu)) * nl;
            if (w > 0.0)
            {
                float3 c[4];
                rcUnpackSh(nb.a, nb.b.xy, c);
                wk[k] = w;
                ek[k] = rcnLum(rcShIrradianceOverPi(c, nu));
                sumW += w;
                sumWE += w * ek[k];
            }
        }
    }
    float disagree = 0.0, ratio = 0.0;
    if (sumW > 1.0e-6)
    {
        const float ebar = sumWE / sumW;
        float dev = 0.0;
        [unroll] for (int k2 = 0; k2 < 6; ++k2) dev += wk[k2] * abs(ek[k2] - ebar);
        disagree = dev / (sumW * (ebar + RCN_EPS));
        ratio = clamp(log2((ebar + RCN_EPS) / (Eo + RCN_EPS)), -4.0, 4.0) * 0.25;
    }
    const float aniso = length(float3(rcnLum(old[1]), rcnLum(old[2]), rcnLum(old[3]))) / max(rcnLum(old[0]), RCN_EPS);

    float f[RCN_INPUTS];
    f[0] = len;
    f[1] = (float)nEff / 15.0;
    f[2] = (float)age / 15.0;
    f[3] = 1.0;                                  // inference: sampling noise is not read as unreliability
    f[4] = sumW / 6.0;
    f[5] = min(disagree, 2.0) * 0.5;
    f[6] = ratio;
    f[7] = min(aniso, 2.0) * 0.5;
    f[8] = nu.y;
    f[9] = (float)cascade * 0.5;

    // The label comes from traced samples only: how far this frame's mean moved the cell's own light.
    const float rho = abs(Ef - Eo) / (Ef + Eo + RCN_EPS);
    const float label = 1.0 - min(1.0, 2.0 * rho);
    const bool eligible = n >= RCN_MIN_FRESH && nEff >= RCN_MIN_NEFF && Eo + Ef > RCN_EPS;

    uint slot;
    InterlockedAdd(gInfCnt[0], 1u, slot);
    if (slot >= RCN_MAX_INFER) return;
    [unroll] for (uint i = 0u; i < RCN_INPUTS; ++i) gInfRec[slot * RCN_INPUTS + i] = f[i];
    gRecCell[slot]  = uint2(cell, tag);
    gRecLabel[slot] = eligible ? label : -1.0;

    if (!eligible) return;
    InterlockedAdd(gTrainCnt[1], 1u);
    if (!rcnKeep(rcnRate(gState[1], RCN_TRAIN_TARGET), cell, frame, 1u)) return;
    uint ts;
    InterlockedAdd(gTrainCnt[0], 1u, ts);
    if (ts >= RCN_MAX_TRAIN) return;
    f[3] = log2(1.0 + (float)n) / 6.0;
    [unroll] for (uint m = 0u; m < RCN_INPUTS; ++m) gTrainRec[ts * RCN_INPUTS + m] = f[m];
    gTrainTgt[ts] = label;
}

[numthreads(1, 1, 1)]
void CSRcNetFinish()
{
    // Raw totals (the atomics may have run past the lists) steer next frame's thinning; the Mlp reads the clamped counts.
    gState[0] = gInfCnt[1];
    gState[1] = gTrainCnt[1];
    gState[2] = gInfCnt[0];
    gState[3] = gTrainCnt[0];
    gInfCnt[0]   = min(gInfCnt[0], RCN_MAX_INFER);
    gTrainCnt[0] = min(gTrainCnt[0], RCN_MAX_TRAIN);
}

groupshared uint gsStat[6][64];

[numthreads(64, 1, 1)]
void CSRcNetApply(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    const uint i = dtid.x;
    uint s[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    if (i < gInfCnt[0])
    {
        const uint2 rec = gRecCell[i];
        const uint4 b = gCells[rec.x].b;
        const float pred = gInfOut[i];
        // The resolve may have emptied or replaced the cell, and a broken prediction keeps the old trust.
        if (rcnValid(b.w, rec.y) && isfinite(pred))
        {
            const uint t6 = rcnTrust6(pred);
            gCells[rec.x].b.z = (b.z & 0x03FFFFFFu) | (t6 << 26);
            s[4] = 1u;
            s[5] = t6;
            const float label = gRecLabel[i];
            if (label >= 0.0)
            {
                const float p = saturate(pred), e = p - label;
                s[0] = 1u;
                s[1] = (uint)(label * RCN_STAT_SCALE + 0.5);
                s[2] = (uint)(p * RCN_STAT_SCALE + 0.5);
                s[3] = (uint)(e * e * RCN_STAT_SCALE + 0.5);
            }
        }
    }
    // A fixed-order tree sum per group, then six atomics.
    [unroll] for (uint q = 0u; q < 6u; ++q) gsStat[q][gi] = s[q];
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = 32u; stride > 0u; stride >>= 1)
    {
        if (gi < stride)
        {
            [unroll] for (uint r = 0u; r < 6u; ++r) gsStat[r][gi] += gsStat[r][gi + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0u && gsStat[4][0] != 0u)
    {
        [unroll] for (uint t = 0u; t < 6u; ++t) InterlockedAdd(gStats[t], gsStat[t][0]);
    }
}
