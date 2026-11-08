/* Link-only experiment: transport-independent provisioning capability. */
#ifndef FM1_PATTERN_PROVISION_PROTOCOL_H
#define FM1_PATTERN_PROVISION_PROTOCOL_H
#if !defined(FM1_PATTERN_PROVISION_EXPERIMENT) || !FM1_PATTERN_PROVISION_EXPERIMENT
#error "Pattern provisioning protocol is explicitly gated"
#endif
#include "pattern_namespace.h"
typedef struct { uint8_t domain[16],generation[32]; uint32_t base,root,peer; } fm1_provision_identity;
typedef struct { void *context; int (*begin)(void *,int write,pns_namespace **,void **scratch,uint32_t *token); int (*end)(void *,uint32_t token); int (*permit_provision)(void *,const pns_namespace *,void *scratch); int (*faulted)(void *); uint32_t (*now_ms)(void *); int (*safe_state)(void *); uint32_t (*context_epoch)(void *); } fm1_provision_ops;
typedef struct { const fm1_provision_identity *identity; const fm1_provision_ops *ops; const uint8_t *secret; int fault,write_armed,challenge_live; uint32_t challenge_at,challenge_counter,challenge_epoch; uint8_t challenge[32]; int (*arm_valid)(void *); void *arm_context; } fm1_provision_protocol;
enum { FM1_PROVISION_OK=0, FM1_PROVISION_DENIED=-1, FM1_PROVISION_IO=-2, FM1_PROVISION_FAULT=-3 };
/* Identity is supplied by a separately generated immutable build anchor. No
 * default initializer or transport binding exists. A successful begin owns a
 * non-reentrant, token-scoped worker/flash fence until end. Missing callbacks,
 * unequal anchors, unknown/untrusted roots and ambiguous sectors fail closed.
 * Callbacks must not mutate identity, namespace configuration or store, and
 * all competing writers must remain excluded through end. End must validate
 * its token and contain failures. All outputs are discarded on error; scratch
 * must not overlap any protocol/configuration/request/output. No API here
 * erases data. Returned hashes can reveal content equality. */
int fm1_provision_identity_read(const fm1_provision_protocol *,fm1_provision_identity *);
int fm1_provision_sector(fm1_provision_protocol *,uint32_t sector,int *blank,uint8_t hash[32]);
/* A separate non-transport authority must explicitly arm each write. The
 * arm callback must certify an independent consent lease including expiry,
 * transport generation and stopped/quiet state, and be rechecked on use.
 * Call disarm on disconnect/playback/context change. Neither D0 data nor
 * MIDI alone authorizes a write. Every D2 attempt consumes the arm. No
 * default lease authority or transport disconnect binding is implemented. */
int fm1_provision_arm_once(fm1_provision_protocol *,int (*authorize)(void *),void *);
void fm1_provision_disarm(fm1_provision_protocol *);
/* D3/D4 HMAC is an additional per-build gate, not device authentication or
 * general replay protection: no verified device entropy/durable counter is
 * assumed, and firmware secrets can be extracted. The target must implement
 * safe_state/expiry/disconnect cancellation and retain blank-only checks. */
int fm1_provision_challenge(fm1_provision_protocol *,const uint8_t nonce[16],uint8_t challenge[32]);
int fm1_provision_hmac_arm(fm1_provision_protocol *,const uint8_t response[32]);
int fm1_provision_commit(fm1_provision_protocol *,const fm1_provision_identity *request);
/* Diagnostic framing: D0 identity (empty request, 60-byte response), D1
 * sector hash (LE32 index, 33-byte response: blank flag then SHA-256), D2
 * explicit root commit (60-byte identity request, empty success response).
 * D3 requests a 32-byte challenge with a 16-byte host nonce; D4 supplies
 * a 32-byte HMAC response to arm one D2 attempt. The caller owns transport
 * framing, rate limits, disconnect cancellation and the safe-state lease. */
int fm1_provision_dispatch(fm1_provision_protocol *,uint8_t command,const uint8_t *body,size_t length,uint8_t *reply,size_t capacity,size_t *reply_length);
/* Runtime binding is explicit, one-shot and absent by default. The identity,
 * operations and their referenced storage must remain immutable/alive. */
/* Available only in an explicit private pinned build. Secret is never
 * included in a diagnostic reply; firmware extraction can still recover it. */
const fm1_provision_identity *fm1_provision_embedded_identity(void);
const uint8_t *fm1_provision_embedded_secret(void);
int fm1_provision_runtime_bind(fm1_provision_protocol *);
int fm1_provision_runtime_dispatch(uint8_t,const uint8_t *,size_t,uint8_t *,size_t,size_t *);
#endif
