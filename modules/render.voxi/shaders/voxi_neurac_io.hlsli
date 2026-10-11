// Radiance cache I/O: SCATTER (training) and LOOKUP (read).
// Included after voxi_neurac.hlsli and RcInfo/RcAccum/RcCells declarations.
// Resources: gRcInfo (t22), gRcAccum (u20), gRcCells (u21).
// WORLD UNITS: cascade cell sizes match cbuffer's gCamPos and engine world unit.

#include "voxi_neurac_lookup.hlsli"

// Lookup's last confidence (1 - fallback fraction) for debug view.
static float gRcLastConf = 0.0;

// Cache enabled: gAmbientParams.w bit 128 set by CPU when cache pipelines run live.
bool rcCacheOn() { return ((uint)gAmbientParams.w & 128u) != 0u; }

// ---- SCATTER: one second-bounce ray = one Monte Carlo sample of incident radiance ----
// Hash of a sample's identity to 16 bits (murmur3 finalizer over the position and direction bits).
// CPU twin: neurac::hash16.
uint rcHash16(float3 hitPos, float3 dir2, uint c) {
    const uint3 a = asuint(hitPos), b = asuint(dir2);
    uint h = 0x9E3779B9u * (c + 1u);
    h = (h ^ a.x) * 0x85EBCA6Bu;
    h = (h ^ a.y) * 0xC2B2AE35u;
    h = (h ^ a.z) * 0x85EBCA6Bu;
    h = (h ^ b.x) * 0xC2B2AE35u;
    h = (h ^ b.y) * 0x85EBCA6Bu;
    h = (h ^ b.z) * 0xC2B2AE35u;
    h ^= h >> 16;  h *= 0x85EBCA6Bu;
    h ^= h >> 13;  h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h >> 16;
}

// One cascade's share of a sample. Bins at the lookup's own cell (hit offset half a cell along N) and
// trains only interior cells, so the cell holding a lookup point carries at least half its 1D weight.
// Accepts at a rate set by last frame's arrival count, so the per-frame cap keeps a random subset
// of arrivals rather than the first ones; the count atomic is the hard bound.
void rcScatterCascade(uint c, float3 hitPos, float3 N, float3 dir2, float3 L, float wgt, RcInfo info) {
    const float size = asfloat(info.cas[c].w);
    const int3  cell = (int3)floor((hitPos + N * (0.5 * size)) / size);
    const int3  rel  = cell - info.cas[c].xyz;
    if (any(rel < AVER_RC_MARGIN_CELLS) || any(rel > AVER_RC_RES - 1 - AVER_RC_MARGIN_CELLS)) return;
    const uint cap = rcCapFor(info, c);
    if (cap == 0u) return;
    const uint idx = rcCellIndex(c, cell);
    InterlockedAdd(gRcAccum[AVER_RC_CELLS + idx], 1);                       // arrival
    // High half = last frame's arrivals (constant during scatter); low half = accepted so far.
    const uint word = (uint)gRcAccum[idx];
    const uint prev = word >> 16;
    if ((word & 0xFFFFu) >= cap) return;                                    // stale-safe prefilter: keeps the low half from carrying
    const float rate = prev <= cap ? 1.0 : max((float)cap / (float)prev, (float)AVER_RC_MIN_ACCEPT_RATE);
    if (rcHash16(hitPos, dir2, c) >= (uint)(rate * 65536.0)) return;
    int old;
    InterlockedAdd(gRcAccum[idx], 1, old);
    if ((uint)(old & 0xFFFF) >= cap) return;
    float y[4];
    rcShBasis(dir2, y);
    const uint base = (uint)AVER_RC_PAYLOAD_BASE + idx * (uint)AVER_RC_PAYLOAD_INTS;
    int ignored;
    [unroll] for (int k = 0; k < 4; ++k) {
        const float3 t = L * (y[k] * wgt * AVER_RC_SH_SCALE);
        InterlockedAdd(gRcAccum[base + k * 3 + 0], (int)round(t.x), ignored);
        InterlockedAdd(gRcAccum[base + k * 3 + 1], (int)round(t.y), ignored);
        InterlockedAdd(gRcAccum[base + k * 3 + 2], (int)round(t.z), ignored);
    }
    const float3 nn = N * AVER_RC_N_SCALE;
    InterlockedAdd(gRcAccum[base + 12], (int)round(nn.x), ignored);
    InterlockedAdd(gRcAccum[base + 13], (int)round(nn.y), ignored);
    InterlockedAdd(gRcAccum[base + 14], (int)round(nn.z), ignored);
}

// Scatter a Monte Carlo sample into every cascade whose interior holds it, finest first.
// [loop], not unrolled: the twin is register-bound.
void rcScatter(float3 hitPos, float3 N, float3 dir2, float3 indY, float cosDir2) {
    const float3 L = min(max(indY, 0.0), AVER_RC_LMAX);
    if (!all(isfinite(L)) || !all(isfinite(dir2)) || !all(isfinite(N))) return;
    const RcInfo info = gRcInfo[0];
    const float  wgt  = 3.14159265 / max(cosDir2, AVER_RC_MIN_COS);
    [loop] for (uint c = 0; c < (uint)AVER_RC_CASCADES; ++c) rcScatterCascade(c, hitPos, N, dir2, L, wgt, info);
}

// ---- LOOKUP: cosine-convolved irradiance / PI at p for normal N, blended across cascades ----
// Blend across cascades (finest first); the per-cascade core is in voxi_neurac_lookup.hlsli.
// Returns confidence and unfilled light fraction in `remaining` for fallback fill.
float3 rcLookup(float3 p, float3 N, out float remaining) {
    remaining = 1.0;
    float3 outV = 0.0;
    gRcLastConf = 0.0;
    const RcInfo info = gRcInfo[0];
    [loop] for (uint c = 0; c < (uint)AVER_RC_CASCADES; ++c) {
        if (remaining < 0.01) break;
        const float  size = asfloat(info.cas[c].w);
        const float3 q    = (p + N * (0.5 * size)) / size - (float3)info.cas[c].xyz;   // position in window cells, [0, 64)
        float3 value;
        float  a;
        rcLookupCascade(c, q, N, value, a);
        outV      += remaining * a * value;
        remaining *= 1.0 - a;
    }
    gRcLastConf = 1.0 - remaining;
    return outV;
}

// ---- VISUALISATION (gAmbientParams.w bits 8-10 mode, bit 11 cell grid; givis::packAmbientW) ----
// NeuRaC visualiser: modes 1-5 show cached light / coverage / cascade / cell state / trust.
// Looked up at the primary surface (world-space, trained at second-bounce hits).
uint rcViewMode() { return ((uint)gAmbientParams.w >> 8) & 7u; }
bool rcViewGrid() { return ((uint)gAmbientParams.w & 2048u) != 0u; }

float3 rcDebugColour(float3 p, float3 N) {
    const uint   mode = rcViewMode();
    const RcInfo info = gRcInfo[0];
    // Find the finest cascade whose window holds p.
    int    cas  = -1;
    float  size = 1.0;
    float3 qc   = 0.0;   // p in that cascade's cell units
    [loop] for (uint c = 0; c < (uint)AVER_RC_CASCADES; ++c) {
        const float  s = asfloat(info.cas[c].w);
        const float3 q = (p + N * (0.5 * s)) / s;
        const float3 rel = q - (float3)info.cas[c].xyz;
        if (all(rel >= (float)AVER_RC_MARGIN_CELLS) && all(rel < (float)(AVER_RC_RES - AVER_RC_MARGIN_CELLS))) {
            cas = (int)c; size = s; qc = q;
            break;
        }
    }
    float3 col = float3(0.25, 0.0, 0.0);   // outside every window
    if (mode == 1u || mode == 2u) {
        float rem;
        const float3 v = rcLookup(p, N, rem);
        col = mode == 1u ? v : lerp(float3(1.0, 0.08, 0.05), float3(0.1, 1.0, 0.2), 1.0 - rem);
    } else if (mode == 3u) {
        if (cas == 0) col = float3(0.1, 0.9, 1.0);
        else if (cas == 1) col = float3(1.0, 0.9, 0.1);
        else if (cas == 2) col = float3(1.0, 0.45, 0.05);
    } else if (mode == 4u && cas >= 0) {
        const int3   wc   = (int3)floor(qc);
        const RcCell cell = gRcCells[rcCellIndex((uint)cas, wc)];
        const uint   meta = cell.b.w;
        const uint   neff = (meta >> 24) & 15u;
        const uint   age  = meta >> 28;
        const bool   live = neff != 0u && (meta & 0xFFFFFFu) == rcTagPack(wc) && age < (uint)AVER_RC_AGE_MAX;
        col = live ? float3((float)age / (float)AVER_RC_AGE_MAX, (float)neff / (float)AVER_RC_NEFF_MAX, 0.1)
                   : float3(0.2, 0.0, 0.3);
    } else if (mode == 5u && cas >= 0) {
        const int3   wc   = (int3)floor(qc);
        const RcCell cell = gRcCells[rcCellIndex((uint)cas, wc)];
        const uint   meta = cell.b.w;
        const uint   neff = (meta >> 24) & 15u;
        const uint   age  = meta >> 28;
        const bool   live = neff != 0u && (meta & 0xFFFFFFu) == rcTagPack(wc) && age < (uint)AVER_RC_AGE_MAX;
        float len, trust;
        rcUnpackNormal(cell.b.z, len, trust);
        col = live ? lerp(float3(1.0, 0.1, 0.05), float3(0.1, 1.0, 0.2), trust) : float3(0.2, 0.0, 0.3);
    }
    // Cell edges: darken within 4% of a boundary on the two axes the surface runs along.
    if (rcViewGrid() && cas >= 0) {
        const float3 f  = frac(qc);
        const float3 d  = min(f, 1.0 - f);
        const float3 an = abs(N);
        const float  ex = an.x >= an.y && an.x >= an.z ? 1.0 : d.x;
        const float  ey = an.y > an.x && an.y >= an.z ? 1.0 : d.y;
        const float  ez = an.z > an.x && an.z > an.y ? 1.0 : d.z;
        if (min(ex, min(ey, ez)) < 0.04) col *= 0.2;
    }
    return col;
}
