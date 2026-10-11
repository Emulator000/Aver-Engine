#pragma once
// The editor's scene-light feed: game::SceneLightFeed (the body moved to Runtime so the packaged game lights its
// levels too) plus an adapter that hands the reference path tracer its next-event lights.
// Include under AVER_MODULE_VOXI. docs/rendering/LIGHTS.md.
#include "aver/game/SceneLightFeed.hpp"
#include "aver/pt/PtSceneView.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace aver::editor {

class SceneLightFeed {
public:
    // Once per frame, before the frame's draws are submitted. `pt` may be null (no reference view).
    void update(scene::World& world, voxi::VoxiRenderer& voxi, pt::PtSceneView* pt, const std::string& contentDir) {
        if (!pt) { feed_.update(world, voxi, nullptr, contentDir); return; }
        PtAdapter a{*pt};
        feed_.update(world, voxi, &a, contentDir);
    }

private:
    struct PtAdapter final : game::LightPtSink {
        explicit PtAdapter(pt::PtSceneView& v) : view(v) {}
        u32 lightTexture(u64 id, u32 width, u32 height, bool ies, const void* pixels) override {
            return view.lightTexture(id, width, height, ies, pixels);
        }
        void setLights(const voxi::PackedLight* lights, u32 count) override {
            std::vector<pt::PtLight> out(count);
            static_assert(sizeof(pt::PtLight) == sizeof(voxi::PackedLight), "PtLight and PackedLight are the same 80-byte record");
            if (count) std::memcpy(out.data(), lights, sizeof(pt::PtLight) * count);
            view.setLights(out.data(), count);
        }
        pt::PtSceneView& view;
    };

    game::SceneLightFeed feed_;
};

} // namespace aver::editor
