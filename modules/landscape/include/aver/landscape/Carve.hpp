#pragma once
// One-shot terrain carving for river beds and lake basins: lowers heights along a polyline (river) or
// inside a closed polygon (basin). Takes plain sampled points so it needs no spline type. Only ever
// LOWERS (`h' = min(h, bed)`), so a carve over terrain that is already lower changes nothing. The
// caller records the before/after of the returned rect for undo (same path as a sculpt stroke).
#include "aver/landscape/Sculpt.hpp"

namespace aver::landscape {

// One sample of a river centreline: world XY, water surface Z there, half the water width.
struct CarvePoint { f32 x, y, surfaceZ, halfWidth; };

struct CarveParams {
    f32 depthCm = 200.0f;        // channel depth below the surface at the centre
    f32 bankCm = 300.0f;         // how far past the water edge the banks reach
    f32 bankSlopeDeg = 35.0f;    // bank slope outside the water edge
    f32 smooth = 0.5f;           // 0..1: one 3x3 relaxation of the carved samples (never raises past the original)
};

// The rect a river carve can reach (empty when nothing overlaps the grid).
BrushRect carveRect(const fmt::OcLandData& d, const CarvePoint* pts, u32 n, const CarveParams& p);
// The rect a basin carve can reach.
BrushRect basinRect(const fmt::OcLandData& d, const f32* polyXY, u32 count, const CarveParams& p);

// Carves a channel along the polyline `pts` (n >= 1). Returns the touched rect (carveRect) with
// bounds refreshed; heights outside it are untouched.
BrushRect carveRiver(fmt::OcLandData& d, const CarvePoint* pts, u32 n, const CarveParams& p);
// Carves a basin inside the closed polygon (count >= 3 vertices as x,y pairs) at water level `levelCm`.
BrushRect carveBasin(fmt::OcLandData& d, const f32* polyXY, u32 count, f32 levelCm, const CarveParams& p);

} // namespace aver::landscape
