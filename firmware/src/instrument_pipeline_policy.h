/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DINGHY_INSTRUMENT_PIPELINE_POLICY_H_
#define DINGHY_INSTRUMENT_PIPELINE_POLICY_H_

#include <stdint.h>
#include "../hal/fm1_drum_profile.h"
#include "../hal/fm1_ram_image_crc_diag.h"
#include "../hal/fm1_cpu1_stack_march.h"

#ifndef FM1_RUNTIME_IRQ_PROFILE
#define FM1_RUNTIME_IRQ_PROFILE 0
#endif

#ifndef FM1_RUNTIME_CHUNK_PROFILE
#define FM1_RUNTIME_CHUNK_PROFILE 0
#endif

#ifndef FM1_FAULT_C0_POPFRAME_SIDECAR
#define FM1_FAULT_C0_POPFRAME_SIDECAR 1
#endif

#ifndef FM1_TIMER5_USB_PHASE
#define FM1_TIMER5_USB_PHASE 1
#endif

#ifndef FM1_TIMER5_RAM
#define FM1_TIMER5_RAM 1
#endif

#ifndef FM1_INPUT_SCANNER_RAM
#define FM1_INPUT_SCANNER_RAM 1
#endif

/* Timer5's 2 kHz work cadence is every fifth 10 kHz interrupt, starting on
 * the first call. Keep these tiny pure helpers shared with the focused test so
 * the opt-in phase counter can be checked against the legacy modulo sequence. */
#if defined(FM1_DEVICE) && defined(FM1_TIMER5_RAM) && FM1_TIMER5_RAM
#define FM1_TIMER5_POLICY_INLINE static inline __attribute__((always_inline))
#else
#define FM1_TIMER5_POLICY_INLINE static inline
#endif

FM1_TIMER5_POLICY_INLINE uint32_t fm1_timer5_usb_phase_due(uint32_t phase)
{
    return phase == 0u;
}

FM1_TIMER5_POLICY_INLINE uint32_t fm1_timer5_usb_phase_next(uint32_t phase)
{
    return phase == 4u ? 0u : phase + 1u;
}

#undef FM1_TIMER5_POLICY_INLINE

static inline uint32_t br_pipeline_mode_command_valid(uint32_t command,
    const uint8_t *body, uint32_t length)
{
    if (!body || length != 4u || body[0] != 'B' || body[1] != 'R' ||
        body[2] != 1u) return 0u;
    if (command == 0x86u) return body[3] <= 3u;
    if (command == 0x7eu) return body[3] <= 1u;
    return 0u;
}

/* Once CPU1 reset could not be confirmed, keep the live audio/FX state and
 * output ownership untouched until a cold reboot. These commands only read
 * retained/runtime status, apart from 0x7C which may schedule the guarded
 * cold reboot only when the hardware already confirms CPU1 is held. */
static inline uint32_t br_pipeline_fault_command_allowed(uint32_t command)
{
    switch (command) {
    case 0x60u: /* audio timing */
    case 0x62u: /* protocol/abort status */
    case 0x6cu: /* DSP timing detail */
    case 0x6fu: /* crash record */
    case 0x75u: /* early worker record */
    case 0x76u: /* early register snapshot */
    case 0x7bu: /* early launch trace */
    case 0x7cu: /* guarded reboot request */
    case 0x7du: /* fault bundle */
    case 0x7fu: /* live fault summary */
    case 0x78u: /* worker status */
    case 0x79u: /* FX progress */
    case 0x7au: /* reset source */
    case 0x81u: /* current master gain */
    case 0x87u: /* worker diagnostics */
    case 0x88u: /* audio tail timing */
    case 0x89u: /* foreground journal */
    case 0x8au: /* USB audio counters */
    case 0x8bu: /* sealed source cost snapshot */
    case 0x8cu: /* sealed same-half timing records */
    case 0x95u: /* C1/C0 shared stack-write guard snapshot */
    case 0x98u: /* read-only TIMER4 state snapshot */
#if FM1_CPU1_STACK_MARCH_DIAG
    case 0x97u: /* read-only early C1 stack March result */
#endif
#if defined(FM1_DEVICE) && FM1_RAM_IMAGE_CRC_DIAG
    case 0x93u: /* fixed RAM-image CRC snapshot */
#endif
#if defined(FM1_DEVICE) && FM1_FAULT_C0_POPFRAME_SIDECAR
    case 0x94u: /* bounded C0 exception pop frame */
#endif
#if FM1_RUNTIME_IRQ_PROFILE
    case 0x8du: /* sealed scanner/Timer5 observations */
#endif
#if FM1_RUNTIME_CHUNK_PROFILE
    case 0x8eu: /* frozen per-half runtime chunk trace */
#endif
#if FM1_DRUM_PROFILE
    case 0x8fu: /* frozen per-core Plaits drum stage profile */
#endif
    case 0xaeu: /* fixed extended P33 readback */
    case 0x99u: /* P33 analog-selector readback */
    case 0xacu: /* retained, bounded dual-core RAM-code windows */
    case 0xafu: /* 360 startup power report */
    case 0xb0u: /* 360 retained transition attempts */
    case 0xb3u: /* bounded read-only MIDI clock counters */
    case 0xb2u: /* bounded read-only late/drop counters */
        return 1u;
    default:
        return 0u;
    }
}

static inline uint32_t br_pipeline_mode_change_allowed(uint32_t playing,
    uint32_t armed, uint32_t muted, uint32_t aborted,
    uint32_t engine_quiet, uint32_t fx_quiet)
{
    return !playing && !armed && !muted && !aborted && engine_quiet && fx_quiet;
}

#endif
