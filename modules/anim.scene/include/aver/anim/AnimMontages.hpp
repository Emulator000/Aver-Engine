// Montages on entities: the players behind AnimSystem's slots, and the provider the framework relays
// to. Owned by AnimSystem (AnimSystem::montages()); montages advance inside AnimSystem::tick, so no
// host wiring is needed beyond installing the provider. Semantics: docs/ANIM_MONTAGES.md.
#pragma once

#include "aver/anim/Montage.hpp"
#include "aver/scene/World.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aver::anim {

class AnimSystem;

class MontageSystem {
public:
    // Event names go out through AnimSystem's notify sink, so OnEvent / CustomEvent nodes receive them:
    //   notify "Hit" -> "Hit"; notify state -> "Hit_Begin" / "Hit_End";
    //   "<Montage>_Started" / "_BlendOut" / "_Interrupted" / "_Ended"; "<Montage>_Section_<Section>".
    using Sink = std::function<void(scene::Entity, const char*)>;

    // Loads (cached by id; a failure is cached as null) and starts. One player per (entity, montage):
    // playing a running one restarts it. In a slot the newest montage wins and the previous top is
    // stopped (Interrupted); at most kMaxPerSlot players are kept per slot (the oldest is dropped).
    // False: unknown asset, unbound clips, an entity without a skeleton. An unknown start section falls
    // back to the first. Adds a CAnimator when the entity has none, so AnimSystem poses it.
    bool play(AnimSystem& anim, scene::World& w, scene::Entity e, u64 montageId, u64 slotOverride,
              u64 sectionHash, f32 rate, f32 blendIn);
    // blendOut < 0 = the asset's. Return how many players were stopped.
    u32  stop(scene::Entity e, u64 montageId /*0 = all*/, f32 blendOut);
    u32  stopSlot(scene::Entity e, u64 slotHash, f32 blendOut);
    bool jump(scene::Entity e, u64 montageId /*0 = newest*/, u64 sectionHash);
    bool setNextSection(scene::Entity e, u64 montageId /*0 = newest*/, u64 fromHash, u64 toHash /*0 = authored*/);
    bool setRate(scene::Entity e, u64 montageId /*0 = newest*/, f32 rate);
    bool isPlaying(scene::Entity e, u64 montageId /*0 = any*/) const;
    // out = position, length, weight, rate of the montage (0 = newest); sectionHash = fnv1a64 of its section.
    bool state(scene::Entity e, u64 montageId, f32 out[4], u64* sectionHash) const;
    // [wave5/sequencer] DRIVEN players: a sequencer owns the clock. Each call places the player at an exact
    // position with an exact weight (state Hold, no events, no notifies, no root motion, no auto blend-out),
    // so scrubbing is exact. Driven players are not advanced by tick(); one that goes more than 2 ticks
    // without a drive call (the sequencer died without releasing) is dropped silently. They join their slot
    // like any player but are invisible to play/stop/jump/state. Both add a CAnimator when the entity has
    // none. False for an entity without a skeleton or an asset that does not load.
    bool driveMontage(AnimSystem& anim, scene::World& w, scene::Entity e, u64 montageId, f32 position, f32 weight, u64 owner);
    // Plays clip `clipId` (an .ocanim ObjectId) as a synthesised one-segment montage (whole clip, rate 1, no
    // blends, DefaultSlot) at `clipTime` clip seconds.
    bool driveClip(AnimSystem& anim, scene::World& w, scene::Entity e, u64 clipId, f32 clipTime, f32 weight, u64 owner);
    // Silent: no Ended, no notifies. assetId 0 = every driven player of this owner on the entity.
    void releaseDriven(scene::Entity e, u64 owner, u64 assetId);
    // Driven players on `e` (all owners); for tests and diagnostics.
    u32 drivenCount(scene::Entity e) const;
    // Drops every player silently (Play stop, level change): no events are delivered.
    void stopAll();
    // Ends every player (Ended and open NotifyEnds through `sink`), then drops players and cached assets.
    void clear(const Sink& sink);

    // ---- used by AnimSystem ----
    void tick(AnimSystem& anim, scene::World& w, f32 dt, const Sink& sink);
    void prune(const scene::World& w, const Sink& sink);
    bool empty() const { return players_.empty(); }
    // Slot hashes with at least one player on `e`, in first-played order.
    void slotHashes(scene::Entity e, std::vector<u64>& out) const;
    // The slot's montage pose over `base`, and its coverage. False when nothing contributes.
    bool evaluateSlot(scene::Entity e, u64 slotHash, const fmt::OcSkeleton& skel, const Pose& base, Pose& out,
                      f32& coverage);
    // This tick's root delta of the slot's root-motion montages mixed oldest to newest, and its coverage.
    bool rootDelta(scene::Entity e, u64 slotHash, RootDelta& out, f32& weight) const;
    // The authored slot name of the first player in the slot, for diagnostics.
    std::string slotLabel(scene::Entity e, u64 slotHash) const;

    static constexpr usize kMaxPerSlot = 4;

private:
    struct Loaded {
        MontageAsset asset;
        std::string label;                  // asset name, else the file stem: the event-name prefix
    };
    struct Player {
        u64 montageId = 0;
        u64 slot = 0;
        const Loaded* loaded = nullptr;
        MontagePlayer mp;
        RootDelta delta;                    // this tick's, unweighted
        bool driven = false;                // placed by a sequencer, never advanced (driveMontage / driveClip)
        bool dead = false;                  // dropped silently by the next tick
        u64 owner = 0;
        u32 stale = 0;                      // ticks since the last drive call
    };
    using Pending = std::vector<std::pair<scene::Entity, std::string>>;

    const Loaded* loadAsset(AnimSystem& anim, u64 id);
    const Loaded* loadClipAsset(AnimSystem& anim, u64 clipId);       // the synthesised one-segment montage
    bool drive(AnimSystem& anim, scene::World& w, scene::Entity e, const Loaded* L, u64 assetId, f32 position,
               f32 weight, u64 owner, bool clip);
    Player* find(scene::Entity e, u64 montageId);                // 0 = newest not finished
    const Player* find(scene::Entity e, u64 montageId) const;
    static void drain(scene::Entity e, Player& p, Pending& out);
    static void deliver(Pending& events, const Sink& sink);

    std::unordered_map<scene::Entity, std::vector<Player>> players_;
    std::unordered_map<u64, std::unique_ptr<Loaded>> assets_;       // null = failed once
    std::unordered_map<u64, std::unique_ptr<Loaded>> clipAssets_;   // driveClip's, by clip id
    Pending deferred_;                                              // events from play/stop between ticks
    std::vector<MontageEvent> scratchEvents_;
    Pose scratchPose_;
};

// The framework relay's provider (framework_anim_abi.h, aver_fw_set_anim_provider), with the
// AnimSystem as `user`. Declared here so the framework never links this module; the composition root
// hands it across: aver_fw_set_anim_provider(&anim::montageProvider, &anim::animSystem()).
i32 montageProvider(i32 op, i32 entity, i64 h0, i64 h1, i64 h2, f32 f0, f32 f1, f32* outF, i64* outI, void* user);

} // namespace aver::anim
