# Gameplay Ability System

Hierarchical gameplay tags, attribute sets, gameplay effects and abilities, as an `AbilitySystem` on any entity. Written in C# in `Aver.Framework` (`scripting/csharp/Aver.Framework/Gas/`); definitions are data (`.ocgas`) or code; graphs reach it through the Abilities node family and hear it through bus events. Design contract: `docs/plans/wave2/GAS.md`.

Not built: replication (a seam only, below), GameplayCues beyond one bus event, ability input binding (use Enhanced Input), level curves (the data-table feature), live-tracking attribute-based magnitudes.

## Quick start

1. Add a definitions file: Content browser, Add, **New Gameplay Definitions** (writes a commented, valid `.ocgas`). Any `*.ocgas` under the project Content folder is loaded at every Play start, so an edit applies on the next Play. Parse and link errors are logged as `[GAS] Content/Combat.ocgas:14: ...`; a bad file never stops Play.
2. Give an actor class an ability system: `ACTORDEFAULTS <Class>` in the file, or `[AverAbilitySystem(AttributeSets = new[] { "Character" }, Grant = new[] { "Ability.Fireball" })]` on a C# actor class. Works for graph classes too (the name is the class name).
3. Use it from a graph (nodes below), or from C#:

```csharp
AbilitySystem hero = player.GetOrAddAbilities();
hero.AddAttributeSet("Character");
hero.Grant("Ability.Fireball");
var health = AttributeRef.Of("Health");
hero.AttributeChanged += c => Log.Info($"{c.Attribute.Name}: {c.OldValue} -> {c.NewValue}");
if (hero.TryActivate(hero.Find("Ability.Fireball"), target) != AbilityFailure.None) { /* cooldown, cost, tag ... */ }
enemy.GetOrAddAbilities().Apply(EffectSpec.Of("Effect.Poison").From(player).SetByCaller(GameplayTag.Request("Data.Damage"), 12f));
```

Code can define the same things the file does: `GameplayDefs.Effect("Effect.Heal").Instant().Modify("Health", ModOp.Add, 25f).Register()`; also `.Ability(...)` and `.AttributeSet(...)`. Put such calls in a static class marked `[AverGameplayDefs]` with `static void Define()`; it runs at every script load and Play start. A C# ability derives `GameplayAbility` and is bound by `[AverAbility("Name")]` plus `BEHAVIOUR Name` on the definition (one instance per granted ability per system; `OnGranted`, `CanActivate`, `OnActivate`, `OnEnd`, `OnRevoked`; call `Commit()` / `End()`).

## Tags

`GameplayTag` is a hierarchical, interned name (`State.Stunned.Hard`): 1 to 8 segments of `[A-Za-z][A-Za-z0-9_]*`, 128 characters, 4096 tags. A tag matches itself and every ancestor, so holding `State.Stunned.Hard` satisfies a query for `State.Stunned`. A `GameplayTagContainer` is ref-counted (two effects granting one tag keep it until both are gone). Ids follow registration order and are stable for a given project until its definitions change, so **persistent data uses names**; graph variables holding tag ids are not safe across edits.

This is separate from the 32-bit `CTags` mask on entities (flat scene ABI used by `Game.WithTag` and the tag nodes). Use the mask for cheap spatial and physics flags, gameplay tags for game rules.

## Attributes

An attribute has **Base** (permanent) and **Current** (Base plus active modifiers), both clamped to `[min, max]`, where either bound may be another attribute's Current (`max=MaxHealth`). Shrinking a bound re-clamps the dependent's Base down; growing it never raises it. Attribute names are unique per entity. Evaluation: `v = Base + sum(Add * stacks)`, then `v *= product(1 + (m - 1) * stacks)` over Multiply modifiers, then the most recently applied Override replaces `v`, then clamp. A **meta** attribute is never stored: an instant Add to `Damage` with `forward=Health scale=-1` becomes an instant `-N` on Health and `Damage` reads 0 again; with no `forward` it raises `AttributeChanged` (old 0, new N) for code to handle. `PreAttributeChange` adjusts a new Current before it is stored (custom clamps).

Notifications queue while a mutating call runs and fire in order when the outermost call ends, so a handler never sees a half-applied effect. A handler that mutates starts a fresh batch; nesting past 8 drops further notifications with one warning.

## Effects

Instant effects change **Base**; duration and infinite effects add modifiers to the aggregators (Current only) and unwind on removal or expiry. A periodic effect executes its modifiers as instant operations on Base every `PERIOD` seconds (`apply` also executes once at application; a long frame executes at most 8 periods, the surplus is dropped; they never enter the aggregator). Magnitudes: a constant, `caller=<tag>` (supplied in the spec, a missing one reads 0 and warns once), or `from=source.X` / `from=target.X` (read at application and **snapshotted**).

Application checks `REQUIRES` / `BLOCKEDBY` against the target's tags (`EffectFailure.TagRequirement`), then `REMOVES` cleanses matching active effects. **Stacking:** `none` (independent instances), `target N` (one instance per effect, stacks up to N, then `StackLimit`, still refreshing if `refresh`), `source N` (one per effect and source). `refresh` resets the expiry on every stacking application, `keep` does not; `expire clear` drops all stacks together, `expire one` drops one and restarts the duration. A stack multiplies Add and scales Multiply; periodic executions apply once per stack. **Inhibition:** while `ONGOING_REQUIRES` is unmet or `ONGOING_BLOCKEDBY` is present, an effect's modifiers and granted tags are off and its periodic executions skipped, but its clock keeps running. Immunity is just a granted tag in someone's `BLOCKEDBY`.

## Abilities

`TryActivate(handle, target)` checks, in order: granted, not already active (unless `RETRIGGER on`), `REQUIRES`, `BLOCKEDBY`, a running ability's `BLOCKS`, cooldown (the owner holds none of the cooldown effect's granted tags), cost (every cost modifier leaves its attribute's Base within its minimum), the C# behaviour's `CanActivate`. Failures are `AbilityFailure` (None 0, NoSystem 1, NotGranted 2, AlreadyActive 3, MissingRequiredTag 4, BlockedByTag 5, BlockedByAbility 6, OnCooldown 7, CannotAfford 8, BehaviourRefused 9, NoAuthority 10) and also raise `GAS.AbilityFailed`. On success it cancels abilities matching `CANCELS`, adds `OWNS` tags and `BLOCKS` counts, commits (`COMMIT activate`, the default: cost then cooldown as one batch) or leaves it to `Commit()` (`manual`), raises `AbilityActivated` and auto-ends after `DURATION`. A **cooldown is an effect**; the remaining time is the longest remaining duration of an active effect granting one of its tags. `SendGameplayEvent(tag, ...)` activates every granted ability with a matching `TRIGGER event`; `TRIGGER granted` makes a passive ability that activates when granted.

**Graph-authored abilities** need no C#: activation raises `GAS.AbilityActivated.<AbilityName>` on the owner; an `OnEvent` entry of that name is the ability body, ending it with the End Ability node. There are no latent ability tasks in this graph runtime (use a timer plus an `OnEvent` continuation); that is a deliberate difference from Unreal.

## Events

Every event is raised on the bus under a generic name and a specific one (`<generic>.<name>`), each only when something listens. `sender` is the instigator (the source), `target` the owner, so a HUD listening to the player's health subscribes with `owner: player.Handle`.

| Generic | Specific suffix | Args |
|---|---|---|
| `GAS.AbilityActivated` | ability name | abilityId, level |
| `GAS.AbilityEnded` | ability name | abilityId, cancelled (0/1) |
| `GAS.AbilityFailed` | ability name | abilityId, reason (`AbilityFailure`) |
| `GAS.AttributeChanged` | attribute name | attrId, newValue, oldValue |
| `GAS.TagChanged` | tag name | tagId, present (0/1), count |
| `GAS.EffectApplied` / `GAS.EffectRemoved` | effect name | effectId, handle, stacks |
| `GAS.GameplayEvent` | tag name | tagId, magnitude, target entity |
| `GAS.Cue` | cue tag name | tagId, phase (0 removed, 1 added, 2 executed) |

C# equivalents on every system: `AttributeChanged`, `TagChanged`, `EffectApplied`, `EffectRemoved`, `AbilityActivated`, `AbilityEnded`.

## Graph nodes (family "Abilities")

Graph strings are node **attributes**, never pins, so each node takes its tag, attribute, effect or ability as an attribute, or as an `int` id pin that overrides it (the `AN_Gas*Id` nodes turn a name into an id). `entity` pins are the owner; 0 fails. Failure never throws; a node with neither id nor attribute fails and logs once.

| Node | Kind | Inputs | Outputs | Attribute |
|---|---|---|---|---|
| `AN_GasTagId` / `AN_GasAttributeId` / `AN_GasEffectId` / `AN_GasAbilityId` | Pure | none | id (0 if no such definition) | `tag` / `attribute` / `effect` / `ability` |
| `AN_GasHasTag` / `AN_GasTagCount` | Pure | entity, tagId | has / count | `tag?` |
| `AN_GasHasTags` | Pure | entity, all | has | `tags` (comma list) |
| `AN_GasAddTag` / `AN_GasRemoveTag` | Exec | entity, tagId, count=1 | success | `tag?` |
| `AN_GasGetAttribute` | Pure | entity, attrId | value, base, success | `attribute?` |
| `AN_GasSetAttribute` | Exec | entity, attrId, value | success | `attribute?` |
| `AN_GasAddAttributeSet` | Exec | entity | success | `set` |
| `AN_GasApplyEffect` | Exec | target, source (0 = target), effectId, callerValue | handle, applied | `effect?`, `callertag?` |
| `AN_GasRemoveEffect` | Exec | entity, handle | success | none |
| `AN_GasRemoveEffectsByTag` / `...ByDef` | Exec | entity, tagId / effectId | count | `tag?` / `effect?` |
| `AN_GasEffectInfo` | Pure | entity, effectId | active, stacks, remaining (1e30 = infinite), handle | `effect?` |
| `AN_GasGrantAbility` | Exec | entity, abilityId, level=1 | handle (0 = failed) | `ability?` |
| `AN_GasRevokeAbility` | Exec | entity, abilityId | success | `ability?` |
| `AN_GasTryActivate` | Exec | entity, abilityId, target | success, reason | `ability?` |
| `AN_GasTryActivateByTag` | Exec | entity, tagId, target | success, abilityId, reason | `tag?` |
| `AN_GasCanActivate` | Pure | entity, abilityId | can, reason | `ability?` |
| `AN_GasCommitAbility` / `AN_GasEndAbility` | Exec | entity, abilityId (, cancelled) | success | `ability?` |
| `AN_GasCancelByTag` | Exec | entity, tagId | count | `tag?` |
| `AN_GasIsAbilityActive` | Pure | entity, abilityId | active | `ability?` |
| `AN_GasCooldown` | Pure | entity, abilityId | remaining, onCooldown | `ability?` |
| `AN_GasSendEvent` | Exec | entity, instigator, tagId, magnitude | triggered | `tag?` |

The C# methods behind them are the `...ForGraph` entry points in `Gas/GasGraph.cs`. A graph hears the system with the existing `OnEvent` entry: `ENTRY n GAS.AttributeChanged.Health` (payload index 0 = id, 1 = new, 2 = old), `GAS.TagChanged.State.Stunned`, `GAS.GameplayEvent.Event.Hit`, `GAS.AbilityActivated.Ability.Fireball`. An effect needing several SetByCaller values is C#-only (the node carries one).

## Save and load

A system saves as the managed-state channel `gas` (the `$State` pseudo-component of an entity; see ABI.md, "Managed entity state"). Payload, text, invariant culture, `\n` separated:

```
GASSTATE 1
SET Character
ATTR Health 73.5                 # Base values only; Current is recomputed
LOOSE State.Alive 1              # loose tag counts (granted tags come back with their effects)
GRANT Ability.Fireball 2         # name, level
EFFECT Effect.Poison stacks=2 remaining=3.25 period=0.5 mags=-3,1 source=self
EFFECT Effect.Regen stacks=1 remaining=inf mags=2 source=none
```

`remaining` is seconds left, `period` seconds until the next execution, `mags` the resolved magnitudes (so SetByCaller and snapshotted values reload exactly), `source` is `self` when the owner was the source and `none` otherwise (entity handles do not survive a file). Restoring creates the system, re-creates the effects on the game clock, recomputes, and raises **no** notifications; unknown definitions or attributes are skipped with one warning each ("save is from a build with different definitions"); a bad header refuses the whole payload. A restored system is complete before begin play, so class defaults are not applied again.

**Not saved:** active ability runs (an ability mid-flight ends at save; its cost and cooldown, being effects, are saved; a passive ability is granted again but does not re-run), pending notifications, handles (renumbered), behaviour state. A behaviour needing persistence uses a graph variable or its own state channel.

## Wiring and lifecycle

The scripting bridge makes eight one-line calls into `GameplayAbilities`: `Startup` (installs the save provider), `RegisterAssembly` (per script load), `SetContentRoot` (in `DeclareGraphClasses`), `BeginSession` (first statement of `ConfigureInput`, the one call both composition roots already make at every Play start; if that call is renamed or split, move `BeginSession` with it), `ActorBeginPlay` (class defaults; skipped when a system exists), `EntityGone` (`DispUnbind`), `Update` (once per frame at tick group 0, on the pausable game clock `Timers.Time`) and `Shutdown` (script unload: drops every user callback and behaviour so the collectible context can unload; data is kept). `BeginSession` clears all systems and definitions and reloads from the content root. A project with no `.ocgas` and no GAS code pays one directory scan per Play start and an empty update loop.

## Replication seam

Nothing networked is built. `AbilitySystem.HasAuthority` (default true): when false, every mutating call returns its failure value (`EffectFailure.NoAuthority`, `AbilityFailure.NoAuthority`, `false`) and reads still work. `GameplayAbilities.Replication` (an `IGasReplicationSink`) hears each authoritative change as plain data; `GameplayAbilities.ClientRequests` (an `IGasClientRequests`) receives an activation made on a non-authoritative system instead of it failing. Handles are per-system monotonic ints; effect specs plus resolved magnitudes plus the `.ocgas` names are the wire and save vocabulary.

## Limits

256 active effects, 64 granted abilities and 128 loose tag entries per system (over-limit applications fail and log once); 4096 tags; 1024 attribute names; 16 modifiers per effect; 8 tags per `.ocgas` line; 8 periodic executions per effect per update; notification nesting 8; save payload 1 MiB.

## Tests

- Managed core: `dotnet run --project scripting/csharp/Aver.Framework.Tests` (also ctest `GasManagedTests` when a .NET SDK is on PATH). Runs against a fake `IGasHost`, so no engine DLL is needed.
- Native plumbing: `SaveWorldTest` (the `$State` capture and restore against a fake provider).
- Real CLR: `GasPersistTest` (the bridge installed the provider and loaded the `.ocgas`; capture to file to restore carries the state).
- Graph nodes: `scripting/csharp/Aver.Graph.Tests/GasNodeTests.cs` (the node agent's).
