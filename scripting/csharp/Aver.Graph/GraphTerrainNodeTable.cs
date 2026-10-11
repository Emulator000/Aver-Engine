// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The landscape and water rows of the game-systems node table (docs/plans/wave3/LANDSCAPE_LAYERS_RIVERS.md
// section 3). Every method is in TerrainGraph (Aver.Framework/TerrainGraph.cs); all are reads. A layerId pin
// of 0 or more beats the layer= attribute. Palette mirror: sandbox/src/GraphNodeDefsTerrain.hpp.

using Aver.Framework;

namespace Aver.Graph;

internal static partial class GameSystemNodes
{
    private static GenericNodeSpec[] TerrainTable() => new[]
    {
        Reg("an_landscapesample", GenericKind.Pure, typeof(TerrainGraph), "SampleForGraph", "x:float,y:float", "found:bool,z:float,nx:float,ny:float,nz:float,layer:int", "p:x,p:y,>z,>nx,>ny,>nz,>layer", "found"),
        Reg("an_landscapelayerid", GenericKind.Pure, typeof(TerrainGraph), "LayerIdForGraph", "", "layerId:int", "@layer", "layerId"),
        Reg("an_landscapelayerweight", GenericKind.Pure, typeof(TerrainGraph), "LayerWeightForGraph", "x:float,y:float,layerId:int=-1", "weight:float", "p:x,p:y,p:layerId,@layer?", "weight"),
        Reg("an_landscapesurface", GenericKind.Pure, typeof(TerrainGraph), "SurfaceForGraph", "x:float,y:float", "surfaceId:int,friction:float,restitution:float,layer:int", "p:x,p:y,>friction,>restitution,>layer", "surfaceId"),
        Reg("an_surfaceid", GenericKind.Pure, typeof(TerrainGraph), "SurfaceIdForGraph", "", "surfaceId:int", "@surface", "surfaceId"),
        Reg("an_watersample", GenericKind.Pure, typeof(TerrainGraph), "WaterSampleForGraph", "x:float,y:float,z:float", "over:bool,submerged:bool,surfaceZ:float,depth:float,flowX:float,flowY:float,flowZ:float,kind:int", "p:x,p:y,p:z,>submerged,>surfaceZ,>depth,>flowX,>flowY,>flowZ,>kind", "over"),
        Reg("an_entitywater", GenericKind.Pure, typeof(TerrainGraph), "EntityWaterForGraph", "entity:int,heightCm:float=180", "over:bool,submersion:float,flowX:float,flowY:float,flowZ:float,kind:int", "p:entity,p:heightCm,>submersion,>flowX,>flowY,>flowZ,>kind", "over"),
    };
}
