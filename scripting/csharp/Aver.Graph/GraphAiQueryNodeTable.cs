// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The AI query rows of the game-systems node table (docs/plans/wave3/EQS_SMARTOBJECTS_STATETREE.md section 9):
// environment queries, smart objects and the entity brain (behaviour tree or state tree). EQS and smart-object
// bodies are Aver.Synapse.QueryGraph, brain bodies are BrainGraph (Aver.Framework/Ai/Brain.cs). Query,
// asset, tag-list, event and state names are NODE attributes, never pins. Palette mirror:
// sandbox/src/GraphNodeDefsAiQuery.hpp.

using Aver.Framework;

namespace Aver.Graph;

internal static partial class GameSystemNodes
{
    private static GenericNodeSpec[] AiQueryTable() => new[]
    {
        // ---- environment queries ----
        Reg("an_eqsrun", GenericKind.Exec, typeof(Aver.Synapse.QueryGraph), "EqsRunForGraph", "querier:int,target:int,px:float,py:float,pz:float,hasPoint:bool", "found:bool,count:int,x:float,y:float,z:float,entity:int,score:float", "p:querier,p:target,p:px,p:py,p:pz,p:hasPoint,@query,@usertags?,>count,>x,>y,>z,>entity,>score", "found"),
        Reg("an_eqsstart", GenericKind.Exec, typeof(Aver.Synapse.QueryGraph), "EqsStartForGraph", "querier:int,target:int,px:float,py:float,pz:float,hasPoint:bool,priority:int", "handle:int", "p:querier,p:target,p:px,p:py,p:pz,p:hasPoint,p:priority,@query,@usertags?", "handle"),
        Reg("an_eqsstatus", GenericKind.Pure, typeof(Aver.Synapse.QueryGraph), "EqsStatusForGraph", "handle:int", "status:int,count:int", "p:handle,>count", "status"),
        Reg("an_eqsgetitem", GenericKind.Pure, typeof(Aver.Synapse.QueryGraph), "EqsGetItemForGraph", "handle:int,index:int", "success:bool,x:float,y:float,z:float,entity:int,score:float,obj:int,slot:int", "p:handle,p:index,>x,>y,>z,>entity,>score,>obj,>slot", "success"),
        Reg("an_eqscancel", GenericKind.Exec, typeof(Aver.Synapse.QueryGraph), "EqsCancelForGraph", "handle:int", "success:bool", "p:handle", "success"),
        Reg("an_eqsrelease", GenericKind.Exec, typeof(Aver.Synapse.QueryGraph), "EqsReleaseForGraph", "handle:int", "success:bool", "p:handle", "success"),
        // ---- smart objects ----
        Reg("an_soattach", GenericKind.Exec, typeof(Aver.Synapse.QueryGraph), "SoAttachForGraph", "entity:int", "success:bool", "p:entity,@asset", "success"),
        Reg("an_sofind", GenericKind.Pure, typeof(Aver.Synapse.QueryGraph), "SoFindForGraph", "user:int,x:float,y:float,z:float,radius:float=1500,matchAll:bool", "found:bool,obj:int,slot:int,ex:float,ey:float,ez:float,count:int", "p:user,p:x,p:y,p:z,p:radius,p:matchAll,@slottags?,@usertags?,>obj,>slot,>ex,>ey,>ez,>count", "found"),
        Reg("an_soclaim", GenericKind.Exec, typeof(Aver.Synapse.QueryGraph), "SoClaimForGraph", "user:int,obj:int,slot:int,ttl:float=-1", "claim:int,refusal:int", "p:user,p:obj,p:slot,p:ttl,@usertags?,>refusal", "claim"),
        Reg("an_souse", GenericKind.Exec, typeof(Aver.Synapse.QueryGraph), "SoUseForGraph", "claim:int", "success:bool", "p:claim", "success"),
        Reg("an_sorelease", GenericKind.Exec, typeof(Aver.Synapse.QueryGraph), "SoReleaseForGraph", "claim:int,reason:int", "success:bool", "p:claim,p:reason", "success"),
        Reg("an_sogetclaim", GenericKind.Pure, typeof(Aver.Synapse.QueryGraph), "SoGetClaimForGraph", "user:int,index:int=0", "has:bool,claim:int,obj:int,slot:int,state:int,x:float,y:float,z:float,yaw:float,ex:float,ey:float,ez:float", "p:user,p:index,>claim,>obj,>slot,>state,>x,>y,>z,>yaw,>ex,>ey,>ez", "has"),
        Reg("an_soslotinfo", GenericKind.Pure, typeof(Aver.Synapse.QueryGraph), "SoSlotInfoForGraph", "entity:int,slot:int", "success:bool,state:int,user:int,x:float,y:float,z:float,yaw:float", "p:entity,p:slot,>state,>user,>x,>y,>z,>yaw", "success"),
        Reg("an_soslotcount", GenericKind.Pure, typeof(Aver.Synapse.QueryGraph), "SoSlotCountForGraph", "entity:int", "count:int", "p:entity", "count"),
        Reg("an_sosetslotenabled", GenericKind.Exec, typeof(Aver.Synapse.QueryGraph), "SoSetSlotEnabledForGraph", "entity:int,slot:int,enabled:bool=true", "success:bool", "p:entity,p:slot,p:enabled", "success"),
        // ---- brain: behaviour tree or state tree ----
        Reg("an_brainset", GenericKind.Exec, typeof(BrainGraph), "SetAssetForGraph", "entity:int", "success:bool", "p:entity,@asset", "success"),
        Reg("an_brainsendevent", GenericKind.Exec, typeof(BrainGraph), "SendEventForGraph", "entity:int,payloadI:int,payloadF:float", "success:bool", "p:entity,p:payloadI,p:payloadF,@event", "success"),
        Reg("an_brainisinstate", GenericKind.Pure, typeof(BrainGraph), "IsInStateForGraph", "entity:int", "inState:bool", "p:entity,@state", "inState"),
        Reg("an_brainstatus", GenericKind.Pure, typeof(BrainGraph), "StatusForGraph", "entity:int", "status:int,kind:int", "p:entity,>kind", "status"),
        Reg("an_brainrestart", GenericKind.Exec, typeof(BrainGraph), "RestartForGraph", "entity:int", "success:bool", "p:entity", "success"),
        Reg("an_brainstop", GenericKind.Exec, typeof(BrainGraph), "StopForGraph", "entity:int", "success:bool", "p:entity", "success"),
    };
}
