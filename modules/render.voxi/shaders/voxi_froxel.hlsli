// voxi_froxel.hlsli -- the froxel fog volume's grid maths and lookup, shared by voxi.hlsl (the shade sites)
// and voxi_froxel.hlsl (the compute passes and the sky draw). docs/rendering/VOLUMETRIC_FOG.md.
//
// The volume is camera-aligned: x/y span the camera's NDC (gViewProj, jitter included -- the volume is built
// and read through the same matrix, so the sub-pixel jitter cancels), z is exponential in view depth from
// `nearCm` to the maximum distance. Texel z holds the light accumulated from the camera out to the FAR
// boundary of slice z: rgb = in-scatter radiance, a = transmittance of the uniform medium. Applied as
// `colour * a + rgb` on top of the analytic height fog (which keeps its own extinction).
//
// Needs gViewProj (the engine per-frame block) in scope. The volume is a parameter so voxi.hlsl (u24) and the
// sky pass (u0) each bind it where their own layout puts it.

// One texel, with the "before the first boundary" row (z < 0) reading as nothing-yet.
float4 froxelFetch(RWTexture3D<float4> vol, int3 c, int3 dim) {
    if (c.z < 0) return float4(0.0, 0.0, 0.0, 1.0);
    c = clamp(c, int3(0, 0, 0), dim - 1);
    return vol[uint3(c)];
}

// Trilinear read at a viewport-relative uv (0..1, y down) and a view-space depth in cm. Typed UAV loads,
// so the filter is done by hand; depths past the volume read its last slice.
float4 froxelLookup(RWTexture3D<float4> vol, float2 uv, float viewZ, float nearCm, float invLogRange) {
    uint3 du;
    vol.GetDimensions(du.x, du.y, du.z);
    const int3 dim = int3(du);
    const float w = saturate(log(max(viewZ, nearCm) / nearCm) * invLogRange);
    // Texel z holds the far boundary of slice z, i.e. w = (z + 1) / depth.
    const float3 t = float3(uv * float2(dim.xy) - 0.5, w * float(dim.z) - 1.0);
    const float3 fl = floor(t);
    const float3 f = t - fl;
    const int3 i0 = int3(fl);
    float4 r = float4(0.0, 0.0, 0.0, 0.0);
    [unroll] for (int k = 0; k < 8; ++k) {
        const int3 o = int3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        const float3 wt = lerp(1.0 - f, f, float3(o));
        r += froxelFetch(vol, i0 + o, dim) * (wt.x * wt.y * wt.z);
    }
    return r;
}

// The volume at a world position: what lies between the camera and it. Behind the eye reads as clear air.
float4 froxelAtWorld(RWTexture3D<float4> vol, float3 wpos, float nearCm, float invLogRange) {
    const float4 clip = mul(float4(wpos, 1.0), gViewProj);
    if (clip.w <= 1e-3) return float4(0.0, 0.0, 0.0, 1.0);
    const float2 ndc = clip.xy / clip.w;
    const float2 uv = saturate(float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5));
    return froxelLookup(vol, uv, clip.w, nearCm, invLogRange);
}
