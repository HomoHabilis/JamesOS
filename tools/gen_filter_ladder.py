#!/usr/bin/env python3
"""Generate firmware/src/instrument_filter_ladder.h, the filter's cutoff tables.

Low-pass (controls 0..62): the six-pole TPT low-pass (four-pole ladder and two post stages). Its resonance-0 -3 dB
point is geometric, F0 = 20 Hz at control 0 to LP_TOP at control 62 (equal
semitones per step), at every output rate. The post stages have twice the ladder cutoff; the numeric solver below places
the combined resonance-0 -3 dB point at the displayed cutoff., stored as G = g/(1+g) in Q30. At 32 kHz
the stage cutoff is capped at STAGE_CAP*fs and the spacing compressed so the
table still has no repeated step.

Ladder resonance: feedback k (Q12) at r = 0, 8, ..., 128, interpolated
linearly between nodes (LADDER_K). k = 0 at r = 0; r = 8 is where the
resonant peak first reaches 0 dB; from there the peak rises evenly in dB to
k = 3.9 at r = 120; 120..127 runs on to 4.2, so self-oscillation (k >= 4)
starts at r = 123.

High-pass (controls 65..127, positions 0..62): two cascaded TPT state-variable stages.
Its -3 dB point at resonance 0 (Butterworth) is geometric from 20 Hz to
HP_TOP (12 kHz; at most HP_G_MAX, the 12 kHz/44.1 kHz coefficient, so the
fixed-point states keep their headroom: 8.7 kHz at 32 kHz). The table holds
g = tan(pi*fc/fs) in Q29. Its damping q = 1/Q comes from 17 nodes (Q15) so
that the peak rises linearly in dB from 0 (r = 0, Butterworth) to +16 dB
(r = 127, Q = 6.3); there is no self-oscillation.

Key tracking moves round((note - 60) * steps per semitone) table steps; the
constants are generated per rate and side. The read-out's Hz comes from the
same geometric law (start 20 Hz, ratio per step in Q24).

Run from the repository root:

    python3 tools/gen_filter_ladder.py          # rewrite the header
    python3 tools/gen_filter_ladder.py --check  # fail if it is stale
"""
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'firmware/src/instrument_filter_ladder.h'
RATES = (('!FM1_NATIVE_OUTPUT_32K && !FM1_NATIVE_OUTPUT_48K', 44100),
         ('FM1_NATIVE_OUTPUT_48K', 48000),
         ('FM1_NATIVE_OUTPUT_32K', 32000))
SUFFIX = {44100: '', 48000: '_48k', 32000: '_32k'}
F0 = 20.0
LP_TOP = 16000.0
STAGE_CAP = 0.42          # highest safe stage cutoff, fraction of fs
def post_ladder_3db():
    # Four ladder poles followed by two poles at twice their cutoff.
    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        if (1 + mid * mid) ** 4 * (1 + (mid / 2) ** 2) ** 2 < 2: lo = mid
        else: hi = mid
    return (lo + hi) / 2
LADDER_3DB = post_ladder_3db()
STEPS = 62


def lp_top(fs):
    """-3 dB at control 62: LP_TOP at 44.1/48 kHz (stage 0.437/0.422*fs); at
    32 kHz what a STAGE_CAP*fs stage gives."""
    if fs >= 44100:
        return LP_TOP
    return math.atan(LADDER_3DB * math.tan(math.pi * STAGE_CAP)) * fs / math.pi


HP_TOP = 12000.0
HP_G_MAX = math.tan(math.pi * HP_TOP / 44100.0)
HP_FACTOR = ((math.sqrt(353.0) - 17.0) / 2.0) ** 0.25
HP_PEAK_DB = 16.0


def hp_top(fs):
    return min(HP_TOP, math.atan(HP_G_MAX / HP_FACTOR) * fs / math.pi)


def hp_hz(fs):
    top = hp_top(fs)
    return [F0 * (top / F0) ** (c / STEPS) for c in range(STEPS + 1)]


def hp_table(fs):
    return [int(round(HP_FACTOR * math.tan(math.pi * f / fs) * (1 << 29))) for f in hp_hz(fs)]


def hp_q_nodes():
    """q = 1/Q at r = 0, 8, ..., 128 for a peak of HP_PEAK_DB*r/127 dB."""
    out = []
    for r in range(0, 129, 8):
        p = 10.0 ** (HP_PEAK_DB * r / 127.0 / 20.0)
        q2 = 0.5 if p <= 1.0 else (p * p + math.sqrt(p ** 4 - p * p)) / 2.0
        out.append(int(round(32768.0 / math.sqrt(q2))))
    return out


def st_per_step(hz):
    return 12.0 * math.log2(hz[1] / hz[0])


def lp_hz(fs):
    top = lp_top(fs)
    return [F0 * (top / F0) ** (c / STEPS) for c in range(STEPS + 1)]


def lp_table(fs):
    vals = []
    for f3 in lp_hz(fs):
        g = math.tan(math.pi * f3 / fs) / LADDER_3DB
        vals.append(int(round(g / (1.0 + g) * (1 << 30))))
    return vals


# Ladder feedback k at r = 0, 8, ..., 120, solved against the firmware ladder's
# measured peak gain (sine at -30 dB FS, cutoff ~1 kHz, its saturator included;
# at k = 0.5 bass compensation the peak first reaches 0 dB at k = 1.06): the
# peak then rises evenly, about 0.16 dB per step, to +18.1 dB at k = 3.9.
LADDER_K = (0.0, 1.061, 1.49, 1.675, 1.932, 2.166, 2.379, 2.574, 2.754,
            2.921, 3.075, 3.221, 3.369, 3.518, 3.693, 3.9)


def ladder_k_nodes():
    """Q12 nodes; the last (r = 128) continues 120..127 to k = 4.2."""
    ks = list(LADDER_K) + [3.9 + 0.3 * 8 / 7]
    return [int(round(k * 4096)) for k in ks]


def table(out, ctype, name, vals, per_line):
    out.append('static const %s %s[%d] = {' % (ctype, name, len(vals)))
    for i in range(0, len(vals), per_line):
        out.append('    ' + ', '.join(str(v) for v in vals[i:i + per_line]) + ',')
    out.append('};')


def render():
    out = ['/* SPDX-License-Identifier: GPL-3.0-only */',
           '/* Generated by tools/gen_filter_ladder.py; do not edit.',
           ' * br_filter_ladder_g: ladder low-pass stage gain G = g/(1+g), Q30, placing',
           ' * the resonance-0 -3 dB point geometrically from %.0f Hz (control 0) to' % F0,
           ' * the top of the rate (control 62): %s. */' %
           ', '.join('%.0f Hz at %g kHz' % (lp_top(fs), fs / 1000) for _, fs in RATES)]
    out.append('#ifndef BR_FILTER_LAW_H')
    out.append('#define BR_FILTER_LAW_H')
    out.append('/* Key tracking: table steps per semitone, Q16. Read-out: Hz at position p is')
    out.append(' * 20 * ratio^p, ratio in Q24. */')
    for cond, fs in RATES:
        lp, hp = lp_hz(fs), hp_hz(fs)
        out.append('#if %s' % cond)
        out.append('#define BR_FILTER_LP_KEY_Q16 %du' % round(65536 / st_per_step(lp)))
        out.append('#define BR_FILTER_HP_KEY_Q16 %du' % round(65536 / st_per_step(hp)))
        out.append('#define BR_FILTER_LP_RATIO_Q24 %du' % round((lp[1] / lp[0]) * (1 << 24)))
        out.append('#define BR_FILTER_HP_RATIO_Q24 %du' % round((hp[1] / hp[0]) * (1 << 24)))
        out.append('#endif')
    out.append('#endif')
    out.append('#if !defined(BR_FILTER_LAW_ONLY) && !defined(BR_FILTER_TABLES_H)')
    out.append('#define BR_FILTER_TABLES_H')
    for cond, fs in RATES:
        out.append('#if %s' % cond)
        table(out, 'int32_t', 'br_filter_ladder_g' + SUFFIX[fs], lp_table(fs), 7)
        out.append('#endif')
    out.append('/* High-pass g = tan(pi*fc/fs), Q29: -3 dB (resonance 0) geometric from 20 Hz')
    out.append(' * (position 0) to %s. */' % ', '.join('%.0f Hz at %g kHz' % (hp_top(fs), fs / 1000) for _, fs in RATES))
    for cond, fs in RATES:
        out.append('#if %s' % cond)
        table(out, 'int32_t', 'br_filter_hp_g' + SUFFIX[fs], hp_table(fs), 7)
        out.append('#endif')
    out.append('/* High-pass damping q = 1/Q, Q15, at resonance 0, 8, ..., 128 (interpolated');
    out.append(' * between): Butterworth at 0, peak +16*r/127 dB. */')
    table(out, 'uint16_t', 'br_filter_hp_q_nodes', hp_q_nodes(), 9)
    out.append('/* Ladder feedback k, Q12, at resonance 0, 8, ..., 128 (interpolated between):')
    out.append(' * the resonant peak rises evenly in dB from r = 8 to r = 120 (k = 3.9, +18 dB at -30 dB FS);')
    out.append(' * k >= 4 (self-oscillation) from r = 123; k(127) = 4.2. */')
    table(out, 'uint16_t', 'br_filter_ladder_k_nodes', ladder_k_nodes(), 9)
    out.append('#endif')
    return '\n'.join(out) + '\n'


def main():
    text = render()
    if '--check' in sys.argv:
        if OUT.read_text() != text:
            print('STALE: run tools/gen_filter_ladder.py')
            return 1
        print('PASS: filter tables are current')
        return 0
    if '--report' in sys.argv:
        for _, fs in RATES:
            hz = lp_hz(fs)
            print('%d: LP top %.0f Hz, %.4f st/step' % (fs, hz[-1], st_per_step(hz)))
            hz = hp_hz(fs)
            print('%d: HP top %.0f Hz, %.4f st/step' % (fs, hz[-1], st_per_step(hz)))
        return 0
    OUT.write_text(text)
    print('wrote', OUT.relative_to(ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
