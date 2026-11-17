#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
#include "pattern_sha256.h"
#include "project_delete_protocol.h"
static br_session_t before_import;
static fm1_fixed_runtime_port port;
static int import_may_erase(void *ctx,uint32_t off){(void)ctx;return pps_owner_may_erase(&runtime.owner[0],off)||pps_owner_may_erase(&runtime.owner[1],off);}
static void setup(void){memset(&port,0,sizeof port);port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;memset(&realpark,0,sizeof realpark);init_runtime_fixture();parkops.may_erase=import_may_erase;assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);assert(fm1_fixed_bridge_save(&runtime.bridge,0,"OLD")==PFS_OK);}
static uint8_t reviewed_payload[PPS_MAX_PAYLOAD];
static uint32_t reviewed_length;
static int reviews,performs;
static int review_delete(void *ctx,uint8_t slot,uint8_t kind,uint8_t name[PDG_NAME],uint8_t digest[PDG_DIGEST]){
 fm1_pattern_sha256 sha;int rc;(void)ctx;reviews++;
 if(kind==PDG_UNAVAILABLE)return fm1_fixed_runtime_inspect_unreadable_experiment(&runtime,slot,digest);
 rc=fm1_fixed_runtime_export(&runtime,slot,reviewed_payload,sizeof reviewed_payload,&reviewed_length,(char *)name);
 if(rc!=PFS_OK)return rc;
 assert(!fm1_pattern_sha256_init(&sha)&&!fm1_pattern_sha256_update(&sha,reviewed_payload,reviewed_length)&&!fm1_pattern_sha256_final(&sha,digest));return PFS_OK;
}
static int perform_delete(void *ctx,const pdg_review *r){(void)ctx;performs++;return r->kind==PDG_UNAVAILABLE?fm1_fixed_runtime_clear_unreadable_experiment(&runtime,r->slot,r->digest):fm1_fixed_runtime_delete_experiment(&runtime,r->slot,(const char *)r->name,r->digest,r->kind==PDG_RETRY);}
static void start_review(pdg_gate *g,pdp_ops *ops,uint8_t req[63],uint8_t reply[65],uint8_t nonce,uint8_t slot,uint8_t kind){unsigned n;memset(req,0,63);req[0]=nonce;req[16]=slot;req[17]=kind;assert(pdp_dispatch(g,ops,PDP_REVIEW,req,18,1,reply,&n)&&n==65);memcpy(req+18,reply+20,13);memcpy(req+31,reply+33,32);}
#ifndef FM1_DELETE_INTEROP_SERVER
int main(void){pdg_gate gate={0};pdp_ops protocol={0,review_delete,perform_delete};uint8_t req[63],reply[65],copy[PPS_MAX_PAYLOAD];unsigned n,erases;uint32_t length=0;char name[PS_NAME_BYTES];
 setup();assert(br_capture(&before_import));memcpy(pair_before,pair_flash,sizeof pair_flash);
 start_review(&gate,&protocol,req,reply,1,0,PDG_NAMED);assert(reviews==1&&!performs);erases=raw_erases;
 assert(pdp_dispatch(&gate,&protocol,PDP_COMMIT,req,63,2,reply,&n)&&n==20&&performs==1&&raw_erases>erases);
 erases=raw_erases;assert(pdp_dispatch(&gate,&protocol,PDP_COMMIT,req,63,3,reply,&n)&&performs==1&&raw_erases==erases);
 assert(fm1_fixed_runtime_export(&runtime,0,copy,sizeof copy,&length,name)==PFS_EMPTY);
 assert(!memcmp(pair_before+2u*PS_SECTOR,pair_flash+2u*PS_SECTOR,sizeof pair_flash-2u*PS_SECTOR));
 assert(br_capture(&target_factory)&&!memcmp(&before_import,&target_factory,sizeof before_import));
 /* A review cannot authorize a different project saved in the meantime. */
 setup();memset(&gate,0,sizeof gate);reviews=performs=0;start_review(&gate,&protocol,req,reply,2,0,PDG_NAMED);
 assert(fm1_fixed_runtime_import(&runtime,0,"NEW",reviewed_payload,reviewed_length)==PFS_OK);memcpy(pair_before,pair_flash,sizeof pair_flash);erases=raw_erases;
 assert(pdp_dispatch(&gate,&protocol,PDP_COMMIT,req,63,2,reply,&n)&&performs==1);assert(raw_erases==erases&&!memcmp(pair_before,pair_flash,sizeof pair_flash));
 /* Unavailable recovery needs its own review and exact pair fingerprint. */
 setup();memset(&gate,0,sizeof gate);reviews=performs=0;pair_flash[0]|=1u;start_review(&gate,&protocol,req,reply,3,0,PDG_UNAVAILABLE);memcpy(pair_before,pair_flash,sizeof pair_flash);erases=raw_erases;
 pair_flash[10]^=1u;assert(pdp_dispatch(&gate,&protocol,PDP_COMMIT,req,63,2,reply,&n)&&performs==1);assert(raw_erases==erases);pair_flash[10]=pair_before[10];
 memset(&gate,0,sizeof gate);start_review(&gate,&protocol,req,reply,4,0,PDG_UNAVAILABLE);
 assert(pdp_dispatch(&gate,&protocol,PDP_COMMIT,req,63,2,reply,&n)&&performs==2);assert(fm1_fixed_runtime_export(&runtime,0,copy,sizeof copy,&length,name)==PFS_EMPTY);
 puts("PASS: reviewed protocol and project runtime preserve other slots and reject stale reviews");return 0;}
#endif
