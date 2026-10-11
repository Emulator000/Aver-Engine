#pragma once
// The editor's side of level instances, with no UI in it: placing one, what a click picks, what is locked, edit in
// place, Create from Selection, break, the override list the Details panel shows, and the return stack of Open Level /
// Back. Every mutating call fills a LevelInstanceEdit (the instance state before and after) so the host can put ONE
// entry on its undo stack; undo() and redo() just restore that state. docs/LEVEL_INSTANCES.md 2.7.
//
// THE HOOK INTO THE EDITOR is the same shape as the prefab model's:
//   * game::GameLevel owns the LevelInstances (level_.instances()) and the LevelAssetLibrary (level_.assets());
//   * Hooks on the LevelInstances register / unregister an entity with the editor's own bookkeeping (levelEntities_,
//     labels, bodies);
//   * saveLevel skips linked entities and writes instances.captureRecords() into OcWorldData::levelInstances.
//
// Plain functions and value types, so a test can drive it (tests/levelinstance/src/LevelInstanceEditorModelTest.cpp).
#if AVER_MODULE_SCENE
#include "aver/core/Math.hpp"
#include "aver/game/LevelInstances.hpp"

#include <functional>
#include <string>
#include <vector>

namespace aver::editor {

// One undoable level-instance operation.
struct LevelInstanceEdit {
    std::string label;
    game::LevelInstances::LevelInstanceState before;
    game::LevelInstances::LevelInstanceState after;
    // A BREAK leaves ordinary entities behind (`plain`); undo removes them before the instance comes back, redo breaks
    // the restored instance again and records the new ones.
    bool isBreak = false;
    u32 breakId = 0;
    std::vector<scene::Entity> plain;
};

// One override as the Details panel lists it.
struct LevelInstanceOverrideRow {
    fmt::OcPrefabOverride ov;
    std::string label;        // "Wall: material = M_Red", "Door: removed", "Lamp: position = 0 0 40"
};

// Where Open Level was pressed, so the level bar's Back button can return.
struct LevelReturnPoint {
    std::string levelPath;    // absolute
    Vec3 camPos{0, 0, 0};
    f32 yawDeg = 0.0f, pitchDeg = 0.0f;
};

class LevelInstanceEditorModel {
public:
    LevelInstanceEditorModel(scene::World& world, game::LevelInstances& instances, world::LevelAssetLibrary& library);

    // The project's Content folder: asset references are relative to it.
    void setContentDir(const std::string& dir) { contentDir_ = dir; }
    const std::string& contentDir() const { return contentDir_; }
    // The level being edited, as a content-relative reference ("" for an untitled one): the cycle check's container.
    void setOpenLevel(const std::string& assetRef) { openLevel_ = assetRef; }
    const std::string& openLevel() const { return openLevel_; }
    // "<content>/Levels/Room.ocworld" -> "Levels/Room.ocworld" (forward slashes); "" when outside or not a level.
    static std::string assetRefFor(const std::string& contentDir, const std::string& fullPath);
    std::string fullPathFor(const std::string& assetRef) const;
    static bool isLevelPath(const std::string& path);

    // ---- place ------------------------------------------------------------------------------------
    struct PlaceResult {
        bool ok = false;
        scene::Entity root = scene::kInvalidEntity;
        LevelInstanceEdit edit;
        std::string error;
    };
    // Refuses (status text, no entity) when the asset does not parse, would contain the open level (a cycle), or has a
    // cycle of its own; otherwise gives the child's placements stable ids, assigns the instance an id and creates it.
    PlaceResult place(const std::string& assetRef, const Transform& xf, scene::Entity parent = scene::kInvalidEntity);

    // ---- picking and locking ------------------------------------------------------------------------
    // What a click on `hit` selects: the instance root for a plain click, the member itself for Alt+click (override
    // mode) or when its instance is being edited. Anything that is not a member comes back unchanged.
    scene::Entity pickTarget(scene::Entity hit, bool altHeld) const;
    // True when creating, reparenting or deleting STRUCTURE under `e` is refused: `e` is a member of an instance that is
    // not being edited. `why` receives the status line.
    bool structureLocked(scene::Entity e, std::string* why = nullptr) const;
    bool isMember(scene::Entity e) const { return instances_.isMember(e); }
    scene::Entity topRootOf(scene::Entity e) const { return instances_.topRootOf(e); }

    // ---- edit in place ---------------------------------------------------------------------------------
    bool beginEdit(scene::Entity root, std::string* why = nullptr);
    bool commit(scene::Entity root, std::string* why = nullptr);
    bool discard(scene::Entity root);
    bool hasUnsavedEdit() const { return instances_.anyDirtyEdit(); }
    static const char* unsavedEditMessage() { return "Commit or discard the instance edit first"; }

    // ---- instance operations (each fills `edit`) ------------------------------------------------------------
    // Deleting a member of a locked instance is an override (`removed`): `destroy` removes the entity the editor's
    // way, the instance's state before and after is the undo entry. False for a root, a plain entity or an instance
    // being edited (those delete as ordinary entities / instances).
    bool deleteMember(scene::Entity member, LevelInstanceEdit& edit, const std::function<void(scene::Entity)>& destroy);
    bool breakInstance(scene::Entity root, LevelInstanceEdit& edit);
    bool destroyInstance(scene::Entity root, LevelInstanceEdit& edit);
    bool revertAll(scene::Entity root, LevelInstanceEdit& edit);
    bool revertOverride(scene::Entity root, const fmt::OcPrefabOverride& ov, LevelInstanceEdit& edit);
    bool setPacked(scene::Entity root, bool packed, LevelInstanceEdit& edit);
    bool setStream(scene::Entity root, i32 stream, LevelInstanceEdit& edit);   // -1 follow, 0 resident, 1 streamed
    bool rename(scene::Entity root, const std::string& name, LevelInstanceEdit& edit);
    bool reload(scene::Entity root);

    // ---- Create Level Instance from Selection ---------------------------------------------------------------------
    struct CreateResult {
        bool ok = false;
        std::string assetRef;
        scene::Entity instance = scene::kInvalidEntity;
        std::string error;
        LevelInstanceEdit edit;     // the instance's creation; the host removes the replaced entities itself
    };
    // `capture` turns one live entity into records (the editor's placement / light / decal capture); roots are
    // re-origined to the first entity's position, yaw only, so the child opens around its own origin.
    using CaptureFn = std::function<bool(scene::Entity, fmt::OcWorldData& into, i32 parentIndex)>;
    CreateResult createFromSelection(const std::vector<scene::Entity>& roots, const std::string& assetRef,
                                     const CaptureFn& capture);

    // ---- undo -----------------------------------------------------------------------------------------------------------
    bool undo(LevelInstanceEdit& e);
    bool redo(LevelInstanceEdit& e);

    // ---- Details --------------------------------------------------------------------------------------------------------------
    std::vector<LevelInstanceOverrideRow> rows(scene::Entity root);
    static std::string describe(const fmt::OcPrefabOverride& ov);
    // Why a packed instance is (or is not) showing its pack, for the Details panel; "" for an unpacked one.
    std::string packState(scene::Entity root) const { return instances_.packState(root); }
    // Placements in the child level of `root`'s instance (the Details panel warns above kBigUnitPlacements).
    u32 placementCount(scene::Entity root) const { return instances_.assetCount(root); }
    std::string assetOf(scene::Entity root) const { return instances_.assetOf(root); }
    bool editing(scene::Entity root) const { return instances_.editing(root); }
    bool isNested(scene::Entity root) const { return instances_.isNested(root); }
    const fmt::OcLevelInstance* recordOf(scene::Entity root) const { return instances_.recordOf(root); }
    // Over this many placements a child level is a big unit to stream (the Details panel warns).
    static constexpr u32 kBigUnitPlacements = 5000;

    // ---- Open Level / Back --------------------------------------------------------------------------------------------------
    void pushReturn(LevelReturnPoint p) { returnStack_.push_back(std::move(p)); }
    bool popReturn(LevelReturnPoint& out);
    usize returnDepth() const { return returnStack_.size(); }
    const LevelReturnPoint* returnTop() const { return returnStack_.empty() ? nullptr : &returnStack_.back(); }
    void clearReturns() { returnStack_.clear(); }

    const std::string& status() const { return status_; }

private:
    u64 freshInstanceId(const std::string& assetRef) const;
    LevelInstanceEdit editOf(const char* label, const std::vector<u32>& ids,
                             const game::LevelInstances::LevelInstanceState& before) const;
    template <class F>
    bool runOn(scene::Entity root, const char* label, LevelInstanceEdit& edit, F&& op);

    scene::World& world_;
    game::LevelInstances& instances_;
    world::LevelAssetLibrary& lib_;
    std::string contentDir_, openLevel_;
    std::vector<LevelReturnPoint> returnStack_;
    std::string status_;
    mutable u32 idCounter_ = 0;
};

} // namespace aver::editor
#endif // AVER_MODULE_SCENE
