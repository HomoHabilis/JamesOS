#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render screens of the real firmware on the host (tests/host.c) to PNG: for the website, the manual and
reviews. No hardware is touched.

    python3 tools/render_screens.py OUTDIR [--scale 2] [--only name,name] [--target fm1|smk37]
                                    [--seq2x8 0|1] [--work NAME] [--poly]

Each screen is a small script of button presses and encoder turns, then a screenshot.
--target smk37 (alias --smk-preview) renders the SMK-37 Pro labels and its screen list
(SMK_SCREENS) with -DBR_PRODUCT=BR_PRODUCT_ID_SMK37, then the SMK-only states (SMK_STATES: fader peek,
polyphony hint, pattern slots, arrows, messages); FM-1 (the default) is unchanged. Both targets end with the pattern
arranger's views (ARR_STATES, through tests/run_pattern_arranger_ui.py). The HD interface is the
only one (the standard interface was removed); --hd is still accepted and changes nothing."""
import argparse, struct, subprocess, sys, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from run_drive import FLAGS            # the same build flags as the host tests
import host_target                     # product defines for --target smk37 (FM-1: none)

BTN = dict(oct_down=0, oct_up=1, fx=2, sel=3, env=4, lfo=5, edit=6, glo=7, home=8, save=9, arp=10, seq=11, play=12, rec=13)
ENC = dict(select=0, algorithm=1, knob1=2, knob2=3, knob3=4, knob4=5, presets=6)

# name -> list of (action, arg, arg2); times are assigned automatically, 150 ms apart
# GLO taps: MIXER, GLOBAL / PAN, GLOBAL / FILTER, GLOBAL / HEAT, COMPRESSOR, COMP TIME; a long GLO press
# (held 0.6 s) opens TEMPO, then taps MIDI / 1, MIDI / 2. Screen names keep the firmware screen IDs.
GLO_LONG = [('buttons', 1), ('buttons', 1 | (1 << 7)), ('buttons', 1), ('buttons', 0)]
HOME_LONG = [('buttons', 1), ('buttons', 1 | (1 << 8)), ('buttons', 1), ('buttons', 0)]
_DK_HOME = [('enc', 'algorithm', 3)]
SCREENS = {
    'p0-sound-0': [],
    'p0-env-0': [('btn', 'env')],
    'p0-fx-0': [('btn', 'fx')],
    'p0-fx-1': [('btn', 'fx')] * 2,
    'p0-fx-3': [('btn', 'fx')] * 4,
    'punch-loop': [('keys', 1 << 25), ('keys', (1 << 25) | (1 << 1))],
    'punch-echo': [('keys', 1 << 25), ('keys', (1 << 25) | (1 << 8))],
    'punch-filter': [('keys', 1 << 25), ('keys', (1 << 25) | (1 << 13))],
    'punch-tremolo': [('keys', 1 << 25), ('keys', (1 << 25) | (1 << 20))],
    'dk-seq-0': [('enc', 'algorithm', 3), ('btn', 'seq')],
    'p0-fx-2': [('btn', 'fx')] + [('btn', 'fx')] * 2,
    'p0-fx-4': [('btn', 'fx')] + [('btn', 'fx')] * 4,
    # FX knob 4, each effect's own parameter, across its range (the reverb's PRE DELAY included)
    'p0-fx-1-bias-50': [('btn', 'fx')] * 2 + [('enc', 'knob4', 64)],
    'p0-fx-1-bias-100': [('btn', 'fx')] * 2 + [('enc', 'knob4', 127)],
    'p0-fx-2-tone-50': [('btn', 'fx')] * 3 + [('enc', 'knob4', -64)],
    'p0-fx-2-tone-0': [('btn', 'fx')] * 3 + [('enc', 'knob4', -127)],
    'p0-fx-3-feedback-50': [('btn', 'fx')] * 4 + [('enc', 'knob4', 64)],
    'p0-fx-3-feedback-100': [('btn', 'fx')] * 4 + [('enc', 'knob4', 127)],
    'p0-fx-4-pre-1-32': [('btn', 'fx')] * 5 + [('enc', 'knob4', 5)],
    'p0-fx-4-pre-1-8': [('btn', 'fx')] * 5 + [('enc', 'knob4', 11)],
    'p0-glo-0': [('btn', 'glo')],
    'p0-glo-0-signal': [('btn', 'glo'), ('keys', 1 << 7)],
    'p0-glo-1': [('btn', 'glo')] * 3,
    'p0-glo-2': GLO_LONG,
    'p0-glo-2-monitor': GLO_LONG + [('enc', 'knob4', 1)],
    'p0-glo-hold-cue': [('btn', 'env'), ('buttons', 1 << 7)],          # GLO held 0.9 s: the long-press cue
    'p0-glo-2-poly-off': GLO_LONG + [('enc', 'knob3', -1)],
    'p0-glo-long-midi-1': GLO_LONG + [('btn', 'glo')],
    'p0-glo-long-midi-2': GLO_LONG + [('btn', 'glo')] * 2,
    'p0-glo-3': [('btn', 'glo')] * 2,
    'p0-glo-4': [('btn', 'glo')] * 5,
    'p0-glo-5': [('btn', 'glo')] * 6,
    'p0-glo-6': [('btn', 'glo')] * 4,
    'p0-seq-0': [('btn', 'seq')],
    'p0-sel-0': [('btn', 'sel')],
    'p0-lfo-0': [('btn', 'lfo')],
    'p0-global-lfo': [('btn', 'lfo')] * 3,
    'p0-arp-0': [('btn', 'arp')],
    'p0-edit-2': [('btn', 'edit')] * 2,
    'p0-mod-depths': HOME_LONG,
    'p0-home-voice': HOME_LONG + [('btn', 'home')],
    'p0-edit-patch-2': [('btn', 'edit')] * 3,
    'p0-patch-note-random': [('btn', 'edit')] * 2 + [('enc', 'knob1', -127), ('enc', 'knob1', 10)],
    'dk-retrig-edit': [('enc', 'algorithm', 3), ('btn', 'seq'), ('keys', 1), ('keys', 0), ('btn', 'arp'), ('keys', 1), ('enc', 'knob1', 3), ('enc', 'knob2', 3)],
    'dk-arp-message': [('enc', 'algorithm', 3), ('btn', 'arp')],
    'dk-sound-0': [('enc', 'algorithm', 3)],
    'dk-kit-solo': [('enc', 'algorithm', 3), ('btn', 'edit'), ('btn', 'oct_up')],
    'dk-kit-mute': [('enc', 'algorithm', 3), ('btn', 'edit'), ('btn', 'oct_down')],
    'dk-kit-level': [('enc', 'algorithm', 3), ('btn', 'edit')],
    'dk-sel-0': [('enc', 'algorithm', 3), ('btn', 'sel')],
    'dk-braids': [('enc', 'algorithm', 3)],
    'dk-plaits': [('enc', 'algorithm', 3), ('enc', 'presets', 1)],
    'dk-80s': [('enc', 'algorithm', 3), ('enc', 'presets', 2)],
    'dk-90s': [('enc', 'algorithm', 3), ('enc', 'presets', 3)],
    # HD screen-body checks: lit steps, playhead, held-step edit, levels and the SAVE / project menus.
    'p0-seq-notes': [('btn', 'seq')] + [x for k in (0, 4, 7, 9, 14, 21, 24) for x in (('keys', 1 << k), ('keys', 0))] + [('btn', 'play')],
    'p0-seq-held': [('btn', 'seq')] + [x for k in (0, 6, 12) for x in (('keys', 1 << k), ('keys', 0))] + [('keys', 1 << 6), ('enc', 'knob2', 4), ('enc', 'knob4', 2)],
    'p0-seq-bank2': [('btn', 'seq'), ('keys', 1), ('keys', 0), ('btn', 'oct_up')],
    'dk-seq-notes': [('enc', 'algorithm', 3), ('btn', 'seq')] + [x for k in (0, 7, 14, 21) for x in (('keys', 1 << k), ('keys', 0))] +
                    [('keys', 1 << 3), ('keys', 0)] + [x for k in (7, 21) for x in (('keys', 1 << k), ('keys', 0))] +
                    [('keys', 1 << 5), ('keys', 0)] + [x for k in (2, 6, 9, 12, 16, 19, 23, 26) for x in (('keys', 1 << k), ('keys', 0))] + [('btn', 'play')],
    'dk-seq-held': [('enc', 'algorithm', 3), ('btn', 'seq')] + [x for k in (0, 7, 14) for x in (('keys', 1 << k), ('keys', 0))] + [('keys', 1 << 7), ('enc', 'knob2', 3), ('enc', 'knob4', 2)],
    'dk-kit-play': [('enc', 'algorithm', 3), ('btn', 'seq')] + [x for k in (0, 4, 7, 12, 14, 19, 21, 24) for x in (('keys', 1 << k), ('keys', 0))] + [('btn', 'edit'), ('btn', 'play'), ('enc', 'knob3', -20)],
    'p0-glo-0-db-low': [('btn', 'glo'), ('enc', 'knob2', -127), ('enc', 'knob2', 20)],
    'p0-glo-0-db-inf': [('btn', 'glo'), ('enc', 'knob4', -127)],
    'dk-kit-level-db': [('enc', 'algorithm', 3), ('btn', 'edit'), ('enc', 'knob2', -30)],
    'p0-glo-0-levels': [('btn', 'glo'), ('enc', 'knob2', -30), ('enc', 'knob3', -60), ('keys', 1 << 7)],
    'p0-fx-0-levels': [('btn', 'fx'), ('enc', 'knob1', 40), ('enc', 'knob2', 90), ('enc', 'knob3', 127), ('enc', 'knob4', 15)],
    'p0-env-shape': [('btn', 'env'), ('enc', 'knob1', 30), ('enc', 'knob2', -40), ('enc', 'knob3', -70), ('enc', 'knob4', 25)],
    'p0-glo-1-filter': [('btn', 'glo')] * 3 + [('enc', 'knob1', -20), ('enc', 'knob2', 60)],
    # Parameter-graphics inventory: every knob page the stock set does not reach, plus a few values and
    # engine/type variants, so the parameter plan is checked against real screens.
    'params-home-filter': [('btn', 'home')],
    'params-home-env-pitch': [('btn', 'env')] * 3 + [('enc', 'knob2', -5), ('btn', 'home'), ('btn', 'home'), ('enc', 'knob3', 3)],
    'params-home-filter-set': [('btn', 'home'), ('enc', 'knob1', -30), ('enc', 'knob2', 60), ('enc', 'knob3', 50), ('enc', 'knob4', 1)],
    'params-env-filter': [('btn', 'env')] * 2,
    'params-env-setup': [('btn', 'env')] * 3,
    'params-env-setup-set': [('btn', 'env')] * 3 + [('enc', 'knob1', -40), ('enc', 'knob2', 2), ('enc', 'knob3', 1), ('enc', 'knob4', -1)],
    'params-lfo-1-set': [('btn', 'lfo'), ('enc', 'knob1', 2), ('enc', 'knob2', 30), ('enc', 'knob3', 1), ('enc', 'knob4', 70)],
    'params-lfo-2': [('btn', 'lfo')] * 2,
    'params-patch-1': [('btn', 'edit')],
    'params-patch-1-set': [('btn', 'edit')] + [('enc', 'knob1', 5), ('enc', 'knob2', 1), ('enc', 'knob3', 40), ('enc', 'knob4', 1)],
    'params-patch-2-set': [('btn', 'edit')] * 2 + [('enc', 'knob1', 11), ('enc', 'knob2', 8), ('enc', 'knob3', -50), ('enc', 'knob4', 1)],
    'params-patch-2-key': [('btn', 'edit')] * 2 + [('enc', 'knob1', 12), ('enc', 'knob2', 16), ('enc', 'knob3', 30)],
    'params-lfo-depths': [('btn', 'lfo')] * 4,
    'params-lfo-depths-set': [('btn', 'lfo')] * 4 + [('enc', 'knob1', -30), ('enc', 'knob2', 25), ('enc', 'knob3', 50)],
    'params-seq-settings': [('btn', 'seq'), ('btn', 'seq')],
    'params-arp-set': [('btn', 'arp'), ('enc', 'knob1', 2), ('enc', 'knob2', 3), ('enc', 'knob3', 2), ('enc', 'knob4', 40)],
    'params-punch-2': [('keys', 1 << 25), ('keys', (1 << 25) | (1 << 8)), ('btn', 'oct_up')],
    'params-midi-1': [('buttons', (1 << 7) | (1 << 0)), ('buttons', 0)],
    'params-midi-2': [('buttons', (1 << 7) | (1 << 0)), ('buttons', 0), ('btn', 'glo')],
    'params-fx-drive-fuzz': [('btn', 'fx')] * 2 + [('enc', 'knob1', 2), ('enc', 'knob2', 50), ('enc', 'knob3', -60), ('enc', 'knob4', 80)],
    'params-fx-delay-tape': [('btn', 'fx')] * 3 + [('enc', 'knob1', 1), ('enc', 'knob3', 50), ('enc', 'knob4', 60)],
    'params-fx-mod-phaser': [('btn', 'fx')] * 4 + [('enc', 'knob1', 2), ('enc', 'knob2', 50), ('enc', 'knob3', 50), ('enc', 'knob4', 60)],
    'params-fx-reverb-space': [('btn', 'fx')] * 5 + [('enc', 'knob1', 2), ('enc', 'knob2', 80), ('enc', 'knob3', 40), ('enc', 'knob4', 60)],
    'params-heat-sat': [('btn', 'glo')] * 4 + [('enc', 'knob1', 1), ('enc', 'knob2', 70)],
    'params-heat-wow': [('btn', 'glo')] * 4 + [('enc', 'knob1', 2), ('enc', 'knob2', 120)],
    'params-comp-set': [('btn', 'glo')] * 5 + [('enc', 'knob1', 2), ('enc', 'knob2', 20), ('enc', 'knob3', 3), ('enc', 'knob4', 8)],
    'params-comp-time': [('btn', 'glo')] * 6 + [('enc', 'knob1', 35), ('enc', 'knob2', 35), ('enc', 'knob3', 35)],
    'params-settings-set': GLO_LONG + [('enc', 'knob3', 40), ('enc', 'knob2', 40)],
    'params-dk-home-filter': [('enc', 'algorithm', 3), ('btn', 'home')],
    'params-dk-env-setup': [('enc', 'algorithm', 3)] + [('btn', 'env')] * 3,
    'params-dk-lfo': [('enc', 'algorithm', 3), ('btn', 'lfo')],
    'params-dk-80s-set': [('enc', 'algorithm', 3), ('enc', 'presets', 2), ('enc', 'knob1', -5), ('enc', 'knob4', -60)],
    # Batch B parameter graphics: each picture at its minimum / maximum, every punch choice.
    'params-pgb-drive-min': [('btn', 'fx')] * 2 + [('enc', 'knob2', -70), ('enc', 'knob3', -130)],
    'params-pgb-drive-max': [('btn', 'fx')] * 2 + [('enc', 'knob2', 70), ('enc', 'knob4', 130)],
    'params-pgb-drive-dist': [('btn', 'fx')] * 2 + [('enc', 'knob1', 1), ('enc', 'knob2', 70), ('enc', 'knob3', -64), ('enc', 'knob4', 64)],
    'params-pgb-drive-dist-min': [('btn', 'fx')] * 2 + [('enc', 'knob1', 1), ('enc', 'knob2', -70), ('enc', 'knob3', -100), ('enc', 'knob4', 20)],
    'params-pgb-delay-min': [('btn', 'fx')] * 3 + [('enc', 'knob3', -130), ('enc', 'knob4', -130)],
    'params-pgb-delay-max': [('btn', 'fx')] * 3 + [('enc', 'knob3', 130), ('enc', 'knob4', 130)],
    'params-pgb-mod-min': [('btn', 'fx')] * 4 + [('enc', 'knob2', -130), ('enc', 'knob3', -130), ('enc', 'knob4', -130)],
    'params-pgb-mod-max': [('btn', 'fx')] * 4 + [('enc', 'knob2', 130), ('enc', 'knob3', 130), ('enc', 'knob4', 130)],
    'params-pgb-mod-play': [('btn', 'fx')] * 4 + [('enc', 'knob2', 40), ('btn', 'play'), ('wait', 700)],
    'params-pgb-reverb-min': [('btn', 'fx')] * 5 + [('enc', 'knob3', -130), ('enc', 'knob4', -130)],
    'params-pgb-reverb-max': [('btn', 'fx')] * 5 + [('enc', 'knob3', 130), ('enc', 'knob4', 130)],
    'params-pgb-sound-min': [('enc', 'knob4', -130)],
    'params-pgb-sound-max': [('enc', 'knob4', 130)],
    'params-pgb-sound-mid': [('enc', 'knob4', -130), ('enc', 'knob4', 64)],
    'params-pgb-sound-play': [('enc', 'knob4', -130), ('enc', 'knob4', 30), ('btn', 'play'), ('wait', 900)],
    'params-pgb-comp-mid': [('btn', 'glo')] * 6 + [('enc', 'knob4', -64)],
    'params-pgb-comp-min': [('btn', 'glo')] * 6 + [('enc', 'knob4', -130)],
    'params-pgb-comp-max': [('btn', 'glo')] * 6 + [('enc', 'knob4', 130)],
    'params-pgb-punch-c0': [('keys', 1 << 25)] + [('enc', 'knob%d' % i, -6) for i in range(1, 5)],
    'params-pgb-punch-c1': [('keys', 1 << 25)] + [('enc', 'knob%d' % i, -6) for i in range(1, 5)] + [('enc', 'knob%d' % i, 1) for i in range(1, 5)],
    'params-pgb-punch-c2': [('keys', 1 << 25)] + [('enc', 'knob%d' % i, -6) for i in range(1, 5)] + [('enc', 'knob%d' % i, 2) for i in range(1, 5)],
    'params-pgb-punch-c3': [('keys', 1 << 25)] + [('enc', 'knob%d' % i, 6) for i in range(1, 5)],
    'params-pgb-punch2-min': [('keys', 1 << 25), ('btn', 'oct_up')] + [('enc', 'knob%d' % i, -40) for i in range(2, 5)],
    'params-pgb-punch2-max': [('keys', 1 << 25), ('btn', 'oct_up')] + [('enc', 'knob%d' % i, 40) for i in range(2, 5)],
    'params-pgb-punch2-mid': [('keys', 1 << 25), ('btn', 'oct_up')] + [('enc', 'knob%d' % i, -40) for i in range(2, 5)] + [('enc', 'knob%d' % i, 15) for i in range(2, 5)],
    'pgf-punch-trem-depth0': [('keys', 1 << 25), ('btn', 'oct_up'), ('enc', 'knob4', -40), ('btn', 'oct_down'), ('enc', 'knob4', 2)],
    'pgf-comp-makeup0': [('btn', 'glo')] * 5 + [('enc', 'knob4', -30)],
    'pgf-comp-makeup1': [('btn', 'glo')] * 5 + [('enc', 'knob4', -30), ('enc', 'knob4', 1)],
    'pgf-comp-makeup2': [('btn', 'glo')] * 5 + [('enc', 'knob4', -30), ('enc', 'knob4', 2)],
    'pgf-comp-makeup4': [('btn', 'glo')] * 5 + [('enc', 'knob4', -30), ('enc', 'knob4', 4)],
    'pgf-env-filter-set': [('btn', 'env')] * 2 + [('enc', 'knob1', 20), ('enc', 'knob2', 50), ('enc', 'knob3', -40), ('enc', 'knob4', 70)],
    'pgf-glo-pan-snap': [('btn', 'glo')] * 2 + [('buttons', 1 << 0), ('enc', 'knob1', -1), ('enc', 'knob2', 1), ('enc', 'knob3', 2), ('buttons', 0)],
    'pgf-glo-pan-set': [('btn', 'glo')] * 2 + [('enc', 'knob1', -30), ('enc', 'knob2', 20), ('enc', 'knob3', 45), ('enc', 'knob4', -10)],
    'params-pgb-punch2-hpsweep': [('keys', 1 << 25), ('enc', 'knob1', 6), ('enc', 'knob2', 6), ('enc', 'knob3', 6), ('btn', 'oct_up'), ('enc', 'knob3', -15)],
    # Batch A parameter graphics: each picture at its extremes, zero and every choice.
    'params-a-hf-min': [('btn', 'home'), ('enc', 'knob3', -130)],
    'params-a-hf-neg': [('btn', 'home'), ('enc', 'knob3', -50), ('enc', 'knob4', 1)],
    'params-a-hf-max': [('btn', 'home'), ('enc', 'knob3', 130), ('enc', 'knob4', 1)],
    'params-a-hf-small': [('btn', 'home'), ('enc', 'knob3', 2)],
    'params-a-hf-small-neg': [('btn', 'home'), ('enc', 'knob3', -1)],
    'params-a-glo-min': [('btn', 'glo')] * 3 + [('enc', 'knob3', -130), ('enc', 'knob4', -110)],
    'params-a-glo-mid': [('btn', 'glo')] * 3 + [('enc', 'knob3', -50), ('enc', 'knob4', 30)],
    'params-a-glo-max': [('btn', 'glo')] * 3 + [('enc', 'knob3', 130), ('enc', 'knob4', 110)],
    'params-a-env-min': [('btn', 'env')] * 3 + [('enc', 'knob1', -130), ('enc', 'knob2', -30), ('enc', 'knob3', 1), ('enc', 'knob4', 1)],
    'params-a-env-max': [('btn', 'env')] * 3 + [('enc', 'knob1', 130), ('enc', 'knob2', -5)],
    'params-a-env-shape': [('btn', 'env')] * 2 + [('enc', 'knob1', 90), ('enc', 'knob2', 60), ('enc', 'knob3', 80), ('enc', 'knob4', 60), ('btn', 'env'), ('enc', 'knob1', 80), ('enc', 'knob2', 7)],
    'params-a-env-shape-neg': [('btn', 'env')] * 2 + [('enc', 'knob1', 90), ('enc', 'knob2', 60), ('enc', 'knob3', 80), ('enc', 'knob4', 60), ('btn', 'env'), ('enc', 'knob1', -90), ('enc', 'knob2', 8)],
    'params-a-env-small': [('btn', 'env')] * 3 + [('enc', 'knob1', 3), ('enc', 'knob2', 9)],
    'params-a-lfo-min': [('btn', 'lfo'), ('enc', 'knob2', -130), ('enc', 'knob3', -30), ('enc', 'knob4', -130)],
    'params-a-lfo-max': [('btn', 'lfo'), ('enc', 'knob2', 130), ('enc', 'knob3', 16), ('enc', 'knob4', 130)],
    'params-a-lfo-mid': [('btn', 'lfo'), ('enc', 'knob2', -40), ('enc', 'knob3', 9), ('enc', 'knob4', -60)],
    'params-a-lfo-play': [('btn', 'lfo'), ('enc', 'knob2', 20), ('enc', 'knob4', 60), ('keys', 1), ('wait', 330)],
    'params-a-lfo2-play': [('btn', 'lfo'), ('btn', 'lfo'), ('enc', 'knob2', 60), ('enc', 'knob4', 127), ('enc', 'knob3', 17), ('keys', 1), ('wait', 700)],
    'params-a-patch-off': [('btn', 'edit')] + [('enc', 'knob1', -20), ('enc', 'knob3', -130), ('enc', 'knob4', -1)],
    'params-a-patch-osc': [('btn', 'edit')] + [('enc', 'knob1', -20), ('enc', 'knob1', 1), ('enc', 'knob3', -40), ('enc', 'knob4', 1)],
    'params-a-patch-mix': [('btn', 'edit')] + [('enc', 'knob1', -20), ('enc', 'knob1', 2), ('enc', 'knob2', 13), ('enc', 'knob3', 130), ('enc', 'knob4', 1)],
    'params-a-patch-vel': [('btn', 'edit')] + [('enc', 'knob1', -20), ('enc', 'knob1', 11), ('enc', 'knob2', 14), ('enc', 'knob3', -5), ('enc', 'knob4', 1)],
    'params-a-patch-note': [('btn', 'edit')] + [('enc', 'knob1', -20), ('enc', 'knob1', 12), ('enc', 'knob2', 15), ('enc', 'knob3', 1), ('enc', 'knob4', 1)],
    'params-a-patch-gate': [('btn', 'edit')] + [('enc', 'knob1', -20), ('enc', 'knob1', 13), ('enc', 'knob2', 16), ('enc', 'knob3', 90), ('enc', 'knob4', 1)],
    'params-a-patch-min': [('btn', 'edit')] + [('enc', 'knob3', -130), ('enc', 'knob4', 1)],
    'params-a-depths-min': [('btn', 'lfo')] * 4 + [('enc', 'knob1', -130), ('enc', 'knob2', -60), ('enc', 'knob3', -10)],
    'params-a-depths-max': [('btn', 'lfo')] * 4 + [('enc', 'knob1', 130), ('enc', 'knob2', 130), ('enc', 'knob3', 130)],
    'params-a-dk-80s-min': [('enc', 'algorithm', 3), ('enc', 'presets', 2), ('enc', 'knob1', -20)],
    'params-a-dk-80s-max': [('enc', 'algorithm', 3), ('enc', 'presets', 2), ('enc', 'knob1', 20)],
    'params-a-dk-80s-up': [('enc', 'algorithm', 3), ('enc', 'presets', 2), ('enc', 'knob1', 5)],
    'params-a-dk-patch': [('enc', 'algorithm', 3), ('btn', 'lfo'), ('btn', 'lfo'), ('btn', 'lfo'), ('enc', 'knob1', -20), ('enc', 'knob1', 12), ('enc', 'knob2', 1), ('enc', 'knob3', 70)],
    # Batch C parameter graphics: each picture at its minimum / middle / maximum and every choice.
    'params-pgc-settings-min': _DK_HOME + [('enc', 'knob3', -130)],          # SNAP moved to the kit's HOME
    'params-pgc-settings-max': _DK_HOME + [('enc', 'knob3', 130)],
    'params-pgc-comp-min': [('btn', 'glo')] * 5 + [('enc', 'knob2', -60), ('enc', 'knob3', -10), ('enc', 'knob4', -30)],
    'params-pgc-comp-max': [('btn', 'glo')] * 5 + [('enc', 'knob2', 60), ('enc', 'knob3', 10), ('enc', 'knob4', 30)],
    'params-pgc-comp-r1': [('btn', 'glo')] * 5 + [('enc', 'knob2', -60), ('enc', 'knob2', 12), ('enc', 'knob3', -10), ('enc', 'knob3', 1), ('enc', 'knob4', 4)],
    'params-pgc-comp-r2': [('btn', 'glo')] * 5 + [('enc', 'knob2', -60), ('enc', 'knob2', 36), ('enc', 'knob3', -10), ('enc', 'knob3', 2), ('enc', 'knob4', 16)],
    'params-pgc-comp-r4': [('btn', 'glo')] * 5 + [('enc', 'knob2', 60), ('enc', 'knob2', -6), ('enc', 'knob3', -10), ('enc', 'knob3', 4), ('enc', 'knob4', 20)],
    'params-pgc-comp-r5': [('btn', 'glo')] * 5 + [('enc', 'knob3', -10), ('enc', 'knob3', 5), ('enc', 'knob4', 2)],
    'params-pgc-ctime-min': [('btn', 'glo')] * 6 + [('enc', 'knob%d' % i, -130) for i in range(1, 4)],
    'params-pgc-ctime-max': [('btn', 'glo')] * 6 + [('enc', 'knob%d' % i, 130) for i in range(1, 4)],
    'params-pgc-ctime-mid': [('btn', 'glo')] * 6 + [('enc', 'knob%d' % i, -130) for i in range(1, 4)] + [('enc', 'knob%d' % i, 64) for i in range(1, 4)],
    'params-pgc-heat-bit-mid': [('btn', 'glo')] * 4 + [('enc', 'knob2', 70)],
    'params-pgc-heat-bit-max': [('btn', 'glo')] * 4 + [('enc', 'knob2', 130)],
    'params-pgc-heat-sat-max': [('btn', 'glo')] * 4 + [('enc', 'knob1', 1), ('enc', 'knob2', 130)],
    'params-pgc-heat-sat-low': [('btn', 'glo')] * 4 + [('enc', 'knob1', 1), ('enc', 'knob2', 25)],
    'params-pgc-heat-wow-mid': [('btn', 'glo')] * 4 + [('enc', 'knob1', 2), ('enc', 'knob2', 60)],
    'params-pgc-midi-1-off': [('buttons', (1 << 7) | (1 << 0)), ('buttons', 0)] + [('enc', 'knob%d' % i, -20) for i in range(1, 4)],
    'params-pgc-midi-1-hi': [('buttons', (1 << 7) | (1 << 0)), ('buttons', 0), ('enc', 'knob1', 9), ('enc', 'knob2', 14), ('enc', 'knob3', 9), ('enc', 'knob4', 1)],
    'params-pgc-midi-2-off': [('buttons', (1 << 7) | (1 << 0)), ('buttons', 0), ('btn', 'glo')] + [('enc', 'knob%d' % i, -20) for i in range(1, 5)],
    'params-pgc-midi-2-10': [('buttons', (1 << 7) | (1 << 0)), ('buttons', 0), ('btn', 'glo')] + [('enc', 'knob%d' % i, -20) for i in range(1, 5)] + [('enc', 'knob%d' % i, 10) for i in range(1, 5)],
    # The SAVE / project menus run only with the device project store, so these go through tools/ui_emulator.c
    # (DINGHY_DEVICE_SAVE_TEST, an in-memory NOR chip): ('emu',) first, then btn / enc / wait steps.
    'save-menu': [('emu',), ('wait', 500), ('btn', 'save'), ('wait', 300)],
    'save-menu-knob': [('emu',), ('wait', 500), ('btn', 'save'), ('enc', 'knob1', 1)],
    'save-load-slots': [('emu',), ('wait', 500), ('btn', 'save'), ('wait', 500), ('enc', 'knob1', 2), ('btn', 'save'), ('wait', 1500)],
    'save-first': [('emu',), ('wait', 500), ('btn', 'save'), ('enc', 'knob1', 3), ('wait', 1300), ('btn', 'save'), ('wait', 300)],
    'save-name': [('emu',), ('wait', 500), ('btn', 'save'), ('wait', 500), ('enc', 'knob1', 1), ('btn', 'save'), ('wait', 1000),
                  ('btn', 'save'), ('wait', 300), ('enc', 'knob2', 3), ('enc', 'knob1', 5), ('wait', 1300)],
}

# Sequencer look audit (seqlook): seeded tonal and drum patterns in every state the step screens draw.
_W = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]     # white key of step 1..16 (br_key_position)
# Kit lanes are picked with SELECT on the step grid (black keys are inert there).
_DK = [('enc', 'algorithm', 3)]
SCREENS.update({
    'seqlook-t-seeded': [('seed', 'tonal'), ('btn', 'seq')],
    'seqlook-t-play': [('seed', 'tonal'), ('btn', 'seq'), ('btn', 'play')],
    'seqlook-t-held': [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[5])],
    'seqlook-t-held-len': [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[5]), ('enc', 'knob2', 1)],
    'seqlook-t-held-cond': [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[10]), ('enc', 'knob4', 1)],
    'seqlook-t-focus-vel': [('seed', 'tonal'), ('btn', 'seq'), ('enc', 'knob3', 1)],
    'seqlook-t-focus-note': [('seed', 'tonal'), ('btn', 'seq'), ('enc', 'knob1', 1)],
    'seqlook-t-len12': [('seed', 'tonal12'), ('btn', 'seq'), ('btn', 'play')],
    'seqlook-t-32-bank1': [('seed', 'tonal32'), ('btn', 'seq'), ('btn', 'play')],
    'seqlook-t-32-bank2': [('seed', 'tonal32'), ('btn', 'seq'), ('btn', 'oct_up'), ('btn', 'play')],
    'seqlook-t-32-bank3': [('seed', 'tonal32'), ('btn', 'seq'), ('btn', 'oct_up'), ('btn', 'oct_up')],
    'seqlook-t-settings': [('seed', 'tonal32'), ('btn', 'seq'), ('btn', 'seq')],
    'seqlook-t-rec': [('seed', 'tonal'), ('btn', 'seq'), ('btn', 'rec'), ('btn', 'play')],
    'seqlook-t-muted': [('seed', 'tonal'), ('btn', 'glo'), ('btn', 'oct_down'), ('btn', 'seq'), ('btn', 'play')],
    'seqlook-t-sound': [('seed', 'tonal'), ('btn', 'play')],
    'seqlook-d-seeded': [('seed', 'kit')] + _DK + [('btn', 'seq')],
    'seqlook-d-play': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('btn', 'play')],
    'seqlook-d-lane3': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 2)],
    'seqlook-d-held': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('keys', 1 << _W[8])],
    'seqlook-d-held-vel': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('keys', 1 << _W[8]), ('enc', 'knob3', 1)],
    'seqlook-d-retrig': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 2), ('btn', 'arp'), ('btn', 'play')],
    'seqlook-d-len12': [('seed', 'kit12')] + _DK + [('btn', 'seq'), ('btn', 'play')],
    'seqlook-d-32-bank2': [('seed', 'kit32')] + _DK + [('btn', 'seq'), ('btn', 'oct_up'), ('btn', 'play')],
    'seqlook-d-mute': [('seed', 'kit')] + _DK + [('btn', 'edit'), ('btn', 'oct_down'), ('btn', 'seq'), ('btn', 'play')],
})
# 2x8 sequencer build (seqbuild): playhead on each row and at the beat gap, held step under the playhead,
# lengths 1 / 12 / 16 / 17 / 32 / 64, every bank, every drum lane, muted and record. ('wait', ms) lets the
# playhead run on: 125 ms per step at 120 BPM, measured from the press of PLAY.
_PL = lambda ms: [('btn', 'play'), ('wait', ms)]
SCREENS.update({
    'sb-t-play-s4': [('seed', 'tonal'), ('btn', 'seq')] + _PL(0 * 125),
    'sb-t-play-a': [('seed', 'tonal'), ('btn', 'seq')] + _PL(3 * 125),
    'sb-t-play-b': [('seed', 'tonal'), ('btn', 'seq')] + _PL(7 * 125),
    'sb-t-play-c': [('seed', 'tonal'), ('btn', 'seq')] + _PL(11 * 125),
    'sb-t-held-play': [('seed', 'tonal'), ('btn', 'seq')] + _PL(1525) + [('keys', 1 << _W[8])],
    'sb-t-held-play-b': [('seed', 'tonal'), ('btn', 'seq')] + _PL(1650) + [('keys', 1 << _W[8])],
    'sb-t-held-play-c': [('seed', 'tonal'), ('btn', 'seq')] + _PL(2025) + [('keys', 1 << _W[12])],
    'sb-t-held-knob1': [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[14]), ('enc', 'knob1', -4)],
    'sb-t-held-knob3': [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[0]), ('enc', 'knob3', 7)],
    'sb-t-held-nudge': [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[2]), ('btn', 'oct_down'), ('btn', 'oct_down')],
    'sb-t-held-cond-long': [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[3]), ('enc', 'knob4', 104)],
    'sb-t-held-cond-first': [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[3]), ('enc', 'knob4', 101)],
    'sb-t-held-len-frac': [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[3]), ('enc', 'knob2', 3)],
    'sb-t-len1': [('seed', 'tonal1'), ('btn', 'seq')] + _PL(300),
    'sb-t-len17': [('seed', 'tonal17'), ('btn', 'seq')] + _PL(300),
    'sb-t-len17-bank2': [('seed', 'tonal17'), ('btn', 'seq'), ('btn', 'oct_up')] + _PL(16 * 125),
    'sb-t-64-bank1': [('seed', 'tonal64'), ('btn', 'seq')] + _PL(5 * 125),
    'sb-t-64-bank2': [('seed', 'tonal64'), ('btn', 'seq'), ('btn', 'oct_up')] + _PL(5 * 125),
    'sb-t-64-bank3': [('seed', 'tonal64'), ('btn', 'seq')] + [('btn', 'oct_up')] * 2 + _PL(5 * 125),
    'sb-t-64-bank4': [('seed', 'tonal64'), ('btn', 'seq')] + [('btn', 'oct_up')] * 3 + _PL(5 * 125),
    'sb-t-64-sound': [('seed', 'tonal64')] + _PL(5 * 125),
    'sb-t-muted-rec': [('seed', 'tonal'), ('btn', 'glo'), ('btn', 'oct_down'), ('btn', 'seq'), ('btn', 'rec')],
    'sb-t-t2': [('seed', 'tonal'), ('enc', 'algorithm', 1), ('btn', 'seq'), ('keys', 1), ('keys', 0), ('keys', 1 << 9), ('keys', 0)] + _PL(2 * 125),
    'sb-d-lane1': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 0)] + _PL(6 * 125),
    'sb-d-lane2': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 1)] + _PL(9 * 125),
    'sb-d-lane3': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 2)] + _PL(13 * 125),
    'sb-d-lane4': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 3)] + _PL(3 * 125),
    'sb-d-held-play': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 2)] + _PL(1775) + [('keys', 1 << _W[10])],
    'sb-d-held-play-b': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 2)] + _PL(1650) + [('keys', 1 << _W[10])],
    'sb-d-mute-lane2': [('seed', 'kit')] + _DK + [('btn', 'edit'), ('enc', 'select', 1), ('btn', 'oct_down'), ('btn', 'seq'), ('enc', 'select', 1)],
    'sb-d-held-lane2': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 1), ('keys', 1 << _W[15]), ('enc', 'knob4', 2)],
    'sb-d-held-nudge': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('keys', 1 << _W[4]), ('btn', 'oct_up')],
    'sb-d-len1': [('seed', 'kit1')] + _DK + [('btn', 'seq')] + _PL(300),
    'sb-d-64-bank4': [('seed', 'kit64')] + _DK + [('btn', 'seq')] + [('btn', 'oct_up')] * 3 + _PL(4 * 125),
    'sb-d-muted-rec': [('seed', 'kit')] + _DK + [('btn', 'edit'), ('btn', 'oct_down'), ('btn', 'seq'), ('btn', 'rec')] + _PL(2 * 125),
    'sb-d-retrig-held': [('seed', 'kit')] + _DK + [('btn', 'seq'), ('enc', 'select', 2), ('btn', 'arp'), ('keys', 1 << _W[14]), ('enc', 'knob1', 1)],
})


# Batch D parameter graphics: sequencer tiles (new steps and held, tonal and drum), SEQ settings and ARP at
# their extremes and every choice. Each entry turns knobs 1-4 by the given detents.
def _pgd(knobs):
    return [('enc', 'knob%d' % (i + 1), n) for i, n in enumerate(knobs) if n]
_PGD_T = {'a': (-60, -15, -100, 25), 'b': (1, -12, -36, 50), 'c': (67, 2000, 27, 75), 'd': (10, 16, -50, 100),
          'e': (6, 144, -80, 101), 'f': (3, 8, 0, 102), 'g': (5, 0, 0, 103), 'h': (8, 0, 0, 104), 'i': (11, 0, 0, 105),
          'j': (4, 0, 0, 106), 'k': (2, 0, 0, 107), 'l': (7, 0, 0, 108), 'm': (9, 0, 0, 109), 'n': (0, 0, 0, 110),
          'o': (0, 0, 0, 117), 'p': (0, 0, 0, 137), 'q': (0, 0, 0, 141), 'r': (0, 0, 0, 144), 's': (0, 0, 0, 1),
          't': (0, 0, 0, 12)}
for _k, _v in _PGD_T.items():
    SCREENS['pgd-t-' + _k] = [('seed', 'tonal'), ('btn', 'seq')] + _pgd(_v)
for _k, _v in {'a': (-30, 3, -90, 25), 'b': (24, 40, 27, 117), 'c': (0, 0, 0, 0), 'd': (-1, 1, 0, 103)}.items():
    SCREENS['pgd-d-' + _k] = [('seed', 'kit')] + _DK + [('btn', 'seq')] + _pgd(_v)
SCREENS['pgd-t-held'] = [('seed', 'tonal'), ('btn', 'seq'), ('keys', 1 << _W[5])] + _pgd((3, -8, 0, 30))
SCREENS['pgd-d-held'] = [('seed', 'kit')] + _DK + [('btn', 'seq'), ('keys', 1 << _W[8])] + _pgd((0, 2000, 0, 140))
for _k, _v in {'a': (-15, -1, 1), 'b': (48, 1, 2), 'c': (16, 0, 3), 'd': (1, 1, 4), 'e': (-8, 0, 5), 'f': (0, 0, 6)}.items():
    SCREENS['pgd-set-' + _k] = [('btn', 'seq'), ('btn', 'seq')] + _pgd(_v)
for _k, _v in {'a': (0, 0, 0, -63), 'b': (0, 0, 1, -40), 'c': (0, 0, 2, 20), 'd': (0, 0, 3, 63)}.items():
    SCREENS['pgd-arp-' + _k] = [('btn', 'arp')] + _pgd(_v)


# Sequencer look audit: screens that start from a seeded pattern. ('seed', NAME) as the first step builds a
# second binary from a temporary copy of the tree in which br_draw_body() first runs SEQLOOK_SEED_C (below)
# once; the shipped sources are not changed. The seeds stand in for states reached by recording, session
# import or many knob turns: sharps and extreme octaves, ties, accents, conditions, velocities, retriggers.
SEQLOOK_SEED_C = r"""
static void seqlook_seed_tonal(uint32_t p, uint32_t length)
{
    static const struct { uint8_t step, note, vel; uint16_t len; uint8_t cond, tie; } t[] = {
        {0, 60, 120, 16, 0, 0}, {2, 61, 100, 8, 0, 0}, {4, 63, 60, 16, 50, 0}, {5, 67, 127, 48, 0, 0},
        {8, 58, 30, 16, 0, 1}, {10, 78, 90, 16, 107, 0}, {12, 72, 110, 16, 0, 1}, {13, 44, 80, 16, 110, 0},
        {14, 3, 64, 16, 0, 0}, {15, 64, 64, 4, 0, 0},
        {16, 65, 100, 16, 0, 0}, {19, 70, 100, 32, 0, 0}, {24, 66, 50, 16, 25, 0}, {28, 127, 127, 16, 0, 0},
        {31, 48, 100, 16, 0, 0}, {33, 61, 90, 16, 0, 0}, {36, 0, 100, 16, 0, 0}, {40, 67, 100, 64, 0, 0},
        {47, 75, 100, 16, 0, 1}, {48, 70, 100, 16, 0, 0}, {52, 123, 100, 16, 0, 0}, {56, 62, 40, 16, 0, 0}, {63, 60, 100, 16, 0, 0},
    };
    br_part[p].length = length;
    for (uint32_t i = 0; i < sizeof t / sizeof t[0]; ++i) {
        if (t[i].step >= length) continue;
        br_part[p].step[t[i].step] = BR_NOTE_STEP(t[i].note, t[i].vel) | (t[i].tie ? BR_STEP_TIE : 0u);
        br_step_length_set(p, t[i].step, 0u, t[i].len);
        br_step_velocity_set(p, t[i].step, 0u, t[i].vel);
        br_step_condition_set(p, t[i].step, 0u, t[i].cond);
    }
}
static void seqlook_seed_kit(uint32_t length)
{
    /* lane, step, accent, velocity, length, condition, note, retrig (count | spacing << 3) */
    static const struct { uint8_t lane, step, acc, vel; uint16_t len; uint8_t cond, note, retrig; } d[] = {
        {0, 0, 1, 127, 0, 0, 60, 0}, {0, 4, 0, 100, 0, 0, 60, 0}, {0, 8, 1, 127, 0, 0, 60, 0}, {0, 12, 0, 100, 0, 0, 60, 0},
        {0, 14, 0, 70, 0, 50, 60, 0},
        {1, 4, 0, 100, 0, 0, 60, 0}, {1, 12, 0, 100, 0, 0, 62, 0}, {1, 15, 0, 30, 0, 107, 60, 2 | 1 << 3},
        {2, 2, 0, 90, 0, 0, 60, 0}, {2, 6, 0, 90, 3, 0, 60, 0}, {2, 10, 0, 90, 0, 0, 60, 0}, {2, 14, 0, 90, 0, 0, 60, 3 | 2 << 3},
        {3, 0, 0, 100, 40, 0, 60, 0}, {3, 11, 0, 100, 0, 25, 57, 0},
        {0, 16, 1, 127, 0, 0, 60, 0}, {0, 20, 0, 100, 0, 0, 60, 0}, {1, 20, 0, 100, 0, 0, 60, 0}, {2, 18, 0, 90, 0, 0, 60, 0},
        {0, 24, 1, 127, 0, 0, 60, 0}, {1, 28, 0, 100, 0, 0, 60, 0}, {2, 30, 0, 90, 0, 0, 60, 1 | 1 << 3},
    };
    uint32_t p = BR_DRUM_PART;
    br_part[p].length = length;
    for (uint32_t i = 0; i < sizeof d / sizeof d[0]; ++i) {
        uint32_t s = d[i].step, l = d[i].lane;
        if (s >= length) continue;
        br_part[p].step[s] |= (1u << l) | (d[i].acc ? 1u << (l + 8u) : 0u);
        br_step_velocity_set(p, s, l, d[i].vel);
        br_step_length_set(p, s, l, d[i].len);
        br_step_condition_set(p, s, l, d[i].cond);
        br_step_note_set(p, s, l, d[i].note);
        br_seq_kit_retrig[s][l] = d[i].retrig;
    }
}
static void seqlook_seed_hook(void)
{
    static int done;
    const char *s;
    if (done) return;
    done = 1;
    s = getenv("SEQLOOK_SEED");
    if (!s || !*s) return;
    if (!strcmp(s, "tonal")) seqlook_seed_tonal(0u, 16u);
    else if (!strcmp(s, "tonal12")) seqlook_seed_tonal(0u, 12u);
    else if (!strcmp(s, "tonal32")) seqlook_seed_tonal(0u, 32u);
    else if (!strcmp(s, "kit")) seqlook_seed_kit(16u);
    else if (!strcmp(s, "kit12")) seqlook_seed_kit(12u);
    else if (!strcmp(s, "kit32")) seqlook_seed_kit(32u);
    else if (!strcmp(s, "tonal1")) seqlook_seed_tonal(0u, 1u);
    else if (!strcmp(s, "tonal17")) seqlook_seed_tonal(0u, 17u);
    else if (!strcmp(s, "tonal64")) seqlook_seed_tonal(0u, 64u);
    else if (!strcmp(s, "kit64")) seqlook_seed_kit(64u);
    else if (!strcmp(s, "kit1")) seqlook_seed_kit(1u);
    br_ui.redraw = 1u;
}
"""


# SMK-37 Pro (from the JamesOS-K render_screens.py --smk-preview): its first screens as above, then
# every module and subpage. Transport and edit gestures run through the firmware's normal host input
# path. The pattern fixture only supplies slot state for the preview; the target pattern
# implementation has its own tests.
SMK_BASE = ('p0-sound-0', 'p0-env-0', 'p0-fx-0', 'p0-fx-1', 'p0-fx-3', 'punch-loop', 'punch-echo', 'punch-filter',
            'punch-tremolo', 'dk-seq-0', 'p0-fx-2', 'p0-fx-4', 'p0-glo-0', 'p0-glo-0-signal', 'p0-glo-1', 'p0-glo-2',
            'p0-glo-2-monitor', 'p0-glo-long-midi-1', 'p0-glo-long-midi-2', 'p0-glo-3', 'p0-glo-4', 'p0-glo-5', 'p0-glo-6', 'p0-seq-0', 'p0-sel-0', 'p0-lfo-0',
            'p0-arp-0', 'p0-edit-2', 'dk-retrig-edit', 'dk-arp-message', 'dk-sound-0', 'dk-kit-solo', 'dk-kit-mute',
            'dk-kit-level', 'dk-sel-0', 'dk-braids', 'dk-plaits', 'dk-80s', 'dk-90s',
            'p0-fx-1-bias-50', 'p0-fx-2-tone-0', 'p0-fx-3-feedback-100', 'p0-fx-4-pre-1-8')
SMK_SCREENS = {
    **{name: SCREENS[name] for name in SMK_BASE},
    "boot": [],
    "sound-filter": [("btn", "home")],
    "envelope-2": [("btn", "env")] * 2,
    "envelope-3": [("btn", "env")] * 3,
    "sequence-settings": [("btn", "seq")] * 2,
    "sequence-step-edit": [("btn", "seq"), ("keys", 1), ("keys", 0), ("keys", 1)],
    "p0-lfo-0": [("btn", "edit"), ("btn", "lfo")],
    "lfo-2": [("btn", "edit")] + [("btn", "lfo")] * 2,
    "global-lfo": [("btn", "edit")] + [("btn", "lfo")] * 3,
    "patch-1": [("btn", "glo")] * 7,
    "patch-2": [("btn", "glo")] * 8,
    "lfo-depths": [("btn", "edit")] + [("btn", "lfo")] * 4,
    "midi-synth": [("buttons", (1 << 0) | (1 << 7)), ("buttons", 0)],
    "midi-drums": [("buttons", (1 << 0) | (1 << 7)), ("buttons", 0), ("btn", "glo")],
    "patterns": [("buttons", (1 << 0) | (1 << 11)), ("buttons", 0)],
    "project-menu": [("btn", "save")],
    "project-save-slot": [("btn", "save"), ("enc", "knob1", 1), ("btn", "save")],
    "project-name": [("btn", "save"), ("enc", "knob1", 1), ("btn", "save"), ("btn", "save")],
    "project-confirm": [("btn", "save"), ("enc", "knob1", 1), ("btn", "save"), ("btn", "save"), ("btn", "save")],
    "project-init": [("btn", "save"), ("enc", "knob1", 3), ("btn", "save")],
    "project-overwrite": [("btn", "save"), ("enc", "knob1", 1), ("btn", "save"), ("btn", "save"), ("btn", "save"), ("btn", "save")],
    "project-load-slot": [("btn", "save"), ("enc", "knob1", 2), ("btn", "save")],
}
# The SMK app is built through this wrapper (JamesOS-K --smk-preview): device save test store, the
# pattern-song experiment with a fixed 7-slot fixture, and the MIDI clock experiment.
SMK_PREVIEW_SOURCE = (
    '#define DINGHY_SMK_PREVIEW 1\n'
    '#define DINGHY_DEVICE_SAVE_TEST 1\n'
    '#define BR_PATTERN_SONG_EXPERIMENT 1\n'
    '#include "{dinghy}"\n'
    'fm1_pattern_song br_ps_state = {{.used = 7, .queued = FM1_PATTERN_NONE}};\n'
    'int br_ps_select(uint32_t target) {{ br_ps_state.active = target; return 1; }}\n'
    'int br_ps_copy_pattern(uint32_t target) {{ (void)target; return 0; }}\n'
    'int br_ps_paste_pattern(uint32_t target) {{ (void)target; return 0; }}\n'
)


# SMK-37 Pro only states (--target smk37): a copy of firmware/ gets a hook in br_draw_body() that applies the
# state named by SMKSTATE once the clock passes SMKSTATE_T (set just before the screenshot):
#   peek       the fader peek (the GLOBAL mixer shown while a fader moves, without leaving the page)
#   msg:TEXT   br_message(TEXT)
#   queued     pattern slots: active 2, queued 5, slots 1-5 saved
#   arrow3     three presses of the track arrow (right)
#   poly       br_oct_hint's polyphony line (device-only; hosts have no poly mode)
#   link       fader link switched on (FADER LINK ON), T1 = T2 = 80; link-quiet: without the
#              message; link-peek: and the fader peek
# Pad-held retrig, velocity, wheels and pad LEDs are device-only and not rendered.
SMK_STATE_HOOK = r"""
static void smkstate_hook(void)
{
    static int done;
    const char *s = getenv("SMKSTATE"), *t = getenv("SMKSTATE_T");
    if (done || !s || !t || fm1_ms < (uint32_t)atoi(t)) return;
    done = 1;
    if (!strcmp(s, "peek")) {
        br_smk_fader_peek.active = 1u; br_smk_fader_peek.until = fm1_ms + 600000u;
        br_smk_fader_peek.page = br_ui.page; br_smk_fader_peek.screen = br_ui.screen[br_ui.page];
        br_smk_fader_peek.part = br_ui.part;
    } else if (!strncmp(s, "msg:", 4)) br_message(s + 4);
    else if (!strcmp(s, "queued")) { br_ps_state.queued = 4u; br_ps_state.active = 1u; br_ps_state.used = 0x1fu; }
    else if (!strcmp(s, "arrow3")) { br_smk_track_turn(1); br_smk_track_turn(1); br_smk_track_turn(1); }
    else if (!strncmp(s, "link", 4)) {          /* fader link on, T1 = T2 moved together */
        br_smk_fader_link = 1u; br_message("FADER LINK ON"); br_ui.message_start = fm1_ms - 400u; br_part[0].sound.level = br_part[1].sound.level = 80;
        if (!strcmp(s, "link-quiet")) br_ui.message_until = fm1_ms;
        if (!strcmp(s, "link-peek")) { br_ui.message_until = fm1_ms; br_smk_fader_moved(0u); br_smk_fader_peek.until = fm1_ms + 600000u; }
    }
    br_ui.redraw = 1u;
}
"""
SMK_STATES = {           # name -> (steps, SMKSTATE)
    'smk-peek-home': ([], 'peek'),
    'smk-peek-seq': ([('btn', 'seq')], 'peek'),
    'smk-peek-dk-seq': ([('enc', 'algorithm', 3), ('btn', 'seq')], 'peek'),
    'smk-fader-link-on': ([('btn', 'glo')], 'link'),
    'smk-fader-link-mixer': ([('btn', 'glo')], 'link-quiet'),
    'smk-fader-link-peek': ([], 'link-peek'),
    'smk-poly-hint': ([], 'poly'),
    'smk-poly-hint-env': ([('btn', 'env')], 'poly'),
    'smk-poly-message': ([], 'msg:POLY: 6 NOTES'),
    'smk-echo-unavailable': ([], 'msg:ECHO UNAVAILABLE IN POLY'),
    'smk-kit-home-or-seq': ([('enc', 'algorithm', 3), ('btn', 'lfo')], 'msg:KIT: HOME OR SEQ'),
    'smk-kit-home-page2': ([('enc', 'algorithm', 3)], 'msg:KIT: HOME PAGE 2'),
    'smk-patterns-hold-message': ([('buttons', (1 << 0) | (1 << 11)), ('buttons', 0), ('btn', 'seq')], None),
    'smk-patterns-queued': ([('buttons', (1 << 0) | (1 << 11)), ('buttons', 0)], 'queued'),
    'smk-arrow-to-dk-from-patch': ([('btn', 'edit')], 'arrow3'),
    'smk-arrow-to-dk-from-lfo': ([('btn', 'edit'), ('btn', 'lfo')], 'arrow3'),
    'smk-knob-bank-home': ([('btn', 'lfo')], None),
    'smk-seq-steps-playing': ([('btn', 'seq'), ('keys', 1), ('keys', 0), ('keys', 1 << 4), ('keys', 0), ('keys', 1 << 7),
                               ('keys', 0), ('keys', 1 << 12), ('keys', 0), ('btn', 'play'), ('wait', 700)], None),
    'smk-seq-held-step': ([('btn', 'seq'), ('keys', 1 << 4), ('keys', 0), ('keys', 1 << 4)], None),
    'smk-dk-seq-hits': ([('enc', 'algorithm', 3), ('btn', 'seq'), ('keys', 1), ('keys', 0), ('keys', 1 << 7), ('keys', 0),
                         ('keys', 1 << 14), ('keys', 0), ('btn', 'play'), ('wait', 500)], None),
    'smk-seq-bank2': ([('btn', 'seq'), ('btn', 'seq'), ('enc', 'knob1', 16), ('btn', 'seq'), ('btn', 'oct_up')], None),
}


# Pattern arranger (the PATTERNS page's PATTERNS and ARRANGE views): tests/test_pattern_arranger_ui.c drives the
# real app (owner, arrangement engine, audio) through buttons, knobs and keys and writes a screenshot of each
# state; rendered for the chosen --target (with polyphony, the device default). Names as below.
ARR_STATES = ('pattern-chain', 'perform-pattern', 'arr-empty', 'arr-gap-between', 'arr-short-song',
              'perform-with-song', 'arr-removed', 'arr-selection', 'arr-loop', 'arr-message-full',
              'arr-long-scrolled', 'arr-playing-loop-pass', 'arr-loaded')


def arranger_states(out, target, scale, only):
    import os, tempfile
    want = [n for n in ARR_STATES if not only or n in only]
    if not want: return
    with tempfile.TemporaryDirectory() as shots:
        env = host_target.environment(target, True)
        subprocess.run([sys.executable, 'tests/run_pattern_arranger_ui.py', '--reuse-dsp', '--shots', shots],
                       cwd=ROOT, env=env, check=True, capture_output=True)
        for name in want:
            w, h, rgb = ppm(Path(shots) / (name + '.ppm')); png(out / (name + '.png'), w, h, rgb, scale); print('wrote', name)


def smk_state_root(work):
    """A throwaway copy of firmware/, tests/ and tools/ with SMK_STATE_HOOK spliced into br_draw_body()."""
    import shutil
    root = work / 'smk-state-tree'
    if root.exists(): shutil.rmtree(root)
    root.mkdir(parents=True)
    for d in ('firmware', 'tests', 'tools'):
        shutil.copytree(ROOT / d, root / d, symlinks=True, ignore=shutil.ignore_patterns('__pycache__'))
    (root / 'build').mkdir()
    (root / 'build/host').symlink_to((ROOT / 'build/host').resolve())
    (root / 'assets').symlink_to((ROOT / 'assets').resolve())
    ui = root / 'firmware/src/instrument_ui.c'
    text = ui.read_text()
    anchor = 'static void br_draw_body(void)\n{\n'
    assert text.count(anchor) == 1, 'SMK state anchor not found'
    text = text.replace(anchor, SMK_STATE_HOOK + anchor + '    smkstate_hook();\n')
    ui.write_text('#include <stdlib.h>\n#include <string.h>\n' + text)
    return root


def build_smk(binary, root=None):
    """the SMK-37 Pro host app (product define, poly off, as the JamesOS-K --smk-preview build)"""
    root = root or ROOT
    objs = sorted((ROOT / 'build/host/braids').glob('*.o')) + sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objs:
        raise SystemExit('Host DSP objects are missing: run tools/build_braids_host.sh first')
    flags = [f for f in FLAGS if f not in host_target.product_flags('smk37')] + host_target.product_flags('smk37')
    planner = binary.parent / 'planner.o'
    subprocess.run(['cc', *flags, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=root, check=True)
    source = binary.parent / (binary.name + '-smk-preview.c')
    source.write_text(SMK_PREVIEW_SOURCE.format(dinghy=root / 'firmware/src/dinghy.c'))
    result = subprocess.run(
        ['c++', '-x', 'c', *flags, '-DDINGHY_HOST', '-DBR_MIDI_CLOCK_EXPERIMENT=1', str(source), '-x', 'none',
         str(planner), *map(str, objs), '-o', str(binary)], cwd=root, capture_output=True, text=True,
    )
    if result.returncode:
        sys.stderr.write(result.stderr)
        result.check_returncode()


def seeded_root(work):
    """A throwaway copy of firmware/ and tests/ with the seed hook spliced into br_draw_body()."""
    import shutil
    root = work / 'seeded-tree'
    if root.exists(): shutil.rmtree(root)
    root.mkdir(parents=True)
    for d in ('firmware', 'tests'): shutil.copytree(ROOT / d, root / d, symlinks=True, ignore=shutil.ignore_patterns('__pycache__'))
    (root / 'build').mkdir()
    (root / 'build/host').symlink_to((ROOT / 'build/host').resolve())
    (root / 'assets').symlink_to((ROOT / 'assets').resolve())
    ui = root / 'firmware/src/instrument_ui.c'
    text = ui.read_text()
    anchor = 'static void br_draw_body(void)\n{\n'
    assert text.count(anchor) == 1, 'br_draw_body anchor not found'
    ui.write_text(text.replace(anchor, '#include <string.h>\n#include <stdlib.h>\n' + SEQLOOK_SEED_C + anchor + '    seqlook_seed_hook();\n'))
    return root


def build_poly(binary):
    """FM-1 with polyphony (BR_POLY_UI): the app and the split audio unit in one host program
    (tests/poly_ui_host.h), so the TEMPO screen's POLY switch exists. DSP objects need the same defines."""
    objs = sorted((ROOT / 'build/host/braids').glob('*.o')) + sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    flags = [f for f in FLAGS if not f.startswith(('-DBR_PRODUCT=', '-DBR_CAP_POLY='))]
    source = binary.parent / 'poly-host.c'
    source.write_text('#include "%s"\n' % (ROOT / 'tests/poly_ui_host.h'))
    subprocess.run(['c++', '-x', 'c', *flags, '-DBR_PRODUCT=BR_PRODUCT_ID_FM1', '-DBR_CAP_POLY=1', '-DFM1_DEVELOPMENT_DIAGNOSTICS=0',
                    str(source), 'firmware/src/audio_fixed8_plan_unit.c', '-x', 'none', *map(str, objs), '-o', str(binary)],
                   cwd=ROOT, check=True, stderr=subprocess.DEVNULL)


def build(binary, root=ROOT):
    objs = sorted((root / 'build/host/braids').glob('*.o')) + sorted((root / 'build/host/plaits_reference').glob('*.o'))
    if not objs:
        raise SystemExit('Host DSP objects are missing: run tools/build_braids_host.sh first')
    planner = binary.parent / 'planner.o'
    subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(planner)], cwd=root, check=True)
    subprocess.run(['c++', '-x', 'c', *FLAGS, '-DDINGHY_HOST', 'firmware/src/dinghy.c', '-x', 'none', str(planner),
                    *map(str, objs), '-o', str(binary)], cwd=root, check=True, stderr=subprocess.DEVNULL)


def build_emulator(binary):
    objs = sorted((ROOT / 'build/host/braids').glob('*.o')) + sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    planner = binary.parent / 'planner.o'
    subprocess.run(['c++', '-x', 'c', *FLAGS, '-DDINGHY_DEVICE_SAVE_TEST', 'tools/ui_emulator.c', '-x', 'none', str(planner),
                    *map(str, objs), '-o', str(binary)], cwd=ROOT, check=True, stderr=subprocess.DEVNULL)


def emulate(binary, steps):
    """Drive tools/ui_emulator.c; every command answers with a JSON line and a 240x240 RGB frame."""
    cmds = []
    for s in steps[1:]:
        if s[0] == 'btn': cmds.append('b %d' % BTN[s[1]])
        elif s[0] == 'enc': cmds += ['e %d %d' % (ENC[s[1]], 1 if s[2] > 0 else -1)] * abs(s[2])
        elif s[0] == 'wait': cmds += ['t 250'] * (s[1] // 250) + (['t %d' % (s[1] % 250)] if s[1] % 250 else [])
    out = subprocess.run([str(binary)], input=('\n'.join(cmds) + '\n').encode(), cwd=ROOT, capture_output=True, check=True).stdout
    frame = 240 * 240 * 3
    at = 0
    for _ in range(len(cmds)):               # one frame is sent per command
        at = out.index(b'\n', at) + 1 + frame
    return out[at - frame:at]


def script_for(steps, start=400):
    t, lines = start, []
    for s in steps:
        if s[0] == 'keys':                        # hold keyboard keys (bit mask) from here on
            lines.append('%d keys %d' % (t, s[1])); t += 600
        elif s[0] == 'buttons':                   # hold panel buttons (bit mask) from here on, for chords
            lines.append('%d buttons %d' % (t, s[1])); t += 600
        elif s[0] == 'btn':
            lines.append('%d btn %d' % (t, BTN[s[1]])); t += 700
        elif s[0] == 'wait':                      # let time pass (the playhead moves 125 ms per step at 120 BPM)
            t += s[1]
        else:
            for _ in range(abs(s[2])):
                lines.append('%d enc %d %d' % (t, ENC[s[1]], 1 if s[2] > 0 else -1)); t += 150
            t += 100
    lines.append('%d shot' % (t + 300)); lines.append('%d end' % (t + 500))
    return '\n'.join(lines) + '\n', t + 300


def ppm(path):
    d = path.read_bytes(); i = 0; parts = []
    while len(parts) < 4:
        while d[i:i + 1].isspace(): i += 1
        j = i
        while not d[j:j + 1].isspace(): j += 1
        parts.append(d[i:j]); i = j
    i += 1
    return int(parts[1]), int(parts[2]), d[i:]


def png(path, w, h, rgb, scale):
    row = lambda y: b''.join(rgb[(y * w + x) * 3:(y * w + x) * 3 + 3] * scale for x in range(w))
    raw = b''.join((b'\0' + row(y)) * scale for y in range(h))
    def chunk(tag, data): return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('out'); ap.add_argument('--scale', type=int, default=2); ap.add_argument('--only', default='')
    ap.add_argument('--hd', action='store_true', help=argparse.SUPPRESS)   # deprecated no-op: HD is the only interface
    ap.add_argument('--work', default='', help='name of a private build folder, so parallel runs do not collide')
    ap.add_argument('--seq2x8', choices=('0', '1'), default='', help='BR_SEQ_2X8: 2x8 sequencer layouts (default: the firmware default)')
    ap.add_argument('--target', choices=host_target.TARGETS, default='fm1', help='product (default fm1)')
    ap.add_argument('--poly', action='store_true', help='FM-1 with polyphony (the POLY switch on TEMPO)')
    ap.add_argument('--smk-preview', action='store_true', help='alias for --target smk37 (SMK-37 Pro labels and all modules)')
    ap.add_argument('--fm1-build', type=int, help='show the specified FM-1 build in the Tempo badge')
    a = ap.parse_args()
    if a.smk_preview: a.target = 'smk37'
    smk = a.target == 'smk37'
    if a.seq2x8: FLAGS.append('-DBR_SEQ_2X8=' + a.seq2x8)
    if a.fm1_build is not None:
        FLAGS.extend([f'-DFELUCCA_ID="FM-1_967{a.fm1_build}"', '-DAPP_VER="1.0b-device-test"'])
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    work = ROOT / ('build/render-screens' + ('-smk37' if smk else '') + ('-' + a.work if a.work else '')); work.mkdir(parents=True, exist_ok=True)
    binary = work / 'dinghy'
    build_smk(binary) if smk else build_poly(binary) if a.poly else build(binary)
    emulator = seeded = None
    import os
    for name, steps in (SMK_SCREENS if smk else SCREENS).items():
        if a.only and name not in a.only.split(','): continue
        if steps and steps[0][0] == 'emu':
            if not emulator: emulator = work / 'ui_emulator'; build_emulator(emulator)
            png(out / (name + '.png'), 240, 240, emulate(emulator, steps), a.scale); print('wrote', name); continue
        run, env = binary, None
        if steps and steps[0][0] == 'seed':
            if not seeded: seeded = work / 'dinghy-seeded'; build(seeded, seeded_root(work))
            run, env = seeded, dict(os.environ, SEQLOOK_SEED=steps[0][1]); steps = steps[1:]
        script, at = script_for(steps, 2400 if smk and name != 'boot' else 400)
        (work / 'script.txt').write_text(script)
        subprocess.run([str(run), str(work / name), str(work / 'script.txt')], cwd=ROOT, capture_output=True, check=True, env=env)
        shot = work / ('%s-%d.ppm' % (name, at))
        if not shot.exists(): print('no screenshot for', name); continue
        w, h, rgb = ppm(shot); png(out / (name + '.png'), w, h, rgb, a.scale); print('wrote', name)
    if smk:                                       # SMK-37 Pro only states (SMK_STATES)
        stated = sroot = None
        for name, (steps, state) in SMK_STATES.items():
            if a.only and name not in a.only.split(','): continue
            if not stated: sroot = smk_state_root(work); stated = work / 'dinghy-smk-states'; build_smk(stated, sroot)
            script, at = script_for(steps, 2400)
            (work / 'script.txt').write_text(script)
            env = dict(os.environ, **({'SMKSTATE': state, 'SMKSTATE_T': str(at - 250)} if state else {}))
            subprocess.run([str(stated), str(work / name), str(work / 'script.txt')], cwd=sroot, capture_output=True, check=True, env=env)
            shot = work / ('%s-%d.ppm' % (name, at))
            if not shot.exists(): print('no screenshot for', name); continue
            w, h, rgb = ppm(shot); png(out / (name + '.png'), w, h, rgb, a.scale); print('wrote', name)
    if not a.poly and not a.seq2x8:               # the pattern arranger's views (ARR_STATES)
        arranger_states(out, a.target, a.scale, a.only.split(',') if a.only else [])


if __name__ == '__main__':
    main()
