// InstanceSetModel -- see the header for the model, the id scheme and the index design.
#include "InstanceSetModel.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace aver::editor {

namespace {

constexpr usize kMaxUndoSteps   = 512;
constexpr usize kMaxUndoChanges = 4'000'000;   // ~450 MB worst case; the newest step is always kept
constexpr i64 kMaxCellSpan      = 64;          // cells one instance may occupy before it is "oversized"
constexpr usize kMinChurn       = 4096;
constexpr f32 kInf              = std::numeric_limits<f32>::infinity();

bool finite12(const f32* m) {
    for (int i = 0; i < 12; ++i) if (!std::isfinite(m[i])) return false;
    return true;
}

bool sameMatrix(const f32* a, const f32* b) {
    for (int i = 0; i < 12; ++i) if (a[i] != b[i]) return false;
    return true;
}

void frustumPlanes(const f32 vp[16], f32 out[24]) {
    // clip_j = x*vp[j] + y*vp[4+j] + z*vp[8+j] + vp[12+j]; plane = column combinations (D3D 0..1 depth).
    f32 c[4][4];
    for (int j = 0; j < 4; ++j) { c[j][0] = vp[j]; c[j][1] = vp[4+j]; c[j][2] = vp[8+j]; c[j][3] = vp[12+j]; }
    for (int k = 0; k < 4; ++k) {
        out[0*4+k] = c[3][k] + c[0][k];   // left
        out[1*4+k] = c[3][k] - c[0][k];   // right
        out[2*4+k] = c[3][k] + c[1][k];   // bottom
        out[3*4+k] = c[3][k] - c[1][k];   // top
        out[4*4+k] = c[2][k];             // near
        out[5*4+k] = c[3][k] - c[2][k];   // far
    }
}

bool aabbOutside(const f32 planes[24], const f32 lo[3], const f32 hi[3]) {
    for (int p = 0; p < 6; ++p) {
        const f32* q = planes + p * 4;
        const f32 x = q[0] >= 0 ? hi[0] : lo[0];
        const f32 y = q[1] >= 0 ? hi[1] : lo[1];
        const f32 z = q[2] >= 0 ? hi[2] : lo[2];
        if (q[0]*x + q[1]*y + q[2]*z + q[3] < 0.0f) return true;
    }
    return false;
}

bool boxesOverlap(const f32 alo[3], const f32 ahi[3], const f32 blo[3], const f32 bhi[3]) {
    for (int a = 0; a < 3; ++a) if (ahi[a] < blo[a] || alo[a] > bhi[a]) return false;
    return true;
}

// Slab test. tmin/tmax are the entry/exit parameters; true when the segment [0,maxT] meets the box.
bool rayBox(const f32 o[3], const f32 d[3], const f32 lo[3], const f32 hi[3], f32 maxT,
            f32& tmin, f32& tmax) {
    f32 t0 = -kInf, t1 = kInf;
    for (int a = 0; a < 3; ++a) {
        if (d[a] == 0.0f) {
            if (o[a] < lo[a] || o[a] > hi[a]) return false;
            continue;
        }
        f32 ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    tmin = t0;
    tmax = t1;
    return t1 >= 0.0f && std::max(t0, 0.0f) <= maxT;
}

void mergeSorted(std::vector<InstanceId>& cur, std::vector<InstanceId>& ids, SelectMode mode) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<InstanceId> out;
    switch (mode) {
    case SelectMode::Replace: cur = std::move(ids); return;
    case SelectMode::Add:
        std::set_union(cur.begin(), cur.end(), ids.begin(), ids.end(), std::back_inserter(out)); break;
    case SelectMode::Remove:
        std::set_difference(cur.begin(), cur.end(), ids.begin(), ids.end(), std::back_inserter(out)); break;
    case SelectMode::Toggle:
        std::set_symmetric_difference(cur.begin(), cur.end(), ids.begin(), ids.end(), std::back_inserter(out));
        break;
    }
    cur = std::move(out);
}

} // namespace

// ---------------------------------------------------------------- free helpers

bool subFrustumViewProj(const f32 vp[16], f32 x0, f32 y0, f32 x1, f32 y1, f32 out[16]) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    const f32 hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    if (!(hx > 1e-6f) || !(hy > 1e-6f)) return false;
    const f32 cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    // clip' = clip * S: x' = (x - cx*w)/hx, y' = (y - cy*w)/hy, z and w unchanged.
    f32 s[16] = {1/hx, 0, 0, 0,   0, 1/hy, 0, 0,   0, 0, 1, 0,   -cx/hx, -cy/hy, 0, 1};
    f32 r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            f32 v = 0;
            for (int k = 0; k < 4; ++k) v += vp[i*4+k] * s[k*4+j];
            r[i*4+j] = v;
        }
    for (int i = 0; i < 16; ++i) out[i] = r[i];
    return true;
}

// ---------------------------------------------------------------- load / export / small queries

InstanceBounds InstanceSetModel::boundsFor(const std::string& asset) const {
    const auto it = assetBounds_.find(asset);
    return it != assetBounds_.end() ? it->second : defaultBounds_;
}

void InstanceSetModel::clear() {
    groups_.clear();
    selection_.clear();
    steps_.clear();
    pos_ = 0;
    saved_ = 0;
    totalChanges_ = 0;
    txDepth_ = 0;
    pending_.changes.clear();
    pendingIdx_.clear();
    cells_.clear();
    oversized_.clear();
    indexValid_ = false;
    churn_ = 0;
}

bool InstanceSetModel::load(const fmt::OcInstanceData& data, std::string* why) {
    clear();
    if (!data.valid()) {
        if (why) *why = "instance table is not valid";
        return false;
    }
    groups_.reserve(data.groups.size());
    for (const fmt::OcInstanceGroup& src : data.groups) {
        Group g;
        g.asset = src.asset;
        g.flags = src.flags;
        g.local = boundsFor(src.asset);
        const f32* from = data.transforms.data() + static_cast<usize>(src.first) * 12;
        g.xf.assign(from, from + static_cast<usize>(src.count) * 12);
        g.alive.assign(src.count, 1);
        g.aliveCount = src.count;
        groups_.push_back(std::move(g));
    }
    return true;
}

void InstanceSetModel::exportTo(fmt::OcInstanceData& out) const {
    out = fmt::OcInstanceData{};
    u64 total = 0;
    for (const Group& g : groups_) total += g.aliveCount;
    out.transforms.reserve(static_cast<usize>(total) * 12);
    u32 first = 0;
    for (const Group& g : groups_) {
        if (g.aliveCount == 0) continue;
        for (usize r = 0; r < g.alive.size(); ++r)
            if (g.alive[r]) out.transforms.insert(out.transforms.end(), g.xf.begin() + r * 12, g.xf.begin() + r * 12 + 12);
        out.groups.push_back({g.asset, g.flags, first, g.aliveCount});
        first += g.aliveCount;
    }
}

u64 InstanceSetModel::aliveCount() const {
    u64 n = 0;
    for (const Group& g : groups_) n += g.aliveCount;
    return n;
}

InstanceSetModel::Group* InstanceSetModel::groupOf(InstanceId id) {
    const u32 g = instanceGroup(id);
    return g < groups_.size() && instanceRow(id) < groups_[g].alive.size() ? &groups_[g] : nullptr;
}
const InstanceSetModel::Group* InstanceSetModel::groupOf(InstanceId id) const {
    const u32 g = instanceGroup(id);
    return g < groups_.size() && instanceRow(id) < groups_[g].alive.size() ? &groups_[g] : nullptr;
}

bool InstanceSetModel::alive(InstanceId id) const {
    const Group* g = groupOf(id);
    return g && g->alive[instanceRow(id)];
}

const f32* InstanceSetModel::matrix(InstanceId id) const {
    const Group* g = groupOf(id);
    return g ? &g->xf[static_cast<usize>(instanceRow(id)) * 12] : nullptr;
}

bool InstanceSetModel::instanceWorldBounds(InstanceId id, f32 lo[3], f32 hi[3]) const {
    const Group* g = groupOf(id);
    if (!g) return false;
    xf::aabb(&g->xf[static_cast<usize>(instanceRow(id)) * 12], g->local.lo, g->local.hi, lo, hi);
    return true;
}

bool InstanceSetModel::worldBounds(InstanceId id, f32 lo[3], f32 hi[3]) const {
    return alive(id) && instanceWorldBounds(id, lo, hi);
}

bool InstanceSetModel::selectionBounds(f32 lo[3], f32 hi[3]) const {
    bool any = false;
    for (const InstanceId id : selection_) {
        f32 l[3], h[3];
        if (!worldBounds(id, l, h)) continue;
        for (int a = 0; a < 3; ++a) {
            lo[a] = any ? std::min(lo[a], l[a]) : l[a];
            hi[a] = any ? std::max(hi[a], h[a]) : h[a];
        }
        any = true;
    }
    return any;
}

void InstanceSetModel::setAssetBounds(const std::string& asset, const InstanceBounds& b) {
    assetBounds_[asset] = b;
    for (Group& g : groups_) if (g.asset == asset) g.local = b;
    indexValid_ = false;
}

void InstanceSetModel::setCellSize(f32 cellCm) {
    requestedCellCm_ = cellCm > 0.0f ? cellCm : 0.0f;
    indexValid_ = false;
}

// ---------------------------------------------------------------- selection

bool InstanceSetModel::selected(InstanceId id) const {
    return std::binary_search(selection_.begin(), selection_.end(), id);
}

void InstanceSetModel::applySelection(std::vector<InstanceId> ids, SelectMode mode) {
    mergeSorted(selection_, ids, mode);
}

void InstanceSetModel::select(const std::vector<InstanceId>& ids, SelectMode mode) {
    std::vector<InstanceId> keep;
    keep.reserve(ids.size());
    for (const InstanceId id : ids) if (alive(id)) keep.push_back(id);
    applySelection(std::move(keep), mode);
}

void InstanceSetModel::selectGroup(u32 group, SelectMode mode) {
    std::vector<InstanceId> ids;
    if (group < groups_.size()) {
        const Group& g = groups_[group];
        ids.reserve(g.aliveCount);
        for (u32 r = 0; r < g.alive.size(); ++r) if (g.alive[r]) ids.push_back(makeInstanceId(group, r));
    }
    applySelection(std::move(ids), mode);
}

void InstanceSetModel::selectInBox(const f32 lo[3], const f32 hi[3], SelectMode mode) {
    ensureIndex();
    std::vector<InstanceId> ids;
    visitBox(lo, hi, [&](InstanceId id, const f32*, const f32*) { ids.push_back(id); });
    applySelection(std::move(ids), mode);
}

void InstanceSetModel::selectInFrustum(const f32 vp[16], SelectMode mode) {
    ensureIndex();
    f32 pl[24];
    frustumPlanes(vp, pl);
    std::vector<InstanceId> ids;
    visitFrustum(pl, [&](InstanceId id, const f32*, const f32*) { ids.push_back(id); });
    applySelection(std::move(ids), mode);
}

void InstanceSetModel::dropDeadFromSelection() {
    selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
                                    [&](InstanceId id) { return !alive(id); }), selection_.end());
}

// ---------------------------------------------------------------- the mutation primitive

void InstanceSetModel::revertTo(const Change& c, bool toOld) {
    Group* g = groupOf(c.id);
    if (!g) return;
    const u32 row = instanceRow(c.id);
    f32* cur = &g->xf[static_cast<usize>(row) * 12];
    const f32* m = toOld ? c.oldM : c.newM;
    const u8 al = toOld ? c.oldAlive : c.newAlive;
    xf::copy(cur, m);
    if (g->alive[row] != al) {
        g->alive[row] = al;
        g->aliveCount += al ? 1u : static_cast<u32>(-1);
    }
    if (indexValid_) {
        ++churn_;
        if (al) indexInsert(c.id);
    }
}

void InstanceSetModel::write(InstanceId id, const f32* newM, int newAlive) {
    Group* g = groupOf(id);
    if (!g) return;
    const u32 row = instanceRow(id);
    f32* cur = &g->xf[static_cast<usize>(row) * 12];

    auto it = pendingIdx_.find(id);
    if (it == pendingIdx_.end()) {
        Change c;
        c.id = id;
        xf::copy(c.oldM, cur);
        c.oldAlive = g->alive[row];
        pendingIdx_[id] = static_cast<u32>(pending_.changes.size());
        pending_.changes.push_back(c);
        it = pendingIdx_.find(id);
    }
    Change& c = pending_.changes[it->second];

    const bool wasAlive = g->alive[row] != 0;
    const bool moved = newM && !sameMatrix(cur, newM);
    if (newM) xf::copy(cur, newM);
    if (newAlive >= 0 && (g->alive[row] != 0) != (newAlive != 0)) {
        g->alive[row] = static_cast<u8>(newAlive);
        g->aliveCount += newAlive ? 1u : static_cast<u32>(-1);
    }
    xf::copy(c.newM, cur);
    c.newAlive = g->alive[row];

    if (indexValid_ && (moved || (g->alive[row] != 0) != wasAlive)) {
        ++churn_;
        if (g->alive[row]) indexInsert(id);
    }
}

void InstanceSetModel::beginEdit() { ++txDepth_; }

void InstanceSetModel::endEdit() {
    if (txDepth_ == 0) return;
    if (--txDepth_ == 0) commitPending();
}

void InstanceSetModel::cancelEdit() {
    if (txDepth_ == 0) return;
    for (usize i = pending_.changes.size(); i-- > 0;) revertTo(pending_.changes[i], true);
    pending_.changes.clear();
    pendingIdx_.clear();
    txDepth_ = 0;
    dropDeadFromSelection();
}

void InstanceSetModel::commitPending() {
    // Drop no-ops (a drag that came back to where it started, a delete+undo inside one edit).
    auto& ch = pending_.changes;
    ch.erase(std::remove_if(ch.begin(), ch.end(), [](const Change& c) {
        return c.oldAlive == c.newAlive && sameMatrix(c.oldM, c.newM);
    }), ch.end());
    pendingIdx_.clear();
    if (ch.empty()) return;

    if (pos_ < steps_.size()) {   // a new edit discards the redo branch
        for (usize i = pos_; i < steps_.size(); ++i) totalChanges_ -= steps_[i].changes.size();
        steps_.resize(pos_);
        if (saved_ > static_cast<long long>(pos_)) saved_ = -1;
    }
    totalChanges_ += ch.size();
    steps_.push_back(std::move(pending_));
    pending_ = Step{};
    ++pos_;
    trimHistory();
}

void InstanceSetModel::trimHistory() {
    while (steps_.size() > 1 && (steps_.size() > kMaxUndoSteps || totalChanges_ > kMaxUndoChanges)) {
        totalChanges_ -= steps_.front().changes.size();
        steps_.erase(steps_.begin());
        --pos_;
        if (saved_ >= 0) --saved_;   // -1 afterwards = the saved state fell off the history
    }
}

bool InstanceSetModel::undo() {
    if (!canUndo()) return false;
    const Step& s = steps_[pos_ - 1];
    for (usize i = s.changes.size(); i-- > 0;) revertTo(s.changes[i], true);
    --pos_;
    dropDeadFromSelection();
    return true;
}

bool InstanceSetModel::redo() {
    if (!canRedo()) return false;
    const Step& s = steps_[pos_];
    for (const Change& c : s.changes) revertTo(c, false);
    ++pos_;
    dropDeadFromSelection();
    return true;
}

bool InstanceSetModel::dirty() const {
    return !pending_.changes.empty() || saved_ < 0 || static_cast<usize>(saved_) != pos_;
}

void InstanceSetModel::markSaved() {
    if (txDepth_ == 0) saved_ = static_cast<long long>(pos_);
}

// ---------------------------------------------------------------- edits

u32 InstanceSetModel::findOrCreateGroup(const std::string& asset) {
    for (u32 i = 0; i < groups_.size(); ++i) if (groups_[i].asset == asset) return i;
    Group g;
    g.asset = asset;
    g.flags = fmt::kOcInstanceFlagCastShadow;
    g.local = boundsFor(asset);
    groups_.push_back(std::move(g));
    return static_cast<u32>(groups_.size() - 1);
}

InstanceId InstanceSetModel::add(const std::string& asset, const f32 m[12]) {
    if (readOnly_ || asset.empty() || !finite12(m)) return kNoInstance;
    beginEdit();
    const u32 gi = findOrCreateGroup(asset);
    Group& g = groups_[gi];
    const u32 row = static_cast<u32>(g.alive.size());
    g.xf.insert(g.xf.end(), 12, 0.0f);
    g.alive.push_back(0);
    const InstanceId id = makeInstanceId(gi, row);
    write(id, m, 1);
    endEdit();
    return id;
}

usize InstanceSetModel::deleteInstances(const std::vector<InstanceId>& ids) {
    if (readOnly_) return 0;
    usize n = 0;
    beginEdit();
    for (const InstanceId id : ids)
        if (alive(id)) { write(id, nullptr, 0); ++n; }
    endEdit();
    dropDeadFromSelection();
    return n;
}

std::vector<InstanceId> InstanceSetModel::duplicate(const std::vector<InstanceId>& ids, const f32 offset[3]) {
    std::vector<InstanceId> out;
    if (readOnly_) return out;
    out.reserve(ids.size());
    beginEdit();
    for (const InstanceId id : ids) {
        if (!alive(id)) continue;
        f32 m[12];
        xf::copy(m, matrix(id));   // copied first: the append below can reallocate the group's storage
        for (int a = 0; a < 3; ++a) m[9 + a] += offset[a];
        if (!finite12(m)) continue;
        Group& g = groups_[instanceGroup(id)];
        const u32 row = static_cast<u32>(g.alive.size());
        g.xf.insert(g.xf.end(), 12, 0.0f);
        g.alive.push_back(0);
        const InstanceId nid = makeInstanceId(instanceGroup(id), row);
        write(nid, m, 1);
        out.push_back(nid);
    }
    endEdit();
    std::vector<InstanceId> sel(out);
    applySelection(std::move(sel), SelectMode::Replace);
    return out;
}

bool InstanceSetModel::setTransform(InstanceId id, const f32 m[12]) {
    if (readOnly_ || !alive(id) || !finite12(m)) return false;
    beginEdit();
    write(id, m, -1);
    endEdit();
    return true;
}

usize InstanceSetModel::applyDelta(const std::vector<InstanceId>& ids, const f32 delta[12]) {
    if (readOnly_ || !finite12(delta)) return 0;
    usize n = 0;
    beginEdit();
    for (const InstanceId id : ids) {
        if (!alive(id)) continue;
        // Inside a drag the base is the row as it was when the edit began, so a TOTAL delta applied
        // every frame does not compound.
        const f32* base = matrix(id);
        const auto it = pendingIdx_.find(id);
        if (it != pendingIdx_.end() && pending_.changes[it->second].oldAlive)
            base = pending_.changes[it->second].oldM;
        f32 out[12];
        xf::mul(base, delta, out);
        write(id, out, -1);
        ++n;
    }
    endEdit();
    return n;
}

// ---------------------------------------------------------------- spatial index

void InstanceSetModel::ensureIndex() const {
    if (!indexValid_ || churn_ > std::max<usize>(kMinChurn, static_cast<usize>(aliveCount() / 4))) rebuildIndex();
}

void InstanceSetModel::rebuildIndex() const {
    cells_.clear();
    oversized_.clear();
    minCx_ = minCy_ = 0;
    maxCx_ = maxCy_ = -1;
    churn_ = 0;
    ++rebuilds_;

    if (requestedCellCm_ > 0.0f) {
        cellCm_ = requestedCellCm_;
    } else {
        // ~64 instances per cell over the XY footprint, clamped; the cell count is capped too so a
        // sparse huge level cannot make millions of empty-neighbour cells.
        u64 n = 0;
        f32 lox = kInf, loy = kInf, hix = -kInf, hiy = -kInf;
        for (const Group& g : groups_)
            for (usize r = 0; r < g.alive.size(); ++r) {
                if (!g.alive[r]) continue;
                ++n;
                const f32 x = g.xf[r*12 + 9], y = g.xf[r*12 + 10];
                lox = std::min(lox, x); hix = std::max(hix, x);
                loy = std::min(loy, y); hiy = std::max(hiy, y);
            }
        f32 cell = 2500.0f;
        if (n > 0) {
            const f32 w = std::max(hix - lox, 1.0f), h = std::max(hiy - loy, 1.0f);
            cell = std::sqrt(w * h * 64.0f / static_cast<f32>(n));
            cell = std::max(cell, std::sqrt(w * h / 4.0e6f));
        }
        cellCm_ = std::clamp(cell, 100.0f, 1.0e6f);
    }

    for (u32 gi = 0; gi < groups_.size(); ++gi) {
        const Group& g = groups_[gi];
        for (u32 r = 0; r < g.alive.size(); ++r)
            if (g.alive[r]) indexInsert(makeInstanceId(gi, r));
    }
    indexValid_ = true;
}

void InstanceSetModel::indexInsert(InstanceId id) const {
    f32 lo[3], hi[3];
    if (!instanceWorldBounds(id, lo, hi)) return;
    for (int a = 0; a < 3; ++a) if (!std::isfinite(lo[a]) || !std::isfinite(hi[a])) return;
    // Clamp the coordinates so a wild matrix cannot overflow the lattice arithmetic.
    constexpr f32 kLim = 1.0e9f;
    const i32 cx0 = cellCoord(std::clamp(lo[0], -kLim, kLim)), cx1 = cellCoord(std::clamp(hi[0], -kLim, kLim));
    const i32 cy0 = cellCoord(std::clamp(lo[1], -kLim, kLim)), cy1 = cellCoord(std::clamp(hi[1], -kLim, kLim));
    const i64 span = static_cast<i64>(cx1 - cx0 + 1) * static_cast<i64>(cy1 - cy0 + 1);
    if (span > kMaxCellSpan) { oversized_.push_back(id); return; }

    for (i32 cy = cy0; cy <= cy1; ++cy)
        for (i32 cx = cx0; cx <= cx1; ++cx) {
            Cell& c = cells_[cellKey(cx, cy)];
            if (c.items.empty()) {
                for (int a = 0; a < 3; ++a) { c.lo[a] = lo[a]; c.hi[a] = hi[a]; }
            } else {
                for (int a = 0; a < 3; ++a) { c.lo[a] = std::min(c.lo[a], lo[a]); c.hi[a] = std::max(c.hi[a], hi[a]); }
            }
            c.items.push_back(id);
        }
    if (maxCx_ < minCx_) { minCx_ = cx0; maxCx_ = cx1; minCy_ = cy0; maxCy_ = cy1; }
    else {
        minCx_ = std::min(minCx_, cx0); maxCx_ = std::max(maxCx_, cx1);
        minCy_ = std::min(minCy_, cy0); maxCy_ = std::max(maxCy_, cy1);
    }
}

void InstanceSetModel::beginQuery() const {
    if (++epoch_ == 0) {
        for (const Group& g : groups_) std::fill(g.stamp.begin(), g.stamp.end(), 0u);
        epoch_ = 1;
    }
}

bool InstanceSetModel::firstVisit(InstanceId id) const {
    const Group& g = groups_[instanceGroup(id)];
    if (g.stamp.size() < g.alive.size()) g.stamp.resize(g.alive.size(), 0u);
    u32& s = g.stamp[instanceRow(id)];
    if (s == epoch_) return false;
    s = epoch_;
    return true;
}

template <class F>
void InstanceSetModel::visitBox(const f32 lo[3], const f32 hi[3], F&& f) const {
    beginQuery();
    auto test = [&](InstanceId id) {
        if (!firstVisit(id) || !alive(id)) return;
        f32 l[3], h[3];
        instanceWorldBounds(id, l, h);
        if (boxesOverlap(l, h, lo, hi)) f(id, l, h);
    };
    for (const InstanceId id : oversized_) test(id);
    if (maxCx_ < minCx_) return;

    constexpr f32 kLim = 1.0e9f;
    const i32 cx0 = std::max(cellCoord(std::clamp(lo[0], -kLim, kLim)), minCx_);
    const i32 cx1 = std::min(cellCoord(std::clamp(hi[0], -kLim, kLim)), maxCx_);
    const i32 cy0 = std::max(cellCoord(std::clamp(lo[1], -kLim, kLim)), minCy_);
    const i32 cy1 = std::min(cellCoord(std::clamp(hi[1], -kLim, kLim)), maxCy_);
    if (cx1 < cx0 || cy1 < cy0) return;
    const i64 n = static_cast<i64>(cx1 - cx0 + 1) * static_cast<i64>(cy1 - cy0 + 1);
    if (n <= static_cast<i64>(cells_.size())) {
        for (i32 cy = cy0; cy <= cy1; ++cy)
            for (i32 cx = cx0; cx <= cx1; ++cx) {
                const auto it = cells_.find(cellKey(cx, cy));
                if (it == cells_.end() || !boxesOverlap(it->second.lo, it->second.hi, lo, hi)) continue;
                for (const InstanceId id : it->second.items) test(id);
            }
    } else {
        for (const auto& kv : cells_) {
            if (!boxesOverlap(kv.second.lo, kv.second.hi, lo, hi)) continue;
            for (const InstanceId id : kv.second.items) test(id);
        }
    }
}

template <class F>
void InstanceSetModel::visitFrustum(const f32 planes[24], F&& f) const {
    beginQuery();
    auto test = [&](InstanceId id) {
        if (!firstVisit(id) || !alive(id)) return;
        f32 l[3], h[3];
        instanceWorldBounds(id, l, h);
        if (!aabbOutside(planes, l, h)) f(id, l, h);
    };
    for (const InstanceId id : oversized_) test(id);
    for (const auto& kv : cells_) {
        if (aabbOutside(planes, kv.second.lo, kv.second.hi)) continue;
        for (const InstanceId id : kv.second.items) test(id);
    }
}

void InstanceSetModel::cull(const CullParams& p, CullResult& out) const {
    ensureIndex();
    out.rows.assign(groups_.size(), {});
    out.total = 0;
    out.capped = false;

    f32 pl[24];
    frustumPlanes(p.viewProj, pl);
    struct Cand { InstanceId id; f32 d2; };
    std::vector<Cand> cands;
    const bool sizeCull = p.pixelScale > 0.0f && p.minPixels > 0.0f;
    visitFrustum(pl, [&](InstanceId id, const f32* lo, const f32* hi) {
        const f32 c[3] = {(lo[0]+hi[0])*0.5f, (lo[1]+hi[1])*0.5f, (lo[2]+hi[2])*0.5f};
        const f32 dx = c[0]-p.eye[0], dy = c[1]-p.eye[1], dz = c[2]-p.eye[2];
        const f32 d2 = dx*dx + dy*dy + dz*dz;
        if (sizeCull) {
            const f32 hx = (hi[0]-lo[0])*0.5f, hy = (hi[1]-lo[1])*0.5f, hz = (hi[2]-lo[2])*0.5f;
            const f32 r = std::sqrt(hx*hx + hy*hy + hz*hz);
            const f32 dist = std::sqrt(d2);
            // Inside the bounding sphere the eye is "on" the object: never size-cull that.
            if (dist > r && 2.0f * r * p.pixelScale / dist < p.minPixels) return;
        }
        cands.push_back({id, d2});
    });

    if (p.maxCount > 0 && cands.size() > p.maxCount) {
        std::nth_element(cands.begin(), cands.begin() + p.maxCount, cands.end(),
                         [](const Cand& a, const Cand& b) { return a.d2 < b.d2; });
        cands.resize(p.maxCount);
        out.capped = true;
    }
    for (const Cand& c : cands) out.rows[instanceGroup(c.id)].push_back(instanceRow(c.id));
    for (auto& v : out.rows) std::sort(v.begin(), v.end());
    out.total = static_cast<u32>(cands.size());
}

bool InstanceSetModel::pick(const f32 o[3], const f32 d[3], f32 maxDist, PickHit& out) const {
    out = PickHit{};
    if (!(maxDist > 0.0f)) return false;
    ensureIndex();
    beginQuery();
    f32 best = maxDist;
    InstanceId bestId = kNoInstance;

    // Exact test: ray into the instance's own space (v * M^-1), slab against the group's local box.
    auto test = [&](InstanceId id) {
        if (!firstVisit(id) || !alive(id)) return;
        f32 wl[3], wh[3], t0, t1;
        instanceWorldBounds(id, wl, wh);
        if (!rayBox(o, d, wl, wh, best, t0, t1)) return;
        const Group& g = groups_[instanceGroup(id)];
        f32 inv[12];
        if (!xf::invert(&g.xf[static_cast<usize>(instanceRow(id)) * 12], inv)) return;
        f32 lo_[3], ld[3];
        xf::point(inv, o, lo_);
        xf::vector(inv, d, ld);   // not normalised, so t stays the world parameter
        f32 a, b;
        if (!rayBox(lo_, ld, g.local.lo, g.local.hi, best, a, b)) return;
        const f32 t = std::max(a, 0.0f);
        if (t <= best) { best = t; bestId = id; }
    };
    for (const InstanceId id : oversized_) test(id);

    if (maxCx_ >= minCx_) {
        const f32 C = cellCm_;
        const f32 rx0 = static_cast<f32>(minCx_) * C, rx1 = static_cast<f32>(maxCx_ + 1) * C;
        const f32 ry0 = static_cast<f32>(minCy_) * C, ry1 = static_cast<f32>(maxCy_ + 1) * C;
        // Clip the ray to the occupied lattice rectangle so empty space is not walked.
        f32 t0 = 0.0f, t1 = maxDist;
        bool ok = true;
        const f32 lo2[2] = {rx0, ry0}, hi2[2] = {rx1, ry1};
        for (int a = 0; a < 2 && ok; ++a) {
            if (d[a] == 0.0f) { ok = o[a] >= lo2[a] && o[a] <= hi2[a]; continue; }
            f32 ta = (lo2[a] - o[a]) / d[a], tb = (hi2[a] - o[a]) / d[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            ok = t0 <= t1;
        }
        if (ok) {
            i32 ix = std::clamp(cellCoord(o[0] + d[0] * t0), minCx_, maxCx_);
            i32 iy = std::clamp(cellCoord(o[1] + d[1] * t0), minCy_, maxCy_);
            const i32 sx = d[0] > 0 ? 1 : -1, sy = d[1] > 0 ? 1 : -1;
            auto tNextOf = [&](f32 oo, f32 dd, i32 i) {
                if (dd == 0.0f) return kInf;
                return ((dd > 0 ? static_cast<f32>(i + 1) : static_cast<f32>(i)) * C - oo) / dd;
            };
            f32 tMaxX = tNextOf(o[0], d[0], ix), tMaxY = tNextOf(o[1], d[1], iy);
            const f32 tDx = d[0] == 0.0f ? kInf : C / std::fabs(d[0]);
            const f32 tDy = d[1] == 0.0f ? kInf : C / std::fabs(d[1]);
            const i64 limit = static_cast<i64>(maxCx_ - minCx_) + (maxCy_ - minCy_) + 4;
            for (i64 iter = 0; iter < limit; ++iter) {
                const auto it = cells_.find(cellKey(ix, iy));
                if (it != cells_.end()) {
                    f32 a, b;
                    if (rayBox(o, d, it->second.lo, it->second.hi, best, a, b))
                        for (const InstanceId id : it->second.items) test(id);
                }
                const f32 tNext = std::min(tMaxX, tMaxY);
                // Any hit nearer than where the ray leaves this cell lies in a cell already visited
                // (an instance is listed in every cell its box overlaps), so nothing later can beat it.
                if (bestId != kNoInstance && best <= tNext) break;
                if (tNext > t1) break;
                if (tMaxX < tMaxY) { ix += sx; tMaxX += tDx; if (ix < minCx_ || ix > maxCx_) break; }
                else               { iy += sy; tMaxY += tDy; if (iy < minCy_ || iy > maxCy_) break; }
            }
        }
    }
    if (bestId == kNoInstance) return false;
    out.id = bestId;
    out.t = best;
    return true;
}

} // namespace aver::editor
