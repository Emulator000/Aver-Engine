# Wave 3, feature #21: Environment Query System, Smart Objects, StateTree (design contract)

Status: DESIGN, frozen for the implementing agent. 2026-10-11. Scope: EQS (generators, tests, scoring, sync and time-sliced async runs, editor preview and debug draw), Smart Objects (definitions, claimable slots on placed entities, find/claim/use/release), StateTree (hierarchical state machine asset that runs on an entity, with tasks, conditions, transitions, evaluators and an editor), the BT/StateTree relationship, a managed (C#) seam for custom leaves, graph nodes, tests. Not in scope: saving AI runtime state (section 12), replication, multi-valued contexts, a navmesh (section 1).

## 0. Decisions in one screen

1. **Native, inside Synapse. No new module, no new third-party code.** Pure cores go in `Aver.Synapse` (no scene), scene joins in `Aver.Synapse.Scene`, the C ABI in `Aver.Synapse.Abi`, C# in `Aver.Synapse` (leaf) and `Aver.Framework`. This is the module's existing split (`docs/SYNAPSE.md` section header, `modules/synapse/CMakeLists.txt`). No `module.json` is created; two descriptions are edited (section 11).
2. **One rule decides where each system lives.** `Aver.Synapse.Scene` is STATIC, so the exe and `Aver.Synapse.Abi.dll` each carry their own copy of every process-global (`btSystem()` in the host is not the one in the DLL; that is why the blackboard needs a relay, `framework_blackboard_abi.h:34-45`, and hearing needs a callback, `GameSystemsWiring.hpp:45-55`). Therefore: **EQS and Smart Objects live in `SynapseAi` (the DLL's single instance)**, reachable from C# by direct `aver_syn_*` P/Invoke and from behaviour trees through `SynapseAi::registerBehaviors`; **StateTree lives in `BtSystem` (host side)** because it must share the BT's boards, teams, hearing sink, debug view and `CSynapseBehavior`, reachable from C# through the existing blackboard relay (extended with brain ops). Two copies of a pure function (`tickBt`, `tickStateTree`) are harmless; two copies of a registry are not.
3. **StateTree is another kind of brain, not a parallel system.** `CSynapseBehavior::treeAssetId` (`SynapseBt.hpp:35`) already means "the brain asset"; `.ocbt` and `.ocst` both load through `BtSystem::loadBrain(path)` (by extension). A StateTree reuses, unchanged: the `BtRegistry` (tasks and evaluators are registered **Action leaves**, conditions are registered **Condition leaves**, `Bt.hpp:30-65`), the `Blackboard` (the tree's parameters ARE a `BbSchema`, `Blackboard.hpp:85`), blackboard expressions (`bbParseExpr`, `BtDecorator`, `Bt.hpp:78-93`), the BT editor's panels, the AI debug sink. **Both are available and either hosts the other**: built-in task `RunBehaviorTree` (a StateTree state runs a `.ocbt`) and built-in leaf `RunStateTree` (a BT node runs a `.ocst`), both registered by `registerBuiltinBehaviors`.
4. **Three text formats** (`.oceqs`, `.ocso`, `.ocst`), each `HEADER <n>`, `#` comments, line-specific errors, exact writers (`parse(write(x)) == x`, second write byte-identical), the `.oclanes` rules (`FORMAT_SPECS.md` 11c). Text, not AVR1: diff-friendly, hand-editable, small. One shared tokenizer, `TextRecords` (2.1).
5. **Smart objects are a component plus an asset.** `CSmartObject` (registered in `Aver.Synapse.Scene`, a `String` field `asset` naming an `.ocso`) on any entity; the asset defines slots in the entity's local space. A slot goes Free -> Claimed -> Occupied -> (Cooldown) -> Free. Behaviours attached to a slot are an event, a `.ocbt` or a `.ocst`, run on the user.
6. **EQS is evaluated by a pure state machine** (`EqsRun`) that a system steps under a budget. A host environment struct of function pointers (the `bakeNav` probe pattern, `NavBake.hpp:36-44`) supplies nav, physics and entity lookups, so the whole algorithm tests against fakes.
7. **Navigation is a 2.5D grid, so "navmesh samples" are walkable grid cells.** The nav generator and the path test work on `OcNavData` (`OcNav.hpp:45`). Stated plainly because the brief said navmesh; a polygon navmesh is a separate project (`docs/SYNAPSE.md` section 1).
8. **Tags are strings with dotted-prefix matching inside Synapse** (`Tags.hpp`), names across every boundary. The C# `GameplayTag` (`Gas/GameplayTags.cs`) is the managed front-end (it passes `.Name`); ids are never shared because each module copy interns separately. The 32-bit `CTags` mask (`Components.hpp:59`) stays the cheap actor filter.
9. **A managed seam for custom leaves** (C# conditions, tasks, EQS tests): one `aver_syn_managed_*` dispatch, the timers precedent (`framework_timers_abi.h:25-28`). A registered name becomes a normal BT/StateTree leaf by that name.
10. **Editor.** StateTree: a new `StateTreeEditor` tab that composes parts **extracted from `BtGraphEditor`** (tree layout, blackboard schema panel, debug entity picker), so the BT editor is refactored, not copied. EQS: a form tab with a "Run on selection" preview. Smart objects: Content Browser entry and text editing (the `.ocgas` precedent), with viewport slot gizmos through the AI debug sink. Three new debug categories.
11. **Graph nodes for every C# capability** (section 9): table rows in a new `GraphAiQueryNodeTable.cs`, palette mirror `GraphNodeDefsAiQuery.hpp`. Graph events for completion use the existing notify seam (`ScriptHost::graphFire`), which carries no payload (`docs/SYNAPSE.md` section 5), so results are read with pure nodes.
12. **Nothing here is rendered except debug lines** through the existing `drawLines` (`SandboxRender.cpp:1965-1975`), identical on D3D12 and Vulkan.

## 1. What exists today (file:line), and what is missing

| Need | Exists | Missing |
|---|---|---|
| Brain assets | `.ocbt` BtAsset: tree + `BBSC` schema + decorators (`BtAsset.hpp:21-37`); `BtSystem::loadTree/reloadTree` (`SynapseBt.hpp:79-83`), per-entity boards, teams, `watch/debugView` (`:60-130`), tick (`SynapseBt.cpp:271`) | A second asset kind; **nothing assigns a BT to an entity from the editor, a graph or C#**: `CSynapseBehavior.treeAssetId` is a bare I64 hash of a path (`SynapseBt.cpp:113-122`) and the only attach/load callers are tests (grep `loadTree` outside synapse.scene and tests: none) |
| Leaves | `BtRegistry` named Condition/Action fns with `onAbort` (`Bt.hpp:30-65`); 7 built-ins (`SynapseBt.cpp:454-464`); 11 tactics leaves bound to `SynapseAi` (`SynapseAi.cpp:218-229`, installed through `aver_syn_ai_register_behaviors`, `GameSystemsWiring.hpp:76`) | C# cannot register a leaf (no ABI); no sub-tree leaf |
| Blackboard | typed keys, scopes, stamps, observers, `bbParseExpr` (`Blackboard.hpp`); C# `entity.Blackboard` (`Blackboard.cs:51`); relay op codes `kBbRelay*` (`SynapseBt.hpp:163-170`) | brain control ops |
| Navigation | grid, `nearestWalkable`, `findPath` with O(1) region refusal (`Nav.hpp:29,81`), `OcNavData::regionId` (`OcNav.hpp:32`); roots hold `gameNav_` / `nav_` and pass `const OcNavData*` into `tickAi` (`GameTick.hpp:174`, `GameApp.cpp:2069`, `SandboxApp.cpp:2314`) | any query over it other than path-following; C# has no nav access at all |
| World queries | `aver_phys_raycast`, `aver_phys_sphere_cast` (`physics_abi.h:570-590`), `aver_phys_capsule_cast` (`physics_queries_abi.h:71`); perception uses a ray (`docs/SYNAPSE.md` section 3); `World::count()/at(i)`, `worldMatrix`, `CTags`, `CName` | actors-of-class query reachable from Synapse (`aver_fw_class_of`, `framework_abi.h:177`, is Framework-side) |
| Claims | `CoverReservations` owner/TTL/moves-previous claim (`Cover.hpp:66-86`) | generic slot claims; kept as is, not rewritten (open question 6) |
| Debug draw | `AiDebugSink` categories, providers (`AiDebug.hpp:19-26,46`); feeders in `AiDebugOverlay.cpp`; Show > AI menu (`SandboxShell.cpp:2148-2153`) | EQS items, smart-object slots, StateTree state labels |
| Editors | `BtGraphEditor` (tree canvas, node/blackboard/debug panels, 1166 lines), registered first (`SandboxApp.cpp:446-450`); `AnimStateMachineEditor` (flat states, `.ocasm`) | StateTree and EQS tabs |
| Tags | `CTags` mask; C# `GameplayTag` hierarchy (wave 2, GAS) | native hierarchical match |
| Callbacks into C# | timers/events single dispatch fn (`framework_timers_abi.h:25-28,76-79`) | the same for AI leaves and EQS tests |

Verified absent: no `Eqs`, `SmartObject` or `StateTree` symbol anywhere outside `.claude/worktrees` and `third_party`.

**Anchors an implementer must respect.** `registerBehaviors` registers into the HOST's `BtRegistry` through a pointer (`SynapseAi.cpp:218`); DLL code sets `CSynapseAgent` goal fields directly (`requestGoal`, `SynapseAi.cpp:24-33`) because the component is shared world memory while `agentSystem()` is per-module; component types are looked up by name (`w.componentId("CSynapseAgent")`, `SynapseAi.cpp:209`). New DLL code follows both.

## 2. Shared foundations

### 2.1 `TextRecords` (pure, `Aver.Synapse`)

`modules/synapse/include/aver/synapse/TextRecords.hpp`, `src/TextRecords.cpp`.

```cpp
namespace aver::synapse {
struct TextRecord {
    u32 line = 0;                                   // 1-based
    std::string keyword;                            // upper-cased first token
    std::vector<std::string> args;                  // remaining tokens without '=' (quotes removed)
    std::vector<std::pair<std::string, std::string>> kv;  // tokens "key=value" (key lower-cased, value unquoted)
    std::string rest;                               // raw text after the keyword, trimmed, comments removed
    std::string get(std::string_view key, std::string_view fallback = {}) const;  // first kv match
    bool has(std::string_view key) const;
};
// Splits `text` into records. The first non-blank non-comment line must be "<header> <version>".
// Tokens split on whitespace; a double-quoted token (or the value after '=') may contain spaces and
// the escapes \" and \\; '#' outside quotes starts a comment; CRLF is fine. False with `why` =
// "line N: ..." on a missing/repeated/unknown header or version, or an unterminated quote.
bool readTextRecords(std::string_view text, std::string_view header, int version,
                     std::vector<TextRecord>& out, std::string* why);
std::string quoteToken(std::string_view s);        // quotes only when it contains space, '"', '#', '=' or is empty
std::string fmtF32(f32 v);                         // shortest text that reads back as the same f32 (std::to_chars), as OcLanes' writer
bool parseF32(std::string_view s, f32& out);       // finite only
}
```

Every parser below checks its own records and returns `"<path-or-name>:<line>: <message>"` on the first problem; a failed parse leaves the output empty.

### 2.2 `Tags.hpp` (pure, header-only)

```cpp
bool tagNameValid(std::string_view);                          // [A-Za-z][A-Za-z0-9_]*(.[A-Za-z][A-Za-z0-9_]*)*, <= 128 chars
bool tagMatches(std::string_view tag, std::string_view query);// tag == query, or tag starts with query + '.'
struct TagList { std::vector<std::string> v;                  // <= 16 entries
    bool hasAny(const TagList& q) const; bool hasAll(const TagList& q) const; bool hasNone(const TagList& q) const; // hierarchical (an entry of *this matches a query entry)
    static TagList parse(std::string_view csv);               // "A.B, C" ; invalid names dropped
};
```

### 2.3 `AiPaths.hpp` (pure)

`void setAiContentRoot(std::string)`, `std::string resolveAiAsset(const std::string& p)`: absolute paths and paths that exist as given are returned unchanged, otherwise `root + "/" + p`. Asset paths in components, `.ocso`, `.oceqs`, `.ocst` and graph attributes are content-relative with `/`. The wiring sets BOTH copies (host and DLL, 7.2).

### 2.4 Notify seam and mirrored blackboard keys

`SynapseAi` holds a `NotifyFn` (`SynapsePerception.hpp`, same signature as `animNotify`) installed by `aver_syn_set_notify`. Events are queued while an API call or tick runs and flushed at the end of the outermost call (depth guard 8, then dropped with one warning) so a graph handler that calls back never sees half-applied state. Event names (fired through `graphFire`, which carries no payload): on the querier `EqsFinished`, `EqsFailed`; on the user `SmartObjectClaimed`, `SmartObjectUsed`, `SmartObjectReleased`; on the object entity `SmartObjectSlotClaimed`, `SmartObjectSlotUsed`, `SmartObjectSlotReleased`; plus the custom name of a slot's `USE event`. Claim data is mirrored to the user's board, defined on the fly (agent scope), cleared on release when no claim remains (the `Heard*` precedent, `docs/BLACKBOARD_BT.md` section 1): `SmartObject` (Entity), `SmartObjectSlot` (Int), `SmartObjectHandle` (Int), `SmartObjectPos` (Vec3), `SmartObjectEntry` (Vec3), `SmartObjectYaw` (Float, degrees, world). The board is `reg_->boardFor(user)` where `reg_` is the registry pointer `registerBehaviors` stored.

### 2.5 The managed seam (C# custom leaves and EQS tests)

Lives in `synapse_query_abi.h` (section 8):

```c
#define AVER_SYN_MANAGED_CONDITION 0   /* Condition leaf: out.status 0/1 */
#define AVER_SYN_MANAGED_ACTION    1   /* Action leaf tick: out.status 0 running, 1 success, 2 failure */
#define AVER_SYN_MANAGED_ABORT     2   /* the running Action was aborted (state exit, observer abort) */
#define AVER_SYN_MANAGED_EQS_TEST  3   /* EQS custom test for one item: out.value */
typedef struct AverSynManagedCall {
    int32_t structBytes;      /* sizeof, checked on both sides */
    int32_t kind;             /* AVER_SYN_MANAGED_* */
    int32_t nameId;           /* from aver_syn_managed_register */
    int32_t subject;          /* entity running the leaf, or the EQS querier */
    float   dt;               /* ACTION: frame delta */
    float   elapsed;          /* ACTION: seconds since the first tick of this run */
    int32_t first;            /* ACTION: 1 on the first tick of a run (elapsed == 0) */
    int32_t status;           /* OUT: condition result / action status */
    float   p[4];             /* node params */
    const char* text;         /* node stringParam, "" when none; valid during the call */
    float   pos[3];           /* EQS_TEST: item position */
    int32_t itemEntity;       /* EQS_TEST: item entity or 0 */
    float   ctx[3];           /* EQS_TEST: the test's context position */
    int32_t ctxEntity;
    float   value;            /* OUT, EQS_TEST: the raw value */
} AverSynManagedCall;
typedef int32_t (*aver_syn_managed_fn)(AverSynManagedCall* call, void* user);   /* returns 1 when handled */
AVER_SYN_API int32_t aver_syn_managed_set_dispatch(aver_syn_managed_fn fn, void* user);  /* null clears; checks structBytes via the call itself */
AVER_SYN_API int32_t aver_syn_managed_register(int32_t kind, const char* name);   /* returns nameId > 0; kind 0/1 also registers the leaf `name` into the host BtRegistry */
AVER_SYN_API void    aver_syn_managed_clear(void);                                /* script unload: drops every managed name; leaves return Failure/false afterwards */
AVER_SYN_API int32_t aver_syn_managed_installed(void);                            /* 1 when a dispatch fn is set */
```

Native: `SynapseManaged` (Synapse.Scene) keeps `vector<Entry{kind,name,id,active}>`. `register` for kind 0/1 registers a trampoline (`user` = the entry) on `reg_`, or queues the name until `registerBehaviors` runs. Trampolines build the call struct, invoke the dispatch fn, and map the result; an inactive entry or null dispatch returns Failure/false (never throws, never crashes). Action trampolines use the `elapsed` scratch slot: `first = (elapsed <= 0)`, set `elapsed = max(elapsed, 1e-6) + dt`, and the managed side sees the true seconds. The abort trampoline is the `BtActionAbortFn`. A name that clashes with an existing native leaf is refused (returns 0).

C# (`Aver.Synapse/ManagedAi.cs`, public; needs `AllowUnsafeBlocks` true in `Aver.Synapse.csproj`, the one csproj edit): `ManagedAi.Startup()` installs an `[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]` thunk that wraps everything in try/catch (an exception becomes Failure/false plus one logged line per name), `Shutdown()` calls `aver_syn_managed_clear` and `aver_syn_managed_set_dispatch(null)`. API:

```csharp
public enum AiStatus { Running = 0, Success = 1, Failure = 2 }
public struct AiTaskContext { public int Entity; public float DeltaTime, Elapsed; public bool First, Aborting; public float P0, P1, P2, P3; public string Text; }
public struct EqsTestContext { public int Querier; public float X, Y, Z; public int ItemEntity; public float CtxX, CtxY, CtxZ; public int CtxEntity; public float P0, P1, P2, P3; public string Text; }
public delegate AiStatus AiActionFn(in AiTaskContext c);
public delegate bool AiConditionFn(in AiTaskContext c);
public delegate float EqsTestFn(in EqsTestContext c);               // raw value; filtering and scoring come from the asset (min/max/want/weight)
[AttributeUsage(AttributeTargets.Method)] public sealed class AverAiActionAttribute(string name) : Attribute { public string Name { get; } = name; }
[AttributeUsage(AttributeTargets.Method)] public sealed class AverAiConditionAttribute(string name) : Attribute { ... }
[AttributeUsage(AttributeTargets.Method)] public sealed class AverEqsTestAttribute(string name) : Attribute { ... }
public static class ManagedAi {
    public static bool RegisterAction(string name, AiActionFn fn);       // false if the name is taken
    public static bool RegisterCondition(string name, AiConditionFn fn);
    public static bool RegisterEqsTest(string name, EqsTestFn fn);
    public static int  ScanAssembly(System.Reflection.Assembly asm);     // registers every public static method carrying an attribute with the matching signature; returns the count; a bad signature logs and skips
    public static void Startup(); public static void Shutdown();
    public static IReadOnlyList<ManagedLeaf> Collect(System.Reflection.Assembly asm);  // no native calls; used by tests
}
```

A registered action is used in an asset by its own name (`Action PatrolStep` in a `.ocbt`; `TASK PatrolStep p0=3` in an `.ocst`). Per-entity task state lives in the user's own dictionary (documented); `First` and `Aborting` bracket a run.

Bridge wiring (`HostBridge.cs`, delimited additions, section 11): `ManagedAi.Startup()` in `SetupManagedActors` (`:1011` beside `GameplayAbilities.Startup()`), `ManagedAi.ScanAssembly(asm)` after `GameplayAbilities.RegisterAssembly(asm)` (`:908`), `ManagedAi.Shutdown()` in `DrainAndUnload` (`:295`, same try/catch shape; the assembly's delegates are script-context objects that must not outlive the unload).

## 3. Environment Query System

### 3.1 Concepts

A **query** (`.oceqs`) is one or more **options**. An option is one **generator** (produces **items**: a position, optionally an entity, optionally tags) and an ordered list of **tests**. A test computes a raw **value** per item; it may **filter** (reject items outside a range or a wrong boolean) and/or **score** (value normalised to 0..1, curved, weighted). A **context** is a named position (and entity) a generator or test refers to. A **run** is one evaluation of a query for a querier; it is a resumable state machine, stepped under a budget (3.5). The result is a sorted item list.

### 3.2 `.oceqs` (text, new section 11e in `FORMAT_SPECS.md`)

```
OCEQS 1
DESC "Cover spots the player cannot see"        # optional
OUTPUT pos=CoverPos entity=CoverEntity score=CoverScore count=CoverCount   # optional: BT/StateTree leaf RunEqsQuery writes these keys (any subset)
RESULT best                                       # all (default) | best | top <n> | percent <p> | randompercent <p>
RUN firstvalid                                    # firstvalid (default) | all : how OPTIONs combine
CONTEXT target key=Target                         # name key=<BbKey>: resolve this context from the querier's board (Entity key -> entity + position, Vec3 key -> point)
OPTION
  GENERATOR ring centre=querier inner=300 outer=1500 rings=3 count=16 project=nav
  TEST trace from=target fromz=160 toz=60 want=0 purpose=filter
  TEST distance to=querier curve=inverse weight=2
  TEST path from=querier metric=length maxexp=4000 weight=1 curve=inverse
OPTION
  GENERATOR nav centre=querier radius=2000 stride=3
  TEST random seed=7 weight=0.2
```

Rules: `OCEQS 1` first; `OPTION` opens an option (at least one; at most 8); `GENERATOR` exactly once per option and before its tests; `TEST` at most 16 per option; every other record is top-level and precedes the first `OPTION`. Built-in context names: `querier` (the running entity), `target` (the request's target entity, or the board key declared by `CONTEXT target key=...`), `seen` (the querier's `CSynapsePerception.lastKnownTargetEntity`), `point` (the request's point). `CONTEXT <name> key=<BbKey>` declares a new lower-case context (`[a-z][a-z0-9]*`, <= 8 per file) usable as `from=`/`to=`/`centre=`. Unknown keywords, keys, enum values, a duplicate record, a missing generator, a numeric outside its range, a context that is never declared, or `RUN all` with `RESULT best` on two options are refused with `path:line: message`. A missing key takes the default in the tables. Numbers are finite decimals.

**Generators** (`GENERATOR <kind> k=v ...`). Common keys: `centre=<ctx>` (default `querier`), `project=none|nav|ground` (default `none`; `nav` snaps each point to the nearest walkable cell centre within 4 cells and drops those with none, then drops duplicates mapping to one cell; `ground` casts straight down from z+500 to 2500 cm and takes the hit z, dropping misses), `zoffset=<cm>` (default 0, applied after projection), `max=<n>` (default 2048, 1..16384; generation stops at the cap and the run records `truncated`).

| Kind | Keys (default, range) | Items |
|---|---|---|
| `grid` | `halfx` (500), `halfy` (500), `step` (100, >= 10) | square lattice about the centre; refused at parse if the count exceeds `max` |
| `ring` | `inner` (0), `outer` (800), `rings` (1, 1..32), `count` (12, 1..256), `arcstart` (0), `arcend` (360) degrees, `facing=<ctx>` | `rings` concentric circles from `inner` to `outer` (one ring = `outer`), `count` points per ring; angle 0 points along centre->facing (or +X); sweep `arcstart..arcend` |
| `around` | `radius` (400), `count` (8), `arc` (360), `facing=<ctx>` | one circle: sugar for `ring inner=radius outer=radius rings=1` |
| `nav` | `radius` (800), `stride` (2, 1..16 cells), `region=same|any` (same) | walkable cell centres (`cellToWorld`) within radius on a `stride` lattice; `same` keeps cells whose `regionId` equals the centre's snapped cell. Needs a nav grid; none -> run fails |
| `actors` | exactly one of `class=<Name>` / `tagbits=<mask>` / `named=<prefix>`, `radius` (0 = unlimited), `derived=1`, `excludequerier=1` | one item per matching entity at its world position. `class` goes through the host actor provider (7.2) and returns nothing without one; `tagbits` matches any bit of `CTags`; `named` is a `CName` prefix |
| `slots` | `tags=<csv>` (any), `matchall=0`, `radius` (1500), `free=1` | smart-object slots (section 4) near the centre whose requirements the request's `usertags` satisfy; item position = the slot's entry point, entity = the object, tags = the slot's tags |
| `points` | `list="x,y,z;x,y,z"` (<= 256) | the literal list (tests, authored spot sets) |

**Tests** (`TEST <kind> k=v ...`). Common keys: `purpose=filter|score|both` (both), `weight` (1; may be negative), `curve=linear|inverse|square|invsquare|sqrt|step` (linear), `lo`/`hi` (score normalisation bounds, default observed min/max of the surviving items), `min`/`max` (filter range on the raw value, default unbounded), `want=1|0` (boolean tests only; default 1), `name` (display only).

| Kind | Keys | Raw value |
|---|---|---|
| `distance` | `to=<ctx>` (querier), `metric=3d|2d|z|absz` (3d) | metric distance item<->context, cm |
| `dot` | `from=<ctx>` (querier), `ref=forward|up|toward:<ctx>|dir:x,y,z` (forward), `metric=3d|2d` (3d) | dot(normalise(item - from), ref direction), -1..1; `forward` = the context entity's world +X (row 0), +X when it has none; zero-length -> 0 |
| `trace` | `from=<ctx>` (querier), `fromz` (0), `toz` (0), `radius` (0 = ray, > 0 sphere cast), `gap` (10) | 1 when the segment from `from+fromz` to `item+toz` is clear up to `gap` cm short of the item, else 0 (boolean, `want=1` keeps visible). Hits on the context entity or the item entity do not block. Without physics every item reads 1 and the run logs one warning |
| `path` | `from=<ctx>` (querier), `metric=length|reachable` (length), `maxexp` (4000), `partial=fail|ok` (fail) | `findPath` length in cm (`Found`; `Partial` only with `partial=ok`); `Unreachable` (O(1) region refusal), `OffMesh`, no nav: the item is rejected whatever the purpose. `reachable` yields 1 and is boolean |
| `tag` | `names=<csv>` or `bits=<mask>`, `match=any|all|none` (any) | boolean: `names` against the item's tags (slot items; hierarchical), `bits` against the item entity's `CTags` |
| `random` | `seed` (0) | uniform [0,1) from `hash(seed, item index)`; deterministic |
| `custom` | `name=<managed test>`, `p0..p3`, `text` | whatever the registered `EqsTestFn` returns (2.5); an unregistered name rejects every item and logs once |

An unresolved context rejects every item for a filter-purpose test and contributes 0 for a score-only test; an unresolved generator `centre` fails the run with the reason stored.

### 3.3 Evaluation (exact)

1. Options in file order. **Generate** (1 unit per item).
2. **Test order** = declaration order stably sorted by cost class: `random`, `tag`, `dot`, `distance` = 0; `custom` = 1; `trace` = 2; `path` = 3. A test costs `{1, 2, 8, 40}` units per item by class. Each test runs over the items still alive, in generation order; a rejected item skips every later test (so expensive tests see few items).
3. **Filter**: numeric tests keep `min <= v <= max`; boolean tests (`trace`, `tag`, `path metric=reachable`) keep `(v != 0) == (want != 0)`. `purpose=score` never filters, except the always-reject cases above.
4. **Score**: for each test with `purpose` score or both, over the surviving items: `lo, hi` = given bounds or the observed min and max; `n = hi == lo ? 1 : clamp((v - lo) / (hi - lo), 0, 1)`; `c` = curve(n) (`linear` n, `inverse` 1-n, `square` n^2, `invsquare` (1-n)^2, `sqrt` sqrt(n), `step` n >= 0.5 ? 1 : 0); item score = `sum(weight_i * c_i) / sum(|weight_i|)`, in [-1, 1]; with no scoring test every item scores 1.
5. Sort by score descending, ties by generation index ascending (stable, deterministic). Apply `RESULT`: `best` keeps 1; `top n`; `percent p` keeps `ceil(count * p / 100)`; `randompercent p` picks one item uniformly from that top slice with `hash(runId)`.
6. `RUN firstvalid`: the first option that leaves >= 1 item wins and later options never run; `all`: every option runs, each normalised on its own, the union is sorted together. No option yields an item: the run is **Finished with zero items** (not Failed). Failed is reserved for unusable input (unresolved generator centre, no nav for a nav generator, unknown query).
7. Time slicing never changes a result: the run is a sequential loop over (option, test, item) with a saved cursor, so any budget gives byte-identical output.

### 3.4 Pure core (`Eqs.hpp`, `EqsAsset.hpp`, `src/Eqs.cpp`, `src/EqsAsset.cpp` in `Aver.Synapse`)

```cpp
enum class EqsGen : u8 { Grid, Ring, Around, Nav, Actors, Slots, Points };
enum class EqsTestKind : u8 { Distance, Dot, Trace, Path, Tag, Random, Custom };
enum class EqsPurpose : u8 { Filter, Score, Both };
enum class EqsCurve : u8 { Linear, Inverse, Square, InvSquare, Sqrt, Step };
enum class EqsProject : u8 { None, Nav, Ground };
enum class EqsResultMode : u8 { All, Best, Top, Percent, RandomPercent };
struct EqsGenerator { EqsGen kind = EqsGen::Grid; std::string centre = "querier", facing; EqsProject project = EqsProject::None;
    f32 zOffset = 0; u32 maxItems = 2048; f32 halfX = 500, halfY = 500, step = 100, inner = 0, outer = 800, arcStart = 0, arcEnd = 360, radius = 800;
    u32 rings = 1, count = 12, stride = 2; bool sameRegion = true, derived = true, excludeQuerier = true, matchAll = false, freeOnly = true;
    std::string className, namePrefix; u32 tagBits = 0; TagList slotTags; std::vector<Vec3> points; };
struct EqsTest { EqsTestKind kind = EqsTestKind::Distance; std::string name, from = "querier", to = "querier", ref = "forward", metric, custom, text, refCtx;
    Vec3 refDir{1, 0, 0}; EqsPurpose purpose = EqsPurpose::Both; EqsCurve curve = EqsCurve::Linear;
    f32 weight = 1, lo = 0, hi = 0, min = -INFINITY, max = INFINITY, fromZ = 0, toZ = 0, radius = 0, gap = 10, p[4] = {};
    bool hasLo = false, hasHi = false, want = true, partialOk = false; u32 maxExp = 4000, seed = 0, bits = 0; TagList tags; int match = 0; };
struct EqsOption { EqsGenerator gen; std::vector<EqsTest> tests; };
struct EqsQuery { std::string name, desc; std::vector<EqsOption> options; bool runAll = false; EqsResultMode mode = EqsResultMode::All; u32 modeN = 0; f32 modeP = 0;
    std::vector<std::pair<std::string, std::string>> contexts;   // declared name -> board key
    struct Output { std::string pos, entity, score, count; } out; };
bool parseEqsQuery(std::string_view text, const std::string& name, EqsQuery& out, std::string* why);
std::string writeEqsQuery(const EqsQuery&);                       // exact writer
bool loadEqsQuery(const std::string& path, EqsQuery& out, std::string* why);
bool saveEqsQuery(const std::string& path, const EqsQuery& in, std::string* why);
std::vector<std::string> validateEqsQuery(const EqsQuery&);        // non-fatal warnings (unused context, score test with no variation possible, ...)

struct EqsRunParams { u32 querier = 0, target = 0; Vec3 point; bool hasPoint = false; TagList userTags; u32 runId = 1; };
struct EqsItem { Vec3 pos; u32 entity = 0; u32 object = 0; i32 slot = -1; u32 tagSet = 0; f32 score = 0; u32 genIndex = 0; u8 option = 0; };
struct EqsResult { enum class Status : u8 { Finished, Failed } status = Status::Finished; std::string reason; std::vector<EqsItem> items;
    std::vector<f32> testValues;     // items.size() * testCount, NaN where the test did not run; testCount = tests of the winning option (all options: the max)
    std::vector<TagList> tagSets; u32 generated = 0, passed = 0, evaluations = 0; bool truncated = false; };

struct EqsContext { Vec3 pos; u32 entity = 0; bool ok = false; };
struct EqsEnv {                                    // every pointer optional; absent = documented degrade
    void* user = nullptr; const fmt::OcNavData* nav = nullptr;
    EqsContext (*context)(void* user, std::string_view name, const EqsRunParams&);
    bool (*forward)(void* user, u32 entity, Vec3& out);
    u32  (*actors)(void* user, const EqsGenerator&, const Vec3& centre, u32 querier, u32* out, u32 cap);
    u32  (*slots)(void* user, const EqsGenerator&, const Vec3& centre, const EqsRunParams&, EqsItem* out, TagList* tags, u32 cap);
    bool (*entityPos)(void* user, u32 entity, Vec3& out);
    u32  (*entityTagBits)(void* user, u32 entity);
    bool (*trace)(void* user, const Vec3& a, const Vec3& b, f32 radius, u32 ignoreA, u32 ignoreB);  // true = blocked
    bool (*ground)(void* user, const Vec3& p, f32 up, f32 down, f32& z);
    bool (*custom)(void* user, const EqsTest&, const EqsRunParams&, const EqsItem&, const EqsContext& ctx, f32& value);
};
class EqsRun {
public:
    EqsRun(const EqsQuery& q, EqsRunParams p, const EqsEnv& env);   // copies the query: the asset may be reloaded mid-run
    bool step(u32 maxUnits);          // false while unfinished; maxUnits 0 = unlimited; one call never splits one (test,item) evaluation
    bool done() const; const EqsResult& result() const; u64 unitsSpent() const; void cancel();
};
```

### 3.5 Scene join: `EqsSystem` (`Aver.Synapse.Scene`, `SynapseEqs.hpp/.cpp`; a member of `SynapseAi`)

```cpp
class EqsSystem {
public:
    u32  loadQuery(const std::string& path);           // fnv1a64-keyed cache of parsed queries (resolveAiAsset); 0 on failure (logged once)
    u32  registerQuery(const std::string& name, const EqsQuery&);   // tests, tools, the editor preview
    bool reloadQuery(const std::string& path);         // swaps in a re-saved file; runs already started keep their copy
    const EqsQuery* query(u32 id) const;
    u32  start(u32 queryId, const EqsRunParams&, i32 priority = 0);       // run handle (nonzero, generation-tagged); queued FIFO within priority, higher first
    u32  runSync(u32 queryId, const EqsRunParams&);                        // unlimited budget, finished before return
    enum class Status : u8 { Unknown, Queued, Running, Finished, Failed, Cancelled };
    Status status(u32 handle) const; const EqsResult* result(u32 handle) const;
    bool cancel(u32 handle); void release(u32 handle);
    void setBudget(f32 maxMs, u32 maxUnitsPerTick);      // defaults 1.0 ms and 0 (off); a test sets units only
    void tick(scene::World&, const fmt::OcNavData* nav, f32 dt, SynapseAi&);
    void reset();                                        // drops runs and results, keeps the query cache
    usize pending() const;
    const std::vector<EqsDebugRun>& debugRuns() const;   // last 8 finished runs (query name, querier, items, test names) for the overlay
};
```

Tick: serve queued runs in (priority desc, handle asc) order, `step(256)` repeatedly until the ms budget (checked every step, `steady_clock`) or unit budget is spent; at least one step per tick so a run always progresses. Finished results are kept `30 s` game time unless `release`d (cap 64 results; oldest unread evicted). A finished run raises `EqsFinished` (>= 0 items) or `EqsFailed` on the querier. A querier destroyed mid-run cancels its runs. Runs queued: <= 64 (more are refused with handle 0).

Environment implementation (all in `SynapseEqs.cpp`, DLL side): contexts resolve from the entity (`worldMatrix` row 3), the board (`reg_->boardFor`), `CSynapsePerception` (`a.perceptionType(w)`); `forward` = `m.m[0][0..2]`; `trace` uses `aver_phys_raycast` / `aver_phys_sphere_cast` (guarded `if(TARGET Aver.Physics)` exactly as `SynapsePerception`); `ground` the same raycast downward; `actors` by `tagbits`/`named` over `World::at(i)` plus the provider for `class`; `slots` from `SmartObjectSystem::findSlots`; `custom` calls the managed seam; the nav pointer is the one `tick` last received (cleared by `reset`, the `TacticsSystem::nav_` lifetime rule), and `runSync` before any tick has no nav.

**BT/StateTree leaves** (registered by `SynapseAi::registerBehaviors`, appended to the list at `SynapseAi.cpp:219-229`):
- Action `RunEqsQuery` (`stringParam` = query path, `params[0]` = timeout seconds, 0 = none, `params[1]` = priority). First tick starts a run (`elapsed` is the "started" flag, the `MoveTo` idiom); Running until done; on a finished run with >= 1 item writes the query's `OUTPUT` keys on the user's board (defined on the fly: Vec3, Entity, Float, Int) and returns Success, otherwise Failure; `onAbort` cancels the run. The run handle is kept in a `SynapseAi`-side map keyed by (subject, `&node`).
- Condition `EqsHasResult` (`stringParam` = query path): true when the last finished run of that query for the subject has >= 1 item and is younger than `params[0]` seconds (0 = any age).

### 3.6 Debug draw

`kAiDebugEqs = 1u << 6`. Feeder `feedEqs` in `AiDebugOverlay.cpp` reads `static_cast<SynapseAi*>(aver_syn_ai_instance())->eqs().debugRuns()`: a small sphere per item coloured green (best) to red (worst) by normalised score, filtered-out items are not kept (the run keeps only survivors plus a count), the best item a larger sphere with a line from the querier and a label `score` plus the per-test values (`name=value`). Shown for runs younger than 5 s. The editor preview (6.3) keeps its run on screen until replaced.

## 4. Smart Objects

### 4.1 `.ocso` (text, new section 11f)

```
OCSO 1
DESC "Park bench"                                  # optional
TAGS Furniture.Seat Furniture.Outdoor              # object tags (space or comma separated)
SLOT sit_left                                      # opens a slot; names unique, [A-Za-z_][A-Za-z0-9_]*, <= 32 slots
  POS -40 0 45                                     # local to the owner entity (cm, +X forward, +Y right, +Z up). Default 0 0 0
  YAW 0                                            # degrees; the user's facing relative to the owner's +X. Default 0
  ENTRY 80 0 0 180                                 # approach point: x y z [yaw]. Default = POS / YAW
  TAGS Activity.Sit                                # slot tags
  REQUIRES User.Humanoid                           # the user's tags must satisfy ALL of these (hierarchical)
  BLOCKEDBY User.Injured                           # and NONE of these
  COOLDOWN 3                                       # seconds the slot stays unavailable after release. Default 0
  CLAIMTTL 20                                      # a Claimed slot not Used within N s is released (Expired). Default 0 = never
  USE statetree Content/AI/Sit.ocst                # none (default) | event <Name> | bt <path> | statetree <path>
  RELEASE auto                                     # auto (default) | manual ; meaningful for bt/statetree only
END
```

Rules: `OCSO 1` first; `SLOT` ... `END`; `TAGS`/`DESC` before the first `SLOT` belong to the object, inside a slot to the slot; sub-records appear once; unknown record, duplicate slot name, bad number, negative cooldown/ttl, `USE` with a missing argument, or a tag that fails `tagNameValid` is refused with `path:line:`. Behaviour semantics: `none`/`event`: the slot stays Occupied until `Release` (an `event` raises its `Name` on the user at `Use`); `bt`/`statetree`: the behaviour runs on the user every tick from `Use`; with `RELEASE auto` the slot is released when it finishes (Success -> `Completed`, Failure -> `Failed`), with `manual` it stays Occupied after it finishes.

### 4.2 Pure core (`SmartObject.hpp`, `SmartObjectAsset.hpp`, sources in `Aver.Synapse`)

```cpp
enum class SoUse : u8 { None, Event, Bt, StateTree };
enum class SoSlotState : u8 { Free, Claimed, Occupied, Cooldown, Disabled };
enum class SoReleaseReason : u8 { Released, Completed, Failed, Expired, UserGone, ObjectGone };
enum class SoRefusal : u8 { None, NoSuchSlot, Disabled, NotFree, Tags, UserLimit, NoUser };
struct SoSlotDef { std::string name; Vec3 pos, entry; f32 yawDeg = 0, entryYawDeg = 0; bool hasEntry = false; TagList tags, requires, blockedBy;
    f32 cooldownSec = 0, claimTtlSec = 0; SoUse use = SoUse::None; std::string useArg; bool autoRelease = true; };
struct SoDef { std::string name, desc, path; TagList tags; std::vector<SoSlotDef> slots; };
bool parseSoDef(std::string_view text, const std::string& name, SoDef& out, std::string* why);
std::string writeSoDef(const SoDef&); bool loadSoDef(const std::string& path, SoDef&, std::string* why); bool saveSoDef(...);

struct SoWorldSlot { Vec3 pos, entry; f32 yawDeg = 0, entryYawDeg = 0; };      // resolved by the join from the owner's world matrix
struct SoQuery { Vec3 origin; f32 radius = 1500; TagList slotTags; bool matchAll = false; bool includeClaimed = false; TagList userTags; u32 max = 16; };
struct SoSlotRef { u32 object = 0; u32 slot = 0; };
class SmartObjectRegistry {
public:
    void setObject(u32 object, const SoDef* def, bool enabled);          // add or update; slot states survive an update with the same def
    void setWorld(u32 object, u32 slot, const SoWorldSlot&);              // the join calls this each tick for moved owners
    void removeObject(u32 object, std::vector<SoRelease>& released);      // releases every claim with ObjectGone
    u32  slotCount(u32 object) const; bool slotInfo(u32 object, u32 slot, SoSlotInfo& out) const;
    bool setSlotEnabled(u32 object, u32 slot, bool enabled);
    u32  find(const SoQuery&, SoSlotRef* out, f32* distOut, u32 cap) const;       // sorted by distance to origin, then object, slot
    u32  claim(SoSlotRef, u32 user, const TagList& userTags, f32 ttlOverride, SoRefusal* why);  // claim handle (nonzero), 0 = refused
    bool use(u32 claim); bool release(u32 claim, SoReleaseReason, SoRelease* out = nullptr);
    bool claimInfo(u32 claim, SoClaimInfo& out) const; u32 claimsOf(u32 user, u32* out, u32 cap) const;
    void releaseUser(u32 user, std::vector<SoRelease>& out);              // UserGone
    void tick(f32 dt, std::vector<SoRelease>& released);                   // claim TTLs, cooldown timers
};
```

Semantics: handles are `(generation << 12 | index)`, never reused within a generation, and become invalid on release (a stale handle fails cleanly). A user may hold <= 4 claims (`UserLimit`). `claim` checks, in order: slot exists, enabled, Free (Cooldown/Claimed/Occupied = `NotFree`), the user's tags satisfy `requires` (all) and none match `blockedBy` (`Tags`). `use` requires Claimed by the same handle. `release` of a slot with `cooldownSec > 0` enters Cooldown for that long. The registry has no entity or world types; ids are `u32`.

### 4.3 Scene join: `SmartObjectSystem` (`SynapseSmartObject.hpp/.cpp`, `SynapseAi` member)

```cpp
struct CSmartObject {                 // registered via World::registerComponent("CSmartObject"); String field + I32s only
    i64 asset = 0;                    // String field "asset": content-relative .ocso path (scene string pool, aver_scene_get_str)
    i32 enabled = 1;                  // I32 field
    i32 slotsFree = 0;                // read-only runtime mirror
};
class SmartObjectSystem {
public:
    u32  registerComponents(scene::World&);   u32 componentType() const;
    CSmartObject* attach(scene::World&, scene::Entity, const std::string& assetPath);   // real defaults applied (the attach() rule, docs/SYNAPSE.md section 6)
    // pass-throughs to the registry in entity terms, plus everything below
    u32  find(scene::World&, scene::Entity user, const Vec3& origin, const SoQuery&, SoSlotRef* out, u32 cap);
    u32  claim(scene::World&, scene::Entity user, SoSlotRef, const TagList& userTags, f32 ttl, SoRefusal*);
    bool use(scene::World&, u32 claim); bool release(scene::World&, u32 claim, SoReleaseReason);
    u32  findSlots(...);                       // the EQS `slots` generator
    void tick(scene::World&, f32 dt, SynapseAi&);   // sync, world transforms, TTL/cooldown, behaviours, dead user/object sweep, event flush
    void reset();
};
```

`tick`: (1) scan the `CSmartObject` pool: new components load their def (`loadSoDef(resolveAiAsset(path))`, cached by path, a bad file logs once and leaves the object unregistered), changed `asset`/`enabled` update, destroyed entities `removeObject`; (2) per object compute each slot's world transform from `worldMatrix(owner)`: `world = ownerM * local` (row-vector convention), `yaw` added to the owner's world yaw (atan2 of row 0); (3) `registry.tick`; (4) sweep claims whose user is no longer valid (`UserGone`); (5) run behaviours of Occupied slots: `bt` -> `loadBtAsset` + `compileBtAsset` once per path, `tickBt` against the user with a `BtRunningState` per claim and the user's board; `statetree` -> `tickStateTree` (5.4) with an instance per claim; a finished behaviour releases per `RELEASE`; `event` -> raised once at `Use`; (6) mirror claim keys and raise events (2.4). A dead user mid-behaviour calls the running leaf's abort.

Authoring a "placed object": add `CSmartObject` through Add Component (`SandboxPanels.cpp:2406` lists registered components) on any entity, or as a prefab/graph-class component (`COMPONENT SmartObject asset=Content/AI/Bench.ocso`, `FORMAT_SPECS.md` section 10; String fields are handled by `PrefabFields.cpp:89,161`). A bare `.ocworld` `PLACE` line cannot carry components (open question 5).

### 4.4 Leaves (registered by `SynapseAi::registerBehaviors`)

| Leaf | Kind | Params / string | Behaviour |
|---|---|---|---|
| `ClaimSmartObject` | Action | `stringParam` slot tags csv; `params[0]` radius (default 1500), `[1]` claim TTL (-1 = slot default), `[2]` 1 = match all tags | find nearest satisfying slot (user tags from the board key `UserTags` String if defined), claim it, mirror keys; Success, or Failure when none/refused |
| `MoveToSmartObject` | Action | `params[0]` arrive radius (default 60) | requires a held claim; `requestGoal` to the slot's entry point (the `InvestigateNoise` idiom, `SynapseAi.cpp:96-108`); Running until `Arrived`, Failure on `Failed` |
| `UseSmartObject` | Action | none | `use` the user's first claim at first tick; Running while the slot is Occupied by that claim; Success when released as Completed, Failure otherwise; `onAbort` releases (`Released`) |
| `ReleaseSmartObject` | Action | none | releases all the user's claims; Success |
| `HasSmartObjectClaim` | Condition | `params[0]` = 1: must be Occupied | true when the user holds a (used) claim |

## 5. StateTree

### 5.1 Model and the relationship with behaviour trees

A **StateTree** is a forest of **states** (nested, each with a unique name among siblings). At any time exactly one chain of states is **active**, root-most to leaf. Each active state runs its **tasks** (Action leaves) concurrently every update; **evaluators** (Action leaves, tree level) run first every update; **transitions** move the active chain; the **parameters** are the entity's blackboard. Versus a BT: persistent state and explicit transitions instead of re-evaluating from the root with composites. Versus `AnimStateMachineEditor`'s flat `.ocasm`: hierarchy, tasks, and blackboard conditions.

How they compose (both stay first-class): an entity's brain is `.ocbt` **or** `.ocst`. Built-ins registered by `registerBuiltinBehaviors` (so ten, not seven, `SynapseBt.hpp:177-189`): Action `RunBehaviorTree` (`stringParam` = `.ocbt`; runs it with its own `BtRunningState` against the same subject and board; Running while it runs; Success/Failure as the tree resolves; aborted -> state reset) and Action `RunStateTree` (`stringParam` = `.ocst`; nested instance; Running until it Succeeds/Fails; used in a BT node or in a StateTree task). State for these lives in the owning `BtSystem`, keyed by (subject, `&node`), pruned with dead subjects, depth guard 8 (a tree that nests itself deeper fails the leaf and logs).

### 5.2 `.ocst` (text, new section 11g)

```
OCST 1
DESC "Guard brain"                                  # optional
ONFINISH restart                                    # restart (default) | stop : what a brain does when the tree Succeeds/Fails
KEY Alert bool agent false                          # KEY <name> <bool|int|float|vec3|string|entity> <agent|shared> [<default>]  (BbKeyDef; vec3 default "x,y,z")
KEY Target entity agent
EVAL UpdateThreat p0=1500                           # evaluator: an Action leaf run every update before tasks; its status is ignored
STATE Patrol
  ENTER
    IF bb !Alert                                    # enter conditions; every IF line must pass
  TASK PatrolStep p0=3                              # TASK <Leaf> [p0..p3=<f32>] [s="string"]
  ON event:Alarm TO Combat                          # ON <tick|completed|failed|event:<Name>> TO <target>
STATE Combat
  COMPLETE none                                     # all (default when it has tasks) | any | none (default without tasks)
  TASK RunBehaviorTree s="Content/AI/Combat.ocbt"
  STATE Chase
    ENTER
      IF cond CanSeeTarget                          # IF cond <Leaf> [k=v...] ; IF not cond <Leaf> ...
    TASK MoveTo p0=0 p1=0 p2=0
    ON tick TO Combat.Search
      IF bb !CanSeeTarget
  END
  STATE Search
    TASK Wait p0=4
    ON completed TO Patrol
  END
  ON failed TO Patrol
END
```

Grammar: records are line based; `STATE <Name>` ... `END` nests (indent is cosmetic); inside a state, in any order: `GROUP` (not selectable as a leaf: only its children are), `DISABLED`, `COMPLETE`, `COMMENT "text"`, `ENTER` (opens an enter-condition list), `TASK`, `ON` (opens a transition), nested `STATE`. `IF` lines attach to the nearest preceding `ENTER` or `ON`; an `IF` elsewhere is refused. `IF bb <expr>` uses the `bbParseExpr` text (`key`, `!key`, `key op value`, ops `== != < <= > >=`); `IF cond <Leaf> k=v` names a registered Condition leaf with `p0..p3` and `s=`. Targets: a dot path from the roots (`Combat.Search`), `NEXT` (next selectable sibling; if none, bubbles as a completion of the parent), `SUCCEEDED`, `FAILED` (finish the whole tree). State names `[A-Za-z_][A-Za-z0-9_]*`, unique among siblings, and not `NEXT`/`SUCCEEDED`/`FAILED`; leaf names `[A-Za-z][A-Za-z0-9_]*`. Refused with `path:line:`: missing header, unknown record, `END` without `STATE` / unclosed `STATE`, duplicate state or key, unresolvable `TO` path, bad expression, unknown `COMPLETE`/`ONFINISH`/trigger, `p`-index outside 0..3, > 256 states, depth > 16, > 16 tasks per state, > 16 transitions per state. Leaf names are NOT checked at parse (the registry is a runtime fact); `validateStateTree(asset, const BtRegistry*)` returns warnings for unknown leaves, unreachable states and keys used but not declared. Writer order: `DESC`, `ONFINISH` (omitted when default), `KEY`s, `EVAL`s, then states pre-order with `GROUP`, `DISABLED`, `COMPLETE` (omitted when default), `COMMENT`, `ENTER` + IFs, `TASK`s, `ON`s + IFs, children, `END`.

### 5.3 Semantics (exact)

`tickStateTree` per update, with `inst.status == Running`:
1. **Start** (first tick, or restart): bind the schema to the board, `select` from the roots: the first enabled root whose enter conditions pass and which can descend (a state with children must find a selectable child, recursively; a `GROUP` is only a path). No selectable state: status Failed. Entering resets each of the state's task slots (scratch 0, phase idle).
2. **Evaluators** in order; a Failure is logged once per evaluator.
3. **Tasks**: every active state root-most first, every task in order that is not yet resolved: `fn` Success -> task done; Failure -> task failed; Running keeps.
4. **State flags**: `failed(S)` = any task of S failed. `complete(S)`: mode `all` = >= 1 task and all done; `any` = some task done; `none` = never.
5. **Transitions**, scanning the active chain **leaf first, then ancestors**; within a state in file order; the first that fires wins. `tick` always; `completed` when `complete(S)`; `failed` when `failed(S)`; `event:X` when an event named X is pending. Its `IF` list must pass (all). Applying a transition ends the scan.
6. **Default bubbling** when nothing fired: a complete leaf behaves as `ON completed TO NEXT`; a failed leaf as `ON failed TO` its parent failing. `NEXT` with no selectable following sibling completes the parent (re-scan with the parent as the "leaf"); a root with no next completes the tree (**Succeeded**); a failure with no handler bubbles to the roots and finishes the tree (**Failed**). At most 8 bubbling steps per update.
7. **Applying** a target: `SUCCEEDED`/`FAILED` exit every state and finish. A state: compute the new chain by `select` descending from the target (enter conditions evaluated now; if none passes the transition fails and the scan CONTINUES with the next transition). States in the old chain but not the new are exited leaf first (a task still Running has its `onAbort` called); states in both stay running; a target that is itself active is exited with its descendants and re-entered (ancestors untouched). New states are entered root-most first.
8. **Events**: `sendEvent(name, i, f)` queues; every queued event is visible to step 5 of the NEXT `tickStateTree` call only, then dropped (<= 16 queued, extras dropped with one warning). On processing, board keys `EventPayload` (Int) and `EventPayloadF` (Float) are written (defined on the fly).
9. A transition applied by step 5 is not re-scanned until the next update, except bubbling (6). **Finish**: status Succeeded/Failed; `ONFINISH restart` begins again next update, `stop` stays terminal until `restart()`.

### 5.4 Pure core (`StateTree.hpp`, `StateTreeAsset.hpp`, sources in `Aver.Synapse`)

```cpp
enum class StTrigger : u8 { Tick, Completed, Failed, Event };
enum class StTargetKind : u8 { Goto, Next, Succeeded, Failed };
enum class StComplete : u8 { All, Any, None };
struct StCond { bool isBlackboard = true; bool negate = false; BtDecorator bb; fmt::OcBtNode leaf; };   // leaf.kind Condition
struct StTransition { StTrigger trigger = StTrigger::Tick; std::string event; std::vector<StCond> conds; StTargetKind target = StTargetKind::Next; std::string targetPath; i32 targetState = -1; };
struct StState { std::string name, comment; i32 parent = -1; std::vector<i32> children; bool group = false, disabled = false; bool completeExplicit = false; StComplete complete = StComplete::None;
    std::vector<StCond> enter; std::vector<fmt::OcBtNode> tasks; std::vector<StTransition> transitions; };
struct StateTreeAsset { std::string name, desc, path; BbSchema schema; std::vector<fmt::OcBtNode> evaluators; std::vector<StState> states /*pre-order, parents first*/; std::vector<i32> roots; bool restartOnFinish = true;
    std::string statePath(i32) const; i32 findPath(std::string_view) const; };
bool parseStateTree(std::string_view text, const std::string& name, StateTreeAsset& out, std::string* why);   // resolves targetState
std::string writeStateTree(const StateTreeAsset&);  bool loadStateTree(...); bool saveStateTree(...);
std::vector<std::string> validateStateTree(const StateTreeAsset&, const BtRegistry* registry = nullptr);
// structural edits for the editor (free functions, headless-testable, each returns the new index or -1 and re-resolves/renames targets):
i32  stAddState(StateTreeAsset&, i32 parent, std::string name); bool stDeleteState(StateTreeAsset&, i32 index);   // delete retargets transitions pointing into the subtree to NEXT and reports how many
bool stRenameState(StateTreeAsset&, i32 index, std::string name);   // rewrites every Goto path
i32  stMoveState(StateTreeAsset&, i32 index, i32 delta); i32 stReparent(StateTreeAsset&, i32 index, i32 newParent); StateTreeAsset stStarter();

enum class StStatus : u8 { Stopped, Running, Succeeded, Failed };
struct StateTreeInstance {
    StStatus status = StStatus::Stopped; std::vector<i32> active;                    // root-most first
    struct Task { f32 elapsed = 0; u8 phase = 0; /*0 idle 1 running 2 done 3 failed*/ };
    std::vector<std::vector<Task>> tasks;                                             // per state index, sized lazily
    struct Event { std::string name; i64 i = 0; f32 f = 0; }; std::vector<Event> events;
    u64 tickCount = 0; i32 lastFrom = -1, lastTo = -1; u64 lastTransitionTick = 0;
    bool trace = false;                                                              // editor: record per-task last status in `traceTasks`
    std::vector<std::vector<u8>> traceTasks;
    void sendEvent(std::string name, i64 i = 0, f32 f = 0); void restart(); void stop();
};
struct StateTreeRuntime { StateTreeAsset asset; std::string path; };
StStatus tickStateTree(const StateTreeAsset&, const BtRegistry&, i32 subject, f32 dt, StateTreeInstance&, Blackboard* board);
```

`tickStateTree` is pure (registry and board are arguments); the same function runs host-side in `BtSystem` and DLL-side in `SmartObjectSystem` / `RunStateTree`. Condition evaluation: `evaluateDecorator(d, board)` for `bb`, the registered `ConditionEntry::fn(subject, node, user)` for `cond`; an unregistered leaf is a Failure/false with one warning per name (the BT's rule). Actions resolve through `registry.findAction`; `onAbort` through the same entry.

### 5.5 `BtSystem` integration (`SynapseBt.hpp/.cpp`, AI-owned edits)

```cpp
u64  loadBrain(const std::string& path);                 // .ocbt -> loadTree; .ocst -> loadStateTree; anything else 0. id = fnv1a64(path), the existing identity scheme
u64  loadStateTree(const std::string& path);              // resolveAiAsset, parse, validate (warnings logged once), cache
u64  registerStateTree(const std::string& name, const StateTreeAsset&);
const StateTreeRuntime* findStateTree(u64 id) const;
bool setBrain(scene::World&, scene::Entity, const std::string& path);   // attaches CSynapseBehavior when missing (real defaults), loadBrain, store the id. THE assignment path that did not exist
i32  brainKind(scene::Entity) const;                      // 0 none, 1 BT, 2 StateTree
i32  brainStatus(scene::Entity) const;                    // 0 idle/stopped, 1 running, 2 succeeded, 3 failed (BT: from lastStatus)
bool stateTreeEvent(scene::Entity, std::string_view name, i64 i, f32 f);
bool brainRestart(scene::Entity); bool brainStop(scene::Entity);
void clearStateTrees();                                   // drops instances (level unload / Play stop), keeps cached assets
struct StateTreeDebugView { u64 treeId = 0; const StateTreeRuntime* tree = nullptr; const StateTreeInstance* state = nullptr; const Blackboard* board = nullptr; };
bool stateTreeDebugView(scene::Entity, StateTreeDebugView&) const;
std::string activeStatePath(scene::Entity) const;         // leaf path, "" when none
bool inState(scene::Entity, std::string_view path) const;  // path is in the active chain
```

`tick` (`SynapseBt.cpp:271`): for each `CSynapseBehavior` entity, `treeAssetId` resolves in `trees_` (BT, unchanged path) or in `stateTrees_`; the latter does `bindBoard` with the StateTree's schema, `syncPerception`, `tickStateTree` (trace on when `e == watched_`), and sets `lastStatus` (Running 0, Succeeded 1, Failed 2). `reloadTree(path)` handles both extensions (resets the instance and rebinds the schema). `prune` erases instances of entities without a brain. `entitiesUsing(world, id)` is unchanged and finds both. A `.ocst` assigned to an entity whose `CSynapseBehavior.teamId` names a team uses the team board for `shared` keys exactly as a BT does.

C ABI for C# (`modules/framework/include/aver/framework/framework_brain_abi.h`, `AVER_FW_BRAIN_ABI_VERSION 1`; definitions appended to `BlackboardAbi.cpp` beside `call()` so they share its one provider; no new install call, the provider the roots already install is extended with ops 6..13 in `blackboardRelay`, `SynapseBt.hpp:163`; a provider that predates them returns 0, which every function treats as "no brain"):

```c
#define AVER_FW_BRAIN_NONE 0
#define AVER_FW_BRAIN_BT 1
#define AVER_FW_BRAIN_STATETREE 2
#define AVER_FW_BRAIN_IDLE 0
#define AVER_FW_BRAIN_RUNNING 1
#define AVER_FW_BRAIN_SUCCEEDED 2
#define AVER_FW_BRAIN_FAILED 3
#define AVER_FW_BB_OP_BRAIN_SET     6   /* key = asset path */
#define AVER_FW_BB_OP_BRAIN_KIND    7   /* *i = kind */
#define AVER_FW_BB_OP_BRAIN_EVENT   8   /* key = event name, *i payload int, f[0] payload float */
#define AVER_FW_BB_OP_BRAIN_STATE   9   /* text = active leaf path */
#define AVER_FW_BB_OP_BRAIN_INSTATE 10  /* key = state path; returns 1 when in the active chain */
#define AVER_FW_BB_OP_BRAIN_STATUS  11  /* *i = status */
#define AVER_FW_BB_OP_BRAIN_RESTART 12
#define AVER_FW_BB_OP_BRAIN_STOP    13
AVER_FW_ABI int32_t aver_fw_brain_abi_version(void);
AVER_FW_ABI int32_t aver_fw_brain_set_asset(int32_t entity, const char* path);       /* 1 on success; .ocbt or .ocst */
AVER_FW_ABI int32_t aver_fw_brain_kind(int32_t entity);                               /* AVER_FW_BRAIN_* ; 0 also when no provider */
AVER_FW_ABI int32_t aver_fw_brain_status(int32_t entity);
AVER_FW_ABI int32_t aver_fw_brain_send_event(int32_t entity, const char* name, int32_t payloadInt, float payloadFloat);
AVER_FW_ABI int32_t aver_fw_brain_state(int32_t entity, char* buf, int32_t cap);      /* 1 and the dotted leaf path; 0 when none */
AVER_FW_ABI int32_t aver_fw_brain_in_state(int32_t entity, const char* path);
AVER_FW_ABI int32_t aver_fw_brain_restart(int32_t entity);
AVER_FW_ABI int32_t aver_fw_brain_stop(int32_t entity);
```

### 5.6 C# (`Aver.Framework/Ai/Brain.cs`)

```csharp
internal static class FwBrain { /* DllImport("Aver.Framework") of the functions above, UTF-8 strings */ }
public enum BrainKind { None = 0, BehaviourTree = 1, StateTree = 2 }
public enum BrainStatus { Idle = 0, Running = 1, Succeeded = 2, Failed = 3 }
public readonly struct Brain {
    public BrainKind Kind { get; } public BrainStatus Status { get; }
    public bool SetAsset(string contentPath);                                  // .ocbt or .ocst; attaches the component if needed
    public bool SendEvent(string name, int payloadInt = 0, float payloadFloat = 0f);
    public string ActiveState { get; }                                         // "Combat.Chase", "" when none
    public bool IsInState(string path);
    public bool Restart(); public bool Stop();
}
public readonly partial struct Entity { public Brain Brain => new(Handle); }   // beside Entity.Blackboard (Blackboard.cs:116)
internal static class BrainGraph { ... }                                       // graph bodies, section 9
```

### 5.7 Built-in leaves added for StateTree use

Host registry (`registerBuiltinBehaviors`): `RunBehaviorTree`, `RunStateTree` (5.1) only. A condition "is in state X" is deliberately not added (the `IF bb` and `AN_BrainIsInState` routes cover it); keep the leaf surface small.

## 6. Editor

### 6.1 StateTree tab (`sandbox/src/StateTreeEditor.hpp/.cpp`, `.ocst`)

Factory `makeStateTreeEditor` registered in `SandboxApp.cpp` next to the BT editors (`:446-450`). Layout, from left: **canvas**, **side panel** with tabs **State**, **Parameters**, **Evaluators**, **Debug**.
- Canvas: the tree laid out top-down by the same algorithm the BT canvas uses; a box per state with header (name, group/disabled badge), one line per task (`Leaf p0.. "s"`), one `ON trigger -> target` line per transition; parent->child edges as in `drawCanvas`; each `Goto` transition also drawn as a dashed curve from the source box to the target box, labelled with the trigger. Pan by dragging empty space, Ctrl+wheel zoom, right-click a state for add child/sibling, delete, move, make group (the BT editor's interactions, `docs/BLACKBOARD_BT.md` section 4).
- **State** tab: name, group, disabled, complete mode, comment; enter conditions (add/remove; `bb` rows reuse the BT decorator editor widgets, `cond` rows pick a leaf name); tasks (leaf name combo fed by the registry, `p0..p3`, string); transitions (trigger, event name, target combo of every state path plus `NEXT`/`SUCCEEDED`/`FAILED`, condition list; order = priority, move up/down).
- **Parameters** tab: the shared schema panel extracted from `BtGraphEditor::drawBlackboardPanel` (`BtGraphEditor.cpp:926-1042`) with the live column and change flashing. **Evaluators**: the same leaf-row widget as tasks. **Debug**: the shared entity picker (`:1042-1109`); when an entity runs this tree the active chain is highlighted, running tasks marked, the last transition shown as `From -> To @ tick`, status and pending events listed.
- Edits snapshot-undoable (`SnapshotUndo<StateTreeAsset>`), Save writes `writeStateTree` and calls `btSystem().reloadTree(path)`; warnings from `validateStateTree` (with `&btSystem().registry()`) show in the toolbar. `New State Tree` in the Content Browser writes `stStarter()` (two states `Idle` -> `Active` on a `Wait`), the `cbCreateBehaviourTree` shape (`SandboxContentBrowser.cpp:641-654`).

### 6.2 Refactor of the BT editor (the "upgrade")

New `sandbox/src/AiEditorParts.hpp/.cpp` (headless-testable pure parts plus `#if AVER_WITH_IMGUI` draw functions): (a) `layoutTree(const std::vector<i32>& parents, const std::vector<f32>& heights, const TreeLayoutParams&)` returning boxes (the body of `btLayout`, `BtGraphEditor.cpp:62-110`, generalised; `btLayout` becomes a wrapper so `BtGraphEditorTest` is unchanged), (b) `drawSchemaPanel(BbSchema&, LiveBoard*, SchemaEdits&)`, (c) `drawEntityPicker(...)`/`LiveBoard` (flash bookkeeping, `lastStamp_`/`flash_` today), (d) canvas pan/zoom state. `BtGraphEditor` is edited to call them; behaviour does not change, and `BtGraphEditorTest` plus its headless build (AVER_WITH_IMGUI undefined) must stay green.

### 6.3 EQS tab and the rest

`EqsEditor` (`sandbox/src/EqsEditor.hpp/.cpp`, `.oceqs`): a form over `EqsQuery`: top fields (desc, result mode, run mode, outputs, contexts), an option list, per option a generator form (fields shown per kind) and a reorderable test list (kind combo, per-kind fields, purpose/weight/curve/range), snapshot undo, Save = `writeEqsQuery` + `eqs().reloadQuery`. **Run on selection** button: `registerQuery` the in-memory edit, `runSync` with the selected entity as querier (and the entity the user picks as target), results table (rank, score, position, per-test values) and the run shown in the viewport via `kAiDebugEqs` until replaced. `New EQS Query` writes a commented starter (one `ring` option with a `distance` score test).

`.ocso`: type-table entry, `New Smart Object Definition` (commented starter bench with two slots). `.oceqs` and `.ocst` open in their own tabs; `.ocso` has no tab, so ONLY `".ocso"` is added to `cbIsSourceFile` (`SandboxContentBrowser.cpp:315-319`, the `.ocgas` precedent) and a double-click opens it in the user's IDE. Slot gizmos: category `kAiDebugSmartObjects = 1u << 7`; per slot a small arrow at the slot position along its yaw (green Free, yellow Claimed, red Occupied, grey Cooldown/Disabled), a dashed line to the entry point, label `object.slot [user]`.

Debug options: `AiDebugOptions` gains `eqs`, `smartObjects`, `stateTree` (`kAiDebugStateTree = 1u << 8`; `kAiDebugAll` becomes `0x1FF`, `AiDebug.hpp:26`); `Show > AI` gains three items beside `Cover` (`SandboxShell.cpp:2153`). StateTree feeder: label above each entity running a `.ocst` with the active leaf path, task statuses and the last transition (fades after 2 s), the BT-state feeder's shape (`AiDebugOverlay.cpp:162-196`).

## 7. Runtime and packaged-game wiring

### 7.1 Order of work per frame

Unchanged call sites. `tickAi` (`GameTick.hpp:174-190`) runs `agentSystem().tick`, `perceptionSystem().tick`, `aver_syn_ai_tick(nav, dt)`, `btSystem().tick`. Inside `SynapseAi::tick` (`SynapseAi.cpp:202`) the order becomes: hearing, tactics, crowd, **smart objects**, **EQS** (so a result is ready for the BT/StateTree that tick runs right after). StateTrees tick inside `btSystem().tick`. `SynapseAi::reset()` (`SynapseAi.hpp:45`) also resets both new systems; `resetGameSystems` already calls `aver_syn_ai_reset` and `btSystem().clearBlackboards()`; it additionally calls `btSystem().clearStateTrees()` (instances only, assets stay cached) via one added line.

### 7.2 Host wiring (`Runtime/include/aver/game/GameSystemsWiring.hpp`, the one copy both roots call)

Added, all under `AVER_WITH_SYNAPSE_AI` (module-guard audit stays green): in `registerGameSystems` `aver_syn_set_actor_provider(&actorsOfClass, nullptr)` (guarded also by `AVER_MODULE_FRAMEWORK`; the function walks `World::at(i)`, tests `aver_fw_class_of(e)` against the name and, when `derived`, its `aver_fw_class_parent` chain); a new `inline void setAiContentRoot(const std::string& dir)` that sets both `synapse::setAiContentRoot(dir)` and `aver_syn_set_content_root(dir.c_str())`; a new `inline void installAiNotify(NotifyFn fn, void* user)` calling `aver_syn_set_notify`. The roots add one line each: `game::setAiContentRoot(project_.contentDir());` beside `declareGraphClasses(project_.contentDir())` (`GameApp.cpp:1193`, `SandboxApp.cpp:1016`) and `game::installAiNotify(&animNotify, this);` beside `btSystem().setNotifySink` (`GameApp.cpp:1250`, `SandboxApp.cpp:988`). `registerComponents` for `CSmartObject` goes through `aver_syn_ai_register` (`SynapseAiAbi.cpp`, which calls `SynapseAi::registerComponents`), so no root registers it. The packaged game needs nothing else: the DLL is already shipped (`game.allowlist:36`), assets ride the content folder.

### 7.3 Editor vs packaged

EQS, smart objects, StateTree and the managed seam are runtime features and run in both. The tabs, preview and debug overlay are editor-only. Play start in the editor: `BeginSession` is not needed (no per-session file scan; assets load on first use and are re-read on `reloadTree`/`reloadQuery`/`CSmartObject.asset` change). Hot reload of the script assembly: `ManagedAi.Shutdown()` then `ScanAssembly` again; native leaves registered earlier stay in the host registry as inactive trampolines until the name is re-registered (reactivated in place).

## 8. Native ABI: `synapse_query_abi.h` (`Aver.Synapse.Abi`, `AVER_SYN_QUERY_ABI_VERSION 1`)

New header `modules/synapse.abi/include/aver/synapse/synapse_query_abi.h`, new source `modules/synapse.abi/src/SynapseQueryAbi.cpp` added to the `add_library` list (`modules/synapse.abi/CMakeLists.txt:12`). `synapse_ai_abi.h` is NOT edited. Conventions as that header: `AVER_SYN_API`, 1/0 returns, `aver_syn_last_error()` reasons (`BadHandle -1`, `NullPointer -2`, `OutOfRange -4`, `InvalidArgument -6`; set to 0 on success), entities are the scene's 32-bit handles, strings UTF-8, cm.

```c
#define AVER_SYN_QUERY_ABI_VERSION 1
AVER_SYN_API int32_t aver_syn_query_abi_version(void);

/* ---- host wiring ---- */
typedef int32_t (*aver_syn_actors_fn)(const char* className, int32_t includeDerived, int32_t* out, int32_t cap, void* user);  /* returns count */
AVER_SYN_API int32_t aver_syn_set_actor_provider(aver_syn_actors_fn fn, void* user);
typedef void (*aver_syn_notify_fn)(int32_t entity, const char* eventName, void* user);   /* same shape as the animation-notify sink */
AVER_SYN_API int32_t aver_syn_set_notify(aver_syn_notify_fn fn, void* user);
AVER_SYN_API int32_t aver_syn_set_content_root(const char* dir);
AVER_SYN_API int32_t aver_syn_eqs_set_budget(float maxMs, int32_t maxUnitsPerTick);       /* defaults 1.0 / 0 ; 0 disables that limit */

/* ---- managed seam: AverSynManagedCall and the four functions in section 2.5 ---- */

/* ---- EQS ---- */
#define AVER_SYN_EQS_UNKNOWN 0
#define AVER_SYN_EQS_QUEUED 1
#define AVER_SYN_EQS_RUNNING 2
#define AVER_SYN_EQS_FINISHED 3
#define AVER_SYN_EQS_FAILED 4
#define AVER_SYN_EQS_CANCELLED 5
AVER_SYN_API int32_t aver_syn_eqs_load(const char* path);                  /* query id, 0 on failure (see aver_syn_eqs_last_message) */
AVER_SYN_API int32_t aver_syn_eqs_reload(const char* path);
AVER_SYN_API const char* aver_syn_eqs_last_message(void);                  /* parse/run failure text, "" when none ; valid until the next call */
AVER_SYN_API int32_t aver_syn_eqs_start(int32_t queryId, int32_t querier, int32_t target, float px, float py, float pz,
                                        int32_t hasPoint, const char* userTagsCsv, int32_t priority);   /* run handle, 0 refused */
AVER_SYN_API int32_t aver_syn_eqs_run(int32_t queryId, int32_t querier, int32_t target, float px, float py, float pz,
                                      int32_t hasPoint, const char* userTagsCsv);                        /* synchronous */
AVER_SYN_API int32_t aver_syn_eqs_status(int32_t handle);
AVER_SYN_API int32_t aver_syn_eqs_count(int32_t handle);                   /* items of a finished run, 0 otherwise */
AVER_SYN_API int32_t aver_syn_eqs_item(int32_t handle, int32_t index, float* outPos3, int32_t* outEntity, float* outScore,
                                       int32_t* outObject, int32_t* outSlot);       /* any out pointer may be NULL */
AVER_SYN_API int32_t aver_syn_eqs_test_value(int32_t handle, int32_t index, int32_t test, float* out);   /* NaN-free: 0 when that test did not run for the item */
AVER_SYN_API int32_t aver_syn_eqs_stats(int32_t handle, int32_t* generated, int32_t* passed, int32_t* evaluations, int32_t* truncated);
AVER_SYN_API int32_t aver_syn_eqs_cancel(int32_t handle);
AVER_SYN_API int32_t aver_syn_eqs_release(int32_t handle);
AVER_SYN_API int32_t aver_syn_eqs_pending(void);

/* ---- Smart objects ---- */
#define AVER_SYN_SO_FREE 0
#define AVER_SYN_SO_CLAIMED 1
#define AVER_SYN_SO_OCCUPIED 2
#define AVER_SYN_SO_COOLDOWN 3
#define AVER_SYN_SO_DISABLED 4
#define AVER_SYN_SO_REL_RELEASED 0
#define AVER_SYN_SO_REL_COMPLETED 1
#define AVER_SYN_SO_REL_FAILED 2
#define AVER_SYN_SO_REL_EXPIRED 3
#define AVER_SYN_SO_REL_USER_GONE 4
#define AVER_SYN_SO_REL_OBJECT_GONE 5
#define AVER_SYN_SO_REFUSE_NONE 0
#define AVER_SYN_SO_REFUSE_NO_SUCH_SLOT 1
#define AVER_SYN_SO_REFUSE_DISABLED 2
#define AVER_SYN_SO_REFUSE_NOT_FREE 3
#define AVER_SYN_SO_REFUSE_TAGS 4
#define AVER_SYN_SO_REFUSE_USER_LIMIT 5
#define AVER_SYN_SO_REFUSE_NO_USER 6
#define AVER_SYN_SO_FIND_MATCH_ALL 1
#define AVER_SYN_SO_FIND_INCLUDE_CLAIMED 2
AVER_SYN_API int32_t aver_syn_so_attach(int32_t entity, const char* assetPath);   /* adds CSmartObject and sets `asset` */
AVER_SYN_API int32_t aver_syn_so_set_enabled(int32_t entity, int32_t enabled);
AVER_SYN_API int32_t aver_syn_so_slot_count(int32_t entity);                       /* -1 when not a registered object */
AVER_SYN_API int32_t aver_syn_so_slot_info(int32_t entity, int32_t slot, float* outPos3, float* outYaw, float* outEntry3,
                                           float* outEntryYaw, int32_t* outState, int32_t* outUser);
AVER_SYN_API int32_t aver_syn_so_set_slot_enabled(int32_t entity, int32_t slot, int32_t enabled);
AVER_SYN_API int32_t aver_syn_so_find(int32_t user, float x, float y, float z, float radius, const char* slotTagsCsv,
                                      const char* userTagsCsv, int32_t flags, int32_t* outObjects, int32_t* outSlots, int32_t cap);  /* count, nearest first */
AVER_SYN_API int32_t aver_syn_so_claim(int32_t user, int32_t object, int32_t slot, const char* userTagsCsv, float ttlSec);       /* claim handle, 0 refused */
AVER_SYN_API int32_t aver_syn_so_last_refusal(void);                               /* AVER_SYN_SO_REFUSE_* of the last claim, thread-local */
AVER_SYN_API int32_t aver_syn_so_use(int32_t claim);
AVER_SYN_API int32_t aver_syn_so_release(int32_t claim, int32_t reason);
AVER_SYN_API int32_t aver_syn_so_claim_info(int32_t claim, int32_t* outObject, int32_t* outSlot, int32_t* outUser, int32_t* outState,
                                            float* outPos3, float* outYaw, float* outEntry3, float* outEntryYaw);
AVER_SYN_API int32_t aver_syn_so_claims_of(int32_t user, int32_t* out, int32_t cap);
```

The `aver_syn_ai_*` lifecycle functions stay in `synapse_ai_abi.h`. `AbiEnumTest` (`tests/abi/src/AbiEnumTest.cpp`) gains rows pairing `AVER_SYN_EQS_*` ↔ C# `EqsStatus`, `AVER_SYN_SO_*` (state) ↔ `SlotState`, `AVER_SYN_SO_REL_*` ↔ `ReleaseReason`, `AVER_SYN_SO_REFUSE_*` ↔ `ClaimRefusal`, `AVER_SYN_MANAGED_*` ↔ `ManagedKind`, `AVER_FW_BRAIN_*` ↔ `BrainKind`/`BrainStatus`. Version bumps: `AVER_SYN_QUERY_ABI_VERSION` 1 and `AVER_FW_BRAIN_ABI_VERSION` 1 are new; `AVER_FW_BLACKBOARD_ABI_VERSION` stays 1 (the relay only gained ops a provider may ignore); the `.NET` assembly `Aver.Synapse` 1.0.0 -> 1.1.0 (additive public surface); `Aver.Framework` stays 1.0.0 for the integrator to bump once for the wave (GAS's rule).

### 8.1 C# for EQS and smart objects (`Aver.Synapse/Eqs.cs`, `SmartObjects.cs`, `Native.cs` additions)

`Native.cs` gets one `[DllImport("Aver.Synapse.Abi")]` per function, spelled exactly as the header (the parity check compares spelling).

```csharp
namespace Aver.Synapse;
public enum EqsStatus { Unknown = 0, Queued = 1, Running = 2, Finished = 3, Failed = 4, Cancelled = 5 }
public readonly record struct EqsItem(float X, float Y, float Z, int Entity, float Score, int Object, int Slot);
public struct EqsRequest { public int Querier, Target; public float PointX, PointY, PointZ; public bool HasPoint; public string? UserTags; public int Priority; }
public readonly struct EqsRun {
    public int Handle { get; } public EqsStatus Status { get; } public int Count { get; }
    public bool IsDone => Status is EqsStatus.Finished or EqsStatus.Failed or EqsStatus.Cancelled;
    public bool TryGetItem(int index, out EqsItem item); public EqsItem Best { get; }            // default when empty
    public IReadOnlyList<EqsItem> Items();                                                         // allocates
    public bool TryGetTestValue(int index, int test, out float value);
    public (int Generated, int Passed, int Evaluations, bool Truncated) Stats { get; }
    public bool Cancel(); public void Release();
}
public static class Eqs {
    public static int Load(string contentPath); public static bool Reload(string contentPath); public static string LastMessage { get; }
    public static EqsRun Start(int queryId, in EqsRequest req); public static EqsRun Run(int queryId, in EqsRequest req);      // Run is synchronous
    public static EqsRun Start(string contentPath, in EqsRequest req);                             // Load + Start
    public static void SetBudget(float maxMs, int maxUnitsPerTick); public static int Pending { get; }
}
public enum SlotState { Free = 0, Claimed = 1, Occupied = 2, Cooldown = 3, Disabled = 4 }
public enum ReleaseReason { Released = 0, Completed = 1, Failed = 2, Expired = 3, UserGone = 4, ObjectGone = 5 }
public enum ClaimRefusal { None = 0, NoSuchSlot, Disabled, NotFree, Tags, UserLimit, NoUser }
[Flags] public enum FindFlags { None = 0, MatchAll = 1, IncludeClaimed = 2 }
public readonly record struct SlotRef(int Object, int Slot);
public readonly record struct SlotInfo(SlotRef Ref, SlotState State, int User, float X, float Y, float Z, float Yaw, float EntryX, float EntryY, float EntryZ, float EntryYaw);
public readonly record struct ClaimInfo(int Handle, SlotRef Slot, int User, SlotState State, float X, float Y, float Z, float Yaw, float EntryX, float EntryY, float EntryZ, float EntryYaw);
public static class SmartObjects {
    public static bool Attach(int entity, string assetPath); public static bool SetEnabled(int entity, bool enabled);
    public static int SlotCount(int entity); public static bool TryGetSlot(int entity, int slot, out SlotInfo info); public static bool SetSlotEnabled(int entity, int slot, bool enabled);
    public static int Find(int user, float x, float y, float z, float radius, string? slotTags, string? userTags, FindFlags flags, Span<SlotRef> results);   // nearest first
    public static List<SlotRef> Find(int user, float x, float y, float z, float radius, string? slotTags = null, string? userTags = null, FindFlags flags = FindFlags.None, int max = 16);
    public static int Claim(int user, SlotRef slot, string? userTags = null, float ttlSec = -1f);    // 0 = refused, see LastRefusal
    public static ClaimRefusal LastRefusal { get; }
    public static bool Use(int claim); public static bool Release(int claim, ReleaseReason reason = ReleaseReason.Released);
    public static bool TryGetClaim(int claim, out ClaimInfo info); public static int ClaimsOf(int user, Span<int> claims);
}
```

`Aver.Framework/Ai/EntityAi.cs` adds `partial struct Entity` conveniences taking `GameplayTag` containers: `EqsRun RunQuery(string path, Entity target = default, Vec3? point = null, bool sync = true)`, `int ClaimSmartObject(SlotRef slot, GameplayTagContainer? userTags = null)` which passes `Name` csv. `EqsRequest.UserTags`/`FindFlags` accept the csv string directly.

## 9. Graph nodes (Blueprint parity)

Rows in a new `scripting/csharp/Aver.Graph/GraphAiQueryNodeTable.cs` (`private static GenericNodeSpec[] AiQueryTable()` in `partial class GameSystemNodes`; `Table()` gains `.Concat(AiQueryTable())`, `GraphGameSystemNodeTable.cs:120`), bodies in `Aver.Synapse.QueryGraph` (public static, primitives only; EQS and smart objects) and `Aver.Framework.BrainGraph` (internal; reachable by the existing `InternalsVisibleTo("Aver.Graph")`). Palette mirror `sandbox/src/GraphNodeDefsAiQuery.hpp` (category `AI`), one include + one `appendAiQueryNodeDefs(t)` call in `GraphNodeDefs.hpp` beside `GraphNodeDefsGas.hpp` (`:109`, `:1088`). Row grammar and the failure rule are `GAS.md` section 3 (strings are NODE attributes, never pins; failures return false/0 and log once per node id, never throw). `GameSystemNodes.Validate()` checks every row against its method.

| Type | Kind | Method | Inputs (defaults) | Outputs | Attributes |
|---|---|---|---|---|---|
| `AN_EqsRun` | Exec | `QueryGraph.EqsRunForGraph(int querier, int target, float px, float py, float pz, bool hasPoint, string query, string userTags, out int count, out float x, out float y, out float z, out int entity, out float score)` | querier, target, px,py,pz, hasPoint | `found:bool, count:int, x,y,z:float, entity:int, score:float` | `query`, `usertags?` |
| `AN_EqsStart` | Exec | `EqsStartForGraph(querier, target, px, py, pz, hasPoint, priority, string query, string userTags)` | same + priority | `handle:int` (0 = refused) | `query`, `usertags?` |
| `AN_EqsStatus` | Pure | `EqsStatusForGraph(int handle, out int count)` | handle | `status:int, count:int` | none |
| `AN_EqsGetItem` | Pure | `EqsGetItemForGraph(int handle, int index, out float x, out float y, out float z, out int entity, out float score, out int obj, out int slot)` | handle, index | `success, x,y,z, entity, score, obj, slot` | none |
| `AN_EqsCancel` / `AN_EqsRelease` | Exec | `EqsCancelForGraph(handle)` / `EqsReleaseForGraph(handle)` | handle | `success:bool` | none |
| `AN_SoAttach` | Exec | `SoAttachForGraph(int entity, string asset)` | entity | `success` | `asset` |
| `AN_SoFind` | Pure | `SoFindForGraph(int user, float x, float y, float z, float radius, bool matchAll, string slotTags, string userTags, out int obj, out int slot, out float ex, out float ey, out float ez, out int count)` | user, x,y,z, radius=1500, matchAll | `found, obj, slot, ex, ey, ez, count` | `slottags?`, `usertags?` |
| `AN_SoClaim` | Exec | `SoClaimForGraph(int user, int obj, int slot, float ttl, string userTags, out int refusal)` | user, obj, slot, ttl=-1 | `claim:int, refusal:int` | `usertags?` |
| `AN_SoUse` / `AN_SoRelease` | Exec | `SoUseForGraph(claim)` / `SoReleaseForGraph(claim, reason)` | claim (, reason=0) | `success` | none |
| `AN_SoGetClaim` | Pure | `SoGetClaimForGraph(int user, int index, out int claim, out int obj, out int slot, out int state, out float x, out float y, out float z, out float yaw, out float ex, out float ey, out float ez)` | user, index=0 | `has, claim, obj, slot, state, x,y,z, yaw, ex,ey,ez` | none |
| `AN_SoSlotInfo` | Pure | `SoSlotInfoForGraph(int entity, int slot, out int state, out int user, out float x, out float y, out float z, out float yaw)` | entity, slot | `success, state, user, x,y,z, yaw` | none |
| `AN_SoSlotCount` | Pure | `SoSlotCountForGraph(int entity)` | entity | `count:int` | none |
| `AN_SoSetSlotEnabled` | Exec | `SoSetSlotEnabledForGraph(entity, slot, enabled)` | entity, slot, enabled | `success` | none |
| `AN_BrainSet` | Exec | `BrainGraph.SetAssetForGraph(int entity, string asset)` | entity | `success` | `asset` (.ocbt or .ocst) |
| `AN_BrainSendEvent` | Exec | `SendEventForGraph(int entity, int payloadI, float payloadF, string eventName)` | entity, payloadI, payloadF | `success` | `event` |
| `AN_BrainIsInState` | Pure | `IsInStateForGraph(int entity, string state)` | entity | `inState:bool` | `state` (dotted path) |
| `AN_BrainStatus` | Pure | `StatusForGraph(int entity, out int kind)` | entity | `status:int, kind:int` | none |
| `AN_BrainRestart` / `AN_BrainStop` | Exec | `RestartForGraph(entity)` / `StopForGraph(entity)` | entity | `success` | none |

**Entries (no new node).** A graph hears the system with ENTRY names through the existing notify seam: `EqsFinished`, `EqsFailed` (on the querier), `SmartObjectClaimed`, `SmartObjectUsed`, `SmartObjectReleased` (on the user), `SmartObjectSlot*` (on the object), and a slot's custom `USE event`. State tree/BT tasks that need a graph reaction use the existing `FireEvent` action (`SynapseBt.cpp:102-108`).

**Parity table.** EQS: run/start/status/items/cancel/release -> nodes; budget and `RegisterEqsTest` are C#-only (the graph equivalent of a custom test is data: `TEST path`/`trace`/`tag` in the asset). Smart objects: attach/find/claim/use/release/info/enable -> nodes; completion -> events; behaviours are assets. StateTree/brain: set/event/is-in-state/status/restart/stop -> nodes; custom tasks and conditions are C#-only (`ManagedAi`) because the graph runtime has no latent model, a stated difference (GAS.md section 2.5): a graph-authored task is an event handler reached by `FireEvent`. `GraphEditor` validity: nodes are Exec/Pure in the push compiler; Pure ones also compile under the pull compiler.

## 10. Tests

New directory `tests/synapse_query/` (CMakeLists: `*Test` executables, auto-registered by the recursive walk, `CMakeLists.txt:603-609`; `add_subdirectory(tests/synapse_query)` beside `tests/synapse_bt`, root `CMakeLists.txt:523`). Pure suites link `Aver.Synapse Aver.Formats Aver.Core Aver.Platform`; scene suites add `Aver.Synapse.Scene Aver.Scene`; the ABI suite `Aver.Synapse.Abi`. Test names are exact; every suite prints `  ok` / `  FAIL  ` lines like the neighbours.

| Suite | Asserts |
|---|---|
| `TextRecordsTest` | header required/versioned/repeated; quotes, escapes, `k="a b"`, trailing `#`, CRLF; `get/has`; `rest`; error text carries the line; `fmtF32` round trips 100 random f32; unterminated quote refused |
| `EqsAssetTest` | the section 3.2 example parses and every field lands; each refusal in 3.2 (unknown kind/key, two generators, undeclared context, grid over `max`, bad `RESULT`) with `name:line:`; `write(parse(x)) == x` byte-identical on a second write; `validateEqsQuery` warnings |
| `EqsTest` (fake `EqsEnv`, ASCII nav) | generator counts: grid `halfx=200 step=100` = 25, ring 3x8 = 24, around, nav with `stride` and `region=same` (second region excluded), points, `max` truncates and sets `truncated`, `project=nav` snaps/drops/dedupes, `ground` drop on miss; filters (range, boolean `want`); score formula against hand-computed numbers (observed vs explicit lo/hi, every curve, a negative weight, no scoring test = 1); tie order by generation index; **cost ordering**: a counting fake proves `trace` is never called for items an earlier `distance` filter rejected; `RESULT best/top/percent/randompercent` (deterministic by runId); `RUN firstvalid` stops at the first non-empty option, `all` merges; unresolved context behaviour per purpose; no nav -> Failed with reason; **time slicing**: `step(1)` loop equals one unlimited step item for item, and takes > 1 call; `cancel` mid-run |
| `SmartObjectAssetTest` | the 4.1 example; refusals (duplicate slot, bad tag, negative ttl, `USE bt` without path, 33 slots); exact writer round trip |
| `SmartObjectTest` (pure registry) | claim/use/release state machine and every `SoRefusal`; `requires` all / `blockedBy` none with hierarchical tags; claim TTL expiry -> Free with `Expired`; cooldown timing; find ordering by distance then ids and `includeClaimed`; user limit 4; stale handle after release fails and a recycled index gets a new generation; `removeObject` releases with `ObjectGone`; `releaseUser` |
| `StateTreeAssetTest` | the 5.2 example; refusals (unresolved path, duplicate sibling, `IF` outside `ENTER`/`ON`, unclosed `STATE`, bad expression, reserved names, depth); `validateStateTree` warnings with a registry; exact writer round trip; structural edit helpers: add/delete (retargeting reported)/rename (Goto paths rewritten)/move/reparent keep the asset valid |
| `StateTreeTest` (pure; fake leaves with counters, real `Blackboard`) | selection order and enter conditions, `GROUP` and no-selectable-child; tasks run concurrently, finished tasks are not re-ticked; `COMPLETE all/any/none`; each trigger; **leaf-first priority** (a child's `tick` beats its parent's); default bubbling to `NEXT`, parent completion, tree Succeeded/Failed; failed enter conditions skip a transition and the scan continues; re-entering the active state restarts its tasks while ancestors keep theirs (counter proof); `onAbort` called for Running tasks on exit; evaluators run first in order; events visible for exactly one tick, payload keys written; `ONFINISH restart` vs `stop`; bubbling cap 8; `stop()/restart()` |
| `EqsSceneTest` | contexts from entities, board keys (`CONTEXT target key=Target`), `seen`; `actors` by `tagbits` and `named`, by `class` through a fake provider; `slots` generator with a real `SmartObjectSystem`; `RunEqsQuery` leaf writes `OUTPUT` keys, Failure on zero items, `onAbort` cancels; FIFO + priority order and the unit budget across ticks; results expire; querier destroyed cancels; `EqsFinished` notify recorded by a fake sink; `trace` with real Jolt only `if(TARGET Aver.Physics)` (a wall between two points) |
| `SmartObjectSceneTest` | `CSmartObject` registers and appears in `componentId`; String field round trip through `aver_scene_set_str`; defs load from a temp file; slot world transform follows owner move/rotate; claim mirrors the six board keys and raises events in order through a fake sink; `USE event`; `USE bt` runs a tiny tree to completion and auto-releases `Completed`, `manual` stays Occupied; user destroyed mid-use aborts the leaf and frees the slot; object destroyed frees claims; the five leaves |
| `StateTreeSystemTest` | `loadBrain` by extension; `setBrain` attaches the component; a `.ocst` ticks under `BtSystem::tick`, `lastStatus` mapped; schema bound, team board shared keys; `RunBehaviorTree` task and `RunStateTree` leaf nest, depth guard 8; `reloadTree` resets and rebinds; `blackboardRelay` ops 6..13 (event, state text, in-state, status, restart, stop); debug view and trace; prune; BT behaviour unchanged (existing `BtSystemTest` stays green) |
| `QueryAbiTest` | every entry point against the real DLL with `aver_syn_last_error`: dead entity -1, null out pointer -2, bad index -4, bad arg -6, "no answer yet" records Ok; a C fake plays the managed side through `aver_syn_managed_*`: a managed Action runs `first`/`dt`/`elapsed`, returns Running then Success, a managed Condition, abort delivered on state exit, an EQS custom test filters items, `register` of an existing native name refused, `clear` makes leaves fail; `aver_syn_query_abi_version() == 1` |
| `AiEditorPartsTest` | `layoutTree` equals the old `btLayout` boxes for the same tree (golden); `StateTreeEditor` headless core (compiled with `AVER_WITH_IMGUI` undefined like `BtGraphEditorTest`): edit bookkeeping, undo/redo, transition retarget on rename/delete, save/load, dirty; `EqsEditor` core: option/test add/remove/reorder, `Run on selection` against a registered in-memory query |
| `ManagedAiClrTest` (real CLR, `tests/save`'s guards; "unavailable is not a failure") | after bridge boot `aver_syn_managed_installed() == 1`; a staged sample assembly with one `[AverAiAction]` and one `[AverEqsTest]` is scanned and a `.ocst` using them runs to Success; `Shutdown` clears |

C# suites: `scripting/csharp/Aver.Graph.Tests/AiQueryNodeTests.cs` (registered by one line in `Program.cs`, beside `GameSystemNodeTests.RunAll()`): `Validate()` clean for every row of section 9, default pins, attributes (`AN_EqsRun` without `query=` fails), exec nodes refused by the pull compiler, pure ones compile under both. `scripting/csharp/Aver.Framework.Tests/AiManagedTests.cs` (fake host, no engine DLL): `ManagedAi.Collect` finds attributed methods with the right signatures and reports bad ones; csv tag formatting from `GameplayTagContainer`; `EqsRun` item sorting helpers; `QueryGraph` argument guards (empty query -> false, never throws). `BtGraphEditorTest`, `BtSystemTest`, `BlackboardTest`, `SynapseAiSceneTest`, `SynapseAbiTest`, `NavTest`, `AbiEnumTest` stay green.

**Verification the implementer must do and report (not claim):** build (Debug is the owner's build; `aver_build`), every suite above, `dotnet run` the two C# projects, then a **headed** Play with a sample project (not committed): a level with a baked `.ocnav`, a bench prefab carrying `CSmartObject` with two slots, an NPC whose brain is a `.ocst` (Patrol -> on `Alarm` event Combat with a `RunEqsQuery` cover search -> back), checking: cover query picks a hidden spot and the debug draw shows it; two NPCs never claim the same slot; the NPC walks to the entry point, sits (event + montage through a graph), releases when the tree finishes; killing the NPC mid-use frees the slot; editing the `.ocst` in the tab and Save hot-reloads the running NPC; the StateTree debug panel follows the active chain. Screenshots at 1440p when the owner is remote (standing rule).

## 11. Files to create or modify, and ownership

**Created (this feature owns outright)**
- `modules/synapse/include/aver/synapse/`: `TextRecords.hpp`, `Tags.hpp`, `AiPaths.hpp`, `Eqs.hpp`, `EqsAsset.hpp`, `SmartObject.hpp`, `SmartObjectAsset.hpp`, `StateTree.hpp`, `StateTreeAsset.hpp`; `modules/synapse/src/`: `TextRecords.cpp`, `AiPaths.cpp`, `Eqs.cpp`, `EqsAsset.cpp`, `SmartObject.cpp`, `SmartObjectAsset.cpp`, `StateTree.cpp`, `StateTreeAsset.cpp`
- `modules/synapse.scene/include/aver/synapse/`: `SynapseEqs.hpp`, `SynapseSmartObject.hpp`, `SynapseManaged.hpp`; `modules/synapse.scene/src/`: `SynapseEqs.cpp`, `SynapseSmartObject.cpp`, `SynapseManaged.cpp`
- `modules/synapse.abi/include/aver/synapse/synapse_query_abi.h`, `modules/synapse.abi/src/SynapseQueryAbi.cpp`
- `modules/framework/include/aver/framework/framework_brain_abi.h`
- `scripting/csharp/Aver.Synapse/`: `Eqs.cs`, `SmartObjects.cs`, `ManagedAi.cs`, `QueryGraph.cs`; `scripting/csharp/Aver.Framework/Ai/`: `Brain.cs`, `EntityAi.cs`; `scripting/csharp/Aver.Graph/GraphAiQueryNodeTable.cs`
- `sandbox/src/`: `AiEditorParts.hpp/.cpp`, `StateTreeEditor.hpp/.cpp`, `EqsEditor.hpp/.cpp`, `GraphNodeDefsAiQuery.hpp`
- `tests/synapse_query/` (CMakeLists + the suites in section 10), `scripting/csharp/Aver.Graph.Tests/AiQueryNodeTests.cs`, `scripting/csharp/Aver.Framework.Tests/AiManagedTests.cs`
- `docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md` (user guide: formats, node list, event table, leaves, limits, differences from Unreal)

**Modified (AI-owned, additive or local)**
- `modules/synapse/CMakeLists.txt` (eight `src/` lines), `modules/synapse.scene/CMakeLists.txt` (three), `modules/synapse.abi/CMakeLists.txt` (one), `modules/framework/CMakeLists.txt` (none: the brain functions are appended to the existing `src/BlackboardAbi.cpp`)
- `modules/synapse.scene/include/aver/synapse/SynapseBt.hpp`, `src/SynapseBt.cpp` (5.5; relay ops 6..13; `RunBehaviorTree`/`RunStateTree`; `clearStateTrees`), `SynapseAi.hpp`, `src/SynapseAi.cpp` (members, tick order, `reset`, leaves, `reg_`). `modules/synapse.abi/src/SynapseAiAbi.cpp` is NOT edited (the new ABI has its own file).
- `modules/synapse/include/aver/synapse/AiDebug.hpp` (three categories, `kAiDebugAll`)
- `modules/framework/src/BlackboardAbi.cpp` (brain functions), `modules/framework/include/aver/framework/framework_blackboard_abi.h` (op defines only)
- `scripting/csharp/Aver.Synapse/Native.cs`, `Aver.Synapse.csproj` (`AllowUnsafeBlocks` true, version 1.1.0)
- `sandbox/src/BtGraphEditor.hpp/.cpp` (6.2), `AiDebugOverlay.hpp/.cpp` (options, three feeders), `sandbox/CMakeLists.txt` (new sources)
- `tests/abi/src/AbiEnumTest.cpp` (rows), `tests/synapse_bt/CMakeLists.txt` (none)
- `module.json` descriptions only: `modules/synapse.scene/module.json` `consequence` -> "No AI agents, perception, behaviour trees, state trees, environment queries or smart objects in a scene."; `modules/synapse.abi/module.json` `consequence` -> add "queries, smart objects"; then `python scripts/module-catalog.py --write` regenerates `cmake/modules.catalog.json` (the catalog check stays green)
- Docs, append-only: `docs/SYNAPSE.md` (section 7 "not built" entries updated, section 6 file table), `docs/BLACKBOARD_BT.md` (a pointer to StateTree and the brain ops), `docs/ABI.md` (the two new headers beside the Synapse/blackboard line, `ABI.md:255`), `docs/formats/FORMAT_SPECS.md` (sections 11e `.oceqs`, 11f `.ocso`, 11g `.ocst`; this feature reserves 11e-11g, other wave-3 features start at 11h), `docs/AVER_NODE_NODES.md` (family `AI Queries`)

**Shared files, delimited additions only (keep a `// AI: wave 3 #21` marker so a merge keeps both sides)**
- `Runtime/include/aver/game/GameSystemsWiring.hpp` (7.2), `Runtime/src/GameApp.cpp` (two lines), `sandbox/src/SandboxApp.cpp` (factory registration at `:446-450`, two lines at `:988` and `:1016`), `sandbox/src/SandboxApp.hpp` (declarations `cbCreateStateTree`, `cbCreateEqsQuery`, `cbCreateSmartObject`), `sandbox/src/SandboxContentBrowser.cpp` (type table `:1868-1876`, three Add-menu items, `cbIsSourceFile` `.ocso`, three `cbCreate*` beside `cbCreateBehaviourTree`), `sandbox/src/SandboxShell.cpp` (three menu items at `:2153`), `sandbox/src/GraphNodeDefs.hpp` (include + call), `scripting/csharp/Aver.Graph/GraphGameSystemNodeTable.cs` (`.Concat`), `scripting/csharp/Aver.Graph.Tests/Program.cs`, `scripting/csharp/Aver.Scripting.Bridge/HostBridge.cs` (three lines, 2.5), root `CMakeLists.txt` (`add_subdirectory(tests/synapse_query)`), `scripts/game.allowlist` / `payload.allowlist` (none needed)
- Partition against the other wave-3 features: **not touched** here: `OcWorld*`, level load/streaming, `SandboxLevelLoad.cpp`, the outliner (level instances), `modules/landscape`, water/fluids, landscape tools (landscape). The nav grid is read, never written. `GameApp.cpp`/`SandboxApp.cpp` edits are single lines at lines the others do not need.

## 12. Save/load, limits, differences from Unreal

- **Not saved**: StateTree instances, EQS runs, smart-object claims and the blackboards (the existing `SaveWorld` carries components and `$GraphVars`, not boards, `docs/plans/wave2/GAS.md` section 2.11). After a load every brain restarts and every slot is Free; `CSmartObject` itself (asset, enabled) rides the save as a component.
- Limits: 256 states, depth 16, 16 tasks and 16 transitions per state; 8 options, 16 tests, 16384 items, 64 queued runs and 64 retained results; 32 slots per object, 4 claims per user; event queue 16.
- Differences from Unreal, so nobody assumes parity: navigation is a 2.5D grid; contexts are single-valued; StateTree transitions scan leaf first and have no delay/priority fields; tasks are BT Action leaves (no per-task enter/exit hooks beyond `First`/abort); no StateTree "linked asset" state kind (a task `RunStateTree` does it); no property binding (parameters are blackboard keys); smart-object slot filters are tag-based only; EQS has no item "context actors" fan-out and no EQS testing pawn (the editor preview replaces it).

## 13. Risks and open questions for the owner

1. **Grid, not navmesh.** Nav generators and the path test see one floor per cell. Multi-storey levels give wrong answers for the upper floors. A real navmesh is out of scope here; the nav API is isolated behind `EqsEnv` so a future mesh drops in.
2. **Two process copies of `Aver.Synapse.Scene`.** The EQS/SO-in-DLL vs StateTree-in-host split follows the existing blackboard/hearing precedent but is a trap for the next feature. The alternative (everything host-side plus a relay op per call) was rejected for EQS/SO because the C# surface would be ~40 relay ops.
3. **Tags.** Three vocabularies now: `CTags` bits, C# `GameplayTag`, native string tags. They meet only at strings. Native code cannot see a GAS tag container, so a "has gameplay tag" EQS filter is a managed `custom` test. Decide if GAS tags should ever be native.
4. **Managed callbacks cost.** An EQS `custom` test is one unmanaged-to-managed transition per item (cost class 1). Acceptable at the default 2048 items; a user test over 16384 items is their budget problem. Exceptions are swallowed and logged once per name.
5. **Placing smart objects in a level.** v1 relies on Add Component, prefabs and graph classes. A bare `PLACE` line cannot carry `CSmartObject`; adding `smartobject=<asset>` to `PLACE` is an `OcWorld` change owned by the level-instance feature. Ask them, or accept prefabs only.
6. **Cover reservations are not rebuilt on smart-object claims.** Two claim systems coexist. Moving cover onto slots is possible later (cover point = slot with tag `Cover.High`) and would delete `CoverReservations`; deferred to keep this change reviewable.
7. **`BtGraphEditor` is refactored.** Extraction (6.2) risks regressions in the one editor that works; the guard is `BtGraphEditorTest` plus a manual BT session. If the owner prefers zero risk, copy the three parts instead (duplication of ~300 lines).
8. **Assignment gap fixed as a side effect.** Today no editor/graph/C# path assigns a BT to an entity. `aver_fw_brain_set_asset`, `AN_BrainSet` and `Brain.SetAsset` create it, for BTs too. There is still no inspector widget for `CSynapseBehavior.treeAssetId` (a hash); a path field would need a String field and a component change. Wanted?
9. **Time-slice budget is wall-clock.** Results are identical for any budget, but when a query finishes varies with machine load; tests use the unit budget. Raycast cost (linear body scan noted in `docs/SYNAPSE.md` section 1) makes `trace` over thousands of items slow; the default `max=2048` and the cost ordering are the mitigation.
10. **StateTree conventions** (leaf-first transitions, default bubbling, no delay) are this design's choice, not Unreal's. Say if exact UE evaluation order matters.
11. **AI runtime state is not saved** (section 12). A save mid-interaction resumes with the NPC back in its start state. Flag if saved NPC behaviour is required for a shipping scenario.
