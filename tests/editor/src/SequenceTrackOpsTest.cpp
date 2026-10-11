// SequenceTrackOpsTest -- the sequencer's track and section editing with no UI (SequenceTrackOps.hpp): new
// tracks of every kind, camera naming and cut repair, the property field list, sections that never overlap,
// which asset a lane accepts, the editing form <-> file form conversions (bind vs target), .ocseq export
// refusals, mute/release and the streamed-level rule. Exit code = failure count.
#include "SequenceTrackOps.hpp"

#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/OcSeq.hpp"
#include "aver/scene/Components.hpp"
#include "aver/scene/World.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace aver;
using namespace aver::editor;
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

static bool approx(f64 a, f64 b, f64 eps = 1e-9) { return std::fabs(a - b) <= eps; }

static i32 tgt(scene::Entity e) { return static_cast<i32>(static_cast<u32>(e)); }

int main() {
    AVER_INFO("=== SequenceTrackOps ===");
    scene::World& w = scene::World::instance();
    using K = OcSeqTrackKind;

    // ---- new tracks ------------------------------------------------------------------------------------
    {
        const K kinds[] = {K::Transform, K::Camera, K::Material, K::Property, K::Event, K::Audio, K::Animation, K::Visibility, K::CameraCut, K::SubSequence};
        bool allKinds = true;
        for (const K k : kinds) allKinds = allKinds && seqNewTrack(k).kind == k && seqNewTrack(k).keys.empty() && seqNewTrack(k).target == -1;
        check(allKinds, "a new track of every kind is empty and unbound");
        const OcSeqKey vis = seqNeutralKey(K::Visibility, 2.0);
        check(approx(vis.t, 2.0) && vis.v[0] == 1.0 && vis.v[1] == 1.0, "a visibility key shows its actor and its descendants");
        check(approx(seqNeutralKey(K::Audio, 1.0).dur, 1.0) && seqKindIsSection(K::Audio) && seqKindIsSection(K::Animation) &&
                  seqKindIsSection(K::SubSequence) && !seqKindIsSection(K::Event),
              "section kinds have a length");
        check(std::string(seqKindLabel(K::CameraCut)) == "Camera Cuts" && seqKindLetter(K::Property) == 'P', "labels and badges");

        OcSequence seq;
        const int c0 = seqAddTrack(seq, seqNewTrack(K::Camera));
        const int c1 = seqAddTrack(seq, seqNewTrack(K::Camera));
        OcSeqTrack named = seqNewTrack(K::Camera);
        named.name = "Crane";
        const int c2 = seqAddTrack(seq, named);
        OcSeqTrack dup = seqNewTrack(K::Camera);
        dup.name = "Crane";
        const int c3 = seqAddTrack(seq, dup);
        check(seq.tracks[static_cast<usize>(c0)].name == "Camera" && seq.tracks[static_cast<usize>(c1)].name == "Camera 2", "cameras are named Camera, Camera 2");
        check(seq.tracks[static_cast<usize>(c2)].name == "Crane" && seq.tracks[static_cast<usize>(c3)].name != "Crane" && !seq.tracks[static_cast<usize>(c3)].name.empty(),
              "a camera whose name is taken gets a unique one");
        check(seqUniqueCameraName(seq) == "Camera 4", "the next free name");

        const int cut = seqAddTrack(seq, seqNewTrack(K::CameraCut));
        check(seqAddTrack(seq, seqNewTrack(K::CameraCut)) == cut && seqCutTrack(seq) == cut && seq.tracks.size() == 5, "there is at most one camera-cut track");
        { OcSeqKey k; k.t = 0; k.text = "Crane"; seq.tracks[static_cast<usize>(cut)].keys.push_back(k); }
        { OcSeqKey k; k.t = 3; k.text = "Camera"; seq.tracks[static_cast<usize>(cut)].keys.push_back(k); }
        check(seqRenameCamera(seq, c2, "Dolly"), "a camera can be renamed");
        check(seq.tracks[static_cast<usize>(cut)].keys[0].text == "Dolly" && seq.tracks[static_cast<usize>(cut)].keys[1].text == "Camera", "and the cut keys that named it follow");
        check(!seqRenameCamera(seq, c2, "Camera") && !seqRenameCamera(seq, c2, "") && !seqRenameCamera(seq, cut, "X"), "a taken name, an empty one, and a non-camera are refused");
        check(seq.tracks[static_cast<usize>(c2)].name == "Dolly", "a refusal changes nothing");
    }

    // ---- property fields -------------------------------------------------------------------------------
    {
        const scene::Entity lamp = w.create("lamp");
        w.addComponent(lamp, scene::kComponentLight);
        const std::vector<SeqFieldInfo> all = seqSequenceableFields(w, lamp, "");
        const auto has = [&](const std::vector<SeqFieldInfo>& v, const char* q) {
            return std::any_of(v.begin(), v.end(), [&](const SeqFieldInfo& f) { return f.qualified == q; });
        };
        check(has(all, "CLight.intensityLux") && has(all, "CLight.colour") && has(all, "CLocal.position") && has(all, "CLocal.rotation"),
              "float, vec3 and quaternion fields of the actor's components are listed");
        check(!has(all, "CLocal.rev") && !has(all, "CHierarchy.depth") && !has(all, "CMeshRenderer.dirty"), "read-only fields are not");
        check(!has(all, "CLight.iesProfile") && !has(all, "CName.objectId") && !has(all, "CHierarchy.parent"), "64-bit, entity and string-like fields are not");
        check(!has(all, "CCamera.fovYRad"), "a component the actor lacks is not listed for it");
        check(has(seqSequenceableFields(w, scene::kInvalidEntity, ""), "CCamera.fovYRad"), "with no actor every registered component is");
        const std::vector<SeqFieldInfo> filtered = seqSequenceableFields(w, lamp, "INTENS");
        check(filtered.size() == 1 && filtered[0].qualified == "CLight.intensityLux" && filtered[0].kind == scene::FieldKind::F32 && filtered[0].arity == 1,
              "the filter is a case-insensitive substring");
        check(seqFieldArity(scene::FieldKind::Vec3) == 3 && seqFieldArity(scene::FieldKind::Quat) == 4 && seqFieldArity(scene::FieldKind::Bool) == 1, "field arities");

        OcSeqKey k;
        auto* light = static_cast<scene::CLight*>(w.getComponent(lamp, scene::kComponentLight));
        light->intensityLux = 1234.0f;
        check(seqKeyFromField(w, lamp, "CLight.intensityLux", k) && approx(k.v[0], 1234.0, 1e-3), "a key can be seeded from the field's current value");
        check(!seqKeyFromField(w, lamp, "CCamera.fovYRad", k) && !seqKeyFromField(w, lamp, "CLight.nope", k), "and not from a missing component or field");
        w.destroy(lamp);
        w.flush();
    }

    // ---- sections never overlap -----------------------------------------------------------------------------
    {
        OcSeqTrack tr = seqNewTrack(K::Audio);
        const int a = seqAddSection(tr, 1.0, 2.0, "a.wav", 10.0);
        const int b = seqAddSection(tr, 5.0, 2.0, "b.wav", 10.0);
        check(a == 0 && b == 1 && !seqSectionsOverlap(tr), "two sections apart");
        const int c = seqAddSection(tr, 2.0, 1.5, "c.wav", 10.0);
        check(c == 1 && approx(tr.keys[0].dur, 1.0) && approx(tr.keys[1].t, 2.0) && !seqSectionsOverlap(tr), "a section dropped inside another trims the earlier one");
        const int d = seqAddSection(tr, 4.0, 3.0, "d.wav", 10.0);
        check(d == 2 && approx(tr.keys[2].dur, 1.0) && !seqSectionsOverlap(tr), "and is itself trimmed to stop where the next begins");
        const int e = seqAddSection(tr, 8.0, 5.0, "e.wav", 10.0);
        check(e >= 0 && approx(tr.keys[static_cast<usize>(e)].dur, 2.0), "a section is trimmed to the sequence's end");
        const int rep = seqAddSection(tr, 2.0, 0.5, "again.wav", 10.0);
        check(tr.keys[static_cast<usize>(rep)].text == "again.wav" && std::count_if(tr.keys.begin(), tr.keys.end(), [](const OcSeqKey& k) { return std::fabs(k.t - 2.0) < 1e-6; }) == 1,
              "a section starting where one already starts replaces it");

        // Moving: refused over a neighbour, accepted in a gap.
        OcSeqTrack m = seqNewTrack(K::Animation);
        seqAddSection(m, 0.0, 2.0, "x.ocanim", 10.0);
        seqAddSection(m, 4.0, 2.0, "y.ocanim", 10.0);
        int idx = 0;
        check(!seqMoveSection(m, idx, 3.0, 10.0) && approx(m.keys[0].t, 0.0), "a move that would overlap is refused and changes nothing");
        check(seqMoveSection(m, idx, 1.5, 10.0) && idx == 0 && approx(m.keys[0].t, 1.5), "a move into the gap is accepted");
        idx = 1;
        check(seqMoveSection(m, idx, 99.0, 10.0) && approx(m.keys[1].t, 8.0), "and is clamped so the section stays inside the sequence");
        idx = 0;
        check(!seqMoveSection(m, idx, 7.0, 10.0) && approx(m.keys[0].t, 1.5), "moving onto the section at the end is refused too");

        // Trimming: the animation offset follows a left trim; neighbours bound the trims.
        OcSeqTrack t = seqNewTrack(K::Animation);
        seqAddSection(t, 2.0, 4.0, "clip.ocanim", 20.0);
        seqAddSection(t, 8.0, 2.0, "next.ocanim", 20.0);
        t.keys[0].v[0] = 1.0;
        check(seqTrimSectionStart(t, 0, 3.0) && approx(t.keys[0].t, 3.0) && approx(t.keys[0].dur, 3.0) && approx(t.keys[0].v[0], 2.0),
              "trimming the start of an animation advances its clip offset by the cut");
        check(!seqTrimSectionStart(t, 0, 6.5) && approx(t.keys[0].t, 3.0), "but not past its own end");
        check(seqTrimSectionEnd(t, 0, 7.0, 20.0) && approx(t.keys[0].dur, 4.0), "the end can be pulled out");
        check(!seqTrimSectionEnd(t, 0, 9.0, 20.0), "but not through the next section");
        check(!seqTrimSectionStart(t, 1, 6.0), "and a start cannot go back through the previous one");
        check(seqTrimSectionStart(t, 1, 7.5) && approx(t.keys[1].v[0], -0.5), "a start trim before the old start moves the offset back");
        check(!seqSectionsOverlap(t), "trims never produce an overlap");
    }

    // ---- which asset a lane takes -------------------------------------------------------------------------
    {
        check(seqClassifyAsset("Audio/door.WAV") == SeqAssetKind::Sound && seqClassifyAsset("m.mp3") == SeqAssetKind::Sound, "sounds by extension, any case");
        check(seqClassifyAsset("Anims/wave.ocanim") == SeqAssetKind::Clip && seqClassifyAsset("Anims/wave.ocmontage") == SeqAssetKind::Montage &&
                  seqClassifyAsset("Seq/boom.ocseq") == SeqAssetKind::Sequence && seqClassifyAsset("x.png") == SeqAssetKind::None && seqClassifyAsset("noext") == SeqAssetKind::None,
              "clips, montages, sequence files, and the rest");
        check(seqLaneAccepts(K::Audio, SeqAssetKind::Sound) && !seqLaneAccepts(K::Audio, SeqAssetKind::Clip) &&
                  seqLaneAccepts(K::Animation, SeqAssetKind::Clip) && seqLaneAccepts(K::Animation, SeqAssetKind::Montage) && !seqLaneAccepts(K::Animation, SeqAssetKind::Sound) &&
                  seqLaneAccepts(K::SubSequence, SeqAssetKind::Sequence) && !seqLaneAccepts(K::Transform, SeqAssetKind::Sound),
              "each lane takes only its own kind of asset");
        check(seqTrackKindForAsset(SeqAssetKind::Sound) == K::Audio && seqTrackKindForAsset(SeqAssetKind::Montage) == K::Animation && seqTrackKindForAsset(SeqAssetKind::Sequence) == K::SubSequence,
              "a drop on empty space makes the matching track");
    }

    // ---- editing form <-> file form ----------------------------------------------------------------------
    {
        const scene::Entity box = w.create("box");        // a placement
        const scene::Entity lamp = w.create("lamp");      // not a placement: bound by name
        const scene::Entity ghost = w.create("ghost");    // not a placement, no name
        const scene::Entity dead = w.create("dead");
        w.destroy(dead);
        w.flush();

        OcSequence ed;
        ed.name = "Ed";
        const auto bound = [&](K kind, scene::Entity e) { OcSeqTrack t = seqNewTrack(kind); t.target = tgt(e); return t; };
        ed.tracks.push_back(bound(K::Transform, box));
        ed.tracks.push_back(bound(K::Visibility, lamp));
        ed.tracks.push_back(bound(K::Property, ghost));
        ed.tracks.push_back(bound(K::Material, dead));
        OcSeqTrack unresolved = seqNewTrack(K::Property);
        unresolved.bind = "Missing Light";
        ed.tracks.push_back(unresolved);
        ed.tracks.push_back(seqNewTrack(K::Event));
        std::unordered_map<u32, i32> slotOf;
        slotOf[static_cast<u32>(box)] = 4;
        const auto bindName = [&](scene::Entity e) -> std::string { return e == lamp ? "Lamp 1" : std::string(); };
        std::vector<std::string> dropped;
        const OcSequence file = seqToFile(ed, slotOf, bindName, &dropped);
        check(file.tracks.size() == 4, "the placement, the named light, the unresolved name and the event survive; two tracks are dropped");
        check(dropped.size() == 2, "and each dropped track is listed");
        if (file.tracks.size() == 4) {
            check(file.tracks[0].target == 4 && file.tracks[0].bind.empty(), "a placement is saved by its slot");
            check(file.tracks[1].target == -1 && file.tracks[1].bind == "Lamp 1", "any other actor by its name");
            check(file.tracks[2].bind == "Missing Light" && file.tracks[2].target == -1, "an unresolved name is kept as it was");
        }

        // Back: names resolve through findByName, placement indices through the table.
        const std::vector<scene::Entity> placements = {scene::kInvalidEntity, scene::kInvalidEntity, scene::kInvalidEntity, scene::kInvalidEntity, box};
        const auto find = [&](const std::string& n) { return n == "Lamp 1" ? lamp : scene::kInvalidEntity; };
        std::vector<std::string> lost;
        const OcSequence back = seqFromFile(file, placements, find, &lost);
        check(back.tracks.size() == 4 && lost.empty(), "all four come back");
        if (back.tracks.size() == 4) {
            check(back.tracks[0].target == tgt(box), "the slot becomes the placement's entity");
            check(back.tracks[1].target == tgt(lamp), "the name becomes the named actor");
            check(back.tracks[2].target == -1 && back.tracks[2].bind == "Missing Light", "an unresolvable name stays unresolved (it shows red)");
            check(seqBindState(back.tracks[0], true) == SeqBindState::Placement && seqBindState(back.tracks[2], false) == SeqBindState::Named &&
                      seqBindState(back.tracks[3], true) == SeqBindState::Free && seqBindState(seqNewTrack(K::Transform), false) == SeqBindState::Unbound,
                  "the binding badge states");
        }
        // A placement index that points nowhere, with no name, drops the track.
        OcSequence stale;
        OcSeqTrack st = seqNewTrack(K::Transform);
        st.target = 9;
        stale.tracks.push_back(st);
        std::vector<std::string> lost2;
        check(seqFromFile(stale, placements, find, &lost2).tracks.empty() && lost2.size() == 1, "a lost placement with no name drops the track and says so");
        // `bind` wins over a stale index.
        OcSequence both;
        OcSeqTrack bt = seqNewTrack(K::Visibility);
        bt.target = 4;
        bt.bind = "Lamp 1";
        both.tracks.push_back(bt);
        const OcSequence bb = seqFromFile(both, placements, find, nullptr);
        check(bb.tracks.size() == 1 && bb.tracks[0].target == tgt(lamp), "when both are present the name wins");

        // The .ocseq form: names only; refused (with the list) for a missing or shared name.
        OcSequence oc;
        oc.name = "Oc";
        oc.tracks.push_back(bound(K::Property, lamp));
        OcSequence out;
        std::vector<std::string> problems;
        check(seqToOcseq(oc, bindName, [](const std::string&) { return 1; }, out, &problems) && problems.empty() &&
                  out.tracks.size() == 1 && out.tracks[0].bind == "Lamp 1" && out.tracks[0].target == -1,
              "export turns an actor into its name and drops the index");
        oc.tracks.push_back(bound(K::Property, ghost));
        problems.clear();
        check(!seqToOcseq(oc, bindName, [](const std::string&) { return 1; }, out, &problems) && problems.size() == 1, "an actor without a name refuses the export, listed");
        problems.clear();
        check(!seqToOcseq(oc, [&](scene::Entity) { return std::string("Twin"); }, [](const std::string&) { return 2; }, out, &problems) && problems.size() == 2,
              "so does a name that several actors share");

        // Mute releases: a muted track no longer drives its actor.
        OcSequence driven;
        OcSeqTrack d1 = bound(K::Transform, box);
        OcSeqTrack d2 = bound(K::Visibility, lamp);
        driven.tracks = {d1, d2};
        check(seqDrivenEntities(driven).size() == 2, "two actors are driven");
        driven.tracks[1].mute = true;
        const auto set = seqDrivenEntities(driven);
        check(set.size() == 1 && set.count(box) == 1, "muting a track drops its actor from the driven set");

        w.destroy(box);
        w.destroy(lamp);
        w.destroy(ghost);
        w.flush();
    }

    // ---- the streamed-level rule -------------------------------------------------------------------------
    {
        OcSequence s;
        OcSeqTrack placed = seqNewTrack(K::Transform);
        placed.target = 3;
        OcSeqTrack named = seqNewTrack(K::Property);
        named.bind = "Lamp";
        OcSeqTrack cam = seqNewTrack(K::Camera);
        OcSeqTrack cutT = seqNewTrack(K::CameraCut);
        s.tracks = {placed, named, cam, cutT};
        std::vector<OcSeqTrack> aside;
        seqSplitStreamed(s, aside);
        check(aside.size() == 1 && aside[0].target == 3, "a track naming a placement by index is kept aside");
        check(s.tracks.size() == 3 && s.tracks[0].kind == K::Property && s.tracks[1].kind == K::Camera && s.tracks[2].kind == K::CameraCut,
              "name-bound, camera and cut tracks stay editable");
    }

    AVER_INFO("==================================================");
    if (g_failures == 0) AVER_INFO("=== PASS: {} assertions, 0 failed ===", g_checks);
    else                 AVER_ERROR("=== FAIL: {} assertions, {} FAILED ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
