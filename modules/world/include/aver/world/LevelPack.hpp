// Packed level instances: bakes a level's statically packable mesh placements into an instance table (.ocinst,
// rows in the level's own frame) plus a sidecar (.ocpack) naming what stayed behind. docs/LEVEL_INSTANCES.md 2.8.
//
// Pure (Core + Formats + the library), so the bake and its tests need no scene or GPU. The host wrapper that knows
// which meshes load and which are translucent is game::LevelPackBaker.
#pragma once

#include "aver/core/Types.hpp"
#include "aver/formats/OcInstances.hpp"
#include "aver/formats/OcPack.hpp"
#include "aver/world/LevelAssetLibrary.hpp"

#include <functional>
#include <string>

namespace aver::world {

struct PackBakeOptions {
    // Host: the mesh loads, has <= 16 parts and no translucent part (pbr::isTranslucent). Absent = everything packable.
    std::function<bool(const std::string& asset, const std::string& material, std::string* whyNot)> meshPackable;
    u32 maxRows = 1000000;
};

struct PackBakeReport {
    u32 rows = 0, groups = 0, kept = 0, notPackable = 0;
    bool capped = false;   // maxRows reached; the rest stayed entities
};

// "Levels/Room.ocworld" -> "Levels_Room" (the flattening LEVEL_STREAMING.md section 1 uses).
std::string levelPackFlatName(const std::string& levelAsset);
// Project-relative paths of the two files a bake writes.
std::string levelPackTablePath(const std::string& levelAsset);
std::string levelPackSidecarPath(const std::string& levelAsset);

bool bakeLevelPack(LevelAssetLibrary& lib, const std::string& levelAsset, const std::string& contentDir,
                   const std::string& projectDir, const PackBakeOptions& opt, PackBakeReport& report,
                   std::string* why = nullptr);

// A baked pack read back.
struct LoadedLevelPack {
    fmt::OcPack pack;
    fmt::OcInstanceData table;
};
// Reads both files under `projectDir`. False with `why` when either is missing or malformed. `stale` (optional)
// receives whether pack.source differs from `expectedSource`.
bool loadLevelPack(const std::string& projectDir, const std::string& levelAsset, u64 expectedSource,
                   LoadedLevelPack& out, bool* stale, std::string* why = nullptr);

} // namespace aver::world
