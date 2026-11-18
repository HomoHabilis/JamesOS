#include "project_delete_protocol.h"
#include <string.h>
#include "pattern_overlap.h"
static void put32(uint8_t *p,int v){uint32_t n=(uint32_t)v;p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static int nonce_ok(const uint8_t *p){uint8_t n=0;for(unsigned i=0;i<PDG_NONCE;i++)n|=p[i];return n!=0;}
static void unpack(pdg_review *r,const uint8_t *b){memcpy(r->nonce,b,16);r->slot=b[16];r->kind=b[17];memcpy(r->name,b+18,PDG_NAME);memcpy(r->digest,b+31,PDG_DIGEST);}
static void answer_review(uint8_t *out,const pdg_review *r){memcpy(out,r->nonce,16);put32(out+16,0);memcpy(out+20,r->name,PDG_NAME);memcpy(out+33,r->digest,PDG_DIGEST);}
int pdp_dispatch(pdg_gate *g,const pdp_ops *ops,uint8_t cmd,const uint8_t *body,unsigned len,uint32_t now,uint8_t reply[PDP_MAX_REPLY],unsigned *reply_len){
 pdg_review r;int result;
 if(!g||!ops||!ops->review||!ops->perform||!body||!reply||!reply_len||len<16||!nonce_ok(body)||
    fm1_mem_overlap(body,len,reply,PDP_MAX_REPLY)||fm1_mem_overlap(body,len,reply_len,sizeof *reply_len)||
    fm1_mem_overlap(reply,PDP_MAX_REPLY,reply_len,sizeof *reply_len)||fm1_mem_overlap(g,sizeof *g,reply,PDP_MAX_REPLY)||
    fm1_mem_overlap(g,sizeof *g,reply_len,sizeof *reply_len)||fm1_mem_overlap(ops,sizeof *ops,reply,PDP_MAX_REPLY)||
    fm1_mem_overlap(ops,sizeof *ops,reply_len,sizeof *reply_len))return 0;
 *reply_len=0;
 if(cmd==PDP_REVIEW){
  if(len!=18||body[16]>=12||body[17]>PDG_RAW)return 0;
  if(g->state==PDG_RUNNING||((g->state==PDG_DONE||g->state==PDG_CANCELED)&&!memcmp(g->review.nonce,body,16))){
   memcpy(reply,body,16);put32(reply+16,-2);*reply_len=20;return 1;
  }
  if(g->state==PDG_ARMED&&(uint32_t)(now-g->reviewed_at)<PDG_TIMEOUT_MS){
   if(!memcmp(g->review.nonce,body,16)&&g->review.slot==body[16]&&g->review.kind==body[17]){answer_review(reply,&g->review);*reply_len=65;return 1;}
   memcpy(reply,body,16);put32(reply+16,-2);*reply_len=20;return 1;
  }
  memset(&r,0,sizeof r);memcpy(r.nonce,body,16);r.slot=body[16];r.kind=body[17];
  result=ops->review(ops->ctx,r.slot,r.kind,r.name,r.digest);
  if(result==0&&!pdg_prepare(g,&r,now))result=-1;
  if(result==0){answer_review(reply,&r);*reply_len=65;}
  else {memcpy(reply,body,16);put32(reply+16,result);*reply_len=20;}
  return 1;
 }
 if(cmd==PDP_COMMIT){
  if(len!=PDP_WIRE_REVIEW)return 0;unpack(&r,body);
  if(!pdg_commit(g,&r,now,ops->perform,ops->ctx,&result))result=-2;
  memcpy(reply,body,16);put32(reply+16,result);*reply_len=20;return 1;
 }
 if(cmd==PDP_CANCEL){
  if(len!=16)return 0;result=pdg_cancel(g,body)?0:-1;memcpy(reply,body,16);put32(reply+16,result);*reply_len=20;return 1;
 }
 return 0;
}
