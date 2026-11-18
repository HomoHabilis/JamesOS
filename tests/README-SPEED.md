# Host suite speed

`python3 tests/run_suite.py --config NAME` runs every `tests/run_*.py`. What makes it fast, and
what keeps the fast run equal to the old serial one.

## Parallel runners (`--jobs N`)

- Default: the number of performance cores (`hw.perflevel0.physicalcpu`, 8 here). `--jobs 1`
  is the old behaviour: one runner at a time, in name order, retries 30 s apart.
- The report and the JSON list runners sorted by name whatever order they finished in; progress
  lines (`[k/n] ...`) go to stderr. The baseline comparison is unchanged.
- The host DSP objects (`build/host/braids`, `build/host/plaits_reference`) are built once, before
  any runner, and are read-only while the runners run. A runner that tries to rebuild or delete
  them fails visibly; the suite also compares them before and after and fails on any change.
- Flaky host compiles (`FLAKY` in `run_suite.py`: "Too many open files", "error reading '…'",
  "'x.h' file not found") are retried up to 4 attempts; with `--jobs` above 1 the retry is
  re-queued and the parallelism halves for the rest of the run (`back-off:` lines, `backoffs` in
  the JSON).

Collisions found (a serial run that records every file each runner writes, plus reading every
runner) and how they were removed:

| Runners | Collision | Fix |
|---|---|---|
| the ten `*_hd` look tests | `hd_runner.py` supports `--reuse-dsp`, but the suite looked for it in the runner file only, so each rebuilt (and first deleted) the shared DSP objects: 10-15 s each, and a race for every other runner | the suite also passes `--reuse-dsp` to runners that use `hd_runner.py` |
| `run_smk_faders.py`, `run_smk_track_arrows.py` | always ran `tools/build_braids_host.sh` (deletes the shared objects) and built in `build/bank-test`, the folder of `run_bank.py` | `--reuse-dsp`; own folders `build/smk-faders-test`, `build/smk-track-arrows-test` |
| `run_midi_clock_diag.py`, `run_midi_clock_control.py` | same folder and binary names (`build/midi-clock-diag-test/{single,split,disabled}`) | control builds in `build/midi-clock-control-test` |

Everything else already has a private place: its own `build/<name>` folder, or a
`tempfile.TemporaryDirectory` (random name) for runners that copy or patch the tree
(`run_late_drop_protocol.py`, `run_session_protection.py`, `run_overrun_counter.py`,
`run_font_fit.py`, ...); `run_seq_behaviour.py` patches its own copy in `build/seq-behaviour/tree`.
No runner opens a network port; no runner writes a fixed name in `/tmp` or outside `build/`.
EXCLUSIVE (runs alone, at the end) is empty: no runner measures wall-clock time.


## Optimisation classes (`LOGIC` in `run_suite.py`)

Almost all of a runner's time is compiling the whole firmware (`dinghy.c`) as one unit at -O2
(8-10 s; at -O0 about 1 s). "Logic" runners (UI, screens, navigation, save and project store,
MIDI routing, metadata: nothing whose assertions depend on rendered audio or timing) are compiled
at -O0; every other runner ("audio") as before. One mechanism for all runners: the suite runs a
logic runner with `tests/host_cc` first on PATH and `BR_HOST_CFLAGS_LAST=-O0`, which the wrapper
appends after the runner's own flags (the last -O wins); nothing in the runners changes.
`--opt O2` compiles everything at -O2 as before (for comparisons).

A runner is in `LOGIC` only if, at -O0, its status AND last output line equal those at -O2 in
three runs on `fm1` and on `smk37`. Runners tried and kept at -O2 are in `NOT_LOGIC` with what
differed. `--sanitize` additionally builds the logic runners with
`-fsanitize=address,undefined` and lists every sanitizer report (not the default).

## Slow runners

- The ten `*_hd` look tests (23-36 s): one app compile each (8-10 s at -O2) plus a full rebuild of
  the shared DSP objects (about 15 s), because the suite did not pass `--reuse-dsp` through
  `hd_runner.py`. Fixed with the collisions above; at -O0 (logic class) each now takes 2-3 s.
- `run_wave_inflate_probe.py` (33-34 s): 240 launches of an ASan binary. The same 120 streams and
  their truncations now go through one process (`test_wave_inflate_probe.cc --batch`, the same
  buffer sizes, cleared per stream), plus six single-stream launches so that mode stays checked.
- `run_seq_behaviour.py` (51-56 s): copying and patching the tree takes 0.2 s; the time is four
  builds of the app (plain, clock, ASan at -O1, patterns), which ran one after the other. They are
  now built and run at the same time; output is printed in the same order. What remains is the
  ASan build and run (about 25 s).
- `run_env_times.py` (three rates), `run_filter_laws.py` (four programs),
  `run_midi_clock_diag.py`/`run_midi_clock_control.py` (three programs each),
  `run_overrun_counter.py` (four programs): the independent builds now run at the same time; the
  programs still run in the old order (filter laws: at the same time, as before). Same builds,
  same flags, same checks. `run_filter_laws.py` stays at about 15 s: its 44.1/48 kHz programs
  themselves run for about 12 s at -O2.

## Quick mode (`--changed [BASE]`)

`python3 tests/run_suite.py --config fm1 --changed` runs only the runners the files changed since
BASE (default `git merge-base HEAD integ`, plus uncommitted and untracked files) can affect. It
prints every changed file with the reason for its choice, the runners chosen and the runners
skipped, and ends with `QUICK MODE: n of m runners; run the full suite before merging`. Its
result goes to `build/host-suite-<config>-quick.json` with `summary.mode = "quick"` (a full run
says `"full"`; a quick or `--only` result is refused as a baseline) and it exits 3, not 0, when
the chosen runners match the baseline.

How it chooses (`tests/suite_changed.py`):
1. Every full run records what each runner reads (`build/suite-deps-<config>.json`): each
   compile is preprocessed once more with `-M -MG` by the `tests/host_cc` wrapper
   (`tests/depscan/ccdeps.py`), and every Python process of the runner reports the files and
   directories it opens, lists or copies (`tests/depscan/sitecustomize.py`, an audit hook). Paths
   in temporary or build copies map back to the tree.
2. Nearly every test compiles all of `dinghy.c`, so a declared table (`AREAS` -> `GROUPS`)
   narrows a firmware change to a group: effects, screens, save, midi; `tools/` adds the tool
   tests to whatever reads the tool. Changes to the suite, its helpers, `FLAGS`, baselines,
   `tools/build.py` or `tools/build_braids_host.sh` run everything; documentation runs nothing; a
   file neither in the table nor read by any runner runs everything. Without a map it runs the
   full suite (which records one).

`--changed --dry-run` prints the choice and runs nothing. Examples (fm1 map, `--dry-run`):
`firmware/src/instrument_fx.c` -> 25 effects runners; `firmware/src/instrument_ui.c` -> 28 screen
runners; `docs/DIAGNOSTICS.md` -> none; `tests/run_suite.py` or `tests/host_target.py` -> all 124;
`tests/test_drive.c` -> the 6 runners that compile it (run_drive plus the tree-copy runners).

## All four configurations at once

Two suites must never share a tree (they share `build/host` and every `build/<name>-test`), and
the runners hard-code `build/`, so the four configurations run in four trees, at the same time,
with fewer jobs each:

    B=/path/to/scratch; for c in fm1 fm1-nopoly smk37 smk37-nopoly; do
        rm -rf $B/$c && mkdir -p $B/$c && git archive HEAD | tar -x -C $B/$c; done
    for c in fm1 fm1-nopoly smk37 smk37-nopoly; do
        (cd $B/$c && python3 tests/run_suite.py --config $c --jobs 4 > $B/$c.out 2>&1) & done; wait
    tail -n 3 $B/*.out

(`git archive HEAD` tests the commit; for uncommitted work copy the tree instead, without
`build/`.) An export has no `.git`, so `run_plaits_regression.py` reads "fail" there, as in the
baselines.

## Unity builds (tests/unity_build.py)

Tests that `#include "../firmware/src/dinghy.c"` with the same lead (the lines up to that include,
plus a following `#undef main`) are compiled once per group instead of once per test.
`tests/host_cc/host-cc` hands a compile+link of one `tests/<name>.c` to `unity_build.py cc`, which
builds (under a file lock) `build/unity/<KEY>/unity_bin` containing every member of the group, each
with its clashing file-scope names renamed, and writes the requested output as a two-line shell
script `exec .../unity_bin <name> "$@"`. KEY covers the compiler, all flags, the object files'
contents, the lead, and the size/mtime of the members, firmware/ and tests/*.h, so any edit makes a
new binary. Members that do not compile in the unity are listed in `excluded.json` and built the
old way, as is every compile that is not a plain `tests/<name>.c` link (generated or copied
sources, `-c`, other trees). `build/unity/requests.log` records which test asked for which key;
cache directories untouched for two days are deleted.

A unity costs about as much as one or two ordinary compiles (-O2: about 8 s for 1 to 15 members,
12 s for all 58, against 6-7 s for one test), so it is only built where it pays. Every compile of a
candidate test (also with `BR_UNITY=0` or a fallback) records `test -> first run that asked` under its
FLAGKEY (compiler, flags without file contents, lead) in `build/unity/popularity.json` (history, never
purged). A unity holds the tests that asked for the same flags in EARLIER runs, and only those use it;
a test first seen in this run, or alone with its flags, compiles the old way. So the first run of a
changed flag set learns and the second benefits.

`BR_UNITY=0` turns it off (e.g. `BR_UNITY=0 python3 tests/run_suite.py ...`); `BR_UNITY=force` builds
a unity regardless of popularity.

### Proof before use (`unity_build.py verify`)

The shared binary is only used for a test that has been proven to behave like its own compile. The
proof belongs to the transformation, not to the firmware: it is recorded under a STRUCTKEY, a hash of
the flags (FLAGKEY, independent of the checkout path), `unity_build.py`, the contents of the tests in
the set and of `tests/*.h`. Nothing under `firmware/` is in it.

Three steps:

1. Learn: run the tests once (any run, `BR_UNITY` unset or 0). Each compile records which tests share
   which flags (`popularity.json`) and the command (`flagcmds.json`).
2. Verify: `python3 tests/unity_build.py verify --config fm1`. It builds each shared set and each test
   the old way, runs both with no arguments and compares exit code, stdout and stderr (2 compiles at a
   time). `build/unity/verified.json` gets `same`, `differs` or `unverified` for every test (unverified:
   the old binary exits non-zero, e.g. it needs arguments, and the results differ). `--changed-only`
   verifies only the STRUCTKEYs that are not in the file yet.
3. Served: `BR_UNITY=1 python3 tests/run_suite.py ...`. A test that is `same` under the current
   STRUCTKEY runs from the shared binary, built on first use for the current firmware (one compile per
   flag set after every firmware edit, no new proof needed). If that build fails, or the test is not
   proven, the old compile runs.

Editing a test, a `tests/*.h` or `unity_build.py` makes the STRUCTKEY new: those tests compile the old
way until `verify --changed-only` has run.
