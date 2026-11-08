/* Bounded pre-BSS transition and post-fault telemetry for the 360 candidate. */
#pragma once
#include <stdint.h>
#include "fm1_fault_diag.h"
#include "fm1_power_read_diag.h"
#include "fm1_vendor360_policy.h"
#define FM1_VPD_READY 0x56504431u
#define FM1_VPD_ATTEMPT_MAGIC 0x56504131u
#define FM1_VPD_FAULT_MAGIC 0x56504631u

typedef struct {
    uint32_t magic,version,byte_size,sequence,boot_id,phase,status,p3,rst_src;
    uint32_t prior_error,entry_timeouts,clock_source,entry[FM1_POWER_READ_COUNT],checksum;
} fm1_vpd_attempt_t;
typedef struct {
    uint32_t magic,version,byte_size,epoch,owner;
    fm1_vp_report_t report;
    uint32_t checksum;
} fm1_vpd_fault_t;
typedef char fm1_vpd_attempt_size[(sizeof(fm1_vpd_attempt_t)==96u)?1:-1];
typedef char fm1_vpd_fault_size[(sizeof(fm1_vpd_fault_t)==136u)?1:-1];
extern volatile fm1_vpd_attempt_t fm1_vpd_attempts[2];
extern volatile fm1_vpd_fault_t fm1_vpd_fault;
extern volatile fm1_vp_report_t fm1_vpd_report;
extern volatile uint32_t fm1_vpd_ready;
void fm1_vpd_fault_capture_owned(uint32_t epoch,uint32_t cpu);

static inline uint32_t fm1_vpd_hash(const volatile uint32_t *p,uint32_t count)
{
    uint32_t h=2166136261u,i;
    for(i=0u;i<count;i++)h=(h^p[i])*16777619u;
    return h;
}
static inline uint32_t fm1_vpd_attempt_valid(const volatile fm1_vpd_attempt_t *r)
{
    const volatile uint32_t *p=(const volatile uint32_t *)r;
    return r && r->magic==FM1_VPD_ATTEMPT_MAGIC && r->version==1u &&
        r->byte_size==sizeof(*r) && r->sequence && r->sequence!=0xffffffffu &&
        (r->phase==1u || r->phase==2u) &&
        r->checksum==fm1_vpd_hash(p+1u,sizeof(*r)/4u-2u);
}
static inline volatile fm1_vpd_attempt_t *fm1_vpd_attempt_begin(
    volatile fm1_vpd_attempt_t slots[2],uint32_t boot_id,uint32_t p3,uint32_t rst,
    uint32_t prior_error,uint32_t source,const volatile fm1_power_read_sample_t *entry,
    uint32_t *expected_sequence)
{
    uint32_t v0=fm1_vpd_attempt_valid(&slots[0]),v1=fm1_vpd_attempt_valid(&slots[1]);
    uint32_t latest=0u,index=0u,seq=1u,i;volatile fm1_vpd_attempt_t *r;
    volatile uint32_t *p;
    if(v0 || v1) {
        latest=!v0?1u:!v1?0u:(int32_t)(slots[1].sequence-slots[0].sequence)>0?1u:0u;
        index=latest^1u;seq=slots[latest].sequence+1u;
        if(!seq || seq==0xffffffffu)seq=1u;
    }
    *expected_sequence=seq;
    r=&slots[index];r->magic=0u;fm1_fault_ram_code_barrier();
    r->version=1u;r->byte_size=sizeof(*r);r->sequence=seq;r->boot_id=boot_id;
    r->phase=1u;r->status=0xffffffffu;r->p3=p3;r->rst_src=rst;
    r->prior_error=prior_error;r->entry_timeouts=entry->timeouts;r->clock_source=source;
    for(i=0u;i<FM1_POWER_READ_COUNT;i++)r->entry[i]=entry->raw[i];
    p=(volatile uint32_t *)r;r->checksum=fm1_vpd_hash(p+1u,sizeof(*r)/4u-2u);
    fm1_fault_ram_code_barrier();r->magic=FM1_VPD_ATTEMPT_MAGIC;fm1_fault_ram_code_barrier();
    return r;
}
static inline void fm1_vpd_attempt_finish(volatile fm1_vpd_attempt_t slots[2],
                                          volatile fm1_vpd_attempt_t *intent,uint32_t status)
{
    volatile fm1_vpd_attempt_t *done=intent==&slots[0]?&slots[1]:&slots[0];
    volatile uint32_t *d=(volatile uint32_t *)done;
    const volatile uint32_t *s=(const volatile uint32_t *)intent;uint32_t i;
    done->magic=0u;fm1_fault_ram_code_barrier();
    for(i=1u;i<sizeof(*done)/4u-1u;i++)d[i]=s[i];
    done->phase=2u;done->status=status;
    done->checksum=fm1_vpd_hash(d+1u,sizeof(*done)/4u-2u);
    fm1_fault_ram_code_barrier();done->magic=FM1_VPD_ATTEMPT_MAGIC;fm1_fault_ram_code_barrier();
}
/* Read back the complete pre-write intent: a merely attempted retained write
 * is not sufficient to protect against a reset during power sequencing. */
static inline uint32_t fm1_vpd_attempt_matches(const volatile fm1_vpd_attempt_t *r,
    uint32_t sequence,uint32_t boot_id,uint32_t p3,uint32_t rst,uint32_t prior_error,uint32_t source,
    const volatile fm1_power_read_sample_t *entry)
{
    uint32_t i;
    if(!entry || !fm1_vpd_attempt_valid(r) || r->phase!=1u || r->status!=0xffffffffu ||
       r->sequence!=sequence || r->boot_id!=boot_id || r->p3!=p3 || r->rst_src!=rst || r->prior_error!=prior_error ||
       r->clock_source!=source || r->entry_timeouts!=entry->timeouts)return 0u;
    for(i=0u;i<FM1_POWER_READ_COUNT;i++)if(r->entry[i]!=entry->raw[i])return 0u;
    return 1u;
}
static inline uint32_t fm1_vpd_completion_valid(const volatile fm1_vpd_attempt_t slots[2],
    const volatile fm1_vpd_attempt_t *intent,uint32_t status)
{
    const volatile fm1_vpd_attempt_t *done=intent==&slots[0]?&slots[1]:&slots[0];
    const volatile uint32_t *a=(const volatile uint32_t *)intent,*b=(const volatile uint32_t *)done;
    uint32_t i;
    if(!fm1_vpd_attempt_valid(intent)||intent->phase!=1u ||
       !fm1_vpd_attempt_valid(done)||done->phase!=2u||done->status!=status)return 0u;
    for(i=1u;i<sizeof(*done)/4u-1u;i++)
        if(i!=5u && i!=6u && a[i]!=b[i])return 0u;
    return 1u;
}
static inline uint32_t fm1_vpd_requires_recovery(const volatile fm1_vpd_attempt_t slots[2])
{
    uint32_t v0=fm1_vpd_attempt_valid(&slots[0]),v1=fm1_vpd_attempt_valid(&slots[1]);
    const volatile fm1_vpd_attempt_t *latest;
    if(!v0&&!v1)return 0u;
    latest=!v0?&slots[1]:!v1?&slots[0]:
        (int32_t)(slots[1].sequence-slots[0].sequence)>0?&slots[1]:&slots[0];
    if(v0&&slots[0].sequence==latest->sequence&&slots[0].phase==2u)return slots[0].status!=0u;
    if(v1&&slots[1].sequence==latest->sequence&&slots[1].phase==2u)return slots[1].status!=0u;
    return 1u;
}
static inline uint32_t fm1_vpd_attempt_chunk(const volatile fm1_vpd_attempt_t slots[2],
    uint32_t off,uint32_t n,uint8_t out[128])
{
    const volatile uint8_t *p=(const volatile uint8_t *)slots;uint32_t i;
    uint32_t valid[2]={fm1_vpd_attempt_valid(&slots[0]),fm1_vpd_attempt_valid(&slots[1])};
    if(!out||!n||n>125u||off>2u*sizeof(*slots)||n>2u*sizeof(*slots)-off)return 0u;
    out[0]=(uint8_t)off;out[1]=(uint8_t)(off>>8);out[2]=(uint8_t)n;
    for(i=0u;i<n;i++)out[3u+i]=valid[(off+i)/sizeof(*slots)]?p[off+i]:0xffu;
    return n+3u;
}
static inline uint32_t fm1_vpd_fault_hash(const volatile fm1_vpd_fault_t *r)
{ return fm1_vpd_hash((const volatile uint32_t *)r+1u,sizeof(*r)/4u-2u); }
static inline uint32_t fm1_vpd_fault_publish(volatile fm1_vpd_fault_t *r,
    const volatile fm1_fault_diag_bundle_t *b,uint32_t epoch,uint32_t cpu,
    const volatile fm1_vp_report_t *report)
{
    uint32_t e,owner,pc,i;volatile uint32_t *d;const volatile uint32_t *s;
    if(!r)return 0u;r->magic=0u;fm1_fault_ram_code_barrier();
    if(!report||!fm1_fault_ram_code_bundle_identity(b,&e,&owner,&pc)||e!=epoch||owner!=cpu)return 0u;
    r->version=1u;r->byte_size=sizeof(*r);r->epoch=epoch;r->owner=cpu;
    d=(volatile uint32_t *)&r->report;s=(const volatile uint32_t *)report;
    for(i=0u;i<sizeof(*report)/4u;i++)d[i]=s[i];
    r->checksum=fm1_vpd_fault_hash(r);fm1_fault_ram_code_barrier();
    r->magic=FM1_VPD_FAULT_MAGIC;fm1_fault_ram_code_barrier();return 1u;
}
static inline uint32_t fm1_vpd_fault_chunk(const volatile fm1_vpd_fault_t *r,
    const volatile fm1_fault_diag_bundle_t *b,uint32_t off,uint32_t n,uint8_t out[128])
{
    uint32_t e,owner,pc,i;const volatile uint8_t *p=(const volatile uint8_t *)r;
    if(!r||!out||!n||n>125u||off>sizeof(*r)||n>sizeof(*r)-off||r->magic!=FM1_VPD_FAULT_MAGIC)return 0u;
    fm1_fault_ram_code_barrier();
    if(r->version!=1u||r->byte_size!=sizeof(*r)||
       !fm1_fault_ram_code_bundle_identity(b,&e,&owner,&pc)||r->epoch!=e||r->owner!=owner||
       r->checksum!=fm1_vpd_fault_hash(r))return 0u;
    out[0]=(uint8_t)off;out[1]=(uint8_t)(off>>8);out[2]=(uint8_t)n;
    for(i=0u;i<n;i++)out[3u+i]=p[off+i];return n+3u;
}
