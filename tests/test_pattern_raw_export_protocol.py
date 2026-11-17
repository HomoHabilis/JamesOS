#!/usr/bin/env python3
"""Compile the real diagnostic import handler with bounded fake storage."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'firmware/src/instrument_diagnostic.c').read_text()
start=s.index('    if (cmd==0xdbu && !n) { reply[0]')
end=s.index('    if (br_import_state && (uint32_t)(fm1_ms-br_import_last_ms)',start)
timeout=s.index('    if (br_export_nonce && (uint32_t)(fm1_ms-br_export_last_ms)')
handler=s[timeout:s.index('\n',timeout)+1]+s[start:end]
preamble=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "pattern_sha256.h"
#define FM1_PATTERN_RAW_EXPORT_EXPERIMENT 1
#define PS_NAME_BYTES 13u
#define PPS_MAX_PAYLOAD 3584u
#define PFS_OK 0
#define PFS_INVALID -1
static uint32_t fm1_ms, br_import_nonce,br_import_length,br_import_slot,br_import_state,br_import_last_ms;
static int br_import_result,br_ps_target_fixed_runtime, calls;
static uint32_t br_export_nonce,br_export_slot,br_export_length,br_export_last_ms,br_export_closed_nonce; static unsigned br_export_raw; static char br_export_name[13]; static uint8_t br_export_digest[32];
static struct { uint8_t state, old; char name[13]; } es_directory[12];
static struct { unsigned redraw; } br_ui;
static struct { uint8_t b[3584]; } br_diagnostic_session;
static uint32_t br_diagnostic_snapshot_valid,br_diagnostic_upload_length,br_diagnostic_chunks[4];
static void br_diagnostic_word(uint8_t *p,uint32_t x){for(int i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static int fm1_fixed_runtime_export(void *p,uint32_t slot,uint8_t *b,size_t cap,uint32_t *n,char *name){(void)p;(void)cap;calls++;if(slot==2)return -1;*n=33;memset(b,0x41,33);memset(name,0,13);name[0]='T';return 0;}
static int fm1_fixed_runtime_export_raw(void *p,uint32_t slot,uint8_t *b,size_t cap,uint32_t *n,char *name){ int x=fm1_fixed_runtime_export(p,slot,b,cap,n,name); if(!x)b[0]=0x77; return x;}
static int command(uint32_t cmd,const uint8_t *body,uint32_t n,uint8_t *reply,uint32_t *reply_n){
'''
post=r'''
return 0; }
int main(void){
 uint8_t b[64]={1,2,3,4,1,0},reply[128],first[55];uint32_t rn;
 br_diagnostic_upload_length=32;assert(!command(0xe4,b,5,reply,&rn));assert(br_diagnostic_upload_length==32&&!calls);br_diagnostic_upload_length=0;
 assert(command(0xe4,0,0,reply,&rn)&&rn==4&&!memcmp(reply,"PRX\1",4));
 assert(command(0xe4,b,5,reply,&rn)&&rn==55&&calls==1);memcpy(first,reply,55);
 assert(command(0xe4,b,5,reply,&rn)&&calls==1&&!memcmp(first,reply,55));
 assert(!command(0xdc,b,5,reply,&rn));assert(!command(0xd6,b,20,reply,&rn));
 b[4]=0;assert(command(0xdd,b,6,reply,&rn)&&rn==38&&reply[6]==0x77);
 b[0]=9;assert(!command(0xdd,b,6,reply,&rn));b[0]=1;
 assert(command(0xde,b,4,reply,&rn));assert(command(0xde,b,4,reply,&rn));
 b[4]=2;assert(command(0xe4,b,5,reply,&rn)&&rn==8&&!br_export_nonce);
 b[4]=1;assert(command(0xe4,b,5,reply,&rn));fm1_ms=120000;b[4]=0;assert(!command(0xdd,b,6,reply,&rn));assert(!br_export_nonce);
 puts("raw export protocol: capability, retry, isolation, close and timeout passed");return 0; }
'''

with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'test.c';p.write_text(preamble+handler+post)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-variable', '-Wno-unused-function', '-Werror','-I'+str(root/'firmware/src'),str(p),str(root/'firmware/src/pattern_sha256.c'),'-o',str(Path(td)/'test')],check=True)
 subprocess.run([str(Path(td)/'test')],check=True)
