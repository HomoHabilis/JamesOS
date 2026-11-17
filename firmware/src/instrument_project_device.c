/* Experimental adapter: included only after the platform and session code.
 * It is not wired into the application yet; see SAVE-SCREEN-DESIGN.md.
 * SMK-37 Pro device: the project partition is hal/smk37/smk37_flash_map.h's (logical offsets,
 * translated around two occupied sectors by the flash layer); its host build keeps FM-1's. */
#include "../product.h"
#include "project_store.h"
#include "project_store.c"

#if BR_PRODUCT_SMK37 && defined(FM1_DEVICE)
#define BR_PROJECT_BASE SMK37_PROJECT_LO
#define BR_PROJECT_END SMK37_PROJECT_HI
#else
#define BR_PROJECT_BASE 0x00097000u
#define BR_PROJECT_END  0x000B1000u
#endif
_Static_assert(BR_PROJECT_END-BR_PROJECT_BASE==PS_TOTAL_SLOTS*2u*PS_SECTOR,
               "project partition must hold two sectors per slot and working state");
_Static_assert(sizeof(br_session_t)<=PS_MAX_PAYLOAD, "session must fit a project copy");

static int br_project_read(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    (void)ctx;
    if (!flash_ok || !FL_IN(off,n,BR_PROJECT_BASE,BR_PROJECT_END)) return -1;
    return st_read(off,dst,n);
}
static int br_project_erase(void *ctx, uint32_t off)
{
    uint32_t took; int rc, resumed; (void)ctx;
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    /* Pattern storage owns no proven writable sector yet. Legacy project
     * callbacks must not erase while its authority policy is unqualified. */
    (void)off; return -1;
#endif
    if (!flash_ok || (off & (PS_SECTOR-1u)) ||
        !FL_IN(off,PS_SECTOR,BR_PROJECT_BASE,BR_PROJECT_END)) return -1;
    if (flash_worker_park()) return -1;
    rc=fl_erase4k(off,&took); resumed=flash_worker_resume();
    return rc || resumed ? -1 : 0;
}
static int br_project_program(void *ctx, uint32_t off, const void *src, uint32_t n)
{
    int rc, resumed; (void)ctx;
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    /* Block every legacy project write, including implicit working saves. */
    (void)off; (void)src; (void)n; return -1;
#endif
    if (!flash_ok || !n || !FL_IN(off,n,BR_PROJECT_BASE,BR_PROJECT_END)) return -1;
    if (flash_worker_park()) return -1;
    rc=fl_write(off,(const uint8_t *)src,n); resumed=flash_worker_resume();
    return rc || resumed ? -1 : 0;
}
static int br_project_valid(const void *payload, uint32_t n)
{
    return n==sizeof(br_session_t) && br_session_valid((const br_session_t *)payload);
}
static const ps_store br_project_store={0,br_project_read,br_project_erase,
    br_project_program,br_project_valid,BR_PROJECT_BASE,sizeof(br_session_t)};

/* The extra record makes the last successfully selected project recoverable
 * at boot without touching the legacy session sector. W00 means unassigned. */
static void br_project_work_name(char name[4], int slot)
{
    unsigned n=slot<0?0u:(unsigned)slot+1u;
    name[0]='W'; name[1]=(char)('0'+n/10u); name[2]=(char)('0'+n%10u); name[3]=0;
}
static int br_project_work_slot(const char *name)
{
    unsigned n;
    if(name[0]!='W' || name[1]<'0' || name[1]>'9' || name[2]<'0' || name[2]>'9' || name[3]) return -2;
    n=(unsigned)(name[1]-'0')*10u+(unsigned)(name[2]-'0');
    return n<=PS_SLOTS ? (int)n-1 : -2;
}
static int br_project_work_write(const br_session_t *session, int slot)
{
    char name[4]; br_project_work_name(name,slot);
    return ps_save_reclaim(&br_project_store,PS_SLOTS,name,session,&br_loaded,0);
}

/* Callers must be prepared for BUSY/IO/CORRUPT and must not advance a
 * pending LOAD or INIT unless a preceding save returns PS_OK. */
static int br_project_save(uint32_t slot, const char *name, int reclaim, ps_info *info)
{
    int rc;
    if (slot>=PS_SLOTS || br_playing || !br_engine_quiet() || !br_fx_quiet() || br_session_save_blocked()) return PS_INVALID;
    if (!br_quiescent_transaction_begin()) return PS_IO;
    if (!br_capture(&br_pending_save) || br_engine_pipeline_uncontained_fault_get()) rc=PS_INVALID;
    else {
        rc=reclaim ? ps_save_reclaim(&br_project_store,slot,name,&br_pending_save,&br_loaded,info) :
            ps_save(&br_project_store,slot,name,&br_pending_save,&br_loaded,info);
        if(rc==PS_OK) rc=br_project_work_write(&br_pending_save,(int)slot);
    }
    br_quiescent_transaction_end();
    return rc;
}
static int br_project_load(uint32_t slot, ps_info *info)
{
    int rc;
    if (slot>=PS_SLOTS || br_playing || !br_engine_quiet() || !br_fx_quiet() || br_session_save_blocked()) return PS_INVALID;
    if (!br_quiescent_transaction_begin()) return PS_IO;
    rc=ps_load(&br_project_store,slot,&br_loaded,info);
    if (rc==PS_OK) {
        br_pending_save=br_loaded;
        rc=br_project_work_write(&br_pending_save,(int)slot);
        if(rc==PS_OK && (!br_restore(&br_pending_save) || br_engine_pipeline_uncontained_fault_get())) rc=PS_INVALID;
    }
    br_quiescent_transaction_end();
    return rc;
}
static int br_project_init(const br_session_t *factory)
{
    int rc;
    if (!factory || br_playing || !br_engine_quiet() || !br_fx_quiet() || br_session_save_blocked()) return PS_INVALID;
    if (!br_quiescent_transaction_begin()) return PS_IO;
    rc=br_project_work_write(factory,-1);
    if(rc==PS_OK && (!br_restore(factory) || br_engine_pipeline_uncontained_fault_get())) rc=PS_INVALID;
    br_quiescent_transaction_end();
    return rc;
}
/* Called during startup's existing quiescent transaction after legacy load. */
static int br_project_recover(int *slot)
{
    ps_info info; int rc, n;
    if(br_session_save_blocked()) return PS_INVALID;
    rc=ps_load(&br_project_store,PS_SLOTS,&br_loaded,&info);
    if(rc!=PS_OK) return rc;
    n=br_project_work_slot(info.name);
    if(n==-2 || !br_restore(&br_loaded)) return PS_CORRUPT;
    if(slot)*slot=n;
    return PS_OK;
}
