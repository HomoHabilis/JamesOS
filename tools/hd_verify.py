#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Dynamic (partial-repaint) verification of the HD interface, host only; no hardware is touched.

Usage: python3 tools/hd_verify.py OUT [--work verify] [--only diff,strips,asan,cost,profile] [--quick]
                                [--target fm1|smk37]
(--target smk37 adds -DBR_PRODUCT=BR_PRODUCT_ID_SMK37 -DBR_CAP_POLY=0 to every build and works in
build/hd-verify-smk37-<work>; FM-1, the default, is unchanged.)
(BR_SEQ_2X8=0 or 1 in the environment adds -DBR_SEQ_2X8=N to every build.)
Builds tests/hd_diff.c (the real app, unity-built like tools/ui_emulator.c; HD, the only interface), runs one long
scripted session (every knob of every page and track in small and large steps, page and track changes, playback
with notes and drum hits, mute/solo, held-step edits, list scrolling, punch, the SAVE menu and name editor,
tempo) and after every frame the firmware draws compares the emulated LCD, accumulated through the firmware's own
partial repaints, with a complete redraw of the same state at the same tick (see tests/hd_diff.c). It writes a
report and, per distinct mismatch, OUT/<build>-<n>.png: [screen before | accumulated LCD | full redraw |
diff] cropped around the bounding box and upscaled 4x. 'strips' rebuilds copies of the sources with every strip
loop stepped at 22 and 32 rows and compares the LCD hash after every draw with the unmodified build; 'asan'
runs the session under -fsanitize=address,undefined; 'cost' counts hd_blend calls and pixels written per full
redraw, out-of-canvas hd_blend/hd_get requests, writes outside the strip and stale polyline joints (instrumented
copies of gfx.c / gfx_hd.h / instrument_widgets.c; the tracked sources are never modified); 'profile' runs
callgrind (instructions per forced full redraw and per frame drawn while playing, with the top self-cost
functions); 'steadycost' (not in the default list) counts drawing work per second per page. Results go to
OUT/report-<stages>.txt. Pass --work (a name starting with 'verify') so parallel runs do not collide. Known
method artefact: br_draw_meter_line advances the meter fall-off on every call, so the extra reference redraw
shows the mixer meters one step lower.
The standard interface was removed (BR_UI_HD=0 no longer compiles), so every stage now builds HD only: the old
'control' stage (the same diff on the standard build) is gone and is refused, and the 'strips', 'asan', 'cost',
'steadycost' and 'profile' stages no longer have a standard column or baseline."""
import argparse, glob, os, re, shutil, struct, subprocess, sys, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True        # leave the tracked tests/__pycache__ alone
sys.path.insert(0, str(ROOT / 'tests'))
from run_drive import FLAGS            # the same build flags as the host tests
import host_target                     # product defines for --target smk37 (FM-1: none)

BTN = dict(oct_down=0, oct_up=1, fx=2, sel=3, env=4, lfo=5, edit=6, glo=7, home=8, save=9, arp=10, seq=11, play=12, rec=13)
SAN_CXX = 'g++'                        # this host's clang has no sanitizer runtime; gcc's libasan/libubsan are used
SAN_GCC = ['-DFM1_PLAITS_COLD_INIT_SIZE=0', '-Wno-attributes']   # gcc has no minsize (an audio-only size attribute)
ENC = dict(select=0, algorithm=1, knob1=2, knob2=3, knob3=4, knob4=5, presets=6)
SEQ2X8 = os.environ.get('BR_SEQ_2X8', '')   # BR_SEQ_2X8=0/1 in the environment is passed to every build
SMK = False                            # --target smk37: SMK-only session parts, pattern song + MIDI clock as on the device
PS_PAGE = 10                           # BR_PAGE_PS (dinghy.c page enum, with BR_PATTERN_SONG_EXPERIMENT)
# The SMK host app is built through this wrapper, as the device builds it: the pattern-song and MIDI clock
# experiments, with a fixed slot fixture standing in for the target pattern store (render_screens.py --target smk37).
SMK_WRAPPER = (
    '#define BR_PATTERN_SONG_EXPERIMENT 1\n'
    '#include "{hd_diff}"\n'
    'fm1_pattern_song br_ps_state = {{.used = 7, .queued = FM1_PATTERN_NONE}};\n'
    'int br_ps_select(uint32_t target) {{ br_ps_state.active = target; return 1; }}\n'
    'int br_ps_copy_pattern(uint32_t target) {{ (void)target; return 0; }}\n'
    'int br_ps_paste_pattern(uint32_t target) {{ (void)target; return 0; }}\n'
)


# ------------------------------------------------------------------------------------------------ the session
class S:
    def __init__(self): self.lines = []
    def L(self, text): self.lines.append('L ' + text)
    def b(self, name): self.lines.append('b %d' % BTN[name])
    def h(self, name, on): self.lines.append('h %d %d' % (BTN[name], on))
    def e(self, name, d, n=1):
        for _ in range(n): self.lines.append('e %d %d' % (ENC[name], d))
    def n(self, key, on): self.lines.append('n %d %d' % (key, on))
    def tap(self, key): self.n(key, 1); self.n(key, 0)
    def t(self, ms): self.lines.append('t %d' % ms)
    def bench(self, label, n=20): self.L('bench ' + label); self.lines.append('B %d' % n)
    def v(self, page, screen): self.lines.append('v %d %d' % (page, screen))      # jump to a page / screen
    def p(self, part): self.lines.append('p %d' % part)                         # select a track


def knob_sweep(s, where, knobs=(1, 2, 3, 4)):
    for k in knobs:
        kn = 'knob%d' % k
        s.L('%s: knob%d +1 x4' % (where, k)); s.e(kn, 1, 4)
        s.L('%s: knob%d +5 x3' % (where, k)); s.e(kn, 5, 3)
        s.L('%s: knob%d -1 x3' % (where, k)); s.e(kn, -1, 3)
        s.L('%s: knob%d to max' % (where, k)); s.e(kn, 16, 9)
        s.L('%s: knob%d to min' % (where, k)); s.e(kn, -16, 18)
        s.L('%s: knob%d +2 x3' % (where, k)); s.e(kn, 2, 3)
        s.L('%s: knob%d highlight timeout' % (where, k)); s.t(1300)


PAGES = [('home', 2), ('env', 3), ('fx', 5), ('glo', 7), ('seq', 2), ('arp', 1), ('lfo', 5), ('edit', 1)]   # lfo: LFO 1/2, PATCH 1/2, LFO DEPTHS
PART = ['T1', 'T2', 'T3', 'DK']


W = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]     # white key of step 1..16


def seq_session(s):
    """The tonal step grid while playing (on the SEQ page, T1): toggles, held steps under the playhead with every
    knob, long notes, 17 / 32 / 64 steps with every bank, and the length set back to 16."""
    s.L('seq: toggles while playing')
    for k in (1, 2, 3, 10, 11, 2, 15, 3): s.tap(W[k]); s.t(170)
    s.t(1500)
    s.L('seq: hold step 9 under the playhead'); s.n(W[8], 1); s.t(2200)
    for k in (1, 2, 3, 4):
        s.L('seq: held step 9 knob%d' % k); s.e('knob%d' % k, 1, 3); s.e('knob%d' % k, 9, 4); s.e('knob%d' % k, -7, 3); s.t(300)
    s.L('seq: held step 9 nudge'); s.b('oct_up'); s.b('oct_up'); s.t(300); s.b('oct_down'); s.t(300)
    s.L('seq: release step 9'); s.n(W[8], 0); s.t(1500)
    s.L('seq: long note on step 2'); s.n(W[1], 1); s.t(450); s.e('knob2', 16, 4); s.e('knob2', 5, 3); s.t(300); s.n(W[1], 0); s.t(2000)
    for n, d in ((17, 1), (32, 15), (64, 32)):
        s.L('seq: length %d' % n); s.b('seq'); s.t(300); s.e('knob1', d); s.t(300); s.b('seq'); s.t(1200)
        for b in range((n + 15) // 16 - 1):
            s.L('seq: length %d bank %d' % (n, b + 2)); s.b('oct_up'); s.t(1300); s.tap(W[b * 3 % 16]); s.t(900)
        s.L('seq: length %d bank 1' % n); s.b('oct_down'); s.b('oct_down'); s.b('oct_down'); s.t(2500)
    s.L('seq: length 64 held step in bank 3'); s.b('oct_up'); s.b('oct_up'); s.t(400); s.n(W[4], 1); s.t(1300); s.e('knob1', -9, 4); s.t(400); s.n(W[4], 0)
    s.b('oct_down'); s.b('oct_down'); s.t(600)
    s.L('seq: length back to 16'); s.b('seq'); s.t(300); s.e('knob1', -48); s.t(300); s.b('seq'); s.t(1500)


def drum_session(s):
    """The drum grid while playing: every lane, toggles in each, a held step per knob, 32 steps and both banks."""
    cur = 0                                       # lanes are picked with SELECT (black keys are inert on the grid)
    for lane in (0, 1, 2, 3, 1):
        s.L('drum: lane %d while playing' % (lane + 1)); s.e('select', 1 if lane > cur else -1, abs(lane - cur)); cur = lane; s.t(900)
        s.L('drum: lane %d toggles' % (lane + 1))
        for k in (lane, lane + 5, lane + 9): s.tap(W[k]); s.t(200)
        s.t(800)
    s.L('drum: hold step 11 under the playhead'); s.n(W[10], 1); s.t(2200)
    for k in (1, 2, 3, 4):
        s.L('drum: held step knob%d' % k); s.e('knob%d' % k, 1, 3); s.e('knob%d' % k, 9, 4); s.e('knob%d' % k, -7, 3); s.t(300)
    s.L('drum: held condition codes'); s.e('knob4', 25, 4); s.e('knob4', 1, 6); s.t(300)
    s.L('drum: release step 11'); s.n(W[10], 0); s.t(1500)
    s.L('drum: length 32'); s.b('seq'); s.t(300); s.e('knob1', 16); s.t(300); s.b('seq'); s.t(1500)
    s.L('drum: bank 2'); s.b('oct_up'); s.t(1200); s.tap(W[3]); s.e('select', 1); s.t(2200)
    s.L('drum: bank 1'); s.b('oct_down'); s.t(1500)
    s.L('drum: length 16'); s.b('seq'); s.t(300); s.e('knob1', -16); s.t(300); s.b('seq'); s.t(800)
    s.L('drum: lane 1'); s.e('select', -1, 2); s.t(800)


def params_playing(s):
    """Every page with parameter graphics, knobs swept while playing with notes held: the LFO pages (ORBIT and
    LFO DEPTH follow the LFO), PATCH 1/2 with every source and target, LFO DEPTHS, ENV/SETUP, the HOME filter,
    GLOBAL filter / settings / heat / compressor / comp knobs, MIDI 1/2, sequencer settings and the arp; then the
    kit track's LFO, ENV/SETUP and the sample-kit PITCH."""
    def sweep(where, knobs=(1, 2, 3, 4), step=1, n=6):
        for k in knobs:
            kn = 'knob%d' % k
            s.L('%s: knob%d' % (where, k)); s.e(kn, step, n); s.t(250); s.e(kn, 9, 3); s.t(250); s.e(kn, -9, 6); s.t(300)
            s.e(kn, -step, n); s.t(250); s.e(kn, 4, 3); s.t(500)
    s.L('params playing: T1'); s.p(0); s.t(300); s.n(7, 1); s.t(200)
    for scr in range(5):                               # LFO 1, LFO 2, PATCH 1, PATCH 2, LFO DEPTHS
        s.L('params playing: lfo cycle %d' % scr); s.b('lfo'); s.t(900)
        sweep('params playing lfo cycle %d' % scr, n=17 if scr in (2, 3) else 6)
    s.L('params playing: env setup'); s.v(1, 2); s.t(900); sweep('params playing env setup', n=18)
    s.L('params playing: home filter'); s.v(0, 1); s.t(900); sweep('params playing home filter', (3, 4))
    for scr, name in ((1, 'filter'), (2, 'settings'), (6, 'heat'), (4, 'comp'), (5, 'comp knobs')):
        s.L('params playing: glo %s' % name); s.v(3, scr); s.t(900); sweep('params playing glo %s' % name)
    s.n(7, 0); s.t(200)
    s.L('params playing: midi'); s.h('oct_down', 1); s.h('glo', 1); s.h('glo', 0); s.h('oct_down', 0); s.t(700)
    sweep('params playing midi/0'); s.e('select', 1); s.t(500); sweep('params playing midi/1'); s.b('home'); s.t(500)
    s.L('params playing: seq settings'); s.v(4, 1); s.t(900); sweep('params playing seq settings', (1, 2, 3))
    s.L('params playing: arp'); s.v(5, 0); s.t(900); s.n(9, 1); sweep('params playing arp'); s.n(9, 0); s.t(300)
    s.L('params playing: DK'); s.p(3); s.t(500); s.n(4, 1)
    s.L('params playing: DK lfo'); s.v(7, 0); s.t(900); sweep('params playing DK lfo')
    s.L('params playing: DK env setup'); s.v(1, 2); s.t(900); sweep('params playing DK env setup')
    s.L('params playing: DK 80s pitch'); s.v(0, 0); s.e('presets', 1); s.t(900); sweep('params playing DK 80s', (1, 4))
    s.e('presets', -1); s.t(600); s.n(4, 0); s.p(0); s.t(500)


def smk_session(s):
    """SMK-37 Pro only: the text intro, the pattern-slot page (layout 12) with selections, saved / queued slots and
    playback, and the fader peek (the GLOBAL mixer shown while a fader moves) over several pages, playing too."""
    s.L('smk: intro'); s.lines.append('I'); s.t(300); s.lines.append('c'); s.t(400); s.e('knob1', 1, 2); s.t(1600)
    s.L('smk patterns: open (OCT- + SEQ)'); s.h('oct_down', 1); s.h('seq', 1); s.h('seq', 0); s.h('oct_down', 0); s.t(900)
    s.L('smk patterns: select'); s.tap(1); s.t(400); s.tap(3); s.t(400); s.tap(6); s.t(400)
    s.L('smk patterns: saved slots'); s.lines.append('S 31 255'); s.t(400); s.lines.append('S 2047 255'); s.t(400)
    s.L('smk patterns: queued'); s.lines.append('S 31 4'); s.t(500); s.lines.append('S 31 1'); s.t(500); s.lines.append('S 7 255'); s.t(400)
    s.L('smk patterns: REC/PLAY without a slot'); s.b('seq'); s.t(900)
    s.L('smk patterns: playing'); s.b('play'); s.t(1800); s.tap(1); s.t(600); s.b('play'); s.t(600)
    s.L('smk patterns: leave'); s.b('home'); s.t(800)
    s.L('smk peek: home'); s.lines.append('P 1 1200'); s.t(600); s.e('knob1', 1, 3); s.t(1200)
    s.L('smk peek: seq'); s.b('seq'); s.t(400); s.lines.append('P 1 1500'); s.t(2000)
    s.L('smk peek: cancelled by a page change'); s.lines.append('P 1 3000'); s.t(400); s.b('env'); s.t(800)
    s.L('smk peek: playing on seq'); s.b('seq'); s.b('play'); s.t(800); s.lines.append('P 1 3000'); s.t(1500); s.lines.append('P 0'); s.t(800)
    s.L('smk peek: DK'); s.e('algorithm', 3); s.t(600); s.lines.append('P 1 1500'); s.t(2000); s.e('algorithm', -3); s.t(400)
    s.L('smk: stop'); s.b('play'); s.t(800); s.b('home'); s.t(600)


def session(quick=False):
    s = S()
    s.L('boot'); s.t(400)
    if SMK: smk_session(s)
    # Notes with level meters (mixer) and the playing keyboard.
    s.L('T1 mixer: notes with signal'); s.b('glo'); s.t(200)
    for k in (7, 11, 14, 7, 2, 9):
        s.L('T1 mixer: hold key %d' % k); s.n(k, 1); s.t(400); s.L('T1 mixer: release key %d' % k); s.n(k, 0); s.t(250)
    s.L('T1 mixer: chord'); s.n(7, 1); s.n(11, 1); s.n(14, 1); s.t(900); s.n(7, 0); s.n(11, 0); s.n(14, 0); s.t(800)
    # T1 sequence: lit steps, playback, page changes while playing.
    s.L('T1 seq: enter steps'); s.b('seq')
    for k in (0, 4, 7, 9, 14, 21, 24): s.tap(k)
    s.L('T1 seq: play'); s.b('play'); s.t(2500)
    s.L('T1 seq: bank 2 while playing'); s.b('oct_up'); s.t(1200); s.b('oct_down'); s.t(800)
    for pg in ('home', 'env', 'glo', 'fx', 'lfo', 'seq'):
        s.L('T1 playing: page %s' % pg); s.b(pg); s.t(1500)
    s.L('T1 playing: held step 6 edit'); s.n(6, 1); s.t(300)
    for k in (1, 2, 3, 4):
        s.L('T1 playing: held step knob%d' % k); s.e('knob%d' % k, 1, 3); s.e('knob%d' % k, -2, 2)
    s.t(500); s.L('T1 playing: release held step'); s.n(6, 0); s.t(1500)
    seq_session(s)
    s.L('T1 playing: tempo up (GLO)'); s.b('glo'); s.b('glo'); s.t(300)
    s.L('T1 playing: GLO screen 1 knob1'); s.e('knob1', 3, 4); s.t(1500)
    s.L('T1 playing: track change T2'); s.e('algorithm', 1); s.t(1500)
    s.L('T2 playing: seq'); s.b('seq'); s.t(1000)
    for k in (2, 5, 10, 13, 17): s.tap(k)
    s.L('T2 playing: steps entered'); s.t(2000)
    s.L('T2 playing: punch loop'); s.n(25, 1); s.n(1, 1); s.t(1200); s.n(1, 0); s.n(25, 0); s.t(800)
    s.L('T2 playing: punch echo'); s.n(25, 1); s.n(8, 1); s.t(1200); s.n(8, 0); s.n(25, 0); s.t(800)
    s.L('T2 playing: punch filter'); s.n(25, 1); s.n(13, 1); s.e('knob1', 2, 4); s.t(800); s.n(13, 0); s.n(25, 0); s.t(800)
    s.L('T2 playing: record arm'); s.b('rec'); s.t(600); s.n(9, 1); s.t(200); s.n(9, 0); s.t(800); s.b('rec'); s.t(1500)
    s.L('T2 stop'); s.b('play'); s.t(1200)
    # Drum kit: hits, playing grid, kit mixer, mute / solo, retrig edit.
    s.L('DK: track change'); s.e('algorithm', 1, 2); s.t(800)
    s.L('DK seq: hits'); s.b('seq')
    for k in (0, 4, 7, 12, 14, 19, 21, 24): s.tap(k)
    s.L('DK seq: lane change'); s.e('select', 1); s.t(300)
    for k in (2, 6, 9, 16, 23): s.tap(k)
    s.L('DK seq: lane change 2'); s.e('select', 2); s.t(300)
    for k in (3, 11, 19, 27 - 1): s.tap(k)
    s.L('DK seq: play'); s.b('play'); s.t(2500)
    s.L('DK seq playing: lane scroll'); s.e('select', 1, 3); s.t(600); s.e('select', -1, 4); s.t(1000)
    drum_session(s)
    s.L('DK seq playing: held step 7'); s.n(7, 1); s.t(200); s.e('knob2', 1, 3); s.e('knob4', 1, 2); s.e('knob1', 1, 2); s.e('knob3', -1, 2); s.t(400); s.n(7, 0); s.t(1500)
    s.L('DK kit mixer playing'); s.b('edit'); s.t(2000)
    s.L('DK kit mixer: solo'); s.b('oct_up'); s.t(1500)
    s.L('DK kit mixer: mute'); s.b('oct_down'); s.t(1500)
    s.L('DK kit mixer: mute again'); s.b('oct_down'); s.t(800); s.b('oct_up'); s.t(800)
    s.L('DK kit mixer: knobs'); knob_sweep(s, 'DK kit mixer playing')
    s.L('DK kit mixer: lane select'); s.e('select', 1, 3); s.t(800); s.e('select', -1, 3); s.t(800)
    s.L('DK mixer (GLO) playing'); s.b('glo'); s.t(2000)
    s.L('GLO mixer playing: mute/solo'); s.b('oct_down'); s.t(1000); s.b('oct_up'); s.t(1000); s.b('oct_down'); s.t(600); s.b('oct_up'); s.t(1000)
    s.L('GLO mixer playing: knobs'); knob_sweep(s, 'GLO mixer playing')
    s.L('GLO mixer playing: track sel'); s.e('algorithm', -1); s.t(800); s.e('algorithm', -1); s.t(800); s.e('algorithm', 2); s.t(800)
    s.L('FX sends playing'); s.b('fx'); s.t(300)
    for i in range(5): s.L('FX screen %d playing' % i); s.t(1200); s.b('fx')
    s.L('DK retrig: seq'); s.b('seq'); s.t(300); s.n(1, 1); s.t(100); s.n(1, 0); s.b('arp'); s.t(300)
    s.L('DK retrig: hold key 1'); s.n(1, 1); s.t(200); s.e('knob1', 1, 3); s.e('knob2', 1, 3); s.t(500); s.n(1, 0); s.t(1300)
    s.L('DK stop'); s.b('play'); s.t(1200)
    # Lists.
    for part in ('DK', 'T1'):
        if part == 'T1': s.L('to T1'); s.e('algorithm', -3); s.t(500)
        s.L('%s list open' % part); s.b('sel'); s.t(500)
        s.L('%s list scroll down' % part); s.e('select', 1, 12); s.t(500)
        s.L('%s list scroll up' % part); s.e('select', -1, 7); s.t(500)
        s.L('%s list fast scroll' % part); s.e('select', 5, 4); s.e('select', -6, 3); s.t(500)
        s.L('%s list presets enc' % part); s.e('presets', 1, 3); s.t(500); s.e('presets', -1, 2); s.t(500)
        s.L('%s list knobs' % part); s.e('knob1', 1, 3); s.e('knob2', 1, 3); s.t(1300)
        s.L('%s list close' % part); s.b('sel'); s.t(800)
    # SAVE menus and the name editor (device project store emulation).
    s.L('save: open'); s.b('save'); s.t(500)
    s.L('save: knob1 actions'); s.e('knob1', 1, 3); s.t(300); s.e('knob1', -1, 3); s.t(300)
    s.L('save: SAVE AS'); s.e('knob1', 1); s.t(300); s.b('save'); s.t(1000)
    s.L('save: slots scroll'); s.e('knob1', 1, 7); s.t(300); s.e('knob1', -1, 3); s.t(500)
    s.L('save: choose slot'); s.b('save'); s.t(1000)
    s.L('save: name editor knob2'); s.e('knob2', 1, 4); s.t(300); s.e('knob2', -1, 2); s.t(300)
    s.L('save: name editor knob1'); s.e('knob1', 1, 6); s.t(300); s.e('knob1', -1, 3); s.t(1300)
    s.L('save: name editor cursor end'); s.e('knob2', 1, 12); s.t(300); s.e('knob1', 3, 4); s.t(1300)
    s.L('save: confirm name'); s.b('save'); s.t(1500)
    s.L('save: confirm write'); s.b('save'); s.t(2500)
    s.L('save: reopen and load'); s.b('save'); s.t(500); s.e('knob1', 2); s.b('save'); s.t(1500); s.e('knob1', 1, 2); s.t(500)
    s.L('save: leave with home'); s.b('home'); s.t(1500)
    s.L('save: second leave'); s.b('home'); s.t(800)
    s.L('save: open for SAVE FIRST'); s.b('save'); s.t(500); s.e('knob1', 1, 3); s.t(300); s.b('save'); s.t(800)
    s.L('save: SAVE FIRST choices'); s.e('knob1', 1, 2); s.t(400); s.e('knob1', -1, 1); s.t(1300); s.e('knob1', 1, 1); s.t(300)
    s.L('save: SAVE FIRST cancel'); s.b('save'); s.t(1200)
    s.L('save: overwrite path'); s.b('save'); s.t(500); s.e('knob1', 1, 1); s.b('save'); s.t(800); s.e('knob1', -1, 12); s.t(300)
    s.b('save'); s.t(1000); s.L('save: overwrite name'); s.b('save'); s.t(1200); s.L('save: overwrite confirm'); s.b('save'); s.t(1500)
    s.L('save: overwrite leave'); s.b('home'); s.t(800); s.b('home'); s.t(800)
    # Punch FX (shift key 25 on a page that plays keys) while playing, both punch pages, knobs.
    s.L('punch: home + play'); s.b('home'); s.b('play'); s.t(600)
    for key, name in ((1, 'loop'), (8, 'echo'), (13, 'filter'), (20, 'tremolo')):
        s.L('punch %s: shift' % name); s.n(25, 1); s.t(300)
        s.L('punch %s: hold' % name); s.n(key, 1); s.t(600)
        s.L('punch %s: knobs' % name); s.e('knob1', 3, 3); s.e('knob2', -3, 3); s.e('knob3', 5, 2); s.e('knob4', 1, 2); s.t(300)
        s.L('punch %s: page 2' % name); s.b('oct_up'); s.t(400); s.e('knob1', 4, 3); s.e('knob2', 4, 3)
        s.e('knob3', -3, 4); s.e('knob4', 5, 3); s.e('knob2', -9, 4); s.t(1300)
        s.L('punch %s: page 1' % name); s.b('oct_down'); s.t(400)
        s.L('punch %s: release' % name); s.n(key, 0); s.t(300); s.n(25, 0); s.t(800)
    # Batch B parameter graphics: every FX effect page swept while playing (the MOD RATE orbit turns).
    s.L('FX pages playing: open'); s.b('fx'); s.t(300)
    for i in range(1, 5):
        s.b('fx'); s.t(600)
        for k in (2, 3, 4):
            s.L('FX screen %d playing: knob%d' % (i, k)); s.e('knob%d' % k, 9, 4); s.t(400); s.e('knob%d' % k, -9, 8); s.t(400); s.e('knob%d' % k, 4, 3); s.t(700)
    s.L('FX pages playing: mod engine'); s.b('fx'); s.b('fx'); s.b('fx'); s.b('fx'); s.e('knob1', 1, 2); s.t(1500)
    params_playing(s)
    s.L('punch: stop'); s.b('play'); s.t(800)
    # MIDI settings (GLO + OCT- chord), both screens.
    s.L('midi settings: chord'); s.h('oct_down', 1); s.h('glo', 1); s.h('glo', 0); s.h('oct_down', 0); s.t(500)
    knob_sweep(s, 'midi settings/0', (1, 2, 3, 4))
    s.L('midi settings: next'); s.e('select', 1); s.t(400)
    knob_sweep(s, 'midi settings/1', (1, 2, 3, 4))
    s.L('midi settings: leave'); s.b('home'); s.t(800)
    # Every knob on every page and screen, per track, small and large steps, extremes, highlight timeout.
    parts = (0, 3) if quick else (0, 1, 2, 3)
    cur = 0
    for p in parts:
        if p != cur: s.L('track -> %s' % PART[p]); s.e('algorithm', p - cur); s.t(600); cur = p
        for pg, screens in PAGES:
            for sc in range(screens if not quick else min(screens, 2)):
                if sc == 0 and pg != 'edit': s.L('%s %s: open' % (PART[p], pg)); s.b(pg); s.t(300)
                elif pg != 'edit': s.L('%s %s: next screen %d' % (PART[p], pg, sc)); s.b(pg); s.t(300)
                else: s.L('%s edit: open' % PART[p]); s.b('edit'); s.t(300)
                knob_sweep(s, '%s %s/%d' % (PART[p], pg, sc))
        s.L('%s home oct' % PART[p]); s.b('home'); s.b('oct_up'); s.t(400); s.b('oct_down'); s.t(400)
    if not quick:                         # the sample kits' PITCH (80S) on the kit HOME page
        s.L('DK 80s: preset'); s.e('presets', 2); s.t(600); s.b('home'); s.t(300)
        knob_sweep(s, 'DK 80s home', (1,)); s.e('presets', -2); s.t(600)
    # Playback again after the sweeps, through every page.
    s.L('final: T? seq play'); s.b('seq'); s.b('play'); s.t(1500)
    for pg in ('home', 'env', 'fx', 'glo', 'lfo', 'arp', 'edit', 'seq'):
        s.L('final playing: %s' % pg); s.b(pg); s.t(1200)
        s.L('final playing: %s knob2 turns' % pg); s.e('knob2', 3, 3); s.e('knob2', -3, 3); s.t(1300)
    s.L('final: track changes while playing')
    for d in (1, 1, 1, -1, -1, -1): s.e('algorithm', d); s.t(900)
    s.L('final: stop'); s.b('play'); s.t(1000)
    return s.lines


def bench_session():
    """Full redraw cost of the heaviest screens (states built up first)."""
    s = S()
    s.t(400)
    s.b('seq')
    for k in (0, 4, 7, 9, 14, 21, 24): s.tap(k)
    s.b('play'); s.t(600)
    for name, steps in [('T1 home/0', ['home']), ('T1 home/1', ['home', 'home']), ('T1 env/0', ['env']),
                        ('T1 env/1', ['env', 'env']), ('T1 fx/0', ['fx']), ('T1 fx/sends', ['fx'] * 1),
                        ('T1 lfo/0', ['lfo']), ('T1 glo/mixer', ['glo']), ('T1 glo/1', ['glo', 'glo']),
                        ('T1 seq/steps', ['seq']), ('T1 seq/1', ['seq', 'seq']), ('T1 arp', ['arp']), ('T1 sel list', ['sel'])]:
        for i, b in enumerate(steps): s.b(b)
        if name == 'T1 fx/sends':
            for _ in range(3): s.b('fx')
        s.t(300); s.bench(name)
        if name == 'T1 sel list': s.b('sel'); s.t(300)
    for name, ps in [('T1 home/1 filter', (0, 1)), ('T1 env/2 setup', (1, 2)), ('T1 fx/1 drive', (2, 1)),
                     ('T1 fx/2 delay', (2, 2)), ('T1 fx/3 mod', (2, 3)), ('T1 fx/4 reverb', (2, 4)), ('T1 patch/0', (8, 0)),
                     ('T1 lfo depths', (8, 2)), ('T1 glo/2 settings', (3, 2)), ('T1 glo/4 comp', (3, 4)),
                     ('T1 glo/5 comp knobs', (3, 5)), ('T1 glo/6 heat', (3, 6)), ('T1 seq/1 settings', (4, 1))]:
        s.v(*ps); s.t(300); s.bench(name)
    s.h('oct_down', 1); s.h('glo', 1); s.h('glo', 0); s.h('oct_down', 0); s.t(300); s.bench('T1 midi/1')
    s.b('home'); s.t(300)
    s.e('algorithm', 3); s.t(500)
    for name, steps in [('DK seq/grid', ['seq']), ('DK kit mixer', ['edit']), ('DK home', ['home']), ('DK glo/mixer', ['glo'])]:
        for b in steps: s.b(b)
        s.t(300); s.bench(name)
    s.v(1, 2); s.t(300); s.bench('DK env/2 setup')
    s.v(7, 0); s.t(300); s.bench('DK lfo/0')
    if SMK:
        s.v(PS_PAGE, 0); s.lines.append('S 31 4'); s.t(300); s.bench('SMK patterns')
        s.b('home'); s.t(300)
    s.b('play'); s.t(300)
    s.b('save'); s.t(500); s.bench('save menu')
    s.e('knob1', 1); s.b('save'); s.t(800); s.bench('save slots')
    s.b('save'); s.t(800); s.bench('name editor')
    return s.lines


# ------------------------------------------------------------------------------------------------ builds
def dsp_objs(root):
    objs = sorted((root / 'build/host/braids').glob('*.o')) + sorted((root / 'build/host/plaits_reference').glob('*.o'))
    if not objs: raise SystemExit('Host DSP objects are missing: run tools/build_braids_host.sh first')
    return objs


def build(root, out, hd=1, extra=(), flags=None, cxx='c++'):
    """Build tests/hd_diff.c into `out`. `hd` is kept for callers (tools/hd_kinds.py, tools/hd_seq_profile.py) and
    must be 1: the standard interface was removed and BR_UI_HD comes from firmware/src/gfx_hd.h's default."""
    if hd != 1: raise SystemExit('hd_verify.build: hd=%r: the standard interface (BR_UI_HD=0) was removed' % (hd,))
    out.parent.mkdir(parents=True, exist_ok=True)
    fl = [f for f in (flags or FLAGS)] + ['-DDINGHY_DEVICE_SAVE_TEST', *extra]
    if SEQ2X8 != '': fl.append('-DBR_SEQ_2X8=' + SEQ2X8)
    planner = out.parent / ('planner%s.o' % ('-san' if any('sanitize' in x for x in extra) else ''))
    if not planner.exists():
        subprocess.run(['cc', *[f for f in fl if 'sanitize' not in f and 'pie' not in f], '-c',
                        'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=root, check=True)
    src = 'tests/hd_diff.c'
    if SMK:
        fl.append('-DBR_MIDI_CLOCK_EXPERIMENT=1')
        src = str(out.parent / (out.name + '-smk.c'))
        Path(src).write_text(SMK_WRAPPER.format(hd_diff=root / 'tests/hd_diff.c'))
    r = subprocess.run([cxx, '-x', 'c', *fl, src, '-x', 'none', str(planner), *map(str, dsp_objs(ROOT)),
                        '-o', str(out)], cwd=root, capture_output=True, text=True)
    if r.returncode: sys.stderr.write(r.stderr); raise SystemExit('build failed: %s' % out)
    return out


def select_target(target):
    """build every binary for `target`: adds its product defines to FLAGS (FM-1: nothing);
    -> the work-folder prefix"""
    for f in host_target.product_flags('smk37'):
        while f in FLAGS: FLAGS.remove(f)
    FLAGS.extend(host_target.product_flags(target))
    return 'hd-verify-' + ('smk37-' if target == 'smk37' else '')


def copy_tree(dst):
    if dst.exists(): shutil.rmtree(dst)
    dst.mkdir(parents=True)
    for d in ('firmware', 'tests', 'tools'):
        shutil.copytree(ROOT / d, dst / d, ignore=shutil.ignore_patterns('__pycache__'))
    os.symlink(ROOT / 'assets', dst / 'assets')
    (dst / 'web').mkdir()
    return dst


def sub(path, old, new, count=None, regex=False):
    s = path.read_text()
    n = len(re.findall(old, s)) if regex else s.count(old)
    if not n or (count is not None and n != count):
        raise SystemExit('patch: %r found %d times in %s' % (old, n, path))
    path.write_text(re.sub(old, new, s) if regex else s.replace(old, new))
    return n


def patch_strips(tree, rows):
    """Step every strip loop of the screen bodies at `rows` instead of 32 / 40 / 60 rows."""
    ui = tree / 'firmware/src/instrument_ui.c'; pj = tree / 'firmware/src/instrument_project_ui.c'
    r = '%du' % rows
    sub(ui, r'offset \+= 32u|offset\+=32u', 'offset += ' + r, 2, regex=True)          # (32 first: 60 -> 32 must not be counted)
    sub(ui, r'rows < 32u \? rows : 32u|rows<32u\?rows:32u', 'rows < %s ? rows : %s' % (r, r), 2, regex=True)
    sub(ui, 'offset += 60u', 'offset += ' + r, 2)
    sub(ui, '134u - offset < 60u ? 134u - offset : 60u', '134u - offset < %s ? 134u - offset : %s' % (r, r), 2)
    sub(ui, 'offset += 40u', 'offset += ' + r, 3)
    sub(ui, r'rows < 40u \? rows : 40u|rows<40u\?rows:40u', 'rows < %s ? rows : %s' % (r, r), 6, regex=True)
    sub(ui, 'y += 40u) {\n        uint32_t rows = 214u - y < 40u ? 214u - y : 40u;',
        'y += %s) {\n        uint32_t rows = 214u - y < %s ? 214u - y : %s;' % (r, r, r), 1)
    if SMK:
        sub(ui, 'for (uint32_t y = 88u; y < 168u; y += 40u) {\n        cv_begin(240u, 40u, C_BLACK);',
            'for (uint32_t y = 88u; y < 168u; y += %s) {\n        cv_begin(240u, %s, C_BLACK);' % (r, r), 1)
    sub(pj, 'y+=40){\n        uint32_t rows=(uint32_t)(216-y);if(rows>40u)rows=40u;',
        'y+=%d){\n        uint32_t rows=(uint32_t)(216-y);if(rows>%s)rows=%s;' % (rows, r, r), 1)


def patch_counters(tree):
    g = tree / 'firmware/src/gfx.c'; h = tree / 'firmware/src/gfx_hd.h'
    sub(g, '    n = w * h;\n', '    n = w * h;\n    hdv_fill_px += n;\n', 1)
    sub(g, '    for (; y < y1; y++)\n        for (i = x; i < x1; i++)\n',
        '    if (x1 > x && y1 > y) hdv_rect_px += (unsigned long long)(x1 - x) * (unsigned long long)(y1 - y);\n'
        '    for (; y < y1; y++)\n        for (i = x; i < x1; i++)\n', 1)
    sub(g, '    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)\n        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);',
        '    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h) {\n        ++hdv_pset_px; cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c); }', 1)
    sub(h, '    y += cv_oy;\n    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h) return cv_background;',
        '    y += cv_oy;\n    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h) { ++hdv_get_oob; return cv_background; }', 1)
    sub(h, '    y += cv_oy;\n    if (!a || (uint32_t)x >= cv_w || (uint32_t)y >= cv_h) return;\n'
           '    uint16_t *p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];\n',
        '    y += cv_oy; ++hdv_blend_calls;\n    if (a && ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h)) ++hdv_blend_oob;\n'
        '    if (!a || (uint32_t)x >= cv_w || (uint32_t)y >= cv_h) return;\n'
        '    uint16_t *p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];\n    ++hdv_blend_px;\n'
        '    if (p < cv_px || p >= cv_px + cv_w * cv_h || cv_w * cv_h > CV_MAX) ++hdv_bad_write;\n', 1)
    sub(g, '    cv_background = bg;\n', '    cv_background = bg;\n    ++hdv_canvas;\n', 1)
    w = tree / 'firmware/src/instrument_widgets.c'
    sub(w, '    int32_t chain = bw_last.w == w && bw_last.c == color && bw_last.x1 == x0 && bw_last.y1 == y0;\n',
        '    int32_t chain = bw_last.w == w && bw_last.c == color && bw_last.x1 == x0 && bw_last.y1 == y0;\n'
        '    int hdv_st = chain && hdv_last_canvas != hdv_canvas; hdv_chain_total += chain != 0;\n'
        '    if (hdv_st) hdv_stale_event(x0, y0, x1, y1, color, __builtin_return_address(0));\n'
        '    hdv_last_canvas = hdv_canvas;\n', 1)
    sub(w, '                if (p >= a) continue;\n', '                hdv_stale_px += hdv_st;\n                if (p >= a) continue;\n', 1)


# ------------------------------------------------------------------------------------------------ run + images
def run(binary, lines, env=None, cwd=ROOT):
    e = dict(os.environ); e.update(env or {})
    r = subprocess.run([str(binary)], input='\n'.join(lines) + '\nQ\n', text=True, capture_output=True, cwd=cwd, env=e)
    return r.returncode, r.stdout, r.stderr


def load565(path):
    d = Path(path).read_bytes()
    return struct.unpack('<%dH' % (len(d) // 2), d)


def rgb(c): return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def png(path, w, h, pix):
    raw = b''.join(b'\0' + b''.join(bytes(pix[y * w + x]) for x in range(w)) for y in range(h))
    def chunk(tag, data): return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def crop_sheet(path, frames, box, scale=4, margin=6):
    x0, y0, x1, y1 = box
    x0 = max(0, x0 - margin); y0 = max(0, y0 - margin); x1 = min(239, x1 + margin); y1 = min(239, y1 + margin)
    w, h = x1 - x0 + 1, y1 - y0 + 1
    act, ref = frames[1], frames[2]
    diff = [((255, 0, 255) if act[i] != ref[i] else tuple(v // 4 for v in rgb(ref[i]))) for i in range(240 * 240)]
    panels = [[rgb(c) for c in f] for f in frames] + [diff]
    gap = 4
    W = len(panels) * w * scale + (len(panels) - 1) * gap
    out = []
    for yy in range(h * scale):
        row = []
        for pi, p in enumerate(panels):
            if pi: row += [(40, 40, 40)] * gap
            sy = y0 + yy // scale
            for xx in range(w * scale): row.append(p[sy * 240 + x0 + xx // scale])
        out += row
    png(path, W, h * scale, out)


def parse(stdout):
    ev = []
    for line in stdout.splitlines():
        m = re.match(r'([MU]) (\d+) n=(\d+) box=(\d+),(\d+),(\d+),(\d+) t=(\d+) page=(\d+) screen=(\d+) part=(\d+) layout=(\d+) es=(\d+) playing=(\d+) \|(.*)\|', line)
        if m:
            g = m.groups()
            ev.append(dict(kind=g[0], idx=int(g[1]), n=int(g[2]), box=tuple(map(int, g[3:7])), t=int(g[7]), page=int(g[8]),
                           screen=int(g[9]), part=int(g[10]), layout=int(g[11]), es=int(g[12]), playing=int(g[13]), label=g[14]))
    summ = re.search(r'^S .*$', stdout, re.M)
    return ev, summ.group(0) if summ else '(no summary)'


def group(events):
    """Distinct findings: same kind, screen (page/screen/layout/es) and an overlapping or nearby box."""
    groups = []
    for e in events:
        for g in groups:
            f = g[0]
            if f['kind'] == e['kind'] and (f['page'], f['screen'], f['layout'], f['es']) == (e['page'], e['screen'], e['layout'], e['es']):
                a, b = g[-1]['box'], e['box']
                if not (a[2] + 8 < b[0] or b[2] + 8 < a[0] or a[3] + 8 < b[1] or b[3] + 8 < a[1]):
                    g.append(e); break
        else:
            groups.append([e])
    return groups


def report_diff(name, binary, lines, out, rep):
    raw = out / ('raw-' + name)
    if raw.exists(): shutil.rmtree(raw)
    code, so, se = run(binary, lines, {'HDV_OUT': str(raw)})
    ev, summ = parse(so)
    rep.append('[%s] exit=%d %s' % (name, code, summ))
    if se.strip(): rep.append('  stderr: ' + se.strip()[-2000:])
    for kind in ('M', 'U'):
        gs = group([e for e in ev if e['kind'] == kind])
        rep.append('  %s groups: %d (%d events)' % ('mismatch' if kind == 'M' else 'unowned-by-full-redraw', len(gs), sum(map(len, gs))))
        for gi, g in enumerate(sorted(gs, key=lambda g: -max(e['n'] for e in g))):
            f = g[0]
            bx = [min(e['box'][0] for e in g), min(e['box'][1] for e in g), max(e['box'][2] for e in g), max(e['box'][3] for e in g)]
            labels = sorted({e['label'] for e in g})
            img = ''
            if kind == 'M' and gi < 40:
                big = max(g, key=lambda e: e['n'])
                fr = [load565(raw / ('%06d-%s.565' % (big['idx'], t))) for t in ('prev', 'act', 'ref')]
                img = out / ('%s-%s%02d.png' % (name, kind, gi))
                crop_sheet(img, fr, big['box'])
            rep.append('   %s%02d page=%d screen=%d part=%d layout=%d es=%d events=%d max_px=%d box=%s first=t%d "%s"%s %s' % (
                kind, gi, f['page'], f['screen'], f['part'], f['layout'], f['es'], len(g), max(e['n'] for e in g), bx, f['t'],
                f['label'], (' (+%d more labels: %s)' % (len(labels) - 1, '; '.join(labels[:6]))) if len(labels) > 1 else '',
                img and ('img=' + str(img))))
    return ev


def steady_session(playing=True):
    """Steady-state drawing while playing (playing=False: stopped, no note held, so only pages that animate by
    themselves draw): one part per page, split by 'L prof' marks."""
    s = S()
    s.t(400); s.b('seq')
    for k in (0, 4, 7, 9, 14, 21, 24): s.tap(k)
    if playing: s.b('play')
    s.t(500)
    # (page, screen) jumps so every row measures the screen its label names; a held note keeps the LFO, the
    # follower and the evolving clocks moving. knob: which knob turns in the '+knob' row.
    s.p(0)
    if playing: s.n(7, 1)
    for name, ps, knob in [('T1 home/0', (0, 0), 2), ('T1 home/1 filter', (0, 1), 3), ('T1 env/0', (1, 0), 2),
                           ('T1 env/2 setup', (1, 2), 1), ('T1 fx/0 sends', (2, 0), 2), ('T1 fx/1 drive', (2, 1), 2),
                           ('T1 fx/2 delay', (2, 2), 3), ('T1 fx/3 mod', (2, 3), 2), ('T1 fx/4 reverb', (2, 4), 3),
                           ('T1 lfo/0', (7, 0), 2), ('T1 patch/0', (8, 0), 3), ('T1 lfo depths', (8, 2), 1),
                           ('T1 glo/mixer', (3, 0), 2), ('T1 glo/1 filter', (3, 1), 4), ('T1 glo/2 settings', (3, 2), 1),
                           ('T1 glo/3 pan', (3, 3), 2), ('T1 glo/4 comp', (3, 4), 2), ('T1 glo/5 comp knobs', (3, 5), 2),
                           ('T1 glo/6 heat', (3, 6), 2), ('T1 seq/steps', (4, 0), 2), ('T1 seq/1 settings', (4, 1), 1),
                           ('T1 arp', (5, 0), 3)]:
        s.v(*ps); s.t(200); s.L('prof ' + name); s.t(2000)
        s.L('prof ' + name + ' +knob'); s.e('knob%d' % knob, 1, 20); s.t(400)
    s.n(7, 0); s.t(100)
    s.h('oct_down', 1); s.h('glo', 1); s.h('glo', 0); s.h('oct_down', 0); s.t(200)
    s.L('prof T1 midi/1'); s.t(2000); s.L('prof T1 midi/1 +knob'); s.e('knob1', 1, 20); s.t(400)
    s.p(3); s.t(300); s.n(4, 1)
    for name, ps in [('DK seq/grid', (4, 0)), ('DK kit mixer', (8, 3)), ('DK home/0', (0, 0)), ('DK lfo/0', (7, 0))]:
        s.v(*ps); s.t(200); s.L('prof ' + name); s.t(2000)
        s.L('prof ' + name + ' +knob'); s.e('knob2', 1, 20); s.t(400)
    s.n(4, 0)
    if SMK:
        s.p(0); s.v(PS_PAGE, 0); s.t(200); s.L('prof SMK patterns'); s.t(2000)
        s.v(4, 0); s.t(200); s.lines.append('P 1 2600'); s.t(200); s.L('prof SMK peek'); s.t(2000)
    s.L('prof end')
    return s.lines


def callgrind_parts(prefix):
    """Dumps in order: prefix.1, prefix.2, ..., then prefix (the rest, written at exit)."""
    fs = sorted(glob.glob(prefix + '.*'), key=lambda f: int(f.rsplit('.', 1)[1])) + [prefix]
    parts = []
    for f in fs:
        t = Path(f).read_text(errors='replace') if Path(f).exists() else ''
        m = re.search(r'^(?:summary|totals): (\d+)', t, re.M)
        parts.append((f, int(m.group(1)) if m else 0))
    return parts


def top_functions(f, n=8):
    """Self cost per function (inlined code summed under its function), as % of the part's total."""
    r = subprocess.run(['callgrind_annotate', '--inclusive=no', '--threshold=100', f], capture_output=True, text=True).stdout
    tot = re.search(r'^([\d,]+) \(100.0%\)\s+PROGRAM TOTALS', r, re.M)
    tot = int(tot.group(1).replace(',', '')) if tot else 0
    by = {}
    for l in r.splitlines():
        m = re.match(r'\s*([\d,]+) \(\s*([\d.]+)%\)\s+(\S+):(\S+)', l)
        if not m or m.group(2) == '100.0' or m.group(4).startswith(('0x', '(below', '__libc')): continue
        by[m.group(4)] = by.get(m.group(4), 0) + int(m.group(1).replace(',', ''))
    return ['%s %.0f%%' % (k, 100.0 * v / max(1, tot)) for k, v in sorted(by.items(), key=lambda kv: -kv[1])[:n]]


def profile(work, out, rep):
    """callgrind instruction counts: forced full redraws (bench) and steady-state frames while playing."""
    if not shutil.which('valgrind'): rep.append('[profile] valgrind missing'); return
    tree = copy_tree(work / 'tree-prof')
    sub(tree / 'firmware/src/instrument_ui.c', 'static void br_draw(void)\n{', 'static __attribute__((noinline)) void br_draw(void)\n{', 1)
    pd = out / 'callgrind'; pd.mkdir(exist_ok=True)
    for f in pd.glob('*'): f.unlink()
    res = {}
    for hd in (1,):
        b = build(tree, tree / ('build/hd_diff_prof%d' % hd), hd, ['-g'])
        for kind, lines, fn, split in (('bench', bench_session(), 'hdv_bench_draws', '--dump-after=hdv_bench_draws'),
                                       ('steady', steady_session(), 'br_draw', '--dump-before=hdv_mark')):
            pre = str(pd / ('%s-hd%d.out' % (kind, hd)))
            e = dict(os.environ, HDV_NOCMP='1', HDV_BENCH_NOFORK='1')
            r = subprocess.run(['valgrind', '--tool=callgrind', '--toggle-collect=' + fn, split, '--callgrind-out-file=' + pre, str(b)],
                               input='\n'.join(lines) + '\nQ\n', text=True, capture_output=True, cwd=tree, env=e)
            labels = [l[2:] for l in lines if l.startswith('L ' + ('bench' if kind == 'bench' else 'prof'))]
            draws = {}
            for m in re.finditer(r'^H \d+ \w+ .*\|(.*)\|$', r.stdout, re.M): draws[m.group(1)] = draws.get(m.group(1), 0) + 1
            if r.returncode: rep.append('[profile] valgrind failed: ' + r.stderr[-500:])
            parts = callgrind_parts(pre)
            if kind == 'steady': parts = parts[1:]       # the first dump is everything before the first mark
            res[(kind, hd)] = (labels, parts, draws)
    for kind in ('bench', 'steady'):
        lh, ph, dh = res[(kind, 1)]
        rep.append('[profile %s] instructions (callgrind Ir) %s' % (kind, 'per forced full redraw (20 redraws averaged)' if kind == 'bench' else 'per drawn frame while playing (br_draw incl.)'))
        for i, lab in enumerate(lh):
            if i >= len(ph): break
            a = ph[i][1] / 20 if kind == 'bench' else ph[i][1] / max(1, dh.get(lab, 0))
            rep.append('   %-26s HD %9.0f  top: %s' % (lab, a, '; '.join(top_functions(ph[i][0], 5))))


class Rep(list):
    """The report, rewritten to disk on every line so a failing late stage keeps the earlier results."""
    def __init__(self, path): super().__init__(); self.path = path
    def append(self, x): super().append(x); self.path.write_text('\n'.join(self) + '\n')
    def __iadd__(self, xs):
        for x in xs: self.append(x)
        return self


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('out'); ap.add_argument('--work', default='verify')
    ap.add_argument('--only', default='diff,strips,asan,cost,profile'); ap.add_argument('--quick', action='store_true')
    ap.add_argument('--target', choices=host_target.TARGETS, default='fm1', help='product (default fm1)')
    a = ap.parse_args()
    if not a.work.startswith(('verify', 'perf')): raise SystemExit('--work must start with "verify" or "perf"')
    prefix = select_target(a.target)
    global SMK; SMK = a.target == 'smk37'
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    work = ROOT / 'build' / (prefix + a.work); work.mkdir(parents=True, exist_ok=True)
    only = a.only.split(',')
    if 'control' in only: raise SystemExit("--only control: the standard interface was removed; there is no control build")
    lines = session(a.quick)
    (out / 'session.txt').write_text('\n'.join(lines) + '\n')
    rep = Rep(out / ('report-%s.txt' % a.only.replace(',', '-'))); rep.append('session: %d commands' % len(lines))
    bins = {}
    def get(hd=1):
        if hd not in bins: bins[hd] = build(ROOT, work / ('hd_diff%d' % hd), hd)
        return bins[hd]
    if 'diff' in only: report_diff('hd', get(1), lines, out, rep)
    if 'noresync' in only:
        code, so, se = run(get(1), lines, {'HDV_NO_RESYNC': '1'}); rep.append('[hd no-resync] ' + parse(so)[1])
    if 'strips' in only:
        for hd in (1,):
            base = run(get(hd), lines, {'HDV_NOCMP': '1'})[1]
            bh = re.findall(r'^H (\d+) (\w+) (.*)$', base, re.M)
            for rows in (22, 32):
                tree = copy_tree(work / ('tree-strips%d' % rows)); patch_strips(tree, rows)
                b = build(tree, tree / ('build/hd_diff%d' % hd), hd)
                vh = re.findall(r'^H (\d+) (\w+) (.*)$', run(b, lines, {'HDV_NOCMP': '1'}, cwd=tree)[1], re.M)
                diffs = [(x, y) for x, y in zip(bh, vh) if x[1] != y[1]]
                rep.append('[strips hd=%d rows=%d] draws %d vs %d, differing frames: %d' % (hd, rows, len(bh), len(vh), len(diffs)))
                for x, y in diffs[:8]: rep.append('   draw %s: %s' % (x[0], x[2]))
                if diffs:
                    d0 = diffs[0][0]
                    run(get(hd), lines, {'HDV_NOCMP': '1', 'HDV_DUMP': d0, 'HDV_OUT': str(work / 'dump40')})
                    run(b, lines, {'HDV_NOCMP': '1', 'HDV_DUMP': d0, 'HDV_OUT': str(work / ('dump%d' % rows))}, cwd=tree)
                    fa = load565(work / 'dump40' / ('%06d-dump.565' % int(d0))); fb_ = load565(work / ('dump%d' % rows) / ('%06d-dump.565' % int(d0)))
                    pts = [i for i in range(240 * 240) if fa[i] != fb_[i]]
                    bx = (min(i % 240 for i in pts), min(i // 240 for i in pts), max(i % 240 for i in pts), max(i // 240 for i in pts))
                    img = out / ('strips-hd%d-rows%d.png' % (hd, rows)); crop_sheet(img, [fa, fa, fb_], bx)
                    rep.append('   first differing draw %s: %d px box=%s img=%s (panels: 40-row, 40-row, %d-row, diff)' % (d0, len(pts), bx, img, rows))
    if 'asan' in only:
        san = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
        for hd in (1,):
            b = build(ROOT, work / ('hd_diff_san%d' % hd), hd, san + SAN_GCC, cxx=SAN_CXX)
            # The session without the per-frame forks (speed), then the forced full redraws of the bench states.
            senv = {'ASAN_OPTIONS': 'detect_leaks=0', 'UBSAN_OPTIONS': 'print_stacktrace=1', 'HDV_NOCMP': '1'}
            code, so, se = run(b, lines, senv)
            code2, so2, se2 = run(b, bench_session(), senv)
            code = code or code2; se += se2
            errs = [l for l in se.splitlines() if 'runtime error' in l or 'ERROR: AddressSanitizer' in l]
            rep.append('[asan+ubsan hd=%d] exit=%d draws=%d (+%d forced full redraws); sanitizer reports: %d (unique %d)' % (
                hd, code, so.count('\nH '), 20 * so2.count('\nB '), len(errs), len(set(errs))))
            for l in sorted(set(errs))[:30]: rep.append('   ' + l)
            # Signed overflow is defined on the device (-fwrapv); run once without it to list overflow sites in HD code.
            if hd:
                b = build(ROOT, work / ('hd_diff_nowrap%d' % hd), hd, ['-fsanitize=signed-integer-overflow,shift', '-g'] + SAN_GCC,
                          flags=[f for f in FLAGS if f != '-fwrapv'], cxx=SAN_CXX)
                code, so, se = run(b, lines, {'HDV_NOCMP': '1'})
                errs = sorted({re.sub(r'^.*?firmware', 'firmware', l) for l in se.splitlines() if 'runtime error' in l})
                rep.append('[ubsan without -fwrapv hd=%d] exit=%d overflow/shift sites: %d' % (hd, code, len(errs)))
                for l in errs[:40]: rep.append('   ' + l)
    if 'cost' in only:
        tree = copy_tree(work / 'tree-counters'); patch_counters(tree)
        bl = bench_session()
        for hd in (1,):
            b = build(tree, tree / ('build/hd_diff_cnt%d' % hd), hd, ['-DHDV_COUNTERS', '-DUI_REFRESH_TEST_COUNTERS', '-g', '-no-pie', '-fno-pie'])
            code, so, se = run(b, bl, cwd=tree)
            rep.append('[cost hd=%d] exit=%d' % (hd, code))
            rep += ['   ' + l for l in so.splitlines() if l.startswith('B ')]
            code, so, se = run(b, lines, {'HDV_NOCMP': '1'}, cwd=tree)
            rep += ['   session: ' + l for l in so.splitlines() if l.startswith('C ')]
            joins = re.findall(r'^J (\S+) c=(\w+) caller=0x(\w+)$', so, re.M)
            if joins:
                by = {}
                for seg, col, addr in joins: by.setdefault(addr, []).append((seg, col))
                for addr, v in sorted(by.items(), key=lambda kv: -len(kv[1])):
                    where = subprocess.run(['addr2line', '-f', '-i', '-e', str(b), addr], capture_output=True, text=True).stdout.split()
                    rep.append('   stale polyline joint: %d times, bw_qline called from %s; e.g. %s colour %s' % (
                        len(v), ' <- '.join(where[::2][:3]) + ' ' + ' '.join(where[1::2][:1]), v[0][0], v[0][1]))
            (out / ('cost-hd%d.txt' % hd)).write_text(so)
    if 'steadycost' in only:     # no valgrind needed: drawing work per second, per page (instrumented counters)
        tree = copy_tree(work / 'tree-counters'); patch_counters(tree)
        res = {}
        for hd in (1,):
            b = build(tree, tree / ('build/hd_diff_cnt%d' % hd), hd, ['-DHDV_COUNTERS', '-DUI_REFRESH_TEST_COUNTERS', '-g'])
            for playing in (1, 0):
                code, so, se = run(b, steady_session(playing), {'HDV_NOCMP': '1'}, cwd=tree)
                pts = [(int(m[0]), int(m[1]), int(m[2]), int(m[3]), int(m[4]), int(m[5]), int(m[6]), m[7]) for m in re.findall(
                    r'^P (\d+) blend=(\d+) rect=(\d+) pset=(\d+) lcd_bytes=(\d+) blits=(\d+) draws=(\d+) \|(.*)\|$', so, re.M)]
                res[hd, playing] = {pts[i][7]: [pts[i + 1][j] - pts[i][j] for j in range(7)] for i in range(len(pts) - 1)}
                rep.append('[steadycost hd=%d playing=%d] exit=%d segments=%d' % (hd, playing, code, len(res[hd, playing])))
        for playing in (1, 0):
            rep.append('   %s; per second: blended px (HD AA work), rect px, LCD KB sent, blits, draws' %
                       ('PLAYING, a note held' if playing else 'STOPPED, nothing held (non-zero: the page animates by itself)'))
            for lab, h in res[1, playing].items():
                sec = max(1, h[0]) / 1000.0
                rep.append('   %-30s blend %8.0f rect %8.0f lcdKB %6.1f blits %5.0f draws %4.0f' % (
                    lab, h[1] / sec, h[2] / sec, h[4] / 1024 / sec, h[5] / sec, h[6] / sec))
    if 'profile' in only: profile(work, out, rep)
    print('\n'.join(rep))


if __name__ == '__main__':
    main()
