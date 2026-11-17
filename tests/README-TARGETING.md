# Targeted test runs

The full host suite is slow. Most changes need only the runners that execute the code you touched.
Use the smallest tier that is honest for the change.

## Tiers

1. Smoke. `SMOKE` in `tests/suite_changed.py` is added to every `--changed` and `--for` selection.
2. Targeted, for every change. Pick one:
   - `python3 tests/run_suite.py --changed HEAD --jobs 2` (the diff against HEAD, plus uncommitted files;
     use a branch point instead of HEAD to cover a whole branch). Add `--dry-run` to see the choice only.
   - `--for FILE[,FILE]`: select as if those whole files changed.
   - `--area NAME[,NAME]`: run a named group (effects, screens, save, midi, tools).
   - `--only a.py,b.py`: run exactly these. Together with `--changed/--for/--area` it keeps only the
     chosen runners among the named ones.
3. Full fm1: `python3 tests/run_suite.py --config fm1`. Before a hardware install.
4. All four configs (fm1, fm1-nopoly, smk37, smk37-nopoly). Before moving `unified` or making a release.

A targeted run exits 3 when its runners match. That is not a full pass.

## How a change is mapped to runners

- A changed line inside a firmware function selects the runners that executed that function.
- A changed line outside any function (macro, enum, table, struct) selects the runners that execute the
  functions whose bodies mention the identifier defined on that line (macros are followed one macro to
  the next). If no identifier can be found, or nothing mentions it, it selects every runner that executes
  any function of the file.
- A change that is only whitespace or comments selects nothing.
- A file with no recorded function (new file, header without functions) uses the older file rule: the
  dependency map and the area table.
- A runner with no recorded function list always runs.
- Files outside `firmware/src` and `firmware/hal` use the older rules. Suite and helper files select everything.
- The dry-run output says which rule was used, which functions were hit, and how many runners each selects.
  It prints a `BROAD:` line when more than 40 runners are chosen, naming the widest cause.

## How the map is recorded and kept fresh

- `--record-funcs` builds each runner with clang `-fprofile-instr-generate`, runs it, and stores the
  firmware functions with a nonzero count in `build/suite-funcs-<config>.json`. It also records the file
  dependency map for partial runs. Only runners that pass are recorded.
- It merges into the existing map, so build it in pieces: `--only a.py,b.py,c.py --jobs 2 --record-funcs`.
  It also works with `--changed`, `--for` and `--area`.
- It costs about 30 % more wall time on a small set (measured on six runners), so it is not on by default.
  A full run records the dependency map as before; add `--record-funcs` to refresh the function map too.
- Refresh after a change that moves what a runner executes, and after merging. A stale map can miss a
  runner that now executes a function.
- No function map: the targeted modes behave as they did before this feature.

## Known limits

- Selection is by function name. Two static functions with the same name in different files cannot be
  told apart; every runner that executes either is kept, and the dry-run says so.
- A compile error in code that no selected runner executes is not caught.
- A changed table or macro that is only used by another table or another non-function definition is
  not followed. It falls back to the file-level rule.
- Edits that only move a macro's value are found through identifiers; a change to an `#if` condition has
  no identifier and uses the file-level rule.
- Function ranges come from a text scanner (column-0 signatures and closing braces). Unusual layout can
  fool it. `tests/run_suite_targeting.py` checks it on real files.
- The scan of the firmware index costs about 2 to 4 seconds per dry run.
- Only the fm1 configuration was exercised. The map is per configuration; the union of recorded
  configurations is used.
