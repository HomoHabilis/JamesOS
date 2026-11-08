#!/usr/bin/env python3
"""Run the real-app unified_real_v16 test on the host; never access hardware."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FLAGS = [
    '-std=c11', '-O2', '-fwrapv', '-Wno-unused-function',
    '-DBR_ENABLE_PLAITS=1', '-DFM1_PAIR_STREAM_HOST_TEST=1',
    '-DFM1_NATIVE_OUTPUT_32K=0', '-DFM1_NATIVE_OUTPUT_48K=0',
    '-DFM1_PATTERN_PROJECT_TARGET_EXPERIMENT=1', '-DFM1_PATTERN_TARGET_EXPERIMENT=1', '-DBR_PATTERN_BANK_EXPERIMENT=1', '-DBR_PATTERN_SESSION_LOAN_EXPERIMENT=1', '-DBR_AUDIO_INTERNAL_BLOCK160=0', '-DBR_AUDIO_INTERNAL_BLOCK256=1',
    '-DPLAITS_INTERNAL_SAMPLE_RATE=44100', '-DFM1_PLAITS_SOURCE_RATE_32K=0',
    '-DFM1_PLAITS_SOURCE_RATE_44100=1', '-DFM1_PLAITS_SOURCE_RATE_38400=0',
    '-DFM1_PLAITS_SOURCE_RATE_48000=0', '-DFM1_PLAITS_SRC_LAGRANGE4=0',
    '-DFM1_PLAITS_SRC_FLOAT_PHASE_FRACTION=0', '-DFM1_PLAITS_PHASE_LUT=0',
    '-DBR_FX_RATE_SIGNED_441_RECIP=0', '-DBR_FX_TAPE_WOW_DELTA_LUT=0',
    '-DBR_AUDIO_FOREGROUND_FIXED8_PLAN=0', '-Ifirmware/src', '-Ifirmware/gen',
]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reuse-dsp', action='store_true',
                    help='Use existing host objects only when their C++ sources/headers are unchanged')
    ap.add_argument('--stream-only', action='store_true', help='Compile with direct fixed-pair fallback disabled')
    ap.add_argument('--compressed-only', action='store_true', help='Run only bounded compressed project checks with sanitizers')
    args = ap.parse_args()
    if args.stream_only: FLAGS.append('-DFM1_PATTERN_STREAM_ONLY=1')
    if args.compressed_only: FLAGS.extend(['-DFM1_COMPRESSED_PROJECT_ONLY=1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all'])
    out = ROOT / 'build/pattern-salvage-runtime-test'
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'compile.log').open('w') as log:
        if not args.reuse_dsp:
            subprocess.run(['sh', 'tools/build_braids_host.sh'], cwd=ROOT,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
        objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
        if not objects:
            raise RuntimeError('No host DSP objects; run without --reuse-dsp')
        planner = out / 'planner.o'
        subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c',
                        '-o', str(planner)], cwd=ROOT, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
        binary = out / 'test_unified_real_v16'
        subprocess.run(['c++', '-x', 'c', *FLAGS, 'tests/test_pattern_salvage_runtime.c', 'firmware/src/pattern_flash_store.c', 'firmware/src/pattern_project.c', 'firmware/src/pattern_project_adapter.c', 'firmware/src/pattern_project_host_bridge.c', 'firmware/src/pattern_project_target_bridge.c', 'firmware/src/pattern_storage_codec.c', 'firmware/src/pattern_codec_source.c', 'firmware/src/pattern_compressed_project.c', 'firmware/src/project_store.c', 'firmware/src/pattern_owned_ps.c', 'firmware/src/pattern_fixed_project.c', 'firmware/src/pattern_fixed_stage.c', 'firmware/src/pattern_fixed_bridge.c', 'firmware/src/pattern_fixed_target.c', 'firmware/src/pattern_pair_stream.c', 'firmware/src/pattern_sha256.c', 'firmware/src/pattern_stream_project.c', 'firmware/src/pattern_pair_project.c', 'firmware/src/pattern_pair_bridge.c', 'firmware/src/pattern_pair_owner.c', 'firmware/src/pattern_fixed_runtime.c', 'firmware/src/pattern_park_io.c', 'firmware/src/pattern_pair_authority.c', 'firmware/src/pattern_fixed_authority.c', 'firmware/src/pattern_bank.c', 'firmware/src/pattern_song.c', 'firmware/src/example_song_import.c',
                        '-x', 'none', str(planner), *map(str, objects),
                        '-o', str(binary)], cwd=ROOT, stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
    (out / 'result.log').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()
    bad = subprocess.run(['cc', *FLAGS, '-DFM1_DEVICE=1', '-c',
                          'firmware/src/pattern_fixed_runtime.c', '-o', str(out / 'must_not_exist.o')],
                         cwd=ROOT, capture_output=True, text=True)
    if bad.returncode == 0 or 'host-only' not in bad.stderr:
        raise RuntimeError('device compile was not rejected by the host-only gate')
    print('device runtime compilation deliberately rejected')

if __name__ == '__main__':
    main()
