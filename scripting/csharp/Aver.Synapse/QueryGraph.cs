// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
//
// The graph-facing surface of environment queries and smart objects: one method per AN_ node, primitive-only
// signatures (strings are NODE attributes, never pins). A failure returns false / 0 and never throws; a
// "nothing to report" outcome is an out value. The node table (Aver.Graph) calls these by name.
namespace Aver.Synapse;

/// <summary>The bodies of the AN_Eqs* and AN_So* nodes.</summary>
public static class QueryGraph
{
    // ---- AN_EqsRun (exec): runs to completion and reports the best item ----------------------------------
    public static bool EqsRunForGraph(int querier, int target, float px, float py, float pz, bool hasPoint, string query, string userTags,
                                      out int count, out float x, out float y, out float z, out int entity, out float score)
    {
        count = 0; x = y = z = 0f; entity = 0; score = 0f;
        if (string.IsNullOrEmpty(query)) return false;
        try
        {
            int id = Eqs.Load(query);
            if (id == 0) return false;
            var req = new EqsRequest { Querier = querier, Target = target, PointX = px, PointY = py, PointZ = pz, HasPoint = hasPoint, UserTags = Tags(userTags) };
            EqsRun run = Eqs.Run(id, req);
            if (run.Handle == 0) return false;
            count = run.Count;
            EqsItem best = default;
            bool found = count > 0 && run.TryGetItem(0, out best);
            if (found) { x = best.X; y = best.Y; z = best.Z; entity = best.Entity; score = best.Score; }
            run.Release();
            return found;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    // ---- AN_EqsStart (exec): queues a run, returns its handle (0 = refused) -------------------------------
    public static int EqsStartForGraph(int querier, int target, float px, float py, float pz, bool hasPoint, int priority, string query, string userTags)
    {
        if (string.IsNullOrEmpty(query)) return 0;
        try
        {
            int id = Eqs.Load(query);
            if (id == 0) return 0;
            var req = new EqsRequest { Querier = querier, Target = target, PointX = px, PointY = py, PointZ = pz, HasPoint = hasPoint, UserTags = Tags(userTags), Priority = priority };
            return Eqs.Start(id, req).Handle;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return 0; }
    }

    // ---- AN_EqsStatus (pure): status is an EqsStatus value ------------------------------------------------
    public static int EqsStatusForGraph(int handle, out int count)
    {
        count = 0;
        if (handle == 0) return 0;
        try
        {
            var run = new EqsRun(handle);
            count = run.Count;
            return (int)run.Status;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return 0; }
    }

    // ---- AN_EqsGetItem (pure) -----------------------------------------------------------------------------
    public static bool EqsGetItemForGraph(int handle, int index, out float x, out float y, out float z, out int entity, out float score, out int obj, out int slot)
    {
        x = y = z = 0f; entity = 0; score = 0f; obj = 0; slot = -1;
        if (handle == 0) return false;
        try
        {
            if (!new EqsRun(handle).TryGetItem(index, out EqsItem it)) return false;
            x = it.X; y = it.Y; z = it.Z; entity = it.Entity; score = it.Score; obj = it.Object; slot = it.Slot;
            return true;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    // ---- AN_EqsCancel / AN_EqsRelease (exec) --------------------------------------------------------------
    public static bool EqsCancelForGraph(int handle)
    {
        try { return handle != 0 && new EqsRun(handle).Cancel(); }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    public static bool EqsReleaseForGraph(int handle)
    {
        try
        {
            if (handle == 0 || new EqsRun(handle).Status == EqsStatus.Unknown) return false;
            new EqsRun(handle).Release();
            return true;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    // ---- AN_SoAttach (exec) -------------------------------------------------------------------------------
    public static bool SoAttachForGraph(int entity, string asset)
    {
        if (string.IsNullOrEmpty(asset)) return false;
        try { return SmartObjects.Attach(entity, asset); }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    // ---- AN_SoFind (pure): the nearest slot and how many matched ------------------------------------------
    public static bool SoFindForGraph(int user, float x, float y, float z, float radius, bool matchAll, string slotTags, string userTags,
                                      out int obj, out int slot, out float ex, out float ey, out float ez, out int count)
    {
        obj = 0; slot = -1; ex = ey = ez = 0f; count = 0;
        try
        {
            var found = SmartObjects.Find(user, x, y, z, radius, Tags(slotTags), Tags(userTags), matchAll ? FindFlags.MatchAll : FindFlags.None, 16);
            count = found.Count;
            if (count == 0) return false;
            obj = found[0].Object;
            slot = found[0].Slot;
            if (SmartObjects.TryGetSlot(obj, slot, out SlotInfo info)) { ex = info.EntryX; ey = info.EntryY; ez = info.EntryZ; }
            return true;
        }
        catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    // ---- AN_SoClaim (exec): the claim handle (0 = refused) and the refusal ---------------------------------
    public static int SoClaimForGraph(int user, int obj, int slot, float ttl, string userTags, out int refusal)
    {
        refusal = (int)ClaimRefusal.None;
        try
        {
            int claim = SmartObjects.Claim(user, new SlotRef(obj, slot), Tags(userTags), ttl);
            if (claim == 0) refusal = (int)SmartObjects.LastRefusal;
            return claim;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return 0; }
    }

    // ---- AN_SoUse / AN_SoRelease (exec) --------------------------------------------------------------------
    public static bool SoUseForGraph(int claim)
    {
        try { return claim != 0 && SmartObjects.Use(claim); }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    public static bool SoReleaseForGraph(int claim, int reason)
    {
        try { return claim != 0 && SmartObjects.Release(claim, (ReleaseReason)reason); }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    // ---- AN_SoGetClaim (pure): the user's index-th claim ----------------------------------------------------
    public static bool SoGetClaimForGraph(int user, int index, out int claim, out int obj, out int slot, out int state,
                                          out float x, out float y, out float z, out float yaw, out float ex, out float ey, out float ez)
    {
        claim = obj = state = 0; slot = -1; x = y = z = yaw = ex = ey = ez = 0f;
        if (index < 0 || index >= 4) return false;
        try
        {
            Span<int> claims = stackalloc int[4];
            int n = SmartObjects.ClaimsOf(user, claims);
            if (index >= n || !SmartObjects.TryGetClaim(claims[index], out ClaimInfo c)) return false;
            claim = c.Handle; obj = c.Slot.Object; slot = c.Slot.Slot; state = (int)c.State;
            x = c.X; y = c.Y; z = c.Z; yaw = c.Yaw; ex = c.EntryX; ey = c.EntryY; ez = c.EntryZ;
            return true;
        }
        catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    // ---- AN_SoSlotInfo (pure) --------------------------------------------------------------------------------
    public static bool SoSlotInfoForGraph(int entity, int slot, out int state, out int user, out float x, out float y, out float z, out float yaw)
    {
        state = user = 0; x = y = z = yaw = 0f;
        try
        {
            if (!SmartObjects.TryGetSlot(entity, slot, out SlotInfo i)) return false;
            state = (int)i.State; user = i.User; x = i.X; y = i.Y; z = i.Z; yaw = i.Yaw;
            return true;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    // ---- AN_SoSlotCount (pure): -1 when the entity is not a smart object --------------------------------------
    public static int SoSlotCountForGraph(int entity)
    {
        try { return SmartObjects.SlotCount(entity); }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return -1; }
    }

    // ---- AN_SoSetSlotEnabled (exec) ---------------------------------------------------------------------------
    public static bool SoSetSlotEnabledForGraph(int entity, int slot, bool enabled)
    {
        try { return SmartObjects.SetSlotEnabled(entity, slot, enabled); }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException) { return false; }
    }

    private static string? Tags(string? s) => string.IsNullOrWhiteSpace(s) ? null : s;
}
