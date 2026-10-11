#pragma once
/* Aver.Synapse.Abi -- environment queries, smart objects and the managed (C#) leaf seam.
 *
 * Conventions as synapse_ai_abi.h: centimetres, +X forward, +Y right, +Z up; entities are the scene's
 * 32-bit handles; strings are UTF-8; functions return 1 on success and 0 on failure unless stated;
 * aver_syn_last_error() says why (0 on success, -1 bad handle, -2 null pointer, -4 out of range,
 * -6 invalid argument). A valid question with no answer yet (an unfinished query, a refused claim) records 0.
 *
 * ONE INSTANCE: EQS and smart objects live in the SynapseAi behind this DLL. A host that links the static
 * libraries must go through these functions (or aver_syn_ai_instance), never build its own.
 *
 * Semantics: docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md. AI: wave 3 #21. */

#include "aver/synapse/synapse_ai_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AVER_SYN_QUERY_ABI_VERSION 1
AVER_SYN_API int32_t aver_syn_query_abi_version(void);

/* ---- host wiring ------------------------------------------------------------------------------ */

/* Writes up to `cap` entity handles whose class is `className` (and derived classes when includeDerived)
 * and returns how many. Answers the `class=` filter of the actors generator. NULL clears. */
typedef int32_t (*aver_syn_actors_fn)(const char* className, int32_t includeDerived, int32_t* out, int32_t cap, void* user);
AVER_SYN_API int32_t aver_syn_set_actor_provider(aver_syn_actors_fn fn, void* user);
/* Named events (EqsFinished, SmartObjectClaimed, ...) fire through this, the animation-notify shape. NULL clears. */
typedef void (*aver_syn_notify_fn)(int32_t entity, const char* eventName, void* user);
AVER_SYN_API int32_t aver_syn_set_notify(aver_syn_notify_fn fn, void* user);
/* The content folder content-relative .oceqs / .ocso / .ocst / .ocbt paths resolve against. */
AVER_SYN_API int32_t aver_syn_set_content_root(const char* dir);
/* Per-tick time slice for queued EQS runs. Defaults 1.0 ms and 0 units; 0 disables that limit. */
AVER_SYN_API int32_t aver_syn_eqs_set_budget(float maxMs, int32_t maxUnitsPerTick);

/* ---- managed seam ------------------------------------------------------------------------------ */

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

/* The single dispatch function the managed side installs. Returns 1 when it handled the call. */
typedef int32_t (*aver_syn_managed_fn)(AverSynManagedCall* call, void* user);
AVER_SYN_API int32_t aver_syn_managed_set_dispatch(aver_syn_managed_fn fn, void* user);   /* NULL clears */
/* Returns nameId > 0, or 0 when the name is taken or clashes with a native leaf. Kinds 0 and 1 also
 * register a Condition / Action leaf of that name into the host's behaviour-tree registry. */
AVER_SYN_API int32_t aver_syn_managed_register(int32_t kind, const char* name);
/* Script unload: every managed name goes inactive; their leaves answer Failure / false afterwards. */
AVER_SYN_API void    aver_syn_managed_clear(void);
AVER_SYN_API int32_t aver_syn_managed_installed(void);   /* 1 when a dispatch function is set */

/* ---- Environment queries ------------------------------------------------------------------------ */

#define AVER_SYN_EQS_UNKNOWN   0
#define AVER_SYN_EQS_QUEUED    1
#define AVER_SYN_EQS_RUNNING   2
#define AVER_SYN_EQS_FINISHED  3
#define AVER_SYN_EQS_FAILED    4
#define AVER_SYN_EQS_CANCELLED 5

AVER_SYN_API int32_t aver_syn_eqs_load(const char* path);                 /* query id, 0 on failure (see aver_syn_eqs_last_message) */
AVER_SYN_API int32_t aver_syn_eqs_reload(const char* path);
AVER_SYN_API const char* aver_syn_eqs_last_message(void);                 /* parse / run failure text, "" when none; valid until the next call */
/* Queues a run; the result appears over the next ticks. Returns the run handle, 0 when refused. */
AVER_SYN_API int32_t aver_syn_eqs_start(int32_t queryId, int32_t querier, int32_t target, float px, float py, float pz,
                                        int32_t hasPoint, const char* userTagsCsv, int32_t priority);
/* Runs to completion before returning. */
AVER_SYN_API int32_t aver_syn_eqs_run(int32_t queryId, int32_t querier, int32_t target, float px, float py, float pz,
                                      int32_t hasPoint, const char* userTagsCsv);
AVER_SYN_API int32_t aver_syn_eqs_status(int32_t handle);                 /* AVER_SYN_EQS_* */
AVER_SYN_API int32_t aver_syn_eqs_count(int32_t handle);                  /* items of a finished run, 0 otherwise */
AVER_SYN_API int32_t aver_syn_eqs_item(int32_t handle, int32_t index, float* outPos3, int32_t* outEntity, float* outScore,
                                       int32_t* outObject, int32_t* outSlot);   /* best first; any out pointer may be NULL */
AVER_SYN_API int32_t aver_syn_eqs_test_value(int32_t handle, int32_t index, int32_t test, float* out);   /* 0 when that test did not run for the item */
AVER_SYN_API int32_t aver_syn_eqs_stats(int32_t handle, int32_t* generated, int32_t* passed, int32_t* evaluations, int32_t* truncated);
AVER_SYN_API int32_t aver_syn_eqs_cancel(int32_t handle);
AVER_SYN_API int32_t aver_syn_eqs_release(int32_t handle);
AVER_SYN_API int32_t aver_syn_eqs_pending(void);

/* ---- Smart objects ------------------------------------------------------------------------------ */

#define AVER_SYN_SO_FREE     0
#define AVER_SYN_SO_CLAIMED  1
#define AVER_SYN_SO_OCCUPIED 2
#define AVER_SYN_SO_COOLDOWN 3
#define AVER_SYN_SO_DISABLED 4

#define AVER_SYN_SO_REL_RELEASED    0
#define AVER_SYN_SO_REL_COMPLETED   1
#define AVER_SYN_SO_REL_FAILED      2
#define AVER_SYN_SO_REL_EXPIRED     3
#define AVER_SYN_SO_REL_USER_GONE   4
#define AVER_SYN_SO_REL_OBJECT_GONE 5

#define AVER_SYN_SO_REFUSE_NONE         0
#define AVER_SYN_SO_REFUSE_NO_SUCH_SLOT 1
#define AVER_SYN_SO_REFUSE_DISABLED     2
#define AVER_SYN_SO_REFUSE_NOT_FREE     3
#define AVER_SYN_SO_REFUSE_TAGS         4
#define AVER_SYN_SO_REFUSE_USER_LIMIT   5
#define AVER_SYN_SO_REFUSE_NO_USER      6

#define AVER_SYN_SO_FIND_NONE            0
#define AVER_SYN_SO_FIND_MATCH_ALL       1
#define AVER_SYN_SO_FIND_INCLUDE_CLAIMED 2

AVER_SYN_API int32_t aver_syn_so_attach(int32_t entity, const char* assetPath);   /* adds CSmartObject and sets `asset` */
AVER_SYN_API int32_t aver_syn_so_set_enabled(int32_t entity, int32_t enabled);
AVER_SYN_API int32_t aver_syn_so_slot_count(int32_t entity);                      /* -1 when not a registered object */
AVER_SYN_API int32_t aver_syn_so_slot_info(int32_t entity, int32_t slot, float* outPos3, float* outYaw, float* outEntry3,
                                           float* outEntryYaw, int32_t* outState, int32_t* outUser);
AVER_SYN_API int32_t aver_syn_so_set_slot_enabled(int32_t entity, int32_t slot, int32_t enabled);
/* Slots within `radius` of (x, y, z), nearest first; returns the count written. `slotTagsCsv` filters against the
 * object's and slot's tags (any, or all with FIND_MATCH_ALL); the slot's REQUIRES / BLOCKEDBY are checked
 * against `userTagsCsv`. */
AVER_SYN_API int32_t aver_syn_so_find(int32_t user, float x, float y, float z, float radius, const char* slotTagsCsv,
                                      const char* userTagsCsv, int32_t flags, int32_t* outObjects, int32_t* outSlots, int32_t cap);
/* Claim handle, or 0 when refused (aver_syn_so_last_refusal says why; aver_syn_last_error stays 0).
 * ttlSec < 0 uses the slot's CLAIMTTL, 0 never expires. */
AVER_SYN_API int32_t aver_syn_so_claim(int32_t user, int32_t object, int32_t slot, const char* userTagsCsv, float ttlSec);
AVER_SYN_API int32_t aver_syn_so_last_refusal(void);                              /* AVER_SYN_SO_REFUSE_* of the last claim, per thread */
AVER_SYN_API int32_t aver_syn_so_use(int32_t claim);
AVER_SYN_API int32_t aver_syn_so_release(int32_t claim, int32_t reason);          /* AVER_SYN_SO_REL_* */
AVER_SYN_API int32_t aver_syn_so_claim_info(int32_t claim, int32_t* outObject, int32_t* outSlot, int32_t* outUser, int32_t* outState,
                                            float* outPos3, float* outYaw, float* outEntry3, float* outEntryYaw);
AVER_SYN_API int32_t aver_syn_so_claims_of(int32_t user, int32_t* out, int32_t cap);

#ifdef __cplusplus
}
#endif
