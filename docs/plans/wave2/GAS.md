# Wave 2, feature #18: Gameplay Ability System (design contract)

Status: DESIGN, frozen for the implementing agent. 2026-10-11. Scope: gameplay tags, attribute sets, gameplay effects, abilities, an `AbilitySystem` component on entities, graph nodes, save/load. Not in scope: replication (a seam is left, section 2.13), GameplayCues beyond one bus event, ability input binding (Enhanced Input sibling feature), level curves (data-table sibling feature).

## 0. Decisions in one screen

1. **GAS is C#, in `Aver.Framework`** (new folder `scripting/csharp/Aver.Framework/Gas/`). No new module, no new third-party code. Nothing in C++ needs to call it, and graphs reach C# the way they already do (`internal static` `...ForGraph` methods called by reflection from the table-driven emitter, see `BlackboardGraph`, `Blackboard.cs:140`).
2. **One native addition**: a generic "managed entity state" seam so a save can carry state that lives in C# (`framework_state_abi.h`, 5 entry points, own version), plus three function pointers on `aver::save::Host`. GAS is its first user. Everything else GAS needs (timers, events, class lookup, entity liveness) already exists.
3. **Time and events reuse the existing services**: game clock `Timers.Time` (pausable, dilatable, `Timers.cs:183`) and the event bus `Events` (`Events.cs`). No parallel clock, no parallel delegate system. Graphs hear GAS through the existing `AN_OnEvent` node.
4. **Definitions are data** in a new text format `.ocgas` (tags, attribute sets, effects, abilities, per-class defaults), parsed by C# (`OcGraphParser.cs` precedent), auto-discovered under the project Content folder. Code can define the same things with builders, and a C# class can supply an ability's behaviour.
5. **Hierarchical `GameplayTag` is separate from the existing 32-bit `CTags` mask** (`EntityScene.cs:159-179`, `GraphInterop.cs:430-470`). The mask is scene ABI and flat; a hierarchical, ref-counted, unbounded tag set cannot live in 32 bits. The mask stays for cheap spatial/physics flags. Open question 1.
6. **Graph strings are NODE attributes, never pins** (`PinType` is float/int/bool; `GraphGameSystemNodes.cs:54`). So every GAS node takes its tag/attribute/effect/ability either as an attribute (`tag=State.Stunned`) or, for dynamic use, as an `int` id pin that overrides it. Id nodes (`AN_GasTagId` etc.) turn a name into an id.

## 1. What exists today, and what is missing

Searched the whole tree (excluding `.claude/worktrees`, `third_party`): no `GameplayTag`, `AbilitySystem`, `GameplayEffect` or `AttributeSet` anywhere. Everything below is a building block, not a GAS.

| Need | Exists | Missing |
|---|---|---|
| Entity flags | `Entity.Tags` is a `uint` over `CTags.bits`; `AddTag/RemoveTag/HasTag` (`EntityScene.cs:160-179`); tag graph nodes pass the mask as an `int` (`GraphInterop.cs:430-470`); `Game.WithTag(mask)` (`Game.cs:239`) | Hierarchy, names, counts (several effects granting one tag), more than 32 tags |
| Clock | `Timers.Time` double seconds, pause + dilation; `Timers.Set/Clear/ClearOwner`; native service cleared when play ends (`docs/GAME_TIMERS_EVENTS.md`, "Session end") | Nothing; GAS reads `Timers.Time` |
| Events | Typed bus: `Events.Dispatch(name, sender, target, args)`; a dispatch with `target != 0` reaches only subscribers whose `owner == target` (`EventBus.hpp:139`); `AN_OnEvent` binds a graph ENTRY to a bus event name with `owner = entity` (`GraphTimerEvents.cs:63-78`); `AN_EventPayload` reads args by index as int/float/bool (`GraphTimerEvents.cs:49`) | GAS event names and payloads |
| Per-entity typed values | `Blackboard` (AI, string-keyed, native) | Not reusable: no modifiers, clamping, change events |
| Actor class defaults | `[AverClass]` + `static void Configure(ClassBuilder b)` (`ClassBuilder.cs`), graph classes declared by `DeclareGraphClasses(contentDir)` (`HostBridge.cs:651`) | Per-class ability-system defaults |
| Per-frame managed hook | `DispTickAll(group)` runs once per frame per group; `if (group == 0) Aver.UI.UiSystem.Pump()` is the precedent for a managed per-frame pump (`HostBridge.cs:1463-1470`). Order each frame: `aver_fw_timers_update`, `aver_fw_events_flush`, then groups (`GameTick.hpp:147-153`) | A GAS update call at the same place |
| Lifecycle hooks | `DispBeginPlay` (1454), `DispEndPlay` (1495), `DispUnbind` (1547, already calls `GraphTimerEvents.UnbindEntity` at 1564), `DrainAndUnload` (271, already calls `Timers.Shutdown(); Events.Shutdown();`), `TryLoadAssembly` -> `DeclareActors` (861-905) | Calls into GAS from each |
| Per-Play-start hook | `ConfigureInput` is called by both composition roots at every Play start, after `DeclareGraphClasses` (`GameApp.cpp:1163-1174`, `SandboxPlay.cpp:292-304`, `HostBridge.cs:401`) | A GAS session reset there (section 2.12) |
| Save | `aver::save::capture/restore` capture components by reflection plus a synthetic `$GraphVars` component through `Host` function pointers (`SaveWorld.hpp:44-58`, `SaveWorld.cpp:42-86, 436-452`); `OcSaveField` has a `STRING` kind (`OcSave.hpp:49`); restore sets state BEFORE begin play (`SaveWorld.cpp:457-479, 510-515`); the framework relays providers (`framework_abi.h:160-172, 295-330`, `FrameworkAbi.cpp:715-745`); both roots build a `save::Host` (`GameApp.cpp:168-185`, `SandboxApp.hpp:371-394`); `Game.SaveGame/LoadGame` + nodes (`Game.cs:142,155`, `GraphInterop.cs:543`) | A way for C#-held state to ride in the save |
| Graph nodes | Table-driven generic emitter: rows in `GraphGameSystemNodeTable.cs`, `GameSystemNodes.Validate()` checks rows against real methods, palette mirror `sandbox/src/GraphNodeDefsGameSystems.hpp` | GAS rows |

## 2. Design

### 2.1 Where each piece lives

| Piece | Language | Why |
|---|---|---|
| Tags, attributes, effects, abilities, `AbilitySystem`, `.ocgas` parser, save blob | C# (`Aver.Framework`) | Only managed code consumes them; a native copy would need an ABI of ~60 entry points to be reachable from C# and graphs. |
| Graph entry points | C# `GasGraph` (internal static, in `Aver.Framework`) + table rows in `Aver.Graph` | Same split as `BlackboardGraph`; `Aver.Graph` already has `InternalsVisibleTo` (`Aver.Framework.csproj`). |
| Managed-state save seam | C ABI in `modules/framework`, three pointers in `modules/save` | Save is C++ and cannot see managed memory. |
| Editor | C++ (`sandbox`) | Authoring conveniences only (2.14). |

The core is host-abstracted: `AbilitySystem` talks to the engine through `IGasHost` (clock, liveness, class name, event raise). The default `EngineGasHost` uses `Timers.Time`, `Entity.IsAlive`, `Fw.aver_fw_class_of`, `Events`. Tests install a fake, so the whole system is testable in a bare process (the engine DLLs are not present there; see `GameSystemNodeTests.cs` header).

### 2.2 Gameplay tags

- Name grammar: segments `[A-Za-z][A-Za-z0-9_]*` joined by `.`, max 8 segments, max 128 chars. Case-sensitive. `State.Stunned` has parent `State`.
- Interned, ids sequential from 1; requesting `A.B.C` first interns `A`, `A.B`. Limit 4096. Id 0 is none.
- **Id stability.** Ids depend on registration order. Defs are loaded files-sorted-ordinal, tags interned in order of appearance (declared `TAG` lines first within a file), so a given project gives the same ids every run until its defs change. Persistent data (saves, VARs) must use names; graph VARs holding ids are not safe across def edits. The save blob uses names only.
- `Matches(other)`: equal to or a descendant of `other`. `MatchesExact`: same id.
- `GameplayTagContainer`: ref-counted. `Add(tag, n)` / `Remove(tag, n)` adjust the exact count (floor 0). `Has(t)` is hierarchical: true if any exact tag in the container `Matches(t)` (so holding `State.Stunned.Hard` satisfies `State.Stunned`). Implementation keeps an exact-count map and an implied-count map (each add increments the tag and every ancestor). `Add/Remove` report presence flips (0 to non-zero or back) for the tag and each ancestor; `AbilitySystem` turns each flip into a `TagChanged` notification.

### 2.3 Attributes

- `AttributeSetDef` = named group of `AttributeDef`: `Name`, `DefaultBase`, `Min`, `Max` (floats, default -inf/+inf), optional `MinFrom`/`MaxFrom` (another attribute's Current as the bound), `IsMeta`, `ForwardTo` + `ForwardScale`.
- Attribute names are global per system: two sets on one entity defining the same name is refused at `AddAttributeSet` (logged, returns false). `AttributeRef.Of(name)` interns (limit 1024).
- Each entity attribute has **Base** (permanent) and **Current** (Base plus active duration/infinite modifiers), both clamped to `[min, max]` where `MaxFrom` is read at evaluation time. When a bounding attribute shrinks, a dependent's Base is re-clamped down (never raised when the bound grows). Cyclic `MinFrom/MaxFrom` chains are refused at definition link.
- **Evaluation** (exact): `v = Base`; `v += sum(Add_i * stacks_i)`; `v *= product(1 + (m_j - 1) * stacks_j)` over Multiply modifiers; if any Override modifier is active, `v = value of the most recently applied Override`; clamp. Instant and periodic effects change **Base** instead: Add `Base += m`, Multiply `Base *= m`, Override `Base = m`, then clamp.
- **Meta attributes** (`meta` flag): never stored. An instant Add of `m` to a meta attribute with `forward=Health scale=-1` is converted at once into an instant Add of `m * scale` on Health (the classic Damage -> Health pattern with the logic in data), and the meta attribute reads 0 again. Without `forward`, the meta attribute still raises `AttributeChanged` (old 0, new m) so C# code can handle it, then resets to 0.
- **Change events.** Any change in Current (> 1e-6) queues an `AttributeChange(System, Attribute, Old, New, Instigator)`. `PreAttributeChange` delegates (per system) may adjust the new value before it is stored (custom clamps).
- **Batching.** Every public mutating call is a batch. Notifications (C# events and bus events) queue while the batch runs and fire in order when the outermost batch ends, so a handler never observes a half-applied effect. A handler that mutates starts a fresh batch; nesting beyond 8 drops further notifications with one logged warning (same guard shape as `GraphEvents.MaxDepth`).

### 2.4 Gameplay effects

`EffectDef` (immutable after link): `Policy` (Instant/Duration/Infinite), `Duration`, `Period`, `ExecuteOnApply`, `Modifiers[]`, tag sets (`AssetTags`, `GrantedTags`, `AppRequired`, `AppBlocked`, `OngoingRequired`, `OngoingBlocked`, `RemovesWithTags`), stacking, optional `Cue` tag.

**Modifier magnitude kinds**: `Constant`; `SetByCaller` (value supplied in the spec under a tag, times `scale`; missing key reads 0 and logs once); `AttributeBased` (`source.X` or `target.X` Current value times `scale` plus `add`, **snapshotted at application**; live tracking is not in v1, open question 3). `MagnitudeKind.Table` is reserved for the data-table sibling's curves.

**Apply flow** (`AbilitySystem.Apply(EffectSpec)` on the TARGET system; authoritative):
1. No such def, dead target, or `HasAuthority == false`: fail (`EffectFailure`).
2. `AppRequired` must all be satisfied by the target's tags, `AppBlocked` must all be absent (hierarchical): else `TagRequirement`.
3. `RemovesWithTags`: remove active effects on the target whose `AssetTags` or `GrantedTags` match any (cleanse).
4. **Instant**: resolve magnitudes, apply each modifier to Base in order, fire notifications. `GrantedTags` on an instant def is a link-time warning (ignored).
5. **Duration/Infinite**: stacking lookup (below). New instance: store resolved magnitudes (`mags[]`), `start`, `expiresAt = start + duration` (or none), `nextPeriod = start + period`, add `GrantedTags` to the target's counted tags, add Add/Multiply/Override modifiers to the attribute aggregators (unless inhibited), recompute affected attributes.
6. If `Period > 0` and `ExecuteOnApply`, run one periodic execution immediately.
7. Notifications: `EffectApplied`, attribute and tag changes, cue.

**Periodic**: every `Period` seconds the modifiers execute as **instant** operations on Base (they do not enter the aggregator). A long frame executes once per elapsed period, at most 8 per update per effect (same cap as `Timers`), the surplus dropped. Period does not reset on stack refresh.

**Stacking** (`STACK none | target N | source N`):
- `none` (default): every application is an independent instance.
- `target N`: one instance per def on the target; reapplying increments `stacks` up to `N`; at `N` the application is refused (`StackLimit`) but still refreshes the duration if `refresh`.
- `source N`: one instance per (def, source entity).
- `refresh` (default) resets `expiresAt` on every stacking application; `keep` does not.
- `expire clear` (default): when the duration ends all stacks go. `expire one`: one stack is removed and the duration restarts until stacks reach 0.
- A stack multiplies Add magnitudes by `stacks` and scales Multiply as `1 + (m-1)*stacks`; Override ignores stacks. Periodic executions apply once per stack.

**Ongoing requirements (inhibition).** While `OngoingRequired` is unsatisfied or `OngoingBlocked` is present, the instance is *inhibited*: its modifiers leave the aggregators, its `GrantedTags` are removed, periodic executions are skipped; its duration clock keeps running. Re-evaluated whenever the target's tag set changes (the same batch).

**Immunity** is expressed with tags exactly as in Unreal: an immunity effect grants `Immune.Fire`; damage effects list it in `BLOCKEDBY`.

**Removal**: `Remove(handle)`, `RemoveByTag(tag)` (asset or granted tag matches), `RemoveByDef(def)`. Expiry, removal and `EndPlay` all unwind modifiers, granted tags and recompute. `EffectRemoved` fires for each (not for entity teardown).

### 2.5 Abilities

`AbilityDef` fields: `Name`, `AbilityTags`, `CancelsTags`, `BlocksTags`, `OwnedTags` (granted to the owner while active), `RequiredTags`, `BlockedTags`, `Cost` (an instant effect), `Cooldown` (a duration effect), `Commit` (`Activate` default | `Manual`), `Duration` (auto-end seconds, 0 = until `End`), `Triggers` (gameplay-event tags; `granted` = passive, activates when granted), `Retrigger` (default false), `Behaviour` (name of a registered C# `GameplayAbility`).

**Activation** `TryActivate(handle, target)` returns `AbilityFailure` (None=0, NoSystem=1, NotGranted=2, AlreadyActive=3, MissingRequiredTag=4, BlockedByTag=5, BlockedByAbility=6, OnCooldown=7, CannotAfford=8, BehaviourRefused=9, NoAuthority=10). Checks in this order: granted; not active (or `Retrigger`: end the old run first); owner has all `RequiredTags`; owner has none of `BlockedTags`; no active ability's `BlocksTags` matches this ability's `AbilityTags` (kept as a counted container on the system); **cooldown**: owner has none of the `Cooldown` effect's `GrantedTags`; **cost**: for every modifier of the `Cost` effect, Base after the modifier stays within the attribute's min (Unreal's default `CheckCost`); behaviour `CanActivate()`.

On success: cancel active abilities whose tags match `CancelsTags`; add `OwnedTags` and `BlocksTags` counts; mark active; if `Commit == Activate`, `Commit()`; raise `AbilityActivated`; call behaviour `OnActivate()`; schedule auto-end.

**Commit** applies the cost effect then the cooldown effect to the owner as a single batch; re-checks cost first and fails without applying anything if it can no longer be paid. **Cooldown is an effect**: its `GrantedTags` are the cooldown tags; remaining time = longest remaining duration of an active effect granting one.

**End** `End(handle, cancelled)`: removes `OwnedTags`/`BlocksTags` counts, raises `AbilityEnded(cancelled)`, calls behaviour `OnEnd(cancelled)`. Revoking a granted ability or `EndPlay` cancels it first.

**Gameplay events**: `SendGameplayEvent(tag, instigator, magnitude, target)` raises `GameplayEvent` and calls `TryActivate` on every granted ability with a matching `Triggers` tag (hierarchical match). Returns how many activated.

**Graph-authored abilities** need no C#: the activation raises `GAS.AbilityActivated.<AbilityName>` on the owner; a graph with an `AN_OnEvent` ENTRY of that name is the ability body, ending it with `AN_GasEndAbility`. Long-running bodies use `AN_SetTimer` + `AN_OnEvent` for continuations (there is no latent node model in this graph runtime, so there are no ability tasks; this is a stated difference from Unreal).

**C# abilities** derive `GameplayAbility` (2.9) and are bound by name; one behaviour instance per granted ability per system (stateful).

### 2.6 `AbilitySystem` (the component)

- A managed object per entity, found through `Entity.Abilities` (null if none) or `AbilitySystem.GetOrAdd(entity)`. Stored in `Dictionary<int, AbilitySystem>` plus a `List<AbilitySystem>` in creation order (deterministic iteration).
- Lifetime: created on demand, or at begin play from class defaults (below). Removed by `DispUnbind` hook (`EntityGone`), and swept in `Update()` when `host.IsAlive(entity)` is false (plain entities have no unbind callback).
- **Class defaults**: `ACTORDEFAULTS <Class>` in `.ocgas` or `[AverAbilitySystem(AttributeSets = ..., Grant = ..., Apply = ..., Tags = ...)]` on an actor class (merged; the attribute wins on conflicts). Applied by `ActorBeginPlay(entity)` only when the entity has no system yet; a system restored from a save (2.11) is complete and is not re-defaulted. Works for graph-declared classes too (name from `Fw.aver_fw_class_of`).
- **Update** (`GameplayAbilities.Update()`, once per frame at tick group 0): `now = host.Now`; per system, process due items in time order (effect expiry, periodic executions, ability auto-end) from a per-system min-heap keyed `(due, sequence)`; sweep dead entities. Cost is O(systems with due work). No native timers are used, so a `Timers.ClearAll()` cannot freeze GAS and play-end clears are handled by `BeginSession`.
- **Limits**: 256 active effects, 64 granted abilities, 128 loose tag entries per system; over-limit applications fail and log once.
- `Describe()` returns a multi-line string (attributes, tags with counts, effects with remaining/stacks, abilities with state) for logs and the future inspector.

### 2.7 Events raised (all through `Events`)

`sender` = instigator entity (source), `target` = owner entity. Each event is raised under a generic name and a specific one (`<generic>.<name>`), each only if `Events.ListenerCount(name) > 0`. Payload args in order:

| Generic name | Specific suffix | args |
|---|---|---|
| `GAS.AbilityActivated` | ability name | abilityId, level |
| `GAS.AbilityEnded` | ability name | abilityId, cancelled (0/1) |
| `GAS.AbilityFailed` | ability name | abilityId, reason (`AbilityFailure`) |
| `GAS.AttributeChanged` | attribute name | attrId, newValue, oldValue |
| `GAS.TagChanged` | tag name | tagId, present (0/1), count |
| `GAS.EffectApplied` | effect name | effectId, handle, stacks |
| `GAS.EffectRemoved` | effect name | effectId, handle, stacks |
| `GAS.GameplayEvent` | tag name | tagId, magnitude, target entity |
| `GAS.Cue` | cue tag name | tagId, phase (0 removed, 1 added, 2 executed) |

Because a targeted event reaches only subscribers with `owner == target`, a HUD listening for the player's attributes subscribes with `owner: player.Handle`. For a direct C# alternative every system also exposes plain C# events (`AttributeChanged`, `TagChanged`, `EffectApplied`, `EffectRemoved`, `AbilityActivated`, `AbilityEnded`).

### 2.8 `.ocgas` format (text, UTF-8)

New section 11d in `docs/formats/FORMAT_SPECS.md` (append-only). Rules like `.oclanes`/`.ocparticle`: first non-comment line is the header `OCGAS 1`; `#` starts a comment (whole line or trailing); CRLF and blank lines fine; records are case-insensitive keywords, one per line, whitespace separated; names are case-sensitive. A **block header** (`ATTRSET`, `EFFECT`, `ABILITY`, `ACTORDEFAULTS`) opens a block; the indented (indent is cosmetic) sub-records that follow belong to it until the next block header or a top-level `TAG`. A record in the wrong block, an unknown record, a bad number, a duplicate name, or a reference to an undefined name is **refused with a line-specific message** (`file:line: ...`); a failed file contributes nothing. Forward references across blocks and across files are fine; linking happens once after all files are parsed.

```
OCGAS 1
TAG State.Stunned                     # declare a tag (and parents); optional, requesting an undeclared one also works

ATTRSET Character
  ATTR Health 100 min=0 max=MaxHealth # ATTR <name> <base> [min=<num|Attr>] [max=<num|Attr>]
  ATTR MaxHealth 100 min=1
  ATTR Mana 50 min=0 max=MaxMana
  ATTR MaxMana 50 min=0
  ATTR Damage 0 meta forward=Health scale=-1   # meta [forward=<Attr> scale=<f>]

EFFECT Effect.Poison
  POLICY duration 5                   # instant | duration <seconds> | infinite
  PERIOD 1 apply                      # seconds; "apply" also executes once at application
  MOD Health add -3                   # MOD <attr> add|multiply|override <constant>
  MOD MoveSpeed multiply 0.5
  MOD Health add caller=Data.Damage scale=-1       # SetByCaller
  MOD Health add from=source.Strength scale=-0.5 add=1   # AttributeBased: from=source.<Attr>|target.<Attr>
  ASSET Effect.Damage.Poison          # tags that describe this effect (matched by REMOVES)
  GRANTS State.Poisoned               # tags held while active (space-separated list)
  REQUIRES State.Alive                # application: target has all
  BLOCKEDBY Immune.Poison             # application: target has none
  ONGOING_REQUIRES State.Alive        # while active (else inhibited)
  ONGOING_BLOCKEDBY State.Dead
  REMOVES Effect.Poison.Weak          # on application, remove active effects with these asset/granted tags
  STACK target 3 refresh expire clear # none | target N | source N, then refresh|keep, then expire clear|one
  CUE Cue.Poison                      # optional GAS.Cue tag

ABILITY Ability.Fireball
  TAGS Ability.Cast.Fire              # own tags
  CANCELS Ability.Cast.Ice            # cancel matching active abilities on activation
  BLOCKS Ability.Cast                 # while active, matching abilities cannot activate
  OWNS State.Casting                  # tags the owner holds while active
  REQUIRES State.Alive
  BLOCKEDBY State.Stunned
  COST Effect.FireballCost            # instant effect
  COOLDOWN Effect.FireballCooldown    # duration effect granting the cooldown tag
  COMMIT activate                     # activate (default) | manual
  DURATION 2.5                        # auto-end after N seconds; omit = until End
  TRIGGER event Event.Hit             # activate on a gameplay event (repeatable)
  TRIGGER granted                     # passive: activate when granted
  RETRIGGER off                       # on | off (default off)
  BEHAVIOUR FireballBehaviour         # optional C# GameplayAbility bound by [AverAbility] name

ACTORDEFAULTS Hero                    # actor class (C# or graph class) name
  ATTRSET Character
  ATTR Health 150                     # per-class override of a set's base
  GRANT Ability.Fireball 1            # GRANT <ability> [level]
  APPLY Effect.Regen                  # applied to self at begin play
  ADDTAG State.Alive
```

Numbers are invariant-culture decimal floats (no NaN/inf). Effect `PERIOD > 0` requires `duration` or `infinite`. `STACK` and `MOD` limits: <= 16 modifiers per effect, <= 8 tags per list line. Link-time validation also refuses: modifier on an attribute defined in no `ATTRSET`; `COST` not instant; `COOLDOWN` not duration/infinite or granting no tag (warning for the latter); `BEHAVIOUR` naming no registered class (warning, the ability still runs graph-only); `MaxFrom/MinFrom` cycles.

**Discovery.** `*.ocgas` anywhere under the project content root, recursive, files sorted ordinal, loaded as one set. The content root is remembered from `DeclareGraphClasses(contentDir)` and the set is (re)loaded by `BeginSession()` at every Play start, so an edit applies on the next Play (the editor-input-scheme behaviour). No new project key and no `OcProject` change; `stage-game.ps1` already ships the content folder. Open question 2.

### 2.9 C# API

All in namespace `Aver.Framework`, folder `scripting/csharp/Aver.Framework/Gas/`. Public XML docs on every member (`GenerateDocumentationFile` is on). Additive public surface; no change to existing types except `Entity` gaining a partial.

```csharp
// Gas/GameplayTags.cs
public readonly struct GameplayTag : IEquatable<GameplayTag> {
    public int Id { get; } public bool IsValid { get; } public static GameplayTag None { get; }
    public static GameplayTag Request(string name);             // interns; invalid name -> None + Log.Warn
    public static bool TryFind(string name, out GameplayTag tag);
    public static GameplayTag FromId(int id);
    public string Name { get; } public GameplayTag Parent { get; } public int Depth { get; }
    public bool Matches(GameplayTag other); public bool MatchesExact(GameplayTag other);
    public static IReadOnlyList<GameplayTag> All { get; }
}
public sealed class GameplayTagContainer : IEnumerable<GameplayTag> {
    public GameplayTagContainer(); public GameplayTagContainer(params GameplayTag[] tags);
    public int Add(GameplayTag t, int count = 1); public int Remove(GameplayTag t, int count = 1);
    public int CountOf(GameplayTag t); public bool Has(GameplayTag t); public bool HasExact(GameplayTag t);
    public bool HasAny(GameplayTagContainer o); public bool HasAll(GameplayTagContainer o);
    public bool HasNone(GameplayTagContainer o); public int Count { get; } public void Clear();
}

// Gas/GasAttributes.cs
public readonly struct AttributeRef : IEquatable<AttributeRef> {
    public int Id { get; } public bool IsValid { get; } public string Name { get; }
    public static AttributeRef Of(string name);
}
public sealed class AttributeDef { public string Name; public float DefaultBase, Min, Max, ForwardScale;
    public AttributeRef MinFrom, MaxFrom, ForwardTo; public bool IsMeta; }
public sealed class AttributeSetDef { public string Name; public IReadOnlyList<AttributeDef> Attributes; }
public readonly record struct AttributeChange(AbilitySystem System, AttributeRef Attribute, float OldValue, float NewValue, Entity Instigator);
public delegate void PreAttributeChange(AbilitySystem system, AttributeRef attribute, ref float newValue);

// Gas/GasEffects.cs
public enum EffectPolicy { Instant, Duration, Infinite }
public enum ModOp { Add, Multiply, Override }
public enum MagnitudeKind { Constant, SetByCaller, AttributeBased }
public enum StackScope { None, Target, Source }
public enum StackExpire { ClearAll, RemoveOne }
public enum EffectFailure { None, NoSystem, NoAuthority, UnknownEffect, TagRequirement, StackLimit, LimitReached }
public sealed class ModifierDef { public AttributeRef Attribute; public ModOp Op; public MagnitudeKind Kind;
    public float Value, Add; public GameplayTag CallerTag; public AttributeRef FromAttribute; public bool FromSource; }
public sealed class EffectDef { public string Name; public int Id; public EffectPolicy Policy; public float Duration, Period;
    public bool ExecuteOnApply, StackRefresh; public StackScope Stack; public int StackLimit; public StackExpire StackExpire;
    public IReadOnlyList<ModifierDef> Modifiers; public GameplayTagContainer AssetTags, GrantedTags,
        AppRequired, AppBlocked, OngoingRequired, OngoingBlocked, RemovesWithTags; public GameplayTag Cue; }
public sealed class EffectSpec {
    public static EffectSpec Of(string effectName); public static EffectSpec Of(EffectDef def);
    public EffectSpec From(Entity source); public EffectSpec SetByCaller(GameplayTag tag, float value);
    public EffectSpec WithDuration(float seconds);                 // overrides Duration for this application
}
public readonly struct ActiveEffectHandle : IEquatable<ActiveEffectHandle> { public int Value { get; } public bool IsValid { get; } }
public readonly record struct ActiveEffectInfo(ActiveEffectHandle Handle, EffectDef Def, Entity Source, int Stacks, float Remaining /* +inf if infinite */, bool Inhibited);

// Gas/GasAbilities.cs
public enum AbilityFailure { None = 0, NoSystem = 1, NotGranted = 2, AlreadyActive = 3, MissingRequiredTag = 4,
    BlockedByTag = 5, BlockedByAbility = 6, OnCooldown = 7, CannotAfford = 8, BehaviourRefused = 9, NoAuthority = 10 }
public enum AbilityCommit { Activate, Manual }
public sealed class AbilityDef { public string Name; public int Id; public GameplayTagContainer AbilityTags, CancelsTags, BlocksTags,
    OwnedTags, RequiredTags, BlockedTags, TriggerEvents; public EffectDef? Cost, Cooldown; public AbilityCommit Commit;
    public float Duration; public bool TriggerOnGranted, Retrigger; public string? Behaviour; }
public readonly struct AbilityHandle : IEquatable<AbilityHandle> { public int Value { get; } public bool IsValid { get; } }
public abstract class GameplayAbility {                       // one instance per granted ability per system
    public AbilitySystem Owner { get; } public AbilityDef Def { get; } public AbilityHandle Handle { get; }
    public int Level { get; } public Entity Target { get; }       // Target valid during an activation
    public virtual void OnGranted() { } public virtual void OnRevoked() { }
    public virtual bool CanActivate() => true;
    public virtual void OnActivate() { } public virtual void OnEnd(bool cancelled) { }
    protected bool Commit(); protected void End(bool cancelled = false);
}

// Gas/AbilitySystem.cs
public sealed class AbilitySystem {
    public Entity Owner { get; } public bool HasAuthority { get; set; }          // false = client proxy (2.13)
    public static AbilitySystem? Of(Entity e); public static AbilitySystem GetOrAdd(Entity e);
    public static bool Remove(Entity e);
    // attributes
    public bool AddAttributeSet(string name); public bool AddAttributeSet(AttributeSetDef def);
    public bool Has(AttributeRef a); public float GetValue(AttributeRef a); public float GetBase(AttributeRef a);
    public bool SetBase(AttributeRef a, float value, Entity instigator = default);
    public IEnumerable<AttributeRef> Attributes { get; }
    public event Action<AttributeChange>? AttributeChanged; public PreAttributeChange? PreAttributeChange;
    // tags
    public GameplayTagContainer Tags { get; }                                   // read-only view of effective tags (loose + granted)
    public bool HasTag(GameplayTag t); public int TagCount(GameplayTag t);
    public void AddLooseTag(GameplayTag t, int count = 1); public void RemoveLooseTag(GameplayTag t, int count = 1);
    public event Action<GameplayTag, bool, int>? TagChanged;                     // tag, present, count
    // effects
    public ActiveEffectHandle Apply(EffectSpec spec, out EffectFailure failure);
    public ActiveEffectHandle Apply(EffectSpec spec) => Apply(spec, out _);
    public static ActiveEffectHandle ApplyTo(Entity target, EffectSpec spec);   // GetOrAdd(target).Apply
    public bool Remove(ActiveEffectHandle h); public int RemoveByTag(GameplayTag t); public int RemoveByDef(EffectDef d);
    public bool TryGetEffect(ActiveEffectHandle h, out ActiveEffectInfo info);
    public IReadOnlyList<ActiveEffectInfo> ActiveEffects { get; }
    public bool TryGetEffect(EffectDef d, out ActiveEffectInfo info);           // first instance of a def
    public event Action<ActiveEffectInfo>? EffectApplied, EffectRemoved;
    // abilities
    public AbilityHandle Grant(string abilityName, int level = 1);              // idempotent per ability
    public bool Revoke(AbilityHandle h); public AbilityHandle Find(string abilityName);
    public AbilityFailure TryActivate(AbilityHandle h, Entity target = default);
    public AbilityFailure CanActivate(AbilityHandle h);
    public bool Commit(AbilityHandle h); public bool End(AbilityHandle h, bool cancelled = false);
    public int CancelAbilities(GameplayTag tag); public bool IsActive(AbilityHandle h);
    public float CooldownRemaining(AbilityHandle h);
    public int SendGameplayEvent(GameplayTag tag, Entity instigator = default, float magnitude = 0f, Entity target = default);
    public AbilityHandle TryActivateByTag(GameplayTag tag, Entity target, out AbilityFailure failure);
    public event Action<AbilityHandle, bool>? AbilityActivated, AbilityEnded;    // ended: cancelled
    public string Describe();
}
public readonly partial struct Entity {                                          // Gas/EntityAbilities.cs
    public AbilitySystem? Abilities { get; }  public AbilitySystem GetOrAddAbilities();
}

// Gas/GameplayDefs.cs: the registry
public static class GameplayDefs {
    public static EffectDef? FindEffect(string name); public static AbilityDef? FindAbility(string name);
    public static AttributeSetDef? FindAttributeSet(string name);
    public static bool LoadText(string text, string sourceName, out string error);   // one file; links after all loaded
    public static bool LoadDirectory(string contentRoot, out string error);          // recursive *.ocgas, sorted ordinal
    public static void Register(EffectDef def); public static void Register(AbilityDef def); public static void Register(AttributeSetDef def);
    public static EffectBuilder Effect(string name); public static AbilityBuilder Ability(string name); public static AttributeSetBuilder AttributeSet(string name);
    public static void Clear();
}

// Gas/GameplayAbilities.cs: host entry points (called by HostBridge, section 2.12) and attributes
public static class GameplayAbilities {
    public static IGasHost Host { get; set; }
    public static IGasReplicationSink? Replication { get; set; }
    public static void Startup();                    // install the managed-state provider (once)
    public static void SetContentRoot(string dir);
    public static void BeginSession();               // clear systems, reload defs from the content root
    public static void RegisterAssembly(System.Reflection.Assembly asm);
    public static void ActorBeginPlay(int entity);   // class defaults
    public static void EntityGone(int entity);
    public static void Update();                     // once per frame, tick group 0
    public static void Shutdown();                   // script unload: drop behaviours and every user callback
}
[AttributeUsage(AttributeTargets.Class)] public sealed class AverAbilityAttribute : Attribute { public AverAbilityAttribute(string name); }
[AttributeUsage(AttributeTargets.Class)] public sealed class AverAbilitySystemAttribute : Attribute {
    public string[] AttributeSets, Grant, Apply, Tags; }                         // class defaults for an actor class
[AttributeUsage(AttributeTargets.Class)] public sealed class AverGameplayDefsAttribute : Attribute { }   // static class with `static void Define()`
public interface IGasHost { double Now { get; } bool IsAlive(int entity); string? ClassNameOf(int entity);
    int ListenerCount(string eventName); void Raise(string eventName, int sender, int target, params object[] args); }
```

Notes: `[AverGameplayDefs]` marks a static class exposing `static void Define()` that calls `GameplayDefs.Effect(...)`/`.Ability(...)`/`.AttributeSet(...)`; `RegisterAssembly` calls it once per load. Builders are fluent (`GameplayDefs.Effect("Effect.Heal").Instant().Modify("Health", ModOp.Add, 25f).Register()`); they build the same def objects the parser builds, and code-first and file-first defs share one namespace (a duplicate name is an error naming both sources).

**Hot reload / unload.** `Shutdown()` must drop every delegate that came from the script context (C# events on systems, `PreAttributeChange`, behaviour instances, builders), otherwise the collectible load context never unloads (the reason `Timers.Shutdown`/`Events.Shutdown` exist, `HostBridge.cs:294`). System data (attributes, effects, tags) is kept; `RegisterAssembly` after reload re-binds behaviours by name. Whole-state hot reload is feature #17, not this one.

### 2.10 Native: the managed-state seam

New header `modules/framework/include/aver/framework/framework_state_abi.h`, new source `modules/framework/src/EntityStateAbi.cpp` (added to `add_library(Aver.Framework ...)` in `modules/framework/CMakeLists.txt`), new P/Invoke file `scripting/csharp/Aver.Framework/EntityStateNative.cs` (`internal static class FwState`, same style as `TimersEventsNative.cs`). It does not touch `framework_abi.h`, so it cannot collide with the input feature's `AVER_FW_ABI_VERSION_MINOR` bump. Same conventions as `framework_timers_abi.h`: own version, int32/char only, UTF-8, relayed provider (framework holds function pointers, links nothing).

```c
#define AVER_FW_STATE_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_state_abi_version(void);

/* Number of named state channels entity `e` currently carries (0 = none). */
typedef int32_t (AVER_FW_CALL* aver_fw_state_count_fn)(int32_t entity, void* user);
/* Channel `index` (0..count-1, stable order): copies the NUL-terminated UTF-8 name into nameBuf
 * (truncated to nameCap) and the payload text into dataBuf (up to dataCap bytes, NOT terminated).
 * Returns the payload's FULL byte length, which may exceed dataCap (the caller retries with a bigger
 * buffer), or -1 for a bad index. */
typedef int32_t (AVER_FW_CALL* aver_fw_state_at_fn)(int32_t entity, int32_t index,
        char* nameBuf, int32_t nameCap, char* dataBuf, int32_t dataCap, void* user);
/* Delivers one channel to the entity (restore). 1 if accepted, 0 if the provider does not know the
 * channel or the payload was rejected. */
typedef int32_t (AVER_FW_CALL* aver_fw_state_set_fn)(int32_t entity, const char* name,
        const char* data, int32_t dataLen, void* user);

AVER_FW_ABI int32_t aver_fw_set_state_provider(aver_fw_state_count_fn count, aver_fw_state_at_fn at,
                                               aver_fw_state_set_fn setState, void* user); /* always 1 */
AVER_FW_ABI int32_t aver_fw_state_count(int32_t entity);                                     /* 0 with no provider */
AVER_FW_ABI int32_t aver_fw_state_at(int32_t entity, int32_t index, char* nameBuf, int32_t nameCap,
                                     char* dataBuf, int32_t dataCap);                        /* -1 with no provider */
AVER_FW_ABI int32_t aver_fw_state_set(int32_t entity, const char* name, const char* data, int32_t dataLen); /* 0 with no provider */
```

The C# provider is installed by `GameplayAbilities.Startup()` as three pinned delegates (`[UnmanagedFunctionPointer(Cdecl)]`, the `TimerDispatchFn` pattern), so `Aver.Framework` needs no `unsafe`. Channels are generic on purpose (a later Smart Objects or quest state can use another channel name); GAS uses the single channel `gas`.

`aver::save` (the module does not link the framework; it only gains pointers):

- `modules/save/include/aver/save/SaveWorld.hpp`: add to `Host` `StateCountFn stateCount`, `StateAtFn stateAt`, `StateSetFn stateSet` (types mirror the three typedefs above with `scene::Entity` in place of the `int32_t`, `i32` for lengths), each defaulting to null with the effect of null stated beside it (null: no managed state is captured / restored; restore counts the fields as dropped).
- `modules/save/src/SaveWorld.cpp`: `constexpr char kStateComponentType[] = "$State"` beside `kGraphVarsComponentType`; `captureState(e, en, opt)` called next to `captureGraphVars` (line 221): one synthetic component `$State` with one `STRING` field per channel (`name` = channel, `s` = payload), payload read with a 4096-byte buffer and one retry at the returned length, hard cap 1 MiB (over: warn with entity and channel, skip). Restore special-cases `$State` exactly where `$GraphVars` is special-cased (line 436), before the `componentId()` lookup, calling `stateSet` per field and counting a refusal in `droppedFields`. Payloads are binary-safe (the string table is length-prefixed; the test covers embedded newlines).
- `Runtime/src/GameApp.cpp` (`saveHost()` at 168-185) and `sandbox/src/SandboxApp.hpp` (371-394): add the three relay functions (`return aver_fw_state_count(static_cast<i32>(e));` etc.) and assign them in `saveHost()`. Both roots are edited identically (the existing duplication, `docs/RUNTIME-DEDUP.md`).
- `OcSaveData::contentVersion` unchanged: a pseudo-component is additive; an older reader reports `$State` as an unregistered component and skips it, and a save without it loads in a new build with GAS state empty.

### 2.11 Save and load

Channel `gas`, payload text (invariant culture, shortest round-trip floats, `\n` separated), written by `GasSave.Write(AbilitySystem)` and read by `GasSave.Read(entity, text)`:

```
GASSTATE 1
SET Character
ATTR Health 73.5                      # BASE values only; Current is recomputed
LOOSE State.Alive 1                   # loose tag counts (granted tags come back with their effects)
GRANT Ability.Fireball 1              # name, level
EFFECT Effect.Poison stacks=2 remaining=3.25 period=0.5 mags=-3,1 source=self
EFFECT Effect.Regen stacks=1 remaining=inf mags=2 source=none
```

- `remaining` is seconds left (`inf` for infinite); `period` the seconds until the next execution (absent without a period); `mags` the resolved magnitudes in modifier order (so SetByCaller and snapshotted AttributeBased values reload exactly); `source` is `self` if the owner was the source, else `none` (entity handles do not survive a file; a later version may add a save-index remap).
- **Not saved**: active ability runs (an ability mid-flight ends at save; its cooldown and cost effects are saved because they are effects), pending notifications, handles (renumbered on load), behaviour state. A behaviour that needs persistence uses the graph-var or its own channel.
- **Read** (`stateSet` for `gas`): creates the system for the entity if absent, adds the sets, sets Base values, loose tags, grants (instantiating behaviours), re-creates effects with `expiresAt = host.Now + remaining`, recomputes, and raises **no** notifications (it is a restore, not a change). Unknown def or attribute names are skipped with one warning each ("save is from a build with different defs"); a bad header or version refuses the whole payload (`stateSet` returns 0).
- **Order guarantee** the restore relies on (verified in `SaveWorld.cpp:436-479, 510-515`): state is delivered after the entity is spawned and before `beginPlay`, so `ActorBeginPlay` finds the system and does not re-apply class defaults.
- Entities are saved only when `AbilitySystem.Of(entity)` exists (the provider answers `count = 1` for them, 0 otherwise).

### 2.12 Runtime and packaged-game wiring

All edits to `scripting/csharp/Aver.Scripting.Bridge/HostBridge.cs` are single-line calls into `GameplayAbilities`; the bridge owns no GAS logic.

| Where (HostBridge.cs, current lines) | Call |
|---|---|
| `SetupManagedActors`, next to `InstallGraphVarProvider()` (1004) | `GameplayAbilities.Startup();` |
| `TryLoadAssembly`, after `DeclareActors(asm, file)` (902) | `GameplayAbilities.RegisterAssembly(asm);` |
| `DeclareGraphClasses`, after the directory check (655-657) | `GameplayAbilities.SetContentRoot(dir);` |
| `ConfigureInput`, first statement inside `try` (404) | `GameplayAbilities.BeginSession();` with a comment: this is the one per-Play-start call both roots already make, so the GAS session reset rides on it rather than adding a ScriptHost export |
| `DispBeginPlay`, before the `TryGetValue` early return (1454) | `GameplayAbilities.ActorBeginPlay(entity);` (placed before the early return so graph-only classes get class defaults; the implementer verifies a graph-class actor reaches `DispBeginPlay`, otherwise hooks the end of `DispBind`) |
| `DispUnbind`, beside `GraphTimerEvents.UnbindEntity` (1564) | `GameplayAbilities.EntityGone(entity);` |
| `DispTickAll`, in the `group == 0` block beside `UiSystem.Pump()` (1469) | `GameplayAbilities.Update();` |
| `DrainAndUnload`, beside `Timers.Shutdown()` (294) | `GameplayAbilities.Shutdown();` |

`BeginSession()` order: clear every system (their effect and cooldown state belongs to the ended session), `GameplayDefs.Clear()`, `LoadDirectory(contentRoot)` (errors logged as `[GAS] file:line: ...`, the session continues with what linked), then re-run `RegisterAssembly` for remembered assemblies. A project with no `.ocgas` files and no GAS code pays: one directory scan per Play start, and an empty `Update()` loop.

Packaged game: nothing extra. The Content folder ships (`docs/PACKAGING.md:108`), `ConfigureInput` runs at startup (`GameApp.cpp:1174`), the runtime's `saveHost()` carries the state pointers. Headless/unscripted builds: no bridge, no GAS, `aver_fw_state_*` answer 0.

### 2.13 Replication seam (nothing networked is built)

- `AbilitySystem.HasAuthority` (default true). When false, every mutating public call returns its failure value (`EffectFailure.NoAuthority`, `AbilityFailure.NoAuthority`, `false`) instead of acting; reads work.
- `IGasReplicationSink` (`GameplayAbilities.Replication`, null by default): called after each authoritative change with plain data: `AttributeBase(system, attr, newBase)`, `EffectAdded/Changed/Removed(system, ActiveEffectInfo + mags)`, `LooseTag(system, tag, count)`, `AbilityGranted/Revoked(system, handle, name, level)`, `AbilityActivated/Ended(system, handle, cancelled)`.
- `IGasClientRequests` (`GameplayAbilities.ClientRequests`): when set on a non-authoritative system, `TryActivate` forwards `(handle, target)` to it instead of failing.
- Handles are per-system monotonic ints and `EffectSpec` + `mags` + the `.ocgas` names are the wire/save vocabulary (the same one the save blob uses), so wave 7 can replicate without redesigning the model. A client applies remote state through internal `ApplyRemote*` methods that bypass `HasAuthority` (to be added in wave 7; not specified here).

### 2.14 Editor surface (small, on purpose)

No new panel in this feature. Reasoning: authoring is a text file with line-specific errors, and a runtime inspector needs a native-to-managed query path that does not exist (no `Describe` export) and is better designed with the Blueprint debugger (#17).

- `sandbox/src/SandboxContentBrowser.cpp`: (a) type table entry `{".ocgas", {ICON_SETTINGS, ..., "Gameplay Definitions"}}` beside `.ocinput` (line 1733); (b) `cbCreateGameplayDefs()` beside `cbCreateInputScheme` (518) writing a commented starter (one `ATTRSET`, one instant and one duration `EFFECT`, one `ABILITY`) via `editor::writeNewFile`; declared in `SandboxApp.hpp`; (c) menu item "New Gameplay Definitions" next to "New Input Scheme" (1155) with `uiReg_.track("cb.add.gameplayDefs")`; (d) add `".ocgas"` to `cbIsSourceFile` (315) so a double-click opens it in the user's IDE instead of Notepad.
- Graph palette: `sandbox/src/GraphNodeDefsGas.hpp` (new, category `Abilities`), included and called from `GraphNodeDefs.hpp` like `appendGameSystemNodeDefs`.
- Parse/link errors surface in the editor log at Play start (`[GAS] Content/Combat.ocgas:14: ...`) and as a `Log.Error` line; a bad file never stops Play.

## 3. Graph nodes

Implemented by the separate graph-node agent, from these rows. Owner class `GasGraph` (internal static, `scripting/csharp/Aver.Framework/Gas/GasGraph.cs`, written by the GAS implementer FIRST so the graph agent has real methods; `Validate()` checks the rows). Rows go in a new `scripting/csharp/Aver.Graph/GraphGasNodeTable.cs` (`private static GenericNodeSpec[] GasTable()` in `partial class GameSystemNodes`); `Table()` in `GraphGameSystemNodeTable.cs` gets one added `.Concat(GasTable())` (merge-adjacent, section 5). Palette mirror: `GraphNodeDefsGas.hpp`, pin for pin. Node type names use the `AN_` prefix and the lower-case table key.

**Resolution rule shared by every row.** A method takes `(..., int xId, string x, ...)`; if `xId != 0` it wins, else the NODE attribute `x` (optional `@x?`) is interned/looked up; both empty = the call fails (false / 0 / handle 0) and logs once per node id. Failure never throws. `entity` pins are the OWNER; `0` means "not given" and fails.

| Type | Kind | `GasGraph` method | Inputs (defaults) | Outputs | Attribute |
|---|---|---|---|---|---|
| `AN_GasTagId` | Pure | `int TagIdForGraph(string tag)` | none | `tagId:int` | `tag` |
| `AN_GasAttributeId` | Pure | `int AttributeIdForGraph(string attribute)` | none | `attrId:int` | `attribute` |
| `AN_GasEffectId` | Pure | `int EffectIdForGraph(string effect)` | none | `effectId:int` (0 if no such def) | `effect` |
| `AN_GasAbilityId` | Pure | `int AbilityIdForGraph(string ability)` | none | `abilityId:int` (0 if no such def) | `ability` |
| `AN_GasHasTag` | Pure | `bool HasTagForGraph(int entity, int tagId, string tag)` | entity, tagId | `has:bool` | `tag?` |
| `AN_GasHasTags` | Pure | `bool HasTagsForGraph(int entity, bool all, string tags)` | entity, all | `has:bool` | `tags` (comma list) |
| `AN_GasTagCount` | Pure | `int TagCountForGraph(int entity, int tagId, string tag)` | entity, tagId | `count:int` | `tag?` |
| `AN_GasAddTag` | Exec | `bool AddTagForGraph(int entity, int tagId, string tag, int count)` | entity, tagId, count=1 | `success:bool` | `tag?` |
| `AN_GasRemoveTag` | Exec | `bool RemoveTagForGraph(int entity, int tagId, string tag, int count)` | entity, tagId, count=1 | `success:bool` | `tag?` |
| `AN_GasGetAttribute` | Pure | `bool GetAttributeForGraph(int entity, int attrId, string attribute, out float value, out float baseValue)` | entity, attrId | `value:float, base:float, success:bool` | `attribute?` |
| `AN_GasSetAttribute` | Exec | `bool SetAttributeBaseForGraph(int entity, int attrId, string attribute, float value)` | entity, attrId, value | `success:bool` | `attribute?` |
| `AN_GasAddAttributeSet` | Exec | `bool AddAttributeSetForGraph(int entity, string set)` | entity | `success:bool` | `set` |
| `AN_GasApplyEffect` | Exec | `int ApplyEffectForGraph(int target, int source, int effectId, string effect, string callerTag, float callerValue, out bool applied)` | target, source (0 = target), effectId, callerValue | `handle:int, applied:bool` | `effect?`, `callertag?` |
| `AN_GasRemoveEffect` | Exec | `bool RemoveEffectForGraph(int entity, int handle)` | entity, handle | `success:bool` | none |
| `AN_GasRemoveEffectsByTag` | Exec | `int RemoveEffectsByTagForGraph(int entity, int tagId, string tag)` | entity, tagId | `count:int` | `tag?` |
| `AN_GasRemoveEffectsByDef` | Exec | `int RemoveEffectsByDefForGraph(int entity, int effectId, string effect)` | entity, effectId | `count:int` | `effect?` |
| `AN_GasEffectInfo` | Pure | `bool EffectInfoForGraph(int entity, int effectId, string effect, out int stacks, out float remaining, out int handle)` | entity, effectId | `active:bool, stacks:int, remaining:float, handle:int` (remaining = 1e30 for infinite) | `effect?` |
| `AN_GasGrantAbility` | Exec | `int GrantAbilityForGraph(int entity, int abilityId, string ability, int level)` | entity, abilityId, level=1 | `handle:int` (0 = failed) | `ability?` |
| `AN_GasRevokeAbility` | Exec | `bool RevokeAbilityForGraph(int entity, int abilityId, string ability)` | entity, abilityId | `success:bool` | `ability?` |
| `AN_GasTryActivate` | Exec | `bool TryActivateForGraph(int entity, int abilityId, string ability, int target, out int reason)` | entity, abilityId, target | `success:bool, reason:int` | `ability?` |
| `AN_GasTryActivateByTag` | Exec | `bool TryActivateByTagForGraph(int entity, int tagId, string tag, int target, out int abilityId, out int reason)` | entity, tagId, target | `success:bool, abilityId:int, reason:int` | `tag?` |
| `AN_GasCanActivate` | Pure | `bool CanActivateForGraph(int entity, int abilityId, string ability, out int reason)` | entity, abilityId | `can:bool, reason:int` | `ability?` |
| `AN_GasCommitAbility` | Exec | `bool CommitAbilityForGraph(int entity, int abilityId, string ability)` | entity, abilityId | `success:bool` | `ability?` |
| `AN_GasEndAbility` | Exec | `bool EndAbilityForGraph(int entity, int abilityId, string ability, bool cancelled)` | entity, abilityId, cancelled | `success:bool` | `ability?` |
| `AN_GasCancelByTag` | Exec | `int CancelAbilitiesByTagForGraph(int entity, int tagId, string tag)` | entity, tagId | `count:int` | `tag?` |
| `AN_GasIsAbilityActive` | Pure | `bool IsAbilityActiveForGraph(int entity, int abilityId, string ability)` | entity, abilityId | `active:bool` | `ability?` |
| `AN_GasCooldown` | Pure | `float AbilityCooldownForGraph(int entity, int abilityId, string ability, out bool onCooldown)` | entity, abilityId | `remaining:float, onCooldown:bool` | `ability?` |
| `AN_GasSendEvent` | Exec | `int SendGameplayEventForGraph(int entity, int instigator, int tagId, string tag, float magnitude)` | entity, instigator, tagId, magnitude | `triggered:int` | `tag?` |

Row strings follow the existing grammar, for example:
`Reg("an_gasapplyeffect", GenericKind.Exec, typeof(GasGraph), "ApplyEffectForGraph", "target:int,source:int,effectId:int,callerValue:float", "handle:int,applied:bool", "p:target,p:source,p:effectId,@effect?,@callertag?,p:callerValue,>applied", "handle")` and
`Reg("an_gasgetattribute", GenericKind.Pure, typeof(GasGraph), "GetAttributeForGraph", "entity:int,attrId:int", "value:float,base:float,success:bool", "p:entity,p:attrId,@attribute?,>value,>base", "success")`.

**Entries (no new node needed).** A graph hears GAS with the existing `AN_OnEvent`: ENTRY name = a bus event from section 2.7, for example `ENTRY n GAS.AbilityActivated.Ability.Fireball` (ability body), `GAS.AttributeChanged.Health` (read args with `AN_EventPayload` index 0 = id, 1 = new, 2 = old), `GAS.TagChanged.State.Stunned`, `GAS.GameplayEvent.Event.Hit`. `AN_Gas*Id` nodes plus an int compare cover the generic names.

**Parity table (every C# capability has a node or an event).** Tags: has/add/remove/count -> nodes; tag-changed -> event. Attributes: get/set/add-set -> nodes; `AttributeChanged` -> event; `PreAttributeChange` hooks and builders are C#-only (their graph equivalent is data in `.ocgas`). Effects: apply/remove(by handle, tag, def)/info -> nodes; applied/removed -> events; effect specs with several SetByCaller values: the node carries one caller value, so effects needing several are C#-only (open question 6). Abilities: grant/revoke/try/commit/end/cancel/can/active/cooldown/send-event -> nodes; activated/ended/failed -> events; `GameplayAbility` subclasses are C#-only by design (graph abilities are event bodies).

## 4. Tests

Three layers, each registered where stated. Test file names are exact.

**A. Managed core: new project `scripting/csharp/Aver.Framework.Tests/` (console exe, house pattern: `Main` returns the failure count, `RunAll()` per file; references `Aver.Framework`; a `FakeGasHost : IGasHost` with a settable clock and an event log).** Run: `dotnet run --project scripting/csharp/Aver.Framework.Tests`. Also wired as ctest `GasManagedTests` in a new `tests/gas/CMakeLists.txt` (`find_program(AVER_DOTNET dotnet)`; the test is skipped, not failed, when it is absent; added in the root `CMakeLists.txt` under `if(AVER_MODULE_FRAMEWORK)`). There is no precedent for a ctest-run managed test (`CMakeLists.txt:475` notes `Aver.Graph.Tests` is hand-run); if the wrapper proves awkward, keep it hand-run and say so in `docs/GAMEPLAY_ABILITIES.md`.

| File | Asserts |
|---|---|
| `GameplayTagTests.cs` | Name grammar accepted/rejected; parents interned (`A.B.C` creates `A`, `A.B`); `Matches` hierarchical, `MatchesExact` not; container counts floor at 0; `Has(State.Stunned)` true when holding `State.Stunned.Hard`; flips reported for tag and ancestors; id order deterministic for a fixed registration sequence. |
| `AttributeTests.cs` | Defaults at `AddAttributeSet`; clamp to literal min/max; clamp to `MaxFrom` and re-clamp Base down when the bound shrinks (not up when it grows); cyclic bounds refused; duplicate attribute name across sets refused; `PreAttributeChange` can clamp; events deferred to batch end and fired in order; handler-mutation nesting guard drops after 8 and warns once. |
| `EffectModifierTests.cs` | Evaluation formula exactly (Add sum, Multiply product, Override last-applied wins, then clamp) with literal numbers; instant ops change Base and persist, duration ops change Current only and unwind on removal; stacks scale Add and Multiply as specified. |
| `EffectDurationTests.cs` | Expiry at the fake-clock time (not before, at/after); infinite never expires; `WithDuration` override; removal restores Current and tags; `RemovesWithTags` cleanse; app required/blocked refusals return `TagRequirement`. |
| `EffectPeriodicTests.cs` | Execution count over a clock sweep; `ExecuteOnApply`; a 10-period jump executes 8 (cap); modifiers hit Base not the aggregator; inhibited effect skips executions. |
| `EffectStackingTests.cs` | `none` independent; `target N` increments then `StackLimit` at N; `refresh` vs `keep`; `expire one` decrements and restarts; `source N` separates sources; periodic execution per stack. |
| `EffectInhibitionTests.cs` | Ongoing-required toggles modifiers and granted tags off/on as tags change in one batch; duration clock unaffected; two effects granting one tag keep it until both are gone (counts). |
| `AbilityTests.cs` | Every `AbilityFailure` reachable and in the documented check order; cost check does not apply anything; commit applies cost + cooldown atomically and fails clean if unaffordable; cooldown tag blocks until expiry then allows; `CancelsTags`/`BlocksTags`/`OwnedTags` counts balanced after End and Revoke; `Retrigger`; auto-end at `Duration`; trigger by gameplay event (hierarchical) and passive `granted`; behaviour hooks called in order; `Grant` idempotent. |
| `GasEventTests.cs` | Every row of section 2.7: name pair, sender/target, args; specific name raised only when it has a listener (`ListenerCount` stub); notifications absent on restore. |
| `GameplayDefsParserTests.cs` | Fixture `Fixtures/combat.ocgas` links and every field lands on the def; each error case refused with `name:line:` text (missing header, record in wrong block, unknown record, bad number, duplicate name, undefined attribute/effect/tag reference, cost not instant, period on instant, cycle); forward refs across two files; failed file contributes nothing; directory load order stable. |
| `GasSaveTests.cs` | Write -> Read into a fresh system reproduces Base values, loose tags, grants, active effects (`stacks`, `remaining`, `period`, `mags`, `source`) and recomputed Currents byte-for-byte on a second Write; unknown def/attribute skipped with one warning each; bad header refused; embedded `\n` payload survives the provider callbacks; no notifications on Read; `ActorBeginPlay` does not re-default a restored system. |
| `GasReplicationSeamTests.cs` | `HasAuthority=false` refuses every mutating call with the right code; sink callbacks fire once per authoritative change with correct data; `ClientRequests` receives forwarded activations. |

**B. Graph nodes: `scripting/csharp/Aver.Graph.Tests/GasNodeTests.cs`** (graph-node agent), registered by one line in `Program.cs` beside `GameSystemNodeTests.RunAll()` (line 265). Mirrors `GameSystemNodeTests`: `GameSystemNodes.Validate()` returns no problems for the GAS rows (every method exists, parameter types match pins, every input pin feeds a parameter); a bare `NODE` gets its default pins and defaults (`count=1`, `level=1`); exec nodes are refused by the pull compiler with the standard message, pure ones compile under both; a missing required attribute (`AN_GasTagId` without `tag=`) fails by name; emitted IL is valid for every GAS node (invoking it fails inside the native call, never `InvalidProgramException`); a sample graph parses with `ENTRY n GAS.AbilityActivated.Ability.Fireball` + `AN_GasEndAbility`.

**C. Native and real-CLR, in `tests/save/`** (add to `tests/save/CMakeLists.txt`):
- `SaveWorldTest.cpp` gains a case: a fake `Host` with `stateCount/stateAt/stateSet` function pointers; capture writes one `$State` component per entity that has channels; restore delivers each channel to `stateSet` before `beginPlay`; a payload with `\n`, 5000 bytes (forces the retry) and a 2 MiB payload (skipped with a warning); no pointers = nothing captured and a restored `$State` is counted as dropped, not an error; a save containing `$State` loads in a build with the pointers null.
- `tests/save/src/GasPersistTest.cpp` (real CLR, same shape and guards as `GraphVarPersistTest.cpp`; "unavailable is not a failure"): boots the bridge, writes a temp content folder with a small `.ocgas`, calls `declareGraphClasses(contentDir)` then `configureInput("", "")` (this is the BeginSession path), creates an entity, drives `aver_fw_state_set` with a hand-written `GASSTATE 1` blob, reads it back through `aver_fw_state_count/at`, and asserts the attribute Base and an effect's `remaining` round-trip; then `aver_fw_save_write` / `aver_fw_save_load` through the installed providers and asserts the same state after a full world restore. This is the only test that proves `Startup()` installed the provider and the bridge hooks fire.
- `tests/abi`: no enum is added to the C ABI; the new constants are `AVER_FW_STATE_ABI_VERSION` only, so `AbiEnumTest` needs no row. `AbilityFailure`/`EffectFailure` are C#-only and are pinned by `AbilityTests`.

Verification the implementer must do and report (not claim): build, `dotnet run` the managed suite and `Aver.Graph.Tests`, ctest `SaveWorldTest` + `GasPersistTest`, then a headed Play with a sample project (one `.ocgas`, one graph class with an ability body and an attribute-changed print) checking Health damage -> clamp -> event -> save -> load -> same Health and a cooldown still running.

## 5. Files to create or modify, and ownership

**Created (GAS owns outright)**
- `scripting/csharp/Aver.Framework/Gas/`: `GameplayTags.cs`, `GasAttributes.cs`, `GasEffects.cs`, `GasAbilities.cs`, `AbilitySystem.cs`, `GameplayDefs.cs`, `GameplayDefsBuilders.cs`, `OcGasParser.cs`, `GameplayAbilities.cs` (host entry points, attributes, `IGasHost`, replication interfaces, `EngineGasHost`), `GasSave.cs`, `GasGraph.cs`, `EntityAbilities.cs`
- `scripting/csharp/Aver.Framework/EntityStateNative.cs`
- `modules/framework/include/aver/framework/framework_state_abi.h`, `modules/framework/src/EntityStateAbi.cpp`
- `scripting/csharp/Aver.Graph/GraphGasNodeTable.cs`, `sandbox/src/GraphNodeDefsGas.hpp`
- `scripting/csharp/Aver.Framework.Tests/` (csproj, `Program.cs`, the test files in section 4A, `FakeGasHost.cs`, `Fixtures/combat.ocgas`)
- `scripting/csharp/Aver.Graph.Tests/GasNodeTests.cs`
- `tests/save/src/GasPersistTest.cpp`, `tests/gas/CMakeLists.txt`
- `docs/GAMEPLAY_ABILITIES.md` (user guide: format, API, node list, event table, save behaviour, limits)

**Modified (GAS-owned edits, additive)**
- `modules/framework/CMakeLists.txt`: add `src/EntityStateAbi.cpp` to the `add_library` list (one line)
- `modules/save/include/aver/save/SaveWorld.hpp`, `modules/save/src/SaveWorld.cpp`: state seam (2.10)
- `Runtime/src/GameApp.cpp`, `sandbox/src/SandboxApp.hpp`: three relays + three `saveHost()` assignments
- `scripting/csharp/Aver.Scripting.Bridge/HostBridge.cs`: the eight one-line calls in 2.12
- `scripting/csharp/Aver.Graph/GraphGameSystemNodeTable.cs`: `Table()` gains `.Concat(GasTable())`
- `scripting/csharp/Aver.Graph.Tests/Program.cs`: one `RunAll()` line
- `sandbox/src/GraphNodeDefs.hpp`: one include + one `appendGasNodeDefs(t)` call
- `sandbox/src/SandboxContentBrowser.cpp`, `sandbox/src/SandboxApp.hpp` (declaration): 2.14
- `tests/save/CMakeLists.txt`, `tests/save/src/SaveWorldTest.cpp`, root `CMakeLists.txt` (`add_subdirectory(tests/gas)`)
- Docs, append-only: `docs/ABI.md` (new subsection "Managed entity state", 5 entry points, `AVER_FW_STATE_ABI_VERSION` 1), `docs/formats/FORMAT_SPECS.md` (section 11d `.ocgas`), `docs/AVER_NODE_NODES.md` (GAS node family), `modules/framework/README.md` (one paragraph; the entry-point count line is left to the integrator because the input feature also changes it)

**Not touched by GAS (to keep the partition clean)**
- `framework_abi.h`, `FrameworkAbi.cpp`, `Native.cs` (`Fw`), `EnhancedInput.cs`, `InputScheme.cs`: input feature's. GAS needs none of them, and the state seam has its own header for that reason.
- `OcProject.hpp/.cpp`, `ProjectDesc`, `ScriptHost.*`, `SandboxPlay.cpp`, `SandboxSettings.cpp`: no new project key and no new ScriptHost export (data tables/localization may add keys; they are free to).
- `modules/anim*`, `Aver.Framework/Animation.cs`, `AnimGraph.cs`: animation feature's. (Ability-driven montages are a later wiring through events and `AN_TriggerAnim`.)
- `Aver.Framework.csproj`: GAS adds files under `Gas/`, which the SDK globs; no edit. The assembly version (1.0.0, `Aver.Framework.csproj`) is left for the integrator to bump once for the wave (the surface is additive).

**Merge-adjacent edits shared with other wave-2 agents (additive lines in the same file; whoever merges second keeps both):** `GraphGameSystemNodeTable.cs` (`Table()`), `Program.cs` (test runner list), `GraphNodeDefs.hpp` (include list), `SandboxContentBrowser.cpp` (type table, Add menu, `cbIsSourceFile`), `HostBridge.cs` (`ConfigureInput`, `DispTickAll`, `DrainAndUnload`), `GameApp.cpp`/`SandboxApp.hpp` (`saveHost`), `FORMAT_SPECS.md`, `ABI.md`, root `CMakeLists.txt`.

**Implementation order** (each step builds and tests): 1 tags + attributes + effects + host abstraction + managed tests; 2 abilities; 3 parser + registry + fixtures; 4 save blob + native seam + `SaveWorldTest` case; 5 bridge hooks + `GasPersistTest` + headed Play check; 6 `GasGraph` methods; 7 graph rows + palette + node tests (graph-node agent); 8 editor entries + docs.

## 6. Risks and open questions

1. **Two tag systems.** Hierarchical `GameplayTag` coexists with the 32-bit `CTags` mask rather than replacing it (mask is scene ABI, flat, used by `Game.WithTag` and tag nodes). Fine for GAS; but a user will ask "why two tags?" Option: later, `AN_GasHasTag` could also consult a registered mask mapping. Decide before shipping docs.
2. **Auto-discovery of every `*.ocgas` under Content** (no project key). Cheap and zero native plumbing, but order is by path and an editor-side stale file would load. If the owner prefers an explicit `GAS.DEFS` key, it is `OcProject` + `SandboxSettings` + `GameApp`/`SandboxPlay` + one ScriptHost export (the `INPUT.SCHEME` route), about a day more.
3. **AttributeBased magnitudes are snapshotted** at application; live tracking (a buff scaling with a changing Strength) needs dependency re-evaluation and is deferred.
4. **Ability runs are not saved** (cooldowns and costs are, being effects). A save during a channelled cast resumes with the cast ended.
5. **Level scaling** is absent: `Grant(level)` is stored and exposed to behaviours but scales nothing until the data-table feature offers curves (`MagnitudeKind.Table` is reserved).
6. **One SetByCaller value per `AN_GasApplyEffect`** node. Effects needing several callers are C#-only until a second `AN_GasSetCaller` staging node is wanted.
7. **Targeted events need `owner == target` subscribers**, so a global C# observer must subscribe per entity (or use the per-system C# events). A broadcast variant was rejected: it would wake every listener on every attribute tick.
8. **`ConfigureInput` as the session hook** is a name mismatch but avoids a new ABI export through `ScriptHost`; if the input wave renames or splits it, `BeginSession()` must move with it (a one-line comment at the call site says so).
9. **`DispBeginPlay` for graph-only classes** is assumed to fire; the implementer verifies in step 5 and falls back to `DispBind` if not.
10. **Managed unit tests are hand-run** unless the ctest wrapper works on the owner's machine; `GasPersistTest` and `SaveWorldTest` are the CI-visible coverage.
