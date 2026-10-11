// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Entity conveniences for environment queries and smart objects, taking gameplay tags where the native
// layer takes comma-separated tag names. The native layer matches tags as dotted-prefix strings; a
// GameplayTag passes its Name.

using Aver.Scene;
using Aver.Synapse;

namespace Aver.Framework;

public readonly partial struct Entity
{
    /// <summary>Runs an environment query with this entity as the querier. <paramref name="sync"/> finishes it before
    /// returning; otherwise it is queued and finishes over the next ticks (EqsFinished fires on this entity).</summary>
    public EqsRun RunQuery(string contentPath, Entity target = default, Vec3? point = null, bool sync = true, GameplayTagContainer? userTags = null)
    {
        int id = Eqs.Load(contentPath);
        if (id == 0) return default;
        var req = new EqsRequest
        {
            Querier = Handle,
            Target = target.Handle,
            HasPoint = point.HasValue,
            PointX = point?.X ?? 0f,
            PointY = point?.Y ?? 0f,
            PointZ = point?.Z ?? 0f,
            UserTags = EntityAiTags.Csv(userTags),
        };
        return sync ? Eqs.Run(id, req) : Eqs.Start(id, req);
    }

    /// <summary>Claims a smart-object slot for this entity; the claim handle, or 0 when refused
    /// (see <see cref="SmartObjects.LastRefusal"/>).</summary>
    public int ClaimSmartObject(SlotRef slot, GameplayTagContainer? userTags = null)
        => SmartObjects.Claim(Handle, slot, EntityAiTags.Csv(userTags));

    /// <summary>The nearest free slot within <paramref name="radius"/> of this entity that this entity may claim.</summary>
    public SlotRef? FindSmartObject(float radius, string? slotTags = null, GameplayTagContainer? userTags = null)
    {
        Vec3 p = WorldPosition;
        List<SlotRef> found = SmartObjects.Find(Handle, p.X, p.Y, p.Z, radius, slotTags, EntityAiTags.Csv(userTags), FindFlags.None, 1);
        return found.Count == 0 ? null : found[0];
    }
}

internal static class EntityAiTags
{
    /// <summary>"A.B,C" from a container's tags, null for none.</summary>
    internal static string? Csv(GameplayTagContainer? tags)
    {
        if (tags == null) return null;
        var parts = new List<string>();
        foreach (GameplayTag t in tags)
            if (t.IsValid) parts.Add(t.Name);
        return parts.Count == 0 ? null : string.Join(',', parts);
    }
}
