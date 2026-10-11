// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// C# ability behaviours the tests bind by name.

using Aver.Framework;

[AverAbility("RecBeh")]
sealed class RecordingBehaviour : GameplayAbility
{
    public static readonly List<string> Trail = new();
    public override void OnGranted() => Trail.Add("granted");
    public override void OnRevoked() => Trail.Add("revoked");
    public override bool CanActivate() { Trail.Add("can"); return true; }
    public override void OnActivate() => Trail.Add("activate");
    public override void OnEnd(bool cancelled) => Trail.Add("end:" + cancelled);
}

[AverAbility("RefuseBeh")]
sealed class RefusingBehaviour : GameplayAbility
{
    public override bool CanActivate() => false;
}
