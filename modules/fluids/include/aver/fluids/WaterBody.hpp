#pragma once
// Spline rivers and closed-outline lakes: the CPU half. A Spline (centripetal Catmull-Rom in XY, monotone
// cubic for z / width / flow), the meshes drawn from them, and WaterBodySet, the point query buoyancy and
// gameplay use. No device: the meshes are plain rhi::MeshVertex arrays the host uploads with createMesh,
// so rays see them as ordinary geometry (the reason WaterSurface is a mesh too).
//
// Units: centimetres, +Z up. A river's mesh carries its flow in uv0 (world XY velocity, cm/s) for the
// material graph's VertexFlow / FlowNormal nodes. Winding is front-facing from +Z, the rule
// buildWaterGrid documents (clockwise seen from +Z in a right-handed XY frame).
#include "aver/core/Types.hpp"
#include "aver/rhi/RHI.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace aver::fluids {

struct SplinePoint { f32 x = 0, y = 0, z = 0, width = 0, flow = 0; };

class Spline {
public:
    // XY: centripetal Catmull-Rom (alpha 0.5). z, width, flow: monotone cubic (PCHIP) over the same chord
    // parameter, so they never overshoot their control values.
    void set(std::vector<SplinePoint> pts, bool closed);

    struct Sample { f32 pos[3] = {}; f32 tangent[2] = {1, 0}; f32 width = 0, flow = 0, s = 0; };

    u32  pointCount() const { return static_cast<u32>(pts_.size()); }
    bool closed() const { return closed_; }
    f32  length() const { return length_; }
    Sample at(f32 s) const;                                  // arc length, clamped to [0, length]
    // Samples every ~stepCm of arc length, at least 4 steps per Catmull-Rom segment, at most 4096 samples
    // in all (the step grows to fit). An open spline includes both ends; a closed one omits the repeat of
    // the first point.
    void sample(f32 stepCm, std::vector<Sample>& out) const;
    bool closest(f32 x, f32 y, f32& outS, f32& outDist) const;   // nearest point of the curve, false if empty

private:
    struct Seg { f64 p[4][2]; f64 knot0 = 0, knotLen = 1; u32 k0 = 0, k1 = 0; };
    void evalSeg(u32 seg, f64 t, f64 xy[2]) const;
    f64  pchip(const std::vector<f64>& d, const std::vector<f64>& y, u32 seg, f64 t) const;

    std::vector<SplinePoint> pts_;
    bool closed_ = false;
    f32 length_ = 0;
    std::vector<Seg> segs_;
    std::vector<f64> knots_, dz_, dw_, df_;        // PCHIP knots and per-knot derivatives
    std::vector<f64> vz_, vw_, vf_;                // knot values (one extra for a closed spline)
    std::vector<f32> tabS_;                        // arc length at each table entry
    std::vector<f32> tabXY_;                       // x,y at each table entry
    static constexpr u32 kSub = 32;                // table entries per segment
};

struct RiverDesc {
    std::string name, material;
    Spline spline;
    f32 depthCm = 200, bankCm = 300, bankSlow = 0.35f, flowScale = 1;
};
struct LakeDesc {
    std::string name, material;
    Spline outline;                                // closed; only x, y matter
    f32 levelCm = 0, depthCm = 300, bankCm = 400, maxEdgeCm = 2500;
};

// Rows every <= stepCm (and at least 4 per spline segment), 3 vertices per row (left, centre, right).
void buildRiverMesh(const RiverDesc&, f32 stepCm, std::vector<rhi::MeshVertex>& v, std::vector<u32>& idx);
// Outline sampled every 300 cm, ear-clipped, then subdivided (conforming, watertight) until every edge is
// <= maxEdgeCm. Flat at levelCm.
void buildLakeMesh(const LakeDesc&, std::vector<rhi::MeshVertex>& v, std::vector<u32>& idx);

enum class WaterKind : u8 { None = 0, Ocean = 1, Lake = 2, River = 3 };

struct WaterQuery {
    bool over = false;
    f32 surfaceZ = 0, normal[3] = {0, 0, 1}, current[3] = {}, depthCm = 0;
    WaterKind kind = WaterKind::None;
    u32 body = 0;                                  // the id addRiver / addLake returned
};

class WaterBodySet {
public:
    void clear();
    u32  addRiver(RiverDesc);
    u32  addLake(LakeDesc);
    void finalize();                               // builds a 1000 cm uniform grid over segments / polygons
    bool sample(f32 x, f32 y, WaterQuery& out) const;   // highest surface wins where bodies overlap
    u32  bodyCount() const { return static_cast<u32>(bodies_.size()); }
    // Optional ground height for depthCm = surfaceZ - ground; without it (or when it returns false) the
    // body's authored depth is reported.
    std::function<bool(f32, f32, f32&)> ground;

private:
    struct Body {
        WaterKind kind = WaterKind::None;
        f32 depthCm = 0, bankSlow = 1, flowScale = 1, levelCm = 0;
        std::vector<Spline::Sample> rows;          // river: samples every 250 cm
        std::vector<f32> poly;                     // lake: x,y pairs
        f32 minX = 0, minY = 0, maxX = 0, maxY = 0;
    };
    struct Entry { u32 body; u32 seg; };           // seg: river segment index, or lake (unused)
    static i64 cellKey(i32 cx, i32 cy) { return (static_cast<i64>(cx) << 32) ^ static_cast<i64>(static_cast<u32>(cy)); }

    std::vector<Body> bodies_;
    std::unordered_map<i64, std::vector<Entry>> cells_;   // river segments by 1000 cm cell
};

} // namespace aver::fluids
