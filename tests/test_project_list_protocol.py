#!/usr/bin/env python3
"""Test actual list dispatch and timeout preamble without a device."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
s = (root / 'firmware/src/instrument_diagnostic.c').read_text()
start = s.index('    if (br_import_state!=0u && (uint32_t)(fm1_ms-br_import_last_ms)')
end = s.index('    if (cmd==0xdbu && !n)', start)
source = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define PS_NAME_BYTES 13u
#define FM1_PROJECT_LIST_PAGE 6u
#define FM1_PROJECT_BUSY (-13)
#define PFS_OK 0
typedef struct { uint8_t state; char name[13]; } fm1_project_list_entry;
static uint32_t br_import_state,br_import_nonce,br_import_length,br_import_last_ms,br_export_nonce,br_export_length,br_export_last_ms,fm1_ms;
static uint32_t br_diagnostic_chunks[4]; static int br_ps_target_fixed_runtime, calls;
static int fm1_fixed_runtime_list(void *r,uint32_t first,fm1_project_list_entry *out){(void)r;(void)first;calls++;memset(out,0,6*sizeof *out);out[0].state=1;memcpy(out[0].name,"HELLO",5);return 0;}
static int command(uint32_t cmd,const uint8_t *body,uint32_t n,uint8_t *reply,uint32_t *reply_n){uint32_t i;
''' + s[start:end] + r'''
(void)cmd;(void)body;(void)n;(void)reply;(void)reply_n;(void)i;return 0;}
int main(void){uint8_t b[1]={0}, reply[128];uint32_t n;
 assert(command(0xdf,0,0,reply,&n)&&n==4&&!memcmp(reply,"PLS\1",4));
 br_import_state=1;br_import_last_ms=1;fm1_ms=2;
 assert(command(0xdf,b,1,reply,&n)&&n==86&&reply[1]==1&&!calls);
 fm1_ms=120001;assert(command(0xdf,b,1,reply,&n)&&n==86&&reply[1]==0&&reply[2]==1&&!memcmp(reply+3,"HELLO",5)&&calls==1&&!br_import_state);
 br_export_nonce=1;br_export_last_ms=fm1_ms;assert(command(0xdf,b,1,reply,&n)&&reply[1]==1);
 fm1_ms+=120000;assert(command(0xdf,b,1,reply,&n)&&reply[1]==0&&!br_export_nonce);
 for(unsigned c=0;c<7;c++){static const unsigned commands[]={0x63,0x64,0x65,0xd6,0xda,0xdc,0xde};br_import_state=1;br_import_last_ms=0;fm1_ms=120000;assert(!command(commands[c],b,1,reply,&n)&&!br_import_state);}
 b[0]=3;assert(!command(0xdf,b,1,reply,&n));puts("project list protocol and expiry passed");}
'''
with tempfile.TemporaryDirectory() as td:
    path = Path(td) / 'test.c'
    path.write_text(source)
    binary = Path(td) / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
