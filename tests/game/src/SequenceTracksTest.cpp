// SequenceTracksTest: what each level-sequence track kind writes into a real scene::World (no GPU, no audio
// device): property tracks through the field registry, visibility with and without descendants, the base
// store that undoes every write, actor cameras and camera cuts, sub-sequences (time map, depth, cycles, a
// .ocseq read through a hook), and the triggered tracks (events and audio) with recording hooks.
// docs/SEQUENCER.md.
#include "aver/game/LevelSequence.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/core/Math.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/World.hpp"

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

using namespace aver;
using aver::fmt::OcSeqInterp;
using aver::fmt::OcSeqKey;
using aver::fmt::OcSeqTrack;
using aver::fmt::OcSeqTrackKind;
using aver::fmt::OcSequence;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) return;
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool approx(f64 a, f64 b, f64 eps = 1e-3) { return std::fabs(a - b) <= eps; }

#if AVER_MODULE_SCENE

namespace {

// A script-style component with every field kind, so the refusals can be tested.
struct TSeq {
    f32 f = 0;
    i32 flag = 0;
    i32 count = 0;
    f32 ro = 0;
    u64 str = 0;
    f32 mat[16] = {};
};

OcSeqKey key(f64 t, OcSeqInterp in, std::initializer_list<f64> vals) {
    OcSeqKey k;
    k.t = t;
    k.interp = in;
    usize i = 0;
    for (const f64 v : vals) k.v[i++] = v;
    return k;
}

OcSeqTrack track(OcSeqTrackKind kind, i32 target, std::vector<OcSeqKey> keys) {
    OcSeqTrack t;
    t.kind = kind;
    t.target = target;
    t.keys = std::move(keys);
    return t;
}

OcSeqTrack prop(i32 target, const char* field, std::vector<OcSeqKey> keys) {
    OcSeqTrack t = track(OcSeqTrackKind::Property, target, std::move(keys));
    t.property = field;
    return t;
}

OcSequence seqOf(const char* name, f64 length, std::vector<OcSeqTrack> tracks) {
    OcSequence s;
    s.name = name;
    s.length = length;
    s.loop = false;
    s.tracks = std::move(tracks);
    return s;
}

struct Recorder {
    std::vector<game::SeqEventCall> events;
    std::vector<game::SeqVoice> played;
    std::vector<i32> stopped;
    std::vector<f32> volumes;
    i32 nextVoice = 10;
    std::vector<std::string> sounds;
    game::SequenceHooks hooks() {
        game::SequenceHooks h;
        h.fireEvent = [this](const game::SeqEventCall& c) { events.push_back(c); };
        h.loadSound = [this](const std::string& p) { sounds.push_back(p); return 1; };
        h.playVoice = [this](i32, const game::SeqVoice& v) { played.push_back(v); return nextVoice++; };
        h.updateVoice = [this](i32, const Vec3*, f32 vol) { volumes.push_back(vol); };
        h.stopVoice = [this](i32 voice, f32) { stopped.push_back(voice); };
        return h;
    }
};

void pre(game::SequencePlayer& p, scene::World& w) { p.evaluate(w, game::SeqPhase::Pre); }
void post(game::SequencePlayer& p, scene::World& w) { p.evaluate(w, game::SeqPhase::Post); }
void both(game::SequencePlayer& p, scene::World& w) { pre(p, w); post(p, w); }

void destroy(scene::World& w, std::initializer_list<scene::Entity> es) {
    for (const scene::Entity e : es) w.destroy(e);
    w.flush();
}

void testProperties(scene::World& w) {
    auto b = w.registerComponent("TSeq", sizeof(TSeq), alignof(TSeq));
    b.field("f", scene::FieldKind::F32, static_cast<u16>(offsetof(TSeq, f)))
        .field("flag", scene::FieldKind::Bool, static_cast<u16>(offsetof(TSeq, flag)))
        .field("count", scene::FieldKind::I32, static_cast<u16>(offsetof(TSeq, count)))
        .field("ro", scene::FieldKind::F32, static_cast<u16>(offsetof(TSeq, ro)), 0, true)
        .field("str", scene::FieldKind::String, static_cast<u16>(offsetof(TSeq, str)))
        .field("mat", scene::FieldKind::Mat4, static_cast<u16>(offsetof(TSeq, mat)));
    const bool verified = b.verify(sizeof(TSeq));
    check(verified, "the script-style test component registers with a complete field table");
    const u32 tseq = w.componentId("TSeq");

    const scene::Entity lamp = w.create("lamp");
    auto* light = static_cast<scene::CLight*>(w.addComponent(lamp, scene::kComponentLight));
    light->intensityLux = 1000.0f;
    light->colour[0] = 1; light->colour[1] = 1; light->colour[2] = 1;
    auto* ts = static_cast<TSeq*>(w.addComponent(lamp, tseq));
    ts->f = 5;
    ts->ro = 9;
    const scene::Entity bare = w.create("bare");   // no light, no TSeq

    OcSequence s = seqOf("P", 4, {
        prop(0, "CLight.intensityLux", {key(0, OcSeqInterp::Linear, {2000}), key(4, OcSeqInterp::Linear, {6000})}),
        prop(0, "CLight.colour", {key(0, OcSeqInterp::Linear, {1, 0, 0}), key(4, OcSeqInterp::Linear, {0, 1, 0})}),
        prop(0, "CLight.kind", {key(0, OcSeqInterp::Linear, {2.4}), key(2, OcSeqInterp::Linear, {7.6})}),
        prop(0, "TSeq.f", {key(0, OcSeqInterp::Smooth, {1}), key(1, OcSeqInterp::Smooth, {2})}),
        prop(0, "TSeq.flag", {key(0, OcSeqInterp::Step, {0}), key(2, OcSeqInterp::Step, {1})}),
        prop(0, "TSeq.ro", {key(0, OcSeqInterp::Step, {123})}),
        prop(0, "TSeq.str", {key(0, OcSeqInterp::Step, {123})}),
        prop(0, "TSeq.mat", {key(0, OcSeqInterp::Step, {123})}),
        prop(0, "CLocal.rotation", {key(0, OcSeqInterp::Linear, {0, 0, 0, 1}), key(1, OcSeqInterp::Linear, {0, 0, 1, 0})}),
        prop(0, "CLocal.position", {key(0, OcSeqInterp::Linear, {0, 0, 0}), key(4, OcSeqInterp::Linear, {40, 0, 0})}),
        prop(1, "CLight.intensityLux", {key(0, OcSeqInterp::Step, {77})}),   // an actor without a light
        prop(1, "TSeq.f", {key(0, OcSeqInterp::Step, {77})}),
    });
    game::SequencePlayer p;
    p.setSequence(s);
    p.bind({lamp, bare});
    p.setTime(2.0);
    pre(p, w);
    check(approx(light->intensityLux, 4000.0f), "a float property lerps (CLight.intensityLux at the midpoint)");
    check(approx(light->colour[0], 0.5f) && approx(light->colour[1], 0.5f) && approx(light->colour[2], 0.0f), "a vec3 property (a colour) lerps per channel");
    check(light->kind == 8, "an integer property holds its key (7.6 at t = 2 is the last key, rounded)");
    ts = static_cast<TSeq*>(w.getComponent(lamp, tseq));
    check(approx(ts->f, 2.0f), "a script-registered float field is written (held after its last key)");
    check(ts->flag == 1, "a bool property holds its key");
    check(approx(ts->ro, 9.0f) && ts->str == 0 && ts->mat[0] == 0.0f, "read-only, String and Mat4 fields are refused and untouched");
    check(!w.hasComponent(bare, tseq) && !w.hasComponent(bare, scene::kComponentLight), "an actor lacking the component is skipped, the component never added");
    const Quat q = w.localTransform(lamp).rotation;
    check(approx(q.z, 1.0f) && approx(q.w, 0.0f), "a quaternion property is written to CLocal (touchLocal made it visible)");
    check(approx(w.localTransform(lamp).position.x, 20.0f), "CLocal.position as a property moves the actor");

    p.setTime(0.0);
    pre(p, w);
    check(light->kind == 2, "integer 2.4 rounds to 2");
    check(p.baseStore().size() > 0, "the writes were recorded in the base store");
    p.restore(w);
    check(approx(light->intensityLux, 1000.0f) && approx(light->colour[0], 1.0f) && light->kind == 0, "restore puts the authored light back");
    ts = static_cast<TSeq*>(w.getComponent(lamp, tseq));
    check(approx(ts->f, 5.0f) && ts->flag == 0, "and the script component");
    check(approx(w.localTransform(lamp).position.x, 0.0f) && approx(w.localTransform(lamp).rotation.w, 1.0f), "and the transform");
    check(p.baseStore().empty(), "the store is empty after a restore");

    // A muted track writes nothing; a destroyed entity does not break restore.
    s.tracks.resize(1);
    s.tracks[0].mute = true;
    p.setSequence(s);
    p.bind({lamp, bare});
    p.setTime(2.0);
    pre(p, w);
    check(approx(light->intensityLux, 1000.0f), "a muted track writes nothing");
    s.tracks[0].mute = false;
    p.setSequence(s);
    p.bind({lamp, bare});
    pre(p, w);
    check(approx(light->intensityLux, 4000.0f), "unmuted it writes again");
    destroy(w, {lamp});
    p.restore(w);   // the actor is gone: dropped quietly
    check(p.baseStore().empty(), "restore forgets entries whose entity died");
    destroy(w, {bare});
}

void testVisibility(scene::World& w) {
    const scene::Entity parent = w.create("parent");
    const scene::Entity child = w.create("child", parent, Transform{});
    const scene::Entity grand = w.create("grand", child, Transform{});
    for (const scene::Entity e : {parent, child, grand}) {
        auto* mr = static_cast<scene::CMeshRenderer*>(w.addComponent(e, scene::kComponentMeshRenderer));
        mr->flags = scene::kMeshRendererVisible;
    }
    const auto vis = [&](scene::Entity e) { return (w.component<scene::CMeshRenderer>(e, scene::kComponentMeshRenderer)->flags & scene::kMeshRendererVisible) != 0; };

    OcSequence s = seqOf("V", 10, {track(OcSeqTrackKind::Visibility, 0, {key(2, OcSeqInterp::Step, {0, 1}), key(6, OcSeqInterp::Step, {1, 1})})});
    game::SequencePlayer p;
    p.setSequence(s);
    p.bind({parent});
    p.setTime(1.0);
    pre(p, w);
    check(vis(parent) && vis(child) && vis(grand), "before the first key the authored value stands");
    p.setTime(3.0);
    pre(p, w);
    check(!vis(parent) && !vis(child) && !vis(grand), "include-children hides the whole subtree");
    p.setTime(7.0);
    pre(p, w);
    check(vis(parent) && vis(child) && vis(grand), "and shows it again");
    p.restore(w);

    s.tracks[0].keys[0].v[1] = 0;   // this key leaves descendants alone
    p.setSequence(s);
    p.bind({parent});
    p.setTime(3.0);
    pre(p, w);
    check(!vis(parent) && vis(child) && vis(grand), "without include-children only the actor is hidden");
    check(p.baseStore().size() == 1, "one flags word was recorded");
    p.restore(w);
    check(vis(parent), "restore shows it again");
    destroy(w, {parent});
}

void testTransformAndCameras(scene::World& w) {
    const scene::Entity box = w.create("box");
    Transform authored;
    authored.position = Vec3{5, 6, 7};
    w.setLocalTransform(box, authored);

    OcSeqTrack tf = track(OcSeqTrackKind::Transform, 0, {key(0, OcSeqInterp::Linear, {0, 0, 0}), key(2, OcSeqInterp::Linear, {200, 0, 0})});
    OcSequence s = seqOf("T", 2, {tf});
    game::SequencePlayer p;
    p.setSequence(s);
    p.bind({box});
    p.setTime(1.0);
    post(p, w);
    check(approx(w.localTransform(box).position.x, 100.0f), "a transform track writes through the base store as before");
    p.restore(w);
    check(approx(w.localTransform(box).position.x, 5.0f) && approx(w.localTransform(box).position.z, 7.0f), "and restore puts the authored transform back");

    // An actor camera reads the actor's world matrix: position and the +X axis as yaw / pitch.
    const scene::Entity crane = w.create("crane");
    Transform ct;
    ct.position = Vec3{100, 0, 50};
    ct.rotation = Quat::fromAxisAngle(Vec3{0, 0, 1}, 3.14159265f * 0.5f);
    w.setLocalTransform(crane, ct);
    w.flush();
    OcSeqTrack actorCam = track(OcSeqTrackKind::Camera, 0, {});
    actorCam.name = "Crane";
    OcSequence cs = seqOf("C", 4, {actorCam});
    game::SequencePlayer cp;
    cp.setSequence(cs);
    cp.bind({crane});
    post(cp, w);
    game::SeqCameraPose pose;
    check(cp.camera(pose) && approx(pose.position.x, 100) && approx(pose.position.z, 50) && approx(pose.yaw, 3.14159265 * 0.5, 1e-3) && approx(pose.pitch, 0, 1e-3),
          "an actor camera takes the actor's world position and facing");
    check(cp.cameraName() == "Crane", "and reports its name");

    // The actor camera follows the actor when a transform track moves it (transforms are written first).
    OcSeqTrack move = track(OcSeqTrackKind::Transform, 0, {key(0, OcSeqInterp::Linear, {0, 0, 0}), key(4, OcSeqInterp::Linear, {400, 0, 0})});
    move.keys[0].v[3] = 0; move.keys[1].v[3] = 0;
    OcSequence ride = seqOf("Ride", 4, {actorCam, move});
    cp.setSequence(ride);
    cp.bind({crane});
    cp.setTime(2.0);
    post(cp, w);
    check(cp.camera(pose) && approx(pose.position.x, 200), "a camera riding a crane reads the crane's new matrix the same frame");
    cp.restore(w);

    // Two free cameras and cuts: a hard cut, a blend, the first camera before the first cut.
    OcSeqTrack camA = track(OcSeqTrackKind::Camera, -1, {key(0, OcSeqInterp::Linear, {0, 0, 0, 0, 0})});
    camA.name = "A";
    OcSeqTrack camB = track(OcSeqTrackKind::Camera, -1, {key(0, OcSeqInterp::Linear, {1000, 0, 0, 0, 0})});
    camB.name = "B";
    OcSeqTrack cuts = track(OcSeqTrackKind::CameraCut, -1, {});
    { OcSeqKey k = key(1, OcSeqInterp::Step, {}); k.text = "A"; cuts.keys.push_back(k); }
    { OcSeqKey k = key(4, OcSeqInterp::Step, {2.0}); k.text = "B"; cuts.keys.push_back(k); }
    OcSequence two = seqOf("Two", 10, {camA, camB, cuts});
    game::SequencePlayer tp;
    tp.setSequence(two);
    tp.setTime(0.5);
    post(tp, w);
    check(tp.camera(pose) && approx(pose.position.x, 0) && tp.cameraName() == "A", "before the first cut: the first camera track");
    tp.setTime(2.0);
    post(tp, w);
    check(tp.camera(pose) && approx(pose.position.x, 0) && tp.cameraName() == "A", "a cut selects its camera");
    tp.setTime(5.0);
    post(tp, w);
    check(tp.camera(pose) && approx(pose.position.x, 500, 0.5) && tp.cameraName() == "B", "half-way through a blend the position is half-way");
    tp.setTime(7.0);
    post(tp, w);
    check(tp.camera(pose) && approx(pose.position.x, 1000) , "after the blend the new camera alone");
    // A muted camera track is out of the running.
    two.tracks[1].mute = true;
    tp.setSequence(two);
    tp.setTime(7.0);
    post(tp, w);
    check(tp.camera(pose) && approx(pose.position.x, 0), "a muted camera is skipped (the cut falls back to the first live camera)");
    destroy(w, {box, crane});
}

void testSubSequences(scene::World& w) {
    const scene::Entity puppet = w.create("puppet");
    OcSeqTrack child = track(OcSeqTrackKind::Transform, 0, {key(0, OcSeqInterp::Linear, {0, 0, 0}), key(10, OcSeqInterp::Linear, {100, 0, 0})});
    OcSequence childSeq = seqOf("Child", 10, {child});

    OcSeqTrack sub = track(OcSeqTrackKind::SubSequence, -1, {});
    { OcSeqKey k = key(2, OcSeqInterp::Step, {1, 0, 0, 0, 0, 0, 2.0}); k.dur = 4; k.text = "Child"; sub.keys.push_back(k); }   // offset 1, scale 2
    OcSequence parent = seqOf("Parent", 20, {sub});

    std::vector<OcSequence> level{parent, childSeq};
    game::SeqLibrary lib;
    lib.setLevel(&level);
    game::SequencePlayer p;
    p.setSequence(level[0]);
    p.setLibrary(&lib);
    p.bind({puppet});
    p.setTime(1.0);
    both(p, w);
    check(approx(w.localTransform(puppet).position.x, 0.0f), "outside the section the child does nothing");
    p.setTime(3.0);   // local = offset 1 + (3 - 2) * scale 2 = 3 -> x = 30
    both(p, w);
    check(approx(w.localTransform(puppet).position.x, 30.0f), "time = offset + (t - start) * scale");
    p.setTime(5.5);   // 1 + 3.5 * 2 = 8
    both(p, w);
    check(approx(w.localTransform(puppet).position.x, 80.0f), "and later in the section");
    check(p.boundEntities().size() == 1 && p.boundEntities()[0] == puppet, "boundEntities recurses into the sub-sequence");
    check(p.movedEntities().size() == 1, "and so does movedEntities");

    // Clamp past the child's end, loop with the section's loop flag.
    level[1].length = 4;
    level[0].tracks[0].keys[0].v[0] = 0;   // offset 0
    p.setSequence(level[0]);
    p.setLibrary(&lib);
    p.bind({puppet});
    p.setTime(5.0);   // 0 + 3 * 2 = 6 > length 4: clamp to the child's end (its x curve reads 40 at t = 4)
    both(p, w);
    check(approx(w.localTransform(puppet).position.x, 40.0f), "past the child's end it clamps to its last instant");
    level[0].tracks[0].keys[0].v[1] = 1;   // loop inside the section
    p.setSequence(level[0]);
    p.setLibrary(&lib);
    p.bind({puppet});
    p.setTime(5.0);   // 6 mod 4 = 2 -> x = 20
    both(p, w);
    check(approx(w.localTransform(puppet).position.x, 20.0f), "with the loop flag it wraps by the child's length");
    p.restore(w);
    check(approx(w.localTransform(puppet).position.x, 0.0f), "restore reaches into the sub-sequence's writes");

    // Nesting depth is limited to 4, and a name already on the stack is refused.
    std::vector<scene::Entity> es;
    std::vector<OcSequence> chain;
    for (int i = 0; i < 7; ++i) {
        es.push_back(w.create("chain"));
        OcSeqTrack mv = track(OcSeqTrackKind::Transform, i, {key(0, OcSeqInterp::Step, {10.0 + i, 0, 0})});
        OcSeqTrack link = track(OcSeqTrackKind::SubSequence, -1, {});
        OcSeqKey k = key(0, OcSeqInterp::Step, {});
        k.dur = 10;
        k.text = "S" + std::to_string(i + 1);
        link.keys.push_back(k);
        chain.push_back(seqOf(("S" + std::to_string(i)).c_str(), 10, {mv, link}));
    }
    std::vector<OcSequence> chainLevel = chain;
    game::SeqLibrary chainLib;
    chainLib.setLevel(&chainLevel);
    game::SequencePlayer cp;
    cp.setSequence(chainLevel[0]);
    cp.setLibrary(&chainLib);
    cp.bind(es);
    cp.setTime(1.0);
    both(cp, w);
    int moved = 0;
    for (const scene::Entity e : es) moved += w.localTransform(e).position.x > 5.0f ? 1 : 0;
    check(moved == 5, "depth 4 is the limit: the top sequence and four levels below it play, the rest are refused");
    cp.restore(w);

    // A cycle: A plays B plays A.
    std::vector<OcSequence> cyc;
    {
        OcSeqTrack toB = track(OcSeqTrackKind::SubSequence, -1, {});
        OcSeqKey kb = key(0, OcSeqInterp::Step, {}); kb.dur = 5; kb.text = "B"; toB.keys.push_back(kb);
        OcSeqTrack toA = track(OcSeqTrackKind::SubSequence, -1, {});
        OcSeqKey ka = key(0, OcSeqInterp::Step, {}); ka.dur = 5; ka.text = "A"; toA.keys.push_back(ka);
        cyc.push_back(seqOf("A", 5, {toB}));
        cyc.push_back(seqOf("B", 5, {toA}));
    }
    game::SeqLibrary cycLib;
    cycLib.setLevel(&cyc);
    game::SequencePlayer yp;
    yp.setSequence(cyc[0]);
    yp.setLibrary(&cycLib);
    yp.setTime(1.0);
    both(yp, w);   // terminates, which is the assertion
    check(true, "a cycle is refused without recursing forever");

    // A .ocseq read through the host's file hook, its actor bound by name.
    {
        fmt::OcSeqFile file;
        OcSequence boom = seqOf("Boom", 2, {});
        OcSeqTrack t = track(OcSeqTrackKind::Transform, -1, {key(0, OcSeqInterp::Linear, {0, 0, 0}), key(2, OcSeqInterp::Linear, {50, 0, 0})});
        t.bind = "Crate";
        boom.tracks.push_back(t);
        file.sequences.push_back(boom);
        const std::string text = fmt::writeOcseq(file);
        game::SequenceHooks hooks;
        hooks.readFile = [&](const std::string& path) { return path == "Seq/boom.ocseq" ? text : std::string(); };

        OcSeqTrack fileSub = track(OcSeqTrackKind::SubSequence, -1, {});
        OcSeqKey fk = key(1, OcSeqInterp::Step, {}); fk.dur = 2; fk.text = "Seq/boom.ocseq#Boom";
        fileSub.keys.push_back(fk);
        OcSequence host = seqOf("Host", 6, {fileSub});
        std::vector<OcSequence> hostLevel{host};
        game::SeqLibrary hostLib;
        hostLib.setLevel(&hostLevel);
        game::SequencePlayer hp;
        hp.setSequence(hostLevel[0]);
        hp.setLibrary(&hostLib);
        hp.setHooks(&hooks);
        game::SeqBindings b;
        b.byName["Crate"] = puppet;
        hp.setBindings(b);
        hp.setTime(2.0);   // 1 s into the 2 s section
        both(hp, w);
        check(approx(w.localTransform(puppet).position.x, 25.0f), "a .ocseq sub-sequence binds its actor by name and plays through readFile");
        check(hp.boundEntities().size() == 1, "and its bindings count as bound");
        hp.restore(w);
    }
    for (const scene::Entity e : es) w.destroy(e);
    w.destroy(puppet);
    w.flush();
}

void testTriggered(scene::World& w) {
    const scene::Entity door = w.create("door");
    auto* tag = w.addComponent(door, scene::kComponentTags);
    (void)tag;

    OcSeqTrack ev = track(OcSeqTrackKind::Event, -1, {});
    { OcSeqKey k = key(0, OcSeqInterp::Step, {1}); k.text = "AtZero"; ev.keys.push_back(k); }
    { OcSeqKey k = key(1, OcSeqInterp::Step, {2, 2, 7, 8}); k.text = "Both"; k.arg = "payload"; ev.keys.push_back(k); }
    { OcSeqKey k = key(3, OcSeqInterp::Step, {0}); k.text = "Bus"; ev.keys.push_back(k); }
    OcSequence s = seqOf("E", 4, {ev});

    Recorder rec;
    const game::SequenceHooks hooks = rec.hooks();
    game::SequencePlayer p;
    p.setSequence(s);
    p.setHooks(&hooks);
    p.setMode(game::SeqMode::Live);
    p.setTime(0.0);
    p.play();
    p.advance(0.5);
    pre(p, w);
    check(rec.events.size() == 1 && rec.events[0].name == "AtZero" && rec.events[0].route == 1, "the key at t = 0 fires once on first play, by its route");
    p.advance(0.6);   // crosses 1.0
    pre(p, w);
    check(rec.events.size() == 2 && rec.events[1].name == "Both", "a forward crossing fires the key");
    if (rec.events.size() == 2) {
        const game::SeqEventCall& c = rec.events[1];
        check(c.route == 2 && c.argc == 2 && approx(c.args[0], 7) && approx(c.args[1], 8) && c.arg == "payload", "route 2 (both), two numeric arguments in order, the string argument");
    }
    pre(p, w);
    check(rec.events.size() == 2, "evaluating again without advancing fires nothing twice");
    p.setTime(0.2);
    pre(p, w);
    check(rec.events.size() == 2, "a jump fires nothing");
    p.setTime(2.5);
    pre(p, w);
    p.advance(0.1);
    pre(p, w);
    check(rec.events.size() == 2, "and neither does the step after it (3.0 is not yet crossed)");
    p.advance(0.5);
    pre(p, w);
    check(rec.events.size() == 3 && rec.events[2].name == "Bus" && rec.events[2].route == 0, "the next crossing does");

    // Preview and Offline never fire events.
    rec.events.clear();
    game::SequencePlayer pv;
    pv.setSequence(s);
    pv.setHooks(&hooks);
    pv.setMode(game::SeqMode::Preview);
    pv.setTime(0.0);
    pv.play();
    pv.advance(3.5);
    pre(pv, w);
    game::SequencePlayer off;
    off.setSequence(s);
    off.setHooks(&hooks);
    off.setMode(game::SeqMode::Offline);
    off.setTime(0.0);
    off.play();
    off.advance(3.5);
    pre(off, w);
    check(rec.events.empty(), "events never fire in Preview or Offline");

    // A looping sequence fires its seam keys once per lap.
    rec.events.clear();
    s.loop = true;
    game::SequencePlayer lp;
    lp.setSequence(s);
    lp.setHooks(&hooks);
    lp.setMode(game::SeqMode::Live);
    lp.setTime(3.5);
    lp.play();
    lp.advance(1.0);   // 3.5 -> 4.5 wraps to 0.5: crosses the seam, so t = 0 fires
    pre(lp, w);
    check(rec.events.size() == 1 && rec.events[0].name == "AtZero", "across the loop seam the key at t = 0 fires");

    // An event bound to an actor is addressed to it; one bound to a missing actor is dropped.
    rec.events.clear();
    OcSeqTrack ev2 = track(OcSeqTrackKind::Event, 0, {});
    { OcSeqKey k = key(0.5, OcSeqInterp::Step, {0}); k.text = "ToDoor"; ev2.keys.push_back(k); }
    OcSequence s2 = seqOf("E2", 2, {ev2});
    game::SequencePlayer ap;
    ap.setSequence(s2);
    ap.setHooks(&hooks);
    ap.bind({door});
    ap.setTime(0.0);
    ap.play();
    ap.advance(1.0);
    pre(ap, w);
    check(rec.events.size() == 1 && rec.events[0].target == door && rec.events[0].sender == door, "a bound event carries its actor as target and sender");
    rec.events.clear();
    game::SequencePlayer mp;
    mp.setSequence(s2);
    mp.setHooks(&hooks);
    mp.bind({scene::kInvalidEntity});
    mp.setTime(0.0);
    mp.play();
    mp.advance(1.0);
    pre(mp, w);
    check(rec.events.empty(), "an event bound to a missing actor is dropped, not broadcast");

    // Audio: starts at a forward crossing, never mid-section, stops at its end, on pause, on a jump and on wrap.
    OcSeqTrack au = track(OcSeqTrackKind::Audio, -1, {});
    { OcSeqKey k = key(1, OcSeqInterp::Step, {0, 0, 0, 0, 0, 0, 0.5, 1.25}); k.dur = 2; k.text = "Audio/creak.wav"; au.keys.push_back(k); }
    OcSequence as = seqOf("A", 6, {au});
    Recorder ar;
    const game::SequenceHooks ah = ar.hooks();
    game::SequencePlayer a1;
    a1.setSequence(as);
    a1.setHooks(&ah);
    a1.setMode(game::SeqMode::Live);
    a1.setTime(0.0);
    a1.play();
    a1.advance(1.2);
    pre(a1, w);
    check(ar.played.size() == 1 && ar.sounds.size() == 1 && ar.sounds[0] == "Audio/creak.wav", "crossing the start forward plays the sound");
    check(ar.played.size() == 1 && approx(ar.played[0].volume, 0.5) && approx(ar.played[0].pitch, 1.25) && !ar.played[0].spatial, "with its volume and pitch, flat without an actor");
    a1.advance(0.5);
    pre(a1, w);
    check(ar.played.size() == 1 && !ar.volumes.empty(), "it keeps playing (and its volume is updated), not restarted");
    a1.advance(1.5);   // t = 3.2, past the end at 3.0
    pre(a1, w);
    check(ar.stopped.size() == 1 && ar.stopped[0] == 10, "it stops when the section ends");

    ar = Recorder{};
    const game::SequenceHooks ah2 = ar.hooks();
    game::SequencePlayer a2;
    a2.setSequence(as);
    a2.setHooks(&ah2);
    a2.setMode(game::SeqMode::Live);
    a2.setTime(1.5);   // inside the section, entered by a jump
    a2.play();
    a2.advance(0.2);
    pre(a2, w);
    check(ar.played.empty(), "a section entered mid-way stays silent");

    ar = Recorder{};
    const game::SequenceHooks ah3 = ar.hooks();
    game::SequencePlayer a3;
    a3.setSequence(as);
    a3.setHooks(&ah3);
    a3.setMode(game::SeqMode::Live);
    a3.setTime(0.0);
    a3.play();
    a3.advance(1.2);
    pre(a3, w);
    a3.pause();
    pre(a3, w);
    check(ar.played.size() == 1 && ar.stopped.size() == 1, "pausing stops the voice");
    a3.play();
    a3.setTime(0.0);
    pre(a3, w);
    check(ar.stopped.size() == 1, "a jump with nothing playing has nothing to stop");

    ar = Recorder{};
    const game::SequenceHooks ah4 = ar.hooks();
    game::SequencePlayer a4;
    a4.setSequence(as);
    a4.setHooks(&ah4);
    a4.setMode(game::SeqMode::Live);
    a4.setTime(0.0);
    a4.play();
    a4.advance(1.2);
    pre(a4, w);
    a4.setTime(4.0);   // a jump while the voice plays
    pre(a4, w);
    check(ar.played.size() == 1 && ar.stopped.size() == 1, "a jump stops the voice");

    // Preview: silent unless the audition toggle is on; Offline never plays.
    ar = Recorder{};
    const game::SequenceHooks ah5 = ar.hooks();
    game::SequencePlayer a5;
    a5.setSequence(as);
    a5.setHooks(&ah5);
    a5.setMode(game::SeqMode::Preview);
    a5.setTime(0.0);
    a5.play();
    a5.advance(1.2);
    pre(a5, w);
    check(ar.played.empty(), "the preview is silent by default");
    game::SequencePlayer a6;
    a6.setSequence(as);
    a6.setHooks(&ah5);
    a6.setMode(game::SeqMode::Preview);
    a6.setAudition(true);
    a6.setTime(0.0);
    a6.play();
    a6.advance(1.2);
    pre(a6, w);
    check(ar.played.size() == 1, "with audition on the preview plays audio sections");
    game::SequencePlayer a7;
    a7.setSequence(as);
    a7.setHooks(&ah5);
    a7.setMode(game::SeqMode::Offline);
    a7.setAudition(true);
    a7.setTime(0.0);
    a7.play();
    a7.advance(1.2);
    pre(a7, w);
    check(ar.played.size() == 1, "and an offline render never does");

    // Positional audio follows its actor.
    ar = Recorder{};
    const game::SequenceHooks ah6 = ar.hooks();
    OcSeqTrack pos = track(OcSeqTrackKind::Audio, 0, {});
    { OcSeqKey k = key(0, OcSeqInterp::Step, {0, 0, 0, 1, 0, 0, 1, 1}); k.dur = 2; k.text = "Audio/step.wav"; pos.keys.push_back(k); }
    Transform dt;
    dt.position = Vec3{10, 20, 30};
    w.setLocalTransform(door, dt);
    w.flush();
    OcSequence ps = seqOf("Pos", 4, {pos});
    game::SequencePlayer a8;
    a8.setSequence(ps);
    a8.setHooks(&ah6);
    a8.setMode(game::SeqMode::Live);
    a8.bind({door});
    a8.setTime(0.0);
    a8.play();
    a8.advance(0.5);
    pre(a8, w);
    check(ar.played.size() == 1 && ar.played[0].spatial && approx(ar.played[0].pos.x, 10) && approx(ar.played[0].pos.z, 30) &&
              approx(ar.played[0].innerCm, 300) && approx(ar.played[0].outerCm, 3000),
          "a positional sound starts at its actor with the default radii");

    // No hooks at all: silent, and nothing breaks.
    game::SequencePlayer quiet;
    quiet.setSequence(as);
    quiet.setMode(game::SeqMode::Live);
    quiet.setTime(0.0);
    quiet.play();
    quiet.advance(1.2);
    pre(quiet, w);
    check(true, "a player without hooks runs its triggered tracks silently");
    destroy(w, {door});
}

} // namespace

#endif

int main() {
    AVER_INFO("SequenceTracksTest");
#if AVER_MODULE_SCENE
    scene::World& w = scene::World::instance();
    testProperties(w);
    testVisibility(w);
    testTransformAndCameras(w);
    testSubSequences(w);
    testTriggered(w);
#endif
    AVER_INFO("=== {} assertions, {} failed ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
