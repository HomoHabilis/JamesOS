#include "pattern_fixed_target.h"
#include <stdint.h>
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b; return an && bn && (x<=y ? y-x<an : x-y<bn); }
#if defined(FM1_DEVICE) && FM1_DEVICE && defined(FM1_PATTERN_FIXED_STORAGE_QUALIFIED) && FM1_PATTERN_FIXED_STORAGE_QUALIFIED
#error "Fixed pattern target storage remains unqualified"
#endif
int fm1_fixed_target_bind(fm1_fixed_target *target,const fm1_fixed_target_config *cfg)
{
    if(!target || target->config || target->fault) return PFS_INVALID;
#if defined(FM1_DEVICE) && FM1_DEVICE
    (void)cfg; return PFS_INVALID;
#else
    if(!cfg || !cfg->bridge || !cfg->bridge->ops || cfg->bridge->fault ||
       cfg->bridge->ops->named_slots!=12u || !cfg->recovery_authorized ||
       !cfg->current_slot || !cfg->scan) return PFS_INVALID;
    target->config=cfg; target->recovered=0; return PFS_OK;
#endif
}
int fm1_fixed_target_ready(const fm1_fixed_target *target)
{ return target && target->config && !target->fault && !target->config->bridge->fault; }
int fm1_fixed_target_scan(fm1_fixed_target *target,unsigned slot,char name[PS_NAME_BYTES])
{ if(!fm1_fixed_target_ready(target) || !target->recovered || slot>=12u || !name) return PFS_INVALID; return target->config->scan(target->config->context,slot,name); }
int fm1_fixed_target_save(fm1_fixed_target *target,unsigned slot,const char *name)
{ if(!fm1_fixed_target_ready(target) || !target->recovered || slot>=12u) return PFS_INVALID; return fm1_fixed_bridge_save(target->config->bridge,slot,name); }
int fm1_fixed_target_load(fm1_fixed_target *target,unsigned slot)
{ if(!fm1_fixed_target_ready(target) || !target->recovered || slot>=12u) return PFS_INVALID; return fm1_fixed_bridge_load(target->config->bridge,slot); }
int fm1_fixed_target_recover(fm1_fixed_target *target,int *slot)
{
    int rc, current;
    if(!fm1_fixed_target_ready(target) || !slot || target->recovered ||
       overlap(slot,sizeof *slot,target,sizeof *target) ||
       overlap(slot,sizeof *slot,target->config,sizeof *target->config) ||
       overlap(slot,sizeof *slot,target->config->bridge,sizeof *target->config->bridge) ||
       overlap(slot,sizeof *slot,target->config->bridge->ops,sizeof *target->config->bridge->ops)) return PFS_INVALID;
    if(target->config->recovery_authorized(target->config->context)!=1) return FM1_PROJECT_BUSY;
    rc=fm1_fixed_bridge_recover(target->config->bridge);
    if(rc!=PFS_OK) return rc;
    if(target->config->current_slot(target->config->context,&current)!=1 || current < -1 || current >= 12) {
        target->fault=1; return FM1_PROJECT_ROLLBACK_FAILED;
    }
    *slot=current; target->recovered=1; return PFS_OK;
}
