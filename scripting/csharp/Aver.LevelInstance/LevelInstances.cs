// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The game-author-facing level-instance API, and the seam the AN_ level-instance nodes' compiled IL calls.

namespace Aver.LevelInstance;

/// <summary>Place, stream and query level instances: one <c>.ocworld</c> placed inside another.
/// Entities are the same int handles <c>Aver.Scene</c> uses.</summary>
/// <remarks>
/// Every call returns 0 (or false) until the host has installed its level-instance system, which the editor and the
/// runtime do at start-up. A level is named by its content-relative path, <c>"Levels/Room.ocworld"</c>. A child
/// placement inside an instance is named by its stable id in hex: <c>"c3"</c>, or <c>"b2/c9"</c> through a nested
/// instance (leading zeros optional).
/// </remarks>
public static class LevelInstances
{
    /// <summary>The ABI version the native library was built with: (major &lt;&lt; 16) | minor.</summary>
    public static int AbiVersion => Native.aver_levelinst_abi_version();

    /// <summary>Spawns a runtime-only instance (it is not written to the level) and returns its root entity, 0 on failure.</summary>
    /// <param name="level">The child level, relative to the project's Content folder.</param>
    /// <param name="yaw">Degrees about the up axis; <paramref name="pitch"/> and <paramref name="roll"/> likewise.</param>
    /// <param name="scale">Uniform; 0 is read as 1.</param>
    /// <param name="parent">The entity to parent it to, 0 for none.</param>
    /// <param name="packed">Show the baked pack of the level instead of its entities, when one is valid and the renderer can.</param>
    public static int Spawn(string level, float x, float y, float z, float yaw = 0f, float pitch = 0f, float roll = 0f,
                            float scale = 1f, int parent = 0, bool packed = false)
        => string.IsNullOrEmpty(level) ? 0 : Native.aver_levelinst_spawn(level, parent, x, y, z, yaw, pitch, roll, scale, packed ? 1 : 0);

    /// <summary>Destroys the instance <paramref name="root"/> and everything under it.</summary>
    public static bool Destroy(int root) => root != 0 && Native.aver_levelinst_destroy(root) != 0;

    /// <summary>The root of the (innermost) instance <paramref name="entity"/> belongs to; 0 when it is not part of one.</summary>
    public static int RootOf(int entity) => entity == 0 ? 0 : Native.aver_levelinst_root_of(entity);

    /// <summary>True when <paramref name="entity"/> is part of a level instance.</summary>
    public static bool IsMember(int entity) => entity != 0 && Native.aver_levelinst_is_member(entity) != 0;

    /// <summary>The entity at <paramref name="idPath"/> inside an instance: "" is the root, "c3" a child placement, "b2/c9" a placement of a nested instance.</summary>
    public static int Find(int root, string idPath = "") => root == 0 ? 0 : Native.aver_levelinst_find(root, idPath ?? "");

    /// <summary>Loads or unloads the instance by hand. A hand-loaded instance is never evicted by streaming.</summary>
    public static bool SetLoaded(int root, bool loaded) => root != 0 && Native.aver_levelinst_set_loaded(root, loaded ? 1 : 0) != 0;

    /// <summary>True while the instance's contents exist; false when unloaded or when <paramref name="root"/> is not an instance.</summary>
    public static bool IsLoaded(int root) => root != 0 && Native.aver_levelinst_is_loaded(root) > 0;

    /// <summary>Rebuilds the instance from its level asset, keeping its overrides.</summary>
    public static bool Reload(int root) => root != 0 && Native.aver_levelinst_reload(root) != 0;

    /// <summary>Switches packed mode. True when the effective state now equals the request: false means the pack is missing or stale,
    /// the renderer cannot show packed rows, or the instance has overrides; it then stays unpacked.</summary>
    public static bool SetPacked(int root, bool packed) => root != 0 && Native.aver_levelinst_set_packed(root, packed ? 1 : 0) != 0;

    /// <summary>The instance's world bounds in centimetres; false when they are not known.</summary>
    public static bool TryGetBounds(int root, out (float X, float Y, float Z) min, out (float X, float Y, float Z) max)
    {
        min = max = default;
        if (root == 0) return false;
        var lo = new float[3];
        var hi = new float[3];
        if (Native.aver_levelinst_bounds(root, lo, hi) == 0) return false;
        min = (lo[0], lo[1], lo[2]);
        max = (hi[0], hi[1], hi[2]);
        return true;
    }

    /// <summary>How many instances exist (nested ones included).</summary>
    public static int Count => Native.aver_levelinst_count();

    /// <summary>The root of the instance at <paramref name="index"/>, 0 past the end.</summary>
    public static int At(int index) => index < 0 ? 0 : Native.aver_levelinst_at(index);

    /// <summary>fnv1a64 of the instance's asset path, the id its entities' link stores; 0 for a handle that is not an instance root.</summary>
    public static long AssetId(int root) => root != 0 && Native.aver_levelinst_asset_id(root, out long id) != 0 ? id : 0L;
}

/// <summary>Reads the reason the last level-instance call failed. The calls keep their 0 / non-zero returns; the
/// reason travels on its own thread-local entry point.</summary>
public static class LevelInstanceAbi
{
    /// <summary>Why the last level-instance call on this thread failed, or <see cref="LevelInstanceError.Ok"/>.</summary>
    public static LevelInstanceError LastError => (LevelInstanceError)Native.aver_levelinst_last_error();

    /// <summary>True when the last level-instance call on this thread recorded a failure.</summary>
    public static bool LastCallFailed => Native.aver_levelinst_last_error() < 0;
}

/// <summary>
/// Scalar-signature seam the AN_SpawnLevelInstance family of nodes' compiled IL calls (the shape GraphPrefabs has for the
/// prefab nodes). The level path and the id path are node ATTRIBUTES (<c>level=</c> and <c>path=</c>), not pins, because a
/// graph pin is float, int or bool. Wiring: docs/LEVEL_INSTANCES.md.
/// </summary>
public static class GraphLevelInstances
{
    /// <summary>The node type names, for the parser and the catalog to share.</summary>
    public const string SpawnNodeType = "AN_SpawnLevelInstance";
    public const string DestroyNodeType = "AN_DestroyLevelInstance";
    public const string RootNodeType = "AN_GetLevelInstanceRoot";
    public const string FindNodeType = "AN_FindLevelInstanceEntity";
    public const string SetLoadedNodeType = "AN_SetLevelInstanceLoaded";
    public const string IsLoadedNodeType = "AN_IsLevelInstanceLoaded";
    public const string ReloadNodeType = "AN_ReloadLevelInstance";
    public const string SetPackedNodeType = "AN_SetLevelInstancePacked";
    public const string BoundsNodeType = "AN_GetLevelInstanceBounds";
    public const string CountNodeType = "AN_GetLevelInstanceCount";
    public const string AtNodeType = "AN_GetLevelInstanceAt";

    /// <summary>AN_SpawnLevelInstance: the new instance's root, 0 on failure.</summary>
    public static int SpawnForGraph(int parent, float x, float y, float z, float yaw, float scale, bool packed, string level)
        => LevelInstances.Spawn(level, x, y, z, yaw, 0f, 0f, scale, parent, packed);

    /// <summary>AN_DestroyLevelInstance.</summary>
    public static bool DestroyForGraph(int root) => LevelInstances.Destroy(root);

    /// <summary>AN_GetLevelInstanceRoot: the root of the instance the entity is part of, 0 if none.</summary>
    public static int RootOfForGraph(int entity) => LevelInstances.RootOf(entity);

    /// <summary>AN_FindLevelInstanceEntity: the entity at the id path inside the instance, 0 if none; an empty path is the root.</summary>
    public static int FindForGraph(int root, string path) => LevelInstances.Find(root, path);

    /// <summary>AN_SetLevelInstanceLoaded.</summary>
    public static bool SetLoadedForGraph(int root, bool loaded) => LevelInstances.SetLoaded(root, loaded);

    /// <summary>AN_IsLevelInstanceLoaded.</summary>
    public static bool IsLoadedForGraph(int root) => LevelInstances.IsLoaded(root);

    /// <summary>AN_ReloadLevelInstance.</summary>
    public static bool ReloadForGraph(int root) => LevelInstances.Reload(root);

    /// <summary>AN_SetLevelInstancePacked: true when the effective state matches the request.</summary>
    public static bool SetPackedForGraph(int root, bool packed) => LevelInstances.SetPacked(root, packed);

    /// <summary>AN_GetLevelInstanceBounds: the world AABB, false when unknown (the outputs are then 0).</summary>
    public static bool BoundsForGraph(int root, out float minX, out float minY, out float minZ,
                                      out float maxX, out float maxY, out float maxZ)
    {
        if (LevelInstances.TryGetBounds(root, out var lo, out var hi))
        {
            minX = lo.X; minY = lo.Y; minZ = lo.Z;
            maxX = hi.X; maxY = hi.Y; maxZ = hi.Z;
            return true;
        }
        minX = minY = minZ = maxX = maxY = maxZ = 0f;
        return false;
    }

    /// <summary>AN_GetLevelInstanceCount.</summary>
    public static int CountForGraph() => LevelInstances.Count;

    /// <summary>AN_GetLevelInstanceAt.</summary>
    public static int AtForGraph(int index) => LevelInstances.At(index);
}
