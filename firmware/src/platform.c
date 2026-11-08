/* SPDX-License-Identifier: GPL-3.0-only */
/* Dinghy's platform: everything an FM-1 firmware needs before it can be fun, taken unchanged from
 * SLOOP / Felucca / sloopDX and wired up here. You should not need to touch this file.
 *
 *   boot + boot guard   a firmware that crashes at boot falls back to the USB rescue, then to the ROM
 *   USB rescue          OCT- held at power-on: no audio, only the updater (the web installer still works)
 *   updater             the M-UPGRADE SysEx path: the web installer / the hub can always reach the FM-1
 *   USB-MIDI            in and out (midi_get, midi_send)
 *   USB audio           the output as a class-compliant 16-bit stereo input, 44.1 or 48 kHz
 *   keys, knobs, LEDs   fm1_in.notes / buttons, fm1_input_edges, fm1_enc_take, fm1_led_key (hal/fm1_input.h)
 *   screen              240 x 240, lcd_fill, draw_text_box at 2x scale (gfx.c)
 *   settings            one 4 KiB flash sector with a CRC (settings_load / settings_save)
 *   crash screen        the PC of a fault, then a reboot
 *
 * The app (dinghy.c) includes this file first and supplies:
 *   static uint32_t app_audio(int32_t *out, uint32_t n) audio ISR: n stereo frames, L R L R, Q15 (+-32767), out is zero
 *   static void app_main(void)                        never returns; calls platform_poll() often (every few ms)
 */
/* the panel by name: buttons (fm1_in.buttons, fm1_input_edges, fm1_led_key: bit / id), the encoders
 * (fm1_enc_take); the 27 note keys are fm1_in.notes bit 0 (F3) .. 26 (G5), LED id 14 + key */
#include "audio_level.h"
#include "audio_deadline.h"
#include "master_gain_control.h"
#include "instrument_pipeline_policy.h"
#include "../hal/fm1_audio_block.h"
#include "../hal/audio_render_status.h"
#include "../hal/fm1_audio_rate.h"
#include "../hal/fm1_memory_sync.h"
#include "../plaits/upstream/plaits/wavetable_pack.h"
#include "../hal/fm1_drum_profile.h"
#include "../hal/fm1_hot_code_policy.h"
#include "../hal/fm1_led_levels.h"
#include "../hal/fm1_battery.h"
#include "../hal/fm1_runtime_trace.h"
#ifndef FM1_RUNTIME_IRQ_PROFILE
#define FM1_RUNTIME_IRQ_PROFILE 0
#endif
uint32_t br_engine_pipeline_uncontained_fault_get(void);
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
#if defined(BR_SPLIT_AUDIO)
uint32_t br_engine_pipeline_mode_get(void);
#else
static inline uint32_t br_engine_pipeline_mode_get(void);
#endif
#ifdef BR_SPLIT_AUDIO
extern volatile uint8_t br_midi_clock_enabled;
extern volatile uint32_t br_midi_clock_generation;
void br_engine_clock_dma_edge(uint32_t tick, uint32_t half, uint32_t lead_frames);
void br_engine_clock_dma_invalid(void);
#else
static volatile uint8_t br_midi_clock_enabled;
static volatile uint32_t br_midi_clock_generation;
static void br_engine_clock_dma_edge(uint32_t tick, uint32_t half, uint32_t lead_frames);
static void br_engine_clock_dma_invalid(void);
#endif
#endif
void br_audio_source_job_timing_reset(void);
#if defined(BR_SPLIT_AUDIO)
void br_audio_late_drop_reset(void);
#endif
static uint32_t audio_block_commits;
static void platform_audio_block_done(int32_t *out, uint32_t n);
static uint32_t app_audio(int32_t *out, uint32_t n);
static volatile uint8_t in_audio;
#if FM1_RUNTIME_IRQ_PROFILE || FM1_RUNTIME_CHUNK_PROFILE || FM1_DRUM_PROFILE
static uint32_t br_foreground_irq_save(void);
static void br_foreground_irq_restore(uint32_t state);
#endif

#if FM1_RUNTIME_IRQ_PROFILE
/* Optional scanner/Timer5 observations. The prefix counter overlaps the body
 * counter: it is entry-to-after-fm1_input_tick, not an additive substage.
 * The existing entry tick is the prefix start, so it includes Timer5 ack and
 * C-side work before the scanner; the body ends just before the in-audio
 * return (including optional UAC service). Neither is a CPU-compute measure. */
typedef struct {
    uint32_t live_body_ticks;
    uint32_t live_postscan_prefix_ticks;
    uint32_t live_count;
    uint32_t sealed_valid;
    uint32_t sealed_generation;
    uint32_t sealed_full_half_ticks;
    uint32_t sealed_callback_ticks;
    uint32_t sealed_body_ticks;
    uint32_t sealed_postscan_prefix_ticks;
    uint32_t sealed_count;
} fm1_runtime_irq_profile_store_t;
#if defined(FM1_DEVICE)
static fm1_runtime_irq_profile_store_t fm1_runtime_irq_profile_store
    __attribute__((section(".pool"), aligned(4)));
#else
static fm1_runtime_irq_profile_store_t fm1_runtime_irq_profile_store;
#endif
static uint32_t fm1_runtime_irq_profile_generation;

static void fm1_runtime_irq_profile_reset(void)
{
    fm1_runtime_irq_profile_store.live_body_ticks = 0u;
    fm1_runtime_irq_profile_store.live_postscan_prefix_ticks = 0u;
    fm1_runtime_irq_profile_store.live_count = 0u;
    fm1_runtime_irq_profile_store.sealed_valid = 0u;
    fm1_runtime_irq_profile_store.sealed_generation = 0u;
    fm1_runtime_irq_profile_store.sealed_full_half_ticks = 0u;
    fm1_runtime_irq_profile_store.sealed_callback_ticks = 0u;
    fm1_runtime_irq_profile_store.sealed_body_ticks = 0u;
    fm1_runtime_irq_profile_store.sealed_postscan_prefix_ticks = 0u;
    fm1_runtime_irq_profile_store.sealed_count = 0u;
    fm1_runtime_irq_profile_generation = 0u;
}

static void fm1_runtime_irq_profile_begin(void)
{
    uint32_t irq = br_foreground_irq_save();
    fm1_runtime_irq_profile_store.live_body_ticks = 0u;
    fm1_runtime_irq_profile_store.live_postscan_prefix_ticks = 0u;
    fm1_runtime_irq_profile_store.live_count = 0u;
    ++fm1_runtime_irq_profile_generation;
    if (!fm1_runtime_irq_profile_generation)
        fm1_runtime_irq_profile_generation = 1u;
    br_foreground_irq_restore(irq);
}

static void fm1_runtime_irq_profile_add(uint32_t postscan_prefix_ticks,
                                        uint32_t body_ticks)
{
    fm1_runtime_irq_profile_store.live_postscan_prefix_ticks +=
        postscan_prefix_ticks;
    fm1_runtime_irq_profile_store.live_body_ticks += body_ticks;
    ++fm1_runtime_irq_profile_store.live_count;
}

/* Called only when 0x88 updates its worst full-half ACK duration. */
static void fm1_runtime_irq_profile_seal(uint32_t full_half_ticks,
                                         uint32_t callback_ticks)
{
    uint32_t irq = br_foreground_irq_save();
    fm1_runtime_irq_profile_store.sealed_generation =
        fm1_runtime_irq_profile_generation;
    fm1_runtime_irq_profile_store.sealed_full_half_ticks = full_half_ticks;
    fm1_runtime_irq_profile_store.sealed_callback_ticks = callback_ticks;
    fm1_runtime_irq_profile_store.sealed_body_ticks =
        fm1_runtime_irq_profile_store.live_body_ticks;
    fm1_runtime_irq_profile_store.sealed_postscan_prefix_ticks =
        fm1_runtime_irq_profile_store.live_postscan_prefix_ticks;
    fm1_runtime_irq_profile_store.sealed_count =
        fm1_runtime_irq_profile_store.live_count;
    fm1_runtime_irq_profile_store.sealed_valid = 1u;
    br_foreground_irq_restore(irq);
}

static void fm1_runtime_irq_profile_snapshot(uint32_t out[7])
{
    uint32_t irq = br_foreground_irq_save();
    out[0] = fm1_runtime_irq_profile_store.sealed_valid;
    out[1] = fm1_runtime_irq_profile_store.sealed_generation;
    out[2] = fm1_runtime_irq_profile_store.sealed_full_half_ticks;
    out[3] = fm1_runtime_irq_profile_store.sealed_callback_ticks;
    out[4] = fm1_runtime_irq_profile_store.sealed_body_ticks;
    out[5] = fm1_runtime_irq_profile_store.sealed_postscan_prefix_ticks;
    out[6] = fm1_runtime_irq_profile_store.sealed_count;
    br_foreground_irq_restore(irq);
}
#endif

/* Shared DMA-half wrapper used by the target IRQ and the host ownership test.
 * A worker ownership fault must bypass pre-clear, fallback commit, and scaling. */
static uint32_t platform_audio_render_half(int32_t *out, uint32_t frames)
{
    uint32_t i, offset, result;
    if (br_engine_pipeline_uncontained_fault_get())
        return FM1_AUDIO_RENDER_UNCONTAINED;
    for (i = 0u; i < 2u * frames; ++i) out[i] = 0;
    audio_block_commits = 0u;
    result = app_audio(out, frames);
    if (br_engine_pipeline_uncontained_fault_get() ||
        result == FM1_AUDIO_RENDER_UNCONTAINED)
        return FM1_AUDIO_RENDER_UNCONTAINED;
    /* The buffer may have become active during the callback. In particular,
     * do not fallback-commit, tap UAC, scale, clear, or shift it here. */
    if (result == FM1_AUDIO_RENDER_DROP_LATE ||
        result == FM1_AUDIO_RENDER_CONTAINED_FAULT)
        return result;
    if (result != FM1_AUDIO_RENDER_OK)
        return FM1_AUDIO_RENDER_UNCONTAINED;
    if (!audio_block_commits) {
        for (offset = 0u; offset < frames; offset += BR_AUDIO_BLOCK_FRAMES) {
            uint32_t count = frames - offset < BR_AUDIO_BLOCK_FRAMES ?
                frames - offset : BR_AUDIO_BLOCK_FRAMES;
            platform_audio_block_done(out + 2u * offset, count);
        }
    }
    for (i = 0u; i < 2u * frames; ++i) out[i] <<= 7;
    return FM1_AUDIO_RENDER_OK;
}
enum { BTN_OCT_DOWN = 0, BTN_OCT_UP = 1, BTN_FX = 2, BTN_SEL = 3, BTN_ENV = 4, BTN_LFO = 5, BTN_EDIT = 6,
       BTN_GLO = 7, BTN_HOME = 8, BTN_SAVE = 9, BTN_ARP = 10, BTN_SEQ = 11, BTN_PLAY = 12, BTN_REC = 13 };
enum { ENC_SELECT = 0, ENC_ALGORITHM = 1, ENC_KNOB1 = 2, ENC_KNOB2 = 3, ENC_KNOB3 = 4, ENC_KNOB4 = 5, ENC_PRESETS = 6 };
#define KEY_LED(k) (14u + (k))

#ifdef DINGHY_HOST
#include "../../tests/host.c"                  /* on a computer: a fake FM-1 (tests/host.c) */
#if FM1_RUNTIME_CHUNK_PROFILE
#include "../hal/fm1_runtime_trace.c"
#endif
#if FM1_DRUM_PROFILE
#include "../hal/fm1_drum_profile.c"
#endif
static volatile int32_t master_q12 = 2048;
static uint32_t platform_audio_block_count;
static void platform_audio_block_done(int32_t *out, uint32_t n)
{
    (void)out; (void)n;
    ++platform_audio_block_count;
    ++audio_block_commits;
}
#elif defined(DINGHY_WEB)
#include "../../web/emu/platform.c"             /* in a browser: fake FM-1 clock, screen, controls and flash */
#if FM1_RUNTIME_CHUNK_PROFILE
#include "../hal/fm1_runtime_trace.c"
#endif
#if FM1_DRUM_PROFILE
#include "../hal/fm1_drum_profile.c"
#endif
static void platform_audio_block_done(int32_t *out, uint32_t n)
{ (void)out; (void)n; ++audio_block_commits; }
#else
#include <stdint.h>
#include "fm1_time.h"
#if FM1_RUNTIME_CHUNK_PROFILE
#include "../hal/fm1_runtime_trace.c"
#endif
#include "fm1_sys.h"
#include "fm1_clock_snapshot.h"
/* Implemented in dinghy.c, where this file's in_audio and br_ui are visible. */
void fm1_fault_diag_context_snapshot(uint32_t out[2]);
#include "fm1_clock_measure.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_cpu1_smoke.h"
#include "fm1_cpu1_early_cookie.h"
#include "fm1_cpu1_autostart_policy.h"
#include "fm1_cpu1_early_hw.h"
#include "fm1_cpu1_early_pc_window.h"
#include "fm1_cpu1_worker.h"
#include "fm1_cpu1_worker_target_hw.h"
#include "fm1_vendor360_target.h"
#include "fm1_ram_code_guard.h"
#if FM1_DRUM_PROFILE
#include "../hal/fm1_drum_profile.c"
#endif
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_foreground_journal.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "bootguard.h"

/* status, timeouts, CON0, CON5, CON6, CON9 from this boot */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
volatile uint32_t fm1_early_p33_snapshot[6];
volatile fm1_power_read_sample_t fm1_power_read_entry;
volatile uint32_t fm1_power_read_entry_ready;
volatile fm1_vp_report_t fm1_vpd_report;
volatile uint32_t fm1_vpd_ready;
#endif

volatile fm1_foreground_journal_t fm1_foreground_journal
    __attribute__((section(".noinit.watchdog"), aligned(4), used));
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static fm1_foreground_journal_t fm1_foreground_previous;
static uint32_t fm1_foreground_previous_mask;
#endif

static void fm1_foreground_journal_barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

static uint32_t fm1_foreground_journal_snapshot(
    const volatile fm1_foreground_journal_t *src,
    fm1_foreground_journal_t *dst)
{
    uint32_t mask = 0u;
    dst->magic = src->magic;
    dst->version = src->version;
    dst->byte_size = src->byte_size;
    dst->boot_id = src->boot_id;
    dst->poll_seq = src->poll_seq;
    dst->poll_seq_inverse = src->poll_seq_inverse;
    dst->poll_t4 = src->poll_t4;
    dst->timer_seq = src->timer_seq;
    dst->timer_seq_inverse = src->timer_seq_inverse;
    dst->timer_reti = src->timer_reti;
    dst->timer_t4 = src->timer_t4;
    if (!fm1_foreground_journal_header_valid(dst)) {
        dst->magic = 0u;
        dst->boot_id = FM1_FOREGROUND_JOURNAL_UNKNOWN;
        dst->poll_t4 = FM1_FOREGROUND_JOURNAL_UNKNOWN;
        dst->timer_reti = FM1_FOREGROUND_JOURNAL_UNKNOWN;
        dst->timer_t4 = FM1_FOREGROUND_JOURNAL_UNKNOWN;
        return 0u;
    }
    if (fm1_foreground_journal_poll_valid(dst))
        mask |= FM1_FOREGROUND_JOURNAL_POLL_VALID;
    else dst->poll_t4 = FM1_FOREGROUND_JOURNAL_UNKNOWN;
    if (fm1_foreground_journal_timer_valid(dst))
        mask |= FM1_FOREGROUND_JOURNAL_TIMER_VALID;
    else {
        dst->timer_reti = FM1_FOREGROUND_JOURNAL_UNKNOWN;
        dst->timer_t4 = FM1_FOREGROUND_JOURNAL_UNKNOWN;
    }
    return mask;
}

static void fm1_foreground_journal_begin(
    const fm1_foreground_journal_t *previous, uint32_t previous_mask)
{
    uint32_t boot_id = 1u;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    fm1_foreground_previous_mask = previous_mask;
    fm1_foreground_previous = *previous;
#else
    (void)previous_mask;
#endif
    if (fm1_foreground_journal_header_valid(previous)) {
        boot_id = previous->boot_id + 1u;
        if (!boot_id || boot_id == FM1_FOREGROUND_JOURNAL_UNKNOWN) boot_id = 1u;
    }
    fm1_foreground_journal.magic = 0u;
    fm1_foreground_journal.version = FM1_FOREGROUND_JOURNAL_VERSION;
    fm1_foreground_journal.byte_size = sizeof(fm1_foreground_journal_t);
    fm1_foreground_journal.boot_id = boot_id;
    fm1_foreground_journal.poll_seq = 1u;
    fm1_foreground_journal.poll_seq_inverse = ~1u;
    fm1_foreground_journal.poll_t4 = FM1_FOREGROUND_JOURNAL_UNKNOWN;
    fm1_foreground_journal.timer_seq = 1u;
    fm1_foreground_journal.timer_seq_inverse = ~1u;
    fm1_foreground_journal.timer_reti = FM1_FOREGROUND_JOURNAL_UNKNOWN;
    fm1_foreground_journal.timer_t4 = FM1_FOREGROUND_JOURNAL_UNKNOWN;
    fm1_foreground_journal_barrier();
    fm1_foreground_journal.magic = FM1_FOREGROUND_JOURNAL_MAGIC;
    fm1_foreground_journal_barrier();
}

#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static void fm1_foreground_journal_store_poll(uint32_t tick)
{
    uint32_t seq = fm1_foreground_journal.poll_seq;
    if (seq & 1u) ++seq;
    fm1_foreground_journal.poll_seq = seq + 1u;
    fm1_foreground_journal_barrier();
    fm1_foreground_journal.poll_t4 = tick;
    fm1_foreground_journal_barrier();
    fm1_foreground_journal.poll_seq = seq + 2u;
    fm1_foreground_journal.poll_seq_inverse = ~(seq + 2u);
    fm1_foreground_journal_barrier();
}

static void fm1_foreground_journal_store_timer(uint32_t reti, uint32_t tick)
{
    uint32_t seq = fm1_foreground_journal.timer_seq;
    if (seq & 1u) ++seq;
    fm1_foreground_journal.timer_seq = seq + 1u;
    fm1_foreground_journal_barrier();
    fm1_foreground_journal.timer_reti = reti;
    fm1_foreground_journal.timer_t4 = tick;
    fm1_foreground_journal_barrier();
    fm1_foreground_journal.timer_seq = seq + 2u;
    fm1_foreground_journal.timer_seq_inverse = ~(seq + 2u);
    fm1_foreground_journal_barrier();
}

static void fm1_foreground_journal_snapshot_reply(uint32_t out[14])
{
    fm1_foreground_journal_t current;
    uint32_t current_mask, irq = fm1_cpu1_worker_irq_save();
    current_mask = fm1_foreground_journal_snapshot(&fm1_foreground_journal, &current);
    out[0] = 1u;
    out[1] = fm1_foreground_previous_mask;
    out[2] = fm1_foreground_previous.magic == FM1_FOREGROUND_JOURNAL_MAGIC ?
        fm1_foreground_previous.boot_id : FM1_FOREGROUND_JOURNAL_UNKNOWN;
    out[3] = (fm1_foreground_previous_mask & FM1_FOREGROUND_JOURNAL_POLL_VALID) ?
        fm1_foreground_previous.poll_t4 : FM1_FOREGROUND_JOURNAL_UNKNOWN;
    out[4] = (fm1_foreground_previous_mask & FM1_FOREGROUND_JOURNAL_TIMER_VALID) ?
        fm1_foreground_previous.timer_reti : FM1_FOREGROUND_JOURNAL_UNKNOWN;
    out[5] = (fm1_foreground_previous_mask & FM1_FOREGROUND_JOURNAL_TIMER_VALID) ?
        fm1_foreground_previous.timer_t4 : FM1_FOREGROUND_JOURNAL_UNKNOWN;
    out[6] = current_mask;
    out[7] = fm1_foreground_journal_header_valid(&current) ? current.boot_id :
        FM1_FOREGROUND_JOURNAL_UNKNOWN;
    out[8] = (current_mask & FM1_FOREGROUND_JOURNAL_POLL_VALID) ? current.poll_t4 :
        FM1_FOREGROUND_JOURNAL_UNKNOWN;
    out[9] = (current_mask & FM1_FOREGROUND_JOURNAL_TIMER_VALID) ? current.timer_reti :
        FM1_FOREGROUND_JOURNAL_UNKNOWN;
    out[10] = (current_mask & FM1_FOREGROUND_JOURNAL_TIMER_VALID) ? current.timer_t4 :
        FM1_FOREGROUND_JOURNAL_UNKNOWN;
    out[11] = fm1_boot.p3_rst;
    out[12] = fm1_boot.rst_src;
    out[13] = fm1_boot.wdt_con;
    fm1_cpu1_worker_irq_restore(irq);
}
#endif

#define FM1_CPU1_EARLY_SNAPSHOT_MAGIC 0x43503153u /* "CP1S" */
#define FM1_CPU1_EARLY_RESULT_MAGIC 0x43503152u /* "CP1R" */
#define FM1_CPU1_EARLY_INTEGRITY_SALT 0x91E10DA5u
enum { FM1_CPU1_EARLY_IDLE = 0u, FM1_CPU1_EARLY_REQUESTED = 1u, FM1_CPU1_EARLY_IN_PROGRESS = 2u };
typedef struct { volatile uint32_t magic, inverse; } fm1_cpu1_early_cookie_t;
typedef struct {
    uint32_t magic, dbg_en, c0_wr_limit_en;
    uint32_t pc0_l, pc0_h, pc1_l, pc1_h, c1_con, entry;
} fm1_cpu1_early_snapshot_t;
typedef struct {
    uint32_t magic, state, saved_control, saved_entry, integrity;
    uint32_t status, heartbeat, cpu_id, restored, precondition_flags;
} fm1_cpu1_early_record_t;
typedef struct {
    uint32_t launch_stage, post_control, wait_rc, mailbox_id, mailbox_state, worker_heartbeat;
} fm1_cpu1_early_trace_t;
static fm1_cpu1_early_cookie_t fm1_cpu1_early_cookie __attribute__((section(".noinit"), used));
static fm1_cpu1_early_snapshot_t fm1_cpu1_early_snapshot __attribute__((section(".noinit"), used));
static fm1_cpu1_early_record_t fm1_cpu1_early_record __attribute__((section(".noinit"), used));
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static fm1_cpu1_early_trace_t fm1_cpu1_early_trace __attribute__((section(".noinit.cpu1_trace"), used));
#define FM1_EARLY_TRACE(field, value) (fm1_cpu1_early_trace.field = (value))
#else
static uint8_t fm1_cpu1_early_trace_reserved[sizeof(fm1_cpu1_early_trace_t)]
    __attribute__((section(".noinit.cpu1_trace"), aligned(4), used));
#define FM1_EARLY_TRACE(field, value) ((void)0)
#endif
static volatile uint32_t fm1_cpu1_early_reboot_pending, fm1_cpu1_early_reboot_at;
static int fm1_cpu1_early_bootstrap(uint32_t boot_mode);
static uint32_t fm1_cpu1_early_cookie_arm(void);
void br_fx_progress_bootstrap(void);
static uint32_t fm1_cpu1_early_integrity(uint32_t state, uint32_t control, uint32_t entry)
{
    return FM1_CPU1_EARLY_RESULT_MAGIC ^ state ^ control ^ entry ^ FM1_CPU1_EARLY_INTEGRITY_SALT;
}

#ifndef APP_NAME
#define APP_NAME "JAMESOS"
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_990"                   /* package identity: 99N = Dinghy build N (tools/build.py) */
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif

bootguard_t bootguard __attribute__((section(".noinit")));
static volatile uint32_t fm1_ms;                /* milliseconds since power-on (TIMER5) */
static fm1_battery_state_t fm1_battery;
static int32_t fm1_battery_level(void) { return fm1_battery_bars(&fm1_battery, fm1_ms); }

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "libc.c"
#include "lcd.c"
#define CV_MAX (240u * 40u)                     /* maximum live canvas: sound header */
#include "gfx.c"

#define FS FM1_AUDIO_RATE_HZ                    /* sample rate shared with the DSP unit */
#define HALF_FRAMES FM1_AUDIO_HALF_FRAMES       /* fixed DMA half, divisible by BR_BLOCK */
#define HALF_WORDS (HALF_FRAMES * 2u)
#if (HALF_FRAMES % BR_AUDIO_BLOCK_FRAMES) != 0u
#error "internal audio block must divide the fixed DMA half"
#endif
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC 1
#define FELUCCA_UART 0
#define FM1_MIDI_TIMESTAMP() fm1_ticks()
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
#define FM1_MIDI_GENERATION() br_midi_clock_generation
#endif
#include "usb.c"

/* ---- flash: read anywhere; erase / program only the update's staging area and the settings sector */
#define SETTINGS_AT 0x000FC000u                 /* one 4 KiB sector of Felucca's globals area */
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_IN(off, n, SETTINGS_AT, SETTINGS_AT + 0x1000u) || FL_IN(off, n, 0x97000u, 0xB1000u))
#include "fm1_flash.h"
#include "fm1_cpu1_cache_start.h"
static uint8_t flash_ok;
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
/* Persists past a released worker fence if transaction completion faults. */
static volatile uint32_t br_ps_project_quarantine;
#endif
static int flash_worker_park(void);
static int flash_worker_resume(void);
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    uint32_t parked = fm1_cpu1_worker_mailbox.cpu_id == 1u;
    if (flash_worker_park()) return -9;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc) {
            if (parked) (void)flash_worker_resume();
            return rc;
        }
        off += k;
        d += k;
        n -= k;
    }
    if (parked && flash_worker_resume()) return -9;
    return 0;
}

static int flash_worker_park(void)
{
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    /* The outer project transaction owns the park and must be the only
     * reader/writer until its token-fenced release. */
    if (br_ps_project_quarantine || fm1_cpu1_worker_project_fence_held()) return -9;
#endif
    if (fm1_cpu1_worker_fault)
        return fm1_cpu1_worker_reset_held() ? 0 : -9;
    if (fm1_cpu1_worker_mailbox.cpu_id != 1u) return 0;
    if (fm1_cpu1_worker_park(FM1_CPU1_WORKER_TIMEOUT_TICKS) == FM1_CPU1_WORKER_OK) return 0;
    (void)fm1_cpu1_worker_abort_hold_reset();
    return -9;
}
static int flash_worker_resume(void)
{
#if defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    /* Only the project transaction may authorize/resume its parked worker. */
    if (br_ps_project_quarantine || fm1_cpu1_worker_project_fence_held()) return -9;
#endif
    if (fm1_cpu1_worker_fault)
        return fm1_cpu1_worker_reset_held() ? 0 : -9;
    if (fm1_cpu1_worker_mailbox.cpu_id != 1u) return 0;
    if (fm1_cpu1_worker_resume(FM1_CPU1_WORKER_TIMEOUT_TICKS) == FM1_CPU1_WORKER_OK) return 0;
    (void)fm1_cpu1_worker_abort_hold_reset();
    return -9;
}

static uint8_t recovery_active;
#define OTA_IDENTITY (recovery_active ? "FM-1_000" : FELUCCA_ID)
#define OTA_APP_COMMAND fm1_app_command
#define OTA_RESCUE_COMMAND fm1_rescue_command
/* Rescue diagnostics: only fixed-size reads; never erase or program settings. */
/* Schema 2: early values from the current boot and live values. The final
 * two words alias the early CON6/CON9; there is no voltage-write step here. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static void fm1_p33_snapshot_words(uint32_t words[14])
{
    uint32_t irq = irq_save(), i;
    words[0] = 2u;
    for (i = 0u; i < 6u; ++i) words[1u + i] = fm1_early_p33_snapshot[i];
    words[7] = fm1_p33_read(0x00u);
    words[8] = fm1_p33_read(0x05u);
    words[9] = fm1_p33_read(0x06u);
    words[10] = fm1_p33_read(0x09u);
    words[11] = fm1_p33_timeouts;
    words[12] = words[5]; words[13] = words[6];
    irq_restore(irq);
}
#endif

static void fm1_rescue_word(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8);
    out[2] = (uint8_t)(v >> 16); out[3] = (uint8_t)(v >> 24);
}
/* 0xAE with no body: schema, entry timeouts/11 bytes, live timeouts/11 bytes.
 * A three-byte body reads only the fixed, epoch-bound prior-fault record. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static int fm1_power_read_command(const uint8_t *body, uint32_t n,
                                 uint8_t reply[128], uint32_t *reply_n)
{
    uint32_t i;
    if (!n) {
        fm1_power_read_sample_t live;
        fm1_p33_checked_t ctx = { 0u };
        uint32_t irq = irq_save();
        fm1_power_read_collect(&live, fm1_p33_checked_read, &ctx, &ctx.timeouts);
        fm1_rescue_word(reply, 1u);
        fm1_rescue_word(reply + 4u, fm1_power_read_entry_ready == FM1_POWER_READ_READY ?
                         fm1_power_read_entry.timeouts : FM1_POWER_READ_UNKNOWN);
        for (i=0u; i<FM1_POWER_READ_COUNT; ++i)
            fm1_rescue_word(reply + 8u + 4u*i, fm1_power_read_entry_ready == FM1_POWER_READ_READY ?
                             fm1_power_read_entry.raw[i] : FM1_POWER_READ_UNKNOWN);
        fm1_rescue_word(reply + 52u, live.timeouts);
        for (i=0u; i<FM1_POWER_READ_COUNT; ++i)
            fm1_rescue_word(reply + 56u + 4u*i, live.raw[i]);
        irq_restore(irq);
        *reply_n = 100u; return 1;
    }
    if (n == 3u && body) {
        uint32_t off=(uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t count=fm1_power_fault_chunk(&fm1_power_fault_record, &fm1_fault_bundle,
                                             off, body[2], reply);
        if (!count || count > 128u) return 0;
        *reply_n=count; return 1;
    }
    return 0;
}

/* 0xAF: current report or bounded fault report; 0xB0: transition attempts. */
static int fm1_vendor360_diag_command(uint32_t cmd,const uint8_t *body,uint32_t n,
                                     uint8_t reply[128],uint32_t *reply_n)
{
    uint32_t i,off,count;
    if(cmd==0xafu && !n) {
        const volatile uint32_t *p=(const volatile uint32_t *)&fm1_vpd_report;
        if(fm1_vpd_ready!=FM1_VPD_READY)return 0;
        fm1_fault_ram_code_barrier();
        for(i=0u;i<sizeof(fm1_vp_report_t)/4u;i++)fm1_rescue_word(reply+4u*i,p[i]);
        *reply_n=sizeof(fm1_vp_report_t);return 1;
    }
    if(n!=3u||!body)return 0;
    off=(uint32_t)body[0] | (uint32_t)body[1]<<8;
    if(cmd==0xafu)count=fm1_vpd_fault_chunk(&fm1_vpd_fault,&fm1_fault_bundle,off,body[2],reply);
    else if(cmd==0xb0u)count=fm1_vpd_attempt_chunk(fm1_vpd_attempts,off,body[2],reply);
    else return 0;
    if(!count||count>128u)return 0;
    *reply_n=count;return 1;
}
#endif

static int fm1_rescue_command(uint32_t cmd, const uint8_t *body, uint32_t n,
                              uint8_t reply[128], uint32_t *reply_n)
{
    uint32_t i;
    *reply_n = 0u;
    if (!recovery_active) return 0;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    if (cmd == 0x6au && !n) {
        uint32_t clocks[8];
        fm1_clock_snapshot(clocks);
        for (i=0u;i<8u;i++) fm1_rescue_word(reply+4u*i,clocks[i]);
        *reply_n=32u;return 1;
    }
    if (cmd == 0xaeu) return fm1_power_read_command(body, n, reply, reply_n);
    if (cmd == 0xafu || cmd == 0xb0u)
        return fm1_vendor360_diag_command(cmd,body,n,reply,reply_n);
#endif
    if (cmd == 0xa2u && !n) {
        reply[0] = 'R'; reply[1] = '4'; reply[2] = '3'; reply[3] = '4';
        reply[4] = flash_ok; *reply_n = 5u; return 1;
    }
    if (cmd == 0xa0u && n == 3u) {
        uint32_t off = (uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t count = body[2];
        if (!flash_ok || !count || count > 32u || off >= 4096u ||
            count > 4096u - off || st_read(SETTINGS_AT + off, reply, count)) return 0;
        *reply_n = count; return 1;
    }
    if (cmd == 0x6fu && !n) {
        uint32_t irq = fm1_cpu1_worker_irq_save();
        fm1_rescue_word(reply, fm1_crash.magic);
        fm1_rescue_word(reply + 4u, fm1_crash.count);
        fm1_rescue_word(reply + 8u, fm1_crash.vec);
        fm1_rescue_word(reply + 12u, fm1_crash.pc);
        fm1_rescue_word(reply + 16u, fm1_crash.dbg);
        fm1_rescue_word(reply + 20u, fm1_crash.uptime_ms);
        fm1_cpu1_worker_irq_restore(irq);
        *reply_n = 24u; return 1;
    }
    if (cmd == 0x7du && n == 3u) {
        uint32_t off = (uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t count = fm1_fault_bundle_chunk(&fm1_fault_bundle, off, body[2], reply);
        if (!count || count > 128u) return 0;
        *reply_n = count; return 1;
    }
#if FM1_FAULT_RAM_CODE_SIDECAR && FM1_FAULT_RAM_CODE_CAPTURE_READ
    /* Fixed-range, epoch-bound fault code sidecar; read only, never exposes
     * an arbitrary caller-supplied address. */
    if (cmd == 0xabu && n == 3u) {
        uint32_t epoch, owner, pc;
        uint32_t off = (uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t count;
        if (!fm1_fault_ram_code_bundle_identity(&fm1_fault_bundle, &epoch, &owner, &pc)) return 0;
        count = fm1_fault_ram_code_chunk(&fm1_fault_ram_code_record, epoch, owner, pc,
                  (uint32_t)(uintptr_t)_rt_start, (uint32_t)(uintptr_t)_rt_end,
                  (uint32_t)(uintptr_t)_ra_start, (uint32_t)(uintptr_t)_ra_end,
                  off, body[2], reply);
        if (!count || count > 128u) return 0;
        *reply_n = count; return 1;
    }
#endif
#if FM1_FAULT_RAM_CODE_SIDECAR && FM1_FAULT_RAM_CODE_CAPTURE_READ
    if (cmd == 0xacu && n == 3u) {
        uint32_t off = (uint32_t)body[0] | (uint32_t)body[1] << 8;
        uint32_t count = fm1_fault_dual_code_chunk(&fm1_fault_dual_code_record, &fm1_fault_bundle,
                  (uint32_t)(uintptr_t)_rt_start, (uint32_t)(uintptr_t)_rt_end,
                  (uint32_t)(uintptr_t)_ra_start, (uint32_t)(uintptr_t)_ra_end,
                  off, body[2], reply);
        if (!count || count > 128u) return 0;
        *reply_n = count; return 1;
    }
#endif
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    if (cmd == 0x99u && !n) {
        uint32_t words[14];
        fm1_p33_snapshot_words(words);
        for (i = 0u; i < 14u; ++i) fm1_rescue_word(reply + 4u * i, words[i]);
        *reply_n = 56u; return 1;
    }
    if (cmd == 0x7au && !n) {
        uint32_t irq = fm1_cpu1_worker_irq_save();
        fm1_rescue_word(reply, 1u);
        fm1_rescue_word(reply + 4u, fm1_boot.p3_rst);
        fm1_rescue_word(reply + 8u, fm1_boot.rst_src);
        fm1_rescue_word(reply + 12u, fm1_boot.wdt_con);
        fm1_cpu1_worker_irq_restore(irq);
        *reply_n = 16u; return 1;
    }
    if (cmd == 0x89u && !n) {
        uint32_t words[FM1_FOREGROUND_DIAG89_WORDS];
        fm1_foreground_journal_snapshot_reply(words);
        for (i = 0u; i < FM1_FOREGROUND_DIAG89_WORDS; ++i)
            fm1_rescue_word(reply + 4u * i, words[i]);
        *reply_n = FM1_FOREGROUND_DIAG89_BYTES; return 1;
    }
#endif
    return 0;
}

static int fm1_app_command(uint32_t cmd, const uint8_t *body, uint32_t n,
                           uint8_t reply[128], uint32_t *reply_n);
#include "ota.c"
static void recovery_poll(void);
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void)
{
    fm1_wdt_feed();
    if (recovery_active) recovery_poll();
}
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    if (flash_worker_park()) return -9;
    { int rc = fl_erase4k(off, &took); return flash_worker_resume() ? -9 : rc; }
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    if (flash_worker_park()) return -9;
    { int rc = fl_write(off, p, n); return flash_worker_resume() ? -9 : rc; }
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    if (recovery_active) return;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, 2u, "UPDATE", C_WHITE, 1);
    if (step < 9u)
        draw_text_box(0, 124, 240, 2u, STEP[step < 6u ? step : 0], C_WHITE, 1);
    else
        draw_text_box(0, 124, 240, 2u, code == 1 ? "DRY RUN OK" : "FAILED", C_WHITE, 1);
}
static int ota_commit(const uint8_t *parm)
{
    if (flash_worker_park()) return -9;
    bootguard_clear(&bootguard);
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
    (void)fm1_cpu1_worker_abort_hold_reset();
    return -9; /* reset/loader handoff unexpectedly returned */
}
#include "recovery.c"

/* ---- settings: [magic][length][crc32] then the bytes; settings_save erases the sector (~50 ms with
 * the interrupts off: the sound drops out), so save when nothing plays, not on every knob step */
#define SETTINGS_MAGIC 0x59474E44u              /* "DNGY" */
/* The existing sector has room for the extended sequencer metadata. Keep the
 * CRC/header-last transaction and the same physical flash boundaries. */
#define SETTINGS_CAPACITY (0x1000u - 3u * sizeof(uint32_t))
/* Cold-boot diagnostic: keep CRC instruction fetches off XIP immediately
 * after settings flash reads. The algorithm and persistent format stay exact. */
static __attribute__((section(".ram_audio"), noinline, used))
uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
/* Startup settings scratch may be lent only to an exclusive stopped project
 * transaction. Guard both directions so settings_load cannot race a loan. */
static uint8_t br_ps_settings_scratch[SETTINGS_CAPACITY] __attribute__((section(".pool"), aligned(4)));
static uint32_t br_ps_settings_scratch_busy, br_ps_settings_scratch_lent;
static int br_ps_settings_scratch_acquire(int (*exclusive)(void *),void *ctx,void **out,size_t *n)
{
    if(!exclusive || !exclusive(ctx) || !out || !n || br_ps_settings_scratch_busy || br_ps_settings_scratch_lent) return 0;
    br_ps_settings_scratch_lent=1; *out=br_ps_settings_scratch; *n=sizeof br_ps_settings_scratch; return 1;
}
static void br_ps_settings_scratch_release(void) { br_ps_settings_scratch_lent=0; }
#endif
static int settings_load(void *p, uint32_t n)   /* 0: loaded; else p is untouched */
{
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    uint8_t *buf=br_ps_settings_scratch;
    if(br_ps_settings_scratch_lent || br_ps_settings_scratch_busy) return -1;
    br_ps_settings_scratch_busy=1;
#define BR_SETTINGS_LOAD_RETURN(v) do { br_ps_settings_scratch_busy=0; return (v); } while(0)
#else
    static uint8_t buf[SETTINGS_CAPACITY] __attribute__((section(".pool"), aligned(4)));
#define BR_SETTINGS_LOAD_RETURN(v) return (v)
#endif
    uint32_t h[3], actual_crc;
    if (!flash_ok || n > SETTINGS_CAPACITY || st_read(SETTINGS_AT, h, sizeof h) || h[0] != SETTINGS_MAGIC || h[1] != n)
        BR_SETTINGS_LOAD_RETURN(-1);
    if (st_read(SETTINGS_AT + sizeof h, buf, n))
        BR_SETTINGS_LOAD_RETURN(-1);
    /* Keep the worker parked for the exact RAM-based settings CRC. */
    if (flash_worker_park()) BR_SETTINGS_LOAD_RETURN(-1);
    actual_crc = FL_FAR(crc32)(buf, n);
    if (flash_worker_resume()) BR_SETTINGS_LOAD_RETURN(-1);
    if (actual_crc != h[2]) BR_SETTINGS_LOAD_RETURN(-1);
    memcpy(p, buf, n);
    BR_SETTINGS_LOAD_RETURN(0);
#undef BR_SETTINGS_LOAD_RETURN
}
static int settings_save(const void *p, uint32_t n)
{
    uint32_t h[3] = {SETTINGS_MAGIC, n, 0}, took;
    int rc;
    if (!flash_ok || n > SETTINGS_CAPACITY)
        return -1;
    h[2] = FL_FAR(crc32)(p, n);
    if (flash_worker_park()) return -9;
    if ((rc = fl_erase4k(SETTINGS_AT, &took)) != 0)
        { (void)flash_worker_resume(); return rc; }
    if ((rc = fl_write(SETTINGS_AT + sizeof h, p, n)) != 0)
        { (void)flash_worker_resume(); return rc; }
    rc = fl_write(SETTINGS_AT, (const uint8_t *)h, sizeof h);   /* the header last: a cut save stays invalid */
    return flash_worker_resume() ? -9 : rc;
}

/* ---- MIDI over USB: 4-byte USB-MIDI packets, cable 0 */
static uint32_t midi_last_timestamp;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
static uint32_t midi_last_generation;
#endif
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)   /* 1: a message (status, data 1, data 2) */
{
    uint32_t pk;
    if (mi_r == mi_w)
        return 0;
    pk = midi_in_q[mi_r % MQ];
    midi_last_timestamp = midi_in_time_q[mi_r % MQ];
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    midi_last_generation = midi_in_generation_q[mi_r % MQ];
#endif
    mi_r++;
    *st = (uint8_t)(pk >> 8);
    *d1 = (uint8_t)(pk >> 16);
    *d2 = (uint8_t)(pk >> 24);
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2)   /* from the main loop only */
{
    midi_out_event((st >> 4) | st << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}

/* ---- the timer: 10 kHz key scan, milliseconds, USB at 2 kHz. It outranks the audio ISR; nested in a
 * render it only scans, counts and feeds the USB audio stream (as sloopDX main.c) */
/* Optional placement in the normal .ram_audio region copied by fm1_cstart.
 * This is not the strict, call-free .ram_text flash routine section. */
#define FM1_TIMER5_RAM_ATTR \
    __attribute__((section(".ram_audio.scanner"), noinline, used))
FM1_TIMER5_RAM_ATTR void fm1_timer5_irq(void)
{
    static uint32_t usb_phase, owed, last, acc;
    uint32_t t0 = fm1_ticks();
    uint32_t usb_due = fm1_timer5_usb_phase_due(usb_phase);
#if FM1_RUNTIME_IRQ_PROFILE
    uint32_t postscan_prefix_ticks = 0u;
#endif
#if FM1_RUNTIME_CHUNK_PROFILE
    uint32_t trace_postscan_tick = 0u;
#endif
    fm1_timer5_ack();
    void (* volatile input_tick_fn)(void) = fm1_input_tick;
    input_tick_fn();
#if FM1_RUNTIME_IRQ_PROFILE || FM1_RUNTIME_CHUNK_PROFILE
    if (in_audio) {
        uint32_t postscan_tick = fm1_ticks();
#if FM1_RUNTIME_IRQ_PROFILE
        postscan_prefix_ticks = postscan_tick - t0;
#endif
#if FM1_RUNTIME_CHUNK_PROFILE
        trace_postscan_tick = postscan_tick;
#endif
    }
#endif
    acc += t0 - last;
    last = t0;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (usb_due)
        owed = 1;
    usb_phase = fm1_timer5_usb_phase_next(usb_phase);
    /* Preserve the foreground sample cadence; while audio owns C0, take a
     * retained interrupted-PC sample at the existing 2 kHz USB phase. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    if (!in_audio || usb_due) {
        void (* volatile journal_store_timer_fn)(uint32_t, uint32_t) =
            fm1_foreground_journal_store_timer;
        journal_store_timer_fn(fm1_timer5_interrupted_reti(), fm1_ticks());
    }
#endif
    if (in_audio) {
        if (usb_due) {
            void (* volatile uac_service_fn)(void) = uac_service;
            uac_service_fn();
        }
#if FM1_RUNTIME_IRQ_PROFILE || FM1_RUNTIME_CHUNK_PROFILE
        {
            uint32_t end_tick = fm1_ticks();
#if FM1_RUNTIME_IRQ_PROFILE
            fm1_runtime_irq_profile_add(postscan_prefix_ticks, end_tick - t0);
#endif
#if FM1_RUNTIME_CHUNK_PROFILE
            void (* volatile trace_timer5_fn)(uint32_t, uint32_t, uint32_t) =
                fm1_runtime_trace_timer5;
            trace_timer5_fn(t0, trace_postscan_tick, end_tick);
#endif
        }
#endif
        return;
    }
    if (owed) {
        void (* volatile usb_poll_fn)(void) = usb_poll;
        usb_poll_fn();
    }
    owed = 0;
}
extern void isr_timer5(void);

/* ---- the sound: ALNK0 I2S, two halves; the app renders, the MASTER knob scales, Q15 -> 24 bit (-6 dBFS ceiling) */
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile int32_t master_q12 = 2048;
static fm1_master_gain_control_t master_gain_control = {2048, 0u};

static uint32_t fm1_master_gain_test_set(uint32_t mode, uint32_t allowed)
{
    return fm1_master_gain_set_test_mode(&master_gain_control, mode, allowed,
                                         &master_q12);
}

static uint32_t fm1_master_gain_test_mode(void)
{
    return master_gain_control.test_mode;
}

static int32_t fm1_master_gain_effective(void)
{
    return master_q12;
}
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
/* Timing-only half-callback observations. Audio code marks the T4 tick
 * immediately after the successful final CPU1 wait returns (source or FX).
 * The interval to ack_half includes all subsequent engine post-processing,
 * commit, scaling, and buffer publication. These values never change timing. */
static volatile uint32_t fm1_audio_cpu1_join_tick;
static volatile uint32_t fm1_audio_cpu1_join_valid;
static volatile uint32_t fm1_audio_tail_last_wait_to_ack;
static volatile uint32_t fm1_audio_tail_max_wait_to_ack;
static volatile uint32_t fm1_audio_tail_last_half_duration;
static volatile uint32_t fm1_audio_tail_max_half_duration;
static volatile uint32_t fm1_audio_tail_valid_halves;

static void fm1_audio_tail_metrics_reset(void)
{
    fm1_audio_cpu1_join_tick = 0u;
    fm1_audio_cpu1_join_valid = 0u;
    fm1_audio_tail_last_wait_to_ack = 0u;
    fm1_audio_tail_max_wait_to_ack = 0u;
    fm1_audio_tail_last_half_duration = 0u;
    fm1_audio_tail_max_half_duration = 0u;
    fm1_audio_tail_valid_halves = 0u;
}

static void fm1_audio_tail_metrics_snapshot(uint32_t out[8])
{
    uint32_t irq = irq_save();
    out[0] = 1u;
    out[1] = fm1_audio_cpu1_join_valid;
    out[2] = fm1_audio_cpu1_join_tick;
    out[3] = fm1_audio_tail_last_wait_to_ack;
    out[4] = fm1_audio_tail_max_wait_to_ack;
    out[5] = fm1_audio_tail_last_half_duration;
    out[6] = fm1_audio_tail_max_half_duration;
    out[7] = fm1_audio_tail_valid_halves;
    irq_restore(irq);
}

void fm1_audio_cpu1_join_clear(void)
{
    fm1_audio_cpu1_join_tick = 0u;
    fm1_audio_cpu1_join_valid = 0u;
}

void fm1_audio_cpu1_join_mark(uint32_t tick)
{
    fm1_audio_cpu1_join_tick = tick;
    fm1_audio_cpu1_join_valid = 1u;
}
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
/* Serial-tail breakdown for diagnostic 0xC8: join, FX start, C0 mix end,
 * C1 master wait end, FX end, commit start, commit end, ACK. Words 1..7 are
 * the last valid half's segment ticks, 8..14 the per-segment maxima. */
#define FM1_TAIL_DETAIL 1
extern volatile uint32_t br_tail_stamp[6];
static volatile uint32_t fm1_tail_detail[16];
static volatile uint32_t fm1_gap_detail[14];
static void fm1_tail_detail_capture(uint32_t join, uint32_t ack)
{
    uint32_t seq[8], k;
    seq[0] = join;
    for (k = 0u; k < 6u; ++k) { seq[k + 1u] = br_tail_stamp[k]; br_tail_stamp[k] = 0u; }
    seq[7] = ack;
    for (k = 0u; k < 8u; ++k) if (!seq[k]) return;
    for (k = 0u; k < 7u; ++k) {
        uint32_t d = seq[k + 1u] - seq[k];
        if (d > 0x7fffffffu) return;            /* out of order: not this half */
    }
    for (k = 0u; k < 7u; ++k) {
        uint32_t d = seq[k + 1u] - seq[k];
        fm1_tail_detail[1u + k] = d;
        if (d > fm1_tail_detail[8u + k]) fm1_tail_detail[8u + k] = d;
    }
    ++fm1_tail_detail[15];
    {   /* join -> validation, history, diagnostics, post copy, modulation, FX start */
        extern volatile uint32_t br_gap_stamp[5];
        uint32_t g[7];
        g[0] = join;
        for (k = 0u; k < 5u; ++k) { g[k + 1u] = br_gap_stamp[k]; br_gap_stamp[k] = 0u; }
        g[6] = seq[1];
        for (k = 0u; k < 7u; ++k) if (!g[k]) return;
        for (k = 0u; k < 6u; ++k) if (g[k + 1u] - g[k] > 0x7fffffffu) return;
        for (k = 0u; k < 6u; ++k) {
            uint32_t d = g[k + 1u] - g[k];
            fm1_gap_detail[1u + k] = d;
            if (d > fm1_gap_detail[7u + k]) fm1_gap_detail[7u + k] = d;
        }
        ++fm1_gap_detail[13];
    }
}
#endif
#if defined(FM1_DEVICE)
extern volatile uint32_t br_fx_output_prescaled;
#endif
static void platform_audio_block_done(int32_t *out, uint32_t n)
{
#if defined(FM1_DEVICE)
    if (br_fx_output_prescaled) br_fx_output_prescaled = 0u;   /* CPU1 applied the same gain */
    else
#endif
    fm1_scale_q12_clip_block(out, 2u * n, &master_q12);
#if FELUCCA_UAC
    uac_tap(out, n);
#endif
    ++audio_block_commits;
}
/* Read-only debugger diagnostics in crystal ticks, including nested USB/key IRQs.
 * Keep these outside the screen path and use them for device/CPU-emulator tests. */
typedef struct {
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    uint32_t last_ticks, worst_ticks, overruns, blocks;
#endif
    /* A window for the on-screen monitor: taken and cleared by the on-screen monitor (fm1_audio_window_take). */
    uint32_t window_sum, window_count;
} fm1_audio_metrics_t;
static volatile fm1_audio_metrics_t fm1_audio_metrics;

/* CPU0 audio-IRQ-owned diagnostics and the post-drop phase guard. The guard
 * filters an immediately repeated half indication; it does not establish a
 * full remaining DMA period or detect an even number of missed boundaries. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static volatile uint32_t fm1_audio_late_drops;
volatile uint32_t fm1_audio_late_contained_faults;
static volatile uint32_t fm1_audio_late_over_one_half;
static volatile uint32_t fm1_audio_late_over_two_halves;
static volatile uint32_t fm1_audio_late_resync_skips;
#endif
static volatile uint32_t fm1_audio_late_resync_active;
static volatile uint32_t fm1_audio_late_resync_baseline;

/* A host diagnostic SysEx can snapshot or reset these counters without
 * racing the audio or USB-audio interrupt updates. out[] is
 * last,worst,overruns,blocks,UAC-underruns,UAC-overruns,adjust-up,adjust-down. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static void fm1_audio_metrics_snapshot(uint32_t out[8])
{
    uint32_t irq = irq_save();
    out[0] = fm1_audio_metrics.last_ticks;
    out[1] = fm1_audio_metrics.worst_ticks;
    out[2] = fm1_audio_metrics.overruns;
    out[3] = fm1_audio_metrics.blocks;
#if FELUCCA_UAC
    out[4] = uac.underruns;
    out[5] = uac.overruns;
    out[6] = uac.adj_up;
    out[7] = uac.adj_down;
#else
    out[4] = out[5] = out[6] = out[7] = 0u;
#endif
    irq_restore(irq);
}
#endif

#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
/* Read-only UAC endpoint and ring snapshot for private diagnostic command 0x8A.
 * All values are copied while CPU0 interrupts are masked; no SIE/MMIO access is
 * performed. Monotonic counters are boot-lifetime except packets/frames, which
 * reset when the host changes sample rate. fill_lo/hi are reset at each stream
 * start and retained after stop; fill_sample is initialized to prime on stream
 * setup, then updated with each packet-time ring-fill sample. */
static void fm1_uac_diagnostic_snapshot(uint32_t out[19])
{
    uint32_t irq = irq_save();
    uint32_t flags = 0u;
    out[0] = 1u; /* schema */
#if FELUCCA_UAC
    flags = (uint32_t)(uac.alt != 0u) |
            ((uint32_t)(uac.r48 != 0u) << 1) |
            ((uint32_t)(uac.ring48 != 0u) << 2) |
            ((uint32_t)(uac.go != 0u) << 3) |
            ((uint32_t)(uac.flowing != 0u) << 4) |
            ((uint32_t)(uac.queued != 0u) << 5) |
            ((uint32_t)(uac.feed != 0u) << 6) |
            ((uint32_t)(usb.up != 0u) << 7) |
            ((uint32_t)(usb.config != 0u) << 8) |
            ((uint32_t)(uac.e0_rx != 0u) << 9);
    out[1] = flags;
    out[2] = uac.wait;
    out[3] = uac.missed;
    out[4] = uac.stalls;
    out[5] = uac.pkts;
    out[6] = uac.frames;
    out[7] = uac.starts;
    out[8] = uac.rate_sets;
    out[9] = uac.underruns;
    out[10] = uac.overruns;
    out[11] = uac.adj_up;
    out[12] = uac.adj_down;
    out[13] = ua_r;
    out[14] = ua_w;
    out[15] = ua_w - ua_r;
    out[16] = uac.fill_sample;
    out[17] = uac.fill_lo;
    out[18] = uac.fill_hi;
#else
    (void)flags;
    for (uint32_t i = 1u; i < 19u; ++i) out[i] = 0u;
#endif
    irq_restore(irq);
}
#endif


/* out: halves rendered, then their mean ticks, since the last call. The audio interrupt owns the counters, so
 * copy and clear them with interrupts masked. */
static void fm1_audio_window_take(uint32_t out[2])
{
    uint32_t irq = irq_save();
    uint32_t count = fm1_audio_metrics.window_count;
    uint32_t sum = fm1_audio_metrics.window_sum;
    fm1_audio_metrics.window_sum = fm1_audio_metrics.window_count = 0u;
    irq_restore(irq);
    out[0] = count;
    out[1] = count ? sum / count : 0u;
}

#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static void fm1_audio_metrics_reset(void)
{
    uint32_t irq = irq_save();
    fm1_audio_metrics.last_ticks = 0u;
    fm1_audio_metrics.worst_ticks = 0u;
    fm1_audio_metrics.overruns = 0u;
    fm1_audio_metrics.blocks = 0u;
    fm1_audio_late_drops = 0u;
    fm1_audio_late_contained_faults = 0u;
    fm1_audio_late_over_one_half = 0u;
    fm1_audio_late_over_two_halves = 0u;
    fm1_audio_late_resync_skips = 0u;
#if defined(BR_SPLIT_AUDIO)
    br_audio_late_drop_reset();
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    fm1_audio_tail_metrics_reset();
#endif
#if FELUCCA_UAC
    uac.underruns = 0u;
    uac.overruns = 0u;
    uac.adj_up = 0u;
    uac.adj_down = 0u;
#endif
    br_audio_source_job_timing_reset();
    irq_restore(irq);
}
#endif
static uint32_t app_audio(int32_t *out, uint32_t n);
void br_engine_pipeline_source_deadline_set(uint32_t absolute_tick);
void br_engine_pipeline_source_deadline_clear(void);
static void app_main(void);

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    in_audio = 1;
    p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
            /* A failed CPU1 reset confirmation leaves an output buffer owned
             * by an unknown core. Stop all further buffer access until reboot. */
            if (br_engine_pipeline_uncontained_fault_get()) {
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
                if (br_midi_clock_enabled) br_engine_clock_dma_invalid();
#endif
                fm1_audio_ack_half();
                in_audio = 0;
                return;
            }
        uint32_t render_start, render_ticks, half, render_result;
#if FM1_RUNTIME_IRQ_PROFILE || FM1_RUNTIME_CHUNK_PROFILE || FM1_DRUM_PROFILE
        uint32_t profile_start_irq = br_foreground_irq_save();
#if FM1_RUNTIME_IRQ_PROFILE
        fm1_runtime_irq_profile_begin();
#endif
        render_start = fm1_ticks();
#if FM1_RUNTIME_CHUNK_PROFILE
        fm1_runtime_trace_half_begin(render_start);
#endif
#if FM1_DRUM_PROFILE
        fm1_drum_profile_half_begin(render_start);
#endif
        br_foreground_irq_restore(profile_start_irq);
#else
        render_start = fm1_ticks();
#endif
        half = fm1_audio_free_half();
        if (fm1_audio_late_resync_active) {
            if (half == fm1_audio_late_resync_baseline) {
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
                if (br_midi_clock_enabled) br_engine_clock_dma_invalid();
#endif
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
                ++fm1_audio_late_resync_skips;
#endif
                fm1_audio_ack_half();
                in_audio = 0;
                return;
            }
            fm1_audio_late_resync_active = 0u;
        }
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        if (br_midi_clock_enabled) {
            if (br_engine_pipeline_mode_get()) br_engine_clock_dma_invalid();
            else
                br_engine_clock_dma_edge(render_start, half, HALF_FRAMES);
        }
#endif
        int32_t *o = &abuf[half * HALF_WORDS];
        uint32_t i;
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
        fm1_audio_cpu1_join_tick = 0u;
        fm1_audio_cpu1_join_valid = 0u;
#endif
        uac_render_start();
        br_engine_pipeline_source_deadline_set(
            render_start + BR_AUDIO_PIPELINE_SOURCE_BUDGET_TICKS);
        render_result = platform_audio_render_half(o, HALF_FRAMES);
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        if (br_midi_clock_enabled && render_result != FM1_AUDIO_RENDER_OK) br_engine_clock_dma_invalid();
#endif
        if (render_result == FM1_AUDIO_RENDER_UNCONTAINED) {
            br_engine_pipeline_source_deadline_clear();
            fm1_audio_ack_half();
            in_audio = 0;
            return; /* preserve the possibly CPU1-owned output memory */
        }
        br_engine_pipeline_source_deadline_clear();               /* guard and DSP profiles remain once per half */
        render_ticks = fm1_ticks() - render_start;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
        fm1_audio_metrics.last_ticks = render_ticks;
        if (fm1_audio_metrics.last_ticks > fm1_audio_metrics.worst_ticks)
            fm1_audio_metrics.worst_ticks = fm1_audio_metrics.last_ticks;
        if (fm1_audio_metrics.last_ticks > FM1_AUDIO_HALF_TICKS)
            ++fm1_audio_metrics.overruns;
        ++fm1_audio_metrics.blocks;
#endif
        fm1_audio_metrics.window_sum += render_ticks;
        ++fm1_audio_metrics.window_count;
        if (render_result == FM1_AUDIO_RENDER_DROP_LATE ||
            render_result == FM1_AUDIO_RENDER_CONTAINED_FAULT) {
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
            if (render_result == FM1_AUDIO_RENDER_DROP_LATE) {
                ++fm1_audio_late_drops;
                if (render_ticks > FM1_AUDIO_HALF_TICKS)
                    ++fm1_audio_late_over_one_half;
                if (render_ticks > 2u * FM1_AUDIO_HALF_TICKS)
                    ++fm1_audio_late_over_two_halves;
            } else {
                ++fm1_audio_late_contained_faults;
            }
#endif
            fm1_audio_ack_half();
            /* Take the baseline after ACK so a phase switch during the ACK
             * cannot make the just-observed half appear to be a fresh event. */
            fm1_audio_late_resync_baseline = fm1_audio_free_half();
            fm1_audio_late_resync_active = 1u;
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
            fm1_audio_cpu1_join_valid = 0u;
            fm1_audio_tail_last_wait_to_ack = 0xffffffffu;
#endif
            in_audio = 0;
            return; /* no output accesses or complete-half diagnostic seals */
        }
        fm1_audio_ack_half();
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
        {
#if FM1_RUNTIME_IRQ_PROFILE || FM1_RUNTIME_CHUNK_PROFILE || FM1_DRUM_PROFILE
            uint32_t profile_seal_irq = br_foreground_irq_save();
#endif
            uint32_t ack_tick = fm1_ticks();
            uint32_t half_duration = ack_tick - render_start;
            fm1_audio_tail_last_half_duration = half_duration;
            if (half_duration > fm1_audio_tail_max_half_duration) {
                fm1_audio_tail_max_half_duration = half_duration;
#if FM1_RUNTIME_IRQ_PROFILE
                fm1_runtime_irq_profile_seal(half_duration,
                                             fm1_audio_metrics.last_ticks);
#endif
            }
#if FM1_RUNTIME_CHUNK_PROFILE
            /* platform_audio_render_half() returned only after its ownership
             * checks succeeded. A full set of internal audio-chunk commits
             * distinguishes a complete callback from fallback output; the
             * trace module also validates every declared job and marker. */
            if (audio_block_commits == HALF_FRAMES / BR_AUDIO_BLOCK_FRAMES)
                fm1_runtime_trace_half_seal(half_duration,
                                            fm1_audio_metrics.last_ticks);
#endif
#if FM1_DRUM_PROFILE
            /* `join_valid` is set only after the CPU1 worker has returned
             * through its confirmed join path. Full output-buffer commits
             * alone can also describe a contained fallback half. */
            if (fm1_audio_cpu1_join_valid &&
                audio_block_commits == HALF_FRAMES / BR_AUDIO_BLOCK_FRAMES)
                fm1_drum_profile_half_seal(half_duration,
                                           fm1_audio_metrics.last_ticks);
#endif
            if (fm1_audio_cpu1_join_valid &&
                audio_block_commits == HALF_FRAMES / BR_AUDIO_BLOCK_FRAMES) {
                uint32_t tail = ack_tick - fm1_audio_cpu1_join_tick;
                fm1_audio_tail_last_wait_to_ack = tail;
                if (tail > fm1_audio_tail_max_wait_to_ack)
                    fm1_audio_tail_max_wait_to_ack = tail;
                ++fm1_audio_tail_valid_halves;
#if defined(FM1_TAIL_DETAIL)
                fm1_tail_detail_capture(fm1_audio_cpu1_join_tick, ack_tick);
#endif
            } else {
                fm1_audio_cpu1_join_valid = 0u;
                fm1_audio_tail_last_wait_to_ack = 0xffffffffu;
            }
#if FM1_RUNTIME_IRQ_PROFILE || FM1_RUNTIME_CHUNK_PROFILE || FM1_DRUM_PROFILE
            br_foreground_irq_restore(profile_seal_irq);
#endif
        }
#endif
    }
    in_audio = 0;
}
extern void isr_alnk0(void);

/* ---- the main loop's duties: watchdog, USB, the installer's update request, the MASTER knob */
static void platform_poll(void)
{
    static int32_t knob_avg = 512 * 16;
    static uint32_t battery_last_try, battery_started, knob_last_ms = 0xffffffffu;
    int32_t a;
    fm1_wdt_feed();
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    fm1_foreground_journal_store_poll(fm1_ticks());
#endif
    usb_retry(fm1_ms);
    ota_service();
    if (usb.ota_req) {
        usb.ota_req = 0;
        if (flash_ok)
            ota_session();                      /* returns only if nothing was committed */
    }
    if (usb.uboot_req) {
        if (flash_worker_park()) return;          /* never leave XIP with the worker running */
        fm1_audio_stop();
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 110, 240, 2u, "UBOOT (USB)", C_WHITE, 1);
        usb_detach();
        fm1_delay_ms(30);
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    if (fm1_cpu1_early_reboot_pending &&
        (int32_t)(fm1_ms - fm1_cpu1_early_reboot_at) >= 0) {
        fm1_cpu1_early_reboot_pending = 0u;
        bootguard_clear(&bootguard);
        usb_detach();
        fm1_delay_ms(10u);
        fm1_reboot();
    }
    if (fm1_ms >= 30000u && bootguard.pending)
        bootguard_clear(&bootguard);            /* 30 s up: this firmware boots */
    if (!battery_started || (uint32_t)(fm1_ms - battery_last_try) >= 1000u) {
        battery_started = 1u;
        battery_last_try = fm1_ms;
        /* Slow, main-loop polling only; ADC work never enters the audio ISR. */
        fm1_battery_sample(&fm1_battery, fm1_adc_read(FM1_ADC_BATT), fm1_ms);
    }
    /* The volume knob is read once a millisecond, not on every pass of the loop: each
     * read busy-waits for a conversion. The 8-step average now spans about 8 ms. */
    if (fm1_ms != knob_last_ms && (a = fm1_adc_read(FM1_ADC_MASTER)) >= 0) {
        uint32_t k10;
        knob_last_ms = fm1_ms;
        knob_avg += (a * 16 - knob_avg) / 8;
        k10 = (uint32_t)(knob_avg / 16);
        master_q12 = fm1_master_gain_update_live(&master_gain_control,
                                                  (int32_t)((k10 * k10) >> 8));
    }
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t i, t0, v[3] = {c->vec, c->pc, c->rets};
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 20, 240, 2u, APP_NAME " CRASH", C_WHITE, 1);
    for (i = 0; i < 3u; i++) {
        uint32_t k;
        for (k = 0; k < 8u; k++) b[k] = "0123456789ABCDEF"[(v[i] >> (28u - 4u * k)) & 15u];
        b[8] = 0;
        draw_text_box(10, 60 + i * 20, 220, 2u, b, C_WHITE, 0);
    }
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 8000u * 1000u * FM1_TICKS_PER_US)
        fm1_wdt_feed();
    fm1_reboot();
}

static void fm1_cpu1_early_snapshot_take(void)
{
    uint32_t r[8];
    fm1_cpu1_early_snapshot.magic = 0u;
    fm1_cpu1_early_hw_barrier();
    fm1_cpu1_early_hw_snapshot(r);
    fm1_cpu1_early_snapshot.dbg_en = r[0];
    fm1_cpu1_early_snapshot.c0_wr_limit_en = r[1];
    fm1_cpu1_early_snapshot.pc0_l = r[2];
    fm1_cpu1_early_snapshot.pc0_h = r[3];
    fm1_cpu1_early_snapshot.pc1_l = r[4];
    fm1_cpu1_early_snapshot.pc1_h = r[5];
    fm1_cpu1_early_snapshot.c1_con = r[6];
    fm1_cpu1_early_snapshot.entry = r[7];
    fm1_cpu1_early_hw_barrier();
    fm1_cpu1_early_snapshot.magic = FM1_CPU1_EARLY_SNAPSHOT_MAGIC;
    fm1_cpu1_early_hw_barrier();
}

static int fm1_cpu1_early_restore_interrupted(void)
{
    uint32_t c;
    if (fm1_cpu1_early_snapshot.magic != FM1_CPU1_EARLY_SNAPSHOT_MAGIC ||
        fm1_cpu1_early_record.magic != FM1_CPU1_EARLY_RESULT_MAGIC ||
        fm1_cpu1_early_record.state != FM1_CPU1_EARLY_IN_PROGRESS ||
        fm1_cpu1_early_record.integrity != fm1_cpu1_early_integrity(
            fm1_cpu1_early_record.state, fm1_cpu1_early_record.saved_control,
            fm1_cpu1_early_record.saved_entry) ||
        fm1_cpu1_early_snapshot.dbg_en != 0u ||
        fm1_cpu1_early_snapshot.c0_wr_limit_en != 0u ||
        !fm1_cpu1_early_pc_windows_match(fm1_cpu1_early_snapshot.pc0_l,
                                         fm1_cpu1_early_snapshot.pc0_h,
                                         fm1_cpu1_early_snapshot.pc1_l,
                                         fm1_cpu1_early_snapshot.pc1_h) ||
        !(fm1_cpu1_early_record.saved_control & FM1_CPU1_RESET_BIT) ||
        (fm1_cpu1_early_record.saved_control & FM1_CPU1_START_BIT))
        return 0;
    c = fm1_cpu1_early_hw_read_control();
    if (!(c & FM1_CPU1_RESET_BIT)) {
        fm1_cpu1_early_hw_write_control((c & ~FM1_CPU1_START_BIT) | FM1_CPU1_RESET_BIT);
        fm1_cpu1_early_hw_barrier();
        c = fm1_cpu1_early_hw_read_control();
        if (!(c & FM1_CPU1_RESET_BIT)) return 0;
    }
    fm1_cpu1_early_hw_write_entry(fm1_cpu1_early_record.saved_entry);
    fm1_cpu1_early_hw_barrier();
    if (fm1_cpu1_early_hw_read_entry() != fm1_cpu1_early_record.saved_entry) return 0;
    fm1_cpu1_early_hw_write_control(fm1_cpu1_early_record.saved_control);
    fm1_cpu1_early_hw_barrier();
    if (fm1_cpu1_early_hw_read_control() != fm1_cpu1_early_record.saved_control) return 0;
    fm1_cpu1_early_record.restored = 1u;
    fm1_cpu1_early_record.state = FM1_CPU1_EARLY_IDLE;
    fm1_cpu1_early_hw_barrier();
    fm1_cpu1_early_record.integrity = fm1_cpu1_early_integrity(
        FM1_CPU1_EARLY_IDLE, fm1_cpu1_early_record.saved_control,
        fm1_cpu1_early_record.saved_entry);
    fm1_cpu1_early_hw_barrier();
    return 1;
}

static int fm1_cpu1_early_bootstrap(uint32_t boot_mode)
{
    uint32_t cookie_kind = fm1_cpu1_early_cookie_consume(&fm1_cpu1_early_cookie.magic,
                                                        &fm1_cpu1_early_cookie.inverse);
    uint32_t control, recovered = 0u, idle_journal_valid, retry_suppressed = 0u;
    /* The cookie helper consumes before snapshotting or touching CPU1. */
    fm1_cpu1_early_snapshot_take();

    if (fm1_cpu1_early_record.magic == FM1_CPU1_EARLY_RESULT_MAGIC &&
        fm1_cpu1_early_record.state == FM1_CPU1_EARLY_IN_PROGRESS) {
        if (fm1_cpu1_early_restore_interrupted()) {
            fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_RECOVERED;
            fm1_cpu1_early_hw_barrier();
            /* The first snapshot was taken after pre-BSS reset and can still
             * contain the resident entry. Capture the verified restored C1
             * tuple before considering another ordinary autostart. */
            fm1_cpu1_early_snapshot_take();
            control = fm1_cpu1_autostart_after_restore(
                cookie_kind, boot_mode == BOOT_RECOVERY || boot_mode == BOOT_ROM, 1u);
            if (control == FM1_CPU1_AUTOSTART_RESTORE_RETURN) return 1;
            if (control == FM1_CPU1_AUTOSTART_RESTORE_CONTINUE) recovered = 1u;
            else return 0;
        } else {
            if ((fm1_cpu1_early_record.integrity != fm1_cpu1_early_integrity(
                     FM1_CPU1_EARLY_IN_PROGRESS, fm1_cpu1_early_record.saved_control,
                     fm1_cpu1_early_record.saved_entry) ||
                 fm1_cpu1_early_snapshot.magic != FM1_CPU1_EARLY_SNAPSHOT_MAGIC) &&
                (fm1_cpu1_early_hw_read_control() & FM1_CPU1_RESET_BIT) &&
                !(fm1_cpu1_early_hw_read_control() & FM1_CPU1_START_BIT)) {
                /* A corrupt stale journal is repairable only when C1 is already
                 * held reset. No C1 entry/control write is made here. */
                fm1_cpu1_early_record.magic = 0u;
                fm1_cpu1_early_record.state = FM1_CPU1_EARLY_IDLE;
                fm1_cpu1_early_record.saved_control = fm1_cpu1_early_snapshot.c1_con;
                fm1_cpu1_early_record.saved_entry = fm1_cpu1_early_snapshot.entry;
                fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_RESTORE_FAILED;
                fm1_cpu1_early_record.restored = 1u;
                fm1_cpu1_early_record.integrity = fm1_cpu1_early_integrity(
                    FM1_CPU1_EARLY_IDLE, fm1_cpu1_early_record.saved_control,
                    fm1_cpu1_early_record.saved_entry);
                fm1_cpu1_early_hw_barrier();
                fm1_cpu1_early_record.magic = FM1_CPU1_EARLY_RESULT_MAGIC;
                fm1_cpu1_early_hw_barrier();
                return 1;
            }
            fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_RESTORE_FAILED;
            fm1_cpu1_early_hw_barrier();
            return 0; /* fail closed: do not enable runtime guards/audio */
        }
    }
    idle_journal_valid = fm1_cpu1_autostart_idle_journal_valid(
        fm1_cpu1_early_record.magic, fm1_cpu1_early_record.state,
        fm1_cpu1_early_record.integrity, FM1_CPU1_EARLY_RESULT_MAGIC,
        FM1_CPU1_EARLY_IDLE,
        fm1_cpu1_early_integrity(FM1_CPU1_EARLY_IDLE,
                                 fm1_cpu1_early_record.saved_control,
                                 fm1_cpu1_early_record.saved_entry));
    if (idle_journal_valid)
        retry_suppressed = fm1_cpu1_autostart_retry_suppressed(
            cookie_kind, fm1_cpu1_early_record.status,
            fm1_cpu1_early_record.restored, FM1_CPU1_SMOKE_RESTORE_FAILED);
    if (!fm1_cpu1_autostart_should_launch(
            cookie_kind,
            boot_mode == BOOT_RECOVERY || boot_mode == BOOT_ROM,
            retry_suppressed)) {
        if (fm1_cpu1_early_record.magic != FM1_CPU1_EARLY_RESULT_MAGIC ||
            fm1_cpu1_early_record.state != FM1_CPU1_EARLY_IDLE ||
            fm1_cpu1_early_record.integrity != fm1_cpu1_early_integrity(
                FM1_CPU1_EARLY_IDLE, fm1_cpu1_early_record.saved_control,
                fm1_cpu1_early_record.saved_entry)) {
            fm1_cpu1_early_record.magic = 0u;
            fm1_cpu1_early_record.state = FM1_CPU1_EARLY_IDLE;
            fm1_cpu1_early_record.saved_control = fm1_cpu1_early_snapshot.c1_con;
            fm1_cpu1_early_record.saved_entry = fm1_cpu1_early_snapshot.entry;
            fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_NOT_RUN;
            fm1_cpu1_early_record.heartbeat = 0u;
            fm1_cpu1_early_record.cpu_id = 0xFFFFFFFFu;
            fm1_cpu1_early_record.restored = 1u;
            fm1_cpu1_early_record.precondition_flags = 0u;
            fm1_cpu1_early_record.integrity = fm1_cpu1_early_integrity(
                fm1_cpu1_early_record.state, fm1_cpu1_early_record.saved_control,
                fm1_cpu1_early_record.saved_entry);
            fm1_cpu1_early_hw_barrier();
            fm1_cpu1_early_record.magic = FM1_CPU1_EARLY_RESULT_MAGIC;
            fm1_cpu1_early_hw_barrier();
        }
        /* A failed auto-launch is skipped for one boot only. Resetting this
         * marker here prevents an endless serial-fallback loop while allowing
         * the next ordinary boot to retry. */
        if (idle_journal_valid && cookie_kind != FM1_CPU1_EARLY_COOKIE_START &&
            fm1_cpu1_early_record.status == FM1_CPU1_SMOKE_RESTORE_FAILED &&
            fm1_cpu1_early_record.restored) {
            fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_NOT_RUN;
            fm1_cpu1_early_hw_barrier();
        }
        return 1;
    }

    /* Default healthy boots use the same journaled early launch as the
     * explicit 0x74 one-shot. Recovery/ROM modes and 0x7C bypass are handled
     * above, before any C1 write. */
    if (cookie_kind == FM1_CPU1_EARLY_COOKIE_NONE || recovered) {
        fm1_cpu1_early_record.magic = 0u;
        fm1_cpu1_early_record.state = FM1_CPU1_EARLY_REQUESTED;
        fm1_cpu1_early_record.saved_control = fm1_cpu1_early_snapshot.c1_con;
        fm1_cpu1_early_record.saved_entry = fm1_cpu1_early_snapshot.entry;
        fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_NOT_RUN;
        fm1_cpu1_early_record.heartbeat = 0u;
        fm1_cpu1_early_record.cpu_id = 0xFFFFFFFFu;
        fm1_cpu1_early_record.restored = 0u;
        fm1_cpu1_early_record.precondition_flags = 0u;
        fm1_cpu1_early_record.integrity = fm1_cpu1_early_integrity(
            fm1_cpu1_early_record.state, fm1_cpu1_early_record.saved_control,
            fm1_cpu1_early_record.saved_entry);
        fm1_cpu1_early_hw_barrier();
        fm1_cpu1_early_record.magic = FM1_CPU1_EARLY_RESULT_MAGIC;
        fm1_cpu1_early_hw_barrier();
    }

    if (fm1_cpu1_early_snapshot.magic != FM1_CPU1_EARLY_SNAPSHOT_MAGIC ||
        fm1_cpu1_early_record.magic != FM1_CPU1_EARLY_RESULT_MAGIC ||
        fm1_cpu1_early_record.state != FM1_CPU1_EARLY_REQUESTED ||
        fm1_cpu1_early_record.integrity != fm1_cpu1_early_integrity(
            fm1_cpu1_early_record.state, fm1_cpu1_early_record.saved_control,
            fm1_cpu1_early_record.saved_entry) ||
        fm1_cpu1_early_record.saved_control != fm1_cpu1_early_snapshot.c1_con ||
        fm1_cpu1_early_record.saved_entry != fm1_cpu1_early_snapshot.entry) {
        FM1_EARLY_TRACE(launch_stage, 2u); /* request/journal refused */
        FM1_EARLY_TRACE(post_control, fm1_cpu1_early_hw_read_control());
        fm1_cpu1_early_record.state = FM1_CPU1_EARLY_IDLE;
        fm1_cpu1_early_hw_barrier();
        fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_BAD_LIMIT;
        fm1_cpu1_early_record.restored = 1u;
        fm1_cpu1_early_record.saved_control = fm1_cpu1_early_snapshot.c1_con;
        fm1_cpu1_early_record.saved_entry = fm1_cpu1_early_snapshot.entry;
        fm1_cpu1_early_record.integrity = fm1_cpu1_early_integrity(
            FM1_CPU1_EARLY_IDLE, fm1_cpu1_early_record.saved_control,
            fm1_cpu1_early_record.saved_entry);
        fm1_cpu1_early_hw_barrier();
        return 1; /* request consumed; no C1 writes */
    }
    /* 0=not run, 1=accepted, 2=invalid request, 3=profile refused,
     * 4=entry write, 5=entry verified, 6=release/readback, 7=wait,
     * 8=ready, 9=launch/restore failure. */
    FM1_EARLY_TRACE(launch_stage, 1u);
    FM1_EARLY_TRACE(post_control, fm1_cpu1_early_snapshot.c1_con);
    FM1_EARLY_TRACE(wait_rc, 0xFFFFFFFFu);
    FM1_EARLY_TRACE(mailbox_id, 0xFFFFFFFFu);
    FM1_EARLY_TRACE(mailbox_state, 0xFFFFFFFFu);
    FM1_EARLY_TRACE(worker_heartbeat, 0u);
    fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_BAD_LIMIT;
    fm1_cpu1_early_record.heartbeat = 0u;
    fm1_cpu1_early_record.cpu_id = 0xFFFFFFFFu;
    fm1_cpu1_early_record.restored = 0u;
    fm1_cpu1_early_record.precondition_flags =
        (fm1_cpu1_early_snapshot.dbg_en == 0u ? 1u : 0u) |
        (fm1_cpu1_early_snapshot.c0_wr_limit_en == 0u ? 2u : 0u) |
        ((fm1_cpu1_early_snapshot.c1_con & FM1_CPU1_RESET_BIT) ? 4u : 0u) |
        (!(fm1_cpu1_early_snapshot.c1_con & FM1_CPU1_START_BIT) ? 8u : 0u) |
        (fm1_cpu1_early_snapshot.pc0_l == FM1_CPU1_EARLY_PC0_L ? 16u : 0u) |
        (fm1_cpu1_early_snapshot.pc0_h == FM1_CPU1_EARLY_PC0_H ? 32u : 0u) |
        (fm1_cpu1_early_snapshot.pc1_l == FM1_CPU1_EARLY_PC1_L ? 64u : 0u) |
        (fm1_cpu1_early_snapshot.pc1_h == FM1_CPU1_EARLY_PC1_H ? 128u : 0u);
    fm1_cpu1_early_record.integrity = fm1_cpu1_early_integrity(
        FM1_CPU1_EARLY_IN_PROGRESS, fm1_cpu1_early_record.saved_control,
        fm1_cpu1_early_record.saved_entry);
    fm1_cpu1_early_hw_barrier();
    fm1_cpu1_early_record.state = FM1_CPU1_EARLY_IN_PROGRESS;
    fm1_cpu1_early_hw_barrier();
    if (!fm1_cpu1_autostart_preflight_ok(fm1_cpu1_early_record.precondition_flags)) {
        FM1_EARLY_TRACE(launch_stage, 3u);
        FM1_EARLY_TRACE(post_control, fm1_cpu1_early_hw_read_control());
        fm1_cpu1_early_hw_barrier();
        fm1_cpu1_early_record.state = FM1_CPU1_EARLY_IDLE;
        fm1_cpu1_early_hw_barrier();
        fm1_cpu1_early_record.integrity = fm1_cpu1_early_integrity(
            FM1_CPU1_EARLY_IDLE, fm1_cpu1_early_record.saved_control,
            fm1_cpu1_early_record.saved_entry);
        fm1_cpu1_early_record.restored = 1u; /* no C1 writes were made */
        fm1_cpu1_early_hw_barrier();
        return 1;
    }
    /* One-shot launch only. The record remains IN_PROGRESS for the worker's
     * entire residency; next boot holds C1 in reset and restores the saved
     * entry/control before ordinary guards and audio can start. */
    fm1_cpu1_worker_init_early();
    FM1_EARLY_TRACE(launch_stage, 4u);
    fm1_cpu1_early_hw_barrier();
    const uint32_t worker_start_entry =
        (uint32_t)(uintptr_t)fm1_cpu1_worker_xip_cache_trampoline;
    fm1_cpu1_early_hw_write_entry(worker_start_entry);
    fm1_cpu1_early_hw_barrier();
    if (fm1_cpu1_early_hw_read_entry() != worker_start_entry)
        goto worker_launch_failed;
    FM1_EARLY_TRACE(launch_stage, 5u);
    control = fm1_cpu1_early_hw_read_control();
    FM1_EARLY_TRACE(launch_stage, 6u);
    fm1_cpu1_early_hw_barrier();
    fm1_cpu1_early_hw_write_control(control | FM1_CPU1_WORKER_START_BIT);
    fm1_cpu1_early_hw_write_control((control | FM1_CPU1_WORKER_START_BIT) &
                                    ~FM1_CPU1_WORKER_RESET_BIT);
    fm1_cpu1_early_hw_barrier();
    control = fm1_cpu1_early_hw_read_control();
    FM1_EARLY_TRACE(post_control, control);
    if (control & FM1_CPU1_WORKER_RESET_BIT)
        goto worker_launch_failed;
    FM1_EARLY_TRACE(launch_stage, 7u);
    fm1_cpu1_early_hw_barrier();
    control = fm1_cpu1_worker_wait_ready(FM1_CPU1_WORKER_TIMEOUT_TICKS);
    FM1_EARLY_TRACE(wait_rc, control);
    FM1_EARLY_TRACE(mailbox_id, fm1_cpu1_worker_mailbox.cpu_id);
    FM1_EARLY_TRACE(mailbox_state, fm1_cpu1_worker_mailbox.state);
    FM1_EARLY_TRACE(worker_heartbeat, fm1_cpu1_worker_mailbox.heartbeat);
    if (control != FM1_CPU1_WORKER_OK)
        goto worker_launch_failed;
    FM1_EARLY_TRACE(launch_stage, 8u);
    fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_OK;
    fm1_cpu1_early_record.heartbeat = fm1_cpu1_worker_mailbox.heartbeat;
    fm1_cpu1_early_record.cpu_id = fm1_cpu1_worker_mailbox.cpu_id;
    fm1_cpu1_early_record.restored = 0u;
    fm1_cpu1_early_hw_barrier();
    return 1;

worker_launch_failed:
    FM1_EARLY_TRACE(launch_stage, 9u);
    FM1_EARLY_TRACE(post_control, fm1_cpu1_early_hw_read_control());
    FM1_EARLY_TRACE(mailbox_id, fm1_cpu1_worker_mailbox.cpu_id);
    FM1_EARLY_TRACE(mailbox_state, fm1_cpu1_worker_mailbox.state);
    FM1_EARLY_TRACE(worker_heartbeat, fm1_cpu1_worker_mailbox.heartbeat);
    fm1_cpu1_early_hw_barrier();
    if (!fm1_cpu1_early_restore_interrupted()) return 0;
    fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_RESTORE_FAILED;
    fm1_cpu1_early_record.restored = 1u;
    fm1_cpu1_early_hw_barrier();
    return 1;
}

static uint32_t fm1_cpu1_early_cookie_arm(void)
{
    if (fm1_cpu1_early_snapshot.magic != FM1_CPU1_EARLY_SNAPSHOT_MAGIC ||
        fm1_cpu1_early_reboot_pending ||
        fm1_cpu1_early_record.magic != FM1_CPU1_EARLY_RESULT_MAGIC ||
        fm1_cpu1_early_record.state != FM1_CPU1_EARLY_IDLE ||
        fm1_cpu1_early_record.integrity != fm1_cpu1_early_integrity(
            FM1_CPU1_EARLY_IDLE, fm1_cpu1_early_record.saved_control,
            fm1_cpu1_early_record.saved_entry))
        return 0u;
    fm1_cpu1_early_record.magic = 0u;
    fm1_cpu1_early_record.state = FM1_CPU1_EARLY_REQUESTED;
    fm1_cpu1_early_record.saved_control = fm1_cpu1_early_snapshot.c1_con;
    fm1_cpu1_early_record.saved_entry = fm1_cpu1_early_snapshot.entry;
    fm1_cpu1_early_record.status = FM1_CPU1_SMOKE_NOT_RUN;
    fm1_cpu1_early_record.heartbeat = 0u;
    fm1_cpu1_early_record.cpu_id = 0xFFFFFFFFu;
    fm1_cpu1_early_record.restored = 0u;
    fm1_cpu1_early_record.precondition_flags = 0u;
    fm1_cpu1_early_record.integrity = fm1_cpu1_early_integrity(
        fm1_cpu1_early_record.state, fm1_cpu1_early_record.saved_control,
        fm1_cpu1_early_record.saved_entry);
    fm1_cpu1_early_hw_barrier();
    fm1_cpu1_early_record.magic = FM1_CPU1_EARLY_RESULT_MAGIC;
    fm1_cpu1_early_hw_barrier();
    fm1_cpu1_early_cookie_store(&fm1_cpu1_early_cookie.magic,
                                &fm1_cpu1_early_cookie.inverse,
                                FM1_CPU1_EARLY_COOKIE_START);
    fm1_cpu1_early_reboot_at = fm1_ms + 100u;
    fm1_cpu1_early_reboot_pending = 1u;
    return 1u;
}

/* Explicit quiet reboot for recovering a resident/faulted worker. Preserve a
 * valid IN_PROGRESS journal so the next boot holds C1 and restores its entry. */
static uint32_t fm1_cpu1_early_reboot_cold(uint32_t mode)
{
    uint32_t id = fm1_cpu1_worker_mailbox.cpu_id;
    uint32_t parked = id == 1u &&
        fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_PARKED &&
        fm1_cpu1_worker_mailbox.park_ack;
    if (!fm1_cpu1_reboot_mode_valid(mode) || fm1_cpu1_early_reboot_pending) return 0u;
    if (fm1_cpu1_worker_fault) {
        if (!fm1_cpu1_worker_reset_held()) return 0u;
    } else if (id == 1u) {
        if (fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_READY) {
            if (fm1_cpu1_worker_park(FM1_CPU1_WORKER_TIMEOUT_TICKS) != FM1_CPU1_WORKER_OK)
                return 0u;
            parked = fm1_cpu1_worker_mailbox.state == FM1_CPU1_WORKER_PARKED &&
                     fm1_cpu1_worker_mailbox.park_ack;
        }
        if (!parked && !fm1_cpu1_worker_reset_held()) return 0u;
    } else if (fm1_cpu1_early_record.state == FM1_CPU1_EARLY_IN_PROGRESS &&
               !fm1_cpu1_worker_reset_held()) {
        return 0u;
    }
    if (mode == 0u) {
        fm1_cpu1_early_cookie_store(&fm1_cpu1_early_cookie.magic,
                                    &fm1_cpu1_early_cookie.inverse,
                                    FM1_CPU1_EARLY_COOKIE_BYPASS);
    } else {
        /* Normal reboot: only after verified park/reset, cancel any stale
         * one-shot cookie so the next boot follows ordinary autostart policy. */
        fm1_cpu1_early_cookie.magic = 0u;
        fm1_cpu1_early_cookie.inverse = 0u;
        fm1_cpu1_early_hw_barrier();
    }
    fm1_cpu1_early_reboot_at = fm1_ms + 100u;
    fm1_cpu1_early_reboot_pending = 1u;
    fm1_cpu1_early_hw_barrier();
    return 1u;
}

/* NOINIT-only early recovery: a resident C1 can still be using its BSS stack
 * while C0 restarts, so stop it before vector, BSS, pool, or RAM-code writes. */
static int fm1_cpu1_early_hold_before_bss(void)
{
    uint32_t c;
    if (fm1_cpu1_early_record.state != FM1_CPU1_EARLY_IN_PROGRESS)
        return 1;
    if (fm1_cpu1_early_record.magic != FM1_CPU1_EARLY_RESULT_MAGIC ||
        fm1_cpu1_early_record.integrity != fm1_cpu1_early_integrity(
            FM1_CPU1_EARLY_IN_PROGRESS, fm1_cpu1_early_record.saved_control,
            fm1_cpu1_early_record.saved_entry) ||
        fm1_cpu1_early_snapshot.magic != FM1_CPU1_EARLY_SNAPSHOT_MAGIC ||
        fm1_cpu1_early_snapshot.dbg_en != 0u ||
        fm1_cpu1_early_snapshot.c0_wr_limit_en != 0u ||
        !fm1_cpu1_early_pc_windows_match(fm1_cpu1_early_snapshot.pc0_l,
                                         fm1_cpu1_early_snapshot.pc0_h,
                                         fm1_cpu1_early_snapshot.pc1_l,
                                         fm1_cpu1_early_snapshot.pc1_h) ||
        fm1_cpu1_early_record.saved_control != fm1_cpu1_early_snapshot.c1_con ||
        fm1_cpu1_early_record.saved_entry != fm1_cpu1_early_snapshot.entry ||
        !(fm1_cpu1_early_record.saved_control & FM1_CPU1_RESET_BIT) ||
        (fm1_cpu1_early_record.saved_control & FM1_CPU1_START_BIT)) {
        c = fm1_cpu1_early_hw_read_control();
        return (c & FM1_CPU1_RESET_BIT) && !(c & FM1_CPU1_START_BIT);
    }
    c = fm1_cpu1_early_hw_read_control();
    if (!(c & FM1_CPU1_RESET_BIT) || (c & FM1_CPU1_START_BIT)) {
        fm1_cpu1_early_hw_write_control((c & ~FM1_CPU1_START_BIT) | FM1_CPU1_RESET_BIT);
        fm1_cpu1_early_hw_barrier();
        c = fm1_cpu1_early_hw_read_control();
    }
    return (c & FM1_CPU1_RESET_BIT) && !(c & FM1_CPU1_START_BIT);
}

#if defined(FM1_DEVICE) && FM1_RAM_IMAGE_CRC_DIAG
static uint32_t fm1_ram_image_crc_record[FM1_RAM_IMAGE_CRC_DIAG_WORDS];

/* Called once after .data/.ram_text/.ram_audio copies and before the early
 * bootstrap can release C1. There is deliberately no command-supplied address,
 * length, or manual rescan path. */
static void fm1_ram_image_crc_capture(uint32_t boot_mode)
{
    extern uint32_t _rt_start[], _rt_end[], _rt_load[];
    extern uint32_t _ra_start[], _ra_end[], _ra_load[];
    uintptr_t rt_start = (uintptr_t)_rt_start;
    uintptr_t rt_end = (uintptr_t)_rt_end;
    uintptr_t rt_load = (uintptr_t)_rt_load;
    uintptr_t ra_start = (uintptr_t)_ra_start;
    uintptr_t ra_end = (uintptr_t)_ra_end;
    uintptr_t ra_load = (uintptr_t)_ra_load;
    uint32_t rt_length = 0u, ra_length = 0u;
    uint32_t rt_valid, ra_valid, control, held, state, journal_valid;
    uint32_t branch_flags, match_mask = 0u, i;

    for (i = 0u; i < FM1_RAM_IMAGE_CRC_DIAG_WORDS; ++i)
        fm1_ram_image_crc_record[i] = 0u;

    control = fm1_cpu1_early_hw_read_control();
    held = (control & FM1_CPU1_RESET_BIT) != 0u &&
           (control & FM1_CPU1_START_BIT) == 0u;
    state = fm1_cpu1_early_record.state;
    journal_valid =
        state == FM1_CPU1_EARLY_IN_PROGRESS &&
        fm1_cpu1_early_record.magic == FM1_CPU1_EARLY_RESULT_MAGIC &&
        fm1_cpu1_early_record.integrity == fm1_cpu1_early_integrity(
            FM1_CPU1_EARLY_IN_PROGRESS, fm1_cpu1_early_record.saved_control,
            fm1_cpu1_early_record.saved_entry) &&
        fm1_cpu1_early_snapshot.magic == FM1_CPU1_EARLY_SNAPSHOT_MAGIC &&
        fm1_cpu1_early_snapshot.dbg_en == 0u &&
        fm1_cpu1_early_snapshot.c0_wr_limit_en == 0u &&
        fm1_cpu1_early_pc_windows_match(fm1_cpu1_early_snapshot.pc0_l,
                                        fm1_cpu1_early_snapshot.pc0_h,
                                        fm1_cpu1_early_snapshot.pc1_l,
                                        fm1_cpu1_early_snapshot.pc1_h) &&
        fm1_cpu1_early_record.saved_control == fm1_cpu1_early_snapshot.c1_con &&
        fm1_cpu1_early_record.saved_entry == fm1_cpu1_early_snapshot.entry &&
        (fm1_cpu1_early_record.saved_control & FM1_CPU1_RESET_BIT) != 0u &&
        (fm1_cpu1_early_record.saved_control & FM1_CPU1_START_BIT) == 0u;
    branch_flags = (state == FM1_CPU1_EARLY_IN_PROGRESS ?
                    FM1_RAM_IMAGE_CRC_BRANCH_EARLY_IN_PROGRESS : 0u) |
                   (journal_valid ?
                    FM1_RAM_IMAGE_CRC_BRANCH_RECOVERY_JOURNAL_VALID : 0u) |
                   (held ? FM1_RAM_IMAGE_CRC_BRANCH_C1_RESET_HELD : 0u);

    rt_valid = fm1_ram_image_crc_range_valid((uintptr_t)_rt_start,
                                              (uintptr_t)_rt_end,
                                              (uintptr_t)_rt_load,
                                              &rt_length);
    ra_valid = fm1_ram_image_crc_range_valid((uintptr_t)_ra_start,
                                              (uintptr_t)_ra_end,
                                              (uintptr_t)_ra_load,
                                              &ra_length);
    fm1_ram_image_crc_record[0] = 1u;
    fm1_ram_image_crc_record[2] = FM1_RAM_IMAGE_CRC_SCOPE_RAM_BYTES_ONLY;
    fm1_ram_image_crc_record[3] = boot_mode;
    fm1_ram_image_crc_record[4] = branch_flags;
    fm1_ram_image_crc_record[5] = state;
    fm1_ram_image_crc_record[6] = journal_valid;
    fm1_ram_image_crc_record[7] = control;
    fm1_ram_image_crc_record[8] = (uint32_t)rt_start;
    fm1_ram_image_crc_record[9] = (uint32_t)rt_end;
    fm1_ram_image_crc_record[10] = (uint32_t)rt_load;
    fm1_ram_image_crc_record[11] = rt_length;
    fm1_ram_image_crc_record[14] = (uint32_t)ra_start;
    fm1_ram_image_crc_record[15] = (uint32_t)ra_end;
    fm1_ram_image_crc_record[16] = (uint32_t)ra_load;
    fm1_ram_image_crc_record[17] = ra_length;

    fm1_cpu1_early_hw_barrier();
    if (!held) {
        fm1_ram_image_crc_record[1] =
            FM1_RAM_IMAGE_CRC_STATUS_SKIPPED_C1_LIVE;
        fm1_cpu1_early_hw_barrier();
        return;
    }
    if (!rt_valid || !ra_valid) {
        fm1_ram_image_crc_record[1] = FM1_RAM_IMAGE_CRC_STATUS_BAD_RANGE;
        fm1_cpu1_early_hw_barrier();
        return;
    }

    fm1_ram_image_crc_record[12] =
        FL_FAR(crc32)((const uint8_t *)_rt_start, rt_length);
    fm1_ram_image_crc_record[13] =
        FL_FAR(crc32)((const uint8_t *)_rt_load, rt_length);
    fm1_ram_image_crc_record[18] =
        FL_FAR(crc32)((const uint8_t *)_ra_start, ra_length);
    fm1_ram_image_crc_record[19] =
        FL_FAR(crc32)((const uint8_t *)_ra_load, ra_length);
    if (fm1_ram_image_crc_record[12] == fm1_ram_image_crc_record[13])
        match_mask |= 1u;
    if (fm1_ram_image_crc_record[18] == fm1_ram_image_crc_record[19])
        match_mask |= 2u;
    fm1_ram_image_crc_record[20] = match_mask;
    fm1_cpu1_early_hw_barrier();
    fm1_ram_image_crc_record[1] = match_mask == 3u ?
        FM1_RAM_IMAGE_CRC_STATUS_MATCH : FM1_RAM_IMAGE_CRC_STATUS_MISMATCH;
    fm1_cpu1_early_hw_barrier();
}

void fm1_ram_image_crc_diag_snapshot(uint32_t out[FM1_RAM_IMAGE_CRC_DIAG_WORDS])
{
    uint32_t i;
    if (!out) return;
    fm1_cpu1_early_hw_barrier();
    for (i = 0u; i < FM1_RAM_IMAGE_CRC_DIAG_WORDS; ++i)
        out[i] = fm1_ram_image_crc_record[i];
}
#endif

#if defined(FM1_WAVE_BOOT_TIMING_PROBE) && FM1_WAVE_BOOT_TIMING_PROBE
static volatile uint32_t fm1_wave_boot_timing[4];
#endif
void fm1_cstart(void)
{
    extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
    extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
    extern uint32_t _ra_start[], _ra_end[], _ra_load[];
    uint32_t *s, *d, p3, rst_src, boot_mode, previous_journal_mask, wdt_value;
    uint8_t wdt_con;
#if defined(FM1_WAVE_BOOT_TIMING_PROBE) && FM1_WAVE_BOOT_TIMING_PROBE
    uint32_t wave_watchdog_tick, wave_decode_tick, wave_end_tick;
#endif
    fm1_foreground_journal_t previous_journal;
    fm1_ram_code_guard_worker_snapshot_t ram_guard_worker;
    uint32_t early_p33[6], early_p33_i;
    fm1_power_read_sample_t extra_entry;
    fm1_p33_checked_t extra_ctx, reset_ctx, wdt_ctx, early_ctx;
    uint32_t vp_executed,vp_rescue,vp_rc,prior_errors,next_boot_id,vp_sequence,early_clock[8];
    fm1_vp_target_t vp_target;
    fm1_vp_io_t vp_io;
    fm1_vp_report_t vp_report;
    volatile fm1_vpd_attempt_t *vp_attempt;
    /* First C startup action: old BSS samples must never label this boot. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    fm1_power_read_entry_ready = 0u;
    fm1_vpd_ready = 0u;
#endif
    fm1_fault_ram_code_barrier();
    vp_executed=0u;vp_rescue=0u;
    extra_ctx.timeouts=0u;reset_ctx.timeouts=0u;wdt_ctx.timeouts=0u;early_ctx.timeouts=0u;
    fm1_p33_timeouts=0u;
    /* The no-journal hold helper may merely return success, so verify the
     * actual hardware state before any checked reset/P33 transfer. */
    if(!fm1_cpu1_early_hold_before_bss() || !fm1_cpu1_worker_hw_reset_held())
        for (;;) { }
    /* TIMER4 is shared with audio deadlines. Repair its configuration only
     * after a prior worker is confirmed held; this hold path uses no timer. */
    fm1_time_init();
    p3=FM1_POWER_READ_UNKNOWN;wdt_value=FM1_POWER_READ_UNKNOWN;rst_src=fm1_reset_source_read();
    if(!fm1_p33_checked_read(&reset_ctx,FM1_P3_RST_SRC,&p3) ||
       !fm1_p33_checked_read(&reset_ctx,FM1_P3_WDT_CON,&wdt_value) ||
       reset_ctx.timeouts || !fm1_cpu1_worker_hw_reset_held())
        for (;;) { } /* no further P33 writes on an untrusted transaction */
    wdt_con=(uint8_t)wdt_value;
    /* P33 0x17 is changed by watchdog setup. Never add P33 accesses while
     * an old C1 may still be live; mark the whole sample unavailable instead. */
    fm1_power_read_invalidate(&extra_entry);
    if (fm1_cpu1_worker_hw_reset_held()) {
        fm1_power_read_collect(&extra_entry, fm1_p33_checked_read,
                               &extra_ctx, &extra_ctx.timeouts);
        if (!fm1_cpu1_worker_hw_reset_held()) fm1_power_read_invalidate(&extra_entry);
    }
    if(extra_entry.timeouts==FM1_POWER_READ_UNKNOWN || extra_entry.timeouts ||
       !fm1_cpu1_worker_hw_reset_held()) for (;;) { }
    /* Preserve a validated prior-boot journal in a stack local before BSS is
     * cleared. It is copied to the new BSS snapshot after initialization. */
    previous_journal_mask = fm1_foreground_journal_snapshot(
        &fm1_foreground_journal, &previous_journal);
    if(!fm1_wdt_arm_checked(&wdt_ctx,0x0d) || wdt_ctx.timeouts ||
       !fm1_cpu1_worker_hw_reset_held()) for (;;) { }
#if defined(FM1_WAVE_BOOT_TIMING_PROBE) && FM1_WAVE_BOOT_TIMING_PROBE
    wave_watchdog_tick = fm1_ticks();
#endif
    if (!reset_ctx.timeouts && (p3 & 1u) && !(p3 & (4u | 0x40u)))
        bootguard_clear(&bootguard);
    boot_mode = bootguard_begin(&bootguard);
    if (!fm1_cpu1_early_hold_before_bss() || !fm1_cpu1_worker_hw_reset_held())
        for (;;) { }                           /* watchdog reset; never clear a live worker stack */
    /* C1 is held and interrupts are off. Use only stack-local timeout state
     * before BSS initialization; no selector register is written. */
    early_p33[0] = 0u;
    early_p33[1] = 0u;
    for(early_p33_i=2u;early_p33_i<6u;early_p33_i++)early_p33[early_p33_i]=FM1_POWER_READ_UNKNOWN;
    if(fm1_p33_checked_read(&early_ctx,0x00u,&early_p33[2]) &&
       fm1_p33_checked_read(&early_ctx,0x05u,&early_p33[3]) &&
       fm1_p33_checked_read(&early_ctx,0x06u,&early_p33[4]))
        (void)fm1_p33_checked_read(&early_ctx,0x09u,&early_p33[5]);
    early_p33[1]=early_ctx.timeouts;
    if(!fm1_cpu1_worker_hw_reset_held())for(;;) { }
    if(boot_mode==BOOT_NORMAL) {
        fm1_vp_target_init(&vp_target);
        fm1_vp_target_set_reset_profile(&vp_target,p3,rst_src);
        if(fm1_vpd_requires_recovery(fm1_vpd_attempts)) {
            if(!fm1_vp_target_timer(&vp_target))for(;;) { }
            vp_rescue=1u;
        } else {
            prior_errors=reset_ctx.timeouts || wdt_ctx.timeouts || extra_ctx.timeouts ||
                early_ctx.timeouts || fm1_p33_timeouts ||
                extra_entry.timeouts==FM1_POWER_READ_UNKNOWN;
            next_boot_id=1u;
            if(fm1_foreground_journal_header_valid(&previous_journal)) {
                next_boot_id=previous_journal.boot_id+1u;
                if(!next_boot_id || next_boot_id==FM1_FOREGROUND_JOURNAL_UNKNOWN)next_boot_id=1u;
            }
            fm1_clock_snapshot(early_clock);
            vp_attempt=fm1_vpd_attempt_begin(fm1_vpd_attempts,next_boot_id,p3,rst_src,
                                            prior_errors,early_clock[3]&0x0fu,&extra_entry,&vp_sequence);
            if(!fm1_vpd_attempt_matches(vp_attempt,vp_sequence,next_boot_id,p3,rst_src,prior_errors,
                                        early_clock[3]&0x0fu,&extra_entry)) {
                if(!fm1_vp_target_timer(&vp_target))for(;;) { }
                vp_rescue=1u;
            } else {
                vp_io=fm1_vp_target_ops(&vp_target);
                fm1_vp_target_mask_irqs();
                vp_rc=fm1_vp_run(&vp_io,prior_errors,&vp_report);
                fm1_vpd_attempt_finish(fm1_vpd_attempts,vp_attempt,vp_rc);
                vp_executed=1u;
                if(!fm1_cpu1_worker_hw_reset_held())for(;;) { }
                if(vp_rc!=FM1_VP_OK || !fm1_vpd_completion_valid(fm1_vpd_attempts,vp_attempt,vp_rc)) {
                    if(!fm1_vp_target_timer(&vp_target))for(;;) { }
                    vp_rescue=1u;
                }
            }
        }
    }
    if (boot_mode == BOOT_ROM &&
        !(fm1_cpu1_early_record.magic == FM1_CPU1_EARLY_RESULT_MAGIC &&
          fm1_cpu1_early_record.state == FM1_CPU1_EARLY_IN_PROGRESS))
        fm1_enter_uboot();
    /* fm1_irq_init writes vectors into the protected top-of-RAM window. Refuse
     * before that write if the C0 write guard survived reset. DBG_EN is checked
     * as a CPU1-probe precondition; ordinary no-cookie boot remains unchanged. */
    if (fm1_cpu1_early_hw_top_write_guard())
        for (;;) { }
    fm1_irq_init();
    /* Validate the linker layout before touching a persistent fixed-range
     * guard, then clear only a matching slot before any startup code copy. */
    if (!fm1_ram_code_guard_prepare_copy((uint32_t)(uintptr_t)_rt_start,
                                         (uint32_t)(uintptr_t)_rt_end,
                                         (uint32_t)(uintptr_t)_ra_start,
                                         (uint32_t)(uintptr_t)_ra_end))
        for (;;) { }
    for (d = _bss_start; d < _bss_end; d++) *d = 0;
    for (d = _pool_start; d < _pool_end; d++) *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++) *d = *s;
    for (s = _ra_load, d = _ra_start; d < _ra_end; s++, d++) *d = *s;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    for (early_p33_i = 0u; early_p33_i < 6u; ++early_p33_i)
        fm1_early_p33_snapshot[early_p33_i] = early_p33[early_p33_i];
    fm1_power_read_entry.timeouts = extra_entry.timeouts;
    for (early_p33_i = 0u; early_p33_i < FM1_POWER_READ_COUNT; ++early_p33_i)
        fm1_power_read_entry.raw[early_p33_i] = extra_entry.raw[early_p33_i];
    fm1_fault_ram_code_barrier();
    fm1_power_read_entry_ready = FM1_POWER_READ_READY;
    fm1_fault_ram_code_barrier();
    if(vp_executed) {
        volatile uint32_t *dst=(volatile uint32_t *)&fm1_vpd_report;
        const uint32_t *src=(const uint32_t *)&vp_report;
        for(early_p33_i=0u;early_p33_i<sizeof(vp_report)/4u;early_p33_i++)dst[early_p33_i]=src[early_p33_i];
        fm1_fault_ram_code_barrier();fm1_vpd_ready=FM1_VPD_READY;fm1_fault_ram_code_barrier();
    }
#endif
    if(vp_rescue) {
        fm1_boot.p3_rst=(uint8_t)p3;fm1_boot.rst_src=rst_src;fm1_boot.wdt_con=wdt_con;
        fm1_foreground_journal_begin(&previous_journal,previous_journal_mask);
        recovery_main();for(;;) { }
    }
#if defined(FM1_DEVICE) && FM1_RAM_IMAGE_CRC_DIAG
    fm1_ram_image_crc_capture(boot_mode);
#endif
#if FM1_PLAITS_PACK_WAVES
    /* Reconstruct the immutable waveform resource before either core can
     * create a voice. The bounded decoder checks every sample through CRC. */
#if defined(FM1_WAVE_BOOT_TIMING_PROBE) && FM1_WAVE_BOOT_TIMING_PROBE
    wave_decode_tick = fm1_ticks();
#endif
    if (!fm1_plaits_waves_init(0))
        for (;;) { }                           /* watchdog; never use a partial resource */
#if defined(FM1_WAVE_BOOT_TIMING_PROBE) && FM1_WAVE_BOOT_TIMING_PROBE
    wave_end_tick = fm1_ticks();
    fm1_wave_boot_timing[0] = 1u;
    fm1_wave_boot_timing[1] = wave_end_tick - wave_decode_tick;
    fm1_wave_boot_timing[2] = wave_end_tick - wave_watchdog_tick;
    fm1_wave_boot_timing[3] = 0x0du;
#endif
    fm1_memory_publish_sync();
#endif
    /* Diagnostic C1-only write guard: C1 is still held here; install before
     * fm1_cpu1_early_bootstrap can start the worker. */
    (void)fm1_guard_c1_stack_install();
    if (!fm1_cpu1_early_bootstrap(boot_mode))
        for (;;) { }                           /* watchdog reset; never resume audio unverified */
    /* Snapshot the idle worker after bootstrap. Slot 4 can be enabled while
     * C1 runs only when this exact mailbox state proves it is quiescent. */
    fm1_cpu1_early_hw_barrier();
    ram_guard_worker.state = fm1_cpu1_worker_mailbox.state;
    ram_guard_worker.cpu_id = fm1_cpu1_worker_mailbox.cpu_id;
    ram_guard_worker.request_seq = fm1_cpu1_worker_mailbox.request_seq;
    ram_guard_worker.done_seq = fm1_cpu1_worker_mailbox.done_seq;
    ram_guard_worker.park_request = fm1_cpu1_worker_mailbox.park_request;
    ram_guard_worker.worker_fault = fm1_cpu1_worker_fault;
    fm1_cpu1_early_hw_barrier();
    if (!fm1_ram_code_guard_install((uint32_t)(uintptr_t)_rt_start,
                                    (uint32_t)(uintptr_t)_rt_end,
                                    (uint32_t)(uintptr_t)_ra_start,
                                    (uint32_t)(uintptr_t)_ra_end,
                                    &ram_guard_worker))
        for (;;) { }
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    br_fx_progress_bootstrap();
#endif
    if (boot_mode == BOOT_ROM)
        fm1_enter_uboot();
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = rst_src;
    fm1_boot.wdt_con = wdt_con;
    fm1_foreground_journal_begin(&previous_journal, previous_journal_mask);
    /* The input matrix requires the LCD backlight pin configured first.
     * Leave panel initialization and flash cleanup until after rescue entry. */
    fm1_lcd_enable_backlight();
    if (boot_mode == BOOT_RECOVERY || recovery_key())
        recovery_main();                        /* OCT- at power-on: the USB rescue */
    if (flash_worker_park() == 0) {
        flash_ok = fl_jedec_known(FL_FAR(fl_jedec_ram)());
        (void)flash_worker_resume();
    } else {
        flash_ok = 0u;
    }
    ota_boot_cleanup();
    lcd_init();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    fm1_input_init();
    fm1_adc_init();
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);   /* (abuf is zero: .bss) */
    usb_start();
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    app_main();
    for (;;)
        platform_poll();
}
#endif
