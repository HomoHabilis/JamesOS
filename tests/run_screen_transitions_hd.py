#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HD screen transitions equal a clean draw; fader values leave nothing behind. (tests/test_screen_transitions_hd.c; host only, never hardware)."""
from hd_runner import main

if __name__ == '__main__':
    main('screen_transitions', __doc__)
