#pragma once
// The String Table editor tab: one localization table (Localization/<culture>/<Table>.ocstrings or .csv) shown across
// EVERY culture folder as a grid -- rows are the union of keys, columns are Key, Note, then one per culture with the
// default culture first. A cell a culture lacks is tinted; typing into it creates the entry. See
// aver/formats/OcStrings.hpp for the file formats and docs/LOCALIZATION.md.
//
// THE HOOK INTO SandboxApp.cpp IS TWO LINES, InputSchemeEditor's pattern, and it is registered BEFORE
// makeDataTableEditor (which declines CSVs under Localization/<culture>/ anyway):
//   #include "StringTableEditor.hpp"
//   assetEditors_.registerFactory(&editor::makeStringTableEditor);
//
// An empty text in a non-default culture means "not translated": it is not written to the file, so the runtime falls
// through to the default culture instead of showing a blank. The default culture may hold an empty text.
// Save merges into each culture file's own text (writeOcstrings(data, existing)), so comments survive; a table that is
// a .csv is written back as one.
#include "AssetEditor.hpp"
#include "LocGatherRun.hpp"
#include "SnapshotUndo.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aver::editor {

struct StringTableEditorHooks {
    // LOCALE.DEFAULT of the open project; "en" when unset or no hook.
    std::function<std::string()> defaultCulture;
    // Opens an asset tab (the "Open table" button on a report row).
    std::function<void(const std::string& absolutePath)> openAsset;
};
void setStringTableEditorHooks(StringTableEditorHooks hooks);

class StringTableEditor final : public AssetEditor {
public:
    explicit StringTableEditor(std::string path);

    const std::string& path() const override { return path_; }
    std::string title() const override;
    bool dirty() const override;
    void draw(Engine& e) override;
    bool save(std::string* why) override;
    void onFileChanged() override;

    // ---- the grid model (also what a headless test reads) ----
    bool loaded() const { return loaded_; }
    const std::string& loadError() const { return loadError_; }
    const std::string& table() const { return table_; }
    const std::string& localizationDir() const { return locDir_; }
    usize cultureCount() const { return grid_.cultures.size(); }
    const std::string& culture(usize i) const { return grid_.cultures[i]; }   // [0] is the default culture
    usize keyCount() const { return grid_.keys.size(); }
    const std::string& key(usize k) const { return grid_.keys[k]; }
    const std::string& text(usize cul, usize k) const { return grid_.text[cul][k]; }
    const std::string& note(usize k) const { return grid_.note[0][k]; }
    bool hasEntry(usize cul, usize k) const { return grid_.present[cul][k] != 0; }
    // Not translated: no entry (default culture) or an empty text (any other).
    bool isMissing(usize cul, usize k) const;
    usize missingCount() const;
    const std::vector<std::string>& diagnostics() const { return diagnostics_; }

    // ---- edits (keys are unique and match [A-Za-z0-9_.-]+; every one pushes an undo step unless refused) ----
    bool addKey(std::string key);
    bool removeKey(usize k);
    // Renames the key in every culture at once.
    bool renameKey(usize k, std::string newKey);
    bool setText(usize cul, usize k, std::string text);
    bool setNote(usize k, std::string note);
    // Creates Localization/<tag>/ and an empty column for it (the file is written on save). False for a malformed or
    // existing tag.
    bool addCulture(std::string tag);
    const std::string& editError() const { return editError_; }

    void pushUndo();
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }

    // ---- Gather and Report over the whole project (LocGatherRun.hpp); both need a clean tab, since they read the disk ----
    bool runReport();
    bool runGather();
    const LocRunResult& lastRun() const { return lastRun_; }
    bool hasRun() const { return hasRun_; }

private:
    struct Grid {
        std::vector<std::string> cultures;                   // [0] = the default culture
        std::vector<std::string> keys;
        std::vector<std::vector<std::string>> text;          // [culture][key]
        std::vector<std::vector<std::string>> note;          // [culture][key]; the Note column edits [0]
        std::vector<std::vector<char>> present;              // [culture][key]
    };
    struct CultureFile {
        std::string path;            // the file (existing, or where it will be created)
        std::string original;        // its text as last loaded or saved: the merge base
        bool csv = false;
        bool exists = false;
        bool broken = false;         // exists but did not parse: never overwritten from here
    };

    void loadFromDisk();
    bool refuse(std::string why);
    void touchAll() { for (char& c : culDirty_) c = 1; }
    void markDirty(usize cul) { if (cul < culDirty_.size()) culDirty_[cul] = 1; }
    std::string defaultTag() const;

    std::string path_;
    std::string table_;
    std::string locDir_;
    bool loaded_ = false;
    std::string loadError_;
    std::string editError_;
    std::vector<std::string> diagnostics_;
    Grid grid_;
    std::map<std::string, CultureFile> files_;               // by culture tag
    std::vector<char> culDirty_;                             // [culture]: needs writing
    bool changedOnDisk_ = false;
    SnapshotUndo<Grid> history_;
    LocRunResult lastRun_;
    bool hasRun_ = false;

#if AVER_WITH_IMGUI
    bool missingOnly_ = false;
    int selectedKey_ = -1;
    bool cellUndone_ = false;
    std::string newKey_, newCulture_;
    void drawToolbar();
    void drawGrid();
    void drawReport();
#endif
};

// Creates a String Table editor for a .ocstrings, or a .csv directly under Localization/<culture>/; else nullptr.
std::unique_ptr<AssetEditor> makeStringTableEditor(const std::string& path);

} // namespace aver::editor
