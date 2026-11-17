/* Experimental reviewed project deletion protocol; not connected to MIDI. */
#ifndef PROJECT_DELETE_PROTOCOL_H
#define PROJECT_DELETE_PROTOCOL_H
#include "project_delete_gate.h"
#define PDP_REVIEW 0xe1u
#define PDP_COMMIT 0xe2u
#define PDP_CANCEL 0xe3u
#define PDP_WIRE_REVIEW 63u
#define PDP_MAX_REPLY 65u
typedef struct {
 void *ctx;
 /* review reads the device: named modes return the validated name and payload
  * digest; unavailable mode returns a digest of both sectors and zero name. */
 int (*review)(void *,uint8_t slot,uint8_t kind,uint8_t name[PDG_NAME],uint8_t digest[PDG_DIGEST]);
 int (*perform)(void *,const pdg_review *);
} pdp_ops;
/* Return 1 when a bounded reply was written; 0 means malformed/no response.
 * Results use 0 for success; negative values report refusal or I/O failure. */
int pdp_dispatch(pdg_gate *,const pdp_ops *,uint8_t cmd,const uint8_t *body,unsigned length,uint32_t now,uint8_t reply[PDP_MAX_REPLY],unsigned *reply_length);
#endif
