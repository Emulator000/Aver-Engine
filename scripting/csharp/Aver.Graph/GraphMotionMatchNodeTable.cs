// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The motion-matching rows of the game-systems node table (docs/plans/wave5/MOTION_MATCHING.md section 11).
// Every method is in MotionMatchGraphInterop (Aver.Framework/MotionMatching.cs); the database, tag lists and
// clip path are NODE attributes. A motion-matching state needs no node (it is a state in the .ocasm).
// Palette mirror: sandbox/src/GraphNodeDefsMotionMatch.hpp.

using Aver.Framework;

namespace Aver.Graph;

internal static partial class GameSystemNodes
{
    private static GenericNodeSpec[] MotionMatchTable() => new[]
    {
        Reg("setmotionmatching", GenericKind.Exec, typeof(MotionMatchGraphInterop), "SetMotionMatchingForGraph", "entity:int,facing:int=0,drive:int=0,playRate:float=1", "success:bool", "p:entity,@database?,p:facing,p:drive,p:playRate", "success"),
        Reg("setmotioninput", GenericKind.Exec, typeof(MotionMatchGraphInterop), "SetMotionInputForGraph", "entity:int,velocityX:float,velocityY:float,facingYaw:float=0", "success:bool", "p:entity,p:velocityX,p:velocityY,p:facingYaw", "success"),
        Reg("setmotiontags", GenericKind.Exec, typeof(MotionMatchGraphInterop), "SetMotionTagsForGraph", "entity:int", "success:bool", "p:entity,@required?,@excluded?", "success"),
        Reg("requestmotionsearch", GenericKind.Exec, typeof(MotionMatchGraphInterop), "RequestMotionSearchForGraph", "entity:int", "success:bool", "p:entity", "success"),
        Reg("getmotionstate", GenericKind.Pure, typeof(MotionMatchGraphInterop), "GetMotionStateForGraph", "entity:int", "found:bool,clipTime:float,cost:float,switches:int", "p:entity,>clipTime,>cost,>switches", "found"),
        Reg("ismotionclip", GenericKind.Pure, typeof(MotionMatchGraphInterop), "IsMotionClipForGraph", "entity:int", "result:bool", "p:entity,@clip", "result"),
    };
}
