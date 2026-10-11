// Aver Engine — Copyright (c) 2026 Hydrogen-Isotope.
// Developed by Vectoric-Core-Systems.
// SPDX-License-Identifier: LGPL-2.1-only. See LICENSE at the repository root.
// Gameplay Ability System: the host entry points the scripting bridge calls, the engine host, the
// replication seam interfaces and the attributes that bind C# code to the system.

using System.Reflection;
using System.Runtime.InteropServices;
using Aver.Scripting;

namespace Aver.Framework;

/// <summary>What the ability system needs from the engine: the game clock, entity liveness, class names and the event bus.
/// Tests install a fake so the whole system runs in a bare process.</summary>
public interface IGasHost
{
    /// <summary>Game time in seconds (pausable and dilatable in the engine).</summary>
    double Now { get; }
    /// <summary>True while the entity exists.</summary>
    bool IsAlive(int entity);
    /// <summary>The actor class name of the entity, or null for a plain entity.</summary>
    string? ClassNameOf(int entity);
    /// <summary>Number of subscribers to an event name.</summary>
    int ListenerCount(string eventName);
    /// <summary>Delivers an event to its subscribers now.</summary>
    void Raise(string eventName, int sender, int target, params object[] args);
}

/// <summary>Receives every authoritative change as plain data, so a networking layer can replicate it. Nothing networked is built.</summary>
public interface IGasReplicationSink
{
    /// <summary>An attribute's Base changed.</summary>
    void AttributeBase(AbilitySystem system, AttributeRef attribute, float newBase);
    /// <summary>An effect instance was added; <paramref name="magnitudes"/> are its resolved modifier magnitudes.</summary>
    void EffectAdded(AbilitySystem system, ActiveEffectInfo info, float[] magnitudes);
    /// <summary>An effect instance changed (stacks or expiry).</summary>
    void EffectChanged(AbilitySystem system, ActiveEffectInfo info, float[] magnitudes);
    /// <summary>An effect instance was removed.</summary>
    void EffectRemoved(AbilitySystem system, ActiveEffectInfo info, float[] magnitudes);
    /// <summary>The loose reference count of a tag changed.</summary>
    void LooseTag(AbilitySystem system, GameplayTag tag, int count);
    /// <summary>An ability was granted.</summary>
    void AbilityGranted(AbilitySystem system, AbilityHandle handle, string name, int level);
    /// <summary>An ability was revoked.</summary>
    void AbilityRevoked(AbilitySystem system, AbilityHandle handle, string name, int level);
    /// <summary>An ability started.</summary>
    void AbilityActivated(AbilitySystem system, AbilityHandle handle);
    /// <summary>An ability ended.</summary>
    void AbilityEnded(AbilitySystem system, AbilityHandle handle, bool cancelled);
}

/// <summary>Receives activation requests made on a non-authoritative system, in place of failing them.</summary>
public interface IGasClientRequests
{
    /// <summary>A client asked to activate an ability.</summary>
    void RequestActivate(AbilitySystem system, AbilityHandle handle, Entity target);
}

/// <summary>Binds a <see cref="GameplayAbility"/> subclass to the name an ability definition's <c>BEHAVIOUR</c> uses.</summary>
[AttributeUsage(AttributeTargets.Class)]
public sealed class AverAbilityAttribute : Attribute
{
    /// <summary>The behaviour name.</summary>
    public string Name { get; }
    /// <summary>Binds the class to <paramref name="name"/>.</summary>
    public AverAbilityAttribute(string name) => Name = name;
}

/// <summary>Class defaults for an actor class's ability system: sets added, abilities granted, effects applied and tags
/// held when an instance begins play (merged with any <c>ACTORDEFAULTS</c> block; this attribute wins on conflicts).
/// <c>Grant</c> entries are <c>"Ability.Name"</c> or <c>"Ability.Name:level"</c>.</summary>
[AttributeUsage(AttributeTargets.Class)]
public sealed class AverAbilitySystemAttribute : Attribute
{
    /// <summary>Attribute sets to add.</summary>
    public string[] AttributeSets { get; set; } = Array.Empty<string>();
    /// <summary>Abilities to grant.</summary>
    public string[] Grant { get; set; } = Array.Empty<string>();
    /// <summary>Effects to apply to the actor itself.</summary>
    public string[] Apply { get; set; } = Array.Empty<string>();
    /// <summary>Loose tags to hold.</summary>
    public string[] Tags { get; set; } = Array.Empty<string>();
}

/// <summary>Marks a static class with <c>static void Define()</c> that registers definitions with the
/// <see cref="GameplayDefs"/> builders. Called once per assembly load and once per Play start.</summary>
[AttributeUsage(AttributeTargets.Class)]
public sealed class AverGameplayDefsAttribute : Attribute { }

internal sealed class EngineGasHost : IGasHost
{
    public double Now => Timers.Time;
    public bool IsAlive(int entity) => entity != 0 && new Entity(entity).IsAlive;

    public string? ClassNameOf(int entity)
    {
        int c = Fw.aver_fw_class_of(entity);
        return c == 0 ? null : new ActorClass(c).Name;
    }

    public int ListenerCount(string eventName) => Events.ListenerCount(eventName);
    public void Raise(string eventName, int sender, int target, params object[] args) => Events.Dispatch(eventName, sender, target, args);
}

/// <summary>The ability system's host entry points. The scripting bridge calls these from its lifecycle hooks; they own
/// the per-frame update, the Play-session reset, class defaults and the managed-state save provider.</summary>
public static class GameplayAbilities
{
    private static IGasHost s_host = new EngineGasHost();
    private static readonly List<Assembly> s_assemblies = new();
    private static readonly Dictionary<string, Type> s_behaviours = new(StringComparer.Ordinal);
    private static readonly List<AbilitySystem> s_scratch = new();
    private static string s_contentRoot = "";
    private static bool s_providerInstalled;
    private static bool s_reloaded;

    // Pinned for the process lifetime: native code holds the function pointers.
    private static readonly StateCountFn s_stateCount = GasSave.StateCount;
    private static readonly StateAtFn s_stateAt = GasSave.StateAt;
    private static readonly StateSetFn s_stateSet = GasSave.StateSet;

    /// <summary>The engine services the system runs on. Assign a fake in tests; null restores the engine host.</summary>
    public static IGasHost Host
    {
        get => s_host;
        set => s_host = value ?? new EngineGasHost();
    }

    /// <summary>The replication sink; null (the default) means nothing is networked.</summary>
    public static IGasReplicationSink? Replication { get; set; }

    /// <summary>Receives activation requests from non-authoritative systems; null means they fail.</summary>
    public static IGasClientRequests? ClientRequests { get; set; }

    /// <summary>Installs the managed-state provider so ability systems ride in saves. Safe to call more than once.</summary>
    public static void Startup()
    {
        if (s_providerInstalled) return;
        try
        {
            FwState.aver_fw_set_state_provider(s_stateCount, s_stateAt, s_stateSet, IntPtr.Zero);
            s_providerInstalled = true;
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException)
        {
            Log.Warn($"[GAS] managed entity state is unavailable; ability systems will not be saved ({ex.GetType().Name})");
        }
    }

    /// <summary>Remembers the project content folder that <see cref="BeginSession"/> scans for <c>*.ocgas</c> files.</summary>
    public static void SetContentRoot(string dir) => s_contentRoot = dir ?? "";

    /// <summary>Starts a Play session: drops every system (their effect and cooldown state belongs to the ended session),
    /// forgets the definitions, reloads them from the content root and re-registers code-defined ones.</summary>
    public static void BeginSession()
    {
        ReloadDefinitions(dropSystems: true);
    }

    private static void ReloadDefinitions(bool dropSystems)
    {
        if (dropSystems)
        {
            GasBatch.Reset();
            AbilitySystem.ClearAll();
        }
        GameplayDefs.Clear();
        s_behaviours.Clear();
        if (s_contentRoot.Length > 0)
        {
            try
            {
                if (!GameplayDefs.LoadDirectory(s_contentRoot, out string error))
                    foreach (string line in error.Split('\n', StringSplitOptions.RemoveEmptyEntries))
                        Log.Error($"[GAS] {line}");
            }
            catch (Exception ex)
            {
                Log.Error($"[GAS] loading gameplay definitions from '{s_contentRoot}' failed: {ex.GetType().Name}: {ex.Message}");
            }
        }
        foreach (Assembly asm in s_assemblies) ScanAssembly(asm);
    }

    /// <summary>Registers an assembly's abilities (<c>[AverAbility]</c>), class defaults (<c>[AverAbilitySystem]</c>) and
    /// <c>[AverGameplayDefs]</c> definitions. Called once per script load.</summary>
    public static void RegisterAssembly(Assembly asm)
    {
        ArgumentNullException.ThrowIfNull(asm);
        if (s_reloaded)
        {
            // A script reload: the previous load's code definitions are stale, so rebuild the registry once.
            s_reloaded = false;
            s_assemblies.Add(asm);
            ReloadDefinitions(dropSystems: false);
            return;
        }
        if (s_assemblies.Contains(asm)) return;
        s_assemblies.Add(asm);
        ScanAssembly(asm);
    }

    private static Type[] TypesOf(Assembly asm)
    {
        try { return asm.GetTypes(); }
        catch (ReflectionTypeLoadException ex) { return ex.Types.Where(t => t != null).ToArray()!; }
    }

    private static void ScanAssembly(Assembly asm)
    {
        foreach (Type t in TypesOf(asm))
        {
            try
            {
                if (!t.IsClass) continue;
                var ab = t.GetCustomAttribute<AverAbilityAttribute>(inherit: false);
                if (ab != null)
                {
                    if (!typeof(GameplayAbility).IsAssignableFrom(t) || t.IsAbstract || t.GetConstructor(Type.EmptyTypes) == null)
                        Log.Warn($"[GAS] [AverAbility(\"{ab.Name}\")] on {t.FullName}: it must be a concrete GameplayAbility with a parameterless constructor");
                    else if (!s_behaviours.TryAdd(ab.Name, t))
                        Log.Warn($"[GAS] ability behaviour '{ab.Name}' is declared twice ({s_behaviours[ab.Name].FullName} and {t.FullName}); keeping the first");
                }
                if (t.GetCustomAttribute<AverGameplayDefsAttribute>(inherit: false) != null)
                {
                    MethodInfo? m = t.GetMethod("Define", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static, null, Type.EmptyTypes, null);
                    if (m == null) Log.Warn($"[GAS] {t.FullName} is marked [AverGameplayDefs] but has no static Define()");
                    else m.Invoke(null, null);
                }
                var sys = t.GetCustomAttribute<AverAbilitySystemAttribute>(inherit: false);
                if (sys != null) MergeClassDefaults(ClassNames.Of(t), sys);
            }
            catch (Exception ex)
            {
                Exception inner = ex is TargetInvocationException tie && tie.InnerException != null ? tie.InnerException : ex;
                Log.Error($"[GAS] registering {t.FullName} failed: {inner.GetType().Name}: {inner.Message}");
            }
        }
    }

    private static void MergeClassDefaults(string className, AverAbilitySystemAttribute a)
    {
        ActorDefaultsDef d = GameplayDefs.FindActorDefaults(className) ?? new ActorDefaultsDef { ClassName = className };
        foreach (string s in a.AttributeSets) if (!d.AttributeSets.Contains(s)) d.AttributeSets.Add(s);
        foreach (string g in a.Grant)
        {
            string name = g;
            int level = 1;
            int colon = g.IndexOf(':');
            if (colon > 0)
            {
                name = g.Substring(0, colon);
                if (!int.TryParse(g.AsSpan(colon + 1), out level) || level < 1) level = 1;
            }
            d.Grants.RemoveAll(x => x.Key == name);
            d.Grants.Add(new KeyValuePair<string, int>(name, level));
        }
        foreach (string e in a.Apply) if (!d.Applies.Contains(e)) d.Applies.Add(e);
        foreach (string t in a.Tags) if (!d.Tags.Contains(t)) d.Tags.Add(t);
        GameplayDefs.SetActorDefaults(d);
    }

    internal static GameplayAbility? CreateBehaviour(AbilityDef def, AbilitySystem owner, AbilityHandle handle, int level)
    {
        if (def.Behaviour == null) return null;
        if (!s_behaviours.TryGetValue(def.Behaviour, out Type? t))
        {
            AbilitySystem.LogOnce("nobehaviour:" + def.Behaviour,
                $"[GAS] ability '{def.Name}' names behaviour '{def.Behaviour}', which no [AverAbility] class provides; it runs graph-only");
            return null;
        }
        try
        {
            var b = (GameplayAbility)Activator.CreateInstance(t)!;
            b.Bind(owner, def, handle, level);
            return b;
        }
        catch (Exception ex)
        {
            Log.Error($"[GAS] creating behaviour '{def.Behaviour}' failed: {ex.GetType().Name}: {ex.Message}");
            return null;
        }
    }

    /// <summary>An actor began play: gives it its class defaults, unless it already has a system (created in code, or restored from a save).</summary>
    public static void ActorBeginPlay(int entity)
    {
        if (entity == 0 || AbilitySystem.Of(new Entity(entity)) != null) return;
        string? cls = Host.ClassNameOf(entity);
        if (cls == null) return;
        ActorDefaultsDef? d = GameplayDefs.FindActorDefaults(cls);
        if (d == null) return;
        var self = new Entity(entity);
        AbilitySystem sys = AbilitySystem.GetOrAdd(self);
        foreach (string set in d.AttributeSets) sys.AddAttributeSet(set);
        foreach (KeyValuePair<string, float> o in d.BaseOverrides)
            if (AttributeRef.TryFind(o.Key, out AttributeRef a)) sys.SetBase(a, o.Value);
        foreach (string t in d.Tags)
        {
            GameplayTag tag = GameplayTag.Request(t);
            if (tag.IsValid) sys.AddLooseTag(tag);
        }
        foreach (KeyValuePair<string, int> g in d.Grants) sys.Grant(g.Key, g.Value);
        foreach (string e in d.Applies) sys.Apply(EffectSpec.Of(e).From(self));
    }

    /// <summary>The entity is going away: its running abilities end and its system is dropped without notifications.</summary>
    public static void EntityGone(int entity)
    {
        AbilitySystem? s = AbilitySystem.Of(new Entity(entity));
        if (s == null) return;
        s.Teardown();
        AbilitySystem.RemoveByHandle(entity);
    }

    /// <summary>Once per frame at tick group 0: runs effect expiry, periodic executions and ability auto-ends that fell due, and
    /// drops systems whose entity no longer exists.</summary>
    public static void Update()
    {
        IReadOnlyList<AbilitySystem> all = AbilitySystem.All;
        if (all.Count == 0) return;
        double now = Host.Now;
        s_scratch.Clear();
        for (int i = 0; i < all.Count; ++i) s_scratch.Add(all[i]);
        foreach (AbilitySystem s in s_scratch)
        {
            if (s.IsDead) continue;
            if (!Host.IsAlive(s.OwnerHandle))
            {
                s.Teardown();
                AbilitySystem.RemoveByHandle(s.OwnerHandle);
                continue;
            }
            try { s.ProcessDue(now); }
            catch (Exception ex) { Log.Error($"[GAS] update of entity {s.OwnerHandle} threw: {ex.GetType().Name}: {ex.Message}"); }
        }
        s_scratch.Clear();
    }

    /// <summary>Script unload: drops every user callback, behaviour instance and remembered assembly so the load context can unload.
    /// System data (attributes, effects, tags) is kept; the next <see cref="RegisterAssembly"/> rebuilds the definitions.</summary>
    public static void Shutdown()
    {
        foreach (AbilitySystem s in AbilitySystem.All) s.DropCallbacks();
        s_assemblies.Clear();
        s_behaviours.Clear();
        s_scratch.Clear();
        Replication = null;
        ClientRequests = null;
        s_reloaded = true;
    }
}
