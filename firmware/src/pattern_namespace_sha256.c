/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_namespace.h"
#include "pattern_sha256.h"
int pns_sha256_digest(void *context,const void *data,size_t length,uint8_t out[PNS_HASH_BYTES])
{
    fm1_pattern_sha256 state; (void)context;
    if(length>UINT32_MAX || fm1_pattern_sha256_init(&state) ||
       fm1_pattern_sha256_update(&state,data,(uint32_t)length) || fm1_pattern_sha256_final(&state,out)) return -1;
    return 0;
}
