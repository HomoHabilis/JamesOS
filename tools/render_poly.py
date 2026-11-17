#!/usr/bin/env python3
"""P1 scratch: render polyphony states of the real firmware on the host (FM-1 or SMK-37 Pro, HD interface).

usage: render_poly.py TREE OUTDIR --target fm1|smk37 [--scale 1] [--only a,b]
(--hd is still accepted and changes nothing: the standard interface was removed.)

TREE is an export with host DSP objects built for the same product and -DBR_CAP_POLY=1 (FM-1).
The app is built from p1-scratch/tests/poly_ui_host.h (dinghy.c + split audio unit, so polyphony really
runs, entered by dinghy.c's host model of the foreground transition). A copy of firmware/ gets a hook in
br_draw_body(): P1_MODEL=<plaits model> loads that model on the selected track once, P1_MSG=<text> shows a
message once at P1_MSG_T ms. Scripts and PNG writing come from TREE/tools/render_screens.py."""
import argparse, os, shutil, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
HOOK = r'''
#include <stdlib.h>
#include <string.h>
static void p1_hook(void)
{
    static int model_done, msg_done;
    const char *m = getenv("P1_MODEL"), *s = getenv("P1_MSG"), *t = getenv("P1_MSG_T");
    if (!model_done && m) {
        br_sound_t sound; uint32_t p = br_ui.part;
        model_done = 1;
        br_preset_sound(BR_FIRST_PLAITS + (uint32_t)atoi(m), &sound);
        br_part[p].revision++; br_part[p].sound = sound; RING_PUBLISH(); br_part[p].revision++;
        br_ui.redraw = 1u;
    }
    if (!msg_done && s && t && fm1_ms >= (uint32_t)atoi(t)) { msg_done = 1; br_message(s); }
}
'''
K = lambda *keys: sum(1 << k for k in keys)
CHORD = K(0, 4, 7, 11, 14, 16)                 # six keys of the FM-1 keyboard
# name -> (steps, model or None, message or None)
STATES = {
    'poly-hint-home': ([('wait', 600)], 8, None),
    'poly-hint-analog': ([('wait', 600)], 0, None),
    'poly-hint-env': ([('btn', 'env'), ('wait', 600)], 8, None),
    'poly-hint-chord': ([('wait', 600), ('keys', CHORD), ('wait', 300)], 8, None),
    'poly-entry-message': ([('wait', 600), ('enc', 'algorithm', 1), ('wait', 200)], 8, None),
    'poly-punch-page-normal-model': ([('wait', 600), ('keys', CHORD), ('keys', CHORD | K(25))], 8, None),
    'poly-punch-page-loan': ([('wait', 600), ('keys', CHORD), ('keys', CHORD | K(25))], 18, None),
    'poly-punch-page2-loan': ([('wait', 600), ('keys', K(25)), ('btn', 'oct_up')], 18, None),
    'poly-punch-echo-refused': ([('wait', 600), ('keys', CHORD), ('keys', CHORD | K(25)), ('keys', CHORD | K(25, 8))], 18, None),
    'poly-punch-filter-loan': ([('wait', 600), ('keys', CHORD), ('keys', CHORD | K(25)), ('keys', CHORD | K(25, 13))], 18, None),
    'poly-punch-echo-allowed': ([('wait', 600), ('keys', CHORD), ('keys', CHORD | K(25)), ('keys', CHORD | K(25, 8))], 8, None),
    'poly-echo-message': ([('wait', 600)], 18, 'ECHO UNAVAILABLE IN POLY'),
    'poly-seq-grid': ([('wait', 600), ('btn', 'seq'), ('wait', 300)], 8, None),
}


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('tree'); ap.add_argument('out')
    ap.add_argument('--target', choices=('fm1', 'smk37'), required=True); ap.add_argument('--hd', action='store_true', help=argparse.SUPPRESS)  # deprecated no-op
    ap.add_argument('--scale', type=int, default=1); ap.add_argument('--only', default='')
    a = ap.parse_args()
    tree = Path(a.tree).resolve(); out = Path(a.out).resolve(); out.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(tree / 'tools')); sys.path.insert(0, str(tree / 'tests'))
    import render_screens as rs
    flags = list(rs.FLAGS) + (['-DBR_PRODUCT=BR_PRODUCT_ID_FM1', '-DBR_CAP_POLY=1'] if a.target == 'fm1'
                              else ['-DBR_PRODUCT=BR_PRODUCT_ID_SMK37'])
    flags += ['-DFM1_DEVELOPMENT_DIAGNOSTICS=0']
    work = tree / 'build' / f"p1-render-{a.target}"
    if work.exists(): shutil.rmtree(work)
    work.mkdir(parents=True)
    root = work / 'tree'; root.mkdir()
    shutil.copytree(tree / 'firmware', root / 'firmware')
    shutil.copytree(tree / 'tests', root / 'tests', ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copy2(HERE / 'tests/poly_ui_host.h', root / 'tests/poly_ui_host.h')
    (root / 'assets').symlink_to(tree / 'assets')
    ui = root / 'firmware/src/instrument_ui.c'; s = ui.read_text()
    anchor = 'static void br_draw_body(void)\n{\n'
    assert s.count(anchor) == 1
    ui.write_text(s.replace(anchor, HOOK + anchor + '    p1_hook();\n'))
    (root / 'tests/p1_render.c').write_text('#include "poly_ui_host.h"\n')
    objs = sorted((tree / 'build/host/braids').glob('*.o')) + sorted((tree / 'build/host/plaits_reference').glob('*.o'))
    binary = work / 'dinghy'
    r = subprocess.run(['c++', '-x', 'c', *flags, 'tests/p1_render.c', 'firmware/src/audio_fixed8_plan_unit.c',
                        '-x', 'none', *map(str, objs), '-o', str(binary)], cwd=root, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr); raise SystemExit('build failed')
    for name, (steps, model, msg) in STATES.items():
        if a.only and name not in a.only.split(','): continue
        script, at = rs.script_for([('wait', 2600)] + steps)   # past the intro of both products
        (work / 'script.txt').write_text(script)
        env = dict(os.environ)
        if model is not None: env['P1_MODEL'] = str(model)
        if msg: env['P1_MSG'] = msg; env['P1_MSG_T'] = str(at - 200)
        r = subprocess.run([str(binary), str(work / name), str(work / 'script.txt')], cwd=root,
                           capture_output=True, text=True, env=env)
        (work / (name + '.log')).write_text(r.stdout + r.stderr)
        shot = work / ('%s-%d.ppm' % (name, at))
        if not shot.exists(): print('no screenshot for', name, r.returncode); continue
        w, h, rgb = rs.ppm(shot); rs.png(out / (name + '.png'), w, h, rgb, a.scale); print('wrote', name)


if __name__ == '__main__':
    main()
