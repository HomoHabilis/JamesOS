#include "pattern_pair_target.h"
#if defined(FM1_DEVICE) && FM1_DEVICE && defined(FM1_PAIR_STREAM_TARGET_QUALIFIED) && FM1_PAIR_STREAM_TARGET_QUALIFIED
#error "Streaming target storage has no qualified ownership backend"
#endif
int pps_target_begin(pps_target *t,br_pattern_park_io *park,uint32_t token,uint32_t slot,
                     const uint8_t role[16],pps_validate valid,void *valid_ctx,
                     pps_park_proof fence,pps_park_reserved_proof reserved,void *ctx)
{
    if(!t || t->active || t->retired) return 0;
#if defined(FM1_DEVICE) && FM1_DEVICE
    (void)park;(void)token;(void)slot;(void)role;(void)valid;(void)valid_ctx;
    (void)fence;(void)reserved;(void)ctx;return 0;
#else
    if(!pps_park_bind(&t->adapter,&t->store,park,token,slot,role,valid,valid_ctx,fence,reserved,ctx)) return 0;
    t->active=1;return 1;
#endif
}
pps_store *pps_target_store(pps_target *t)
{
#if defined(FM1_DEVICE) && FM1_DEVICE
    (void)t; return 0;
#else
    return t && t->active && !t->retired ? &t->store : 0;
#endif
}
void pps_target_end(pps_target *t)
{
    if(!t || !t->active) return;
    /* A copied store handle still points here and must fail after revocation. */
    t->adapter.token=0;t->adapter.park=0;t->active=0;t->retired=1;
}
