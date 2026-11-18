#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Unity builds of the host tests that include firmware/src/dinghy.c.

Every such test starts with a LEAD (its lines up to and including the dinghy.c include, plus an
immediately following `#undef main`).  Tests with identical LEADs form a group; `make` emits one
translation unit per group: the LEAD once, then every member's body with its clashing file-scope
names renamed, then a `main` that dispatches on argv[1].  See tests/README-SPEED.md.

  unity_build.py make --out DIR --lead-of tests/test_X.c [--members a,b,...] [-- compiler flags...]
"""
import argparse
import fcntl
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / 'tests'
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"\.\./firmware/src/dinghy\.c"')
UNDEF_MAIN_RE = re.compile(r'^\s*#\s*undef\s+main\b')
KEYWORDS = {'struct', 'union', 'enum'}


# ---------------------------------------------------------------- lexical helpers
def strip_comments(text, keep_strings=True):
    """text without comments (newlines kept); string/char literal bodies kept, or emptied"""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(' ' + '\n' * text.count('\n', i, j))
            i = j
        elif c == '/' and text.startswith('//', i):
            j = text.find('\n', i)
            # a backslash-continued // comment is not used in this code base
            i = n if j < 0 else j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            if keep_strings:
                out.append(text[i:j])
            else:
                out.append(c + c)
                out.append('\n' * text.count('\n', i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def split_lead(text):
    """(lead_lines, body_start_index) : lead = lines up to the dinghy.c include and an immediately
    following #undef main; body_start_index is the 0-based index of the first body line.
    None if the file does not include dinghy.c."""
    lines = text.split('\n')
    for k, line in enumerate(lines):
        if INCLUDE_RE.match(line):
            end = k
            # look ahead over blank / comment-only lines for #undef main
            clean = strip_comments('\n'.join(lines[k + 1:k + 8])).split('\n')
            for m, cl in enumerate(clean):
                if not cl.strip():
                    continue
                if UNDEF_MAIN_RE.match(cl):
                    end = k + 1 + m
                break
            return lines[:end + 1], end + 1
    return None


def norm_lead(lead_lines):
    out = []
    for l in strip_comments('\n'.join(lead_lines)).split('\n'):
        l = ' '.join(l.split())
        if l:
            out.append(l)
    return tuple(out)


def lead_of(path):
    text = Path(path).read_text(errors='replace')
    r = split_lead(text)
    if r is None:
        return None
    return norm_lead(r[0])


def find_candidates():
    """{test name: Path} of every tests/*.c that includes dinghy.c"""
    out = {}
    for p in sorted(TESTS.glob('*.c')):
        try:
            text = p.read_text(errors='replace')
        except OSError:
            continue
        if 'firmware/src/dinghy.c' in text and split_lead(text) is not None:
            out[p.stem] = p
    return out


def group_of(lead_path, candidates=None):
    """({name: path} members whose lead equals the lead of lead_path, the verbatim lead text)"""
    candidates = candidates or find_candidates()
    want = lead_of(lead_path)
    members = {}
    for name, p in candidates.items():
        if lead_of(p) == want:
            members[name] = p
    verbatim = split_lead(Path(lead_path).read_text(errors='replace'))[0]
    return members, '\n'.join(verbatim)


# ---------------------------------------------------------------- body scanner
TOKEN_RE = re.compile(r'[A-Za-z_]\w*|\d[\w.]*|\.\.\.|->|[{}()\[\];,=*]|\S')


def mask_text(text):
    """text with comments and the insides of string/char literals blanked (same length, newlines kept)"""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
        elif c == '/' and text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            for k in range(i + 1, max(i + 1, j - 1)):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
            continue
        else:
            i += 1
            continue
        for k in range(i, j):
            if out[k] != '\n':
                out[k] = ' '
        i = j
    return ''.join(out)


MAIN_DEF_RE = re.compile(r'(?<![\w])main\s*\(')


def add_main_return(body):
    """A test's main may fall off its end (an implicit `return 0` only exists for the real main);
    once it is renamed the value would be garbage.  Put `return 0;` before the closing brace of the
    file-scope definition of main (same line, so line numbers stay)."""
    m = mask_text(body)
    # blank preprocessor lines so their braces/quotes do not count
    lines = m.split('\n')
    for k, l in enumerate(lines):
        if l.lstrip().startswith('#'):
            lines[k] = ' ' * len(l)
    m = '\n'.join(lines)
    depth = 0
    i, n = 0, len(m)
    while i < n:
        c = m[i]
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
        elif depth == 0 and c == 'm':
            mm = MAIN_DEF_RE.match(m, i)
            if mm and not m[max(0, i - 1)].isalnum():
                j = m.find('{', mm.end())
                semi = m.find(';', mm.end())
                if j >= 0 and (semi < 0 or j < semi):
                    d, k = 0, j
                    while k < n:
                        if m[k] == '{':
                            d += 1
                        elif m[k] == '}':
                            d -= 1
                            if d == 0:
                                return body[:k] + ' return 0; ' + body[k:]
                        k += 1
        i += 1
    return body


def scan_body(body):
    """(defined names, macro names defined/undefined, main parameter text) of a test body.
    Names: functions with a body, variables, typedef names, struct/union/enum tags (when defined),
    enum constants - all at file scope.  A simple scanner, the compiler is the judge."""
    clean = strip_comments(body, keep_strings=False)
    macros = set()
    code_lines = []
    lines = clean.split('\n')
    k = 0
    while k < len(lines):
        line = lines[k]
        if line.lstrip().startswith('#'):
            full = line
            while full.rstrip().endswith('\\') and k + 1 < len(lines):
                k += 1
                full = full.rstrip()[:-1] + ' ' + lines[k]
                code_lines.append('')
            m = re.match(r'\s*#\s*(define|undef)\s+([A-Za-z_]\w*)', full)
            if m:
                macros.add(m.group(2))
            code_lines.append('')
        else:
            code_lines.append(line)
        k += 1
    toks = TOKEN_RE.findall('\n'.join(code_lines))

    names = {}          # name -> kind
    main_params = None
    stmt = []           # tokens of the current top-level statement (aggregate bodies replaced by '{}')
    depth = 0
    i, n = 0, len(toks)

    def declarator_names(tokens):
        """names declared by a declaration's tokens (initialisers and bodies removed)"""
        parts, cur, pd = [], [], 0
        for t in tokens:
            if t in '([':
                pd += 1
            elif t in ')]':
                pd -= 1
            if t == ',' and pd == 0:
                parts.append(cur)
                cur = []
            else:
                cur.append(t)
        parts.append(cur)
        res = []
        for pi, part in enumerate(parts):
            if '=' in part:
                part = part[:part.index('=')]
            part = [t for t in part if t != '{}']
            # drop __attribute__((...)) groups
            q, cleaned = 0, []
            while q < len(part):
                if part[q] in ('__attribute__', '__asm__', 'asm'):
                    q += 1
                    if q < len(part) and part[q] == '(':
                        d = 0
                        while q < len(part):
                            d += part[q] == '('
                            d -= part[q] == ')'
                            q += 1
                            if d == 0:
                                break
                    continue
                cleaned.append(part[q])
                q += 1
            part = cleaned
            if not part:
                continue
            if '(' in part:
                p = part.index('(')
                if p + 1 < len(part) and part[p + 1] in ('*', '^'):
                    ids = [t for t in part[p + 2:] if re.match(r'[A-Za-z_]', t)]
                    if ids:
                        res.append((ids[0], 'var'))
                elif p > 0 and re.match(r'[A-Za-z_]', part[p - 1]):
                    res.append((part[p - 1], 'func'))
            else:
                while part and part[-1] == ']':
                    while part and part[-1] != '[':
                        part.pop()
                    if part:
                        part.pop()
                if part and re.match(r'[A-Za-z_]', part[-1]) and (len(part) > 1 or pi > 0):
                    res.append((part[-1], 'var'))
                elif part and re.match(r'[A-Za-z_]', part[-1]) and part[-1] not in KEYWORDS:
                    pass
        return res

    def finish(tokens, has_body, params_hint=None):
        nonlocal main_params
        if not tokens:
            return
        if 'extern' in tokens:
            return
        if tokens[0] == 'typedef':
            for nm, _ in declarator_names(tokens[1:]):
                names[nm] = 'typedef'
            return
        for nm, kind in declarator_names(tokens):
            if kind == 'func':
                if has_body:
                    names[nm] = 'func'
                    if nm == 'main':
                        main_params = params_hint
            else:
                names[nm] = 'var'

    while i < n:
        t = toks[i]
        if t == '{':
            # aggregate (struct/union/enum definition or initialiser) or function body?
            is_agg = False
            tag = None
            if stmt and stmt[-1] == '=':
                is_agg = True
            else:
                for back in (1, 2):
                    if len(stmt) >= back and stmt[-back] in KEYWORDS and (back == 1 or re.match(r'[A-Za-z_]', stmt[-1])):
                        is_agg = True
                        kw = stmt[-back]
                        tag = stmt[-1] if back == 2 else None
                        break
            # find the matching close
            d, j = 0, i
            while j < n:
                if toks[j] == '{':
                    d += 1
                elif toks[j] == '}':
                    d -= 1
                    if d == 0:
                        break
                j += 1
            inner = toks[i + 1:j]
            if is_agg:
                if tag:
                    names[tag] = 'tag'
                if stmt and (stmt[-1] != '=') and 'enum' in stmt[-2:]:
                    # enum constants: identifiers at depth 1 after '{' or ','
                    dd, expect = 0, True
                    for x in inner:
                        if x == '{' or x == '(':
                            dd += 1
                        elif x == '}' or x == ')':
                            dd -= 1
                        elif dd == 0 and x == ',':
                            expect = True
                        elif dd == 0 and expect and re.match(r'[A-Za-z_]', x):
                            names[x] = 'enumconst'
                            expect = False
                        elif dd == 0:
                            expect = False if x == '=' else expect
                stmt.append('{}')
                i = j + 1
                continue
            # function body: statement so far is the declarator; params for main
            params = None
            if 'main' in stmt and '(' in stmt:
                p = stmt.index('(')
                params = ' '.join(stmt[p + 1:stmt.index(')', p)]) if ')' in stmt[p:] else None
            finish(stmt, True, params)
            stmt = []
            i = j + 1
            continue
        if t == ';':
            finish(stmt, False)
            stmt = []
        else:
            stmt.append(t)
        i += 1
    return names, macros, main_params


# ---------------------------------------------------------------- unity text
def build_unity(lead_text, members, renames, mains):
    """members: ordered {name: (path, body_text, first_body_line, macros)}"""
    out = [lead_text]
    for name, (path, body, first, macros) in members.items():
        rel = os.path.relpath(path, ROOT)
        out.append(f'/* member {name} */')
        for m in sorted(macros | {'main'}):
            out.append(f'#pragma push_macro("{m}")')
        out.append('#undef main')
        for r in sorted(set(renames.get(name, [])) | {'main'}):
            out.append(f'#define {r} u_{name}_{r}')
        out.append(f'#line {first} "{rel}"')
        out.append(body)
        out.append('')
        out.append(f'#line 1 "unity:{name}"')
        for r in sorted(set(renames.get(name, [])) | {'main'}):
            out.append(f'#undef {r}')
        for m in sorted(macros | {'main'}):
            out.append(f'#pragma pop_macro("{m}")')
    out.append('#undef main')
    out.append('#include <stdio.h>')
    out.append('#include <string.h>')
    out.append('int main(int argc, char **argv)')
    out.append('{')
    out.append('    if (argc >= 2) {')
    out.append('        const char *want = argv[1];')
    out.append('        argv[1] = argv[0];')
    for name in members:
        call = f'u_{name}_main()' if mains.get(name) in (None, '', 'void') else f'u_{name}_main(argc - 1, argv + 1)'
        out.append(f'        if (!strcmp(want, "{name}")) return {call};')
    out.append('    }')
    out.append('    fprintf(stderr, "unity members:");')
    for name in members:
        out.append(f'    fprintf(stderr, " {name}");')
    out.append('    fprintf(stderr, "\\n");')
    out.append('    return 2;')
    out.append('}')
    return '\n'.join(out) + '\n'


DIAG_RE = re.compile(r'^(?P<file>[^\s:][^:]*):(?P<line>\d+):(?:\d+:)?\s*(?P<sev>error|fatal error):\s*(?P<msg>.*)$')
INCL_RE = re.compile(r'^In file included from (?P<file>[^:]+):(?P<line>\d+)')
REDEF_RES = [
    re.compile(r"redefinition of (?:enumerator |parameter )?'(?:struct |union |enum )?(\w+)'"),
    re.compile(r"typedef redefinition .*"),
    re.compile(r"conflicting types for '(\w+)'"),
    re.compile(r"'(\w+)' redeclared as different kind of symbol"),
    re.compile(r"redefinition of '(\w+)' as different kind of symbol"),
]
NOTE_PREV_RE = re.compile(r"note: previous (?:definition|declaration)")


def parse_diagnostics(stderr, rels):
    """[(rel member path or None, msg)] for each error; member via the #line file or include chain"""
    errs = []
    chain = None
    prev_incl = False
    for line in stderr.split('\n'):
        m = INCL_RE.match(line)
        if m:
            if not prev_incl:
                chain = m.group('file')
            prev_incl = True
            continue
        prev_incl = False
        d = DIAG_RE.match(line)
        if d:
            f = d.group('file')
            if f in rels:
                chain = None
            mem = rels.get(f) or (rels.get(chain) if chain else None)
            errs.append((mem, f, int(d.group('line')), d.group('msg')))
    return errs


SENTINEL_UNITY = '@UNITY@'
SENTINEL_OUT = '@OUT@'
NOT_HANDLED = 97


def make(out_dir, lead_of_path, only=None, syntax_cmd=None, build_cmd=None, max_rename_rounds=8, max_rounds=40):
    """Generate DIR/unity.c for the group of lead_of_path; check it with syntax_cmd (a command line
    with the SENTINEL_UNITY word in place of the unity source), renaming and excluding members as
    the diagnostics say; then, with build_cmd (SENTINEL_UNITY and SENTINEL_OUT words), compile it
    to DIR/unity_bin.  Returns (result dict, excluded dict)."""
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    cands = find_candidates()
    lp = Path(lead_of_path)
    group, lead_text = group_of(lp if lp.is_absolute() else ROOT / lp, cands)
    if only:
        group = {k: v for k, v in group.items() if k in only}
    members = {}
    for name, p in group.items():
        text = p.read_text(errors='replace')
        lead_lines, start = split_lead(text)
        body = '\n'.join(re.sub(r'^\s*#\s*undef\s+main\b.*$', '', l) for l in text.split('\n')[start:])   # keeps the line numbers
        body = add_main_return(body)
        members[name] = [p, body, start + 1, None]
    scans = {}
    for name, m in list(members.items()):
        names, macros, mp = scan_body(m[1])
        scans[name] = (names, macros, mp)
        m[3] = macros
    count = {}
    for name, (names, _, _) in scans.items():
        for nm in names:
            count.setdefault(nm, []).append(name)
    renames = {name: sorted(nm for nm in scans[name][0] if len(count[nm]) > 1) for name in members}
    excluded = {}
    rounds = 0
    rename_rounds = 0
    rels = {os.path.relpath(m[0], ROOT): name for name, m in members.items()}
    unity_path = out_dir / 'unity.c'
    sub = lambda tmpl, **kw: [kw.get(w, w) for w in tmpl]
    while True:
        rounds += 1
        active = {n: tuple(m) for n, m in members.items() if n not in excluded}
        mains = {n: scans[n][2] for n in active}
        unity_path.write_text(build_unity(lead_text, active, renames, mains))
        cmd = sub(syntax_cmd, **{SENTINEL_UNITY: str(unity_path)})
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode == 0:
            break
        errs = parse_diagnostics(p.stderr, rels)
        (out_dir / f'diag_round{rounds}.txt').write_text(p.stderr)
        if not errs:
            raise RuntimeError('unity: compile failed with no parsable error:\n' + p.stderr[-3000:])
        unattributed = [e for e in errs if e[0] is None]
        if unattributed:
            raise RuntimeError('unity: error outside any member:\n' + '\n'.join(f'{e[1]}:{e[2]}: {e[3]}' for e in unattributed[:10]))
        added = False
        if rename_rounds < max_rename_rounds:
            for mem, f, ln, msg in errs:
                for rx in REDEF_RES[:1] + REDEF_RES[2:]:
                    r = rx.search(msg)
                    if r and r.groups():
                        nm = r.group(1)
                        if nm not in renames[mem] and nm != 'main':
                            renames[mem].append(nm)
                            added = True
            if added:
                rename_rounds += 1
                continue
        bad = {}
        for mem, f, ln, msg in errs:
            bad.setdefault(mem, f'{f}:{ln}: {msg}')
        for mem, why in bad.items():
            excluded[mem] = why
        if len(excluded) == len(members):
            raise RuntimeError('unity: every member excluded')
        if rounds >= max_rounds:
            raise RuntimeError('unity: too many rounds')
    (out_dir / 'excluded.json').write_text(json.dumps(excluded, indent=1) + '\n')
    result = {
        'lead_of': str(lead_of_path),
        'members': sorted(n for n in members if n not in excluded),
        'renames': {n: renames[n] for n in members if n not in excluded and renames[n]},
        'rename_rounds': rename_rounds,
        'rounds': rounds,
        'files': {n: {'path': os.path.relpath(m[0], ROOT)} for n, m in members.items() if n not in excluded},
    }
    if build_cmd:
        tmp = out_dir / f'unity_bin.tmp{os.getpid()}'
        t0 = time.time()
        p = subprocess.run(sub(build_cmd, **{SENTINEL_UNITY: str(unity_path), SENTINEL_OUT: str(tmp)}),
                           capture_output=True, text=True)
        result['compile_seconds'] = round(time.time() - t0, 2)
        (out_dir / 'compile.log').write_text(p.stdout + p.stderr)
        if p.returncode != 0:
            tmp.unlink(missing_ok=True)
            raise RuntimeError('unity: final compile/link failed:\n' + p.stderr[-3000:])
        os.replace(tmp, out_dir / 'unity_bin')
    (out_dir / 'members.json').write_text(json.dumps(result, indent=1) + '\n')
    return result, excluded


# ---------------------------------------------------------------- cc role (called by tests/host_cc/host-cc)
OPT_WITH_ARG = {'-o', '-x', '-I', '-iquote', '-isystem', '-include', '-D', '-U', '-L', '-framework',
                '-arch', '-Xlinker', '-MF', '-MT', '-MQ', '-idirafter', '-isysroot', '-target', '-l'}
REFUSE = {'-c', '-E', '-S', '-shared', '-fsyntax-only', '-M', '-MM', '-MD', '-MMD', '-MG', '-MP'}
OBJ_EXT = ('.o', '.a', '.dylib', '.so')
SRC_OTHER = ('.cc', '.cpp', '.cxx', '.m', '.mm', '.S', '.s', '.i')


def parse_cc(args):
    """(source index, output index, object indexes) of a unity-candidate compile+link, else None"""
    src, out, objs, i = [], None, [], 0
    while i < len(args):
        a = args[i]
        if a in REFUSE:
            return None
        if a == '-o':
            if i + 1 >= len(args):
                return None
            out = i + 1
            i += 2
            continue
        if a in OPT_WITH_ARG:
            i += 2
            continue
        if a.startswith('-'):
            if a.startswith('-M') or a.startswith('-Wl,-r'):
                return None
            i += 1
            continue
        if a.endswith('.c'):
            src.append(i)
        elif a.endswith(OBJ_EXT):
            objs.append(i)
        elif a.endswith(SRC_OTHER):
            return None
        i += 1
    if len(src) != 1 or out is None:
        return None
    return src[0], out, objs


def tree_stats(root):
    """sorted (relative path, mtime_ns, size) of every file under firmware/ and tests/*.h"""
    res = []
    stack = [root / 'firmware']
    while stack:
        d = stack.pop()
        try:
            it = list(os.scandir(d))
        except OSError:
            continue
        for e in it:
            if e.is_dir(follow_symlinks=False):
                stack.append(e.path)
            else:
                st = e.stat()
                res.append((e.path, st.st_mtime_ns, st.st_size))
    for e in os.scandir(root / 'tests'):
        if e.name.endswith('.h'):
            st = e.stat()
            res.append((e.path, st.st_mtime_ns, st.st_size))
    return sorted(res)


def file_hash(path):
    h = hashlib.sha256()
    try:
        with open(path, 'rb') as f:
            for chunk in iter(lambda: f.read(1 << 20), b''):
                h.update(chunk)
    except OSError:
        h.update(b'missing')
    return h.hexdigest()


def run_id():
    """identifies the suite run (or, outside run_suite.py, the runner) this compile belongs to: the
    nearest ancestor process running run_suite.py, else the nearest running a tests/run_*.py"""
    pid, runner = os.getppid(), None
    for _ in range(12):
        try:
            p = subprocess.run(['ps', '-o', 'ppid=,lstart=,command=', '-p', str(pid)], capture_output=True, text=True).stdout.split(None, 1)
        except OSError:
            break
        if len(p) < 2:
            break
        ppid, rest = p[0], p[1]
        ident = f'{pid}@{" ".join(rest.split()[:5])}'
        if 'run_suite.py' in rest:
            return ident
        if runner is None and re.search(r'\brun_\w+\.py', rest):
            runner = ident
        if not ppid.isdigit() or int(ppid) <= 1:
            break
        pid = int(ppid)
    return runner or 'adhoc'


def record_popularity(cache, flagkey, name, rid, cmd=None):
    """popularity.json[flagkey][name] = id of the run that first asked (locked, atomic); returns the dict.
    History is never purged."""
    cache.mkdir(parents=True, exist_ok=True)
    with open(cache / '.pop.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        path = cache / 'popularity.json'
        try:
            pop = json.loads(path.read_text())
        except (OSError, ValueError):
            pop = {}
        entry = pop.get(flagkey)
        if not isinstance(entry, dict):
            entry = pop[flagkey] = {}
        if name not in entry:
            entry[name] = rid
            tmp = cache / f'popularity.json.tmp{os.getpid()}'
            tmp.write_text(json.dumps(pop, indent=1, sort_keys=True) + '\n')
            os.replace(tmp, path)
        if cmd is not None:                 # the compile that verify repeats (once per FLAGKEY)
            cpath = cache / 'flagcmds.json'
            try:
                cmds = json.loads(cpath.read_text())
            except (OSError, ValueError):
                cmds = {}
            if flagkey not in cmds:
                cmds[flagkey] = cmd
                tmp = cache / f'flagcmds.json.tmp{os.getpid()}'
                tmp.write_text(json.dumps(cmds, indent=1, sort_keys=True) + '\n')
                os.replace(tmp, cpath)
        return dict(entry)


def struct_key(flagkey, included, group):
    """STRUCTKEY: what the transformation depends on - the flags, this generator, the members' sources and
    tests/*.h, by content; nothing under firmware/.  Verification is recorded under it."""
    h = hashlib.sha256()
    h.update(f'struct-v1\0{flagkey}\0'.encode())
    h.update(file_hash(__file__).encode() + b'\0')
    for n in included:
        h.update(f'{n}\0{file_hash(group[n])}\0'.encode())
    for e in sorted(os.scandir(TESTS), key=lambda e: e.name):
        if e.name.endswith('.h'):
            h.update(f'{e.name}\0{file_hash(e.path)}\0'.encode())
    return h.hexdigest()[:40]


def unity_key(flagkey, args, objs, cwd, included, group):
    """KEY of one unity binary: flags, object contents, member and tree stats, and this script"""
    h = hashlib.sha256()
    def feed(*parts):
        for x in parts:
            h.update(str(x).encode('utf-8', 'replace') + b'\0')
    feed('unity-v2', flagkey)
    for i in objs:
        feed('obj', file_hash(args[i] if os.path.isabs(args[i]) else os.path.join(cwd, args[i])))
    for n in included:
        st = group[n].stat()
        feed('member', n, group[n], st.st_mtime_ns, st.st_size)
    st = os.stat(__file__)
    feed('generator', st.st_mtime_ns, st.st_size)          # an edit of this script makes new unity binaries
    for t in tree_stats(ROOT):
        feed('file', *t)
    return h.hexdigest()[:40]


def ensure_unity(cache, kdir, real, args, si, oi, objs, included, lead_path, cands):
    """build kdir/unity_bin if absent (flock; failure leaves failed.txt); True if it exists afterwards"""
    ubin = kdir / 'unity_bin'
    if ubin.exists():
        return True
    kdir.mkdir(parents=True, exist_ok=True)
    with open(kdir / '.lock', 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if (kdir / 'failed.txt').exists():
            return False
        if ubin.exists():
            return True
        cutoff = time.time() - 2 * 86400
        for d in cache.iterdir():
            try:
                if d != kdir and d.is_dir() and d.stat().st_mtime < cutoff:
                    shutil.rmtree(d, ignore_errors=True)
            except OSError:
                pass
        build_cmd = [real] + [SENTINEL_UNITY if i == si else SENTINEL_OUT if i == oi else a for i, a in enumerate(args)]
        build_cmd += ['-iquote', str(TESTS)]
        syn = [real] + [SENTINEL_UNITY if i == si else a for i, a in enumerate(args) if i not in objs and i not in (oi - 1, oi)]
        syn += ['-fsyntax-only', '-ferror-limit=0', '-iquote', str(TESTS)]
        try:
            make(kdir, lead_path, included, syn, build_cmd)
        except Exception as e:             # the old way of compiling then stays in force
            (kdir / 'failed.txt').write_text(str(e))
            return False
    return ubin.exists()


def cc_role(argv):
    mode = os.environ.get('BR_UNITY', '')
    if not argv or argv[0] != '--':
        raise SystemExit('usage: unity_build.py cc -- COMPILER ARGS...')
    real, args = argv[1], argv[2:]
    parsed = parse_cc(args)
    if parsed is None:
        return NOT_HANDLED
    si, oi, objs = parsed
    cwd = os.getcwd()
    src = Path(args[si])
    # only the plain relative form "tests/<name>.c" (so __FILE__ matches the old build)
    if src.is_absolute() or src.parent != Path('tests') or (Path(cwd) / src).resolve() != (TESTS / src.name).resolve():
        return NOT_HANDLED
    name = src.stem
    cands = find_candidates()
    if name not in cands:
        return NOT_HANDLED
    group, lead_text = group_of(cands[name], cands)
    if len(group) < 2:
        return NOT_HANDLED
    try:
        ver = subprocess.run([real, '--version'], capture_output=True, text=True).stdout
    except OSError:
        return NOT_HANDLED
    # FLAGKEY: the compile's identity without file contents (what decides whether two tests could share a unity)
    fk = hashlib.sha256()
    rootstr = str(ROOT)
    rel = lambda x: str(x).replace(rootstr, '<ROOT>')          # the same flags mean the same in any checkout
    for x in ('flagkey-v2', real, ver, rel(cwd), *lead_of(cands[name])):
        fk.update(str(x).encode('utf-8', 'replace') + b'\0')
    last_def = {}                    # a repeated identical -DNAME=V (no other -D/-U of NAME between) changes nothing
    for i, a in enumerate(args):
        if i in (si, oi - 1, oi):
            continue
        if a.startswith(('-D', '-U')) and len(a) > 2:
            nm = a[2:].split('=', 1)[0]
            if a.startswith('-D') and last_def.get(nm) == a:
                continue
            last_def[nm] = a
        fk.update((('obj:' + os.path.basename(a)) if i in objs else ('arg:' + rel(a))).encode('utf-8', 'replace') + b'\0')
    flagkey = fk.hexdigest()[:40]
    cache = ROOT / 'build' / 'unity'
    cur = run_id()
    cmd = {'real': real, 'cwd': cwd, 'args': args, 'si': si, 'oi': oi, 'objs': objs, 'lead_member': name,
           'target': os.environ.get('BR_HOST_TARGET', ''), 'poly': os.environ.get('BR_HOST_POLY', '')}
    entry = record_popularity(cache, flagkey, name, cur, cmd)   # every request teaches, also with BR_UNITY=0 or a fallback
    if mode == '0':
        return NOT_HANDLED
    # The unity holds the tests that asked for these flags in EARLIER runs (so its key is the same for the
    # whole current run); a test first seen in this run, or alone, is compiled the old way this time.
    frozen = sorted(n for n, r in entry.items() if r != cur and n in group)
    if mode == 'force':
        included = sorted(set(frozen) | {name})
    else:
        if name not in frozen or len(frozen) < 2:
            return NOT_HANDLED
        included = frozen
    key = unity_key(flagkey, args, objs, cwd, included, group)
    kdir = cache / key
    ubin = kdir / 'unity_bin'
    if mode != 'force':
        # served only if verify proved this member identical under the current STRUCTKEY (flags, generator,
        # member and tests/*.h contents); the binary for the current firmware is built here when absent
        try:
            proven = json.loads((cache / 'verified.json').read_text()).get(struct_key(flagkey, included, group), {})
        except (OSError, ValueError):
            proven = {}
        if proven.get(name) != 'same':
            return NOT_HANDLED
    if not ubin.exists() and not ensure_unity(cache, kdir, real, args, si, oi, objs, included, cands[name], cands):
        return NOT_HANDLED
    try:
        os.utime(kdir)
        excluded = json.loads((kdir / 'excluded.json').read_text())
        with open(cache / 'requests.log', 'a') as rq:
            rq.write(f'{key}\t{name}\n')
    except OSError:
        return NOT_HANDLED
    if name in excluded:
        return NOT_HANDLED
    out = Path(cwd) / args[oi]
    tmp = out.with_name(out.name + f'.u{os.getpid()}')
    tmp.write_text(f'#!/bin/sh\nexec "{ubin}" {name} "$@"\n')
    os.chmod(tmp, 0o755)
    os.replace(tmp, out)
    return 0


def run_pair(cmd, cwd):
    try:
        q = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=300, errors='replace')
        return q.returncode, q.stdout, q.stderr
    except subprocess.TimeoutExpired:
        return 'timeout', '', ''


def verify(config, jobs=2, log=print, changed_only=False):
    """Prove, per FLAGKEY with at least two known members, that each member run from the shared unity binary
    behaves like the member compiled the old way at the same flags (no arguments; exit code, stdout, stderr).
    Writes build/unity/verified.json: {FLAGKEY: {"_key": KEY, member: "same" | "differs" | "unverified"}}.
    "unverified": the old binary itself exits non-zero (it may need arguments) and the unity does not match."""
    from concurrent.futures import ThreadPoolExecutor
    sys.path.insert(0, str(TESTS))
    import host_target
    cfg = host_target.CONFIGS[config]
    cache = ROOT / 'build' / 'unity'
    pop = json.loads((cache / 'popularity.json').read_text())
    cmds = json.loads((cache / 'flagcmds.json').read_text()) if (cache / 'flagcmds.json').exists() else {}
    cands = find_candidates()
    os.chdir(ROOT)
    work = cache / 'verify'
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    try:
        done_keys = set(json.loads((cache / 'verified.json').read_text()))
    except (OSError, ValueError):
        done_keys = set()
    plan, skipped = [], []
    for fk, entry in sorted(pop.items()):
        if not isinstance(entry, dict) or len(entry) < 2:
            continue
        c = cmds.get(fk)
        if c is None:
            skipped.append((fk, 'no recorded command (run the tests once with this tree first)'))
            continue
        if c.get('target') != cfg.target or c.get('poly') != ('1' if cfg.poly else '0'):
            continue
        lead_member = c['lead_member']
        if Path(c['cwd']).resolve() != ROOT.resolve():
            skipped.append((fk, 'recorded from another tree'))
            continue
        if lead_member not in cands:
            skipped.append((fk, 'test gone'))
            continue
        group, _ = group_of(cands[lead_member], cands)
        included = sorted(n for n in entry if n in group)
        if len(included) < 2:
            continue
        if changed_only and struct_key(fk, included, group) in done_keys:
            continue
        plan.append((fk, c, group, included))
    results = {}
    t0 = time.time()
    tasks = []          # (callable) compile jobs
    info = {}
    for fk, c, group, included in plan:
        args, si, oi, objs, cwd = c['args'], c['si'], c['oi'], c['objs'], c['cwd']
        key = unity_key(fk, args, objs, cwd, included, group)
        kdir = cache / key
        info[fk] = {'key': key, 'kdir': kdir, 'cmd': c, 'included': included, 'skey': struct_key(fk, included, group)}
        def build_unity_job(fk=fk, c=c, kdir=kdir, included=included):
            return ensure_unity(cache, kdir, c['real'], c['args'], c['si'], c['oi'], c['objs'], included, cands[c['lead_member']], cands)
        tasks.append(('unity', fk, None, build_unity_job))
        for m in included:
            old = work / f'{fk[:8]}_{m}'
            def old_job(c=c, m=m, old=old):
                a = list(c['args'])
                a[c['si']] = f'tests/{m}.c'
                a[c['oi']] = str(old)
                p = subprocess.run([c['real']] + a, cwd=c['cwd'], capture_output=True, text=True)
                return p.returncode == 0
            tasks.append(('old', fk, m, old_job))
    with ThreadPoolExecutor(jobs) as ex:
        futs = [(k, fk, m, ex.submit(job)) for k, fk, m, job in tasks]
        built = {(k, fk, m): f.result() for k, fk, m, f in futs}
    t_compile = time.time() - t0
    def compare(fk, m):
        d = info[fk]
        old = work / f'{fk[:8]}_{m}'
        if not built.get(('unity', fk, None)):
            return fk, m, 'unverified'
        if not built.get(('old', fk, m)):
            return fk, m, 'unverified'
        try:
            excl = json.loads((d['kdir'] / 'excluded.json').read_text())
        except OSError:
            excl = {}
        if m in excl:
            return fk, m, 'unverified'
        a = run_pair([str(old)], ROOT)
        b = run_pair([str(d['kdir'] / 'unity_bin'), m], ROOT)
        if a == b:
            return fk, m, 'same'
        return fk, m, ('differs' if a[0] == 0 else 'unverified')
    with ThreadPoolExecutor(jobs) as ex:
        futs = [ex.submit(compare, fk, m) for fk, _, _, included in plan for m in included]
        for f in futs:
            fk, m, st = f.result()
            results.setdefault(info[fk]['skey'], {})[m] = st
    vpath = cache / 'verified.json'
    try:
        allv = json.loads(vpath.read_text())
    except (OSError, ValueError):
        allv = {}
    allv.update(results)
    tmp = cache / f'verified.json.tmp{os.getpid()}'
    tmp.write_text(json.dumps(allv, indent=1, sort_keys=True) + '\n')
    os.replace(tmp, vpath)
    counts = {}
    for fk, r in results.items():
        for m, st in r.items():
            if True:
                counts[st] = counts.get(st, 0) + 1
    log(json.dumps({'config': config, 'structkeys': len(results), 'members': counts, 'skipped': skipped,
                    'compile_seconds': round(t_compile, 1), 'wall_seconds': round(time.time() - t0, 1),
                    'differs': sorted(f'{fk[:8]}:{m}' for fk, r in results.items() for m, st in r.items() if st == 'differs'),
                    'unverified': sorted(f'{fk[:8]}:{m}' for fk, r in results.items() for m, st in r.items() if st == 'unverified')}, indent=1))
    return results


def main():
    if len(sys.argv) > 1 and sys.argv[1] == 'cc':
        sys.exit(cc_role(sys.argv[2:]))
    if len(sys.argv) > 1 and sys.argv[1] == 'verify':
        vp = argparse.ArgumentParser(prog='unity_build.py verify')
        vp.add_argument('--config', default='fm1')
        vp.add_argument('--jobs', type=int, default=2)
        vp.add_argument('--changed-only', action='store_true', help='verify only STRUCTKEYs not yet in verified.json')
        va = vp.parse_args(sys.argv[2:])
        verify(va.config, min(va.jobs, 2), changed_only=va.changed_only)
        return
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    mk = sub.add_parser('make')
    mk.add_argument('--out', required=True)
    mk.add_argument('--lead-of', required=True)
    mk.add_argument('--members', default='')
    mk.add_argument('cc', nargs=argparse.REMAINDER, help='-- compiler and flags used for the syntax check')
    a = ap.parse_args()
    if a.cmd == 'make':
        cc = [x for x in a.cc if x != '--']
        if not cc:
            raise SystemExit('make needs "-- <compiler> <flags>" for the error-driven check')
        only = [m for m in a.members.split(',') if m] or None
        res, exc = make(a.out, a.lead_of, only, cc + ['-fsyntax-only', '-ferror-limit=0', '-iquote', str(TESTS), SENTINEL_UNITY])
        print(json.dumps({'members': len(res['members']), 'excluded': exc, 'rename_rounds': res['rename_rounds'], 'rounds': res['rounds']}, indent=1))


if __name__ == '__main__':
    main()
