#pragma once
// The host wrapper around world::bakeLevelPack: it knows which meshes load and which materials are translucent, which
// the pure bake does not. A mesh is packable when it loads, has at most 16 parts (the foliage prototype limit) and no
// part resolves to a translucent material (foliage is ray-traced geometry: a blended instance has no meaning there).
// docs/LEVEL_INSTANCES.md 2.8.2.
#include "aver/core/Types.hpp"
#include "aver/world/LevelPack.hpp"

#include <string>

#if AVER_MODULE_SCENE
namespace aver::game {

class GameContent;

// Bakes `<projectDir>/Binaries/LevelPacks/<flat>.ocinst/.ocpack` for `levelAsset` (content-relative). `lib` parses the
// level and its nested levels. False with `why` on a cycle, an unreadable level or a failed write.
bool bakeLevelPackWithContent(world::LevelAssetLibrary& lib, const std::string& levelAsset, GameContent& content,
                              const std::string& contentDir, const std::string& projectDir,
                              world::PackBakeReport& report, std::string* why, u32 maxRows = 1000000);

} // namespace aver::game
#endif
