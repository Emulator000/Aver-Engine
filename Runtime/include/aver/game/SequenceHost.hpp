#pragma once
// What a host (GameApp, SandboxApp) needs to run level sequences: the hooks that reach the audio and
// event ABIs, the binding table of a loaded level, and the provider the framework's sequence ABI relays
// to. HEADER-ONLY on purpose, the LevelLights.hpp arrangement: each host translation unit supplies its own
// AVER_WITH_AUDIO_ABI / AVER_MODULE_FRAMEWORK, so a scene-only build links and a hook whose ABI is not in
// the build is simply silent. docs/SEQUENCER.md.
#include "aver/game/SequenceDirector.hpp"

#if AVER_MODULE_SCENE

#include "aver/core/Hash.hpp"
#include "aver/core/Log.hpp"
#include "aver/platform/FileSystem.hpp"
#include "aver/scene/World.hpp"

#if AVER_WITH_AUDIO_ABI
#  include "aver/audio/audio_abi.h"
#endif
#if AVER_MODULE_FRAMEWORK
#  include "aver/framework/framework_sequence_abi.h"
#  include "aver/framework/framework_timers_abi.h"
#endif

#include <functional>
#include <string>
#include <unordered_set>

namespace aver::game {

// Hooks over the audio ABI and the framework event bus. `contentDir` roots sound and .ocseq paths;
// `graphFire(entity, name)` delivers an entity graph event (ScriptHost::graphFire) and may be empty.
inline SequenceHooks makeSequenceHooks(const std::string& contentDir,
                                       std::function<bool(i32, const std::string&)> graphFire) {
    SequenceHooks h;
    const auto full = [contentDir](const std::string& path) {
        return contentDir.empty() ? path : contentDir + "/" + path;
    };
#if AVER_WITH_AUDIO_ABI
    h.loadSound = [full](const std::string& path) -> i32 { return aver_audio_load(full(path).c_str()); };
    h.playVoice = [](i32 sound, const SeqVoice& v) -> i32 {
        if (v.spatial)
            return aver_audio_play_at(sound, v.pos.x, v.pos.y, v.pos.z, v.volume, v.pitch, v.loop ? 1 : 0,
                                      AVER_AUDIO_BUS_SFX, v.innerCm, v.outerCm);
        return aver_audio_play(sound, v.volume, v.pitch, v.loop ? 1 : 0, AVER_AUDIO_BUS_SFX);
    };
    h.updateVoice = [](i32 voice, const Vec3* pos, f32 volume) {
        aver_audio_set_voice_volume(voice, volume);
        if (pos) aver_audio_set_voice_position(voice, pos->x, pos->y, pos->z);
    };
    h.stopVoice = [](i32 voice, f32 fade) {
        if (fade > 0.0f) aver_audio_fade_voice(voice, 0.0f, fade, AVER_AUDIO_FADE_LINEAR, 1);
        else             aver_audio_stop(voice);
    };
#endif
#if AVER_MODULE_FRAMEWORK
    h.fireEvent = [graphFire](const SeqEventCall& c) {
        const i32 target = static_cast<i32>(static_cast<u32>(c.target == scene::kInvalidEntity ? 0u : c.target));
        if (c.route == 0 || c.route == 2) {
            aver_fw_event_payload_clear();
            for (u8 a = 0; a < c.argc && a < 4; ++a) aver_fw_event_push_float(c.args[a]);
            if (!c.arg.empty()) aver_fw_event_push_string(c.arg.c_str());
            aver_fw_event_post(c.name.c_str(), 0, target);   // delivered at the next flush: one frame of latency
        }
        if ((c.route == 1 || c.route == 2) && target != 0 && graphFire) graphFire(target, c.name);
    };
#else
    (void)graphFire;
#endif
    h.readFile = [full](const std::string& path) {
        std::string text;
        readFileText(full(path), text);
        return text;
    };
    return h;
}

// The level's actors by placement index and by name (placement `name`, then light names; the first of a
// duplicate wins and is logged once). `lights` are the entities spawnLevelLights returned, in record order.
inline SeqBindings makeLevelBindings(const fmt::OcWorldData& w, const std::vector<scene::Entity>& byPlacement,
                                     const std::vector<scene::Entity>& lights) {
    SeqBindings b;
    b.byPlacement = byPlacement;
    std::unordered_set<std::string> warned;
    const auto add = [&](const std::string& name, scene::Entity e) {
        if (name.empty() || e == scene::kInvalidEntity) return;
        if (!b.byName.emplace(name, e).second && warned.insert(name).second)
            AVER_WARN("[Sequencer] two actors are named '{}'; tracks bound by that name use the first", name);
    };
    for (usize i = 0; i < w.placements.size() && i < byPlacement.size(); ++i) add(w.placements[i].name, byPlacement[i]);
    for (usize i = 0; i < w.lights.size() && i < lights.size(); ++i) add(w.lights[i].name, lights[i]);
    b.find = [](const std::string& n) { return scene::World::instance().find(n); };
    return b;
}

#if AVER_MODULE_FRAMEWORK
// The framework relay's provider (framework_sequence_abi.h), with the SequenceDirector as `user`.
// Declared in the ABI header as aver_fw_sequence_fn; the composition root installs it:
//   aver_fw_set_sequence_provider(&game::sequenceProvider, &director);
inline int32_t sequenceProvider(int32_t op, int32_t entity, int64_t h0, int64_t h1, float f0, float f1, float* outF,
                                int64_t* outI, void* user) {
    if (!user) return 0;
    SequenceDirector& d = *static_cast<SequenceDirector*>(user);
    const auto idx = [&](int64_t hash) { return d.find(static_cast<u64>(hash)); };
    switch (op) {
        case AVER_FW_SEQ_OP_PLAY:     return d.play(idx(h0), f0, f1) ? 1 : 0;
        case AVER_FW_SEQ_OP_PAUSE:    return d.pause(idx(h0)) ? 1 : 0;
        case AVER_FW_SEQ_OP_STOP: {   // 0 = every sequence; an unknown name must not read as "all"
            const i32 s = h0 == 0 ? -1 : idx(h0);
            return (h0 == 0 || s >= 0) && d.stop(s) ? 1 : 0;
        }
        case AVER_FW_SEQ_OP_SET_TIME: return d.setTime(idx(h0), f0) ? 1 : 0;
        case AVER_FW_SEQ_OP_SET_RATE: return d.setRate(idx(h0), f0) ? 1 : 0;
        case AVER_FW_SEQ_OP_PLAYING:
            if (h0 == 0) return d.playingAny() ? 1 : 0;
            if (const SequencePlayer* p = d.player(idx(h0))) return p->playing() ? 1 : 0;
            return 0;
        case AVER_FW_SEQ_OP_STATE: {
            f64 t = 0, len = 0, rate = 0;
            bool playing = false;
            if (!d.state(idx(h0), t, len, rate, playing)) return 0;
            if (outF) { outF[0] = static_cast<float>(t); outF[1] = static_cast<float>(len); outF[2] = static_cast<float>(rate); }
            if (outI) outI[0] = playing ? 1 : 0;
            return 1;
        }
        case AVER_FW_SEQ_OP_BIND:
            return d.bindOverride(idx(h0), static_cast<u64>(h1),
                                  entity <= 0 ? scene::kInvalidEntity : static_cast<scene::Entity>(static_cast<u32>(entity))) ? 1 : 0;
        default: return 0;
    }
}
#endif

} // namespace aver::game

#endif
