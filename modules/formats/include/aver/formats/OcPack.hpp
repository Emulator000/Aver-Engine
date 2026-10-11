#pragma once
// .ocpack -- the sidecar of a baked level pack (docs/LEVEL_INSTANCES.md, FORMAT_SPECS.md section 11f).
//
// Baking a level into a pack moves its statically packable mesh placements out of the entity list and into an
// .ocinst instance table (child-local rows). The .ocpack says what the table is for: which closure of the level
// it was baked from (so a stale pack is detected), what each group's material and collision are, and which
// placements are NOT in the table (they stay entities).
//
//   OCPACK 1
//   SOURCE 3f9a1c0e2b7d4455
//   TABLE Binaries/LevelPacks/Levels_Room.ocinst
//   GROUP 0 collide 1 material M_Wood          one per OcInstanceGroup, same order; `material -` = the mesh's own
//   GROUP 1 collide 0 material -
//   KEEP 00000000000000c3                      every placement NOT in the table (path ids, `b2/c9` in a nested instance)
#include "aver/core/Math.hpp"
#include "aver/formats/OcInstances.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::fmt {

struct OcPackGroup {
    bool collide = false;
    std::string material;          // empty = the mesh's own
};

struct OcPack {
    int version = 1;
    u64 source = 0;                // closure hash of the level when baked
    std::string table;             // content/project-relative .ocinst path
    std::vector<OcPackGroup> groups;
    std::vector<std::string> keep; // placement path ids, 16 hex digits per segment, '/' between

    // True when `id` (an id path in any leading-zero spelling) is in the KEEP list.
    bool keeps(std::string_view idPath) const;
};

bool parseOcPack(std::string_view text, OcPack& out, std::string* why = nullptr);
bool loadOcPack(const std::string& path, OcPack& out, std::string* why = nullptr);
std::string writeOcPack(const OcPack& p);
bool saveOcPack(const std::string& path, const OcPack& p, std::string* why = nullptr);

// The pack's group list lines up with the table it names (same count).
bool ocPackMatchesTable(const OcPack& pack, const OcInstanceData& table, std::string* why = nullptr);

// rows * m for `count` 12-float rows (row-vector convention, v*M; see OcInstances.hpp), into `out`
// (which may alias `rows`).
void transformOcInstanceRows(const f32* rows, usize count, const Mat4& m, f32* out);

} // namespace aver::fmt
