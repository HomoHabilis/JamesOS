#!/usr/bin/env python3
"""Measure audible reverb onset through the app's shared effects graph."""
from pathlib import Path
import os
import subprocess
import run_global_reverb as base

root = Path(__file__).resolve().parents[1]
out = root / 'build/reverb-sync-app-test'
out.mkdir(parents=True, exist_ok=True)
prelude = (root / 'tests/test_global_reverb.c').read_text().split('int main(void)')[0]
test = r'''
int main(void) {
    static float input[44100], output[88200];
    static const unsigned tempos[] = {60u, 120u, 240u};
    static const unsigned ticks[] = {0u,8u,12u,16u,18u,24u,32u,36u,48u,64u,72u,96u};
    input[200] = 240000.0f;
    for (unsigned k = 0; k < 3; ++k) {
        long baseline = -1;
        for (unsigned choice = 1; choice <= 11; ++choice) {
            memset(output, 0, sizeof output);
            setup_inject(BR_REVERB_ROOM, 64u, 64u);
            br_bpm = tempos[k]; br_master.reverb_extra[0] = choice; br_master.reverb_manual = 32u;
            inject(input, 44100, output);
            long first = -1;
            for (long n = 0; n < 44100; ++n)
                if (fabsf(output[n * 2]) > 2 || fabsf(output[n * 2 + 1]) > 2) { first = n; break; }
            assert(first >= 0);
            if (choice == 1) baseline = first;
            double actual = (first - baseline) * 1000.0 / 44100.0;
            double expected = (ticks[choice] - ticks[1]) * 312.5 / tempos[k];
            if (fabs(actual - expected) >= 1.0) fprintf(stderr, "%u BPM choice %u: %.2f ms, expected %.2f ms\n", tempos[k], choice, actual, expected);
            assert(fabs(actual - expected) < 1.0);
        }
        printf("%u BPM: all eleven reverb divisions match their timings\n", tempos[k]);
    }
    /* A delayed impulse must sound, then clear; queued silence cannot keep the
     * shared reverb alive indefinitely. */
    setup_inject(BR_REVERB_ROOM, 64u, 64u);
    br_bpm = 60u; br_master.reverb_extra[0] = 11u; br_master.reverb_manual = 32u;
    memset(output, 0, sizeof output); inject(input, 44100, output);
    memset(input, 0, sizeof input);
    for (unsigned n = 0; n < 8; ++n) inject(input, 44100, output);
    assert(rv2_idle(&br_gr_rv));
    puts("long pre-delay completes and the reverb returns to idle");
    puts("shared-reverb tempo sync: full audio-path checks passed");
    return 0;
}
'''
source = out / 'reverb_sync_app.c'
source.write_text((prelude + test).replace('"../firmware/src/dinghy.c"', '"' + str(root / 'firmware/src/dinghy.c') + '"'))
flags = base.FLAGS + ['-DFM1_DEVELOPMENT_DIAGNOSTICS=1', '-DBR_REVERB_AUDIO_ONLY=1', '-Itests']
if os.environ.get('REVERB_SANITIZE') == '1': flags += ['-fsanitize=address,undefined']
objects = sorted((root / 'build/host/braids').glob('*.o')) + sorted((root / 'build/host/plaits_reference').glob('*.o'))
if not objects:
    raise SystemExit('Host DSP objects are missing; build them with tools/build_braids_host.sh')
planner = out / 'planner.o'
subprocess.run(['cc', *flags, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=root, check=True)
binary = out / 'reverb_sync_app'
subprocess.run(['c++', '-x', 'c', *flags, str(source), '-x', 'none', str(planner), *map(str, objects), '-o', str(binary)], cwd=root, check=True)
subprocess.run([str(binary)], cwd=root, check=True)
