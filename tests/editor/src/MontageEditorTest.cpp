// The montage editor tab's headless core: the structural edits (which repair the links between
// sections), the timeline geometry, clip-path references, and the tab's load / save / dirty / undo
// bookkeeping.
//
// Same BtEditorTest arrangement: Sandbox is add_executable-only, so this compiles the editor source
// directly with AVER_WITH_IMGUI left undefined, leaving exactly the part worth testing.
#include "MontageEditor.hpp"

#include "aver/anim/AnimGraphAsset.hpp"
#include "aver/core/Log.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace aver;
using namespace aver::editor;
namespace fs = std::filesystem;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static bool approx(f32 a, f32 b, f32 eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static i32 find(const anim::MontageAsset& a, const char* section) { return a.sectionIndex(section); }

static void segmentEdits() {
    AVER_INFO("segments");
    anim::MontageAsset a = montageStarter();
    check(!a.valid(), "the starter has no clip yet, so it is not valid");
    check(a.sections.size() == 1 && a.sections[0].name == "Default" && a.sections[0].start == 0.0f, "but it has its Default section");
    check(mtgAddSegment(a, "a.ocanim") == 0 && a.valid(), "adding a clip makes it valid");
    check(mtgAddSegment(a, "") == -1, "an empty clip name is refused");
    mtgAddSegment(a, "b.ocanim");
    mtgAddSegment(a, "c.ocanim");
    check(mtgAddSegment(a, "x.ocanim", 1) == 1 && a.segments[1].clip == "x.ocanim" && a.segments[2].clip == "b.ocanim",
          "inserting at an index shifts the rest");
    check(a.segments[1].start == 0.0f && a.segments[1].end == -1.0f && a.segments[1].rate == 1.0f, "a new segment plays the whole clip");
    check(mtgMoveSegment(a, 0, 1) == 1 && a.segments[0].clip == "x.ocanim" && a.segments[1].clip == "a.ocanim", "a segment moves down one");
    check(mtgMoveSegment(a, 3, 5) == 3, "moving past the end clamps");
    check(mtgMoveSegment(a, 9, 1) == -1, "an index out of range is refused");
    check(mtgRemoveSegment(a, 0) && a.segments.size() == 3 && a.segments[0].clip == "a.ocanim", "a segment can be removed");
    check(!mtgRemoveSegment(a, 7), "removing out of range is refused");
}

static void sectionEdits() {
    AVER_INFO("sections");
    anim::MontageAsset a = montageStarter();
    mtgAddSegment(a, "a.ocanim");

    check(mtgAddSection(a, "Slash", 0.4f) == 1, "a section lands after the first");
    check(mtgAddSection(a, "Windup", 0.2f) == 1 && a.sections[2].name == "Slash", "sections stay sorted by start");
    check(mtgAddSection(a, "Slash", 0.8f) == 3 && a.sections[3].name == "Slash 2", "a taken name gets a numbered one");
    const i32 early = mtgAddSection(a, "Early", -5.0f);
    check(early == 1 && approx(a.sections[1].start, 1.0f / 30.0f), "a start at or before 0 is pushed just after the first section");
    check(a.valid(), "the result is a valid montage");

    check(mtgSetNext(a, find(a, "Slash"), "Slash 2"), "a link to a section is accepted");
    check(mtgSetNext(a, find(a, "Slash"), "@end") && mtgSetNext(a, find(a, "Slash"), ""), "so are @end and the following section");
    check(!mtgSetNext(a, find(a, "Slash"), "Nope") && a.sections[static_cast<usize>(find(a, "Slash"))].next.empty(), "an unknown target is refused and changes nothing");
    check(!mtgSetNext(a, 9, ""), "an index out of range is refused");

    mtgSetNext(a, find(a, "Windup"), "Slash");
    check(mtgRenameSection(a, find(a, "Slash"), "Strike"), "a section can be renamed");
    check(a.sections[static_cast<usize>(find(a, "Windup"))].next == "Strike", "and every link that named it follows the rename");
    check(!mtgRenameSection(a, find(a, "Strike"), "Windup"), "renaming to a taken name is refused");
    check(!mtgRenameSection(a, find(a, "Strike"), ""), "an empty name is refused");
    check(!mtgRenameSection(a, find(a, "Strike"), "@end"), "@end is reserved");
    check(mtgRenameSection(a, find(a, "Strike"), "Strike"), "renaming to itself is a harmless yes");

    // Windup -> Strike -> Slash 2. Removing Strike hands its link to whatever led there.
    mtgSetNext(a, find(a, "Strike"), "Slash 2");
    check(mtgRemoveSection(a, find(a, "Strike")), "a section can be removed");
    check(a.sections[static_cast<usize>(find(a, "Windup"))].next == "Slash 2", "what led to it now leads where it led");

    // A loop on itself falls back to "following" for whoever led there.
    mtgSetNext(a, find(a, "Slash 2"), "Slash 2");
    mtgSetNext(a, find(a, "Windup"), "Slash 2");
    mtgRemoveSection(a, find(a, "Slash 2"));
    check(a.sections[static_cast<usize>(find(a, "Windup"))].next.empty(), "a removed self-loop leaves the link as \"following\"");
    check(a.valid(), "every link still names a section");

    // The first section is pinned at 0 even when it is the one removed.
    anim::MontageAsset b = montageStarter();
    mtgAddSegment(b, "a.ocanim");
    mtgAddSection(b, "Second", 0.5f);
    check(mtgRemoveSection(b, 0) && b.sections.size() == 1 && b.sections[0].name == "Second" && b.sections[0].start == 0.0f,
          "removing the first section re-pins the next one at 0");
    check(!mtgRemoveSection(b, 0), "the last section cannot be removed");

    // Dragging a marker.
    anim::MontageAsset c = montageStarter();
    mtgAddSection(c, "A", 0.2f);
    mtgAddSection(c, "B", 0.5f);
    check(mtgMoveSection(c, 0, 0.3f) == -1, "the first section cannot move");
    check(mtgMoveSection(c, 1, 0.9f) == 2 && c.sections[1].name == "B" && c.sections[2].name == "A", "dragging past a neighbour re-sorts and reports the new index");
    check(mtgMoveSection(c, 1, -3.0f) == 1 && c.sections[1].start > 0.0f, "a drag to before 0 stops just after the first section");
    check(mtgMoveSection(c, 9, 1.0f) == -1, "an index out of range is refused");
}

static void notifyEdits() {
    AVER_INFO("notifies");
    anim::MontageAsset a = montageStarter();
    check(mtgAddNotify(a, 0.5f, "B", 0.0f) == 0, "the first notify");
    check(mtgAddNotify(a, 0.2f, "A", 0.3f) == 0 && a.notifies[1].name == "B", "notifies stay sorted by time");
    check(mtgAddNotify(a, 0.5f, "", 0.0f) == 2 && a.notifies[2].name == "Notify", "an empty name becomes Notify, after an equal time");
    check(mtgAddNotify(a, 0.1f, "x", -1.0f) == -1, "a negative duration is refused");
    check(mtgAddNotify(a, NAN, "x", 0.0f) == -1, "a non-finite time is refused");
    check(mtgAddNotify(a, -2.0f, "Zero", 0.0f) == 0 && a.notifies[0].time == 0.0f, "a time before 0 clamps to 0");
    check(mtgMoveNotify(a, 0, 0.9f) == 3 && a.notifies[3].name == "Zero", "moving a notify re-sorts it");
    check(mtgMoveNotify(a, 9, 0.9f) == -1, "an index out of range is refused");
    check(mtgRemoveNotify(a, 3) && a.notifies.size() == 3, "a notify can be removed");
    check(!mtgRemoveNotify(a, 9), "removing out of range is refused");
}

static void timelineGeometry() {
    AVER_INFO("timeline");
    anim::MontageAsset a = montageStarter();
    mtgAddSegment(a, "c1");
    mtgAddSegment(a, "c2");
    a.segments[1].start = 0.5f;
    a.segments[1].end = 1.5f;
    a.segments[1].rate = 2.0f;
    const MtgTimeline t = mtgTimeline(a, [](const std::string& c) { return c == "c1" ? 1.0f : 2.0f; });
    check(t.starts.size() == 2 && approx(t.starts[0], 0.0f) && approx(t.starts[1], 1.0f), "segment 2 starts where segment 1 ends");
    check(approx(t.lengths[0], 1.0f) && approx(t.lengths[1], 0.5f) && approx(t.total, 1.5f), "lengths follow range and rate");

    a.segments[1].end = 9.0f;
    check(approx(mtgTimeline(a, [](const std::string&) { return 2.0f; }).lengths[1], 0.75f), "an end past the clip is clamped to the clip (1.5 s of clip at rate 2)");

    const MtgTimeline unknown = mtgTimeline(a, [](const std::string&) { return 0.0f; });
    check(approx(unknown.lengths[0], 0.0f), "an unknown clip with an open end occupies nothing");
    check(approx(unknown.lengths[1], (9.0f - 0.5f) / 2.0f), "and with an explicit end it occupies that range");
    check(mtgTimeline(a, nullptr).starts.size() == 2, "a missing duration function is treated as all unknown");

    check(approx(mtgSnap(0.12f), 4.0f / 30.0f) && approx(mtgSnap(0.1f), 0.1f) && approx(mtgSnap(0.0f), 0.0f), "snapping rounds to the 1/30 s grid");
    check(approx(mtgSnap(0.123f, 0.0f), 0.123f), "a zero step leaves the time alone");
}

static void clipPaths(const fs::path& root) {
    AVER_INFO("clip paths");
    const fs::path content = root / "proj" / "Content";
    const std::string montage = (content / "Montages" / "m.ocmontage").string();
    std::error_code ec;
    fs::create_directories(content / "Anims", ec);
    fs::create_directories(content / "Montages", ec);
    { std::ofstream(content / "Anims" / "slash.ocanim") << "x"; }
    { std::ofstream(content / "Montages" / "local.ocanim") << "x"; }

    check(fs::equivalent(mtgContentRootOf(montage), content, ec), "the content root is the nearest ancestor named Content");
    const std::string nested = (content / "Deep" / "er" / "m.ocmontage").string();
    check(fs::equivalent(mtgContentRootOf(nested), content, ec), "however deep the montage sits");
    const std::string loose = (root / "loose" / "m.ocmontage").string();
    check(mtgContentRootOf(loose) == (root / "loose").string(), "outside any Content folder the montage's own folder is the root");

    check(mtgClipRefFromPath(montage, (content / "Anims" / "slash.ocanim").string()) == "Anims/slash.ocanim",
          "a clip path becomes a content-relative reference with forward slashes");
    check(mtgClipRefFromPath(montage, (root / "elsewhere" / "far.ocanim").string()) == "far.ocanim", "a file outside the content root falls back to its name");

    const std::string hit = mtgResolveClipPath(montage, "Anims/slash.ocanim");
    check(!hit.empty() && fs::equivalent(hit, content / "Anims" / "slash.ocanim", ec), "a reference resolves against the content root");
    const std::string near = mtgResolveClipPath(montage, "local.ocanim");
    check(!near.empty() && fs::equivalent(near, content / "Montages" / "local.ocanim", ec), "or against the montage's own folder");
    check(mtgResolveClipPath(montage, "Anims/missing.ocanim").empty(), "a missing clip resolves to nothing");
    check(mtgResolveClipPath(montage, "").empty(), "and so does an empty reference");
    const std::string abs = (content / "Anims" / "slash.ocanim").string();
    check(mtgResolveClipPath(montage, abs) == abs, "an absolute path that exists is used as given");
}

static void tab(const fs::path& root) {
    AVER_INFO("the tab");
    const fs::path content = root / "proj2" / "Content";
    std::error_code ec;
    fs::create_directories(content / "Anims", ec);
    fs::create_directories(content / "Montages", ec);

    fmt::OcAnimation clip;
    clip.duration = 1.5f;
    fmt::OcTrack t;
    t.boneIndex = 0;
    t.channels = fmt::kOcChannelTranslation;
    t.times = {0.0f, 1.5f};
    t.values = {0, 0, 0, 10, 0, 0};
    clip.tracks.push_back(t);
    std::string why;
    check(fmt::saveOcAnim((content / "Anims" / "slash.ocanim").string(), clip, &why), "the clip writes " + why);

    const std::string file = (content / "Montages" / "combo.ocmontage").string();
    anim::MontageAsset a = montageStarter();
    a.name = "Combo";
    mtgAddSegment(a, "Anims/slash.ocanim");
    check(anim::saveMontage(file, a, &why), "the montage writes " + why);

    MontageEditor ed(file);
    check(ed.loaded() && ed.loadError().empty(), "it loads");
    check(!ed.dirty() && !ed.canUndo() && !ed.canRedo(), "clean, with no history");
    check(ed.title().find("combo.ocmontage") != std::string::npos && ed.title().find("###mtg:") != std::string::npos, "the tab title carries the file name and a stable id");
    check(approx(ed.clipDuration("Anims/slash.ocanim"), 1.5f), "clip lengths come from the real clips");
    check(approx(ed.clipDuration("Anims/none.ocanim"), 0.0f), "an unknown clip reads 0");
    check(approx(ed.timeline().total, 1.5f), "so the timeline is 1.5 s");

    check(ed.addSection("Hit", 0.4f) == 1, "adding a section reports its index");
    check(ed.dirty() && ed.canUndo() && ed.selectedSection() == 1, "and marks the tab dirty with the section selected");
    check(ed.asset().sections.size() == 2, "the asset has it");
    ed.undo();
    check(ed.asset().sections.size() == 1 && ed.canRedo() && ed.selectedSection() == -1, "undo removes it and clears the selection");
    ed.redo();
    check(ed.asset().sections.size() == 2, "redo brings it back");

    ed.addNotify(0.5f, "Hit", 0.1f);
    check(ed.selectedNotify() == 0 && ed.asset().notifies.size() == 1, "a notify is added and selected");
    check(ed.moveNotify(0, 1.0f) == 0 && approx(ed.asset().notifies[0].time, 1.0f), "and can be moved");

    const bool undoable = ed.canUndo();
    check(!ed.renameSection(0, "") && ed.canUndo() == undoable, "a refused edit leaves no undo entry behind");
    check(ed.renameSection(1, "Strike"), "a rename goes through");
    ed.setNext(0, "Strike");
    check(ed.asset().sections[0].next == "Strike", "and links can be set");
    check(ed.removeSection(1) && ed.asset().sections[0].next.empty(), "removing a section repairs the link that named it");

    check(ed.addSegment("Anims/slash.ocanim") == 1 && ed.selectedSegment() == 1, "a second segment is added and selected");
    check(approx(ed.timeline().total, 3.0f), "the timeline doubles");
    check(ed.moveSegment(1, -1) == 0, "segments move");
    check(ed.removeSegment(0) && ed.asset().segments.size() == 1, "and are removed");

    check(ed.save(&why), "it saves " + why);
    check(!ed.dirty(), "clean after saving");
    anim::MontageAsset back;
    check(anim::loadMontage(file, back, &why) && back.name == "Combo" && back.notifies.size() == 1 && back.segments.size() == 1,
          "the file on disk has the edits");

    // A change on disk reloads a clean tab, and is ignored by a dirty one.
    anim::MontageAsset other = back;
    other.name = "Renamed";
    anim::saveMontage(file, other, &why);
    ed.onFileChanged();
    check(ed.asset().name == "Renamed", "a clean tab picks up a change on disk");
    ed.addNotify(0.1f, "Mine", 0.0f);
    other.name = "Again";
    anim::saveMontage(file, other, &why);
    ed.onFileChanged();
    check(ed.asset().name == "Renamed" && ed.dirty(), "a dirty tab keeps its edits");

    // A corrupt or missing file reports instead of crashing.
    const std::string bad = (content / "Montages" / "bad.ocmontage").string();
    { std::ofstream(bad) << "{ not a montage"; }
    MontageEditor broken(bad);
    check(!broken.loaded() && !broken.loadError().empty(), "a corrupt file reports a load error");
    check(!broken.save(&why) && !why.empty(), "and refuses to save over it");
    check(broken.addSegment("x") == -1 && broken.addSection("s", 1.0f) == -1 && !broken.removeNotify(0), "and every edit is refused");
    MontageEditor missing((content / "Montages" / "gone.ocmontage").string());
    check(!missing.loaded() && !missing.loadError().empty(), "a missing file reports a load error too");

    check(makeMontageEditor(file) != nullptr, "the factory claims .ocmontage");
    check(makeMontageEditor((content / "Anims" / "slash.ocanim").string()) == nullptr, "and nothing else");
    check(makeMontageEditor(file + ".bak") == nullptr, "not even a lookalike");
}

int main() {
    const fs::path root = fs::temp_directory_path() / "aver-montage-editor-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    segmentEdits();
    sectionEdits();
    notifyEdits();
    timelineGeometry();
    clipPaths(root);
    tab(root);

    AVER_INFO(g_failures == 0 ? "MontageEditorTest: PASS" : "MontageEditorTest: FAIL");
    return g_failures == 0 ? 0 : 1;
}
