# SPDX-License-Identifier: GPL-3.0-only
"""Quick mode for tests/run_suite.py (--changed [BASE]): run only the runners a change can affect.

A convenience for iteration, never a substitute for the full suite. Two layers:

1. A derived dependency map, recorded by every full suite run (build/suite-deps-<config>.json):
   for each runner, every file its compiles read (tests/host_cc runs each compile once more
   under -M -MG: tests/depscan/ccdeps.py) and every file or directory its Python processes open,
   list or copy (an audit hook: tests/depscan/sitecustomize.py). Paths inside a temporary or
   build/ copy of the tree are mapped back to the tree file of the same relative name.
2. A small declared table (AREAS) from source areas to runner groups (GROUPS). Nearly every test
   compiles all of firmware/src/dinghy.c, so layer 1 alone selects almost everything for any
   firmware change; an AREAS entry narrows it ("narrow": the runners that depend on the file AND
   are in the area's groups) or adds a group ("add": the group OR the runners that depend on it).

Rules, per changed file (the union over all changed files is run):
  ALWAYS_ALL (the suite, its helpers, FLAGS, baselines, build scripts)  -> every runner
  NONE (documentation)                                                  -> no runner
  an AREAS entry                                                        -> as above
  otherwise, a file in the dependency map                               -> the runners that read it
  otherwise (nothing is known to read it)                               -> every runner
A runner the map does not know yet (new, or never recorded) always runs, and without any map the
whole suite runs (and records one).

Function level (build/suite-funcs-<config>.json, recorded by run_suite.py --record-funcs): for a
changed file under firmware/src or firmware/hal the choice is by what a runner EXECUTES. The
recorded runs are built with clang's -fprofile-instr-generate (tests/host_cc/host-cc, BR_HOST_INSTR);
the executed firmware function names of each passing runner are stored. A changed line inside
function F selects the runners that executed F (scan_functions() finds the function ranges, in the
new file and in the base version); a changed line outside any function, or a whole-file change
(--for FILE), selects the runners that executed ANY function defined in the file. Where that finds
nothing (a header without functions, a new file, a new function, a file nothing executed) the rule
above for that file applies. A runner absent from the function map always runs when any file was
chosen at function level. Without a function map none of this applies.
"""
import bisect
import fnmatch
import json
import os
import re
import shutil
import subprocess
import tempfile
from collections import namedtuple
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEPSCAN = ROOT / 'tests/depscan'
ENV_LOG = 'BR_HOST_DEPS_LOG'

ALWAYS_ALL = ['tests/run_suite.py', 'tests/suite_changed.py', 'tests/hd_runner.py', 'tests/run_drive.py',
              'tests/host_target.py', 'tests/host_cc/*', 'tests/depscan/*', 'tests/baselines/*.json',
              'tools/build_braids_host.sh', 'tools/build.py']
NONE = ['docs/*', '*.md', 'LICENSE', 'tests/README-SPEED.md']

# A small fixed set that every targeted selection (--changed, --for) also runs. The designer chooses it.
SMOKE = ['run_suite_targeting.py', 'run_screen_map.py', 'run_seq_defaults.py', 'run_drive.py', 'run_reader.py',
         'run_arp.py']

GROUPS = {
    'effects': ['run_drive.py', 'run_delay_*.py', 'run_global_reverb.py', 'run_reverb2_golden.py',
                'run_fx_render.py', 'run_comp_local.py', 'run_punch*.py', 'run_global_filter.py',
                'run_filter_*.py', 'run_mix_laws.py', 'run_master_*.py', 'run_output_scale.py',
                'run_partial_frame_reconstruction.py', 'run_kit_mixer.py', 'run_mixer*.py',
                'run_lfo_mod_law.py', 'run_env_times.py', 'run_session_pcm.py', 'run_screen_map.py',
                'run_reader.py', 'run_mix_icon_hd.py', 'run_screen_transitions_hd.py'],
    'screens': ['run_*_hd.py', 'run_hd_text.py', 'run_font_fit.py', 'run_startup_art.py', 'run_track_redraw.py',
                'run_screen_map.py', 'run_reader.py', 'run_global_shortcuts.py', 'run_keyboard_modes.py',
                'run_lfo_edit_navigation.py', 'run_retrig_ui.py', 'run_seq_leds.py', 'run_seq_defaults.py',
                'run_save_*.py', 'run_pattern_panel_validation.py', 'run_pattern_target_ui_lock2.py',
                'run_smk_*.py', 'run_mod_target_tests.py', 'run_poly_ui.py', 'run_punch_keys.py',
                'run_global_timing.py', 'run_perform_heat.py'],
    'save': ['run_project_*.py', 'run_session_*.py', 'run_midi_session_v16.py', 'run_pattern_*.py',
             'run_guarded_*.py', 'run_save_*.py', 'run_take_pattern_capacity.py', 'run_late_drop_protocol.py',
             'run_bank.py', 'run_mod_target_tests.py', 'run_retrig_ui.py'],
    'midi': ['run_midi_*.py', 'run_arp.py', 'run_microtiming.py', 'run_mono_priority.py', 'run_keyboard_modes.py',
             'run_poly_ui.py'],
    'tools': ['run_firmware_metadata.py', 'run_fm1_install.py', 'run_product_switch.py', 'run_product_tests.py',
              'run_validate_package_padding.py'],
}

# (file patterns, mode, groups). The first entry that matches decides.
AREAS = [
    (['firmware/src/instrument_fx*', 'firmware/src/instrument_reverb2*', 'firmware/src/instrument_compressor.c',
      'firmware/src/instrument_filter*', 'firmware/src/instrument_softclip_table.h',
      'firmware/src/master_gain_control.h', 'firmware/src/instrument_punch.h',
      'firmware/src/instrument_level_smoothing.h'], 'narrow', ['effects']),
    (['firmware/src/instrument_ui.c', 'firmware/src/instrument_widgets*', 'firmware/src/gfx.c',
      'firmware/src/gfx_hd*', 'firmware/src/james_font*', 'firmware/src/instrument_shapes.c',
      'firmware/src/instrument_plaits_icons.h', 'firmware/src/instrument_reader.h', 'firmware/src/lcd.c',
      'firmware/src/instrument_project_ui.c', 'firmware/src/instrument_project_device_ui.c',
      'firmware/gen/jamesos_start_rle.inc', 'tools/fonts/*'], 'narrow', ['screens']),
    (['firmware/src/instrument_session*', 'firmware/src/instrument_project_device.c', 'firmware/src/pattern_*',
      'firmware/src/project_store*', 'firmware/src/example_song_import*'], 'narrow', ['save']),
    (['firmware/src/midi_*', 'firmware/src/instrument_midi_routes.h'], 'narrow', ['midi']),
    (['tools/*'], 'add', ['tools']),
]


def _match(path, patterns):
    return any(fnmatch.fnmatch(path, p) or fnmatch.fnmatch(os.path.basename(path), p) and '/' not in p
               for p in patterns)


def group_runners(groups, all_runners):
    pats = [p for g in groups for p in GROUPS[g]]
    return {r for r in all_runners if any(fnmatch.fnmatch(r, p) for p in pats)}


# ---- recording (every full suite run) -------------------------------------------------------
def recording_env(env, log):
    """env for one runner that appends what it reads to `log`"""
    env = dict(env)
    env[ENV_LOG] = str(log)
    shim = str(ROOT / 'tests/host_cc')                 # every compile through the wrapper (ccdeps.py)
    path = [p for p in env.get('PATH', '').split(os.pathsep) if p and p != shim]
    env['PATH'] = os.pathsep.join([shim] + path)
    env['PYTHONPATH'] = os.pathsep.join([str(DEPSCAN)] + [p for p in env.get('PYTHONPATH', '').split(os.pathsep) if p])
    return env


_TOP = ('firmware', 'tests', 'tools', 'assets', 'releases', 'docs')


def to_tree(path):
    """a recorded absolute path -> the tree-relative file it stands for, or None"""
    p = Path(path)
    try:
        rel = p.resolve().relative_to(ROOT)
        if rel.parts and rel.parts[0] != 'build' and (ROOT / rel).exists():
            return str(rel)
        if rel.parts and rel.parts[0] != 'build':
            return None                          # a relative name the runner resolved elsewhere
    except (ValueError, OSError):
        pass
    parts = p.parts                              # a copy (a temporary tree, build/seq-behaviour/tree)
    for i, part in enumerate(parts):
        if part in _TOP:
            cand = Path(*parts[i:])
            if (ROOT / cand).exists():
                return str(cand)
    return None


def read_log(log, runner):
    files, dirs = {f'tests/{runner}'}, set()
    try:
        lines = Path(log).read_text(errors='replace').splitlines()
    except OSError:
        return sorted(files), []
    for line in lines:
        kind, path = (line[:2], line[2:]) if line[:2] in ('F ', 'D ') else ('F ', line)
        rel = to_tree(path)
        if not rel:
            continue
        if kind == 'D ' or (ROOT / rel).is_dir():
            dirs.add(rel)
        else:
            files.add(rel)
    return sorted(files), sorted(dirs)


def map_path(config):
    return ROOT / f'build/suite-deps-{config}.json'


def save_map(config, deps):
    """deps: runner -> (files, dirs). Kept with any earlier entries for runners not run now."""
    path = map_path(config)
    try:
        old = json.loads(path.read_text()).get('runners', {})
    except (OSError, ValueError):
        old = {}
    old.update({r: {'files': f, 'dirs': d} for r, (f, d) in deps.items()})
    path.write_text(json.dumps({'config': config, 'runners': old}, indent=0, sort_keys=True))


def load_maps():
    """runner -> (set of files, set of dirs), the union over every recorded configuration"""
    out = {}
    for path in sorted((ROOT / 'build').glob('suite-deps-*.json')):
        try:
            data = json.loads(path.read_text())['runners']
        except (OSError, ValueError, KeyError):
            continue
        for r, v in data.items():
            f, d = out.setdefault(r, (set(), set()))
            f.update(v.get('files', ()))
            d.update(v.get('dirs', ()))
    return out


# ---- function scanner ------------------------------------------------------------------------
Func = namedtuple('Func', 'name start end')            # 1-based, inclusive line numbers
_NOT_NAMES = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'defined', 'typeof', '__typeof__',
              '__attribute__', '__declspec', '_Alignas', '__asm__', 'asm', '_Static_assert', 'static_assert'}


def _blank_noncode(text):
    """text with comments, string and character literals and preprocessor lines (with their
    continuations) replaced by spaces; line structure and columns are kept"""
    out, i, n = [], 0, len(text)
    state, line_start = 'code', True
    while i < n:
        c = text[i]
        if state == 'code':
            if line_start:                               # a preprocessor line (and its continuations)
                j = i
                while j < n and text[j] in ' \t':
                    j += 1
                if j < n and text[j] == '#':
                    k = j
                    while k < n:
                        e = text.find('\n', k)
                        e = n if e < 0 else e
                        cont = text[k:e].rstrip().endswith('\\')
                        k = e + 1
                        if not cont:
                            break
                    seg = text[i:min(k, n)]
                    out.append(re.sub(r'[^\n]', ' ', seg))
                    i = min(k, n)
                    line_start = True
                    continue
            line_start = c == '\n'
            if c == '/' and text[i:i + 2] == '//':
                e = text.find('\n', i)
                e = n if e < 0 else e
                out.append(' ' * (e - i))
                i = e
            elif c == '/' and text[i:i + 2] == '/*':
                state = 'block'
                out.append('  ')
                i += 2
            elif c in '"\'':
                state = c
                out.append(' ')
                i += 1
            else:
                out.append(c)
                i += 1
        elif state == 'block':
            if text[i:i + 2] == '*/':
                state = 'code'
                out.append('  ')
                i += 2
            else:
                out.append(c if c == '\n' else ' ')
                line_start = False
                i += 1
        else:                                            # a string or character literal
            if c == '\\' and i + 1 < n:
                out.append(' ' + (text[i + 1] if text[i + 1] == '\n' else ' '))
                i += 2
            elif c == state or c == '\n':
                state = 'code'
                out.append(c if c == '\n' else ' ')
                line_start = c == '\n'
                i += 1
            else:
                out.append(' ')
                i += 1
    return ''.join(out)


def _last_paren_name(header):
    """the function name of a signature `header` (text before the body's '{'), or None: the identifier in
    front of the last top-level (...) group that is not an attribute; None if there is an '=' outside
    parentheses or no such group"""
    depth, groups, start = 0, [], None
    for k, ch in enumerate(header):
        if ch == '(':
            if depth == 0:
                start = k
            depth += 1
        elif ch == ')' and depth:
            depth -= 1
            if depth == 0:
                groups.append((start, k))
        elif ch == '=' and depth == 0 and header[k - 1:k + 1] not in ('==', '!=', '<=', '>='):
            return None
    if depth:
        return None
    for a, b in reversed(groups):
        m = re.search(r'([A-Za-z_]\w*)\s*$', header[:a])
        if not m:
            return None
        if m.group(1) in _NOT_NAMES:
            continue
        return m.group(1)
    return None


def scan_functions(text):
    """-> [Func] for the function definitions in C `text`. A definition is a signature `name(...)`
    (one or more lines, starting at column 0) followed by `{` on the same or a later line; its
    body ends at the next `}` at column 0, or earlier where the braces balance (a compact body).
    Struct and initialiser bodies are skipped the same way."""
    code = _blank_noncode(text).split('\n')
    n, funcs, i = len(code), [], 0
    head, first = '', None                               # pending signature text, its first line
    while i < n:
        line = code[i]
        j = 0
        while j < len(line):
            ch = line[j]
            if ch == ';':
                head, first = '', None
            elif ch == '{':
                name = _last_paren_name(head) if head.strip() else None
                depth, e_depth = 0, None                 # where do the braces close?
                for q in range(i, n):
                    for ch2 in (code[q][j:] if q == i else code[q]):
                        depth += (ch2 == '{') - (ch2 == '}')
                    if depth <= 0:
                        e_depth = q
                        break
                e_col = next((q for q in range(i + 1, n) if code[q][:1] == '}'), n - 1)
                end = e_col if e_depth is None else min(e_depth, e_col)
                if name:
                    funcs.append(Func(name, first + 1, end + 1))
                head, first = '', None
                i, line, j = end, '', 0                  # resume after the body
                break
            else:
                if head.strip() or ch not in ' \t':
                    if first is None:
                        first = i
                    head += ch
            j += 1
        else:
            head += ' '
        i += 1
    return funcs


def function_at(funcs, line):
    """the Func containing 1-based `line`, or None (funcs from scan_functions, in file order)"""
    starts = [f.start for f in funcs]
    k = bisect.bisect_right(starts, line) - 1
    return funcs[k] if k >= 0 and funcs[k].start <= line <= funcs[k].end else None


def _scannable(path):
    """files whose changes are chosen at function level"""
    return (fnmatch.fnmatch(path, 'firmware/src/*.[ch]') or
            (path.startswith('firmware/hal/') and path.endswith(('.c', '.h'))))


def firmware_function_names():
    """(names defined under firmware/src and firmware/hal, names defined under tests/)"""
    def names(files):
        out = set()
        for f in files:
            try:
                out |= {fn.name for fn in scan_functions(Path(f).read_text(errors='replace'))}
            except OSError:
                pass
        return out
    fw = list((ROOT / 'firmware/src').glob('*.[ch]')) + [p for p in (ROOT / 'firmware/hal').rglob('*')
                                                         if p.suffix in ('.c', '.h')]
    return names(fw), names((ROOT / 'tests').glob('*.[ch]'))


# ---- function-map recording (run_suite.py --record-funcs) -----------------------------------
ENV_INSTR = 'BR_HOST_INSTR'


def funcs_env(env, profile_file):
    """env for one runner whose compiles are instrumented (host_cc/host-cc) and whose binaries
    write profiles to `profile_file` (a pattern with %p-%m)"""
    env = dict(env)
    env[ENV_INSTR] = '1'
    env['LLVM_PROFILE_FILE'] = str(profile_file)
    shim = str(ROOT / 'tests/host_cc')
    path = [p for p in env.get('PATH', '').split(os.pathsep) if p and p != shim]
    env['PATH'] = os.pathsep.join([shim] + path)
    return env


def _profdata():
    for cmd in (['xcrun', 'llvm-profdata'], ['llvm-profdata']):
        try:
            if subprocess.run(cmd + ['--version'], capture_output=True).returncode == 0:
                return cmd
        except OSError:
            pass
    return None


def executed_functions(profdir, fw_names, test_names):
    """the sorted firmware function names with a nonzero count in the profiles of `profdir`, or None if
    there is no profile or no llvm-profdata. Static functions appear as <mainfile>:<name>."""
    raws = sorted(Path(profdir).glob('*.profraw'))
    tool = _profdata()
    if not raws or not tool:
        return None
    merged = Path(profdir) / 'merged.profdata'
    r = subprocess.run(tool + ['merge', '-o', str(merged), *map(str, raws)], capture_output=True, text=True)
    if r.returncode:
        return None
    r = subprocess.run(tool + ['show', '--all-functions', str(merged)], capture_output=True, text=True,
                       errors='replace')
    if r.returncode:
        return None
    return parse_profile_show(r.stdout, fw_names, test_names)


def parse_profile_show(text, fw_names, test_names):
    names, cur = set(), None
    for line in text.splitlines():
        m = re.match(r'^  (\S+):$', line)
        if m:
            cur = m.group(1).split(':')[-1]              # strip the <mainfile>: of a static function
            continue
        m = re.match(r'^\s+Function count: (\d+)', line)
        if m and cur and int(m.group(1)) > 0:
            names.add(cur)
    return sorted(n for n in names if n in fw_names and n not in test_names)


def funcs_map_path(config):
    return ROOT / f'build/suite-funcs-{config}.json'


def save_funcs(config, funcs):
    """funcs: runner -> [names]; merged into the recorded map, written atomically"""
    path = funcs_map_path(config)
    try:
        old = json.loads(path.read_text())
    except (OSError, ValueError):
        old = {}
    if not isinstance(old, dict):
        old = {}
    old.update({r: sorted(set(v)) for r, v in funcs.items()})
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=path.name + '.')
    with os.fdopen(fd, 'w') as f:
        json.dump(old, f, indent=0, sort_keys=True)
    os.replace(tmp, path)


def load_funcs():
    """runner -> set of executed function names, the union over every recorded configuration"""
    out = {}
    for path in sorted((ROOT / 'build').glob('suite-funcs-*.json')):
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError):
            continue
        for r, names in data.items():
            out.setdefault(r, set()).update(names)
    return out


# ---- selection ------------------------------------------------------------------------------
Change = namedtuple('Change', 'new old hunks', defaults=((),))   # changed line numbers in the new / base file, and
#                                       the hunks (old start, old count, new start, new count) they come from


def _git(*a):
    return subprocess.run(['git', *a], cwd=ROOT, capture_output=True, text=True, check=True).stdout


def changed_files(base=None):
    """(base commit, files changed since base: committed, staged, unstaged and untracked)"""
    try:
        if not base:
            base = _git('merge-base', 'HEAD', 'integ').strip()
        files = set(_git('diff', '--name-only', base).split())
        files |= set(_git('ls-files', '--others', '--exclude-standard').split())
    except (OSError, subprocess.CalledProcessError) as e:
        raise SystemExit(f'--changed needs a git checkout with {base or "integ"}: {e}')
    return base, sorted(files)


def parse_diff_lines(diff):
    """`git diff -U0` text -> {path: Change}"""
    out, path = {}, None
    for line in diff.splitlines():
        if line.startswith('+++ '):
            p = line[4:].strip()
            path = p[2:] if p.startswith('b/') else None
            if path:
                out.setdefault(path, Change(set(), set(), []))
        elif line.startswith('--- ') and not line.startswith('--- a/'):
            path = None                                  # /dev/null: a new file, its '+++' follows
        elif line.startswith('@@') and path:
            m = re.match(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', line)
            if m:
                a, b, c, d = int(m[1]), int(m[2] or 1), int(m[3]), int(m[4] or 1)
                out[path].old.update(range(a, a + b))
                out[path].new.update(range(c, c + d))
                out[path].hunks.append((a, b, c, d))
    return out


def _strip_comments(text):
    """text without // and /* */ comments (string and character literals kept), newlines kept"""
    out, i, n, state = [], 0, len(text), 'code'
    while i < n:
        c = text[i]
        if state == 'code':
            if text[i:i + 2] == '//':
                e = text.find('\n', i)
                i = n if e < 0 else e
            elif text[i:i + 2] == '/*':
                state = 'block'
                i += 2
            else:
                if c in '"\'':
                    state = c
                out.append(c)
                i += 1
        elif state == 'block':
            if text[i:i + 2] == '*/':
                state = 'code'
                out.append(' ')
                i += 2
            else:
                if c == '\n':
                    out.append(c)
                i += 1
        else:
            out.append(c)
            if c == '\\' and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == state or c == '\n':
                state = 'code'
            i += 1
    return ''.join(out)


def drop_trivial_hunks(change, old_text, new_text):
    """`change` without the hunks whose old and new text are equal once comments and whitespace are
    removed (whitespace-only and comment-only edits; the comments are found in the whole file, so a
    line in the middle of a /* */ block counts)"""
    if change is None or not change.hunks:
        return change
    o = _strip_comments(old_text or '').split('\n')
    n = _strip_comments(new_text or '').split('\n')
    new, old, hunks = set(), set(), []
    for a, b, c, d in change.hunks:
        ot = re.sub(r'\s+', '', ''.join(o[a - 1:a - 1 + b]))
        nt = re.sub(r'\s+', '', ''.join(n[c - 1:c - 1 + d]))
        if ot == nt:
            continue
        old.update(range(a, a + b))
        new.update(range(c, c + d))
        hunks.append((a, b, c, d))
    return Change(new, old, hunks)


def changed_lines(base, files):
    """{path: Change} for the changed files under function-level selection; None for a file whose
    lines are unknown (untracked: the whole file counts)"""
    paths = [f for f in files if _scannable(f)]
    if not paths:
        return {}
    diff = _git('diff', '-U0', '--no-color', '--no-ext-diff', base, '--', *paths)
    out = parse_diff_lines(diff)
    for f in paths:
        if f not in out:
            out[f] = None                                # untracked (or mode-only): whole file
        else:
            out[f] = drop_trivial_hunks(out[f], _base_text(base, f), _new_text(f))
    return out


def _base_text(base, path):
    try:
        return _git('show', f'{base}:{path}')
    except (OSError, subprocess.CalledProcessError):
        return None


def _new_text(path):
    try:
        return (ROOT / path).read_text(errors='replace')
    except OSError:
        return None


def _file_funcs(path):
    text = _new_text(path)
    return scan_functions(text) if text is not None else []


# ---- the firmware index: who defines what, who mentions what ----------------------------------
class Index:
    """name_files: function name -> files defining it; mentions: identifier -> names of the functions
    whose bodies contain it as a whole word; macros: macro name -> identifiers its body mentions"""

    def __init__(self, texts):
        self.name_files, self.mentions, self.macros = {}, {}, {}
        for path, text in texts.items():
            self._add(path, text)

    def _add(self, path, text):
        code = _blank_noncode(text).split('\n')
        for fn in scan_functions(text):
            self.name_files.setdefault(fn.name, set()).add(path)
            for w in set(re.findall(r'[A-Za-z_]\w*', '\n'.join(code[fn.start - 1:fn.end]))):
                self.mentions.setdefault(w, set()).add(fn.name)
        raw = _strip_comments(text).split('\n')
        k = 0
        while k < len(raw):
            m = re.match(r'\s*#\s*define\s+([A-Za-z_]\w*)(.*)', raw[k])
            if m:
                body = m.group(2)
                while body.rstrip().endswith('\\') and k + 1 < len(raw):
                    k += 1
                    body = body.rstrip()[:-1] + ' ' + raw[k]
                self.macros.setdefault(m.group(1), set()).update(re.findall(r'[A-Za-z_]\w*', body))
            k += 1

    def referencing_functions(self, idents):
        """(functions mentioning any of `idents`, the idents closed over macros that use them)"""
        ids = set(idents)
        grew = True
        while grew:
            grew = False
            for m, body in self.macros.items():
                if m not in ids and body & ids:
                    ids.add(m)
                    grew = True
        out = set()
        for w in ids:
            out |= self.mentions.get(w, set())
        return out, ids


_INDEX_CACHE = {}                                        # path -> (mtime_ns, size, per-file Index)


def load_index():
    """the Index over firmware/src and firmware/hal (a file is re-read only when its mtime or size changed)"""
    files = list((ROOT / 'firmware/src').glob('*.[ch]')) + [p for p in (ROOT / 'firmware/hal').rglob('*')
                                                            if p.suffix in ('.c', '.h')]
    merged = Index({})
    for p in sorted(files):
        try:
            st = p.stat()
        except OSError:
            continue
        key = (st.st_mtime_ns, st.st_size)
        hit = _INDEX_CACHE.get(str(p))
        if not hit or hit[0] != key:
            hit = (key, Index({str(p.relative_to(ROOT)): p.read_text(errors='replace')}))
            _INDEX_CACHE[str(p)] = hit
        one = hit[1]
        for n, fs in one.name_files.items():
            merged.name_files.setdefault(n, set()).update(fs)
        for w, fs in one.mentions.items():
            merged.mentions.setdefault(w, set()).update(fs)
        for m, b in one.macros.items():
            merged.macros.setdefault(m, set()).update(b)
    return merged


_IDENT = r'[A-Za-z_]\w*'


def defined_identifiers(text, line):
    """the identifiers DEFINED on 1-based `line` of `text`, outside functions: a macro's name (also
    from a continuation line), a variable or table name, enum constants, a struct tag, field and
    typedef name. -> (set, ok): ok False when nothing could be extracted (use the file-level rule);
    a line with no code (blank, comment) gives (set(), True)."""
    raw = text.split('\n')
    if not 1 <= line <= len(raw):
        return set(), True
    k = line - 1
    while k > 0 and raw[k - 1].rstrip().endswith('\\'):
        k -= 1
    first = raw[k]
    if first.lstrip().startswith('#'):
        m = re.match(r'\s*#\s*define\s+(' + _IDENT + ')', first)
        return ({m.group(1)}, True) if m else (set(), False)
    code = _blank_noncode(text).split('\n')
    if not code[line - 1].strip():
        return set(), True
    j = line - 1                                         # the construct's first line: col 0, not a "}"
    while j > 0 and (not code[j].strip() or code[j][:1] in ' \t}'):
        j -= 1
    head = ''
    for q in range(j, min(len(code), j + 8)):
        head += ' ' + code[q]
        if '{' in code[q] or ';' in code[q]:
            break
    h = re.split(r'[{;]', head, 1)[0]
    ids = set()
    inside = line - 1 > j or code[j].find('{') >= 0
    body = code[line - 1] if line - 1 > j else code[j].partition('{')[2]
    m = re.match(r'\s*(?:typedef\s+)?(struct|union|enum)\b\s*(' + _IDENT + ')?', h)
    if m:
        if m.group(2):
            ids.add(m.group(2))
        if inside and m.group(1) == 'enum':
            for part in re.split(r',', re.sub(r'\([^)]*\)', '', body.split('}')[0])):
                e = re.match(r'\s*(' + _IDENT + r')\s*(?:=|$)', part)
                if e:
                    ids.add(e.group(1))
        elif inside:
            for st in body.split(';'):
                st = re.sub(r'\[[^\]]*\]|:\s*\d+', '', st)
                e = re.findall(_IDENT, st)
                if len(e) >= 2:
                    ids.add(e[-1])
        end = next((q for q in range(line - 1, len(code)) if code[q][:1] == '}'), None)
        if end is not None:
            t = re.match(r'\}\s*(' + _IDENT + ')?', code[end])
            if t and t.group(1):
                ids.add(t.group(1))
    elif '=' in h:
        v = re.sub(r'\[[^\]]*\]', '', h.split('=', 1)[0])
        e = re.findall(_IDENT, v)
        if e:
            ids.add(e[-1])
    else:
        e = re.findall(_IDENT, re.sub(r'\[[^\]]*\]|\(.*', '', h))
        if e:
            ids.add(e[-1])
    return (ids, True) if ids else (set(), False)


def _file_rule(f, deps, readers, all_runners):
    """the dependency-map / AREAS rule for one changed file -> (runners, why)"""
    dep = set(readers.get(f, ()))
    dep |= {r for r, (_, dirs) in deps.items()
            if any(f == d or f.startswith(d.rstrip('/') + '/') for d in dirs)}
    area = next((a for a in AREAS if _match(f, a[0])), None)
    if area:
        pats, mode, groups = area
        grp = group_runners(groups, all_runners)
        sel = (dep & grp if dep else grp) if mode == 'narrow' else (dep | grp)
        return sel, f'{"/".join(groups)} area ({mode}): {len(sel)} runner(s)'
    if dep:
        return dep, f'read by {len(dep)} runner(s) (dependency map)'
    return set(all_runners), 'not in the dependency map nor the area table: every runner'


def _function_rule(f, change, funcs, by_func, base, index):
    """function-level choice for one firmware file -> (runners or None for 'use the file rule', why, causes).
    change None: the whole file counts (--for, untracked). causes: [(label, runner count)]."""
    new_text = _new_text(f) or ''
    new = scan_functions(new_text)
    file_names = {fn.name for fn in new}
    in_file = set()
    for n in file_names:
        in_file |= by_func.get(n, set())
    causes = []
    if change is not None:
        old_text = (_base_text(base, f) if base else None)
        old = scan_functions(old_text) if old_text is not None else []
        hit, fresh = set(), set()
        out_new, out_old = [], []                        # changed lines outside any function
        old_names = {fn.name for fn in old}
        for ln in sorted(change.new):
            fn = function_at(new, ln)
            if fn:
                hit.add(fn.name)
                if fn.name not in old_names:
                    fresh.add(fn.name)
            else:
                out_new.append(ln)
        for ln in sorted(change.old):
            fn = function_at(old, ln)
            if fn:
                hit.add(fn.name)
            else:
                out_old.append(ln)
        if not fresh:
            sel, lines = set(), []
            for n in sorted(hit):
                rs = by_func.get(n, set())
                sel |= rs
                where = sorted(index.name_files.get(n, ())) if index else []
                lines.append(f'    {n}: {len(rs)} runner(s)' + ('' if rs else ' (executed by no recorded runner)')
                             + (f' (name defined in {len(where)} files: {", ".join(where)}; all their runners kept)'
                                if len(where) > 1 else ''))
                causes.append((f'{f}:{n}', len(rs)))
            why = f'function level: {len(hit)} function(s) hit'
            if out_new or out_old:
                narrow = _outside_rule(new_text, old_text, out_new, out_old, by_func, index)
                if narrow is not None:
                    nsel, nwhy = narrow
                    sel |= nsel
                    why += f'; lines outside functions: {nwhy}'
                    causes.append((f'{f} (outside-function lines)', len(nsel)))
                else:
                    if not in_file:
                        return None, 'lines outside functions, and no recorded runner executes a function of this file', causes
                    sel |= in_file
                    why += (f'; lines outside any function: file-level rule, every runner executing any '
                            f'function of the file ({len(in_file)})')
                    causes.append((f'{f} (outside-function lines)', len(in_file)))
            return sel, why + f' -> {len(sel)} runner(s)' + ('\n' + '\n'.join(lines) if lines else ''), causes
        why = f'new function(s) {", ".join(sorted(fresh))} (not recorded)'
    else:
        why = 'whole file'
    if not in_file:
        return None, why + ': no recorded runner executes a function of this file', causes
    causes.append((f, len(in_file)))
    return (in_file, f'{why}: runners executing any of its {len(file_names)} function(s): {len(in_file)} runner(s)',
            causes)


def _outside_rule(new_text, old_text, out_new, out_old, by_func, index):
    """the narrow rule for changed lines outside functions: the functions whose bodies mention an
    identifier DEFINED on those lines (macro closure included) -> (runners, why), or None if some
    line yields no identifier or nothing references them (the caller uses the file-level rule)"""
    if index is None:
        return None
    ids = set()
    for text, lns in ((new_text, out_new), (old_text, out_old)):
        for ln in lns:
            got, ok = defined_identifiers(text or '', ln)
            if not ok:
                return None
            ids |= got
    if not ids:
        return None
    funcs, closed = index.referencing_functions(ids)
    if not funcs:
        return None
    sel = set()
    for n in funcs:
        sel |= by_func.get(n, set())
    extra = sorted(closed - ids)
    return sel, (f'identifier rule: defines {", ".join(sorted(ids))}'
                 + (f' (via macros {", ".join(extra[:4])}{"..." if len(extra) > 4 else ""})' if extra else '')
                 + f'; mentioned by {len(funcs)} function(s)')


def select_ex(changed, all_runners, lines=None, whole=(), base=None, deps=None, funcs=None, index=None):
    """-> (selected runners, {changed file: why}, causes [(label, runner count)]); selected == all_runners
    for a full run. lines: {file: Change or None} from changed_lines() (function level, needs a function
    map); whole: files that count as changed as a whole (--for). deps, funcs: the maps (default: recorded);
    index: the firmware Index (default: scanned from the tree when needed)."""
    deps = load_maps() if deps is None else deps
    funcs = load_funcs() if funcs is None else funcs
    all_runners = sorted(all_runners)
    if not deps:
        return set(all_runners), {'*': 'no dependency map yet (build/suite-deps-*.json): full suite'}, []
    readers = {}
    for r, (files, dirs) in deps.items():
        for f in files:
            readers.setdefault(f, set()).add(r)
    by_func = {}
    for r, names in funcs.items():
        for n in names:
            by_func.setdefault(n, set()).add(r)
    lines = lines or {}
    chosen, why, causes, used_funcs = set(), {}, [], False
    for f in changed:
        if _match(f, ALWAYS_ALL):
            chosen |= set(all_runners)
            why[f] = 'suite, helper, flags, baseline or build script: every runner'
            causes.append((f, len(all_runners)))
            continue
        if _match(f, NONE):
            why[f] = 'documentation: no runner'
            continue
        if funcs and _scannable(f):
            ch = None if f in whole else lines.get(f)
            if f not in whole and f in lines and ch is not None and not ch.new and not ch.old:
                why[f] = 'only whitespace or comment changes: no runner'
                continue
            if index is None:
                index = load_index()
            sel, w, cs = _function_rule(f, ch, funcs, by_func, base, index)
            if sel is not None:
                used_funcs = True
                chosen |= sel
                why[f] = w
                causes += cs
                continue
            fallback = w
        else:
            fallback = None
        sel, w = _file_rule(f, deps, readers, all_runners)
        chosen |= sel
        why[f] = (fallback + '; ' if fallback else '') + w
        causes.append((f, len(sel)))
    if used_funcs:
        nofunc = set(all_runners) - set(funcs)
        if nofunc:
            chosen |= nofunc
            why['(runners without a function map)'] = f'{len(nofunc)} runner(s) always run until recorded (--record-funcs)'
            causes.append(('(runners without a function map)', len(nofunc)))
    unknown = set(all_runners) - set(deps)
    if unknown:
        chosen |= unknown
        why['(new runners)'] = 'not in the dependency map: ' + ', '.join(sorted(unknown))
        causes.append(('(new runners)', len(unknown)))
    smoke = set(SMOKE) & set(all_runners)
    if smoke - chosen:
        chosen |= smoke
        why['(smoke)'] = 'fixed smoke set: ' + ', '.join(sorted(smoke))
    return chosen & set(all_runners), why, causes


def select(*a, **k):
    """select_ex() without the causes -> (selected runners, why)"""
    sel, why, _ = select_ex(*a, **k)
    return sel, why


BROAD = 40


def broad_line(chosen, causes):
    """the report line for a targeted selection above BROAD runners (else None)"""
    if len(chosen) <= BROAD:
        return None
    label, n = max(causes, key=lambda c: c[1]) if causes else ('(none)', 0)
    return f'BROAD: {len(chosen)} runners; widest cause: {label} ({n} runners)'
