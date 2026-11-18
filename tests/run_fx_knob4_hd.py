#!/usr/bin/env python3
"""Verify the FX knob 4 pictures (DRIVE BIAS, DELAY TONE, MOD FEEDBACK, REVERB PRE DELAY) on the host: their
pages map them, they stay in their cells, follow their values and hold still; SEND stays on the sends."""
import host_target
host_target.activate(host_target.target())
from hd_runner import main

if __name__ == '__main__':
    main('fx_knob4', __doc__)
