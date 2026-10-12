#pragma once
// InstanceSetModel -- the editing model behind a `.ocinst` (fmt::OcInstanceData) opened for hand
// edits: select, add, delete, duplicate and move instances, with delta undo/redo, a dirty flag, a
// spatial index for culling / marquee / ray picking, and an export back to OcInstanceData.
// ImGui-free on purpose, so a headless test (tests/editor InstanceSetModelTest) drives all of it; a
// panel or viewport tool is a thin layer over this.
//
// WHY NOT SnapshotUndo<State>. An instance table is up to millions of rows (OcInstances.hpp: 8M at
// kMaxFoliageInstances, 384 MB of transforms), and a whole-state snapshot per edit is a copy of that.
// Every edit here records only the ROWS IT CHANGED -- id, old and new matrix, alive flips -- so moving
// 50 instances costs 50 small records, and a drag is one step (beginEdit/endEdit coalesce).
//
// IDENTITY. A row is addressed by InstanceId = (group << 32 | row) and ids are STABLE: a delete only
// clears the row's alive flag and never shifts anything, an add appends, and undo/redo flip the flag
// back. That is what lets undo records, the selection and the spatial index all hold plain ids. Dead
// rows cost memory until export drops them (and the file is saved, which is when ids stop mattering).
//
// WHAT LOAD KEEPS. Group order, asset and flags, and each group's rows in order. Rows that no group
// covers are dropped. Cells/runs (the streaming bake, docs/LEVEL_STREAMING.md) are NOT kept and
// export clears them: an edit invalidates the cell bounds and the importer/bake recomputes them with
// fmt::cellOcInstances, so a stale table can never reach the streamer.
//
// SPATIAL INDEX. A uniform XY grid over each instance's WORLD AABB (the group's local bounds, which
// the caller supplies per asset, pushed through the instance matrix). An instance is listed in every
// cell its AABB overlaps (one that would span more than kMaxCellSpan cells goes in an "oversized"
// list tested on every query). It is built lazily on the first query and then maintained
// INCREMENTALLY: an edit appends the new cell entries and leaves the old ones, which is harmless
// because every candidate is re-tested against its CURRENT geometry; accumulated stale entries
// trigger a full rebuild past a churn threshold. NOT thread-safe: queries use per-group stamp arrays
// to de-duplicate (hence `mutable`).
//
// MATRICES are the .ocinst convention: 12 floats, the engine's row-vector world matrix with its 4th
// column dropped, t[r*3+c] = M[r][c], translation in row 3 (indices 9..11), centimetres. View-proj
// matrices are 16 floats row-major in the same row-vector convention (clip = [p,1] * VP), D3D 0..1
// depth -- exactly what ActorEditor's gizmo and PreviewChrome use.
#include "aver/formats/OcInstances.hpp"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace aver::editor {

// ---------------------------------------------------------------- 12-float affine helpers
namespace xf {

inline void identity(f32 m[12]) {
    for (int i = 0; i < 12; ++i) m[i] = 0.0f;
    m[0] = m[4] = m[8] = 1.0f;
}
inline void copy(f32 dst[12], const f32 src[12]) { for (int i = 0; i < 12; ++i) dst[i] = src[i]; }

// out = a then b (row-vector: p*a*b). `out` may alias a or b.
inline void mul(const f32 a[12], const f32 b[12], f32 out[12]) {
    f32 r[12];
    for (int row = 0; row < 4; ++row)
        for (int c = 0; c < 3; ++c) {
            f32 s = a[row*3+0]*b[0*3+c] + a[row*3+1]*b[1*3+c] + a[row*3+2]*b[2*3+c];
            if (row == 3) s += b[9 + c];
            r[row*3+c] = s;
        }
    copy(out, r);
}

// Inverse of the affine; false when the 3x3 is singular (|det| ~ 0).
inline bool invert(const f32 m[12], f32 out[12]) {
    const f32 a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const f32 c00 = e*i - f*h, c01 = f*g - d*i, c02 = d*h - e*g;
    const f32 det = a*c00 + b*c01 + c*c02;
    if (!(std::fabs(det) > 1e-20f)) return false;
    const f32 s = 1.0f / det;
    f32 inv[9] = {
        c00*s, (c*h - b*i)*s, (b*f - c*e)*s,
        c01*s, (a*i - c*g)*s, (c*d - a*f)*s,
        c02*s, (b*g - a*h)*s, (a*e - b*d)*s,
    };
    const f32 tx = m[9], ty = m[10], tz = m[11];
    for (int k = 0; k < 9; ++k) out[k] = inv[k];
    for (int cc = 0; cc < 3; ++cc) out[9 + cc] = -(tx*inv[0*3+cc] + ty*inv[1*3+cc] + tz*inv[2*3+cc]);
    return true;
}

inline void point(const f32 m[12], const f32 p[3], f32 out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = p[0]*m[0*3+c] + p[1]*m[1*3+c] + p[2]*m[2*3+c] + m[9+c];
}
inline void vector(const f32 m[12], const f32 v[3], f32 out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = v[0]*m[0*3+c] + v[1]*m[1*3+c] + v[2]*m[2*3+c];
}

// World AABB of the local box [lo,hi] under m.
inline void aabb(const f32 m[12], const f32 lo[3], const f32 hi[3], f32 outLo[3], f32 outHi[3]) {
    for (int c = 0; c < 3; ++c) {
        f32 mn = m[9+c], mx = m[9+c];
        for (int r = 0; r < 3; ++r) {
            const f32 a = m[r*3+c] * lo[r], b = m[r*3+c] * hi[r];
            mn += a < b ? a : b;
            mx += a < b ? b : a;
        }
        outLo[c] = mn;
        outHi[c] = mx;
    }
}

} // namespace xf

// ---------------------------------------------------------------- ids and small types
using InstanceId = u64;
inline constexpr InstanceId kNoInstance = ~0ull;
inline InstanceId makeInstanceId(u32 group, u32 row) { return (static_cast<u64>(group) << 32) | row; }
inline u32 instanceGroup(InstanceId id) { return static_cast<u32>(id >> 32); }
inline u32 instanceRow(InstanceId id) { return static_cast<u32>(id & 0xFFFFFFFFu); }

enum class SelectMode { Replace, Add, Toggle, Remove };

struct InstanceBounds {   // an asset's LOCAL (object-space) box, cm
    f32 lo[3] = {-50.0f, -50.0f, -50.0f};
    f32 hi[3] = { 50.0f,  50.0f,  50.0f};
};

struct CullParams {
    f32 viewProj[16] = {};
    f32 eye[3] = {0, 0, 0};
    // Pixel-size cull: an instance whose bounding-sphere diameter projects under minPixels is dropped.
    // pixelScale = viewportHeightPx / (2*tan(fovY/2)) (perspective); 0 disables the cull.
    f32 pixelScale = 0.0f;
    f32 minPixels = 0.0f;
    // 0 = unlimited. Over the cap, the NEAREST maxCount to `eye` are kept.
    u32 maxCount = 0;
};

struct CullResult {
    std::vector<std::vector<u32>> rows;   // per group, ascending row indices (alive + visible only)
    u32 total = 0;
    bool capped = false;                  // maxCount dropped some
};

struct PickHit {
    InstanceId id = kNoInstance;
    f32 t = 0.0f;   // ray parameter in the units of the (unnormalised) direction passed in
};

// Re-targets a view-proj to the sub-rectangle [x0,x1]x[y0,y1] of NDC (each -1..1, y up) so a marquee
// can reuse selectInFrustum / cull. Returns false for an empty rectangle.
bool subFrustumViewProj(const f32 vp[16], f32 x0, f32 y0, f32 x1, f32 y1, f32 out[16]);

class InstanceSetModel {
public:
    // ---- load / export --------------------------------------------------------------------------
    // Replaces everything (selection, undo, bounds are kept). False, model cleared, when !data.valid().
    bool load(const fmt::OcInstanceData& data, std::string* why = nullptr);
    void clear();
    // Groups contiguous in their original order, dead rows dropped, empty groups omitted, cells/runs
    // cleared (see the header comment).
    void exportTo(fmt::OcInstanceData& out) const;

    // ---- queries --------------------------------------------------------------------------------
    usize groupCount() const { return groups_.size(); }
    const std::string& groupAsset(u32 g) const { return groups_[g].asset; }
    u32 groupFlags(u32 g) const { return groups_[g].flags; }
    u32 groupAlive(u32 g) const { return groups_[g].aliveCount; }
    u32 groupRows(u32 g) const { return static_cast<u32>(groups_[g].alive.size()); }   // alive + dead
    u64 aliveCount() const;
    bool alive(InstanceId id) const;
    // The row's 12 floats (valid until the next edit/add); nullptr when the id does not exist.
    const f32* matrix(InstanceId id) const;
    // World AABB of one instance; false when dead/unknown.
    bool worldBounds(InstanceId id, f32 lo[3], f32 hi[3]) const;
    // Union of the selection's world AABBs; false when the selection is empty.
    bool selectionBounds(f32 lo[3], f32 hi[3]) const;

    // Local bounds per asset. Applies to existing and future groups of that asset; rebuilds the index.
    void setAssetBounds(const std::string& asset, const InstanceBounds& b);
    // Index cell edge in cm; 0 = pick from the data at the next (re)build.
    void setCellSize(f32 cellCm);

    // ---- selection (not part of undo) -----------------------------------------------------------
    const std::vector<InstanceId>& selection() const { return selection_; }   // sorted, unique, alive
    bool selected(InstanceId id) const;
    void selectNone() { selection_.clear(); }
    void select(const std::vector<InstanceId>& ids, SelectMode mode = SelectMode::Replace);
    void selectGroup(u32 group, SelectMode mode = SelectMode::Replace);
    // Instances whose WORLD AABB overlaps the box (conservative: AABB, not the exact shape).
    void selectInBox(const f32 lo[3], const f32 hi[3], SelectMode mode = SelectMode::Replace);
    // Instances whose world AABB intersects the frustum of `vp` (use subFrustumViewProj for marquee).
    void selectInFrustum(const f32 vp[16], SelectMode mode = SelectMode::Replace);

    // [wave3/level-instances] A baked level pack (Binaries/LevelPacks/*.ocinst) opens read-only so it can be inspected:
    // every edit below refuses. A pack is regenerated by Bake Pack, never hand-edited.
    void setReadOnly(bool on) { readOnly_ = on; }
    bool readOnly() const { return readOnly_; }

    // ---- edits (each one undo step unless inside beginEdit/endEdit) -----------------------------
    // Appends to the first group of `asset` (created with kOcInstanceFlagCastShadow if none).
    InstanceId add(const std::string& asset, const f32 m[12]);
    // Returns how many rows changed. Dead/unknown ids are skipped.
    usize deleteInstances(const std::vector<InstanceId>& ids);
    usize deleteSelected() { return deleteInstances(std::vector<InstanceId>(selection_)); }
    // Copies at +offset (world cm); the selection becomes the copies. Returns the new ids.
    std::vector<InstanceId> duplicate(const std::vector<InstanceId>& ids, const f32 offset[3]);
    std::vector<InstanceId> duplicateSelected(const f32 offset[3]) {
        return duplicate(std::vector<InstanceId>(selection_), offset);
    }
    bool setTransform(InstanceId id, const f32 m[12]);
    // new = base * delta (a world-space delta in the row-vector convention). Inside beginEdit() the
    // BASE is each row's matrix as it was when the edit began, so calling this every frame of a drag
    // with the drag's TOTAL delta is correct (no accumulation drift) and is one undo step.
    usize applyDelta(const std::vector<InstanceId>& ids, const f32 delta[12]);
    usize applyDeltaToSelected(const f32 delta[12]) { return applyDelta(std::vector<InstanceId>(selection_), delta); }

    // Coalesce several edits into one undo step (a gizmo drag). Nestable.
    void beginEdit();
    void endEdit();
    // Reverts everything since the outermost beginEdit and records nothing (drag cancelled).
    void cancelEdit();
    bool editing() const { return txDepth_ > 0; }

    // ---- undo / redo / dirty --------------------------------------------------------------------
    bool canUndo() const { return txDepth_ == 0 && pos_ > 0; }
    bool canRedo() const { return txDepth_ == 0 && pos_ < steps_.size(); }
    bool undo();
    bool redo();
    // True when the content differs from what markSaved() last saw (or from load); undoing back to
    // the saved point makes it clean again.
    bool dirty() const;
    void markSaved();
    usize undoDepth() const { return pos_; }

    // ---- spatial queries ------------------------------------------------------------------------
    void cull(const CullParams& p, CullResult& out) const;
    // Nearest instance along origin + t*dir, t in [0,maxDist]; exact against the local box.
    bool pick(const f32 origin[3], const f32 dir[3], f32 maxDist, PickHit& out) const;
    // Forces the lazy index to exist now (otherwise the first query pays for it).
    void buildIndex() const { ensureIndex(); }
    // Diagnostics for tests / HUD.
    usize indexCellCount() const { return cells_.size(); }
    usize indexRebuilds() const { return rebuilds_; }

private:
    struct Group {
        std::string asset;
        u32 flags = 0;
        std::vector<f32> xf;     // 12 per row
        std::vector<u8> alive;   // per row
        u32 aliveCount = 0;
        InstanceBounds local;
        mutable std::vector<u32> stamp;   // query de-dup
    };
    struct Change {
        InstanceId id = 0;
        f32 oldM[12] = {};
        f32 newM[12] = {};
        u8 oldAlive = 0, newAlive = 0;
    };
    struct Step { std::vector<Change> changes; };
    struct Cell {
        std::vector<InstanceId> items;
        f32 lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    };

    u32 findOrCreateGroup(const std::string& asset);
    InstanceBounds boundsFor(const std::string& asset) const;
    Group* groupOf(InstanceId id);
    const Group* groupOf(InstanceId id) const;
    // THE mutation primitive: sets matrix (if non-null) and alive (if >= 0), records the change in
    // the open edit, updates the index. `id`'s row must exist.
    void write(InstanceId id, const f32* newM, int newAlive);
    void revertTo(const Change& c, bool toOld);
    void commitPending();
    void trimHistory();
    void dropDeadFromSelection();
    void applySelection(std::vector<InstanceId> ids, SelectMode mode);

    // index
    void ensureIndex() const;
    void rebuildIndex() const;
    void indexInsert(InstanceId id) const;
    bool instanceWorldBounds(InstanceId id, f32 lo[3], f32 hi[3]) const;   // no alive check
    static u64 cellKey(i32 cx, i32 cy) {
        return (static_cast<u64>(static_cast<u32>(cx)) << 32) | static_cast<u32>(cy);
    }
    i32 cellCoord(f32 v) const { return static_cast<i32>(std::floor(v / cellCm_)); }
    void beginQuery() const;
    bool firstVisit(InstanceId id) const;   // stamp test-and-set
    template <class F> void visitBox(const f32 lo[3], const f32 hi[3], F&& f) const;
    template <class F> void visitFrustum(const f32 planes[24], F&& f) const;   // 6 planes x (a,b,c,d)

    std::vector<Group> groups_;
    InstanceBounds defaultBounds_;
    std::unordered_map<std::string, InstanceBounds> assetBounds_;

    std::vector<InstanceId> selection_;
    bool readOnly_ = false;   // [wave3/level-instances]

    // history: steps_[0..pos_) applied, [pos_..) redoable
    std::vector<Step> steps_;
    usize pos_ = 0;
    long long saved_ = 0;   // pos_ at the last save; -1 = unreachable
    usize totalChanges_ = 0;
    int txDepth_ = 0;
    Step pending_;
    std::unordered_map<InstanceId, u32> pendingIdx_;

    // index (lazy; mutable so const queries can build it)
    mutable bool indexValid_ = false;
    mutable f32 cellCm_ = 0.0f;
    f32 requestedCellCm_ = 0.0f;
    mutable std::unordered_map<u64, Cell> cells_;
    mutable std::vector<InstanceId> oversized_;
    mutable i32 minCx_ = 0, maxCx_ = -1, minCy_ = 0, maxCy_ = -1;   // lattice bounds (empty: max<min)
    mutable usize churn_ = 0;
    mutable usize rebuilds_ = 0;
    mutable u32 epoch_ = 0;
};

} // namespace aver::editor
