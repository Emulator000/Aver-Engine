// The retargeter. See Retarget.hpp for the method; the conventions (Hamilton quaternions, row-vector matrices)
// are IkMath.hpp's, and every local<->model rotation conversion here goes through conj(parent) * wanted.
#include "aver/anim/Retarget.hpp"

#include "IkMath.hpp"
#include "aver/anim/AnimSampler.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace aver::anim {

using namespace ikm;

namespace {

i32 boneNamed(const fmt::OcSkeleton& s, const std::string& n) {
    for (usize i = 0; i < s.bones.size(); ++i)
        if (s.bones[i].name == n) return static_cast<i32>(i);
    return -1;
}

bool isAncestorOf(const fmt::OcSkeleton& s, i32 anc, i32 b) {
    for (usize guard = 0; b >= 0 && guard <= s.bones.size(); ++guard) {
        if (b == anc) return true;
        b = s.bones[static_cast<usize>(b)].parent;
    }
    return false;
}

// start..end, root first. False when start is not an ancestor-or-self of end.
bool pathBones(const fmt::OcSkeleton& s, i32 start, i32 end, std::vector<u32>& out) {
    out.clear();
    if (start < 0 || end < 0) return false;
    for (i32 k = end; k >= 0; k = s.bones[static_cast<usize>(k)].parent) {
        out.push_back(static_cast<u32>(k));
        if (k == start) { std::reverse(out.begin(), out.end()); return true; }
    }
    out.clear();
    return false;
}

Vec3 xformPoint(const Vec3& v, const Mat4& m) { return transformDir(v, m) + originOf(m); }

void restModels(const fmt::OcSkeleton& s, std::vector<Mat4>& model, std::vector<Vec3>& pos, std::vector<Quat>& q) {
    Pose p;
    restPose(s, p);
    poseToModel(s, p, model);
    pos.resize(model.size());
    q.resize(model.size());
    for (usize i = 0; i < model.size(); ++i) {
        pos[i] = originOf(model[i]);
        q[i] = modelRotation(model[i]);
    }
}

// Cumulative-length fractions of a chain's bones: per bone the centre of its own segment, the last bone at 1.
void centres(const std::vector<u32>& bones, const std::vector<Vec3>& restPos, std::vector<f32>& out, f32& total) {
    const usize n = bones.size();
    out.assign(n, 1.0f);
    total = 0.0f;
    if (n < 2) return;
    std::vector<f32> cum(n, 0.0f);
    for (usize k = 1; k < n; ++k) cum[k] = cum[k - 1] + lengthOf(restPos[bones[k]] - restPos[bones[k - 1]]);
    total = cum[n - 1];
    for (usize k = 0; k + 1 < n; ++k)
        out[k] = total > kEps ? 0.5f * (cum[k] + cum[k + 1]) / total : (static_cast<f32>(k) + 0.5f) / static_cast<f32>(n - 1);
    out[n - 1] = 1.0f;
}

// Angle between two rotations. atan2 of the relative rotation, not acos of the dot: f32 acos near 1 has a
// noise floor of about 0.04 degrees, above the key-reduction tolerance.
f32 quatAngle(const Quat& a, const Quat& b) {
    const Quat r = a * conj(b);
    return 2.0f * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::fabs(r.w));
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::string pathStem(const std::string& path) {
    usize slash = path.find_last_of("/\\");
    std::string f = slash == std::string::npos ? path : path.substr(slash + 1);
    const usize dot = f.find_last_of('.');
    if (dot != std::string::npos && dot > 0) f = f.substr(0, dot);
    return f;
}

bool Retargeter::bind(const RetargetAsset& a, const fmt::OcSkeleton& src, const fmt::OcSkeleton& dst,
                      std::vector<RetargetIssue>* issues) {
    ok_ = false;
    chains_.clear();
    mapped_.clear();
    translated_.clear();
    frame_ = false;
    auto note = [&](RetargetIssue::Kind k, std::string t) { if (issues) issues->push_back({k, std::move(t)}); };

    std::string why;
    if (src.bones.empty() || dst.bones.empty()) { note(RetargetIssue::Kind::Error, "a skeleton has no bones"); return false; }
    if (!a.valid(src, dst, &why)) { note(RetargetIssue::Kind::Error, why); return false; }
    src_ = src;
    dst_ = dst;
    sourceStem_ = pathStem(a.source);
    targetStem_ = pathStem(a.target);
    dstRole_.assign(dst_.bones.size(), BoneRole{});

    std::vector<Mat4> srcRestModel, dstRestModel;
    restModels(src_, srcRestModel, srcRestPos_, srcRestQ_);
    restModels(dst_, dstRestModel, dstRestPos_, dstRestQ_);

    srcPelvis_ = a.root.srcBone.empty() ? -1 : boneNamed(src_, a.root.srcBone);
    dstPelvis_ = a.root.dstBone.empty() ? -1 : boneNamed(dst_, a.root.dstBone);
    if ((srcPelvis_ < 0) != (dstPelvis_ < 0)) { srcPelvis_ = dstPelvis_ = -1; note(RetargetIssue::Kind::Warn, "root needs both bones; pelvis translation is off"); }
    srcRootBone_ = a.rootMotion.srcBone.empty() ? -1 : boneNamed(src_, a.rootMotion.srcBone);
    dstRootBone_ = a.rootMotion.dstBone.empty() ? -1 : boneNamed(dst_, a.rootMotion.dstBone);
    if (srcRootBone_ >= 0 && dstRootBone_ >= 0 && srcPelvis_ >= 0 && dstPelvis_ >= 0 &&
        isAncestorOf(src_, srcRootBone_, srcPelvis_) && isAncestorOf(dst_, dstRootBone_, dstPelvis_) &&
        srcRootBone_ != srcPelvis_ && dstRootBone_ != dstPelvis_)
        frame_ = true;
    // Positions are carried in the frame the root bone has MOVED to: rel = p * inverse(rootPose) * rootRest, so
    // the root bone's own rest orientation (an arbitrary bone frame) never enters, and rest reads as rest.
    srcRestRootModel_ = frame_ ? srcRestModel[static_cast<usize>(srcRootBone_)] : Mat4::identity();
    dstRestRootInv_ = frame_ ? dstRestModel[static_cast<usize>(dstRootBone_)].inverse() : Mat4::identity();

    for (const RetargetChainDef& def : a.chains) {
        if (def.mode == RtgMode::None) continue;
        Chain c;
        c.mode = def.mode;
        pathBones(src_, boneNamed(src_, def.srcStart), boneNamed(src_, def.srcEnd), c.src);
        pathBones(dst_, boneNamed(dst_, def.dstStart), boneNamed(dst_, def.dstEnd), c.dst);
        const usize N = c.src.size(), M = c.dst.size();
        if (N == 0 || M == 0) { note(RetargetIssue::Kind::Warn, "chain '" + def.name + "' skipped: a path is not parent-linked"); continue; }

        std::vector<f32> cs, ct;
        f32 totalS = 0.0f, totalT = 0.0f;
        centres(c.src, srcRestPos_, cs, totalS);
        centres(c.dst, dstRestPos_, ct, totalT);
        c.srcLen = totalS > kEps ? totalS : 1.0f;
        c.dstLen = totalT > kEps ? totalT : 1.0f;

        const bool direct = N == M || def.rotation == RtgRotation::OneToOne;
        c.a.assign(M, 0);
        c.b.assign(M, 0);
        c.f.assign(M, 0.0f);
        for (usize j = 0; j < M; ++j) {
            if (direct) { c.a[j] = c.b[j] = static_cast<u32>(std::min(j, N - 1)); continue; }
            const f32 u = ct[j];
            if (N == 1 || u <= cs.front()) { c.a[j] = c.b[j] = 0; continue; }
            if (u >= cs.back()) { c.a[j] = c.b[j] = static_cast<u32>(N - 1); continue; }
            usize k = 0;
            while (k + 2 < N && cs[k + 1] < u) ++k;
            c.a[j] = static_cast<u32>(k);
            c.b[j] = static_cast<u32>(k + 1);
            const f32 span = cs[k + 1] - cs[k];
            c.f[j] = span > kEps ? std::clamp((u - cs[k]) / span, 0.0f, 1.0f) : 0.0f;
        }

        // The automatic retarget pose: swing-only alignment of the target's rest bone directions with the
        // source's, so the rests may differ (T-pose against A-pose) and a pose still means the same silhouette.
        c.align.assign(M, Quat::identity());
        if (N >= 2 && M >= 2) {
            std::vector<Vec3> sd(N), dd(M);
            for (usize k = 0; k + 1 < N; ++k) sd[k] = (srcRestPos_[c.src[k + 1]] - srcRestPos_[c.src[k]]).getSafeNormal();
            sd[N - 1] = sd[N - 2];
            for (usize k = 0; k + 1 < M; ++k) dd[k] = (dstRestPos_[c.dst[k + 1]] - dstRestPos_[c.dst[k]]).getSafeNormal();
            dd[M - 1] = dd[M - 2];
            for (usize j = 0; j < M; ++j) {
                const Vec3 s = c.a[j] == c.b[j] ? sd[c.a[j]] : lerp(sd[c.a[j]], sd[c.b[j]], c.f[j]).getSafeNormal();
                if (s.sizeSquared() > 0.5f && dd[j].sizeSquared() > 0.5f) c.align[j] = arc(dd[j], s);
            }
        }

        c.scaled = def.translation == RtgTranslation::Scaled;
        if (c.scaled) {
            c.posRatio.assign(M, 1.0f);
            for (usize j = 0; j < M; ++j) {
                const u32 s = c.f[j] < 0.5f ? c.src[c.a[j]] : c.src[c.b[j]];
                const f32 sl = lengthOf(src_.bones[s].translation), dl = lengthOf(dst_.bones[c.dst[j]].translation);
                if (sl > 1e-4f) c.posRatio[j] = dl / sl;
            }
        }

        if (c.mode == RtgMode::Ik) {
            if (N < 2 || M < 2) {
                c.mode = RtgMode::Fk;
                note(RetargetIssue::Kind::Warn, "chain '" + def.name + "': ik needs at least two bones on each side, using fk");
            } else {
                c.ik.bones = c.dst;
                c.anchor = def.anchor;
                c.scale = def.scale;
                c.poleFk = def.pole == RtgPole::Fk;
                c.tipRotation = def.tipRotation;
                c.srcRestTipRel = srcRestPos_[c.src.back()];
                c.dstRestTipRel = dstRestPos_[c.dst.back()];
                const Vec3 sv = (srcRestPos_[c.src.back()] - srcRestPos_[c.src.front()]).getSafeNormal();
                const Vec3 dv = (dstRestPos_[c.dst.back()] - dstRestPos_[c.dst.front()]).getSafeNormal();
                if (sv.sizeSquared() > 0.5f && dv.sizeSquared() > 0.5f) c.startAlign = arc(sv, dv);
            }
        }
        const i32 ci = static_cast<i32>(chains_.size());
        for (usize j = 0; j < M; ++j) {
            dstRole_[c.dst[j]].chain = ci;
            dstRole_[c.dst[j]].k = static_cast<u32>(j);
        }
        chains_.push_back(std::move(c));
    }

    // Scales: the ratio of the two pelvises' rest heights above the ground (the lowest IK chain end), or above
    // the model origin when no chain is IK.
    f32 srcGround = 0.0f, dstGround = 0.0f;
    bool anyIk = false;
    for (const Chain& c : chains_) {
        if (c.mode != RtgMode::Ik) continue;
        const f32 sz = c.srcRestTipRel.z, dz = c.dstRestTipRel.z;
        srcGround = anyIk ? std::min(srcGround, sz) : sz;
        dstGround = anyIk ? std::min(dstGround, dz) : dz;
        anyIk = true;
    }
    f32 autoScale = 1.0f;
    if (srcPelvis_ >= 0) {
        srcRestRel_ = srcRestPos_[static_cast<usize>(srcPelvis_)];
        dstRestRel_ = dstRestPos_[static_cast<usize>(dstPelvis_)];
        const f32 sh = srcRestRel_.z - srcGround, dh = dstRestRel_.z - dstGround;
        if (sh > 1e-3f && dh > 1e-3f) autoScale = dh / sh;
    }
    scaleH_ = a.root.scaleH.autoScale ? autoScale : a.root.scaleH.value;
    scaleV_ = a.root.scaleV.autoScale ? autoScale : a.root.scaleV.value;
    rootScale_ = a.rootMotion.scale.autoScale ? scaleH_ : a.rootMotion.scale.value;
    offset_ = a.root.offsetCm;

    for (Chain& c : chains_) {
        if (c.mode != RtgMode::Ik || c.anchor != RtgAnchor::Auto) continue;
        c.anchor = (srcPelvis_ < 0 || srcRestPos_[c.src.back()].z < srcRestPos_[static_cast<usize>(srcPelvis_)].z)
                 ? RtgAnchor::Root : RtgAnchor::Start;
    }

    if (dstPelvis_ >= 0) dstRole_[static_cast<usize>(dstPelvis_)].pelvis = true;
    if (dstRootBone_ >= 0 && srcRootBone_ >= 0) dstRole_[static_cast<usize>(dstRootBone_)].rootMotion = true;
    else dstRootBone_ = srcRootBone_ = -1;
    for (usize i = 0; i < dst_.bones.size(); ++i) {
        const BoneRole& r = dstRole_[i];
        if (r.chain >= 0 || r.pelvis || r.rootMotion) mapped_.push_back(static_cast<u32>(i));
        if (r.pelvis || r.rootMotion || (r.chain >= 0 && chains_[static_cast<usize>(r.chain)].scaled))
            translated_.push_back(static_cast<u32>(i));
    }
    note(RetargetIssue::Kind::Info, "retarget bound: " + std::to_string(chains_.size()) + " chains, scale " +
         std::to_string(scaleH_) + " / " + std::to_string(scaleV_));
    ok_ = true;
    return true;
}

void Retargeter::retarget(const Pose& sp, Pose& dp) const {
    const usize ns = src_.bones.size(), nd = dst_.bones.size();
    if (!ok_ || sp.local.size() != ns) { restPose(dst_, dp); return; }

    poseToModel(src_, sp, sModel_);
    sq_.resize(ns);
    delta_.resize(ns);
    for (usize i = 0; i < ns; ++i) {
        const i32 p = src_.bones[i].parent;
        sq_[i] = p >= 0 ? (sq_[static_cast<usize>(p)] * sp.local[i].rotation).normalized() : sp.local[i].rotation.normalized();
        delta_[i] = (sq_[i] * conj(srcRestQ_[i])).normalized();
    }

    restPose(dst_, dp);
    dModel_.resize(nd);
    dq_.resize(nd);
    fkTip_.resize(chains_.size());
    Mat4 toRel;   // source model space -> the root-relative frame
    if (frame_) toRel = sModel_[static_cast<usize>(srcRootBone_)].inverse() * srcRestRootModel_;

    // A translation delta of source bone `s`, carried through model space so differing parent frames agree.
    auto scaledDelta = [&](u32 s, const Quat& dstParentQ, f32 k) {
        const Vec3 d = sp.local[s].position - src_.bones[s].translation;
        const i32 sp0 = src_.bones[s].parent;
        const Vec3 dm = sp0 >= 0 ? sq_[static_cast<usize>(sp0)].rotate(d) : d;
        return conj(dstParentQ).rotate(dm * k);
    };

    for (usize i = 0; i < nd; ++i) {
        Transform& L = dp.local[i];
        const i32 p = dst_.bones[i].parent;
        const Quat pq = p >= 0 ? dq_[static_cast<usize>(p)] : Quat::identity();
        const BoneRole& r = dstRole_[i];

        if (r.chain >= 0) {
            const Chain& c = chains_[static_cast<usize>(r.chain)];
            const u32 ia = c.src[c.a[r.k]], ib = c.src[c.b[r.k]];
            const Quat D = ia == ib ? delta_[ia] : Quat::slerp(delta_[ia], delta_[ib], c.f[r.k]);
            const Quat W = (D * c.align[r.k] * dstRestQ_[i]).normalized();
            L.rotation = (conj(pq) * W).normalized();
            dq_[i] = W;
            if (c.mode == RtgMode::Ik && r.k + 1 == c.dst.size()) fkTip_[static_cast<usize>(r.chain)] = W;
            if (c.scaled) {
                const u32 s = c.f[r.k] < 0.5f ? c.src[c.a[r.k]] : c.src[c.b[r.k]];
                L.position = L.position + scaledDelta(s, pq, c.posRatio[r.k]);
            }
        } else if (r.rootMotion) {
            const Quat W = (delta_[static_cast<usize>(srcRootBone_)] * dstRestQ_[i]).normalized();
            L.rotation = (conj(pq) * W).normalized();
            dq_[i] = W;
        } else {
            dq_[i] = (pq * L.rotation).normalized();
        }

        if (r.rootMotion)
            L.position = L.position + scaledDelta(static_cast<u32>(srcRootBone_), pq, rootScale_);

        const Mat4 lm = L.toMatrix();
        dModel_[i] = p >= 0 ? lm * dModel_[static_cast<usize>(p)] : lm;

        if (r.pelvis) {
            // The pelvis follows the source pelvis by its displacement from rest, scaled by the rest heights,
            // in the frame of the root-motion bone when there is one.
            const Vec3 sPos = originOf(sModel_[static_cast<usize>(srcPelvis_)]);
            const Vec3 srcRel = frame_ ? xformPoint(sPos, toRel) : sPos;
            const Vec3 rel = dstRestRel_ + Vec3{(srcRel.x - srcRestRel_.x) * scaleH_, (srcRel.y - srcRestRel_.y) * scaleH_,
                                                (srcRel.z - srcRestRel_.z) * scaleV_} + offset_;
            const Vec3 modelPos = frame_ ? xformPoint(rel, dstRestRootInv_ * dModel_[static_cast<usize>(dstRootBone_)]) : rel;
            L.position = p >= 0 ? xformPoint(modelPos, dModel_[static_cast<usize>(p)].inverse()) : modelPos;
            const Mat4 lm2 = L.toMatrix();
            dModel_[i] = p >= 0 ? lm2 * dModel_[static_cast<usize>(p)] : lm2;
        }
    }

    // IK chains: goals from the source, solved over the FK pose, never stretching.
    for (usize ci = 0; ci < chains_.size(); ++ci) {
        const Chain& c = chains_[ci];
        if (c.mode != RtgMode::Ik) continue;
        const Vec3 srcTip = originOf(sModel_[c.src.back()]);
        Vec3 goal{0, 0, 0};
        if (c.anchor == RtgAnchor::Start) {
            const Vec3 srcStart = originOf(sModel_[c.src.front()]);
            const f32 k = c.scale.autoScale ? c.dstLen / c.srcLen : c.scale.value;
            goal = originOf(dModel_[c.dst.front()]) + c.startAlign.rotate(srcTip - srcStart) * k;
        } else {
            const Vec3 tipRel = frame_ ? xformPoint(srcTip, toRel) : srcTip;
            const f32 sx = c.scale.autoScale ? scaleH_ : c.scale.value, sz = c.scale.autoScale ? scaleV_ : c.scale.value;
            const Vec3 rel = c.dstRestTipRel + Vec3{(tipRel.x - c.srcRestTipRel.x) * sx, (tipRel.y - c.srcRestTipRel.y) * sx,
                                                    (tipRel.z - c.srcRestTipRel.z) * sz};
            goal = frame_ ? xformPoint(rel, dstRestRootInv_ * dModel_[static_cast<usize>(dstRootBone_)]) : rel;
        }
        IkGoal g;
        g.position = goal;
        if (c.tipRotation) { g.hasRotation = true; g.rotation = fkTip_[ci]; }
        IkParams params;
        params.kind = c.dst.size() == 3 ? IkSolverKind::TwoBone : IkSolverKind::Fabrik;
        params.maxIterations = 24;
        params.toleranceCm = 0.05f;
        solveChain(dst_, dp, c.ik, g, params, ikScratch_);
        poseToModel(dst_, dp, dModel_);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Clips

namespace {

// Drops interior keys whose removal moves the value less than the tolerances: from each kept key, extend as
// far as every skipped key stays within tolerance of the line to the candidate next key.
void reduceTrack(fmt::OcTrack& t, bool hasPos, f32 rotTolRad, f32 posTol) {
    const usize n = t.times.size();
    if (n < 3) return;
    const usize stride = hasPos ? 7 : 4;
    auto quatAt = [&](usize i) {
        const f32* v = &t.values[i * stride + (hasPos ? 3 : 0)];
        return Quat{v[0], v[1], v[2], v[3]};
    };
    auto posAt = [&](usize i) {
        const f32* v = &t.values[i * stride];
        return Vec3{v[0], v[1], v[2]};
    };
    auto within = [&](usize j, usize a, usize b) {
        const f32 span = t.times[b] - t.times[a];
        const f32 u = span > 1e-9f ? (t.times[j] - t.times[a]) / span : 0.0f;
        if (quatAngle(Quat::slerp(quatAt(a), quatAt(b), u), quatAt(j)) > rotTolRad) return false;
        if (hasPos && lengthOf(lerp(posAt(a), posAt(b), u) - posAt(j)) > posTol) return false;
        return true;
    };
    std::vector<usize> keep{0};
    usize anchor = 0;
    for (usize i = 1; i + 1 < n; ++i) {
        bool ok = true;
        for (usize j = anchor + 1; j <= i && ok; ++j) ok = within(j, anchor, i + 1);
        if (!ok) { keep.push_back(i); anchor = i; }
    }
    keep.push_back(n - 1);
    if (keep.size() == n) return;
    std::vector<f32> times, values;
    for (const usize k : keep) {
        times.push_back(t.times[k]);
        for (usize c = 0; c < stride; ++c) values.push_back(t.values[k * stride + c]);
    }
    t.times = std::move(times);
    t.values = std::move(values);
}

} // namespace

bool retargetClip(const Retargeter& rt, const fmt::OcAnimation& in, const fmt::OcSkeleton& src, fmt::OcAnimation& out,
                  const RetargetOptions& opt, std::vector<RetargetIssue>* issues) {
    auto note = [&](RetargetIssue::Kind k, std::string t) { if (issues) issues->push_back({k, std::move(t)}); };
    if (!rt.bound()) { note(RetargetIssue::Kind::Error, "the retargeter is not bound"); return false; }
    if (in.flags & fmt::kOcAnimObject) { note(RetargetIssue::Kind::Error, "object clips are not retargeted"); return false; }
    if (in.flags & fmt::kOcAnimAdditiveBase) { note(RetargetIssue::Kind::Error, "additive clips are not retargeted"); return false; }
    if (!in.valid()) { note(RetargetIssue::Kind::Error, "the source clip is not valid"); return false; }

    const f32 fps = opt.fps >= 1.0f ? opt.fps : 30.0f;
    std::vector<f32> times;
    if (in.duration > 1e-6f) {
        const u32 steps = static_cast<u32>(std::ceil(in.duration * fps - 1e-4f));
        for (u32 k = 0; k < steps; ++k) {
            const f32 t = static_cast<f32>(k) / fps;
            if (t >= in.duration - 1e-6f) break;
            times.push_back(t);
        }
        times.push_back(in.duration);
    } else {
        times.push_back(0.0f);
    }

    const std::vector<u32>& mapped = rt.mappedDstBones();
    const std::vector<u32>& translated = rt.translatedDstBones();
    const usize nk = times.size();
    std::vector<std::vector<f32>> values(mapped.size());
    std::vector<u8> hasPos(mapped.size(), 0);
    for (usize m = 0; m < mapped.size(); ++m) {
        hasPos[m] = std::binary_search(translated.begin(), translated.end(), mapped[m]) ? 1 : 0;
        values[m].reserve(nk * (hasPos[m] ? 7 : 4));
    }

    Pose sp, dp;
    for (usize k = 0; k < nk; ++k) {
        restPose(src, sp);
        sampleAnimation(in, times[k], sp);
        rt.retarget(sp, dp);
        for (usize m = 0; m < mapped.size(); ++m) {
            const Transform& x = dp.local[mapped[m]];
            std::vector<f32>& v = values[m];
            if (hasPos[m]) { v.push_back(x.position.x); v.push_back(x.position.y); v.push_back(x.position.z); }
            const Quat q = x.rotation.normalized();
            v.push_back(q.x); v.push_back(q.y); v.push_back(q.z); v.push_back(q.w);
        }
    }
    // Consecutive keys on one hemisphere, so interpolation takes the short way.
    for (usize m = 0; m < mapped.size(); ++m) {
        const usize stride = hasPos[m] ? 7 : 4, qo = hasPos[m] ? 3 : 0;
        std::vector<f32>& v = values[m];
        for (usize k = 1; k < nk; ++k) {
            f32* a = &v[(k - 1) * stride + qo];
            f32* b = &v[k * stride + qo];
            if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0.0f) { b[0] = -b[0]; b[1] = -b[1]; b[2] = -b[2]; b[3] = -b[3]; }
        }
    }

    fmt::OcAnimation o;
    o.duration = in.duration;
    o.storage = fmt::OcAnimStorage::Keyframed;
    o.flags = static_cast<u8>(in.flags & (fmt::kOcAnimLoop | fmt::kOcAnimRootMotion));
    o.sampleRate = 0;
    o.skeletonRef = rt.targetStem();
    o.notifies = in.notifies;
    o.notifyDurations = in.notifyDurations;
    o.curves = in.curves;
    const f32 rotTol = radians(opt.rotToleranceDeg), posTol = opt.posToleranceCm;
    for (usize m = 0; m < mapped.size(); ++m) {
        fmt::OcTrack t;
        t.boneIndex = static_cast<u16>(mapped[m]);
        t.channels = static_cast<u8>(fmt::kOcChannelRotation | (hasPos[m] ? fmt::kOcChannelTranslation : 0));
        t.interp = fmt::OcInterp::Linear;
        t.times = times;
        t.values = std::move(values[m]);
        if (opt.reduceKeys) reduceTrack(t, hasPos[m] != 0, rotTol, posTol);
        o.tracks.push_back(std::move(t));
    }
    if (!o.valid()) { note(RetargetIssue::Kind::Error, "the retargeted clip failed validation"); return false; }
    out = std::move(o);
    return true;
}

RetargetBatchResult retargetFolder(const RetargetAsset& asset, const std::string& srcSkelPath, const std::string& dstSkelPath,
                                   const std::vector<std::string>& clipPaths, const std::string& outDir,
                                   const std::string& suffix, const RetargetOptions& opt, bool overwrite) {
    RetargetBatchResult R;
    auto note = [&](RetargetIssue::Kind k, std::string t) { R.issues.push_back({k, std::move(t)}); };
    namespace fs = std::filesystem;

    fmt::OcSkeleton src, dst;
    std::string why;
    if (!fmt::loadOcSkel(srcSkelPath, src, &why)) { note(RetargetIssue::Kind::Error, "source skeleton: " + why); R.failed = static_cast<u32>(clipPaths.size()); return R; }
    if (!fmt::loadOcSkel(dstSkelPath, dst, &why)) { note(RetargetIssue::Kind::Error, "target skeleton: " + why); R.failed = static_cast<u32>(clipPaths.size()); return R; }
    Retargeter rt;
    std::vector<RetargetIssue> bindIssues;
    const bool bound = rt.bind(asset, src, dst, &bindIssues);
    for (RetargetIssue& i : bindIssues) if (i.kind != RetargetIssue::Kind::Info) R.issues.push_back(i);
    if (!bound) { R.failed = static_cast<u32>(clipPaths.size()); return R; }

    std::error_code ec;
    fs::create_directories(fs::path(outDir), ec);
    const std::string srcStem = lower(pathStem(srcSkelPath)), dstStem = lower(pathStem(dstSkelPath));
    for (const std::string& path : clipPaths) {
        fmt::OcAnimation in;
        if (!fmt::loadOcAnim(path, in, &why)) { ++R.failed; note(RetargetIssue::Kind::Error, path + ": " + why); continue; }
        if (in.flags & fmt::kOcAnimObject) { ++R.skipped; note(RetargetIssue::Kind::Warn, path + ": object clips are not retargeted"); continue; }
        if (in.flags & fmt::kOcAnimAdditiveBase) { ++R.skipped; note(RetargetIssue::Kind::Warn, path + ": additive clips are not retargeted"); continue; }
        if (lower(pathStem(in.skeletonRef)) != srcStem) {
            ++R.skipped;
            note(RetargetIssue::Kind::Warn, path + ": built for skeleton '" + in.skeletonRef + "', not '" + pathStem(srcSkelPath) + "'");
            continue;
        }
        const fs::path outPath = fs::path(outDir) / (pathStem(path) + suffix + ".ocanim");
        if (fs::exists(outPath, ec)) {
            fmt::OcAnimation prev;
            const bool previousOutput = fmt::loadOcAnim(outPath.string(), prev, nullptr) && lower(pathStem(prev.skeletonRef)) == dstStem;
            if (!previousOutput && !overwrite) {
                ++R.skipped;
                note(RetargetIssue::Kind::Warn, outPath.string() + " exists and is not a retarget output; not overwritten");
                continue;
            }
        }
        fmt::OcAnimation out;
        std::vector<RetargetIssue> clipIssues;
        if (!retargetClip(rt, in, src, out, opt, &clipIssues)) {
            ++R.failed;
            for (RetargetIssue& i : clipIssues) { i.text = path + ": " + i.text; R.issues.push_back(i); }
            continue;
        }
        if (!fmt::saveOcAnim(outPath.string(), out, &why)) { ++R.failed; note(RetargetIssue::Kind::Error, outPath.string() + ": " + why); continue; }
        ++R.written;
        R.files.push_back(outPath.string());
    }
    return R;
}

} // namespace aver::anim
