/* HOST-ONLY experiment; no FM-1 backend. Must not be used for target I/O. */
#ifndef JOS_GUARDED_PAIR_REPAIR_H
#define JOS_GUARDED_PAIR_REPAIR_H
#if !defined(JOS_HOST_RECOVERY_MODEL) || defined(FM1_DEVICE)
#error "Recovery model has no qualified hardware backend"
#endif
#include <stdint.h>
#include "pattern_pair_stream.h"
typedef struct {
 void *ctx;
 /* Exactly 1 while exclusive, drained, parked and contained. */
 int (*fenced)(void *);
 /* Exactly 1 only for independently recorded consent, device identity,
  * exact slot/sector ownership and an independently verified full backup. */
 int (*proof)(void *,uint32_t slot,unsigned bad_copy);
 int (*backup_read)(void *,uint32_t off,void *dst,uint32_t len);
 /* Independently validate a whole copy: 1 valid, 0 invalid, -1 read error. */
 int (*copy_state)(void *,uint32_t slot,unsigned copy);
 /* Latch quarantine; caller must NOT resume or automatically retry. */
 void (*fault)(void *);
 const pps_store *store;
} jos_repair_model;
enum { JOS_REPAIR_OK=0,JOS_REPAIR_DENIED=-1,JOS_REPAIR_FAULT=-2 };
int jos_repair_damaged_copy(const jos_repair_model *,uint32_t slot,unsigned bad_copy,uint8_t scratch[256]);
#endif
