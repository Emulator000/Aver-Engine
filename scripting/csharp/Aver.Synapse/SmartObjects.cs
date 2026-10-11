// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Smart objects: entities with claimable slots (an .ocso asset on a CSmartObject component). Find a slot,
// claim it, use it, release it. Native side: synapse_query_abi.h; docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md.
namespace Aver.Synapse;

/// <summary>A slot's state. Mirrors AVER_SYN_SO_FREE and friends.</summary>
public enum SlotState { Free = 0, Claimed = 1, Occupied = 2, Cooldown = 3, Disabled = 4 }

/// <summary>Why a claim ended. Mirrors AVER_SYN_SO_REL_*.</summary>
public enum ReleaseReason { Released = 0, Completed = 1, Failed = 2, Expired = 3, UserGone = 4, ObjectGone = 5 }

/// <summary>Why a claim was refused. Mirrors AVER_SYN_SO_REFUSE_*.</summary>
public enum ClaimRefusal { None = 0, NoSuchSlot, Disabled, NotFree, Tags, UserLimit, NoUser }

[Flags]
public enum FindFlags { None = 0, MatchAll = 1, IncludeClaimed = 2 }

/// <summary>One slot of one object: the object's entity handle and the slot's index in its definition.</summary>
public readonly record struct SlotRef(int Object, int Slot);

/// <summary>A slot in world space (centimetres, yaw in degrees).</summary>
public readonly record struct SlotInfo(SlotRef Ref, SlotState State, int User, float X, float Y, float Z, float Yaw,
                                       float EntryX, float EntryY, float EntryZ, float EntryYaw);

public readonly record struct ClaimInfo(int Handle, SlotRef Slot, int User, SlotState State, float X, float Y, float Z, float Yaw,
                                        float EntryX, float EntryY, float EntryZ, float EntryYaw);

/// <summary>Finding, claiming and using smart-object slots. A claim handle is nonzero and dies with the claim;
/// a stale handle fails cleanly. Events (SmartObjectClaimed, SmartObjectUsed, SmartObjectReleased on the user;
/// SmartObjectSlot* on the object) reach graphs through the notify seam.</summary>
public static class SmartObjects
{
    private const int MaxFind = 64;

    /// <summary>Adds the CSmartObject component to the entity and points it at an .ocso asset.</summary>
    public static bool Attach(int entity, string assetPath) => Native.aver_syn_so_attach(entity, assetPath) != 0;
    public static bool SetEnabled(int entity, bool enabled) => Native.aver_syn_so_set_enabled(entity, enabled ? 1 : 0) != 0;

    /// <summary>Slots the entity's definition declares; -1 when it is not a registered smart object.</summary>
    public static int SlotCount(int entity) => Native.aver_syn_so_slot_count(entity);

    public static bool TryGetSlot(int entity, int slot, out SlotInfo info)
    {
        info = default;
        var pos = new float[3];
        var entry = new float[3];
        if (Native.aver_syn_so_slot_info(entity, slot, pos, out float yaw, entry, out float entryYaw, out int state, out int user) == 0) return false;
        info = new SlotInfo(new SlotRef(entity, slot), (SlotState)state, user, pos[0], pos[1], pos[2], yaw, entry[0], entry[1], entry[2], entryYaw);
        return true;
    }

    public static bool SetSlotEnabled(int entity, int slot, bool enabled) => Native.aver_syn_so_set_slot_enabled(entity, slot, enabled ? 1 : 0) != 0;

    /// <summary>Slots within <paramref name="radius"/> of a point, nearest first. <paramref name="slotTags"/> is a
    /// comma-separated list matched against the object's and slot's tags (any, or all with <see cref="FindFlags.MatchAll"/>);
    /// a slot's REQUIRES / BLOCKEDBY are checked against <paramref name="userTags"/>. Returns how many were written.</summary>
    public static int Find(int user, float x, float y, float z, float radius, string? slotTags, string? userTags, FindFlags flags, Span<SlotRef> results)
    {
        int cap = Math.Min(results.Length, MaxFind);
        if (cap <= 0) return 0;
        var objects = new int[cap];
        var slots = new int[cap];
        int n = Native.aver_syn_so_find(user, x, y, z, radius, slotTags, userTags, (int)flags, objects, slots, cap);
        for (int i = 0; i < n; i++) results[i] = new SlotRef(objects[i], slots[i]);
        return Math.Max(n, 0);
    }

    public static List<SlotRef> Find(int user, float x, float y, float z, float radius, string? slotTags = null, string? userTags = null,
                                     FindFlags flags = FindFlags.None, int max = 16)
    {
        var buf = new SlotRef[Math.Clamp(max, 1, MaxFind)];
        int n = Find(user, x, y, z, radius, slotTags, userTags, flags, buf);
        return new List<SlotRef>(buf.AsSpan(0, n).ToArray());
    }

    /// <summary>Claims a slot for <paramref name="user"/>; the claim handle, or 0 when refused (see <see cref="LastRefusal"/>).
    /// <paramref name="ttlSec"/> &lt; 0 uses the slot's CLAIMTTL, 0 never expires.</summary>
    public static int Claim(int user, SlotRef slot, string? userTags = null, float ttlSec = -1f)
        => Native.aver_syn_so_claim(user, slot.Object, slot.Slot, userTags, ttlSec);

    /// <summary>Why the last <see cref="Claim"/> on this thread was refused.</summary>
    public static ClaimRefusal LastRefusal => (ClaimRefusal)Native.aver_syn_so_last_refusal();

    /// <summary>Starts using a claimed slot (Claimed to Occupied); runs the slot's behaviour, if it has one.</summary>
    public static bool Use(int claim) => Native.aver_syn_so_use(claim) != 0;

    public static bool Release(int claim, ReleaseReason reason = ReleaseReason.Released) => Native.aver_syn_so_release(claim, (int)reason) != 0;

    public static bool TryGetClaim(int claim, out ClaimInfo info)
    {
        info = default;
        var pos = new float[3];
        var entry = new float[3];
        if (Native.aver_syn_so_claim_info(claim, out int obj, out int slot, out int user, out int state, pos, out float yaw, entry, out float entryYaw) == 0) return false;
        info = new ClaimInfo(claim, new SlotRef(obj, slot), user, (SlotState)state, pos[0], pos[1], pos[2], yaw, entry[0], entry[1], entry[2], entryYaw);
        return true;
    }

    /// <summary>The claims <paramref name="user"/> holds (at most 4). Returns how many were written.</summary>
    public static int ClaimsOf(int user, Span<int> claims)
    {
        int cap = Math.Min(claims.Length, 4);
        if (cap <= 0) return 0;
        var buf = new int[cap];
        int n = Native.aver_syn_so_claims_of(user, buf, cap);
        for (int i = 0; i < n; i++) claims[i] = buf[i];
        return Math.Max(n, 0);
    }
}
