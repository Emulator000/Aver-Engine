// SequenceEditorTest -- the Sequencer's model behind Animate mode (SequenceEditor.hpp), with no UI: every
// sequence of a level loads and saves back (the old "keep the first, drop the rest" is gone), switching and
// managing sequences, the preview's writes and how restoreBases undoes them, the camera queries the render
// queue uses, a render pose in Offline mode fires nothing, and a .ocseq imported by name.
// Exit code = failure count.
#include "SequenceEditor.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcSeq.hpp"
#include "aver/platform/FileSystem.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/World.hpp"

#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

using namespace aver;
using namespace aver::editor;
using fmt::OcSeqInterp;
using fmt::OcSeqKey;
using fmt::OcSeqTrack;
using fmt::OcSeqTrackKind;
using fmt::OcSequence;

static int g_checks = 0, g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("   ok    {}", what); return; }
    AVER_ERROR("   FAIL  {}", what);
    ++g_failures;
}

static bool approx(f64 a, f64 b, f64 eps = 1e-3) { return std::fabs(a - b) <= eps; }

static OcSeqKey key(f64 t, std::initializer_list<f64> vals, OcSeqInterp in = OcSeqInterp::Step) {
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

int main() {
    AVER_INFO("=== SequenceEditor ===");
    scene::World& w = scene::World::instance();
    using K = OcSeqTrackKind;

    const scene::Entity lamp = w.create("lamp");
    auto* light = static_cast<scene::CLight*>(w.addComponent(lamp, scene::kComponentLight));
    light->intensityLux = 1000.0f;
    const scene::Entity door = w.create("door");
    auto* mr = static_cast<scene::CMeshRenderer*>(w.addComponent(door, scene::kComponentMeshRenderer));
    mr->flags = scene::kMeshRendererVisible;
    const std::vector<scene::Entity> placements = {lamp, door};
    const std::unordered_map<u32, i32> slotOf = {{static_cast<u32>(lamp), 0}, {static_cast<u32>(door), 1}};

    // The level: three sequences. Main has a property, a visibility, a free camera and an event track.
    OcSequence main1;
    main1.name = "Main";
    main1.length = 6;
    main1.tracks.push_back(track(K::Property, 0, {key(0, {5000})}));
    main1.tracks.back().property = "CLight.intensityLux";
    main1.tracks.push_back(track(K::Visibility, 1, {key(0, {0, 1})}));
    main1.tracks.push_back(track(K::Camera, -1, {key(0, {0, 0, 100, 0, 0}, OcSeqInterp::Smooth)}));
    main1.tracks.back().name = "Wide";
    OcSeqTrack ev = track(K::Event, -1, {});
    { OcSeqKey k = key(0.5, {0}); k.text = "Boom"; ev.keys.push_back(k); }
    main1.tracks.push_back(ev);
    OcSequence alt;
    alt.name = "Alt";
    alt.length = 3;
    alt.autoplay = false;
    alt.tracks.push_back(track(K::Transform, 1, {key(0, {1, 2, 3}, OcSeqInterp::Linear)}));
    OcSequence cams;
    cams.name = "Cams";
    OcSeqTrack camTrack = track(K::Camera, -1, {key(0, {500, 0, 0, 0, 0}, OcSeqInterp::Smooth)});
    camTrack.name = "Only";
    cams.tracks.push_back(camTrack);
    OcSeqTrack sub = track(K::SubSequence, -1, {});
    { OcSeqKey k = key(0, {}); k.dur = 2; k.text = "Alt"; sub.keys.push_back(k); }
    cams.tracks.push_back(sub);

    SequenceEditor ed;
    ed.load({main1, alt, cams}, placements);
    check(ed.sequenceCount() == 3, "all three sequences load (the editor no longer keeps just the first)");
    check(ed.sequenceNames() == std::vector<std::string>({"Main", "Alt", "Cams"}) && ed.sequenceName() == "Main", "in the level's order, the first current");

    fmt::OcWorldData out;
    ed.save(out, slotOf);
    check(out.sequences.size() == 3, "and all three save back");
    if (out.sequences.size() == 3) {
        check(out.sequences[0].name == "Main" && out.sequences[0].tracks.size() == 4 && out.sequences[1].name == "Alt" && out.sequences[2].name == "Cams", "names, order and track counts survive");
        check(out.sequences[0].tracks[0].target == 0 && out.sequences[0].tracks[1].target == 1 && out.sequences[1].tracks[0].target == 1, "actors map back to their slots");
        check(out.sequences[0].tracks[0].property == "CLight.intensityLux" && out.sequences[2].tracks[1].keys[0].text == "Alt" && !out.sequences[1].autoplay,
              "property names, sub-sequence references and flags survive");
    }

    // Switching keeps the order, and the other sequences are not touched.
    check(ed.switchSequence(1) && ed.sequenceName() == "Alt" && ed.sequenceCount() == 3, "switching makes another sequence current");
    fmt::OcWorldData out2;
    ed.save(out2, slotOf);
    check(out2.sequences.size() == 3 && out2.sequences[0].name == "Main" && out2.sequences[1].name == "Alt" && out2.sequences[2].name == "Cams", "the level's order is kept while another is current");
    check(!ed.switchSequence(1) && !ed.switchSequence(9), "switching to the current or to nothing is a no-op");

    // Camera queries.
    check(!ed.hasCamera() && ed.anyCamera(), "Alt has no camera but the level does");
    std::string why;
    check(ed.selectRunSequence("Cams", &why) && ed.sequenceName() == "Cams" && ed.hasCamera(), "a render job selects its sequence by name");
    check(!ed.selectRunSequence("Nope", &why) && why.find("'Main'") != std::string::npos && why.find("'Cams'") != std::string::npos, "an unknown name fails and lists the level's sequences");
    check(ed.selectRunSequence("", &why) && ed.sequenceName() == "Main", "an empty name picks the first sequence with a camera");

    // The preview writes into the scene and restoreBases puts it back.
    ed.enter();
    SequenceTickCtx ctx;
    ed.tickPre(ctx);
    ed.tickPost();
    check(approx(light->intensityLux, 5000.0) && (mr->flags & scene::kMeshRendererVisible) == 0, "the preview writes the property and hides the actor");
    check(ed.basesApplied(), "and remembers what it overwrote");
    ed.restoreBases();
    check(approx(light->intensityLux, 1000.0) && (mr->flags & scene::kMeshRendererVisible) != 0 && !ed.basesApplied(),
          "restoreBases puts back the light and the visibility");
    ed.tickPre(ctx);
    ed.tickPost();
    check(approx(light->intensityLux, 5000.0), "and the next frame re-applies the sequence");
    ed.leave();
    check(approx(light->intensityLux, 1000.0), "leaving Animate restores too");

    // A render pose is Offline: sampled tracks only, no event, no sound.
    {
        std::vector<std::string> fired;
        game::SequenceHooks hooks;
        hooks.fireEvent = [&](const game::SeqEventCall& c) { fired.push_back(c.name); };
        ed.setHooks(hooks);
        ed.enter();
        ed.beginRun();
        ed.runAt(1.0);
        ed.tickPre(ctx);
        ed.tickPost();
        check(approx(light->intensityLux, 5000.0), "a render pose evaluates the sampled tracks");
        check(fired.empty(), "and fires no event (the key at 0.5 s was crossed by the jump)");
        check(ed.runFrameCount() == 360 && approx(ed.runFps(), 60.0), "frames in one pass");
        ed.endRun();
        ed.leave();
    }

    // Managing sequences.
    check(ed.newSequence("Extra") && ed.sequenceCount() == 4 && ed.sequenceName() == "Extra", "New adds a sequence and makes it current");
    check(ed.duplicateSequence() && ed.sequenceCount() == 5 && ed.sequenceName() == "Extra Copy", "Duplicate copies it under a free name");
    check(!ed.renameSequence("Main") && !ed.renameSequence(""), "a taken or empty name is refused");
    check(ed.renameSequence("Renamed") && ed.sequenceName() == "Renamed", "Rename works");
    // A rename follows into sub-sequence sections that named the old one.
    check(ed.switchSequence(2), "to Cams");
    check(ed.renameSequence("Cams2"), "rename");
    ed.switchSequence(1);
    check(ed.renameSequence("Alt2"), "rename Alt (Cams has a section naming it)");
    fmt::OcWorldData out3;
    ed.save(out3, slotOf);
    bool followed = false;
    for (const OcSequence& s : out3.sequences)
        for (const OcSeqTrack& tr : s.tracks)
            if (tr.kind == K::SubSequence && !tr.keys.empty() && tr.keys[0].text == "Alt2") followed = true;
    check(followed, "the sub-sequence section that named it followed the rename");
    const usize before = ed.sequenceCount();
    check(ed.deleteSequence() && ed.sequenceCount() == before - 1, "Delete removes the current sequence");

    // A .ocseq imported by name.
    {
        std::error_code ec;
        const std::string dir = (std::filesystem::temp_directory_path(ec) / "aver-seq-editor-test").string();
        std::filesystem::create_directories(dir, ec);
        const std::string path = dir + "/imp.ocseq";
        fmt::OcSeqFile file;
        OcSequence imp;
        imp.name = "Imp";
        OcSeqTrack t = track(K::Property, -1, {key(0, {42})});
        t.property = "CLight.intensityLux";
        t.bind = "lamp";
        imp.tracks.push_back(t);
        OcSeqTrack lost = track(K::Visibility, -1, {key(0, {0, 1})});
        lost.bind = "nobody";
        imp.tracks.push_back(lost);
        file.sequences.push_back(imp);
        check(writeFileText(path, fmt::writeOcseq(file)), "the .ocseq is written");
        const usize n = ed.sequenceCount();
        const auto find = [&](const std::string& name) { return name == "lamp" ? lamp : scene::kInvalidEntity; };
        check(ed.importOcseq(path, find) && ed.sequenceCount() == n + 1 && ed.sequenceName() == "Imp", "import adds the file's sequence and makes it current");
        fmt::OcWorldData out4;
        ed.save(out4, slotOf);
        const OcSequence* got = nullptr;
        for (const OcSequence& s : out4.sequences) if (s.name == "Imp") got = &s;
        check(got && got->tracks.size() == 2 && got->tracks[0].target == 0 && got->tracks[0].bind.empty(), "its name-bound actor resolved to the placement's slot");
        check(got && got->tracks.size() == 2 && got->tracks[1].bind == "nobody" && got->tracks[1].target == -1, "and a name nothing answers to is kept, unresolved");
        check(!ed.exportOcseq(dir + "/out.ocseq") && !ed.status().empty(), "export needs the level's names (it is refused outside the UI)");
        check(!ed.importOcseq(dir + "/missing.ocseq", find), "a missing file is refused");
        std::filesystem::remove_all(dir, ec);
    }

    ed.reset();
    w.destroy(lamp);
    w.destroy(door);
    w.flush();
    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== PASS: {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== FAIL: {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
