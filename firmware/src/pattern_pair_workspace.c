#include "pattern_pair_workspace.h"
#include <string.h>
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b; return an && bn && (x<=y ? y-x<an : x-y<bn); }
int pps_workspace_acquire(pps_workspace *w,void *buffer,size_t size,const pps_workspace_range *excluded,size_t count,pps_workspace_fence fence,void *context,uint32_t token)
{
    uint8_t *p=buffer; size_t i;
    if(!w || w->active || w->retired || !p || size<PPS_WORKSPACE_TOTAL || !fence || !token || count>32u ||
       (count && !excluded) || overlap(w,sizeof *w,p,PPS_WORKSPACE_TOTAL) ||
       overlap(w,sizeof *w,excluded,count*sizeof *excluded) ||
       overlap(p,PPS_WORKSPACE_TOTAL,excluded,count*sizeof *excluded)) return 0;
    for(i=0;i<count;i++) if((excluded[i].length && !excluded[i].address) ||
       overlap(p,PPS_WORKSPACE_TOTAL,excluded[i].address,excluded[i].length) ||
       overlap(w,sizeof *w,excluded[i].address,excluded[i].length)) return 0;
    if(fence(context,token)!=1) return 0;
    w->encoded=p;w->writer_page=p+PPS_WORKSPACE_ENCODED;
    w->owner_page=w->writer_page+PPS_WORKSPACE_PAGE;w->context=context;
    w->fence=fence;w->token=token;w->active=1;return 1;
}
int pps_workspace_valid(const pps_workspace *w,uint32_t token)
{ return w && w->active && token && token==w->token && w->fence && w->fence(w->context,token)==1; }
int pps_workspace_release(pps_workspace *w,uint32_t token)
{ if(!pps_workspace_valid(w,token)) return 0;memset(w,0,sizeof *w);w->retired=1;return 1; }
