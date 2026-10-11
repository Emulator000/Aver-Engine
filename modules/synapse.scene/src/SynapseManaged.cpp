#include "aver/synapse/SynapseManaged.hpp"

#include "aver/core/Log.hpp"

#include <algorithm>

namespace aver::synapse {
namespace {
// Stored in the leaf's scratch float so a first tick (0) is told apart from a later one, and subtracted
// again before the managed side sees the elapsed seconds.
constexpr f32 kElapsedBias = 1e-6f;

void fillCommon(ManagedCall& c, i32 kind, i32 id, i32 subject, const fmt::OcBtNode& node) {
    c = ManagedCall{};
    c.structBytes = static_cast<i32>(sizeof(ManagedCall));
    c.kind = kind;
    c.nameId = id;
    c.subject = subject;
    for (int i = 0; i < 4; ++i) c.p[i] = node.params[i];
    c.text = node.stringParam.c_str();
}
} // namespace

i32 SynapseManaged::registerName(i32 kind, const std::string& name) {
    if (name.empty() || (kind != kManagedCondition && kind != kManagedAction && kind != kManagedEqsTest)) return 0;
    const bool leaf = kind != kManagedEqsTest;
    for (Entry& e : entries_) {
        if (e.name != name || (e.kind == kManagedEqsTest) == leaf) continue;
        if (e.active || e.kind != kind) return 0;
        e.active = true;   // reactivated in place after a clear()
        return e.id;
    }
    if (leaf && reg_ && (reg_->findCondition(name) || reg_->findAction(name))) return 0;   // a native leaf owns it
    Entry e;
    e.owner = this;
    e.kind = kind;
    e.id = static_cast<i32>(entries_.size()) + 1;
    e.name = name;
    e.active = true;
    entries_.push_back(std::move(e));
    if (leaf && reg_) install(entries_.back());
    return entries_.back().id;
}

void SynapseManaged::install(Entry& e) {
    if (e.kind == kManagedCondition) reg_->registerCondition(e.name, &conditionTramp, &e);
    else reg_->registerAction(e.name, &actionTramp, &e, &abortTramp);
    e.inRegistry = true;
}

void SynapseManaged::clear() {
    for (Entry& e : entries_) e.active = false;
}

void SynapseManaged::attachRegistry(BtRegistry* reg) {
    reg_ = reg;
    if (!reg_) return;
    for (Entry& e : entries_) {
        if (e.kind == kManagedEqsTest || e.inRegistry) continue;
        if (reg_->findCondition(e.name) || reg_->findAction(e.name)) {
            AVER_WARN("[Synapse] managed leaf '{}' clashes with a native leaf and was dropped", e.name);
            e.active = false;
            continue;
        }
        install(e);
    }
}

bool SynapseManaged::conditionTramp(i32 subject, const fmt::OcBtNode& node, void* user) {
    Entry* e = static_cast<Entry*>(user);
    SynapseManaged& m = *e->owner;
    if (!e->active || !m.fn_) return false;
    ManagedCall c;
    fillCommon(c, kManagedCondition, e->id, subject, node);
    if (!m.fn_(&c, m.user_)) return false;
    return c.status != 0;
}

BtStatus SynapseManaged::actionTramp(i32 subject, const fmt::OcBtNode& node, f32 dt, f32& elapsed, void* user) {
    Entry* e = static_cast<Entry*>(user);
    SynapseManaged& m = *e->owner;
    if (!e->active || !m.fn_) return BtStatus::Failure;
    ManagedCall c;
    fillCommon(c, kManagedAction, e->id, subject, node);
    const bool first = elapsed <= 0.0f;
    c.first = first ? 1 : 0;
    c.dt = dt;
    c.elapsed = first ? 0.0f : elapsed - kElapsedBias;
    elapsed = std::max(elapsed, kElapsedBias) + dt;
    if (!m.fn_(&c, m.user_)) return BtStatus::Failure;
    return c.status == 0 ? BtStatus::Running : c.status == 1 ? BtStatus::Success : BtStatus::Failure;
}

void SynapseManaged::abortTramp(i32 subject, const fmt::OcBtNode& node, void* user) {
    Entry* e = static_cast<Entry*>(user);
    SynapseManaged& m = *e->owner;
    if (!e->active || !m.fn_) return;
    ManagedCall c;
    fillCommon(c, kManagedAbort, e->id, subject, node);
    m.fn_(&c, m.user_);
}

bool SynapseManaged::callEqsTest(const EqsTest& t, const EqsRunParams& p, const EqsItem& item, const EqsContext& ctx, f32& value) {
    if (!fn_) return false;
    const Entry* found = nullptr;
    for (const Entry& e : entries_)
        if (e.kind == kManagedEqsTest && e.active && e.name == t.custom) { found = &e; break; }
    if (!found) return false;
    ManagedCall c;
    c.structBytes = static_cast<i32>(sizeof(ManagedCall));
    c.kind = kManagedEqsTest;
    c.nameId = found->id;
    c.subject = static_cast<i32>(p.querier);
    for (int i = 0; i < 4; ++i) c.p[i] = t.p[i];
    c.text = t.text.c_str();
    c.pos[0] = item.pos.x; c.pos[1] = item.pos.y; c.pos[2] = item.pos.z;
    c.itemEntity = static_cast<i32>(item.entity);
    c.ctx[0] = ctx.pos.x; c.ctx[1] = ctx.pos.y; c.ctx[2] = ctx.pos.z;
    c.ctxEntity = static_cast<i32>(ctx.entity);
    if (!fn_(&c, user_)) return false;
    value = c.value;
    return true;
}

} // namespace aver::synapse
