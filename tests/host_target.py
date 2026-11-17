# SPDX-License-Identifier: GPL-3.0-only
"""Host test configuration: product (FM-1, the default, or SMK-37 Pro) and polyphony.

Host builds do not force-include firmware/product.h; they select the configuration with the
same compiler defines as tools/build.py:

    fm1 + poly     -DBR_CAP_POLY=1 (tools/build.py --target fm1, the device default; product.h
                   then also selects BR_POLY_SUPPORT and the size-constrained mode)
    fm1, no poly   nothing (product.h's FM-1 default; = tools/build.py --target fm1 --no-polyphony)
    smk37 + poly   -DBR_PRODUCT=BR_PRODUCT_ID_SMK37 (product.h's SMK default; = tools/build.py
                   --target smk37)
    smk37, no poly -DBR_PRODUCT=BR_PRODUCT_ID_SMK37 -DBR_CAP_POLY=0 (--target smk37 --no-polyphony)

The interface is always HD: firmware/src/gfx_hd.h defines BR_UI_HD 1 (the standard pixel
interface was removed), so no interface define is passed.

The configuration comes from the environment: BR_HOST_TARGET (default fm1) and BR_HOST_POLY
('1' on, '0' or unset off). Most runners keep their own FLAGS and call `cc`/`c++` by name, so
`activate()`/`environment()` also put tests/host_cc/ first on PATH: a small wrapper that adds
$BR_HOST_CFLAGS to every compiler call (DSP objects, planner, test programs) and then runs the
real compiler. tests/run_suite.py and run_bank.py --target use it; nothing changes for FM-1
without polyphony, where the wrapper is not used at all.

Suite configurations (tests/run_suite.py --config NAME) are named in CONFIGS.
BR_HOST_EXTRA_DEFINES optionally supplies additional space-separated -D flags;
these survive nested environment() calls so candidate-specific tests can run
with the same feature switches as the firmware under review.

Optimisation class (tests/run_suite.py OPT_CLASS): the suite compiles a "logic" runner (UI,
screens, navigation, save, MIDI routing, metadata: nothing that renders or measures audio) at
-O0 and every other ("audio") runner as the runner itself says (-O2). `with_class()` does it for
every compiler call of a runner at once: it puts the wrapper on PATH with BR_HOST_CFLAGS_LAST,
flags the wrapper appends after the runner's own (the last -O on a clang command line wins), so
no runner states its level twice. --sanitize adds -fsanitize=address,undefined there too.
"""
import os
import re
from collections import namedtuple
from pathlib import Path

TARGETS = ('fm1', 'smk37')
ENV_TARGET, ENV_POLY, ENV_CFLAGS = 'BR_HOST_TARGET', 'BR_HOST_POLY', 'BR_HOST_CFLAGS'
ENV_CFLAGS_LAST = 'BR_HOST_CFLAGS_LAST'
ENV_EXTRA_DEFINES = 'BR_HOST_EXTRA_DEFINES'
SHIM = Path(__file__).resolve().parent / 'host_cc'

Config = namedtuple('Config', 'target poly')
# The suite matrix: each product with polyphony (its device default) and without
# (tools/build.py --no-polyphony).
CONFIGS = {
    'fm1': Config('fm1', True),
    'fm1-nopoly': Config('fm1', False),
    'smk37': Config('smk37', True),
    'smk37-nopoly': Config('smk37', False),
}
MATRIX = ('fm1', 'fm1-nopoly', 'smk37', 'smk37-nopoly')


def config_name(cfg):
    return cfg.target + ('' if cfg.poly else '-nopoly')


def _env(env):
    return env if env is not None else os.environ


def target(env=None):
    """the selected target ('fm1' unless BR_HOST_TARGET says otherwise)"""
    t = _env(env).get(ENV_TARGET, '') or 'fm1'
    if t not in TARGETS:
        raise SystemExit(f'{ENV_TARGET}={t!r}: use one of {", ".join(TARGETS)}')
    return t


def poly(env=None):
    return _env(env).get(ENV_POLY, '') == '1'


def current(env=None):
    return Config(target(env), poly(env))


def product_flags(tgt=None, with_poly=None):
    """compiler defines that select the product and its polyphony (empty for FM-1 without)"""
    tgt = target() if tgt is None else tgt
    if tgt not in TARGETS:
        raise ValueError(f'unknown target {tgt!r}')
    with_poly = poly() if with_poly is None else with_poly
    if tgt == 'fm1':
        return ['-DBR_CAP_POLY=1'] if with_poly else []
    return ['-DBR_PRODUCT=BR_PRODUCT_ID_SMK37'] + ([] if with_poly else ['-DBR_CAP_POLY=0'])


def environment(tgt, with_poly=None, base=None):
    """a copy of `base` (default os.environ) that builds every host program for `tgt`
    (with_poly None keeps the polyphony selection of `base`)"""
    env = dict(os.environ if base is None else base)
    with_poly = poly(env) if with_poly is None else with_poly
    env[ENV_TARGET] = tgt
    env[ENV_POLY] = '1' if with_poly else '0'
    flags = product_flags(tgt, with_poly)
    extra = env.get(ENV_EXTRA_DEFINES, '').split()
    if any(not re.fullmatch(r'-D[A-Za-z_][A-Za-z0-9_]*(?:=[A-Za-z0-9_+-]+)?', flag) for flag in extra):
        raise ValueError(f'{ENV_EXTRA_DEFINES} accepts only simple -DNAME or -DNAME=VALUE flags')
    flags += extra
    path = [p for p in env.get('PATH', '').split(os.pathsep) if p and Path(p).resolve() != SHIM]
    if flags:
        env[ENV_CFLAGS] = ' '.join(flags)
    else:
        env.pop(ENV_CFLAGS, None)
    if flags or env.get(ENV_CFLAGS_LAST) or env.get('BR_HOST_DEPS_LOG'):   # with_class(); recording
        path.insert(0, str(SHIM))
    env['PATH'] = os.pathsep.join(path)
    return env


OPT_FLAGS = {'audio': [], 'logic': ['-O0']}
SANITIZE_FLAGS = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']


def with_class(env, opt_class='audio', sanitize=False):
    """a copy of a suite `env` (environment()) that compiles at the optimisation class
    `opt_class` ('audio': unchanged; 'logic': -O0), with sanitizers if asked"""
    env = dict(env)
    last = list(OPT_FLAGS[opt_class]) + (SANITIZE_FLAGS if sanitize else [])
    if not last:
        env.pop(ENV_CFLAGS_LAST, None)
        return env
    env[ENV_CFLAGS_LAST] = ' '.join(last)
    path = [p for p in env.get('PATH', '').split(os.pathsep) if p and Path(p).resolve() != SHIM]
    env['PATH'] = os.pathsep.join([str(SHIM)] + path)
    if sanitize:
        env.setdefault('ASAN_OPTIONS', 'detect_leaks=0:abort_on_error=0')
        env.setdefault('UBSAN_OPTIONS', 'print_stacktrace=1')
    return env


def activate(tgt, with_poly=None):
    """select `tgt` for this process and every compiler it starts (polyphony: kept unless given)"""
    new = environment(tgt, with_poly)
    for k in [k for k in os.environ if k not in new]:
        del os.environ[k]
    os.environ.update(new)
    return product_flags(tgt, with_poly)


# ---- source-text tests: keep only the selected product's lines ------------------------------
# Several runners read firmware sources as text (function bodies, call order). The unified
# sources hold both products under #if BR_PRODUCT_SMK37 / BR_CAP_* ... #else ... #endif;
# select_product() resolves exactly those conditionals for one target and leaves every other
# preprocessor line alone. Removed lines become empty lines, so line numbers stay put.
import re as _re

_TOKEN = _re.compile(r'\s*(?:(\d+)[uUlL]*|([A-Za-z_]\w*)|(&&|\|\||==|!=|!|\(|\)))')


def known_macros(tgt=None, with_poly=None):
    """the product.h macros select_product() resolves, as product.h defines them"""
    tgt = target() if tgt is None else tgt
    smk = 1 if tgt == 'smk37' else 0
    p = int(bool(poly() if with_poly is None else with_poly))
    return {'BR_PRODUCT_ID_FM1': 1, 'BR_PRODUCT_ID_SMK37': 2, 'BR_PRODUCT': 2 if smk else 1,
            'BR_PRODUCT_FM1': 1 - smk, 'BR_PRODUCT_SMK37': smk,
            'BR_CAP_PADS': smk, 'BR_CAP_FADERS': smk, 'BR_CAP_WHEELS': smk, 'BR_CAP_VELOCITY': smk,
            'BR_CAP_DIN_MIDI': 1 - smk, 'BR_CAP_SUPPLY396': 1 - smk, 'BR_CAP_POLY': p,
            # product.h defines the poly engine switch (value 1) only when BR_CAP_POLY is 1
            'BR_POLY_SCHEDULER_EXPERIMENT': 1 if p else None,
            'defined(BR_POLY_SCHEDULER_EXPERIMENT)': p,
            'BR_POLY_SUPPORT': int(p or smk),
            'BR_POLY_SIZE_CONSTRAINED': int(p and not smk)}


def eval_condition(expr, known):
    """three-valued #if evaluation: 0/1 when only known macros decide it, else None"""
    toks, pos = [], 0
    expr = _re.sub(r'/\*.*?\*/|//.*', '', expr).strip()
    while pos < len(expr):
        m = _TOKEN.match(expr, pos)
        if not m or m.end() == pos:
            return None                            # an operator this evaluator does not model
        toks.append(m.group(1) or m.group(2) or m.group(3))
        pos = m.end()
        if pos < len(expr) and expr[pos:].strip() == '':
            break
    i = 0

    def peek():
        return toks[i] if i < len(toks) else None

    def take():
        nonlocal i
        i += 1
        return toks[i - 1]

    def primary():
        t = take()
        if t == '(':
            v = orr()
            if take() != ')':
                raise ValueError
            return v
        if t == '!':
            v = primary()
            return None if v is None else int(not v)
        if t == 'defined':
            paren = peek() == '('
            if paren:
                take()
            name = take()
            if paren and take() != ')':
                raise ValueError
            return known.get(f'defined({name})')   # only modelled for the poly switch
        if t is None or t in ('&&', '||', '==', '!=', ')'):
            raise ValueError
        if t[0].isdigit():
            return int(t)
        if known.get(f'defined({t})') == 0:
            return 0                               # an undefined name is 0 in #if
        return known.get(t)

    def eq():
        v = primary()
        while peek() in ('==', '!='):
            op, w = take(), primary()
            v = None if v is None or w is None else int((v == w) == (op == '=='))
        return v

    def andd():
        v = eq()
        while peek() == '&&':
            take()
            w = eq()
            v = 0 if v == 0 or w == 0 else (1 if v is not None and w is not None else None)
        return v

    def orr():
        v = andd()
        while peek() == '||':
            take()
            w = andd()
            v = 1 if v or w else (0 if v == 0 and w == 0 else None)
        return v

    try:
        v = orr()
        if i != len(toks):
            return None
    except (ValueError, IndexError):
        return None
    return None if v is None else int(bool(v))


def select_product(text, tgt=None, with_poly=None):
    """`text` with the product conditionals resolved for `tgt` (others untouched)"""
    known = known_macros(tgt, with_poly)
    lines = text.split('\n')
    out = []
    # frame: [resolved, emitting_branch, any_branch_taken, parent_emitting]
    stack = []
    emitting = True
    i = 0
    while i < len(lines):
        line = lines[i]
        # join continued directive lines
        j = i
        full = line
        while full.endswith('\\') and j + 1 < len(lines):
            j += 1
            full = full[:-1] + ' ' + lines[j]
        m = _re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', full)
        span = lines[i:j + 1]
        i = j + 1
        if not m:
            out.extend(span if emitting else [''] * len(span))
            continue
        kw, rest = m.group(1), m.group(2)
        if kw in ('if', 'ifdef', 'ifndef'):
            v = eval_condition(rest, known) if kw == 'if' else None
            if v is None:
                stack.append([False, True, False, emitting])
                out.extend(span if emitting else [''] * len(span))
            else:
                stack.append([True, bool(v), bool(v), emitting])
                out.extend([''] * len(span))
                emitting = emitting and bool(v)
            continue
        if not stack:
            raise ValueError(f'unbalanced #{kw}')
        f = stack[-1]
        if kw == 'endif':
            stack.pop()
            out.extend([''] * len(span) if f[0] or not f[3] else span)
            emitting = f[3]
            continue
        if not f[0]:                              # unresolved frame: keep its directives
            out.extend(span if f[3] else [''] * len(span))
            continue
        if kw == 'elif':
            v = 0 if f[2] else eval_condition(rest, known)
            if v is None:                         # the rest of the chain is not a product test:
                f[0] = False                      # keep it, starting as a plain #if
                emitting = f[3]
                out.append(_re.sub(r'#\s*elif', '#if', span[0], count=1) if emitting else '')
                out.extend((span[1:] if emitting else [''] * (len(span) - 1)))
                continue
        else:
            v = 0 if f[2] else 1
        f[1] = bool(v)
        f[2] = f[2] or bool(v)
        emitting = f[3] and f[1]
        out.extend([''] * len(span))
    if stack:
        raise ValueError('unterminated #if')
    return '\n'.join(out)
