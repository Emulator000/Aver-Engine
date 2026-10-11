// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The level-instance rows of the game-systems node table (docs/plans/wave3/LEVEL_INSTANCES.md section 3).
// Every method is in GraphLevelInstances (Aver.LevelInstance/LevelInstances.cs); the level path and the
// child id path are NODE attributes (level=, path=). Palette mirror: sandbox/src/GraphNodeDefsLevelInstances.hpp.

using Aver.LevelInstance;

namespace Aver.Graph;

internal static partial class GameSystemNodes
{
    private static GenericNodeSpec[] LevelInstanceTable() => new[]
    {
        Reg("an_spawnlevelinstance", GenericKind.Exec, typeof(GraphLevelInstances), "SpawnForGraph", "parent:int,x:float,y:float,z:float,yaw:float,scale:float=1,packed:bool", "entity:int", "p:parent,p:x,p:y,p:z,p:yaw,p:scale,p:packed,@level", "entity"),
        Reg("an_destroylevelinstance", GenericKind.Exec, typeof(GraphLevelInstances), "DestroyForGraph", "root:int", "success:bool", "p:root", "success"),
        Reg("an_getlevelinstanceroot", GenericKind.Pure, typeof(GraphLevelInstances), "RootOfForGraph", "entity:int", "root:int", "p:entity", "root"),
        Reg("an_findlevelinstanceentity", GenericKind.Pure, typeof(GraphLevelInstances), "FindForGraph", "root:int", "entity:int", "p:root,@path?", "entity"),
        Reg("an_setlevelinstanceloaded", GenericKind.Exec, typeof(GraphLevelInstances), "SetLoadedForGraph", "root:int,loaded:bool=true", "success:bool", "p:root,p:loaded", "success"),
        Reg("an_islevelinstanceloaded", GenericKind.Pure, typeof(GraphLevelInstances), "IsLoadedForGraph", "root:int", "loaded:bool", "p:root", "loaded"),
        Reg("an_reloadlevelinstance", GenericKind.Exec, typeof(GraphLevelInstances), "ReloadForGraph", "root:int", "success:bool", "p:root", "success"),
        Reg("an_setlevelinstancepacked", GenericKind.Exec, typeof(GraphLevelInstances), "SetPackedForGraph", "root:int,packed:bool", "success:bool", "p:root,p:packed", "success"),
        Reg("an_getlevelinstancebounds", GenericKind.Pure, typeof(GraphLevelInstances), "BoundsForGraph", "root:int", "minX:float,minY:float,minZ:float,maxX:float,maxY:float,maxZ:float,success:bool", "p:root,>minX,>minY,>minZ,>maxX,>maxY,>maxZ", "success"),
        Reg("an_getlevelinstancecount", GenericKind.Pure, typeof(GraphLevelInstances), "CountForGraph", "", "count:int", "", "count"),
        Reg("an_getlevelinstanceat", GenericKind.Pure, typeof(GraphLevelInstances), "AtForGraph", "index:int", "root:int", "p:index", "root"),
    };
}
