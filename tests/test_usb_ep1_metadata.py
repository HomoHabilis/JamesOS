#!/usr/bin/env python3
"""Exercise the actual USB EP1 parser/backpressure code with a fake SIE."""
from pathlib import Path
import subprocess
import re
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / 'firmware/src/usb.c').read_text()

def function(name):
    match = re.search(r'^static\s+(?:void|int)\s+' + re.escape(name) + r'\s*\(', src, re.M)
    assert match, name
    start = match.start()
    brace = src.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[start:end]

metadata = src[src.index('#define MQ 64u'):src.index('static void midi_out_event(')]
# Reset coverage: these entry points reset before their controller operation;
# fake-SIE execution below exercises the packet/no-packet paths directly.
for name in ('ep1_config', 'usb_start', 'usb_detach'):
    assert 'EP1_MIDI_RESET();' in function(name)
assert 'usb.rx_pend = 0;\n        EP1_MIDI_RESET();' in src  # bus reset

prefix = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define FELUCCA_OTA 0
#define FM1_MIDI_GENERATION() test_generation
#define FM1_MIDI_TIMESTAMP() test_tick
#define RING_PUBLISH() ((void)0)
static uint32_t test_generation, test_tick, csr, count;
static uint8_t ep1rx[68];
static struct { uint8_t rx_pend, sx_on; uint32_t rx_held, rx_pkts, rx_bad; } usb;
enum { S_INDEX=14, S_RXCSR1=20, S_RXCSR2=21, S_RXCOUNT1=22, S_RXCOUNT2=23 };
static void sie_wr(uint32_t r, uint32_t v) { if (r==S_RXCSR1 && (v&0x10u)) csr=0u; }
static uint32_t sie_rd(uint32_t r) {
    if(r==S_RXCSR1) return csr;
    if(r==S_RXCOUNT1) return count;
    return 0u;
}
static void fm1_usb_rx_sync(void) {}
static void sysex_byte(uint8_t b) { (void)b; }
'''
test = r'''
int main(void) {
    /* With 41 of 64 slots occupied, the 24-slot reserve blocks the packet. */
    const uint8_t packet[] = {0x0f,0xfa,0,0, 0x0f,0xf8,0,0,
        0x09,0x90,60,100, 0x08,0x80,60,0, 0x03,0xf2,0,0};
    for (uint32_t i=0;i<sizeof packet;i++) ep1rx[i]=packet[i];
    test_generation=7; test_tick=12345; csr=1; count=sizeof packet;
    mi_r=0; mi_w=41; usb.rx_pend=1; ep1_rx();
    assert(usb.rx_held==1 && mi_w==41 && ep1_midi_latched);
    assert(ep1_midi_generation==7 && ep1_midi_timestamp==12345);
    /* A second held poll and a source change must not change metadata. */
    test_generation=8; test_tick=45678; ep1_rx();
    assert(usb.rx_held==2 && ep1_midi_generation==7 && ep1_midi_timestamp==12345);
    mi_r=2; ep1_rx();
    assert(!ep1_midi_latched && !usb.rx_pend && usb.rx_pkts==1 && mi_w==46);
    for(uint32_t i=41;i<46;i++) {
        assert(midi_in_generation_q[i%MQ]==7);
        assert(midi_in_time_q[i%MQ]==12345);
    }
    assert(midi_in_q[41]==(0x0fu|(0xfau<<8)));
    assert(midi_in_q[42]==(0x0fu|(0xf8u<<8)));
    assert(midi_in_q[43]==(0x09u|(0x90u<<8)|(60u<<16)|(100u<<24)));
    assert(midi_in_q[44]==(0x08u|(0x80u<<8)|(60u<<16)));
    assert(midi_in_q[45]==(0x03u|(0xf2u<<8)));
    /* Next packet receives a fresh generation/time. */
    mi_r=mi_w; csr=1; count=4; ep1rx[1]=0xfa; test_tick=78901;
    ep1_rx(); assert(midi_in_generation_q[46]==8 && midi_in_time_q[46]==78901);
    /* Lost endpoint readiness and explicit controller reset discard latch. */
    mi_r=0; mi_w=41; csr=1; ep1_rx(); assert(ep1_midi_latched);
    csr=0; ep1_rx(); assert(!ep1_midi_latched);
    csr=1; ep1_rx(); assert(ep1_midi_latched);
    EP1_MIDI_RESET(); assert(!ep1_midi_latched);
    puts("PASS: USB EP1 held-packet generation, timestamp, note ordering and reset");
    return 0;
}
'''
unit = prefix + metadata + function('midi_in_event') + '\n#define EP1_ROOM (16u + 8u)\n' + function('ep1_take') + function('ep1_rx') + test
with tempfile.TemporaryDirectory(prefix='fm1-usb-ep1-') as tmp:
    c = Path(tmp) / 'test.c'
    binary = Path(tmp) / 'test'
    c.write_text(unit)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-Wno-unused-variable',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
    # No generation or timestamp sidecars: ordinary app and loader still
    # parse MIDI, and the loader retains its existing no-backpressure path.
    plain_prefix = prefix.replace('#define FM1_MIDI_GENERATION() test_generation\n', '')
    plain_prefix = plain_prefix.replace('#define FM1_MIDI_TIMESTAMP() test_tick\n', '')
    plain_test = r'''
int main(void) {
    mi_r=0; mi_w=0; csr=1; count=4;
    ep1rx[0]=0x09; ep1rx[1]=0x90; ep1rx[2]=60; ep1rx[3]=100;
    ep1_rx(); assert(mi_w==1 && midi_in_q[0]==(0x09u|(0x90u<<8)|(60u<<16)|(100u<<24)));
    /* F2 remains absent from the reviewed loader, but is delivered by the
     * application for the explicit unsupported-song-position policy. */
    midi_in_event(0x03u | (0xf2u<<8) | (1u<<16) | (2u<<24));
#ifdef FELUCCA_LOADER
    assert(mi_w==1);
#else
    assert(mi_w==2 && midi_in_q[1]==(0x03u | (0xf2u<<8) | (1u<<16) | (2u<<24)));
#endif
    { uint32_t before=mi_w;
      midi_in_event(0x03u | (0xf2u<<8) | (0x80u<<16));
      assert(mi_w==before);
      midi_in_event(0x0fu | (0xf8u<<8));
      assert(mi_w==before+1 && midi_in_q[before%MQ]==(0x0fu|(0xf8u<<8)));
    }
    puts("PASS: app/loader F2 policy and ordinary USB MIDI parsing");
    return 0;
}
'''
    funcs = metadata + function('midi_in_event') + '\n#define EP1_ROOM (16u + 8u)\n' + function('ep1_take') + function('ep1_rx')
    for loader in (False, True):
        config = plain_prefix
        if loader:
            config = config.replace('#define FELUCCA_OTA 0', '#define FELUCCA_OTA 1')
            config += '\nstatic uint8_t sx_collect;\n'
        c.write_text(('#define FELUCCA_LOADER 1\n' if loader else '') + config + funcs + plain_test)
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
                        '-Wno-unused-variable',str(c),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
