// OcSeqFileTest -- the standalone .ocseq level-sequence file and the shared record code (OcSeq.hpp): the
// header gate, actors bound by name only (a placement `target` is ignored on read and never written), a
// track that needs an actor and has no name is dropped with a message, several sequences, and the record
// helpers the .ocworld reader shares. Exit code = failure count.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcSeq.hpp"

#include <string>

using namespace aver;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

int main() {
    using K = fmt::OcSeqTrackKind;
    using I = fmt::OcSeqInterp;
    AVER_INFO("=== .ocseq ===");

    fmt::OcSeqFile f;
    {
        fmt::OcSequence a;
        a.name = "Door Scene";
        a.length = 8;
        a.autoplay = false;
        fmt::OcSeqTrack lamp;
        lamp.kind = K::Property;
        lamp.bind = "Lamp 1";
        lamp.target = 7;   // a placement index means nothing outside a level: never written
        lamp.property = "CLight.intensityLux";
        fmt::OcSeqKey k0; k0.t = 0; k0.interp = I::Linear; k0.v[0] = 100;
        fmt::OcSeqKey k1; k1.t = 2.5; k1.interp = I::Cubic; k1.v[0] = 900; k1.tan = {10, 20};
        lamp.keys = {k0, k1};
        a.tracks.push_back(lamp);
        fmt::OcSeqTrack cam;
        cam.kind = K::Camera;
        cam.name = "Wide";
        fmt::OcSeqKey c0; c0.t = 0; c0.v[0] = -500; c0.v[2] = 150;
        cam.keys.push_back(c0);
        a.tracks.push_back(cam);
        fmt::OcSeqTrack ev;
        ev.kind = K::Event;
        fmt::OcSeqKey e0; e0.t = 1; e0.text = "Door_Open"; e0.interp = I::Step;
        ev.keys.push_back(e0);
        a.tracks.push_back(ev);
        f.sequences.push_back(a);
        fmt::OcSequence b;
        b.name = "Second";
        f.sequences.push_back(b);
    }

    const std::string text = fmt::writeOcseq(f);
    check(text.rfind("OCSEQ 1\n", 0) == 0, "the file starts with the OCSEQ 1 header");
    check(!contains(text, "target"), "target is never written");
    check(contains(text, "bind Lamp%201"), "an actor is written by its (percent-encoded) name");

    fmt::OcSeqFile back;
    std::string err;
    check(fmt::parseOcseq(text, back, &err) && err.empty(), "it parses without a note: " + err);
    check(back.sequences.size() == 2, "several sequences come back");
    if (back.sequences.size() == 2) {
        const fmt::OcSequence& a = back.sequences[0];
        check(a.name == "Door Scene" && a.length == 8 && !a.autoplay && a.tracks.size() == 3, "the first sequence round-trips");
        if (a.tracks.size() == 3) {
            check(a.tracks[0].bind == "Lamp 1" && a.tracks[0].target == -1, "the bind name survives and the target stays unset");
            check(a.tracks[0].keys.size() == 2 && a.tracks[0].keys[1].interp == I::Cubic && a.tracks[0].keys[1].tan.size() == 2,
                  "keys, cubic and slopes survive");
            check(a.tracks[1].name == "Wide" && a.tracks[2].keys[0].text == "Door_Open", "a camera name and an event key survive");
        }
        check(back.sequences[1].name == "Second", "and the second one");
    }
    check(fmt::writeOcseq(back) == text, "write(parse(write(x))) == write(x)");

    // The header gate.
    fmt::OcSeqFile g;
    check(!fmt::parseOcseq("OCSEQ 2\nSEQUENCE name A\nENDSEQUENCE\n", g, &err) && !err.empty(), "a newer version is refused");
    check(!fmt::parseOcseq("SEQUENCE name A\nENDSEQUENCE\n", g, &err), "a file without the header is refused");
    check(!fmt::parseOcseq("", g, &err), "an empty file is refused");

    // A target in the file is ignored; a track that needs an actor and names none is dropped with one message.
    const std::string loose =
        "OCSEQ 1\n"
        "SEQUENCE name L length 4\n"
        "SEQTRACK transform target 3\n"       // no bind: dropped
        "SEQKEY 0 smooth 0 0 0 0 0 0 1 1 1\n"
        "SEQTRACK transform target 3 bind Crate\n"
        "SEQKEY 0 smooth 1 2 3 0 0 0 1 1 1\n"
        "SEQTRACK event\n"
        "SEQKEY 1 step 0 text Ping\n"
        "ENDSEQUENCE\n";
    fmt::OcSeqFile h;
    check(fmt::parseOcseq(loose, h, &err), "a file with a stray target parses");
    check(!err.empty(), "and says a track was dropped: " + err);
    check(h.sequences.size() == 1 && h.sequences[0].tracks.size() == 2, "only the track without a bind name is dropped");
    if (h.sequences.size() == 1 && h.sequences[0].tracks.size() == 2) {
        check(h.sequences[0].tracks[0].bind == "Crate" && h.sequences[0].tracks[0].target == -1, "the target is ignored on read");
        check(h.sequences[0].tracks[1].kind == K::Event, "an event track needs no actor");
    }

    // The record helpers: kinds and the binding rule.
    check(std::string(fmt::seqTrackKindName(K::CameraCut)) == "cut" && std::string(fmt::seqTrackKindName(K::SubSequence)) == "sub",
          "the kind names are the file's spelling");
    check(fmt::seqTrackNeedsBinding(K::Transform) && fmt::seqTrackNeedsBinding(K::Property) && !fmt::seqTrackNeedsBinding(K::Event) &&
              !fmt::seqTrackNeedsBinding(K::Camera) && !fmt::seqTrackNeedsBinding(K::Audio),
          "which kinds need an actor");

    // appendSeqRecords with a remap that drops everything: only name-bound tracks and unbound kinds remain.
    {
        std::string out;
        fmt::OcSequence s;
        fmt::OcSeqTrack t;
        t.kind = K::Transform;
        t.target = 2;
        fmt::OcSeqKey k;
        t.keys.push_back(k);
        s.tracks.push_back(t);
        fmt::OcSeqTrack byName = t;
        byName.target = 2;
        byName.bind = "Crate";
        s.tracks.push_back(byName);
        fmt::appendSeqRecords(out, {s}, [](i32) { return -1; });
        check(contains(out, "bind Crate") && out.find("SEQTRACK", out.find("SEQTRACK") + 1) == std::string::npos,
              "a track whose placement is gone survives only through its bind name");
    }

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== PASS: {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== FAIL: {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
