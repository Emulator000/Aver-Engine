// Building, baking and loading a PoseDatabase. See PoseDatabase.hpp. Search lives in PoseSearch.cpp.
#include "aver/anim/PoseDatabase.hpp"

#include "aver/anim/AnimSampler.hpp"
#include "aver/anim/RootMotion.hpp"
#include "aver/core/Hash.hpp"
#include "aver/formats/Avr1.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace aver::anim {

namespace {

constexpr u32 kSubtypeMmdb = fmt::avrFourCC("MMDB");
constexpr u32 kChMhdr = fmt::avrFourCC("MHDR"), kChStrt = fmt::avrFourCC("STRT"), kChGrps = fmt::avrFourCC("GRPS"),
              kChNorm = fmt::avrFourCC("NORM"), kChTraj = fmt::avrFourCC("TRAJ"), kChRnge = fmt::avrFourCC("RNGE"),
              kChRowm = fmt::avrFourCC("ROWM"), kChFeat = fmt::avrFourCC("FEAT"), kChPca8 = fmt::avrFourCC("PCA8"),
              kChKdtr = fmt::avrFourCC("KDTR");

f32 wrapPi(f32 a) {
    while (a > kPi) a -= kTwoPi;
    while (a <= -kPi) a += kTwoPi;
    return a;
}

// A planar rigid transform: rotate by yaw about +Z, then translate.
struct Rigid { f32 x = 0, y = 0, yaw = 0; };

Rigid compose(const Rigid& a, const Rigid& b) {   // a o b: b first
    const f32 c = std::cos(a.yaw), s = std::sin(a.yaw);
    return {a.x + c * b.x - s * b.y, a.y + s * b.x + c * b.y, a.yaw + b.yaw};
}
Rigid inverse(const Rigid& a) {
    const f32 c = std::cos(a.yaw), s = std::sin(a.yaw);
    return {-(c * a.x + s * a.y), -(-s * a.x + c * a.y), -a.yaw};
}
Rigid power(const Rigid& g, i32 k) {
    Rigid r, base = k < 0 ? inverse(g) : g;
    for (i32 i = 0, n = k < 0 ? -k : k; i < n; ++i) r = compose(base, r);
    return r;
}

// Samples the planar root and the selected bones of one clip range at any (unwrapped) time. A looping
// range continues its root travel across the seam: lap k applies the seam transform k times.
struct RangeSampler {
    const fmt::OcAnimation& clip;
    const fmt::OcSkeleton& skel;
    u32 rootBone;
    f32 start, end, sr;
    bool loop;
    std::vector<u32> bones;          // skeleton indices of the selected bones, by slot
    Rigid seam;                      // loop: T(end) o T(start)^-1
    Pose pose;
    std::vector<Mat4> model;

    struct Sample { Rigid root; std::vector<Vec3> bone; };
    std::unordered_map<i64, Sample> cache;

    RangeSampler(const fmt::OcAnimation& c, const fmt::OcSkeleton& s, u32 root, f32 st, f32 en, f32 rate, bool lp,
                 std::vector<u32> sel)
        : clip(c), skel(s), rootBone(root), start(st), end(en), sr(rate), loop(lp), bones(std::move(sel)) {
        if (loop) {
            const Rigid a = rootAt(start), b = rootAt(end);
            seam = compose(b, inverse(a));
        }
    }

    Rigid rootAt(f32 t) {
        restPose(skel, pose);
        sampleAnimation(clip, t, pose);
        const Transform& x = pose.local[rootBone];
        return {x.position.x, x.position.y, yawOf(x.rotation)};
    }

    f32 clampT(f32 t) const { return loop ? t : std::min(std::max(t, start), end); }

    const Sample& at(f32 t) {
        const i64 key = std::llround(static_cast<double>(t) * sr * 64.0);
        const auto hit = cache.find(key);
        if (hit != cache.end()) return hit->second;

        i32 lap = 0;
        f32 u = t;
        if (loop) {
            const f32 len = end - start;
            lap = static_cast<i32>(std::floor((t - start) / len));
            u = t - static_cast<f32>(lap) * len;
            u = std::min(std::max(u, start), end);
        } else {
            u = clampT(t);
        }
        restPose(skel, pose);
        sampleAnimation(clip, u, pose);
        const Transform& rt = pose.local[rootBone];
        poseToModel(skel, pose, model);
        const Rigid g = lap == 0 ? Rigid{} : power(seam, lap);

        Sample s;
        s.root = compose(g, Rigid{rt.position.x, rt.position.y, yawOf(rt.rotation)});
        s.bone.reserve(bones.size());
        const f32 c = std::cos(g.yaw), sn = std::sin(g.yaw);
        for (const u32 b : bones) {
            const Mat4& m = model[b];
            const f32 px = m.m[3][0], py = m.m[3][1];
            s.bone.push_back(Vec3{g.x + c * px - sn * py, g.y + sn * px + c * py, m.m[3][2]});
        }
        return cache.emplace(key, std::move(s)).first->second;
    }
};

// Heading-frame helpers: rotate a planar offset by -yaw.
inline void toHeading(f32 yaw, f32 dx, f32 dy, f32& hx, f32& hy) {
    const f32 c = std::cos(yaw), s = std::sin(yaw);
    hx = c * dx + s * dy;
    hy = -s * dx + c * dy;
}

struct Writer {
    std::vector<u8> b;
    template <class T> void put(T v) {
        const u8* s = reinterpret_cast<const u8*>(&v);
        b.insert(b.end(), s, s + sizeof(T));
    }
    void bytes(const void* p, usize n) {
        const u8* s = static_cast<const u8*>(p);
        b.insert(b.end(), s, s + n);
    }
};

struct Reader {
    const u8* p;
    usize n;
    usize o = 0;
    bool ok = true;
    template <class T> T get() {
        T v{};
        if (!ok || o + sizeof(T) > n) { ok = false; return v; }
        std::memcpy(&v, p + o, sizeof(T));
        o += sizeof(T);
        return v;
    }
    bool bytes(void* dst, usize len) {
        if (!ok || len > n - o) { ok = false; return false; }
        std::memcpy(dst, p + o, len);
        o += len;
        return true;
    }
    usize left() const { return n - o; }
};

} // namespace

// ---- PoseDatabase accessors ------------------------------------------------------------------------

PoseDatabase::PoseDatabase() = default;
PoseDatabase::~PoseDatabase() = default;
PoseDatabase::PoseDatabase(PoseDatabase&&) noexcept = default;
PoseDatabase& PoseDatabase::operator=(PoseDatabase&&) noexcept = default;

u32 PoseDatabase::tagBit(u64 nameHash) const {
    for (usize i = 0; i < tagHashes_.size(); ++i)
        if (tagHashes_[i] == nameHash) return 1u << i;
    return 0;
}

u32 PoseDatabase::tagMaskOf(const u64* hashes, u32 n) const {
    u32 m = 0;
    for (u32 i = 0; i < n; ++i)
        if (hashes[i]) m |= tagBit(hashes[i]);
    return m;
}

u32 PoseDatabase::rowOf(u32 range, f32 clipSeconds) const {
    if (range >= ranges_.size() || ranges_[range].rowCount == 0) return kNoRow;
    const MotionRange& r = ranges_[range];
    const f32 k = std::floor((clipSeconds - r.start) * sampleRate_ + 1e-3f);
    const i64 i = std::min<i64>(std::max<i64>(static_cast<i64>(k), 0), static_cast<i64>(r.rowCount) - 1);
    return r.firstRow + static_cast<u32>(i);
}

f32 PoseDatabase::timeOf(u32 rowIndex) const {
    if (rowIndex >= rows_) return 0.0f;
    const MotionRange& r = ranges_[meta_[rowIndex].range];
    return r.start + static_cast<f32>(rowIndex - r.firstRow) / sampleRate_;
}

void PoseDatabase::normalise(const f32* raw, f32* out) const {
    std::fill(out, out + dimPadded_, 0.0f);
    for (const FeatureGroup& g : groups_) {
        const f32 k = g.weight / g.scale;
        for (u32 j = g.firstDim; j < g.firstDim + g.count; ++j) out[j] = (raw[j] - mean_[j]) * k;
    }
}

void PoseDatabase::denormalise(const f32* v, f32* out) const {
    std::fill(out, out + dimPadded_, 0.0f);
    for (const FeatureGroup& g : groups_) {
        const f32 k = g.weight > 0.0f ? g.scale / g.weight : 0.0f;
        for (u32 j = g.firstDim; j < g.firstDim + g.count; ++j) out[j] = v[j] * k + mean_[j];
    }
}

f32 PoseDatabase::groupCost(const f32* q, u32 rowIndex, u32 group) const {
    if (rowIndex >= rows_ || group >= groups_.size()) return 0.0f;
    const FeatureGroup& g = groups_[group];
    const f32* a = row(rowIndex);
    f32 s = 0.0f;
    for (u32 j = g.firstDim; j < g.firstDim + g.count; ++j) {
        const f32 d = q[j] - a[j];
        s += d * d;
    }
    return s;
}

f32 PoseDatabase::cost(const f32* q, u32 rowIndex) const {
    if (rowIndex >= rows_) return std::numeric_limits<f32>::infinity();
    return distance(q, rowIndex, std::numeric_limits<f32>::infinity(), false) + rangeBias_[meta_[rowIndex].range];
}

u64 PoseDatabase::memoryBytes() const {
    return static_cast<u64>(rows_) * dimPadded_ * sizeof(f32) + meta_.size() * sizeof(RowMeta) +
           pca_.size() * sizeof(f32) + kdNodes_.size() * sizeof(KdNode) + kdOrder_.size() * sizeof(u32);
}

// ---- build -----------------------------------------------------------------------------------------

bool buildPoseDatabase(const MotionAsset& asset, const fmt::OcSkeleton& skel, const MotionClipFn& clips,
                       PoseDatabase& out, MotionBuildReport* report, std::string* why) {
    const auto t0 = std::chrono::steady_clock::now();
    auto fail = [&](const std::string& m) { if (why) *why = m; return false; };
    MotionBuildReport rep;
    auto warn = [&](const std::string& m) { rep.warnings.push_back(m); };

    std::string reason;
    if (!asset.valid(&reason)) return fail("invalid motion asset: " + reason);
    if (skel.bones.empty()) return fail("the skeleton has no bones");

    u32 rootBone = kNoBone;
    if (asset.rootBone.empty()) {
        rootBone = rootBoneOf(skel);
    } else {
        for (usize i = 0; i < skel.bones.size(); ++i)
            if (skel.bones[i].name == asset.rootBone) { rootBone = static_cast<u32>(i); break; }
        if (rootBone == kNoBone) return fail("root bone '" + asset.rootBone + "' is not in the skeleton");
    }
    if (rootBone == kNoBone) return fail("the skeleton has no root bone");

    PoseDatabase db;
    db.sampleRate_ = static_cast<f32>(asset.sampleRate);
    db.rootBone_ = rootBone;
    db.futureTimes_ = asset.futureTimes;
    db.pastTimes_ = asset.pastTimes;
    for (const fmt::OcBone& b : skel.bones) db.boneNames_.push_back(b.name);
    db.tagNames_ = asset.tags;
    for (const std::string& t : asset.tags) db.tagHashes_.push_back(fnv1a64(std::string_view(t)));
    db.defaults_ = asset;
    db.defaults_.clips.clear();

    // Feature layout, in file order: per bone pos then vel, root velocity, future pos / dir, past pos.
    const u32 nF = static_cast<u32>(asset.futureTimes.size()), nP = static_cast<u32>(asset.pastTimes.size());
    u32 dim = 0;
    auto addGroup = [&](FeatureKind kind, u32 bone, u32 count, f32 weight) {
        FeatureGroup g;
        g.kind = kind;
        g.bone = bone;
        g.firstDim = dim;
        g.count = count;
        g.weight = weight;
        dim += count;
        db.groups_.push_back(g);
    };
    std::vector<u32> selBones;
    std::vector<i32> slotOf(skel.bones.size(), -1);
    for (const MotionBoneFeature& bf : asset.bones) {
        u32 bi = kNoBone;
        for (usize i = 0; i < skel.bones.size(); ++i)
            if (skel.bones[i].name == bf.bone) { bi = static_cast<u32>(i); break; }
        if (bi == kNoBone) { warn("feature bone '" + bf.bone + "' is not in the skeleton; its groups are dropped"); continue; }
        if (slotOf[bi] < 0) { slotOf[bi] = static_cast<i32>(selBones.size()); selBones.push_back(bi); }
        if (bf.pos > 0.0f) addGroup(FeatureKind::BonePos, bi, 3, bf.pos);
        if (bf.vel > 0.0f) addGroup(FeatureKind::BoneVel, bi, 3, bf.vel);
    }
    if (asset.rootVelocity > 0.0f) addGroup(FeatureKind::RootVel, kNoBone, 3, asset.rootVelocity);
    if (asset.futurePos > 0.0f && nF) addGroup(FeatureKind::FuturePos, kNoBone, 2 * nF, asset.futurePos);
    if (asset.futureDir > 0.0f && nF) addGroup(FeatureKind::FutureDir, kNoBone, 2 * nF, asset.futureDir);
    if (asset.pastPos > 0.0f && nP) addGroup(FeatureKind::PastPos, kNoBone, 2 * nP, asset.pastPos);
    if (db.groups_.empty()) return fail("no feature group survives (every feature bone is missing)");
    const bool hasFuturePos = std::any_of(db.groups_.begin(), db.groups_.end(),
                                          [](const FeatureGroup& g) { return g.kind == FeatureKind::FuturePos; });
    const bool hasFuture = hasFuturePos || std::any_of(db.groups_.begin(), db.groups_.end(),
                                          [](const FeatureGroup& g) { return g.kind == FeatureKind::FutureDir; });
    const f32 maxFuture = nF ? asset.futureTimes.back() : 0.0f;

    db.dim_ = dim;
    db.dimPadded_ = std::max<u32>(8, (dim + 7) / 8 * 8);
    const f32 sr = db.sampleRate_;

    std::vector<f32> raw;                 // rows * dim, before normalisation
    std::vector<RowMeta> meta;
    for (usize ei = 0; ei < asset.clips.size(); ++ei) {
        const MotionClipEntry& e = asset.clips[ei];
        const fmt::OcAnimation* clip = clips ? clips(e.clip) : nullptr;
        if (!clip) { ++rep.skippedClips; warn("clip '" + e.clip + "' did not resolve; entry skipped"); continue; }
        if (clip->duration <= 0.0f) { ++rep.skippedClips; warn("clip '" + e.clip + "' has no duration; entry skipped"); continue; }

        const f32 start = e.start;
        const f32 end = e.end < 0.0f ? clip->duration : std::min(e.end, clip->duration);
        if (!(end > start)) { ++rep.skippedClips; warn("clip '" + e.clip + "': the range is empty; entry skipped"); continue; }
        const bool loop = e.loop < 0 ? (clip->flags & fmt::kOcAnimLoop) != 0 : e.loop == 1;
        const u32 n = loop ? std::max<u32>(1, static_cast<u32>(std::llround((end - start) * sr)))
                           : static_cast<u32>(std::floor((end - start) * sr + 1e-4f)) + 1;

        u32 entryTags = 0;
        for (const std::string& t : e.tags) entryTags |= 1u << asset.tagIndex(t);

        MotionRange mr;
        mr.clip = e.clip;
        mr.start = start;
        mr.end = end;
        mr.firstRow = static_cast<u32>(meta.size());
        mr.rowCount = n;
        mr.loop = loop;
        mr.tagMask = entryTags;
        mr.bias = e.bias;
        mr.rootBone = rootBone;
        const u16 rangeIndex = static_cast<u16>(db.ranges_.size());

        bool hasTrack = false;
        for (const fmt::OcTrack& t : clip->tracks) hasTrack |= t.boneIndex == rootBone;
        RangeSampler rs(*clip, skel, rootBone, start, end, sr, loop, selBones);
        const f32 h = 1.0f / sr;

        raw.resize(raw.size() + static_cast<usize>(n) * dim);
        f32 pathLen = 0.0f;
        Rigid prevRoot;
        for (u32 k = 0; k < n; ++k) {
            const f32 t = start + static_cast<f32>(k) / sr;
            f32* v = raw.data() + (static_cast<usize>(mr.firstRow) + k) * dim;
            const RangeSampler::Sample& c0 = rs.at(t);      // element references survive later inserts
            const f32 yaw0 = c0.root.yaw;
            f32 tm = rs.clampT(t - h), tp = rs.clampT(t + h);
            const f32 span = tp - tm;
            const RangeSampler::Sample& cm = rs.at(tm);
            const RangeSampler::Sample& cp = rs.at(tp);
            if (k) pathLen += std::hypot(c0.root.x - prevRoot.x, c0.root.y - prevRoot.y);
            prevRoot = c0.root;

            for (const FeatureGroup& g : db.groups_) {
                f32* o = v + g.firstDim;
                switch (g.kind) {
                    case FeatureKind::BonePos: {
                        const Vec3& p = c0.bone[static_cast<usize>(slotOf[g.bone])];
                        toHeading(yaw0, p.x - c0.root.x, p.y - c0.root.y, o[0], o[1]);
                        o[2] = p.z;
                        break;
                    }
                    case FeatureKind::BoneVel: {
                        const usize s = static_cast<usize>(slotOf[g.bone]);
                        if (span > 1e-6f) {
                            const Vec3 d = (cp.bone[s] - cm.bone[s]) / span;
                            toHeading(yaw0, d.x, d.y, o[0], o[1]);
                            o[2] = d.z;
                        } else {
                            o[0] = o[1] = o[2] = 0.0f;
                        }
                        break;
                    }
                    case FeatureKind::RootVel: {
                        if (span > 1e-6f) {
                            toHeading(yaw0, (cp.root.x - cm.root.x) / span, (cp.root.y - cm.root.y) / span, o[0], o[1]);
                            o[2] = wrapPi(cp.root.yaw - cm.root.yaw) / span;
                        } else {
                            o[0] = o[1] = o[2] = 0.0f;
                        }
                        break;
                    }
                    case FeatureKind::FuturePos:
                    case FeatureKind::FutureDir:
                    case FeatureKind::PastPos: {
                        const std::vector<f32>& times = g.kind == FeatureKind::PastPos ? asset.pastTimes : asset.futureTimes;
                        for (usize i = 0; i < times.size(); ++i) {
                            const RangeSampler::Sample& cs = rs.at(rs.clampT(t + times[i]));
                            if (g.kind == FeatureKind::FutureDir) {
                                const f32 dy = cs.root.yaw - yaw0;
                                o[2 * i] = std::cos(dy);
                                o[2 * i + 1] = std::sin(dy);
                            } else {
                                toHeading(yaw0, cs.root.x - c0.root.x, cs.root.y - c0.root.y, o[2 * i], o[2 * i + 1]);
                            }
                        }
                        break;
                    }
                }
            }

            RowMeta rm;
            rm.range = rangeIndex;
            rm.tagMask = entryTags;
            if (e.noSearch) rm.flags |= kRowNoSearch;
            for (const MotionTagRange& r : e.ranges) {
                const f32 rEnd = r.end < 0.0f ? end : r.end;
                if (t + 1e-4f < r.start || t >= rEnd - 1e-4f) continue;
                for (const std::string& tg : r.tags) rm.tagMask |= 1u << asset.tagIndex(tg);
                if (r.noSearch) rm.flags |= kRowNoSearch;
            }
            if (!loop && hasFuture && t + maxFuture > end + 1e-4f) rm.flags |= kRowTail;
            meta.push_back(rm);
        }
        if (!hasTrack) warn("clip '" + e.clip + "': the root bone has no track; trajectory features are zero");
        else if (hasFuturePos && pathLen < 1.0f)
            warn("clip '" + e.clip + "': root carries no motion; trajectory features are zero");
        db.ranges_.push_back(mr);
        db.rangeBias_.push_back(e.bias);
    }

    const u32 rows = static_cast<u32>(meta.size());
    if (rows == 0) return fail("no rows: every clip entry was skipped");
    if (db.ranges_.size() > 0xFFFFu) return fail("more than 65535 clip ranges");
    db.rows_ = rows;
    db.meta_ = std::move(meta);

    // Normalise: mean over ALL rows, each group scaled by its total standard deviation so every group has
    // unit variance before its weight, however many components it has.
    std::vector<double> mean(db.dimPadded_, 0.0), var(db.dimPadded_, 0.0);
    for (u32 r = 0; r < rows; ++r)
        for (u32 j = 0; j < dim; ++j) mean[j] += raw[static_cast<usize>(r) * dim + j];
    for (u32 j = 0; j < dim; ++j) mean[j] /= rows;
    for (u32 r = 0; r < rows; ++r)
        for (u32 j = 0; j < dim; ++j) {
            const double d = raw[static_cast<usize>(r) * dim + j] - mean[j];
            var[j] += d * d;
        }
    db.mean_.assign(db.dimPadded_, 0.0f);
    for (u32 j = 0; j < dim; ++j) db.mean_[j] = static_cast<f32>(mean[j]);
    for (FeatureGroup& g : db.groups_) {
        double s = 0.0;
        for (u32 j = g.firstDim; j < g.firstDim + g.count; ++j) s += var[j] / rows;
        s = std::sqrt(s);
        g.scale = s < 1e-4 ? 1.0f : static_cast<f32>(s);
    }
    db.feat_.reset(PoseDatabase::allocAligned(static_cast<usize>(rows) * db.dimPadded_));
    std::fill(db.feat_.get(), db.feat_.get() + static_cast<usize>(rows) * db.dimPadded_, 0.0f);
    for (u32 r = 0; r < rows; ++r)
        db.normalise(raw.data() + static_cast<usize>(r) * dim, db.feat_.get() + static_cast<usize>(r) * db.dimPadded_);
    raw.clear();

    const bool wantKd = rows >= asset.search.kdMinRows || asset.search.accel == SearchAccel::KdTree;
    if (wantKd && asset.search.accel != SearchAccel::Linear) db.buildKd();

    rep.rows = rows;
    rep.ranges = static_cast<u32>(db.ranges_.size());
    rep.bytes = db.memoryBytes();
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    out = std::move(db);
    if (report) *report = std::move(rep);
    return true;
}

// ---- sidecar ---------------------------------------------------------------------------------------

u64 motionSourceKey(const MotionAsset& asset, const std::vector<u8>& skeletonBytes,
                    const std::vector<std::vector<u8>>& clipBytes) {
    const std::string text = writeMotionAsset(asset);
    std::vector<u64> parts{kMotionBuildVersion, fmt::avrHash64(text.data(), text.size()),
                           fmt::avrHash64(skeletonBytes.data(), skeletonBytes.size()), clipBytes.size()};
    for (const std::vector<u8>& c : clipBytes) parts.push_back(fmt::avrHash64(c.data(), c.size()));
    return fmt::avrHash64(parts.data(), parts.size() * sizeof(u64));
}

bool savePoseDatabase(const std::string& path, const PoseDatabase& db, u64 sourceKey, std::string* why) {
    fmt::AvrStringTable table;
    std::vector<u32> tagRefs, boneRefs, clipRefs;
    for (const std::string& t : db.tagNames_) tagRefs.push_back(table.add(t));
    for (const std::string& b : db.boneNames_) boneRefs.push_back(table.add(b));
    for (const MotionRange& r : db.ranges_) clipRefs.push_back(table.add(r.clip));

    fmt::Avr1File f;
    f.subtype = kSubtypeMmdb;
    f.contentVersion = 1;
    f.minReaderVersion = 1;
    f.alignLog2 = 5;

    Writer h;
    h.put<u32>(kMotionBuildVersion);
    h.put<u32>(db.dim_);
    h.put<u32>(db.dimPadded_);
    h.put<u32>(db.rows_);
    h.put<u32>(static_cast<u32>(db.ranges_.size()));
    h.put<u32>(static_cast<u32>(db.tagNames_.size()));
    h.put<u32>(static_cast<u32>(db.groups_.size()));
    h.put<u32>(0);   // flags
    h.put<f32>(db.sampleRate_);
    h.put<u64>(sourceKey);
    h.put<f32>(db.minBias_);
    h.put<u32>(static_cast<u32>(db.kdNodes_.size()));
    h.put<u32>(db.kdNodes_.empty() ? 0 : kKdDims);
    h.put<u32>(static_cast<u32>(db.boneNames_.size()));   // reserved[0]: bone count
    h.put<u32>(db.rootBone_);                             // reserved[1]
    h.put<u32>(static_cast<u32>(db.kdOrder_.size()));     // reserved[2]
    f.add(kChMhdr, std::move(h.b), fmt::kAvrChunkRequired);

    Writer s;
    s.put<u32>(static_cast<u32>(table.bytes().size()));
    s.bytes(table.bytes().data(), table.bytes().size());
    while (s.b.size() % 4) s.b.push_back(0);
    for (const u32 r : tagRefs) s.put<u32>(r);
    for (const u32 r : boneRefs) s.put<u32>(r);
    f.add(kChStrt, std::move(s.b), fmt::kAvrChunkRequired);

    Writer g;
    for (const FeatureGroup& fg : db.groups_) {
        g.put<u32>(static_cast<u32>(fg.kind));
        g.put<u32>(fg.bone);
        g.put<u32>(fg.firstDim);
        g.put<u32>(fg.count);
        g.put<f32>(fg.weight);
        g.put<f32>(fg.scale);
    }
    f.add(kChGrps, std::move(g.b), fmt::kAvrChunkRequired);

    Writer nm;
    nm.bytes(db.mean_.data(), db.mean_.size() * sizeof(f32));
    f.add(kChNorm, std::move(nm.b), fmt::kAvrChunkRequired);

    Writer tj;
    tj.put<u32>(static_cast<u32>(db.futureTimes_.size()));
    tj.put<u32>(static_cast<u32>(db.pastTimes_.size()));
    for (const f32 t : db.futureTimes_) tj.put<f32>(t);
    for (const f32 t : db.pastTimes_) tj.put<f32>(t);
    const MotionAsset& d = db.defaults_;
    tj.put<f32>(d.search.interval);
    tj.put<f32>(d.search.switchMargin);
    tj.put<f32>(d.search.acceptCost);
    tj.put<f32>(d.search.continuityWindow);
    tj.put<u32>(static_cast<u32>(d.search.accel));
    tj.put<u32>(d.search.kdMinRows);
    tj.put<f32>(d.spring.velocityHalfLife);
    tj.put<f32>(d.spring.turnHalfLife);
    tj.put<f32>(d.spring.historySeconds);
    tj.put<f32>(d.spring.faceSpeedMin);
    tj.put<f32>(d.inertHalfLife);
    f.add(kChTraj, std::move(tj.b), fmt::kAvrChunkRequired);

    Writer rg;
    for (usize i = 0; i < db.ranges_.size(); ++i) {
        const MotionRange& r = db.ranges_[i];
        rg.put<u32>(clipRefs[i]);
        rg.put<f32>(r.start);
        rg.put<f32>(r.end);
        rg.put<u32>(r.firstRow);
        rg.put<u32>(r.rowCount);
        rg.put<u32>(r.loop ? 1u : 0u);
        rg.put<u32>(r.tagMask);
        rg.put<f32>(r.bias);
        rg.put<u32>(r.rootBone);
    }
    f.add(kChRnge, std::move(rg.b), fmt::kAvrChunkRequired);

    Writer rm;
    for (const RowMeta& m : db.meta_) {
        rm.put<u32>(m.tagMask);
        rm.put<u16>(m.range);
        rm.put<u8>(m.flags);
        rm.put<u8>(0);
    }
    f.add(kChRowm, std::move(rm.b), fmt::kAvrChunkRequired);

    Writer ft;
    ft.bytes(db.feat_.get(), static_cast<usize>(db.rows_) * db.dimPadded_ * sizeof(f32));
    f.add(kChFeat, std::move(ft.b), fmt::kAvrChunkRequired);

    if (!db.kdNodes_.empty()) {
        Writer pc;
        pc.bytes(db.pca_.data(), db.pca_.size() * sizeof(f32));
        f.add(kChPca8, std::move(pc.b));
        Writer kd;
        kd.bytes(db.kdNodes_.data(), db.kdNodes_.size() * sizeof(KdNode));
        kd.bytes(db.kdOrder_.data(), db.kdOrder_.size() * sizeof(u32));
        f.add(kChKdtr, std::move(kd.b));
    }
    return fmt::saveAvr1(path, f, why);
}

bool loadPoseDatabase(const std::string& path, PoseDatabase& out, u64* sourceKey, std::string* why) {
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    fmt::Avr1File f;
    if (!fmt::loadAvr1(path, f, why)) return false;
    if (f.subtype != kSubtypeMmdb) return fail("not a motion database sidecar");
    const fmt::AvrChunk* mh = f.find(kChMhdr);
    const fmt::AvrChunk* st = f.find(kChStrt);
    const fmt::AvrChunk* gp = f.find(kChGrps);
    const fmt::AvrChunk* nm = f.find(kChNorm);
    const fmt::AvrChunk* tj = f.find(kChTraj);
    const fmt::AvrChunk* rg = f.find(kChRnge);
    const fmt::AvrChunk* rw = f.find(kChRowm);
    const fmt::AvrChunk* ft = f.find(kChFeat);
    if (!mh || !st || !gp || !nm || !tj || !rg || !rw || !ft) return fail("a required chunk is missing");

    Reader h{mh->data.data(), mh->data.size()};
    if (h.get<u32>() != kMotionBuildVersion) return fail("builder version mismatch");
    PoseDatabase db;
    db.dim_ = h.get<u32>();
    db.dimPadded_ = h.get<u32>();
    const u32 rows = h.get<u32>(), nRanges = h.get<u32>(), nTags = h.get<u32>(), nGroups = h.get<u32>();
    h.get<u32>();   // flags
    db.sampleRate_ = h.get<f32>();
    const u64 key = h.get<u64>();
    db.minBias_ = h.get<f32>();
    const u32 nKd = h.get<u32>(), kdDims = h.get<u32>(), nBones = h.get<u32>();
    db.rootBone_ = h.get<u32>();
    const u32 nOrder = h.get<u32>();
    if (!h.ok) return fail("truncated header");
    if (db.dimPadded_ < 8 || db.dimPadded_ % 8 || db.dim_ > db.dimPadded_ || db.dimPadded_ > 4096 || rows == 0 ||
        rows > 50'000'000u || nRanges == 0 || nRanges > 0xFFFFu || nTags > kMaxMotionTags || nGroups == 0 ||
        nGroups > 4096 || nBones > 100'000 || !(db.sampleRate_ > 0.0f))
        return fail("implausible header");
    if (static_cast<usize>(rows) * db.dimPadded_ * sizeof(f32) != ft->data.size()) return fail("FEAT size mismatch");
    if (static_cast<usize>(rows) * 8 != rw->data.size()) return fail("ROWM size mismatch");
    if (static_cast<usize>(db.dimPadded_) * sizeof(f32) != nm->data.size()) return fail("NORM size mismatch");

    Reader s{st->data.data(), st->data.size()};
    const u32 tableBytes = s.get<u32>();
    if (!s.ok || tableBytes > s.left()) return fail("bad string table");
    std::vector<u8> tb(tableBytes);
    s.bytes(tb.data(), tableBytes);
    s.o = (s.o + 3) & ~usize(3);
    fmt::AvrStringTable table;
    table.setBytes(std::move(tb));
    for (u32 i = 0; i < nTags; ++i) {
        const std::string n(table.get(s.get<u32>()));
        db.tagNames_.push_back(n);
        db.tagHashes_.push_back(fnv1a64(std::string_view(n)));
    }
    for (u32 i = 0; i < nBones; ++i) db.boneNames_.emplace_back(table.get(s.get<u32>()));
    if (!s.ok) return fail("truncated string refs");

    Reader g{gp->data.data(), gp->data.size()};
    for (u32 i = 0; i < nGroups; ++i) {
        FeatureGroup fg;
        const u32 kind = g.get<u32>();
        fg.kind = static_cast<FeatureKind>(kind);
        fg.bone = g.get<u32>();
        fg.firstDim = g.get<u32>();
        fg.count = g.get<u32>();
        fg.weight = g.get<f32>();
        fg.scale = g.get<f32>();
        if (!g.ok || kind > 5 || fg.firstDim + fg.count > db.dim_) return fail("bad feature group");
        db.groups_.push_back(fg);
    }

    db.mean_.resize(db.dimPadded_);
    std::memcpy(db.mean_.data(), nm->data.data(), nm->data.size());

    Reader t{tj->data.data(), tj->data.size()};
    const u32 nf = t.get<u32>(), np = t.get<u32>();
    if (nf > kMaxTrajectorySamples || np > kMaxTrajectorySamples) return fail("bad trajectory counts");
    for (u32 i = 0; i < nf; ++i) db.futureTimes_.push_back(t.get<f32>());
    for (u32 i = 0; i < np; ++i) db.pastTimes_.push_back(t.get<f32>());
    MotionAsset& d = db.defaults_;
    d.sampleRate = static_cast<u32>(std::lround(db.sampleRate_));
    d.tags = db.tagNames_;
    d.futureTimes = db.futureTimes_;
    d.pastTimes = db.pastTimes_;
    d.search.interval = t.get<f32>();
    d.search.switchMargin = t.get<f32>();
    d.search.acceptCost = t.get<f32>();
    d.search.continuityWindow = t.get<f32>();
    d.search.accel = static_cast<SearchAccel>(t.get<u32>());
    d.search.kdMinRows = t.get<u32>();
    d.spring.velocityHalfLife = t.get<f32>();
    d.spring.turnHalfLife = t.get<f32>();
    d.spring.historySeconds = t.get<f32>();
    d.spring.faceSpeedMin = t.get<f32>();
    d.inertHalfLife = t.get<f32>();
    if (!t.ok) return fail("truncated trajectory chunk");

    Reader r{rg->data.data(), rg->data.size()};
    u32 rowSum = 0;
    for (u32 i = 0; i < nRanges; ++i) {
        MotionRange mr;
        mr.clip = std::string(table.get(r.get<u32>()));
        mr.start = r.get<f32>();
        mr.end = r.get<f32>();
        mr.firstRow = r.get<u32>();
        mr.rowCount = r.get<u32>();
        mr.loop = (r.get<u32>() & 1u) != 0;
        mr.tagMask = r.get<u32>();
        mr.bias = r.get<f32>();
        mr.rootBone = r.get<u32>();
        if (!r.ok || mr.firstRow != rowSum || mr.rowCount == 0) return fail("bad range table");
        rowSum += mr.rowCount;
        db.rangeBias_.push_back(mr.bias);
        db.ranges_.push_back(std::move(mr));
    }
    if (rowSum != rows) return fail("range rows do not add up");

    db.meta_.resize(rows);
    Reader m{rw->data.data(), rw->data.size()};
    for (u32 i = 0; i < rows; ++i) {
        db.meta_[i].tagMask = m.get<u32>();
        db.meta_[i].range = m.get<u16>();
        db.meta_[i].flags = m.get<u8>();
        m.get<u8>();
        if (db.meta_[i].range >= nRanges) return fail("row names a missing range");
    }

    db.rows_ = rows;
    db.feat_.reset(PoseDatabase::allocAligned(static_cast<usize>(rows) * db.dimPadded_));
    std::memcpy(db.feat_.get(), ft->data.data(), ft->data.size());

    if (nKd) {
        const fmt::AvrChunk* pc = f.find(kChPca8);
        const fmt::AvrChunk* kd = f.find(kChKdtr);
        if (!pc || !kd || kdDims != kKdDims) return fail("KD chunks missing");
        if (pc->data.size() != static_cast<usize>(kKdDims) * db.dimPadded_ * sizeof(f32) ||
            kd->data.size() != static_cast<usize>(nKd) * sizeof(KdNode) + static_cast<usize>(nOrder) * sizeof(u32))
            return fail("KD chunk size mismatch");
        db.pca_.resize(static_cast<usize>(kKdDims) * db.dimPadded_);
        std::memcpy(db.pca_.data(), pc->data.data(), pc->data.size());
        db.kdNodes_.resize(nKd);
        std::memcpy(db.kdNodes_.data(), kd->data.data(), static_cast<usize>(nKd) * sizeof(KdNode));
        db.kdOrder_.resize(nOrder);
        std::memcpy(db.kdOrder_.data(), kd->data.data() + static_cast<usize>(nKd) * sizeof(KdNode),
                    static_cast<usize>(nOrder) * sizeof(u32));
        for (const KdNode& nd : db.kdNodes_)
            if (nd.left >= nKd || nd.right >= nKd || nd.rowBegin > nd.rowEnd || nd.rowEnd > nOrder)
                return fail("KD node out of range");
        for (const u32 o : db.kdOrder_)
            if (o >= rows) return fail("KD order out of range");
    }
    if (sourceKey) *sourceKey = key;
    out = std::move(db);
    return true;
}

} // namespace aver::anim
