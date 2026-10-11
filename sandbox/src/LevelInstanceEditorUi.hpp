#pragma once
// The ImGui half of the level-instance editor: the "Level Instance" section of the Details panel (what this is an
// instance of, edit in place, Open Level, packed, streaming, the override list) and the "Create Level Instance" dialog the
// outliner opens. All behaviour is in LevelInstanceEditorModel; these only draw and forward.
//
// THE HOOKS (docs/LEVEL_INSTANCES.md "Editor wiring"):
//   Details panel, for the selected entity:   editor::levelInstanceDetailsDraw(model, sel, cb);
//   Outliner context menu:                    if (ImGui::MenuItem("Create Level Instance...")) editor::levelInstanceCreateDialogOpen(dlg, sel, name);
//   Once per frame, beside the other modals:  editor::levelInstanceCreateDialogDraw(dlg, model, capture, cb);
// Everything is a no-op in a build without ImGui.
#if AVER_MODULE_SCENE
#include "LevelInstanceEditorModel.hpp"

#include <functional>
#include <string>
#include <vector>

namespace aver::editor {

// What the host does with the results. All optional.
struct LevelInstanceUiCallbacks {
    std::function<void(const LevelInstanceEdit&)> pushUndo;     // one undo entry per successful operation
    std::function<void(scene::Entity)> select;
    std::function<void()> markDirty;                            // the level has unsaved edits
    std::function<void(const std::string&)> status;             // a line for the status bar
    std::function<void(const std::string& assetRef)> openLevel; // Open Level: the host pushes the return point and opens it
    std::function<bool(scene::Entity root, std::string* why)> bakePack;   // Bake Pack
    std::function<void(const std::vector<scene::Entity>&)> removeReplaced; // Create from Selection: take the originals out
};

// Draws the Level Instance section for `e`. Does nothing for an entity that is not part of an instance.
// True when it changed the scene (the caller refreshes whatever it caches).
bool levelInstanceDetailsDraw(LevelInstanceEditorModel& m, scene::Entity e, const LevelInstanceUiCallbacks& cb);

struct LevelInstanceCreateDialog {
    bool wantOpen = false;
    std::vector<scene::Entity> sources;
    char name[128] = {};
    std::string dir = "Levels";   // content-relative folder
    bool replace = true;          // swap the selection for an instance of the new level
    std::string error;
};

void levelInstanceCreateDialogOpen(LevelInstanceCreateDialog& d, const std::vector<scene::Entity>& sources,
                                   const std::string& suggestedName);
// Call every frame. True on the frame a level was created.
bool levelInstanceCreateDialogDraw(LevelInstanceCreateDialog& d, LevelInstanceEditorModel& m,
                                   const LevelInstanceEditorModel::CaptureFn& capture, const LevelInstanceUiCallbacks& cb);

} // namespace aver::editor
#endif // AVER_MODULE_SCENE
