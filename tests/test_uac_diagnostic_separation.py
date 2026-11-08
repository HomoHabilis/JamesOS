#!/usr/bin/env python3
"""Run the real UAC packet/stall code in both configurations with fake hardware."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/src/usb.c').read_text()
def function(name):
    match = re.search(r'^static\s+(?:void|uint32_t)\s+' + name + r'\s*\(', source, re.M)
    assert match, name
    start = match.start(); brace = source.index('{', start); depth = 1; end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}'); end += 1
    return source[start:end]

state = source[source.index('static uint32_t ua_ring[UA_N];'):source.index('#if FELUCCA_UAC_48K\n#include "uac_fir.h"')]
prefix = r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#define FELUCCA_UAC_48K 0
#define FM1_NATIVE_OUTPUT_48K 0
#define UA_N 1024u
#define UA_MAXF 46u
#define UA_LO 320u
#define UA_HI 384u
#define UAC_RATE 44100u
#define RING_PUBLISH() ((void)0)
#define FM1_UAC_DEVELOPMENT_DIAGNOSTICS FM1_DEVELOPMENT_DIAGNOSTICS
static struct { uint8_t up,config; } usb;
static uint32_t csr, sent, checksum;
enum { S_INDEX, S_TXCSR1 };
static void sie_wr(uint32_t reg,uint32_t val) { if(reg==S_TXCSR1) csr=val; }
static uint32_t sie_rd(uint32_t reg) { return reg==S_TXCSR1?csr:0u; }
static void fm1_usb_ep4_send(const uint32_t *p,uint32_t bytes) {
    for(uint32_t i=0;i<bytes/4u;i++) checksum=(checksum*33u)^p[i]; ++sent;
}
'''
test = r'''
int main(void) {
    uint32_t d[UA_MAXF];
    uac_stream(1); assert(uac.alt && !uac.go && !uac.flowing);
    assert(uac_packet(d)==44u); for(uint32_t i=0;i<44u;i++) assert(!d[i]);
    for(uint32_t i=0;i<UA_N;i++) ua_ring[i]=i+1u;
    uac.go=1;ua_w=400;ua_r=0;
    assert(uac_packet(d)==45u && ua_r==45u && d[0]==1u && d[44]==45u);
    ua_w=ua_r+8u; assert(uac_packet(d)==43u && ua_r==53u);
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(uac.underruns==1u);
#endif
    assert(d[7]==53u && d[42]==53u);
    usb.up=usb.config=1;uac.flowing=1;uac.wait=39;csr=1;
    uac_service();assert(!uac.flowing && uac.wait==40u);
    csr=0;uac_service();assert(uac.queued && sent==1u);
    csr=0;uac_service();assert(uac.flowing && sent==2u);
    printf("%u %u %u %u %u\n",ua_r,uac.acc,uac.flowing,sent,checksum);
}
'''
unit = prefix + state + function('uac_stream') + function('uac_packet') + function('uac_service') + test
with tempfile.TemporaryDirectory(prefix='jamesos-uac-') as directory:
    src = Path(directory) / 'test.c'; src.write_text(unit); results = []
    for enabled in (0, 1):
        exe = Path(directory) / str(enabled)
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-variable',
                        f'-DFM1_DEVELOPMENT_DIAGNOSTICS={enabled}',str(src),'-o',str(exe)],check=True)
        results.append(subprocess.check_output([str(exe)], text=True))
    assert results[0] == results[1], results
print('PASS: UAC packet output, flow and stall recovery match both configurations')
