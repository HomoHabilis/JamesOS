#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unit test of the function-level quick-mode selection (tests/suite_changed.py): the function scanner
on two real firmware files and the selection rules on a synthetic map. Pure Python, no compiles, no
arguments, well under a second."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import suite_changed as sc  # noqa: E402

failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print('FAIL', what)


def lines_of(path):
    return (ROOT / path).read_text(errors='replace').split('\n')


def find(path, name):
    """the Func the scanner reports for `name`, plus the file's lines"""
    funcs = sc.scan_functions((ROOT / path).read_text(errors='replace'))
    hits = [f for f in funcs if f.name == name]
    check(len(hits) == 1, f'{path}: exactly one definition of {name} found (got {len(hits)})')
    return (hits[0] if hits else None), funcs


# ---- the scanner on real files ---------------------------------------------------------------
def scanner():
    for path, names in (('firmware/src/instrument_widgets_params_b.c',
                         ('pgb_quad', 'pgb_ruler', 'br_param_widget_b', 'br_param_animated_b')),
                        ('firmware/src/instrument_ui.c', ('br_route_valid', 'br_draw'))):
        L = lines_of(path)
        for name in names:
            f, funcs = find(path, name)
            if not f:
                continue
            sig = next(i for i, l in enumerate(L, 1) if f'{name}(' in l and l[:1].isalpha() and not l.rstrip().endswith(';'))
            check(f.start <= sig <= f.start + 2, f'{path}:{name} starts at its signature (line {sig}), got {f.start}')
            check(L[f.end - 1].startswith('}'), f'{path}:{name} ends on a "}}" line, got {L[f.end - 1]!r}')
            check(f.end > f.start, f'{path}:{name} has a body ({f.start}-{f.end})')
            check(sc.function_at(funcs, f.start) == f and sc.function_at(funcs, f.end) == f,
                  f'{path}:{name} contains its own first and last line')
            check(sc.function_at(funcs, f.end + 1) != f, f'{path}:{name} does not contain the line after it')
        check(all(a.end < b.start for a, b in zip(funcs, funcs[1:])), f'{path}: function ranges are disjoint, in order')
    # a header with compact one-line bodies (the body closes without a "}" at column 0)
    f, funcs = find('firmware/src/instrument.h', 'br_is_plaits')
    if f:
        check(f.end - f.start <= 2, f'compact body of br_is_plaits is short, got {f.start}-{f.end}')
        nxt = [g for g in funcs if g.start > f.start][:1]
        check(not nxt or nxt[0].start > f.end, 'the function after br_is_plaits starts after it')
    # edge cases
    text = ('#define M(x) \\\n  { x }\n/* int fake(void) { */\nstatic const int t[] = {\n 1,\n};\n'
            'struct s { int a; };\nstatic int f(int a,\n  int b)\n{\n  const char *p = "}";\n  return a + b;\n}\n'
            'int g(void) { return 1; }\nvoid h(void) __attribute__((noinline));\n'
            'void h(void) __attribute__((noinline))\n{\n}\n')
    got = {x.name: (x.start, x.end) for x in sc.scan_functions(text)}
    check(got == {'f': (8, 13), 'g': (14, 14), 'h': (16, 18)}, f'edge cases: {got}')


# ---- selection rules on a synthetic map -------------------------------------------------------
ALL = [f'run_{c}.py' for c in 'abcdefgh']
FUNCS = {'run_a.py': {'f1', 'f2'}, 'run_b.py': {'f2', 'f3'}, 'run_c.py': {'f3'}, 'run_d.py': {'f9'}}
DEPS = {r: ({'firmware/src/x.c', 'firmware/src/t.h', 'firmware/src/new.c'}, set()) for r in ALL}
DEPS['run_h.py'] = ({'firmware/src/other.c'}, set())
SRC = ('static int f1(void)\n{\n    return 1;\n}\n\nstatic int f2(void)\n{\n    return 2;\n}\n\n'
       'static int f3(void)\n{\n    return 3;\n}\n\nstatic const int table[] = { 1, 2 };\n')
OLD = SRC.replace('table[]', 'table[]')


def run_select(changed, lines=None, whole=(), funcs=FUNCS, src=SRC, old=OLD, per_file=None, smoke=(), index=None,
               old_per_file=None, causes=False):
    old_new, old_base, old_smoke = sc._new_text, sc._base_text, sc.SMOKE
    sc._new_text = lambda p: (per_file or {}).get(p, src)
    sc._base_text = lambda b, p: (old_per_file or per_file or {}).get(p, old)
    sc.SMOKE = list(smoke)
    index = index or sc.Index({'firmware/src/x.c': src})
    try:
        r = sc.select_ex(changed, ALL, lines=lines, whole=whole, base='B', deps=DEPS, funcs=funcs, index=index)
        return r if causes else r[:2]
    finally:
        sc._new_text, sc._base_text, sc.SMOKE = old_new, old_base, old_smoke


def chg(new=(), old=()):
    return sc.Change(set(new), set(old))


def selection():
    X = 'firmware/src/x.c'
    unrec = set(ALL) - set(FUNCS)                      # runners absent from the function map always run
    sel, why = run_select([X], {X: chg(new=[3])})                                  # inside f1
    check(sel == {'run_a.py'} | unrec, f'line in f1 -> runners executing f1 (+ unrecorded): {sorted(sel)}')
    check('f1: 1 runner' in why[X], f'why names the function and its runner count: {why[X]!r}')
    sel, _ = run_select([X], {X: chg(new=[7, 12])})                                # inside f2 and f3
    check(sel == {'run_a.py', 'run_b.py', 'run_c.py'} | unrec, f'f2+f3 -> union: {sorted(sel)}')
    sel, _ = run_select([X], {X: chg(new=[16])})                                   # the table: outside functions
    check(sel == {'run_a.py', 'run_b.py', 'run_c.py'} | unrec,
          f'outside any function -> runners executing any function of the file: {sorted(sel)}')
    sel, _ = run_select([X], {X: chg(new=[5])})                                    # a blank line between functions
    check({'run_a.py', 'run_b.py', 'run_c.py'} <= sel, 'a line between functions is "outside"')
    sel, _ = run_select([X], {X: None})                                            # untracked: whole file
    check({'run_a.py', 'run_b.py', 'run_c.py'} <= sel, 'whole-file change -> any function of the file')
    sel, why = run_select([X], whole={X})                                          # --for
    check({'run_a.py', 'run_b.py', 'run_c.py'} <= sel and 'run_h.py' in sel, f'--for: {why[X]!r}')
    # a function that exists but no runner executed: nobody (the unrecorded runners still run)
    sel, why = run_select([X], {X: chg(new=[3])}, funcs={'run_b.py': {'f3'}})
    check(sel == set(ALL) - {'run_b.py'} and 'executed by no recorded runner' in why[X],
          f'unexecuted function selects no recorded runner: {sorted(sel)} {why[X]!r}')
    # a new function (not in the base version): the file-level rule
    sel, why = run_select([X], {X: chg(new=[3])}, old=SRC.replace('f1', 'f0'))
    check('new function' in why[X] and {'run_a.py', 'run_b.py', 'run_c.py'} <= sel, f'new function: {why[X]!r}')
    # a deleted function: the base version's executed set
    sel, _ = run_select([X], {X: chg(old=[2])}, src='static const int table[] = { 1 };\n')
    check('run_a.py' in sel, 'deleted function selects the runners that executed it')
    # a header without functions: the existing rule (dependency map)
    T = 'firmware/src/t.h'
    sel, why = run_select([T], {T: chg(new=[1])}, per_file={T: '#define A 1\nstruct q { int a; };\n'})
    check(sel >= set(ALL) - {'run_h.py'} and 'dependency map' in why[T], f'function-less header: {why[T]!r}')
    # a file nobody executed: the existing rule
    N = 'firmware/src/new.c'
    sel, why = run_select([N], {N: chg(new=[3])}, per_file={N: 'static int zz(void)\n{\n    return 0;\n}\n'},
                         old_per_file={N: SRC})
    check('dependency map' in why[N] or 'every runner' in why[N], f'file nobody executed: {why[N]!r}')
    # without a function map: exactly the existing rule
    sel, why = run_select([X], {X: chg(new=[3])}, funcs={})
    check('dependency map' in why[X] and 'function level' not in why[X], f'no function map: {why[X]!r}')
    # a file outside firmware/src and firmware/hal: the existing rule; documentation: nothing
    sel, why = run_select(['tools/x.py', 'docs/y.md'])
    check('documentation' in why['docs/y.md'], 'documentation selects no runner')
    # the smoke set joins a targeted selection
    sel, why = run_select([X], {X: chg(new=[3])}, funcs={'run_a.py': {'f1'}, 'run_b.py': {'f3'}, 'run_c.py': {'f3'},
                                                          'run_d.py': {'f3'}, 'run_e.py': {'f3'}, 'run_f.py': {'f3'},
                                                          'run_g.py': {'f3'}, 'run_h.py': {'f3'}},
                          smoke=['run_g.py'])
    check(sel == {'run_a.py', 'run_g.py'} and '(smoke)' in why, f'smoke set added: {sorted(sel)}')
    # the diff parser
    d = '+++ b/firmware/src/x.c\n@@ -3,2 +3,3 @@\n@@ -10 +11,0 @@\n'
    c = sc.parse_diff_lines('--- a/firmware/src/x.c\n' + d)['firmware/src/x.c']
    check(c.new == {3, 4, 5} and c.old == {3, 4, 10}, f'diff -U0 hunks: {c}')


def trivial_hunks():
    old = 'int a;\n/* one\n * two\n */\nint f(void)\n{\n    return 1; // x\n}\n'
    def run(new, hunks):
        return sc.drop_trivial_hunks(sc.Change(set(), set(), hunks), old, new)
    # a line in the middle of a block comment, a trailing comment and indentation: all ignored
    c = run(old.replace(' * two', ' * three'), [(3, 1, 3, 1)])
    check(not c.new and not c.old, 'edit inside a /* */ block is ignored')
    c = run(old.replace('// x', '// y'), [(7, 1, 7, 1)])
    check(not c.new and not c.old, 'edit of a trailing // comment is ignored')
    c = run(old.replace('    return 1;', 'return   1;'), [(7, 1, 7, 1)])
    check(not c.new and not c.old, 'whitespace-only edit is ignored')
    c = run(old.replace('return 1;', 'return 2;'), [(7, 1, 7, 1)])
    check(c.new == {7} and c.old == {7}, 'a code edit is kept')
    c = run(old.replace(' * two\n', ''), [(3, 1, 3, 0)])
    check(not c.new and not c.old, 'deleting a comment line is ignored')
    c = run(old.replace('int a;', 'int b;'), [(7, 1, 7, 1), (1, 1, 1, 1)])
    check(c.new == {1} and c.old == {1}, 'only the hunks that differ are kept')
    # select(): a file whose every hunk was dropped selects nothing
    X = 'firmware/src/x.c'
    sel, why = run_select([X], {X: sc.Change(set(), set(), [])})
    check(sel == set() and 'whitespace or comment' in why[X], f'trivial change selects no runner: {sel} {why[X]!r}')


def same_name_and_causes():
    X = 'firmware/src/x.c'
    idx = sc.Index({X: SRC, 'firmware/src/z.c': 'static int f1(void)\n{\n    return 9;\n}\n'})
    sel, why, causes = run_select([X], {X: chg(new=[3])}, index=idx, causes=True)
    check('name defined in 2 files' in why[X] and 'firmware/src/z.c' in why[X], f'same-name note: {why[X]!r}')
    check(('firmware/src/x.c:f1', 1) in causes, f'causes name the function: {causes}')
    check(sc.broad_line(set(ALL), causes) is None and sc.broad_line(set(range(41)), causes).startswith('BROAD: 41 runners; widest cause: '),
          'BROAD line only above 40 runners')


TREE = {'firmware/src/m.h': '#define LIM 5\n#define LIM2 (LIM+1)\nenum { E_A = 1,\n  E_B };\nstatic const int tab[] = {\n 1,\n};\n'
                            '#if LIM\n#endif\n',
        'firmware/src/y.c': 'int u1(void)\n{\n    return LIM2;\n}\nint u2(void)\n{ return tab[0]; }\nint u3(void)\n{ return E_B; }\n'
                            'int u4(void)\n{\n    return 0;\n}\n'}


def outside_identifiers():
    M = 'firmware/src/m.h'
    funcs = {'run_a.py': {'u1'}, 'run_b.py': {'u2'}, 'run_c.py': {'u3'}, 'run_d.py': {'u4'}}
    idx = sc.Index(TREE)
    unrec = set(ALL) - set(funcs)
    for line, want, rule in ((1, 'run_a.py', 'LIM'), (4, 'run_c.py', 'E_B'), (6, 'run_b.py', 'tab')):
        sel, why = run_select([M], {M: chg(new=[line], old=[line])}, funcs=funcs, per_file=TREE, index=idx)
        check(sel == {want} | unrec and 'identifier rule' in why[M] and rule in why[M],
              f'{M}:{line} selects only the runners of the functions mentioning {rule}: {sorted(sel)} {why[M]!r}')
    check(sc.defined_identifiers(TREE[M], 2) == ({'LIM2'}, True), 'macro name extracted')
    check(sc.defined_identifiers(TREE[M], 3) == ({'E_A'}, True), 'enum constant on the opening line extracted')
    # an #if line yields no identifier: the file-level rule (a header without functions -> the dependency map)
    sel, why = run_select([M], {M: chg(new=[8])}, funcs=funcs, per_file=TREE, index=idx)
    check('identifier rule' not in why[M] and 'dependency map' in why[M], f'#if line falls back: {why[M]!r}')
    # an identifier nothing references falls back as well
    t2 = dict(TREE)
    t2[M] = TREE[M] + '#define UNUSED 1\n'
    sel, why = run_select([M], {M: chg(new=[10])}, funcs=funcs, per_file=t2, index=sc.Index(t2))
    check('identifier rule' not in why[M], f'unreferenced identifier falls back: {why[M]!r}')


scanner()
selection()
trivial_hunks()
same_name_and_causes()
outside_identifiers()
if failures:
    print(f'{len(failures)} check(s) failed')
    sys.exit(1)
print('suite targeting: scanner and selection rules passed')
