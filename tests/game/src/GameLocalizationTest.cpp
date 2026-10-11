// Culture selection and content hot reload (Runtime/src/GameLocalization.cpp): bestCultureMatch, the
// override > saved > system > default precedence over a temp project, and onContentFilesChanged routing
// file events to the string and data-table stores. No window, no device.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/framework/framework_loc_abi.h"
#include "aver/framework/framework_table_abi.h"
#include "aver/game/GameLocalization.hpp"
#include "aver/platform/FileSystem.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace aver;

static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

static std::string g_dir;   // project dir, forward slashes

static void writeFile(const std::string& rel, const std::string& text) {
    const fs::path p = fs::path(g_dir) / rel;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    writeFileText(p.generic_string(), text);
}

static std::string resolve(const std::string& ref) {
    char buf[256];
    const int32_t n = aver_fw_loc_resolve(ref.c_str(), buf, sizeof buf);
    return n < 0 ? std::string("<-1>") : std::string(buf);
}

static fmt::ProjectDesc project() {
    fmt::ProjectDesc p;
    p.name = "LocTest";
    p.dir = g_dir;
    p.contentRoot = "Content";
    return p;
}

int main() {
    std::error_code ec;
    g_dir = (fs::temp_directory_path() / "aver_game_loc_test").generic_string();
    fs::remove_all(g_dir, ec);
    fs::create_directories(g_dir, ec);

    AVER_INFO("=== bestCultureMatch ===");
    {
        using game::bestCultureMatch;
        const std::vector<std::string> a = {"de", "en", "pt-BR", "zh-Hans"};
        check(bestCultureMatch(a, "de") == "de", "exact");
        check(bestCultureMatch(a, "de-AT") == "de", "the bare language serves a regional tag");
        check(bestCultureMatch(a, "pt_br") == "pt-BR", "the wanted tag is normalised");
        check(bestCultureMatch(a, "pt-PT") == "pt-BR", "pt-PT is served by the only pt available");
        check(bestCultureMatch(a, "zh-Hans-CN") == "zh-Hans", "script and language match");
        check(bestCultureMatch(a, "fr") == "", "no match is empty");
        check(bestCultureMatch(a, "") == "" && bestCultureMatch(a, "!!") == "", "malformed is empty");
        const std::vector<std::string> b = {"pt", "pt-BR", "pt-PT"};
        check(bestCultureMatch(b, "pt-PT") == "pt-PT", "an exact regional tag beats the bare language");
        check(bestCultureMatch(b, "pt-AO") == "pt", "an unlisted region falls to the bare language, not a stranger's region");
        check(bestCultureMatch({"zh-Hans", "zh-Hant"}, "zh-Hant-TW") == "zh-Hant", "the script picks between variants");
    }

    AVER_INFO("=== savedCulture ===");
    {
        writeFile("Saved/Settings.ini", "# comment\naudio.master = 0.5\nlocale.culture = pt_br \nother=1\n");
        check(game::savedCulture(g_dir + "/Saved/Settings.ini") == "pt-BR", "the saved culture is read and normalised");
        check(game::savedCulture(g_dir + "/Saved/Missing.ini").empty(), "a missing file is empty");
        writeFile("Saved/None.ini", "audio.master=1\n");
        check(game::savedCulture(g_dir + "/Saved/None.ini").empty(), "an absent key is empty");
    }

    AVER_INFO("=== initLocalization precedence ===");
    writeFile("Content/Localization/en/UI.ocstrings", "OCSTRINGS 1\nENTRY Play \"Play\"\nENTRY OnlyEn \"Only English\"\n");
    writeFile("Content/Localization/de/UI.ocstrings", "OCSTRINGS 1\nENTRY Play \"Spielen\"\n");
    writeFile("Content/Localization/pt-BR/UI.ocstrings", "OCSTRINGS 1\nENTRY Play \"Jogar\"\n");
    writeFile("Content/Data/items.csv", "Name,Damage:int\nsword,30\n");
    {
        const fmt::ProjectDesc p = project();
        game::LocalizationInit li;
        li.project = &p;
        li.settingsPath = g_dir + "/Saved/Settings.ini";   // locale.culture = pt-BR

        li.cultureOverride = "de";
        check(game::initLocalization(li) == "de" && resolve("@UI:Play") == "Spielen", "the override wins over the saved setting");
        check(resolve("@UI:OnlyEn") == "Only English", "an untranslated key falls back to the default culture");

        li.cultureOverride.clear();
        check(game::initLocalization(li) == "pt-BR" && resolve("@UI:Play") == "Jogar", "the saved setting is next");

        li.readSavedCulture = false;
        const std::string c = game::initLocalization(li);
        check(c != "pt-BR", "readSavedCulture=false ignores the saved choice (got " + c + ")");
        check(c == aver_fw_loc_culture(), "...and the result is the active culture");

        li.cultureOverride = "xx";
        li.readSavedCulture = true;
        check(game::initLocalization(li) == "xx" && resolve("@UI:Play") == "Play", "an override without tables resolves from the default");

        fmt::ProjectDesc q = p;
        q.localeDefault = "de";
        li.project = &q;
        li.cultureOverride.clear();
        li.readSavedCulture = false;
        li.settingsPath.clear();
        game::initLocalization(li);
        check(std::string(aver_fw_loc_default_culture()) == "de", "LOCALE.DEFAULT is the fallback culture");
        check(std::string(aver_fw_content_root()) == g_dir + "/Content", "the content root points at the project");
    }

    AVER_INFO("=== onContentFilesChanged ===");
    {
        const fmt::ProjectDesc p = project();
        game::LocalizationInit li;
        li.project = &p;
        li.cultureOverride = "de";
        li.readSavedCulture = false;
        game::initLocalization(li);
        const int32_t h = aver_fw_table_load("Data/items.csv");
        check(h > 0 && std::string(aver_fw_table_cell(h, 0, 0)) == "30", "a table loads through the content root");

        writeFile("Content/Localization/de/UI.ocstrings", "OCSTRINGS 1\nENTRY Play \"Los geht's\"\n");
        writeFile("Content/Data/items.csv", "Name,Damage:int\nsword,45\n");
        std::vector<FileEvent> ev;
        ev.push_back({FileChange::Modified, "Localization/de/UI.ocstrings", ""});
        ev.push_back({FileChange::Modified, "Data/items.csv", ""});
        ev.push_back({FileChange::Modified, "Textures/rock.png", ""});
        const usize n = game::onContentFilesChanged(p.contentDir(), ev);
        check(n == 2, "two files touched a store (" + std::to_string(n) + ")");
        check(resolve("@UI:Play") == "Los geht's", "the changed string file is picked up");
        check(std::string(aver_fw_table_cell(h, 0, 0)) == "45", "the changed table is picked up");

        fs::remove(fs::path(g_dir) / "Content/Localization/de/UI.ocstrings", ec);
        std::vector<FileEvent> del = {{FileChange::Deleted, "Localization/de/UI.ocstrings", ""}};
        game::onContentFilesChanged(p.contentDir(), del);
        check(resolve("@UI:Play") == "Play", "a deleted string table drops and the text falls back");

        std::vector<FileEvent> ren = {{FileChange::Renamed, "Data/renamed.csv", "Data/items.csv"}};
        writeFile("Content/Data/items.csv", "Name,Damage:int\nsword,50\n");
        game::onContentFilesChanged(p.contentDir(), ren);
        check(std::string(aver_fw_table_cell(h, 0, 0)) == "50", "a rename reloads the old path too");
    }

    fs::remove_all(g_dir, ec);
    AVER_INFO("GameLocalization tests done: {} failure(s)", g_failures);
    return aver::exitCode(g_failures ? aver::ExitCode::Failed : aver::ExitCode::Ok);
}
