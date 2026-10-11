// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The IK and rig-input rows of the game-systems node table (docs/plans/wave5/IK_RETARGETING.md section 9).
// Every method is in IkGraphInterop (Aver.Framework/Ik.cs); rig, goal, foot and input names are NODE
// attributes, never pins, and an empty one means "all" where the design says so. The older SetControlRig
// node is untouched. Palette mirror: sandbox/src/GraphNodeDefsIk.hpp.

using Aver.Framework;

namespace Aver.Graph;

internal static partial class GameSystemNodes
{
    private static GenericNodeSpec[] IkTable() => new[]
    {
        Reg("setikrig", GenericKind.Exec, typeof(IkGraphInterop), "SetIkRigForGraph", "entity:int,weight:float=1", "success:bool", "p:entity,@rig,p:weight", "success"),
        Reg("setikgoal", GenericKind.Exec, typeof(IkGraphInterop), "SetIkGoalForGraph", "entity:int,x:float,y:float,z:float,useRotation:bool,pitch:float,yaw:float,roll:float,space:int", "success:bool", "p:entity,@goal,p:x,p:y,p:z,p:useRotation,p:pitch,p:yaw,p:roll,p:space", "success"),
        Reg("bindikgoal", GenericKind.Exec, typeof(IkGraphInterop), "BindIkGoalForGraph", "entity:int,target:int,ox:float,oy:float,oz:float", "success:bool", "p:entity,@goal,p:target,@socket?,p:ox,p:oy,p:oz", "success"),
        Reg("setikgoalweight", GenericKind.Exec, typeof(IkGraphInterop), "SetIkGoalWeightForGraph", "entity:int,weight:float,blendSeconds:float=0.15", "success:bool", "p:entity,@goal,p:weight,p:blendSeconds", "success"),
        Reg("clearikgoal", GenericKind.Exec, typeof(IkGraphInterop), "ClearIkGoalForGraph", "entity:int", "success:bool", "p:entity,@goal?", "success"),
        Reg("getikgoalstate", GenericKind.Pure, typeof(IkGraphInterop), "GetIkGoalStateForGraph", "entity:int", "found:bool,alpha:float,errorCm:float,reached:bool", "p:entity,@goal,>alpha,>errorCm,>reached", "found"),
        Reg("setfootikweight", GenericKind.Exec, typeof(IkGraphInterop), "SetFootIkWeightForGraph", "entity:int,weight:float,blendSeconds:float=0.2", "success:bool", "p:entity,@foot?,p:weight,p:blendSeconds", "success"),
        Reg("getfootikstate", GenericKind.Pure, typeof(IkGraphInterop), "GetFootIkStateForGraph", "entity:int", "found:bool,offsetCm:float,plant:float,nx:float,ny:float,nz:float", "p:entity,@foot?,>offsetCm,>plant,>nx,>ny,>nz", "found"),
        Reg("setriginputfloat", GenericKind.Exec, typeof(IkGraphInterop), "SetRigInputFloatForGraph", "entity:int,value:float", "success:bool", "p:entity,@input,p:value", "success"),
        Reg("setriginputvec3", GenericKind.Exec, typeof(IkGraphInterop), "SetRigInputVec3ForGraph", "entity:int,x:float,y:float,z:float", "success:bool", "p:entity,@input,p:x,p:y,p:z", "success"),
        Reg("clearriginput", GenericKind.Exec, typeof(IkGraphInterop), "ClearRigInputForGraph", "entity:int", "success:bool", "p:entity,@input?", "success"),
    };
}
