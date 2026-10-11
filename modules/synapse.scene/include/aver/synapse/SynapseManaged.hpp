#pragma once
// The managed seam: behaviour-tree / StateTree leaves and EQS tests implemented in C#. Native keeps a
// list of names; every call goes through ONE dispatch function the managed side installs (the timers
// precedent). A registered leaf name becomes a normal BtRegistry entry whose trampoline calls out; an
// EQS test is looked up by name when a `custom` test runs. Never throws, never crashes: a missing
// dispatch or an inactive name reads as Failure / false / "not handled".
#include "aver/core/Math.hpp"
#include "aver/core/Types.hpp"
#include "aver/synapse/Bt.hpp"
#include "aver/synapse/Eqs.hpp"

#include <deque>
#include <string>

namespace aver::synapse {

enum ManagedKind : i32 { kManagedCondition = 0, kManagedAction = 1, kManagedAbort = 2, kManagedEqsTest = 3 };

// Layout mirror of AverSynManagedCall (synapse_query_abi.h); SynapseQueryAbi.cpp static_asserts the match.
struct ManagedCall {
    i32 structBytes = 0;
    i32 kind = 0;
    i32 nameId = 0;
    i32 subject = 0;
    f32 dt = 0;
    f32 elapsed = 0;
    i32 first = 0;
    i32 status = 0;
    f32 p[4] = {};
    const char* text = "";
    f32 pos[3] = {};
    i32 itemEntity = 0;
    f32 ctx[3] = {};
    i32 ctxEntity = 0;
    f32 value = 0;
};
using ManagedDispatchFn = i32 (*)(ManagedCall* call, void* user);

class SynapseManaged {
public:
    SynapseManaged() = default;
    SynapseManaged(const SynapseManaged&) = delete;
    SynapseManaged& operator=(const SynapseManaged&) = delete;

    void setDispatch(ManagedDispatchFn fn, void* user) { fn_ = fn; user_ = user; }
    bool installed() const { return fn_ != nullptr; }

    // nameId > 0, or 0 when the name is taken, empty, or clashes with a native leaf. Kinds 0 and 1 also
    // register the leaf into the BtRegistry (queued until attachRegistry when none is known yet).
    i32 registerName(i32 kind, const std::string& name);
    // Script unload: every name goes inactive. Leaves stay in the registry and answer Failure / false
    // until the name is registered again (reactivated in place).
    void clear();
    // Called by SynapseAi::registerBehaviors with the host registry.
    void attachRegistry(BtRegistry* reg);

    // EQS custom tests. False when no dispatch is installed, the name is unknown, or the call was not handled.
    bool callEqsTest(const EqsTest& t, const EqsRunParams& p, const EqsItem& item, const EqsContext& ctx, f32& value);

    usize nameCount() const { return entries_.size(); }

private:
    struct Entry {
        SynapseManaged* owner = nullptr;
        i32 kind = 0;
        i32 id = 0;
        std::string name;
        bool active = false;
        bool inRegistry = false;
    };
    static bool conditionTramp(i32 subject, const fmt::OcBtNode& node, void* user);
    static BtStatus actionTramp(i32 subject, const fmt::OcBtNode& node, f32 dt, f32& elapsed, void* user);
    static void abortTramp(i32 subject, const fmt::OcBtNode& node, void* user);
    void install(Entry& e);

    ManagedDispatchFn fn_ = nullptr;
    void* user_ = nullptr;
    BtRegistry* reg_ = nullptr;
    std::deque<Entry> entries_;   // stable addresses: trampolines hold them
};

} // namespace aver::synapse
