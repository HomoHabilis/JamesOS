#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Static worst-case stack depth from a device disassembly (build/<cfg>/dinghy.dis), for pi32v2 clang 4 which
has no -fstack-usage.

Usage: python3 tools/stack_depth.py DINGHY.dis ROOT [ROOT...] [--top N] [--path]

Per function: frame = 4 * registers pushed by the `[--sp] = {...}` prologue + every negative `sp += -N`.
Edges: direct `call` (relative targets resolved: target = address + 4 + offset) and tail `goto <other fn>`.
Indirect calls (`call rN`) are counted per function and reported; their callees are NOT followed, so the result
is a lower bound wherever a chain goes through a function pointer. Recursion is cut (reported)."""
import re, sys
from functools import lru_cache

FN = re.compile(r'^([A-Za-z_$][\w.$]*):$')          # (.GJTIS/.GJTIE jump-table labels stay in their function)
INS = re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$')
PUSH = re.compile(r'\[--sp\] = \{([^}]*)\}')
SPADJ = re.compile(r'\bsp \+= (-\d+)\b')
CALLSYM = re.compile(r'\bcall -?\d+ <([\w.$]+) : [0-9a-f]+ >')
CALLREL = re.compile(r'\bcall (-?\d+)\s*$')
CALLREG = re.compile(r'\bcall r\d+')
GOTOSYM = re.compile(r'\bgoto -?\d+ <([\w.$]+) : [0-9a-f]+ >')


def regs(spec):
    n = 0
    for part in spec.split(','):
        part = part.strip()
        m = re.match(r'r(\d+)-r(\d+)$', part)
        if m: n += abs(int(m.group(1)) - int(m.group(2))) + 1
        elif part: n += 1
    return n


def load(path):
    fns, addr2fn, cur = {}, {}, None
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        m = FN.match(line)
        if m:
            cur = m.group(1); fns[cur] = dict(frame=0, calls=set(), indirect=0, start=None); continue
        if not cur: continue
        m = INS.match(line)
        if not m: continue
        a, text = int(m.group(1), 16), m.group(3)
        f = fns[cur]
        if f['start'] is None: f['start'] = a; addr2fn[a] = cur
        p = PUSH.search(text)
        if p: f['frame'] += 4 * regs(p.group(1))
        s = SPADJ.search(text)
        if s: f['frame'] += -int(s.group(1))
        c = CALLSYM.search(text)
        if c: f['calls'].add(c.group(1)); continue
        c = CALLREL.search(text)
        if c: f['calls'].add(('@', a + 4 + int(c.group(1)))); continue
        if CALLREG.search(text): f['indirect'] += 1; continue
        g = GOTOSYM.search(text)
        if g and g.group(1) != cur and '+0x' not in text.split('<', 1)[1].split(':')[0]: f['calls'].add(g.group(1))
    for f in fns.values():
        f['calls'] = {addr2fn.get(c[1], '?%x' % c[1]) if isinstance(c, tuple) else c for c in f['calls']}
    return fns


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 0
    if top: args = [a for a in args if a != str(top)]
    fns = load(args[0])
    cut = set()

    @lru_cache(None)
    def depth(fn, stack=()):
        f = fns.get(fn)
        if not f: return 0, (fn + '(?)',)
        best = (0, ())
        for c in f['calls']:
            if c in stack or c == fn: cut.add((fn, c)); continue
            d = depth(c, stack + (fn,))
            if d[0] > best[0]: best = d
        return f['frame'] + best[0], (fn,) + best[1]

    sys.setrecursionlimit(20000)
    for root in args[1:]:
        if root not in fns: print('%s: not found' % root); continue
        d, path = depth(root)
        ind = [(p, fns[p]['indirect']) for p in path if p in fns and fns[p]['indirect']]
        print('%s: worst static depth %d B' % (root, d))
        print('   ' + ' -> '.join('%s(%d)' % (p, fns[p]['frame'] if p in fns else 0) for p in path))
        if ind: print('   indirect calls on this path (not followed): ' + ', '.join('%s x%d' % x for x in ind))
    if top:
        print('largest frames:')
        for k, f in sorted(fns.items(), key=lambda kv: -kv[1]['frame'])[:top]: print('   %6d %s' % (f['frame'], k))
    if cut: print('recursion cut at %d edges (e.g. %s)' % (len(cut), sorted(cut)[:3]))


if __name__ == '__main__':
    main()
