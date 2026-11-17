/* SPDX-License-Identifier: GPL-3.0-only */
/* The whole app (dinghy.c) and the split audio unit in one host program, so the polyphony engine
 * (instrument_audio.c, BR_SPLIT_AUDIO only) and the UI meet as on the device. dinghy.c's host model of
 * the polyphony foreground transition (BR_POLY_UI) runs from br_app_tick as the device one does.
 * Build with -DBR_PRODUCT=BR_PRODUCT_ID_FM1 -DBR_CAP_POLY=1 (or the SMK-37 product) and DSP objects
 * built with the same defines. Same staging as tests/test_late_drop_protocol.c. */
#include <stdint.h>
#include <string.h>
#include "../firmware/src/instrument_fx_progress.h"
uint32_t br_fx_init_test_calls;
uint32_t br_app_test_inject_fault_before_mutation;
#define FM1_TICKS_PER_US 24u
static struct { uint32_t last_ticks; } fm1_audio_metrics;
static uint32_t fm1_ticks(void) { static uint32_t tick; return ++tick; }
static void fm1_audio_metrics_snapshot(uint32_t out[8]) { memset(out, 0, 32u); }
static void fm1_audio_metrics_reset(void) { }
static void fm1_uac_diagnostic_snapshot(uint32_t out[19]) { memset(out, 0, 76u); out[0] = 1u; }
#define DINGHY_HOST 1
#define BR_SPLIT_AUDIO 1
#define BR_DIAGNOSTIC_TEST 1
#define DINGHY_DEVICE_SAVE_TEST 1      /* the device SAVE screens (es.mode) the suspend rule reads */
#ifdef POLY_UI_HOST_TEST
#define main unused_host_main
#endif
#include "../firmware/src/dinghy.c"
#ifdef POLY_UI_HOST_TEST
#undef main
#endif
#include "../firmware/src/instrument_audio_unit.c"
#if !BR_POLY_UI
#error "poly_ui_host.h needs a polyphony build (BR_CAP_POLY=1) so that BR_POLY_UI is on"
#endif
