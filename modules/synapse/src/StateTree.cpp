#include "aver/synapse/StateTree.hpp"

#include "aver/core/Log.hpp"

#include <algorithm>
#include <unordered_set>

namespace aver::synapse {
namespace {

constexpr int kMaxBubbleSteps = 8;
constexpr usize kMaxQueuedEvents = 16;

void warnOnce(const std::string& key, const std::string& msg) {
    static std::unordered_set<std::string> seen;
    if (seen.insert(key).second) AVER_WARN("[Synapse] {}", msg);
}

enum class Res : u8 { None, Applied, Finished };

struct Runner {
    const StateTreeAsset& a;
    const BtRegistry& reg;
    i32 subject;
    f32 dt;
    StateTreeInstance& inst;
    Blackboard* board;
    int steps = 0;
    std::vector<char> vc, vf;                        // per chain level: complete / failed, as seen by the scan
    std::vector<StateTreeInstance::Event> events;    // visible this update

    const StState& st(i32 i) const { return a.states[static_cast<usize>(i)]; }

    bool condPass(const StCond& c) {
        bool r = false;
        if (c.isBlackboard) {
            r = evaluateDecorator(c.bb, board);
        } else if (const BtRegistry::ConditionEntry* e = reg.findCondition(c.leaf.name)) {
            r = e->fn(subject, c.leaf, e->user);
        } else {
            warnOnce("stc:" + c.leaf.name, "StateTree condition '" + c.leaf.name + "' is not registered -- treated as false");
        }
        return c.negate ? !r : r;
    }

    bool entersOk(i32 s) {
        if (st(s).disabled) return false;
        for (const StCond& c : st(s).enter)
            if (!condPass(c)) return false;
        return true;
    }

    // Appends `s` and the first selectable descent below it. Leaves `chain` unchanged on failure.
    bool selectFrom(i32 s, std::vector<i32>& chain) {
        if (!entersOk(s)) return false;
        chain.push_back(s);
        const StState& x = st(s);
        if (x.children.empty()) {
            if (!x.group) return true;
            chain.pop_back();
            return false;
        }
        for (const i32 c : x.children) {
            const usize mark = chain.size();
            if (selectFrom(c, chain)) return true;
            chain.resize(mark);
        }
        chain.pop_back();
        return false;
    }

    void abortTask(i32 s, usize j) {
        const fmt::OcBtNode& n = st(s).tasks[j];
        if (const BtRegistry::ActionEntry* e = reg.findAction(n.name))
            if (e->onAbort) e->onAbort(subject, n, e->user);
    }

    // Exits chain[from..] leaf first and truncates the chain there.
    void exitFrom(usize from) {
        for (usize k = inst.active.size(); k > from; --k) {
            const i32 s = inst.active[k - 1];
            auto& ts = inst.tasks[static_cast<usize>(s)];
            for (usize j = 0; j < ts.size(); ++j)
                if (ts[j].phase == 1) abortTask(s, j);
            ts.clear();
        }
        inst.active.resize(std::min(from, inst.active.size()));
    }

    void enter(i32 s) {
        const usize n = st(s).tasks.size();
        inst.tasks[static_cast<usize>(s)].assign(n, StateTreeInstance::Task{});
        if (inst.trace) {
            if (inst.traceTasks.size() < a.states.size()) inst.traceTasks.resize(a.states.size());
            inst.traceTasks[static_cast<usize>(s)].assign(n, 255);
        }
    }

    // Replaces chain[from..] with `next[from..]`.
    void replaceChain(const std::vector<i32>& next, usize from) {
        inst.resumeLevel = -1;
        const i32 oldLeaf = inst.active.empty() ? -1 : inst.active.back();
        exitFrom(from);
        for (usize k = from; k < next.size(); ++k) {
            inst.active.push_back(next[k]);
            enter(next[k]);
        }
        inst.lastFrom = oldLeaf;
        inst.lastTo = inst.active.empty() ? -1 : inst.active.back();
        inst.lastTransitionTick = inst.tickCount;
    }

    Res finishTree(StStatus s) {
        inst.resumeLevel = -1;
        const i32 oldLeaf = inst.active.empty() ? -1 : inst.active.back();
        exitFrom(0);
        inst.status = s;
        inst.lastFrom = oldLeaf;
        inst.lastTo = -1;
        inst.lastTransitionTick = inst.tickCount;
        return Res::Finished;
    }

    Res applyGoto(i32 target) {
        std::vector<i32> path;
        for (i32 p = target; p >= 0; p = st(p).parent) path.push_back(p);
        std::reverse(path.begin(), path.end());
        const usize m = path.size() - 1;
        usize c = 0;
        while (c < m && c < inst.active.size() && inst.active[c] == path[c]) ++c;
        std::vector<i32> next(inst.active.begin(), inst.active.begin() + static_cast<isize>(c));
        for (usize i = c; i < m; ++i) {
            if (!entersOk(path[i])) return Res::None;
            next.push_back(path[i]);
        }
        if (!selectFrom(target, next)) return Res::None;
        replaceChain(next, c);
        return Res::Applied;
    }

    // The next selectable sibling of the state at chain level k, or None.
    Res applyNext(usize k) {
        const i32 s = inst.active[k];
        const std::vector<i32>& sibs = st(s).parent < 0 ? a.roots : st(st(s).parent).children;
        usize at = 0;
        while (at < sibs.size() && sibs[at] != s) ++at;
        for (usize i = at + 1; i < sibs.size(); ++i) {
            std::vector<i32> next(inst.active.begin(), inst.active.begin() + static_cast<isize>(k));
            if (selectFrom(sibs[i], next)) {
                replaceChain(next, k);
                return Res::Applied;
            }
        }
        return Res::None;
    }

    Res applyTransition(const StTransition& t, usize k) {
        switch (t.target) {
            case StTargetKind::Succeeded: return finishTree(StStatus::Succeeded);
            case StTargetKind::Failed: return finishTree(StStatus::Failed);
            case StTargetKind::Goto: return t.targetState >= 0 ? applyGoto(t.targetState) : Res::None;
            case StTargetKind::Next: return escalate(k, false);
        }
        return Res::None;
    }

    void putPayload(const StateTreeInstance::Event& e) {
        if (!board) return;
        if (!board->has("EventPayload")) {
            BbKeyDef d;
            d.name = "EventPayload";
            d.type = BbType::Int;
            board->defineKey(d);
        }
        if (!board->has("EventPayloadF")) {
            BbKeyDef d;
            d.name = "EventPayloadF";
            d.type = BbType::Float;
            board->defineKey(d);
        }
        board->setInt("EventPayload", e.i);
        board->setFloat("EventPayloadF", e.f);
    }

    // Scans levels fromLevel..0 (leaf first). `tickAndEvents` false limits it to completed / failed triggers.
    Res scan(usize fromLevel, bool tickAndEvents) {
        for (usize kk = fromLevel + 1; kk > 0; --kk) {
            const usize k = kk - 1;
            if (k >= inst.active.size()) continue;
            const i32 s = inst.active[k];
            // Copy: applying a transition changes the chain but not the asset, so a reference is safe.
            const StState& state = st(s);
            for (const StTransition& t : state.transitions) {
                const StateTreeInstance::Event* ev = nullptr;
                bool fires = false;
                switch (t.trigger) {
                    case StTrigger::Tick: fires = tickAndEvents; break;
                    case StTrigger::Completed: fires = vc[k] != 0; break;
                    case StTrigger::Failed: fires = vf[k] != 0; break;
                    case StTrigger::Event:
                        if (tickAndEvents)
                            for (const auto& e : events)
                                if (e.name == t.event) { ev = &e; fires = true; break; }
                        break;
                }
                if (!fires) continue;
                if (ev) putPayload(*ev);
                bool pass = true;
                for (const StCond& c : t.conds) {
                    if (!condPass(c)) { pass = false; break; }
                }
                if (!pass) continue;
                const Res r = applyTransition(t, k);
                if (r != Res::None) return r;
            }
        }
        return Res::None;
    }

    // The state at chain level k completed (or failed) with no explicit handler: take the default and
    // climb. Completion steps to the next sibling; each climb re-scans the parent's completed / failed
    // triggers, and a root with nowhere left to go finishes the tree.
    Res escalate(usize k, bool failure) {
        while (steps++ < kMaxBubbleSteps) {
            if (!failure) {
                const Res r = applyNext(k);
                if (r != Res::None) return r;
            }
            if (k == 0) return finishTree(failure ? StStatus::Failed : StStatus::Succeeded);
            --k;
            (failure ? vf : vc)[k] = 1;
            const Res r = scan(k, false);
            if (r != Res::None) return r;
        }
        // Out of steps this update: carry on from the highest level reached (nearest the root) next update.
        if (inst.resumeLevel < 0 || static_cast<i32>(k) < inst.resumeLevel) {
            inst.resumeLevel = static_cast<i32>(k);
            inst.resumeFailure = failure;
        }
        return Res::None;
    }

    void exitAll() {
        if (!inst.tasks.empty()) exitFrom(0);
        inst.active.clear();
    }

    bool start() {
        inst.tasks.resize(a.states.size());
        inst.evalElapsed.assign(a.evaluators.size(), 0.0f);
        std::vector<i32> chain;
        for (const i32 r : a.roots)
            if (selectFrom(r, chain)) break;
        if (chain.empty()) {
            warnOnce("stnone:" + a.name, "StateTree '" + a.name + "' has no selectable state");
            inst.status = StStatus::Failed;
            return false;
        }
        inst.active.clear();
        for (const i32 s : chain) {
            inst.active.push_back(s);
            enter(s);
        }
        inst.status = StStatus::Running;
        inst.lastFrom = -1;
        inst.lastTo = chain.back();
        inst.lastTransitionTick = inst.tickCount;
        return true;
    }
};

} // namespace

void StateTreeInstance::sendEvent(std::string name, i64 i, f32 f) {
    if (events.size() >= kMaxQueuedEvents) {
        warnOnce("stev", "StateTree event queue is full (16): extra events are dropped");
        return;
    }
    events.push_back(Event{std::move(name), i, f});
}

StStatus tickStateTree(const StateTreeAsset& asset, const BtRegistry& registry, i32 subject, f32 dt,
                       StateTreeInstance& inst, Blackboard* board) {
    Runner r{asset, registry, subject, dt, inst, board};

    if (inst.status == StStatus::Running && inst.tasks.size() != asset.states.size()) {
        inst.active.clear();   // the asset changed shape under a live instance: start over
        inst.tasks.clear();
        inst.status = StStatus::Stopped;
    }
    if (inst.pendingStop) {
        r.exitAll();
        inst.pendingStop = false;
        inst.pendingRestart = false;
        inst.events.clear();
        inst.status = StStatus::Stopped;
        inst.stopped = true;
        return inst.status;
    }
    if (inst.pendingRestart) {
        r.exitAll();
        inst.pendingRestart = false;
        inst.status = StStatus::Stopped;
        inst.stopped = false;
    }
    if (inst.status == StStatus::Succeeded || inst.status == StStatus::Failed) {
        if (!asset.restartOnFinish) return inst.status;
        inst.status = StStatus::Stopped;
    }
    if (inst.status == StStatus::Stopped) {
        if (inst.stopped) return inst.status;
        if (!r.start()) return inst.status;
    }
    if (inst.status != StStatus::Running) return inst.status;
    ++inst.tickCount;

    r.events.swap(inst.events);   // events sent from now on belong to the next update
    const i32 resume = inst.resumeLevel;
    const bool resumeFailure = inst.resumeFailure;
    inst.resumeLevel = -1;

    // Evaluators.
    if (inst.evalElapsed.size() != asset.evaluators.size()) inst.evalElapsed.assign(asset.evaluators.size(), 0.0f);
    for (usize i = 0; i < asset.evaluators.size(); ++i) {
        const fmt::OcBtNode& n = asset.evaluators[i];
        const BtRegistry::ActionEntry* e = registry.findAction(n.name);
        if (!e) {
            warnOnce("stev:" + n.name, "StateTree evaluator '" + n.name + "' is not registered");
            continue;
        }
        const BtStatus s = e->fn(subject, n, dt, inst.evalElapsed[i], e->user);
        if (s == BtStatus::Failure) warnOnce("stevf:" + asset.name + n.name, "StateTree evaluator '" + n.name + "' failed");
        if (s != BtStatus::Running) inst.evalElapsed[i] = 0.0f;
    }

    // Tasks, root-most state first.
    for (usize k = 0; k < inst.active.size(); ++k) {
        const i32 s = inst.active[k];
        const StState& state = r.st(s);
        auto& ts = inst.tasks[static_cast<usize>(s)];
        for (usize j = 0; j < state.tasks.size() && j < ts.size(); ++j) {
            StateTreeInstance::Task& t = ts[j];
            if (t.phase >= 2) continue;
            const fmt::OcBtNode& n = state.tasks[j];
            BtStatus res = BtStatus::Failure;
            if (const BtRegistry::ActionEntry* e = registry.findAction(n.name)) res = e->fn(subject, n, dt, t.elapsed, e->user);
            else warnOnce("stt:" + n.name, "StateTree task '" + n.name + "' is not registered -- treated as Failure");
            t.phase = res == BtStatus::Running ? 1 : res == BtStatus::Success ? 2 : 3;
            if (res != BtStatus::Running) t.elapsed = 0.0f;
            if (inst.trace) {
                if (inst.traceTasks.size() < asset.states.size()) inst.traceTasks.resize(asset.states.size());
                auto& tt = inst.traceTasks[static_cast<usize>(s)];
                if (tt.size() < state.tasks.size()) tt.resize(state.tasks.size(), 255);
                tt[j] = static_cast<u8>(res);
            }
        }
    }

    // Flags per chain level.
    const usize n = inst.active.size();
    if (n == 0) return inst.status;
    r.vc.assign(n, 0);
    r.vf.assign(n, 0);
    for (usize k = 0; k < n; ++k) {
        const i32 s = inst.active[k];
        const auto& ts = inst.tasks[static_cast<usize>(s)];
        bool anyDone = false, allDone = !ts.empty(), anyFailed = false;
        for (const auto& t : ts) {
            anyDone = anyDone || t.phase == 2;
            allDone = allDone && t.phase == 2;
            anyFailed = anyFailed || t.phase == 3;
        }
        r.vf[k] = anyFailed ? 1 : 0;
        switch (stEffectiveComplete(r.st(s))) {
            case StComplete::All: r.vc[k] = allDone ? 1 : 0; break;
            case StComplete::Any: r.vc[k] = anyDone ? 1 : 0; break;
            case StComplete::None: break;
        }
    }

    // Transitions, leaf first.
    Res res = r.scan(n - 1, true);
    if (res == Res::None) {
        // Defaults for the leaf: a failed leaf fails its parent, a complete leaf steps to the next sibling.
        if (resume >= 0 && static_cast<usize>(resume) < n) {
            (resumeFailure ? r.vf : r.vc)[static_cast<usize>(resume)] = 1;
            res = r.escalate(static_cast<usize>(resume), resumeFailure);
        } else if (r.vf[n - 1]) {
            res = r.escalate(n - 1, true);
        } else if (r.vc[n - 1]) {
            res = r.escalate(n - 1, false);
        }
    }
    return inst.status;
}

void abortStateTree(const StateTreeAsset& asset, const BtRegistry& registry, i32 subject, StateTreeInstance& inst) {
    Runner r{asset, registry, subject, 0.0f, inst, nullptr};
    if (inst.tasks.size() == asset.states.size()) r.exitAll();
    else inst.active.clear();
    inst.tasks.clear();
    inst.events.clear();
    inst.status = StStatus::Stopped;
    inst.stopped = false;
    inst.pendingRestart = inst.pendingStop = false;
}

} // namespace aver::synapse
