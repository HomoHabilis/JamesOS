#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Which widget kinds can the HD interface actually draw? Host only; no hardware is touched.

Usage: python3 tools/hd_kinds.py OUT [--work verify-kinds] [--seq2x8 0|1]
(--hd 1 is still accepted and changes nothing; --hd 0 is refused: the standard interface was removed.)

Copies the tree, patches the two br_widget_draw call sites of instrument_ui.c (the parameter cards and the
perform page's knobs 3-4) to log every widget drawn as (page, screen, part, preset, knob, kind, source, target,
label), and builds tests/hd_diff.c with it. Runs the hd_verify session (tools/hd_verify.py) followed by a
coverage session: every page and screen for each synth track, each kit type (Braids, Plaits, 80s, 90s), the
global pages, both MIDI pages, both punch pages with every punch key, the sequencer (grid, held steps, settings)
and arp / retrig, with every knob stepped one detent at a time through its whole range (so every choice of a
source, target or choice control is drawn). Writes OUT/kinds.txt: the distinct (context, kind) rows, and per
kind the number of contexts; and OUT/kind-counts.txt. The tracked sources are never modified."""
import argparse, os, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
import hd_verify as V   # noqa: E402

LOG = r'''
static uint64_t hdk_seen[8192];
static void hdk_log(uint32_t k, uint32_t kind, const br_control_t *c, uint32_t where)
{
    uint32_t preset = br_part[br_ui.part].sound.preset, src = br_source_control_id(k), tgt = br_target_control_id(k);
    uint64_t key = ((((((((uint64_t)br_ui.page * 16u + br_ui.screen[br_ui.page]) * 8u + br_ui.part) * 256u + preset) * 4u + k)
                    * 128u + kind) * 32u + src) * 32u + tgt) * 4u + where;
    uint32_t h = (uint32_t)(key ^ key >> 29) * 2654435761u >> 19;
    while (hdk_seen[h] && hdk_seen[h] != key + 1u) h = (h + 1u) & 8191u;
    if (hdk_seen[h]) return;
    hdk_seen[h] = key + 1u;
    fprintf(stdout, "K page=%u screen=%u part=%u preset=%u k=%u kind=%u src=%u tgt=%u where=%u seqedit=%u gate=%u retrig=%u |%s|\n",
            br_ui.page, br_ui.screen[br_ui.page], br_ui.part, preset, k, kind, src, tgt, where, br_seq_step_edit(),
            br_gate_page(), br_retrig_page(), c->label);
}
'''


def patch(tree):
    ui = tree / 'firmware/src/instrument_ui.c'
    V.sub(ui, 'static void br_draw_control(uint32_t k, const br_control_t *c, uint32_t kind,',
          LOG + 'static void br_draw_control(uint32_t k, const br_control_t *c, uint32_t kind,', 1)
    V.sub(ui, '        br_widget_draw(kind, 0, BR_PARAM_ICON_Y, 60, q, variant, env, phase);',
          '        if (!offset) hdk_log(k, kind, c, 0u);\n        br_widget_draw(kind, 0, BR_PARAM_ICON_Y, 60, q, variant, env, phase);', 1)
    V.sub(ui, '                    uint32_t kind=br_control_widget(k,c);\n',
          '                    uint32_t kind=br_control_widget(k,c);\n                    if(!offset) hdk_log(k,kind,c,1u);\n', 1)


def sweep(s, where, knobs=(1, 2, 3, 4), detents=40):
    """Every knob one detent at a time up and down (every choice drawn), then the extremes."""
    for k in knobs:
        kn = 'knob%d' % k
        s.L('%s: knob%d detents' % (where, k))
        s.e(kn, -16, 10); s.e(kn, 1, detents); s.e(kn, 16, 10); s.e(kn, -1, detents); s.e(kn, -16, 10)
        s.e(kn, 4, 4); s.t(200)


def coverage_session():
    s = V.S()
    s.L('cov: boot'); s.t(400)
    seq_pages = [('home', 2), ('env', 3), ('fx', 5), ('glo', 7), ('seq', 2), ('arp', 1), ('lfo', 5), ('edit', 3)]
    # T1..T3 (synth), then DK with each kit (preset encoder steps from the session's kit; the log names the preset).
    for p, kit in ((0, 0), (1, 0), (2, 0), (3, 0), (3, 1), (3, 2), (3, 3), (3, 4)):
        s.L('cov: track %d' % p); s.lines.append('p %d' % p); s.t(600)
        if p == 3 and kit: s.L('cov: kit +1'); s.e('presets', 1); s.t(600)
        for pg, screens in seq_pages:
            for sc in range(screens):
                s.L('cov %d/%d %s/%d' % (p, kit, pg, sc)); s.b(pg); s.t(300)
                s.lines.append('p %d' % p); s.t(100)      # the mixer knobs select a track: keep this one
                sweep(s, 'cov %d/%d %s/%d' % (p, kit, pg, sc))
            s.b('home'); s.t(200)
        # held step edit on the sequencer grid (tonal key / drum key), knobs swept
        s.L('cov %d/%d seq held' % (p, kit)); s.lines.append('p %d' % p); s.b('seq'); s.t(200)
        s.tap(4); s.t(100); s.n(4, 1); s.t(300)
        sweep(s, 'cov %d/%d seq held' % (p, kit), detents=30)
        s.n(4, 0); s.t(300)
        if p == 3:   # retrig edit on the arp page: hold a hit step
            s.L('cov %d/%d retrig' % (p, kit)); s.b('arp'); s.t(200); s.n(4, 1); s.t(200)
            sweep(s, 'cov %d/%d retrig held' % (p, kit), (1, 2), detents=20); s.n(4, 0); s.t(300)
        s.b('home'); s.t(300)
    s.L('cov: kit back'); s.e('presets', -4); s.t(600); s.lines.append('p 0'); s.t(600)
    # MIDI pages (GLO + OCT- chord) and the punch pages with every punch key, on T1 and on DK.
    for p in (0, 3):
        s.lines.append('p %d' % p); s.t(600)
        s.L('cov midi %d' % p); s.h('oct_down', 1); s.h('glo', 1); s.h('glo', 0); s.h('oct_down', 0); s.t(500)
        sweep(s, 'cov midi/0 %d' % p)
        s.e('select', 1); s.t(400); sweep(s, 'cov midi/1 %d' % p); s.b('home'); s.t(600)
        for key in (1, 8, 13, 20):
            s.L('cov punch %d key %d' % (p, key)); s.n(25, 1); s.t(300); s.n(key, 1); s.t(400)
            sweep(s, 'cov punch/0 %d %d' % (p, key), detents=12)
            s.b('oct_up'); s.t(300); sweep(s, 'cov punch/1 %d %d' % (p, key), detents=40)
            s.b('oct_down'); s.t(300); s.n(key, 0); s.n(25, 0); s.t(500)
    return s.lines


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('out'); ap.add_argument('--work', default='verify-kinds')
    ap.add_argument('--seq2x8', default=''); ap.add_argument('--hd', type=int, default=1, help=argparse.SUPPRESS)
    a = ap.parse_args()
    if a.hd != 1: raise SystemExit('--hd %d: the standard interface was removed; HD is the only interface' % a.hd)
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    work = ROOT / 'build' / ('hd-verify-' + a.work); work.mkdir(parents=True, exist_ok=True)
    tree = V.copy_tree(work / 'tree-kinds'); patch(tree)
    extra = ['-DBR_SEQ_2X8=' + a.seq2x8] if a.seq2x8 != '' else []
    b = V.build(tree, tree / 'build/hd_kinds', 1, extra)
    lines = V.session() + coverage_session()
    (out / 'session.txt').write_text('\n'.join(lines) + '\n')
    code, so, se = V.run(b, lines, {'HDV_NOCMP': '1'}, cwd=tree)
    rows = [l for l in so.splitlines() if l.startswith('K ')]
    (out / 'kinds.txt').write_text('\n'.join(rows) + '\n')
    counts = {}
    for r in rows:
        m = re.search(r' kind=(\d+) ', r); counts.setdefault(int(m.group(1)), []).append(r)
    rep = ['exit=%d draws=%d rows=%d' % (code, so.count('\nH '), len(rows))]
    for kind in sorted(counts): rep.append('kind %3d: %4d contexts  e.g. %s' % (kind, len(counts[kind]), counts[kind][0]))
    (out / 'kind-counts.txt').write_text('\n'.join(rep) + '\n')
    print('\n'.join(rep))
    if se.strip(): print(se[-2000:])


if __name__ == '__main__':
    main()
