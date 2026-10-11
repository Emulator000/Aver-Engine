#pragma once
// Gather and Report over a project's content tree: walks the files, runs the text scanners of
// aver/formats/LocGather.hpp, loads every string table, and either reports the difference or writes the missing
// keys into the default culture's tables. Headless (no ImGui, no engine): the string-table editor calls it, and
// tests/editor drives it on a temp tree. docs/LOCALIZATION.md.
#include "aver/core/Types.hpp"
#include "aver/formats/LocGather.hpp"

#include <map>
#include <string>
#include <vector>

namespace aver::editor {

// Every string table on disk: parsed data plus where each one lives (so a write goes back to the same file).
struct LocFiles {
    fmt::LocCorpus corpus;                               // culture -> table -> data
    std::map<std::string, std::string> paths;            // "culture/Table" -> absolute file (.ocstrings wins over .csv)
    std::vector<std::string> diagnostics;                // parse problems, "file: message"
};
LocFiles loadLocFiles(const std::string& localizationDir);

// The files that can hold text references: .ocui, .ocgraph, .cs and data tables (.csv/.json outside Localization/),
// under `contentRoot`, skipping bin/ obj/ Source/ directories.
std::vector<std::string> listLocSources(const std::string& contentRoot);

// Runs the scanner matching each file's extension. Unreadable files are skipped and named in `diagnostics`.
std::vector<fmt::LocUse> scanLocUses(const std::vector<std::string>& files, std::vector<std::string>* diagnostics = nullptr);

struct LocRunResult {
    fmt::LocReport report;                  // computed against the tables as they were BEFORE a gather writes anything
    usize usedKeys = 0;                     // distinct Table:Key referenced
    usize added = 0;                        // gather: entries added to the default culture
    std::vector<std::string> filesWritten;  // gather
    std::vector<std::string> diagnostics;
    std::string text;                       // the report as plain text, for "Copy as text"
};

// Scans `<contentRoot>`, loads `<contentRoot>/Localization`, builds the report.
LocRunResult runLocReport(const std::string& contentRoot, const std::string& defaultCulture);
// The same, then writes every default-culture entry the scan found missing (text = the inline default), creating
// Localization/<default>/<Table>.ocstrings where a table is new and merging into the file otherwise.
LocRunResult runLocGather(const std::string& contentRoot, const std::string& defaultCulture);

// Plain-text rendering of a report.
std::string formatLocReport(const fmt::LocReport& r, usize usedKeys);

} // namespace aver::editor
