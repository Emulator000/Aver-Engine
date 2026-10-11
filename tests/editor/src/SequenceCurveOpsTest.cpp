// SequenceCurveOpsTest -- the sequencer's curve editing with no UI (SequenceCurveOps.hpp): the tangent
// presets and how a key reads back, editing one channel without moving the others, retiming a key, and that
// inserting a key on a Cubic curve does not change the curve. Exit code = failure count.
#include "SequenceCurveOps.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/game/LevelSequence.hpp"

#include <cmath>
#include <initializer_list>
#include <string>

using namespace aver;
using namespace aver::editor;
using fmt::OcSeqInterp;
using fmt::OcSeqKey;
using fmt::OcSeqTrack;
using fmt::OcSeqTrackKind;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool approx(f64 a, f64 b, f64 eps = 1e-6) { return std::fabs(a - b) <= eps; }

static OcSeqKey key(f64 t, OcSeqInterp in, f64 x, f64 y = 0) {
    OcSeqKey k;
    k.t = t;
    k.interp = in;
    k.v[0] = x;
    k.v[1] = y;
    return k;
}

static OcSeqTrack track(std::initializer_list<OcSeqKey> keys) {
    OcSeqTrack t;
    t.kind = OcSeqTrackKind::Transform;
    t.keys = keys;
    return t;
}

int main() {
    AVER_INFO("=== SequenceCurveOps ===");

    // ---- tangent modes ---------------------------------------------------------------------------------
    {
        OcSeqTrack tr = track({key(0, OcSeqInterp::Cubic, 0, 5), key(1, OcSeqInterp::Cubic, 10, 5), key(3, OcSeqInterp::Cubic, 50, 5)});
        check(seqKeyTangentMode(tr.keys[1], 0) == SeqTangentMode::Auto, "a fresh key reads Auto");
        check(seqTangentChannels(tr) == 9, "a transform key stores slopes for 9 channels");

        seqSetTangentMode(tr, 1, SeqTangentMode::Linear);
        check(tr.keys[1].tan.size() == 18 && approx(tr.keys[1].tan[0], 10.0) && approx(tr.keys[1].tan[1], 20.0),
              "Linear points each side at its neighbour: arrive 10 / s, leave 20 / s");
        check(seqKeyTangentMode(tr.keys[1], 0) == SeqTangentMode::Broken, "different sides read as Broken");
        check(approx(tr.keys[1].tan[2], 0.0) && approx(tr.keys[1].tan[3], 0.0), "a constant channel has flat slopes");

        seqSetTangentMode(tr, 1, SeqTangentMode::Flat);
        check(seqKeyTangentMode(tr.keys[1], 0) == SeqTangentMode::Flat && approx(tr.keys[1].tan[1], 0.0), "Flat is zero slopes");

        seqSetTangent(tr, 1, 0, 5.0, 5.0, false);
        check(seqKeyTangentMode(tr.keys[1], 0) == SeqTangentMode::User, "equal non-zero slopes read as User");
        seqSetTangent(tr, 1, 0, 5.0, 9.0, true);
        check(seqKeyTangentMode(tr.keys[1], 0) == SeqTangentMode::Broken && approx(tr.keys[1].tan[0], 5.0) && approx(tr.keys[1].tan[1], 9.0),
              "a broken edit keeps the two slopes apart");
        seqSetTangent(tr, 1, 0, 7.0, 99.0, false);
        check(approx(tr.keys[1].tan[0], 7.0) && approx(tr.keys[1].tan[1], 7.0), "an unbroken edit mirrors the arrive slope");

        seqSetTangentMode(tr, 1, SeqTangentMode::Broken);
        check(approx(tr.keys[1].tan[0], 7.0), "Break leaves the slopes as they are");
        seqSetTangent(tr, 1, 0, 3.0, 8.0, true);
        seqSetTangentMode(tr, 1, SeqTangentMode::User);
        check(approx(tr.keys[1].tan[0], tr.keys[1].tan[1]), "User makes arrive equal leave");

        seqSetTangentMode(tr, 1, SeqTangentMode::Auto);
        check(tr.keys[1].tan.empty() && seqKeyTangentMode(tr.keys[1], 0) == SeqTangentMode::Auto, "Auto clears the slopes, and the mode is Auto again");
    }

    // ---- editing one channel pins the others at their automatic slopes -------------------------------------
    {
        OcSeqTrack tr = track({key(0, OcSeqInterp::Cubic, 0, 0), key(2, OcSeqInterp::Cubic, 4, 20), key(4, OcSeqInterp::Cubic, 8, 0)});
        f64 arrive = 0, leave = 0;
        game::seqAutoTangent(tr.keys, 1, 1, arrive, leave);   // channel 1 (y): (0 - 0) / 4 = 0 for key 1? neighbours are 0 and 0
        const f64 autoY = leave;
        game::seqAutoTangent(tr.keys, 1, 0, arrive, leave);
        const f64 autoX = leave;
        seqSetTangent(tr, 1, 2, 3.0, 3.0, false);   // edit channel 2 only
        check(tr.keys[1].tan.size() == 18, "editing one channel materialises the key's slopes");
        check(approx(tr.keys[1].tan[0], autoX) && approx(tr.keys[1].tan[2], autoY), "the other channels keep the slopes they had");
        check(approx(tr.keys[1].tan[4], 3.0), "and the edited one has its new slope");
        seqSetTangent(tr, 1, 99, 1.0, 1.0, false);
        check(tr.keys[1].tan.size() == 18, "a channel past the end is ignored");
    }

    // ---- moving a key keeps the order ---------------------------------------------------------------------
    {
        OcSeqTrack tr = track({key(0, OcSeqInterp::Linear, 0), key(1, OcSeqInterp::Linear, 10), key(2, OcSeqInterp::Linear, 20)});
        const usize at = seqMoveKey(tr, 0, 1.5, 0, 77.0, 10.0);
        check(at == 1 && approx(tr.keys[0].t, 1.0) && approx(tr.keys[1].t, 1.5) && approx(tr.keys[2].t, 2.0), "a key moved past its neighbour re-sorts and returns its new index");
        check(approx(tr.keys[1].v[0], 77.0), "and carries its new value");
        check(seqMoveKey(tr, 2, 99.0, 0, 5.0, 4.0) == 2 && approx(tr.keys[2].t, 4.0), "the time is clamped to the sequence length");
        check(seqMoveKey(tr, 0, -3.0, 0, 5.0, 4.0) == 0 && approx(tr.keys[0].t, 0.0), "and to zero");
        check(seqMoveKey(tr, 9, 1.0, 0, 0.0, 4.0) == 9, "an index out of range changes nothing");
    }

    // ---- inserting a key does not change a Cubic curve -------------------------------------------------------
    {
        OcSeqTrack tr = track({key(0, OcSeqInterp::Cubic, 0, 3), key(2, OcSeqInterp::Cubic, 10, -1), key(5, OcSeqInterp::Cubic, 4, 8)});
        seqSetTangent(tr, 1, 0, 6.0, -2.0, true);   // an asymmetric explicit slope on the middle key
        const f64 times[] = {0.2, 0.9, 1.4, 1.99, 2.0, 2.6, 3.7, 4.9};
        f64 before[8][2];
        for (int i = 0; i < 8; ++i) { before[i][0] = game::sampleSeqChannel(tr, 0, times[i]); before[i][1] = game::sampleSeqChannel(tr, 1, times[i]); }
        const f64 want31 = game::sampleSeqChannel(tr, 0, 3.1);
        const usize at = seqInsertKeyAt(tr, 3.1, 1);
        check(at == 2 && tr.keys.size() == 4, "the new key lands between its neighbours");
        check(approx(tr.keys[at].v[0], want31), "it takes the curve's value there");
        bool same = true;
        for (int i = 0; i < 8; ++i) {
            same = same && approx(game::sampleSeqChannel(tr, 0, times[i]), before[i][0], 1e-6);
            same = same && approx(game::sampleSeqChannel(tr, 1, times[i]), before[i][1], 1e-6);
        }
        check(same, "the Cubic curve is unchanged by the insert (both channels, eight sample times)");
        check(tr.keys[at].interp == OcSeqInterp::Cubic && !tr.keys[at].tan.empty(), "the new key is Cubic with explicit slopes");

        // A Smooth curve: the inserted key sits on the curve at its time.
        OcSeqTrack sm = track({key(0, OcSeqInterp::Smooth, 0), key(1, OcSeqInterp::Smooth, 10), key(2, OcSeqInterp::Smooth, 15), key(3, OcSeqInterp::Smooth, 40)});
        const f64 want = game::sampleSeqChannel(sm, 0, 1.4);
        const usize s = seqInsertKeyAt(sm, 1.4, 1);
        check(approx(sm.keys[s].v[0], want), "inserting on a Smooth curve takes its value at that time");
        check(approx(game::sampleSeqChannel(sm, 0, 1.4), want, 1e-6), "and the curve passes through the new key");

        // Before the first key.
        OcSeqTrack e = track({key(2, OcSeqInterp::Linear, 5), key(4, OcSeqInterp::Linear, 9)});
        const usize f = seqInsertKeyAt(e, 0.5, 1);
        check(f == 0 && approx(e.keys[0].v[0], 5.0), "before the first key the curve holds its first value");
    }

    // ---- bounds ---------------------------------------------------------------------------------------------
    {
        OcSeqTrack tr = track({key(1, OcSeqInterp::Linear, 3, -4), key(5, OcSeqInterp::Linear, 9, 12)});
        f64 t0, t1, v0, v1;
        seqCurveBounds(tr, 0x1, 1, t0, t1, v0, v1);
        check(approx(t0, 1) && approx(t1, 5) && approx(v0, 3) && approx(v1, 9), "bounds of channel 0");
        seqCurveBounds(tr, 0x3, 1, t0, t1, v0, v1);
        check(approx(v0, -4) && approx(v1, 12), "bounds of channels 0 and 1");
        OcSeqTrack flat = track({key(2, OcSeqInterp::Linear, 7)});
        seqCurveBounds(flat, 0x1, 1, t0, t1, v0, v1);
        check(t1 > t0 && v1 > v0, "a flat, single-key curve is padded so it can be framed");
        OcSeqTrack none;
        none.kind = OcSeqTrackKind::Transform;
        seqCurveBounds(none, 0x1, 1, t0, t1, v0, v1);
        check(approx(t0, 0) && approx(t1, 1) && approx(v0, 0) && approx(v1, 1), "an empty track frames the unit box");
    }

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== PASS: {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== FAIL: {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
