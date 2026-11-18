#!/usr/bin/env python3
"""Sequencer behaviour audit driver (host only; never touches hardware).

Builds tests/test_seq_behaviour.c against a private copy of firmware/src in
build/seq-behaviour/tree. The copy is identical to the shipped source except
for six observation hooks inserted into instrument_audio.c, so the test can see
every sequencer trigger (part, lane, note, velocity) and its time in samples.
The hooks only record; they do not change behaviour. The shipped tree is never
modified.

Configurations:
  plain     the host flags every sequencer test uses
  clock     plus -DBR_MIDI_CLOCK_EXPERIMENT=1, as tools/build.py sets for the device
  asan      the clock configuration under -fsanitize=address,undefined, built
            with gcc/g++ because this machine's clang has no ASan runtime
            (FM1_PLAITS_COLD_INIT_SIZE=0 only drops a clang size attribute)
  patterns  the clock configuration plus BR_PATTERN_SONG_EXPERIMENT with the
            slot-state stand-in the SMK-37 tree's --smk-preview uses (runs only
            the pattern-page test)

    python3 tests/run_seq_behaviour.py [--config plain,clock,asan]

The configurations are independent programs: they are built at the same time and run at the
same time; their output is printed in configuration order, exactly as when one ran after the
other. (Copying the tree takes about 0.2 s; the time is the four builds of dinghy.c, the ASan
one the slowest.)
"""
import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from run_drive import FLAGS  # noqa: E402  (the same flags as the other host tests)

OUT = ROOT / 'build/seq-behaviour'
TREE = OUT / 'tree'

# (anchor, replacement). Each anchor must occur exactly once in instrument_audio.c.
HOOKS = [
    ('static void br_seq_process(uint32_t p, uint32_t n)\n{\n    uint32_t remaining = n;\n',
     'static void br_seq_process(uint32_t p, uint32_t n)\n{\n    uint32_t remaining = n;\n    seqtest_at(p, 0u);\n'),
    ('    while (remaining) {\n        uint32_t span;\n',
     '    while (remaining) {\n        uint32_t span;\n        seqtest_at(p, n - remaining);\n'),
    ('                    r->pending = 1u;\n',
     '                    r->pending = 1u; seqtest_retrig_due(p, lane, n - remaining);\n'),
    ('                r->pending = 0u;\n',
     '                r->pending = 0u; seqtest_retrig_firing = 1u;\n'),
    ('    uint32_t note = br_drum_lane_note(lane);\n',
     '    uint32_t note = br_drum_lane_note(lane);\n    seqtest_kit(p, lane, voice_id, velocity, pitch);\n'),
    ('                br_release_note(p, br_seq_note[p]);\n            if (!sustain)',
     '                { seqtest_off(p, br_seq_note[p], 1u); br_release_note(p, br_seq_note[p]); }\n            if (!sustain)'),
    ('            if (!sustain) br_start_note(p, note, velocity, slide);\n',
     '            seqtest_synth(p, note, velocity, sustain);\n            if (!sustain) br_start_note(p, note, velocity, slide);\n'),
    ('    if (br_seq_note[p] != 255u) br_release_note(p, br_seq_note[p]);\n',
     '    if (br_seq_note[p] != 255u) { seqtest_off(p, br_seq_note[p], 0u); br_release_note(p, br_seq_note[p]); }\n'),
    ('static void br_start_note_now(uint32_t p, uint32_t note, uint32_t velocity, uint32_t slide)\n{\n',
     'static void br_start_note_now(uint32_t p, uint32_t note, uint32_t velocity, uint32_t slide)\n{\n    seqtest_voice(p, note, velocity);\n'),
]


def prepare_tree():
    if TREE.exists():
        shutil.rmtree(TREE)
    (TREE / 'firmware').mkdir(parents=True)
    shutil.copytree(ROOT / 'firmware/src', TREE / 'firmware/src')
    for name in os.listdir(ROOT / 'firmware'):
        if name != 'src':
            os.symlink(ROOT / 'firmware' / name, TREE / 'firmware' / name)
    # Real directories holding per-file links, so "../firmware/..." from a test
    # or tool resolves inside the instrumented tree, not the shipped one.
    for name in ('tests', 'tools'):
        (TREE / name).mkdir()
        for entry in os.listdir(ROOT / name):
            if entry != '__pycache__':
                os.symlink(ROOT / name / entry, TREE / name / entry)
    audio = TREE / 'firmware/src/instrument_audio.c'
    text = audio.read_text()
    for anchor, replacement in HOOKS:
        count = text.count(anchor)
        if count != 1:
            raise SystemExit('hook anchor found %d times (want 1): %r' % (count, anchor[:70]))
        text = text.replace(anchor, replacement)
    audio.write_text(text)


def build(config, log):
    objects = sorted((ROOT / 'build/host/braids').glob('*.o'))
    objects += sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objects:
        raise SystemExit('No host DSP objects in build/host; run tools/build_braids_host.sh first')
    flags = list(FLAGS)
    if config in ('clock', 'asan', 'patterns'):
        flags.append('-DBR_MIDI_CLOCK_EXPERIMENT=1')
    if config == 'patterns':
        flags += ['-DBR_PATTERN_SONG_EXPERIMENT=1', '-DSEQTEST_PATTERN_STUBS=1']
    cc, cxx = 'cc', 'c++'
    if config == 'asan':
        cc, cxx = 'gcc', 'g++'
        flags = [f for f in flags if f != '-O2'] + ['-O1', '-g', '-fno-omit-frame-pointer',
                                                    '-fsanitize=address,undefined',
                                                    '-DFM1_PLAITS_COLD_INIT_SIZE=0']
    work = OUT / config
    work.mkdir(parents=True, exist_ok=True)
    planner = work / 'planner.o'
    subprocess.run([cc, *flags, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)],
                   cwd=TREE, stdout=log, stderr=subprocess.STDOUT, check=True)
    binary = work / 'test_seq_behaviour'
    if config == 'asan':
        obj = work / 'test.o'
        subprocess.run([cc, *flags, '-c', 'tests/test_seq_behaviour.c', '-o', str(obj)],
                       cwd=TREE, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([cxx, '-fsanitize=address,undefined', str(obj), str(planner), *map(str, objects),
                        '-o', str(binary)], cwd=TREE, stdout=log, stderr=subprocess.STDOUT, check=True)
    else:
        subprocess.run([cxx, '-x', 'c', *flags, 'tests/test_seq_behaviour.c', '-x', 'none', str(planner),
                        *map(str, objects), '-o', str(binary)],
                       cwd=TREE, stdout=log, stderr=subprocess.STDOUT, check=True)
    return binary


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--config', default='plain,clock,asan,patterns')
    ap.add_argument('--only', default='', help='comma-separated test names passed to the binary')
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    prepare_tree()
    configs = a.config.split(',')
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
               UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')

    def build_and_run(config):
        with (OUT / ('compile-%s.log' % config)).open('w') as log:
            binary = build(config, log)
        only = a.only or ('patterns' if config == 'patterns' else '')
        args = [str(binary)] + ([only] if only else [])
        result = subprocess.run(args, cwd=TREE, capture_output=True, text=True, env=env)
        text = result.stdout + result.stderr
        (OUT / ('result-%s.log' % config)).write_text(text)
        return result.returncode, text

    with concurrent.futures.ThreadPoolExecutor(max_workers=len(configs)) as pool:
        futures = [pool.submit(build_and_run, config) for config in configs]
        status = 0
        for config, future in zip(configs, futures):
            code, text = future.result()          # a failed build raises here, as before
            print('===== %s (exit %d) =====' % (config, code))
            print(text, end='')
            status |= code
    sys.exit(1 if status else 0)


if __name__ == '__main__':
    main()
