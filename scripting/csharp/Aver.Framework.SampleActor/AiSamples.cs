// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Managed AI leaves for tests/synapse_query's ManagedAiClrTest: the bridge scans every script assembly for
// [AverAiAction] / [AverAiCondition] / [AverEqsTest] methods (ManagedAi.ScanAssembly), so these become
// leaves a .ocst task or an .oceqs custom test can name.

using Aver.Synapse;

namespace Aver.Framework.SampleActor;

public static class AiSamples
{
    /// <summary>Running until 15 ms have passed since its first tick, then Success.</summary>
    [AverAiAction("SampleAiStep")]
    public static AiStatus Step(in AiTaskContext c) => c.Elapsed >= 0.015f ? AiStatus.Success : AiStatus.Running;

    [AverAiCondition("SampleAiAlways")]
    public static bool Always(in AiTaskContext c) => true;

    /// <summary>The item's height above the world origin plane.</summary>
    [AverEqsTest("SampleAiHeight")]
    public static float Height(in EqsTestContext c) => c.Z;
}
