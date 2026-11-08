/* Replay measured 396 warm/reset and inferred physical profiles; inject faults. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/hal/fm1_vendor360_policy.h"

typedef struct {
    uint32_t r[32], p3, rst, clock_ok, core, irq, timer_ok, timeouts;
    uint32_t reads, writes, loops, failed, pending_toggle;
    uint32_t fail_read, soft_read, fail_write, soft_write, mismatch_write;
    uint32_t drop_core, enable_irq, fail_loop;
    uint32_t wr_reg[20], wr_val[20], loop_at[3]; char wr_op[20];
} mock_t;
static const uint32_t physical[11] = {0x77,0x60,0x18,0x44,0x0c,0,0,0x1e,0x05,0,0x20};
static const uint32_t reset[11] = {0x77,0x60,0x18,0x44,0x0c,0,0,0x1e,0x3d,0,0x20};
static const uint32_t warm[11] = {0xfb,0x20,0x58,0x44,0x0c,0xa5,0x96,0x1e,0x3d,0,0};
static const uint32_t after[11] = {0xfb,0x20,0x58,0x74,0x0f,0xa5,0x96,0x1f,0x3d,0,0};
static void setup(mock_t *m, unsigned profile)
{
    const uint32_t *src = profile == 0 ? physical : profile == 1 ? reset : warm;
    unsigned i; memset(m,0,sizeof *m);
    for (i=0;i<32;i++) m->r[i]=0x80u+i;
    for (i=0;i<11;i++) m->r[fm1_vp_reg(i)]=src[i];
    m->p3=profile==0?1u:0x40u; m->rst=profile==2?0x20u:1u;
    m->clock_ok=m->core=m->irq=m->timer_ok=1u;
}
static int rd(void *p,uint32_t reg,uint32_t *out)
{
    mock_t *m=p; assert(!m->failed && m->core && m->irq && reg<32u);
    ++m->reads;
    if (m->fail_read==m->reads) { ++m->timeouts;m->failed=1;return 0; }
    if (m->soft_read==m->reads) { ++m->timeouts;m->failed=1;*out=m->r[reg];return 1; }
    /* Volatile pending may change after the 11-register entry snapshot. */
    if (m->pending_toggle && m->reads>11u && reg==0x11u) m->r[reg]^=0x80u;
    *out=m->r[reg]; return 1;
}
static int writeop(void *p,uint32_t reg,uint32_t value,char op)
{
    mock_t *m=p; uint32_t n;
    assert(!m->failed && m->core && m->irq && m->writes<20u);
    n=m->writes++; m->wr_reg[n]=reg; m->wr_val[n]=value; m->wr_op[n]=op;
    if (reg==0x11u) {
        uint32_t pending=m->r[reg]&0x80u;
        if (op=='o') {
            if (value&0x40u) pending=0u; /* clear pulse self-clears */
            m->r[reg]=(m->r[reg]&0x3fu)|(value&0x3fu)|pending;
        } else {
            assert(op=='w'); if (value&0x40u) pending=0u;
            m->r[reg]=(value&0x3fu)|pending;
        }
    } else { assert(op=='w');m->r[reg]=value; }
    if (m->mismatch_write==m->writes) m->r[reg]^=(reg==0x11u?0x02u:0x01u);
    if (m->fail_write==m->writes || m->soft_write==m->writes) {
        ++m->timeouts;m->failed=1;return m->soft_write==m->writes;
    }
    if (m->drop_core==m->writes)m->core=0u;
    if (m->enable_irq==m->writes)m->irq=0u;
    return 1;
}
static int wr(void *p,uint32_t r,uint32_t v){return writeop(p,r,v,'w');}
static int hw_or(void *p,uint32_t r,uint32_t v){assert(r==0x11u&&(v==0x40u||v==1u));return writeop(p,r,v,'o');}
static int delay(void *p){mock_t *m=p;assert(!m->failed&&m->core&&m->irq&&m->loops<3u);m->loop_at[m->loops++]=m->writes;if(m->fail_loop==m->loops){m->failed=1;return 0;}return 1;}
static int held(void *p){return ((mock_t*)p)->core!=0u;}
static int masked(void *p){return ((mock_t*)p)->irq!=0u;}
static uint32_t timeouts(void *p){return ((mock_t*)p)->timeouts;}
static int eligible(void *p,const fm1_vp_sample_t *s){mock_t *m=p;return m->clock_ok&&fm1_vp_entry_profile_eligible(m->p3,m->rst,s);}
static int timer_ready(void *p){return ((mock_t*)p)->timer_ok!=0u;}
static fm1_vp_io_t ops(mock_t *m){fm1_vp_io_t io={m,rd,wr,delay,held,masked,timeouts,eligible,hw_or,timer_ready};return io;}

int main(void)
{
    static const uint32_t regs[20]={0x11,0x11,0x11,0,0,0,1,5,6,7,8,9,5,6,5,0,0x16,2,0x17,9};
    static const uint32_t vals[17]={0x77,0x7f,0x7b,0x20,0x40,0x0c,0xa5,0x96,0x1e,
                                     0x44,0x0f,0x74,0xfb,0,0x58,0,0x1f};
    static const char op[20]={'o','w','o','w','w','w','w','w','w','w','w','w','w','w','w','w','w','w','w','w'};
    mock_t m; fm1_vp_io_t io; fm1_vp_report_t report; fm1_vp_sample_t sample;
    unsigned p,i,nreads=0; assert(sizeof(report)==112u);
    assert(fm1_vp_clock_profile_eligible(0x10200u,0x1c1u,7u,0x0940022bu,0x075030c4u));
    assert(fm1_vp_clock_profile_eligible(0x10200u,0x1c1u,0x107u,0x0940022bu,0x075030c4u));
    assert(!fm1_vp_clock_profile_eligible(0x10200u,0x1c1u,6u,0x0940022bu,0x0750310cu));
    assert(!fm1_vp_clock_profile_eligible(0x20200u,0x1c1u,7u,0x0940022bu,0x075030c4u));
    assert(!fm1_vp_clock_profile_eligible(0x10200u,0x1c0u,7u,0x0940022bu,0x075030c4u));
    assert(!fm1_vp_clock_profile_eligible(0x10200u,0x1c1u,6u,0x0940022bu,0x075030c4u));
    assert(!fm1_vp_clock_profile_eligible(0x10200u,0x1c1u,7u,0x0940022au,0x075030c4u));
    assert(!fm1_vp_clock_profile_eligible(0x10200u,0x1c1u,7u,0x0940022bu,0x0750310cu));
    for(p=0;p<3;p++) {
        setup(&m,p); for(i=0;i<11;i++)sample.raw[i]=m.r[fm1_vp_reg(i)];
        assert(fm1_vp_entry_profile_eligible(m.p3,m.rst,&sample));
        for(i=0;i<11;i++){sample.raw[i]^=1u;assert(!fm1_vp_entry_profile_eligible(m.p3,m.rst,&sample));sample.raw[i]^=1u;}
        if(p==0)assert(!fm1_vp_entry_profile_eligible(0x40u,1u,&sample));
        if(p==1)assert(!fm1_vp_entry_profile_eligible(1u,1u,&sample));
        for(i=0;i<11;i++){
            mock_t changed;
            fm1_vp_io_t changed_io;
            setup(&changed,p);changed.r[fm1_vp_reg(i)]^=1u;changed_io=ops(&changed);
            assert(fm1_vp_run(&changed_io,0u,&report)==FM1_VP_INELIGIBLE);
            assert(changed.writes==0u);
        }
        io=ops(&m);assert(fm1_vp_run(&io,0u,&report)==FM1_VP_OK);
        assert(m.writes==20u&&report.write_count==20u&&m.loops==3u&&!m.timeouts);
        assert(!memcmp(m.wr_reg,regs,sizeof regs)&&!memcmp(m.wr_op,op,sizeof op));
        assert(m.wr_val[0]==0x40u&&m.wr_val[1]==0x3du&&m.wr_val[2]==1u);
        for(i=0;i<17;i++) {
            uint32_t expected=vals[i];
            if(p==2 && i<3) expected=i==2?0xfbu:0xffu;
            assert(m.wr_val[i+3u]==expected);
        }
        assert(m.loop_at[0]==4u&&m.loop_at[1]==5u&&m.loop_at[2]==6u);
        assert(!memcmp(report.after.raw,after,sizeof after));
        assert(report.last_step==20u&&report.last_reg==9u&&report.last_confirmed==0x1fu);
        if(!p)nreads=m.reads;
    }
    /* A pending event can change asynchronously; control bits 6/7 are not
       asserted to latch, while the six selector/enable bits still match. */
    setup(&m,0);m.pending_toggle=1;io=ops(&m);
    assert(fm1_vp_run(&io,0u,&report)==FM1_VP_OK&&m.writes==20u);
    assert((report.after.raw[8]&0x3fu)==0x3du);
    setup(&m,0);m.r[0x11u]|=0x80u;io=ops(&m);
    assert(fm1_vp_run(&io,0u,&report)==FM1_VP_INELIGIBLE&&!m.writes);
    setup(&m,0);m.clock_ok=0;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_INELIGIBLE&&!m.writes);
    /* Observed after 1503 OTA: warm register values, but a mixed reset tuple.
     * The existing policy must reject it before changing any supply setting. */
    setup(&m,2);m.p3=1u;io=ops(&m);
    assert(fm1_vp_run(&io,0,&report)==FM1_VP_INELIGIBLE && !m.writes);
    setup(&m,0);m.timer_ok=0;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_WAIT_ERROR&&!m.writes&&!m.loops);
    setup(&m,0);m.r[0]=0x7fu;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_UNSAFE_MODE&&!m.writes);
    setup(&m,0);m.core=0;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_CORE_NOT_HELD&&!m.reads);
    setup(&m,0);m.irq=0;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_IRQ_NOT_DISABLED&&!m.reads);
    setup(&m,0);io=ops(&m);assert(fm1_vp_run(&io,1,&report)==FM1_VP_PREVIOUS_TIMEOUT&&!m.reads);
    for(i=1;i<=nreads;i++) {
        setup(&m,0);m.fail_read=i;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_READ_ERROR&&report.timeouts==1u);
        setup(&m,0);m.soft_read=i;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_READ_ERROR&&report.timeouts==1u);
    }
    for(i=1;i<=20;i++) {
        setup(&m,0);m.fail_write=i;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_WRITE_ERROR&&m.writes==i&&report.write_count==i&&report.last_step==i&&report.last_confirmed==FM1_VP_UNKNOWN);
        setup(&m,0);m.soft_write=i;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_WRITE_ERROR&&m.writes==i&&report.last_confirmed==FM1_VP_UNKNOWN);
        setup(&m,0);m.drop_core=i;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_CORE_NOT_HELD&&m.writes==i);
        setup(&m,0);m.enable_irq=i;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_IRQ_NOT_DISABLED&&m.writes==i);
        setup(&m,0);m.mismatch_write=i;io=ops(&m);
        if(i==9u)assert(fm1_vp_run(&io,0,&report)==FM1_VP_STATE_CHANGED&&m.writes==13u);
        else assert(fm1_vp_run(&io,0,&report)==FM1_VP_READBACK_ERROR&&m.writes==(i>=10u&&i<=12u?14u:i));
    }
    for(i=1;i<=3;i++){setup(&m,0);m.fail_loop=i;io=ops(&m);assert(fm1_vp_run(&io,0,&report)==FM1_VP_WAIT_ERROR&&m.loops==i&&m.writes==i+3u);}
    printf("PASS: measured 396 warm/reset and inferred physical profiles, 20 ordered writes, VLVD pending, %u read faults and all write/guard/delay faults\n",nreads);
    return 0;
}
