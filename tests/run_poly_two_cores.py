#!/usr/bin/env python3
"""Compare serial and simultaneous worker scheduling with real DSP."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import platform
import subprocess
import sys
import host_target
host_target.activate(host_target.target(), with_poly=True)   # polyphony (BR_CAP_POLY=1) for the selected product
from run_drive import FLAGS, ROOT  # noqa: E402

out = ROOT / 'build/poly-two-cores'
out.mkdir(parents=True, exist_ok=True)
fw = ROOT / 'firmware'
plaits = [fw / 'plaits/plaits_c_api.cc', fw / 'plaits/plaits_kit_c_api.cc']
plaits += sorted((fw / 'plaits/upstream/plaits').rglob('*.cc'))
plaits += sorted((fw / 'plaits/upstream/stmlib').rglob('*.cc'))
braids = [fw / 'braids/braids_c_api.cc', fw / 'braids/braids_perc_c_api.cc']
braids += [fw / 'braids/upstream/braids' / (name + '.cc') for name in
           ('analog_oscillator', 'digital_oscillator', 'macro_oscillator', 'resources')]
includes = ['-I' + str(fw / p) for p in ('src', 'gen', 'plaits', 'plaits/upstream', 'braids', 'braids/upstream')]
profile = [arg for arg in FLAGS if arg.startswith('-D')]
sources = [(p, True) for p in plaits] + [(p, False) for p in braids]
def compile_one(item):
    i, (source, plaits_source) = item
    obj = out / f'{i:03}.o'
    flags = ['-DTEST', '-Dstmlib=plaits_stmlib', '-DSTMLIB_RANDOM_TEST_CORE_ID'] if plaits_source else []
    subprocess.run(['c++', '-std=c++14', '-O2', '-fwrapv', '-ffp-contract=off', '-fno-exceptions',
                    '-fno-rtti', '-fno-threadsafe-statics', *profile, *flags, *includes,
                    '-c', str(source), '-o', str(obj)], cwd=ROOT, check=True)
    return obj
if '--reuse-dsp' in sys.argv:
    objects = [out / f'{i:03}.o' for i in range(len(sources))]
    if not all(obj.exists() for obj in objects):
        raise SystemExit('Run without --reuse-dsp first')
else:
    with ThreadPoolExecutor(max_workers=6) as pool:
        objects = list(pool.map(compile_one, enumerate(sources)))
planner = out / 'planner.o'
subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=ROOT, check=True)
shim = out / 'core.o'
subprocess.run(['c++', '-std=c++14', '-c', 'tests/poly_random_test_core.cc', '-o', str(shim)], cwd=ROOT, check=True)
binary = out / 'test_poly_two_cores'
linker = '-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections'
subprocess.run(['c++', '-x', 'c', *FLAGS, '-DBR_POLY_TEST_TWO_CORES=1', '-pthread',
                '-ffunction-sections', '-fdata-sections', 'tests/test_poly_scheduler.c', '-x', 'none',
                str(planner), str(shim), *map(str, objects), linker, '-o', str(binary)], cwd=ROOT, check=True)
# Each trace is 20 blocks of the selected track's bus. The harness decodes the packed Plaits
# resources first (fm1_plaits_waves_init, as the device does at boot), so every model must render
# sound: an all-zero serial trace would make "parallel equals serial" prove nothing.
silent = []
for model in range(24):
    serial = subprocess.check_output([str(binary), 'serial', str(model)], cwd=ROOT)
    if not any(serial):
        silent.append(f'model {model}')
    for i in range(2):
        parallel = subprocess.check_output([str(binary), 'parallel', str(model)], cwd=ROOT)
        if parallel != serial:
            raise SystemExit(f'parallel audio differs from serial for model {model}, run {i + 1}')
    print(f'model {model}: two parallel runs matched serial audio ({sum(b != 0 for b in serial)} non-zero bytes)')
for selected in (1, 2):
    for model in (0, 8, 15, 18, 19, 20):
        args = [str(model), str(selected)]
        serial = subprocess.check_output([str(binary), 'serial', *args], cwd=ROOT)
        if not any(serial):
            silent.append(f'model {model} track {selected + 1}')
        parallel = subprocess.check_output([str(binary), 'parallel', *args], cwd=ROOT)
        if parallel != serial:
            raise SystemExit(f'parallel audio differs for model {model}, track {selected + 1}')
    print(f'track {selected + 1}: representative models matched serial audio')
if silent:
    raise SystemExit(f'serial trace all zero (the comparison proves nothing there): {", ".join(silent)}')
print('all 24 models and 12 track cases: non-zero serial traces, parallel identical')
