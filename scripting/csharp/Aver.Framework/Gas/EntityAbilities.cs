// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: the Entity accessors.

namespace Aver.Framework;

public readonly partial struct Entity
{
    /// <summary>This entity's gameplay ability system, or null if it has none (see <see cref="AbilitySystem"/>).</summary>
    public AbilitySystem? Abilities => AbilitySystem.Of(this);

    /// <summary>This entity's gameplay ability system, created if it has none.</summary>
    public AbilitySystem GetOrAddAbilities() => AbilitySystem.GetOrAdd(this);
}
