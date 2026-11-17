#!/usr/bin/env python3
"""Verify the model and global LFO target artwork on the host."""
import os
os.environ['BR_HOST_EXTRA_DEFINES'] = (os.environ.get('BR_HOST_EXTRA_DEFINES', '') + ' -DBR_MODEL_MOD_EXPERIMENT=1').strip()
import host_target
host_target.activate(host_target.target())
from hd_runner import main

if __name__ == '__main__':
    main('lfo_target_icons', __doc__)
