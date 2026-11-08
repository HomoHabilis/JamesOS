#!/usr/bin/env python3
"""Run the target worker dispatcher with fake timing and audio work."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/hal/fm1_cpu1_worker_target.c').read_text()
start = source.index('static uint32_t worker_dispatch_target(')
brace = source.index('{', start)
depth = 0
for pos in range(brace, len(source)):
    depth += (source[pos] == '{') - (source[pos] == '}')
    if depth == 0:
        function = source[start:pos + 1]
        break
else:
    raise AssertionError('worker dispatcher missing')
prefix = '''#include <stdint.h>
#include <assert.h>
#define FM1_CPU1_WORKER_TASK_SMOKE 1u
#define BR_CPU1_AUDIO 1
#define FM1_CPU1_WORKER_BARRIER() ((void)0)
static uint32_t ticks;
static uint32_t fm1_ticks(void) { return ++ticks; }
static struct { uint32_t request_seq; } fm1_cpu1_worker_mailbox;
#if FM1_DEVELOPMENT_DIAGNOSTICS
static struct { uint32_t last_job_status, last_job_task, last_job_context,
last_job_start_ticks, last_job_sequence, last_job_elapsed_ticks, last_job_result; } fm1_cpu1_worker_diag;
#define FM1_WORKER_TRACE_SET(field, value) (fm1_cpu1_worker_diag.field = (value))
static uint32_t worker_smoke_callback(void *context) { (void)context; return 33u; }
#else
#define FM1_WORKER_TRACE_SET(field, value) ((void)0)
#endif
static uint32_t fm1_cpu1_worker_dispatch_audio_task(uint32_t id, void *context)
{ (void)context; return id == 2u ? 42u : 0xffffffffu; }
'''
test = '''
int main(void) {
  fm1_cpu1_worker_mailbox.request_seq = 7u;
  assert(worker_dispatch_target(0, 2u, 0) == 42u);
#if FM1_DEVELOPMENT_DIAGNOSTICS
  assert(worker_dispatch_target(0, 1u, 0) == 33u);
  assert(fm1_cpu1_worker_diag.last_job_sequence == 7u && ticks == 4u);
#else
  assert(worker_dispatch_target(0, 1u, 0) == 0xffffffffu && ticks == 0u);
#endif
  assert(worker_dispatch_target(0, 99u, 0) == 0xffffffffu);
  return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='jamesos-worker-') as tmp:
    src = Path(tmp) / 'worker.c'
    src.write_text(prefix + function + test)
    for enabled in (0, 1):
        for opt in ('-O0', '-O2'):
            exe = Path(tmp) / f'{enabled}{opt}'
            subprocess.run(['cc', '-std=c11', opt, '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', f'-DFM1_DEVELOPMENT_DIAGNOSTICS={enabled}',
                            str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
print('PASS: worker audio dispatch and development smoke selection in both configurations')
