#!/usr/bin/env python3
"""Exercise actual boot-journal initialization with and without telemetry."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/src/platform.c').read_text()

def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for pos in range(brace, len(source)):
        depth += (source[pos] == '{') - (source[pos] == '}')
        if depth == 0:
            return source[start:pos + 1]
    raise AssertionError(signature)

body = function('static uint32_t fm1_foreground_journal_snapshot(')
body += '\n' + function('static void fm1_foreground_journal_begin(')
prefix = '''#include <assert.h>
#include "fm1_foreground_journal.h"
static volatile fm1_foreground_journal_t fm1_foreground_journal;
#if FM1_DEVELOPMENT_DIAGNOSTICS
static fm1_foreground_journal_t fm1_foreground_previous;
static uint32_t fm1_foreground_previous_mask;
#endif
static void fm1_foreground_journal_barrier(void) {}
'''
test = '''
int main(void) {
  fm1_foreground_journal_t previous = {0}, copy;
  uint32_t mask = fm1_foreground_journal_snapshot(&previous, &copy);
  assert(mask == 0u && !fm1_foreground_journal_header_valid(&copy));
  fm1_foreground_journal_begin(&copy, mask);
  assert(fm1_foreground_journal_header_valid((const fm1_foreground_journal_t *)&fm1_foreground_journal));
  assert(fm1_foreground_journal.boot_id == 1u);
  mask = fm1_foreground_journal_snapshot(&fm1_foreground_journal, &copy);
  fm1_foreground_journal_begin(&copy, mask);
  assert(fm1_foreground_journal.boot_id == 2u);
  copy.boot_id = 0xfffffffeu;
  fm1_foreground_journal_begin(&copy, mask);
  assert(fm1_foreground_journal.boot_id == 1u);
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='jamesos-journal-') as tmp:
    src = Path(tmp) / 'journal.c'
    src.write_text(prefix + body + test)
    for enabled in (0, 1):
        exe = Path(tmp) / str(enabled)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        f'-DFM1_DEVELOPMENT_DIAGNOSTICS={enabled}',
                        '-I', str(root / 'firmware/hal'), str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('PASS: boot journal validity and boot-id progression in both configurations')
