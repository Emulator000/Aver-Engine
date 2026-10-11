// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The sequencer rows of the game-systems node table (docs/plans/wave5/SEQUENCER_TRACKS.md section 10).
// Every method is in SequenceGraphInterop (Aver.Framework/LevelSequence.cs); sequences are named by a NODE
// attribute, never a pin, and have no entity of their own. Lifecycle and event-track events arrive as
// AN_OnEvent entries, so they need no node. Palette mirror: sandbox/src/GraphNodeDefsSequencer.hpp.

using Aver.Framework;

namespace Aver.Graph;

internal static partial class GameSystemNodes
{
    private static GenericNodeSpec[] SequenceTable() => new[]
    {
        Reg("playsequence", GenericKind.Exec, typeof(SequenceGraphInterop), "PlaySequenceForGraph", "start:float=-1,rate:float=0", "success:bool", "@sequence,p:start,p:rate", "success"),
        Reg("pausesequence", GenericKind.Exec, typeof(SequenceGraphInterop), "PauseSequenceForGraph", "", "success:bool", "@sequence", "success"),
        Reg("stopsequence", GenericKind.Exec, typeof(SequenceGraphInterop), "StopSequenceForGraph", "", "success:bool", "@sequence?", "success"),
        Reg("setsequencetime", GenericKind.Exec, typeof(SequenceGraphInterop), "SetSequenceTimeForGraph", "seconds:float", "success:bool", "@sequence,p:seconds", "success"),
        Reg("setsequencerate", GenericKind.Exec, typeof(SequenceGraphInterop), "SetSequenceRateForGraph", "rate:float", "success:bool", "@sequence,p:rate", "success"),
        Reg("bindsequenceactor", GenericKind.Exec, typeof(SequenceGraphInterop), "BindSequenceActorForGraph", "entity:int", "success:bool", "@sequence,@binding,p:entity", "success"),
        Reg("issequenceplaying", GenericKind.Pure, typeof(SequenceGraphInterop), "IsSequencePlayingForGraph", "", "result:bool", "@sequence?", "result"),
        Reg("getsequencestate", GenericKind.Pure, typeof(SequenceGraphInterop), "GetSequenceStateForGraph", "", "found:bool,time:float,length:float,rate:float", "@sequence,>time,>length,>rate", "found"),
    };
}
