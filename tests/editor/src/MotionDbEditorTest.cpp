// The motion-matching database editor tab's headless core: the structural edits (tags, tag ranges and
// clip entries repair each other), content references, a build and a bake against real files in a temp
// tree, the query lab, and the tab's load / save / dirty / undo bookkeeping.
//
// Same MontageEditorTest arrangement: Sandbox is add_executable-only, so this compiles the editor source
// directly with AVER_WITH_IMGUI left undefined, leaving exactly the part worth testing.
#include "MotionDbEditor.hpp"

#include "../../anim/src/MotionTestKit.hpp"

#include "aver/core/Hash.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace aver;
using namespace aver::editor;
namespace fs = std::filesystem;
using mt::check;

static bool approx(f32 a, f32 b, f32 eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void clipEdits() {
    AVER_INFO("clip entries");
    anim::MotionAsset a = motionStarter();
    check(!a.valid(), "the starter has no clips and no skeleton yet, so it is not valid");
    check(mdbAddClip(a, "a.ocanim") == 0 && a.clips[0].clip == "a.ocanim", "adding a clip appends an entry");
    check(a.clips[0].start == 0.0f && a.clips[0].end == -1.0f && a.clips[0].loop == -1, "that plays the whole clip, looping as the clip says");
    check(mdbAddClip(a, "") == -1, "an empty reference is refused");
    mdbAddClip(a, "b.ocanim");
    mdbAddClip(a, "c.ocanim");
    check(mdbAddClip(a, "x.ocanim", 1) == 1 && a.clips[1].clip == "x.ocanim" && a.clips[2].clip == "b.ocanim", "inserting at an index shifts the rest");
    check(mdbMoveClip(a, 0, 1) == 1 && a.clips[0].clip == "x.ocanim", "an entry moves down one");
    check(mdbMoveClip(a, 3, 5) == 3 && mdbMoveClip(a, 9, 1) == -1, "moving past the end clamps, an index out of range is refused");
    check(mdbRemoveClip(a, 0) && a.clips.size() == 3 && !mdbRemoveClip(a, 7), "entries can be removed");
}

static void tagEdits() {
    AVER_INFO("tags");
    anim::MotionAsset a = motionStarter();
    mdbAddClip(a, "walk.ocanim");
    mdbAddClip(a, "stop.ocanim");
    check(mdbAddTag(a, "walk") == 0 && mdbAddTag(a, "stop") == 1, "tags are declared in order");
    check(mdbAddTag(a, "walk") == -1 && mdbAddTag(a, "") == -1, "a duplicate and an empty name are refused");
    for (int i = 0; i < 40; ++i) mdbAddTag(a, "t" + std::to_string(i));
    check(a.tags.size() == anim::kMaxMotionTags, "at most 32 tags");
    a.tags.resize(2);

    a.clips[0].tags = {"walk"};
    a.clips[1].tags = {"stop", "walk"};
    const i32 r = mdbAddRange(a, 1, 0.0f, 0.2f);
    check(r == 0, "a range is added to an entry");
    a.clips[1].ranges[0].tags = {"stop"};
    check(mdbAddRange(a, 1, 0.5f, 0.2f) == -1 && mdbAddRange(a, 9, 0.0f, 1.0f) == -1 && mdbAddRange(a, 1, std::nanf(""), 1.0f) == -1,
          "a reversed range, a bad entry and a NaN are refused");
    check(mdbAddRange(a, 1, -3.0f, -1.0f) == 1 && a.clips[1].ranges[1].start == 0.0f && a.clips[1].ranges[1].end == -1.0f,
          "a negative start clamps to 0 and end -1 means open");
    check(a.valid() == false, "(still no skeleton)");
    a.skeleton = "rig.ocskel";
    check(a.valid(), "valid once it names a skeleton");

    check(mdbRenameTag(a, 1, "halt") && a.tags[1] == "halt", "a tag can be renamed");
    check(a.clips[1].tags[0] == "halt" && a.clips[1].ranges[0].tags[0] == "halt" && a.clips[0].tags[0] == "walk",
          "and every entry and range that named it follows");
    check(!mdbRenameTag(a, 1, "walk") && !mdbRenameTag(a, 1, "") && mdbRenameTag(a, 1, "halt"), "a taken or empty name is refused, the same name is a harmless yes");
    check(mdbRemoveTag(a, 1), "a tag can be removed");
    check(a.clips[1].tags == std::vector<std::string>{"walk"} && a.clips[1].ranges[0].tags.empty(), "and is stripped from entries and ranges");
    check(a.valid(), "the asset stays valid");
    check(!mdbRemoveTag(a, 5), "an index out of range is refused");

    check(mdbMoveRange(a, 1, 0, 0.1f, 0.6f) && approx(a.clips[1].ranges[0].start, 0.1f) && approx(a.clips[1].ranges[0].end, 0.6f), "a range moves");
    check(mdbMoveRange(a, 1, 0, 0.5f, 0.2f) && a.clips[1].ranges[0].end > a.clips[1].ranges[0].start, "an end before the start is pushed past it");
    check(mdbMoveRange(a, 1, 0, -2.0f, -1.0f) && a.clips[1].ranges[0].start == 0.0f && a.clips[1].ranges[0].end == -1.0f, "start clamps, end -1 stays open");
    check(!mdbMoveRange(a, 1, 4, 0, 1) && !mdbMoveRange(a, 7, 0, 0, 1), "bad indices are refused");
    check(mdbRemoveRange(a, 1, 0) && a.clips[1].ranges.size() == 1 && !mdbRemoveRange(a, 1, 5), "a range can be removed");
}

static void featureEdits() {
    AVER_INFO("features");
    anim::MotionAsset a = motionStarter();
    check(a.bones.empty(), "the starter lists no bones (the rig decides)");
    check(mdbAddBone(a, "foot_l", 1.0f, 0.5f) == 0 && a.bones[0].vel == 0.5f, "a bone is added with its weights");
    check(mdbAddBone(a, "foot_l") == -1 && mdbAddBone(a, "") == -1, "a duplicate and an empty name are refused");
    mdbAddBone(a, "foot_r");
    check(mdbRemoveBone(a, 0) && a.bones.size() == 1 && a.bones[0].bone == "foot_r" && !mdbRemoveBone(a, 4), "bones are removed");

    check(mdbSetTrajectoryTimes(a, true, {1.0f, 0.2f, -0.5f, 0.2f, 0.7f, 0.0f, std::nanf("")}) == 3, "future times: wrong side, zero, NaN and repeats are dropped");
    check(a.futureTimes == std::vector<f32>{0.2f, 0.7f, 1.0f}, "and the rest is sorted");
    check(mdbSetTrajectoryTimes(a, false, {-0.5f, -0.2f, 0.3f}) == 2 && a.pastTimes == std::vector<f32>{-0.2f, -0.5f},
          "past times are sorted by magnitude (nearest first)");
    std::vector<f32> many;
    for (int i = 1; i <= 12; ++i) many.push_back(0.1f * static_cast<f32>(i));
    check(mdbSetTrajectoryTimes(a, true, many) == 8 && a.futureTimes.size() == 8, "at most eight times");
    check(mdbSetTrajectoryTimes(a, true, {}) == 0 && a.futureTimes.empty(), "an empty list turns the group off");

    check(approx(mdbSnap(0.12f), 4.0f / 30.0f, 1e-5f) && approx(mdbSnap(0.016f), 0.0f, 1e-6f), "times snap to the 1/30 s grid");
}

static void clipPaths(const fs::path& root) {
    AVER_INFO("content references");
    const fs::path content = root / "proj" / "Content";
    const std::string asset = (content / "Motion" / "m.ocmotion").string();
    std::error_code ec;
    fs::create_directories(content / "Anims", ec);
    fs::create_directories(content / "Motion", ec);
    { std::ofstream(content / "Anims" / "walk.ocanim") << "x"; }
    { std::ofstream(content / "Motion" / "local.ocanim") << "x"; }

    check(fs::equivalent(mdbContentRootOf(asset), content, ec), "the content root is the nearest ancestor named Content");
    check(mdbContentRootOf((root / "loose" / "m.ocmotion").string()) == (root / "loose").string(), "outside any Content folder the asset's own folder is the root");
    check(mdbClipRefFromPath(asset, (content / "Anims" / "walk.ocanim").string()) == "Anims/walk.ocanim", "a path becomes a content-relative reference with forward slashes");
    check(mdbClipRefFromPath(asset, (root / "elsewhere" / "far.ocanim").string()) == "far.ocanim", "a file outside the root falls back to its name");
    const std::string hit = mdbResolveClipPath(asset, "Anims/walk.ocanim");
    check(!hit.empty() && fs::equivalent(hit, content / "Anims" / "walk.ocanim", ec), "a reference resolves against the content root");
    const std::string near = mdbResolveClipPath(asset, "local.ocanim");
    check(!near.empty() && fs::equivalent(near, content / "Motion" / "local.ocanim", ec), "or against the asset's own folder");
    check(mdbResolveClipPath(asset, "Anims/none.ocanim").empty() && mdbResolveClipPath(asset, "").empty(), "a missing or empty reference resolves to nothing");
    check(fs::path(mdbSidecarPath(asset)).extension() == ".ocmbake" && fs::path(mdbSidecarPath(asset)).stem() == "m", "the sidecar sits beside the asset");
}

// A content tree with a rig and three locomotion clips, and an asset that uses them.
static anim::MotionAsset writeTree(const fs::path& content, const std::string& assetFile) {
    std::error_code ec;
    fs::create_directories(content / "Anims", ec);
    fs::create_directories(content / "Skeletons", ec);
    fs::create_directories(content / "Motion", ec);
    std::string why;
    fmt::saveOcSkel((content / "Skeletons" / "rig.ocskel").string(), mt::rig(), &why);
    mt::ClipTable t;
    anim::MotionAsset a = mt::locomotionAsset(t);
    for (const char* n : {"idle", "walk", "run"}) fmt::saveOcAnim((content / "Anims" / (std::string(n) + ".ocanim")).string(), *t.find(n), &why);
    a.skeleton = "Skeletons/rig.ocskel";
    for (anim::MotionClipEntry& e : a.clips) e.clip = "Anims/" + e.clip + ".ocanim";
    a.name = "Loco";
    a.bones = {{"foot_l", 0.3f, 0.3f}, {"foot_r", 0.3f, 0.3f}};
    anim::saveMotionAsset(assetFile, a, &why);
    return a;
}

static void buildAndBake(const fs::path& root) {
    AVER_INFO("build, bake, query lab");
    const fs::path content = root / "proj3" / "Content";
    const std::string file = (content / "Motion" / "loco.ocmotion").string();
    anim::MotionAsset a = writeTree(content, file);

    MdbPreview p = mdbBuildPreview(a, file);
    check(p.ok && p.db && p.db->rows() == 114 && p.report.rows == 114 && p.report.ranges == 3, "a build against the real files reports its rows and ranges (" + p.why + ")");
    check(p.report.bytes > 0 && p.report.seconds >= 0.0, "with size and time");

    // The key matches what the runtime computes from the same bytes.
    auto bytes = [](const fs::path& f) {
        std::ifstream in(f, std::ios::binary);
        return std::vector<u8>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    const u64 want = anim::motionSourceKey(a, bytes(content / "Skeletons" / "rig.ocskel"),
        {bytes(content / "Anims" / "idle.ocanim"), bytes(content / "Anims" / "walk.ocanim"), bytes(content / "Anims" / "run.ocanim")});
    check(p.sourceKey == want, "the source key is the runtime's");

    anim::MotionAsset ghost = a;
    ghost.bones.push_back({"ghost", 1.0f, 1.0f});
    ghost.clips.push_back(mt::entry("Anims/absent.ocanim"));
    const MdbPreview pg = mdbBuildPreview(ghost, file);
    bool sawBone = false, sawClip = false;
    for (const std::string& w : pg.report.warnings) { sawBone |= w.find("ghost") != std::string::npos; sawClip |= w.find("absent") != std::string::npos; }
    check(pg.ok && sawBone && sawClip && pg.report.skippedClips == 1, "missing bones and clips warn instead of failing");

    anim::MotionAsset noSkel = a;
    noSkel.skeleton = "Skeletons/none.ocskel";
    check(!mdbBuildPreview(noSkel, file).ok && !mdbBuildPreview(noSkel, file).why.empty(), "a missing skeleton fails with a reason");
    check(!mdbBuildPreview(motionStarter(), file).ok, "an invalid asset fails with a reason");

    MdbPreview baked;
    check(mdbBake(a, file, baked) && fs::exists(mdbSidecarPath(file)), "a bake writes the sidecar");
    anim::PoseDatabase loaded;
    u64 key = 0;
    check(anim::loadPoseDatabase(mdbSidecarPath(file), loaded, &key) && key == want && loaded.rows() == 114, "which loads back with the same key");

    // The lab: what the search returns.
    MdbLabInput in;
    in.currentSpeed = 150.0f;
    in.wantedSpeed = 150.0f;
    const MdbLabResult r = mdbRunQuery(*baked.db, in, 8);
    check(!r.hits.empty() && r.hits.size() <= 8 && r.query.valid, "the lab returns up to eight hits for a valid query");
    check(r.hits[0].clip == "Anims/walk.ocanim", "moving and wanting 150 cm/s lands on the walk clip");
    bool sorted = true;
    for (usize i = 1; i < r.hits.size(); ++i) sorted &= r.hits[i - 1].cost <= r.hits[i].cost;
    check(sorted && r.hits[0].groupCosts.size() == baked.db->groups().size(), "cheapest first, with a cost per feature group");
    check(r.linearMicros >= 0.0 && r.sameAnswer, "and times the linear scan");

    in.currentSpeed = 0.0f;
    in.wantedSpeed = 0.0f;
    check(mdbRunQuery(*baked.db, in, 3).hits[0].clip == "Anims/idle.ocanim" && mdbRunQuery(*baked.db, in, 3).hits.size() == 3, "standing still lands on idle");
    in.currentSpeed = 400.0f;
    in.wantedSpeed = 400.0f;
    in.require = baked.db->tagBit(fnv1a64(std::string_view("walk")));
    check(mdbRunQuery(*baked.db, in, 1).hits[0].clip == "Anims/walk.ocanim", "a required tag overrides the better fit");
    in.require = 0;
    in.exclude = 0xFFFFFFFFu;
    check(mdbRunQuery(*baked.db, in, 4).hits.empty(), "a filter that leaves nothing gives no hits");

    anim::MotionAsset kd = a;
    kd.search.accel = anim::SearchAccel::KdTree;
    MdbPreview pk = mdbBuildPreview(kd, file);
    MdbLabInput in2;
    in2.currentSpeed = 150.0f;
    in2.wantedSpeed = 150.0f;
    const MdbLabResult rk = mdbRunQuery(*pk.db, in2, 4);
    check(pk.db->hasKd() && rk.sameAnswer && rk.kdMicros >= 0.0, "with a KD tree the lab also times it and checks it agrees");
}

static void tab(const fs::path& root) {
    AVER_INFO("the tab");
    const fs::path content = root / "proj4" / "Content";
    const std::string file = (content / "Motion" / "loco.ocmotion").string();
    writeTree(content, file);

    MotionDbEditor ed(file);
    std::string why;
    check(ed.loaded() && ed.loadError().empty(), "it loads");
    check(!ed.dirty() && !ed.canUndo() && !ed.canRedo(), "clean, with no history");
    check(ed.title().find("loco.ocmotion") != std::string::npos && ed.title().find("###mdb:") != std::string::npos, "the tab title carries the file name and a stable id");
    check(approx(ed.clipDuration("Anims/walk.ocanim"), 1.0f) && approx(ed.clipDuration("Anims/none.ocanim"), 0.0f), "clip lengths come from the real clips");

    check(ed.addTag("extra") == 4 && ed.dirty() && ed.canUndo(), "a tag is added and the tab goes dirty");
    ed.undo();
    check(ed.asset().tags.size() == 4 && ed.canRedo(), "undo takes it back");
    ed.redo();
    check(ed.asset().tags.size() == 5, "redo brings it back");
    const bool undoable = ed.canUndo();
    check(ed.addTag("extra") == -1 && ed.canUndo() == undoable, "a refused edit leaves no undo entry behind");
    check(ed.addClip("Anims/walk.ocanim") == 3 && ed.selectedClip() == 3, "a clip is added and selected");
    check(ed.addRange(3, 0.0f, 0.2f) == 0 && ed.asset().clips[3].ranges.size() == 1, "a range is added");
    check(ed.renameTag(3, "halt"), "a tag renames");
    check(ed.addBone("hips") == 2 && ed.removeBone(2), "bones are added and removed");
    check(ed.setTrajectoryTimes(true, {0.3f, 0.1f}) && ed.asset().futureTimes == std::vector<f32>{0.1f, 0.3f}, "trajectory times are set sorted");
    check(ed.moveClip(3, -1) == 2 && ed.removeClip(2) && ed.asset().clips.size() == 3, "entries move and are removed");
    check(ed.removeRange(2, 0) == false, "removing a range that is gone is refused");

    check(ed.save(&why) && !ed.dirty(), "it saves " + why);
    anim::MotionAsset back;
    check(anim::loadMotionAsset(file, back, &why) && back.tags.size() == 5 && back.futureTimes.size() == 2, "the file on disk has the edits");

    // Bake from the tab: saves first when dirty, then writes the sidecar.
    ed.addTag("later");
    check(ed.dirty(), "an edit makes it dirty");
    fs::remove(mdbSidecarPath(file));
    check(ed.bake(&why) && !ed.dirty() && fs::exists(mdbSidecarPath(file)), "bake saves a dirty asset and writes the sidecar " + why);
    check(ed.database() != nullptr && ed.lastBake().ok && ed.lastBake().report.rows == ed.database()->rows(), "and keeps the database for the lab");
    anim::MotionAsset onDisk;
    anim::loadMotionAsset(file, onDisk);
    anim::PoseDatabase db;
    u64 key = 0;
    anim::loadPoseDatabase(mdbSidecarPath(file), db, &key);
    check(db.rows() > 0 && key == ed.lastBake().sourceKey, "the sidecar's key is the saved asset's");

    // A change on disk reloads a clean tab, and is ignored by a dirty one.
    anim::MotionAsset other = back;
    other.name = "Renamed";
    anim::saveMotionAsset(file, other, &why);
    ed.onFileChanged();
    check(ed.asset().name == "Renamed", "a clean tab picks up a change on disk");
    ed.addTag("mine");
    other.name = "Again";
    anim::saveMotionAsset(file, other, &why);
    ed.onFileChanged();
    check(ed.asset().name == "Renamed" && ed.dirty(), "a dirty tab keeps its edits");

    const std::string bad = (content / "Motion" / "bad.ocmotion").string();
    { std::ofstream(bad) << "{ not a database"; }
    MotionDbEditor broken(bad);
    check(!broken.loaded() && !broken.loadError().empty(), "a corrupt file reports a load error");
    check(!broken.save(&why) && !why.empty(), "and refuses to save over it");
    check(broken.addClip("x") == -1 && broken.addTag("t") == -1 && !broken.removeBone(0) && !broken.bake(&why), "and every edit is refused");
    MotionDbEditor missing((content / "Motion" / "gone.ocmotion").string());
    check(!missing.loaded() && !missing.loadError().empty(), "a missing file reports a load error too");

    check(makeMotionDbEditor(file) != nullptr, "the factory claims .ocmotion");
    check(makeMotionDbEditor((content / "Anims" / "walk.ocanim").string()) == nullptr, "and nothing else");
    check(makeMotionDbEditor(file + ".bak") == nullptr && makeMotionDbEditor(mdbSidecarPath(file)) == nullptr, "not even a lookalike or the sidecar");
}

int main() {
    const fs::path root = fs::temp_directory_path() / "aver-motiondb-editor-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    clipEdits();
    tagEdits();
    featureEdits();
    clipPaths(root);
    buildAndBake(root);
    tab(root);

    fs::remove_all(root, ec);
    AVER_INFO(mt::g_failures == 0 ? "MotionDbEditorTest: PASS" : "MotionDbEditorTest: FAIL");
    return mt::g_failures == 0 ? 0 : 1;
}
