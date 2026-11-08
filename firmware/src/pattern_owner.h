/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_PATTERN_OWNER_H
#define FM1_PATTERN_OWNER_H
#if defined(FM1_DEVICE) && (!defined(FM1_PATTERN_TARGET_EXPERIMENT) || !FM1_PATTERN_TARGET_EXPERIMENT)
#error "Experimental pattern owner is host-only"
#endif
#include "pattern_bank.h"
/* This singleton is an adapter for host UI experiments only. The caller must
 * keep audio readers and foreground edits stopped for every operation; the
 * supplied predicate attests that condition, not merely transport stopped.
 * No bar-boundary changes, background synchronization, or persistence. */
extern fm1_pattern_song br_ps_state;
/* Pass NULL as clipboard to keep it compressed in the bounded arena. */
int br_ps_host_init(uint8_t *arena, size_t arena_capacity,
    br_pattern_snapshot *live, br_pattern_snapshot *work,
    br_pattern_snapshot *clipboard, uint8_t *encoded, size_t encoded_capacity,
    int (*is_quiescent)(void *), void *context);
/* Adopt an independently staged bank and live pattern under the same
 * quiescence requirement. The source arena transfers to the owner and must
 * remain alive and exclusively owned until the owner is reinitialized. */
/* Under an exclusive stopped project transaction, lend the two owner
 * snapshots and codec scratch. No other owner/engine/project/diagnostic work
 * may run until release; only export and copy-adopt are admitted. */
int br_ps_host_workspace_acquire(br_pattern_snapshot **previous_live,
    br_pattern_snapshot **stage, uint8_t **scratch, size_t *scratch_capacity);
void br_ps_host_workspace_release(void);
/* Under stopped quiescence, start an empty pattern/song bank using current
 * engine globals. No flash is touched; pending switches reject reset. */
/* Under stopped exclusive quiescence, preflight before a live session restore.
 * No other owner or audio handoff may start until the matching reset finishes. */
int br_ps_host_can_reset(void);
int br_ps_host_reset(void);
/* Preflight may use and overwrite owner staging; keep source immutable and
 * quiescence held through subsequent session restore and adopt. */
int br_ps_host_can_adopt(const br_pattern_bank *source, const br_pattern_snapshot *active,
                         const fm1_pattern_song *song);
int br_ps_host_adopt(const br_pattern_bank *source, const br_pattern_snapshot *active,
                     const fm1_pattern_song *song);
/* Constrained load: validate a distinct staged arena, then deterministically
 * copy its bank into the existing owner arena after session restore succeeds.
 * Keep the same exclusive quiescence and immutable source through both calls. */
int br_ps_host_can_adopt_copy(const br_pattern_bank *, const br_pattern_snapshot *, const fm1_pattern_song *);
int br_ps_host_adopt_copy(const br_pattern_bank *, const br_pattern_snapshot *, const fm1_pattern_song *);
/* Stopped/quiescent view for serialization. The copied bank points to the
 * owner's arena; no owner operation may mutate it until serialization ends.
 * Runtime transport state is reset in the returned song copy. */
int br_ps_host_export(br_pattern_bank *view, br_pattern_snapshot *active,
                      fm1_pattern_song *song);
int br_ps_select(uint32_t target);
int br_ps_copy_pattern(uint32_t source);
int br_ps_paste_pattern(uint32_t target);
#if defined(BR_SPLIT_AUDIO) && defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
#include "pattern_publication_irq.h"
/* Experimental host bridge. Bind under a stopped quiescent transaction; queue
 * under the audio foreground claim. Service must run outside that claim when
 * edits_blocked is true. No song progression or device use is supported. */
int br_ps_runtime_bind(br_pattern_irq_publication *, br_pattern_snapshot *, br_pattern_snapshot *, br_pattern_snapshot *);
int br_ps_runtime_queue(uint32_t target);
/* Under stopped quiescence with the audio IRQ excluded, cancel a queued
 * switch only if audio has not consumed it. */
int br_ps_runtime_cancel_stopped(void);
/* Clear a reported runtime fault only while stopped and quiescent. If an
 * unseen request remains, it is canceled; a consumed handoff must finish. */
int br_ps_runtime_reset_stopped(void);
/* Returns 1 on acknowledged completion, 0 while waiting, -1 on error. An
 * error after the swap leaves edits gated for explicit recovery. */
int br_ps_runtime_service(void);
#endif
#endif
