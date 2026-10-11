#ifndef AVER_FRAMEWORK_SEQUENCE_ABI_H
#define AVER_FRAMEWORK_SEQUENCE_ABI_H

/* Level sequence C ABI (Aver.Framework): play, pause, stop, scrub, bind and query the level's sequences,
 * relayed to game::SequenceDirector.
 *
 * Same conventions as framework_anim_abi.h (int32_t/int64_t/float only, 1 on success and 0 on refusal).
 * Aver.Framework does not link the runtime, so a composition root installs ONE provider function and
 * every call below forwards to it; with no provider every call returns 0 and nothing is written. The
 * provider is game::sequenceProvider (Runtime/include/aver/game/SequenceHost.hpp). Own version, separate
 * from AVER_FW_ABI_VERSION_MINOR. A sequence is named by the fnv1a64 hash of its name, a binding likewise
 * (Assets.ObjectIdOf in C#). Semantics: docs/SEQUENCER.md. */

#include "aver/framework/framework_abi.h"   /* AVER_FW_ABI, AVER_FW_CALL */

#ifdef __cplusplus
extern "C" {
#endif

#define AVER_FW_SEQUENCE_ABI_VERSION 1
AVER_FW_ABI int32_t aver_fw_sequence_abi_version(void);

/* Provider operations; the argument usage of each is listed with it. */
#define AVER_FW_SEQ_OP_PLAY      1   /* h0 sequence, f0 start seconds (<0 continue, a stopped one starts at 0), f1 rate (0 keeps) */
#define AVER_FW_SEQ_OP_PAUSE     2   /* h0 sequence */
#define AVER_FW_SEQ_OP_STOP      3   /* h0 sequence (0 = every sequence): pause, time 0, restore, stop voices */
#define AVER_FW_SEQ_OP_SET_TIME  4   /* h0 sequence, f0 seconds (a jump: fires nothing for the skipped span) */
#define AVER_FW_SEQ_OP_SET_RATE  5   /* h0 sequence, f0 rate >= 0 */
#define AVER_FW_SEQ_OP_PLAYING   6   /* h0 sequence (0 = any): 1 when playing */
#define AVER_FW_SEQ_OP_STATE     7   /* h0 sequence; outF[0..2] = time, length, rate; outI[0] = playing */
#define AVER_FW_SEQ_OP_BIND      8   /* h0 sequence, h1 binding-name hash, entity = the actor (0 clears the override) */

typedef int32_t (AVER_FW_CALL* aver_fw_sequence_fn)(int32_t op, int32_t entity, int64_t h0, int64_t h1,
                                                    float f0, float f1, float* outF, int64_t* outI, void* user);
/* Installs the provider. Passing null clears it. Always returns 1. */
AVER_FW_ABI int32_t aver_fw_set_sequence_provider(aver_fw_sequence_fn fn, void* user);

/* Starts or resumes the sequence. startSeconds < 0 continues where it is (a stopped or finished one starts
 * at 0); rate 0 keeps the current rate. 1 on success, 0 for an unknown sequence. */
AVER_FW_ABI int32_t aver_fw_sequence_play(int64_t sequence, float startSeconds, float rate);
AVER_FW_ABI int32_t aver_fw_sequence_pause(int64_t sequence);
/* Stops the sequence (0 = every sequence): pause, time 0, what it wrote is put back, voices stop. */
AVER_FW_ABI int32_t aver_fw_sequence_stop(int64_t sequence);
AVER_FW_ABI int32_t aver_fw_sequence_set_time(int64_t sequence, float seconds);
AVER_FW_ABI int32_t aver_fw_sequence_set_rate(int64_t sequence, float rate);
/* 1 when the sequence (0 = any) is playing. */
AVER_FW_ABI int32_t aver_fw_sequence_is_playing(int64_t sequence);
/* Writes time, length, rate (3 floats) and whether it is playing; the outputs are untouched and 0 returned
 * for an unknown sequence. */
AVER_FW_ABI int32_t aver_fw_sequence_state(int64_t sequence, float* outTimeLengthRate, int32_t* outPlaying);
/* Makes the actor the one the sequence's `bind` name refers to (entity 0 clears the override). 1 when the
 * sequence has a track bound by that name. */
AVER_FW_ABI int32_t aver_fw_sequence_bind(int64_t sequence, int64_t bindingName, int32_t entity);

#ifdef __cplusplus
}
#endif

#endif /* AVER_FRAMEWORK_SEQUENCE_ABI_H */
