#ifndef AVER_FRAMEWORK_STATE_ABI_H
#define AVER_FRAMEWORK_STATE_ABI_H

/* Managed entity state C ABI (Aver.Framework): lets state that lives in C# ride in a save.
 *
 * Save is native and cannot see managed memory, so a managed system (gameplay abilities is the first)
 * installs ONE provider of three functions and the composition roots relay the five entry points below
 * into aver::save::Host. Same conventions as framework_timers_abi.h: int32_t/char only, UTF-8, the
 * framework holds function pointers and links nothing. With no provider every call answers 0 / -1.
 * A "channel" is a named blob of text per entity; channels are generic so a later system can use
 * another name. Own version, bumped on additive change. */

#include "aver/framework/framework_abi.h"   /* AVER_FW_ABI, AVER_FW_CALL */

#ifdef __cplusplus
extern "C" {
#endif

#define AVER_FW_STATE_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_state_abi_version(void);

/* Number of named state channels entity `e` currently carries (0 = none). */
typedef int32_t (AVER_FW_CALL* aver_fw_state_count_fn)(int32_t entity, void* user);
/* Channel `index` (0..count-1, stable order): copies the NUL-terminated UTF-8 name into nameBuf
 * (truncated to nameCap) and the payload text into dataBuf (up to dataCap bytes, NOT terminated).
 * Returns the payload's FULL byte length, which may exceed dataCap (the caller retries with a bigger
 * buffer), or -1 for a bad index. */
typedef int32_t (AVER_FW_CALL* aver_fw_state_at_fn)(int32_t entity, int32_t index,
        char* nameBuf, int32_t nameCap, char* dataBuf, int32_t dataCap, void* user);
/* Delivers one channel to the entity (restore). 1 if accepted, 0 if the provider does not know the
 * channel or the payload was rejected. */
typedef int32_t (AVER_FW_CALL* aver_fw_state_set_fn)(int32_t entity, const char* name,
        const char* data, int32_t dataLen, void* user);

AVER_FW_ABI int32_t aver_fw_set_state_provider(aver_fw_state_count_fn count, aver_fw_state_at_fn at,
                                               aver_fw_state_set_fn setState, void* user); /* always 1 */
AVER_FW_ABI int32_t aver_fw_state_count(int32_t entity);                                     /* 0 with no provider */
AVER_FW_ABI int32_t aver_fw_state_at(int32_t entity, int32_t index, char* nameBuf, int32_t nameCap,
                                     char* dataBuf, int32_t dataCap);                        /* -1 with no provider */
AVER_FW_ABI int32_t aver_fw_state_set(int32_t entity, const char* name, const char* data, int32_t dataLen); /* 0 with no provider */

#ifdef __cplusplus
}
#endif

#endif /* AVER_FRAMEWORK_STATE_ABI_H */
