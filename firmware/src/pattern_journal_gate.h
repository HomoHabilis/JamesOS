/* Host-only cursor gate prototype. Not connected to target flash. */
#ifndef FM1_PATTERN_JOURNAL_GATE_H
#define FM1_PATTERN_JOURNAL_GATE_H
#include "pattern_namespace.h"
typedef struct {
 const pns_namespace *ns; pns_namespace pinned; pfs_store pinned_store; void *ctx; uint8_t *scratch;
 int (*exclusive)(void *);
 int (*write_authorized)(void *,uint32_t,const void *,uint32_t);
 uint8_t entry[48],before[32],body[32],after[32]; uint32_t cursor; unsigned phase;
} pns_journal_gate;
/* Caller is the trusted namespace transaction and provides the exact event
 * it intends to commit. CLAIM, REVOKE and rotation INTENT are supported. The caller must keep
 * all flash writers excluded and this gate private until disarm. */
int pns_journal_gate_arm(pns_journal_gate *,const pns_namespace *,void *ctx,
 int (*exclusive)(void *),int (*write_authorized)(void *,uint32_t,const void *,uint32_t),
 const uint8_t event[48],void *scratch);
int pns_journal_gate_program(void *,uint32_t,const void *,uint32_t);
void pns_journal_gate_disarm(pns_journal_gate *);
#endif
