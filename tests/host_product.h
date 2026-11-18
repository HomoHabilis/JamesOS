/* SPDX-License-Identifier: GPL-3.0-only */
/* Product macros for host tests that are compiled on their own (not through dinghy.c).
 * In the unified tree this is firmware/product.h, selected by -DBR_PRODUCT=... (FM-1 when
 * nothing is passed; tests/host_target.py). In a pre-unification SMK-37 tree (used only to
 * check that the merged tests are intact) there is no product.h: a BR_PRODUCT define on the
 * command line then means SMK-37 Pro. */
#ifndef BR_HOST_PRODUCT_H
#define BR_HOST_PRODUCT_H
#if __has_include("../firmware/product.h")
#include "../firmware/product.h"
#else
#ifdef BR_PRODUCT
#define BR_PRODUCT_SMK37 1
#define BR_PRODUCT_FM1 0
#else
#define BR_PRODUCT_SMK37 0
#define BR_PRODUCT_FM1 1
#endif
#define BR_CAP_PADS BR_PRODUCT_SMK37
#endif
#endif
