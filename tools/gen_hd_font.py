#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate firmware/src/james_font_hd.h: anti-aliased glyphs for the HD UI.

    python3 tools/gen_hd_font.py                  # default build: 2 bits/pixel, scales 1 and 2
    python3 tools/gen_hd_font.py --bpp 4          # 4-bit coverage (about +1.9 KB, barely visible)
    python3 tools/gen_hd_font.py --scale3 "0123456789%./-:+ "   # scale 3 too (unused by the UI today)
    python3 tools/gen_hd_font.py --check          # is firmware/src/james_font_hd.h up to date?
    python3 tools/gen_hd_font.py --preview out.png  # also write a quick specimen

The glyphs are rasterised from Chakra Petch SemiBold (SIL Open Font License
1.1, see docs/legal/HD-FONT-NOTICE.md; the font file and its OFL.txt are in
tools/fonts/chakrapetch/) by a small exact-coverage rasteriser in this
file, with no font hinting from the font itself. Instead every glyph is fitted
to the pixel font's metrics (james_font.h) and to the pixel grid:

  * the glyph is never wider than the pixel glyph's ink box,
    james_font_width(ch) * scale, and sits in the same 8 * scale rows (cap
    height 7 * scale, baseline under row 7 * scale - 1, as the pixel font);
  * a glyph wider than its box is squeezed horizontally, never stretched
    (except parentheses, which fill their box); W may bleed one pixel into
    each side gap at scale 2 and up (--bleed; its open diagonals keep the
    narrower gaps from reading as a collision, where M's stems would not);
  * spacing is proportional: the ink starts at the left of the cell and each
    glyph advances by its own whole-pixel ink width plus one gap of `scale`
    pixels (the pixel font's gap), never more than the pixel font's advance
    (james_font_width(ch) + 1) * scale, so no string gets wider;
  * digits stay tabular: 0-9 share one advance (the widest digit plus the
    gap) and each is centred in it, so changing values do not jitter; the
    tabular 1 (with a foot, see below) fills its cell instead of floating in it;
  * a short pair-kerning table (--kern) tightens the worst letter pairs
    (around T, A, V, L, Y, P, F, W) by whole pixels, measured on the
    rasterised glyphs; pairs with a digit are never kerned, nor are the
    symbols of the OCT-/+ key names (kerned, they read as one cramped mark);
  * per-face glyph forms (FACE_FORMS, picked by the font's family name):
    Inter's own alternates (tabular 1 with a foot, G with a spur, open 6
    and 9); for Chakra Petch, which has no such alternates, two synthetic
    marks drawn into the outline: a foot under 1 as wide as 0 (so 1 fills
    the tabular cell and differs from I) and a dot in 0 (so 0 differs from
    O), the forms the pixel font has too;
  * a light auto-hinter snaps the baseline, cap height, x-height, overshoots
    and every detected vertical and horizontal stem (straight or round) to
    whole pixels (every stem near the face's standard stem gets the same
    width, so squeezed and unsqueezed letters weigh the same), then
    interpolates the outline between those knots.  Diagonals and curves keep
    their anti-aliasing; coverage gets a 0.8 exponent so thin diagonals hold
    up on the panel.

Only the standard library, numpy and fontTools are needed.  Nothing is
downloaded; the font is read from tools/fonts/ (--font-dir).  Inter, the
previous face, is still selectable: --font-dir DIR --font1 Inter-SemiBold.otf
--font2 Inter-SemiBold.otf --font3 Inter-SemiBold.otf (an absolute path in
--fontN works too); its alternates follow from its family name.
"""
import argparse
import math
import re
import sys
from pathlib import Path

import numpy as np
from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
FONT_DIR = ROOT / 'tools/fonts'
DEFAULT_FONT = {s: 'chakrapetch/ChakraPetch-SemiBold.ttf' for s in (1, 2, 3)}

# Characters the UI draws at each scale (from a log of every string drawn by
# the host build across all pages, presets and boot screens), plus the few
# symbols the pixel font has a form for.  Characters left out fall back to
# the pixel glyph, drawn hard-edged, so nothing ever disappears.
CHARS = {
    1: " !\"%'()*+,-./0123456789:;<=>?#ABCDEFGHIJKLMNOPQRSTUVWXYZ_d",
    2: " !%()+-./0123456789:?ABCDEFGHIJKLMNOPQRSTUVWXYZ_",
    3: "",
}
# Per-face glyph forms, keyed by typographic family name (name ID 16, else 1).
#   alternates: character -> glyph name in that font (skipped if the font lacks it)
#   synth: synthetic marks added to the outline (see Face.outline):
#     'foot1' a foot under 1 as wide as 0's ink, 'dot0' a dot in the counter of 0
FACE_FORMS = {
    # Inter: a footed tabular 1 (fills its digit cell), a spurred G, open 6 and 9.
    'Inter': {'alternates': {'1': 'one.tf.ss01', 'G': 'G.1', '6': 'six.ss01', '9': 'nine.ss01'}},
    # Chakra Petch has no alternates; 0 and O differ by 0.04 em and 1 is a bare flagged stem.
    'Chakra Petch': {'synth': ('foot1', 'dot0')},
}
# Kerning candidates: the most frequent pairs in the UI's strings (string
# literals of firmware/src) that the measure in kern_pair() finds open, chiefly
# around T, A, V, W, Y, L, P, F and R.  Pairs it finds closed are dropped.
KERN_PAIRS = ('AT AV AW AY AC FA LO LT LY LW OA OT PA RT RU RO RY TA TC TO TS T: '
              'VA WA YA Y: ES CT')
# Characters stretched to fill their box and left unhinted horizontally
# (curves that would otherwise snap into straight brackets).
FILL_BOX = '()'


# ---------------------------------------------------------------- pixel font

def pixel_font_widths():
    """james_font_width() for 0..127, read from firmware/src/james_font.h."""
    src = (ROOT / 'firmware/src/james_font.h').read_text()
    table = re.findall(r'\{(\d+),\{[^}]*\}\}, /\* U\+00([0-9A-F]{2})', src)
    widths = {}
    for w, code in table:
        widths[int(code, 16)] = int(w)
    out = {}
    for ch in range(128):
        if ch == 32:
            out[ch] = 3
        elif 32 <= ch <= 126 and widths.get(ch, 0):
            out[ch] = widths[ch]
        else:
            out[ch] = 5          # '#', '/', '<', '>', '_' forms and the missing box
    return out


# ---------------------------------------------------------------- outlines

class FlattenPen(BasePen):
    """Outline to closed polylines (font units)."""

    def __init__(self, glyphset, steps=24):
        super().__init__(glyphset)
        self.contours, self.cur, self.steps = [], None, steps

    def _moveTo(self, p):
        self.cur = [p]

    def _lineTo(self, p):
        self.cur.append(p)

    def _curveToOne(self, p1, p2, p3):
        p0 = self.cur[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            mt = 1 - t
            self.cur.append((mt ** 3 * p0[0] + 3 * mt * mt * t * p1[0] + 3 * mt * t * t * p2[0] + t ** 3 * p3[0],
                             mt ** 3 * p0[1] + 3 * mt * mt * t * p1[1] + 3 * mt * t * t * p2[1] + t ** 3 * p3[1]))

    def _qCurveToOne(self, p1, p2):
        p0 = self.cur[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            mt = 1 - t
            self.cur.append((mt * mt * p0[0] + 2 * mt * t * p1[0] + t * t * p2[0],
                             mt * mt * p0[1] + 2 * mt * t * p1[1] + t * t * p2[1]))

    def _closePath(self):
        if self.cur and len(self.cur) > 2:
            if self.cur[0] == self.cur[-1]:
                self.cur.pop()
            self.contours.append(self.cur)
        self.cur = None

    _endPath = _closePath


def winding(contours, x, y):
    w = 0
    for c in contours:
        n = len(c)
        for i in range(n):
            x0, y0 = c[i]
            x1, y1 = c[(i + 1) % n]
            if (y0 <= y < y1) or (y1 <= y < y0):
                xi = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
                if xi > x:
                    w += 1 if y1 > y0 else -1
    return w


def edge_features(contours, axis, min_len, tol):
    """Edges perpendicular to `axis` (axis 0: vertical edges, positions in x).

    Returns a list of (pos, lo, hi, side): an edge at coordinate `pos`
    spanning lo..hi along the other axis; side is +1 when ink lies on the
    positive side (a left/bottom edge), -1 when on the negative side.
    Straight segments and round extrema both count."""
    o = 1 - axis
    feats = []
    for c in contours:
        n = len(c)
        # straight runs
        i = 0
        while i < n:
            a, b = c[i], c[(i + 1) % n]
            if abs(b[axis] - a[axis]) <= 0.08 * abs(b[o] - a[o]) and abs(b[o] - a[o]) >= min_len:
                feats.append(((a[axis] + b[axis]) / 2, min(a[o], b[o]), max(a[o], b[o])))
            i += 1
        # round extrema: local extremum of the coordinate along the contour
        for i in range(n):
            v = c[i][axis]
            prev = [c[(i - k) % n][axis] for k in range(1, 4)]
            nxt = [c[(i + k) % n][axis] for k in range(1, 4)]
            for sgn in (1, -1):
                if all(sgn * (v - p) >= 0 for p in prev + nxt) and any(sgn * (v - p) > 0 for p in prev + nxt):
                    # extent of the flat-ish region around the extremum
                    span = [c[i][o]]
                    for k in range(1, n):
                        q = c[(i + k) % n]
                        if abs(q[axis] - v) > tol: break
                        span.append(q[o])
                    for k in range(1, n):
                        q = c[(i - k) % n]
                        if abs(q[axis] - v) > tol: break
                        span.append(q[o])
                    feats.append((v, min(span), max(span)))
    out = []
    for pos, lo, hi in feats:
        mid = (lo + hi) / 2
        d = 6.0
        if axis == 0:
            inner_pos = winding(contours, pos + d, mid) != 0
            inner_neg = winding(contours, pos - d, mid) != 0
        else:
            inner_pos = winding(contours, mid, pos + d) != 0
            inner_neg = winding(contours, mid, pos - d) != 0
        if inner_pos == inner_neg:
            continue
        out.append((pos, lo, hi, 1 if inner_pos else -1))
    # merge duplicates
    out.sort()
    merged = []
    for f in out:
        if merged and abs(merged[-1][0] - f[0]) < 2 and merged[-1][3] == f[3] and \
                f[1] <= merged[-1][2] + 2 and f[2] >= merged[-1][1] - 2:
            p = merged[-1]
            merged[-1] = (p[0], min(p[1], f[1]), max(p[2], f[2]), p[3])
        else:
            merged.append(f)
    return merged


def find_stems(feats, max_w, min_overlap):
    """Pair a +1 edge with the nearest following -1 edge that overlaps it."""
    stems = []
    for a in feats:
        if a[3] != 1: continue
        best = None
        for b in feats:
            if b[3] != -1 or b[0] <= a[0] or b[0] - a[0] > max_w: continue
            ov = min(a[2], b[2]) - max(a[1], b[1])
            if ov < min_overlap: continue
            if best is None or b[0] < best[0]: best = b
        if best is not None:
            ov = min(a[2], best[2]) - max(a[1], best[1])
            stems.append((a[0], best[0], ov))
    return stems


class Map1D:
    """Monotone piecewise-linear map from font units to pixels."""

    def __init__(self, slope, offset):
        self.slope, self.offset, self.knots = slope, offset, {}

    def base(self, u):
        return self.slope * u + self.offset

    def __call__(self, u):
        if not self.knots:
            return self.base(u)
        ks = sorted(self.knots.items())
        if u <= ks[0][0]:
            return ks[0][1] + (u - ks[0][0]) * self.slope
        if u >= ks[-1][0]:
            return ks[-1][1] + (u - ks[-1][0]) * self.slope
        for (u0, p0), (u1, p1) in zip(ks, ks[1:]):
            if u0 <= u <= u1:
                return p0 if u1 == u0 else p0 + (p1 - p0) * (u - u0) / (u1 - u0)
        raise AssertionError

    def consistent(self, u, p):
        for ku, kp in self.knots.items():
            if abs(ku - u) < 1e-6:
                if abs(kp - p) > 1e-6: return False
            elif (ku < u and kp > p) or (ku > u and kp < p):
                return False
        return True

    def add(self, u, p):
        self.knots[u] = p


def fit_stems(m, stems, px_scale, min_w, round_mode, std=None):
    """Snap stems (u0 < u1) to whole pixels, longest first.  Stems close to
    the face's standard stem `std` = (units, px) all get the same width, so
    weight stays even whether or not a glyph was squeezed."""
    for u0, u1, _ in sorted(stems, key=lambda s: -s[2]):
        if std and 0.7 * std[0] <= u1 - u0 <= 1.4 * std[0]:
            w = std[1]
        else:
            w = max(min_w, int(math.floor((u1 - u0) * px_scale + round_mode)))
        if u0 in m.knots and u1 in m.knots:
            continue
        if u0 in m.knots:
            p0 = m.knots[u0]; p1 = p0 + w
        elif u1 in m.knots:
            p1 = m.knots[u1]; p0 = p1 - w
        else:
            c = m((u0 + u1) / 2)
            p0 = int(math.floor(c - w / 2 + 0.5)); p1 = p0 + w
        if m.consistent(u0, p0) and m.consistent(u1, p1):
            m.add(u0, p0); m.add(u1, p1)


# ---------------------------------------------------------------- raster

def rasterise(contours, fx, fy, x0, x1, y0, y1, ss=16):
    """Exact horizontal coverage, `ss` sub-scanlines per pixel row.
    Returns coverage[y1-y0][x1-x0] for pixel columns x0..x1-1, rows y0..y1-1."""
    polys = [[(fx(x), fy(y)) for x, y in c] for c in contours]
    edges = []
    for c in polys:
        n = len(c)
        for i in range(n):
            a, b = c[i], c[(i + 1) % n]
            if a[1] != b[1]:
                edges.append((a[0], a[1], b[0], b[1]))
    cov = np.zeros((y1 - y0, x1 - x0))
    for row in range(y0, y1):
        for j in range(ss):
            yc = row + (j + 0.5) / ss
            xs = []
            for ax, ay, bx, by in edges:
                if (ay <= yc < by) or (by <= yc < ay):
                    xs.append((ax + (yc - ay) * (bx - ax) / (by - ay), 1 if by > ay else -1))
            xs.sort()
            w = 0
            for k in range(len(xs) - 1):
                w += xs[k][1]
                if w == 0: continue
                xa, xb = xs[k][0], xs[k + 1][0]
                if xb <= xa: continue
                for col in range(int(math.floor(xa)), int(math.ceil(xb))):
                    if x0 <= col < x1:
                        cov[row - y0, col - x0] += (min(xb, col + 1) - max(xa, col)) / ss
    return np.clip(cov, 0, 1)


# ---------------------------------------------------------------- glyph fit

class Face:
    def __init__(self, path, extra_alternates=None):
        self.font = TTFont(path)
        self.gs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        os2 = self.font['OS/2']
        self.upm = self.font['head'].unitsPerEm
        self.cap = os2.sCapHeight
        self.xh = os2.sxHeight
        self.name = self.font['name']
        self.family = self.name_record(16) or self.name_record(1)
        forms = FACE_FORMS.get(self.family, {})
        self.alternates = dict(forms.get('alternates', {}))
        self.alternates.update(extra_alternates or {})
        self.synth = tuple(forms.get('synth', ()))
        self.std_v = self.std_h = None
        # standard stems: vertical from 'H' and 'I', horizontal from 'E' and 'H'
        def stems(chars, axis):
            out = []
            for c in chars:
                cont = self.outline(c)
                f = edge_features(cont, axis, 0.12 * self.cap, 0.02 * self.cap)
                out += [b - a for a, b, _ in find_stems(f, 0.34 * self.cap, 0.05 * self.cap)]
            return sorted(out)[len(out) // 2]
        self.std_v = stems('HIL', 0)
        self.std_h = stems('EFH', 1)

    def outline(self, ch):
        gname = self.alternates.get(ch, self.cmap.get(ord(ch)))
        if gname not in self.gs:
            gname = self.cmap.get(ord(ch))
        pen = FlattenPen(self.gs)
        self.gs[gname].draw(pen)
        contours = pen.contours
        if self.std_v is not None:              # synthetic marks need the standard stems
            if ch == '1' and 'foot1' in self.synth:
                contours = contours + [self.foot_one(contours)]
            elif ch == '0' and 'dot0' in self.synth:
                contours = contours + [self.dot_zero(contours)]
        return contours

    def ink_x(self, ch):
        pen = FlattenPen(self.gs)
        self.gs[self.cmap[ord(ch)]].draw(pen)
        xs = [p[0] for c in pen.contours for p in c]
        return min(xs), max(xs)

    def foot_one(self, contours):
        """A baseline foot under 1, centred on its stem, as wide as 0's ink and
        one standard horizontal stem thick (clockwise, merged by nonzero fill)."""
        f = edge_features(contours, 0, 0.12 * self.cap, 0.02 * self.cap)
        st = max(find_stems(f, 0.34 * self.cap, 0.08 * self.cap), key=lambda t: t[2])
        mid = (st[0] + st[1]) / 2
        z0, z1 = self.ink_x('0')
        x0, x1 = mid - (z1 - z0) / 2, mid + (z1 - z0) / 2
        h = self.std_h
        return [(x0, 0), (x0, h), (x1, h), (x1, 0)]

    def dot_zero(self, contours):
        """A dot in the middle of 0's counter, one standard vertical stem wide
        and one horizontal stem tall (clockwise, inside the counter)."""
        z0, z1 = self.ink_x('0')
        cx, cy = (z0 + z1) / 2, self.cap / 2
        w, h = self.std_v / 2, self.std_h / 2
        return [(cx - w, cy - h), (cx - w, cy + h), (cx + w, cy + h), (cx + w, cy - h)]

    def name_record(self, i):
        r = self.name.getName(i, 3, 1, 0x409) or self.name.getName(i, 1, 0, 0)
        return str(r) if r else ''


def fit_glyph(face, ch, scale, box_w, opts, cell=None):
    """Return coverage array, its (x, y) origin in the cell and the ink width.

    The ink starts at the left of the cell (a glyph that bleeds wider than its
    box starts half the bleed to the left); with `cell` (tabular digits) it is
    centred in a cell that many pixels wide."""
    contours = face.outline(ch)
    if not contours:
        return None
    pts = [p for c in contours for p in c]
    xmin = min(p[0] for p in pts); xmax = max(p[0] for p in pts)
    ymin = min(p[1] for p in pts); ymax = max(p[1] for p in pts)
    cap_px = 7 * scale
    k = cap_px / face.cap                       # units -> px, vertical
    ovs = 0.03 * face.cap                       # overshoot zone

    # ---- vertical (y down in pixels: py = cap_px - f(u))
    my = Map1D(k, 0.0)                          # maps units to height above baseline in px
    xh_px = round(face.xh * k)
    for zone_u, zone_p in ((0, 0), (face.cap, cap_px), (face.xh, xh_px)):
        my.add(zone_u, zone_p)
    hfe = edge_features(contours, 1, 0.12 * face.cap, 0.02 * face.cap)
    # overshoots and near-zone edges snap onto the zone
    for pos, lo, hi, side in hfe:
        for zone_u, zone_p in ((0, 0), (face.cap, cap_px), (face.xh, xh_px)):
            if abs(pos - zone_u) <= ovs and pos != zone_u:
                if my.consistent(pos, zone_p): my.add(pos, zone_p)
    hst = find_stems(hfe, 0.32 * face.cap, 0.05 * face.cap)
    fit_stems(my, hst, k, 1, opts.stem_round,
              (face.std_h, max(1, int(face.std_h * k * opts.hstem_scale + 0.5))))
    # keep the glyph inside its 8 * scale rows
    if my(ymin) < -scale + 0.0:
        my.add(ymin, -scale)
    if my(ymax) > cap_px:
        my.add(ymax, cap_px)

    # ---- horizontal
    nat = (xmax - xmin) * k
    extra = int(opts.bleed.get(ch, 0.0) * scale)
    allow = box_w + extra - (extra & 1)         # even, so both sides stay on whole pixels
    if ch in FILL_BOX:
        tw = box_w
    elif nat <= allow:
        tw = max(1, round(nat))                 # natural width, whole pixels
    else:
        tw = allow                              # squeezed into the box
    sx = tw / (xmax - xmin)
    if cell is not None:
        left = math.floor((cell - tw) / 2 + 0.5)
    else:
        left = min(0, (box_w - tw) // 2)        # whole pixels (bleed is even)
    mx = Map1D(sx, left - xmin * sx)
    mx.add(xmin, left)
    mx.add(xmax, left + tw)
    vfe = edge_features(contours, 0, 0.12 * face.cap, 0.02 * face.cap)
    vst = find_stems(vfe, 0.34 * face.cap, 0.08 * face.cap)
    if ch not in FILL_BOX:
        fit_stems(mx, vst, sx, 1, opts.stem_round,
                  (face.std_v, max(1, int(face.std_v * k * opts.vstem_scale + 0.5))))

    fx = mx
    fy = lambda u: cap_px - my(u)
    gx0 = int(math.floor(fx(xmin))) - 1
    gx1 = int(math.ceil(fx(xmax))) + 1
    gy0 = int(math.floor(fy(ymax))) - 1
    gy1 = int(math.ceil(fy(ymin))) + 1
    cov = rasterise(contours, fx, fy, gx0, gx1, gy0, gy1)
    return cov, gx0, gy0, left, tw


def quantise(cov, bpp, gamma):
    top = (1 << bpp) - 1
    c = np.power(cov, gamma)
    q = np.floor(c * top + 0.5).astype(int)
    return q


def trim(q, ox, oy, rows):
    """Trim to the nonzero box and clip to rows 0..rows-1."""
    for r in range(q.shape[0]):
        y = oy + r
        if y < 0 or y >= rows:
            q[r, :] = 0
    nz = np.argwhere(q > 0)
    if len(nz) == 0:
        return None
    r0, c0 = nz.min(axis=0); r1, c1 = nz.max(axis=0)
    return q[r0:r1 + 1, c0:c1 + 1], ox + c0, oy + r0


# ---------------------------------------------------------------- output

def pack(q, bpp):
    vals = q.flatten().tolist()
    per = 8 // bpp
    out = []
    for i in range(0, len(vals), per):
        b = 0
        for j, v in enumerate(vals[i:i + per]):
            b |= v << (j * bpp)                 # first pixel in the low bits
        out.append(b)
    return out


DIGITS = '0123456789'


def build(opts):
    """Glyphs {(scale, ch): (q, x, y) or None} and advances {(scale, ch): px}."""
    widths = pixel_font_widths()
    faces = {}
    glyphs = {}
    adv = {}
    for s in (1, 2, 3):
        chars = opts.chars[s]
        if not chars: continue
        if opts.font[s] not in faces:
            faces[opts.font[s]] = Face(Path(opts.font_dir) / opts.font[s], opts.alternates)
        face = faces[opts.font[s]]
        digits = [c for c in DIGITS if c in chars]
        cell = max(fit_glyph(face, c, s, widths[ord(c)] * s, opts)[4] for c in digits) if digits else None
        for ch in chars:
            px_adv = (widths[ord(ch)] + 1) * s
            adv[(s, ch)] = px_adv
            if ch == ' ':
                glyphs[(s, ch)] = None
                continue
            r = fit_glyph(face, ch, s, widths[ord(ch)] * s, opts, cell if ch in DIGITS else None)
            if r is None:
                glyphs[(s, ch)] = None; continue
            cov, ox, oy, left, tw = r
            adv[(s, ch)] = min(px_adv, (cell if ch in DIGITS else left + tw) + s)
            glyphs[(s, ch)] = trim(quantise(cov, opts.bpp, opts.gamma), ox, oy, 8 * s)
    return glyphs, faces, widths, adv


def profiles(g, s):
    """Per cap row: leftmost and rightmost ink column in the cell (None: no ink)."""
    lo, hi = [None] * (7 * s), [None] * (7 * s)
    if g is None: return lo, hi
    q, ox, oy = g
    for r in range(q.shape[0]):
        y = oy + r
        if not 0 <= y < 7 * s: continue
        cols = [c for c in range(q.shape[1]) if q[r, c]]
        if cols:
            lo[y], hi[y] = ox + cols[0], ox + cols[-1]
    return lo, hi


def kern_pair(glyphs, adv, s, a, b, opts):
    """Pixels to tighten the pair a, b at scale s (0 when it should not be kerned).

    Row whitespace between the two inks (each side's open space capped at
    `cap`), averaged over the cap height, against the plain gap of `s` pixels
    that two straight stems get.  The pair tightens by `opts.kern_strength`
    of the excess, but every row where both glyphs have ink keeps at least
    `s` pixels between them, so letters never touch."""
    if (s, a) not in adv or (s, b) not in adv or glyphs.get((s, a)) is None or glyphs.get((s, b)) is None:
        return 0
    cap = 3 * s
    _, ra = profiles(glyphs[(s, a)], s)
    lb, _ = profiles(glyphs[(s, b)], s)
    tot, both = 0.0, []
    for y in range(7 * s):
        right = cap if ra[y] is None else min(cap, adv[(s, a)] - 1 - ra[y])
        left = cap if lb[y] is None else min(cap, lb[y])
        tot += right + left
        if ra[y] is not None and lb[y] is not None:
            both.append(adv[(s, a)] - 1 - ra[y] + lb[y])
    excess = tot / (7 * s) - s
    k = int(math.floor(excess * opts.kern_strength + 0.5))
    if both: k = min(k, min(both) - s)
    return max(0, min(k, 15))


def build_kerning(opts, glyphs, adv):
    """[(a, b, k1, k2)] for the most frequent UI pairs that need it."""
    if not opts.kern: return []
    pairs = []
    for item in opts.kern.split():
        a, b = item[0], item[1]
        if a in DIGITS or b in DIGITS: continue
        k = [kern_pair(glyphs, adv, s, a, b, opts) if opts.chars[s] else 0 for s in (1, 2)]
        if any(k): pairs.append((a, b, k[0], k[1]))
    return sorted(pairs)


def emit(opts, glyphs, faces, widths, adv, kern):
    bpp = opts.bpp
    lines = []
    face = next(iter(faces.values()))
    lines.append('/* SPDX-License-Identifier: OFL-1.1 */')
    lines.append('/* GENERATED by tools/gen_hd_font.py - do not edit.')
    lines.append(' * Anti-aliased glyph coverage for the HD UI, rasterised from')
    for f in sorted(set(opts.font[s] for s in (1, 2, 3) if opts.chars[s])):
        fc = faces[f]
        lines.append(' *   %s, %s' % (fc.name_record(4), fc.name_record(5)))
    lines.append(' *   %s.' % face.name_record(0))
    lines.append(' *   Licensed under the SIL Open Font License 1.1; see docs/legal/HD-FONT-NOTICE.md.')
    lines.append(' * Options: %s */' % opts.cmdline)
    lines.append('#pragma once')
    lines.append('#include <stdint.h>')
    lines.append('#define JAMES_FONT_HD_BPP %du' % bpp)
    lines.append('/* One glyph: coverage rows, top to bottom, %d bits a pixel, first pixel in' % bpp)
    lines.append(' * the low bits, packed continuously; x/y are the box origin in the cell.')
    lines.append(' * james_hd_adv_sN[i] is glyph i\'s advance in pixels (ink width plus a gap of')
    lines.append(' * N pixels, never more than the pixel font\'s (width + 1) * N; digits tabular). */')
    lines.append('typedef struct { uint16_t off; int8_t x; uint8_t y, w, h; } james_hd_glyph_t;')
    stats = {}
    for s in (1, 2, 3):
        chars = opts.chars[s]
        if not chars:
            lines.append('#define JAMES_FONT_HD_HAS_S%d 0' % s)
            continue
        lines.append('#define JAMES_FONT_HD_HAS_S%d 1' % s)
        data, hdr, index = [], [], [0] * 95
        order = sorted(set(chars), key=ord)
        for ch in order:
            g = glyphs[(s, ch)]
            if g is None:
                hdr.append((len(data), 0, 0, 0, 0, ch))
            else:
                q, ox, oy = g
                assert -2 <= ox < 100 and 0 <= oy and q.shape[1] < 256
                hdr.append((len(data), ox, oy, q.shape[1], q.shape[0], ch))
                data += pack(q, bpp)
            index[ord(ch) - 32] = len(hdr)
        assert len(data) < 65536 and len(hdr) < 255
        lines.append('static const uint8_t james_hd_index_s%d[95] = {' % s)
        for i in range(0, 95, 19):
            lines.append('    ' + ','.join('%d' % v for v in index[i:i + 19]) + ',')
        lines.append('};')
        lines.append('static const james_hd_glyph_t james_hd_glyph_s%d[%d] = {' % (s, len(hdr)))
        for off, ox, oy, w, h, ch in hdr:
            lines.append('    {%d,%d,%d,%d,%d}, /* %s */' % (off, ox, oy, w, h, repr(ch)[1:-1].replace('*/', '* /')))
        lines.append('};')
        lines.append('static const uint8_t james_hd_adv_s%d[%d] = {' % (s, len(hdr)))
        for i in range(0, len(hdr), 24):
            lines.append('    ' + ','.join('%d' % adv[(s, h[5])] for h in hdr[i:i + 24]) + ',')
        lines.append('};')
        lines.append('static const uint8_t james_hd_bits_s%d[%d] = {' % (s, max(1, len(data))))
        for i in range(0, len(data), 24):
            lines.append('    ' + ','.join('0x%02X' % v for v in data[i:i + 24]) + ',')
        lines.append('};')
        stats[s] = (len(order), 95, len(hdr) * 7, len(data))
    lines.append('/* Pair kerning: tighten the pair {left, right} by (k & 15) pixels at scale 1')
    lines.append(' * and (k >> 4) at scale 2 (never at scale 3). Sorted; no pair has a digit. */')
    lines.append('#define JAMES_FONT_HD_KERN %du' % len(kern))
    lines.append('static const uint8_t james_hd_kern[%d][3] = {' % max(1, len(kern)))
    for i in range(0, len(kern), 8):
        lines.append('    ' + ','.join("{'%s','%s',0x%02X}" % (a.replace("'", "\\'"), b.replace("'", "\\'"), k1 | k2 << 4)
                                      for a, b, k1, k2 in kern[i:i + 8]) + ',')
    if not kern: lines.append('    {0, 0, 0},')
    lines.append('};')
    total = 0
    lines.append('/* Flash cost (bytes): */')
    for s, (n, ib, hb, db) in stats.items():
        lines.append('/*   scale %d: %d glyphs, index %d + headers and advances %d + bitmaps %d = %d */' % (s, n, ib, hb, db, ib + hb + db))
        total += ib + hb + db
    lines.append('/*   kerning: %d pairs = %d */' % (len(kern), 3 * len(kern)))
    total += 3 * len(kern)
    lines.append('/*   total %d */' % total)
    return '\n'.join(lines) + '\n', stats, total


# ---------------------------------------------------------------- preview

def preview(opts, glyphs, adv, kern, path, strings):
    from PIL import Image
    bpp = opts.bpp
    top = (1 << bpp) - 1
    rows = []
    for s in (1, 2, 3):
        if not opts.chars[s]: continue
        for text in strings:
            kd = {(a, b): (k1, k2)[s - 1] if s < 3 else 0 for a, b, k1, k2 in kern}
            w = sum(adv.get((s, c), 6 * s) for c in text) + 4
            img = np.zeros((8 * s + 4, max(w, 8)))
            x = 2
            for i, c in enumerate(text):
                g = glyphs.get((s, c))
                if g is not None:
                    q, ox, oy = g
                    for r in range(q.shape[0]):
                        for cc in range(q.shape[1]):
                            X, Y = x + ox + cc, 2 + oy + r
                            if 0 <= X < img.shape[1] and 0 <= Y < img.shape[0]:
                                img[Y, X] = max(img[Y, X], q[r, cc] / top)
                x += adv.get((s, c), 6 * s) - kd.get((c, text[i + 1:i + 2]), 0)
            rows.append(img)
    W = max(r.shape[1] for r in rows)
    H = sum(r.shape[0] for r in rows)
    out = np.zeros((H, W))
    y = 0
    for r in rows:
        out[y:y + r.shape[0], :r.shape[1]] = r; y += r.shape[0]
    im = Image.fromarray((out * 255).astype(np.uint8)).resize((W * opts.zoom, H * opts.zoom), Image.NEAREST)
    im.save(path)


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--out', default=str(ROOT / 'firmware/src/james_font_hd.h'))
    ap.add_argument('--no-write', action='store_true', help='do not write the header')
    ap.add_argument('--check', action='store_true', help='only check that --out matches what would be generated')
    ap.add_argument('--bpp', type=int, choices=(2, 4), default=2)
    ap.add_argument('--font-dir', default=str(FONT_DIR), help='directory the --fontN names are in (default tools/fonts)')
    ap.add_argument('--font1', default=DEFAULT_FONT[1], help='face file for scale 1 (in --font-dir, or absolute)')
    ap.add_argument('--font2', default=DEFAULT_FONT[2])
    ap.add_argument('--font3', default=DEFAULT_FONT[3])
    ap.add_argument('--chars1', default=CHARS[1])
    ap.add_argument('--chars2', default=CHARS[2])
    ap.add_argument('--scale3', default=CHARS[3], help='characters to generate at scale 3 (default none: the UI never uses it)')
    ap.add_argument('--gamma', type=float, default=0.8, help='coverage exponent (<1 is bolder)')
    ap.add_argument('--stem-round', type=float, default=0.5, help='0.5 rounds stems to nearest; higher favours thicker')
    ap.add_argument('--vstem-scale', type=float, default=0.85,
                    help='standard vertical stem in px = stem * cap scale * this (squeezed faces are ~0.85)')
    ap.add_argument('--hstem-scale', type=float, default=1.0)
    ap.add_argument('--bleed', dest='bleed_chars', default='W=1',
                    help='extra ink width per character, in pixel-font pixels, split evenly into the gaps')
    ap.add_argument('--kern', default=KERN_PAIRS, help='pairs to consider for kerning, space separated ("" for none)')
    ap.add_argument('--kern-strength', type=float, default=0.5, help='share of the excess space a kerned pair loses')
    ap.add_argument('--alt', default='', help='extra alternates on top of the face\'s own (FACE_FORMS), e.g. 6=six.ss01')
    ap.add_argument('--preview', default='')
    ap.add_argument('--zoom', type=int, default=4)
    o = ap.parse_args(argv)
    o.alternates = {}
    for item in filter(None, o.alt.split(',')):
        k, v = item.split('=', 1)
        o.alternates[k] = v
    o.font = {1: o.font1, 2: o.font2, 3: o.font3}
    o.chars = {1: o.chars1, 2: o.chars2, 3: o.scale3}
    o.bleed = {}
    for item in filter(None, o.bleed_chars.split(',')):
        k, v = item.split('=', 1)
        o.bleed[k] = float(v)
    o.cmdline = ' '.join(['--bpp', str(o.bpp)] + (['--scale3', o.scale3] if o.scale3 else []) +
                         ([] if o.gamma == 0.8 else ['--gamma', str(o.gamma)]) +
                         ([] if not o.alt else ['--alt', o.alt]) +
                         ([] if o.kern == KERN_PAIRS else ['--kern', repr(o.kern)]) +
                         ([] if o.kern_strength == 0.5 else ['--kern-strength', str(o.kern_strength)]) + ([] if o.bleed_chars == 'W=1' else ['--bleed', o.bleed_chars]))
    return o


def main(argv=None):
    o = parse_args(argv)
    glyphs, faces, widths, adv = build(o)
    kern = build_kerning(o, glyphs, adv)
    text, stats, total = emit(o, glyphs, faces, widths, adv, kern)
    if o.check:
        same = Path(o.out).exists() and Path(o.out).read_text() == text
        print('james_font_hd.h is %s' % ('up to date' if same else 'OUT OF DATE'))
        return 0 if same else 1
    if not o.no_write:
        Path(o.out).write_text(text)
    for s, (n, ib, hb, db) in stats.items():
        print('scale %d: %3d glyphs  index %3d  headers %4d  bitmaps %5d  = %5d bytes' % (s, n, ib, hb, db, ib + hb + db))
    print('kerning: %d pairs' % len(kern))
    print('total %d bytes (%d bpp)' % (total, o.bpp))
    if o.preview:
        preview(o, glyphs, adv, kern, o.preview,
                ['ANALOG VCF', 'KNOB1 TIMBRE', 'HARMONICS 60%', '743MS SEQUENCER', '01 REESE 1/16',
                 'OCT-/+ STEP BANK', 'LOAD PRESET', '0123456789', 'ABCDEFGHIJKLM', 'NOPQRSTUVWXYZ', '-30 dB ?!:.()'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
