#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HD home page: the Plaits model chip leads the top-bar title on SOUND screen 0; the band above the controls stays clear. (tests/test_home_algo_hd.c; host only, never hardware)."""
from hd_runner import main

if __name__ == '__main__':
    main('home_algo', __doc__)
