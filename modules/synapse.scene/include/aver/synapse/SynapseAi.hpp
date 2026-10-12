#pragma once
// SynapseAi: the crowd, hearing and tactics systems under one roof, plus the behaviour-tree
// actions and conditions that drive them.
//
// This class is NOT a process-global. The singleton lives in Aver.Synapse.Abi (one copy, behind
// the C ABI), so a composition root that links the static libraries too never ends up with a
// second, silent instance. Tests and tools construct their own.
//
// Frame order: AgentSystem::tick, then SynapseAi::tick, then BtSystem::tick.
#include "aver/synapse/Bt.hpp"
#include "aver/synapse/SynapseCrowd.hpp"
#include "aver/synapse/SynapseEqs.hpp"          // AI: wave 3 #21
#include "aver/synapse/SynapseHearing.hpp"
#include "aver/synapse/SynapseManaged.hpp"      // AI: wave 3 #21
#include "aver/synapse/SynapsePerception.hpp"   // AI: wave 3 #21 (NotifyFn)
#include "aver/synapse/SynapseSmartObject.hpp"  // AI: wave 3 #21
#include "aver/synapse/SynapseTactics.hpp"

namespace aver::synapse {

class SynapseAi {
public:
    SynapseAi();

    // Registers every component of the three systems. Idempotent.
    void registerComponents(scene::World& world);

    // Hearing, then tactics (cover, squads), then crowd, then smart objects and environment queries
    // (so a result is ready for the behaviour tree or state tree that ticks right after).
    void tick(scene::World& world, const fmt::OcNavData* nav, f32 dt);

    // Installs the behaviour-tree vocabulary below into `reg`, bound to this object:
    //   Conditions
    //     HeardNoise            hearing memory confidence > params[0]
    //     HeardNoiseTag         a memory with tag params[0]
    //     InCover               holds a reserved point and stands on it (params[0] = reach, cm)
    //     SquadRoleIs           CSynapseSquad.role == params[0] (SquadRole)
    //   Actions
    //     InvestigateNoise      path to the strongest remembered noise; Running until there
    //     TakeCover             params: [0] threat 0 = seen target, 1 = heard noise; [1] max seek cm;
    //                           [2] min threat distance cm; [3] 1 = full-height only.
    //                           Claims a point, paths to it, Running until there
    //     LeaveCover            releases the reservation
    //     SquadFlank            path to this member's squad slot; Running until there
    //     SquadKeepSpacing      steps out of squad-mates' spacing circles; Success when clear
    //     SquadSetTarget        params[0..2] = target; applies to this member's squad
    //     CrowdSetMode          params[0] = CrowdMode, [1..3] = steer target
    //   Environment queries and smart objects (wave 3):
    //     RunEqsQuery           stringParam = .oceqs path; params[0] timeout s (0 none), [1] priority;
    //                           writes the query's OUTPUT keys on success
    //     EqsHasResult          (Condition) the last run of stringParam found >= 1 item, newer than params[0] s
    //     ClaimSmartObject      stringParam slot tags; params[0] radius, [1] claim TTL (<= 0 slot default), [2] match all
    //     MoveToSmartObject     params[0] arrive radius; paths to the held claim's entry point
    //     UseSmartObject        starts using the held claim; Running until the slot is released
    //     ReleaseSmartObject    releases every claim the subject holds
    //     HasSmartObjectClaim   (Condition) params[0] = 1: the claim must be in use
    // `reg` is remembered: the board resolver, the managed leaves and the smart-object behaviours use it.
    void registerBehaviors(BtRegistry& reg);

    void reset();

    CrowdSystem& crowd() { return crowd_; }
    HearingSystem& hearing() { return hearing_; }
    TacticsSystem& tactics() { return tactics_; }
    EqsSystem& eqs() { return eqs_; }                          // AI: wave 3 #21
    SmartObjectSystem& smartObjects() { return smart_; }       // AI: wave 3 #21
    SynapseManaged& managed() { return managed_; }             // AI: wave 3 #21

    // ---- events (AI: wave 3 #21) ----
    // Named events fire on an entity's graph through the host's notify sink (ScriptHost::graphFire). They
    // are queued while an API call or a tick runs and delivered when the outermost call ends, so a graph
    // handler that calls back never sees half-applied state. Depth 8, then dropped with one warning.
    void setNotify(NotifyFn fn, void* user) { notify_ = fn; notifyUser_ = user; }
    void raise(scene::Entity e, const char* name);
    struct CallScope {
        explicit CallScope(SynapseAi& a) : ai(a) { ++ai.depth_; }
        ~CallScope() { if (--ai.depth_ == 0) ai.flushEvents(); }
        CallScope(const CallScope&) = delete;
        CallScope& operator=(const CallScope&) = delete;
        SynapseAi& ai;
    };
    // The registry registerBehaviors received (the host's BtRegistry), or null.
    BtRegistry* behaviorRegistry() const { return reg_; }
    Blackboard* boardOf(scene::Entity e) const { return reg_ ? reg_->boardFor(static_cast<i32>(e)) : nullptr; }

    // Cached component type lookups for the behaviours (0 until registered).
    u32 agentType(scene::World& w);
    u32 perceptionType(scene::World& w);

private:
    void flushEvents();

    CrowdSystem crowd_;
    HearingSystem hearing_;
    TacticsSystem tactics_;
    EqsSystem eqs_;
    SmartObjectSystem smart_;
    SynapseManaged managed_;
    BtRegistry* reg_ = nullptr;
    NotifyFn notify_ = nullptr;
    void* notifyUser_ = nullptr;
    int depth_ = 0, flushDepth_ = 0;
    struct QueuedEvent { scene::Entity e; std::string name; };
    std::vector<QueuedEvent> queued_;
    u32 agentType_ = 0;
    u32 perceptionType_ = 0;
};

} // namespace aver::synapse
