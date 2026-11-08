/* SPDX-License-Identifier: GPL-3.0-only */
#include "pattern_project_target_bridge.h"
#include <string.h>
static const br_pp_target_config *config;
static int current_slot=-1, fault;
typedef struct { fm1_project_adapter *a; const fm1_project_adapter_ops *ops; br_ps_target_store_handle handle; uint32_t token; } transaction;
#if defined(FM1_DEVICE) && FM1_DEVICE
/* The host prototype does not establish target flash ownership. No build flag
 * may turn this into a device writer until a reviewed backend replaces it. */
#if defined(FM1_PATTERN_FIXED_STORAGE_QUALIFIED) && FM1_PATTERN_FIXED_STORAGE_QUALIFIED
#error "Fixed pattern storage has no qualified target backend"
#endif
int br_pp_target_bind(const br_pp_target_config *c){(void)c;config=0;return PFS_INVALID;}
#else
int br_pp_target_bind(const br_pp_target_config *c){if(!c||!c->can_access||!c->factory||!c->empty_is_safe){config=0;return PFS_INVALID;}config=c;return PFS_OK;}
#endif
int br_pp_target_ready(void){return config&&!fault&&config->can_access&&config->factory&&config->empty_is_safe;}
static int allowed(transaction *t,unsigned id,int write){return config&&!fault&&id<=12&&config->can_access(config->context,&t->handle.store,t->a->sector,id,write)==1;}
static int begin(transaction *t,int mode){memset(t,0,sizeof *t);if(!br_ps_target_project_begin(mode,&t->a,&t->ops,&t->token)){if(br_ps_target_project_faulted()){fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return FM1_PROJECT_BUSY;}if(!br_ps_target_project_store(t->token,&t->handle)){if(!br_ps_target_project_end(t->token)){fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return PFS_IO;}return PFS_OK;}
static int end(transaction *t,int rc){if(!br_ps_target_project_end(t->token)){fault=1;return FM1_PROJECT_ROLLBACK_FAILED;}return rc;}
static int scan(transaction *t,unsigned id,char name[13]){uint8_t h[20];unsigned i;int rc=pfs_reader_open(&t->handle.store,id,&t->a->reader,t->a->sector);if(rc==PFS_EMPTY)return config->empty_is_safe(config->context,&t->handle.store,t->a->sector,id)==1?PFS_EMPTY:PFS_INVALID;if(rc)return rc;if(t->a->reader.length<=20||pfs_reader_read_at(&t->a->reader,0,h,sizeof h,t->a->sector)||memcmp(h,"FPA1",4)||h[17]>12||(id!=12&&h[17])||h[18]||h[19])return PFS_INVALID;for(i=0;i<13&&h[4+i];i++)if(h[4+i]<32||h[4+i]>126)return PFS_INVALID;if(!i||i==13)return PFS_INVALID;for(;i<13;i++)if(h[4+i])return PFS_INVALID;memcpy(name,h+4,13);return PFS_OK;}
int br_pp_target_scan(unsigned id,char name[13]){transaction t;int rc;if(!name||!br_pp_target_ready()||id>12)return PFS_INVALID;rc=begin(&t,2);if(rc)return rc;if(!allowed(&t,id,0))return end(&t,PFS_INVALID);return end(&t,scan(&t,id,name));}
static int working(transaction *t){if(!allowed(t,12,1))return PFS_INVALID;if(!br_ps_target_project_mode(t->token,0))return PFS_INVALID;t->a->current_slot=current_slot;return fm1_project_adapter_save(&t->handle.store,12,"WORKING",t->ops,t->a);}
int br_pp_target_save(unsigned id,const char *name){transaction t;int rc,old;if(id>=12||!name||!br_pp_target_ready())return PFS_INVALID;rc=begin(&t,0);if(rc)return rc;if(!allowed(&t,id,1)||!allowed(&t,12,1))return end(&t,PFS_INVALID);rc=fm1_project_adapter_save(&t.handle.store,id,name,t.ops,t.a);if(!rc){old=current_slot;current_slot=(int)id;if(working(&t)){current_slot=old;rc=FM1_PROJECT_WORKING_FAILED;}}return end(&t,rc);}
int br_pp_target_load(unsigned id){transaction t;char name[13];int rc;if(id>=12||!br_pp_target_ready())return PFS_INVALID;rc=begin(&t,1);if(rc)return rc;if(!allowed(&t,id,0)||!allowed(&t,12,1))return end(&t,PFS_INVALID);rc=fm1_project_adapter_load(&t.handle.store,id,name,t.ops,t.a);if(rc==FM1_PROJECT_ROLLBACK_FAILED)fault=1;if(!rc){current_slot=(int)id;if(working(&t))rc=FM1_PROJECT_WORKING_FAILED;}return end(&t,rc);}
int br_pp_target_recover(int *slot){transaction t;char name[13];int rc;if(!slot||!br_pp_target_ready())return PFS_INVALID;rc=begin(&t,1);if(rc)return rc;if(!allowed(&t,12,0))return end(&t,PFS_INVALID);rc=fm1_project_adapter_load(&t.handle.store,12,name,t.ops,t.a);if(rc==PFS_EMPTY)rc=config->empty_is_safe(config->context,&t.handle.store,t.a->sector,12)==1?FM1_PROJECT_SAFE_EMPTY:PFS_INVALID;if(rc==FM1_PROJECT_ROLLBACK_FAILED)fault=1;if(!rc){current_slot=t.a->current_slot;*slot=current_slot;}return end(&t,rc);}
/* Decode the factory pattern without touching live state. The old session and
 * active snapshot survive until all preflight checks pass. The working copy
 * commits before adoption; a subsequent failure is reported as partial and
 * the previous session and active pattern are restored. */
static int rollback_new(transaction *t){return t->ops->restore(t->a->previous,FM1_PROJECT_SESSION)&&t->ops->restore_pattern(t->ops->context,t->a->previous_live);}
int br_pp_target_init_project(void){transaction t;int rc,changed=0;if(!br_pp_target_ready())return PFS_INVALID;rc=begin(&t,1);if(rc)return rc;if(!allowed(&t,12,1))return end(&t,PFS_INVALID);
 if(!t.ops->export_bank(t.ops->context,&t.a->bank,t.a->previous_live,&t.a->song)||!t.ops->capture(t.a->previous,FM1_PROJECT_SESSION)||!config->factory(config->context,t.a->session,FM1_PROJECT_SESSION)||!t.ops->validate(t.a->session,FM1_PROJECT_SESSION,t.ops->context)){rc=PFS_INVALID;goto done;}
 if(!br_pattern_session_snapshot(t.a->session,FM1_PROJECT_SESSION,t.a->live)){rc=PFS_INVALID;goto done;}
 if(br_pattern_bank_init(&t.a->bank,t.a->arena,t.a->arena_capacity)!=BR_BANK_OK){rc=PFS_INVALID;goto done;}fm1_pattern_song_init(&t.a->song);if(!t.ops->can_adopt(t.ops->context,&t.a->bank,t.a->live,&t.a->song)){rc=PFS_INVALID;goto done;}
 rc=fm1_project_adapter_save_initial(&t.handle.store,t.ops,t.a);if(rc)goto done;
 changed=1;if(!t.ops->restore(t.a->session,FM1_PROJECT_SESSION)||!t.ops->adopt(t.ops->context,&t.a->bank,t.a->live,&t.a->song)){rc=FM1_PROJECT_NEW_WORKING_ONLY;goto restore_old;}changed=0;current_slot=-1;rc=PFS_OK;goto done;
restore_old:
 if(changed&&!rollback_new(&t)){fault=1;rc=FM1_PROJECT_ROLLBACK_FAILED;}
done:
 return end(&t,rc);
}
