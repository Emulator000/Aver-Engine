// The editor's level-instance model (sandbox/src/LevelInstanceEditorModel.*), with no UI: place and its refusals,
// picking and locking, Create from Selection, edit dirty tracking, break + undo, the return stack.
#include "LevelInstanceFixture.hpp"

#include "LevelInstanceEditorModel.hpp"

static void writeLevel(const std::filesystem::path& file, const std::string& text) {
    fmt::OcWorldData d = parse(text);
    std::string why;
    if (!fmt::saveOcworld(file.string(), d, &why)) AVER_ERROR("could not write {}: {}", file.string(), why);
}

int main() {
#if AVER_MODULE_PHYSICS
    aver_phys_init();
#endif
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "aver_levelinst_model_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    writeLevel(dir / "A.ocworld", "OCWORLD 1\nPLACE Meshes/a.ocmesh 0 0 0 0 0 0 1\n");
    writeLevel(dir / "B.ocworld", "OCWORLD 1\nLEVELINST A.ocworld pos 0 0 0 id=7\nENDLEVELINST\n");
    writeLevel(dir / "C.ocworld", "OCWORLD 1\nPLACE Meshes/c1.ocmesh 10 0 0 0 0 0 1\nPLACE Meshes/c2.ocmesh 20 0 0 0 0 0 1\n");

    Fixture f;
    f.contentDir = dir.string();
    f.lib.setLoader(world::LevelAssetLibrary::diskLoader(dir.string()));
    f.begin(parse("OCWORLD 1\n"));
    scene::World& w = scene::World::instance();
    editor::LevelInstanceEditorModel model(w, f.li, f.lib);
    model.setContentDir(dir.string());
    model.setOpenLevel("A.ocworld");

    // ---- refusals ----
    check(editor::LevelInstanceEditorModel::assetRefFor(dir.string(), (dir / "Sub" / "X.ocworld").string()) == "Sub/X.ocworld",
          "assetRefFor gives a forward-slash content-relative path");
    check(editor::LevelInstanceEditorModel::assetRefFor(dir.string(), (dir / "X.ocmesh").string()).empty(), "...and nothing for a non-level");
    auto r = model.place("A.ocworld", Transform{});
    check(!r.ok && !model.status().empty() && f.li.count() == 0 && r.root == scene::kInvalidEntity, "A into A is refused with a status line and no entity");
    r = model.place("B.ocworld", Transform{});
    check(!r.ok && f.li.count() == 0, "B into A is refused when B contains A");
    r = model.place("Nope.ocworld", Transform{});
    check(!r.ok && f.li.count() == 0, "a level that does not parse is refused");
    check(!model.place("Meshes/a.ocmesh", Transform{}).ok, "a non-level is refused");

    // ---- place ----
    model.setOpenLevel("Host.ocworld");
    Transform at;
    at.position = Vec3{1000.0f, 0.0f, 0.0f};
    r = model.place("C.ocworld", at);
    check(r.ok && r.root != scene::kInvalidEntity && f.li.count() == 1, "C places");
    const fmt::OcLevelInstance* rec = f.li.recordOf(r.root);
    check(rec && rec->id != 0 && rec->asset == "C.ocworld", "the instance has an id");
    fmt::OcWorldData onDisk;
    check(fmt::loadOcworld((dir / "C.ocworld").string(), onDisk) && onDisk.placements.size() == 2 &&
              onDisk.placements[0].placementId != 0 && onDisk.placements[1].placementId != 0 &&
              onDisk.placements[0].placementId != onDisk.placements[1].placementId,
          "placing gave the child's placements stable ids on disk");
    const scene::Entity root = r.root;
    flush();
    const scene::Entity m1 = [&] {
        for (const scene::Entity e : descendants(root)) if (std::string(w.name(e)) == "Meshes/c1.ocmesh") return e;
        return scene::kInvalidEntity;
    }();
    check(m1 != scene::kInvalidEntity, "the instance's members exist");

    // ---- picking and locking ----
    check(model.pickTarget(m1, false) == root, "a plain click on a member picks the instance root");
    check(model.pickTarget(m1, true) == m1, "Alt+click picks the member (override mode)");
    check(model.pickTarget(root, false) == root, "the root picks itself");
    const scene::Entity plainE = w.create("Plain", scene::kInvalidEntity, Transform{});
    check(model.pickTarget(plainE, false) == plainE, "a non-member is unchanged");
    std::string why;
    check(model.structureLocked(m1, &why) && !why.empty(), "structure under a locked instance is refused with a reason");
    check(!model.structureLocked(root) && !model.structureLocked(plainE), "the root and plain entities are not locked");

    // ---- edit tracking ----
    check(!model.hasUnsavedEdit(), "nothing is being edited");
    check(model.beginEdit(root), "begin editing");
    check(model.hasUnsavedEdit(), "an instance being edited counts as unsaved");
    const scene::Entity em = f.li.find(root, fmt::ocLevelInstJoinIds({onDisk.placements[0].placementId}));
    check(model.pickTarget(em, false) == em, "while editing, a click picks the member itself");
    check(!model.structureLocked(em), "...and its structure is not locked");
    check(model.discard(root) && !model.hasUnsavedEdit(), "discard ends it");

    // ---- break + undo ----
    {
        editor::LevelInstanceEdit br;
        check(model.breakInstance(root, br), "break succeeds");
        flush();
        check(f.li.count() == 0 && !br.plain.empty(), "the instance is gone and its members are plain");
        const scene::Entity keep = br.plain.empty() ? scene::kInvalidEntity : br.plain[0];
        check(model.undo(br), "undo of a break succeeds");
        flush();
        check(f.li.count() == 1, "the instance is back");
        check(keep == scene::kInvalidEntity || !w.valid(keep), "the plain entities were removed again");
        const scene::Entity back = f.li.at(0);
        check(back != scene::kInvalidEntity && f.li.assetOf(back) == "C.ocworld", "...of the same asset");
    }

    // ---- undo of a place ----
    r = model.place("C.ocworld", Transform{});
    check(r.ok && f.li.count() == 2, "a second place");
    check(model.undo(r.edit) && f.li.count() == 1, "undo of a place removes the instance");
    check(model.redo(r.edit) && f.li.count() == 2, "redo puts it back");

    // ---- Create from selection ----
    {
        scene::Entity e1 = w.create("Meshes/s1.ocmesh", scene::kInvalidEntity, Transform{});
        Transform t1;
        t1.position = Vec3{500.0f, 100.0f, 0.0f};
        w.setLocalTransform(e1, t1);
        w.addComponent(e1, scene::kComponentMeshRenderer);
        Transform t2;
        t2.position = Vec3{700.0f, 100.0f, 0.0f};
        const scene::Entity e2 = w.create("Meshes/s2.ocmesh", scene::kInvalidEntity, t2);
        w.addComponent(e2, scene::kComponentMeshRenderer);
        flush();
        const auto capture = f.hooks().captureEntity;
        const auto cr = model.createFromSelection({e1, e2}, "Made.ocworld", capture);
        check(cr.ok && cr.instance != scene::kInvalidEntity, "Create from Selection succeeds: " + cr.error);
        fmt::OcWorldData made;
        check(fmt::loadOcworld((dir / "Made.ocworld").string(), made) && made.placements.size() == 2 && made.hasExtent,
              "the new level was written with its extent");
        if (made.placements.size() == 2) {
            check(made.placements[0].placementId != 0 && made.placements[1].placementId != 0, "...and ids");
            check(near(static_cast<f32>(made.placements[0].x), 0.0f) && near(static_cast<f32>(made.placements[1].x), 200.0f),
                  "its placements are re-origined to the first entity");
        }
        flush();
        bool found = false;
        for (const scene::Entity m : descendants(cr.instance))
            if (std::string(w.name(m)) == "Meshes/s2.ocmesh") {
                const Vec3 p = worldPos(m);
                found = near(p.x, 700.0f) && near(p.y, 100.0f);
            }
        check(found, "the instance puts the contents back at their old world poses");
        check(!model.createFromSelection({e1}, "Made.ocworld", capture).ok, "an existing level is not overwritten");
        w.destroy(e1);
        w.destroy(e2);
        flush();
    }

    // ---- the return stack ----
    {
        editor::LevelReturnPoint p;
        p.levelPath = "X.ocworld";
        p.camPos = Vec3{1, 2, 3};
        model.pushReturn(p);
        p.levelPath = "Y.ocworld";
        model.pushReturn(p);
        check(model.returnDepth() == 2 && model.returnTop() && model.returnTop()->levelPath == "Y.ocworld", "push");
        editor::LevelReturnPoint out;
        check(model.popReturn(out) && out.levelPath == "Y.ocworld" && model.returnDepth() == 1, "pop returns the last");
        check(model.popReturn(out) && !model.popReturn(out), "...and then nothing");
    }

    w.destroy(plainE);
    flush();
    f.li.end();
    std::filesystem::remove_all(dir, ec);
#if AVER_MODULE_PHYSICS
    aver_phys_shutdown();
#endif
    if (g_failures) { AVER_ERROR("LevelInstanceEditorModelTest: {} failure(s)", g_failures); return 1; }
    AVER_INFO("LevelInstanceEditorModelTest: all passed");
    return 0;
}
