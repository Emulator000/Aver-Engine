// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// A simulated engine for the ability system: a settable clock, entity liveness, class names and an event log.

using Aver.Framework;

record RaisedEvent(string Name, int Sender, int Target, object[] Args)
{
    public float Arg(int i) => Convert.ToSingle(Args[i], System.Globalization.CultureInfo.InvariantCulture);
}

sealed class FakeGasHost : IGasHost
{
    public double Time = 100.0;
    public readonly HashSet<int> Dead = new();
    public readonly Dictionary<int, string> Classes = new();
    public readonly HashSet<string> Listening = new(StringComparer.Ordinal);
    public bool ListenToAll;
    public readonly List<RaisedEvent> Events = new();

    public double Now => Time;
    public bool IsAlive(int entity) => entity != 0 && !Dead.Contains(entity);
    public string? ClassNameOf(int entity) => Classes.TryGetValue(entity, out string? c) ? c : null;
    public int ListenerCount(string eventName) => ListenToAll || Listening.Contains(eventName) ? 1 : 0;
    public void Raise(string eventName, int sender, int target, params object[] args) =>
        Events.Add(new RaisedEvent(eventName, sender, target, args));

    public void Reset()
    {
        Time = 100.0;
        Dead.Clear();
        Classes.Clear();
        Listening.Clear();
        ListenToAll = false;
        Events.Clear();
    }

    public IEnumerable<RaisedEvent> Named(string name) => Events.Where(e => e.Name == name);
}
