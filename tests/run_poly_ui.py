#!/usr/bin/env python3
"""Polyphony through the real UI and engine (tests/test_poly_ui.c), FM-1 or SMK-37 Pro; never touches hardware.

    python3 tests/run_poly_ui.py --target fm1|smk37

The host DSP objects (build/host) must be built with the same product defines
(PLAITS_CXXFLAGS/BRAIDS_CXXFLAGS for tools/build_braids_host.sh): FM-1
-DBR_PRODUCT=BR_PRODUCT_ID_FM1 -DBR_CAP_POLY=1, SMK-37 -DBR_PRODUCT=BR_PRODUCT_ID_SMK37."""
import argparse, subprocess
import host_target
from run_drive import FLAGS, ROOT

PRODUCT = {'fm1': ['-DBR_PRODUCT=BR_PRODUCT_ID_FM1', '-DBR_CAP_POLY=1'],
           'smk37': ['-DBR_PRODUCT=BR_PRODUCT_ID_SMK37']}

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--target', choices=sorted(PRODUCT), default=host_target.target(),
                    help='default: the selected host target (BR_HOST_TARGET, else fm1)')
    ap.add_argument('--reuse-dsp', action='store_true', help='accepted for the suite runner; DSP objects are never rebuilt here')
    a = ap.parse_args()
    # every compile with this product and polyphony (the suite's wrapper flags agree; HD kept)
    host_target.activate(a.target, with_poly=True)
    objects = sorted((ROOT / 'build/host/braids').glob('*.o')) + sorted((ROOT / 'build/host/plaits_reference').glob('*.o'))
    if not objects:
        raise SystemExit('No host DSP objects; run tools/build_braids_host.sh with the product defines first')
    out = ROOT / f'build/poly-ui-test-{a.target}'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'test_poly_ui'
    # run_drive's FLAGS carry the suite's product defines (e.g. -DBR_CAP_POLY=0): replace them
    flags = [f for f in FLAGS if not f.startswith(('-DBR_PRODUCT=', '-DBR_CAP_POLY='))]
    subprocess.run(['c++', '-x', 'c', *flags, *PRODUCT[a.target], '-DFM1_DEVELOPMENT_DIAGNOSTICS=0',
                    'tests/test_poly_ui.c', 'firmware/src/audio_fixed8_plan_unit.c', '-x', 'none',
                    *map(str, objects), '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)

if __name__ == '__main__':
    main()
