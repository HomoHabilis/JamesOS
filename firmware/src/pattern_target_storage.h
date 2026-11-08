/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FM1_PATTERN_TARGET_STORAGE_H
#define FM1_PATTERN_TARGET_STORAGE_H
#if !defined(FM1_PATTERN_TARGET_EXPERIMENT) || !FM1_PATTERN_TARGET_EXPERIMENT
#error "Target pattern storage is a link-only experiment"
#endif
#include "pattern_project_adapter.h"
#include "pattern_park_io.h"
/* Handle storage belongs to the caller and must remain live until end. A
 * stale handle retains its old token and cannot authorize a later loan. */
typedef struct { pfs_store store; uint32_t token; } br_ps_target_store_handle;
/* mode 0: save; 1: load; 2: scan only (no session scratch loan). */
int br_ps_target_project_begin(int,fm1_project_adapter **,const fm1_project_adapter_ops **,uint32_t *);
int br_ps_target_project_mode(uint32_t,int);
int br_ps_target_project_store(uint32_t,br_ps_target_store_handle *);
int br_ps_target_project_end(uint32_t);
#if defined(FM1_PATTERN_NAMESPACE_TARGET_EXPERIMENT) && FM1_PATTERN_NAMESPACE_TARGET_EXPERIMENT
#include "pattern_journal_gate.h"
/* Link-only: caller must independently authenticate and pin namespace data.
 * Handle and sector scratch are transaction-owned and remain live until end;
 * the namespace and its store must remain immutable and independently pinned. */
typedef struct { uint32_t token; const pns_namespace *ns; pns_namespace pinned; pfs_store pinned_store; void *sector; pns_journal_gate gate; unsigned writing; } br_ps_target_journal;
br_ps_target_journal *br_ps_target_journal_handle(void);
int br_ps_target_journal_init(br_ps_target_journal *,uint32_t,const pns_namespace *,void *);
int br_ps_target_journal_program(void *,uint32_t,const void *,uint32_t);
void br_ps_target_journal_close(br_ps_target_journal *);
#endif
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
/* Provisioning only: arm after project begin, before any scan/write, and end
 * before project end. Activity is never consumed; any change aborts I/O. */
int br_ps_target_provision_watch_begin(uint32_t token);
int br_ps_target_provision_watch_end(uint32_t token);
#endif
/* A latched fault requires recovery; caller must not retry as BUSY. */
/* Borrow only for the same active token. The pointer does not outlive end; a
 * retained handle must independently reject stale token/fence changes. */
br_pattern_park_io *br_ps_target_project_park(uint32_t token);
/* Valid only for the same active token and while the workspace loan is held. */
size_t br_ps_target_project_scratch_capacity(uint32_t token);
int br_ps_target_project_faulted(void);
#endif
