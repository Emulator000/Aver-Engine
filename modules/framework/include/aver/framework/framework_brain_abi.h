#ifndef AVER_FRAMEWORK_BRAIN_ABI_H
#define AVER_FRAMEWORK_BRAIN_ABI_H

/* Brain C ABI (Aver.Framework): give an entity a behaviour tree (.ocbt) or a state tree (.ocst), send a
 * state tree events, and read where it is. A relay like the blackboard ABI: Aver.Framework does not link
 * Aver.Synapse.Scene, so every call forwards to the blackboard provider a composition root installs
 * (aver_fw_set_blackboard_provider, framework_blackboard_abi.h) using ops AVER_FW_BB_OP_BRAIN_*. A
 * provider that predates those ops answers 0, which every function here treats as "no brain".
 * Semantics: docs/AI_QUERIES_SMARTOBJECTS_STATETREE.md. AI: wave 3 #21. */

#include "aver/framework/framework_blackboard_abi.h"   /* AVER_FW_ABI, AVER_FW_CALL, the provider and its ops */

#ifdef __cplusplus
extern "C" {
#endif

#define AVER_FW_BRAIN_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_brain_abi_version(void);

#define AVER_FW_BRAIN_NONE 0
#define AVER_FW_BRAIN_BT 1
#define AVER_FW_BRAIN_STATETREE 2

#define AVER_FW_BRAIN_IDLE 0
#define AVER_FW_BRAIN_RUNNING 1
#define AVER_FW_BRAIN_SUCCEEDED 2
#define AVER_FW_BRAIN_FAILED 3

/* Assigns the brain at a content-relative .ocbt or .ocst path, attaching the component when missing.
 * 1 on success, 0 when the file does not load, the extension is neither, or there is no provider. */
AVER_FW_ABI int32_t aver_fw_brain_set_asset(int32_t entity, const char* path);
/* AVER_FW_BRAIN_NONE / _BT / _STATETREE. */
AVER_FW_ABI int32_t aver_fw_brain_kind(int32_t entity);
/* AVER_FW_BRAIN_IDLE (also stopped, or no brain) / _RUNNING / _SUCCEEDED / _FAILED. */
AVER_FW_ABI int32_t aver_fw_brain_status(int32_t entity);
/* Queues an event for a state tree; visible to its next update only. Also sets the blackboard keys
 * EventPayload (Int) and EventPayloadF (Float) when a transition consumes it. 0 for a non-state-tree brain. */
AVER_FW_ABI int32_t aver_fw_brain_send_event(int32_t entity, const char* name, int32_t payloadInt, float payloadFloat);
/* The active leaf state's dotted path ("Combat.Chase"). 1 and the text, or 0 and "" when none. */
AVER_FW_ABI int32_t aver_fw_brain_state(int32_t entity, char* buf, int32_t cap);
/* 1 when the state path is anywhere in the active chain ("Combat" is while "Combat.Chase" runs). */
AVER_FW_ABI int32_t aver_fw_brain_in_state(int32_t entity, const char* path);
/* Exit every state (aborting running tasks) and start again at the next update. */
AVER_FW_ABI int32_t aver_fw_brain_restart(int32_t entity);
/* Exit every state and stay stopped until aver_fw_brain_restart. */
AVER_FW_ABI int32_t aver_fw_brain_stop(int32_t entity);

#ifdef __cplusplus
}
#endif

#endif /* AVER_FRAMEWORK_BRAIN_ABI_H */
