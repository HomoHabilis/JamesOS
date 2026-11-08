/* Host-only repair contract. No calls to platform hardware exist here. */
#include "guarded_pair_repair.h"
#include <string.h>
#define PAGE 256u
static int overlap(const void *a,size_t n,const void *b,size_t k){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return n&&k&&(x<=y?y-x<n:x-y<k);}
static int safe(const jos_repair_model *m,uint32_t slot,unsigned bad){return m->fenced(m->ctx)==1 && m->proof(m->ctx,slot,bad)==1;}
static int fault(const jos_repair_model *m){m->fault(m->ctx);return JOS_REPAIR_FAULT;}
int jos_repair_damaged_copy(const jos_repair_model *m,uint32_t slot,unsigned bad,uint8_t scratch[256]){
 uint8_t ref[PAGE];uint32_t begin,off,i;unsigned good;int a,b,nonblank=0;
 if(!m||!scratch||overlap(m,sizeof *m,scratch,PAGE)||!m->fenced||!m->proof||!m->backup_read||!m->copy_state||!m->fault||!m->store||!m->store->read||!m->store->erase||overlap(m->store,sizeof *m->store,scratch,PAGE)||m->store->base%PS_SECTOR||slot>=PS_TOTAL_SLOTS||bad>1||m->store->base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR)return JOS_REPAIR_DENIED;
 if(!safe(m,slot,bad))return JOS_REPAIR_DENIED;
 good=!bad;begin=m->store->base;off=begin+(slot*2u+good)*PS_SECTOR;
 a=m->copy_state(m->ctx,slot,good);b=m->copy_state(m->ctx,slot,bad);
 if(!safe(m,slot,bad))return fault(m);
 if(a!=1||b!=0)return JOS_REPAIR_DENIED;
 /* Verify the independently archived whole partition, under the fence. */
 for(i=0;i<PS_TOTAL_SLOTS*2u*PS_SECTOR;i+=PAGE){
  if(!safe(m,slot,bad))return fault(m);
  if(m->backup_read(m->ctx,i,ref,PAGE)||m->store->read(m->store->ctx,begin+i,scratch,PAGE))return fault(m);
  if(memcmp(ref,scratch,PAGE))return JOS_REPAIR_DENIED;
 }
 /* The external verified archive supplies the exact survivor bytes. It must
  * stay pinned and immutable until the operation is finished. */
 off=begin+(slot*2u+bad)*PS_SECTOR;
 for(i=0;i<PS_SECTOR;i+=PAGE){if(m->store->read(m->store->ctx,off+i,scratch,PAGE))return fault(m);for(unsigned j=0;j<PAGE;j++)nonblank|=scratch[j]!=255;}
 if(!safe(m,slot,bad))return fault(m);
 if(!nonblank || m->copy_state(m->ctx,slot,good)!=1 || m->copy_state(m->ctx,slot,bad)!=0)return JOS_REPAIR_DENIED;
 if(!safe(m,slot,bad))return fault(m);
 /* The sole destructive operation is never retried after failure. */
 if(m->store->erase(m->store->ctx,off))return fault(m);
 if(!safe(m,slot,bad))return fault(m);
 for(i=0;i<PS_SECTOR;i+=PAGE){
  if(m->store->read(m->store->ctx,off+i,scratch,PAGE))return fault(m);
  for(unsigned j=0;j<PAGE;j++)if(scratch[j]!=255)return fault(m);
  if(m->backup_read(m->ctx,(slot*2u+good)*PS_SECTOR+i,ref,PAGE)||m->store->read(m->store->ctx,begin+(slot*2u+good)*PS_SECTOR+i,scratch,PAGE)||memcmp(scratch,ref,PAGE))return fault(m);
 }
 for(i=0;i<PS_TOTAL_SLOTS*2u*PS_SECTOR;i+=PAGE){
  if(begin+i>=off && begin+i<off+PS_SECTOR)continue;
  if(!safe(m,slot,bad)||m->backup_read(m->ctx,i,ref,PAGE)||m->store->read(m->store->ctx,begin+i,scratch,PAGE)||memcmp(ref,scratch,PAGE))return fault(m);
 }
 if(m->copy_state(m->ctx,slot,good)!=1||!safe(m,slot,bad))return fault(m);
 return JOS_REPAIR_OK;
}
