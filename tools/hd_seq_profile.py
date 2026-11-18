#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Drawing cost of the sequencer screens (HD interface), host only (callgrind instruction counts).

Usage: python3 tools/hd_seq_profile.py OUT [--work verify-seqprof]
(BR_SEQ_2X8=0 or 1 in the environment is passed to every build, as in tools/hd_verify.py.)

Covers the cases tools/hd_verify.py's 'profile' stage does not: the tonal and drum step grids while playing,
with a step held while every knob turns, on a 64-step pattern (bank 1 and bank 3), at 32 drum steps, and
while the drum lane changes. Reports, per case, instructions per drawn frame (br_draw inclusive, split with
'L prof' marks) and per forced full redraw of the same state (20 redraws averaged). The standard interface was
removed, so there is no standard column and no HD/standard ratio target any more: compare the absolute counts with
an earlier report instead."""
import argparse, os, re, shutil, subprocess, sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import hd_verify as hv
from hd_verify import S, W

# Kit lanes are picked with SELECT (the black keys are inert on the step grid since 24c2dfe,
# which also dropped hd_verify.B); lane_moves() turns a lane sequence into SELECT detents.


def lane_moves(x, lanes, cur=0, pause=350):
    for lane in lanes:
        if lane != cur:
            x.e('select', 1 if lane > cur else -1, abs(lane - cur))
        cur = lane
        x.t(pause)
    return cur



def held_knobs(s, key):
    s.n(key, 1); s.t(300)
    for k in (1, 2, 3, 4):
        for d in (1, 1, 2, -1, 3, -2):
            s.e('knob%d' % k, d); s.t(60)
    s.t(200)


def states():
    """(label, actions-to-reach-state, actions-to-profile). Each state is profiled while playing."""
    s = []
    def T(label, setup, prof): s.append((label, setup, prof))
    T('T1 seq playing (16)', lambda x: None, lambda x: x.t(2000))
    T('T1 seq held step + knobs', lambda x: None, lambda x: (held_knobs(x, W[8]), x.n(W[8], 0), x.t(300)))
    T('T1 seq playing (64, bank 1)',
      lambda x: (x.b('seq'), x.t(300), x.e('knob1', 48), x.t(300), x.b('seq'), x.t(1200)), lambda x: x.t(2500))
    T('T1 seq playing (64, bank 3)', lambda x: (x.b('oct_up'), x.b('oct_up'), x.t(600)), lambda x: x.t(2500))
    T('T1 seq held step + knobs (64)', lambda x: None, lambda x: (held_knobs(x, W[4]), x.n(W[4], 0), x.t(300)))
    T('T1 seq bank changes (64)', lambda x: None,
      lambda x: [(x.b('oct_down'), x.t(500)) for _ in range(2)] + [(x.b('oct_up'), x.t(500)) for _ in range(3)])
    T('DK seq playing (16)',
      lambda x: (x.b('oct_down'), x.b('oct_down'), x.b('oct_down'), x.t(300), x.e('algorithm', 3), x.t(500),
                 [x.tap(k) for k in (0, 4, 7, 12, 14, 19, 21, 24)], x.e('select', 1), x.t(200),
                 [x.tap(k) for k in (2, 6, 9, 16, 23)], x.e('select', -1), x.t(1000)),
      lambda x: x.t(2000))
    T('DK seq held step + knobs', lambda x: None, lambda x: (held_knobs(x, W[10]), x.n(W[10], 0), x.t(300)))
    T('DK lane changes', lambda x: None, lambda x: lane_moves(x, (1, 2, 3, 0, 2, 1)))
    T('DK seq playing (32, bank 2)',
      lambda x: (x.b('seq'), x.t(300), x.e('knob1', 16), x.t(300), x.b('seq'), x.t(1200), x.b('oct_up'), x.t(800)),
      lambda x: x.t(2500))
    return s


def steady_session():
    s = S(); s.t(400); s.b('seq')
    for k in (0, 4, 7, 9, 14, 21, 24): s.tap(k)
    s.b('play'); s.t(500)
    for i, (label, setup, prof) in enumerate(states()):
        s.L('prof setup %d' % i); setup(s)
        s.L('prof ' + label); prof(s)
    s.L('prof end')
    return s.lines


def bench_session():
    s = S(); s.t(400); s.b('seq')
    for k in (0, 4, 7, 9, 14, 21, 24): s.tap(k)
    s.b('play'); s.t(500)
    for label, setup, prof in states():
        setup(s); s.t(200)
        if 'held' in label:                    # the held state itself, mid-edit
            key = W[8] if label.startswith('T1 seq held step + knobs') and '64' not in label else (W[4] if '64' in label else W[10])
            s.n(key, 1); s.t(300); s.e('knob2', 1); s.t(100); s.bench(label); s.n(key, 0); s.t(300)
        elif 'lane' in label:
            s.e('select', 2); s.t(200); s.bench(label); s.e('select', -2); s.t(200)
        elif 'bank changes' in label:
            continue
        else:
            s.bench(label)
    return s.lines


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('out'); ap.add_argument('--work', default='verify-seqprof')
    ap.add_argument('--target', choices=hv.host_target.TARGETS, default='fm1', help='product (default fm1)')
    a = ap.parse_args()
    if not a.work.startswith(('verify', 'perf')): raise SystemExit('--work must start with "verify" or "perf"')
    if not shutil.which('valgrind'): raise SystemExit('valgrind is missing (callgrind counts are this tool\'s output)')
    prefix = hv.select_target(a.target)        # SMK-37 Pro: product defines in every build (FM-1: none)
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    work = hv.ROOT / 'build' / (prefix + a.work); work.mkdir(parents=True, exist_ok=True)
    tree = hv.copy_tree(work / 'tree-prof')
    hv.sub(tree / 'firmware/src/instrument_ui.c', 'static void br_draw(void)\n{', 'static __attribute__((noinline)) void br_draw(void)\n{', 1)
    pd = out / 'callgrind'; pd.mkdir(exist_ok=True)
    for f in pd.glob('*'): f.unlink()
    res = {}
    for hd in (1,):
        b = hv.build(tree, tree / ('build/hd_diff_prof%d' % hd), hd, ['-g'])
        for kind, lines, fn, split in (('bench', bench_session(), 'hdv_bench_draws', '--dump-after=hdv_bench_draws'),
                                       ('steady', steady_session(), 'br_draw', '--dump-before=hdv_mark')):
            (out / ('session-%s.txt' % kind)).write_text('\n'.join(lines) + '\n')
            pre = str(pd / ('%s-hd%d.out' % (kind, hd)))
            e = dict(os.environ, HDV_NOCMP='1', HDV_BENCH_NOFORK='1')
            r = subprocess.run(['valgrind', '--tool=callgrind', '--toggle-collect=' + fn, split, '--callgrind-out-file=' + pre, str(b)],
                               input='\n'.join(lines) + '\nQ\n', text=True, capture_output=True, cwd=tree, env=e)
            if r.returncode: print('valgrind failed: ' + r.stderr[-800:]); sys.exit(1)
            labels = [l[2:] for l in lines if l.startswith('L ' + ('bench' if kind == 'bench' else 'prof'))]
            draws = {}
            for m in re.finditer(r'^H \d+ \w+ .*\|(.*)\|$', r.stdout, re.M): draws[m.group(1)] = draws.get(m.group(1), 0) + 1
            parts = hv.callgrind_parts(pre)
            if kind == 'steady': parts = parts[1:]
            res[(kind, hd)] = (labels, parts, draws)
    rep = []
    for kind in ('steady', 'bench'):
        lh, ph, dh = res[(kind, 1)]
        rep.append('[%s] %s' % (kind, 'instructions per drawn frame while playing, br_draw inclusive'
                                if kind == 'steady' else 'instructions per forced full redraw (20 averaged)'))
        rep.append('   %-34s %10s %6s  %s' % ('case', 'HD', 'frames', 'top self cost'))
        for i, lab in enumerate(lh):
            if lab.startswith(('prof setup', 'prof end')) or i >= len(ph): continue
            if kind == 'bench': x, nf = ph[i][1] / 20, '20'
            else:
                nh = dh.get(lab, 0); x, nf = ph[i][1] / max(1, nh), '%d' % nh
            rep.append('   %-34s %10.0f %6s  %s' % (lab.split(' ', 1)[1], x, nf, '; '.join(hv.top_functions(ph[i][0], 4))))
    (out / 'report.txt').write_text('\n'.join(rep) + '\n')
    print('\n'.join(rep))


if __name__ == '__main__':
    main()
