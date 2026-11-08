/* Host-tested fixed-pair save bridge. No default target binding or authority. */
#ifndef FM1_PATTERN_FIXED_BRIDGE_H
#define FM1_PATTERN_FIXED_BRIDGE_H
#include "pattern_fixed_project.h"
#include "pattern_project_adapter.h"
#define FM1_FIXED_MAX_NAMED_SLOTS 12u
#define FM1_FIXED_WORKING_SLOT 12u
/* Source and all callback/configuration objects stay live and immutable for
 * the transaction. Writable workspaces are disjoint from source/live data. */
typedef struct { const ps_store *store; fm1_project_source *source; uint8_t *payload,*verify; fm1_fixed_preflight_fn preflight; void *preflight_ctx;
 /* Optional certified backend: if supplied, it must recheck both sector
  * certificates and the fence on every storage operation. */
 int (*write_pair)(void *,uint32_t,const char *,fm1_project_source *,unsigned initializing,pops_certify_fn,void *); void *write_ctx; } fm1_fixed_bridge_transaction;
typedef struct { pops_certify_fn certify; void *context; unsigned initializing; } fm1_fixed_bridge_access;
typedef struct {
 void *context;
 /* Claim the writer fence, capture the current project and certify up front
  * that both the named and working pair are eligible. On failure no write;
  * return 0 for busy/denied or negative for an uncontained fault. */
 int (*prepare)(void *,uint32_t,fm1_fixed_bridge_transaction *);
 /* A fresh token-bound certificate is opened separately for each write; close
  * it before changing the authority journal. Return 0 for denial, negative
  * for a lost/uncontained fence, or exactly 1 for success. */
 int (*open_pair)(void *,uint32_t,fm1_fixed_bridge_access *);
 int (*close_pair)(void *,fm1_fixed_bridge_access *);
 int (*activate)(void *,uint32_t);
 /* Release/verify the same fence; failure latches the bridge. */
 int (*finish)(void *);
 /* Load callbacks are optional until a backend is bound. prepare_load owns the
  * same fence and preflights both source and working access. stage_load uses
  * the supplied ACTIVE certificate to fully validate into disposable staging,
  * without touching live state. adopt returns 1 on success, 0 if unchanged,
  * or -1 if rollback failed. Once adoption succeeds, a later working-save or
  * fence-release error can leave the new live project active; it must not be
  * reported as an unchanged-state failure. stage_load and recapture return 0 on a contained
  * failure and negative on an uncontained fault. recapture rebuilds the source
  * after adoption. */
 int (*prepare_load)(void *,uint32_t,fm1_fixed_bridge_transaction *);
 int (*stage_load)(void *,uint32_t,pops_certify_fn,void *);
 int (*adopt)(void *);
 int (*recapture)(void *,fm1_fixed_bridge_transaction *);
 /* Backend declares 11 or 12 usable named slots; never infer ownership. */
 unsigned named_slots;
 /* Recovery obtains the working pair under the same fence. stage_working
  * verifies its entire record and decoded project into disposable buffers,
  * returns its committed canonical name, and never touches live state.
  * prepare_recovery returns 2 only after proving both working sectors blank
  * under its fence and releasing it; 3 denotes invalid or foreign data. */
 int (*prepare_recovery)(void *,fm1_fixed_bridge_transaction *);
 int (*stage_working)(void *,pops_certify_fn,void *,char name[PS_NAME_BYTES]);
 /* Publish the verified slot under the still-held fence; exact 1 on success. */
 int (*commit_slot)(void *,int);
} fm1_fixed_bridge_ops;
/* Zero-initialize once; a fault is sticky until an independently verified
 * engine/authority reset constructs a fresh bridge. */
typedef struct { const fm1_fixed_bridge_ops *ops; unsigned busy,fault; } fm1_fixed_bridge;
int fm1_fixed_bridge_bind(fm1_fixed_bridge *,const fm1_fixed_bridge_ops *);
/* On success both named and working records hold the same captured project.
 * If named commits but working fails, returns FM1_PROJECT_WORKING_FAILED.
 * Optional commit_slot publishes selection under the fence after save or
 * adoption; a failed commit latches recovery-required.
 * A torn initial record or journal/fence fault locks subsequent operations. */
int fm1_fixed_bridge_save(fm1_fixed_bridge *,uint32_t,const char *);
int fm1_fixed_bridge_load(fm1_fixed_bridge *,uint32_t);
/* Explicit recovery only; never called automatically by bind. Changes live
 * state only after complete validation and name/policy checks. */
int fm1_fixed_bridge_recover(fm1_fixed_bridge *);
/* Parse a verified working-record name: -1 is unassigned; -2 is invalid. */
int fm1_fixed_bridge_working_slot(const char name[PS_NAME_BYTES],unsigned named_slots);
#endif
