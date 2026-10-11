// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// The managed AI seam without the engine: ManagedAi.Collect over attributed methods, the tag csv the entity
// conveniences send native code, the enum values that mirror the C headers, and the graph bodies' argument
// guards. A call that would reach native code in this bare process hits a missing library; the graph bodies
// must absorb that and answer false / 0 rather than throw.

using Aver.Framework;
using Aver.Synapse;

/// <summary>Leaves the test scans. Public static, attributed, in the test assembly.</summary>
public static class AiSampleLeaves
{
    [AverAiAction("TestAct")] public static AiStatus Act(in AiTaskContext c) => AiStatus.Success;
    [AverAiCondition("TestCond")] public static bool Cond(in AiTaskContext c) => c.P0 > 0f;
    [AverEqsTest("TestEqs")] public static float Eqs(in EqsTestContext c) => c.X * 2f;

    [AverAiAction("BadReturn")] public static int WrongReturn(in AiTaskContext c) => 0;
    [AverAiAction("BadParam")] public static AiStatus ByValue(AiTaskContext c) => AiStatus.Running;
    [AverEqsTest("BadContext")] public static float WrongContext(in AiTaskContext c) => 0f;

    public static AiStatus NotAttributed(in AiTaskContext c) => AiStatus.Success;
}

static class AiManagedTests
{
    public static int RunAll() => T.Suite(() =>
    {
        T.Run("ManagedAi.Collect finds attributed methods and reports bad signatures", Collect);
        T.Run("ManagedAi: nothing is registered without native code", NoNativeNoRegistration);
        T.Run("entity tag csv", TagCsv);
        T.Run("enums mirror the C headers", EnumValues);
        T.Run("graph bodies: argument guards and no throwing", GraphGuards);
    });

    private static void Collect()
    {
        var found = ManagedAi.Collect(typeof(AiSampleLeaves).Assembly).Where(l => l.Method.DeclaringType == typeof(AiSampleLeaves)).ToList();
        T.Check("six attributed methods are reported", found.Count == 6, $"{found.Count}");
        ManagedLeaf act = found.First(l => l.Name == "TestAct");
        T.Check("TestAct is a good Action", act.Kind == ManagedKind.Action && act.Error == null);
        T.Check("TestCond is a good Condition", found.First(l => l.Name == "TestCond") is { Kind: ManagedKind.Condition, Error: null });
        T.Check("TestEqs is a good EqsTest", found.First(l => l.Name == "TestEqs") is { Kind: ManagedKind.EqsTest, Error: null });
        T.Check("a wrong return type is reported", found.First(l => l.Name == "BadReturn").Error is { } e1 && e1.Contains("AiStatus"));
        T.Check("a by-value context is reported", found.First(l => l.Name == "BadParam").Error is { } e2 && e2.Contains("in AiTaskContext"));
        T.Check("the wrong context type for a test is reported", found.First(l => l.Name == "BadContext").Error is { } e3 && e3.Contains("EqsTestContext"));
        T.Check("an unattributed method is ignored", found.All(l => l.Name != "NotAttributed"));

        // The good ones bind to their delegate types.
        var del = (AiActionFn)Delegate.CreateDelegate(typeof(AiActionFn), act.Method);
        var ctx = new AiTaskContext { Entity = 3, Text = "" };
        T.Check("the collected method binds as an AiActionFn and runs", del(in ctx) == AiStatus.Success);
        var cond = (AiConditionFn)Delegate.CreateDelegate(typeof(AiConditionFn), found.First(l => l.Name == "TestCond").Method);
        var ctx2 = new AiTaskContext { P0 = 1f, Text = "" };
        T.Check("...a condition too", cond(in ctx2));
        var test = (EqsTestFn)Delegate.CreateDelegate(typeof(EqsTestFn), found.First(l => l.Name == "TestEqs").Method);
        var tctx = new EqsTestContext { X = 21f, Text = "" };
        T.Check("...and an EQS test", T.Near(test(in tctx), 42f));
    }

    private static void NoNativeNoRegistration()
    {
        var errors = new List<string>();
        var previous = ManagedAi.LogError;
        ManagedAi.LogError = errors.Add;
        try
        {
            bool registered = ManagedAi.RegisterAction("NoNativeAct", (in AiTaskContext c) => AiStatus.Success);
            T.Check("RegisterAction answers false (and logs) when the native library is missing", !registered && errors.Count >= 1);
            T.Check("an empty name or a null delegate is refused", !ManagedAi.RegisterCondition("", (in AiTaskContext c) => true) && !ManagedAi.RegisterEqsTest("X", null!));
        }
        finally { ManagedAi.LogError = previous; }
    }

    private static void TagCsv()
    {
        T.Reset();
        T.Check("null container: null", EntityAiTags.Csv(null) == null);
        T.Check("empty container: null", EntityAiTags.Csv(new GameplayTagContainer()) == null);
        var c = new GameplayTagContainer(T.Tag("Aism.User.Humanoid"), T.Tag("Aism.User.Adult"));
        string? csv = EntityAiTags.Csv(c);
        T.Check("tags are joined by commas", csv != null && csv.Split(',').Length == 2 && csv.Contains("Aism.User.Humanoid") && csv.Contains("Aism.User.Adult"), csv ?? "null");
    }

    private static void EnumValues()
    {
        T.Check("EqsStatus matches AVER_SYN_EQS_*", (int)EqsStatus.Unknown == 0 && (int)EqsStatus.Queued == 1 && (int)EqsStatus.Running == 2 && (int)EqsStatus.Finished == 3 && (int)EqsStatus.Failed == 4 && (int)EqsStatus.Cancelled == 5);
        T.Check("SlotState matches AVER_SYN_SO_*", (int)SlotState.Free == 0 && (int)SlotState.Claimed == 1 && (int)SlotState.Occupied == 2 && (int)SlotState.Cooldown == 3 && (int)SlotState.Disabled == 4);
        T.Check("ReleaseReason matches AVER_SYN_SO_REL_*", (int)ReleaseReason.Released == 0 && (int)ReleaseReason.Completed == 1 && (int)ReleaseReason.Failed == 2 && (int)ReleaseReason.Expired == 3 && (int)ReleaseReason.UserGone == 4 && (int)ReleaseReason.ObjectGone == 5);
        T.Check("ClaimRefusal matches AVER_SYN_SO_REFUSE_*", (int)ClaimRefusal.None == 0 && (int)ClaimRefusal.NoSuchSlot == 1 && (int)ClaimRefusal.Disabled == 2 && (int)ClaimRefusal.NotFree == 3 && (int)ClaimRefusal.Tags == 4 && (int)ClaimRefusal.UserLimit == 5 && (int)ClaimRefusal.NoUser == 6);
        T.Check("FindFlags matches AVER_SYN_SO_FIND_*", (int)FindFlags.MatchAll == 1 && (int)FindFlags.IncludeClaimed == 2);
        T.Check("ManagedKind matches AVER_SYN_MANAGED_*", (int)ManagedKind.Condition == 0 && (int)ManagedKind.Action == 1 && (int)ManagedKind.Abort == 2 && (int)ManagedKind.EqsTest == 3);
        T.Check("BrainKind matches AVER_FW_BRAIN_*", (int)BrainKind.None == 0 && (int)BrainKind.BehaviourTree == 1 && (int)BrainKind.StateTree == 2);
        T.Check("BrainStatus matches AVER_FW_BRAIN_*", (int)BrainStatus.Idle == 0 && (int)BrainStatus.Running == 1 && (int)BrainStatus.Succeeded == 2 && (int)BrainStatus.Failed == 3);
        T.Check("AiStatus is Running 0, Success 1, Failure 2", (int)AiStatus.Running == 0 && (int)AiStatus.Success == 1 && (int)AiStatus.Failure == 2);
    }

    private static void GraphGuards()
    {
        bool found = QueryGraph.EqsRunForGraph(1, 0, 0, 0, 0, false, "", "", out int count, out float x, out float y, out float z, out int entity, out float score);
        T.Check("EqsRun with no query: false and zeros", !found && count == 0 && x == 0f && y == 0f && z == 0f && entity == 0 && score == 0f);
        T.Check("EqsRun with a query but no native library: false, no throw", !QueryGraph.EqsRunForGraph(1, 0, 0, 0, 0, false, "AI/q.oceqs", "", out _, out _, out _, out _, out _, out _));
        T.Check("EqsStart with no query: 0", QueryGraph.EqsStartForGraph(1, 0, 0, 0, 0, false, 0, "", "") == 0);
        T.Check("EqsStart with a query but no native library: 0, no throw", QueryGraph.EqsStartForGraph(1, 0, 0, 0, 0, false, 0, "AI/q.oceqs", "") == 0);
        T.Check("EqsStatus of handle 0: 0 and no count", QueryGraph.EqsStatusForGraph(0, out int n) == 0 && n == 0);
        T.Check("EqsStatus of a real handle without native code: 0, no throw", QueryGraph.EqsStatusForGraph(7, out _) == 0);
        T.Check("EqsGetItem of handle 0: false", !QueryGraph.EqsGetItemForGraph(0, 0, out _, out _, out _, out _, out _, out _, out int slot) && slot == -1);
        T.Check("EqsCancel / EqsRelease of handle 0: false", !QueryGraph.EqsCancelForGraph(0) && !QueryGraph.EqsReleaseForGraph(0));
        T.Check("SoAttach with no asset: false", !QueryGraph.SoAttachForGraph(3, ""));
        T.Check("SoClaim without native code: 0, refusal None, no throw", QueryGraph.SoClaimForGraph(1, 2, 0, -1f, "", out int refusal) == 0 && refusal == 0);
        T.Check("SoUse / SoRelease of claim 0: false", !QueryGraph.SoUseForGraph(0) && !QueryGraph.SoReleaseForGraph(0, 0));
        T.Check("SoGetClaim with a bad index: false", !QueryGraph.SoGetClaimForGraph(1, 9, out _, out _, out _, out _, out _, out _, out _, out _, out _, out _, out _));
        T.Check("SoFind / SoSlotInfo without native code: false, no throw",
            !QueryGraph.SoFindForGraph(1, 0, 0, 0, 100, false, "", "", out _, out _, out _, out _, out _, out int found2) && found2 == 0 &&
            !QueryGraph.SoSlotInfoForGraph(1, 0, out _, out _, out _, out _, out _, out _));
        T.Check("SoSlotCount without native code: -1", QueryGraph.SoSlotCountForGraph(1) == -1);
        T.Check("SoSetSlotEnabled without native code: false", !QueryGraph.SoSetSlotEnabledForGraph(1, 0, true));
    }
}
