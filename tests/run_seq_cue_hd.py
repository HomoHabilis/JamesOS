#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sequencer bottom cue: alternates SHIFT NOTES / step-bank paging every 3 s when the track has 2+ banks. (tests/test_seq_cue_hd.c; host only, never hardware)."""
from hd_runner import main

if __name__ == '__main__':
    main('seq_cue', __doc__)
