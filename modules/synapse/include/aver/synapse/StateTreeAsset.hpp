#pragma once
// The StateTree asset (.ocst): a forest of nested states with tasks, enter conditions and transitions,
// whose parameters are a blackboard schema. Pure; exact text reader/writer; structural edits for the
// editor. Evaluation lives in StateTree.hpp. Format: FORMAT_SPECS.md 11g.
#include "aver/formats/OcBt.hpp"
#include "aver/synapse/Bt.hpp"
#include "aver/synapse/Blackboard.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace aver::synapse {

enum class StTrigger : u8 { Tick, Completed, Failed, Event };
enum class StTargetKind : u8 { Goto, Next, Succeeded, Failed };
enum class StComplete : u8 { All, Any, None };

inline constexpr u32 kStMaxStates = 256;
inline constexpr u32 kStMaxDepth = 16;
inline constexpr u32 kStMaxTasks = 16;
inline constexpr u32 kStMaxTransitions = 16;

// One condition: a blackboard test (`bb`) or a registered Condition leaf (`leaf`, kind Condition).
struct StCond {
    bool isBlackboard = true;
    bool negate = false;
    BtDecorator bb;
    fmt::OcBtNode leaf;
};

struct StTransition {
    StTrigger trigger = StTrigger::Tick;
    std::string event;
    std::vector<StCond> conds;
    StTargetKind target = StTargetKind::Next;
    std::string targetPath;
    i32 targetState = -1;   // resolved index for Goto, -1 otherwise (or when unresolved)
};

struct StState {
    std::string name, comment;
    i32 parent = -1;
    std::vector<i32> children;
    bool group = false, disabled = false;
    bool completeExplicit = false;
    StComplete complete = StComplete::None;   // meaningful when completeExplicit
    std::vector<StCond> enter;
    std::vector<fmt::OcBtNode> tasks;
    std::vector<StTransition> transitions;
};

struct StateTreeAsset {
    std::string name, desc, path;
    BbSchema schema;
    std::vector<fmt::OcBtNode> evaluators;
    std::vector<StState> states;   // pre-order, parents first
    std::vector<i32> roots;
    bool restartOnFinish = true;

    std::string statePath(i32 index) const;       // "Combat.Chase"; "" for an invalid index
    i32 findPath(std::string_view dotted) const;    // -1 when no state has that dotted path
};

// The completion mode in effect: the explicit one, else `all` for a state with tasks and `none` without.
StComplete stEffectiveComplete(const StState& s);

// "<name>:<line>: <message>" on the first problem; `out` is empty after a failure. Resolves targetState.
bool parseStateTree(std::string_view text, const std::string& name, StateTreeAsset& out, std::string* why);
std::string writeStateTree(const StateTreeAsset& a);   // exact: a second write is byte-identical
bool loadStateTree(const std::string& path, StateTreeAsset& out, std::string* why);
bool saveStateTree(const std::string& path, const StateTreeAsset& in, std::string* why);

// Warnings that do not stop a tree running: unknown leaves (when a registry is given), unreachable
// states, disabled transition targets, blackboard keys used but not declared.
std::vector<std::string> validateStateTree(const StateTreeAsset& a, const BtRegistry* registry = nullptr);

// ---- structural edits for the editor ---------------------------------------------------------------
// Each returns the new index of what it acted on (or false/-1 on refusal) and keeps every transition
// target resolved. Indices move across a call.

// Appends a child of `parent` (-1 = a new root). An empty name picks "State", "State2", ...
i32 stAddState(StateTreeAsset& a, i32 parent, std::string name);
// Removes the state and its subtree; transitions pointing into it become NEXT. `retargeted` receives how many.
bool stDeleteState(StateTreeAsset& a, i32 index, i32* retargeted = nullptr);
// Renames and rewrites every Goto path. False for a bad or duplicate sibling name.
bool stRenameState(StateTreeAsset& a, i32 index, std::string name);
// Moves among siblings by `delta` places (clamped); -1 when it cannot move.
i32 stMoveState(StateTreeAsset& a, i32 index, i32 delta);
// Makes the state the last child of `newParent` (-1 = a root). -1 for a cycle, a name clash or too much depth.
i32 stReparent(StateTreeAsset& a, i32 index, i32 newParent);
// What "New State Tree" writes: Idle -> Active, each on a Wait.
StateTreeAsset stStarter();

bool stStateNameValid(std::string_view name);   // [A-Za-z_][A-Za-z0-9_]*, not NEXT / SUCCEEDED / FAILED

} // namespace aver::synapse
