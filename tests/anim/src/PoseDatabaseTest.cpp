// The pose database: row counts, hand-computed feature values (straight walk, circle, yawed root, loop
// seam, head clamp), Tail / NoSearch / tag flags, normalisation, the .ocmbake sidecar and the source key.
// Synthetic clips with closed-form root travel, so a wrong frame or a lost lap shows as a wrong number.
#include "MotionTestKit.hpp"

#include "aver/core/Hash.hpp"
#include "aver/formats/Avr1.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace aver;
using mt::check;
using mt::checkNear;

namespace {

anim::PoseDatabase build(const anim::MotionAsset& a, const mt::ClipTable& t, anim::MotionBuildReport* rep = nullptr,
                         bool* ok = nullptr) {
    anim::PoseDatabase db;
    std::string why;
    const bool good = anim::buildPoseDatabase(a, mt::rig(), t.fn(), db, rep, &why);
    if (!good) AVER_INFO("  (build: {})", why);
    if (ok) *ok = good;
    return db;
}

bool hasWarning(const anim::MotionBuildReport& r, const char* needle) {
    for (const std::string& w : r.warnings)
        if (w.find(needle) != std::string::npos) return true;
    return false;
}

std::vector<u8> readBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<u8>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void writeBytes(const std::string& path, const std::vector<u8>& b) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

} // namespace

int main() {
    AVER_INFO("PoseDatabaseTest");
    const fmt::OcSkeleton skel = mt::rig();

    AVER_INFO("row counts, layout");
    {
        mt::ClipTable t;
        anim::MotionAsset a = mt::locomotionAsset(t);
        t.add("short", mt::walkClip(1.0f, 150.0f, false));
        a.clips.push_back(mt::entry("short", {"walk"}));
        anim::MotionBuildReport rep;
        bool ok = false;
        const anim::PoseDatabase db = build(a, t, &rep, &ok);
        check(ok, "builds");
        check(db.ranges().size() == 4, "one range per entry");
        check(db.ranges()[0].rowCount == 60 && db.ranges()[0].loop, "2 s loop at 30 Hz = 60 rows");
        check(db.ranges()[1].rowCount == 30, "1 s loop = 30 rows");
        check(db.ranges()[2].rowCount == 24, "0.8 s loop = 24 rows");
        check(db.ranges()[3].rowCount == 31 && !db.ranges()[3].loop, "1 s non-loop = 31 rows (both ends)");
        check(db.rows() == 60 + 30 + 24 + 31 && rep.rows == db.rows(), "rows add up and the report agrees");
        // foot_l pos + vel, foot_r pos + vel (3 each), root velocity 3, future pos 8, dir 8, past pos 4.
        check(db.dim() == 12 + 3 + 8 + 8 + 4 && db.dimPadded() == 40, "dim 35, padded to 40");
        check(db.groups().size() == 8, "eight groups");
        check(reinterpret_cast<std::uintptr_t>(db.row(0)) % 32 == 0 && reinterpret_cast<std::uintptr_t>(db.row(1)) % 32 == 0,
              "rows are 32-byte aligned");
        check(rep.bytes == db.memoryBytes() && db.memoryBytes() >= static_cast<u64>(db.rows()) * 40 * 4, "memory is reported");
        check(db.timeOf(db.ranges()[1].firstRow + 3) > 0.099f && db.timeOf(db.ranges()[1].firstRow + 3) < 0.101f, "timeOf");
        check(db.rowOf(1, 0.1f) == db.ranges()[1].firstRow + 3, "rowOf at a sample time");
        check(db.rowOf(1, 0.115f) == db.ranges()[1].firstRow + 3, "rowOf takes the row at or before");
        check(db.rowOf(1, 99.0f) == db.ranges()[1].firstRow + 29 && db.rowOf(1, -5.0f) == db.ranges()[1].firstRow,
              "rowOf clamps to the range");
        check(db.rowOf(9, 0.0f) == anim::kNoRow, "rowOf on a bad range");
    }

    AVER_INFO("straight walk features, hand computed");
    {
        mt::ClipTable t;
        anim::MotionAsset a = mt::locomotionAsset(t);
        const anim::PoseDatabase db = build(a, t);
        const u32 row = db.ranges()[1].firstRow + 8;   // t = 8/30, inside the first swing segment
        const std::vector<f32> raw = mt::rawRow(db, row);
        auto at = [&](anim::FeatureKind k, u32 bone, u32 i) { return raw[mt::groupOf(db, k, bone)->firstDim + i]; };
        checkNear(at(anim::FeatureKind::RootVel, 0xFFFFFFFFu, 0), 150.0f, 0.01f, "root velocity x");
        checkNear(at(anim::FeatureKind::RootVel, 0xFFFFFFFFu, 1), 0.0f, 0.01f, "root velocity y");
        checkNear(at(anim::FeatureKind::RootVel, 0xFFFFFFFFu, 2), 0.0f, 0.001f, "yaw rate");
        checkNear(at(anim::FeatureKind::BoneVel, 2, 0), 190.0f, 0.05f, "foot_l world velocity = travel + swing (150 + 40)");
        checkNear(at(anim::FeatureKind::BoneVel, 3, 0), 110.0f, 0.05f, "foot_r swings the other way (150 - 40)");
        checkNear(at(anim::FeatureKind::BonePos, 2, 0), 0.6667f, 0.01f, "foot_l position along the heading");
        checkNear(at(anim::FeatureKind::BonePos, 2, 1), -15.0f, 0.01f, "foot_l position across (left)");
        checkNear(at(anim::FeatureKind::BonePos, 2, 2), 0.0f, 0.01f, "foot_l height is the model height");
        const f32 taus[] = {0.2f, 0.4f, 0.7f, 1.0f};
        for (u32 i = 0; i < 4; ++i) {
            checkNear(at(anim::FeatureKind::FuturePos, 0xFFFFFFFFu, 2 * i), 150.0f * taus[i], 0.02f,
                      "future position x at " + std::to_string(taus[i]) + " (1.0 crosses the loop seam)");
            checkNear(at(anim::FeatureKind::FuturePos, 0xFFFFFFFFu, 2 * i + 1), 0.0f, 0.01f, "future position y");
            checkNear(at(anim::FeatureKind::FutureDir, 0xFFFFFFFFu, 2 * i), 1.0f, 1e-4f, "future dir cos");
            checkNear(at(anim::FeatureKind::FutureDir, 0xFFFFFFFFu, 2 * i + 1), 0.0f, 1e-4f, "future dir sin");
        }
        checkNear(at(anim::FeatureKind::PastPos, 0xFFFFFFFFu, 0), -30.0f, 0.02f, "past position at -0.2");
        checkNear(at(anim::FeatureKind::PastPos, 0xFFFFFFFFu, 2), -75.0f, 0.02f, "past position at -0.5 wraps back a lap");
    }

    AVER_INFO("heading frame: a yawed root puts the travel on the heading axis");
    {
        mt::ClipTable t;
        t.add("north", mt::walkClip(1.0f, 150.0f, true, 0.0f, kPi * 0.5f));   // travels along world +Y
        anim::MotionAsset a = mt::baseAsset();
        a.clips = {mt::entry("north")};
        const anim::PoseDatabase db = build(a, t);
        const std::vector<f32> raw = mt::rawRow(db, 5);
        auto at = [&](anim::FeatureKind k, u32 bone, u32 i) { return raw[mt::groupOf(db, k, bone)->firstDim + i]; };
        checkNear(at(anim::FeatureKind::FuturePos, 0xFFFFFFFFu, 0), 30.0f, 0.02f, "forward travel is +x in the heading frame");
        checkNear(at(anim::FeatureKind::FuturePos, 0xFFFFFFFFu, 1), 0.0f, 0.02f, "and nothing sideways");
        checkNear(at(anim::FeatureKind::RootVel, 0xFFFFFFFFu, 0), 150.0f, 0.02f, "root velocity is forward");
        checkNear(at(anim::FeatureKind::BonePos, 2, 1), -15.0f, 0.01f, "bone positions are in the heading frame too");
    }

    AVER_INFO("circle: turning trajectory");
    {
        mt::ClipTable t;
        const f32 R = 100.0f, w = kPi * 0.5f;
        t.add("circle", mt::circleClip(R, w));
        anim::MotionAsset a = mt::baseAsset();
        a.clips = {mt::entry("circle")};
        const anim::PoseDatabase db = build(a, t);
        const std::vector<f32> raw = mt::rawRow(db, 30);   // t = 1.0
        auto at = [&](anim::FeatureKind k, u32 bone, u32 i) { return raw[mt::groupOf(db, k, bone)->firstDim + i]; };
        const f32 taus[] = {0.2f, 0.4f, 0.7f, 1.0f};
        for (u32 i = 0; i < 4; ++i) {
            const f32 th = w * taus[i];
            checkNear(at(anim::FeatureKind::FuturePos, 0xFFFFFFFFu, 2 * i), R * std::sin(th), 0.5f, "arc x");
            checkNear(at(anim::FeatureKind::FuturePos, 0xFFFFFFFFu, 2 * i + 1), R * (1.0f - std::cos(th)), 0.5f, "arc y");
            checkNear(at(anim::FeatureKind::FutureDir, 0xFFFFFFFFu, 2 * i), std::cos(th), 2e-3f, "heading cos");
            checkNear(at(anim::FeatureKind::FutureDir, 0xFFFFFFFFu, 2 * i + 1), std::sin(th), 2e-3f, "heading sin");
        }
        checkNear(at(anim::FeatureKind::RootVel, 0xFFFFFFFFu, 0), R * w, 0.3f, "tangential speed");
        checkNear(at(anim::FeatureKind::RootVel, 0xFFFFFFFFu, 2), w, 0.01f, "yaw rate");
    }

    AVER_INFO("non-loop: head clamp and the Tail flag");
    {
        mt::ClipTable t;
        t.add("w", mt::walkClip(1.0f, 150.0f, false));
        t.add("long", mt::walkClip(2.0f, 150.0f, false));
        anim::MotionAsset a = mt::baseAsset();
        a.clips = {mt::entry("w")};
        {
            const anim::PoseDatabase db = build(a, t);
            const std::vector<f32> r0 = mt::rawRow(db, 0), r6 = mt::rawRow(db, 6), r15 = mt::rawRow(db, 15);
            const u32 pd = mt::groupOf(db, anim::FeatureKind::PastPos)->firstDim;
            checkNear(r0[pd], 0.0f, 0.01f, "first frame: the past reads \"was standing still\"");
            checkNear(r6[pd + 2], -30.0f, 0.02f, "past -0.5 at t = 0.2 clamps to the start (-30, not -75)");
            checkNear(r15[pd], -30.0f, 0.02f, "past -0.2 at t = 0.5");
            checkNear(r15[pd + 2], -75.0f, 0.02f, "past -0.5 at t = 0.5");
        }
        a.clips = {mt::entry("long")};
        const anim::PoseDatabase db = build(a, t);
        u32 tail = 0;
        for (u32 r = 0; r < db.rows(); ++r) {
            const bool isTail = (db.meta(r).flags & anim::kRowTail) != 0;
            tail += isTail;
            if (isTail != (db.timeOf(r) > 1.0f + 1e-3f)) { check(false, "Tail exactly when the horizon passes the end"); break; }
        }
        check(db.rows() == 61 && tail == 30, "30 of 61 rows have a future horizon past the end");
        const std::vector<f32> q(db.dimPadded(), 0.0f);
        bool never = true;
        for (int i = 0; i < 200; ++i) {
            std::vector<f32> v(db.dimPadded(), 0.0f);
            for (u32 j = 0; j < db.dim(); ++j) v[j] = std::sin(static_cast<f32>(i * 31 + j * 7));
            const anim::SearchHit h = db.search(v.data(), {});
            never &= h.row != anim::kNoRow && !(db.meta(h.row).flags & anim::kRowTail);
        }
        check(never, "search never returns a Tail row");
    }

    AVER_INFO("tags and noSearch");
    {
        mt::ClipTable t;
        anim::MotionAsset a = mt::locomotionAsset(t);
        anim::MotionTagRange r;
        r.start = 0.0f;
        r.end = 0.5f;
        r.tags = {"stop"};
        r.noSearch = true;
        a.clips[1].ranges.push_back(r);
        a.clips[2].noSearch = true;
        a.clips[0].bias = 3.0f;
        const anim::PoseDatabase db = build(a, t);
        const u32 walk = db.tagBit(fnv1a64(std::string_view("walk"))), stop = db.tagBit(fnv1a64(std::string_view("stop")));
        const u32 w0 = db.ranges()[1].firstRow;
        check(walk != 0 && stop != 0 && walk != stop, "tag bits resolve");
        check(db.tagBit(fnv1a64(std::string_view("nope"))) == 0, "an unknown tag has no bit");
        check(db.meta(w0 + 6).tagMask == (walk | stop) && (db.meta(w0 + 6).flags & anim::kRowNoSearch), "inside the range: both tags, no search");
        check(db.meta(w0 + 20).tagMask == walk && !(db.meta(w0 + 20).flags & anim::kRowNoSearch), "outside the range: the entry tag only");
        check(db.meta(w0 + 15).tagMask == walk, "the range end is exclusive");
        check(db.ranges()[1].tagMask == walk, "the range keeps the entry-wide mask");
        check((db.meta(db.ranges()[2].firstRow).flags & anim::kRowNoSearch) != 0, "an entry-wide noSearch flags every row");
        const u64 hs[] = {fnv1a64(std::string_view("walk")), 0, fnv1a64(std::string_view("zzz"))};
        check(db.tagMaskOf(hs, 3) == walk, "tagMaskOf ignores zeros and unknown hashes");
        check(db.ranges()[0].bias == 3.0f, "the bias is stored per range");
        // NoSearch rows are never found: query exactly a run row.
        std::vector<f32> q(db.dimPadded());
        std::copy(db.row(db.ranges()[2].firstRow), db.row(db.ranges()[2].firstRow) + db.dimPadded(), q.begin());
        check(db.search(q.data(), {}).row != db.ranges()[2].firstRow, "a noSearch row is not returned even for its own vector");
    }

    AVER_INFO("normalisation");
    {
        mt::ClipTable t;
        anim::MotionAsset a = mt::locomotionAsset(t);
        a.bones = {{"foot_l", 1.0f, 1.0f}, {"foot_r", 2.0f, 0.0f}, {"hips", 1.0f, 0.0f}};
        anim::MotionBuildReport rep;
        const anim::PoseDatabase db = build(a, t, &rep);
        check(mt::groupOf(db, anim::FeatureKind::BoneVel, 3) == nullptr && mt::groupOf(db, anim::FeatureKind::BoneVel, 1) == nullptr,
              "a zero weight drops the group");
        for (usize g = 0; g < db.groups().size(); ++g) {
            const anim::FeatureGroup& fg = db.groups()[g];
            double var = 0, mean = 0;
            for (u32 r = 0; r < db.rows(); ++r)
                for (u32 j = fg.firstDim; j < fg.firstDim + fg.count; ++j) {
                    mean += db.row(r)[j];
                    var += static_cast<double>(db.row(r)[j]) * db.row(r)[j];
                }
            var /= db.rows();
            mean /= db.rows();
            if (var < 1e-9) {
                check(fg.scale == 1.0f, "a constant group (hips position, heading) gets scale 1 and stores zeros");
            } else {
                checkNear(static_cast<f32>(var), fg.weight * fg.weight, 0.01f * fg.weight * fg.weight + 1e-4f,
                          "group " + std::to_string(g) + ": total variance 1 before its weight (weight^2 after)");
                checkNear(static_cast<f32>(mean), 0.0f, 0.02f, "and zero mean");
            }
        }
        // normalise / denormalise invert each other.
        const std::vector<f32> raw = mt::rawRow(db, 40);
        std::vector<f32> back(db.dimPadded());
        db.normalise(raw.data(), back.data());
        f32 worst = 0;
        for (u32 j = 0; j < db.dimPadded(); ++j) worst = std::max(worst, std::fabs(back[j] - db.row(40)[j]));
        check(worst < 1e-4f, "normalise inverts denormalise");
        check(db.groupCost(db.row(40), 40, 0) == 0.0f && db.cost(db.row(40), 40) == db.ranges()[db.meta(40).range].bias, "a row costs nothing against itself");
    }

    AVER_INFO("warnings, failures");
    {
        mt::ClipTable t;
        anim::MotionAsset a = mt::locomotionAsset(t);
        anim::MotionBuildReport rep;
        build(a, t, &rep);
        check(hasWarning(rep, "idle") && hasWarning(rep, "carries no motion"), "an idle root warns that trajectory features are zero");

        a.bones.push_back({"ghost", 1.0f, 1.0f});
        a.clips.push_back(mt::entry("missing"));
        anim::MotionBuildReport rep2;
        bool ok = false;
        const anim::PoseDatabase db = build(a, t, &rep2, &ok);
        check(ok && hasWarning(rep2, "ghost"), "a missing bone warns and its groups are dropped");
        check(rep2.skippedClips == 1 && hasWarning(rep2, "missing"), "a clip that does not resolve is skipped with a warning");
        check(db.groups().size() == 8, "no group was made for the missing bone");

        anim::MotionAsset none = mt::baseAsset();
        none.clips = {mt::entry("absent")};
        build(none, t, nullptr, &ok);
        check(!ok, "zero rows after skipping is a failure");
        anim::MotionAsset bad = mt::baseAsset();
        build(bad, t, nullptr, &ok);
        check(!ok, "an invalid asset is a failure");
        anim::PoseDatabase tmp;
        std::string why;
        anim::MotionAsset rootless = a;
        rootless.rootBone = "no_such_bone";
        check(!anim::buildPoseDatabase(rootless, skel, t.fn(), tmp, nullptr, &why), "an unknown root bone is a failure: " + why);
    }

    AVER_INFO("sidecar");
    {
        mt::ClipTable t;
        anim::MotionAsset a = mt::locomotionAsset(t);
        a.search.accel = anim::SearchAccel::KdTree;
        a.clips[1].ranges.push_back({0.0f, 0.3f, {"stop"}, true});
        a.clips[2].bias = -0.5f;
        const anim::PoseDatabase db = build(a, t);
        check(db.hasKd(), "KdTree accel builds the tree even for a tiny database");

        std::vector<u8> skelBytes;
        fmt::writeOcSkel(skel, skelBytes);
        std::vector<std::vector<u8>> clipBytes;
        for (const char* n : {"idle", "walk", "run"}) {
            std::vector<u8> b;
            fmt::writeOcAnim(*t.find(n), b);
            clipBytes.push_back(std::move(b));
        }
        const u64 key = anim::motionSourceKey(a, skelBytes, clipBytes);
        check(key == anim::motionSourceKey(a, skelBytes, clipBytes), "the source key is deterministic");
        anim::MotionAsset a2 = a;
        a2.clips[0].bias = 1.0f;
        check(anim::motionSourceKey(a2, skelBytes, clipBytes) != key, "the key changes with the asset");
        auto c2 = clipBytes;
        c2[1][c2[1].size() / 2] ^= 1;
        check(anim::motionSourceKey(a, skelBytes, c2) != key, "the key changes with a clip byte");
        auto s2 = skelBytes;
        s2.back() ^= 1;
        check(anim::motionSourceKey(a, s2, clipBytes) != key, "the key changes with the skeleton");

        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "aver-posedb-test";
        std::filesystem::create_directories(dir, ec);
        const std::string path = (dir / "t.ocmbake").string();
        std::string why;
        check(anim::savePoseDatabase(path, db, key, &why), "saves " + why);
        anim::PoseDatabase back;
        u64 gotKey = 0;
        check(anim::loadPoseDatabase(path, back, &gotKey, &why), "loads " + why);
        check(gotKey == key, "the key survives");
        check(back.rows() == db.rows() && back.dim() == db.dim() && back.dimPadded() == db.dimPadded(), "sizes survive");
        bool same = back.groups().size() == db.groups().size();
        for (usize g = 0; same && g < db.groups().size(); ++g)
            same = back.groups()[g].kind == db.groups()[g].kind && back.groups()[g].bone == db.groups()[g].bone &&
                   back.groups()[g].firstDim == db.groups()[g].firstDim && back.groups()[g].count == db.groups()[g].count &&
                   back.groups()[g].weight == db.groups()[g].weight && back.groups()[g].scale == db.groups()[g].scale;
        check(same, "groups survive");
        check(std::memcmp(back.row(0), db.row(0), static_cast<usize>(db.rows()) * db.dimPadded() * 4) == 0, "FEAT is identical");
        bool metaSame = true;
        for (u32 r = 0; r < db.rows(); ++r)
            metaSame &= back.meta(r).tagMask == db.meta(r).tagMask && back.meta(r).range == db.meta(r).range &&
                        back.meta(r).flags == db.meta(r).flags;
        check(metaSame, "row meta survives");
        bool rangesSame = back.ranges().size() == db.ranges().size();
        for (usize i = 0; rangesSame && i < db.ranges().size(); ++i)
            rangesSame = back.ranges()[i].clip == db.ranges()[i].clip && back.ranges()[i].start == db.ranges()[i].start &&
                         back.ranges()[i].end == db.ranges()[i].end && back.ranges()[i].firstRow == db.ranges()[i].firstRow &&
                         back.ranges()[i].loop == db.ranges()[i].loop && back.ranges()[i].bias == db.ranges()[i].bias &&
                         back.ranges()[i].tagMask == db.ranges()[i].tagMask;
        check(rangesSame, "ranges survive");
        check(back.tagBit(fnv1a64(std::string_view("run"))) == db.tagBit(fnv1a64(std::string_view("run"))) &&
              back.boneNames() == db.boneNames() && back.rootBone() == db.rootBone() &&
              back.futureTimes() == db.futureTimes() && back.pastTimes() == db.pastTimes(), "tags, bones and times survive");
        check(back.defaults().search.interval == db.defaults().search.interval &&
              back.defaults().inertHalfLife == db.defaults().inertHalfLife &&
              back.defaults().spring.turnHalfLife == db.defaults().spring.turnHalfLife, "defaults survive");
        check(back.hasKd() && back.kdNodeCount() == db.kdNodeCount(), "the KD tree survives");
        std::vector<f32> q(db.dimPadded());
        for (u32 j = 0; j < db.dim(); ++j) q[j] = std::sin(static_cast<f32>(j) * 1.7f);
        check(back.search(q.data(), {}).row == db.search(q.data(), {}).row && back.search(q.data(), {}).cost == db.search(q.data(), {}).cost,
              "a loaded database answers like the built one");

        // Misses, not crashes.
        const std::vector<u8> bytes = readBytes(path);
        anim::PoseDatabase junk;
        writeBytes((dir / "trunc.ocmbake").string(), std::vector<u8>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2)));
        check(!anim::loadPoseDatabase((dir / "trunc.ocmbake").string(), junk, nullptr), "a truncated sidecar is a miss");
        std::vector<u8> flipped = bytes;
        flipped[flipped.size() * 3 / 4] ^= 0x55;
        writeBytes((dir / "flip.ocmbake").string(), flipped);
        check(!anim::loadPoseDatabase((dir / "flip.ocmbake").string(), junk, nullptr), "a corrupted sidecar is a miss");
        writeBytes((dir / "empty.ocmbake").string(), {});
        check(!anim::loadPoseDatabase((dir / "empty.ocmbake").string(), junk, nullptr), "an empty file is a miss");
        check(!anim::loadPoseDatabase((dir / "absent.ocmbake").string(), junk, nullptr), "a missing file is a miss");

        fmt::Avr1File f;
        check(fmt::loadAvr1(path, f), "the sidecar is a plain AVR1 container");
        check(f.subtype == fmt::avrFourCC("MMDB"), "subtype MMDB");
        for (fmt::AvrChunk& c : f.chunks)
            if (c.id == fmt::avrFourCC("MHDR")) c.data[0] = 0;   // builder version 0
        fmt::saveAvr1((dir / "old.ocmbake").string(), f);
        check(!anim::loadPoseDatabase((dir / "old.ocmbake").string(), junk, nullptr, &why), "an old builder version is a miss: " + why);
        std::filesystem::remove_all(dir, ec);
    }

    AVER_INFO("{} failure(s)", mt::g_failures);
    return mt::g_failures == 0 ? 0 : 1;
}
