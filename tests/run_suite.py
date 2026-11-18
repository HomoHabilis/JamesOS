#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run every host test runner (tests/run_*.py) for one configuration and compare with a baseline.

    python3 tests/run_suite.py [--config NAME] [--baseline FILE] [--out FILE] [--only a,b]
    python3 tests/run_suite.py --changed [BASE] | --for FILE[,FILE] | --area NAME[,NAME] [--dry-run]
    python3 tests/run_suite.py --record-funcs [--only a,b]

Configurations (tests/host_target.py CONFIGS; the matrix is fm1, fm1-nopoly, smk37,
smk37-nopoly): product x polyphony (fm1 and smk37 are the device defaults, polyphony on;
-nopoly is tools/build.py --no-polyphony). The interface is always HD. Never touches
hardware. Builds the host DSP objects once for the configuration (tools/build_braids_host.sh),
then runs the runners, --jobs at a time (default: the number of performance cores; --jobs 1 runs
them one at a time in name order, as before). The shared DSP objects are read-only while the
runners run, so a runner that tried to rebuild or delete them would fail visibly instead of
breaking its neighbours. Each runner has its own build/<name> folder or a private temporary
directory (tests/README-SPEED.md lists how collisions were removed); EXCLUSIVE runners run alone,
after the others. Never run two suites in one tree (they share build/host). A runner gets
--reuse-dsp when it (or tests/hd_runner.py, which it uses) supports it. The
configuration reaches every compiler call through tests/host_target.py (FM-1 without
polyphony: no change at all; otherwise -DBR_PRODUCT=... and -DBR_CAP_POLY=... as selected).

Runners in LOGIC (UI, screens, navigation, save, MIDI routing, metadata) are compiled at -O0,
the rest at -O2 as before (tests/host_target.py with_class; --opt O2 compiles everything at -O2;
--sanitize adds -fsanitize=address,undefined to the logic runners and reports what it finds).

QUICK MODE: --changed [BASE] runs only the runners the changed files can affect (tests/suite_changed.py;
--dry-run shows the choice). Never a substitute for the full suite: its result is
build/host-suite-<config>-quick.json with summary.mode "quick", and it exits 3 when the chosen
runners match. Every full run records the dependency map it uses (build/suite-deps-<config>.json).
Function level: --record-funcs builds each runner with clang's -fprofile-instr-generate and stores the
firmware functions every passing runner executed (build/suite-funcs-<config>.json; with --only it merges
into the existing map, so it can be built up in pieces); --changed and --for then select by what runners
execute (tests/suite_changed.py). --for FILE[,FILE] selects as if those whole files changed; --area
NAME[,NAME] runs a named GROUPS group (suite_changed.GROUPS). Both combine with --dry-run.

A runner that has no meaning in a configuration is a declared skip (SKIP below, each with its
reason; status "skip", never run), not a silent pass.

A runner is retried (up to 4 attempts, 15 s apart) when it fails with a message of a flaky
host compile (FLAKY: the system's open-file table exhausted, "error reading", a system header
"file not found"); with --jobs above 1 the retry is re-queued and the suite's parallelism is
halved for the rest of the run (back-off), so file-handle pressure never becomes a false
failure. Each result records runner, args, exit code, status and the last output line; the
JSON is written after every runner (default build/host-suite-<config>.json, full logs next to
it), its runners sorted by name whatever order they finished in.

Status: "pass" (exit 0), "fail", "timeout", "skip" (declared, with its reason) or "needs
arguments" (argparse exit 2 for missing required arguments: such runners need inputs that the
suite cannot supply). The status set is compared with the baseline (default
tests/baselines/host-<config>.json; the WP0 files work too). Every remaining failure has a
written reason in tests/baselines/KNOWN-FAILURES.md. Any change, an added or a missing runner exits 1. A baseline
entry with "status": "unknown" is reported but not compared.
"""
import argparse
import collections
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import host_target  # noqa: E402
import suite_changed  # noqa: E402

# Messages of a flaky host compile (the machine, not the code): the open-file table exhausted
# (a system process holds most of kern.num_files here), and the clang errors the same pressure
# produces: "error reading 'x'" and a system header "'x.h' file not found".
FLAKY = re.compile(r"Too many open files|ENFILE|EMFILE|Resource temporarily unavailable"
                   r"|error reading '|fatal error: '[^']+' file not found")
ENFILE = FLAKY                                # the old name
NEEDS_ARGS = re.compile(r'the following arguments are required')

# Declared skips: runner -> [(applies(config), reason)]. The first that applies decides.
# config is a host_target.Config(target, poly).
def _fm1(c): return c.target == 'fm1'
def _no_poly_support(c): return c.target == 'fm1' and not c.poly    # product.h BR_POLY_SUPPORT == 0


SMK_ONLY = 'SMK-37 Pro hardware (faders, track arrows, pads, loader map): no FM-1 meaning'
POLY_RUNNER = ('needs BR_POLY_SUPPORT objects (product.h: SMK-37 always, FM-1 only with polyphony): '
               'FM-1 runs it in the fm1 configuration')
SKIP = {
    'run_smk_faders.py': [(_fm1, SMK_ONLY)], 'run_smk_track_arrows.py': [(_fm1, SMK_ONLY)],
    'run_smk37_units.py': [(_fm1, SMK_ONLY)],
    'run_smk_fader_link.py': [(_fm1, SMK_ONLY)],
    'run_poly_render.py': [(_no_poly_support, POLY_RUNNER)],
    'run_poly_scheduler.py': [(_no_poly_support, POLY_RUNNER)],
    'run_poly_two_cores.py': [(_no_poly_support, POLY_RUNNER)],
    'run_model_poly_scheduler.py': [(_no_poly_support, POLY_RUNNER)],
    'run_poly_units.py': [(_no_poly_support, POLY_RUNNER)],
    'run_poly_ui.py': [(_no_poly_support, POLY_RUNNER)],
    'run_adaptive_poly_engine.py': [(_no_poly_support, POLY_RUNNER)],
    'run_home_poly_cue.py': [(_no_poly_support, POLY_RUNNER)],
    'run_lpg_release_audio.py': [(_no_poly_support, POLY_RUNNER)],
    'run_plaits_regression.py': [(_no_poly_support,
                                  'compares the BR_POLY_SUPPORT (compact state) Plaits voice with HEAD: '
                                  'FM-1 runs it in the fm1 configuration')],
}


def skip_reason(name, cfg):
    for applies, reason in SKIP.get(name, ()):
        if applies(cfg):
            return reason
    return None


# Runners whose own objects must be built in the run (their --reuse-dsp means "my
# objects from an earlier run", which a fresh tree does not have).
NO_REUSE = {'run_poly_two_cores.py'}
# The runner of this suite itself and helpers that are not test runners.
NOT_RUNNERS = {'run_suite.py'}


def status_of(exit_code, last_line):
    if exit_code == 'skip':
        return 'skip'
    if exit_code == 0:
        return 'pass'
    if exit_code == 'timeout':
        return 'timeout'
    if exit_code == 2 and NEEDS_ARGS.search(last_line or ''):
        return 'needs arguments'
    return 'fail'


def baseline_status(path):
    data = json.loads(Path(path).read_text())
    mode = (data.get('summary') or {}).get('mode', 'full')
    if mode != 'full':
        raise SystemExit(f'{path} is a {mode} run (summary.mode), not a baseline')
    out = {}
    for r in data['runners']:
        out[r['runner']] = r.get('status') or status_of(r['exit_code'], r.get('last_line', ''))
    return out


def compare(results, baseline):
    """-> list of (runner, baseline status, now) for every difference"""
    now = {r['runner']: r['status'] for r in results}
    diffs = []
    for name in sorted(set(now) | set(baseline)):
        was, got = baseline.get(name, 'absent'), now.get(name, 'absent')
        if was == 'unknown':
            continue
        if was != got:
            diffs.append((name, was, got))
    return diffs


def run(cmd, env, timeout):
    t0 = time.time()
    try:
        r = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout,
                           errors='replace')
        return r.returncode, r.stdout + r.stderr, time.time() - t0
    except subprocess.TimeoutExpired as e:
        o = (e.stdout or b'') + (e.stderr or b'')
        o = o.decode(errors='replace') if isinstance(o, bytes) else o
        return 'timeout', o, time.time() - t0


def run_retrying(cmd, env, timeout, retries=4, pause=15):
    for attempt in range(1, retries + 1):
        rc, out, secs = run(cmd, env, timeout)
        if rc == 0 or not FLAKY.search(out):
            break
        if attempt < retries:
            time.sleep(pause)
    return rc, out, secs, attempt


def default_jobs():
    """the number of performance cores (macOS hw.perflevel0.physicalcpu), else physical cores"""
    for key in ('hw.perflevel0.physicalcpu', 'hw.physicalcpu'):
        try:
            n = int(subprocess.run(['sysctl', '-n', key], capture_output=True, text=True).stdout)
            if n > 0:
                return n
        except (OSError, ValueError):
            pass
    return max(1, (os.cpu_count() or 2) // 2)


# Runners that must have the machine to themselves (they run after the others, one at a time).
# Each with its reason; tests/README-SPEED.md explains how every other collision was removed.
# The model scheduler uses the isolated objects built by run_poly_two_cores.
# Run it after the shared runners so its prerequisite has finished.
EXCLUSIVE = {'run_model_poly_scheduler.py'}

# Optimisation class (tests/host_target.py with_class): "logic" runners compile at -O0, the rest
# ("audio": anything that renders or measures audio, golden hashes, laws, timing, the DSP
# regressions) as the runner says, -O2. A runner is "logic" only if its status and last line at
# -O0 equal those at -O2 (three runs each, fm1 and smk37; tests/README-SPEED.md); when unsure, audio.
LOGIC = {
    'run_arrangement_strip.py',
    'run_control_spacing_hd.py',
    'run_filter_chart_height_hd.py',
    'run_firmware_metadata.py',
    'run_fm1_install.py',
    'run_font_fit.py',
    'run_gfx_hd.py',
    'run_global_shortcuts.py',
    'run_guarded_repair_real_v4.py',
    'run_hd_text.py',
    'run_home_algo_hd.py',
    'run_keyboard_modes.py',
    'run_late_drop_protocol.py',
    'run_lfo_edit_navigation.py',
    'run_lfo_target_icons_hd.py',
    'run_midi_app_regression.py',
    'run_midi_channel_ui_hd.py',
    'run_midi_clock_bias.py',
    'run_midi_clock_bridge.py',
    'run_midi_clock_continue.py',
    'run_midi_clock_control.py',
    'run_midi_clock_diag.py',
    'run_midi_clock_engine.py',
    'run_midi_clock_resilience.py',
    'run_midi_kit.py',
    'run_midi_routes.py',
    'run_midi_session_v16.py',
    'run_mix_icon_hd.py',
    'run_mono_priority.py',
    'run_patch_track_color_hd.py',
    'run_pattern_arranger_app.py',
    'run_pattern_delete_protocol_runtime.py',
    'run_pattern_export_runtime.py',
    'run_pattern_fixed_runtime.py',
    'run_pattern_foreign_reclaim.py',
    'run_pattern_import_runtime.py',
    'run_pattern_only_project.py',
    'run_pattern_panel_validation.py',
    'run_pattern_raw_delete_runtime.py',
    'run_pattern_raw_export_runtime.py',
    'run_pattern_salvage_runtime.py',
    'run_pattern_save_cut_runtime.py',
    'run_pattern_sector_diagnostic.py',
    'run_pattern_sector_panel_combined.py',
    'run_pattern_target_ui_lock2.py',
    'run_pattern_v16_only.py',
    'run_product_switch.py',
    'run_product_tests.py',
    'run_project_device_adapter.py',
    'run_project_list.py',
    'run_project_store.py',
    'run_reader.py',
    'run_retrig_ui.py',
    'run_route_badges_hd.py',
    'run_save_cue_stability.py',
    'run_save_led_stability.py',
    'run_save_ux_1953.py',
    'run_screen_map.py',
    'run_screen_transitions_hd.py',
    'run_seq_defaults.py',
    'run_seq_leds.py',
    'run_seq_top_gap_hd.py',
    'run_session_old_store.py',
    'run_session_protection.py',
    'run_session_retrig.py',
    'run_session_v5_store.py',
    'run_session_wire.py',
    'run_shape_symmetry_hd.py',
    'run_smk37_units.py',
    'run_smk_fader_link.py',
    'run_smk_faders.py',
    'run_smk_track_arrows.py',
    'run_startup_art.py',
    'run_track_patch_home_hd.py',
    'run_track_redraw.py',
    'run_ui_polish_hd.py',
    'run_ui_tidy_hd.py',
    'run_ui_timing_diag.py',
    'run_v4_migration_import.py',
    'run_validate_package_padding.py',
}
# Every runner was also tried at -O0 (--opt O0, fm1 and smk37): all but one gave the -O2 status and
# last line. Audio runners stay at -O2 by rule; this one could not move in any case:
NOT_LOGIC = {
    'run_preflight_retry_engine.py': 'links only at -O2: instrument_drums.c br_drum_lane_scheduler_state_ex() '
                                     'calls braids_perc_initialized(), which the test does not stub; -O2 '
                                     'drops the unused call, -O0 keeps it (undefined symbol)',
}


def opt_class(name):
    return 'logic' if name in LOGIC else 'audio'


SANITIZER_REPORT = re.compile(r'runtime error:|ERROR: AddressSanitizer|ERROR: LeakSanitizer|SUMMARY: (?:Address|Undefined)')


DSP_DIRS = ('build/host/braids', 'build/host/plaits_reference')


def lock_dsp(locked):
    """make the shared host DSP objects read-only (locked) or writable again; returns their state
    (path -> (size, mtime)) so the suite can prove no runner changed them"""
    state = {}
    for d in DSP_DIRS:
        d = ROOT / d
        if not d.is_dir():
            continue
        for f in sorted(d.iterdir()):
            f.chmod(0o444 if locked else 0o644)
            s = f.stat()
            state[str(f.relative_to(ROOT))] = (s.st_size, s.st_mtime_ns)
        d.chmod(0o555 if locked else 0o755)
    return state


def reuse_args(runner):
    """--reuse-dsp for a runner that supports it (itself or through tests/hd_runner.py)"""
    if runner.name in NO_REUSE:
        return []
    text = runner.read_text(errors='replace')
    if '--reuse-dsp' in text or re.search(r'^from hd_runner import', text, re.M):
        return ['--reuse-dsp']
    return []


class Scheduler:
    """Run runners `limit` at a time; a flaky failure is re-queued (up to `retries` attempts,
    not before `pause` seconds) and halves the limit for the rest of the run."""

    def __init__(self, limit, retries, pause, launch, done):
        self.limit, self.retries, self.pause = limit, retries, pause
        self.serial = limit == 1
        self.launch, self.done = launch, done          # launch(item) -> result; done(item, result, attempts)
        self.backoffs = []                             # (runner, new limit)
        self.cv = threading.Condition()

    def run(self, items):
        queue = collections.deque((it, 1, 0.0) for it in items)   # (item, attempt, not before)
        running, finished = 0, []

        def work(item, attempt):
            res = self.launch(item)
            with self.cv:
                finished.append((item, attempt, res))
                self.cv.notify_all()

        with self.cv:
            while queue or running:
                now = time.time()
                started = False
                while queue and running < self.limit and queue[0][2] <= now:
                    item, attempt, _ = queue.popleft()
                    running += 1
                    threading.Thread(target=work, args=(item, attempt), daemon=True).start()
                    started = True
                if started:
                    continue
                if not finished:
                    wait = None
                    if queue and running < self.limit:
                        wait = max(0.0, queue[0][2] - now)
                    self.cv.wait(timeout=wait if wait is not None else 1.0)
                    continue
                item, attempt, res = finished.pop(0)
                running -= 1
                rc, out = res[0], res[1]
                if rc != 0 and FLAKY.search(out) and attempt < self.retries:
                    if self.limit > 1:
                        self.limit = max(1, self.limit // 2)
                        self.backoffs.append((item.name, self.limit))
                    retry = (item, attempt + 1, time.time() + self.pause)
                    if self.serial:                    # --jobs 1: the old pause, then the retry
                        queue.appendleft(retry)
                    else:                              # others carry on meanwhile
                        queue.append(retry)
                    continue
                self.done(item, res, attempt)


def runners_for(cfg, only=()):
    names = []
    for p in sorted((ROOT / 'tests').glob('run_*.py')):
        if p.name in NOT_RUNNERS:
            continue
        if only and p.name not in only and p.stem not in only:
            continue
        names.append(p)
    return names


def quick_selection(cfg, changed, for_files=(), areas=()):
    """QUICK MODE: choose the runners (tests/suite_changed.py) and print the choice. `changed` is the
    --changed BASE (None: not given), for_files the --for files, areas the --area group names."""
    names = [r.name for r in runners_for(cfg) if not skip_reason(r.name, cfg)]
    base, files, chosen, why, causes = None, [], set(), {}, []
    if changed is not None:
        base, files = suite_changed.changed_files(changed or None)
        lines = suite_changed.changed_lines(base, files)
        chosen, why, causes = suite_changed.select_ex(files, names, lines=lines, base=base)
    if for_files:
        c2, w2, cs2 = suite_changed.select_ex(list(for_files), names, whole=set(for_files), base=base)
        causes += cs2
        chosen |= c2
        why.update({f'--for {k}': v for k, v in w2.items()})
        files = sorted(set(files) | set(for_files))
    if areas:
        c3 = suite_changed.group_runners(areas, names)
        chosen |= c3
        why[f'--area {",".join(areas)}'] = f'group(s): {len(c3)} runner(s)'
        causes.append((f'--area {",".join(areas)}', len(c3)))
    quick = {'base': base, 'changed_files': files, 'why': why,
             'chosen': sorted(chosen), 'skipped': sorted(set(names) - chosen), 'of': len(names)}
    print(f'QUICK MODE: base {(base or "-")[:12]}, {len(files)} changed file(s)', flush=True)
    for f, w in sorted(why.items()):
        print(f'  {f}: {w}')
    broad = suite_changed.broad_line(chosen, causes)
    if broad:
        print('  ' + broad)
    print(f'  chosen ({len(chosen)}): ' + ' '.join(sorted(chosen)))
    print(f'  skipped ({len(quick["skipped"])}): ' + ' '.join(quick['skipped']), flush=True)
    return quick


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--config', choices=host_target.CONFIGS, default='fm1',
                    help='a named configuration (default fm1: FM-1 with polyphony, the device default)')
    ap.add_argument('--baseline', help='baseline JSON (default tests/baselines/host-<config>.json); "none" skips')
    ap.add_argument('--out', help='result JSON (default build/host-suite-<config>.json)')
    ap.add_argument('--only', default='', help='comma-separated runner names (comparison then covers only these); with '
                                               '--changed/--for/--area: run only the chosen runners among these')
    ap.add_argument('--timeout', type=int, default=2400, help='seconds per runner')
    ap.add_argument('--jobs', type=int, default=0,
                    help='runners at a time (default: the number of performance cores; 1: one at a time, '
                         'in name order)')
    ap.add_argument('--opt', choices=('class', 'O2', 'O0'), default='class',
                    help='class (default): logic runners at -O0, audio runners at -O2 (LOGIC); '
                         'O2: every runner at -O2, as before the classes; O0: every runner at -O0 '
                         '(to find the runners whose result depends on the level)')
    ap.add_argument('--sanitize', action='store_true',
                    help='also build the logic runners with -fsanitize=address,undefined (findings are '
                         'reported, not the default)')
    ap.add_argument('--changed', nargs='?', const='', default=None, metavar='BASE',
                    help='QUICK MODE: run only the runners the files changed since BASE (default: git '
                         'merge-base HEAD integ, plus uncommitted and untracked files) can affect '
                         '(tests/suite_changed.py). Never a substitute for the full suite.')
    ap.add_argument('--for', dest='for_files', default='', metavar='FILE[,FILE]',
                    help='QUICK MODE: select as if these whole files changed (runners executing any function '
                         'of a firmware file; otherwise the --changed file rule)')
    ap.add_argument('--area', default='', metavar='NAME[,NAME]',
                    help='QUICK MODE: run the named runner group(s) of tests/suite_changed.py GROUPS: '
                         + ', '.join(suite_changed.GROUPS))
    ap.add_argument('--dry-run', action='store_true',
                    help='with --changed, --for or --area: print the runners it would choose and skip, run nothing')
    ap.add_argument('--record-funcs', action='store_true',
                    help='record which firmware functions each passing runner executes '
                         '(build/suite-funcs-<config>.json, merged with --only; function-level quick mode)')
    ap.add_argument('--no-record-deps', action='store_true',
                    help='do not record the dependency map quick mode uses (recorded by every full run)')
    ap.add_argument('--retry-pause', type=int, default=15, help=argparse.SUPPRESS)
    a = ap.parse_args(argv)
    for_files = [f.strip() for f in a.for_files.split(',') if f.strip()]
    areas = [x.strip() for x in a.area.split(',') if x.strip()]
    bad = [x for x in areas if x not in suite_changed.GROUPS]
    if bad:
        ap.error(f'--area: unknown group {", ".join(bad)} (known: {", ".join(suite_changed.GROUPS)})')
    targeted = a.changed is not None or bool(for_files) or bool(areas)
    if a.dry_run:
        if not targeted:
            ap.error('--dry-run needs --changed, --for or --area')
        quick_selection(host_target.CONFIGS[a.config], a.changed, for_files, areas)
        return 0
    jobs = a.jobs if a.jobs > 0 else default_jobs()

    cfg = host_target.CONFIGS[a.config]
    name = a.config
    env = host_target.environment(cfg.target, cfg.poly)
    out = Path(a.out) if a.out else ROOT / (f'build/host-suite-{name}.json' if not targeted
                                            else f'build/host-suite-{name}-quick.json')
    out.parent.mkdir(parents=True, exist_ok=True)
    logs = out.parent / (out.stem + '-logs')
    logs.mkdir(exist_ok=True)
    only = {n.strip() for n in a.only.split(',') if n.strip()}

    res = {'tree': str(ROOT), 'config': name, 'target': cfg.target, 'poly': cfg.poly,
           'product_flags': host_target.product_flags(cfg.target, cfg.poly),
           'extra_defines': env.get(host_target.ENV_EXTRA_DEFINES, '').split(),
           'started': time.strftime('%Y-%m-%d %H:%M:%S'), 'jobs': jobs, 'opt': a.opt, 'sanitize': a.sanitize,
           'dsp_build': None, 'runners': []}
    try:                                               # the last run's times, to start the longest first
        prev = {r['runner']: r.get('seconds', 0) for r in json.loads(out.read_text()).get('runners', [])}
    except (OSError, ValueError, AttributeError):
        prev = {}
    t_start = time.time()
    lock_dsp(False)                                    # a previous run interrupted while locked
    rc, o, s, n = run_retrying(['sh', 'tools/build_braids_host.sh'], env, 3600, pause=a.retry_pause)
    (logs / 'build_braids_host.log').write_text(o)
    res['dsp_build'] = {'exit_code': rc, 'seconds': round(s, 1), 'attempts': n,
                        'last_line': (o.strip().splitlines() or [''])[-1][:300]}
    out.write_text(json.dumps(res, indent=1))
    if rc != 0:
        print(f'host DSP build failed (rc={rc}); see {logs / "build_braids_host.log"}', file=sys.stderr)
        return 1

    lock = threading.Lock()
    serial = jobs == 1
    todo, results = [], {}

    def record(entry):
        with lock:
            results[entry['runner']] = entry
            res['runners'] = [results[k] for k in sorted(results)]
            out.write_text(json.dumps(res, indent=1))

    def report(entry, n_done, total):
        st, s, n = entry['status'], entry['seconds'], entry['attempts']
        if entry['status'] == 'skip':
            line = f'{entry["runner"]:48s} {"skip":16s}     -  ({entry["reason"]})'
        else:
            line = f'{entry["runner"]:48s} {st:16s} {s:5.0f}s' + (f'  (attempts {n})' if n > 1 else '')
        if serial:
            print(line, flush=True)
        else:                                          # progress; the sorted table follows at the end
            print(f'[{n_done:3d}/{total}] {line}', file=sys.stderr, flush=True)

    for runner in runners_for(cfg, only):
        why = skip_reason(runner.name, cfg)
        if why:
            record({'runner': runner.name, 'args': [], 'exit_code': 'skip', 'status': 'skip',
                    'reason': why, 'last_line': '', 'seconds': 0, 'attempts': 0})
            continue
        todo.append(runner)
    quick = None
    if targeted:
        quick = res['quick'] = quick_selection(cfg, a.changed, for_files, areas)
        todo = [r for r in todo if r.name in quick['chosen']]
        if only:
            print(f'--only: {len([r for r in todo])} of the {len(quick["chosen"])} chosen runner(s) are among the named ones; '
                  'the others are not run', flush=True)
        for k in [k for k, e in results.items() if e['status'] == 'skip']:
            del results[k]                             # declared skips are not part of a quick run
        res['runners'] = [results[k] for k in sorted(results)]
    # full runs record the dependency map; a partial run (--only, quick mode) only with --record-funcs (merged per
    # runner). Measured: recording costs about 30 % wall time on six runners, so it is opt-in there.
    record_deps = not a.no_record_deps and (a.record_funcs or (quick is None and not only))
    deps_dir = out.parent / (out.stem + '-deps')
    deps = {}
    if record_deps:
        deps_dir.mkdir(exist_ok=True)
    funcs, funcs_dir, fw_names = {}, out.parent / (out.stem + '-funcs'), None
    if a.record_funcs:
        fw_names = suite_changed.firmware_function_names()
        funcs_dir.mkdir(exist_ok=True)
    total = len(todo)
    counter = [0]

    def runner_class(runner):
        return {'O2': 'audio', 'O0': 'logic'}.get(a.opt) or opt_class(runner.name)

    def launch(runner):
        args = reuse_args(runner)
        cls = runner_class(runner)
        renv = host_target.with_class(env, cls, sanitize=a.sanitize and cls == 'logic')
        if record_deps:
            log = deps_dir / (runner.stem + '.log')
            log.write_text('')
            renv = suite_changed.recording_env(renv, log)
        pdir = None
        if a.record_funcs:                             # instrumented compiles; profiles merged right here
            pdir = funcs_dir / runner.stem
            shutil.rmtree(pdir, ignore_errors=True)
            pdir.mkdir()
            renv = suite_changed.funcs_env(renv, pdir / '%p-%m.profraw')
        rc, o, s = run([sys.executable, f'tests/{runner.name}', *args], renv, a.timeout)
        if pdir is not None:
            if rc == 0:                                # a failing runner may stop before it executes all
                names = suite_changed.executed_functions(pdir, *fw_names)
                if names is None:
                    o += f'\n[record-funcs] no profile or no llvm-profdata for {runner.name}: not recorded'
                else:
                    funcs[runner.name] = names
            shutil.rmtree(pdir, ignore_errors=True)
        return rc, o, s, args

    def done(runner, result, attempts):
        rc, o, s, args = result
        (logs / (runner.stem + '.log')).write_text(o)
        lines = [l for l in o.strip().splitlines() if l.strip()]
        last = (lines[-1] if lines else '')[:300]
        entry = {'runner': runner.name, 'args': args, 'exit_code': rc, 'status': status_of(rc, last),
                 'last_line': last, 'seconds': round(s, 1), 'attempts': attempts, 'opt': runner_class(runner)}
        if a.sanitize and entry['opt'] == 'logic':
            entry['sanitizer'] = sorted({l.strip()[:200] for l in o.splitlines() if SANITIZER_REPORT.search(l)})
        record(entry)
        if record_deps:
            deps[runner.name] = suite_changed.read_log(deps_dir / (runner.stem + '.log'), runner.name)
        counter[0] += 1
        report(entry, counter[0], total)

    dsp_before = lock_dsp(True)
    backoffs = []
    try:
        if serial:                                     # in name order; skips print in their place
            for runner in runners_for(cfg, only):
                if runner.name in results:
                    report(results[runner.name], 0, total)
                elif runner in todo:
                    Scheduler(1, 4, a.retry_pause, launch, done).run([runner])
        else:
            shared = [r for r in todo if r.name not in EXCLUSIVE]
            # longest first (by the last result in this tree, if any) so the tail stays short
            shared.sort(key=lambda r: -float(prev.get(r.name, 0) or 0))
            sched = Scheduler(jobs, 4, a.retry_pause, launch, done)
            sched.run(shared)
            backoffs = list(sched.backoffs)
            for runner in [r for r in todo if r.name in EXCLUSIVE]:
                Scheduler(1, 4, a.retry_pause, launch, done).run([runner])
    finally:
        dsp_after = lock_dsp(False)
    if dsp_after != dsp_before:
        res['dsp_changed'] = sorted(k for k in set(dsp_before) | set(dsp_after)
                                    if dsp_before.get(k) != dsp_after.get(k))
    if record_deps and deps:
        suite_changed.save_map(name, deps)
    if a.record_funcs:
        if funcs:
            suite_changed.save_funcs(name, funcs)
        print(f'record-funcs: {len(funcs)} of {total} runner(s) recorded in '
              f'{suite_changed.funcs_map_path(name).relative_to(ROOT)} (only passing runners are recorded)', flush=True)
    if not serial:
        print(f'host suite {name}: {total} runners, --jobs {jobs}', flush=True)
        for k in sorted(results):
            e = results[k]
            if e['status'] == 'skip':
                print(f'{k:48s} {"skip":16s}     -  ({e["reason"]})')
            else:
                print(f'{k:48s} {e["status"]:16s} {e["seconds"]:5.0f}s'
                      + (f'  (attempts {e["attempts"]})' if e['attempts'] > 1 else ''))
    res['backoffs'] = [{'runner': r, 'jobs_after': j} for r, j in backoffs]
    res['wall_seconds'] = round(time.time() - t_start, 1)

    rs = res['runners']
    res['summary'] = {k: sum(r['status'] == k for r in rs) for k in ('pass', 'fail', 'needs arguments', 'timeout', 'skip')}
    res['summary']['total'] = len(rs)
    res['summary']['mode'] = 'quick' if quick else ('partial' if only else 'full')
    res['finished'] = time.strftime('%Y-%m-%d %H:%M:%S')

    base_path = a.baseline or str(ROOT / f'tests/baselines/host-{name}.json')
    code = 0
    if base_path != 'none':
        base = baseline_status(base_path)
        if only or quick:
            base = {k: v for k, v in base.items() if k in {r['runner'] for r in rs}}
        diffs = compare(rs, base)
        res['baseline'] = {'file': base_path, 'differences': [
            {'runner': n, 'baseline': w, 'now': g} for n, w, g in diffs]}
        unknown = sorted(k for k, v in base.items() if v == 'unknown')
        print(json.dumps(res['summary']))
        for n, w, g in diffs:
            print(f'CHANGED {n}: baseline {w}, now {g}')
        for n in unknown:
            print(f'unknown in baseline (not compared): {n} now {next((r["status"] for r in rs if r["runner"] == n), "absent")}')
        print(f'{"MATCHES" if not diffs else "DIFFERS FROM"} baseline {base_path}')
        code = 1 if diffs else 0
    else:
        print(json.dumps(res['summary']))
        # Skipping a historical baseline must not turn failed checks into a
        # successful exit status. Declared skips and argument-only tools are
        # still reported separately in the summary.
        code = 1 if res['summary']['fail'] or res['summary']['timeout'] else 0
    if a.sanitize:
        found = [r for r in rs if r.get('sanitizer')]
        print(f'sanitizers (logic runners, -O0 -fsanitize=address,undefined): {len(found)} runner(s) with reports')
        for r in found:
            print(f'  {r["runner"]}: ' + ' | '.join(r['sanitizer'][:3]))
    for r, j in backoffs:
        print(f'back-off: {r} failed on a flaky-compile message; parallelism now {j}')
    if quick:
        print(f'QUICK MODE: {len(quick["chosen"])} of {quick["of"]} runners; run the full suite before merging'
              + ('' if code else ' (exit 3: the chosen runners match; this is not a full pass)'))
        if code == 0:
            code = 3
    if res.get('dsp_changed'):
        print('SHARED DSP OBJECTS CHANGED during the run (a runner rebuilt or deleted build/host): '
              + ', '.join(res['dsp_changed'][:5]))
        code = 1
    out.write_text(json.dumps(res, indent=1))
    return code


if __name__ == '__main__':
    sys.exit(main())
