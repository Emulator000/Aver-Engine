#pragma once
// StateTree evaluation: one pure function (tickStateTree) over an asset, a leaf registry, a subject, a
// blackboard and a per-subject StateTreeInstance. The same function runs host-side (BtSystem) and
// Synapse-DLL-side (smart-object behaviours, RunStateTree). Semantics: docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md.
#include "aver/synapse/StateTreeAsset.hpp"

#include <string>
#include <vector>

namespace aver::synapse {

enum class StStatus : u8 { Stopped, Running, Succeeded, Failed };

struct StateTreeInstance {
    StStatus status = StStatus::Stopped;
    std::vector<i32> active;                         // root-most first
    struct Task {
        f32 elapsed = 0;                             // the leaf's persistent scratch float
        u8 phase = 0;                                // 0 idle, 1 running, 2 done, 3 failed
    };
    std::vector<std::vector<Task>> tasks;            // per state index
    struct Event {
        std::string name;
        i64 i = 0;
        f32 f = 0;
    };
    std::vector<Event> events;                       // visible to the NEXT tick only
    u64 tickCount = 0;
    i32 lastFrom = -1, lastTo = -1;                  // leaf states of the last applied transition
    u64 lastTransitionTick = 0;
    bool trace = false;                              // editor: record the last status of each task
    std::vector<std::vector<u8>> traceTasks;         // per state, per task: 0 running, 1 success, 2 failure, 255 not run
    std::vector<f32> evalElapsed;
    bool stopped = false, pendingRestart = false, pendingStop = false;
    // A bubbling climb that hit the 8-steps-per-update cap resumes here next update (a chain is up to 16 deep).
    i32 resumeLevel = -1;
    bool resumeFailure = false;

    // At most 16 queued; extras are dropped with one warning.
    void sendEvent(std::string name, i64 i = 0, f32 f = 0);
    // Exit every state (aborting running tasks) and begin again at the next tick.
    void restart() { pendingRestart = true; pendingStop = false; }
    // Exit every state and stay Stopped until restart().
    void stop() { pendingStop = true; pendingRestart = false; }
};

struct StateTreeRuntime {
    StateTreeAsset asset;
    std::string path;
};

// Advances the tree by one update and returns its status. `board` may be null (conditions on it read false).
StStatus tickStateTree(const StateTreeAsset& asset, const BtRegistry& registry, i32 subject, f32 dt,
                       StateTreeInstance& inst, Blackboard* board);

// Exits every active state now (running tasks get their onAbort) and leaves the instance Stopped but
// restartable. For reloads and for a subject that goes away mid-run.
void abortStateTree(const StateTreeAsset& asset, const BtRegistry& registry, i32 subject, StateTreeInstance& inst);

} // namespace aver::synapse
