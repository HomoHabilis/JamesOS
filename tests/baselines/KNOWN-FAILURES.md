# Known host-suite failures

Every runner that does not pass in a suite configuration (`python3 tests/run_suite.py --config NAME`;
baselines `tests/baselines/host-<config>.json`) is listed here with its reason. Configurations (all
the HD interface, the only one): `fm1` (FM-1 with polyphony, the device default), `fm1-nopoly`,
`smk37` (SMK-37 Pro with polyphony, the device default), `smk37-nopoly`. Declared skips (a runner
without meaning in a configuration) are not failures: their reasons are in `tests/run_suite.py`
(`SKIP`) and in each result JSON. Baselines are cut on an export (no `.git`).

## Needs arguments (all configurations)

- `run_plaits_kit_mix_hoist.py`: `--baseline` (a tree with the original kit code to compare against).
- `run_session_pcm.py`: `SESSION.bin OUT.pcm` (renders a saved session for build-to-build comparison).
- `run_take_pattern_capacity.py`: `--take` (a captured take to measure).

## Needs a git checkout or a tool not installed

- `run_plaits_regression.py` (`fm1`, `smk37`, `smk37-nopoly`; skipped in `fm1-nopoly`, where
  BR_POLY_SUPPORT is 0): runs `git show HEAD:firmware/plaits/plaits_c_api.cc`; an export has no
  `.git`. Passes in a checkout.
- `run_hd_text.py` (all): the generator check `tools/gen_hd_font.py --check` needs fontTools, not
  installed on this Mac. The C specimen test itself passes (`--skip-gen-check`: OK, 0 failures);
  the Pillow preview is optional.
