/* SPDX-License-Identifier: GPL-3.0-only */
/* Product selection: one switch for FM-1 (the default) and SMK-37 Pro.
 *
 * Select the product with exactly one compiler define:
 *     -DBR_PRODUCT=BR_PRODUCT_ID_FM1      (also the default when nothing is passed)
 *     -DBR_PRODUCT=BR_PRODUCT_ID_SMK37
 * tools/build.py --target {fm1,smk37} passes it, and force-includes this header into
 * every C/C++ translation unit (-include firmware/product.h). Host builds do not
 * force-include it: a source file that tests a product macro also includes it itself
 * ("../product.h" from src/, hal/ or loader/).
 *
 * Every BR_PRODUCT_* and BR_CAP_* flag below is always defined to 0 or 1: test it with
 * #if, never with #ifdef. Definitions only: including this header must not change a
 * single byte of any build (WP1 gate G-FM1).
 *
 * BR_CAP_POLY is the only capability a build may choose, and either product may choose
 * it (polyphony is a shared feature; the products differ in user interface only).
 * Defaults: SMK-37 1 (tools/build.py --no-polyphony passes -DBR_CAP_POLY=0), FM-1 0
 * until its flash is recovered (tools/build.py --target fm1 --polyphony passes
 * -DBR_CAP_POLY=1). It defines the existing internal switch
 * BR_POLY_SCHEDULER_EXPERIMENT. BR_POLY_SUPPORT selects the code polyphony depends on
 * (see below). */
#ifndef BR_PRODUCT_H
#define BR_PRODUCT_H

#define BR_PRODUCT_ID_FM1   1
#define BR_PRODUCT_ID_SMK37 2

#if defined(BR_PRODUCT_FM1) || defined(BR_PRODUCT_SMK37)
#error "select the product with -DBR_PRODUCT=BR_PRODUCT_ID_FM1 or BR_PRODUCT_ID_SMK37, not by defining BR_PRODUCT_FM1/BR_PRODUCT_SMK37"
#endif
#if defined(BR_CAP_PADS) || defined(BR_CAP_FADERS) || defined(BR_CAP_WHEELS) || \
    defined(BR_CAP_VELOCITY) || defined(BR_CAP_DIN_MIDI) || defined(BR_CAP_SUPPLY396)
#error "hardware capabilities follow from BR_PRODUCT; do not define BR_CAP_* (except BR_CAP_POLY) on the command line"
#endif

#ifndef BR_PRODUCT
#define BR_PRODUCT BR_PRODUCT_ID_FM1
#endif

#if BR_PRODUCT == BR_PRODUCT_ID_FM1
#define BR_PRODUCT_FM1   1
#define BR_PRODUCT_SMK37 0
#elif BR_PRODUCT == BR_PRODUCT_ID_SMK37
#define BR_PRODUCT_FM1   0
#define BR_PRODUCT_SMK37 1
#else
#error "unknown BR_PRODUCT: use BR_PRODUCT_ID_FM1 or BR_PRODUCT_ID_SMK37"
#endif

/* ---- identity and layout data (values as built today in each source tree) ---- */
#if BR_PRODUCT_SMK37
#define BR_PRODUCT_NAME              "SMK-37 Pro"
#define BR_PRODUCT_IDENTITY_PREFIX   "SMK-37 Pro_"            /* app identity: prefix + digits */
#define BR_PRODUCT_RESCUE_IDENTITY   "SMK-37 Pro_000"
#define BR_PRODUCT_LOADER_IDENTITY   "ota-SMK-37 Pro_9260001" /* loader.c FELUCCA_ID */
#define BR_PRODUCT_LOADER_RECORD     "ota-SMK-37 Pro_016"     /* ota.c parm record name */
#define BR_PRODUCT_APP_NAME          "JAMESOS-K"
#define BR_PRODUCT_SETTINGS_MAGIC    0x4B534F4Au              /* "JOSK" */
#define BR_PRODUCT_PLAY_KEYS         37u
#define BR_PRODUCT_APP_SLOT          0x97400u                 /* XIP length, app.bin slot */
#define BR_PRODUCT_APP_WINDOW_END    0x9D000u                 /* app window 0x4000..end */
#else
#define BR_PRODUCT_NAME              "FM-1"
#define BR_PRODUCT_IDENTITY_PREFIX   "FM-1_"
#define BR_PRODUCT_RESCUE_IDENTITY   "FM-1_000"
#define BR_PRODUCT_LOADER_IDENTITY   "ota-FM-1_904"
#define BR_PRODUCT_LOADER_RECORD     "ota-FM-1_015"
#define BR_PRODUCT_APP_NAME          "JAMESOS"
#define BR_PRODUCT_SETTINGS_MAGIC    0x59474E44u              /* "DNGY" */
#define BR_PRODUCT_PLAY_KEYS         27u
#define BR_PRODUCT_APP_SLOT          0x8DFBCu
#define BR_PRODUCT_APP_WINDOW_END    0x93000u
#endif

/* ---- hardware capabilities (fixed per product) ---- */
#define BR_CAP_PADS       BR_PRODUCT_SMK37   /* 16 RGB pads, LEDs on SPI1 */
#define BR_CAP_FADERS     BR_PRODUCT_SMK37
#define BR_CAP_WHEELS     BR_PRODUCT_SMK37   /* pitch bend and mod wheel */
#define BR_CAP_VELOCITY   BR_PRODUCT_SMK37   /* velocity-sensing keys */
#define BR_CAP_DIN_MIDI   BR_PRODUCT_FM1     /* UART (DIN) MIDI */
#define BR_CAP_SUPPLY396  BR_PRODUCT_FM1     /* 396 MHz supply step at boot (vendor360) */

/* ---- polyphony: build option for either product (default: SMK-37 on, FM-1 off) ---- */
#if !defined(BR_CAP_POLY) && defined(BR_POLY_SCHEDULER_EXPERIMENT)
#if BR_POLY_SCHEDULER_EXPERIMENT
#define BR_CAP_POLY 1
#else
#define BR_CAP_POLY 0
#endif
#endif
#ifndef BR_CAP_POLY
#define BR_CAP_POLY BR_PRODUCT_SMK37
#endif
#if BR_CAP_POLY != 0 && BR_CAP_POLY != 1
#error "BR_CAP_POLY must be 0 or 1"
#endif
#if BR_CAP_POLY && !defined(BR_POLY_SCHEDULER_EXPERIMENT)
#define BR_POLY_SCHEDULER_EXPERIMENT 1
#elif defined(BR_POLY_SCHEDULER_EXPERIMENT)
#if (BR_POLY_SCHEDULER_EXPERIMENT ? 1 : 0) != BR_CAP_POLY
#error "BR_POLY_SCHEDULER_EXPERIMENT and BR_CAP_POLY disagree"
#endif
#endif

/* ---- screen reader adapter (firmware/src/instrument_reader.h, docs/SCREEN-READER.md) ----
 * Default on for both products. -DBR_CAP_READER=0 leaves it out completely: every line of it
 * (and of its hooks in dinghy.c and platform.c) is inside #if BR_CAP_READER. */
#ifndef BR_CAP_READER
#define BR_CAP_READER 1
#endif
#if BR_CAP_READER != 0 && BR_CAP_READER != 1
#error "BR_CAP_READER must be 0 or 1"
#endif

/* BR_POLY_SUPPORT: the code polyphony depends on, beyond the polyphony engine itself
 * (which stays under BR_POLY_SCHEDULER_EXPERIMENT): the Plaits compact voice state
 * (plaits_c_api), 8-byte delay-line alignment for the loan (instrument_fx.c), the
 * br_env_start_state split and the late-owner selector rule (instrument_audio.c).
 * It is BR_CAP_POLY || BR_PRODUCT_SMK37, not just BR_CAP_POLY, because the SMK-37 tree
 * compiled that code unconditionally: the SMK-37 --no-polyphony build contains it, and
 * its bytes are fixed by the G-SMK reference. On FM-1 it follows BR_CAP_POLY, so a
 * build without polyphony is unchanged. Do not define it on the command line. */
#ifdef BR_POLY_SUPPORT
#error "BR_POLY_SUPPORT follows from BR_CAP_POLY and BR_PRODUCT; do not define it"
#endif
#if BR_CAP_POLY || BR_PRODUCT_SMK37
#define BR_POLY_SUPPORT 1
#else
#define BR_POLY_SUPPORT 0
#endif

/* BR_POLY_SIZE_CONSTRAINED: 1 only for FM-1 with polyphony (BR_CAP_POLY), whose HD
 * image does not fit the app slot without size measures. It selects them and nothing
 * else: BR_POLY_COLD below (control-rate and per-event code compiled for size, out of
 * line) and the per-source optimisation overrides in tools/build.py (keyed to the same
 * configuration). Every other build (FM-1 without polyphony, SMK-37 with or without)
 * keeps its code generation byte for byte. Do not define it on the command line.
 *
 * BR_POLY_COLD marks a function that runs per event or per control block, never per
 * sample: noinline keeps it out of the -O3 render function it would be inlined into,
 * minsize compiles it for size. Empty unless BR_POLY_SIZE_CONSTRAINED. */
#if defined(BR_POLY_SIZE_CONSTRAINED) || defined(BR_POLY_COLD)
#error "BR_POLY_SIZE_CONSTRAINED and BR_POLY_COLD follow from BR_CAP_POLY and BR_PRODUCT; do not define them"
#endif
#if BR_CAP_POLY && BR_PRODUCT_FM1
#define BR_POLY_SIZE_CONSTRAINED 1
#if defined(__clang__)
#define BR_POLY_COLD __attribute__((noinline, minsize))
#else
#define BR_POLY_COLD __attribute__((noinline))
#endif
#else
#define BR_POLY_SIZE_CONSTRAINED 0
#define BR_POLY_COLD
#endif

#endif /* BR_PRODUCT_H */
