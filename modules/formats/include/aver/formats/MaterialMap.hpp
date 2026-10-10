#pragma once
// --material-map: gives imported submeshes that arrived with NO material one, by the name of the
// GeomSubset (or other submesh) they came from. File and matching rules live here, not in
// AverAssetC, so the matching can be tested without running the tool and the editor can reuse it.
//
// FILE FORMAT. One rule per line, '#' starts a comment line:
//     <pattern> <material stem | ->
// '-' as the stem DROPS the matching faces. Patterns are case-insensitive.
//
// MATCHING (default). The pattern is matched against the lowercased LEAF of the submesh name, which
// is split on '_' into tokens:
//     water           a whole token: water, water_01, big_water, water2 (trailing digits allowed)
//                     but NOT waterfall and NOT metal_stainless for `stain`
//     water_tank      contiguous tokens, in order: water_tank_01, big_water_tank
//     *tank*          substring anywhere        tank*   prefix of the whole name
//     *_01            suffix of the whole name  *       anything
// THE MOST SPECIFIC MATCHING RULE WINS, so rule order no longer matters: more tokens first, then the
// longer pattern, and file order only breaks an exact tie. `water_tank Tank` therefore beats
// `water Water` for water_tank_01 wherever each sits in the file.
//
// LEGACY (--material-map-legacy): the first rule in file order wins; a material rule is a plain
// substring, a drop rule a whole-token match, '*' anything -- exactly what the tool did before the
// wildcard syntax existed.
#include "aver/formats/OcMesh.hpp"

#include <string>
#include <vector>

namespace aver::fmt {

enum class MaterialRuleKind : u8 { Any, Tokens, Substring, Prefix, Suffix };

struct MaterialRule {
    std::string pattern;       // as written, lowercased ('*' wildcards included)
    std::string stem;          // material stem, or "-" to drop the faces
    u32 line = 0;              // 1-based line in the file, for the report
    MaterialRuleKind kind = MaterialRuleKind::Tokens;   // derived from `pattern`
    std::string text;          // `pattern` without its wildcards
    u32 tokenCount = 0;        // '_'-separated tokens in `text`; the specificity key
    bool drop() const { return stem == "-"; }
};

struct MaterialMap {
    std::vector<MaterialRule> rules;
    bool legacy = false;                  // first-substring-in-file-order matching
    std::vector<std::string> warnings;    // lines that were skipped or cannot match
};

// Parses a map from text / a file. loadMaterialMap is false only when the file cannot be opened.
void parseMaterialMap(const std::string& text, bool legacy, MaterialMap& out);
bool loadMaterialMap(const std::string& path, bool legacy, MaterialMap& out);

// Index into map.rules of the rule that wins for `leafLower` (already lowercased), -1 for none.
int matchMaterialRule(const MaterialMap& map, const std::string& leafLower);

struct MaterialMapStats {
    usize mapped = 0, dropped = 0, unmatched = 0, emptied = 0;
    usize alreadyBound = 0;               // submeshes that kept the material they came with
};

// One row per distinct leaf name that was looked up.
struct MaterialMapReportEntry {
    std::string leaf;
    int rule = -1;                        // winning rule, -1 = none
    u32 count = 0;                        // submeshes with this leaf name
    std::string firstMesh;                // the first mesh it was seen in
};

struct MaterialMapReport {
    std::vector<MaterialMapReportEntry> entries;   // sorted by leaf
    std::vector<u32> ruleWins;                     // per rule: submeshes it won
};

// Applies `map` to every submesh whose material slot is empty, then regroups each touched mesh to
// one submesh per material. A mesh the drops empty entirely is logged with what emptied it.
// `report` (optional) is filled for materialMapReportText.
MaterialMapStats applyMaterialMap(std::vector<OcMeshData>& meshes, const std::vector<std::string>& names,
                                  const MaterialMap& map, MaterialMapReport* report = nullptr);

// The report as text: mapped names with the rule line that won, dropped names, unmatched names and
// rules that never won. `source` names the import it describes.
std::string materialMapReportText(const MaterialMap& map, const MaterialMapReport& report,
                                  const MaterialMapStats& stats, const std::string& source);

} // namespace aver::fmt
