// The .ocasm pipeline (slot, layered and additive nodes) and the pose arithmetic under it, with no
// scene and no GPU: the version-2 file format, the branch filter, masked blending, and each node kind
// against a hand-computed pose.
//
// The failures worth catching are silent ones: a branch weight that leaks onto the wrong bones, an
// additive reference taken from the wrong frame, or a slot that is applied but never reported as
// consumed (so the host applies it a second time, full body).
#include "aver/anim/AnimGraphAsset.hpp"
#include "aver/anim/AnimPipeline.hpp"
#include "aver/anim/AnimSampler.hpp"
#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static void checkNear(f32 got, f32 want, f32 eps, const std::string& what) {
    if (std::fabs(got - want) <= eps) { AVER_INFO("  ok    {} ({:.5f})", what, got); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {} (got {:.6f}, want {:.6f})", what, got, want);
}

// root -> spine -> head -> hand, and root -> leg.
static fmt::OcSkeleton rig() {
    fmt::OcSkeleton s;
    auto bone = [](const char* n, i32 parent) {
        fmt::OcBone b;
        b.name = n;
        b.parent = parent;
        return b;
    };
    s.bones = {bone("root", -1), bone("spine", 0), bone("head", 1), bone("hand", 2), bone("leg", 0)};
    s.rootBone = 0;
    return s;
}

static std::map<std::string, fmt::OcAnimation>& clips() {
    static std::map<std::string, fmt::OcAnimation> t;
    return t;
}
static const fmt::OcAnimation* findClip(const std::string& n) {
    const auto it = clips().find(n);
    return it == clips().end() ? nullptr : &it->second;
}

// A clip that moves each listed bone from x = from to x = to over `dur`.
static fmt::OcAnimation slide(f32 dur, f32 from, f32 to, std::vector<u16> bones) {
    fmt::OcAnimation c;
    c.duration = dur;
    c.flags = fmt::kOcAnimLoop;
    for (const u16 b : bones) {
        fmt::OcTrack t;
        t.boneIndex = b;
        t.channels = fmt::kOcChannelTranslation;
        t.times = {0.0f, dur};
        t.values = {from, 0, 0, to, 0, 0};
        c.tracks.push_back(t);
    }
    return c;
}

static const anim::AsmBlendSpaceFn kNoSpaces = [](const std::string&) -> const anim::BlendSpaceAsset* { return nullptr; };
static const anim::ParamFn kNoParams = [](const std::string&) { return std::numeric_limits<f32>::quiet_NaN(); };
static const anim::SlotEvalFn kNoSlots = [](u64, const anim::Pose&, anim::Pose&, f32&) { return false; };

static anim::Pose flat(const fmt::OcSkeleton& s, f32 x) {
    anim::Pose p;
    anim::restPose(s, p);
    for (Transform& t : p.local) t.position = Vec3{x, 0, 0};
    return p;
}

static f32 xOf(const anim::Pose& p, usize bone) { return p.local[bone].position.x; }

static anim::PipeNode layered(anim::PipeSource src, std::vector<anim::BranchEntry> br, f32 w = 1.0f,
                              const char* weightParam = "") {
    anim::PipeNode n;
    n.kind = anim::PipeNodeKind::Layered;
    n.source = std::move(src);
    n.branches = std::move(br);
    n.weight = w;
    n.weightParam = weightParam;
    return n;
}
static anim::PipeSource clipSrc(const char* asset, bool loop = true, f32 speed = 1.0f) {
    anim::PipeSource s;
    s.kind = anim::PipeSourceKind::Clip;
    s.asset = asset;
    s.loop = loop;
    s.speed = speed;
    return s;
}
static anim::PipeSource slotSrc(const char* slot) {
    anim::PipeSource s;
    s.kind = anim::PipeSourceKind::Slot;
    s.slot = slot;
    return s;
}
static anim::PipeNode additive(anim::PipeSource src, anim::PipeRef ref, f32 w = 1.0f, const char* weightParam = "") {
    anim::PipeNode n;
    n.kind = anim::PipeNodeKind::Additive;
    n.source = std::move(src);
    n.ref = std::move(ref);
    n.weight = w;
    n.weightParam = weightParam;
    return n;
}

static anim::AnimStateMachineAsset machine() {
    anim::AnimStateMachineAsset a;
    a.name = "Hero";
    a.params = {{"upperAlpha", anim::AsmParamType::Float, 1.0f}, {"lean", anim::AsmParamType::Float, 0.0f}};
    anim::AsmMachine m;
    m.name = "Root";
    anim::AsmState s;
    s.name = "Idle";
    s.asset = "idle.ocanim";
    m.states = {s};
    a.machines = {m};
    return a;
}

int main() {
    const fmt::OcSkeleton skel = rig();
    clips()["spineLeg"] = slide(1.0f, 0.0f, 100.0f, {1, 4});
    clips()["add"] = slide(1.0f, 2.0f, 12.0f, {1});
    clips()["ref"] = slide(1.0f, 0.0f, 4.0f, {1});
    clips()["lo"] = slide(1.0f, 0.0f, 100.0f, {3});
    clips()["hi"] = slide(1.0f, 0.0f, 300.0f, {3});

    AVER_INFO("buildBranchMask");
    {
        std::vector<f32> w;
        check(anim::buildBranchMask(skel, {{"spine", 0, 1.0f}}, w) == 1, "one entry matched");
        check(w.size() == 5 && w[0] == 0.0f && w[1] == 1.0f && w[2] == 1.0f && w[3] == 1.0f && w[4] == 0.0f,
              "a whole subtree at full weight, nothing else");

        anim::buildBranchMask(skel, {{"spine", 3, 1.0f}}, w);
        checkNear(w[1], 1.0f / 3.0f, 1e-5f, "depth 3: the entry bone is a third");
        checkNear(w[2], 2.0f / 3.0f, 1e-5f, "two thirds one bone down");
        checkNear(w[3], 1.0f, 1e-5f, "and full three bones in");
        check(w[0] == 0.0f && w[4] == 0.0f, "bones outside the branch stay 0");

        anim::buildBranchMask(skel, {{"spine", 0, 1.0f}, {"hand", 0, 0.0f}}, w);
        check(w[1] == 1.0f && w[2] == 1.0f && w[3] == 0.0f, "a weight-0 entry carves a hole: the nearest entry wins");

        anim::buildBranchMask(skel, {{"spine", 0, 0.5f}}, w);
        checkNear(w[3], 0.5f, 1e-6f, "the entry weight scales the subtree");

        check(anim::buildBranchMask(skel, {{"nope", 0, 1.0f}, {"leg", 0, 1.0f}}, w) == 1, "an unknown bone is skipped, not an error");
        check(w[4] == 1.0f && w[1] == 0.0f, "and the rest still apply");
        check(anim::buildBranchMask(skel, {}, w) == 0 && w.size() == 5 && w[1] == 0.0f, "no entries: all zero");
    }

    AVER_INFO("blendPoseMasked");
    {
        const anim::Pose a = flat(skel, 0.0f), b = flat(skel, 10.0f);
        anim::Pose out;
        anim::blendPoseMasked(a, b, {0.0f, 1.0f, 0.5f, 2.0f, 0.25f}, out);
        checkNear(xOf(out, 0), 0.0f, 1e-6f, "weight 0 keeps a");
        checkNear(xOf(out, 1), 10.0f, 1e-5f, "weight 1 takes b");
        checkNear(xOf(out, 2), 5.0f, 1e-5f, "weight 0.5 is half way");
        checkNear(xOf(out, 3), 10.0f, 1e-5f, "a weight above 1 is clamped");
        checkNear(xOf(out, 4), 2.5f, 1e-5f, "a quarter");
        anim::blendPoseMasked(a, b, {1.0f}, out);
        checkNear(xOf(out, 1), 0.0f, 1e-6f, "a weight list of the wrong size leaves out == a");
    }

    AVER_INFO("a layered clip moves only its branch");
    {
        std::vector<anim::PipeNode> nodes = {layered(clipSrc("spineLeg"), {{"spine", 0, 1.0f}})};
        anim::AnimPipeline pipe;
        pipe.bind(&nodes, &skel, &findClip, kNoSpaces);
        check(pipe.bound() && !pipe.empty(), "binds");
        pipe.tick(0.5f, kNoParams);
        anim::Pose p = flat(skel, -1.0f);
        pipe.evaluate(kNoParams, kNoSlots, p);
        checkNear(xOf(p, 1), 50.0f, 1e-3f, "the spine follows the clip");
        checkNear(xOf(p, 4), -1.0f, 1e-6f, "the leg, which the clip also animates, is outside the branch");
        checkNear(xOf(p, 2), -1.0f, 1e-6f, "a branch bone the clip does not animate keeps what was under it");
        checkNear(xOf(p, 0), -1.0f, 1e-6f, "the root keeps the base");

        // The clip's own clock: speed doubles it, loop wraps it.
        std::vector<anim::PipeNode> fast = {layered(clipSrc("spineLeg", true, 2.0f), {{"spine", 0, 1.0f}})};
        anim::AnimPipeline pf;
        pf.bind(&fast, &skel, &findClip, kNoSpaces);
        pf.tick(0.75f, kNoParams);   // 1.5 s of clip: loops to 0.5
        anim::Pose q = flat(skel, -1.0f);
        pf.evaluate(kNoParams, kNoSlots, q);
        checkNear(xOf(q, 1), 50.0f, 1e-3f, "speed 2 and a looping clock: 0.75 s -> 1.5 s -> 0.5");

        std::vector<anim::PipeNode> once = {layered(clipSrc("spineLeg", false), {{"spine", 0, 1.0f}})};
        anim::AnimPipeline po;
        po.bind(&once, &skel, &findClip, kNoSpaces);
        po.tick(3.0f, kNoParams);
        anim::Pose r = flat(skel, -1.0f);
        po.evaluate(kNoParams, kNoSlots, r);
        checkNear(xOf(r, 1), 100.0f, 1e-3f, "a non-looping clip holds its last frame");
    }

    AVER_INFO("weights and weightParam");
    {
        std::vector<anim::PipeNode> nodes = {layered(clipSrc("spineLeg"), {{"spine", 0, 1.0f}}, 1.0f, "upperAlpha")};
        anim::AnimPipeline pipe;
        pipe.bind(&nodes, &skel, &findClip, kNoSpaces);
        pipe.tick(0.5f, kNoParams);
        auto alpha = [](f32 v) { return anim::ParamFn([v](const std::string& n) { return n == "upperAlpha" ? v : std::numeric_limits<f32>::quiet_NaN(); }); };

        anim::Pose p = flat(skel, -1.0f);
        pipe.evaluate(alpha(0.5f), kNoSlots, p);
        checkNear(xOf(p, 1), -1.0f + (50.0f + 1.0f) * 0.5f, 1e-3f, "the parameter scales the blend");
        p = flat(skel, -1.0f);
        pipe.evaluate(alpha(0.0f), kNoSlots, p);
        checkNear(xOf(p, 1), -1.0f, 1e-6f, "parameter 0: the base");
        p = flat(skel, -1.0f);
        pipe.evaluate(kNoParams, kNoSlots, p);
        checkNear(xOf(p, 1), 50.0f, 1e-3f, "an unknown parameter reads as 1");

        std::vector<anim::PipeNode> half = {layered(clipSrc("spineLeg"), {{"spine", 0, 1.0f}}, 0.5f, "upperAlpha")};
        anim::AnimPipeline ph;
        ph.bind(&half, &skel, &findClip, kNoSpaces);
        ph.tick(0.5f, kNoParams);
        p = flat(skel, 0.0f);
        ph.evaluate(alpha(0.5f), kNoSlots, p);
        checkNear(xOf(p, 1), 50.0f * 0.25f, 1e-3f, "weight 0.5 x parameter 0.5 = a quarter");
    }

    AVER_INFO("additive nodes and their references");
    {
        auto run = [&](anim::PipeRef ref, f32 w) {
            std::vector<anim::PipeNode> nodes = {additive(clipSrc("add"), ref, w)};
            anim::AnimPipeline pipe;
            pipe.bind(&nodes, &skel, &findClip, kNoSpaces);
            pipe.tick(0.5f, kNoParams);
            anim::Pose p = flat(skel, 0.0f);
            p.local[1].position.x = 10.0f;
            pipe.evaluate(kNoParams, kNoSlots, p);
            return p;
        };
        anim::PipeRef first;
        anim::Pose p = run(first, 1.0f);
        checkNear(xOf(p, 1), 10.0f + (7.0f - 2.0f), 1e-3f, "firstFrame: base + (sample - frame 0)");
        checkNear(xOf(p, 4), 0.0f, 1e-6f, "bones the clip does not animate get no delta");

        anim::PipeRef rest;
        rest.kind = anim::PipeRefKind::Rest;
        checkNear(xOf(run(rest, 1.0f), 1), 10.0f + 7.0f, 1e-3f, "rest: base + (sample - rest pose)");

        anim::PipeRef clip;
        clip.kind = anim::PipeRefKind::Clip;
        clip.asset = "ref";
        clip.time = 1.0f;
        checkNear(xOf(run(clip, 1.0f), 1), 10.0f + (7.0f - 4.0f), 1e-3f, "clip: base + (sample - the named clip frame)");
        checkNear(xOf(run(first, 0.5f), 1), 10.0f + 2.5f, 1e-3f, "the weight scales the delta");

        // By hand through addPose, the function the node must agree with.
        anim::Pose base = flat(skel, 0.0f), src, ref, want;
        base.local[1].position.x = 10.0f;
        anim::restPose(skel, src);
        anim::sampleAnimation(clips()["add"], 0.5f, src);
        anim::restPose(skel, ref);
        anim::sampleAnimation(clips()["add"], 0.0f, ref);
        anim::addPose(base, src, ref, 1.0f, want);
        checkNear(xOf(run(first, 1.0f), 1), xOf(want, 1), 1e-4f, "equals addPose computed by hand");

        std::vector<anim::PipeNode> nodes = {additive(clipSrc("add"), first, 1.0f, "lean")};
        anim::AnimPipeline pipe;
        pipe.bind(&nodes, &skel, &findClip, kNoSpaces);
        pipe.tick(0.5f, kNoParams);
        anim::Pose q = flat(skel, 0.0f);
        pipe.evaluate([](const std::string&) { return 0.0f; }, kNoSlots, q);
        checkNear(xOf(q, 1), 0.0f, 1e-6f, "weightParam 0 applies nothing");
    }

    AVER_INFO("a blend-space source");
    {
        anim::BlendSpaceAsset space;
        space.dims = 1;
        space.axisX = {"mv", 0.0f, 1.0f, 0.0f};
        anim::BlendSample a, b;
        a.clip = "lo";
        a.x = 0.0f;
        b.clip = "hi";
        b.x = 1.0f;
        space.samples = {a, b};
        anim::PipeSource src;
        src.kind = anim::PipeSourceKind::BlendSpace;
        src.asset = "space";
        src.xParam = "mv";
        std::vector<anim::PipeNode> nodes = {layered(src, {{"hand", 0, 1.0f}})};
        anim::AnimPipeline pipe;
        pipe.bind(&nodes, &skel, &findClip,
                  [&](const std::string& n) -> const anim::BlendSpaceAsset* { return n == "space" ? &space : nullptr; });
        const anim::ParamFn param = [](const std::string& n) { return n == "mv" ? 0.25f : std::numeric_limits<f32>::quiet_NaN(); };
        pipe.tick(0.5f, param);
        anim::Pose p = flat(skel, 0.0f);
        pipe.evaluate(param, kNoSlots, p);
        checkNear(xOf(p, 3), 0.75f * 50.0f + 0.25f * 150.0f, 1e-2f, "the hand takes the blend space at the machine's parameter");
        checkNear(xOf(p, 1), 0.0f, 1e-6f, "and nothing else moves");
    }

    AVER_INFO("slot nodes");
    {
        const u64 defaultSlot = fnv1a64(std::string_view("DefaultSlot"));
        const u64 upper = fnv1a64(std::string_view("UpperBody"));
        std::vector<u64> asked;
        const anim::SlotEvalFn slot = [&](u64 h, const anim::Pose& base, anim::Pose& out, f32& cov) {
            asked.push_back(h);
            out = base;
            out.local[3].position.x = 99.0f;
            out.local[4].position.x = 99.0f;
            cov = 0.5f;
            return true;
        };

        anim::PipeNode sn;
        sn.kind = anim::PipeNodeKind::Slot;
        sn.slot = "DefaultSlot";
        std::vector<anim::PipeNode> nodes = {sn};
        anim::AnimPipeline pipe;
        pipe.bind(&nodes, &skel, &findClip, kNoSpaces);
        anim::Pose p = flat(skel, 0.0f);
        pipe.evaluate(kNoParams, slot, p);
        check(asked.size() == 1 && asked[0] == defaultSlot, "the slot function is asked for the node's slot");
        checkNear(xOf(p, 3), 49.5f, 1e-3f, "a slot node blends the whole pose by coverage");
        checkNear(xOf(p, 4), 49.5f, 1e-3f, "full body");
        check(pipe.consumedSlot(0) == defaultSlot, "the node reports the slot as consumed");
        checkNear(pipe.rootWeight(defaultSlot), 1.0f, 1e-6f, "a slot node keeps all of the slot's root motion");
        checkNear(pipe.rootWeight(upper), 0.0f, 1e-6f, "an unconsumed slot keeps none");

        // A slot with nothing playing still counts as consumed, and leaves the pose alone.
        anim::Pose q = flat(skel, 0.0f);
        pipe.evaluate(kNoParams, kNoSlots, q);
        checkNear(xOf(q, 3), 0.0f, 1e-6f, "nothing playing: the pose is untouched");
        check(pipe.consumedSlot(0) == defaultSlot, "but the slot is still the pipeline's");

        // A layered node with a slot source applies it through the branch.
        std::vector<anim::PipeNode> up = {layered(slotSrc("UpperBody"), {{"spine", 0, 1.0f}})};
        anim::AnimPipeline pu;
        pu.bind(&up, &skel, &findClip, kNoSpaces);
        asked.clear();
        anim::Pose r = flat(skel, 0.0f);
        const anim::SlotEvalFn full = [&](u64 h, const anim::Pose& base, anim::Pose& out, f32& cov) {
            asked.push_back(h);
            out = base;
            out.local[3].position.x = 99.0f;
            out.local[4].position.x = 99.0f;
            cov = 1.0f;
            return true;
        };
        pu.evaluate(kNoParams, full, r);
        check(asked.size() == 1 && asked[0] == upper, "the layered node asks for its source slot");
        checkNear(xOf(r, 3), 99.0f, 1e-3f, "the hand, inside the branch, takes the montage pose");
        checkNear(xOf(r, 4), 0.0f, 1e-6f, "the leg, outside it, does not");
        check(pu.consumedSlot(0) == upper, "the slot is reported consumed");
        checkNear(pu.rootWeight(upper), 0.0f, 1e-6f, "a branch that excludes the root keeps none of the montage's root motion");

        std::vector<anim::PipeNode> withRoot = {layered(slotSrc("UpperBody"), {{"root", 0, 1.0f}}, 0.75f)};
        anim::AnimPipeline pw;
        pw.bind(&withRoot, &skel, &findClip, kNoSpaces);
        anim::Pose s = flat(skel, 0.0f);
        pw.evaluate(kNoParams, full, s);
        checkNear(pw.rootWeight(upper), 0.75f, 1e-6f, "a branch that includes the root keeps its weight of it");
        checkNear(xOf(s, 4), 0.75f * 99.0f, 1e-3f, "and blends by that weight");
    }

    AVER_INFO("the .ocasm file format");
    {
        anim::AnimStateMachineAsset a = machine();
        std::string why;
        check(a.valid(&why), "the baseline machine is valid " + why);
        const std::string v1 = anim::writeStateMachine(a);
        check(v1.find("\"version\": 1,") != std::string::npos, "no pipeline: written as version 1");
        check(v1.find("pipeline") == std::string::npos, "and the file has no pipeline key");
        check(v1.size() >= 7 && v1.compare(v1.size() - 7, 7, "\n  ]\n}\n") == 0,
              "the tail is exactly what version-1 files have always ended with");
        anim::AnimStateMachineAsset back;
        check(anim::parseStateMachine(v1, back, &why) && anim::writeStateMachine(back) == v1,
              "a pipeline-less file round-trips byte-identically " + why);
        check(back.pipeline.empty(), "and parses with an empty pipeline");

        anim::PipeNode sn;
        sn.kind = anim::PipeNodeKind::Slot;
        sn.slot = "DefaultSlot";
        anim::PipeSource bs;
        bs.kind = anim::PipeSourceKind::BlendSpace;
        bs.asset = "Anims/lean.ocblend";
        bs.xParam = "lean";
        anim::PipeRef clipRef;
        clipRef.kind = anim::PipeRefKind::Clip;
        clipRef.asset = "Anims/neutral.ocanim";
        clipRef.time = 0.25f;
        a.pipeline = {sn,
                      layered(slotSrc("UpperBody"), {{"spine_01", 3, 1.0f}, {"hand_l", 0, 0.0f}}, 0.8f, "upperAlpha"),
                      layered(clipSrc("Anims/aim.ocanim", false, 1.5f), {{"spine_01", 0, 1.0f}}),
                      additive(bs, anim::PipeRef{}, 0.6f, "leanAmount"),
                      additive(clipSrc("Anims/sway.ocanim"), clipRef)};
        check(a.valid(&why), "a machine with a pipeline is valid " + why);
        const std::string v2 = anim::writeStateMachine(a);
        check(v2.find("\"version\": 2,") != std::string::npos, "with a pipeline: written as version 2");
        anim::AnimStateMachineAsset b;
        check(anim::parseStateMachine(v2, b, &why), "it parses " + why);
        check(anim::writeStateMachine(b) == v2, "write -> parse -> write is byte-identical");
        check(b.pipeline.size() == 5 && b.pipeline[0].kind == anim::PipeNodeKind::Slot && b.pipeline[0].slot == "DefaultSlot",
              "the slot node survives");
        const anim::PipeNode& l = b.pipeline[1];
        check(l.kind == anim::PipeNodeKind::Layered && l.source.kind == anim::PipeSourceKind::Slot &&
                  l.source.slot == "UpperBody" && l.branches.size() == 2 && l.branches[0].bone == "spine_01" &&
                  l.branches[0].depth == 3 && l.branches[1].weight == 0.0f && l.weight == 0.8f &&
                  l.weightParam == "upperAlpha",
              "the layered slot node survives");
        const anim::PipeNode& c = b.pipeline[2];
        check(c.source.kind == anim::PipeSourceKind::Clip && c.source.asset == "Anims/aim.ocanim" && !c.source.loop &&
                  c.source.speed == 1.5f,
              "a clip source keeps loop and speed");
        const anim::PipeNode& d = b.pipeline[3];
        check(d.kind == anim::PipeNodeKind::Additive && d.source.kind == anim::PipeSourceKind::BlendSpace &&
                  d.source.xParam == "lean" && d.ref.kind == anim::PipeRefKind::FirstFrame && d.weight == 0.6f &&
                  d.weightParam == "leanAmount",
              "an additive blend-space node survives");
        const anim::PipeNode& e = b.pipeline[4];
        check(e.ref.kind == anim::PipeRefKind::Clip && e.ref.asset == "Anims/neutral.ocanim" && e.ref.time == 0.25f,
              "a clip reference keeps its asset and time");

        std::string unknown = v2;
        const usize at = unknown.find("\"layered\"");
        if (at != std::string::npos) unknown.replace(at, 9, "\"bogus\"");
        anim::AnimStateMachineAsset u;
        check(!anim::parseStateMachine(unknown, u), "an unknown node type is refused");
        std::string newer = v2;
        const usize vat = newer.find("\"version\": 2");
        if (vat != std::string::npos) newer.replace(vat, 12, "\"version\": 3");
        check(!anim::parseStateMachine(newer, u), "a version newer than 2 is refused");
    }

    AVER_INFO("pipeline validation");
    {
        auto bad = [&](const char* what, auto mutate) {
            anim::AnimStateMachineAsset a = machine();
            a.pipeline = {layered(clipSrc("a.ocanim"), {{"spine", 0, 1.0f}})};
            mutate(a);
            check(!a.valid(), std::string("refused: ") + what);
        };
        anim::AnimStateMachineAsset good = machine();
        good.pipeline = {layered(clipSrc("a.ocanim"), {{"spine", 0, 1.0f}})};
        std::string why;
        check(good.valid(&why), "the baseline pipeline is valid " + why);
        bad("a slot node with no slot", [](anim::AnimStateMachineAsset& a) {
            anim::PipeNode n;
            n.kind = anim::PipeNodeKind::Slot;
            a.pipeline = {n};
        });
        bad("a layered node with no branches", [](anim::AnimStateMachineAsset& a) { a.pipeline[0].branches.clear(); });
        bad("a node with no source", [](anim::AnimStateMachineAsset& a) { a.pipeline[0].source = anim::PipeSource{}; });
        bad("a clip source with no asset", [](anim::AnimStateMachineAsset& a) { a.pipeline[0].source.asset.clear(); });
        bad("a slot source with no slot", [](anim::AnimStateMachineAsset& a) { a.pipeline[0].source = slotSrc(""); });
        bad("a branch with no bone", [](anim::AnimStateMachineAsset& a) { a.pipeline[0].branches[0].bone.clear(); });
        bad("a negative depth", [](anim::AnimStateMachineAsset& a) { a.pipeline[0].branches[0].depth = -1; });
        bad("a non-finite branch weight", [](anim::AnimStateMachineAsset& a) { a.pipeline[0].branches[0].weight = INFINITY; });
        bad("a non-finite node weight", [](anim::AnimStateMachineAsset& a) { a.pipeline[0].weight = NAN; });
        bad("an additive clip reference with no asset", [](anim::AnimStateMachineAsset& a) {
            anim::PipeRef r;
            r.kind = anim::PipeRefKind::Clip;
            a.pipeline = {additive(clipSrc("a.ocanim"), r)};
        });
    }

    AVER_INFO(g_failures == 0 ? "AnimPipelineTest: PASS" : "AnimPipelineTest: FAIL");
    return g_failures == 0 ? 0 : 1;
}
