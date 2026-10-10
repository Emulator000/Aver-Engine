// --material-map matching (aver/formats/MaterialMap.hpp): whole-token and multi-token patterns,
// explicit wildcards, most-specific-wins independent of file order, drops, the legacy behaviour and
// the report. Pure logic on strings and OcMeshData; no files, no device.
#include "aver/core/ErrorCodes.hpp"
#include "aver/core/Log.hpp"
#include "aver/formats/MaterialMap.hpp"

#include <string>
#include <vector>

using namespace aver;

static int g_checks = 0;
static int g_failures = 0;

static void check(bool cond, const std::string& what) {
    ++g_checks;
    if (cond) { AVER_INFO("  ok    {}", what); return; }
    ++g_failures;
    AVER_ERROR("  FAIL  {}", what);
}

// The material a leaf name resolves to: the winning rule's stem, "" for no rule.
static std::string resolve(const fmt::MaterialMap& m, const std::string& leaf) {
    const int i = fmt::matchMaterialRule(m, leaf);
    return i < 0 ? std::string() : m.rules[usize(i)].stem;
}

static fmt::MaterialMap make(const std::string& text, bool legacy = false) {
    fmt::MaterialMap m;
    fmt::parseMaterialMap(text, legacy, m);
    return m;
}

// A mesh of `names.size()` one-triangle submeshes, each with its own EMPTY material slot.
static fmt::OcMeshData meshWith(const std::vector<std::string>& names) {
    fmt::OcMeshData m;
    for (usize i = 0; i < names.size(); ++i) {
        fmt::OcMeshSubmesh sm;
        sm.name = "/root/mesh/" + names[i];
        sm.materialSlot = static_cast<u32>(i);
        sm.indexStart = static_cast<u32>(i * 3);
        sm.indexCount = 3;
        m.submeshes.push_back(sm);
        m.materialSlots.emplace_back();
        for (u32 k = 0; k < 3; ++k) m.indices.push_back(static_cast<u32>(i * 3) + k);
    }
    return m;
}

int main() {
    AVER_INFO("MaterialMapTest");

    AVER_INFO("-- parsing --");
    {
        const fmt::MaterialMap m = make("# a comment\n\nWater  Water_Mat\n  clip -\nlonely\n*tank* Tank\n");
        check(m.rules.size() == 3, "comments, blank lines and a line with no stem are skipped (got " +
                                       std::to_string(m.rules.size()) + ")");
        check(m.rules.size() >= 1 && m.rules[0].pattern == "water" && m.rules[0].stem == "Water_Mat",
              "patterns are lowercased, stems keep their case");
        check(m.rules.size() >= 2 && m.rules[1].drop(), "a '-' stem is a drop");
        check(m.rules.size() >= 3 && m.rules[2].line == 6, "rules remember their line number");
        check(!m.warnings.empty(), "a line with no stem is reported in warnings");
    }

    AVER_INFO("-- whole tokens, trailing digits --");
    {
        const fmt::MaterialMap m = make("stain Stain\nclip -\n");
        check(resolve(m, "wall_stain_01") == "Stain", "stain matches the token in wall_stain_01");
        check(resolve(m, "stain") == "Stain", "and a name that is only the token");
        check(resolve(m, "stain01") == "Stain", "and the token with trailing digits");
        check(resolve(m, "metal_stainless_steel").empty(), "but NOT the front of stainless");
        check(resolve(m, "clip_player_ai") == "-", "clip drops clip_player_ai");
        check(resolve(m, "clipboard").empty(), "and does not touch a clipboard prop");
        check(resolve(m, "mesh_clip2") == "-", "tokens are found anywhere in the name");
    }

    AVER_INFO("-- multi-token patterns --");
    {
        const fmt::MaterialMap m = make("water_tank Tank\ntank_water Other\n");
        check(resolve(m, "water_tank_01") == "Tank", "a_b matches contiguous tokens");
        check(resolve(m, "big_water_tank") == "Tank", "wherever they sit in the name");
        check(resolve(m, "water_big_tank").empty(), "but not tokens with something between them");
        check(resolve(m, "tank_water") == "Other", "and order matters");
        check(resolve(m, "water").empty(), "a single token does not satisfy a two-token pattern");
    }

    AVER_INFO("-- explicit wildcards --");
    {
        const fmt::MaterialMap m = make("*ater* Sub\nroad* Pre\n*_decal Suf\n");
        check(resolve(m, "waterfall") == "Sub", "*x* is a substring (waterfall)");
        check(resolve(m, "bigwatery") == "Sub", "even inside a token");
        check(resolve(m, "road_main") == "Pre", "x* is a prefix");
        check(resolve(m, "main_road").empty(), "and only a prefix");
        check(resolve(m, "wall_decal") == "Suf", "*x is a suffix");
        check(resolve(m, "decal_wall").empty(), "and only a suffix");
        const fmt::MaterialMap any = make("* Fallback\nwall Wall\n");
        check(resolve(any, "whatever") == "Fallback", "* matches anything");
        check(resolve(any, "wall_01") == "Wall", "and loses to any real rule, wherever it sits in the file");
        const fmt::MaterialMap inner = make("a*b X\n");
        check(resolve(inner, "a_b").empty() && !inner.warnings.empty(),
              "a '*' inside a pattern never matches and says so");
    }

    AVER_INFO("-- the most specific rule wins, not the first --");
    {
        const std::string both[2] = {"water Water\nwater_tank Tank\n", "water_tank Tank\nwater Water\n"};
        for (const std::string& text : both) {
            const fmt::MaterialMap m = make(text);
            check(resolve(m, "water_tank_01") == "Tank", "water_tank beats water in either file order");
            check(resolve(m, "water_01") == "Water", "and water still takes names water_tank does not match");
        }
        const fmt::MaterialMap ord = make("rvl -\nasphalt Asphalt\n");
        check(resolve(ord, "asphalt_crack_01_rvl") == "-", "at the same token count the earlier line wins, not the longer pattern");
        const fmt::MaterialMap tie = make("wall First\nwall Second\n");
        check(resolve(tie, "wall_01") == "First", "an exact tie goes to the earlier line");
        const fmt::MaterialMap dropVsMat = make("glass_clip -\nglass Glass\n");
        check(resolve(dropVsMat, "glass_clip_01") == "-", "a more specific drop beats a material rule");
        check(resolve(dropVsMat, "glass_pane") == "Glass", "and the material rule takes the rest");
    }

    AVER_INFO("-- legacy: first match in file order --");
    {
        const fmt::MaterialMap m = make("water Water\nwater_tank Tank\nclip -\n", true);
        check(resolve(m, "water_tank_01") == "Water", "the first rule wins even when a later one is more specific");
        check(resolve(m, "waterfall") == "Water", "a material rule is a plain substring");
        check(resolve(m, "clipboard").empty(), "a drop rule is still whole-token");
        check(resolve(m, "clip_a") == "-", "and drops whole tokens");
        const fmt::MaterialMap star = make("* Any\n", true);
        check(resolve(star, "x") == "Any", "* matches anything");
        const fmt::MaterialMap lit = make("*x* Lit\n", true);
        check(resolve(lit, "abxcd").empty(), "wildcard syntax is not interpreted");
    }

    AVER_INFO("-- applying to meshes, and the report --");
    {
        const fmt::MaterialMap map = make("water Water\nwater_tank Tank\nclip -\nunused_thing Never\n");
        std::vector<fmt::OcMeshData> meshes;
        meshes.push_back(meshWith({"water_01", "water_tank_01", "clip_a", "glass_01"}));
        fmt::OcMeshData bound = meshWith({"water_02"});
        bound.materialSlots[0] = "Authored";
        meshes.push_back(bound);
        meshes.push_back(meshWith({"CLIP_b"}));
        const std::vector<std::string> names = {"/root/a", "/root/b", "/root/c"};

        fmt::MaterialMapReport rep;
        const fmt::MaterialMapStats st = fmt::applyMaterialMap(meshes, names, map, &rep);
        check(st.mapped == 2, "two submeshes mapped (got " + std::to_string(st.mapped) + ")");
        check(st.dropped == 2, "two dropped (got " + std::to_string(st.dropped) + ")");
        check(st.unmatched == 1, "one matched nothing (got " + std::to_string(st.unmatched) + ")");
        check(st.alreadyBound == 1, "one kept the material it came with (got " + std::to_string(st.alreadyBound) + ")");
        check(st.emptied == 1, "and the mesh holding only a dropped submesh is counted as emptied");
        check(meshes[0].submeshes.size() == 3, "drops remove their submesh (got " +
                                                   std::to_string(meshes[0].submeshes.size()) + ")");
        // Regrouped to one submesh per material, in order of first appearance; unbound ones keep slot "".
        const std::vector<std::string> wantSlots = {"Water", "Tank", ""};
        check(meshes[0].materialSlots == wantSlots, "each water submesh got its own rule's material");
        check(meshes[0].indices.size() == 9, "and the surviving triangles are all still there");
        check(meshes[1].materialSlots[0] == "Authored", "an authored material is left alone");
        check(meshes[2].submeshes.empty(), "a mesh whose only submesh dropped is emptied (case-insensitive)");

        const std::string text = fmt::materialMapReportText(map, rep, st, "test.usda");
        check(text.find("water_tank_01 -> line 2 \"water_tank Tank\" -> Tank") != std::string::npos,
              "the report names the winning rule line for a name");
        check(text.find("clip_a -> line 3") != std::string::npos && text.find("## dropped") != std::string::npos,
              "dropped names are listed with their rule");
        check(text.find("glass_01") != std::string::npos && text.find("## unmatched") != std::string::npos,
              "unmatched names are listed");
        check(text.find("line 4 \"unused_thing Never\"") != std::string::npos,
              "and a rule that won nothing is listed as unused");
        const usize unusedAt = text.find("## unused rules");
        check(unusedAt != std::string::npos && text.find("water Water", unusedAt) == std::string::npos,
              "a rule that won something is not listed as unused");
    }

    AVER_INFO("=== {} assertions, {} failed ===", g_checks, g_failures);
    return exitCode(g_failures ? ExitCode::Failed : ExitCode::Ok);
}
