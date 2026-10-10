# Wave 2, feature #19: Enhanced Input upgrade (design, frozen contract)

2026-10-11. Design only. The implementing agent follows this without the author. Parity-plan row: "Triggers (hold, tap, chord) and modifiers (dead zone, swizzle, negate); verify and wire the `.ocinput` C# loader".

Rules in force: upgrade the existing named-action layer, no parallel system; no new third-party code; module boundaries hold (Aver.Framework already depends on Aver.Formats, nothing new is linked); Blueprint parity (every C# capability gets graph nodes); lean comments; D3D12/Vulkan irrelevant (CPU input only).

## 0. Decisions in ten lines

1. The native algorithm in `FrameworkAbi.cpp` (NAMED ACTIONS section) is extended, not replaced. `aver_fw_action_held/pressed/released/value2` keep their exact behaviour for any action with no triggers/modifiers (defaults: no trigger = implicit Down at the pinned 0.15 actuation, no modifier = identity). Existing FrameworkTest cases must pass unchanged.
2. Triggers and modifiers are written as ONE whitespace-free **spec token** (`hold:0.5`, `deadzone:0.2:1:radial`). One C++ parser/formatter in `Aver.Formats` (`OcInput.cpp`) is used by the `.ocinput` reader/writer, by the framework ABI (`aver_fw_action_add_trigger(…, spec)`), and by the editor. C# builds specs with typed factories and never parses them.
3. Nine triggers: Down, Pressed, Released, Hold (time, once), HoldAndRelease (time), Tap (max time), Pulse (interval, limit, on-start), Chorded (another action), Combo (ordered action sequence). Six modifiers: DeadZone (lower, upper, radial/axial), Scale (xyz), Negate (xyz mask), Swizzle (6 orders), Smooth (time constant), ResponseCurve (exponent xyz).
4. Both attach per binding (`BIND … trigger X modifier Y`) and per action (`ACTION … trigger X modifier Y`).
5. Trigger state per action each frame: None / Ongoing / Triggered. Events derived from state transitions: Started / Ongoing / Triggered / Completed / Canceled (bit mask).
6. Time-based triggers need `dt` and per-action memory, so a new per-frame step `aver_fw_action_update(dt)` runs once a frame from `game::tickGameplayGroups` (the one function both hosts already call). Plain queries stay on-demand and need no update.
7. Graphs see events two ways: a polled `InputActionState` node, and bus events `Input.<Action>.<Event>` delivered to any `AN_OnEvent`/`OnInputAction` entry. C# gets `InputAction.Started/Ongoing/Triggered/Completed/Canceled` events.
8. The `.ocinput` C# loader already exists and is wired (section 1.3). "Verify and wire" reduces to: carry triggers/modifiers through `InputScheme.Load`, and add the missing managed coverage and a headed check.
9. Framework ABI goes 1.10 to **1.11**, additive only.
10. Triggers/modifiers are authored data, never rebindable at run time; the existing rebind keys (`Rebind.<ctx>.<action>.<slot>.Source/.Key`) are unchanged, so a saved rebind never freezes an old trigger.

## 1. What exists today

### 1.1 Native (all `modules/framework`)

- `framework_abi.h:59` `AVER_FW_ABI_VERSION_MINOR 10`. NAMED ACTIONS section `:406-474`: `aver_fw_action_register/find/bind/clear_bindings/value2/held/pressed/released`. Sources `AVER_FW_ACTION_SRC_*` `:435-440`. INPUT SCHEME section `:626-676`.
- `FrameworkAbi.cpp`, all inside `extern "C" {` (opened `:352`): `InputState` `:1094-1106` (`cur/prev`, `mouse/prevMouse`, `curVk/prevVk`); `aver_fw_input_new_frame` `:1118`; `GamepadState` `:1204-1243`; `ActionDef` `:1256`; `ActionBinding{action,source,key,scale,component,priority}` `:1267`; `actionKeyConsumedByHigherPriority` `:1283`; `actionAccumulate` `:1296-1351` (sums `value*scale` into component 0/1/2, key consumption by strictly higher priority, no dead zone); `actionActive` `:1354` (pinned 0.15, any of 3 channels); `aver_fw_action_register` `:1377`, `_bind` `:1389`, `_clear_bindings` `:1402`, `_value2` `:1405`, `_held` `:1414`, `_pressed` `:1422`, `_released` `:1431`; scheme section `:1572-1668` (one file-static `fmt::OcInputData`).
- Everything is **on demand**: no per-frame action step exists, no `dt` reaches the input layer. `held/pressed/released` compare current vs previous `InputState` bytes.
- Gaps: no per-binding or per-action modifier (raw gamepad stick has no dead zone beyond the 0.15 gate, no response curve, no swizzle/negate except `scale`); no trigger other than implicit Down; no Started/Completed/Canceled events; no way to read an action's name or declared value type back (`InputAction.Find` comment, `EnhancedInput.cs:114-123`); `value2` has no Z.

### 1.2 Format (`modules/formats`, base target)

- `OcInput.hpp:75-130` `OcInputAction{name,type}`, `OcInputBinding{action,source,key,scale,component}`, `OcInputData{version,name,actions,bindings,contextName,contextPriority}`. `OcInput.cpp:216-230` parses `ACTION` (exactly 3 tokens), `:231-305` `BIND` (bare key token then `keyword value` pairs `scale`/`component`; any other keyword is an error), `:306-320` `CONTEXT`. Strict: malformed = whole file rejected; unknown record kinds are skipped and preserved on write. `writeOcinput` merges into existing text (`:362-466`). Tests: `tests/formats/src/OcInputTest.cpp` (target `OcInputTest`, `tests/formats/CMakeLists.txt:54`). No sample `.ocinput` ships in the tree.
- `OcProject` key `INPUT.SCHEME` (`OcProject.hpp:30`).

### 1.3 The `.ocinput` C# loader: verified, already wired

`InputScheme.Load` (`Aver.Framework/InputScheme.cs:65-131`) calls `aver_fw_input_scheme_*`, builds a `SchemeContext : InputMappingContext`, pushes it, overlays saved rebinds (`LoadBindings`), pushes again. Callers: `HostBridge.ConfigureInput` (`Aver.Scripting.Bridge/HostBridge.cs:401`) <- `ScriptHost::configureInput` (`modules/scripting/src/ScriptHost.cpp:472`) <- `Runtime/src/GameApp.cpp:1163-1176` (game start) and `sandbox/src/SandboxPlay.cpp:289-306` (every Play start; re-parses so saved edits apply). Native half is tested (`FrameworkTest.cpp:944` `testInputScheme`). **Not covered:** the managed `InputScheme.Load` (no test can see the ABI from managed tests, per repo convention), and nothing runs a real scheme in Play. Both are in the test plan (section 8).

### 1.4 C# (`scripting/csharp/Aver.Framework`)

- `EnhancedInput.cs`: `InputAction` thin wrapper `:67-158` (`Digital/Axis1D/Axis2D`, `Find` limited to wrappers this runtime built, `Value2D` Z always 0, `IsHeld/WasPressed/WasReleased`); `internal readonly struct InputBinding` `:163-184`; `InputMappingContext` `:190-355` (`BindKey/BindGamepadButton/BindGamepadAxis/BindAxis1D/BindAxis2D/BindMouseLook/BindMouseWheel` all `void`, `SaveBindings/LoadBindings/ResetToDefaults`); `EnhancedInput` `:360-551` (`AddContext/RemoveContext/ClearContexts`, `Rebind()` `:412-425` clears the native table and re-issues every binding, `Save/Load/ResetAllBindings`, `RebindAction` `:500`, `TryGetBindingKey` `:530`). `new InputBinding(` appears in `:182`, `:232-238`, `:339`, `:515`, `InputScheme.cs:178`.
- `UiGameSettings.cs:30-47` `FrameworkRebindProvider.FromContext` reads `context.Bindings` (rebind menu rows).
- Graph: `InputAction/InputActionPressed/InputActionReleased` (compiled by hand, `GraphCompiler.cs:1989-2078`, palette `sandbox/src/GraphNodeDefs.hpp:520-527`), rebinding family `:537-557`. Table-driven game-system nodes live in `Aver.Graph/GraphGameSystemNodeTable.cs` (`Reg(...)` rows validated by `GameSystemNodes.Validate()`, palette mirror `sandbox/src/GraphNodeDefsGameSystems.hpp`).
- Events: `Events.Subscribe/Dispatch` (`Events.cs`), `AN_OnEvent` entry = bus event named by its `ENTRY` (`GraphTimerEvents.BindGraph`, `GraphTimerEvents.cs:66-80`). A targeted event reaches only subscribers whose owner equals the target (`EventBus.hpp:139`); `target 0` = broadcast.
- Time: `game::tickGameplayGroups(dt)` (`Runtime/include/aver/game/GameTick.hpp:146`) is the single per-frame gameplay entry used by both hosts (`GameApp.cpp:551/556`, `SandboxApp.cpp:2107`); it calls `aver_fw_timers_update(dt)` first. Input is published (`publishInput`, which calls `aver_fw_input_new_frame`) earlier in the frame (`GameApp.cpp:1930`, `SandboxPlay.cpp:792`). While paused `tickGameplayGroups` is not called.

### 1.5 Editor

`sandbox/src/InputSchemeEditor.{hpp,cpp}`: the `.ocinput` asset tab (actions table `drawActions` `:273`, bindings table `drawBindings` `:313`, context, undo via `SnapshotUndo<fmt::OcInputData>`, text-merging save). Registered at `SandboxApp.cpp:453`. No test target exists for it today. The in-game rebind UI is `FrameworkRebindProvider`/`SettingsScreen` (keys only). GraphEditor offers an `action=` picker from the project scheme (`GraphEditor.cpp:1796-1851`, `:2872`).

## 2. Semantics (the part the implementation must reproduce exactly)

### 2.1 Value pipeline

Per action, each evaluation:

1. For each binding `b` of the action (table order): sample as today (`actionAccumulate` switch, key consumption by a strictly higher priority binding on the same key makes the sample 0), giving scalar `s`. Place `s * b.scale` in channel `b.component` of a `Vec3 v` (other channels 0).
2. Apply `b`'s modifiers in listed order to `v`.
3. `Vsum += v`.
4. `V = applyModifiers(action.modifiers, Vsum)`.

`V` is what `value2`/`value3` return. `actuated(v, a)` = any `|channel| > a`. Default actuation `a = 0.15` (the existing pinned dead zone; kept so old actions behave identically).

**Modifier semantics** (all operate on the Vec3, per channel unless noted):

| Modifier | Effect |
|---|---|
| `deadzone:lo:hi:radial` | Radial: `len=|v|`; if `len<lo` -> 0; else `v * (clamp((len-lo)/(hi-lo),0,1)/len)`. Axial: same remap per channel keeping sign. Defaults lo 0.2, hi 1, radial. Per-binding a radial dead zone sees one channel only (so it behaves axially); put the radial one on the ACTION. |
| `scale:x:y:z` | multiply channels; one value = uniform. Distinct from `BIND scale`, which scales the raw source before placement. |
| `negate[:xyz]` | negate the listed channels; default all three. |
| `swizzle:yxz` | output channel i = input channel named by letter i. Orders: xyz, yxz, zyx, xzy, yzx, zxy. |
| `smooth:tau` | stateful exponential filter per channel, `y += (v-y)*(1-exp(-dt/tau))`, `tau=0` = pass-through. State advances only in `aver_fw_action_update`; an on-demand query returns the last updated output; before any update it is a pass-through. |
| `curve:ex:ey:ez` | `sign(v)*|v|^e` per channel; one value = uniform. |

Only `smooth` is stateful. An action containing `smooth` (action or any binding) serves `value2/value3/held/pressed/released` from the last update's `V` and previous-update `V` (cached); all other actions stay on demand.

### 2.2 Trigger evaluation

Each trigger has its own runtime memory (`wasActuated`, timers, counters) and is evaluated only inside `aver_fw_action_update`. Input to a trigger is the owner's value (`v` for a binding trigger, `V` for an action trigger). Trigger result is `N` (None), `O` (Ongoing) or `T` (Triggered), rank N < O < T. `dt` is clamped to [0, 0.25] s inside update so a hitch is not read as a long hold. `act` = the trigger's actuation threshold (default 0.15). `A = actuated(v, act)`.

| Trigger | Spec | Result per update |
|---|---|---|
| Down | `down[:act]` | `A ? T : N` |
| Pressed | `pressed[:act]` | `T` on the update `A` becomes true (`!was`), else `N` |
| Released | `released[:act]` | `O` while `A`; `T` on the update `A` becomes false after being true; else `N` |
| Hold | `hold:sec[:act][:once]` | while `A`: `t+=dt`; `t<sec` -> `O`; `t>=sec` -> `T` every update (with `once`: `T` for exactly one update, then `N` and latched until `A` goes false). `!A`: reset, `N`. |
| HoldAndRelease | `holdrelease:sec[:act]` | while `A`: `t+=dt`, `O`. On the update `A` turns false: `T` if `t>=sec` else `N`. |
| Tap | `tap[:max][:act]` (max default 0.2) | while `A`: `t+=dt`; `t<=max` -> `O`; `t>max` -> `N` (expired, cannot fire until released). On the update `A` turns false: `T` if not expired else `N`. |
| Pulse | `pulse:interval[:limit][:act][:onstart\|:nostart]` (limit 0 = unlimited, default onstart) | on actuation start `acc=0,count=0`. While `A`: `acc+=dt`; fire when `(limit==0 \|\| count<limit) && acc >= (onstart ? count : count+1)*interval` -> `T`, `count++`; between pulses `O`; after `limit` pulses `N` (latched). At most one `T` per update. `!A`: reset, `N`. |
| Chorded | `chord:Action` (IMPLICIT) | `T` if `actuated(v,0.15)` and the named action's state this update is `T`, else `N`. The named action is evaluated first (recursive, memoised per update); a cycle reads that action's previous-update state. |
| Combo | `combo:window:A,B,C` | ignores the owner's value. Step `k` advances when action `steps[k]` ROSE (state `T` now, not `T` last update) with timer reset; if `k>0`, `timer+=dt` and `timer>window` resets `k=0`; a rise of any other step action resets to 0 (to 1 if it is `steps[0]`). When `k==n`: `T` for that update, then reset. `k>0` -> `O`, else `N`. |

**Combining triggers on one owner.** Chorded is implicit; all others are explicit. `explicit = max rank of explicit triggers` (neutral `T` if there are none), `implicit = min rank of implicit triggers` (neutral `T` if none), `result = min(explicit, implicit)`. An owner with no trigger of either kind uses an implicit `down:0.15`.

**Binding vs action state.** Let `bState` = max over trigger-bearing bindings of their result. If the action has its own triggers: `A_state = evaluate(action triggers, V)`. Else if no binding has triggers: `A_state = down(V)` (this is the legacy behaviour exactly). Else `A_state = N`, and every trigger-LESS binding in that action contributes its own `down(v)` to `bState` (so "Space = tap, gamepad A = plain" works). **Action state `S = max(A_state, bState)`.**

**Events** from `prevS -> S` (table, implemented as a 3x3 lookup):

| prev -> now | mask |
|---|---|
| N -> N | 0 |
| N -> O | Started \| Ongoing |
| N -> T | Started \| Triggered |
| O -> O | Ongoing |
| O -> T | Triggered |
| O -> N | Canceled |
| T -> T | Triggered |
| T -> O | Ongoing |
| T -> N | Completed |

`elapsed` = continuous seconds `S != N` (0 when N); `triggeredTime` = continuous seconds `S == T`. A plain key action: press = Started+Triggered, held = Triggered each update, release = Completed. A tap: press = Started+Ongoing, quick release = Triggered, next update = Completed.

### 2.3 Timing and compatibility guarantees

- `aver_fw_action_update(dt)` is called exactly once per gameplay frame, after `publishInput`, as the FIRST statement of `game::tickGameplayGroups` (before `aver_fw_timers_update`). Hosts that never call it still get every legacy query; the new `state/events/elapsed/triggered_time` then read `None/0`. There is deliberately NO lazy auto-update on query (a dt=0 pre-update would consume edge memory and lose a Pressed).
- Paused: `tickGameplayGroups` is not called, so trigger state and the last event mask freeze; nothing is re-dispatched (the event pump runs inside the gameplay tick).
- `aver_fw_action_clear_bindings` drops bindings and their trigger/modifier memory; action state and action-level config survive (so a context push turns a held action's value to 0 and yields a natural Completed/Canceled). `aver_fw_action_reset_state()` zeroes all runtime memory and event lists; `InputScheme.Load` calls it at every Play start.
- `FrameworkTest::testActions` (`:843`) and `testInputScheme` (`:944`) must pass untouched.

## 3. `.ocinput` syntax

Spec tokens contain no whitespace, `#` or `;`; keywords are case-insensitive; numbers are decimal floats; referenced action names must not contain `:`, `,` or whitespace.

```
trigger  := down[:act] | pressed[:act] | released[:act]
          | hold:sec[:act][:once] | holdrelease:sec[:act] | tap[:max][:act]
          | pulse:interval[:limit][:act][:onstart|:nostart]
          | chord:ActionName | combo:window:Action,Action[,Action...]
modifier := deadzone[:lo[:hi]][:radial|:axial] | scale:x[:y[:z]] | negate[:xyz-subset]
          | swizzle:(xyz|yxz|zyx|xzy|yzx|zxy) | smooth:tau | curve:ex[:ey[:ez]]
```

Positional numbers fill in the order shown; the flags `once/onstart/nostart/radial/axial` may appear at any position. Validation (parser errors, file rejected): sec/max/window >= 0 (`hold` 0 allowed, `window > 0`, `interval > 0`), `act >= 0`, `limit >= 0` integer, `0 <= lo < hi`, `tau >= 0`, exponents `> 0`, combo needs >= 2 steps, chord/combo names must be declared ACTIONs (post-pass, like BIND), a chord may not name its own action.

Attachment: repeatable trailing `keyword value` pairs on both records (same pair grammar BIND already uses; `ACTION` gains pairs, its 3-token rule becomes `size>=3 && (size-3)%2==0`).

```
OCINPUT 1
NAME OnFoot
ACTION Move   axis2   modifier deadzone:0.2:1:radial
ACTION Look   axis2   modifier smooth:0.04
ACTION Jump   digital
ACTION Fire   digital
ACTION Charge digital trigger hold:0.6
ACTION Dash   digital trigger tap:0.18
ACTION Auto   digital trigger pulse:0.12
ACTION Save   digital trigger pressed trigger chord:Mod
ACTION Mod    digital
ACTION Combo1 digital trigger combo:0.4:Fire,Fire,Jump

BIND Move gamepadaxis LeftX
BIND Move gamepadaxis LeftY scale -1 component y
BIND Move key W component y
BIND Move key S scale -1 component y
BIND Look mousex scale 0.5 modifier curve:1.4
BIND Fire key MouseLeft
BIND Jump key Space trigger pressed
BIND Charge gamepadbutton RightShoulder
BIND Save key S
BIND Mod key LeftCtrl
CONTEXT OnFoot priority 5
```

Writer: canonical (`formatTriggerSpec/formatModifierSpec`: lowercase kind, `%.6g`, omit trailing defaults), always `trigger`s then `modifier`s, after `scale`/`component`. Files with none of the new keywords round-trip byte-identically to today.

## 4. Formats API (`modules/formats`)

`OcInput.hpp` additions (all in `aver::fmt`; implementation `OcInput.cpp`):

```cpp
enum class OcTriggerKind : i32 { Down, Pressed, Released, Hold, HoldRelease, Tap, Pulse, Chord, Combo };
struct OcTriggerSpec {
    OcTriggerKind kind = OcTriggerKind::Down;
    f32  actuation = 0.15f;
    f32  time = 0.0f;              // hold/holdrelease: seconds; tap: max; pulse: interval; combo: window
    i32  limit = 0;                // pulse
    bool once = false;             // hold
    bool onStart = true;           // pulse
    std::vector<std::string> refs; // chord: 1 name; combo: steps
};
enum class OcModifierKind : i32 { DeadZone, Scale, Negate, Swizzle, Smooth, Curve };
struct OcModifierSpec {
    OcModifierKind kind = OcModifierKind::DeadZone;
    f32 v[3] = {0.2f, 1.0f, 0.0f}; // deadzone lo,hi; scale/curve xyz; smooth tau in v[0]
    u8  mask = 7;                  // negate: bit0 x, bit1 y, bit2 z
    i32 mode = 0;                  // deadzone 0 radial 1 axial; swizzle 0..5 = xyz,yxz,zyx,xzy,yzx,zxy
};
bool parseTriggerSpec (std::string_view spec, OcTriggerSpec&  out, std::string* err = nullptr);
bool parseModifierSpec(std::string_view spec, OcModifierSpec& out, std::string* err = nullptr);
std::string formatTriggerSpec (const OcTriggerSpec&);   // canonical; parse(format(x)) == x
std::string formatModifierSpec(const OcModifierSpec&);
```

Struct changes: `OcInputAction` and `OcInputBinding` each gain `std::vector<std::string> triggers, modifiers;` holding CANONICAL spec text (already validated; the format never stores half-parsed data). Parser: `ACTION`/`BIND` loops accept `trigger`/`modifier` pairs (each value through `parse*Spec`, stored via `format*Spec`; error text prefixed `BIND trigger:` etc.). Post-pass: every `chord`/`combo` ref must name a declared ACTION. Writer: `actionLine`/`bindLine` (`OcInput.cpp:143/132`) append the pairs. Update the header comment grammar block (`OcInput.cpp:1-40`) and the "C# loader not written" stale paragraph in `OcInput.hpp:19-24`.

## 5. C ABI (`framework_abi.h` + `FrameworkAbi.cpp`), minor 11

Version block: add `11: INPUT TRIGGERS AND MODIFIERS (aver_fw_action_bind_ex/add_trigger/add_modifier/clear_config/update/reset_state/state/events/elapsed/triggered_time/value3/count/name/value_type/event_count/event_at/config_error; aver_fw_input_scheme_trigger_count/trigger_spec/modifier_count/modifier_spec)`; `#define AVER_FW_ABI_VERSION_MINOR 11`.

Constants (note the prefixes: they must not start with `AVER_FW_ACTION_`, which `AbiEnumTest` already maps to `InputValueType`):

```c
#define AVER_FW_TRIGGER_STATE_NONE      0
#define AVER_FW_TRIGGER_STATE_ONGOING   1
#define AVER_FW_TRIGGER_STATE_TRIGGERED 2
#define AVER_FW_INPUT_EVENT_STARTED     1
#define AVER_FW_INPUT_EVENT_ONGOING     2
#define AVER_FW_INPUT_EVENT_TRIGGERED   4
#define AVER_FW_INPUT_EVENT_COMPLETED   8
#define AVER_FW_INPUT_EVENT_CANCELED   16
```

Functions (placed at the end of the NAMED ACTIONS section, after `aver_fw_action_released`; scheme getters at the end of the INPUT SCHEME section):

```c
/* Like aver_fw_action_bind but returns the binding id (index into the binding table; valid until the
 * next aver_fw_action_clear_bindings), or -1 when rejected. aver_fw_action_bind becomes a thin call to it. */
AVER_FW_ABI int32_t aver_fw_action_bind_ex(int32_t action, int32_t source, int32_t key, float scale,
                                           int32_t component, int32_t contextPriority);
/* Appends a trigger/modifier spec (section 3 grammar) to a binding (binding >= 0, which must belong to
 * `action`) or to the action itself (binding == -1). 1 on success; 0 on a bad handle, bad binding, or
 * a spec the parser rejects (aver_fw_action_config_error carries why). Modifiers apply in call order.
 * A chord/combo name not yet registered is registered later-bound: it resolves by name on the first
 * update and reads None until it exists. */
AVER_FW_ABI int32_t aver_fw_action_add_trigger (int32_t action, int32_t binding, const char* spec);
AVER_FW_ABI int32_t aver_fw_action_add_modifier(int32_t action, int32_t binding, const char* spec);
AVER_FW_ABI const char* aver_fw_action_config_error(void);
/* Drops the action-level triggers and modifiers (not bindings, not its binding-level config). */
AVER_FW_ABI void    aver_fw_action_clear_config(int32_t action);
/* Advances every action once. Call once per gameplay frame after the input is published. */
AVER_FW_ABI void    aver_fw_action_update(float dt);
/* Zeroes all trigger/modifier memory, states, elapsed times and the event list. Config survives. */
AVER_FW_ABI void    aver_fw_action_reset_state(void);
/* Results of the last aver_fw_action_update. 0 / 0.0f for an invalid handle. */
AVER_FW_ABI int32_t aver_fw_action_state(int32_t action);           /* AVER_FW_TRIGGER_STATE_* */
AVER_FW_ABI int32_t aver_fw_action_events(int32_t action);          /* AVER_FW_INPUT_EVENT_* mask */
AVER_FW_ABI float   aver_fw_action_elapsed(int32_t action);
AVER_FW_ABI float   aver_fw_action_triggered_time(int32_t action);
/* {X,Y,Z} of the modified value (aver_fw_action_value2 keeps returning X,Y only). */
AVER_FW_ABI void    aver_fw_action_value3(int32_t action, float* out3);
/* Registry readback (fills the gap noted in InputAction.Find). */
AVER_FW_ABI int32_t     aver_fw_action_count(void);
AVER_FW_ABI const char* aver_fw_action_name(int32_t action);        /* valid until process end; nullptr invalid */
AVER_FW_ABI int32_t     aver_fw_action_value_type(int32_t action);  /* AVER_FW_ACTION_DIGITAL..; -1 invalid */
/* The actions that raised any event in the last update, for the managed pump. */
AVER_FW_ABI int32_t aver_fw_action_event_count(void);
AVER_FW_ABI int32_t aver_fw_action_event_at(int32_t index, int32_t* outAction, int32_t* outMask);

/* Scheme getters. owner >= 0: binding index. owner < 0: action index is (-1 - owner) (the bitwise not). */
AVER_FW_ABI int32_t     aver_fw_input_scheme_trigger_count (int32_t owner);
AVER_FW_ABI const char* aver_fw_input_scheme_trigger_spec  (int32_t owner, int32_t index);
AVER_FW_ABI int32_t     aver_fw_input_scheme_modifier_count(int32_t owner);
AVER_FW_ABI const char* aver_fw_input_scheme_modifier_spec (int32_t owner, int32_t index);
```

Scheme strings stay valid until the next `aver_fw_input_scheme_load` (same rule as the section).

### 5.1 Implementation shape (native)

- New internal files `modules/framework/src/ActionEngine.hpp/.cpp` (add to `modules/framework/CMakeLists.txt` source list; not exported, not in `include/`): pure functions, no dependency on `InputState`.
  `Vec3f applyModifiers(const std::vector<fmt::OcModifierSpec>&, Vec3f v, float dt, ModRuntime* rt /*null = stateless*/)`,
  `Rank stepTrigger(const fmt::OcTriggerSpec&, TriggerRuntime&, Vec3f v, float dt, const TriggerContext&)` (the context supplies "state of action X this update", "rose(X)"),
  `Rank combineTriggers(...)`, `int eventsFor(Rank prev, Rank now)`.
- `FrameworkAbi.cpp` (existing structs extended, existing functions kept in place): `ActionBinding` gains `std::vector<fmt::OcTriggerSpec> triggers; std::vector<fmt::OcModifierSpec> modifiers;` plus parallel runtime vectors; `ActionDef` gains the same action-level lists, `Rank state, prevState; float elapsed, triggeredTime; Vec3f V, Vprev; int events; bool hasSmooth; uint32 evalSeq; bool busy;`. `actionAccumulate` is refactored to produce the per-binding Vec3 (stateless modifiers applied; `smooth` returns the stored output) and sums it; with empty modifier lists its output is bit-identical to today's. `actionActive` keeps its 0.15. `aver_fw_action_update` = clamp dt, bump `evalSeq`, evaluate all actions in registration order via `evalAction(a)` (memoised, `busy` cycle guard), rebuild the event list.
- Chord/combo name -> handle resolution is cached per trigger runtime and retried each update until found.
- `aver_fw_input_new_frame` is untouched.

## 6. C# API (`scripting/csharp/Aver.Framework`)

New P/Invokes go in a NEW file `InputNative.cs` (`internal static class FwIn`, `Lib = "Aver.Framework"`, same pattern as `TimersEventsNative.cs`'s `FwTE`); `Native.cs` is not touched (shared by other features).

### 6.1 `InputTriggers.cs` (new)

```csharp
namespace Aver.Framework;
public enum TriggerState { None = 0, Ongoing = 1, Triggered = 2 }
[Flags] public enum ActionEvent { None = 0, Started = 1, Ongoing = 2, Triggered = 4, Completed = 8, Canceled = 16 }
public enum DeadZoneType { Radial, Axial }
public enum SwizzleOrder { XYZ, YXZ, ZYX, XZY, YZX, ZXY }

public readonly struct InputTrigger {
    public string Spec { get; }
    public static InputTrigger FromSpec(string spec);                       // unvalidated; the native add rejects bad text
    public static InputTrigger Down(float actuation = 0.15f);
    public static InputTrigger Pressed(float actuation = 0.15f);
    public static InputTrigger Released(float actuation = 0.15f);
    public static InputTrigger Hold(float seconds, bool once = false, float actuation = 0.15f);
    public static InputTrigger HoldAndRelease(float seconds, float actuation = 0.15f);
    public static InputTrigger Tap(float maxSeconds = 0.2f, float actuation = 0.15f);
    public static InputTrigger Pulse(float interval, int limit = 0, bool triggerOnStart = true, float actuation = 0.15f);
    public static InputTrigger Chorded(InputAction other);
    public static InputTrigger Combo(float window, params InputAction[] steps);
}
public readonly struct InputModifier {
    public string Spec { get; }
    public static InputModifier FromSpec(string spec);
    public static InputModifier DeadZone(float lower = 0.2f, float upper = 1f, DeadZoneType type = DeadZoneType.Radial);
    public static InputModifier Scale(float uniform);
    public static InputModifier Scale(float x, float y, float z);
    public static InputModifier Negate(bool x = true, bool y = true, bool z = true);
    public static InputModifier Swizzle(SwizzleOrder order);
    public static InputModifier Smooth(float timeConstantSeconds);
    public static InputModifier ResponseCurve(float exponent);
    public static InputModifier ResponseCurve(float x, float y, float z);
}
```

Factories format with `CultureInfo.InvariantCulture`, `"R"`/shortest round-trip, canonical form identical to `formatTriggerSpec` (omit defaults) so a spec compares equal to what the native side would print.

### 6.2 `EnhancedInput.cs` changes

- `InputBinding` stays an `internal readonly struct` but gains `public readonly string[] Triggers, Modifiers` (never null; `Array.Empty<string>()`), an extended constructor (`...int component, string[]? triggers = null, string[]? modifiers = null`) and `InputBinding WithKey(InputSource s, int rawKey)` returning a copy that keeps scale/component/triggers/modifiers. **Every `new InputBinding(` that rewrites a key must become `b.WithKey(...)`** (`LoadBindings` `:339`, `RebindAction` `:515`) so a rebind or a saved rebind never drops a trigger. `ResetToDefaults` restores them for free (default list copies the structs).
- `InputMappingContext`: the `BindKey/BindGamepadButton/BindGamepadAxis/BindMouseWheel` protected methods change return type from `void` to `BindingHandle` (source-compatible for callers). `BindAxis1D/2D/MouseLook` stay `void` (modifiers on those go through the per-key calls or action-level config).
  ```csharp
  public readonly struct BindingHandle {
      public BindingHandle Trigger(InputTrigger t);    // replaces the context's list entry with a copy that has t appended
      public BindingHandle Modify(InputModifier m);
  }
  ```
- `EnhancedInput.Rebind()` `:412` uses `int id = Fw.aver_fw_action_bind_ex(...)` then `FwIn.aver_fw_action_add_trigger(handle, id, spec)` / `_add_modifier` for each spec. `EnhancedInput` gains `public static void PumpEvents()` (6.4).
- `InputAction` gains:
  ```csharp
  public InputAction AddTrigger(InputTrigger t);   // action-level, lives natively, survives Rebind(); returns this
  public InputAction AddModifier(InputModifier m);
  public InputAction ClearConfig();
  public TriggerState State { get; }  public ActionEvent Events { get; }
  public float ElapsedSeconds { get; } public float TriggeredSeconds { get; }
  public bool WasStarted, IsOngoing, IsTriggered, WasCompleted, WasCanceled { get; }   // from Events/State
  public Vec3 Value3D { get; }                      // aver_fw_action_value3; Value2D now also fills Z
  public event Action<InputAction>? Started, Ongoing, Triggered, Completed, Canceled;
  public static InputAction? Find(string name);     // now works for natively registered actions (uses count/name/value_type)
  ```
  `IsHeld/WasPressed/WasReleased` keep their meaning (raw actuation of the modified value) and are documented as such; `IsTriggered` is the trigger-aware one.

### 6.3 `InputScheme.cs`

`Load` calls `FwIn.aver_fw_action_reset_state()` first, registers all actions (already does), then for each action `ClearConfig()` and applies `aver_fw_input_scheme_trigger_spec(~actionIndex, i)` / modifier specs, and for each binding passes `aver_fw_input_scheme_trigger_spec(bindingIndex, i)` into the `SchemeContext.AddBinding` (new optional `triggers`/`modifiers` args). An action whose spec the framework rejects is logged (`Log.Warn`, with `aver_fw_action_config_error`) and skipped, never fatal, the same tolerance as an unknown key name.

### 6.4 Event delivery (managed pump)

`EnhancedInput.PumpEvents()`: for `i < aver_fw_action_event_count()`: `aver_fw_action_event_at(i, out a, out mask)`; resolve the `InputAction` wrapper (`InputAction.FromHandle`, builds one via `name/value_type` when no C# code made it); raise the C# events for each set bit in the order Started, Ongoing, Triggered, Completed, Canceled; then, only when `Events.ListenerCount(name) > 0`, `Events.Dispatch("Input.<Action>.<Event>", sender: aver_fw_player_controller(0), target: 0, x, y, elapsed, triggeredSeconds)` (four floats, so `AN_EventPayload` index 0..3 reads `f` = x, y, elapsed, triggered; z via `InputActionState`). Broadcast (`target 0`) is deliberate: owner-0 C# subscribers and every graph with that entry hear it. Handler exceptions are caught and logged per handler.

Called from `HostBridge.DispTickAll` `if (group == 0)` branch, right after `UiSystem.Pump` (`HostBridge.cs:1463-1469`), inside the existing try/catch style. One line; `PumpEvents` is also `public` so a C#-only host or test can call it.

## 7. Graph nodes (graph-node agent implements from this list)

Mechanism: table-driven (`GraphGameSystemNodeTable.cs` rows + `GraphNodeDefsGameSystems.hpp` palette rows, `Validate()` checks signatures). Owner class: new `internal static class InputGraph` in new file `scripting/csharp/Aver.Framework/InputGraph.cs` (internal, `InternalsVisibleTo Aver.Graph` already exists). Strings are NODE-line attributes (`action=`, `trigger=`, ...), so graphs address actions by NAME (no handle pin). Category `Input` (palette) except the entry node.

| Node | Kind | Attrs | In pins | Out pins | Wraps |
|---|---|---|---|---|---|
| `InputActionState` | Pure (D) | `action=` required | - | `state` int (0/1/2), `started`,`ongoing`,`triggered`,`completed`,`canceled` bool, `elapsed`,`triggeredTime`,`x`,`y`,`z` float, `success` bool | `bool InputGraph.ActionStateForGraph(string action, out int state, out bool started, out bool ongoing, out bool triggered, out bool completed, out bool canceled, out float elapsed, out float triggeredTime, out float x, out float y, out float z)` (`aver_fw_action_find` + `state/events/elapsed/triggered_time/value3`; false and zeros for an unknown action). Row Args: `@action,>state,>started,>ongoing,>triggered,>completed,>canceled,>elapsed,>triggeredTime,>x,>y,>z`, Ret `success`. |
| `AddInputTrigger` | Exec (P) | `action=`, `trigger=` required | `exec` | `then`, `success` bool | `bool InputGraph.AddTriggerForGraph(string action, string spec)` (action-level `aver_fw_action_add_trigger(h,-1,spec)`). Args `@action,@trigger`. |
| `AddInputModifier` | Exec (P) | `action=`, `modifier=` required | `exec` | `then`, `success` | `bool InputGraph.AddModifierForGraph(string action, string spec)`. |
| `ClearInputConfig` | Exec (P) | `action=` required | `exec` | `then`, `success` | `bool InputGraph.ClearConfigForGraph(string action)`. Use before the Add nodes in `OnStart` (adds are not idempotent). |
| `OnInputAction` | Trigger (X, category `Event`) | `action=`, `event=` (Started/Ongoing/Triggered/Completed/Canceled; palette default `Triggered`) | - | `exec` (out) | no method. Same runtime as `AN_OnEvent`: `GraphTimerEvents.BindGraph` accepts either node type (`GraphTimerEvents.cs:73` compare). The ENTRY name is `Input.<action>.<event>`; the editor composes and keeps it in sync exactly the way it does for `CustomEvent` `name=` (`GraphEditor.cpp:592-600`, `:1477-1480`, `:1506`: on spawn and on `action`/`event` attribute edits call `syncEventEntry(id, "Input." + action + "." + event)`, or `""` while either is empty). Hand-authored equivalent that needs no editor support: `NODE e AN_OnEvent` + `ENTRY e Input.Jump.Triggered`. |

Palette rows are written with the same pins in the same order as the table (`pin("exec","exec",false)` etc., per file header). The `action=` combo picker in `GraphEditor.cpp:2872` (`row.key == "action"` branch) already applies to any node declaring an `action` attribute, so the new nodes get it for free; update its comment.

Existing nodes `InputAction/InputActionPressed/InputActionReleased` and the rebinding family are unchanged. Docs: `docs/AVER_NODE_NODES.md` "Input" table gets the four rows; "Events" gets `OnInputAction`.

## 8. Editor

`sandbox/src/InputSchemeEditor.hpp/.cpp` (only extension point; no new panel):

- New setters (headless-testable, each `pushUndo()`, `dirty_`, same clamp style as the existing ones):
  ```cpp
  enum class SpecKind { Trigger, Modifier };
  struct SpecOwner { bool isAction; usize index; };      // action index or binding index
  bool addSpec   (SpecOwner o, SpecKind k, std::string spec);          // false + specError_ when parse*Spec rejects
  bool setSpec   (SpecOwner o, SpecKind k, usize i, std::string spec);
  void removeSpec(SpecOwner o, SpecKind k, usize i);
  void moveSpec  (SpecOwner o, SpecKind k, usize i, int delta);        // modifier order matters
  const std::string& specError() const;
  ```
  `removeAction`/`renameAction` also cascade into chord/combo refs (rename rewrites the ref; remove deletes any trigger that references the removed action, otherwise the file would not reload).
- `drawActions`: add a fourth column "Config" showing the count and a `TreeNode` row under each action listing its triggers then modifiers; `drawBindings` the same per binding. Each entry is edited structurally, not as free text: a kind combo, then the kind's fields (`DragFloat` for seconds/actuation/lo/hi/tau/exponents, checkbox for once/onstart, axis checkboxes for negate, combo for dead-zone type and swizzle order, an action combo for chord and a list of action combos for combo steps). The spec string is produced by `fmt::formatTriggerSpec/format ModifierSpec`; the raw text is shown read-only as a tooltip. Add/remove/move-up/move-down buttons per entry.
- Live column: when `aver_fw_play_state() == AVER_FW_PLAY_PLAYING`, the actions table shows `state` (colour) and `elapsed` from `aver_fw_action_state/elapsed` (sandbox already links Aver.Framework). Pure read, no side effect.
- The `.ocinput` text-merge save (`writeOcinput(data_, originalText_)`) needs no change beyond section 4.
- The in-game rebind screen (`FrameworkRebindProvider`) is unchanged: it lists one row per key slot and rebinds keys only; triggers/modifiers are not user-editable at run time.

## 9. Runtime and packaged-game wiring

- `Runtime/include/aver/game/GameTick.hpp:146`: first line of `tickGameplayGroups` becomes `aver_fw_action_update(dt);` (under the existing `AVER_MODULE_FRAMEWORK` guard). That covers `GameApp` (packaged game) and `SandboxApp` (editor Play) with one edit.
- `HostBridge.DispTickAll` group 0: `EnhancedInput.PumpEvents();` (section 6.4).
- Loader: unchanged call sites (`GameApp.cpp:1174`, `SandboxPlay.cpp:304`); `InputScheme.Load` does the reset + config (6.3).
- Packaged games need no new file: the scheme is data already staged via `INPUT.SCHEME`; the framework DLL carries the engine.
- Vulkan/D3D12: n/a. Threading: main thread only, same as the rest of the input layer.
- Save/load: no `.ocsave` interaction (input state is not saved). Settings keys for rebinds are unchanged (`Rebind.<ctx>.<action>.<slot>.Source/.Key`), triggers/modifiers are not persisted, so editing a scheme's triggers takes effect even when the player has saved rebinds.

## 10. Tests

All native tests are executables built from the existing targets (the repo runs `*Test.exe`; none is `ctest`-registered by name). New code goes into existing files unless stated.

**`tests/formats/src/OcInputTest.cpp`** (target `OcInputTest`): `testTriggerModifierGrammar`, asserts: every trigger and modifier kind parses with exact field values (`hold:0.5:0.3:once` -> time 0.5, actuation 0.3, once; `pulse:0.25:3:nostart`; `deadzone:0.1:0.9:axial`; `negate:xz` -> mask 5; `swizzle:zyx` -> mode 2; one-value `scale:2`/`curve:2` expand to uniform); `parse(format(x))==x` and `format(parse(s))==s` for canonical text over a table of 30 specs; rejections with named errors (negative seconds, `lo>=hi`, unknown kind, unknown flag, `combo` with 1 step, trailing `:`, whitespace); `ACTION … trigger … modifier …` and `BIND … trigger … modifier …` parse in either order with `scale`/`component`; chord/combo naming an undeclared action rejects the file; a chord naming its own action rejects; write->parse round trip including a hand comment preserved by the merge path; a file with no new keywords writes byte-identically to the pre-change writer output (golden string from the existing `kFull`).

**`tests/framework/src/FrameworkTest.cpp`** (target `FrameworkTest`; call each from `main` after `testInputScheme`, `:1034`; every new export must be driven here): `testActionTriggers`, `testActionModifiers`, `testActionEvents`, `testActionChordCombo`, `testInputSchemeTriggers`, `testActionLegacyUntouched`. Each helper drives a tiny `step(keysDown, dt)` that does `aver_fw_input_new_frame()`, `set_key`, `aver_fw_action_update(dt)`. Assertions:
- Down/Pressed/Released: Pressed is `T` for exactly one update; Released is `O` while held, `T` on release.
- Hold 0.5: 0.4 s held -> `O`, events Started+Ongoing then Ongoing; 0.6 s -> `T` with Triggered each update; `once` -> one Triggered update then `N` and Completed, no retrigger until released and pressed again; release at 0.3 s -> Canceled.
- HoldAndRelease: no `T` while held; `T` on release only if >= time.
- Tap 0.2: 0.1 s press+release -> `T` on release; 0.3 s hold -> `N` at expiry (Canceled) and release does not fire.
- Pulse 0.25 limit 3 onstart: `T` at t=0, 0.25, 0.5 only; `nostart`: first `T` at 0.25; at most one `T` per update with a 1.0 s dt (clamped 0.25).
- Chord: `Save = S` with `chord:Mod` and `pressed`: S alone -> `N`; Mod then S -> `T` once; S then Mod -> `N`.
- Combo `0.4:Fire,Fire,Jump`: in-time sequence -> `T` once; gap > window -> back to `N` (Canceled); wrong action in the middle -> reset; a cycle (A chords B, B chords A) does not hang and reads `N`.
- Modifiers: radial dead zone 0.2 on (0.15,0.15)->0 and on (0.6,0) -> 0.5; axial independent per channel; negate `y`; swizzle `yxz` moves a W binding's component; scale; curve 2 on 0.5 -> 0.25; smooth tau 0.1: step input reaches ~63% after 0.1 s of updates, `tau 0` passes through, on-demand `value2` between updates returns the last updated value.
- Binding vs action rules: a plain-Space + hold-gamepad-A action fires from either; an action trigger plus a plain binding uses the action trigger only; trigger-less everything == legacy answers on the same scripted key sequence as `testActions`.
- Events table: every row of the 3x3 table is produced by a scripted transition; `elapsed/triggeredTime` accumulate and reset; `aver_fw_action_event_count/at` list exactly the actions with events; `reset_state` clears them.
- Registry readback: `count/name/value_type` for registered actions, invalid handle behaviour (`nullptr`, -1, zeros).
- `testInputSchemeTriggers`: writes a temp `.ocinput` with action- and binding-level triggers/modifiers; `scheme_trigger_count/spec` and `modifier_*` read back canonical text for `owner = bindingIndex` and `owner = ~actionIndex`; out-of-range -> 0 / nullptr; then binds from them via `bind_ex/add_*` and steps a hold to `T`.
- `bind_ex` returns increasing ids, -1 for a rejected bind; `add_*` returns 0 for a binding of another action and for a bad spec (`config_error` non-empty).

**`tests/abi/src/AbiEnumTest.cpp`**: two `Group` rows after `:716`: `{"trigger states", framework_abi.h, "AVER_FW_TRIGGER_STATE_", EnhancedInput/InputTriggers.cs -> "InputTriggers.cs", "TriggerState", ""}` and `{"input events", framework_abi.h, "AVER_FW_INPUT_EVENT_", "InputTriggers.cs", "ActionEvent", ""}`. The C# enums use explicit values (the test reads `= n`). Verify no other row's prefix collides.

**`tests/game/src/InputBridgeTest.cpp`** (target `InputBridgeTest`, links Runtime.Game.Core): one wiring case: register an action with `hold:0.5`, publish W through `publishInput`, call `game::tickGameplayGroups(0.6f)` once, assert `aver_fw_action_state == TRIGGERED` (proves `GameTick.hpp` calls the update).

**`tests/editor`** (new target `InputSchemeEditorTest`, FoliageTypeEditorTest arrangement: compile `sandbox/src/InputSchemeEditor.cpp`, no `AVER_WITH_IMGUI`, link `Aver.Formats Aver.Core Aver.Platform`; add it to `tests/editor/CMakeLists.txt` and create `tests/editor/src/InputSchemeEditorTest.cpp`): load/save/dirty/undo round trip; `addSpec/setSpec/removeSpec/moveSpec` on action and binding; rejected spec leaves data and undo history untouched and sets `specError()`; rename/remove of an action cascades into chord/combo refs and the saved file reloads through `parseOcinput`; starter data from `inputSchemeStarterData()` still saves identically; a comment line in the original text survives a save.

**`scripting/csharp/Aver.Graph.Tests/EnhancedInputTests.cs`** (new; register `failures += EnhancedInputTests.RunAll();` in `Program.cs` beside `CustomEventTests`): managed-only (no native call): factory output strings (`InputTrigger.Hold(0.5f).Spec == "hold:0.5"`, `Hold(0.5f, once: true) == "hold:0.5:once"`, `Combo(0.4f, a, b)` joins names, culture-invariant under a `de-DE` current culture), canonical equality with the C++ writer's examples copied as literals, `InputBinding.WithKey` keeps triggers/modifiers (internal via `InternalsVisibleTo Aver.Graph`... if the tests project lacks the grant, assert through a public path instead), `GameSystemNodes.Validate()` returns no problems with the new rows, parser pins for the four nodes (default pins), and `OnInputAction` + `AN_OnEvent` both bind in `GraphTimerEvents.BindGraph` (assert the subscribed event name `Input.Jump.Triggered`).

**Headed verification (main loop, not a unit test):** add `Content/Input/Default.ocinput` to a scratch project with `Jump trigger tap`, `Charge trigger hold:0.6`, `Move modifier deadzone`; Play in the editor; confirm with `--input-echo` and the live column that holds time correctly, that a saved rebind of Jump's key keeps the tap trigger, and that a graph with `OnInputAction Charge Triggered` prints once per frame while held. Per repo practice the user verifies the feel.

## 11. Files

Owned by THIS feature (exclusive unless marked shared):

| File | Change |
|---|---|
| `modules/formats/include/aver/formats/OcInput.hpp`, `modules/formats/src/OcInput.cpp` | section 4 |
| `tests/formats/src/OcInputTest.cpp` | tests |
| `modules/framework/include/aver/framework/framework_abi.h` | **shared file**: only the version block (this feature owns minor 11; if another wave-2 feature needs a framework ABI bump it takes 12), NAMED ACTIONS end, INPUT SCHEME end |
| `modules/framework/src/FrameworkAbi.cpp` | **shared file**: only the NAMED ACTIONS (`:1245-1436`) and INPUT SCHEME (`:1572-1668`) regions |
| `modules/framework/src/ActionEngine.hpp`, `ActionEngine.cpp` (new), `modules/framework/CMakeLists.txt` | engine, source list |
| `Runtime/include/aver/game/GameTick.hpp` | one line (`aver_fw_action_update(dt)`) |
| `scripting/csharp/Aver.Framework/EnhancedInput.cs`, `InputScheme.cs` | section 6 |
| `scripting/csharp/Aver.Framework/InputTriggers.cs`, `InputNative.cs`, `InputGraph.cs` (new) | section 6, 7 |
| `scripting/csharp/Aver.Scripting.Bridge/HostBridge.cs` | **shared**: one line in `DispTickAll` group 0 |
| `scripting/csharp/Aver.Graph/GraphTimerEvents.cs` | accept `OnInputAction` in `BindGraph` |
| `scripting/csharp/Aver.Graph/GraphGameSystemNodeTable.cs` | **shared across wave-2 graph-node agents**: four rows in one marked block `// ---- Enhanced Input (#19)` |
| `sandbox/src/GraphNodeDefsGameSystems.hpp` | **shared**: four palette rows in a marked block (+ `OnInputAction` in category "Event") |
| `sandbox/src/GraphEditor.cpp` | **shared**: `OnInputAction` ENTRY sync (3 call sites) and the picker comment |
| `sandbox/src/InputSchemeEditor.hpp/.cpp` | section 8 |
| `tests/framework/src/FrameworkTest.cpp`, `tests/game/src/InputBridgeTest.cpp`, `tests/abi/src/AbiEnumTest.cpp`, `tests/editor/CMakeLists.txt`, `tests/editor/src/InputSchemeEditorTest.cpp` (new), `scripting/csharp/Aver.Graph.Tests/EnhancedInputTests.cs` (new) + `Program.cs` (one line) | tests |
| `docs/ABI.md` (named-actions + input-scheme sections only), `docs/SCRIPTING_API.md` ("Action mappings" only), `docs/AVER_NODE_NODES.md` ("Input" and "Events" tables only) | docs |

NOT touched by this feature (belong to the other wave-2 features): `Aver.Framework` abilities/GAS files, `modules/anim*`, data-table/localization formats and UI text files, `Native.cs`, `modules/rhi.vulkan`.

Interfaces other features may rely on: C# `InputAction.Started/Triggered/Completed/Canceled` events and `InputAction.State/Events` (GAS can bind abilities to them); bus event names `Input.<Action>.<Event>`.

Coordination: the three "shared" graph/editor files (`GraphGameSystemNodeTable.cs`, `GraphNodeDefsGameSystems.hpp`, `GraphEditor.cpp`) are also touched by other wave-2 graph-node agents. Run the graph-node agents one after another, or give each its own marked block and merge by hand.

Suggested implementation order (each step builds and passes before the next): formats (4) + OcInputTest -> engine + ABI + FrameworkTest legacy/regression -> triggers/modifiers tests -> C# (6) -> GameTick/HostBridge wiring + InputBridgeTest -> graph nodes (7) -> editor (8) -> docs. Roughly one workflow of 3 to 4 agents; native engine is the only hard part.

## 12. Risks and open questions

1. **Event audience.** Bus events broadcast to every entity with a matching entry, not only the possessed pawn/controller. Simple and works with owner-0 C# subscribers; the cost is that an AI graph that declared `OnInputAction` would hear the player's input. Owner choice: keep broadcast (recommended; authors put these entries on the pawn/controller graph) or route to pawn + controller owners only.
2. **Hold timing granularity** is frame-quantised and uses the raw frame `dt`, clamped to 0.25 s; a 10 fps frame can overshoot a 0.05 s threshold. UE behaves the same.
3. **No lazy update** means a `state` query before the gameplay tick in the same frame (for example `vehicles_.prePhysics` in `SandboxApp.cpp:2103`, which runs before `tickGameplayGroups`) sees the previous frame's result. Acceptable; documented. Moving the update call earlier (right after `publishInput`) would fix it but touches both hosts' frame code.
4. **Radial dead zone on a stick needs the action-level modifier** because bindings are one scalar each. Documented in the grammar section and the editor tooltip; a "Gamepad Stick" template binding helper is out of scope.
5. **Not in v1** (UE has them): trigger blockers, per-trigger "consume input", `FOVScaling`/`ToWorldSpace` modifiers, a multi-pad/player index, per-device sensitivity profiles, runtime rebinding of triggers. All additive later because the spec grammar is open-ended.
6. **Strict format**: a `.ocinput` using the new keywords is rejected by an older engine ("unknown BIND attribute"). Intentional (matches the format's strictness contract), but note it in the changelog.
7. **Action names with `:` `,` or spaces** cannot be chord/combo targets. Validate in the editor's rename (warn) rather than restrict the format.
8. **Doc drift found while reading:** `docs/ABI.md` still says framework minor 8 / 100 exports (header is 10); the implementing agent only corrects the sections it edits and notes the rest.
