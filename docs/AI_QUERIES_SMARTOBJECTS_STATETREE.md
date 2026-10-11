# Environment queries, smart objects and StateTree

Three AI features built on the Synapse module (wave 3, feature 21). Each is a text asset, a runtime system, a set of behaviour-tree leaves, a C# surface and an editor.

| Feature | Asset | Where it runs | Editor |
|---|---|---|---|
| Environment Query System (EQS) | `.oceqs` | `SynapseAi` in `Aver.Synapse.Abi` | EQS tab |
| Smart objects | `.ocso` | `SynapseAi` in `Aver.Synapse.Abi` | text file, opens in your IDE |
| StateTree | `.ocst` | `BtSystem` in the host | StateTree tab |

The file grammars are in [formats/FORMAT_SPECS.md](formats/FORMAT_SPECS.md) sections 11g, 11h and 11i. The behaviour-tree side is [BLACKBOARD_BT.md](BLACKBOARD_BT.md) and the rest of Synapse is [SYNAPSE.md](SYNAPSE.md).

---

## 1. Environment queries

A query is one or more **options**. Each option has one **generator** (produces items: a position, optionally an entity, optionally tags) and an ordered list of **tests**. A test computes a raw value per item and may **filter** (drop items outside a range or the wrong boolean) and/or **score** (normalise to 0..1, apply a curve, weight it). A run produces a sorted item list.

Generators: `grid`, `ring`, `around`, `nav`, `actors`, `slots`, `points`. Tests: `distance`, `dot`, `trace`, `path`, `tag`, `random`, `custom`. Contexts name a position a generator or test refers to: `querier`, `target`, `seen`, `point`, plus any `CONTEXT <name> key=<BbKey>` the file declares.

### How a run is evaluated

1. Options run in file order; generation costs 1 unit per item.
2. Tests run cheapest first: `random`, `tag`, `dot`, `distance` (1 unit per item), then `custom` (2), `trace` (8), `path` (40). A rejected item skips every later test, so expensive tests see few items.
3. Each scoring test normalises over the surviving items (or between `lo` and `hi`), applies its curve and contributes `weight * curve`. The item score is the weighted sum divided by the sum of absolute weights, so it lies in [-1, 1]. With no scoring test every item scores 1.
4. Items sort by score descending, ties by generation order. `RESULT` then keeps all, the best, the top N, a percentage, or one random item from the top percentage.
5. `RUN firstvalid` stops at the first option that leaves an item. `RUN all` runs every option and sorts the union.
6. A query that yields no item is **Finished with zero items**, not Failed. Failed means unusable input: an unresolved generator centre, a `nav` generator with no nav grid, an unknown query.

Time slicing never changes the result: a run is one sequential loop with a saved cursor, so any budget gives identical output.

### Budget and lifetime

`EqsSystem` steps queued runs every tick under a budget (default 1 ms; also an optional per-tick unit cap, `Eqs.SetBudget`). Finished results stay retrievable for 30 seconds. At most 64 runs may be queued and 64 retained. A synchronous run (`Eqs.Run`, `RunQuery(sync: true)`) ignores the budget and finishes before returning.

### From a behaviour tree or StateTree

Leaf `RunEqsQuery` (Action): `stringParam` is the content-relative `.oceqs` path. It starts a run, stays Running until it finishes, and writes the keys named by the file's `OUTPUT` line (`pos`, `entity`, `score`, `count`; any subset) to the board. Success when at least one item came back, Failure otherwise. It cancels the run if aborted. Leaf `EqsHasResult` (Condition) is true when the entity's last run produced an item.

### From C#

```csharp
EqsRun run = entity.RunQuery("AI/FindCover.oceqs", target: player);   // Aver.Framework
if (run.Status == EqsStatus.Finished && run.Count > 0) { EqsItem best = run.Best; ... }   // run.Items() lists them all
```

`Aver.Synapse.Eqs` has the lower level calls (`Load`, `Reload`, `Start`, `Run`, `SetBudget`, `Pending`). Runs are handles: `Release` them when done.

### Custom tests in C#

```csharp
[AverEqsTest("HeightAboveGround")]
public static float Height(in EqsTestContext c) => c.Z;
```

Scripts are scanned at load, so the attribute is all that is needed. A `TEST custom name=HeightAboveGround min=50` then calls it per item. An unregistered name rejects every item and logs once.

### Caveats

- **`nav` and `path` use the baked grid, not a navmesh.** Distances are grid path lengths; a level with no `.ocnav` has no navigation, so `nav` generators fail the run and `path` tests reject every item.
- **`trace` needs physics.** Without a physics world every item reads as visible and the run logs one warning. Sphere-cast traces cannot ignore item entities (the physics query reports no entity), so a sphere trace to an item that is itself a physics body may be blocked by it.
- The `actors` generator by class goes through the host actor provider and returns nothing in a build without one.

---

## 2. Smart objects

A **smart object** is an entity with a `CSmartObject` component whose `asset` field names an `.ocso`. The definition lists **slots**: places where an actor can do something (sit, lean, use a console). Each slot has a local position and yaw, an entry point to walk to, tag requirements for the user, a cooldown, a claim time-to-live and an optional behaviour that runs on the user.

### Slot life cycle

```
Free --claim--> Claimed --use--> Occupied --release--> Cooldown --timer--> Free
                   |                                       (Disabled by SetEnabled, any time)
                   +--TTL expired / user or object gone--> released
```

- **Claim** reserves a slot for a user. A user may hold at most 4 claims. A claim is refused with a reason (`ClaimRefusal`): no such slot, disabled, not free, tags not satisfied, user limit, no user.
- **Use** starts the slot's behaviour: `none` and `event` leave the slot Occupied until released (an `event` raises its name on the user); `bt` and `statetree` run the tree on the user each tick. With `RELEASE auto` the slot is released when the tree finishes (Success is `Completed`, Failure is `Failed`); with `manual` it stays Occupied.
- **Release** (`ReleaseReason`: Released, Completed, Failed, Expired, UserGone, ObjectGone) starts the slot's cooldown.
- A claim that is never Used is released as `Expired` after `CLAIMTTL` seconds. Cooldown timers advance before the TTL check in a tick.
- Handles are `(generation << 12) | index`, so a stale handle is rejected rather than aliasing a new claim.
- Slot tags and the user's tags match hierarchically: a requirement `User.Humanoid` is satisfied by `User.Humanoid.Adult`.

Slot positions follow the owner entity: the registry is re-synced from the world transform whenever the owner moves.

### Leaves

| Leaf | Kind | Parameters | Behaviour |
|---|---|---|---|
| `ClaimSmartObject` | Action | `stringParam` slot tags csv; `params[0]` radius (default 1500), `params[1]` claim TTL seconds, `params[2]` 1 = match all tags | Finds the nearest satisfying free slot (user tags from the board key `UserTags` if defined), claims it, mirrors the keys. Success, or Failure when none or refused. |
| `MoveToSmartObject` | Action | `params[0]` arrive radius (default 60) | Needs a held claim; requests a path to the slot entry point; Running until arrived, Failure if the path fails. |
| `UseSmartObject` | Action | none | Uses the user's first claim; Running while the slot is Occupied by it; Success when released as `Completed`; aborting releases it. |
| `ReleaseSmartObject` | Action | none | Releases all the user's claims; Success. |
| `HasSmartObjectClaim` | Condition | `params[0]` = 1 requires Occupied | True when the user holds a (used) claim. |

**Deviation from the design doc:** `ClaimSmartObject` treats any `params[1] <= 0` as "use the slot's own `CLAIMTTL`" (the design said -1). Since a leaf's parameters default to 0, this makes the default do the sensible thing.

### Board keys mirrored on a claim

Defined on the fly on the user's board, cleared when the last claim is released: `SmartObject` (Entity), `SmartObjectSlot` (Int), `SmartObjectHandle` (Int), `SmartObjectPos` (Vec3), `SmartObjectEntry` (Vec3), `SmartObjectYaw` (Float, degrees, world).

### From C#

```csharp
SlotRef? slot = entity.FindSmartObject(radius: 1200, slotTags: "Activity.Sit", userTags: myTags);
if (slot is { } s) { int claim = entity.ClaimSmartObject(s, myTags); SmartObjects.Use(claim); }
```

`Aver.Synapse.SmartObjects` has `Attach`, `SetEnabled`, `SlotCount`, `TryGetSlot`, `SetSlotEnabled`, `Find`, `Claim`, `LastRefusal`, `Use`, `Release`, `TryGetClaim`, `ClaimsOf`.

---

## 3. StateTree

A StateTree is a forest of nested **states**. Exactly one chain (root-most to leaf) is active. Each active state runs its **tasks** every update; **evaluators** run first every update; **transitions** move the active chain. The parameters are the entity's blackboard, declared with `KEY` lines.

An entity's **brain** is an `.ocbt` **or** an `.ocst`; both use the same leaves and the same board. They nest: a StateTree task can be `RunBehaviorTree`, a behaviour-tree node can be `RunStateTree`, to a depth of 8. A tree that nests itself fails the leaf instead of recursing.

### Tasks, evaluators and conditions

A task or evaluator is a registered Action leaf (native, or managed with `[AverAiAction]`); a condition is a registered Condition leaf (`[AverAiCondition]`). A leaf's name is not checked when the file parses; use `validateStateTree` (the editor shows its warnings) to find unknown leaves.

### Exact semantics, per update

1. First tick (or restart): enter the first enabled root whose enter conditions pass and that can descend. Nothing selectable means the tree fails.
2. Evaluators run in order; their status is ignored (a failure is logged once).
3. Every active state, root-most first, ticks every unresolved task. Success marks it done, Failure marks it failed, Running keeps it.
4. A state is **failed** if any task failed. It is **complete** under `COMPLETE all` when it has tasks and all are done, under `any` when one is done, never under `none`.
5. Transitions scan the active chain leaf first, then its ancestors; within a state in file order; the first that fires is applied. `tick` always fires, `completed` when complete, `failed` when failed, `event:X` when that event is pending. Its `IF` list must pass.
6. If nothing fired, a complete leaf acts as `ON completed TO NEXT`; a failed leaf bubbles its failure up. `NEXT` with no following selectable sibling completes the parent; a root with nothing next ends the tree Succeeded; an unhandled failure ends it Failed. Bubbling is limited to 8 steps per update; the remainder resumes on the next update.
7. Applying a target: states leaving the chain exit leaf first (a running task gets its abort callback), states in both chains keep running, a target that is already active exits and re-enters with its descendants. A target whose enter conditions fail does not apply, and the scan continues with the next transition.
8. Events sent with `SendEvent` are visible to step 5 of the **next** update only, then dropped (at most 16 queued). The board keys `EventPayload` (Int) and `EventPayloadF` (Float) carry the payload.
9. When the tree finishes, `ONFINISH restart` begins again next update and `stop` stays terminal until restarted.

### From C#

```csharp
var brain = entity.Brain;                       // Aver.Framework
brain.SetAsset("AI/Guard.ocst");
brain.SendEvent("Alarm", payloadInt: 3);
if (brain.IsInState("Combat.Chase")) ...
BrainKind kind = brain.Kind;  BrainStatus status = brain.Status;  string leaf = brain.ActiveState;
brain.Restart();  brain.Stop();
```

The StateTree runs in the host (`BtSystem`); managed code reaches it through blackboard relay ops 6 to 13 (`framework_brain_abi.h`). No Synapse DLL round trip is needed.

---

## 4. Events

Fired through the graph event seam (no payload). Events raised during an API call or tick are queued and delivered when the outermost call ends (nesting depth 8, then dropped with one warning), so a handler that calls back never sees half-applied state.

| Event | Raised on | When |
|---|---|---|
| `EqsFinished`, `EqsFailed` | the querier | a queued run ended |
| `SmartObjectClaimed`, `SmartObjectUsed`, `SmartObjectReleased` | the user | the matching slot event |
| `SmartObjectSlotClaimed`, `SmartObjectSlotUsed`, `SmartObjectSlotReleased` | the object entity | the matching slot event |
| the slot's `USE event` name | the user | at Use |

---

## 5. Editors

- **StateTree tab** (`.ocst`): a canvas (same pan and zoom as the behaviour-tree canvas), Goto transitions drawn as labelled dashed curves, and a side panel with State, Parameters, Evaluators and Debug tabs. Debug highlights the active chain of a picked entity and the last transition. Save writes the file and reloads it in the running game.
- **EQS tab** (`.oceqs`): a form over the query with a reorderable test list. **Run on selection** evaluates the in-memory edit with the selected entity as querier and shows a ranked results table and the items in the viewport.
- **Behaviour-tree editor** now shares its canvas input, schema panel, entity picker and live board with the StateTree editor (`AiEditorParts`). Behaviour is unchanged.
- **Content browser:** New State Tree, New EQS Query, New Smart Object Definition. A `.ocso` has no tab and opens in your IDE.
- **Debug overlays** (Show > AI): EQS items (scored, best highlighted), smart-object slots (arrow per slot by state: green Free, yellow Claimed, red Occupied, grey Cooldown or Disabled), StateTree labels (active leaf path, task statuses, last transition).

---

## 6. Limits

| What | Limit |
|---|---|
| Options per query / tests per option / contexts per file | 8 / 16 / 8 |
| Items per generator | 16384 (default 2048) |
| Queued / retained EQS runs; result lifetime | 64 / 64; 30 s |
| Debug runs kept for the overlay | 8 |
| Slots per object | 32 |
| Claims per user | 4 |
| States per tree / depth / tasks / transitions per state | 256 / 16 / 16 / 16 |
| Bubbling steps per update | 8 |
| Queued events per tree | 16 |
| Nested `RunBehaviorTree` / `RunStateTree` depth | 8 |

---

## 7. Differences from Unreal Engine

- **Queries run on a grid.** There is no navmesh projection or navigation-data actor; `project=nav` snaps to the baked grid within 4 cells.
- **A query result is a plain item list**, not typed item classes. Items carry a position, an optional entity and optional tags.
- **Custom generators are not available**; use the `points`, `actors` and `slots` generators or compute positions in C# and pass them to a query as `points`. Custom **tests** are (`[AverEqsTest]`).
- **Smart objects are a component plus a text asset**, not world-partition actors with behaviour definitions. Claim, use and release are explicit; the slot behaviour is a tree or an event.
- **StateTree conditions and tasks are the same leaves behaviour trees use**; there is no separate schema or property-binding system. Parameters are the entity's blackboard.
- **AI state is not saved.** Queries in flight, claims and the active state chain are rebuilt on load.
- Placing smart objects is the same as any entity with the component; they cannot be placed through a PLACE record in a level file.

---

## 8. Graph nodes

Graph nodes call into the C# surface through `Aver.Synapse.QueryGraph` (queries and smart objects) and `Aver.Framework.BrainGraph` (brains). See [AVER_NODE_NODES.md](AVER_NODE_NODES.md) for the palette when the node table lands.

---

## 9. Where things live

| Path | What |
|---|---|
| `modules/synapse/…/TextRecords`, `Tags`, `AiPaths` | shared text reader, hierarchical tags, content-relative path handling |
| `modules/synapse/…/Eqs`, `EqsAsset` | pure query core and format |
| `modules/synapse/…/SmartObject`, `SmartObjectAsset` | pure slot registry and format |
| `modules/synapse/…/StateTree`, `StateTreeAsset` | pure StateTree evaluator and format |
| `modules/synapse.scene/…/SynapseEqs`, `SynapseSmartObject`, `SynapseManaged` | scene systems and the managed seam |
| `modules/synapse.abi/…/synapse_query_abi.h` | C ABI for queries, smart objects, managed leaves |
| `modules/framework/…/framework_brain_abi.h` | brain constants for the blackboard relay |
| `scripting/csharp/Aver.Synapse/{Eqs,SmartObjects,ManagedAi,QueryGraph}.cs` | C# bindings |
| `scripting/csharp/Aver.Framework/Ai/{Brain,EntityAi}.cs` | `Entity.Brain`, `RunQuery`, `ClaimSmartObject`, `FindSmartObject` |
| `sandbox/src/{StateTreeEditor,EqsEditor,AiEditorParts}.*` | editors |
| `tests/synapse_query/` | suites for all of the above |
