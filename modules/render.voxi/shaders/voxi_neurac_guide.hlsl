// NeuRaC guide: the cached light at every primary surface pixel, for NRD2's anchor (NeuRaCGuide.cpp).
// rgb = irradiance/pi from the cascade lookup, a = 1 - the fraction the cache could not fill.
// Standalone compute with its own small layout; nothing here touches Voxi's table 0.
#include "voxi_neurac.hlsli"

StructuredBuffer<RcInfo>   gRcInfo      : register(t0);   // this frame's ring slot
Texture2D<float>           gGuideViewZ  : register(t1);   // G-buffer view Z, cm
Texture2D<float4>          gGuideNormal : register(t2);   // G-buffer averPackNormalRoughness
RWStructuredBuffer<RcCell> gRcCells     : register(u0);   // read only
RWTexture2D<float4>        gGuide       : register(u1);
RWStructuredBuffer<uint>   gGuideStats  : register(u2);   // [0] surface pixels, [1] covered; running totals

cbuffer GuideCB : register(b3) {
    uint4  gRect;          // viewport x y w h (render-target pixels)
    float4 gView[3];       // right, up, forward (xyz); w of right and up: tan of the half FOV
    float4 gEyeCell[3];    // per cascade: eye / size - originCell, from the CPU in double (about [32, 33))
};

// Shared lookup core: rcLookupCascade(c, q, N, out value, out a), q in window cells.
#include "voxi_neurac_lookup.hlsli"

// The decode half of voxi.hlsl's averPackNormalRoughness.
float3 guideDecodeNormal(float4 e) {
    const float2 f = e.xy * 2.0 - 1.0;
    float3 n = float3(f, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}

groupshared uint gsSurf[64];
groupshared uint gsCov[64];

[numthreads(8, 8, 1)]
void CSNeuracGuide(uint3 t : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    uint surf = 0u, cov = 0u;
    if (all(t.xy < gRect.zw))
    {
        const int2  p = int2(gRect.xy + t.xy);
        const float z = gGuideViewZ.Load(int3(p, 0));
        float4 outG = 0.0;
        if (z > 0.0 && z < 1.0e6)
        {
            const float3 N = guideDecodeNormal(gGuideNormal.Load(int3(p, 0)));
            const float2 ndc = float2((float(t.x) + 0.5) / float(gRect.z) * 2.0 - 1.0,
                                      1.0 - (float(t.y) + 0.5) / float(gRect.w) * 2.0);
            // The surface point, cm from the eye. Added to the eye cell below, so no absolute world position
            // is ever formed in fp32.
            const float3 off = z * (gView[2].xyz + ndc.x * gView[0].w * gView[0].xyz +
                                    ndc.y * gView[1].w * gView[1].xyz);
            float3 e = 0.0;
            float  rem = 1.0;
            const RcInfo info = gRcInfo[0];
            // Finest first, blended as rcLookup does.
            [loop] for (uint c = 0u; c < (uint)AVER_RC_CASCADES; ++c)
            {
                if (rem < 0.01) break;
                const float  size = asfloat(info.cas[c].w);
                const float3 q    = (off + N * (0.5 * size)) / size + gEyeCell[c].xyz;
                float3 value = 0.0;
                float  a     = 0.0;
                rcLookupCascade(c, q, N, value, a);
                e   += rem * a * value;
                rem *= 1.0 - a;
            }
            // A non-finite cache value is no guide at all.
            const bool ok   = all(isfinite(e)) && isfinite(rem);
            const float conf = ok ? saturate(1.0 - rem) : 0.0;
            outG = float4(ok ? e : 0.0, conf);
            surf = 1u;
            cov  = conf >= 0.5 ? 1u : 0u;
        }
        gGuide[p] = outG;
    }

    // Coverage: a fixed-order tree sum per group, then two atomics.
    gsSurf[gi] = surf;
    gsCov[gi]  = cov;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 32u; s > 0u; s >>= 1)
    {
        if (gi < s)
        {
            gsSurf[gi] += gsSurf[gi + s];
            gsCov[gi]  += gsCov[gi + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0u && gsSurf[0] != 0u)
    {
        InterlockedAdd(gGuideStats[0], gsSurf[0]);
        InterlockedAdd(gGuideStats[1], gsCov[0]);
    }
}
