#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The product switch itself (firmware/product.h, tools/build.py --target), host only, builds nothing.

product.h: FM-1 is the default; -DBR_PRODUCT selects SMK-37 Pro (by name or number); the data
macros and capabilities of each product. Polyphony (BR_CAP_POLY) is a capability of both
products (product.h: SMK-37 default on, FM-1 default off: tools/build.py turns it on for FM-1
device candidates with -DBR_CAP_POLY=1); BR_POLY_SCHEDULER_EXPERIMENT follows
it (either spelling selects it). BR_POLY_SUPPORT = BR_CAP_POLY || BR_PRODUCT_SMK37;
BR_POLY_SIZE_CONSTRAINED (and a non-empty BR_POLY_COLD) only for FM-1 with polyphony. Still
stopped: an unknown product, a hardware BR_CAP_* or BR_PRODUCT_FM1/SMK37 on the command line,
BR_CAP_POLY not 0/1, BR_CAP_POLY and BR_POLY_SCHEDULER_EXPERIMENT disagreeing, and a
command-line BR_POLY_SUPPORT, BR_POLY_SIZE_CONSTRAINED or BR_POLY_COLD.

build.py --print-config: each target resolves to its product define, identity, slot, layout and
paths; the WP0 reference build commands predict the reference build-settings.json
(tests/baselines/build-settings-*.json), with the later
project transfer SHA-256 source added, on every key that does not depend on the build output
(FM-1: with --no-polyphony, the build the reference was cut from);
a wrong-prefix identity is refused. Interface: HD (-DBR_UI_HD=1, "ui_hd": true, plain output
directories without an -hd suffix) is the only interface of both targets; --ui-hd resolves exactly as the default; the
standard interface was removed, so --ui-standard is refused, as is --ui-hd with --ui-standard.
Polyphony: FM-1 device candidates default to
polyphony: -DBR_CAP_POLY=1, the size-constrained configuration (poly_size_os_sources), the plain
output directory, "polyphony": true; --polyphony resolves exactly as the default; --target fm1
--no-polyphony is the pre-polyphony build (no BR_CAP_POLY define, no polyphony keys) in a distinct
-nopoly output directory; outside device candidates nothing changes. The cflags of every resolved
configuration, compiled against product.h, give the BR_CAP_POLY / BR_POLY_SIZE_CONSTRAINED that
build.py reports.
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# These checks choose every define themselves: drop the defines tests/host_target.py adds to every
# compiler call for an SMK suite run (BR_HOST_CFLAGS), or they would be checked twice over.
os.environ.pop('BR_HOST_CFLAGS', None)
SMK = '-DBR_PRODUCT=BR_PRODUCT_ID_SMK37'
FAILED = []


def check(ok, what):
    print(('ok    ' if ok else 'FAIL  ') + what, flush=True)
    if not ok:
        FAILED.append(what)


def compile_header(defines, asserts=''):
    """compile a unit that includes product.h (twice) -> (ok, compiler output)"""
    with tempfile.TemporaryDirectory(prefix='product-h-') as tmp:
        src = Path(tmp) / 'unit.c'
        src.write_text('#include "firmware/product.h"\n#include "firmware/product.h"\n' + asserts + '\nint x;\n')
        r = subprocess.run(['cc', '-std=c11', '-fsyntax-only', '-Werror', '-I', str(ROOT), *defines, str(src)],
                           capture_output=True, text=True)
        return r.returncode == 0, r.stderr


def expect_values(defines, values, undefined=()):
    lines = [f'_Static_assert(({k}) == ({v}), "{k.replace(chr(34), chr(39))}");' for k, v in values.items()]
    lines += [f'#ifdef {k}\n#error "{k} defined"\n#endif' for k in undefined]
    ok, err = compile_header(defines, COLD_PRELUDE + '\n'.join(lines))
    check(ok, f'product.h {" ".join(defines) or "(no define)"}: {", ".join(f"{k}={v}" for k, v in values.items())}'
          + (f'; {", ".join(undefined)} undefined' if undefined else '') + ('' if ok else '\n' + err))


def expect_error(defines, message):
    ok, err = compile_header(defines)
    check(not ok and message in err, f'product.h {" ".join(defines)} stops the compile: "{message}"'
          + ('' if not ok and message in err else '\n' + err))


FM1_VALUES = {'BR_PRODUCT_FM1': 1, 'BR_PRODUCT_SMK37': 0, 'BR_PRODUCT_PLAY_KEYS': '27u', 'BR_PRODUCT_APP_SLOT': '0x8DFBCu',
              'BR_PRODUCT_APP_WINDOW_END': '0x93000u', 'BR_PRODUCT_SETTINGS_MAGIC': '0x59474E44u', 'BR_CAP_PADS': 0,
              'BR_CAP_FADERS': 0, 'BR_CAP_WHEELS': 0, 'BR_CAP_VELOCITY': 0, 'BR_CAP_DIN_MIDI': 1,
              'BR_CAP_SUPPLY396': 1, 'BR_CAP_POLY': 0, 'BR_POLY_SUPPORT': 0, 'BR_POLY_SIZE_CONSTRAINED': 0,
              'sizeof BR_PRODUCT_NAME': 5, 'sizeof BR_PRODUCT_LOADER_IDENTITY': len('ota-FM-1_904') + 1}
SMK_VALUES = {'BR_PRODUCT_FM1': 0, 'BR_PRODUCT_SMK37': 1, 'BR_PRODUCT_PLAY_KEYS': '37u', 'BR_PRODUCT_APP_SLOT': '0x97400u',
              'BR_PRODUCT_APP_WINDOW_END': '0x9D000u', 'BR_PRODUCT_SETTINGS_MAGIC': '0x4B534F4Au', 'BR_CAP_PADS': 1,
              'BR_CAP_FADERS': 1, 'BR_CAP_WHEELS': 1, 'BR_CAP_VELOCITY': 1, 'BR_CAP_DIN_MIDI': 0,
              'BR_CAP_SUPPLY396': 0, 'BR_POLY_SUPPORT': 1, 'BR_POLY_SIZE_CONSTRAINED': 0,
              'sizeof BR_PRODUCT_NAME': 11, 'sizeof BR_PRODUCT_LOADER_IDENTITY': len('ota-SMK-37 Pro_9260001') + 1}
# FM-1 with polyphony: the engine switch, the support code and the size-constrained mode
FM1_POLY = {'BR_PRODUCT_FM1': 1, 'BR_CAP_POLY': 1, 'BR_POLY_SCHEDULER_EXPERIMENT': 1, 'BR_POLY_SUPPORT': 1,
            'BR_POLY_SIZE_CONSTRAINED': 1, 'BR_CAP_DIN_MIDI': 1, 'BR_CAP_PADS': 0}
# BR_POLY_COLD is empty (stringises to "") except in the size-constrained configuration
COLD_EMPTY = {'sizeof COLD_STR(BR_POLY_COLD)': 1}
COLD_SET = {'sizeof COLD_STR(BR_POLY_COLD) > 1': 1}
COLD_PRELUDE = '#define COLD_STR_(...) #__VA_ARGS__\n#define COLD_STR(x) COLD_STR_(x)\n'


def header_checks():
    expect_values([], dict(FM1_VALUES, **COLD_EMPTY), ['BR_POLY_SCHEDULER_EXPERIMENT'])
    expect_values(['-DBR_PRODUCT=BR_PRODUCT_ID_FM1'], FM1_VALUES, ['BR_POLY_SCHEDULER_EXPERIMENT'])
    expect_values(['-DBR_PRODUCT=1'], {'BR_PRODUCT_FM1': 1})
    expect_values(['-DBR_CAP_POLY=0'], {'BR_CAP_POLY': 0, 'BR_PRODUCT_FM1': 1, 'BR_POLY_SUPPORT': 0,
                                        'BR_POLY_SIZE_CONSTRAINED': 0}, ['BR_POLY_SCHEDULER_EXPERIMENT'])
    expect_values(['-DBR_POLY_SCHEDULER_EXPERIMENT=0'], {'BR_CAP_POLY': 0, 'BR_POLY_SUPPORT': 0})
    # polyphony on FM-1 is allowed (default off), by either name, and is the size-constrained mode
    expect_values(['-DBR_CAP_POLY=1'], dict(FM1_POLY, **COLD_SET))
    expect_values(['-DBR_PRODUCT=BR_PRODUCT_ID_FM1', '-DBR_CAP_POLY=1'], FM1_POLY)
    expect_values(['-DBR_POLY_SCHEDULER_EXPERIMENT=1'], FM1_POLY)        # old -D spelling
    expect_values(['-DBR_CAP_POLY=1', '-DBR_POLY_SCHEDULER_EXPERIMENT=1'], FM1_POLY)
    # SMK-37: polyphony on by default; BR_POLY_SUPPORT always 1; never size-constrained
    expect_values([SMK], dict(SMK_VALUES, BR_CAP_POLY=1, BR_POLY_SCHEDULER_EXPERIMENT=1, **COLD_EMPTY))
    expect_values(['-DBR_PRODUCT=2'], {'BR_PRODUCT_SMK37': 1, 'BR_CAP_POLY': 1})
    expect_values([SMK, '-DBR_CAP_POLY=0'], dict(SMK_VALUES, BR_CAP_POLY=0, **COLD_EMPTY),
                  ['BR_POLY_SCHEDULER_EXPERIMENT'])
    expect_values([SMK, '-DBR_CAP_POLY=1'], {'BR_CAP_POLY': 1, 'BR_POLY_SCHEDULER_EXPERIMENT': 1,
                                             'BR_POLY_SUPPORT': 1, 'BR_POLY_SIZE_CONSTRAINED': 0})
    expect_values([SMK, '-DBR_POLY_SCHEDULER_EXPERIMENT=1'], {'BR_CAP_POLY': 1})   # old -D spelling
    expect_values([SMK, '-DBR_POLY_SCHEDULER_EXPERIMENT=0'], {'BR_CAP_POLY': 0, 'BR_POLY_SUPPORT': 1})
    # the remaining consistency errors, for both products
    for prod in ([], [SMK]):
        expect_error(prod + ['-DBR_CAP_POLY=0', '-DBR_POLY_SCHEDULER_EXPERIMENT=1'], 'disagree')
        expect_error(prod + ['-DBR_CAP_POLY=1', '-DBR_POLY_SCHEDULER_EXPERIMENT=0'], 'disagree')
        expect_error(prod + ['-DBR_CAP_POLY=2'], 'BR_CAP_POLY must be 0 or 1')
        for v in ('0', '1'):
            expect_error(prod + [f'-DBR_POLY_SUPPORT={v}'], 'BR_POLY_SUPPORT follows from BR_CAP_POLY')
            expect_error(prod + [f'-DBR_POLY_SIZE_CONSTRAINED={v}'], 'do not define them')
        expect_error(prod + ['-DBR_POLY_COLD='], 'do not define them')
    # unknown product
    for bad in ('-DBR_PRODUCT=3', '-DBR_PRODUCT=0', '-DBR_PRODUCT=BR_PRODUCT_ID_SMK38'):
        expect_error([bad], 'unknown BR_PRODUCT')
    # hardware capabilities and product flags are not command-line options
    for cap in ('PADS', 'FADERS', 'WHEELS', 'VELOCITY', 'DIN_MIDI', 'SUPPLY396'):
        for v in ('0', '1'):
            expect_error([f'-DBR_CAP_{cap}={v}'], 'do not define BR_CAP_*')
    expect_error([SMK, '-DBR_CAP_DIN_MIDI=1'], 'do not define BR_CAP_*')
    for flag in ('-DBR_PRODUCT_FM1=1', '-DBR_PRODUCT_SMK37=1', '-DBR_PRODUCT_SMK37=0'):
        expect_error([flag], 'select the product with -DBR_PRODUCT')


def build_py(*args):
    r = subprocess.run([sys.executable, 'tools/build.py', '--print-config', *args], cwd=ROOT,
                       capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def config(*args):
    rc, out, err = build_py(*args)
    if rc:
        check(False, f'build.py --print-config {" ".join(args)} resolves\n{err}')
        return None
    return json.loads(out)


def header_agrees(c, what):
    """compile product.h with the -D/-include flags build.py resolved -> its polyphony macros agree"""
    defines = [f for f in c['cflags'] if f.startswith('-DBR_')]
    size = 'poly_size_os_sources' in c['build_settings']
    ok, err = compile_header(defines, f'_Static_assert(BR_CAP_POLY == {int(c["polyphony"])}, "poly");\n'
                                      f'_Static_assert(BR_POLY_SIZE_CONSTRAINED == {int(size)}, "size");')
    check(ok, f'{what}: product.h with its cflags gives BR_CAP_POLY={int(c["polyphony"])}, '
              f'BR_POLY_SIZE_CONSTRAINED={int(size)} as build.py reports' + ('' if ok else '\n' + err))


CAND = ('--pattern-device-candidate', '--app-opt', 'Oz')


def poly_checks():
    """polyphony is a build option of both products (device candidates only)"""
    def no_poly_define(c):
        return not any(f.startswith('-DBR_CAP_POLY') for f in c['cflags'])

    # outside device candidates FM-1 has no polyphony and no define, whatever the flag
    for extra in ((),):
        c, d = config('--target', 'fm1', *extra, '--no-polyphony'), config('--target', 'fm1', *extra)
        if c and d:
            check(c == d and not d['polyphony'] and no_poly_define(d),
                  f'build.py --target fm1 {" ".join(extra + ("--no-polyphony",))} (no candidate) resolves exactly as the '
                  'default: no polyphony, no BR_CAP_POLY define')
    for ui in ((),):
        what = 'standard' if ui else 'HD'
        dirname = 'build/pattern-device-candidate'
        on, explicit = config('--target', 'fm1', *CAND, *ui), config('--target', 'fm1', '--polyphony', *CAND, *ui)
        off = config('--target', 'fm1', '--no-polyphony', *CAND, *ui)
        if on:
            bs = on['build_settings']
            check(on['polyphony'] and '-DBR_CAP_POLY=1' in on['cflags'] and '-DBR_CAP_POLY=0' not in on['cflags'] and
                  on['output_dir'] == dirname and bs.get('polyphony') is True and bs.get('poly_size_os_sources'),
                  f'build.py --target fm1 {what} candidate: polyphony on by default, -DBR_CAP_POLY=1, '
                  f'size-constrained (poly_size_os_sources {bs.get("poly_size_os_sources")}), {dirname}, '
                  '"polyphony": true')
            header_agrees(on, f'fm1 {what} candidate')
        if on and explicit:
            check(on == explicit, f'build.py --target fm1 {what} --polyphony resolves exactly as the default')
        if off:
            check(not off['polyphony'] and no_poly_define(off) and off['output_dir'] == dirname + '-nopoly'
                  and 'polyphony' not in off['build_settings'] and 'poly_size_os_sources' not in off['build_settings'],
                  f'build.py --target fm1 {what} --no-polyphony: no BR_CAP_POLY define, {dirname}-nopoly, '
                  'build-settings.json without polyphony keys')
            header_agrees(off, f'fm1 {what} --no-polyphony')
        if on and off:
            same = {k: on[k] == off[k] for k in ('identity', 'app_slot', 'package_layout', 'product_define')}
            check(all(same.values()), f'build.py --target fm1 {what}: polyphony keeps the FM-1 identity, slot, '
                  f'layout: {same}')
            check([f for f in on['cflags'] if f != '-DBR_CAP_POLY=1'] == off['cflags'],
                  f'build.py --target fm1 {what}: cflags with polyphony = --no-polyphony cflags + -DBR_CAP_POLY=1 only')
    for ui, dirname in (((), 'pattern-device-candidate-distribution'),):
        on = config('--target', 'fm1', *CAND, '--diagnostics', 'distribution', *ui)
        off = config('--target', 'fm1', '--no-polyphony', *CAND, '--diagnostics', 'distribution', *ui)
        if on and off:
            check(on['output_dir'] == 'build/' + dirname and off['output_dir'] == f'build/{dirname}-nopoly',
                  f'build.py --target fm1 distribution {" ".join(ui) or "HD (default)"}: build/{dirname}, '
                  f'--no-polyphony build/{dirname}-nopoly')
    rc, out, err = build_py('--target', 'fm1', '--polyphony')
    check(rc == 2 and 'requires --pattern-device-candidate' in err and not out,
          'build.py --target fm1 --polyphony without a device candidate is refused')
    for t in ('fm1', 'smk37'):
        rc, out, err = build_py('--target', t, '--polyphony', '--no-polyphony', *CAND)
        check(rc == 2 and 'not allowed with' in err and not out,
              f'build.py --target {t} --polyphony --no-polyphony is refused')
    s_def, s_on = config('--target', 'smk37', *CAND), config('--target', 'smk37', '--polyphony', *CAND)
    if s_def and s_on:
        check(s_def == s_on, 'build.py --target smk37 --polyphony resolves exactly as the default')
        check(s_def['output_dir'] == 'build/pattern-device-candidate-smk37' and
              'poly_size_os_sources' not in s_def['build_settings'] and 'polyphony' not in s_def['build_settings'],
              'build.py --target smk37 candidate: not size-constrained, build/pattern-device-candidate-smk37, '
              'no polyphony keys (its default)')
        header_agrees(s_def, 'smk37 candidate')
    c = config('--target', 'smk37', '--no-polyphony', *CAND)
    if c:
        header_agrees(c, 'smk37 --no-polyphony candidate')
        check(c['output_dir'] == 'build/pattern-device-candidate-smk37',
              'build.py --target smk37 --no-polyphony: same output directory as the default')


def without_hd(output_dir):
    """the output directory without an old hd suffix: build/hd -> build, build/hd-smk37 -> build/smk37,
    build/x-hd[-smk37] -> build/x[-smk37] (HD outputs no longer carry it: the result must equal the input)"""
    if '/' not in output_dir:
        return output_dir
    head, name = output_dir.rsplit('/', 1)
    parts = name.split('-')
    if 'hd' in parts:
        parts.remove('hd')
    return '/'.join([head] + (['-'.join(parts)] if parts else []))


def ui_checks():
    """HD is the only interface of both targets; --ui-standard (the removed standard interface) is refused"""
    modes = {'plain': (), 'distribution': ('--diagnostics', 'distribution'), 'candidate': CAND,
             'distribution candidate': CAND + ('--diagnostics', 'distribution'),
             'distribution candidate --no-polyphony': CAND + ('--diagnostics', 'distribution', '--no-polyphony'),
             'target experiment': ('--pattern-target-experiment',)}
    for t in ('fm1', 'smk37'):
        for what, args in modes.items():
            d = config('--target', t, *args)
            h = config('--target', t, *args, '--ui-hd')
            if not (d and h):
                continue
            check(d == h, f'build.py --target {t} {what}: --ui-hd resolves exactly as the default')
            check('-DBR_UI_HD=1' in d['app_defines'] and d['build_settings']['ui_hd'] is True and
                  d['output_dir'] == without_hd(d['output_dir']),
                  f'build.py --target {t} {what}: default is HD (-DBR_UI_HD=1, "ui_hd": true, {d["output_dir"]})')
            rc, out, err = build_py('--target', t, *args, '--ui-standard')
            check(rc == 2 and 'the standard interface was removed' in err and not out,
                  f'build.py --target {t} {what} --ui-standard is refused: the standard interface was removed')
        rc, out, err = build_py('--target', t, '--ui-hd', '--ui-standard')
        check(rc == 2 and 'not allowed with' in err and not out, f'build.py --target {t} --ui-hd --ui-standard is refused')


def build_py_checks():
    for target, expected in (('fm1', 'FM-1_9673043'), ('smk37', 'SMK-37 Pro_9653043')):
        paired = config('--target', target, '--build-number', '3043')
        if paired:
            check(paired['identity'] == expected, f'{target}: shared build suffix uses product-specific identity')
    rc, _, _ = build_py('--build-number', '123')
    check(rc != 0, 'build numbers must have four digits')
    rc, _, _ = build_py('--build-number', '3043', '--product', 'FM-1_9263043')
    check(rc != 0, 'explicit product and build-number are mutually exclusive')
    c = config()
    if c:
        check(c['target'] == 'fm1' and c['product_define'] == '-DBR_PRODUCT=BR_PRODUCT_ID_FM1' and
              c['identity'] == 'FM-1_9261389' and c['app_slot_hex'] == '0x8DFBC' and c['package_layout'] == 'fm1' and
              not c['polyphony'] and c['output_dir'] == 'build' and c['link_ready'] and
              '-DBR_CAP_POLY=1' not in c['cflags'] and '-DBR_CAP_POLY=0' not in c['cflags'],
              'build.py default target is fm1: FM-1 define, FM-1_9261389, slot 0x8DFBC, fm1 layout, no poly, build')
    c = config('--target', 'smk37')
    if c:
        check(c['target'] == 'smk37' and c['product_define'] == SMK and c['identity'] == 'SMK-37 Pro_9650001' and
              c['app_slot_hex'] == '0x97400' and c['package_layout'] == 'smk37' and not c['polyphony'] and
              '-DBR_CAP_POLY=0' in c['cflags'] and c['output_dir'] == 'build/smk37',
              'build.py --target smk37: SMK define, SMK-37 Pro_9650001, slot 0x97400, smk37 layout, build/smk37, '
              'poly off (-DBR_CAP_POLY=0) outside device candidates, as in JamesOS-K')
    c = config('--target', 'smk37', '--pattern-device-candidate', '--app-opt', 'Oz')
    if c:
        check(c['polyphony'] and '-DBR_CAP_POLY=1' in c['cflags'] and '-DBR_CAP_POLY=0' not in c['cflags'],
              'build.py --target smk37 --pattern-device-candidate: poly on (-DBR_CAP_POLY=1)')
    c = config('--target', 'smk37', '--pattern-device-candidate', '--app-opt', 'Oz', '--no-polyphony')
    if c:
        check(not c['polyphony'] and '-DBR_CAP_POLY=0' in c['cflags'] and '-DBR_CAP_POLY=1' not in c['cflags'],
              'build.py --target smk37 --pattern-device-candidate --no-polyphony: -DBR_CAP_POLY=0')
    poly_checks()
    ui_checks()
    for target, bad in (('fm1', 'SMK-37 Pro_9260012'), ('smk37', 'FM-1_9262153'), ('fm1', 'FM-2_1'),
                        ('smk37', 'SMK-37_1'), (None, 'SMK-37 Pro_1')):
        args = (['--target', target] if target else []) + ['--product', bad]
        rc, out, err = build_py(*args)
        check(rc == 2 and '--product must be' in err and not out,
              f'build.py {" ".join(args)}: wrong-prefix identity refused')
    # The WP0 reference commands predict the reference build-settings.json,
    # with the project transfer SHA-256 source added since those builds.
    # FM-1_9262153 was cut before polyphony was the FM-1 default: --no-polyphony is that build.
    refs = (('fm1', ['--product', 'FM-1_9262153', '--ui-hd', '--no-polyphony'], 'build-settings-fm1-2153.json', set()),
            ('smk37', ['--target', 'smk37', '--product', 'SMK-37 Pro_9260012'],
             'build-settings-smk37-9260012.json',
             {'ui_hd'}))   # JamesOS-K had no HD option; the unified build records ui_hd
    for name, args, ref, extra_ok in refs:
        c = config('--pattern-device-candidate', '--app-opt', 'Oz', '--diagnostics', 'distribution', *args)
        if not c:
            continue
        want = json.loads((ROOT / 'tests/baselines' / ref).read_text())
        want['per_source_optimization']['src/pattern_sha256.c'] = 'Oz'
        got, pending = c['build_settings'], {p.split('.')[0] for p in c['build_settings_pending']}
        same = [k for k in want if k in got and k not in pending and got[k] == want[k]]
        differ = [k for k in want if k in got and k not in pending and got[k] != want[k]]
        missing = [k for k in want if k not in got]
        check(got['product_identity'] == c['identity'] and got['requested_defines'] == [],
              f'{name}: product identity and requested compiler defines are recorded')
        check(got['app_version'] == c['version'] and got['compiler_flags'] == c['cflags'] and
              got['sdk_required_sha256'] == {key: pin['sha256'] for key, pin in c['sdk']['pin'].items()},
              f'{name}: version, compiler flags and SDK pins are recorded')
        extra = sorted(set(got) - set(want) - extra_ok -
                       {'product_identity', 'app_version', 'requested_defines', 'compiler_flags', 'sdk_required_sha256'})
        check(not differ and not missing and not extra,
              f'{name}: predicted build-settings.json equals {ref} plus transfer source on {len(same)} keys '
              f'(build-output keys not compared: {", ".join(sorted(pending))})'
              + (f'\n  differ {differ} missing {missing} extra {extra}' if differ or missing or extra else ''))
    c = config('--pattern-device-candidate', '--app-opt', 'Oz', '--diagnostics', 'distribution',
               '--build-number', '3045', '--define', 'BR_MODEL_MOD_EXPERIMENT=1')
    if c:
        check(c['build_settings']['requested_defines'] == ['BR_MODEL_MOD_EXPERIMENT=1'],
              'explicit build definitions are recorded for reproduction')


if __name__ == '__main__':
    header_checks()
    build_py_checks()
    if FAILED:
        raise SystemExit(f'{len(FAILED)} product switch check(s) failed')
    print('product switch: product.h selection, capabilities and guards; build.py targets: passed')
