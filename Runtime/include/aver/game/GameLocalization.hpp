// Localization host wiring shared by the game and the editor: points the framework's string and data-table
// stores at the project, picks the active culture, and routes hot-reload events to them. The UI resolver hook
// (aver_ui_set_text_resolver) is installed by each host beside its own UI setup, not here: this library does
// not link Aver.UI.Abi. docs/LOCALIZATION.md, docs/plans/wave2/DATA_TABLES_LOCALIZATION.md 4.4.
#pragma once
#include "aver/core/Types.hpp"
#include "aver/formats/OcProject.hpp"
#include "aver/platform/DirectoryWatcher.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::game {

struct LocalizationInit {
    const fmt::ProjectDesc* project = nullptr;
    std::string settingsPath;        // runtime: userDataDir()\<Project>\Settings.ini ; editor: <project>\Saved\Settings.ini
    std::string cultureOverride;     // --culture, or the editor's preview-culture pref; empty = auto
    bool readSavedCulture = true;    // false in the editor (a preview never reads the player's saved choice)
};

// Sets the content root, loads the project's Localization folder with LOCALE.DEFAULT as the fallback, and picks the
// culture: override > Settings "locale.culture" > the OS UI language reduced to the nearest available culture > default.
// Logs one line. Returns the chosen culture ("en" when the framework is not built in).
std::string initLocalization(const LocalizationInit&);

// The OS UI language as a culture tag (GetUserDefaultLocaleName), "" when unknown.
std::string systemUiCulture();

// Pure: `wanted` itself if available; else the nearest same-language culture (the bare language, then the one sharing
// the wanted script or region); else "". Tags are compared normalised.
std::string bestCultureMatch(const std::vector<std::string>& available, std::string_view wanted);

// The `locale.culture` value in a Settings.ini (key=value lines), "" when absent or the file is missing.
std::string savedCulture(const std::string& settingsPath);

// Hot reload: each event path under contentRoot goes to the string and data-table stores (a deletion too, so a removed
// string table drops). Returns the number of files that touched either store.
usize onContentFilesChanged(const std::string& contentRoot, const std::vector<FileEvent>&);

} // namespace aver::game
