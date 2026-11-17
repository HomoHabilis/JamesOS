#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "project_delete_gate.h"
static pdg_gate gate;static int calls,reentered;
static int perform(void *ctx,const pdg_review *r){int out;(void)ctx;calls++;reentered=pdg_commit(&gate,r,1u,perform,0,&out);return -7;}
int main(void){pdg_review r={0},changed;int out;r.nonce[0]=1;r.digest[0]=2;r.slot=2;memcpy(r.name,"SUNDAY",7);
 assert(pdg_prepare(&gate,&r,1));changed=r;changed.slot=3;assert(!pdg_commit(&gate,&changed,2,perform,0,&out));changed=r;changed.digest[0]^=1;assert(!pdg_commit(&gate,&changed,2,perform,0,&out));
 assert(pdg_commit(&gate,&r,2,perform,0,&out)&&out==-7&&calls==1&&!reentered);assert(pdg_commit(&gate,&r,3,perform,0,&out)&&out==-7&&calls==1);assert(!pdg_prepare(&gate,&r,4));
 r.nonce[0]=2;assert(pdg_prepare(&gate,&r,UINT32_MAX-10u));assert(!pdg_commit(&gate,&r,PDG_TIMEOUT_MS-10u,perform,0,&out));assert(calls==1);assert(!pdg_cancel(&gate,r.nonce));assert(!pdg_commit(&gate,&r,1,perform,0,&out));assert(!pdg_prepare(&gate,&r,1));
 r.nonce[0]=4;assert(pdg_prepare(&gate,&r,1));assert(pdg_cancel(&gate,r.nonce));assert(!pdg_commit(&gate,&r,2,perform,0,&out));assert(!pdg_prepare(&gate,&r,2));assert(calls==1);
 r.nonce[0]=3;r.kind=PDG_UNAVAILABLE;assert(!pdg_prepare(&gate,&r,1));memset(r.name,0,sizeof r.name);assert(pdg_prepare(&gate,&r,1));changed=r;changed.kind=PDG_NAMED;assert(!pdg_commit(&gate,&changed,2,perform,0,&out));assert(pdg_commit(&gate,&r,2,perform,0,&out)&&calls==2);puts("PASS: reviewed delete gate pins identity, expires, cancels and never replays an erase");return 0;}
