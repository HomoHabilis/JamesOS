#!/usr/bin/env python3
"""Check application and loader USB product strings without accessing hardware."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / 'firmware/src/usb.c').read_text()
strings = src[src.index('static const uint8_t STR0'):src.index('static int get_desc(')]
program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
''' + strings + r'''
int main(void) {
    const char *expected = "FM-1";
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
    for loader in (False, True):
        executable = Path(d) / ('loader' if loader else 'app')
        subprocess.run(['cc', '-std=c11', *(['-DFELUCCA_LOADER=1'] if loader else []),
                        str(source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)

sys.path.insert(0, str(root / 'tools'))
from fm1_install import PORT_RE
for name in ('JamesOS', 'JamesOS Update', 'Felucca', 'Felucca Update', 'FM-1'):
    assert PORT_RE.search(name), name
assert not PORT_RE.search('Unrelated Keyboard')
print('PASS: FM-1 app and update USB strings, with legacy port discovery')
