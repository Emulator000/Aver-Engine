#pragma once
// The displaced water surface: a camera-following (or bounds-clamped) grid MESH whose vertices a
// compute pass moves by the same Gerstner waves the analytic ocean uses (WaterRenderer).
//
// WHY A MESH AND NOT ANOTHER PIXEL SHADER. WaterRenderer draws its own pipeline in transparentPass,
// so it is invisible to the ray-traced primary visibility, shadows, reflections and fog -- and the
// editor's primary visibility is ray traced. This surface is ordinary geometry instead: a
// createSkinTargetMesh mesh the host draws through the standard material path (setDrawBinding +
// setDrawBlended + drawMesh) with a WATER record's .ocmat, and Voxi refits its BLAS every frame
// because the vertices are compute-written. Rasteriser and rays therefore see the same waves.
//
// Same recipe as the skinning pass (modules/render.skin): the compute dispatch runs in prePass --
// also Vulkan's rule, compute outside the scene scope -- and the buffer is returned to Common in
// overlayPass so no state outlives the frame.
#include "aver/fluids/GerstnerWave.hpp"
#include "aver/rhi/RHI.hpp"
#include "aver/rhi/RHIResources.hpp"

#include <vector>

namespace aver::fluids {

// Most cells along one axis. 512^2 cells is 263k vertices, the most a per-frame BLAS refit should be
// asked to carry; a larger request is coarsened (cell enlarged) rather than refused.
inline constexpr u32 kWaterMaxCellsPerAxis = 512;

struct WaterSurfaceDesc {
    f32 levelCm = 0.0f;       // surface height, world Z
    f32 cellCm = 100.0f;      // grid cell size (WATER `cell`)
    f32 radiusCm = 12800.0f;  // half-extent of the camera-following grid (WATER `radius`)

    // Bounded: the grid covers this rectangle (X, Y cm) and stays put instead of following the camera.
    bool bounded = false;
    f32 boundsMinCm[2] = {0.0f, 0.0f};
    f32 boundsMaxCm[2] = {0.0f, 0.0f};
};

// The grid's shape, derived from a desc. Separate from the GPU class so it can be tested on the CPU.
struct WaterGridLayout {
    u32 vertsX = 0, vertsY = 0;   // vertices per axis (cells + 1)
    f32 cellX = 0.0f, cellY = 0.0f;   // actual cell size, cm
    // Unbounded only: flat rings from the grid's edge out to the horizon (the grid edge scaled by
    // skirtScale), so open water reaches it; the waves fade out before the edge so the seam is flat.
    // Ring r sits at skirtScale^(r/skirtRings): each at most twice the last, because one ring spanning
    // the whole scale made triangles ~10 km long that the RX 7800 XT's ray tests dropped in strips.
    f32 skirtScale = 0.0f;
    u32 skirtRings = 0;
};

// The horizon rings: one vertex per grid boundary vertex, per ring.
inline u32 waterSkirtVertexCount(const WaterGridLayout& l) {
    return l.skirtScale > 1.0f && l.vertsX > 1 ? 4u * (l.vertsX - 1) * l.skirtRings : 0u;
}

// Unbounded: an even cell count (so a vertex sits on the origin) of `cellCm`-sized cells spanning
// 2 * radiusCm, coarsened if that exceeds kWaterMaxCellsPerAxis. Bounded: the bounds divided into
// cells of about `cellCm`, the size adjusted so the grid fills them exactly. Degenerate input is
// clamped, never refused.
WaterGridLayout waterGridLayout(const WaterSurfaceDesc& d);

inline u32 waterGridVertexCount(const WaterGridLayout& l) { return l.vertsX * l.vertsY + waterSkirtVertexCount(l); }
inline u32 waterGridIndexCount(const WaterGridLayout& l) {
    return l.vertsX > 1 && l.vertsY > 1 ? (l.vertsX - 1) * (l.vertsY - 1) * 6u + waterSkirtVertexCount(l) * 6u : 0u;
}

// The flat grid, LOCAL about its origin and centred on it, +Z normals, uv = local xy / 100. Winding
// is front-facing seen from +Z (the (p,q),(p,q+1),(p+1,q) pattern FluidVolume documents).
void buildWaterGrid(const WaterGridLayout& l, std::vector<rhi::MeshVertex>& verts, std::vector<u32>& indices);

// World XY the grid is centred on for a camera at (camX, camY): the camera floored to the cell lattice
// when unbounded (so no vertex swims as the camera moves), the bounds' centre when bounded.
void waterGridOrigin(const WaterSurfaceDesc& d, const WaterGridLayout& l, f32 camX, f32 camY, f32 out[2]);

class WaterSurface final : public rhi::IRenderFeature {
public:
    ~WaterSurface() override;

    // Compiles the displacement shader and builds its pipeline. False leaves the feature inert (a
    // device without a resource factory or compute is a supported configuration).
    bool init(rhi::IDevice& dev);
    void shutdown();
    bool ready() const { return pipeline_ != 0; }

    // (Re)builds the grid mesh for `d`, replacing any previous one. False when the device refuses the
    // mesh; the surface then draws nothing.
    bool configure(const WaterSurfaceDesc& d);
    // Releases the mesh (the level unloaded). Safe to configure again afterwards.
    void clear();

    // Copies up to kMaxGerstnerWaves, as WaterRenderer::setWaves does.
    void setWaves(const GerstnerWave* waves, size_t count);
    // Advances the wave clock; clamped to [0, 1] seconds like WaterRenderer::tick.
    void tick(f32 dtSeconds);
    // The wave clock, in seconds (the compute pass's time; the CPU twin in buoyancy samples at the same instant).
    f32 elapsedSeconds() const { return elapsedSeconds_; }

    // Re-centres an unbounded grid under `camPos`. Call before the frame's draw: the draw matrix and
    // the compute pass both read the origin this sets, so they cannot disagree.
    void place(const f32 camPos[3]);

    // What to draw, or 0 before configure() or when the device refused the mesh.
    rhi::MeshHandle drawHandle() const { return drawMesh_; }
    // Translation to draw it with: {origin x, origin y, level}.
    void drawOriginCm(f32 out[3]) const;
    f32 levelCm() const { return desc_.levelCm; }
    // The height alone changes without rebuilding the grid: it only moves the draw translation.
    void setLevelCm(f32 z) { desc_.levelCm = z; }
    const WaterSurfaceDesc& desc() const { return desc_; }
    const WaterGridLayout& layout() const { return layout_; }

    const char* name() const override { return "Aver.Fluids.Surface"; }
    void prePass(rhi::IRenderContext& ctx) override;
    void overlayPass(rhi::IRenderContext& ctx, u32 width, u32 height) override;

private:
    rhi::IDevice* dev_ = nullptr;
    rhi::IResourceFactory* res_ = nullptr;

    rhi::ShaderHandle cs_ = 0;
    rhi::PipelineHandle pipeline_ = 0;
    rhi::BindingSetHandle set_ = 0;

    WaterSurfaceDesc desc_{};
    WaterGridLayout layout_{};
    rhi::MeshHandle source_ = 0;      // the flat grid; owned here, freed after drawMesh_
    rhi::MeshHandle drawMesh_ = 0;    // createSkinTargetMesh's result
    rhi::BufferHandle vertices_ = 0;  // drawMesh_'s vertex stream, freed with the mesh
    // Common, not GeometryRead: createSkinTargetMesh barriers the seeded buffer back to Common.
    rhi::ResourceState state_ = rhi::ResourceState::Common;

    GerstnerWave waves_[kMaxGerstnerWaves]{};
    size_t waveCount_ = 0;
    bool loggedWaveOverflow_ = false;
    bool loggedBadDt_ = false;
    f32 elapsedSeconds_ = 0.0f;
    f32 originXY_[2] = {0.0f, 0.0f};
};

}  // namespace aver::fluids
