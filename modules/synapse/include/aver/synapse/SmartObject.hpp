#pragma once
// The smart-object registry: objects with claimable slots, and the Free -> Claimed -> Occupied ->
// (Cooldown) -> Free state machine. Pure: ids are u32 (the join passes entity handles), world transforms
// are pushed in by the join, no scene or world type appears here.
#include "aver/synapse/SmartObjectAsset.hpp"

#include <map>
#include <vector>

namespace aver::synapse {

enum class SoSlotState : u8 { Free, Claimed, Occupied, Cooldown, Disabled };
enum class SoReleaseReason : u8 { Released, Completed, Failed, Expired, UserGone, ObjectGone };
enum class SoRefusal : u8 { None, NoSuchSlot, Disabled, NotFree, Tags, UserLimit, NoUser };

inline constexpr u32 kSoMaxClaimsPerUser = 4;

// A slot resolved into world space by the join from the owner's world matrix.
struct SoWorldSlot {
    Vec3 pos, entry;
    f32 yawDeg = 0, entryYawDeg = 0;
};

struct SoSlotRef {
    u32 object = 0;
    u32 slot = 0;
    bool operator==(const SoSlotRef& o) const { return object == o.object && slot == o.slot; }
};

struct SoQuery {
    Vec3 origin;
    f32 radius = 1500;
    TagList slotTags;       // matched against the object's tags plus the slot's; empty = any slot
    bool matchAll = false;
    bool includeClaimed = false;
    TagList userTags;       // the slot's REQUIRES / BLOCKEDBY are checked against these
    u32 max = 16;
};

struct SoSlotInfo {
    SoSlotState state = SoSlotState::Free;
    u32 user = 0;
    u32 claim = 0;
    f32 timer = 0;          // Cooldown: seconds left
    SoWorldSlot world;
    const SoSlotDef* def = nullptr;
};

struct SoClaimInfo {
    u32 handle = 0;
    SoSlotRef slot;
    u32 user = 0;
    SoSlotState state = SoSlotState::Claimed;   // Claimed or Occupied
    f32 age = 0;
    SoWorldSlot world;
    const SoSlotDef* def = nullptr;
};

struct SoRelease {
    u32 claim = 0;
    SoSlotRef slot;
    u32 user = 0;
    SoReleaseReason reason = SoReleaseReason::Released;
    bool wasUsed = false;   // the claim had reached Occupied
    const SoSlotDef* def = nullptr;
};

class SmartObjectRegistry {
public:
    // Adds or updates an object. `def` must outlive the registration. Slot states survive an update
    // (matched by slot name); claims on slots that no longer exist are released (ObjectGone) on the next tick.
    void setObject(u32 object, const SoDef* def, bool enabled);
    void setWorld(u32 object, u32 slot, const SoWorldSlot& w);
    // Releases every claim on the object (ObjectGone) and forgets it.
    void removeObject(u32 object, std::vector<SoRelease>& released);
    bool hasObject(u32 object) const { return objects_.count(object) != 0; }
    const SoDef* defOf(u32 object) const;
    std::vector<u32> objects() const;

    u32 slotCount(u32 object) const;
    bool slotInfo(u32 object, u32 slot, SoSlotInfo& out) const;
    bool setSlotEnabled(u32 object, u32 slot, bool enabled);
    bool objectEnabled(u32 object) const;

    // Slots near the origin satisfying the query, nearest first (ties by object then slot).
    u32 find(const SoQuery& q, SoSlotRef* out, f32* distOut, u32 cap) const;

    // Claim handle (nonzero), or 0 with `why` set. ttlOverride < 0 uses the slot's CLAIMTTL, 0 = never expires.
    u32 claim(SoSlotRef slot, u32 user, const TagList& userTags, f32 ttlOverride, SoRefusal* why);
    bool use(u32 claim);
    bool release(u32 claim, SoReleaseReason reason, SoRelease* out = nullptr);
    bool claimInfo(u32 claim, SoClaimInfo& out) const;
    u32 claimsOf(u32 user, u32* out, u32 cap) const;
    u32 claimCount() const { return liveClaims_; }
    void releaseUser(u32 user, std::vector<SoRelease>& out);

    // Claim time-outs and cooldown timers; also flushes releases queued by setObject.
    void tick(f32 dt, std::vector<SoRelease>& released);

private:
    struct Slot {
        SoSlotState state = SoSlotState::Free;
        u32 claim = 0;
        f32 timer = 0;
        bool disabled = false;
        SoWorldSlot world;
    };
    struct Obj {
        const SoDef* def = nullptr;
        bool enabled = true;
        std::vector<Slot> slots;
    };
    struct ClaimRec {
        u32 gen = 1;
        bool active = false;
        u32 object = 0, slot = 0, user = 0;
        SoSlotState state = SoSlotState::Claimed;
        f32 age = 0, ttl = 0;
    };

    ClaimRec* rec(u32 handle);
    const ClaimRec* rec(u32 handle) const;
    SoSlotState visibleState(const Obj& o, const Slot& s) const;
    bool releaseRec(u32 handle, SoReleaseReason reason, bool cooldown, SoRelease* out);

    std::map<u32, Obj> objects_;
    std::vector<ClaimRec> claims_;
    std::vector<u32> freeClaims_;
    u32 liveClaims_ = 0;
    std::vector<SoRelease> pending_;
};

} // namespace aver::synapse
