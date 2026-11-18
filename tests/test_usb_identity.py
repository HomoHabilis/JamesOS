#!/usr/bin/env python3
"""Check application and loader USB product strings without accessing hardware."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / 'firmware/src/usb.c').read_text()
strings = src[src.index('static const uint8_t STR0'):src.index('static int get_desc(')]
config = src[src.index('#define CDC_IF'):src.index('#if FELUCCA_CDC\n/* SLOOP 2.4')]
descriptor_function = src[src.index('static int get_desc('):src.index('/* ---------------------------------------------------------- the SIE')]
program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define FELUCCA_CDC 0
#if defined(FELUCCA_LOADER)
#define FELUCCA_UAC 0
#else
#define FELUCCA_UAC 1
#endif
#define FELUCCA_UAC_48K 0
#define UAC_MAXP 192
#define UAC_RATE 44100
''' + config + strings + descriptor_function + r'''
int main(void) {
    unsigned found_in = 0, found_out = 0;
    assert(DEV_DESC[12] == 0x10 * FELUCCA_UAC + (BR_PRODUCT_SMK37 ? 1 : 0));
    for (size_t at = 0; at < sizeof CFG_DESC; at += CFG_DESC[at]) {
        assert(CFG_DESC[at] && at + CFG_DESC[at] <= sizeof CFG_DESC);
        if (CFG_DESC[at + 1] != 0x24) continue;
        if (CFG_DESC[at] == 6 && CFG_DESC[at + 2] == 2 && CFG_DESC[at + 3] == 1 && CFG_DESC[at + 4] == 1) {
            assert(CFG_DESC[at + 5] == (BR_PRODUCT_SMK37 ? 3 : 0)); ++found_in;
        }
        if (CFG_DESC[at] == 9 && CFG_DESC[at + 2] == 3 && CFG_DESC[at + 3] == 1 && CFG_DESC[at + 4] == 3) {
            assert(CFG_DESC[at + 8] == (BR_PRODUCT_SMK37 ? 3 : 0)); ++found_out;
        }
    }
    assert(found_in == 1 && found_out == 1);
    const uint8_t *desc = 0; uint16_t size = 0;
#if BR_PRODUCT_SMK37
# if defined(FELUCCA_LOADER)
    const char *expected = "SMK-37 OTA";
# else
    const char *expected = "SMK-37";
# endif
    const char *jack = "SMK-37";
    assert(sizeof STR3 == 2 + 2 * strlen(jack) && STR3[0] == sizeof STR3 && STR3[1] == 3);
    for (size_t i = 0; i < strlen(jack); ++i)
        assert(STR3[2 + 2 * i] == jack[i] && STR3[3 + 2 * i] == 0);
    assert(get_desc(0x0303, &desc, &size) && desc == STR3 && size == sizeof STR3);
#else
    const char *expected = "FM-1";
    assert(!get_desc(0x0303, &desc, &size));
#endif
    size_t n = strlen(expected);
    assert(sizeof STR2 == 2 + 2 * n && STR2[0] == sizeof STR2 && STR2[1] == 3);
    for (size_t i = 0; i < n; ++i) {
        assert(STR2[2 + 2 * i] == expected[i] && STR2[3 + 2 * i] == 0);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='fm1-usb-identity-') as d:
    source = Path(d) / 'test.c'
    source.write_text(program)
    for smk in (False, True):
        for loader in (False, True):
            executable = Path(d) / ('smk' if smk else 'fm1')
            defines = (['-DBR_PRODUCT_SMK37=1', '-DJAMESOS_K_USB=1'] if smk else ['-DBR_PRODUCT_SMK37=0'])
            defines += ['-DFELUCCA_LOADER=1'] if loader else []
            subprocess.run(['cc', '-std=c11', *defines, str(source), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

sys.path.insert(0, str(root / 'tools'))
from fm1_install import PORT_RE
for name in ('JamesOS', 'JamesOS Update', 'Felucca', 'Felucca Update', 'FM-1'):
    assert PORT_RE.search(name), name
assert not PORT_RE.search('Unrelated Keyboard')
print('PASS: FM-1 and SMK app and loader USB strings, with port discovery')
