// Nearest-neighbour search over a PoseDatabase: an SSE2 linear scan and an exact KD tree over the
// 8-D PCA projection of the searchable rows. See PoseDatabase.hpp.
//
// BOTH ARE EXACT and return the same row. The projection has orthonormal rows, so |P(q - r)| <= |q - r|
// and the 8-D box distance is a true lower bound of the full-dimension squared distance. The two
// searches share one distance routine and one tie rule (the lower row index), so the equality holds
// bit for bit, not just up to rounding.
#include "aver/anim/PoseDatabase.hpp"

#if defined(_M_X64) || defined(__SSE2__)
#  include <emmintrin.h>
#  define AVER_MM_SSE2 1
#else
#  define AVER_MM_SSE2 0
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace aver::anim {

namespace {

constexpr u32 kLeafRows = 16;

bool rowPasses(const RowMeta& m, const SearchFilter& f) {
    if (m.flags & (kRowNoSearch | kRowTail)) return false;
    return (m.tagMask & f.require) == f.require && (m.tagMask & f.exclude) == 0;
}

#if AVER_MM_SSE2
inline f32 hsum(__m128 v) {
    v = _mm_add_ps(v, _mm_movehl_ps(v, v));
    v = _mm_add_ss(v, _mm_shuffle_ps(v, v, 1));
    return _mm_cvtss_f32(v);
}
#endif

// Prune slack: the 8-D bound is exact in real arithmetic; f32 rounding gets a hair of room so an
// exact tie in a neighbouring leaf is never lost.
inline f32 pruneAt(f32 best) { return best + 1e-5f * std::fabs(best) + 1e-6f; }

} // namespace

f32* PoseDatabase::allocAligned(usize floats) {
    if (floats == 0) floats = 8;
    return static_cast<f32*>(::operator new(floats * sizeof(f32), std::align_val_t(32)));
}

void PoseDatabase::AlignedFree::operator()(f32* p) const {
    if (p) ::operator delete(p, std::align_val_t(32));
}

// Squared distance to a row, or +inf once the running sum passes `limit` (checked every 16 floats).
f32 PoseDatabase::distance(const f32* q, u32 r, f32 limit, bool scalarOnly) const {
    const f32* a = row(r);
    const u32 n = dimPadded_;
#if AVER_MM_SSE2
    if (!scalarOnly) {
        __m128 acc0 = _mm_setzero_ps(), acc1 = _mm_setzero_ps();
        u32 j = 0;
        for (; j + 16 <= n; j += 16) {
            const __m128 d0 = _mm_sub_ps(_mm_loadu_ps(q + j), _mm_load_ps(a + j));
            const __m128 d1 = _mm_sub_ps(_mm_loadu_ps(q + j + 4), _mm_load_ps(a + j + 4));
            const __m128 d2 = _mm_sub_ps(_mm_loadu_ps(q + j + 8), _mm_load_ps(a + j + 8));
            const __m128 d3 = _mm_sub_ps(_mm_loadu_ps(q + j + 12), _mm_load_ps(a + j + 12));
            acc0 = _mm_add_ps(acc0, _mm_add_ps(_mm_mul_ps(d0, d0), _mm_mul_ps(d2, d2)));
            acc1 = _mm_add_ps(acc1, _mm_add_ps(_mm_mul_ps(d1, d1), _mm_mul_ps(d3, d3)));
            if (j + 16 < n && hsum(_mm_add_ps(acc0, acc1)) > limit) return std::numeric_limits<f32>::infinity();
        }
        for (; j < n; j += 8) {   // n is a multiple of 8
            const __m128 d0 = _mm_sub_ps(_mm_loadu_ps(q + j), _mm_load_ps(a + j));
            const __m128 d1 = _mm_sub_ps(_mm_loadu_ps(q + j + 4), _mm_load_ps(a + j + 4));
            acc0 = _mm_add_ps(acc0, _mm_mul_ps(d0, d0));
            acc1 = _mm_add_ps(acc1, _mm_mul_ps(d1, d1));
        }
        const f32 s = hsum(_mm_add_ps(acc0, acc1));
        return s > limit ? std::numeric_limits<f32>::infinity() : s;
    }
#endif
    f32 s = 0.0f;
    for (u32 j = 0; j < n; ++j) {
        const f32 d = q[j] - a[j];
        s += d * d;
        if ((j & 15u) == 15u && j + 1 < n && s > limit) return std::numeric_limits<f32>::infinity();
    }
    return s > limit ? std::numeric_limits<f32>::infinity() : s;
}

SearchHit PoseDatabase::searchLinear(const f32* q, const SearchFilter& f, bool scalarOnly) const {
    SearchHit best;
    for (u32 r = 0; r < rows_; ++r) {
        const RowMeta& m = meta_[r];
        if (!rowPasses(m, f)) continue;
        const f32 bias = rangeBias_[m.range];
        const f32 limit = best.row == kNoRow ? std::numeric_limits<f32>::infinity() : best.cost - bias;
        const f32 c = distance(q, r, limit, scalarOnly) + bias;
        if (c < best.cost) best = {r, c};
    }
    return best;
}

SearchHit PoseDatabase::searchKd(const f32* q, const SearchFilter& f) const {
    if (kdNodes_.empty()) return searchLinear(q, f);

    f32 pq[kKdDims];
    for (u32 k = 0; k < kKdDims; ++k) {
        const f32* p = pca_.data() + static_cast<usize>(k) * dimPadded_;
        f32 s = 0.0f;
        for (u32 j = 0; j < dimPadded_; ++j) s += p[j] * q[j];
        pq[k] = s;
    }
    auto bound = [&](const KdNode& nd) {
        f32 s = 0.0f;
        for (u32 k = 0; k < kKdDims; ++k) {
            const f32 c = std::min(std::max(pq[k], nd.lo[k]), nd.hi[k]);
            const f32 d = pq[k] - c;
            s += d * d;
        }
        return s + minBias_;
    };
    auto usable = [&](const KdNode& nd) { return nd.flagsAny != 0 && (nd.tagOr & f.require) == f.require; };

    struct Item { f32 bound; u32 node; };
    auto later = [](const Item& a, const Item& b) { return a.bound > b.bound; };   // min-heap
    std::vector<Item> heap;
    SearchHit best;
    if (usable(kdNodes_[0])) heap.push_back({bound(kdNodes_[0]), 0});
    while (!heap.empty()) {
        std::pop_heap(heap.begin(), heap.end(), later);
        const Item it = heap.back();
        heap.pop_back();
        if (best.row != kNoRow && it.bound > pruneAt(best.cost)) break;   // everything left is farther
        const KdNode& nd = kdNodes_[it.node];
        if (nd.left == 0 && nd.right == 0) {
            for (u32 i = nd.rowBegin; i < nd.rowEnd; ++i) {
                const u32 r = kdOrder_[i];
                const RowMeta& m = meta_[r];
                if (!rowPasses(m, f)) continue;
                const f32 bias = rangeBias_[m.range];
                const f32 limit = best.row == kNoRow ? std::numeric_limits<f32>::infinity() : best.cost - bias;
                const f32 c = distance(q, r, limit, false) + bias;
                if (c < best.cost || (c == best.cost && best.row != kNoRow && r < best.row)) best = {r, c};
            }
            continue;
        }
        for (const u32 child : {nd.left, nd.right}) {
            const KdNode& cn = kdNodes_[child];
            if (!usable(cn)) continue;
            const f32 b = bound(cn);
            if (best.row != kNoRow && b > pruneAt(best.cost)) continue;
            heap.push_back({b, child});
            std::push_heap(heap.begin(), heap.end(), later);
        }
    }
    return best;
}

SearchHit PoseDatabase::search(const f32* q, const SearchFilter& f, SearchAccel accel) const {
    if (accel == SearchAccel::Linear || !hasKd()) return searchLinear(q, f);
    return searchKd(q, f);
}

void PoseDatabase::searchTopK(const f32* q, const SearchFilter& f, u32 k, std::vector<SearchHit>& out) const {
    out.clear();
    if (k == 0) return;
    for (u32 r = 0; r < rows_; ++r) {
        const RowMeta& m = meta_[r];
        if (!rowPasses(m, f)) continue;
        const f32 bias = rangeBias_[m.range];
        const f32 limit = out.size() < k ? std::numeric_limits<f32>::infinity() : out.back().cost - bias;
        const f32 c = distance(q, r, limit, false) + bias;
        if (!(c < std::numeric_limits<f32>::infinity())) continue;
        if (out.size() == k && !(c < out.back().cost)) continue;
        const auto at = std::upper_bound(out.begin(), out.end(), c, [](f32 v, const SearchHit& h) { return v < h.cost; });
        out.insert(at, SearchHit{r, c});
        if (out.size() > k) out.pop_back();
    }
}

// ---- KD tree build ---------------------------------------------------------------------------------

namespace {

// Gram-Schmidt on `v` against rows [0, k) of `basis` (n floats each); a degenerate residual is
// replaced by the first coordinate axis that still has weight outside the span.
void orthonormalise(std::vector<double>& basis, u32 k, u32 n) {
    double* v = basis.data() + static_cast<usize>(k) * n;
    auto removeSpan = [&](double* x) {
        for (u32 pass = 0; pass < 2; ++pass)   // twice for numerical hygiene
            for (u32 i = 0; i < k; ++i) {
                const double* b = basis.data() + static_cast<usize>(i) * n;
                double d = 0;
                for (u32 j = 0; j < n; ++j) d += x[j] * b[j];
                for (u32 j = 0; j < n; ++j) x[j] -= d * b[j];
            }
    };
    removeSpan(v);
    double len = 0;
    for (u32 j = 0; j < n; ++j) len += v[j] * v[j];
    len = std::sqrt(len);
    if (len < 1e-9) {
        for (u32 m = 0; m < n; ++m) {
            std::fill(v, v + n, 0.0);
            v[m] = 1.0;
            removeSpan(v);
            len = 0;
            for (u32 j = 0; j < n; ++j) len += v[j] * v[j];
            len = std::sqrt(len);
            if (len > 1e-3) break;
        }
    }
    const double inv = len > 0 ? 1.0 / len : 0.0;
    for (u32 j = 0; j < n; ++j) v[j] *= inv;
}

} // namespace

void PoseDatabase::buildKd() {
    kdNodes_.clear();
    kdOrder_.clear();
    pca_.clear();
    minBias_ = 0.0f;

    std::vector<u32> idx;
    for (u32 r = 0; r < rows_; ++r)
        if (!(meta_[r].flags & (kRowNoSearch | kRowTail))) idx.push_back(r);
    if (idx.size() < 2 * kLeafRows) return;   // a tree that is one leaf is just the linear scan

    const u32 n = dimPadded_, d = dim_;
    std::vector<double> mu(n, 0.0), cov(static_cast<usize>(n) * n, 0.0);
    for (const u32 r : idx) {
        const f32* a = row(r);
        for (u32 j = 0; j < d; ++j) mu[j] += a[j];
    }
    for (u32 j = 0; j < d; ++j) mu[j] /= static_cast<double>(idx.size());
    std::vector<double> dv(d);
    for (const u32 r : idx) {
        const f32* a = row(r);
        for (u32 j = 0; j < d; ++j) dv[j] = a[j] - mu[j];
        for (u32 i = 0; i < d; ++i) {
            const double di = dv[i];
            double* c = cov.data() + static_cast<usize>(i) * n;
            for (u32 j = i; j < d; ++j) c[j] += di * dv[j];
        }
    }
    for (u32 i = 0; i < d; ++i)
        for (u32 j = i + 1; j < d; ++j) cov[static_cast<usize>(j) * n + i] = cov[static_cast<usize>(i) * n + j];

    // Orthogonal subspace iteration from the identity-leading basis: fixed 40 rounds, no randomness.
    std::vector<double> basis(static_cast<usize>(kKdDims) * n, 0.0), z(static_cast<usize>(kKdDims) * n, 0.0);
    for (u32 k = 0; k < kKdDims; ++k) basis[static_cast<usize>(k) * n + k] = 1.0;
    for (u32 it = 0; it < 40; ++it) {
        for (u32 k = 0; k < kKdDims; ++k) {
            const double* b = basis.data() + static_cast<usize>(k) * n;
            double* o = z.data() + static_cast<usize>(k) * n;
            for (u32 i = 0; i < n; ++i) {
                const double* c = cov.data() + static_cast<usize>(i) * n;
                double s = 0;
                for (u32 j = 0; j < d; ++j) s += c[j] * b[j];
                o[i] = s;
            }
        }
        basis = z;
        for (u32 k = 0; k < kKdDims; ++k) orthonormalise(basis, k, n);
    }
    pca_.resize(static_cast<usize>(kKdDims) * n);
    for (usize i = 0; i < pca_.size(); ++i) pca_[i] = static_cast<f32>(basis[i]);

    // 8-D projection of every searchable row (the same loop the query uses).
    std::vector<f32> proj(static_cast<usize>(rows_) * kKdDims, 0.0f);
    f32 minB = std::numeric_limits<f32>::infinity();
    for (const u32 r : idx) {
        const f32* a = row(r);
        for (u32 k = 0; k < kKdDims; ++k) {
            const f32* p = pca_.data() + static_cast<usize>(k) * n;
            f32 s = 0.0f;
            for (u32 j = 0; j < n; ++j) s += p[j] * a[j];
            proj[static_cast<usize>(r) * kKdDims + k] = s;
        }
        minB = std::min(minB, rangeBias_[meta_[r].range]);
    }
    minBias_ = minB;

    kdOrder_ = idx;
    kdNodes_.reserve(idx.size() / (kLeafRows / 2) + 4);
    struct Build {
        PoseDatabase& db;
        const std::vector<f32>& proj;
        u32 make(u32 begin, u32 end) {
            const u32 me = static_cast<u32>(db.kdNodes_.size());
            db.kdNodes_.emplace_back();
            KdNode nd{};
            for (u32 k = 0; k < kKdDims; ++k) { nd.lo[k] = std::numeric_limits<f32>::infinity(); nd.hi[k] = -nd.lo[k]; }
            for (u32 i = begin; i < end; ++i) {
                const u32 r = db.kdOrder_[i];
                nd.tagOr |= db.meta_[r].tagMask;
                for (u32 k = 0; k < kKdDims; ++k) {
                    const f32 v = proj[static_cast<usize>(r) * kKdDims + k];
                    nd.lo[k] = std::min(nd.lo[k], v);
                    nd.hi[k] = std::max(nd.hi[k], v);
                }
            }
            nd.flagsAny = 1;
            if (end - begin <= kLeafRows) {
                nd.rowBegin = begin;
                nd.rowEnd = end;
                db.kdNodes_[me] = nd;
                return me;
            }
            u32 dimMax = 0;
            f32 ext = -1.0f;
            for (u32 k = 0; k < kKdDims; ++k)
                if (nd.hi[k] - nd.lo[k] > ext) { ext = nd.hi[k] - nd.lo[k]; dimMax = k; }
            const u32 mid = begin + (end - begin) / 2;
            std::nth_element(db.kdOrder_.begin() + begin, db.kdOrder_.begin() + mid, db.kdOrder_.begin() + end,
                             [&](u32 a, u32 b) {
                                 const f32 va = proj[static_cast<usize>(a) * kKdDims + dimMax];
                                 const f32 vb = proj[static_cast<usize>(b) * kKdDims + dimMax];
                                 return va < vb || (va == vb && a < b);
                             });
            nd.left = make(begin, mid);
            nd.right = make(mid, end);
            db.kdNodes_[me] = nd;
            return me;
        }
    } b{*this, proj};
    b.make(0, static_cast<u32>(kdOrder_.size()));
}

} // namespace aver::anim
