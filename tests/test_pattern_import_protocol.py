#!/usr/bin/env python3
"""Compile the real diagnostic import handler with bounded fake storage."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'firmware/src/instrument_diagnostic.c').read_text()
start=s.index('    if (br_import_state && (uint32_t)(fm1_ms-br_import_last_ms)')
end=s.index('\n#endif\n#if BR_UI_TIMING_DIAG',start)
handler=s[start:end]
preamble=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "pattern_sha256.h"
#define PS_NAME_BYTES 13u
#define PPS_MAX_PAYLOAD 3584u
#define PFS_OK 0
#define PFS_INVALID -1
static uint32_t fm1_ms, br_import_nonce,br_import_length,br_import_slot,br_import_state,br_import_last_ms;
static int br_import_result,br_ps_target_fixed_runtime, calls;
static uint8_t br_import_digest[32]; static char br_import_name[13];
static struct { uint8_t state, old; char name[13]; } es_directory[12];
static struct { unsigned redraw; } br_ui;
static struct { uint8_t b[3584]; } br_diagnostic_session;
static uint32_t br_diagnostic_snapshot_valid,br_diagnostic_upload_length,br_diagnostic_chunks[4];
static void br_diagnostic_word(uint8_t *p,uint32_t x){for(int i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static int fm1_fixed_runtime_import(void *p,uint32_t slot,const char *name,const uint8_t *b,uint32_t n){(void)p;(void)slot;(void)name;(void)b;(void)n;calls++;return 0;}
static int command(uint32_t cmd,const uint8_t *body,uint32_t n,uint8_t *reply,uint32_t *reply_n){
'''
post=r'''
return 0; }
int main(void){
 uint8_t b[64]={1,2,3,4,1,33,0,'T',0},reply[128],sum[32];uint32_t rn;fm1_pattern_sha256 sha;
 es_directory[0].state=1;es_directory[1].state=2;es_directory[2].state=3;
 assert(command(0xd6,b,20,reply,&rn)&&rn==7);assert(br_import_state==1);
 assert(!command(0x63,b,3,reply,&rn));
 b[4]=0;b[5]=0;memset(b+6,0x41,32);assert(command(0xd7,b,38,reply,&rn));
 b[6]^=1;assert(!command(0xd7,b,38,reply,&rn));b[6]^=1;
 assert(!command(0xd8,b,36,reply,&rn));assert(calls==0);
 b[4]=32;b[5]=0;b[6]=0x42;assert(command(0xd7,b,7,reply,&rn));
 assert(!fm1_pattern_sha256_init(&sha));assert(!fm1_pattern_sha256_update(&sha,br_diagnostic_session.b,33));assert(!fm1_pattern_sha256_final(&sha,sum));
 memcpy(b+4,sum,32);assert(command(0xd8,b,36,reply,&rn)&&calls==1&&br_import_state==2);assert(es_directory[0].state==1&&es_directory[1].state==0&&es_directory[2].state==3&&br_ui.redraw==1);es_directory[1].state=1;br_ui.redraw=0;assert(command(0xd8,b,36,reply,&rn)&&calls==1&&es_directory[1].state==1&&!br_ui.redraw);
 assert(command(0xd9,b,4,reply,&rn)&&reply[4]==2);fm1_ms=120000;assert(!command(0x63,b,3,reply,&rn));assert(br_import_state==0);
 b[4]=1;b[5]=1;b[6]=0;b[7]='T';memset(b+8,0,12);assert(command(0xd6,b,20,reply,&rn));b[4]=0;b[5]=0;b[6]=3;assert(command(0xd7,b,7,reply,&rn));memset(b+4,0,32);assert(command(0xd8,b,36,reply,&rn)&&br_import_state==3&&calls==1&&es_directory[1].state==1&&!br_ui.redraw);assert(command(0xd8,b,36,reply,&rn)&&calls==1);assert(command(0xda,b,4,reply,&rn)&&!br_import_state);
 puts("import protocol: ownership, chunks, SHA, result latch, expiry and cancel passed");return 0; }
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'test.c';p.write_text(preamble+handler+post)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(root/'firmware/src'),str(p),str(root/'firmware/src/pattern_sha256.c'),'-o',str(Path(td)/'test')],check=True)
 subprocess.run([str(Path(td)/'test')],check=True)
