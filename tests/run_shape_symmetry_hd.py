#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""HD shapes: circles, lines, arcs mirror exactly; toggle track and puck; gate pulse symmetry. (tests/test_shape_symmetry_hd.c; host only, never hardware)."""
from hd_runner import main

if __name__ == '__main__':
    main('shape_symmetry', __doc__)
