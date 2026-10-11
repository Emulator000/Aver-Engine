#include "aver/synapse/Eqs.hpp"

#include "aver/core/Log.hpp"
#include "aver/synapse/Nav.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace aver::synapse {
namespace {

constexpr f32 kNaN = std::numeric_limits<f32>::quiet_NaN();
constexpr f32 kPi = 3.14159265358979323846f;

u32 costClass(EqsTestKind k) {
    switch (k) {
        case EqsTestKind::Random:
        case EqsTestKind::Tag:
        case EqsTestKind::Dot:
        case EqsTestKind::Distance: return 0;
        case EqsTestKind::Custom: return 1;
        case EqsTestKind::Trace: return 2;
        case EqsTestKind::Path: return 3;
    }
    return 0;
}

f32 applyCurve(EqsCurve c, f32 n) {
    switch (c) {
        case EqsCurve::Linear: return n;
        case EqsCurve::Inverse: return 1.0f - n;
        case EqsCurve::Square: return n * n;
        case EqsCurve::InvSquare: return (1.0f - n) * (1.0f - n);
        case EqsCurve::Sqrt: return std::sqrt(n);
        case EqsCurve::Step: return n >= 0.5f ? 1.0f : 0.0f;
    }
    return n;
}

Vec3 flat(Vec3 v) {
    v.z = 0;
    return v;
}

} // namespace

u64 eqsMix64(u64 x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

f32 eqsRandom01(u32 seed, u32 index) {
    const u64 h = eqsMix64((static_cast<u64>(seed) << 32) | index);
    return static_cast<f32>(h >> 40) * (1.0f / 16777216.0f);
}

u32 eqsTestCost(EqsTestKind k) {
    static constexpr u32 kCost[4] = {1, 2, 8, 40};
    return kCost[costClass(k)];
}

EqsRun::EqsRun(const EqsQuery& q, EqsRunParams p, const EqsEnv& env) : q_(q), params_(std::move(p)), env_(env) {
    result_.tagSets.emplace_back();
    if (q_.options.empty()) {
        phase_ = Phase::Finalize;
    }
}

void EqsRun::cancel() {
    if (done_) return;
    done_ = true;
    cancelled_ = true;
    result_.status = EqsResult::Status::Failed;
    result_.reason = "cancelled";
    result_.items.clear();
    result_.testValues.clear();
}

void EqsRun::fail(const std::string& reason) {
    done_ = true;
    result_.status = EqsResult::Status::Failed;
    result_.reason = reason;
    result_.items.clear();
    result_.testValues.clear();
}

bool EqsRun::step(u32 maxUnits) {
    if (done_) return true;
    const u64 start = units_;
    do {
        advance();
    } while (!done_ && (maxUnits == 0 || units_ - start < maxUnits));
    return done_;
}

const EqsContext& EqsRun::ctx(const std::string& name) {
    const auto it = ctxCache_.find(name);
    if (it != ctxCache_.end()) return it->second;
    EqsContext c;
    if (name == "point") {
        c.pos = params_.point;
        c.ok = params_.hasPoint;
    } else if (env_.context) {
        c = env_.context(env_.user, name, params_);
    }
    return ctxCache_.emplace(name, c).first->second;
}

bool EqsRun::pushItem(const EqsGenerator& g, Vec3 p, u32 entity, u32 object, i32 slot, u32 tagSet) {
    if (g.project == EqsProject::Nav && g.kind != EqsGen::Nav) {
        u32 cx = 0, cy = 0;
        if (!env_.nav || !nearestWalkable(*env_.nav, p.x, p.y, 4, cx, cy)) return true;   // dropped
        const usize key = static_cast<usize>(cy) * env_.nav->widthCells + cx;
        if (navSeen_.size() <= key) navSeen_.resize(static_cast<usize>(env_.nav->widthCells) * env_.nav->heightCells, 0);
        if (navSeen_[key]) return true;
        navSeen_[key] = 1;
        p = cellToWorld(*env_.nav, cx, cy);
    } else if (g.project == EqsProject::Ground && env_.ground) {
        f32 z = 0;
        if (!env_.ground(env_.user, p, 500.0f, 2500.0f, z)) return true;
        p.z = z;
    }
    p.z += g.zOffset;
    if (cands_.size() >= g.maxItems) {
        result_.truncated = true;
        return false;
    }
    Cand c;
    c.item.pos = p;
    c.item.entity = entity;
    c.item.object = object;
    c.item.slot = slot;
    c.item.tagSet = tagSet;
    c.item.genIndex = static_cast<u32>(cands_.size());
    c.item.option = static_cast<u8>(optIdx_);
    cands_.push_back(std::move(c));
    return true;
}

bool EqsRun::generate(const EqsOption& opt) {
    const EqsGenerator& g = opt.gen;
    cands_.clear();
    navSeen_.clear();

    if (g.project == EqsProject::Nav && !env_.nav) {
        fail("project=nav needs a navigation grid");
        return false;
    }

    Vec3 c;
    if (g.kind != EqsGen::Points) {
        const EqsContext& cc = ctx(g.centre);
        if (!cc.ok) {
            fail("generator centre '" + g.centre + "' did not resolve");
            return false;
        }
        c = cc.pos;
    }

    switch (g.kind) {
        case EqsGen::Grid: {
            const u32 nx = static_cast<u32>(std::floor(2.0f * g.halfX / g.step + 1e-4f)) + 1;
            const u32 ny = static_cast<u32>(std::floor(2.0f * g.halfY / g.step + 1e-4f)) + 1;
            for (u32 ix = 0; ix < nx; ++ix)
                for (u32 iy = 0; iy < ny; ++iy)
                    if (!pushItem(g, c + Vec3{-g.halfX + static_cast<f32>(ix) * g.step, -g.halfY + static_cast<f32>(iy) * g.step, 0}, 0, 0, -1, 0))
                        return true;
            break;
        }
        case EqsGen::Ring:
        case EqsGen::Around: {
            f32 angle0 = 0;
            if (!g.facing.empty()) {
                const EqsContext& f = ctx(g.facing);
                if (f.ok && (f.pos.x != c.x || f.pos.y != c.y)) angle0 = std::atan2(f.pos.y - c.y, f.pos.x - c.x);
            }
            const u32 rings = g.kind == EqsGen::Around ? 1 : g.rings;
            const f32 span = g.arcEnd - g.arcStart;
            f32 stepDeg = 0, first = g.arcStart;
            if (span >= 360.0f - 1e-3f) stepDeg = span / static_cast<f32>(g.count);
            else if (g.count > 1) stepDeg = span / static_cast<f32>(g.count - 1);
            else first = g.arcStart + span * 0.5f;
            for (u32 r = 0; r < rings; ++r) {
                f32 radius = g.radius;
                if (g.kind == EqsGen::Ring) radius = rings == 1 ? g.outer : g.inner + (g.outer - g.inner) * static_cast<f32>(r) / static_cast<f32>(rings - 1);
                for (u32 k = 0; k < g.count; ++k) {
                    const f32 a = angle0 + (first + static_cast<f32>(k) * stepDeg) * (kPi / 180.0f);
                    if (!pushItem(g, c + Vec3{std::cos(a) * radius, std::sin(a) * radius, 0}, 0, 0, -1, 0)) return true;
                }
            }
            break;
        }
        case EqsGen::Nav: {
            if (!env_.nav) {
                fail("the nav generator needs a navigation grid");
                return false;
            }
            const fmt::OcNavData& nav = *env_.nav;
            u32 cx = 0, cy = 0;
            if (!nearestWalkable(nav, c.x, c.y, 4, cx, cy)) break;   // the centre is off the grid: nothing to offer
            const u16 region = nav.at(cx, cy)->regionId;
            const i32 n = static_cast<i32>(std::floor(g.radius / (nav.cellSizeCm * static_cast<f32>(g.stride))));
            const i32 s = static_cast<i32>(g.stride);
            for (i32 j = -n; j <= n; ++j) {
                const i32 y = static_cast<i32>(cy) + j * s;
                if (y < 0 || y >= static_cast<i32>(nav.heightCells)) continue;
                for (i32 i = -n; i <= n; ++i) {
                    const i32 x = static_cast<i32>(cx) + i * s;
                    if (x < 0 || x >= static_cast<i32>(nav.widthCells)) continue;
                    const fmt::OcNavCell* cell = nav.at(static_cast<u32>(x), static_cast<u32>(y));
                    if (!cell || !(cell->flags & fmt::kOcNavWalkable)) continue;
                    if (g.sameRegion && cell->regionId != region) continue;
                    const Vec3 w = cellToWorld(nav, static_cast<u32>(x), static_cast<u32>(y));
                    const f32 dx = w.x - c.x, dy = w.y - c.y;
                    if (dx * dx + dy * dy > g.radius * g.radius) continue;
                    if (!pushItem(g, w, 0, 0, -1, 0)) return true;
                }
            }
            break;
        }
        case EqsGen::Actors: {
            if (!env_.actors) break;
            std::vector<u32> ids(static_cast<usize>(g.maxItems) + 1);
            const u32 n = env_.actors(env_.user, g, c, params_.querier, ids.data(), static_cast<u32>(ids.size()));
            for (u32 i = 0; i < n && i < ids.size(); ++i) {
                if (g.excludeQuerier && ids[i] == params_.querier) continue;
                Vec3 p;
                if (!env_.entityPos || !env_.entityPos(env_.user, ids[i], p)) continue;
                if (g.radius > 0 && (p - c).size() > g.radius) continue;
                if (!pushItem(g, p, ids[i], 0, -1, 0)) return true;
            }
            break;
        }
        case EqsGen::Slots: {
            if (!env_.slots) break;
            const u32 cap = g.maxItems + 1;
            std::vector<EqsItem> items(cap);
            std::vector<TagList> tags(cap);
            const u32 n = env_.slots(env_.user, g, c, params_, items.data(), tags.data(), cap);
            for (u32 i = 0; i < n && i < cap; ++i) {
                u32 set = 0;
                if (!tags[i].v.empty()) {
                    set = static_cast<u32>(result_.tagSets.size());
                    result_.tagSets.push_back(tags[i]);
                }
                if (!pushItem(g, items[i].pos, items[i].entity, items[i].object, items[i].slot, set)) return true;
            }
            break;
        }
        case EqsGen::Points:
            for (const Vec3& p : g.points)
                if (!pushItem(g, p, 0, 0, -1, 0)) return true;
            break;
    }
    return true;
}

bool EqsRun::evalTest(const EqsTest& t, u32 declIdx, Cand& c) {
    f32 v = 0;
    bool unresolved = false;
    const Vec3& pos = c.item.pos;

    switch (t.kind) {
        case EqsTestKind::Distance: {
            const EqsContext& x = ctx(t.to);
            if (!x.ok) { unresolved = true; break; }
            const Vec3 d = pos - x.pos;
            if (t.metric == "2d") v = flat(d).size();
            else if (t.metric == "z") v = d.z;
            else if (t.metric == "absz") v = std::fabs(d.z);
            else v = d.size();
            break;
        }
        case EqsTestKind::Dot: {
            const EqsContext& from = ctx(t.from);
            if (!from.ok) { unresolved = true; break; }
            Vec3 ref{1, 0, 0};
            if (t.ref == "forward") {
                Vec3 f;
                if (from.entity != 0 && env_.forward && env_.forward(env_.user, from.entity, f)) ref = f;
            } else if (t.ref == "up") {
                ref = Vec3{0, 0, 1};
            } else if (t.ref == "toward") {
                const EqsContext& to = ctx(t.refCtx);
                if (!to.ok) { unresolved = true; break; }
                ref = to.pos - from.pos;
            } else {
                ref = t.refDir;
            }
            Vec3 d = pos - from.pos;
            if (t.metric == "2d") { d = flat(d); ref = flat(ref); }
            const Vec3 dn = d.getSafeNormal(), rn = ref.getSafeNormal();
            v = (dn.sizeSquared() < 0.5f || rn.sizeSquared() < 0.5f) ? 0.0f : dot(dn, rn);
            break;
        }
        case EqsTestKind::Trace: {
            const EqsContext& from = ctx(t.from);
            if (!from.ok) { unresolved = true; break; }
            const Vec3 a = from.pos + Vec3{0, 0, t.fromZ};
            const Vec3 b = pos + Vec3{0, 0, t.toZ};
            const Vec3 ab = b - a;
            const f32 len = ab.size();
            if (!env_.trace) {
                if (!warnedTrace_) { AVER_WARN("[Synapse] EQS trace test has no physics: every item reads clear"); warnedTrace_ = true; }
                v = 1;
            } else if (len <= t.gap) {
                v = 1;
            } else {
                const Vec3 end = b - ab * (t.gap / len);
                v = env_.trace(env_.user, a, end, t.radius, from.entity, c.item.entity) ? 0.0f : 1.0f;
            }
            break;
        }
        case EqsTestKind::Path: {
            const EqsContext& from = ctx(t.from);
            if (!from.ok) { unresolved = true; break; }
            if (!env_.nav) return false;
            PathRequest req;
            req.startXCm = from.pos.x;
            req.startYCm = from.pos.y;
            req.goalXCm = pos.x;
            req.goalYCm = pos.y;
            req.snapRingCells = 4;
            req.maxExpansions = t.maxExp;
            const PathResult res = findPath(*env_.nav, req);
            if (res.status != PathStatus::Found && !(res.status == PathStatus::Partial && t.partialOk)) return false;
            if (t.metric == "reachable") {
                v = 1;
            } else {
                for (usize i = 1; i < res.points.size(); ++i) v += (res.points[i] - res.points[i - 1]).size();
            }
            break;
        }
        case EqsTestKind::Tag: {
            if (!t.tags.v.empty()) {
                const TagList& have = result_.tagSets[c.item.tagSet < result_.tagSets.size() ? c.item.tagSet : 0];
                const bool r = t.match == 0 ? have.hasAny(t.tags) : t.match == 1 ? have.hasAll(t.tags) : have.hasNone(t.tags);
                v = r ? 1.0f : 0.0f;
            } else {
                const u32 bits = (c.item.entity != 0 && env_.entityTagBits) ? env_.entityTagBits(env_.user, c.item.entity) : 0;
                const bool r = t.match == 0 ? (bits & t.bits) != 0 : t.match == 1 ? (bits & t.bits) == t.bits : (bits & t.bits) == 0;
                v = r ? 1.0f : 0.0f;
            }
            break;
        }
        case EqsTestKind::Random:
            v = eqsRandom01(t.seed, c.item.genIndex);
            break;
        case EqsTestKind::Custom: {
            if (!env_.custom) {
                if (!warnedCustom_) { AVER_WARN("[Synapse] EQS custom test '{}' has no managed host: every item is rejected", t.custom); warnedCustom_ = true; }
                return false;
            }
            const EqsContext& from = ctx(t.from);
            if (!env_.custom(env_.user, t, params_, c.item, from, v)) {
                if (!warnedCustom_) { AVER_WARN("[Synapse] EQS custom test '{}' is not registered: every item is rejected", t.custom); warnedCustom_ = true; }
                return false;
            }
            break;
        }
    }

    if (unresolved) {
        if (t.purpose != EqsPurpose::Score) return false;
        c.values[declIdx] = kNaN;
        return true;
    }
    if (!std::isfinite(v)) return false;
    c.values[declIdx] = v;
    if (t.purpose == EqsPurpose::Score) return true;
    if (eqsTestIsBoolean(t)) return (v != 0.0f) == t.want;
    return v >= t.min && v <= t.max;
}

void EqsRun::scoreOption(const EqsOption& opt) {
    const usize nTests = opt.tests.size();
    struct Range { f32 lo = 0, hi = 0; };
    std::vector<Range> ranges(nTests);
    f32 denom = 0;
    for (usize t = 0; t < nTests; ++t) {
        const EqsTest& test = opt.tests[t];
        if (test.purpose == EqsPurpose::Filter) continue;
        denom += std::fabs(test.weight);
        f32 lo = std::numeric_limits<f32>::infinity(), hi = -std::numeric_limits<f32>::infinity();
        for (const u32 ci : alive_) {
            const f32 v = cands_[ci].values[t];
            if (std::isnan(v)) continue;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        if (lo > hi) lo = hi = 0;
        ranges[t].lo = test.hasLo ? test.lo : lo;
        ranges[t].hi = test.hasHi ? test.hi : hi;
    }
    scored_.clear();
    scored_.reserve(alive_.size());
    for (const u32 ci : alive_) {
        Cand& cand = cands_[ci];
        f32 sum = 0;
        for (usize t = 0; t < nTests; ++t) {
            const EqsTest& test = opt.tests[t];
            if (test.purpose == EqsPurpose::Filter) continue;
            f32 v = cand.values[t];
            if (std::isnan(v)) continue;   // contributes 0
            if (eqsTestIsBoolean(test) && !test.want) v = 1.0f - v;
            const Range& r = ranges[t];
            const f32 n = r.hi == r.lo ? 1.0f : std::clamp((v - r.lo) / (r.hi - r.lo), 0.0f, 1.0f);
            sum += test.weight * applyCurve(test.curve, n);
        }
        cand.item.score = denom > 0 ? sum / denom : 1.0f;
        Final f;
        f.item = cand.item;
        f.values = std::move(cand.values);
        scored_.push_back(std::move(f));
    }
}

void EqsRun::finalize() {
    std::vector<Final>& list = q_.runAll ? merged_ : scored_;
    std::sort(list.begin(), list.end(), [](const Final& a, const Final& b) {
        if (a.item.score != b.item.score) return a.item.score > b.item.score;
        if (a.item.option != b.item.option) return a.item.option < b.item.option;
        return a.item.genIndex < b.item.genIndex;
    });
    const usize count = list.size();
    result_.passed = static_cast<u32>(count);
    usize first = 0, keep = count;
    const auto slice = [&](f32 p) {
        return std::min(count, std::max<usize>(1, static_cast<usize>(std::ceil(static_cast<f64>(count) * p / 100.0))));
    };
    if (count > 0) {
        switch (q_.mode) {
            case EqsResultMode::All: break;
            case EqsResultMode::Best: keep = 1; break;
            case EqsResultMode::Top: keep = std::min<usize>(count, q_.modeN); break;
            case EqsResultMode::Percent: keep = slice(q_.modeP); break;
            case EqsResultMode::RandomPercent:
                first = static_cast<usize>(eqsMix64(params_.runId) % slice(q_.modeP));
                keep = 1;
                break;
        }
    }
    u32 testCount = winnerTests_;
    if (q_.runAll)
        for (const EqsOption& o : q_.options) testCount = std::max<u32>(testCount, static_cast<u32>(o.tests.size()));
    result_.testCount = testCount;
    result_.items.reserve(keep);
    result_.testValues.assign(keep * testCount, kNaN);
    for (usize i = 0; i < keep; ++i) {
        const Final& f = list[first + i];
        result_.items.push_back(f.item);
        for (usize t = 0; t < f.values.size() && t < testCount; ++t) result_.testValues[i * testCount + t] = f.values[t];
    }
    result_.status = EqsResult::Status::Finished;
    done_ = true;
}

void EqsRun::advance() {
    switch (phase_) {
        case Phase::Begin:
            if (optIdx_ >= q_.options.size()) { phase_ = Phase::Finalize; return; }
            phase_ = Phase::Generate;
            return;

        case Phase::Generate: {
            const EqsOption& opt = q_.options[optIdx_];
            if (!generate(opt)) return;   // failed
            result_.generated += static_cast<u32>(cands_.size());
            units_ += std::max<usize>(1, cands_.size());
            for (Cand& c : cands_) c.values.assign(opt.tests.size(), kNaN);
            order_.resize(opt.tests.size());
            for (u32 i = 0; i < order_.size(); ++i) order_[i] = i;
            std::stable_sort(order_.begin(), order_.end(), [&](u32 a, u32 b) { return costClass(opt.tests[a].kind) < costClass(opt.tests[b].kind); });
            alive_.resize(cands_.size());
            for (u32 i = 0; i < alive_.size(); ++i) alive_[i] = i;
            nextAlive_.clear();
            tcur_ = 0;
            icur_ = 0;
            phase_ = Phase::Tests;
            return;
        }

        case Phase::Tests: {
            const EqsOption& opt = q_.options[optIdx_];
            if (tcur_ >= order_.size() || alive_.empty()) { phase_ = Phase::Score; return; }
            if (icur_ >= alive_.size()) {
                alive_.swap(nextAlive_);
                nextAlive_.clear();
                icur_ = 0;
                ++tcur_;
                return;
            }
            const u32 declIdx = order_[tcur_];
            const EqsTest& t = opt.tests[declIdx];
            const u32 ci = alive_[icur_++];
            ++result_.evaluations;
            units_ += eqsTestCost(t.kind);
            if (evalTest(t, declIdx, cands_[ci])) nextAlive_.push_back(ci);
            return;
        }

        case Phase::Score:
            scoreOption(q_.options[optIdx_]);
            units_ += std::max<usize>(1, alive_.size());
            phase_ = Phase::EndOption;
            return;

        case Phase::EndOption: {
            const EqsOption& opt = q_.options[optIdx_];
            if (!q_.runAll) {
                if (!scored_.empty()) {
                    winnerTests_ = static_cast<u32>(opt.tests.size());
                    phase_ = Phase::Finalize;
                    return;
                }
            } else {
                for (Final& f : scored_) merged_.push_back(std::move(f));
                scored_.clear();
            }
            ++optIdx_;
            phase_ = Phase::Begin;
            return;
        }

        case Phase::Finalize:
            finalize();
            return;
    }
}

} // namespace aver::synapse
