#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "project_delete_protocol.h"
static int reads,writes;
static int review(void *ctx,uint8_t slot,uint8_t kind,uint8_t name[PDG_NAME],uint8_t digest[PDG_DIGEST]){(void)ctx;reads++;if(slot==9)return -13;if(kind!=PDG_UNAVAILABLE)memcpy(name,"HOUSE",6);memset(digest,slot+1,PDG_DIGEST);return 0;}
static int perform(void *ctx,const pdg_review *r){(void)ctx;assert(r->slot==2);writes++;return 0;}
int main(void){pdg_gate g={0};pdp_ops ops={0,review,perform};uint8_t req[PDP_WIRE_REVIEW]={0},reply[PDP_MAX_REPLY],original[PDP_WIRE_REVIEW];unsigned n;req[0]=1;req[16]=2;
 assert(!pdp_dispatch(&g,&ops,PDP_REVIEW,req,18,10,req,&n));
 assert(pdp_dispatch(&g,&ops,PDP_REVIEW,req,18,10,reply,&n)&&n==65&&reads==1);memcpy(req+18,reply+20,13);memcpy(req+31,reply+33,32);memcpy(original,req,sizeof req);
 assert(pdp_dispatch(&g,&ops,PDP_REVIEW,req,18,11,reply,&n)&&n==65&&reads==1);
 req[31]^=1;assert(pdp_dispatch(&g,&ops,PDP_COMMIT,req,sizeof req,12,reply,&n)&&n==20&&reply[16]==254&&!writes);memcpy(req,original,sizeof req);
 assert(pdp_dispatch(&g,&ops,PDP_COMMIT,req,sizeof req,12,reply,&n)&&n==20&&writes==1);
 assert(pdp_dispatch(&g,&ops,PDP_COMMIT,req,sizeof req,13,reply,&n)&&writes==1);
 assert(pdp_dispatch(&g,&ops,PDP_REVIEW,req,18,14,reply,&n)&&n==20&&reads==1);
 req[0]=2;req[17]=PDG_UNAVAILABLE;assert(pdp_dispatch(&g,&ops,PDP_REVIEW,req,18,20,reply,&n)&&n==65&&reads==2);
 assert(pdp_dispatch(&g,&ops,PDP_CANCEL,req,16,21,reply,&n)&&n==20);assert(pdp_dispatch(&g,&ops,PDP_COMMIT,req,sizeof req,22,reply,&n)&&reply[16]==254);assert(writes==1);
 req[0]=3;req[16]=9;req[17]=0;assert(pdp_dispatch(&g,&ops,PDP_REVIEW,req,18,30,reply,&n)&&n==20&&reads==3);assert(writes==1);
 memset(req,0,sizeof req);assert(!pdp_dispatch(&g,&ops,PDP_REVIEW,req,18,40,reply,&n));assert(!pdp_dispatch(&g,&ops,PDP_COMMIT,req,2,40,reply,&n));
 puts("PASS: delete protocol reviews before commit and makes retries idempotent");return 0;}
