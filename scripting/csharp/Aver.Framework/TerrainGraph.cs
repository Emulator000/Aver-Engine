// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Terrain and water: the entry points the Aver Node landscape and water nodes call (docs/LANDSCAPE_LAYERS_WATER.md).

using Aver.Scripting;

namespace Aver.Framework;

/// <summary>What the landscape and water graph nodes call. Reached by reflection from Aver.Graph, like <c>GasGraph</c>.
/// Graph strings are node attributes, never pins, so a method taking both an id and a name lets a wired id win and falls back
/// to the attribute. Nothing here throws: a missing provider or a point off the section answers the defaults (found false,
/// weight 0, surface id 0), and a node with neither an id nor a name logs once and answers its default.</summary>
internal static class TerrainGraph
{
    private static readonly HashSet<string> Logged = new();

    private static void LogOnce(string key, string message)
    {
        if (Logged.Add(key)) Log.Warn(message);
    }

    /// <summary>AN_LandscapeSample: ground height, normal and dominant layer at a world XY. False off the section.</summary>
    internal static bool SampleForGraph(float x, float y, out float z, out float nx, out float ny, out float nz, out int layer)
    {
        GroundSample g = Landscape.Sample(x, y);
        z = g.Z; nx = g.Normal.X; ny = g.Normal.Y; nz = g.Normal.Z; layer = g.DominantLayer;
        return g.Found;
    }

    /// <summary>AN_LandscapeLayerId: the index of the layer called <paramref name="layer"/>, or -1 (also when the node has no
    /// layer= attribute, which is logged once).</summary>
    internal static int LayerIdForGraph(string layer)
    {
        if (string.IsNullOrEmpty(layer))
        {
            LogOnce("terrain:nolayer", "[Terrain] an AN_LandscapeLayerId node has no layer= attribute");
            return -1;
        }
        return Landscape.LayerIndex(layer);
    }

    /// <summary>AN_LandscapeLayerWeight: the paint weight of a layer at a world XY. A wired <paramref name="layerId"/> (0 or more)
    /// wins; otherwise the layer= attribute names it. 0 when neither resolves.</summary>
    internal static float LayerWeightForGraph(float x, float y, int layerId, string layer)
    {
        if (layerId < 0)
        {
            if (string.IsNullOrEmpty(layer))
            {
                LogOnce("terrain:noweightlayer", "[Terrain] an AN_LandscapeLayerWeight node has neither a layerId pin nor a layer= attribute");
                return 0.0f;
            }
            layerId = Landscape.LayerIndex(layer);
        }
        return Landscape.LayerWeight(x, y, layerId);
    }

    /// <summary>AN_LandscapeSurface: the dominant surface id (0 = none), the averaged friction and restitution, and the
    /// dominant layer at a world XY.</summary>
    internal static int SurfaceForGraph(float x, float y, out float friction, out float restitution, out int layer)
    {
        GroundSample g = Landscape.Sample(x, y);
        friction = g.Friction; restitution = g.Restitution; layer = g.DominantLayer;
        return g.Found ? g.SurfaceId : 0;
    }

    /// <summary>AN_SurfaceId: the id of a surface name (the node's surface= attribute); 0 for none.</summary>
    internal static int SurfaceIdForGraph(string surface) => Surface.IdOf(surface);

    /// <summary>AN_WaterSample: the water at a world position. True when water covers the XY.</summary>
    internal static bool WaterSampleForGraph(float x, float y, float z, out bool submerged, out float surfaceZ, out float depth,
                                             out float flowX, out float flowY, out float flowZ, out int kind)
    {
        WaterSample w = Water.Sample(new Aver.Scene.Vec3(x, y, z));
        submerged = w.Submerged; surfaceZ = w.SurfaceZ; depth = w.DepthCm;
        flowX = w.Current.X; flowY = w.Current.Y; flowZ = w.Current.Z; kind = (int)w.Kind;
        return w.Over;
    }

    /// <summary>AN_EntityWater: how much of an entity's <paramref name="heightCm"/> tall body is under water (0..1), and the
    /// current at it. False when the entity is not over water or is dead.</summary>
    internal static bool EntityWaterForGraph(int entity, float heightCm, out float submersion, out float flowX, out float flowY,
                                             out float flowZ, out int kind)
    {
        submersion = 0.0f; flowX = flowY = flowZ = 0.0f; kind = 0;
        if (entity == 0) return false;
        Aver.Scene.Vec3 p = new Entity(entity).WorldPosition;
        WaterSample w = Water.Sample(p);
        if (!w.Over) return false;
        float h = heightCm > 1.0f ? heightCm : 180.0f;
        submersion = Math.Clamp((w.SurfaceZ - p.Z) / h, 0.0f, 1.0f);
        flowX = w.Current.X; flowY = w.Current.Y; flowZ = w.Current.Z; kind = (int)w.Kind;
        return true;
    }
}
