/* Experimental reviewed-delete transaction gate; no transport entry point. */
#ifndef PROJECT_DELETE_GATE_H
#define PROJECT_DELETE_GATE_H
#include <stdint.h>
#define PDG_NONCE 16u
#define PDG_DIGEST 32u
#define PDG_NAME 13u
#define PDG_TIMEOUT_MS 120000u
enum { PDG_NAMED=0, PDG_RETRY=1, PDG_UNAVAILABLE=2, PDG_RAW=3 };
enum { PDG_IDLE=0, PDG_ARMED=1, PDG_RUNNING=2, PDG_DONE=3, PDG_CANCELED=4 };
typedef struct { uint8_t nonce[PDG_NONCE],digest[PDG_DIGEST],name[PDG_NAME],slot,kind; } pdg_review;
/* Zero-initialize the gate before first use. */
typedef struct { pdg_review review;uint32_t reviewed_at;unsigned state;int result; } pdg_gate;
/* A prepare is only valid after a device read has established this identity.
 * Retry and unavailable mean separate, explicitly described confirmations. */
int pdg_prepare(pdg_gate *,const pdg_review *,uint32_t now);
int pdg_cancel(pdg_gate *,const uint8_t nonce[PDG_NONCE]);
/* Exactly one callback invocation per armed review; repeated commits return
 * the latched result, never erase again. Return 1 when result is available. */
int pdg_commit(pdg_gate *,const pdg_review *,uint32_t now,int (*perform)(void *,const pdg_review *),void *ctx,int *result);
#endif
