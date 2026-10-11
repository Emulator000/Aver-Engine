// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Test runner for the Gameplay Ability System (Aver.Framework/Gas). Returns the failure count.

using Aver.Framework;

static class Program
{
    static int Main()
    {
        // Abilities named by [AverAbility] in this assembly (TestBehaviours.cs) bind by name.
        GameplayAbilities.RegisterAssembly(typeof(Program).Assembly);

        int failures = 0;
        failures += GameplayTagTests.RunAll();
        failures += AttributeTests.RunAll();
        failures += EffectModifierTests.RunAll();
        failures += EffectDurationTests.RunAll();
        failures += EffectPeriodicTests.RunAll();
        failures += EffectStackingTests.RunAll();
        failures += EffectInhibitionTests.RunAll();
        failures += AbilityTests.RunAll();
        failures += GasEventTests.RunAll();
        failures += GameplayDefsParserTests.RunAll();
        failures += GasSaveTests.RunAll();
        failures += GasReplicationSeamTests.RunAll();

        Console.WriteLine(failures == 0 ? "\nAll tests passed." : $"\n{failures} test(s) failed.");
        return failures;
    }
}
