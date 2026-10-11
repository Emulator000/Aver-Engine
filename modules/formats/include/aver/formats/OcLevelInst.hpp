#pragma once
// LEVELINST -- a level instance placed in an .ocworld (docs/LEVEL_INSTANCES.md, FORMAT_SPECS.md section 11).
//
// An instance is a record that places ANOTHER .ocworld: asset reference, transform and a list of overrides,
// written before the placements exactly as PREFABINST is, additive so an older reader skips the unknown
// records (it loses the instance, it never misplaces anything). The child is an ordinary .ocworld; there is no
// "instanceable" flag.
//
//   LEVELINST Levels/Room.ocworld pos 1200 0 0 rot 90 0 0 scale 1 1 1 name Room%20A id=00000000000000a1 packed stream 1
//     LIOVERRIDE set 00000000000000c3 @Placement material string ~M_Red
//     LIOVERRIDE set 00000000000000b2/00000000000000c9 @Placement position vec3 0 0 40
//   ENDLEVELINST
//   EXTENT -500 -500 0 500 500 400
//
// An override is the prefab override type (OcPrefabOverride) with the pseudo-component `@Placement` and the child
// placement's stable `id=` as the path (`<nested instance id>/<placement id>` inside a nested instance).
#include "aver/formats/OcPrefab.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::fmt {

// The pseudo-component an override of a child placement names; the field is one of the kLiField* below.
inline constexpr const char* kOcLevelInstPlacementComponent = "@Placement";
inline constexpr const char* kLiFieldPosition = "position";   // vec3, cm, the child's own frame
inline constexpr const char* kLiFieldRotation = "rotation";   // vec3 yaw pitch roll degrees
inline constexpr const char* kLiFieldScale    = "scale";      // vec3
inline constexpr const char* kLiFieldMaterial = "material";   // string; empty = the mesh's own
inline constexpr const char* kLiFieldVisible  = "visible";    // bool
inline constexpr const char* kLiFieldCollide  = "collide";    // bool
inline constexpr const char* kLiFieldRemoved  = "removed";    // bool; 1 drops the placement and its children
inline constexpr const char* kLiFieldName     = "name";       // string; outliner label

struct OcLevelInstance {
    std::string asset;                 // content-relative .ocworld, forward slashes, percent-encoded on disk
    std::string name;                  // outliner label; empty = the asset's file stem
    f64 x = 0, y = 0, z = 0, yaw = 0, pitch = 0, roll = 0, sx = 1, sy = 1, sz = 1;
    u64 id = 0;                        // instance id (hex `id=`); 0 = unassigned, the editor assigns
    bool packed = false;               // bare token `packed`
    i32 stream = -1;                   // `stream 0|1`; -1 = follow the host level (absent on disk)
    std::vector<OcPrefabOverride> overrides;
};

// One tokenised .ocworld line. True when it was ours (LEVELINST, LIOVERRIDE, ENDLEVELINST) and was consumed;
// `open` is the index of the instance a LIOVERRIDE attaches to, -1 outside one.
bool parseOcLevelInstanceLine(const std::vector<std::string_view>& tokens,
                              std::vector<OcLevelInstance>& out, i32& open);
// Appends the records, preceded by a blank line. Nothing for an empty list.
void appendOcLevelInstances(std::string& out, const std::vector<OcLevelInstance>& v);

// An id as 16 lowercase hex digits ("00000000000000a1"), and back (leading zeros optional, 0 on a bad token).
std::string ocLevelInstIdText(u64 id);
u64 ocLevelInstIdParse(std::string_view hex);

// An override path ("c3" or "b2/c9") as a list of ids, and back (16 digits each).
bool ocLevelInstSplitIds(std::string_view path, std::vector<u64>& ids);
std::string ocLevelInstJoinIds(const std::vector<u64>& ids);

// The EXTENT record line (with newline): min then max, cm, the level's own frame.
std::string writeOcExtentLine(const f64 mn[3], const f64 mx[3]);

} // namespace aver::fmt
