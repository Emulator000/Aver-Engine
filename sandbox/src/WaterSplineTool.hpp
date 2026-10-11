#pragma once
// Editor helpers for the Landscape mode's Water tab (docs/LANDSCAPE_LAYERS_WATER.md): the pure operations on
// RIVER / LAKE records the tab's buttons run (reverse, snap to terrain, fit a lake level, sample a body into
// carve points) and the screen-space picking. No ImGui and no SandboxApp in here; the tab and its viewport
// tool are SandboxApp::buildLandscapeWaterTab / handleWaterSpline in WaterSplineTool.cpp.
#include "aver/core/Types.hpp"
#include "aver/formats/OcWorld.hpp"

#if AVER_MODULE_LANDSCAPE && AVER_MODULE_FLUIDS
#include "aver/landscape/Carve.hpp"

#include <functional>
#include <vector>

namespace aver::editor {

using GroundFn = std::function<bool(f64 x, f64 y, f64& outZ)>;

// Points in reverse order (the flow direction flips with them).
void reverseRiver(fmt::OcRiver& r);
// Every river point's z set to the ground under it (the ones off the section keep theirs).
void snapRiverToGround(fmt::OcRiver& r, const GroundFn& ground);
// A lake's level set to the lowest ground height along its outline. False when no outline point is on the section.
bool fitLakeLevel(fmt::OcLake& l, const GroundFn& ground);

// A river sampled every `stepCm` into the points carveRiver takes (surface z and half the water width at each).
void riverCarvePoints(const fmt::OcRiver& r, f32 stepCm, std::vector<landscape::CarvePoint>& out);
// A lake's outline sampled every `stepCm` as x,y pairs, for carveBasin.
void lakeOutlineXY(const fmt::OcLake& l, f32 stepCm, std::vector<f32>& out);

// The same lists, point for point? (An edit gesture that changed nothing makes no undo entry.)
bool sameBodies(const std::vector<fmt::OcRiver>& ra, const std::vector<fmt::OcRiver>& rb,
                const std::vector<fmt::OcLake>& la, const std::vector<fmt::OcLake>& lb);

// Screen-space picking: the index of the point of `xy` (pairs of pixel coordinates) nearest (mx, my) within
// `radiusPx`, or -1. Points flagged invalid (behind the camera: valid[i] == 0) are skipped.
int pickPoint(const std::vector<f32>& xy, const std::vector<u8>& valid, f32 mx, f32 my, f32 radiusPx);
// The segment of the polyline `xy` nearest (mx, my) within `radiusPx` (segment i joins points i and i+1; with
// `closed` the last joins the first), or -1; `outT` is the position along it.
int pickSegment(const std::vector<f32>& xy, const std::vector<u8>& valid, bool closed, f32 mx, f32 my, f32 radiusPx,
                f32& outT);

} // namespace aver::editor

#endif // AVER_MODULE_LANDSCAPE && AVER_MODULE_FLUIDS
