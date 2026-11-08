#!/usr/bin/env python3
"""Check disabled target port adapters with extracted code and mock loans."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'firmware/src/dinghy.c').read_text()
def extract(prefix):
 a=s.index(prefix);q=s.index('{',a);d=0
 for i in range(q,len(s)):
  d+=(s[i]=='{')-(s[i]=='}')
  if not d:return s[a:i+1]
 raise ValueError(prefix)
body='\n'.join(extract(x) for x in ('static int br_ps_runtime_port_begin(', 'static int br_ps_runtime_port_mode(', 'static int br_ps_runtime_port_end(', 'static int br_ps_runtime_port_fence(', 'static int br_ps_runtime_port_fault(', 'static int br_ps_runtime_port_pair(', 'static int br_ps_runtime_port_cert(', 'static int br_ps_runtime_port_activate(', 'static int br_ps_runtime_port_commit('))
shim=r'''
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef struct {int x;} fm1_project_adapter;typedef struct {int x;} fm1_project_adapter_ops;typedef struct {int x;} br_pattern_park_io;
static fm1_project_adapter a;static fm1_project_adapter_ops o;static br_pattern_park_io p;
static uint32_t br_ps_target_project_token=7;static int begin_ok=1,fault,park_ok=1,end_ok=1,ends,exclusive=1,watch_ok=1,masked,lose_fence_on_mask;static size_t cap=4096;
static struct {int current;} es;
static uint32_t irq_save(void){assert(!masked);masked=1;if(lose_fence_on_mask)exclusive=0;return 1;}
static void irq_restore(uint32_t x){assert(masked&&x==1);masked=0;}
static int br_ps_project_watch_ok_locked(void){return watch_ok;}
static int br_ps_target_project_begin(int m,fm1_project_adapter **aa,const fm1_project_adapter_ops **oo,uint32_t *t){(void)m;if(!begin_ok)return 0;*aa=&a;*oo=&o;*t=7;return 1;}
static br_pattern_park_io *br_ps_target_project_park(uint32_t t){return t==7&&park_ok?&p:0;}
static size_t br_ps_target_project_scratch_capacity(uint32_t t){return t==7?cap:0;}
static int br_ps_target_project_end(uint32_t t){assert(t==7);ends++;return end_ok;}
static int br_ps_target_project_faulted(void){return fault;}
static int br_ps_target_project_mode(uint32_t t,int m){return t==7&&m==1;}
static int br_pattern_loan_matches(uint32_t *t,uint32_t v){return v&&*t==v;}
static int br_ps_target_project_exclusive(void*x){(void)x;return exclusive;}
static int br_ps_target_parked(void*x){(void)x;return exclusive;}
'''
tail=r'''
int main(void){fm1_project_adapter *aa=0;const fm1_project_adapter_ops *oo=0;br_pattern_park_io *pp=0;uint32_t t=0;size_t n=0;unsigned state=0x55;
 assert(br_ps_runtime_port_begin(0,0,&aa,&oo,&t,&pp,&n)==1&&aa==&a&&oo==&o&&t==7&&pp==&p&&n==4096);
 aa=0;oo=0;t=0;pp=0;n=0;park_ok=0;assert(br_ps_runtime_port_begin(0,0,&aa,&oo,&t,&pp,&n)==0&&ends==1&&!aa&&!oo&&!t&&!pp&&!n);
 end_ok=0;assert(br_ps_runtime_port_begin(0,0,&aa,&oo,&t,&pp,&n)==-1&&ends==2&&!aa&&!oo&&!t&&!pp&&!n);
 begin_ok=0;fault=1;assert(br_ps_runtime_port_begin(0,0,&aa,&oo,&t,&pp,&n)==-1);fault=0;assert(br_ps_runtime_port_begin(0,0,&aa,&oo,&t,&pp,&n)==0);
 assert(br_ps_runtime_port_mode(0,7,1)&&!br_ps_runtime_port_mode(0,8,1));
 assert(br_ps_runtime_port_fence(0,7)&&!br_ps_runtime_port_fence(0,8));exclusive=0;assert(!br_ps_runtime_port_fence(0,7));
 assert(!br_ps_runtime_port_pair(0,7,0,1,&state)&&state==0x55);
 assert(!br_ps_runtime_port_cert(0,7,0,0,0)&&!br_ps_runtime_port_activate(0,7,0));
 exclusive=1;es.current=4;assert(!br_ps_runtime_port_commit(0,8,2)&&es.current==4);
 assert(!br_ps_runtime_port_commit(0,7,12)&&es.current==4);
 watch_ok=0;assert(!br_ps_runtime_port_commit(0,7,2)&&es.current==4);watch_ok=1;
 lose_fence_on_mask=1;assert(!br_ps_runtime_port_commit(0,7,2)&&es.current==4);lose_fence_on_mask=0;exclusive=1;
 assert(br_ps_runtime_port_commit(0,7,-1)&&es.current==-1&&!masked);
 puts("target port loan, denied authority callbacks and fenced slot commit passed");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='fm1-runtime-port-') as d:
 pth=Path(d);(pth/'probe.c').write_text(shim+body+tail)
 subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined',str(pth/'probe.c'),'-o',str(pth/'probe')],check=True)
 subprocess.run([str(pth/'probe')],check=True)
