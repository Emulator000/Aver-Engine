// Exact nearest-neighbour search: the SSE2 scan equals the scalar scan and a brute-force reference, and the
// KD tree over the PCA projection returns the very same row as the linear scan under every filter. Plus the
// tie rule (lower row wins), empty filters and the optional --bench.
#include "MotionTestKit.hpp"

#include "aver/core/Hash.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

using namespace aver;
using mt::check;

namespace {

struct Rng {
    u64 s = 0x9E3779B97F4A7C15ull;
    u32 next() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return static_cast<u32>(s >> 32);
    }
    f32 unit() { return static_cast<f32>(next()) / 4294967296.0f; }            // [0, 1)
    f32 range(f32 a, f32 b) { return a + (b - a) * unit(); }
    u32 below(u32 n) { return next() % n; }
};

// A long clip of random motion: a jittery root path and random feet, keyed every 1/30 s.
fmt::OcAnimation noiseClip(u32 frames, Rng& rng) {
    fmt::OcAnimation c;
    c.duration = static_cast<f32>(frames - 1) / 30.0f;
    fmt::OcTrack r;
    r.boneIndex = 0;
    r.channels = fmt::kOcChannelTranslation | fmt::kOcChannelRotation;
    Vec3 p{0, 0, 0};
    f32 yaw = 0.0f, speed = 100.0f;
    for (u32 i = 0; i < frames; ++i) {
        r.times.push_back(static_cast<f32>(i) / 30.0f);
        const Quat q = mt::rotZ(yaw);
        r.values.insert(r.values.end(), {p.x, p.y, p.z, q.x, q.y, q.z, q.w});
        speed = std::min(std::max(speed + rng.range(-8.0f, 8.0f), 0.0f), 450.0f);
        yaw += rng.range(-0.05f, 0.05f);
        p += Vec3{std::cos(yaw), std::sin(yaw), 0.0f} * (speed / 30.0f);
    }
    c.tracks.push_back(r);
    for (int side = 0; side < 2; ++side) {
        fmt::OcTrack t;
        t.boneIndex = static_cast<u16>(2 + side);
        t.channels = fmt::kOcChannelTranslation;
        for (u32 i = 0; i < frames; ++i) {
            t.times.push_back(static_cast<f32>(i) / 30.0f);
            t.values.insert(t.values.end(), {rng.range(-25.0f, 25.0f), (side ? 15.0f : -15.0f) + rng.range(-6.0f, 6.0f),
                                             rng.range(-100.0f, -88.0f)});
        }
        c.tracks.push_back(t);
    }
    return c;
}

anim::MotionAsset noiseAsset(u32 entries, f32 windowSeconds, anim::SearchAccel accel, u32 kdMinRows) {
    anim::MotionAsset a = mt::baseAsset();
    a.search.accel = accel;
    a.search.kdMinRows = kdMinRows;
    const char* names[] = {"idle", "walk", "run"};
    for (u32 k = 0; k < entries; ++k) {
        anim::MotionClipEntry e = mt::entry("noise", {names[k % 3]}, (static_cast<f32>(k) - 3.0f) * 0.3f);
        e.start = static_cast<f32>(k) * windowSeconds;
        e.end = e.start + windowSeconds;
        e.loop = 0;
        e.noSearch = (k == 5);
        if (k == 2) e.ranges.push_back({e.start + 10.0f, e.start + 20.0f, {"stop"}, true});
        a.clips.push_back(e);
    }
    return a;
}

// Brute force in double: the reference answer for a query and filter.
anim::SearchHit reference(const anim::PoseDatabase& db, const std::vector<f32>& q, const anim::SearchFilter& f) {
    anim::SearchHit best;
    double bestCost = 1e300;
    for (u32 r = 0; r < db.rows(); ++r) {
        const anim::RowMeta& m = db.meta(r);
        if (m.flags & (anim::kRowNoSearch | anim::kRowTail)) continue;
        if ((m.tagMask & f.require) != f.require || (m.tagMask & f.exclude)) continue;
        double s = db.ranges()[m.range].bias;
        for (u32 j = 0; j < db.dimPadded(); ++j) {
            const double d = static_cast<double>(q[j]) - db.row(r)[j];
            s += d * d;
        }
        if (s < bestCost) { bestCost = s; best = {r, static_cast<f32>(s)}; }
    }
    return best;
}

double costOf(const anim::PoseDatabase& db, const std::vector<f32>& q, u32 r) {
    double s = db.ranges()[db.meta(r).range].bias;
    for (u32 j = 0; j < db.dimPadded(); ++j) {
        const double d = static_cast<double>(q[j]) - db.row(r)[j];
        s += d * d;
    }
    return s;
}

std::vector<f32> randomQuery(const anim::PoseDatabase& db, Rng& rng) {
    std::vector<f32> q(db.dimPadded(), 0.0f);
    if (rng.below(4) == 0) {
        for (u32 j = 0; j < db.dim(); ++j) q[j] = rng.range(-2.0f, 2.0f);
    } else {
        const u32 r0 = rng.below(db.rows());
        const f32 amount = rng.range(0.0f, 0.6f);
        for (u32 j = 0; j < db.dim(); ++j) q[j] = db.row(r0)[j] + rng.range(-amount, amount);
    }
    return q;
}

} // namespace

int main(int argc, char** argv) {
    AVER_INFO("PoseSearchTest");
    Rng rng;
    mt::ClipTable t;
    t.add("noise", noiseClip(21000, rng));

    anim::MotionAsset asset = noiseAsset(8, 87.5f, anim::SearchAccel::Auto, 1024);
    anim::PoseDatabase db;
    anim::MotionBuildReport rep;
    std::string why;
    check(anim::buildPoseDatabase(asset, mt::rig(), t.fn(), db, &rep, &why), "builds 20k rows " + why);
    AVER_INFO("  {} rows, {} MB, build {:.2f} s", db.rows(), static_cast<double>(db.memoryBytes()) / 1048576.0, rep.seconds);
    check(db.rows() > 20000 && db.hasKd(), "Auto builds the tree above kdMinRows");

    const u32 walk = db.tagBit(fnv1a64(std::string_view("walk"))), run = db.tagBit(fnv1a64(std::string_view("run"))),
              stop = db.tagBit(fnv1a64(std::string_view("stop")));
    const anim::SearchFilter filters[] = {{0, 0}, {walk, 0}, {run, 0}, {0, stop}, {0, run | walk}, {walk, stop}, {run, walk}};

    AVER_INFO("SSE2 vs scalar vs brute force (5000 queries)");
    {
        int bad = 0, nearTie = 0;
        f32 worstRel = 0;
        for (int i = 0; i < 5000; ++i) {
            const std::vector<f32> q = randomQuery(db, rng);
            const anim::SearchFilter f = filters[i % 7];
            const anim::SearchHit a = db.searchLinear(q.data(), f, false);
            const anim::SearchHit b = db.searchLinear(q.data(), f, true);
            const anim::SearchHit ref = reference(db, q, f);
            if (a.row == anim::kNoRow || b.row == anim::kNoRow || ref.row == anim::kNoRow) { ++bad; continue; }
            const double ca = costOf(db, q, a.row), cr = costOf(db, q, ref.row);
            const f32 rel = std::fabs(a.cost - b.cost) / std::max(1.0f, std::fabs(b.cost));
            worstRel = std::max(worstRel, rel);
            if (a.row != ref.row) {
                // Only an (almost) exact tie may disagree.
                if (std::fabs(ca - cr) > 1e-5 * std::max(1.0, cr)) ++bad; else ++nearTie;
            }
            if (rel > 1e-5f) ++bad;
        }
        AVER_INFO("  worst SSE-vs-scalar relative cost difference {:.2e}, near ties {}", worstRel, nearTie);
        check(bad == 0, "the SSE2 path equals the scalar path (1e-5) and the brute-force reference");
    }

    AVER_INFO("KD tree equals the linear scan (2000 queries)");
    {
        int diff = 0;
        for (int i = 0; i < 2000; ++i) {
            const std::vector<f32> q = randomQuery(db, rng);
            const anim::SearchFilter f = filters[i % 7];
            const anim::SearchHit lin = db.searchLinear(q.data(), f);
            const anim::SearchHit kd = db.searchKd(q.data(), f);
            const anim::SearchHit viaAuto = db.search(q.data(), f, anim::SearchAccel::Auto);
            if (lin.row != kd.row || lin.cost != kd.cost || viaAuto.row != kd.row) {
                ++diff;
                if (diff < 4) AVER_ERROR("  query {}: linear {} ({}) vs kd {} ({})", i, lin.row, lin.cost, kd.row, kd.cost);
            }
        }
        check(diff == 0, "the KD result is the linear result row for row, with bias, require, exclude, NoSearch and Tail");
        check(db.search(randomQuery(db, rng).data(), {}, anim::SearchAccel::Linear).row != anim::kNoRow, "accel Linear is honoured");
    }

    AVER_INFO("top-k");
    {
        const std::vector<f32> q = randomQuery(db, rng);
        std::vector<anim::SearchHit> top;
        db.searchTopK(q.data(), {}, 8, top);
        check(top.size() == 8 && top[0].row == db.searchLinear(q.data(), {}).row, "top-k starts at the best hit");
        bool sorted = true;
        for (usize i = 1; i < top.size(); ++i) sorted &= top[i - 1].cost <= top[i].cost;
        check(sorted, "and is ordered by cost");
    }

    AVER_INFO("ties, empty filters, forced KD below the threshold");
    {
        mt::ClipTable small;
        small.add("walk", mt::walkClip(1.0f, 150.0f, true, 10.0f));
        anim::MotionAsset a = mt::baseAsset();
        a.search.accel = anim::SearchAccel::KdTree;
        a.clips = {mt::entry("walk", {"walk"}), mt::entry("walk", {"walk"}), mt::entry("walk", {"run"})};
        anim::PoseDatabase sdb;
        check(anim::buildPoseDatabase(a, mt::rig(), small.fn(), sdb, nullptr, &why), "builds a duplicated database " + why);
        check(sdb.rows() == 90 && sdb.hasKd() && sdb.rows() < a.search.kdMinRows, "KdTree is forced below kdMinRows");
        // Range 1 repeats range 0 exactly: querying a row of the copy must still return the original.
        bool lower = true;
        for (u32 k = 0; k < 30; k += 3) {
            std::vector<f32> q(sdb.dimPadded());
            std::copy(sdb.row(30 + k), sdb.row(30 + k) + sdb.dimPadded(), q.begin());
            const anim::SearchHit lin = sdb.searchLinear(q.data(), {});
            const anim::SearchHit kd = sdb.searchKd(q.data(), {});
            lower &= lin.row == k && kd.row == k && lin.cost == 0.0f && kd.cost == 0.0f;
        }
        check(lower, "an exact tie returns the lower row index, linear and KD");
        std::vector<f32> q(sdb.dimPadded(), 0.0f);
        check(sdb.searchLinear(q.data(), {1u << 20, 0}).row == anim::kNoRow && sdb.searchKd(q.data(), {1u << 20, 0}).row == anim::kNoRow,
              "a filter that leaves nothing gives kNoRow");
        check(sdb.searchKd(q.data(), {0, 0xFFFFFFFFu}).row == anim::kNoRow, "excluding every tag gives kNoRow");
    }

    if (argc > 1 && std::strcmp(argv[1], "--bench") == 0) {
        Rng r2;
        mt::ClipTable big;
        big.add("noise", noiseClip(100000, r2));
        anim::MotionAsset ba = noiseAsset(1, 3333.0f, anim::SearchAccel::KdTree, 1024);
        anim::PoseDatabase bdb;
        anim::MotionBuildReport brep;
        anim::buildPoseDatabase(ba, mt::rig(), big.fn(), bdb, &brep, &why);
        std::vector<std::vector<f32>> qs;
        for (int i = 0; i < 200; ++i) qs.push_back(randomQuery(bdb, r2));
        auto timeIt = [&](bool kd) {
            const auto t0 = std::chrono::steady_clock::now();
            u32 sink = 0;
            for (const auto& q : qs) sink += kd ? bdb.searchKd(q.data(), {}).row : bdb.searchLinear(q.data(), {}).row;
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / qs.size();
            return std::pair<double, u32>(us, sink);
        };
        const auto lin = timeIt(false), kd = timeIt(true);
        AVER_INFO("bench: {} rows x {} floats ({:.1f} MB), build {:.1f} s: linear {:.0f} us, kd {:.0f} us per search",
                  bdb.rows(), bdb.dimPadded(), static_cast<double>(bdb.memoryBytes()) / 1048576.0, brep.seconds, lin.first, kd.first);
    }

    AVER_INFO("{} failure(s)", mt::g_failures);
    return mt::g_failures == 0 ? 0 : 1;
}
