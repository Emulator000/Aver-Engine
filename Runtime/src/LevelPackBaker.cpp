#include "aver/game/LevelPackBaker.hpp"

#if AVER_MODULE_SCENE

#include "aver/core/Hash.hpp"
#include "aver/game/GameContent.hpp"

#if AVER_MODULE_PBR
#  include "aver/pbr/Material.hpp"
#  include "aver/scene/scene_abi.h"
#endif

namespace aver::game {

namespace {

constexpr usize kMaxPackParts = 16;   // VoxiRenderer::kMaxFoliagePartsPerPrototype

#if AVER_MODULE_PBR
bool translucentToken(GameContent& content, i32 token) {
    if (!token) return false;
    const pbr::MaterialHandle h = content.authoredFor(token);
    if (!h) return false;
    const pbr::MaterialDesc* d = pbr::MaterialLibrary::get().desc(h);
    return d && pbr::isTranslucent(*d);
}
#endif

} // namespace

bool bakeLevelPackWithContent(world::LevelAssetLibrary& lib, const std::string& levelAsset, GameContent& content,
                              const std::string& contentDir, const std::string& projectDir,
                              world::PackBakeReport& report, std::string* why, u32 maxRows) {
    world::PackBakeOptions opt;
    opt.maxRows = maxRows;
    opt.meshPackable = [&content](const std::string& asset, const std::string& material, std::string* whyNot) {
        const u64 id = fnv1a64(std::string_view(asset));
        if (!content.meshFor(id) && !content.boundsFor(id)) {
            if (whyNot) *whyNot = "the mesh is not loaded";
            return false;
        }
        if (const std::vector<GameContent::MeshPart>* parts = content.partsFor(id)) {
            if (parts->size() > kMaxPackParts) {
                if (whyNot) *whyNot = "more than 16 material slots";
                return false;
            }
#if AVER_MODULE_PBR
            for (const GameContent::MeshPart& part : *parts)
                if (translucentToken(content, part.material)) {
                    if (whyNot) *whyNot = "a translucent material slot";
                    return false;
                }
#endif
        }
#if AVER_MODULE_PBR
        if (translucentToken(content, content.meshDefaultMaterial(id))) {
            if (whyNot) *whyNot = "a translucent material";
            return false;
        }
        if (!material.empty()) {
            const pbr::MaterialHandle h = content.materialForSurface(material);
            const pbr::MaterialDesc* d = h ? pbr::MaterialLibrary::get().desc(h) : nullptr;
            if (d && pbr::isTranslucent(*d)) {
                if (whyNot) *whyNot = "its material '" + material + "' is translucent";
                return false;
            }
        }
#else
        (void)material;
#endif
        return true;
    };
    return world::bakeLevelPack(lib, levelAsset, contentDir, projectDir, opt, report, why);
}

} // namespace aver::game

#endif // AVER_MODULE_SCENE
