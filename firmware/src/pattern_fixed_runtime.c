#include "pattern_fixed_runtime.h"
#include "pattern_overlap.h"
#include "pattern_codec_source.h"
#include "pattern_storage_codec.h"
#include "pattern_sha256.h"
#include <string.h>
#if (defined(FM1_PAIR_STREAM_HOST_TEST) && FM1_PAIR_STREAM_HOST_TEST && !defined(FM1_DEVICE)) || (defined(FM1_PATTERN_DELETE_EXPERIMENT) && FM1_PATTERN_DELETE_EXPERIMENT && defined(FM1_DEVICE))
#define FM1_DELETE_RAW_MODE(mode) ((mode)==5)
#else
#define FM1_DELETE_RAW_MODE(mode) 0
#endif
static int same_port(const fm1_fixed_runtime_port *a,const fm1_fixed_runtime_port *b){return a&&b&&a->ctx==b->ctx&&a->begin==b->begin&&a->set_mode==b->set_mode&&a->end==b->end&&a->fence==b->fence&&a->faulted==b->faulted&&a->pair_state==b->pair_state&&a->certificate==b->certificate&&a->activate==b->activate&&a->commit_slot==b->commit_slot&&a->named_slots==b->named_slots&&a->fixed_partition_auto==b->fixed_partition_auto&&!memcmp(a->role,b->role,sizeof a->role);}
static int fence(void *v){fm1_fixed_runtime *r=v;return r&&r->active&&same_port(r->port,&r->pinned)&&r->pinned.fence(r->pinned.ctx,r->token)==1&&r->pinned.faulted(r->pinned.ctx)==0;}
static int faulted(void *v){fm1_fixed_runtime *r=v;return !r||!r->port||r->pinned.faulted(r->pinned.ctx)!=0||!fence(r);}
static int safe(void *v){fm1_fixed_runtime *r=v;fm1_fixed_stage *s;fm1_project_source *p;if(!fence(r))return 0;s=&r->stage;p=&r->adapter->source;if(r->mode==0){if(!p->session||!p->bank||!p->live)return 0;if(!fm1_fixed_stage_source_separate(s,p,sizeof *p)||!fm1_fixed_stage_source_separate(s,p->session,FM1_PROJECT_SESSION)||!fm1_fixed_stage_source_separate(s,p->bank,sizeof *p->bank)||!fm1_fixed_stage_source_separate(s,p->bank->arena,p->bank->capacity)||!fm1_fixed_stage_source_separate(s,p->live,sizeof *p->live))return 0;}else{if(!r->old_bank.arena||!fm1_fixed_stage_source_separate(s,&r->old_bank,sizeof r->old_bank)||!fm1_fixed_stage_source_separate(s,r->old_bank.arena,r->old_bank.capacity)||!fm1_fixed_stage_source_separate(s,&r->old_state,sizeof r->old_state)||!fm1_fixed_stage_source_separate(s,r->adapter->previous_live,sizeof *r->adapter->previous_live))return 0;}return 1;}
static int cert(void *v,uint32_t address){fm1_fixed_runtime_cert *c=v;fm1_fixed_runtime *r;if(!c||!c->active||!(r=c->runtime)||!fence(r)||c->token!=r->token||c->slot>=PS_TOTAL_SLOTS)return 0;if(r->pinned.fixed_partition_auto){uint32_t first=r->park->base+c->slot*2u*PS_SECTOR;return address==first||address==first+PS_SECTOR;}return r->pinned.certificate(r->pinned.ctx,c->token,c->slot,c->initializing,address)==1;}
static int scratch_safe(fm1_fixed_runtime *r){fm1_project_adapter *a=r->adapter;const void *p=a->sector;if(fm1_mem_overlap(p,4096,r,sizeof *r)||fm1_mem_overlap(p,4096,r->port,sizeof *r->port)||fm1_mem_overlap(p,4096,a,sizeof *a)||fm1_mem_overlap(p,4096,r->ops,sizeof *r->ops)||fm1_mem_overlap(p,4096,r->park,sizeof *r->park)||!fm1_fixed_stage_source_separate(&r->stage,p,4096))return 0;if(r->mode==0)return fm1_fixed_project_source_separate(&a->source,p,4096);return !fm1_mem_overlap(p,4096,r->old_bank.arena,r->old_bank.capacity)&&!fm1_mem_overlap(p,4096,a->previous_live,sizeof *a->previous_live);}
static int capture(fm1_fixed_runtime *r){fm1_project_adapter *a=r->adapter;const fm1_project_adapter_ops *o=r->ops;int rc;if(!fence(r)||!o->quiescent(o->context)||!o->capture(a->session,FM1_PROJECT_SESSION)||!o->export_bank(o->context,&a->bank,a->live,&a->song))return 0;rc=fm1_project_source_init(&a->source,a->session,&a->bank,a->live,&a->song,o->validate,o->context,a->work);if(rc!=FM1_PROJECT_OK)return 0;r->mode=0;r->stage.session=a->previous;return safe(r);}
static int end_port(fm1_fixed_runtime *r){int ok;if(!r->active)return 0;r->cert[0].active=r->cert[1].active=0;pps_owner_end(&r->owner[0]);pps_owner_end(&r->owner[1]);ok=r->pinned.end(r->pinned.ctx,r->token);r->active=0;return ok==1;}
static int readonly_program(void *v,uint32_t a,const void *p,uint32_t n){(void)v;(void)a;(void)p;(void)n;return -1;}
static int readonly_erase(void *v,uint32_t a){(void)v;(void)a;return -1;}
/* Reads can select one validated survivor without changing an unknown sibling.
 * Explicit replacement reclaims incompatible copies under the parked owner. */
static int auto_state(fm1_fixed_runtime *r,uint32_t slot,unsigned *initializing,int read_only){pps_store s;pps_reader reader;int rc;if(!fence(r)||!r->park||slot>=PS_TOTAL_SLOTS)return -1;memset(&s,0,sizeof s);s.ctx=r->park;s.read=br_pattern_park_read;s.program=readonly_program;s.erase=readonly_erase;s.base=r->park->base;memcpy(s.role,r->pinned.role,16);s.validate=fm1_pair_project_validate;s.validate_ctx=&r->stage;rc=pps_reader_open(&s,slot,&reader,r->adapter->sector+3584u);if(rc==PS_EMPTY){*initializing=1;return 1;}if(rc!=PS_OK)return faulted(r)?-1:rc==PS_CORRUPT?-2:-3;if(!read_only&&pps_erase_allowed(&s,slot,!reader.copy,r->adapter->sector+3584u)!=1)return faulted(r)?-1:-2;*initializing=0;return 1;}
static int begin(fm1_fixed_runtime *r,uint32_t slot,int mode,int working_write,fm1_fixed_bridge_transaction *t){fm1_project_adapter *a=0;const fm1_project_adapter_ops *o=0;br_pattern_park_io *park=0;uint32_t token=0;size_t cap=0;int x=0;unsigned init0=0,init1=0;if(!r||!same_port(r->port,&r->pinned)||r->active||slot>=PS_TOTAL_SLOTS||r->next_lease>UINT32_MAX-2u)return -1;x=r->pinned.begin(r->pinned.ctx,(mode==2||mode==3||mode==4||FM1_DELETE_RAW_MODE(mode))?1:mode,&a,&o,&token,&park,&cap);if(x!=1)return x<0?-1:0;r->active=1;r->token=token;r->adapter=a;r->ops=o;r->park=park;r->opened=0;r->mode=mode==0?0u:1u;memset(r->owner,0,sizeof r->owner);memset(r->lease,0,sizeof r->lease);memset(r->cert,0,sizeof r->cert);if(!token||!a||!o||!park||cap<4096u||!a->sector||!a->work||!a->work->pattern||!a->work->session||!a->session||!a->previous||!a->previous_live||!a->arena||!a->arena_capacity||!o->quiescent||!o->capture||!o->validate||!o->restore||!o->export_bank||!o->can_adopt||!o->adopt||!o->restore_pattern||!fence(r))goto error;
 if(fm1_mem_overlap(a->sector,4096,r,sizeof *r)||fm1_mem_overlap(a->sector,4096,r->port,sizeof *r->port)||fm1_mem_overlap(a->sector,4096,a,sizeof *a)||fm1_mem_overlap(a->sector,4096,o,sizeof *o)||fm1_mem_overlap(a->sector,4096,park,sizeof *park))goto error;
 r->states[0]=init0;r->states[1]=init1;r->stage=(fm1_fixed_stage){0,a->sector,o->validate,o->context,a->work,mode==0?a->previous:a->session,&r->staged_bank,a->arena,a->arena_capacity,a->work->pattern,&r->staged_state,safe,faulted,r};
 r->pair=(fm1_pair_bridge){0,&r->stage,a->sector,a->sector+3584u,fence,faulted,r};r->lease[0]=(pps_owner_lease){.token=++r->next_lease,.active=1};r->lease[1]=(pps_owner_lease){.token=++r->next_lease,.active=1};
 if(!fm1_fixed_stage_source_separate(&r->stage,r->port,sizeof *r->port)||!fm1_fixed_stage_source_separate(&r->stage,&r->bridge,sizeof r->bridge)||!fm1_fixed_stage_source_separate(&r->stage,&r->bridge_ops,sizeof r->bridge_ops)||!fm1_fixed_stage_source_separate(&r->stage,&r->pinned,sizeof r->pinned)||!fm1_fixed_stage_source_separate(&r->stage,r->owner,sizeof r->owner)||!fm1_fixed_stage_source_separate(&r->stage,r->lease,sizeof r->lease)||!fm1_fixed_stage_source_separate(&r->stage,r->cert,sizeof r->cert)||!fm1_fixed_stage_source_separate(&r->stage,&r->pair,sizeof r->pair))goto error;
 if(mode==0){if(!capture(r))goto denied;}else if(!o->export_bank(o->context,&r->old_bank,a->previous_live,&r->old_state))goto denied;if(!fm1_fixed_stage_ready(&r->stage)||!scratch_safe(r))goto error;
 if(r->pinned.fixed_partition_auto){x=FM1_DELETE_RAW_MODE(mode)?1:auto_state(r,slot,&init0,mode==1||mode==2);if(x==-2&&(mode==0||mode==3)){init0=2;x=1;}if(x!=1)goto denied;if(mode==2&&init0){if(!end_port(r))return -1;return 2;}if((mode==1||mode==2)&&init0)goto denied;if(working_write){x=auto_state(r,FM1_FIXED_WORKING_SLOT,&init1,0);if(x==-2){init1=2;x=1;}if(x!=1)goto denied;}}
 else{x=r->pinned.pair_state(r->pinned.ctx,token,slot,mode==0||mode==3||mode==4,&init0);if(x!=1||init0>1||((mode==1||mode==2)&&init0))goto denied;if(working_write){x=r->pinned.pair_state(r->pinned.ctx,token,FM1_FIXED_WORKING_SLOT,1,&init1);if(x!=1||init1>1)goto denied;}}
 r->states[0]=init0;r->states[1]=init1;if(!pps_owner_bind(&r->owner[0],park,&r->lease[0],r->lease[0].token,slot,r->pinned.role,fm1_pair_project_validate,&r->stage,a->sector+3840u))goto error;r->pair.store=pps_owner_store(&r->owner[0]);if(!r->pair.store)goto error;
 memset(t,0,sizeof *t);t->source=&a->source;t->write_pair=fm1_pair_bridge_write;t->write_ctx=&r->pair;return 1;
denied: if(!end_port(r))return -1;return x==-2?(mode==2&&slot==FM1_FIXED_WORKING_SLOT?5:3):x==-3?4:x<0?-1:0;
error: (void)end_port(r);return -1;}
static int prepare(void *v,uint32_t slot,fm1_fixed_bridge_transaction *t){return begin(v,slot,0,1,t);}
static int prepare_load(void *v,uint32_t slot,fm1_fixed_bridge_transaction *t){return begin(v,slot,1,1,t);}
static int prepare_recovery(void *v,fm1_fixed_bridge_transaction *t){return begin(v,FM1_FIXED_WORKING_SLOT,2,0,t);}
static int open_pair(void *v,uint32_t slot,fm1_fixed_bridge_access *a){fm1_fixed_runtime *r=v;unsigned i,init;int x;if(!r||!a||!fence(r))return -1;i=slot==FM1_FIXED_WORKING_SLOT&&r->owner[0].slot!=slot?1u:0u;if(i==1){if(r->opened!=1||!pps_owner_handoff_working(&r->owner[0],&r->owner[1],&r->lease[1],r->lease[1].token,r->adapter->sector+3840u))return faulted(r)?-1:0;r->pair.store=pps_owner_store(&r->owner[1]);}else if(r->opened||r->owner[0].slot!=slot)return 0;init=r->states[i];if(init==2&&r->pinned.fixed_partition_auto){size_t len=0;fm1_project_source *src=&r->adapter->source;int enc;if(r->mode!=0||!fm1_fixed_project_source_separate(src,r->adapter->sector,PPS_MAX_PAYLOAD))return 0;enc=fm1_codec_source_encode(fm1_project_source_read,src,src->total,r->adapter->sector,PPS_MAX_PAYLOAD,&len);if(enc==FM1_CODEC_FULL)return 2;if(enc!=FM1_CODEC_OK)return 0;x=pps_owner_reclaim_incompatible(&r->owner[i]);if(x!=PS_OK)return faulted(r)?-1:0;x=auto_state(r,slot,&init,0);if(x!=1)return x<0?-1:0;r->states[i]=init;}else{x=r->pinned.fixed_partition_auto?auto_state(r,slot,&init,i==0&&r->mode==1):r->pinned.pair_state(r->pinned.ctx,r->token,slot,i==1||r->mode==0,&init);if(x!=1||init!=r->states[i])return x<0?-1:0;}r->cert[i]=(fm1_fixed_runtime_cert){r,r->token,slot,init,1};a->certify=cert;a->context=&r->cert[i];a->initializing=init;r->opened++;return 1;}
static int close_pair(void *v,fm1_fixed_bridge_access *a){fm1_fixed_runtime *r=v;fm1_fixed_runtime_cert *c=a?a->context:0;if(!r||!c||c->runtime!=r||!c->active)return 0;c->active=0;return fence(r);}
static int activate(void *v,uint32_t slot){fm1_fixed_runtime *r=v;unsigned init=1;if(!fence(r))return 0;if(r->pinned.fixed_partition_auto)return auto_state(r,slot,&init,0)==1&&!init&&fence(r);return r->pinned.activate(r->pinned.ctx,r->token,slot)==1;}
static int finish(void *v){return end_port(v);}
static int stage_load(void *v,uint32_t slot,pops_certify_fn c,void *ctx){fm1_fixed_runtime *r=v;return fm1_pair_bridge_stage(&r->pair,slot,c,ctx);}
static int stage_working(void *v,pops_certify_fn c,void *ctx,char name[PS_NAME_BYTES]){fm1_fixed_runtime *r=v;return fm1_pair_bridge_working(&r->pair,c,ctx,name);}
static int adopt(void *v){fm1_fixed_runtime *r=v;fm1_project_adapter *a=r->adapter;const fm1_project_adapter_ops *o=r->ops;if(!fence(r)||!o->can_adopt(o->context,&r->staged_bank,a->work->pattern,&r->staged_state)||!o->capture(a->previous,FM1_PROJECT_SESSION))return 0;if(o->restore(a->session,FM1_PROJECT_SESSION)&&o->adopt(o->context,&r->staged_bank,a->work->pattern,&r->staged_state))return 1;if(!o->restore(a->previous,FM1_PROJECT_SESSION)||!o->restore_pattern(o->context,a->previous_live))return -1;return 0;}
static int recapture(void *v,fm1_fixed_bridge_transaction *t){fm1_fixed_runtime *r=v;if(!fence(r)||r->pinned.set_mode(r->pinned.ctx,r->token,0)!=1)return -1;if(!capture(r))return faulted(r)?-1:0;t->source=&r->adapter->source;t->write_pair=fm1_pair_bridge_write;t->write_ctx=&r->pair;return 1;}
static int commit_slot(void *v,int slot){fm1_fixed_runtime *r=v;return fence(r)&&r->pinned.commit_slot(r->pinned.ctx,r->token,slot)==1;}
int fm1_fixed_runtime_bind(fm1_fixed_runtime *r,const fm1_fixed_runtime_port *p){fm1_fixed_bridge_ops *o;if(!r||!p||fm1_mem_overlap(r,sizeof *r,p,sizeof *p)||r->port||r->active||!p->begin||!p->set_mode||!p->end||!p->fence||!p->faulted||!p->pair_state||!p->certificate||!p->activate||!p->commit_slot||!p->named_slots||p->named_slots>12||p->fixed_partition_auto>1)return PFS_INVALID;r->port=p;r->pinned=*p;o=&r->bridge_ops;memset(o,0,sizeof *o);o->context=r;o->prepare=prepare;o->open_pair=open_pair;o->close_pair=close_pair;o->activate=activate;o->finish=finish;o->prepare_load=prepare_load;o->stage_load=stage_load;o->adopt=adopt;o->recapture=recapture;o->named_slots=p->named_slots;o->prepare_recovery=prepare_recovery;o->stage_working=stage_working;o->commit_slot=commit_slot;if(fm1_fixed_bridge_bind(&r->bridge,o)!=PFS_OK){r->port=0;return PFS_INVALID;}return PFS_OK;}

int fm1_fixed_runtime_scan(fm1_fixed_runtime *r,uint32_t slot,char name[PS_NAME_BYTES]){fm1_fixed_bridge_transaction t;fm1_fixed_bridge_access a;ps_info info;int rc,x;unsigned i;if(!r||!name||!r->port||r->bridge.busy||r->bridge.fault||slot>=r->pinned.named_slots||fm1_mem_overlap(name,PS_NAME_BYTES,r,sizeof *r)||fm1_mem_overlap(name,PS_NAME_BYTES,r->port,sizeof *r->port))return PFS_INVALID;memset(&t,0,sizeof t);memset(&a,0,sizeof a);r->bridge.busy=1;x=begin(r,slot,2,0,&t);if(x!=1){r->bridge.busy=0;if(x<0){r->bridge.fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return x==2?PFS_EMPTY:x==3?PFS_INVALID:FM1_PROJECT_BUSY;}if(fm1_mem_overlap(name,PS_NAME_BYTES,r->adapter,sizeof *r->adapter)||fm1_mem_overlap(name,PS_NAME_BYTES,r->ops,sizeof *r->ops)||fm1_mem_overlap(name,PS_NAME_BYTES,r->park,sizeof *r->park)||fm1_mem_overlap(name,PS_NAME_BYTES,r->adapter->previous,FM1_PROJECT_SESSION)||fm1_mem_overlap(name,PS_NAME_BYTES,r->adapter->session,FM1_PROJECT_SESSION)||fm1_mem_overlap(name,PS_NAME_BYTES,r->adapter->sector,4096)||!fm1_fixed_stage_source_separate(&r->stage,name,PS_NAME_BYTES)||fm1_mem_overlap(name,PS_NAME_BYTES,r->old_bank.arena,r->old_bank.capacity)||fm1_mem_overlap(name,PS_NAME_BYTES,r->adapter->previous_live,sizeof *r->adapter->previous_live))rc=PFS_INVALID;else{x=open_pair(r,slot,&a);if(x<0){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;}else if(x!=1)rc=PFS_INVALID;else{rc=fm1_pair_bridge_info(&r->pair,slot,a.certify,a.context,&info);if(close_pair(r,&a)!=1){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;}else if(rc==PS_OK){for(i=0;i<PS_NAME_BYTES&&info.name[i];i++)if((unsigned char)info.name[i]<32||(unsigned char)info.name[i]>126)break;if(!i||i==PS_NAME_BYTES||info.name[i])rc=PFS_INVALID;else{for(;i<PS_NAME_BYTES;i++)if(info.name[i]){rc=PFS_INVALID;break;}}}}}if(!end_port(r)){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;}r->bridge.busy=0;if(rc==PS_OK){memcpy(name,info.name,PS_NAME_BYTES);return PFS_OK;}return rc==FM1_PROJECT_ROLLBACK_FAILED?rc:rc==PS_EMPTY?PFS_EMPTY:rc==PS_IO?PFS_IO:PFS_INVALID;}

int fm1_fixed_runtime_new(fm1_fixed_runtime *r,const uint8_t *factory,size_t length){fm1_fixed_bridge_transaction t;fm1_fixed_bridge_access access;fm1_pattern_song state;fm1_project_adapter *a;const fm1_project_adapter_ops *o;char name[PS_NAME_BYTES];int x,rc,closed,staged=0;if(!r||!factory||length!=FM1_PROJECT_SESSION||!r->port||r->bridge.busy||r->bridge.fault||fm1_mem_overlap(factory,length,r,sizeof *r)||fm1_mem_overlap(factory,length,r->port,sizeof *r->port))return PFS_INVALID;memset(&t,0,sizeof t);memset(&access,0,sizeof access);r->bridge.busy=1;x=begin(r,FM1_FIXED_WORKING_SLOT,3,0,&t);if(x!=1){r->bridge.busy=0;if(x<0){r->bridge.fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return x==3?PFS_INVALID:FM1_PROJECT_BUSY;}a=r->adapter;o=r->ops;if(fm1_mem_overlap(factory,length,a,sizeof *a)||fm1_mem_overlap(factory,length,o,sizeof *o)||fm1_mem_overlap(factory,length,r->park,sizeof *r->park)||fm1_mem_overlap(factory,length,r->old_bank.arena,r->old_bank.capacity)||fm1_mem_overlap(factory,length,a->sector,4096)||fm1_mem_overlap(factory,length,a->session,FM1_PROJECT_SESSION)||fm1_mem_overlap(factory,length,a->previous,FM1_PROJECT_SESSION)||fm1_mem_overlap(factory,length,a->previous_live,sizeof *a->previous_live)||!fm1_fixed_stage_source_separate(&r->stage,factory,length)||!o->validate(factory,length,o->context)){rc=PFS_INVALID;goto finish_new;}memcpy(a->session,factory,length);if(!br_pattern_session_snapshot(a->session,FM1_PROJECT_SESSION,a->previous_live)||br_pattern_bank_init(&r->old_bank,r->factory_arena,sizeof r->factory_arena)!=BR_BANK_OK){rc=PFS_INVALID;goto finish_new;}fm1_pattern_song_new(&state,br_pattern_snapshot_bars(a->previous_live));if(fm1_project_source_init(&a->source,a->session,&r->old_bank,a->previous_live,&state,o->validate,o->context,a->work)!=FM1_PROJECT_OK){rc=PFS_INVALID;goto finish_new;}r->mode=0;r->stage.session=a->previous;if(!safe(r)||!scratch_safe(r)){rc=PFS_INVALID;goto finish_new;}x=open_pair(r,FM1_FIXED_WORKING_SLOT,&access);if(x!=1){rc=x<0?FM1_PROJECT_ROLLBACK_FAILED:x==2?PFS_FULL:PFS_INVALID;goto finish_new;}rc=fm1_pair_bridge_write(&r->pair,FM1_FIXED_WORKING_SLOT,"W00",&a->source,access.initializing,access.certify,access.context);if(rc==PS_OK)staged=fm1_pair_bridge_working(&r->pair,access.certify,access.context,name);closed=close_pair(r,&access);if(closed!=1||rc==FM1_PROJECT_ROLLBACK_FAILED){rc=FM1_PROJECT_ROLLBACK_FAILED;goto finish_new;}if(rc!=PS_OK){if(access.initializing&&rc!=FM1_FIXED_FULL)rc=FM1_PROJECT_ROLLBACK_FAILED;else rc=fm1_fixed_project_bridge_result(rc);goto finish_new;}if(staged!=1){rc=(staged<0||access.initializing)?FM1_PROJECT_ROLLBACK_FAILED:FM1_PROJECT_NEW_WORKING_ONLY;goto finish_new;}if(fm1_fixed_bridge_working_slot(name,r->port->named_slots)!=-1||(access.initializing&&activate(r,FM1_FIXED_WORKING_SLOT)!=1)){rc=FM1_PROJECT_ROLLBACK_FAILED;goto finish_new;}r->mode=1;if(!o->export_bank(o->context,&r->old_bank,a->previous_live,&r->old_state)||!o->can_adopt(o->context,&r->staged_bank,a->work->pattern,&r->staged_state)||!o->capture(a->previous,FM1_PROJECT_SESSION)){rc=FM1_PROJECT_NEW_WORKING_ONLY;goto finish_new;}if(!o->restore(a->session,FM1_PROJECT_SESSION)||!o->adopt(o->context,&r->staged_bank,a->work->pattern,&r->staged_state)){if(!o->restore(a->previous,FM1_PROJECT_SESSION)||!o->restore_pattern(o->context,a->previous_live))rc=FM1_PROJECT_ROLLBACK_FAILED;else rc=FM1_PROJECT_NEW_WORKING_ONLY;goto finish_new;}if(!commit_slot(r,-1)){rc=FM1_PROJECT_ROLLBACK_FAILED;goto finish_new;}rc=PFS_OK;
finish_new:if(rc==FM1_PROJECT_ROLLBACK_FAILED)r->bridge.fault=1;if(!end_port(r)){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;}r->bridge.busy=0;return rc;}

/* The input is retained independently of the disposable decode and flash
 * scratch. Each storage operation rechecks the exact-pair certificate. */
typedef struct { fm1_fixed_runtime *r; fm1_fixed_bridge_access *access; const pps_store *inner; uint32_t first; const uint8_t *bytes; uint32_t length; } import_guard;
static int import_allowed(import_guard *g) { return g && fence(g->r) && g->access->certify && g->access->certify(g->access->context,g->first)==1 && g->access->certify(g->access->context,g->first+PS_SECTOR)==1; }
static int import_read(void *v,uint32_t off,void *dst,uint32_t n) { import_guard *g=v; return import_allowed(g) && off>=g->first && off-g->first<2u*PS_SECTOR && n<=2u*PS_SECTOR-(off-g->first) ? g->inner->read(g->inner->ctx,off,dst,n) : -1; }
static int import_program(void *v,uint32_t off,const void *src,uint32_t n) { import_guard *g=v; return import_allowed(g) && off>=g->first && off-g->first<2u*PS_SECTOR && n<=2u*PS_SECTOR-(off-g->first) ? g->inner->program(g->inner->ctx,off,src,n) : -1; }
static int import_erase(void *v,uint32_t off) { import_guard *g=v; return import_allowed(g) && (off==g->first || off==g->first+PS_SECTOR) ? g->inner->erase(g->inner->ctx,off) : -1; }
static int import_source(void *v,uint32_t off,void *dst,uint32_t n) { import_guard *g=v; if(!import_allowed(g) || off>g->length || n>g->length-off || (n && (!dst || fm1_mem_overlap(dst,n,g->bytes,g->length)))) return -1; if(n) memcpy(dst,g->bytes+off,n); return 0; }
int fm1_fixed_runtime_import(fm1_fixed_runtime *r,uint32_t slot,const char *name,const uint8_t *bytes,size_t length)
{
 fm1_fixed_bridge_transaction t; fm1_fixed_bridge_access a; import_guard g; pps_store s; pps_reader reader; ps_info info; char nm[PS_NAME_BYTES]={0}; unsigned i; int x,rc;
 if(!r || !r->port || !name || !bytes || !length || length>PPS_MAX_PAYLOAD || slot>=r->pinned.named_slots || r->bridge.busy || r->bridge.fault || fm1_mem_overlap(bytes,length,r,sizeof *r) || fm1_mem_overlap(bytes,length,r->port,sizeof *r->port)) return PFS_INVALID;
 for(i=0;i<PS_NAME_BYTES-1u && name[i];i++){ if((unsigned char)name[i]<32u || (unsigned char)name[i]>126u) return PFS_INVALID; nm[i]=name[i]; } if(!i || name[i]) return PFS_INVALID;
 memset(&t,0,sizeof t); memset(&a,0,sizeof a); r->bridge.busy=1; x=begin(r,slot,4,0,&t);
 if(x!=1){r->bridge.busy=0;if(x<0){r->bridge.fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return x==3?PFS_INVALID:FM1_PROJECT_BUSY;}
 if(fm1_mem_overlap(bytes,length,r->adapter,sizeof *r->adapter) || fm1_mem_overlap(bytes,length,r->ops,sizeof *r->ops) || fm1_mem_overlap(bytes,length,r->park,sizeof *r->park) || fm1_mem_overlap(bytes,length,r->adapter->sector,4096) || fm1_mem_overlap(bytes,length,r->adapter->previous,FM1_PROJECT_SESSION) || fm1_mem_overlap(bytes,length,r->old_bank.arena,r->old_bank.capacity) || fm1_mem_overlap(bytes,length,r->adapter->previous_live,sizeof *r->adapter->previous_live) || !fm1_fixed_stage_source_separate(&r->stage,bytes,length)){rc=PFS_INVALID;goto import_finish;}
 x=open_pair(r,slot,&a); if(x!=1){rc=x<0?FM1_PROJECT_ROLLBACK_FAILED:PFS_INVALID;goto import_finish;}
 g=(import_guard){r,&a,r->pair.store,r->park->base+slot*2u*PS_SECTOR,bytes,(uint32_t)length}; s=*g.inner; s.ctx=&g;s.read=import_read;s.program=import_program;s.erase=import_erase;
 rc=pps_save(&s,slot,nm,(uint32_t)length,import_source,&g,r->adapter->sector+3584u,&info);
 if(rc==PS_OK){rc=pps_reader_open(&s,slot,&reader,r->adapter->sector+3584u);if(rc==PS_OK)rc=pps_reader_equal(&reader,bytes,(uint32_t)length,r->adapter->sector+3584u);}
 if(rc==PS_OK && a.initializing && activate(r,slot)!=1)rc=FM1_PROJECT_ROLLBACK_FAILED;
 if(close_pair(r,&a)!=1 || !fence(r))rc=FM1_PROJECT_ROLLBACK_FAILED;
 if(rc!=FM1_PROJECT_ROLLBACK_FAILED) rc=fm1_fixed_project_bridge_result(rc);
import_finish:
 if(rc==FM1_PROJECT_ROLLBACK_FAILED)r->bridge.fault=1;
 if(!end_port(r)){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;} r->bridge.busy=0;return rc;
}

int fm1_fixed_runtime_export(fm1_fixed_runtime *r,uint32_t slot,uint8_t *bytes,size_t capacity,uint32_t *length,char name[PS_NAME_BYTES])
{
 fm1_fixed_bridge_transaction t; fm1_fixed_bridge_access a; import_guard g; pps_store s; pps_reader reader; ps_info info; unsigned i; int x,rc;
 if(!r || !r->port || !bytes || capacity<PPS_MAX_PAYLOAD || !length || !name || slot>=r->pinned.named_slots || r->bridge.busy || r->bridge.fault || fm1_mem_overlap(bytes,capacity,r,sizeof *r) || fm1_mem_overlap(bytes,capacity,r->port,sizeof *r->port) || fm1_mem_overlap(bytes,capacity,length,sizeof *length) || fm1_mem_overlap(bytes,capacity,name,PS_NAME_BYTES) || fm1_mem_overlap(length,sizeof *length,name,PS_NAME_BYTES) || fm1_mem_overlap(length,sizeof *length,r,sizeof *r) || fm1_mem_overlap(name,PS_NAME_BYTES,r,sizeof *r) || fm1_mem_overlap(length,sizeof *length,r->port,sizeof *r->port) || fm1_mem_overlap(name,PS_NAME_BYTES,r->port,sizeof *r->port)) return PFS_INVALID;
 memset(&t,0,sizeof t); memset(&a,0,sizeof a); r->bridge.busy=1; x=begin(r,slot,2,0,&t);
 if(x!=1){r->bridge.busy=0;if(x<0){r->bridge.fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return x==2?PFS_EMPTY:x==3?PFS_INVALID:FM1_PROJECT_BUSY;}
 #define EXPORT_DISJOINT(p,n) (!fm1_mem_overlap(p,n,r->adapter,sizeof *r->adapter) && !fm1_mem_overlap(p,n,r->ops,sizeof *r->ops) && !fm1_mem_overlap(p,n,r->park,sizeof *r->park) && !fm1_mem_overlap(p,n,r->adapter->sector,4096) && !fm1_mem_overlap(p,n,r->adapter->previous,FM1_PROJECT_SESSION) && !fm1_mem_overlap(p,n,r->old_bank.arena,r->old_bank.capacity) && !fm1_mem_overlap(p,n,r->adapter->previous_live,sizeof *r->adapter->previous_live) && fm1_fixed_stage_source_separate(&r->stage,p,n))
 if(!EXPORT_DISJOINT(bytes,capacity) || !EXPORT_DISJOINT(length,sizeof *length) || !EXPORT_DISJOINT(name,PS_NAME_BYTES)){rc=PFS_INVALID;goto export_finish;}
 #undef EXPORT_DISJOINT
 x=open_pair(r,slot,&a); if(x!=1){rc=x<0?FM1_PROJECT_ROLLBACK_FAILED:PFS_INVALID;goto export_finish;}
 g=(import_guard){r,&a,r->pair.store,r->park->base+slot*2u*PS_SECTOR,0,0}; s=*g.inner; s.ctx=&g; s.read=import_read; s.program=readonly_program; s.erase=readonly_erase;
 rc=pps_reader_open(&s,slot,&reader,r->adapter->sector+3584u);
 if(rc==PS_OK)rc=pps_reader_info(&reader,&info);
 if(rc==PS_OK){for(i=0;i<PS_NAME_BYTES && info.name[i];i++)if((unsigned char)info.name[i]<32u || (unsigned char)info.name[i]>126u)break; if(!i || i==PS_NAME_BYTES || info.name[i])rc=PS_INVALID; else for(;i<PS_NAME_BYTES;i++)if(info.name[i]){rc=PS_INVALID;break;}}
 if(rc==PS_OK)rc=pps_reader_read_at(&reader,0,bytes,reader.length,r->adapter->sector+3584u);
 if(close_pair(r,&a)!=1 || !fence(r))rc=FM1_PROJECT_ROLLBACK_FAILED;
 if(rc==PS_OK){*length=reader.length;memcpy(name,info.name,PS_NAME_BYTES);} else if(rc!=FM1_PROJECT_ROLLBACK_FAILED)rc=fm1_fixed_project_bridge_result(rc);
export_finish:
 if(rc==FM1_PROJECT_ROLLBACK_FAILED)r->bridge.fault=1;
 if(!end_port(r)){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;} r->bridge.busy=0;return rc;
}

/* Listing uses the fixed partition's read-only fenced park directly, without
 * acquiring a per-pair write owner or borrowing the diagnostic snapshot. A
 * CRC-valid record is only a candidate: semantic validation happens on load. */
static uint32_t list_get(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t list_crc_add(uint32_t c,const uint8_t *p,uint32_t n){uint32_t i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return c;}
static int list_blank(const uint8_t *p,uint32_t n){while(n--)if(*p++!=255u)return 0;return 1;}
typedef struct { int state; uint32_t generation, length, crc; char name[PS_NAME_BYTES]; } list_copy;
static int list_read(fm1_fixed_runtime *r,uint32_t off,void *p,uint32_t n){return fence(r)&&!br_pattern_park_read(r->park,off,p,n)&&fence(r);}
static int list_inspect(fm1_fixed_runtime *r,uint32_t slot,unsigned copy,list_copy *out,uint8_t *page){
 uint8_t header[256]; uint32_t off=r->park->base+(slot*2u+copy)*PS_SECTOR,n,i,remaining,c=~0u,part; int erased;
 memset(out,0,sizeof *out);out->state=FM1_LIST_UNAVAILABLE;
 if(!list_read(r,off,header,256))return 0;
 erased=list_blank(header,256);
 if(!erased){
  if(list_get(header)!=0x32535050u||memcmp(header+4,r->pinned.role,16)||list_get(header+20)!=slot||list_get(header+24)!=copy||!list_get(header+28)||!(n=list_get(header+32))||n>PPS_MAX_PAYLOAD||header[52]||header[53]||header[54]||header[55]||!list_blank(header+56,32)||list_get(header+88)!=~list_crc_add(~0u,header,88)||list_get(header+92)!=0x32435050u||!list_blank(header+96,160))return 1;
  for(i=0;i<PS_NAME_BYTES-1u&&header[40+i];i++)if(header[40+i]<32u||header[40+i]>126u)return 1;
  if(!i||header[40+i])return 1;
  for(;i<PS_NAME_BYTES;i++)if(header[40+i])return 1;
 }else n=0;
 remaining=n;
 for(i=1;i<16;i++){
  if(!list_read(r,off+i*256u,page,256))return 0;
  part=remaining<256u?remaining:256u;
  if(part){c=list_crc_add(c,page,part);remaining-=part;}
  if(!list_blank(page+part,256u-part))return 1;
 }
 if(!list_read(r,off,page,256)||memcmp(page,header,256))return 0;
 if(erased){out->state=FM1_LIST_EMPTY;return 1;}
 if(remaining||~c!=list_get(header+36))return 1;
 out->state=FM1_LIST_PRESENT;out->generation=list_get(header+28);out->length=n;out->crc=list_get(header+36);memcpy(out->name,header+40,PS_NAME_BYTES);return 1;
}
/* Snapshot a storage-valid payload, without invoking the current project
 * decoder. An invalid sibling is refused: no stale survivor is inferred. */
#if (defined(FM1_PAIR_STREAM_HOST_TEST) && FM1_PAIR_STREAM_HOST_TEST && !defined(FM1_DEVICE)) || (defined(FM1_PATTERN_RAW_EXPORT_EXPERIMENT) && FM1_PATTERN_RAW_EXPORT_EXPERIMENT && defined(FM1_DEVICE))
int fm1_fixed_runtime_export_raw(fm1_fixed_runtime *r,uint32_t slot,uint8_t *bytes,size_t capacity,uint32_t *length,char name[PS_NAME_BYTES]) {
 fm1_project_adapter *a=0; const fm1_project_adapter_ops *o=0; br_pattern_park_io *park=0;
 uint32_t token=0,off,n; size_t cap=0; list_copy before[2],after[2]; int x,rc=PFS_INVALID,selected=-1;
 if(!r||!r->port||!same_port(r->port,&r->pinned)||!r->pinned.fixed_partition_auto||!bytes||capacity<PPS_MAX_PAYLOAD||!length||!name||slot>=r->pinned.named_slots||r->active||r->bridge.busy||r->bridge.fault||fm1_mem_overlap(bytes,capacity,r,sizeof *r)||fm1_mem_overlap(bytes,capacity,r->port,sizeof *r->port)||fm1_mem_overlap(bytes,capacity,length,sizeof *length)||fm1_mem_overlap(bytes,capacity,name,PS_NAME_BYTES)||fm1_mem_overlap(length,sizeof *length,name,PS_NAME_BYTES)||fm1_mem_overlap(length,sizeof *length,r,sizeof *r)||fm1_mem_overlap(name,PS_NAME_BYTES,r,sizeof *r)||fm1_mem_overlap(length,sizeof *length,r->port,sizeof *r->port)||fm1_mem_overlap(name,PS_NAME_BYTES,r->port,sizeof *r->port))return PFS_INVALID;
 r->bridge.busy=1;x=r->pinned.begin(r->pinned.ctx,2,&a,&o,&token,&park,&cap);
 if(x!=1){r->bridge.busy=0;if(x<0){r->bridge.fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return FM1_PROJECT_BUSY;}
 r->active=1;r->token=token;r->adapter=a;r->ops=o;r->park=park;memset(r->owner,0,sizeof r->owner);memset(r->cert,0,sizeof r->cert);
 if(!a||!o||!park||!token||!a->sector||cap<256u||park->base%PS_SECTOR||park->base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR||park->end<park->base||park->end-park->base<PS_TOTAL_SLOTS*2u*PS_SECTOR||!fence(r)||fm1_mem_overlap(bytes,capacity,a,sizeof *a)||fm1_mem_overlap(bytes,capacity,o,sizeof *o)||fm1_mem_overlap(bytes,capacity,park,sizeof *park)||fm1_mem_overlap(bytes,capacity,a->sector,cap)||fm1_mem_overlap(length,sizeof *length,a->sector,cap)||fm1_mem_overlap(name,PS_NAME_BYTES,a->sector,cap))goto raw_done;
 /* The transport buffer must not alias the borrowed working state. */
#define RAW_SEPARATE(p,n) (!fm1_mem_overlap((p),(n),a,sizeof *a) && !fm1_mem_overlap((p),(n),o,sizeof *o) && !fm1_mem_overlap((p),(n),park,sizeof *park) && !fm1_mem_overlap((p),(n),a->sector,cap) && !fm1_mem_overlap((p),(n),a->session,FM1_PROJECT_SESSION) && !fm1_mem_overlap((p),(n),a->previous,FM1_PROJECT_SESSION) && !fm1_mem_overlap((p),(n),a->arena,a->arena_capacity) && !fm1_mem_overlap((p),(n),a->previous_live,sizeof *a->previous_live) && !fm1_mem_overlap((p),(n),a->live,sizeof *a->live) && (!a->work || (!fm1_mem_overlap((p),(n),a->work,sizeof *a->work) && !fm1_mem_overlap((p),(n),a->work->pattern,sizeof *a->work->pattern) && !fm1_mem_overlap((p),(n),a->work->session,FM1_PROJECT_SESSION) && !fm1_mem_overlap((p),(n),a->work->arena,a->work->arena_capacity) && !fm1_mem_overlap((p),(n),a->work->compressed,a->work->compressed_capacity))))
 if(!RAW_SEPARATE(bytes,capacity)||!RAW_SEPARATE(length,sizeof *length)||!RAW_SEPARATE(name,PS_NAME_BYTES)||fm1_mem_overlap(a->sector,256,r,sizeof *r)||fm1_mem_overlap(a->sector,256,a,sizeof *a)||fm1_mem_overlap(a->sector,256,o,sizeof *o)||fm1_mem_overlap(a->sector,256,park,sizeof *park)||fm1_mem_overlap(a->sector,256,a->session,FM1_PROJECT_SESSION)||fm1_mem_overlap(a->sector,256,a->previous,FM1_PROJECT_SESSION)||fm1_mem_overlap(a->sector,256,a->arena,a->arena_capacity)||fm1_mem_overlap(a->sector,256,a->previous_live,sizeof *a->previous_live)||fm1_mem_overlap(a->sector,256,a->live,sizeof *a->live)||(a->work&&(fm1_mem_overlap(a->sector,256,a->work,sizeof *a->work)||fm1_mem_overlap(a->sector,256,a->work->pattern,sizeof *a->work->pattern)||fm1_mem_overlap(a->sector,256,a->work->session,FM1_PROJECT_SESSION)||fm1_mem_overlap(a->sector,256,a->work->arena,a->work->arena_capacity)||fm1_mem_overlap(a->sector,256,a->work->compressed,a->work->compressed_capacity))))goto raw_done;
#undef RAW_SEPARATE
 if(!list_inspect(r,slot,0,&before[0],a->sector)||!list_inspect(r,slot,1,&before[1],a->sector)){rc=PFS_IO;goto raw_done;}
 if(before[0].state==FM1_LIST_EMPTY&&before[1].state==FM1_LIST_EMPTY){rc=PFS_EMPTY;goto raw_done;}
 if(before[0].state==FM1_LIST_PRESENT&&before[1].state==FM1_LIST_EMPTY)selected=0;
 else if(before[1].state==FM1_LIST_PRESENT&&before[0].state==FM1_LIST_EMPTY)selected=1;
 else if(before[0].state==FM1_LIST_PRESENT&&before[1].state==FM1_LIST_PRESENT&&before[0].generation!=before[1].generation&&(before[0].generation>before[1].generation?before[0].generation-before[1].generation:before[1].generation-before[0].generation)==1u)selected=before[0].generation>before[1].generation?0:1;
 if(selected<0)goto raw_done;
 off=park->base+(slot*2u+(uint32_t)selected)*PS_SECTOR+256u;n=before[selected].length;
 if(!list_read(r,off,bytes,n)){rc=PFS_IO;goto raw_done;}
 if(~list_crc_add(~0u,bytes,n)!=before[selected].crc)goto raw_done;
 if(!list_inspect(r,slot,0,&after[0],a->sector)||!list_inspect(r,slot,1,&after[1],a->sector)){rc=PFS_IO;goto raw_done;}
 if(memcmp(before,after,sizeof before))goto raw_done;
 *length=n;memcpy(name,before[selected].name,PS_NAME_BYTES);rc=PFS_OK;
raw_done:
 x=fence(r);if(!end_port(r)||!x){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;}r->bridge.busy=0;return rc;
}
#endif
int fm1_fixed_runtime_list(fm1_fixed_runtime *r,uint32_t first,fm1_project_list_entry out[FM1_PROJECT_LIST_PAGE]){
 fm1_project_adapter *a=0;const fm1_project_adapter_ops *o=0;br_pattern_park_io *park=0;
 fm1_project_list_entry result[FM1_PROJECT_LIST_PAGE];uint32_t token=0,i;size_t cap=0;int x,rc=PFS_OK;list_copy c[2];
 if(!r||!out||!r->port||!same_port(r->port,&r->pinned)||!r->pinned.fixed_partition_auto||r->active||r->bridge.busy||r->bridge.fault||first>r->pinned.named_slots||r->pinned.named_slots-first<FM1_PROJECT_LIST_PAGE||fm1_mem_overlap(out,sizeof result,r,sizeof *r)||fm1_mem_overlap(out,sizeof result,r->port,sizeof *r->port))return PFS_INVALID;
 r->bridge.busy=1;x=r->pinned.begin(r->pinned.ctx,2,&a,&o,&token,&park,&cap);
 if(x!=1){r->bridge.busy=0;if(x<0){r->bridge.fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return FM1_PROJECT_BUSY;}
 r->active=1;r->token=token;r->adapter=a;r->ops=o;r->park=park;memset(r->owner,0,sizeof r->owner);memset(r->cert,0,sizeof r->cert);
 if(!a||!o||!park||!token||!a->sector||cap<256u||park->base%PS_SECTOR||park->base>UINT32_MAX-PS_TOTAL_SLOTS*2u*PS_SECTOR||park->end<park->base||park->end-park->base<PS_TOTAL_SLOTS*2u*PS_SECTOR||!fence(r)||fm1_mem_overlap(out,sizeof result,a,sizeof *a)||fm1_mem_overlap(out,sizeof result,o,sizeof *o)||fm1_mem_overlap(out,sizeof result,park,sizeof *park)||fm1_mem_overlap(out,sizeof result,a->sector,cap)||fm1_mem_overlap(a->sector,256,r,sizeof *r)||fm1_mem_overlap(a->sector,256,a,sizeof *a)||fm1_mem_overlap(a->sector,256,o,sizeof *o)||fm1_mem_overlap(a->sector,256,park,sizeof *park)){rc=PFS_INVALID;goto list_done;}
 memset(result,0,sizeof result);
 for(i=0;i<FM1_PROJECT_LIST_PAGE;i++){
  uint32_t slot=first+i; int selected=-1;
  if(!list_inspect(r,slot,0,&c[0],a->sector)||!list_inspect(r,slot,1,&c[1],a->sector)){rc=PFS_IO;break;}
  if(c[0].state==FM1_LIST_EMPTY&&c[1].state==FM1_LIST_EMPTY)result[i].state=FM1_LIST_EMPTY;
  else if(c[0].state==FM1_LIST_PRESENT&&c[1].state==FM1_LIST_EMPTY)selected=0;
  else if(c[1].state==FM1_LIST_PRESENT&&c[0].state==FM1_LIST_EMPTY)selected=1;
  else if(c[0].state==FM1_LIST_PRESENT&&c[1].state==FM1_LIST_PRESENT&&c[0].generation!=c[1].generation&&
          (c[0].generation>c[1].generation?c[0].generation-c[1].generation:c[1].generation-c[0].generation)==1u)selected=c[0].generation>c[1].generation?0:1;
  else result[i].state=FM1_LIST_UNAVAILABLE;
  if(selected>=0){result[i].state=FM1_LIST_PRESENT;memcpy(result[i].name,c[selected].name,PS_NAME_BYTES);}
 }
list_done:
 x=fence(r);if(!end_port(r)||!x){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;}
 r->bridge.busy=0;if(rc==PFS_OK)memcpy(out,result,sizeof result);return rc;
}

#if (defined(FM1_PAIR_STREAM_HOST_TEST) && FM1_PAIR_STREAM_HOST_TEST && !defined(FM1_DEVICE)) || (defined(FM1_PATTERN_DELETE_EXPERIMENT) && FM1_PATTERN_DELETE_EXPERIMENT && defined(FM1_DEVICE))
/* Experimental integration. The caller's retry flag represents fresh explicit
 * authorization to remove an incompatible sibling in this exact slot. */
typedef struct { fm1_fixed_runtime *r; fm1_fixed_bridge_access *access; uint32_t first; } delete_runtime_guard;
static int delete_runtime_fence(void *v){
 delete_runtime_guard *g=v;
 return g && fence(g->r) && g->access->certify &&
        g->access->certify(g->access->context,g->first)==1 &&
        g->access->certify(g->access->context,g->first+PS_SECTOR)==1;
}
int fm1_fixed_runtime_delete_experiment(fm1_fixed_runtime *r,uint32_t slot,const char *name,const uint8_t digest[32],int retry){
 fm1_fixed_bridge_transaction t;fm1_fixed_bridge_access a;delete_runtime_guard g;
 char nm[PS_NAME_BYTES]={0};uint8_t hash[32];unsigned i;int x,rc;
 if(!r||!r->port||!name||!digest||slot>=r->pinned.named_slots||r->bridge.busy||r->bridge.fault||(retry!=0&&retry!=1))return PFS_INVALID;
 for(i=0;i<PS_NAME_BYTES-1u&&name[i];i++){if((unsigned char)name[i]<32u||(unsigned char)name[i]>126u)return PFS_INVALID;nm[i]=name[i];}
 if(!i||name[i])return PFS_INVALID;memcpy(hash,digest,sizeof hash);
 memset(&t,0,sizeof t);memset(&a,0,sizeof a);r->bridge.busy=1;x=begin(r,slot,2,0,&t);
 if(x!=1){r->bridge.busy=0;if(x<0){r->bridge.fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return x==2?PFS_EMPTY:x==3?PFS_INVALID:FM1_PROJECT_BUSY;}
 x=open_pair(r,slot,&a);if(x!=1){rc=x<0?FM1_PROJECT_ROLLBACK_FAILED:PFS_INVALID;goto delete_done;}
 g=(delete_runtime_guard){r,&a,r->park->base+slot*2u*PS_SECTOR};
 rc=retry?pps_owner_delete_retry_experiment(&r->owner[0],nm,hash,delete_runtime_fence,&g):
          pps_owner_delete_experiment(&r->owner[0],nm,hash,delete_runtime_fence,&g);
 if(close_pair(r,&a)!=1||!fence(r))rc=FM1_PROJECT_ROLLBACK_FAILED;
 if(rc!=FM1_PROJECT_ROLLBACK_FAILED)rc=fm1_fixed_project_bridge_result(rc);
delete_done:
 if(rc==FM1_PROJECT_ROLLBACK_FAILED)r->bridge.fault=1;
 if(!end_port(r)){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;}r->bridge.busy=0;return rc;
}
#endif

#if (defined(FM1_PAIR_STREAM_HOST_TEST) && FM1_PAIR_STREAM_HOST_TEST && !defined(FM1_DEVICE)) || (defined(FM1_PATTERN_DELETE_EXPERIMENT) && FM1_PATTERN_DELETE_EXPERIMENT && defined(FM1_DEVICE))
/* Review the exact raw contents of an unavailable pair without exposing its
 * bytes. No caller may infer authorization to clear from this read. */
static int unreadable_runtime(fm1_fixed_runtime *r,uint32_t slot,uint8_t *digest,int clear){
 fm1_fixed_bridge_transaction t;fm1_fixed_bridge_access a;delete_runtime_guard g;
 pps_store s;pps_reader reader;fm1_pattern_sha256 sha;uint8_t page[256],hash[32];uint32_t off;int x,rc;
 if(!r||!r->port||!r->pinned.fixed_partition_auto||!digest||slot>=r->pinned.named_slots||r->bridge.busy||r->bridge.fault||fm1_mem_overlap(digest,32,r,sizeof *r)||fm1_mem_overlap(digest,32,r->port,sizeof *r->port))return PFS_INVALID;
 if(clear)memcpy(hash,digest,32);
 memset(&t,0,sizeof t);memset(&a,0,sizeof a);r->bridge.busy=1;x=begin(r,slot,5,0,&t);
 if(x!=1){r->bridge.busy=0;if(x<0){r->bridge.fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return FM1_PROJECT_BUSY;}
 if(fm1_mem_overlap(digest,32,r->adapter,sizeof *r->adapter)||fm1_mem_overlap(digest,32,r->ops,sizeof *r->ops)||fm1_mem_overlap(digest,32,r->park,sizeof *r->park)||fm1_mem_overlap(digest,32,r->adapter->sector,4096)||!fm1_fixed_stage_source_separate(&r->stage,digest,32)){rc=PFS_INVALID;goto unreadable_done;}
 r->cert[0]=(fm1_fixed_runtime_cert){r,r->token,slot,0,1};a.certify=cert;a.context=&r->cert[0];a.initializing=0;
 g=(delete_runtime_guard){r,&a,r->park->base+slot*2u*PS_SECTOR};
 if(!delete_runtime_fence(&g)){rc=FM1_PROJECT_ROLLBACK_FAILED;goto unreadable_close;}
 if(clear)rc=pps_owner_clear_unreadable_experiment(&r->owner[0],hash,delete_runtime_fence,&g);
 else {
  s=*r->pair.store;
  rc=pps_reader_open(&s,slot,&reader,page);
  if(rc==PS_CORRUPT){
   if(fm1_pattern_sha256_init(&sha))rc=PS_INVALID;
   else {rc=PS_OK;for(off=0;off<2u*PS_SECTOR;off+=sizeof page){
    if(!delete_runtime_fence(&g)||s.read(s.ctx,g.first+off,page,sizeof page)||!delete_runtime_fence(&g)||fm1_pattern_sha256_update(&sha,page,sizeof page)){rc=PS_IO;break;}
   }if(rc==PS_OK&&fm1_pattern_sha256_final(&sha,hash))rc=PS_IO;}
  }else if(rc!=PS_IO)rc=PS_INVALID;
 }
unreadable_close:
 if(close_pair(r,&a)!=1||!fence(r))rc=FM1_PROJECT_ROLLBACK_FAILED;
 if(rc!=FM1_PROJECT_ROLLBACK_FAILED)rc=fm1_fixed_project_bridge_result(rc);
unreadable_done:
 if(rc==FM1_PROJECT_ROLLBACK_FAILED)r->bridge.fault=1;
 if(!end_port(r)){r->bridge.fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;}r->bridge.busy=0;
 if(rc==PFS_OK&&!clear)memcpy(digest,hash,32);return rc;
}
int fm1_fixed_runtime_inspect_unreadable_experiment(fm1_fixed_runtime *r,uint32_t slot,uint8_t digest[32]){return unreadable_runtime(r,slot,digest,0);}
int fm1_fixed_runtime_clear_unreadable_experiment(fm1_fixed_runtime *r,uint32_t slot,const uint8_t digest[32]){
 uint8_t expected[32];if(!digest)return PFS_INVALID;memcpy(expected,digest,32);return unreadable_runtime(r,slot,expected,1);
}
#endif
