#include "aver/synapse/synapse_query_abi.h"

#include "aver/core/ErrorCodes.hpp"
#include "aver/scene/World.hpp"
#include "aver/synapse/AiPaths.hpp"
#include "aver/synapse/SynapseAi.hpp"

#include <cmath>
#include <cstddef>
#include <string>

using namespace aver;
using namespace aver::synapse;

// The managed seam passes AverSynManagedCall straight through ManagedCall: the two must agree byte for byte.
static_assert(sizeof(AverSynManagedCall) == sizeof(ManagedCall), "AverSynManagedCall layout drift");
static_assert(offsetof(AverSynManagedCall, structBytes) == offsetof(ManagedCall, structBytes), "layout drift");
static_assert(offsetof(AverSynManagedCall, kind) == offsetof(ManagedCall, kind), "layout drift");
static_assert(offsetof(AverSynManagedCall, nameId) == offsetof(ManagedCall, nameId), "layout drift");
static_assert(offsetof(AverSynManagedCall, subject) == offsetof(ManagedCall, subject), "layout drift");
static_assert(offsetof(AverSynManagedCall, dt) == offsetof(ManagedCall, dt), "layout drift");
static_assert(offsetof(AverSynManagedCall, elapsed) == offsetof(ManagedCall, elapsed), "layout drift");
static_assert(offsetof(AverSynManagedCall, first) == offsetof(ManagedCall, first), "layout drift");
static_assert(offsetof(AverSynManagedCall, status) == offsetof(ManagedCall, status), "layout drift");
static_assert(offsetof(AverSynManagedCall, p) == offsetof(ManagedCall, p), "layout drift");
static_assert(offsetof(AverSynManagedCall, text) == offsetof(ManagedCall, text), "layout drift");
static_assert(offsetof(AverSynManagedCall, pos) == offsetof(ManagedCall, pos), "layout drift");
static_assert(offsetof(AverSynManagedCall, itemEntity) == offsetof(ManagedCall, itemEntity), "layout drift");
static_assert(offsetof(AverSynManagedCall, ctx) == offsetof(ManagedCall, ctx), "layout drift");
static_assert(offsetof(AverSynManagedCall, ctxEntity) == offsetof(ManagedCall, ctxEntity), "layout drift");
static_assert(offsetof(AverSynManagedCall, value) == offsetof(ManagedCall, value), "layout drift");
static_assert(AVER_SYN_MANAGED_CONDITION == kManagedCondition && AVER_SYN_MANAGED_ACTION == kManagedAction &&
              AVER_SYN_MANAGED_ABORT == kManagedAbort && AVER_SYN_MANAGED_EQS_TEST == kManagedEqsTest, "managed kind drift");
static_assert(AVER_SYN_EQS_UNKNOWN == static_cast<int>(EqsSystem::Status::Unknown) && AVER_SYN_EQS_QUEUED == static_cast<int>(EqsSystem::Status::Queued) &&
              AVER_SYN_EQS_RUNNING == static_cast<int>(EqsSystem::Status::Running) && AVER_SYN_EQS_FINISHED == static_cast<int>(EqsSystem::Status::Finished) &&
              AVER_SYN_EQS_FAILED == static_cast<int>(EqsSystem::Status::Failed) && AVER_SYN_EQS_CANCELLED == static_cast<int>(EqsSystem::Status::Cancelled), "EQS status drift");
static_assert(AVER_SYN_SO_FREE == static_cast<int>(SoSlotState::Free) && AVER_SYN_SO_CLAIMED == static_cast<int>(SoSlotState::Claimed) &&
              AVER_SYN_SO_OCCUPIED == static_cast<int>(SoSlotState::Occupied) && AVER_SYN_SO_COOLDOWN == static_cast<int>(SoSlotState::Cooldown) &&
              AVER_SYN_SO_DISABLED == static_cast<int>(SoSlotState::Disabled), "slot state drift");
static_assert(AVER_SYN_SO_REL_RELEASED == static_cast<int>(SoReleaseReason::Released) && AVER_SYN_SO_REL_COMPLETED == static_cast<int>(SoReleaseReason::Completed) &&
              AVER_SYN_SO_REL_FAILED == static_cast<int>(SoReleaseReason::Failed) && AVER_SYN_SO_REL_EXPIRED == static_cast<int>(SoReleaseReason::Expired) &&
              AVER_SYN_SO_REL_USER_GONE == static_cast<int>(SoReleaseReason::UserGone) && AVER_SYN_SO_REL_OBJECT_GONE == static_cast<int>(SoReleaseReason::ObjectGone), "release reason drift");
static_assert(AVER_SYN_SO_REFUSE_NONE == static_cast<int>(SoRefusal::None) && AVER_SYN_SO_REFUSE_NO_SUCH_SLOT == static_cast<int>(SoRefusal::NoSuchSlot) &&
              AVER_SYN_SO_REFUSE_DISABLED == static_cast<int>(SoRefusal::Disabled) && AVER_SYN_SO_REFUSE_NOT_FREE == static_cast<int>(SoRefusal::NotFree) &&
              AVER_SYN_SO_REFUSE_TAGS == static_cast<int>(SoRefusal::Tags) && AVER_SYN_SO_REFUSE_USER_LIMIT == static_cast<int>(SoRefusal::UserLimit) &&
              AVER_SYN_SO_REFUSE_NO_USER == static_cast<int>(SoRefusal::NoUser), "refusal drift");

namespace {

SynapseAi& inst() { return *static_cast<SynapseAi*>(aver_syn_ai_instance()); }
scene::World& world() { return scene::World::instance(); }

int32_t fail(AbiError e) {
    setAbiError(e);
    return 0;
}
int32_t ok() {
    setAbiError(AbiError::Ok);
    return 1;
}
// A valid question with no answer yet.
int32_t none() {
    setAbiError(AbiError::Ok);
    return 0;
}
bool live(int32_t e) {
    if (e > 0 && world().valid(static_cast<scene::Entity>(e))) return true;
    setAbiError(AbiError::BadHandle);
    return false;
}
scene::Entity ent(int32_t e) { return static_cast<scene::Entity>(e); }

TagList tagsOf(const char* csv) { return csv ? TagList::parse(csv) : TagList{}; }

thread_local int32_t t_refusal = 0;

// ---- host wiring thunks ------------------------------------------------------------------------------

aver_syn_notify_fn g_notifyFn = nullptr;
void* g_notifyUser = nullptr;
void notifyThunk(scene::Entity e, const char* name, void*) {
    if (g_notifyFn) g_notifyFn(static_cast<int32_t>(e), name, g_notifyUser);
}

aver_syn_managed_fn g_managedFn = nullptr;
void* g_managedUser = nullptr;
int32_t managedThunk(ManagedCall* call, void*) {
    return g_managedFn ? g_managedFn(reinterpret_cast<AverSynManagedCall*>(call), g_managedUser) : 0;
}

std::string g_message;

EqsRunParams paramsOf(int32_t querier, int32_t target, float px, float py, float pz, int32_t hasPoint, const char* tags) {
    EqsRunParams p;
    p.querier = static_cast<u32>(querier);
    p.target = target > 0 ? static_cast<u32>(target) : 0;
    p.point = Vec3{px, py, pz};
    p.hasPoint = hasPoint != 0;
    p.userTags = tagsOf(tags);
    static u32 s_runId = 1;
    p.runId = s_runId++;
    return p;
}

} // namespace

extern "C" {

int32_t aver_syn_query_abi_version(void) { return AVER_SYN_QUERY_ABI_VERSION; }

// ---- host wiring -------------------------------------------------------------------------------------

int32_t aver_syn_set_actor_provider(aver_syn_actors_fn fn, void* user) {
    // Same shape as ActorProviderFn (int32_t is i32), so the pointer is stored as is.
    inst().eqs().setActorProvider(reinterpret_cast<ActorProviderFn>(fn), user);
    return ok();
}

int32_t aver_syn_set_notify(aver_syn_notify_fn fn, void* user) {
    g_notifyFn = fn;
    g_notifyUser = user;
    inst().setNotify(fn ? &notifyThunk : nullptr, nullptr);
    return ok();
}

int32_t aver_syn_set_content_root(const char* dir) {
    if (!dir) return fail(AbiError::NullPointer);
    setAiContentRoot(dir);
    return ok();
}

int32_t aver_syn_eqs_set_budget(float maxMs, int32_t maxUnitsPerTick) {
    if (!std::isfinite(maxMs) || maxMs < 0.0f || maxUnitsPerTick < 0) return fail(AbiError::InvalidArgument);
    inst().eqs().setBudget(maxMs, static_cast<u32>(maxUnitsPerTick));
    return ok();
}

// ---- managed seam ------------------------------------------------------------------------------------

int32_t aver_syn_managed_set_dispatch(aver_syn_managed_fn fn, void* user) {
    g_managedFn = fn;
    g_managedUser = user;
    inst().managed().setDispatch(fn ? &managedThunk : nullptr, nullptr);
    return ok();
}

int32_t aver_syn_managed_register(int32_t kind, const char* name) {
    if (!name || !*name) {
        setAbiError(AbiError::InvalidArgument);
        return 0;
    }
    if (kind != AVER_SYN_MANAGED_CONDITION && kind != AVER_SYN_MANAGED_ACTION && kind != AVER_SYN_MANAGED_EQS_TEST) {
        setAbiError(AbiError::InvalidArgument);
        return 0;
    }
    const i32 id = inst().managed().registerName(kind, name);
    setAbiError(AbiError::Ok);
    return id;
}

void aver_syn_managed_clear(void) { inst().managed().clear(); }

int32_t aver_syn_managed_installed(void) { return inst().managed().installed() ? 1 : 0; }

// ---- environment queries -----------------------------------------------------------------------------

int32_t aver_syn_eqs_load(const char* path) {
    if (!path || !*path) {
        setAbiError(AbiError::InvalidArgument);
        return 0;
    }
    const u32 id = inst().eqs().loadQuery(path);
    g_message = inst().eqs().lastMessage();
    setAbiError(AbiError::Ok);
    return static_cast<int32_t>(id);
}

int32_t aver_syn_eqs_reload(const char* path) {
    if (!path || !*path) return fail(AbiError::InvalidArgument);
    const bool good = inst().eqs().reloadQuery(path);
    g_message = inst().eqs().lastMessage();
    setAbiError(AbiError::Ok);
    return good ? 1 : 0;
}

const char* aver_syn_eqs_last_message(void) { return g_message.c_str(); }


int32_t aver_syn_eqs_start(int32_t queryId, int32_t querier, int32_t target, float px, float py, float pz, int32_t hasPoint,
                           const char* userTagsCsv, int32_t priority) {
    if (queryId <= 0 || !inst().eqs().query(static_cast<u32>(queryId))) {
        setAbiError(AbiError::InvalidArgument);
        return 0;
    }
    SynapseAi::CallScope scope(inst());
    const u32 h = inst().eqs().start(static_cast<u32>(queryId), paramsOf(querier, target, px, py, pz, hasPoint, userTagsCsv), priority);
    setAbiError(AbiError::Ok);
    return static_cast<int32_t>(h);
}

int32_t aver_syn_eqs_run(int32_t queryId, int32_t querier, int32_t target, float px, float py, float pz, int32_t hasPoint,
                         const char* userTagsCsv) {
    if (queryId <= 0 || !inst().eqs().query(static_cast<u32>(queryId))) {
        setAbiError(AbiError::InvalidArgument);
        return 0;
    }
    SynapseAi::CallScope scope(inst());
    const u32 h = inst().eqs().runSync(static_cast<u32>(queryId), paramsOf(querier, target, px, py, pz, hasPoint, userTagsCsv));
    if (h != 0) {
        if (const EqsResult* r = inst().eqs().result(h)) g_message = r->reason;
    }
    setAbiError(AbiError::Ok);
    return static_cast<int32_t>(h);
}

int32_t aver_syn_eqs_status(int32_t handle) {
    setAbiError(AbiError::Ok);
    return static_cast<int32_t>(inst().eqs().status(static_cast<u32>(handle)));
}

int32_t aver_syn_eqs_count(int32_t handle) {
    setAbiError(AbiError::Ok);
    const EqsResult* r = inst().eqs().status(static_cast<u32>(handle)) == EqsSystem::Status::Finished ? inst().eqs().result(static_cast<u32>(handle)) : nullptr;
    return r ? static_cast<int32_t>(r->items.size()) : 0;
}

int32_t aver_syn_eqs_item(int32_t handle, int32_t index, float* outPos3, int32_t* outEntity, float* outScore, int32_t* outObject,
                          int32_t* outSlot) {
    const EqsResult* r = inst().eqs().status(static_cast<u32>(handle)) == EqsSystem::Status::Finished ? inst().eqs().result(static_cast<u32>(handle)) : nullptr;
    if (!r) return fail(AbiError::BadHandle);
    if (index < 0 || static_cast<usize>(index) >= r->items.size()) return fail(AbiError::OutOfRange);
    const EqsItem& it = r->items[static_cast<usize>(index)];
    if (outPos3) { outPos3[0] = it.pos.x; outPos3[1] = it.pos.y; outPos3[2] = it.pos.z; }
    if (outEntity) *outEntity = static_cast<int32_t>(it.entity);
    if (outScore) *outScore = it.score;
    if (outObject) *outObject = static_cast<int32_t>(it.object);
    if (outSlot) *outSlot = it.slot;
    return ok();
}

int32_t aver_syn_eqs_test_value(int32_t handle, int32_t index, int32_t test, float* out) {
    if (!out) return fail(AbiError::NullPointer);
    const EqsResult* r = inst().eqs().status(static_cast<u32>(handle)) == EqsSystem::Status::Finished ? inst().eqs().result(static_cast<u32>(handle)) : nullptr;
    if (!r) return fail(AbiError::BadHandle);
    if (index < 0 || static_cast<usize>(index) >= r->items.size() || test < 0 || static_cast<u32>(test) >= r->testCount) return fail(AbiError::OutOfRange);
    const f32 v = r->testValues[static_cast<usize>(index) * r->testCount + static_cast<usize>(test)];
    *out = std::isnan(v) ? 0.0f : v;
    setAbiError(AbiError::Ok);
    return std::isnan(v) ? 0 : 1;
}

int32_t aver_syn_eqs_stats(int32_t handle, int32_t* generated, int32_t* passed, int32_t* evaluations, int32_t* truncated) {
    const EqsResult* r = inst().eqs().result(static_cast<u32>(handle));
    if (!r) return fail(AbiError::BadHandle);
    if (generated) *generated = static_cast<int32_t>(r->generated);
    if (passed) *passed = static_cast<int32_t>(r->passed);
    if (evaluations) *evaluations = static_cast<int32_t>(r->evaluations);
    if (truncated) *truncated = r->truncated ? 1 : 0;
    return ok();
}

int32_t aver_syn_eqs_cancel(int32_t handle) {
    SynapseAi::CallScope scope(inst());
    if (inst().eqs().status(static_cast<u32>(handle)) == EqsSystem::Status::Unknown) return fail(AbiError::BadHandle);
    setAbiError(AbiError::Ok);
    return inst().eqs().cancel(static_cast<u32>(handle)) ? 1 : 0;
}

int32_t aver_syn_eqs_release(int32_t handle) {
    if (inst().eqs().status(static_cast<u32>(handle)) == EqsSystem::Status::Unknown) return fail(AbiError::BadHandle);
    inst().eqs().release(static_cast<u32>(handle));
    return ok();
}

int32_t aver_syn_eqs_pending(void) {
    setAbiError(AbiError::Ok);
    return static_cast<int32_t>(inst().eqs().pending());
}

// ---- smart objects -----------------------------------------------------------------------------------

int32_t aver_syn_so_attach(int32_t entity, const char* assetPath) {
    if (!assetPath || !*assetPath) return fail(AbiError::InvalidArgument);
    if (!live(entity)) return 0;
    if (!inst().smartObjects().attach(world(), ent(entity), assetPath))
        return fail(inst().smartObjects().componentType() == 0 ? AbiError::NotInitialised : AbiError::AllocationFailed);
    return ok();
}

int32_t aver_syn_so_set_enabled(int32_t entity, int32_t enabled) {
    if (!live(entity)) return 0;
    if (!inst().smartObjects().setEnabled(world(), ent(entity), enabled != 0)) return fail(AbiError::BadHandle);
    return ok();
}

int32_t aver_syn_so_slot_count(int32_t entity) {
    setAbiError(AbiError::Ok);
    if (entity <= 0 || !world().valid(ent(entity))) return -1;
    return inst().smartObjects().slotCount(world(), ent(entity));
}

int32_t aver_syn_so_slot_info(int32_t entity, int32_t slot, float* outPos3, float* outYaw, float* outEntry3, float* outEntryYaw,
                              int32_t* outState, int32_t* outUser) {
    if (!live(entity)) return 0;
    const i32 n = inst().smartObjects().slotCount(world(), ent(entity));
    if (n < 0) return fail(AbiError::BadHandle);
    if (slot < 0 || slot >= n) return fail(AbiError::OutOfRange);
    SoSlotInfo info;
    if (!inst().smartObjects().slotInfo(world(), ent(entity), static_cast<u32>(slot), info)) return fail(AbiError::OutOfRange);
    if (outPos3) { outPos3[0] = info.world.pos.x; outPos3[1] = info.world.pos.y; outPos3[2] = info.world.pos.z; }
    if (outYaw) *outYaw = info.world.yawDeg;
    if (outEntry3) { outEntry3[0] = info.world.entry.x; outEntry3[1] = info.world.entry.y; outEntry3[2] = info.world.entry.z; }
    if (outEntryYaw) *outEntryYaw = info.world.entryYawDeg;
    if (outState) *outState = static_cast<int32_t>(info.state);
    if (outUser) *outUser = static_cast<int32_t>(info.user);
    return ok();
}

int32_t aver_syn_so_set_slot_enabled(int32_t entity, int32_t slot, int32_t enabled) {
    if (!live(entity)) return 0;
    if (slot < 0) return fail(AbiError::OutOfRange);
    if (!inst().smartObjects().setSlotEnabled(world(), ent(entity), static_cast<u32>(slot), enabled != 0)) return fail(AbiError::OutOfRange);
    return ok();
}

int32_t aver_syn_so_find(int32_t user, float x, float y, float z, float radius, const char* slotTagsCsv, const char* userTagsCsv,
                         int32_t flags, int32_t* outObjects, int32_t* outSlots, int32_t cap) {
    (void)user;
    if (!outObjects || !outSlots) return fail(AbiError::NullPointer);
    if (cap <= 0 || !std::isfinite(radius) || radius < 0.0f) return fail(AbiError::InvalidArgument);
    SoQuery q;
    q.origin = Vec3{x, y, z};
    q.radius = radius;
    q.slotTags = tagsOf(slotTagsCsv);
    q.userTags = tagsOf(userTagsCsv);
    q.matchAll = (flags & AVER_SYN_SO_FIND_MATCH_ALL) != 0;
    q.includeClaimed = (flags & AVER_SYN_SO_FIND_INCLUDE_CLAIMED) != 0;
    q.max = static_cast<u32>(cap);
    std::vector<SoSlotRef> refs(static_cast<usize>(cap));
    const u32 n = inst().smartObjects().find(world(), q, refs.data(), nullptr, static_cast<u32>(cap));
    for (u32 i = 0; i < n; ++i) {
        outObjects[i] = static_cast<int32_t>(refs[i].object);
        outSlots[i] = static_cast<int32_t>(refs[i].slot);
    }
    setAbiError(AbiError::Ok);
    return static_cast<int32_t>(n);
}

int32_t aver_syn_so_claim(int32_t user, int32_t object, int32_t slot, const char* userTagsCsv, float ttlSec) {
    t_refusal = AVER_SYN_SO_REFUSE_NONE;
    if (!live(user)) {
        t_refusal = AVER_SYN_SO_REFUSE_NO_USER;
        return 0;
    }
    if (object <= 0 || slot < 0 || !world().valid(ent(object))) {
        t_refusal = AVER_SYN_SO_REFUSE_NO_SUCH_SLOT;
        setAbiError(AbiError::Ok);
        return 0;
    }
    SynapseAi::CallScope scope(inst());
    SoRefusal why = SoRefusal::None;
    const u32 h = inst().smartObjects().claim(world(), ent(user), SoSlotRef{static_cast<u32>(object), static_cast<u32>(slot)}, tagsOf(userTagsCsv),
                                              std::isfinite(ttlSec) ? ttlSec : -1.0f, &why);
    t_refusal = static_cast<int32_t>(why);
    setAbiError(AbiError::Ok);
    return static_cast<int32_t>(h);
}

int32_t aver_syn_so_last_refusal(void) { return t_refusal; }

int32_t aver_syn_so_use(int32_t claim) {
    SynapseAi::CallScope scope(inst());
    SoClaimInfo info;
    if (claim <= 0 || !inst().smartObjects().claimInfo(static_cast<u32>(claim), info)) return fail(AbiError::BadHandle);
    setAbiError(AbiError::Ok);
    return inst().smartObjects().use(world(), static_cast<u32>(claim)) ? 1 : 0;
}

int32_t aver_syn_so_release(int32_t claim, int32_t reason) {
    if (reason < AVER_SYN_SO_REL_RELEASED || reason > AVER_SYN_SO_REL_OBJECT_GONE) return fail(AbiError::InvalidArgument);
    SynapseAi::CallScope scope(inst());
    SoClaimInfo info;
    if (claim <= 0 || !inst().smartObjects().claimInfo(static_cast<u32>(claim), info)) return fail(AbiError::BadHandle);
    return inst().smartObjects().release(world(), static_cast<u32>(claim), static_cast<SoReleaseReason>(reason)) ? ok() : 0;
}

int32_t aver_syn_so_claim_info(int32_t claim, int32_t* outObject, int32_t* outSlot, int32_t* outUser, int32_t* outState, float* outPos3,
                               float* outYaw, float* outEntry3, float* outEntryYaw) {
    SoClaimInfo info;
    if (claim <= 0 || !inst().smartObjects().claimInfo(static_cast<u32>(claim), info)) return fail(AbiError::BadHandle);
    if (outObject) *outObject = static_cast<int32_t>(info.slot.object);
    if (outSlot) *outSlot = static_cast<int32_t>(info.slot.slot);
    if (outUser) *outUser = static_cast<int32_t>(info.user);
    if (outState) *outState = static_cast<int32_t>(info.state);
    if (outPos3) { outPos3[0] = info.world.pos.x; outPos3[1] = info.world.pos.y; outPos3[2] = info.world.pos.z; }
    if (outYaw) *outYaw = info.world.yawDeg;
    if (outEntry3) { outEntry3[0] = info.world.entry.x; outEntry3[1] = info.world.entry.y; outEntry3[2] = info.world.entry.z; }
    if (outEntryYaw) *outEntryYaw = info.world.entryYawDeg;
    return ok();
}

int32_t aver_syn_so_claims_of(int32_t user, int32_t* out, int32_t cap) {
    if (!out) return fail(AbiError::NullPointer);
    if (cap <= 0) return fail(AbiError::InvalidArgument);
    std::vector<u32> hs(static_cast<usize>(cap));
    const u32 n = inst().smartObjects().claimsOf(ent(user), hs.data(), static_cast<u32>(cap));
    for (u32 i = 0; i < n; ++i) out[i] = static_cast<int32_t>(hs[i]);
    setAbiError(AbiError::Ok);
    return static_cast<int32_t>(n);
}

} // extern "C"
