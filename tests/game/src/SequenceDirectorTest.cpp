// SequenceDirectorTest: the director over a level's several sequences: only the autoplay ones start with the
// session, the script controls (play / pause / stop / setTime / setRate / state / bindOverride), the
// _Started / _Finished lifecycle events, which sequence owns the camera, bound entities through
// sub-sequences, and that stop puts back only what that sequence drove. docs/SEQUENCER.md.
#include "aver/game/SequenceDirector.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"
#include "aver/scene/World.hpp"

#include <cmath>
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

static OcSeqKey key(f64 t, OcSeqInterp in, std::initializer_list<f64> vals) {
    OcSeqKey k;
    k.t = t;
    k.interp = in;
    usize i = 0;
    for (const f64 v : vals) k.v[i++] = v;
    return k;
}

static OcSeqTrack track(OcSeqTrackKind kind, i32 target, std::vector<OcSeqKey> keys) {
    OcSeqTrack t;
    t.kind = kind;
    t.target = target;
    t.keys = std::move(keys);
    return t;
}

static OcSequence seqOf(const char* name, f64 length, bool autoplay, std::vector<OcSeqTrack> tracks) {
    OcSequence s;
    s.name = name;
    s.length = length;
    s.loop = false;
    s.autoplay = autoplay;
    s.tracks = std::move(tracks);
    return s;
}

static OcSeqTrack mover(i32 target, f64 x0, f64 x1, f64 len) {
    return track(OcSeqTrackKind::Transform, target, {key(0, OcSeqInterp::Linear, {x0, 0, 0}), key(len, OcSeqInterp::Linear, {x1, 0, 0})});
}

static OcSeqTrack camera(const char* name, f64 x) {
    OcSeqTrack t = track(OcSeqTrackKind::Camera, -1, {key(0, OcSeqInterp::Linear, {x, 0, 0, 0, 0})});
    t.name = name;
    return t;
}

static void pump(game::SequenceDirector& d, scene::World& w, f64 dt) {
    d.advance(dt);
    d.evaluate(w, game::SeqPhase::Pre);
    d.evaluate(w, game::SeqPhase::Post);
}

int main() {
    AVER_INFO("SequenceDirectorTest");
    scene::World& w = scene::World::instance();
    const scene::Entity a = w.create("a"), b = w.create("b"), c = w.create("c");

    std::vector<std::string> events;
    game::SequenceHooks hooks;
    hooks.fireEvent = [&](const game::SeqEventCall& e) { events.push_back(e.name); };

    OcSeqTrack sub = track(OcSeqTrackKind::SubSequence, -1, {});
    { OcSeqKey k = key(0, OcSeqInterp::Step, {}); k.dur = 5; k.text = "Inner"; sub.keys.push_back(k); }

    std::vector<OcSequence> level{
        seqOf("Intro", 40, true, {mover(0, 0, 1000, 40), camera("IntroCam", 0)}),   // long enough never to finish here
        seqOf("Outro", 4, false, {mover(1, 0, 200, 4), camera("OutroCam", 1000)}),
        seqOf("Wrapper", 6, false, {sub}),
        seqOf("Inner", 5, false, {mover(2, 0, 50, 5)}),
    };

    game::SequenceDirector d;
    d.setHooks(hooks);
    d.setMode(game::SeqMode::Live);
    d.setSequences(level);
    game::SeqBindings bind;
    bind.byPlacement = {a, b, c};
    d.setBindings(bind);
    check(d.count() == 4 && !d.empty(), "four sequences");
    check(d.find(std::string("Outro")) == 1 && d.find(fnv1a64(std::string_view("Inner"))) == 3 && d.find(std::string("Nope")) < 0, "found by name and by hash");

    // Only the flagged one autoplays.
    d.startAutoplay();
    f64 t = 0, len = 0, rate = 0;
    bool playing = false;
    check(d.state(0, t, len, rate, playing) && playing && approx(len, 40) && approx(rate, 1), "Intro autoplays");
    check(d.state(1, t, len, rate, playing) && !playing, "Outro does not");
    check(!d.state(9, t, len, rate, playing), "an unknown sequence has no state");
    check(events.size() == 1 && events[0] == "Intro_Started", "a start raises <Name>_Started once, got " + std::to_string(events.size()));
    pump(d, w, 1.0);
    check(approx(w.localTransform(a).position.x, 25.0f), "Intro moves its actor");
    check(approx(w.localTransform(b).position.x, 0.0f), "and Outro's actor stays (never started, never evaluated)");
    game::SeqCameraPose pose;
    check(d.camera(pose) && approx(pose.position.x, 0), "the camera is the started sequence's");

    // Starting another takes the camera (most recently started wins).
    check(d.play(1, -1.0, 0.0) && events.back() == "Outro_Started", "play starts Outro");
    pump(d, w, 1.0);
    check(d.camera(pose) && approx(pose.position.x, 1000), "the most recently started sequence with a camera owns the view");
    check(approx(w.localTransform(b).position.x, 50.0f), "Outro moves its actor");

    // Controls.
    check(d.pause(1), "pause");
    pump(d, w, 1.0);
    check(approx(w.localTransform(b).position.x, 50.0f), "paused: the clock holds");
    check(d.setTime(1, 2.0), "setTime");
    pump(d, w, 0.0);
    check(approx(w.localTransform(b).position.x, 100.0f), "and the pose follows the jump");
    check(d.setRate(1, 2.0) && d.play(1, -1.0, 0.0), "setRate then continue");
    check(d.state(1, t, len, rate, playing) && playing && approx(rate, 2) && approx(t, 2.0), "state reports rate, time and playing");
    check(d.playingAny(), "something is playing");
    events.clear();
    pump(d, w, 1.0);   // 2.0 + 1.0 * rate 2 = 4.0: the end of a non-looping sequence
    check(d.state(1, t, len, rate, playing) && !playing && approx(t, 4.0), "a non-looping sequence stops at its end");
    check(events.size() == 1 && events[0] == "Outro_Finished", "and raises _Finished once");
    pump(d, w, 1.0);
    check(events.size() == 1, "not again");
    check(approx(w.localTransform(b).position.x, 200.0f), "it leaves its final pose in place");

    // Stop puts back what only that sequence drove, and leaves what another still drives.
    const f32 introX = w.localTransform(a).position.x;
    check(d.stop(1), "stop Outro");
    check(approx(w.localTransform(b).position.x, 0.0f), "its actor is back where it was authored");
    check(d.state(1, t, len, rate, playing) && approx(t, 0) && !playing, "time 0, not playing");
    check(approx(w.localTransform(a).position.x, introX), "Intro's actor is untouched by that stop");
    check(d.camera(pose) && approx(pose.position.x, 0), "the camera returns to the sequence still running");
    check(!d.stop(9), "stopping an unknown sequence is refused");

    // Bound entities include sub-sequences.
    check(d.play(2, 0.0, 0.0), "start Wrapper");
    {
        const std::vector<scene::Entity> moved = d.boundEntities();
        bool hasA = false, hasC = false;
        for (const scene::Entity e : moved) { hasA = hasA || e == a; hasC = hasC || e == c; }
        check(hasA && hasC, "boundEntities covers Intro's actor and the sub-sequence's");
    }
    pump(d, w, 2.0);
    check(approx(w.localTransform(c).position.x, 20.0f), "the sub-sequence ran through its wrapper");

    // A runtime bind overrides a `bind` name for that sequence; clearing restores it.
    {
        OcSeqTrack named = mover(-1, 0, 80, 4);
        named.bind = "Hero";
        std::vector<OcSequence> lv{seqOf("Cut", 4, false, {named})};
        const f32 introX2 = w.localTransform(a).position.x;
        game::SequenceDirector e;
        e.setHooks(hooks);
        e.setSequences(lv);
        game::SeqBindings nb;
        nb.byName["Hero"] = a;
        e.setBindings(nb);
        const u64 heroHash = fnv1a64(std::string_view("Hero"));
        check(e.bindOverride(0, heroHash, b), "bindOverride finds the binding name");
        check(!e.bindOverride(0, fnv1a64(std::string_view("Villain")), b), "an unknown binding name is refused");
        e.play(0, 0.0, 0.0);
        e.advance(2.0);
        e.evaluate(w, game::SeqPhase::Pre);
        e.evaluate(w, game::SeqPhase::Post);
        check(approx(w.localTransform(b).position.x, 40.0f) && approx(w.localTransform(a).position.x, introX2), "the override redirects the track to the other actor");
        e.stop(0);
        check(e.bindOverride(0, heroHash, scene::kInvalidEntity), "clearing the override");
        e.play(0, 0.0, 0.0);
        e.advance(2.0);
        e.evaluate(w, game::SeqPhase::Post);
        check(approx(w.localTransform(a).position.x, 40.0f), "and the track drives the named actor again");
        e.restore(w);
        e.clear();
        check(e.empty(), "a cleared director is empty");
    }

    // restore puts everything back, clear forgets the sequences.
    d.restore(w);
    check(approx(w.localTransform(a).position.x, 0.0f) && approx(w.localTransform(c).position.x, 0.0f), "restore after stop leaves the authored transforms");
    d.clear();
    check(d.empty() && d.count() == 0, "clear forgets the sequences");

    for (const scene::Entity e : {a, b, c}) w.destroy(e);
    w.flush();
    AVER_INFO("=== {} assertions, {} failed ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}

#else

int main() {
    AVER_INFO("SequenceDirectorTest: the scene module is off; nothing to test");
    return 0;
}

#endif
