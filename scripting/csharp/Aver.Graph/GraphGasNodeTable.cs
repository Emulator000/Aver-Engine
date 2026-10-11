// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The gameplay-ability rows of the game-systems node table (docs/plans/wave2/GAS.md section 3): tags,
// attributes, effects and abilities. Every method is in GasGraph (Aver.Framework/Gas/GasGraph.cs);
// an id pin beats the node's name attribute when both are given. Palette mirror:
// sandbox/src/GraphNodeDefsGas.hpp.

using Aver.Framework;

namespace Aver.Graph;

internal static partial class GameSystemNodes
{
    private static GenericNodeSpec[] GasTable() => new[]
    {
        Reg("an_gastagid", GenericKind.Pure, typeof(GasGraph), "TagIdForGraph", "", "tagId:int", "@tag", "tagId"),
        Reg("an_gasattributeid", GenericKind.Pure, typeof(GasGraph), "AttributeIdForGraph", "", "attrId:int", "@attribute", "attrId"),
        Reg("an_gaseffectid", GenericKind.Pure, typeof(GasGraph), "EffectIdForGraph", "", "effectId:int", "@effect", "effectId"),
        Reg("an_gasabilityid", GenericKind.Pure, typeof(GasGraph), "AbilityIdForGraph", "", "abilityId:int", "@ability", "abilityId"),
        Reg("an_gashastag", GenericKind.Pure, typeof(GasGraph), "HasTagForGraph", "entity:int,tagId:int", "has:bool", "p:entity,p:tagId,@tag?", "has"),
        Reg("an_gashastags", GenericKind.Pure, typeof(GasGraph), "HasTagsForGraph", "entity:int,all:bool", "has:bool", "p:entity,p:all,@tags", "has"),
        Reg("an_gastagcount", GenericKind.Pure, typeof(GasGraph), "TagCountForGraph", "entity:int,tagId:int", "count:int", "p:entity,p:tagId,@tag?", "count"),
        Reg("an_gasaddtag", GenericKind.Exec, typeof(GasGraph), "AddTagForGraph", "entity:int,tagId:int,count:int=1", "success:bool", "p:entity,p:tagId,@tag?,p:count", "success"),
        Reg("an_gasremovetag", GenericKind.Exec, typeof(GasGraph), "RemoveTagForGraph", "entity:int,tagId:int,count:int=1", "success:bool", "p:entity,p:tagId,@tag?,p:count", "success"),
        Reg("an_gasgetattribute", GenericKind.Pure, typeof(GasGraph), "GetAttributeForGraph", "entity:int,attrId:int", "value:float,base:float,success:bool", "p:entity,p:attrId,@attribute?,>value,>base", "success"),
        Reg("an_gassetattribute", GenericKind.Exec, typeof(GasGraph), "SetAttributeBaseForGraph", "entity:int,attrId:int,value:float", "success:bool", "p:entity,p:attrId,@attribute?,p:value", "success"),
        Reg("an_gasaddattributeset", GenericKind.Exec, typeof(GasGraph), "AddAttributeSetForGraph", "entity:int", "success:bool", "p:entity,@set", "success"),
        Reg("an_gasapplyeffect", GenericKind.Exec, typeof(GasGraph), "ApplyEffectForGraph", "target:int,source:int,effectId:int,callerValue:float", "handle:int,applied:bool", "p:target,p:source,p:effectId,@effect?,@callertag?,p:callerValue,>applied", "handle"),
        Reg("an_gasremoveeffect", GenericKind.Exec, typeof(GasGraph), "RemoveEffectForGraph", "entity:int,handle:int", "success:bool", "p:entity,p:handle", "success"),
        Reg("an_gasremoveeffectsbytag", GenericKind.Exec, typeof(GasGraph), "RemoveEffectsByTagForGraph", "entity:int,tagId:int", "count:int", "p:entity,p:tagId,@tag?", "count"),
        Reg("an_gasremoveeffectsbydef", GenericKind.Exec, typeof(GasGraph), "RemoveEffectsByDefForGraph", "entity:int,effectId:int", "count:int", "p:entity,p:effectId,@effect?", "count"),
        Reg("an_gaseffectinfo", GenericKind.Pure, typeof(GasGraph), "EffectInfoForGraph", "entity:int,effectId:int", "active:bool,stacks:int,remaining:float,handle:int", "p:entity,p:effectId,@effect?,>stacks,>remaining,>handle", "active"),
        Reg("an_gasgrantability", GenericKind.Exec, typeof(GasGraph), "GrantAbilityForGraph", "entity:int,abilityId:int,level:int=1", "handle:int", "p:entity,p:abilityId,@ability?,p:level", "handle"),
        Reg("an_gasrevokeability", GenericKind.Exec, typeof(GasGraph), "RevokeAbilityForGraph", "entity:int,abilityId:int", "success:bool", "p:entity,p:abilityId,@ability?", "success"),
        Reg("an_gastryactivate", GenericKind.Exec, typeof(GasGraph), "TryActivateForGraph", "entity:int,abilityId:int,target:int", "success:bool,reason:int", "p:entity,p:abilityId,@ability?,p:target,>reason", "success"),
        Reg("an_gastryactivatebytag", GenericKind.Exec, typeof(GasGraph), "TryActivateByTagForGraph", "entity:int,tagId:int,target:int", "success:bool,abilityId:int,reason:int", "p:entity,p:tagId,@tag?,p:target,>abilityId,>reason", "success"),
        Reg("an_gascanactivate", GenericKind.Pure, typeof(GasGraph), "CanActivateForGraph", "entity:int,abilityId:int", "can:bool,reason:int", "p:entity,p:abilityId,@ability?,>reason", "can"),
        Reg("an_gascommitability", GenericKind.Exec, typeof(GasGraph), "CommitAbilityForGraph", "entity:int,abilityId:int", "success:bool", "p:entity,p:abilityId,@ability?", "success"),
        Reg("an_gasendability", GenericKind.Exec, typeof(GasGraph), "EndAbilityForGraph", "entity:int,abilityId:int,cancelled:bool", "success:bool", "p:entity,p:abilityId,@ability?,p:cancelled", "success"),
        Reg("an_gascancelbytag", GenericKind.Exec, typeof(GasGraph), "CancelAbilitiesByTagForGraph", "entity:int,tagId:int", "count:int", "p:entity,p:tagId,@tag?", "count"),
        Reg("an_gasisabilityactive", GenericKind.Pure, typeof(GasGraph), "IsAbilityActiveForGraph", "entity:int,abilityId:int", "active:bool", "p:entity,p:abilityId,@ability?", "active"),
        Reg("an_gascooldown", GenericKind.Pure, typeof(GasGraph), "AbilityCooldownForGraph", "entity:int,abilityId:int", "remaining:float,onCooldown:bool", "p:entity,p:abilityId,@ability?,>onCooldown", "remaining"),
        Reg("an_gassendevent", GenericKind.Exec, typeof(GasGraph), "SendGameplayEventForGraph", "entity:int,instigator:int,tagId:int,magnitude:float", "triggered:int", "p:entity,p:instigator,p:tagId,@tag?,p:magnitude", "triggered"),
    };
}
