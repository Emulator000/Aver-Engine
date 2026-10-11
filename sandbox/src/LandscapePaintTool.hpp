#pragma once
// Editor helpers for the Landscape mode's Paint tab (docs/LANDSCAPE_LAYERS_WATER.md): the foliage scatter
// candidates for one paint layer, and the pure parts of the layered-terrain material editor. No ImGui and no
// SandboxApp in here, so the logic can be read (and tested) on its own; the tab itself is
// SandboxApp::buildLandscapePaintTab in LandscapePaintTool.cpp.
#include "aver/core/Types.hpp"
#include "aver/formats/OcLand.hpp"

#include <vector>

namespace aver::editor {

// One place a layer-driven foliage instance may go, with the random stream that instance draws its scale and
// yaw from (so a re-run reproduces it exactly).
struct ScatterPoint { f32 x = 0.0f, y = 0.0f; u32 rng = 1u; };

// Candidates for a section layer: a jittered grid whose cell area is 1e6 / densityPer100m2 cm^2, each cell's
// jitter a pure hash of (salt, cell), kept where the layer's paint weight is at least `minWeight`. Never more
// than `cap` points. `sectionLayer` indexes OcLandData::layerNames.
void layerScatterCandidates(const fmt::OcLandData& d, u32 sectionLayer, f32 densityPer100m2, f32 minWeight, u32 salt,
                            usize cap, std::vector<ScatterPoint>& out);

} // namespace aver::editor
