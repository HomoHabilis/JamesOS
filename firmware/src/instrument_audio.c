#ifndef BR_AUDIO_FUSION_FOUR_WAY_KIT
#define BR_AUDIO_FUSION_FOUR_WAY_KIT 1
#endif
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
#define BR_AUDIO_DIAGNOSTIC_SOURCE_SLOTS 4u
#else
#define BR_AUDIO_DIAGNOSTIC_SOURCE_SLOTS 2u
#endif
#if defined(FM1_DEVICE)
#include "../hal/fm1_time.h"
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
volatile uint32_t br_diagnostic_synth_ticks;
#endif
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
volatile uint32_t br_tail_stamp[BR_TAIL_STAMPS];
volatile uint32_t br_gap_stamp[BR_GAP_STAMPS];
#endif
static uint32_t br_diagnostic_source_cost[4][BR_AUDIO_DIAGNOSTIC_SOURCE_SLOTS];
#if defined(BR_CPU1_AUDIO)
#include "../hal/fm1_cpu1_worker.h"
volatile uint32_t br_audio_source_worker_fault;
#endif
#endif
#if (defined(BR_DIAGNOSTIC_TEST) || defined(BR_SOURCE_QUEUE_TEST)) && \
    !defined(FM1_DEVICE)
/* Host protocol tests seed the existing producer storage. This does not add
 * target storage; production keeps the array under FM1_DEVICE above. */
static uint32_t br_diagnostic_source_cost[4][BR_AUDIO_DIAGNOSTIC_SOURCE_SLOTS];
#endif
#if defined(BR_SOURCE_QUEUE_TEST) && !defined(FM1_DEVICE)
#include "../hal/fm1_cpu1_worker.h"
#endif
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST) || \
    defined(BR_SOURCE_QUEUE_TEST)
/* Per-part scratch for source job timing. */
static uint32_t br_diagnostic_part_synth_ticks[4][BR_AUDIO_DIAGNOSTIC_SOURCE_SLOTS];
#endif
#if defined(BR_SOURCE_QUEUE_TEST) && !defined(FM1_DEVICE)
volatile uint32_t br_audio_source_worker_fault;
#endif
/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-loop events become sound only in the audio interrupt. */
#include "instrument.h"
#include "instrument_math.h"
#if defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
#include "pattern_publication_irq.h"
#include "pattern_bar_clock.h"
/* Experimental single-core IRQ handoff; target validation is mandatory. */
#if defined(FM1_DEVICE) && FM1_DEVICE
#define BR_PATTERN_BSS __attribute__((section(".bss.pattern_transport")))
#define BR_PATTERN_DATA __attribute__((section(".data.pattern_transport")))
#else
#define BR_PATTERN_BSS
#define BR_PATTERN_DATA
#endif
static br_pattern_irq_publication *br_pattern_host_publication BR_PATTERN_BSS;
static const br_pattern_snapshot *br_pattern_host_snapshot BR_PATTERN_BSS;
static fm1_pattern_bar_clock br_pattern_host_clock BR_PATTERN_BSS;
static unsigned br_pattern_host_due BR_PATTERN_BSS;
static unsigned br_pattern_host_retired BR_PATTERN_DATA = BR_PATTERN_IRQ_NO_SLOT;
static volatile uint32_t br_pattern_host_bars BR_PATTERN_BSS;
static volatile uint32_t br_pattern_host_error BR_PATTERN_BSS;
static volatile uint32_t br_pattern_irq_gate BR_PATTERN_BSS;
static volatile uint32_t br_pattern_irq_return_request BR_PATTERN_BSS;
static volatile uint32_t br_pattern_irq_foreground_busy BR_PATTERN_BSS;
static volatile uint32_t br_pattern_irq_late_blocks BR_PATTERN_BSS;
static uint32_t br_pattern_irq_internal_quarter BR_PATTERN_BSS;
#undef BR_PATTERN_BSS
#undef BR_PATTERN_DATA
static void br_pattern_host_reset_clock(void);
static void br_pattern_host_clock_shift(uint64_t shift);
#define BR_PLAY_FIELD(p, f) (br_pattern_host_snapshot ? br_pattern_host_snapshot->f[p] : br_part[p].f)
#define BR_PLAY_STEP(p, i) (br_pattern_host_snapshot ? br_pattern_host_snapshot->step[p][i] : br_part[p].step[i])
#define BR_PLAY_META(name, a, b) (br_pattern_host_snapshot ? br_pattern_host_snapshot->name[a][b] : br_seq_##name[a][b])
static inline int32_t br_play_micro(uint32_t p, uint32_t i, uint32_t l)
{ return !br_pattern_host_snapshot ? br_step_micro_get(p,i,l) : p == BR_DRUM_PART ? br_pattern_host_snapshot->kit_micro[i][l] : br_pattern_host_snapshot->synth_micro[p][i]; }
static inline uint32_t br_play_condition(uint32_t p, uint32_t i, uint32_t l)
{ return !br_pattern_host_snapshot ? br_step_condition_get(p,i,l) : p == BR_DRUM_PART ? br_pattern_host_snapshot->kit_condition[i][l] : br_pattern_host_snapshot->synth_condition[p][i]; }
static inline uint32_t br_play_length(uint32_t p, uint32_t i, uint32_t l)
{ uint32_t n; if (!br_pattern_host_snapshot) return br_step_length_get(p,i,l); n = p == BR_DRUM_PART ? br_pattern_host_snapshot->kit_length[i][l] : br_pattern_host_snapshot->synth_length[p][i]; return p != BR_DRUM_PART && !n ? BR_STEP_LENGTH_DEFAULT : n; }
static inline uint32_t br_play_pitch(uint32_t p, uint32_t i, uint32_t l)
{ uint32_t n; if (!br_pattern_host_snapshot) return br_step_note_get(p,i,l); n = br_pattern_host_snapshot->kit_pitch[i][l]; return n >= 36u && n <= 84u ? n : BR_STEP_PITCH_DEFAULT; }
#else
#define BR_PLAY_FIELD(p, f) br_part[p].f
#define BR_PLAY_STEP(p, i) br_part[p].step[i]
#define BR_PLAY_META(name, a, b) br_seq_##name[a][b]
#define br_play_micro br_step_micro_get
#define br_play_condition br_step_condition_get
#define br_play_length br_step_length_get
#define br_play_pitch br_step_note_get
#endif
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
#include "midi_clock_scheduler.h"
#include "midi_clock_arp.h"
#include "midi_clock.h"
#include "midi_clock_timeline.h"
#include "midi_clock_diag.h"
#endif
#include "../hal/fm1_cpu1_worker_ownership.h"
#include "audio_deadline.h"
#include "instrument_kit_source_controls.h"
#include "../hal/fm1_runtime_trace.h"
#ifndef BR_AUDIO_KIT_KEY_WRITE_ELISION
#define BR_AUDIO_KIT_KEY_WRITE_ELISION 1
#endif
#if BR_AUDIO_KIT_KEY_WRITE_ELISION != 0 && BR_AUDIO_KIT_KEY_WRITE_ELISION != 1
#error "BR_AUDIO_KIT_KEY_WRITE_ELISION must be 0 or 1"
#endif
#if defined(FM1_DEVICE) && FM1_RUNTIME_CHUNK_PROFILE
#define BR_AUDIO_RUNTIME_TRACE_ENABLED 1
#else
#define BR_AUDIO_RUNTIME_TRACE_ENABLED 0
#endif
#ifdef BR_SPLIT_AUDIO
#include "../hal/fm1_fault_diag.h"
#include "../hal/fm1_memory_sync.h"
#endif
#ifndef BR_SPLIT_AUDIO
/* A monolithic source build has no deferred source pipeline. */
#define br_pipeline_enabled 0u
#endif
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
#include "../plaits/plaits_c_api.h"
#include "../plaits/plaits_kit_c_api.h"
#endif
#include "instrument_drums.h"
/* Pitched tracks use Plaits only. */
#include "instrument_drums.c"
#include "instrument_filter.c"
#include "instrument_modulation.c"
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

#define BR_EVENT_CAP 64u
#define BR_EVENT_MASK (BR_EVENT_CAP - 1u)
#define BR_EVENT_ON 1u
#define BR_EVENT_OFF 2u
#define BR_EVENT_STOP 3u
static void br_arp_reset(void);
#define BR_SEQ_TRACKS (BR_SYNTH_PARTS + BR_DRUM_LANES)
#define BR_ENV_OFF 0u
#define BR_ENV_ATTACK 1u
#define BR_ENV_DECAY 2u
#define BR_ENV_SUSTAIN 3u
#define BR_ENV_RELEASE 4u
#define BR_KIT_LANES_A 0x03u /* Logical kick and snare lanes. */
#define BR_KIT_LANES_B 0x0cu /* Logical hat and cymbal/percussion lanes. */
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
#define BR_AUDIO_KIT_JOB_LANES BR_DRUM_LANES
#else
#define BR_AUDIO_KIT_JOB_LANES 2u
#endif

/* Experiment switch, selectable by a candidate's compile flags. */
#ifndef BR_AUDIO_LPT_COST_SNAPSHOT
#define BR_AUDIO_LPT_COST_SNAPSHOT 1
#endif
#ifndef BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR
#define BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR 0
#endif
#define BR_AUDIO_NATIVE_RENDER_MAX 32u
#ifndef BR_AUDIO_POST_SKIP_UNUSED_SUM
#define BR_AUDIO_POST_SKIP_UNUSED_SUM 1
#endif
#ifndef BR_AUDIO_POST_POW2_MEAN
#define BR_AUDIO_POST_POW2_MEAN 1
#endif
#ifndef BR_AUDIO_POST_GAIN_Q15_APPROX
#define BR_AUDIO_POST_GAIN_Q15_APPROX 1
#endif
#ifndef BR_AUDIO_ENV_CURVE_CONTROL_KEY
#define BR_AUDIO_ENV_CURVE_CONTROL_KEY 1
#endif
#ifndef BR_AUDIO_POST_GAIN_SUSTAIN_FOLD
#define BR_AUDIO_POST_GAIN_SUSTAIN_FOLD 1
#endif

/* Constant Q6 preset calibration, bounded by full-velocity macro sweeps. */
#ifndef BR_AUDIO_Q6_CENTER_TRIM
#define BR_AUDIO_Q6_CENTER_TRIM 1
#endif
#if BR_AUDIO_Q6_CENTER_TRIM
static const uint16_t br_audio_preset_gain_q6[BR_PRESET_COUNT] = {
    39u, 20u, 51u, 37u, 58u, 18u, 47u, 34u, 58u, 31u, 18u, 31u, 25u,
    83u, 65u, 31u, 40u, 41u, 58u, 46u, 45u, 32u, 52u, 121u, 61u, 25u,
    25u, 27u, 83u, 64u, 94u, 65u, 76u, 64u, 58u, 60u, 58u, 37u, 30u,
    54u, 80u, 61u, 71u, 59u, 60u, 110u, 25u, 255u, 59u, 84u, 84u, 55u,
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    /* The 24 models and the engine kit, calibrated from device recordings of each model at C5 on a neutral
     * patch (audit/neutral.json): every sustained model lands near -21 dBFS RMS, and no model peaks above
     * -9 dBFS. Q6 so 64 is unity; values above 255 need the 16-bit table. */
    171u, 101u, 69u, 82u, 114u, 89u, 278u, 112u, 134u, 237u, 113u, 158u, 185u,
    162u, 98u, 142u, 186u, 142u, 208u, 215u, 165u, 58u, 58u, 58u, 57u,
#endif
};
#else
static const uint16_t br_audio_preset_gain_q6[BR_PRESET_COUNT] = {
    64u, 38u, 58u, 58u, 58u, 44u, 58u, 46u, 79u, 60u, 47u, 60u, 59u,
    123u, 65u, 58u, 67u, 49u, 52u, 60u, 59u, 59u, 64u, 121u, 61u, 47u,
    50u, 51u, 83u, 64u, 94u, 65u, 76u, 64u, 58u, 60u, 58u, 59u, 58u,
    58u, 80u, 61u, 71u, 59u, 60u, 110u, 50u, 255u, 59u, 84u, 84u, 58u,
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    58u, 83u, 81u, 65u, 59u, 83u, 102u, 104u, 73u, 84u, 97u, 58u, 73u,
    58u, 58u, 58u, 58u, 58u, 58u, 65u, 58u, 58u, 58u, 58u, 57u,
#endif
};
#endif

static uint32_t br_audio_preset_gain(uint32_t preset)
{
    return preset < BR_PRESET_COUNT ? br_audio_preset_gain_q6[preset] : 64u;
}

#ifndef BR_AUDIO_FUSION_FAST_READY_SCAN
#define BR_AUDIO_FUSION_FAST_READY_SCAN 1
#endif
#ifndef BR_AUDIO_FUSION_NONBLOCKING_DRAIN
#define BR_AUDIO_FUSION_NONBLOCKING_DRAIN 1
#endif
#ifndef BR_AUDIO_FUSION_JOB_TIMING_DIAG
#define BR_AUDIO_FUSION_JOB_TIMING_DIAG 0
#endif
#ifndef BR_AUDIO_FUSION_PEAK_JOB_TIMING_DIAG
#define BR_AUDIO_FUSION_PEAK_JOB_TIMING_DIAG 0
#endif
#ifndef BR_AUDIO_FUSION_STAGE_TIMING_DIAG
#define BR_AUDIO_FUSION_STAGE_TIMING_DIAG 0
#endif
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG && !BR_AUDIO_FUSION_PEAK_JOB_TIMING_DIAG
#undef BR_AUDIO_FUSION_PEAK_JOB_TIMING_DIAG
#define BR_AUDIO_FUSION_PEAK_JOB_TIMING_DIAG 0
#endif
#if BR_AUDIO_FUSION_PEAK_JOB_TIMING_DIAG && !BR_AUDIO_FUSION_JOB_TIMING_DIAG
#undef BR_AUDIO_FUSION_JOB_TIMING_DIAG
#define BR_AUDIO_FUSION_JOB_TIMING_DIAG 0
#endif
#ifndef BR_AUDIO_FUSION_EWMA_SCHEDULER
#define BR_AUDIO_FUSION_EWMA_SCHEDULER 1
#endif
#ifndef BR_AUDIO_FUSION_KIT_LANE_ACTIVITY_CLASS
#define BR_AUDIO_FUSION_KIT_LANE_ACTIVITY_CLASS 1
#endif
#ifndef BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE
#define BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE 1
#endif
#ifndef BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY
#define BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY 1
#endif
#ifndef BR_AUDIO_FUSION_BRAIDS_COLD_FLOOR
#define BR_AUDIO_FUSION_BRAIDS_COLD_FLOOR 1
#endif
/* Private, default-off history keys that distinguish a cold model init from
 * steady rendering after the previous frame has joined. */
#ifndef BR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE
#define BR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE 1
#endif
#ifndef BR_AUDIO_FUSION_BRAIDS_COLD_RENDER_SIGNATURE
#define BR_AUDIO_FUSION_BRAIDS_COLD_RENDER_SIGNATURE 0
#endif
#ifndef BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE
#define BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE 1
#endif
#ifndef BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE
#define BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE 1
#endif
#ifndef BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE
#define BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE 1
#endif
#if BR_AUDIO_FUSION_KIT_LANE_ACTIVITY_CLASS != 0 && \
    BR_AUDIO_FUSION_KIT_LANE_ACTIVITY_CLASS != 1
#error "BR_AUDIO_FUSION_KIT_LANE_ACTIVITY_CLASS must be 0 or 1"
#endif
#if BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE != 0 && \
    BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE != 1
#error "BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE must be 0 or 1"
#endif
#if BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY != 0 && \
    BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY != 1
#error "BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY must be 0 or 1"
#endif
#if BR_AUDIO_FUSION_BRAIDS_COLD_FLOOR != 0 && \
    BR_AUDIO_FUSION_BRAIDS_COLD_FLOOR != 1
#error "BR_AUDIO_FUSION_BRAIDS_COLD_FLOOR must be 0 or 1"
#endif
#if BR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE != 0 && \
    BR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE != 1
#error "BR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE must be 0 or 1"
#endif
#if BR_AUDIO_FUSION_BRAIDS_COLD_RENDER_SIGNATURE != 0 && \
    BR_AUDIO_FUSION_BRAIDS_COLD_RENDER_SIGNATURE != 1
#error "BR_AUDIO_FUSION_BRAIDS_COLD_RENDER_SIGNATURE must be 0 or 1"
#endif
#if (BR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE || \
     BR_AUDIO_FUSION_BRAIDS_COLD_RENDER_SIGNATURE) && \
    !BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE
#error "cold-render signatures require pre-dispatch snapshot reuse"
#endif
#if BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY && \
    (!BR_AUDIO_FUSION_ACTIVITY_EWMA || !BR_AUDIO_FUSION_KIT_LANE_ACTIVITY_CLASS)
#error "BRAIDS lane history requires kit lane activity classification"
#endif
#if BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE != 0 && \
    BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE != 1
#error "BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE must be 0 or 1"
#endif
#if BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE != 0 && \
    BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE != 1
#error "BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE must be 0 or 1"
#endif
#if BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE != 0 && \
    BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE != 1
#error "BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE must be 0 or 1"
#endif
#if BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE && \
    !(BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE || \
      BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE || \
      BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE)
#error "signature snapshot reuse requires a signature snapshot"
#endif
#ifndef BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
#define BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR 1
#endif
#ifndef BR_AUDIO_FUSION_GUARDED_HISTORY
#define BR_AUDIO_FUSION_GUARDED_HISTORY 0
#endif
#ifndef BR_AUDIO_FUSION_GUARDED_HISTORY_PAD_DIVISOR
#define BR_AUDIO_FUSION_GUARDED_HISTORY_PAD_DIVISOR 8u
#endif
#if BR_AUDIO_FUSION_GUARDED_HISTORY && \
    (!BR_AUDIO_FUSION_EWMA_SCHEDULER || \
     !BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR)
#error "guarded history requires the robust EWMA scheduler"
#endif
#if BR_AUDIO_FUSION_GUARDED_HISTORY_PAD_DIVISOR < 1u
#error "guarded history padding divisor must be nonzero"
#endif
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR && !BR_AUDIO_FUSION_ACTIVITY_EWMA
#error "robust fusion costs require activity-aware job classification"
#endif
#if BR_AUDIO_FUSION_ACTIVITY_EWMA && !BR_AUDIO_FUSION_EWMA_SCHEDULER
#error "activity-aware costs require the whole-job EWMA scheduler"
#endif
#ifndef BR_AUDIO_FUSION_FIXED_PLAN
#define BR_AUDIO_FUSION_FIXED_PLAN 1
#endif
#ifndef BR_AUDIO_FUSION_FIXED_PLAN_FAST_CUTOFF
#define BR_AUDIO_FUSION_FIXED_PLAN_FAST_CUTOFF 1
#endif
#ifndef BR_AUDIO_FUSION_FIXED_PLAN_BALANCED_STARTUP
#define BR_AUDIO_FUSION_FIXED_PLAN_BALANCED_STARTUP 1
#endif
#ifndef BR_AUDIO_FUSION_FIXED_PLAN_FORCE_VALID_SIX
#define BR_AUDIO_FUSION_FIXED_PLAN_FORCE_VALID_SIX 1
#endif
#ifndef BR_AUDIO_FUSION_FIXED_PLAN_PART0_CPU0
#define BR_AUDIO_FUSION_FIXED_PLAN_PART0_CPU0 1
#endif
#ifndef BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT
#define BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT 1
#endif
#ifndef BR_AUDIO_FUSION_FIXED_PLAN_EIGHT_READY_QUEUE
#define BR_AUDIO_FUSION_FIXED_PLAN_EIGHT_READY_QUEUE 1
#endif
#ifndef BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
#define BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN 1
#endif
#ifndef BR_AUDIO_FUSION_LAZY_FX_COST_GETTER
#define BR_AUDIO_FUSION_LAZY_FX_COST_GETTER 1
#endif
#ifndef BR_AUDIO_FIXED_PLAN_SKIP_DUPLICATE_DISABLE
#define BR_AUDIO_FIXED_PLAN_SKIP_DUPLICATE_DISABLE 1
#endif
#if BR_AUDIO_FUSION_LAZY_FX_COST_GETTER != 0 && \
    BR_AUDIO_FUSION_LAZY_FX_COST_GETTER != 1
#error "BR_AUDIO_FUSION_LAZY_FX_COST_GETTER must be 0 or 1"
#endif
#if BR_AUDIO_FIXED_PLAN_SKIP_DUPLICATE_DISABLE != 0 && \
    BR_AUDIO_FIXED_PLAN_SKIP_DUPLICATE_DISABLE != 1
#error "BR_AUDIO_FIXED_PLAN_SKIP_DUPLICATE_DISABLE must be 0 or 1"
#endif
#ifndef BR_AUDIO_FUSION_FIXED8_HISTORY_MIN_OBSERVATIONS
#define BR_AUDIO_FUSION_FIXED8_HISTORY_MIN_OBSERVATIONS 2u
#endif
#ifndef BR_AUDIO_FUSION_FIXED8_PLAN_MIN_GAIN_TICKS
#define BR_AUDIO_FUSION_FIXED8_PLAN_MIN_GAIN_TICKS 1024u
#endif
#if BR_AUDIO_FIXED8_PLAN_DIAGNOSTIC_V2 && \
    (!BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN || !BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT || \
     !BR_AUDIO_FUSION_JOB_TIMING_DIAG)
#error "fixed-eight plan diagnostic requires the history planner and job timing"
#endif
#if BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN != 0 && \
    BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN != 1
#error "BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN must be 0 or 1"
#endif
#if BR_AUDIO_FUSION_FIXED8_HISTORY_MIN_OBSERVATIONS < 1u
#error "BR_AUDIO_FUSION_FIXED8_HISTORY_MIN_OBSERVATIONS must be positive"
#endif
#if BR_AUDIO_FUSION_FIXED8_PLAN_MIN_GAIN_TICKS < 1u
#error "BR_AUDIO_FUSION_FIXED8_PLAN_MIN_GAIN_TICKS must be positive"
#endif
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN && \
    (!BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT || \
     !BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN)
#error "foreground fixed-eight planning requires the validated 8-job planner and gain gate"
#endif

/* Optional cold-start Modal Plaits source prior. This is used only on the
 * history-miss path; learned per-core EWMA costs continue to take precedence.
 */
#ifndef BR_AUDIO_FUSION_MODAL_COLD_SOURCE_CHUNK_TICKS
#define BR_AUDIO_FUSION_MODAL_COLD_SOURCE_CHUNK_TICKS 0
#endif
#if BR_AUDIO_FUSION_MODAL_COLD_SOURCE_CHUNK_TICKS != 0u && \
    BR_AUDIO_FUSION_MODAL_COLD_SOURCE_CHUNK_TICKS != 15000u && \
    BR_AUDIO_FUSION_MODAL_COLD_SOURCE_CHUNK_TICKS != 18060u && \
    BR_AUDIO_FUSION_MODAL_COLD_SOURCE_CHUNK_TICKS != 20000u
#error "Modal cold source chunk must be disabled, 15000, 18060, or 20000 ticks"
#endif
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG && \
    !(BR_AUDIO_FUSION_JOB_TIMING_DIAG || BR_AUDIO_FUSION_EWMA_SCHEDULER)
#error "failure-job profile requires the existing per-job timing path"
#endif
#define BR_AUDIO_C1_LATE_OWNER_KNOWN \
    (BR_AUDIO_FUSION_JOB_TIMING_DIAG || BR_AUDIO_FUSION_EWMA_SCHEDULER)
#if BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT && \
    (!BR_AUDIO_FUSION_FIXED_PLAN || !BR_AUDIO_FUSION_FOUR_WAY_KIT)
#error "general eight-job fixed plan requires fixed planning and four-way kit jobs"
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT
#include "audio_fixed8_plan.h"
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN_PART0_CPU0 && \
    (!BR_AUDIO_FUSION_FIXED_PLAN || !BR_AUDIO_FUSION_FOUR_WAY_KIT)
#error "part-0 fixed plan requires the fixed planner and four-way kit jobs"
#endif
static volatile uint32_t br_audio_source_fx_fusion_active;
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
typedef struct {
    uint32_t sequence, epoch, frames, kit_part;
    uint32_t ready_queue_makespan, workclass_key;
    uint32_t role_desc[8];
    uint32_t independent_cost[7][2], finalizer_cost[2];
    uint8_t core_for_job[8], order_for_job[8], count_for_core[2];
    uint32_t solved_makespan;
} br_audio_foreground_fixed8_dto_t;
enum {
    BR_AUDIO_FOREGROUND_FIXED8_FREE = 0u,
    BR_AUDIO_FOREGROUND_FIXED8_INPUT = 1u,
    BR_AUDIO_FOREGROUND_FIXED8_SOLVING = 2u,
    BR_AUDIO_FOREGROUND_FIXED8_RESULT = 3u
};
static br_audio_foreground_fixed8_dto_t br_audio_foreground_fixed8_dto;
static volatile uint32_t br_audio_foreground_fixed8_state;
static volatile uint32_t br_audio_foreground_fixed8_epoch = 1u;
static uint32_t br_audio_foreground_fixed8_next_sequence;

void br_audio_fixed8_foreground_invalidate(void)
{
    uint32_t next = br_audio_foreground_fixed8_epoch + 1u;
    if (!next) next = 1u;
    RING_PUBLISH();
    br_audio_foreground_fixed8_epoch = next;
    RING_PUBLISH();
}
#endif

/* Worker jobs and their outputs must live in shared internal RAM rather than
 * CPU0's transient stack, so the resident CPU1 worker can safely dereference
 * the published job pointer. The audio ISR is the sole producer and joins all
 * jobs before returning, so one static workspace is sufficient. */
#ifdef BR_SPLIT_AUDIO
enum { BR_AUDIO_QUEUE_FAILED = 0u, BR_AUDIO_QUEUE_OK = 1u,
       BR_AUDIO_QUEUE_COMPLETE_LATE = 2u,
       BR_AUDIO_QUEUE_PUBLISHED_COMPLETE = 3u,
       BR_AUDIO_QUEUE_CUTOFF_DRAINED = 4u };
#if defined(FM1_DEVICE)
static br_audio_source_job_t br_audio_source_jobs[BR_AUDIO_MAX_SOURCE_JOBS]
    __attribute__((section(".pool"), aligned(4)));
#else
static br_audio_source_job_t br_audio_source_jobs[BR_AUDIO_MAX_SOURCE_JOBS];
#endif
static br_audio_source_queue_t br_audio_source_queue;
/* Split a synth track with heavy effects into a source job and an effects job,
 * so the two cores can balance stacked effects. The fused eight-job graph is
 * unchanged; a split graph appends one effects job per split track. */
#ifndef BR_AUDIO_SPLIT_TRACK_FX
#define BR_AUDIO_SPLIT_TRACK_FX 1
#endif
static uint32_t br_audio_graph_count_ok(uint32_t count)
{
    return count >= 8u && count <= 8u + (BR_AUDIO_SPLIT_TRACK_FX ? BR_SYNTH_PARTS + 1u : 0u);
}
/* The one job allowed to return after the deadline, all others complete: the kit
 * finalizer in the fused graph, or any terminal job (finalizer or track effects)
 * in a split graph. Nothing depends on a terminal job. */
static uint32_t br_audio_late_index_ok(uint32_t index, uint32_t count);
/* Published before the matching worker submit; stable until both actors
 * relinquish the current frame. Unsupported render paths leave it clear. */
static volatile uint32_t br_audio_source_publication_enabled;
static uint32_t br_audio_source_can_publish_late(uint32_t n, uint32_t count)
{
#if BR_AUDIO_FUSION_ONLY_BUILD
    return BR_AUDIO_C1_LATE_OWNER_KNOWN &&
        BR_AUDIO_FUSION_FOUR_WAY_KIT && n == BR_BLOCK &&
        BR_BLOCK == FM1_AUDIO_HALF_FRAMES && br_audio_graph_count_ok(count) &&
        br_audio_source_fx_fusion_active;
#else
    (void)n; (void)count;
    return 0u;
#endif
}
static br_audio_source_batch_t br_audio_legacy_batches[2];
static volatile uint32_t br_audio_source_legacy_mode = 1u;
typedef struct {
    uint32_t index;
    uint32_t returned_tick;
    uint32_t cutoff_tick;
} br_audio_source_late_t;
typedef struct {
    volatile uint32_t valid, index, returned_tick, cutoff_tick, deadline, sequence;
} br_audio_c1_late_record_t;
#define BR_AUDIO_C1_LATE_VALID 0x43314c54u
static br_audio_c1_late_record_t br_audio_c1_late_record;
/* Written only by the audio owner. Diagnostic readers exclude the audio IRQ. */
static volatile uint32_t br_audio_late_render;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
static volatile uint32_t br_audio_late_drop_count;
static volatile uint32_t br_audio_partial_drop_count;
static volatile uint32_t br_audio_late_last_index, br_audio_late_last_count;
static volatile uint32_t br_audio_late_last_deadline, br_audio_late_last_return;
static volatile uint32_t br_audio_late_last_sequence;
static volatile uint32_t br_audio_late_last_cutoff;
static volatile uint32_t br_audio_late_last_core;
#endif

uint32_t br_engine_late_drop_get(void) { return br_audio_late_render; }
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
void br_audio_late_drop_snapshot(uint32_t out[9])
{
    if (!out) return;
    out[0] = br_audio_late_drop_count;
    out[1] = br_audio_late_last_index;
    out[2] = br_audio_late_last_count;
    out[3] = br_audio_late_last_deadline;
    out[4] = br_audio_late_last_return;
    out[5] = br_audio_late_last_sequence;
    out[6] = br_audio_late_last_cutoff;
    out[7] = br_audio_late_last_core;
    out[8] = br_audio_partial_drop_count;
}
void br_audio_late_drop_reset(void)
{
    br_audio_late_drop_count = 0u;
    br_audio_partial_drop_count = 0u;
    br_audio_late_last_index = br_audio_late_last_count = 0u;
    br_audio_late_last_deadline = br_audio_late_last_return = 0u;
    br_audio_late_last_sequence = 0u;
    br_audio_late_last_cutoff = 0u;
    br_audio_late_last_core = 0u;
}
#endif
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
/* CPU0-owned BSS only. The C1-shared job fields are copied here after a
 * confirmed hold/reset and before any output or engine state is cleared. */
static br_audio_failure_job_profile_t br_audio_failure_job_profile;

static uint32_t br_audio_failure_job_profile_fixed_plan_mode(void)
{
    uint32_t mode = 0u;
#if BR_AUDIO_FUSION_FIXED_PLAN
    mode |= BR_AUDIO_FAILURE_PLAN_FIXED_ENABLED;
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT
    mode |= BR_AUDIO_FAILURE_PLAN_GENERAL_EIGHT;
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN_PART0_CPU0
    mode |= BR_AUDIO_FAILURE_PLAN_PART0_CPU0;
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN_FORCE_VALID_SIX
    mode |= BR_AUDIO_FAILURE_PLAN_FORCE_VALID_SIX;
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN_BALANCED_STARTUP
    mode |= BR_AUDIO_FAILURE_PLAN_BALANCED_STARTUP;
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN_FAST_CUTOFF
    mode |= BR_AUDIO_FAILURE_PLAN_FAST_CUTOFF;
#endif
    if (br_audio_source_queue.fixed_plan_active)
        mode |= BR_AUDIO_FAILURE_PLAN_ACTIVE;
    return mode;
}

static uint32_t br_audio_failure_job_profile_read_sample(
    void *context, uint32_t index, br_audio_failure_job_sample_t *out)
{
    const br_audio_source_job_t *job;
    (void)context;
    if (!out || index >= br_audio_source_queue.count ||
        index >= BR_AUDIO_MAX_SOURCE_JOBS) return 0u;
    job = &br_audio_source_jobs[index];
    out->state = job->state;
    out->kind = job->kind;
    out->part = job->part;
    out->executor_core = job->executor_core;
    out->frames = job->frames;
    out->cost_hint = job->cost_hint;
    out->duration_ticks = job->duration_ticks;
    out->start_tick = job->failure_profile_start_tick;
    out->profile_flags = job->failure_profile_flags;
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    out->stage_flags = job->failure_profile_stage_flags;
    out->stage_start_tick = job->failure_profile_stage_start_tick;
    out->source_ticks = job->failure_profile_source_ticks;
    out->merge_ticks = job->failure_profile_merge_ticks;
    out->post_ticks = job->failure_profile_post_ticks;
    out->fx_ticks = job->failure_profile_fx_ticks;
#endif
    return 1u;
}

static __attribute__((noinline)) void br_audio_failure_job_profile_capture_after_reset(
    uint32_t reset_confirmed, uint32_t reset_request_tick,
    uint32_t queue_origin_tick, uint32_t frames,
    uint32_t origin_estimated, uint32_t queue_source_start_tick,
    uint32_t source_start_estimated)
{
    uint32_t uncontained = br_engine_pipeline_uncontained_fault_get();
    if (!reset_confirmed || uncontained) return;
#if defined(FM1_DEVICE)
    fm1_memory_publish_sync();
#endif
    (void)br_audio_failure_job_profile_capture(
        &br_audio_failure_job_profile, reset_confirmed, uncontained,
        frames,
        br_audio_source_queue.count,
        br_audio_source_queue.completed,
        br_audio_failure_job_profile_fixed_plan_mode(),
        queue_origin_tick, queue_source_start_tick,
        br_audio_source_queue.deadline_tick, reset_request_tick,
        origin_estimated, source_start_estimated,
        br_audio_failure_job_profile_read_sample, 0);
}

uint32_t br_audio_failure_job_profile_read_page(
    uint32_t page, uint32_t words[BR_AUDIO_FAILURE_JOB_PROFILE_PAGE_WORDS])
{
    return br_audio_failure_job_profile_encode_requested_page(
        &br_audio_failure_job_profile, page, words);
}
#endif
/* Independent opt-in: track source, postprocess, and track FX can share one
 * bounded queue job. Production remains on the existing schedule by default. */
static volatile uint32_t br_audio_source_fx_fusion;
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
typedef struct {
    uint32_t average_ticks[2];
    uint32_t samples[2];
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
    uint32_t peak_ticks[2];
#endif
    uint32_t last_core;
    uint32_t has_last_core;
    uint32_t signature;
} br_audio_fusion_job_history_t;
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
#define BR_AUDIO_FUSION_HISTORY_SLOTS 5u
#else
#define BR_AUDIO_FUSION_HISTORY_SLOTS 4u
#endif
#if defined(FM1_DEVICE)
static br_audio_fusion_job_history_t br_audio_fusion_job_history[
    BR_PARTS][BR_AUDIO_FUSION_HISTORY_SLOTS][2]
    __attribute__((section(".pool"), aligned(4)));
#else
static br_audio_fusion_job_history_t br_audio_fusion_job_history[
    BR_PARTS][BR_AUDIO_FUSION_HISTORY_SLOTS][2];
#endif
#if BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE || \
    BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY || \
    BR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE || \
    BR_AUDIO_FUSION_BRAIDS_COLD_RENDER_SIGNATURE || \
    BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE || \
    BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE
/* A voice signature can include pending/override lane state, which workers
 * consume during rendering. Keep the pre-dispatch key for history commit. */
static uint32_t br_audio_fusion_signature_snapshot[
    BR_AUDIO_MAX_SOURCE_JOBS];
static uint8_t br_audio_fusion_signature_snapshot_valid[
    BR_AUDIO_MAX_SOURCE_JOBS];
#endif
#endif
#if (BR_AUDIO_FUSION_EWMA_SCHEDULER || BR_AUDIO_FUSION_FIXED_PLAN) && \
    !BR_AUDIO_FUSION_JOB_TIMING_DIAG
static uint32_t br_audio_job_timing_now(void)
{
#if defined(FM1_DEVICE)
    return fm1_ticks();
#elif defined(BR_SOURCE_QUEUE_TEST)
    extern uint32_t br_audio_source_test_ticks(void);
    return br_audio_source_test_ticks();
#else
    return 0u;
#endif
}

static uint32_t br_audio_job_timing_core(void)
{
#if defined(FM1_DEVICE)
    uint32_t core = fm1_fault_diag_read_cpu_id();
#elif defined(BR_SOURCE_QUEUE_TEST)
    extern __thread uint32_t test_core_id;
    uint32_t core = test_core_id;
#else
    uint32_t core = 0u;
#endif
    return core < 2u ? core : 0u;
}
#endif
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG
static volatile uint32_t br_audio_job_timing_collect;
static volatile uint32_t br_audio_job_timing_frames;
static volatile uint32_t br_audio_job_timing_expected_jobs;
static volatile uint32_t br_audio_job_timing_queue_start;
static volatile uint32_t br_audio_job_timing_core_ticks[2];
static volatile uint32_t br_audio_job_timing_core_jobs[2];
static volatile uint32_t br_audio_job_timing_kind_ticks[2][BR_AUDIO_JOB_KIND_COUNT];
static volatile uint32_t br_audio_job_timing_kind_jobs[2][BR_AUDIO_JOB_KIND_COUNT];
static volatile uint32_t br_audio_job_timing_part_ticks[2][BR_PARTS];
static volatile uint32_t br_audio_job_timing_part_jobs[2][BR_PARTS];
static volatile uint32_t br_audio_job_timing_sealed_seq;
static volatile br_audio_source_job_timing_t br_audio_job_timing_sealed;
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
static volatile uint32_t br_audio_stage_source_ticks[2][BR_PARTS];
static volatile uint32_t br_audio_stage_fx_ticks[2][BR_PARTS];
static volatile uint32_t br_audio_stage_source_jobs[2][BR_PARTS];
static volatile uint32_t br_audio_stage_fx_jobs[2][BR_PARTS];
static volatile br_audio_source_stage_timing_t br_audio_stage_timing_sealed;
#endif
static uint32_t br_audio_job_timing_generation;

typedef char br_audio_job_timing_words_match[
    (sizeof(br_audio_source_job_timing_t) / sizeof(uint32_t) ==
     BR_AUDIO_SOURCE_JOB_TIMING_WORDS) ? 1 : -1];
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
typedef char br_audio_stage_timing_words_match[
    (sizeof(br_audio_source_stage_timing_t) / sizeof(uint32_t) ==
     BR_AUDIO_SOURCE_STAGE_TIMING_WORDS) ? 1 : -1];
#endif

static uint32_t br_audio_job_timing_now(void)
{
#if defined(FM1_DEVICE)
    return fm1_ticks();
#elif defined(BR_SOURCE_QUEUE_TEST)
    extern uint32_t br_audio_source_test_ticks(void);
    return br_audio_source_test_ticks();
#else
    return 0u;
#endif
}

static uint32_t br_audio_job_timing_core(void)
{
#if defined(FM1_DEVICE)
    uint32_t core = fm1_fault_diag_read_cpu_id();
#elif defined(BR_SOURCE_QUEUE_TEST)
    extern __thread uint32_t test_core_id;
    uint32_t core = test_core_id;
#else
    uint32_t core = 0u;
#endif
    return core < 2u ? core : 0u;
}

static void br_audio_job_timing_job_end_at(br_audio_source_job_t *job,
                                          uint32_t core, uint32_t started,
                                          uint32_t ended)
{
    uint32_t elapsed;
    if (!br_audio_job_timing_collect || !job || core >= 2u ||
        job->kind >= BR_AUDIO_JOB_KIND_COUNT || job->part >= BR_PARTS) return;
    elapsed = ended - started;
    job->duration_ticks = elapsed;
    job->executor_core = core;
    br_audio_job_timing_core_ticks[core] += elapsed;
    br_audio_job_timing_core_jobs[core] += 1u;
    br_audio_job_timing_kind_ticks[core][job->kind] += elapsed;
    br_audio_job_timing_kind_jobs[core][job->kind] += 1u;
    br_audio_job_timing_part_ticks[core][job->part] += elapsed;
    br_audio_job_timing_part_jobs[core][job->part] += 1u;
}

static void br_audio_job_timing_job_end(br_audio_source_job_t *job,
                                       uint32_t core, uint32_t started)
{
    br_audio_job_timing_job_end_at(job, core, started,
                                   br_audio_job_timing_now());
}


#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
static void br_audio_job_timing_stage_end(uint32_t part, uint32_t core,
                                         uint32_t started, uint32_t fx_stage)
{
    uint32_t elapsed;
    if (!br_audio_job_timing_collect || core >= 2u || part >= BR_PARTS) return;
    elapsed = br_audio_job_timing_now() - started;
    if (fx_stage) {
        br_audio_stage_fx_ticks[core][part] += elapsed;
        if (br_audio_stage_fx_jobs[core][part] < 15u)
            br_audio_stage_fx_jobs[core][part] += 1u;
    } else {
        br_audio_stage_source_ticks[core][part] += elapsed;
        if (br_audio_stage_source_jobs[core][part] < 15u)
            br_audio_stage_source_jobs[core][part] += 1u;
    }
}
#endif

static void br_audio_job_timing_begin(uint32_t frames, uint32_t jobs)
{
    uint32_t core, index;
    br_audio_job_timing_collect = 0u;
    br_audio_job_timing_frames = frames;
    br_audio_job_timing_expected_jobs = jobs;
    br_audio_job_timing_core_ticks[0] = br_audio_job_timing_core_ticks[1] = 0u;
    br_audio_job_timing_core_jobs[0] = br_audio_job_timing_core_jobs[1] = 0u;
    for (core = 0u; core < 2u; ++core) {
        for (index = 0u; index < BR_AUDIO_JOB_KIND_COUNT; ++index) {
            br_audio_job_timing_kind_ticks[core][index] = 0u;
            br_audio_job_timing_kind_jobs[core][index] = 0u;
        }
        for (index = 0u; index < BR_PARTS; ++index) {
            br_audio_job_timing_part_ticks[core][index] = 0u;
            br_audio_job_timing_part_jobs[core][index] = 0u;
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
            br_audio_stage_source_ticks[core][index] = 0u;
            br_audio_stage_fx_ticks[core][index] = 0u;
            br_audio_stage_source_jobs[core][index] = 0u;
            br_audio_stage_fx_jobs[core][index] = 0u;
#endif
        }
    }
    br_audio_job_timing_queue_start = br_audio_job_timing_now();
    br_audio_job_timing_collect = 1u;
}

static void br_audio_job_timing_seal(uint32_t completed)
{
    uint32_t core, index, seq, wall_ticks;
    if (!br_audio_job_timing_collect ||
        completed != br_audio_job_timing_expected_jobs) return;
    wall_ticks = br_audio_job_timing_now() - br_audio_job_timing_queue_start;
#if BR_AUDIO_FUSION_PEAK_JOB_TIMING_DIAG
    /* Keep one coherent, fully joined queue: every field in the record below
     * belongs to the frame that set this peak. No cross-frame arithmetic. */
    if (br_audio_job_timing_sealed.valid &&
        wall_ticks <= br_audio_job_timing_sealed.queue_wall_ticks) {
        br_audio_job_timing_collect = 0u;
        return;
    }
#endif
    /* Queue workers have all joined before this function. The sequence lets a
     * foreground diagnostic copy the old sealed record if an audio interrupt
     * arrives partway through publication. */
    seq = br_audio_job_timing_sealed_seq;
    if (seq & 1u) ++seq;
    br_audio_job_timing_sealed_seq = seq + 1u;
#if defined(FM1_DEVICE)
    fm1_memory_publish_sync();
#else
    __sync_synchronize();
#endif
    br_audio_job_timing_sealed.version = 1u;
    br_audio_job_timing_sealed.generation = ++br_audio_job_timing_generation;
    br_audio_job_timing_sealed.valid = 1u;
    br_audio_job_timing_sealed.frames = br_audio_job_timing_frames;
    br_audio_job_timing_sealed.job_count = br_audio_job_timing_expected_jobs;
    br_audio_job_timing_sealed.completed = completed;
    br_audio_job_timing_sealed.queue_wall_ticks = wall_ticks;
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
    br_audio_stage_timing_sealed.version = 1u;
    br_audio_stage_timing_sealed.generation =
        br_audio_job_timing_sealed.generation;
    br_audio_stage_timing_sealed.valid = 1u;
    br_audio_stage_timing_sealed.frames = br_audio_job_timing_frames;
    br_audio_stage_timing_sealed.job_count =
        br_audio_job_timing_expected_jobs;
    br_audio_stage_timing_sealed.completed = completed;
    br_audio_stage_timing_sealed.queue_wall_ticks = wall_ticks;
    br_audio_stage_timing_sealed.core_mask = 0u;
#endif
    for (core = 0u; core < 2u; ++core) {
        br_audio_job_timing_sealed.core_ticks[core] =
            br_audio_job_timing_core_ticks[core];
        br_audio_job_timing_sealed.core_jobs[core] =
            br_audio_job_timing_core_jobs[core];
        for (index = 0u; index < BR_AUDIO_JOB_KIND_COUNT; ++index) {
            br_audio_job_timing_sealed.kind_ticks[core][index] =
                br_audio_job_timing_kind_ticks[core][index];
            br_audio_job_timing_sealed.kind_jobs[core][index] =
                br_audio_job_timing_kind_jobs[core][index];
        }
        for (index = 0u; index < BR_PARTS; ++index) {
            br_audio_job_timing_sealed.part_ticks[core][index] =
                br_audio_job_timing_part_ticks[core][index];
            br_audio_job_timing_sealed.part_jobs[core][index] =
                br_audio_job_timing_part_jobs[core][index];
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
            uint32_t source_jobs = br_audio_stage_source_jobs[core][index];
            uint32_t fx_jobs = br_audio_stage_fx_jobs[core][index];
            br_audio_stage_timing_sealed.source_ticks[core][index] =
                br_audio_stage_source_ticks[core][index];
            br_audio_stage_timing_sealed.fx_ticks[core][index] =
                br_audio_stage_fx_ticks[core][index];
            br_audio_stage_timing_sealed.packed_jobs[core][index] =
                (source_jobs & 15u) | ((fx_jobs & 15u) << 4);
            if (source_jobs || fx_jobs)
                br_audio_stage_timing_sealed.core_mask |= 1u << core;
#endif
        }
    }
#if defined(FM1_DEVICE)
    fm1_memory_publish_sync();
#else
    __sync_synchronize();
#endif
    br_audio_job_timing_sealed_seq = seq + 2u;
    br_audio_job_timing_collect = 0u;
}

static uint32_t br_audio_source_job_timing_snapshot_try(
    br_audio_source_job_timing_t *out)
{
    uint32_t before, after;
    if (!out) return 0u;
    before = br_audio_job_timing_sealed_seq;
    if (before & 1u) return 0u;
#if defined(FM1_DEVICE)
    fm1_memory_publish_sync();
#else
    __sync_synchronize();
#endif
    out->version = br_audio_job_timing_sealed.version;
        out->generation = br_audio_job_timing_sealed.generation;
        out->valid = br_audio_job_timing_sealed.valid;
        out->frames = br_audio_job_timing_sealed.frames;
        out->job_count = br_audio_job_timing_sealed.job_count;
        out->completed = br_audio_job_timing_sealed.completed;
        out->queue_wall_ticks = br_audio_job_timing_sealed.queue_wall_ticks;
        for (uint32_t core = 0u; core < 2u; ++core) {
            out->core_ticks[core] = br_audio_job_timing_sealed.core_ticks[core];
            out->core_jobs[core] = br_audio_job_timing_sealed.core_jobs[core];
            for (uint32_t index = 0u; index < BR_AUDIO_JOB_KIND_COUNT; ++index) {
                out->kind_ticks[core][index] =
                    br_audio_job_timing_sealed.kind_ticks[core][index];
                out->kind_jobs[core][index] =
                    br_audio_job_timing_sealed.kind_jobs[core][index];
            }
            for (uint32_t index = 0u; index < BR_PARTS; ++index) {
                out->part_ticks[core][index] =
                    br_audio_job_timing_sealed.part_ticks[core][index];
                out->part_jobs[core][index] =
                    br_audio_job_timing_sealed.part_jobs[core][index];
            }
        }
#if defined(FM1_DEVICE)
        fm1_memory_publish_sync();
#else
        __sync_synchronize();
#endif
    after = br_audio_job_timing_sealed_seq;
    return before == after && !(after & 1u);
}

void br_audio_source_job_timing_snapshot(br_audio_source_job_timing_t *out)
{
    while (out && !br_audio_source_job_timing_snapshot_try(out)) { }
}


void br_audio_source_stage_timing_snapshot(
    br_audio_source_stage_timing_t *out)
{
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
    uint32_t before, after;
    if (!out) return;
    for (;;) {
        before = br_audio_job_timing_sealed_seq;
        if (before & 1u) continue;
#if defined(FM1_DEVICE)
        fm1_memory_publish_sync();
#else
        __sync_synchronize();
#endif
        out->version = br_audio_stage_timing_sealed.version;
        out->generation = br_audio_stage_timing_sealed.generation;
        out->valid = br_audio_stage_timing_sealed.valid;
        out->frames = br_audio_stage_timing_sealed.frames;
        out->job_count = br_audio_stage_timing_sealed.job_count;
        out->completed = br_audio_stage_timing_sealed.completed;
        out->queue_wall_ticks = br_audio_stage_timing_sealed.queue_wall_ticks;
        out->core_mask = br_audio_stage_timing_sealed.core_mask;
        for (uint32_t core = 0u; core < 2u; ++core) {
            for (uint32_t part = 0u; part < BR_PARTS; ++part) {
                out->source_ticks[core][part] =
                    br_audio_stage_timing_sealed.source_ticks[core][part];
                out->fx_ticks[core][part] =
                    br_audio_stage_timing_sealed.fx_ticks[core][part];
                out->packed_jobs[core][part] =
                    br_audio_stage_timing_sealed.packed_jobs[core][part];
            }
        }
#if defined(FM1_DEVICE)
        fm1_memory_publish_sync();
#else
        __sync_synchronize();
#endif
        after = br_audio_job_timing_sealed_seq;
        if (before == after && !(after & 1u)) break;
    }
#else
    if (out) *out = (br_audio_source_stage_timing_t){0};
#endif
}

void br_audio_source_job_timing_reset(void)
{
    uint32_t seq;
    br_audio_job_timing_collect = 0u;
    seq = br_audio_job_timing_sealed_seq;
    if (seq & 1u) ++seq;
    br_audio_job_timing_sealed_seq = seq + 1u;
#if defined(FM1_DEVICE)
    fm1_memory_publish_sync();
#else
    __sync_synchronize();
#endif
    br_audio_job_timing_sealed = (br_audio_source_job_timing_t){0};
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
    br_audio_stage_timing_sealed = (br_audio_source_stage_timing_t){0};
#endif
    br_audio_job_timing_generation = 0u;
#if defined(FM1_DEVICE)
    fm1_memory_publish_sync();
#else
    __sync_synchronize();
#endif
    br_audio_job_timing_sealed_seq = seq + 2u;
}
#else
void br_audio_source_job_timing_snapshot(br_audio_source_job_timing_t *out)
{
    if (out) *out = (br_audio_source_job_timing_t){0};
}
void br_audio_source_stage_timing_snapshot(
    br_audio_source_stage_timing_t *out)
{
    if (out) *out = (br_audio_source_stage_timing_t){0};
}
void br_audio_source_job_timing_reset(void) { }
#endif
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
/* Only the profiling diagnostic build needs this cross-core indication. It is
 * published before worker submission and cleared only after a safe join. */
static volatile uint32_t br_audio_runtime_trace_chunk_active;
#endif
#if defined(BR_SOURCE_BATCH_TEST) && !defined(FM1_DEVICE)
/* Cost injection exists only in the focused host test translation unit. */
static uint32_t br_audio_source_batch_test_cost[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR
/* A/B jobs pack two 2-bit lane counts; four-way jobs keep one per slot. */
static uint8_t br_audio_source_batch_test_prev_profile[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
static uint8_t br_audio_source_batch_test_next_profile[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
static uint8_t br_audio_source_batch_test_signature_match[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
static uint8_t br_audio_source_batch_test_nonproportional[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
#endif
#endif
#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR && \
    defined(BR_SOURCE_QUEUE_TEST) && !defined(FM1_DEVICE)
/* The full-engine host trace injects only the prior timing baseline; append
 * counts and lookahead still come from the real Plaits kit states. */
static uint32_t br_audio_source_queue_test_cost[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
#endif
#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR && \
    defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
/* The predictor only uses the last successfully joined frame. Counts are
 * packed as 2-bit per-lane values (each lane is bounded to two appends for a
 * 32-frame job). A parameter signature prevents scaling across sound changes. */
static uint8_t br_audio_plaits_prev_append_profile[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
static uint8_t br_audio_plaits_current_append_profile[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
static uint32_t br_audio_plaits_prev_cost_signature[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
static uint32_t br_audio_plaits_current_cost_signature[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
static uint8_t br_audio_plaits_prev_cost_valid[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
static uint8_t br_audio_plaits_current_cost_valid[BR_PARTS][BR_AUDIO_KIT_JOB_LANES];
#endif
#if defined(BR_SOURCE_QUEUE_TEST)
extern uint32_t br_audio_source_test_parallel;
extern uint32_t br_audio_source_test_start(void *queue);
extern uint32_t br_audio_source_test_join(void);
extern uint32_t br_audio_source_test_join_ticks(uint32_t timeout_ticks);
extern void br_audio_source_test_begin(uint32_t job_index);
extern void br_audio_source_test_end(uint32_t job_index);
extern void br_audio_source_test_reset(uint32_t job_count);
extern uint32_t br_audio_source_test_done(uint32_t job_count);
#endif

uint32_t br_engine_source_mode_get(void) { return br_audio_source_legacy_mode; }
void br_engine_source_mode_set(uint32_t mode) { if (mode <= 1u) br_audio_source_legacy_mode = mode; }
uint32_t br_engine_source_fx_fusion_get(void) { return br_audio_source_fx_fusion; }
void br_engine_source_fx_fusion_set(uint32_t enabled)
{
    const uint32_t value = enabled ? 1u : 0u;
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    if (br_audio_source_fx_fusion != value)
        br_audio_fixed8_foreground_invalidate();
#endif
    br_audio_source_fx_fusion = value;
}
/* FX fusion entrypoints reside in the target's RAM audio section. Keep these
 * cross-section calls indirect so XIP callers do not require long-jump stubs. */
static uint32_t br_audio_call_fx_fusion_prepare(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{
    uint32_t (* volatile call)(int32_t (*)[BR_BLOCK], uint32_t) =
        br_fx_fusion_prepare;
    return call(bus, n);
}
static void br_audio_call_fx_fusion_process_track(
    uint32_t part, const int32_t *input, uint32_t n
    )
{
    void (* volatile call)(uint32_t, const int32_t *, uint32_t) =
        br_fx_fusion_process_track;
    call(part, input, n);
}
static uint32_t br_audio_call_fx_shared_reverb_runs(void)
{
    uint32_t (* volatile call)(void) = br_fx_shared_reverb_runs;
    return call();
}
static void br_audio_call_fx_shared_reverb_separate(void)
{
    void (* volatile call)(void) = br_fx_shared_reverb_separate;
    call();
}
static void br_audio_call_fx_shared_reverb(uint32_t n)
{
    void (* volatile call)(uint32_t) = br_fx_fusion_shared_reverb;
    call(n);
}
static uint32_t br_audio_call_fx_shared_reverb_cost(void)
{
    uint32_t (* volatile call)(void) = br_fx_shared_reverb_cost;
    return call();
}
static void br_audio_call_fx_fusion_cancel(void)
{
    void (* volatile call)(void) = br_fx_fusion_cancel;
    call();
}
static uint32_t br_audio_call_fx_fusion_complete(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{
    uint32_t (* volatile call)(int32_t (*)[BR_BLOCK], uint32_t) =
        br_fx_fusion_complete;
    return call(bus, n);
}
static uint32_t br_audio_call_fx_fusion_matches_mask(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t mask)
{
    uint32_t (* volatile call)(int32_t (*)[BR_BLOCK], uint32_t, uint32_t) =
        br_fx_fusion_matches_mask;
    return call(bus, n, mask);
}
static uint32_t br_audio_call_fx_fusion_cost(uint32_t part)
{
    uint32_t (* volatile call)(uint32_t) = br_fx_fusion_cost;
    return call(part);
}
static void br_audio_source_fusion_cancel(void)
{
    if (br_audio_source_fx_fusion_active) {
        br_audio_call_fx_fusion_cancel();
        br_audio_source_fx_fusion_active = 0u;
    }
}
#endif
/* Track-role validation guarantees there is exactly one kit. Partial lane
 * buses therefore need one owner rather than four impossible kit instances. */
_Static_assert(BR_DRUM_PART == 3u && BR_PARTS == 4u && BR_DRUM_LANES == 4u,
               "single-kit partial storage depends on fixed track roles");
#if defined(FM1_DEVICE)
static int32_t br_audio_kit_partial[1][BR_AUDIO_KIT_JOB_LANES][BR_BLOCK]
    __attribute__((section(".pool"), aligned(4)));
static int32_t br_audio_kit_key_partial[1][BR_AUDIO_KIT_JOB_LANES][BR_BLOCK]
    __attribute__((section(".pool"), aligned(4)));
#else
static int32_t br_audio_kit_partial[1][BR_AUDIO_KIT_JOB_LANES][BR_BLOCK];
static int32_t br_audio_kit_key_partial[1][BR_AUDIO_KIT_JOB_LANES][BR_BLOCK];
#endif
/* CPU0 audio IRQ is the only peak writer; CPU0 foreground is the only reader
 * and enable writer. CPU1 does not touch this mailbox. Aligned word accesses
 * cannot tear on the target; compiler barriers preserve publication order across
 * an IRQ. This is target-specific ISR/foreground synchronization. */
static volatile uint32_t br_kit_meter_enabled __attribute__((aligned(4)));
static volatile uint32_t br_kit_meter_values[BR_DRUM_LANES] __attribute__((aligned(4)));
static volatile uint32_t br_kit_meter_epoch __attribute__((aligned(4)));
BR_AUDIO_API void br_audio_kit_meter_enable(uint32_t enabled)
{
    RING_PUBLISH();
    br_kit_meter_enabled = !!enabled;
    RING_PUBLISH();
}
BR_AUDIO_API uint32_t br_audio_kit_meter_read(uint32_t lane)
{
    uint32_t value = lane < BR_DRUM_LANES ? br_kit_meter_values[lane] : 0u;
    RING_PUBLISH();
    return value;
}
BR_AUDIO_API uint32_t br_audio_kit_meter_epoch(void)
{
    uint32_t epoch = br_kit_meter_epoch;
    RING_PUBLISH();
    return epoch;
}
static void br_audio_kit_meter_capture(uint32_t n, uint32_t mode)
{
    static uint32_t was_enabled;
    uint32_t lane;
    RING_PUBLISH();
    if (!br_kit_meter_enabled) {
        was_enabled = 0u;
        return;
    }
    for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
        uint32_t peak = 0u;
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
        if (mode == 1u || mode == 4u) {
            uint32_t i;
            for (i = 0u; i < n; ++i) {
                int32_t value = br_audio_kit_partial[0][lane][i];
                uint32_t magnitude = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
                if (magnitude > peak) peak = magnitude;
            }
            if (peak > 32768u) peak = 32768u;
        }
#endif
        {
            uint32_t prev = was_enabled ? br_kit_meter_values[lane] : 0u;
            uint32_t fall = prev > 8u ? prev - (prev >> 3) : 0u;
            br_kit_meter_values[lane] = peak > fall ? peak : fall;
        }
    }
    was_enabled = 1u;
    RING_PUBLISH();
    ++br_kit_meter_epoch;
    RING_PUBLISH();
}
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static int32_t br_audio_fused_osc_block[BR_PARTS] __attribute__((section(".pool")));
#else
static int32_t br_audio_fused_osc_block[BR_PARTS];
#endif

typedef union {
    uint64_t align;
    uint8_t drums[BR_DRUM_STATE_BYTES];
    uint8_t drums_pair[2u * BR_DRUM_MASKED_STATE_BYTES];
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    uint8_t plaits[PLAITS_STATE_BYTES];
    uint8_t plaits_kit[PLAITS_KIT_STATE_BYTES];
    uint8_t plaits_kit_pair[2u * PLAITS_KIT_STATE_BYTES];
    uint8_t plaits_kit_quad[4u * PLAITS_KIT_STATE_BYTES];
#endif
} br_dsp_state_t;
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
typedef char br_dsp_state_cap[(sizeof(br_dsp_state_t) <= PLAITS_STATE_BYTES) ? 1 : -1];
typedef char br_dsp_state_stride_alignment[(sizeof(br_dsp_state_t) % PLAITS_STATE_ALIGN == 0u) ? 1 : -1];
#endif

typedef struct {
    uint32_t stage, note, velocity;
    uint32_t level;
    uint32_t attack, decay, release;
    uint32_t attack_step, decay_step, release_step;
    uint32_t sustain, amp, release_start, release_step_start;
    uint32_t velocity_gain_q15, velocity_gain_target_q15;
    uint32_t velocity_gain_ramp_left;
    int32_t velocity_gain_step;
} br_env_t;

#define BR_ENV_VELOCITY_RAMP_SAMPLES 32u

#if defined(__ELF__)
static br_dsp_state_t br_dsp[BR_PARTS] __attribute__((section(".pool"), aligned(8)));
#else
static br_dsp_state_t br_dsp[BR_PARTS];
#endif
static br_sound_t br_cached_sound[BR_PARTS];
static br_mod_config_t br_cached_mod[BR_PARTS];
static br_fx_config_t br_cached_fx[BR_PARTS];
static br_sound_t br_effective_sound[BR_PARTS];
static br_fx_config_t br_effective_fx[BR_PARTS];
static int32_t br_pitch_mod_q7[BR_PARTS];
static uint32_t br_cached_lane[BR_PARTS][BR_DRUM_LANES];
static uint32_t br_cached_lane_gate[BR_PARTS][BR_DRUM_LANES];
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static br_kit_source_controls_t br_audio_kit_source_controls[BR_PARTS]
    __attribute__((section(".pool")));
#else
static br_kit_source_controls_t br_audio_kit_source_controls[BR_PARTS];
#endif
static uint32_t br_cached_revision[BR_PARTS];
static uint8_t br_cached_mod_inactive[BR_PARTS];
static uint8_t br_effective_valid[BR_PARTS];
static uint8_t br_mode[BR_PARTS];           /* 1 BRAIDS/sample kit, 3 Plaits mono, 4 Plaits kit; 0/2 retired. */
static uint8_t br_active_preset[BR_PARTS];
static br_env_t br_env[BR_PARTS];
static br_filter_t br_filter[BR_PARTS];
static br_mod_state_t br_mod_state[BR_PARTS];
static uint8_t br_active_raw_note[BR_PARTS], br_active_dsp_note[BR_PARTS];
static volatile uint32_t br_events[BR_EVENT_CAP];
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
static uint32_t br_event_timestamp[BR_EVENT_CAP], br_event_generation[BR_EVENT_CAP];
static uint8_t br_clock_source_prev;
static uint32_t br_clock_generation_seen;
#endif
static volatile uint32_t br_event_read, br_event_write;
static volatile uint32_t br_panic_parts;
static uint32_t br_dropped_events;
#ifdef BR_SPLIT_AUDIO
volatile uint32_t br_ready;
#else
static volatile uint32_t br_ready;
#endif
static uint32_t br_transport_prev;
/* MIDI Stop pauses musical state; local PLAY retains its restart semantics. */
static uint32_t br_midi_paused;
static uint8_t br_midi_paused_velocity[BR_SYNTH_PARTS];
static volatile uint32_t br_midi_transport_overflow;
static uint32_t br_metro_frames;
static uint32_t br_seq_frames[BR_PARTS], br_seq_gate_frames[BR_PARTS];
static uint8_t br_seq_note[BR_PARTS], br_seq_tie[BR_PARTS], br_seq_next[BR_PARTS];
static uint8_t br_seq_deferred_off[BR_PARTS];
#define BR_SEQ_EVENT_SLOTS (BR_DRUM_LANES * 2u)
typedef struct {
    uint32_t remaining, cycle;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    uint32_t clock_base_duration;
#endif
    uint8_t step, lane, active;
} br_micro_event_t;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
/* Experimental audio-owned injection point only. No production caller exists. */
static struct {
    uint32_t enabled, invalid, block_offset, lead_frames, suspect_pause, wait_transport, seek_unsupported;
    uint64_t phase_q16, phase_end_q16, period_q16, render_frame, wait_boundary_q16, wait_origin_q16, arp_start_phase_q16, suspend_phase_q16;
    fm1_mct_anchor_t anchor;
    fm1_midi_clock_t follower;
    fm1_mcs_track_t track[BR_PARTS];
    fm1_mca_t arp[BR_SYNTH_PARTS];
    uint8_t arp_ready[BR_SYNTH_PARTS], arp_start_armed[BR_SYNTH_PARTS];
} br_clock_experiment;
#endif
static br_micro_event_t br_micro_event[BR_PARTS][BR_SEQ_EVENT_SLOTS];
static uint8_t br_seq_started[BR_PARTS];
typedef struct { volatile uint32_t revision, step, elapsed, duration, unit; } br_record_position_t;
static br_record_position_t br_record_position[BR_PARTS];
static uint32_t br_seq_current_duration[BR_PARTS], br_seq_micro_unit[BR_PARTS];

volatile uint16_t br_seq_synth_length[BR_SYNTH_PARTS][BR_STEPS];
volatile uint8_t br_seq_synth_condition[BR_SYNTH_PARTS][BR_STEPS];
volatile uint8_t br_seq_synth_note[BR_SYNTH_PARTS][BR_STEPS];
volatile uint8_t br_seq_synth_velocity[BR_SYNTH_PARTS][BR_STEPS];
volatile uint16_t br_seq_kit_length[BR_STEPS][BR_DRUM_LANES];
volatile uint8_t br_seq_kit_condition[BR_STEPS][BR_DRUM_LANES];
volatile uint8_t br_seq_kit_velocity[BR_STEPS][BR_DRUM_LANES];
volatile uint8_t br_seq_kit_retrig[BR_STEPS][BR_DRUM_LANES];
#ifdef BR_SPLIT_AUDIO
volatile uint32_t br_kit_mute_mask, br_kit_solo_mask;
#endif
volatile uint8_t br_seq_kit_note[BR_STEPS][BR_DRUM_LANES];
volatile uint8_t br_seq_kit_pitch[BR_STEPS][BR_DRUM_LANES];
volatile int8_t br_seq_synth_micro[BR_SYNTH_PARTS][BR_STEPS];
volatile int8_t br_seq_kit_micro[BR_STEPS][BR_DRUM_LANES];
volatile uint8_t br_seq_record_quantize[BR_PARTS];
volatile uint32_t br_fill_active;
static volatile uint32_t br_seq_clock[BR_PARTS];
static uint32_t br_seq_cycle[BR_SEQ_TRACKS], br_seq_rng[BR_SEQ_TRACKS];
static uint32_t br_seq_lane_gate_frames[BR_PARTS][BR_DRUM_LANES];
typedef struct {
    uint32_t remaining, interval, gate;
    uint8_t count, step, setting, voice, velocity, pitch, pending;
} br_retrig_t;
static br_retrig_t br_retrig[BR_DRUM_LANES];

static uint8_t br_seq_prev_condition_valid[BR_SEQ_TRACKS];
static uint8_t br_seq_prev_condition_result[BR_SEQ_TRACKS];

#ifdef BR_SPLIT_AUDIO
#if BR_AUDIO_FUSION_ONLY_BUILD
static volatile uint32_t br_pipeline_source_deadline_tick;
static volatile uint32_t br_pipeline_source_deadline_valid;
static volatile uint32_t br_pipeline_uncontained_fault;
/* Kept as an always-null test visibility hook; the device frame buffers and
 * all mode-specific ownership state are absent in this profile. */
static br_pipeline_frame_t *br_pipeline_previous;
#define br_pipeline_mode 0u
#define br_pipeline_enabled 0u

static uint32_t br_pipeline_note_worker_reset(uint32_t reset_confirmed)
{
    return fm1_cpu1_worker_note_reset_confirmation(
        &br_pipeline_uncontained_fault, reset_confirmed);
}

#if defined(BR_DIAGNOSTIC_TEST) && defined(BR_SPLIT_AUDIO)
void br_engine_pipeline_test_uncontained_fault_latch(void)
{
    (void)br_pipeline_note_worker_reset(0u);
}
#endif

uint32_t br_engine_pipeline_uncontained_fault_get(void)
{
    uint32_t uncontained = br_pipeline_uncontained_fault;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    if (fm1_cpu1_worker_uncontained_fault_get()) uncontained = 1u;
#endif
    return uncontained;
}

uint32_t br_engine_pipeline_mode_get(void) { return 0u; }
uint32_t br_engine_pipeline_generation_get(void) { return 0u; }
void br_engine_pipeline_source_deadline_set(uint32_t absolute_tick)
{
    br_pipeline_source_deadline_tick = absolute_tick;
    br_pipeline_source_deadline_valid = 1u;
}
void br_engine_pipeline_source_deadline_clear(void)
{
    br_pipeline_source_deadline_valid = 0u;
}
uint32_t br_engine_pipeline_mode_set(uint32_t mode) { return mode == 0u; }
void br_engine_pipeline_begin(br_pipeline_frame_t *previous, int32_t *out, uint32_t n)
{ (void)previous; (void)out; (void)n; }
uint32_t br_engine_pipeline_finish(void) { return 1u; }
uint32_t br_engine_pipeline_capture(br_pipeline_frame_t *current,
                                    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{ (void)current; (void)bus; (void)n; return 0u; }
void br_engine_pipeline_reset(void) { }
static inline void br_engine_pipeline_process_pending(void) { }
static inline void br_engine_pipeline_process_hybrid_pending(void) { }
#else
static br_pipeline_frame_t *br_pipeline_previous;
static int32_t *br_pipeline_out;
static uint32_t br_pipeline_frames, br_pipeline_enabled, br_pipeline_processed;
static volatile uint32_t br_pipeline_mode;
static volatile uint32_t br_pipeline_generation;
static volatile uint32_t br_pipeline_source_deadline_tick;
static volatile uint32_t br_pipeline_source_deadline_valid;
static uint32_t br_pipeline_reverse_started;
static uint32_t br_pipeline_hybrid_started;
static volatile uint32_t br_pipeline_uncontained_fault;
static uint32_t br_audio_source_remaining_ticks(void);

static uint32_t br_pipeline_note_worker_reset(uint32_t reset_confirmed)
{
    return fm1_cpu1_worker_note_reset_confirmation(
        &br_pipeline_uncontained_fault, reset_confirmed);
}

#if defined(BR_DIAGNOSTIC_TEST) && defined(BR_SPLIT_AUDIO)
void br_engine_pipeline_test_uncontained_fault_latch(void)
{
    (void)br_pipeline_note_worker_reset(0u);
}
#endif

uint32_t br_engine_pipeline_uncontained_fault_get(void)
{
    uint32_t uncontained = br_pipeline_uncontained_fault;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    if (fm1_cpu1_worker_uncontained_fault_get()) uncontained = 1u;
#endif
    return uncontained;
}

uint32_t br_engine_pipeline_mode_get(void) { return br_pipeline_mode; }
uint32_t br_engine_pipeline_generation_get(void) { return br_pipeline_generation; }
void br_engine_pipeline_source_deadline_set(uint32_t absolute_tick)
{
    br_pipeline_source_deadline_tick = absolute_tick;
    br_pipeline_source_deadline_valid = 1u;
}
void br_engine_pipeline_source_deadline_clear(void)
{
    br_pipeline_source_deadline_valid = 0u;
}
uint32_t br_engine_pipeline_mode_set(uint32_t mode)
{
    if (mode > 3u) return 0u;
    br_pipeline_mode = mode;
    br_engine_pipeline_reset();
    return 1u;
}

void br_engine_pipeline_begin(br_pipeline_frame_t *previous, int32_t *out, uint32_t n)
{
    br_pipeline_previous = previous;
    br_pipeline_out = out;
    br_pipeline_frames = n;
    /* The warm-up block still belongs to the pipeline: it renders source
     * jobs on CPU1 while CPU0 emits silence for the not-yet-available FX
     * frame. A null previous frame means silence, not legacy scheduling. */
    br_pipeline_enabled = out && n == BR_BLOCK;
    br_pipeline_processed = 0u;
    br_pipeline_reverse_started = 0u;
    br_pipeline_hybrid_started = 0u;
}

static void br_engine_pipeline_process_pending(void)
{
    if (!br_pipeline_enabled || br_pipeline_processed) return;
    br_pipeline_reverse_started = 0u;
    if (br_pipeline_previous && br_pipeline_previous->valid &&
        br_pipeline_previous->frames == br_pipeline_frames) {
        if (br_pipeline_mode == 2u) {
            br_pipeline_reverse_started = br_fx_process_pipeline_reverse_start(
                br_pipeline_previous->bus, br_pipeline_previous->key,
                &br_pipeline_previous->controls, br_pipeline_out, br_pipeline_frames);
        } else {
#if defined(FM1_DEVICE)
            typedef void (*br_fx_pipeline_fn)(int32_t [BR_PARTS][BR_BLOCK],
                int32_t [BR_PARTS][BR_BLOCK], const br_pipeline_controls_t *,
                int32_t *, uint32_t);
            void *volatile target = (void *)&br_fx_process_pipeline;
            ((br_fx_pipeline_fn)target)(br_pipeline_previous->bus,
                br_pipeline_previous->key, &br_pipeline_previous->controls,
                br_pipeline_out, br_pipeline_frames);
#else
            br_fx_process_pipeline(br_pipeline_previous->bus, br_pipeline_previous->key,
                &br_pipeline_previous->controls, br_pipeline_out, br_pipeline_frames);
#endif
        }
    } else {
        uint32_t i;
        for (i = 0; i < br_pipeline_frames * 2u; ++i) br_pipeline_out[i] = 0;
    }
    br_pipeline_processed = 1u;
}

/* Hybrid mode may only publish its composite CPU1 task after the current
 * source queue has been fully initialized and made visible. Unlike mode 2,
 * this is called from br_engine_render() after that queue publication. */
static void br_engine_pipeline_process_hybrid_pending(void)
{
    uint32_t fx_valid;
    if (!br_pipeline_enabled || br_pipeline_processed ||
        br_pipeline_mode != 3u) return;
    fx_valid = br_pipeline_previous && br_pipeline_previous->valid &&
        br_pipeline_previous->frames == br_pipeline_frames;
    if (!fx_valid) {
        uint32_t i;
        for (i = 0; i < br_pipeline_frames * 2u; ++i)
            br_pipeline_out[i] = 0;
    }
    br_pipeline_hybrid_started = br_fx_process_pipeline_hybrid_start(
        fx_valid ? br_pipeline_previous->bus : 0,
        fx_valid ? br_pipeline_previous->key : 0,
        fx_valid ? &br_pipeline_previous->controls : 0,
        br_pipeline_out, br_pipeline_frames, fx_valid, &br_audio_source_queue);
    /* A 0 return means the fixed task was not submitted; FX has already
     * completed serially (or warm-up is silent), so never process it again. */
    br_pipeline_processed = 1u;
}

uint32_t br_engine_pipeline_finish(void)
{
    if (br_pipeline_enabled && br_pipeline_processed) {
        uint32_t worker_started =
            (br_pipeline_mode == 2u && br_pipeline_reverse_started) ||
            (br_pipeline_mode == 3u && br_pipeline_hybrid_started);
        if (worker_started) {
            uint32_t finish_status;
            uint32_t remaining = br_audio_source_remaining_ticks();
            finish_status = br_fx_process_pipeline_reverse_finish(br_pipeline_out,
                    br_pipeline_frames, remaining);
            if (finish_status != 1u) {
#if defined(FM1_DEVICE) || defined(BR_SOURCE_QUEUE_TEST)
                br_audio_source_worker_fault = 1u;
#endif
                if (finish_status == 2u) {
                    /* CPU1 may still own this buffer; keep it untouched. */
                    (void)br_pipeline_note_worker_reset(0u);
                    return 2u;
                }
            }
            if (br_pipeline_mode == 2u) br_pipeline_reverse_started = 0u;
            if (br_pipeline_mode == 3u) br_pipeline_hybrid_started = 0u;
            return finish_status;
        }
        return 1u;
    }
    br_engine_pipeline_process_pending();
    return 1u;
}

uint32_t br_engine_pipeline_capture(br_pipeline_frame_t *current,
                                    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{
    uint32_t p;
    if (!current || !bus || n != BR_BLOCK) {
        if (current) current->valid = 0u;
        return 0u;
    }
    for (p = 0; p < BR_PARTS; ++p) {
        uint32_t i;
        for (i = 0; i < BR_BLOCK; ++i) {
            current->bus[p][i] = bus[p][i];
            current->key[p][i] = br_comp_key[p][i];
        }
    }
    br_fx_capture_pipeline_controls(&current->controls);
    current->frames = n;
    current->valid = 1u;
    return 1u;
}

void br_engine_pipeline_reset(void)
{
    ++br_pipeline_generation;
    br_pipeline_previous = 0;
    br_pipeline_out = 0;
    br_pipeline_frames = br_pipeline_enabled = br_pipeline_processed = 0u;
    br_pipeline_reverse_started = 0u;
    br_pipeline_hybrid_started = 0u;
}
#endif /* BR_AUDIO_FUSION_ONLY_BUILD */
#else
uint32_t br_engine_pipeline_uncontained_fault_get(void) { return 0u; }
void br_engine_pipeline_source_deadline_set(uint32_t absolute_tick)
{
    (void)absolute_tick;
}
void br_engine_pipeline_source_deadline_clear(void) { }
#endif

static uint32_t br_bound(uint32_t x, uint32_t hi) { return x > hi ? hi : x; }

static void *br_drum_pair_state(uint32_t p, uint32_t lanes)
{
    const uint32_t b_only = (lanes & BR_KIT_LANES_B) &&
                            !(lanes & BR_KIT_LANES_A);
    return br_dsp[p].drums_pair + (b_only ? BR_DRUM_MASKED_STATE_BYTES : 0u);
}

static uint32_t br_audio_kit_job_slot(uint32_t lane_mask)
{
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
    uint32_t lane;
    for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
        if (lane_mask == (1u << lane)) return lane;
#endif
    return lane_mask == BR_KIT_LANES_B ? 1u : 0u;
}

#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
static void *br_plaits_kit_pair_state(uint32_t p, uint32_t lanes)
{
    return br_dsp[p].plaits_kit_pair + (lanes == BR_KIT_LANES_B ? PLAITS_KIT_STATE_BYTES : 0u);
}

static void *br_plaits_kit_lane_state(uint32_t p, uint32_t lane)
{
    return br_dsp[p].plaits_kit_quad + lane * PLAITS_KIT_STATE_BYTES;
}

static void *br_plaits_kit_job_state(uint32_t p, uint32_t lane_mask)
{
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
    uint32_t lane;
    for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
        if (lane_mask == (1u << lane)) return br_plaits_kit_lane_state(p, lane);
#endif
    return br_plaits_kit_pair_state(p, lane_mask);
}

static int br_plaits_kit_pair_quiet(uint32_t p)
{
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
    uint32_t lane;
    for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
        if (!plaits_kit_quiet_mask(br_plaits_kit_lane_state(p, lane),
                                   1u << lane)) return 0;
    return 1;
#else
    return plaits_kit_quiet_mask(br_plaits_kit_pair_state(p, BR_KIT_LANES_A), BR_KIT_LANES_A) &&
           plaits_kit_quiet_mask(br_plaits_kit_pair_state(p, BR_KIT_LANES_B), BR_KIT_LANES_B);
#endif
}
#endif

static int br_drum_pair_quiet(uint32_t p)
{
    return br_drum_quiet_mask(br_drum_pair_state(p, BR_KIT_LANES_A), BR_KIT_LANES_A) &&
           br_drum_quiet_mask(br_drum_pair_state(p, BR_KIT_LANES_B), BR_KIT_LANES_B);
}

/* Tempo-derived frame counts are shared by sequencer steps, note gates, and
 * all four kit lanes. Cache the exact integer quarter-note result so a kit
 * block does not repeat the same runtime divide for each lane. */
static uint32_t br_tempo_cached_bpm = 0xFFFFFFFFu;
static uint32_t br_tempo_cached_quarter;

static uint32_t br_tempo_quarter_frames(void)
{
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (br_clock_experiment.enabled && br_clock_experiment.period_q16) {
        uint64_t quarter = br_clock_experiment.period_q16 * 24u >> 16;
        if (quarter && quarter <= UINT32_MAX) return (uint32_t)quarter;
    }
#endif
    uint32_t bpm = br_bound(br_bpm, BR_BPM_MAX);
    if (bpm < BR_BPM_MIN) bpm = BR_BPM_MIN;
    if (bpm != br_tempo_cached_bpm) {
        br_tempo_cached_bpm = bpm;
        br_tempo_cached_quarter = (FS * 60u) / bpm;
    }
    return br_tempo_cached_quarter;
}

BR_AUDIO_API uint32_t br_time_samples(uint32_t value, uint32_t scale)
{
    /* The time curve is authored at 44.1 kHz. Scale the entire duration,
     * including its minimum click-safe ramp, to the selected output rate. */
    uint64_t samples;
    value = br_bound(value, 127u);
    samples = 32u + (uint64_t)value * value * scale;
    return (uint32_t)((samples * FS + 22050u) / 44100u);
}

static uint32_t br_q15(uint32_t value)
{
    return br_scale_7bit_q15_exact(br_bound(value, 127u));
}

#if BR_AUDIO_ENV_CURVE_CONTROL_KEY
/* `br_env_t.level` is not read after note start. Reuse its word to cache the
 * four live seven-bit controls that determine the attack/decay/release curve.
 * This avoids rebuilding the same scaled durations on every source block;
 * a changed effective control still takes effect on the very next refresh. */
static uint32_t br_env_curve_control_key(const br_sound_t *s)
{
    uint32_t attack = br_bound(s->attack, 127u);
    uint32_t decay = br_bound(s->decay, 127u);
    uint32_t release = br_bound(s->release, 127u);
    uint32_t sustain = br_bound(s->sustain, 127u);
    return attack | (decay << 7) | (release << 14) | (sustain << 21);
}
#endif

/* Snapshot lane gains and gates once on CPU0 after controls/events are applied,
 * then share immutable controls across each kit's split render jobs. */
/* Freeze the detector source with its compressor controls before any source
 * worker starts. The same snapshot travels with pipelined bus/key audio. */
static void br_snapshot_source_compressor(void)
{
    uint32_t before = br_master.revision, after;
    br_compressor_config_t c;
    if (before & 1u) return;
    c.source = br_master.comp.source; c.threshold = br_master.comp.threshold;
    c.ratio = br_master.comp.ratio; c.makeup = br_master.comp.makeup;
    c.attack = br_master.comp.attack; c.release = br_master.comp.release;
    c.hold = br_master.comp.hold; c.mix = br_master.comp.mix;
    RING_PUBLISH(); after = br_master.revision;
    if (before != after || (after & 1u)) return;
    if (c.source >= BR_COMP_SOURCE_COUNT) c.source = BR_COMP_OFF;
    br_source_compressor = c; br_source_compressor_revision = after;
    RING_PUBLISH();
}

static void br_prepare_kit_source_controls(void)
{
    uint32_t p, has_kit = 0u;
    for (p = 0; p < BR_PARTS; ++p)
        if (br_mode[p] == 1u || br_mode[p] == 4u) has_kit = 1u;
    if (!has_kit) return;
    {
        uint32_t quarter_frames = br_tempo_quarter_frames();
        for (p = 0; p < BR_PARTS; ++p) {
            br_sound_t *sound = &br_effective_sound[p];
            sound->drum_key_lane = br_comp_source_drum_lane(br_source_compressor.source);
            if (br_mode[p] == 1u || br_mode[p] == 4u)
                br_kit_source_controls_prepare(br_cached_lane[p],
                    br_cached_lane_gate[p], sound->level, sound->mute,
                    quarter_frames, &br_audio_kit_source_controls[p]);
            if (br_mode[p] == 1u || br_mode[p] == 4u) {
                uint32_t q6 = br_audio_preset_gain(sound->preset);
                uint32_t lane;
                if (sound->preset == BR_KIT_808 || sound->preset == BR_KIT_909) {
                    /* Internal sample kits store base Q7 level times the
                     * preset Q6 calibration as Q13 for the existing level
                     * multiply in the sample renderer. The maximum is
                     * 127 * 84 = 10668, well inside uint32_t. */
                    /* Per-lane trim (Q4, 16 = unity) so the kits balance: measured on the unit, the hats and
                     * cymbals of the sample kits sat 13 to 26 dB under the kick. */
                    static const uint8_t lane_trim_q4[BR_DRUM_LANES] = {16u, 24u, 56u, 40u};
                    for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
                        br_audio_kit_source_controls[p].level[lane] =
                            (br_audio_kit_source_controls[p].level[lane] * q6 * lane_trim_q4[lane]) >> 4;
                } else if (q6 < 64u) {
                    for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
                        br_audio_kit_source_controls[p].level[lane] =
                            (br_audio_kit_source_controls[p].level[lane] * q6 + 32u) >> 6;
                }
            }
            if (br_mode[p] == 1u || br_mode[p] == 4u) {
                uint32_t lane, kit_limit = br_gate_samples(sound->gate);
                for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
                    uint32_t limit = br_seq_lane_gate_frames[p][lane];
                    /* Zero is an open gate; the shorter nonzero limit wins. */
                    if (kit_limit && (!limit || kit_limit < limit)) limit = kit_limit;
                    br_audio_kit_source_controls[p].gate_frames[lane] = limit;
                }
            }
        }
    }
}

/* The compressor sidechain can consume one drum lane as its detector source.
 * In the opt-in non-pipeline path, only that lane needs a key scratch row;
 * all other key writes and reductions are unobservable. */
static uint32_t br_audio_kit_key_elision_active(void)
{
#if BR_AUDIO_KIT_KEY_WRITE_ELISION
    return br_pipeline_enabled ? 0u : 1u;
#else
    return 0u;
#endif
}

static uint32_t br_audio_kit_key_lane_mask(uint32_t slot)
{
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
    return slot < BR_DRUM_LANES ? (1u << slot) : 0u;
#else
    return slot == 0u ? BR_KIT_LANES_A :
           slot == 1u ? BR_KIT_LANES_B : 0u;
#endif
}

static int32_t *br_audio_kit_key_output(uint32_t p, uint32_t slot,
                                        uint32_t lane_mask)
{
    if (p >= BR_PARTS || slot >= BR_AUDIO_KIT_JOB_LANES) return 0;
#if BR_AUDIO_KIT_KEY_WRITE_ELISION
    if (!br_pipeline_enabled) {
        const uint32_t source = br_source_compressor.source;
        if (p != BR_DRUM_PART || (br_mode[p] != 1u && br_mode[p] != 4u) ||
            source < BR_COMP_DRUM_1 || source > BR_COMP_DRUM_4 ||
            !(lane_mask & (1u << (source - BR_COMP_DRUM_1))))
            return 0;
    }
#endif
    return br_audio_kit_key_partial[0][slot];
}

static void br_copy_sound(br_sound_t *d, volatile br_sound_t *s)
{
    d->preset = s->preset; d->timbre = s->timbre; d->color = s->color;
    d->tune = s->tune; d->level = s->level; d->attack = s->attack;
    d->decay = s->decay; d->sustain = s->sustain; d->release = s->release;
    d->drive = s->drive; d->delay = s->delay; d->reverb = s->reverb;
    d->space = s->space; d->mute = s->mute; d->filter = s->filter;
    d->filter_resonance = s->filter_resonance;
    d->filter_keytrack = s->filter_keytrack;
    d->timbre_mod = s->timbre_mod; d->fm_mod = s->fm_mod;
    d->morph_mod = s->morph_mod;
    d->drum_snap = s->drum_snap;
    d->plaits_harmonics = s->plaits_harmonics; d->plaits_aux = s->plaits_aux;
    d->pan = s->pan;
    d->gate = s->gate;
    d->drum_voice_mask = s->drum_voice_mask & BR_DRUM_CHOICE_MASK;
    d->drum_key_lane = 0u;
}

static void br_copy_mod(br_mod_config_t *d, volatile br_mod_config_t *s)
{
    uint32_t i;
    d->attack = s->attack; d->decay = s->decay; d->sustain = s->sustain;
    d->release = s->release; d->amount = s->amount; d->target = s->target;
    d->loop = s->loop; d->retrigger = s->retrigger; d->scale = s->scale;
    d->root = s->root; d->quantize = s->quantize; d->transpose = s->transpose;
    for (i = 0; i < BR_LFOS; ++i) {
        d->lfo[i].wave = s->lfo[i].wave; d->lfo[i].rate = s->lfo[i].rate;
        d->lfo[i].depth = s->lfo[i].depth; d->lfo[i].target = s->lfo[i].target;
    }
    for (i = 0; i < BR_ROUTES; ++i) {
        d->route[i].source = s->route[i].source; d->route[i].target = s->route[i].target;
        d->route[i].amount = s->route[i].amount; d->route[i].enabled = s->route[i].enabled;
    }
}

static void br_copy_fx(br_fx_config_t *d, volatile br_fx_config_t *s)
{
    d->drive_mode = s->drive_mode; d->drive_gain = s->drive_gain; d->drive_tone = s->drive_tone;
    d->delay_engine = s->delay_engine;
    d->delay_time = s->delay_time; d->delay_feedback = s->delay_feedback;
    d->reverb_engine = s->reverb_engine; d->reverb_tone = s->reverb_tone;
    d->mod_engine = s->mod_engine; d->mod_rate = s->mod_rate;
    d->mod_depth = s->mod_depth; d->mod_mix = s->mod_mix;
}

static uint32_t br_mod_has_active_target(const br_mod_config_t *mod,
                                         const br_sound_t *sound)
{
    uint32_t i;
    if (sound->timbre_mod != 64u || sound->fm_mod != 64u ||
        (br_is_plaits(sound->preset) && sound->morph_mod != 64u)) return 1u;
    for (i = 0; i < BR_LFOS; ++i)
        if (mod->lfo[i].depth && br_mod_target_supported(sound->preset, mod->lfo[i].target)) return 1u;
    if (mod->amount && br_mod_target_supported(sound->preset, mod->target))
        return 1u;
    for (i = 0; i < BR_ROUTES; ++i) {
        const br_route_config_t *r = &mod->route[i];
        if (r->enabled && r->amount && br_mod_source_active(r->source) &&
            br_mod_target_supported(sound->preset, r->target)) return 1u;
    }
    return 0u;
}

/* Send levels (DELAY, REVERB, MOD MIX) change in small steps, a block at a time, instead of jumping:
 * a quick turn of a knob that moves a send by 30 would otherwise step the wet mix by a quarter in one
 * go, which clicks. The knob's own value is walked toward its target; anything a modulator adds on top
 * is applied as it comes. DRIVE is smoothed per sample inside its own stage. */
static uint8_t br_send_now[BR_PARTS][3];
static uint8_t br_send_ready[BR_PARTS];
static uint32_t br_send_walk(uint32_t *value, uint32_t base, uint8_t *now, uint32_t step)
{
    uint32_t cur = *now;
    int32_t delta = (int32_t)*value - (int32_t)base, out;
    if (base > 127u) base = 127u;
    if (cur < base) cur = base - cur > step ? cur + step : base;
    else if (cur > base) cur = cur - base > step ? cur - step : base;
    *now = (uint8_t)cur;
    out = (int32_t)cur + delta;
    *value = (uint32_t)(out < 0 ? 0 : out > 127 ? 127 : out);
    return cur != base;
}
static void br_smooth_sends(void)
{
    uint32_t p;
    for (p = 0; p < BR_PARTS; ++p) {
        br_sound_t *s = &br_effective_sound[p];
        br_fx_config_t *f = &br_effective_fx[p];
        uint32_t moving;
        if (!br_send_ready[p]) {
            br_send_now[p][0] = (uint8_t)(br_cached_sound[p].delay > 127u ? 127u : br_cached_sound[p].delay);
            br_send_now[p][1] = (uint8_t)(br_cached_sound[p].reverb > 127u ? 127u : br_cached_sound[p].reverb);
            br_send_now[p][2] = (uint8_t)(br_cached_fx[p].mod_mix > 127u ? 127u : br_cached_fx[p].mod_mix);
            br_send_ready[p] = 1u;
            continue;
        }
        moving = br_send_walk(&s->delay, br_cached_sound[p].delay, &br_send_now[p][0], 1u);
        moving |= br_send_walk(&s->reverb, br_cached_sound[p].reverb, &br_send_now[p][1], 2u);
        moving |= br_send_walk(&f->mod_mix, br_cached_fx[p].mod_mix, &br_send_now[p][2], 1u);
        if (moving) br_effective_valid[p] = 0u;       /* rebuild next block, then keep walking */
    }
}

static void br_build_effective(uint32_t p)
{
    if (br_cached_mod_inactive[p] && br_effective_valid[p]) return;
    int32_t amp = (int32_t)br_env[p].amp;
    br_mod_effective(&br_mod_state[p], &br_cached_mod[p], &br_cached_sound[p],
                     &br_cached_fx[p], amp, &br_effective_sound[p], &br_effective_fx[p],
                     &br_pitch_mod_q7[p]);
    if (br_cached_mod_inactive[p]) br_effective_valid[p] = 1u;
}

static void br_env_start(uint32_t p, uint32_t note, uint32_t velocity,
                         const br_sound_t *s, uint32_t keep_amp)
{
    br_env_t *e = &br_env[p];
    uint32_t new_velocity = br_bound(velocity, 127u);
    uint32_t amp = keep_amp && e->stage != BR_ENV_OFF ? e->amp : 0u;
    if (keep_amp) {
        uint32_t velocity_gain_q15 =
            (new_velocity * 32768u + 63u) / 127u;
        if (e->stage == BR_ENV_OFF) {
            e->velocity_gain_q15 = velocity_gain_q15;
            e->velocity_gain_target_q15 = velocity_gain_q15;
            e->velocity_gain_ramp_left = 0u;
            e->velocity_gain_step = 0;
        } else if (new_velocity != e->velocity) {
            e->velocity_gain_target_q15 = velocity_gain_q15;
            e->velocity_gain_ramp_left = BR_ENV_VELOCITY_RAMP_SAMPLES;
            e->velocity_gain_step =
                ((int32_t)velocity_gain_q15 - (int32_t)e->velocity_gain_q15) /
                (int32_t)BR_ENV_VELOCITY_RAMP_SAMPLES;
        }
    }
    e->stage = BR_ENV_ATTACK; e->note = note;
    e->velocity = new_velocity;
#if BR_AUDIO_ENV_CURVE_CONTROL_KEY
    e->level = br_env_curve_control_key(s);
#else
    e->level = br_bound(s->level, 127u);
#endif
    e->attack = br_time_samples(s->attack, 3u);
    e->decay = br_time_samples(s->decay, 8u);
    e->release = br_time_samples(s->release, 8u);
    e->sustain = br_q15(s->sustain);
    e->amp = amp; e->release_start = e->release_step_start = 0;
    e->attack_step = (32767u - e->amp + e->attack - 1u) / e->attack;
    e->decay_step = (32767u - e->sustain + e->decay - 1u) / e->decay;
    e->release_step = (e->release_start + e->release - 1u) / e->release;
}

static uint32_t br_map_pitch_q7(uint32_t p, uint32_t raw_note)
{
    /* Legacy scale and transpose fields remain serialized for project format
     * compatibility, but no longer affect playback. */
    int32_t base_tune = br_cached_sound[p].tune;
    if (base_tune < -48) base_tune = -48;
    if (base_tune > 48) base_tune = 48;
    int32_t pitch = ((int32_t)raw_note + base_tune) * 128 + br_pitch_mod_q7[p];
    if (pitch < 0) pitch = 0;
    if (pitch > 127 * 128) pitch = 127 * 128;
    return (uint32_t)pitch;
}

static uint32_t br_map_note(uint32_t p, uint32_t raw_note)
{
    return (br_map_pitch_q7(p, raw_note) + 64u) / 128u;
}

static void br_snapshot(uint32_t p)
{
    uint32_t tries, before, after, i;
    br_part_t *part = &br_part[p];
    br_sound_t sound;
    br_mod_config_t mod;
    br_fx_config_t fx;
    uint32_t lane[BR_DRUM_LANES], lane_gate[BR_DRUM_LANES];
    for (tries = 0; tries < 2u; ++tries) {
        before = part->revision;
        if (before & 1u) continue;
        if (before == br_cached_revision[p]) return;
        br_copy_sound(&sound, &part->sound);
        if (br_solo_mask && !(br_solo_mask & (1u << p))) sound.mute = 1u;
        br_copy_mod(&mod, &part->mod);
        br_copy_fx(&fx, &part->fx);
        {
            /* Snapshot these with the drum part revision so source workers
             * see one coherent set of effective gains for the whole block. */
            uint32_t kit_mute = p == BR_DRUM_PART ? br_kit_mute_mask : 0u;
            uint32_t kit_solo = p == BR_DRUM_PART ? br_kit_solo_mask & BR_DRUM_ALL_MASK : 0u;
            for (i = 0; i < BR_DRUM_LANES; ++i) {
                uint32_t excluded = p == BR_DRUM_PART &&
                    ((kit_mute & (1u << i)) ||
                     (kit_solo && !(kit_solo & (1u << i))));
                lane[i] = excluded ? 0u : part->lane_level[i];
                /* Legacy per-lane gate values remain in the session prefix for
                 * compatibility; trigger length is now explicit per step. */
                lane_gate[i] = 128u;
            }
        }
        after = part->revision;
        if (before == after && !(after & 1u)) {
            if (!br_preset_allowed_on_part(p, sound.preset))
                sound.preset = br_default_part_preset(p);
            br_cached_sound[p] = sound;
            br_cached_mod[p] = mod;
            br_cached_fx[p] = fx;
            for (i = 0; i < BR_DRUM_LANES; ++i) {
                br_cached_lane[p][i] = lane[i];
                br_cached_lane_gate[p][i] = lane_gate[i];
            }
            br_cached_revision[p] = after;
            br_cached_mod_inactive[p] =
                (uint8_t)!br_mod_has_active_target(&mod, &sound);
            br_effective_valid[p] = 0u;
            return;
        }
    }
}

static uint32_t br_pack_event(uint32_t op, uint32_t part, uint32_t note, uint32_t vel)
{
    return (op << 30) | ((part & 3u) << 28) | ((note & 127u) << 21) | ((vel & 127u) << 14);
}

static void br_enqueue_timed(uint32_t event, uint32_t timestamp)
{
    uint32_t w = br_event_write, r = br_event_read;
    if (w - r >= BR_EVENT_CAP) {
        ++br_dropped_events;
        if ((event >> 30) == 0u) br_midi_transport_overflow = 1u;
        if ((event >> 30) == BR_EVENT_OFF || (event >> 30) == BR_EVENT_STOP) {
            uint32_t p = (event >> 28) & 3u;
            uint32_t note = (event >> 21) & 127u, vel = (event >> 14) & 127u;
            uint32_t mask = (event >> 30) == BR_EVENT_STOP && p == 3u && note == 127u && vel == 127u
                ? 15u : 1u << p;
            br_panic_parts |= mask;
        }
        return;
    }
    br_events[w & BR_EVENT_MASK] = event;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    br_event_timestamp[w & BR_EVENT_MASK] = timestamp;
    br_event_generation[w & BR_EVENT_MASK] = br_midi_clock_generation;
#else
    (void)timestamp;
#endif
    RING_PUBLISH();
    br_event_write = w + 1u;
}

static void br_enqueue(uint32_t event) { br_enqueue_timed(event, 0u); }

static void br_start_note_now(uint32_t p, uint32_t note, uint32_t velocity, uint32_t slide)
{
    br_sound_t *s = &br_effective_sound[p];
    (void)slide;
    br_mod_note_on(&br_mod_state[p], &br_cached_mod[p], note, velocity);
    if (br_is_kit(s->preset)) {
        uint32_t lane = br_drum_lane(note);
        if (lane < BR_DRUM_LANES) br_seq_lane_gate_frames[p][lane] = 0u;
        br_env_start(p, note, velocity, s, 0u);
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        if (s->preset == BR_KIT_PLAITS) {
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
            uint32_t lane = br_drum_lane(note);
            if (lane < BR_DRUM_LANES)
                plaits_kit_note_on_mask(br_plaits_kit_lane_state(p, lane),
                    note, velocity, 1u << lane);
#else
            plaits_kit_note_on_mask(br_plaits_kit_pair_state(p, BR_KIT_LANES_A), note, velocity, BR_KIT_LANES_A);
            plaits_kit_note_on_mask(br_plaits_kit_pair_state(p, BR_KIT_LANES_B), note, velocity, BR_KIT_LANES_B);
#endif
        }
        else
#endif
        {
            br_drum_note_on_mask(br_drum_pair_state(p, BR_KIT_LANES_A), note, velocity, BR_KIT_LANES_A);
            br_drum_note_on_mask(br_drum_pair_state(p, BR_KIT_LANES_B), note, velocity, BR_KIT_LANES_B);
        }
        return;
    }

    br_env_start(p, note, velocity, s, 1u);
    int32_t pitch = (int32_t)br_map_pitch_q7(p, note);
    if (pitch < 0) pitch = 0;
    if (pitch > 16383) pitch = 16383;
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    if (br_is_plaits(s->preset)) {
        plaits_note_on(br_dsp[p].plaits, s->preset - BR_FIRST_PLAITS,
            (uint32_t)pitch,
            (uint16_t)(br_scale_7bit_q15_exact(br_bound(s->timbre, 127u))),
            (uint16_t)(br_scale_7bit_q15_exact(br_bound(s->color, 127u))),
            (uint16_t)(br_scale_7bit_q15_exact(br_bound(s->plaits_harmonics, 127u))),
            (uint8_t)br_bound(velocity, 127u));
        return;
    }
#endif
    /* Invalid legacy mono IDs are normalized before event dispatch. */
}

/* Cold synth voices cost about 28-30k ticks each in their first half, so three
 * of them starting together can overrun the 139k half budget (measured: one
 * cold note peaks at 93k, a three-note cold chord at 146k). Admit one cold
 * Plaits start per half and defer the others by whole halves. Warm voices and
 * the drum part start immediately. */
#ifndef BR_AUDIO_STAGGER_COLD_SYNTH_STARTS
#define BR_AUDIO_STAGGER_COLD_SYNTH_STARTS 1
#endif
/* Warm voices cost less to start than cold ones, but three of them retriggering on the same beat still add
 * 10 to 18 points to that half (measured on the unit: ordinary halves of a busy session top out near 77 %,
 * the halves on beat boundaries reach 85 to 94 %). With this on, the same one-start-per-half rule applies to
 * them: at most the second and third simultaneous start wait 5.8 and 11.6 ms. */
#ifndef BR_AUDIO_STAGGER_WARM_SYNTH_STARTS
#define BR_AUDIO_STAGGER_WARM_SYNTH_STARTS 0   /* tried on the unit: no gain on the worst half */
#endif
#if BR_AUDIO_STAGGER_COLD_SYNTH_STARTS && defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
typedef struct { uint32_t due; uint8_t note, velocity, slide, valid; } br_deferred_start_t;
static br_deferred_start_t br_deferred_start[BR_SYNTH_PARTS];
static uint32_t br_start_half, br_start_next_free;
/* The kit takes part in the same one-cold-start-per-half rule. Measured on the unit: three synths and four
 * drum lanes all starting from silence reached 97.8 % of a half, against 82.6 % for the three synths alone.
 * A cold kit start claims one half like a cold synth voice; every kit note that arrives for the same start
 * joins it, so lanes that hit together still sound together. */
#ifndef BR_AUDIO_STAGGER_COLD_KIT_STARTS
#define BR_AUDIO_STAGGER_COLD_KIT_STARTS 1
#endif
#if BR_AUDIO_STAGGER_COLD_KIT_STARTS
#define BR_DEFERRED_KIT_NOTES 8u
static br_deferred_start_t br_deferred_kit[BR_DEFERRED_KIT_NOTES];
static uint32_t br_kit_start_at, br_kit_start_open;
#endif

static void br_deferred_start_flush(uint32_t p)
{
    if (p < BR_SYNTH_PARTS && br_deferred_start[p].valid) {
        br_deferred_start_t d = br_deferred_start[p];
        br_deferred_start[p].valid = 0u;
        br_start_note_now(p, d.note, d.velocity, d.slide);
    }
#if BR_AUDIO_STAGGER_COLD_KIT_STARTS
    if (p == BR_DRUM_PART) {
        uint32_t i;
        for (i = 0u; i < BR_DEFERRED_KIT_NOTES; ++i) if (br_deferred_kit[i].valid) {
            br_deferred_start_t d = br_deferred_kit[i];
            br_deferred_kit[i].valid = 0u;
            br_start_note_now(p, d.note, d.velocity, d.slide);
        }
    }
#endif
}

static void br_deferred_start_drop(uint32_t p)
{
    if (p < BR_SYNTH_PARTS) br_deferred_start[p].valid = 0u;
#if BR_AUDIO_STAGGER_COLD_KIT_STARTS
    if (p == BR_DRUM_PART) { memset(br_deferred_kit, 0, sizeof br_deferred_kit); br_kit_start_open = 0u; }
#endif
}

static void br_deferred_start_reset(void)
{
    memset(br_deferred_start, 0, sizeof br_deferred_start);
#if BR_AUDIO_STAGGER_COLD_KIT_STARTS
    memset(br_deferred_kit, 0, sizeof br_deferred_kit);
    br_kit_start_at = br_kit_start_open = 0u;
#endif
    br_start_half = br_start_next_free = 0u;
}

/* Called once per block, before events are applied. */
static void br_deferred_starts_run(void)
{
    uint32_t p;
    ++br_start_half;
    for (p = 0u; p < BR_SYNTH_PARTS; ++p)
        if (br_deferred_start[p].valid &&
            (int32_t)(br_start_half - br_deferred_start[p].due) >= 0)
            br_deferred_start_flush(p);
#if BR_AUDIO_STAGGER_COLD_KIT_STARTS
    if (br_kit_start_open) {
        /* In its own half the planned start plays, and later notes that half still join it. */
        if ((int32_t)(br_start_half - br_kit_start_at) >= 0) br_deferred_start_flush(BR_DRUM_PART);
        if ((int32_t)(br_start_half - br_kit_start_at) > 0) br_kit_start_open = 0u;
    }
#endif
}

static void br_start_note(uint32_t p, uint32_t note, uint32_t velocity, uint32_t slide)
{
    if (p < BR_SYNTH_PARTS && br_is_plaits(br_effective_sound[p].preset) &&
        (BR_AUDIO_STAGGER_WARM_SYNTH_STARTS ||
         (br_env[p].stage == BR_ENV_OFF && plaits_quiet(br_dsp[p].plaits)))) {
        uint32_t at = br_start_next_free;
        if ((int32_t)(at - br_start_half) < 0) at = br_start_half;
        br_start_next_free = at + 1u;
        if (at != br_start_half) {
            br_deferred_start_flush(p);          /* keep order: older start first */
            br_deferred_start[p].due = at;
            br_deferred_start[p].note = (uint8_t)note;
            br_deferred_start[p].velocity = (uint8_t)velocity;
            br_deferred_start[p].slide = (uint8_t)slide;
            br_deferred_start[p].valid = 1u;
            return;
        }
    }
#if BR_AUDIO_STAGGER_COLD_KIT_STARTS
    if (p == BR_DRUM_PART && (br_kit_start_open || br_quiet_part[p])) {
        uint32_t at, i;
        if (br_kit_start_open) at = br_kit_start_at;       /* join the kit start already planned */
        else {
            at = br_start_next_free;
            if ((int32_t)(at - br_start_half) < 0) at = br_start_half;
            br_start_next_free = at + 1u;
            br_kit_start_at = at; br_kit_start_open = 1u;
        }
        if (at != br_start_half) {
            for (i = 0u; i < BR_DEFERRED_KIT_NOTES; ++i) if (!br_deferred_kit[i].valid) {
                br_deferred_kit[i].due = at;
                br_deferred_kit[i].note = (uint8_t)note;
                br_deferred_kit[i].velocity = (uint8_t)velocity;
                br_deferred_kit[i].slide = (uint8_t)slide;
                br_deferred_kit[i].valid = 1u;
                return;
            }
            br_deferred_start_flush(p);          /* queue full: start what is waiting, then this one */
        }
        br_start_note_now(p, note, velocity, slide);
        return;
    }
#endif
    br_deferred_start_flush(p);
    br_start_note_now(p, note, velocity, slide);
}
#else
#define br_deferred_start_flush(p) ((void)(p))
#define br_deferred_start_drop(p) ((void)(p))
#define br_deferred_start_reset() ((void)0)
#define br_deferred_starts_run() ((void)0)
static void br_start_note(uint32_t p, uint32_t note, uint32_t velocity, uint32_t slide)
{
    br_start_note_now(p, note, velocity, slide);
}
#endif

static void br_release_note(uint32_t p, uint32_t note)
{
    br_env_t *e = &br_env[p];
    br_deferred_start_flush(p);
    if (br_is_kit(br_cached_sound[p].preset)) {
        /* Each kit lane has its own gate, independent of the most recent
         * note used by the shared modulation envelope. */
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        if (br_cached_sound[p].preset == BR_KIT_PLAITS)
        {
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
            if (note >= 128u) {
                uint32_t lane;
                for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
                    plaits_kit_note_off_mask(br_plaits_kit_lane_state(p, lane),
                        note, 1u << lane);
            } else {
                uint32_t lane = br_drum_lane(note);
                if (lane < BR_DRUM_LANES)
                    plaits_kit_note_off_mask(br_plaits_kit_lane_state(p, lane),
                        note, 1u << lane);
            }
#else
            plaits_kit_note_off_mask(br_plaits_kit_pair_state(p, BR_KIT_LANES_A), note, BR_KIT_LANES_A);
            plaits_kit_note_off_mask(br_plaits_kit_pair_state(p, BR_KIT_LANES_B), note, BR_KIT_LANES_B);
#endif
        }
#endif
        if (note >= 128u || note == br_mod_state[p].note) {
            br_mod_note_off(&br_mod_state[p]);
            if (e->stage != BR_ENV_OFF && (note >= 128u || e->note == note)) {
                e->stage = BR_ENV_RELEASE; e->release_start = e->amp;
            }
        }
        return;
    }

#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    if (br_is_plaits(br_cached_sound[p].preset)) {
        if (note >= 128u || note == br_env[p].note) {
            /* Raw MIDI identity is checked here, before pitch mapping. */
            plaits_note_off(br_dsp[p].plaits, 128u);
            br_mod_note_off(&br_mod_state[p]);
            if (br_env[p].stage != BR_ENV_OFF) {
                br_env[p].stage = BR_ENV_RELEASE;
                br_env[p].release_start = br_env[p].amp;
            }
        }
        return;
    }
#endif
    if (e->stage == BR_ENV_OFF || (note < 128u && e->note != note)) return;
    br_mod_note_off(&br_mod_state[p]);
    e->stage = BR_ENV_RELEASE;
    e->release_start = e->amp;
}

/* Live keyboard and MIDI notes, newest last, independently for each synth.
 * Sequencer and arpeggiator notes bypass this stack. A count handles the
 * same pitch held by more than one input. This state is never serialized. */
typedef struct { uint8_t note, velocity, count; } br_live_note_t;
static br_live_note_t br_live_notes[BR_SYNTH_PARTS][128];
static uint8_t br_live_count[BR_SYNTH_PARTS];

static void br_live_note_on(uint32_t p, uint32_t note, uint32_t velocity)
{
    br_live_note_t *notes = br_live_notes[p], entry;
    uint32_t i, n = br_live_count[p];
    for (i = 0u; i < n && notes[i].note != note; ++i) {}
    entry.note = (uint8_t)note; entry.velocity = (uint8_t)velocity;
    entry.count = i < n && notes[i].count < 255u ? (uint8_t)(notes[i].count + 1u) : 1u;
    if (i < n) {
        if (notes[i].count == 255u) entry.count = 255u;
        memmove(&notes[i], &notes[i + 1u], (n - i - 1u) * sizeof *notes);
        --n;
    }
    notes[n++] = entry;
    br_live_count[p] = (uint8_t)n;
    br_start_note(p, note, velocity, 0u);
}

static void br_live_note_off(uint32_t p, uint32_t note)
{
    br_live_note_t *notes = br_live_notes[p];
    uint32_t i, n = br_live_count[p], active;
    for (i = 0u; i < n && notes[i].note != note; ++i) {}
    if (i == n) { br_release_note(p, note); return; }
    if (notes[i].count > 1u) { --notes[i].count; return; }
    active = i + 1u == n; /* only release of the newest live note changes priority */
    memmove(&notes[i], &notes[i + 1u], (n - i - 1u) * sizeof *notes);
    br_live_count[p] = (uint8_t)--n;
    br_deferred_start_flush(p);
    if (active && n) br_start_note(p, notes[n - 1u].note, notes[n - 1u].velocity, 0u);
    else br_release_note(p, note);
}

static void br_stop_part(uint32_t p)
{
    br_deferred_start_drop(p);
    if (p < BR_SYNTH_PARTS) br_live_count[p] = 0u;
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    br_audio_fixed8_foreground_invalidate();
#endif
    if (br_mode[p] == 1u || br_mode[p] == 4u) {
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        if (br_mode[p] == 4u) {
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
            uint32_t lane;
            for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
                plaits_kit_all_off_mask(br_plaits_kit_lane_state(p, lane),
                                        1u << lane);
#else
            plaits_kit_all_off_mask(br_plaits_kit_pair_state(p, BR_KIT_LANES_A), BR_KIT_LANES_A);
            plaits_kit_all_off_mask(br_plaits_kit_pair_state(p, BR_KIT_LANES_B), BR_KIT_LANES_B);
#endif
        }
        else
#endif
        {
            br_drum_all_off_mask(br_drum_pair_state(p, BR_KIT_LANES_A), BR_KIT_LANES_A);
            br_drum_all_off_mask(br_drum_pair_state(p, BR_KIT_LANES_B), BR_KIT_LANES_B);
        }
        if (br_env[p].stage != BR_ENV_OFF) {
            br_env[p].stage = BR_ENV_RELEASE;
            br_env[p].release_start = br_env[p].amp;
        }
    }

#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    else if (br_mode[p] == 3u) {
        plaits_all_off(br_dsp[p].plaits);
        if (br_env[p].stage != BR_ENV_OFF) {
            br_env[p].stage = BR_ENV_RELEASE;
            br_env[p].release_start = br_env[p].amp;
        }
    }
#endif
    else br_release_note(p, 128u);
    br_mod_note_off(&br_mod_state[p]);
    br_seq_note[p] = 255u;
    br_seq_tie[p] = 0;
    br_seq_gate_frames[p] = 0;
    br_seq_deferred_off[p] = 0u;
    br_seq_frames[p] = 0u;
    br_seq_started[p] = 0u;
    memset(br_micro_event[p], 0, sizeof br_micro_event[p]);
    if (p == BR_DRUM_PART) memset(br_retrig, 0, sizeof br_retrig);
    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
        br_seq_lane_gate_frames[p][lane] = 0u;
}

/* Release sounding voices on MIDI Stop, but keep the exact sequencer
 * deadlines and pending events frozen until Continue. */
static void br_midi_pause_part(uint32_t p)
{
    uint32_t frames = br_seq_frames[p], gate = br_seq_gate_frames[p];
    uint32_t lane_gates[BR_DRUM_LANES];
    uint8_t note = br_seq_note[p], tie = br_seq_tie[p];
    uint8_t deferred = br_seq_deferred_off[p], started = br_seq_started[p];
    br_micro_event_t micro[BR_SEQ_EVENT_SLOTS];
    br_retrig_t retrig[BR_DRUM_LANES];
    memcpy(micro, br_micro_event[p], sizeof micro);
    memcpy(lane_gates, br_seq_lane_gate_frames[p], sizeof lane_gates);
    if (p == BR_DRUM_PART) memcpy(retrig, br_retrig, sizeof retrig);
    if (p < BR_SYNTH_PARTS) {
        br_midi_paused_velocity[p] = br_env[p].velocity;
#if BR_AUDIO_STAGGER_COLD_SYNTH_STARTS && defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        if (br_deferred_start[p].valid && br_deferred_start[p].note == note)
            br_midi_paused_velocity[p] = br_deferred_start[p].velocity;
#endif
    }
    br_stop_part(p);
    br_seq_frames[p] = frames; br_seq_gate_frames[p] = gate;
    br_seq_note[p] = note; br_seq_tie[p] = tie;
    br_seq_deferred_off[p] = deferred; br_seq_started[p] = started;
    memcpy(br_micro_event[p], micro, sizeof micro);
    memcpy(br_seq_lane_gate_frames[p], lane_gates, sizeof lane_gates);
    if (p == BR_DRUM_PART) memcpy(br_retrig, retrig, sizeof retrig);
}

static void br_transport_reset_position(void)
{
    uint32_t p;
    br_midi_paused = 0u;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (br_clock_experiment.enabled) {
        /* MIDI Start begins the arpeggiator's note order afresh too. */
        br_arp_reset();
        for (p = 0u; p < BR_PARTS; ++p)
            fm1_mcs_start(&br_clock_experiment.track[p], br_clock_experiment.phase_q16);
        memset(br_clock_experiment.arp_ready, 0, sizeof br_clock_experiment.arp_ready);
        memset(br_clock_experiment.arp_start_armed, 1, sizeof br_clock_experiment.arp_start_armed);
        br_clock_experiment.arp_start_phase_q16 = br_clock_experiment.phase_q16;
    }
#endif
    memset(br_retrig, 0, sizeof br_retrig);
    br_metro_frames = 0u;
    br_transport_phase = 0u;
    br_global_beat = 0u;
#if defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
    br_pattern_host_reset_clock();
#endif
    for (p = 0; p < BR_PARTS; ++p) {
        br_seq_frames[p] = br_seq_gate_frames[p] = 0;
        br_seq_note[p] = 255u; br_seq_tie[p] = 0; br_seq_next[p] = 0;
        br_seq_deferred_off[p] = 0u;
        br_seq_started[p] = 0u;
        memset(br_micro_event[p], 0, sizeof br_micro_event[p]);
        br_step_pos[p] = 0;
    }
    for (p = 0u; p < BR_SEQ_TRACKS; ++p) {
        br_seq_cycle[p] = 0u;
        br_seq_prev_condition_valid[p] = 0u;
        br_seq_prev_condition_result[p] = 0u;
    }
    for (p = 0u; p < BR_DRUM_LANES; ++p)
        br_seq_lane_gate_frames[BR_DRUM_PART][p] = 0u;
}

static void br_midi_apply_transport(uint32_t status)
{
    uint32_t p;
    if (status == 0xFCu) {
        if (br_playing || br_transport_prev) {
            for (p = 0u; p < BR_PARTS; ++p) br_midi_pause_part(p);
            br_midi_paused = 1u;
        }
        br_playing = br_transport_prev = 0u;
    } else if (status == 0xFBu) {
        if (br_midi_paused) {
            br_midi_paused = 0u;
            br_playing = br_transport_prev = 1u;
            for (p = 0u; p < BR_SYNTH_PARTS; ++p)
                if (br_seq_note[p] != 255u && br_seq_gate_frames[p] &&
                    !br_seq_deferred_off[p])
                    br_start_note(p, br_seq_note[p], br_midi_paused_velocity[p], 0u);
        } else if (!br_playing) {
            /* With no paused position, Continue starts at the beginning. */
            br_transport_reset_position();
            br_playing = br_transport_prev = 1u;
        }
    } else if (status == 0xFAu) {
        if (br_playing || br_midi_paused)
            for (p = 0u; p < BR_PARTS; ++p) br_stop_part(p);
        br_transport_reset_position();
        br_playing = br_transport_prev = 1u;
    }
}

#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
/* Single audio-owner writes: no per-block stores or atomic operations. */
static volatile uint32_t br_clock_fail_count[5];
static volatile uint32_t br_clock_last_failure;
/* Updated only when a mapped F8 is processed; audio-owned, sampled under IRQ mask. */
static volatile uint32_t br_clock_f8_processed, br_clock_f8_rejected, br_clock_f8_suspect;
static volatile uint32_t br_clock_interval_count, br_clock_interval_sum;
static volatile uint32_t br_clock_interval_last, br_clock_interval_min, br_clock_interval_max;
static uint64_t br_clock_prev_f8_frame;
static uint32_t br_clock_prev_f8_valid;
#endif
static void br_clock_fail(uint32_t reason)
{
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    uint32_t i;
    for (i = 0u; i < 5u; ++i)
        if (reason & (1u << i)) ++br_clock_fail_count[i];
    br_clock_last_failure = reason;
#else
    (void)reason;
#endif
    br_clock_experiment.enabled = 0u;
    br_clock_experiment.invalid = 1u;
    br_clock_experiment.follower.running = 0u;
    br_clock_experiment.follower.pending = 0u;
    br_clock_experiment.follower.have_tick = 0u;
    br_clock_experiment.follower.intervals = 0u;
    br_clock_experiment.follower.locked = 0u;
    br_clock_experiment.follower.probe_active = 0u;
    br_clock_experiment.follower.probe_intervals = 0u;
    br_clock_experiment.follower.multiple_run = 0u;
    br_clock_experiment.follower.window_have = 0u;
    br_clock_experiment.follower.window_intervals = 0u;
    br_clock_experiment.wait_transport = 0u;
    br_midi_clock_active = 0u;
    br_midi_apply_transport(0xFCu);
}

/* Runs on the audio owner. The producer's timestamp remains the follower
 * observation time even when processing it in a later writable block. */
static int br_clock_event(uint32_t status, uint32_t tick)
{
    uint64_t event_frame;
    uint32_t flags;
    if (!br_clock_experiment.anchor.valid ||
        !fm1_mct_map(&br_clock_experiment.anchor, tick, 48000000u, &event_frame)) {
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
        if (status == 0xF8u) { ++br_clock_f8_processed; ++br_clock_f8_rejected; }
        br_clock_prev_f8_valid = 0u;
#endif
        br_clock_fail(BR_CLOCK_FAIL_ANCHOR); return 1;
    }
    if (event_frame > br_clock_experiment.render_frame) return 0;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    if (status == 0xF8u) {
        ++br_clock_f8_processed;
        if (br_clock_prev_f8_valid && event_frame >= br_clock_prev_f8_frame &&
            event_frame - br_clock_prev_f8_frame <= UINT32_MAX) {
            uint32_t interval = (uint32_t)(event_frame - br_clock_prev_f8_frame);
            if (!br_clock_interval_count || interval < br_clock_interval_min) br_clock_interval_min = interval;
            if (!br_clock_interval_count || interval > br_clock_interval_max) br_clock_interval_max = interval;
            br_clock_interval_last = interval;
            br_clock_interval_sum += interval;
            ++br_clock_interval_count;
        }
        br_clock_prev_f8_frame = event_frame; br_clock_prev_f8_valid = 1u;
    }
#endif
    if (status == 0xF2u) {
        /* SPP position reconstruction is not implemented. Stop rather than
         * allowing a later Continue to play the wrong song location. */
        br_clock_fail(BR_CLOCK_FAIL_SPP);
        br_clock_experiment.seek_unsupported = 1u;
        return 1;
    }
    if (status == 0xFBu && br_clock_experiment.seek_unsupported) return 1;
    if (status == 0xFAu) br_clock_experiment.seek_unsupported = 0u;
    /* An idle follower has no advancing phase; rebase its event-time cursor
     * at the first new event after arbitrary silence, without a timer-wrap
     * limit or fabricating elapsed clock pulses. */
    if (!br_clock_experiment.follower.have_tick && !br_clock_experiment.follower.running &&
        !br_clock_experiment.follower.pending && event_frame > br_clock_experiment.follower.now)
        br_clock_experiment.follower.now = event_frame;
    flags = fm1_mc_event(&br_clock_experiment.follower, (uint8_t)status, event_frame);
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    if (status == 0xF8u) {
        if (flags & (FM1_MC_INVALID | FM1_MC_LOST | FM1_MC_REJECTED)) ++br_clock_f8_rejected;
        if (flags & FM1_MC_SUSPECT) ++br_clock_f8_suspect;
    }
#endif
    if (flags & (FM1_MC_INVALID | FM1_MC_LOST)) {
        br_clock_fail(((flags & FM1_MC_INVALID) ? BR_CLOCK_FAIL_INVALID : 0u) |
                      ((flags & FM1_MC_LOST) ? BR_CLOCK_FAIL_LOST : 0u)); return 1;
    }
    if ((flags & FM1_MC_SUSPECT) && br_playing) {
        br_clock_experiment.suspend_phase_q16 = br_clock_experiment.phase_end_q16;
        br_midi_apply_transport(0xFCu);
        br_clock_experiment.enabled = 0u;
        br_clock_experiment.suspect_pause = 1u;
    }
    if ((flags & FM1_MC_LOCKED) && br_clock_experiment.suspect_pause &&
        br_clock_experiment.follower.running) {
        uint64_t shift;
        if (br_clock_experiment.follower.phase_q16 < br_clock_experiment.suspend_phase_q16) {
            br_clock_fail(BR_CLOCK_FAIL_INVALID); return 1;
        }
        shift = br_clock_experiment.follower.phase_q16 - br_clock_experiment.suspend_phase_q16;
#if defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
        br_pattern_host_clock_shift(shift);
#endif
        for (uint32_t p = 0u; p < BR_PARTS; ++p) {
            if (UINT64_MAX - br_clock_experiment.track[p].target_phase_q16 < shift) {
                br_clock_fail(BR_CLOCK_FAIL_INVALID); return 1;
            }
            br_clock_experiment.track[p].target_phase_q16 += shift;
        }
        br_clock_experiment.suspect_pause = 0u;
        br_clock_experiment.enabled = 1u;
        br_midi_apply_transport(0xFBu);
    }
    if (flags & (FM1_MC_START | FM1_MC_CONTINUE)) {
        uint32_t command = (flags & FM1_MC_START) ? 0xFAu : 0xFBu;
        br_clock_experiment.suspect_pause = 0u;
        br_clock_experiment.invalid = 0u;
        if (!br_clock_experiment.follower.locked) {
            /* No trustworthy period yet: do not render against the local
             * provisional tempo. Lock, then join at a quarter boundary. */
            br_clock_experiment.enabled = 0u;
            br_clock_experiment.wait_transport = command;
            br_clock_experiment.wait_origin_q16 = br_clock_experiment.follower.phase_q16;
            br_clock_experiment.wait_boundary_q16 = UINT64_MAX;
        } else {
            br_clock_experiment.wait_transport = 0u;
            br_clock_experiment.enabled = 1u;
            if (command == 0xFAu) br_clock_experiment.phase_q16 = 0u;
            br_midi_apply_transport(command);
        }
    }
    if (flags & FM1_MC_STOP) {
        br_clock_experiment.suspect_pause = 0u;
        br_clock_experiment.wait_transport = 0u;
        br_midi_apply_transport(0xFCu);
    }
    return 1;
}
static void br_clock_project(void)
{
    fm1_midi_clock_t projected = br_clock_experiment.follower;
    uint32_t flags;
    if (!br_midi_clock_enabled || !br_clock_experiment.anchor.valid) return;
    /* An idle follower does not advance phase. Its last event may be older
     * than the projection span limit; rebase the copy without changing the
     * event-time cursor or hiding invalid spans while running/acquiring. */
    if (!projected.have_tick && !projected.pending && !projected.running &&
        br_clock_experiment.render_frame > projected.now &&
        br_clock_experiment.render_frame - projected.now > 0x00ffffffu)
        projected.now = br_clock_experiment.render_frame;
    flags = fm1_mc_advance(&projected, br_clock_experiment.render_frame);
    if (flags & (FM1_MC_INVALID | FM1_MC_LOST)) {
        /* Preserve the audible stop position while keeping the follower's
         * event-time cursor: later-delivered pulses can predate this horizon. */
        br_clock_experiment.follower.phase_q16 = projected.phase_q16;
        br_clock_experiment.follower.phase_remainder = 0u;
        br_clock_fail(((flags & FM1_MC_INVALID) ? BR_CLOCK_FAIL_INVALID : 0u) |
                      ((flags & FM1_MC_LOST) ? BR_CLOCK_FAIL_LOST : 0u)); return;
    }
    if (br_clock_experiment.wait_transport && projected.locked) {
        if (br_clock_experiment.wait_boundary_q16 == UINT64_MAX) {
            uint64_t rem, quarter = (uint64_t)24u << 16;
            uint64_t beat = fm1_clock_div64(projected.phase_q16, quarter, &rem);
            br_clock_experiment.wait_boundary_q16 = (beat + !!rem) * quarter;
        }
        if (projected.phase_q16 >= br_clock_experiment.wait_boundary_q16) {
            uint32_t command = br_clock_experiment.wait_transport;
            br_clock_experiment.wait_transport = 0u;
            br_clock_experiment.phase_q16 = br_clock_experiment.wait_boundary_q16;
            br_clock_experiment.enabled = 1u;
            br_midi_apply_transport(command);
        }
    }
    if (projected.period_q16) {
        if (br_clock_experiment.period_q16 != projected.period_q16 || !br_midi_clock_bpm)
            br_midi_clock_bpm = (uint32_t)fm1_clock_div64((uint64_t)FS * 60u << 16,
                                          projected.period_q16 * 24u, 0);
        br_clock_experiment.period_q16 = projected.period_q16;
        br_clock_experiment.phase_q16 = projected.phase_q16;
    }
    br_midi_clock_active = projected.locked;
}
#endif

static void br_take_events(void)
{
    br_deferred_starts_run();
    while (br_event_read != br_event_write) {
        uint32_t e = br_events[br_event_read & BR_EVENT_MASK];
        uint32_t op = e >> 30, p = (e >> 28) & 3u;
        uint32_t note = (e >> 21) & 127u, vel = (e >> 14) & 127u;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        if (op == 0u && br_event_generation[br_event_read & BR_EVENT_MASK] !=
                        br_midi_clock_generation) {
            RING_PUBLISH(); br_event_read++;
            continue;
        }
        if (op == 0u && (e & 0x100u)) {
            uint32_t timestamp = br_event_timestamp[br_event_read & BR_EVENT_MASK];
            if (br_midi_clock_enabled && !br_clock_event(e & 255u, timestamp)) break;
            RING_PUBLISH(); br_event_read++;
            continue;
        }
#endif
        RING_PUBLISH();
        br_event_read++;
        if (op == 0u) {
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
            if ((e & 255u) == 0xFCu && br_midi_clock_enabled) {
                br_clock_experiment.follower.running = 0u;
                br_clock_experiment.follower.pending = 0u;
                br_clock_experiment.suspect_pause = 0u;
                br_clock_experiment.wait_transport = 0u;
                br_clock_experiment.follower.phase_q16 = br_clock_experiment.phase_end_q16;
                br_clock_experiment.follower.phase_remainder = 0u;
            }
#endif
            br_midi_apply_transport(e & 255u);
        }
        else if (op == BR_EVENT_ON) {
            if (p < BR_SYNTH_PARTS) br_live_note_on(p, note, vel);
            else br_start_note(p, note, vel, 0u);
        } else if (op == BR_EVENT_OFF) {
            if (p < BR_SYNTH_PARTS) br_live_note_off(p, note);
            else br_release_note(p, note);
        }
        else if (op == BR_EVENT_STOP) {
            if (p == 3u && note == 127u && vel == 127u) {
                uint32_t i; for (i = 0; i < BR_PARTS; ++i) br_stop_part(i);
            }
            else br_stop_part(p);
        }
    }
    if (br_midi_transport_overflow) {
        br_midi_transport_overflow = 0u;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        if (br_midi_clock_enabled) br_clock_fail(BR_CLOCK_FAIL_OVERFLOW);
        else
#endif
            br_midi_apply_transport(0xFCu);
    }
    if (br_panic_parts) {
        uint32_t mask = br_panic_parts, p;
        br_panic_parts = 0;
        for (p = 0; p < BR_PARTS; ++p)
            if (mask & (1u << p)) br_stop_part(p);
    }
}

static void br_select_mode(uint32_t p)
{
    if (!br_preset_allowed_on_part(p, br_cached_sound[p].preset)) {
        br_cached_sound[p].preset = br_default_part_preset(p);
        br_effective_valid[p] = 0u;
    }
    uint8_t mode = br_is_kit(br_cached_sound[p].preset) ?
        (br_cached_sound[p].preset == BR_KIT_PLAITS ? 4u : 1u) :
        3u;
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    if (br_is_plaits(br_cached_sound[p].preset)) mode = 3u;
#endif
    uint8_t preset = (uint8_t)br_cached_sound[p].preset;
    if (preset == br_active_preset[p]) return;
#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR && \
    defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS && defined(BR_SPLIT_AUDIO)
    /* A preset transition invalidates the previous timing/count pairing. */
    uint32_t lane;
    for (lane = 0u; lane < BR_AUDIO_KIT_JOB_LANES; ++lane) {
        br_audio_plaits_prev_append_profile[p][lane] = 0u;
        br_audio_plaits_current_append_profile[p][lane] = 0u;
        br_audio_plaits_prev_cost_signature[p][lane] = 0u;
        br_audio_plaits_current_cost_signature[p][lane] = 0u;
        br_audio_plaits_prev_cost_valid[p][lane] = 0u;
        br_audio_plaits_current_cost_valid[p][lane] = 0u;
    }
#endif
    /* Plaits owns model activation inside Voice::Render. Preserve the track's
     * live gate/envelope, modulation phase, filter state and note lengths when
     * only its model changes. Initial construction still takes the path below. */
    if (mode == 3u && br_mode[p] == 3u && br_active_preset[p] != 255u) {
        br_active_preset[p] = preset;
        return;
    }
    br_mode[p] = mode;
    br_active_preset[p] = preset;
    br_env[p].stage = BR_ENV_OFF;
    br_env[p].amp = 0;
    br_filter_init(&br_filter[p]);
    br_mod_init(&br_mod_state[p], 0x4D4F4400u + p + preset);
#if BR_MOD_LFO_PHASE_DELTA_CACHE
    br_mod_phase_delta_cache_reset(p);
#endif
    br_active_raw_note[p] = br_active_dsp_note[p] = 255u;
    if (mode == 1u) {
        br_drum_init(br_drum_pair_state(p, BR_KIT_LANES_A), 0x42520021u + p, preset);
        br_drum_init(br_drum_pair_state(p, BR_KIT_LANES_B), 0x42520021u + p, preset);
    }
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    else if (mode == 4u) {
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
        void *states[BR_DRUM_LANES];
        uint32_t lane;
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
            states[lane] = br_plaits_kit_lane_state(p, lane);
        plaits_kit_init_quad(states, 0x42520021u + p);
#else
        plaits_kit_init_pair(br_plaits_kit_pair_state(p, BR_KIT_LANES_A),
            br_plaits_kit_pair_state(p, BR_KIT_LANES_B), 0x42520021u + p);
#endif
    }
    else if (mode == 3u) {
        plaits_state_init(br_dsp[p].plaits, 0x42520021u + p);
        plaits_prepare_model(br_dsp[p].plaits,
                             (uint32_t)preset - BR_FIRST_PLAITS);
    }
#endif
    br_seq_note[p] = 255u;
    br_seq_tie[p] = 0;
    br_seq_gate_frames[p] = 0;
    for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
        br_seq_lane_gate_frames[p][lane] = 0u;
}

static uint32_t br_step_base_duration(uint32_t p)
{
    uint32_t division = br_bound(BR_PLAY_FIELD(p, division), 2u);
    uint32_t quarter = br_tempo_quarter_frames();
    uint32_t base = quarter / (2u << division);
    return base ? base : 1u;
}

static uint32_t br_step_duration_from_base(uint32_t p, uint32_t step, uint32_t base)
{
    /* Track 1's legacy swing field is the shared project setting. Older
     * projects with different track values retain their bytes on save. */
    uint32_t swing;
    (void)p;
    swing = br_bound(br_part[0].swing, BR_SWING_MAX);
    if (step & 1u) base = base * (128u - swing) / 128u;
    else base = base * (128u + swing) / 128u;
    return base ? base : 1u;
}

static uint32_t br_step_duration(uint32_t p, uint32_t step)
{
    return br_step_duration_from_base(p, step, br_step_base_duration(p));
}

BR_AUDIO_API uint32_t br_engine_step_duration(uint32_t p, uint32_t step)
{
    (void)step;
    return p < BR_PARTS ? br_step_base_duration(p) : 1u;
}

static uint32_t br_seq_random(uint32_t row)
{
    uint32_t x = br_seq_rng[row];
    if (!x) x = 0x9E3779B9u ^ (row + 1u);
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    br_seq_rng[row] = x ? x : 0x6D2B79F5u;
    return br_seq_rng[row];
}

static uint32_t br_seq_is_pre_condition(uint32_t condition)
{
    return condition == BR_STEP_COND_PRE || condition == BR_STEP_COND_NOT_PRE;
}

static uint32_t br_seq_condition_for_cycle(uint32_t row, uint32_t condition,
                                           uint32_t previous_valid,
                                           uint32_t previous_result, uint32_t cycle)
{
    if (condition == BR_STEP_COND_ALWAYS || condition >= BR_STEP_COND_COUNT)
        return 1u;
    if (condition <= 100u)
        return condition == 100u || (br_seq_random(row) % 100u) < condition;
    if (condition == BR_STEP_COND_FIRST) return cycle == 0u;
    if (condition == BR_STEP_COND_NOT_FIRST) return cycle != 0u;
    if (condition == BR_STEP_COND_PRE) return previous_valid && previous_result;
    if (condition == BR_STEP_COND_NOT_PRE) return !previous_valid || !previous_result;
    if (condition == BR_STEP_COND_NEI)
        return row != 0u && br_seq_prev_condition_valid[row - 1u] &&
               br_seq_prev_condition_result[row - 1u];
    if (condition == BR_STEP_COND_NOT_NEI)
        return row == 0u || !br_seq_prev_condition_valid[row - 1u] ||
               !br_seq_prev_condition_result[row - 1u];
    if (condition == BR_STEP_COND_FILL) return br_fill_active != 0u;
    if (condition == BR_STEP_COND_NOT_FILL) return br_fill_active == 0u;
    if (condition >= BR_STEP_COND_AB_BASE) {
        uint32_t code = condition - BR_STEP_COND_AB_BASE;
        uint32_t denominator;
        for (denominator = 1u; denominator <= 8u; ++denominator) {
            uint32_t choices = denominator;
            if (code < choices) {
                uint32_t numerator = code + 1u;
                return cycle % denominator == numerator - 1u;
            }
            code -= choices;
        }
    }
    return 1u;
}

static uint32_t br_seq_condition_result(uint32_t row, uint32_t condition,
                                        uint32_t previous_valid, uint32_t previous_result)
{ return br_seq_condition_for_cycle(row, condition, previous_valid, previous_result, br_seq_cycle[row]); }

static uint32_t br_seq_length_frames(uint32_t duration, uint32_t units)
{
    uint32_t frames;
    if (!units) units = BR_STEP_LENGTH_DEFAULT;
    if (units > BR_STEP_LENGTH_MAX) units = BR_STEP_LENGTH_MAX;
    frames = (duration * units + 15u) / 16u;
    return frames ? frames : 1u;
}

static void br_seq_note_expire(uint32_t p)
{
    if (br_seq_note[p] != 255u) br_release_note(p, br_seq_note[p]);
    br_seq_note[p] = 255u;
    br_seq_tie[p] = 0u;
    br_seq_gate_frames[p] = 0u;
    br_seq_deferred_off[p] = 0u;
}

static void br_start_sequence_kit_voice(uint32_t p, uint32_t lane,
                                       uint32_t voice_id, uint32_t velocity,
                                       uint32_t pitch)
{
    br_sound_t *sound = &br_effective_sound[p];
    uint32_t note = br_drum_lane_note(lane);
    int32_t pitch_delta = (int32_t)pitch - (int32_t)BR_STEP_PITCH_DEFAULT;
    int32_t pitched_note = (int32_t)note + pitch_delta;
    if (pitched_note < 0) pitched_note = 0;
    if (pitched_note > 127) pitched_note = 127;
    br_mod_note_on(&br_mod_state[p], &br_cached_mod[p],
                   (uint32_t)pitched_note, velocity);
    br_env_start(p, (uint32_t)pitched_note, velocity, sound, 0u);
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    if (sound->preset == BR_KIT_PLAITS) {
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
        plaits_kit_note_on_voice_pitch_mask(
            br_plaits_kit_lane_state(p, lane), note, velocity,
            1u << lane, voice_id, pitch_delta);
#else
        uint32_t mask = lane < 2u ? BR_KIT_LANES_A : BR_KIT_LANES_B;
        plaits_kit_note_on_voice_pitch_mask(
            br_plaits_kit_pair_state(p, mask), note, velocity, 1u << lane,
            voice_id, pitch_delta);
#endif
        return;
    }
#endif
    br_drum_note_on_voice_pitch_mask(br_drum_pair_state(p, BR_KIT_LANES_A),
        note, velocity, BR_KIT_LANES_A, voice_id, pitch_delta);
    br_drum_note_on_voice_pitch_mask(br_drum_pair_state(p, BR_KIT_LANES_B),
        note, velocity, BR_KIT_LANES_B, voice_id, pitch_delta);
}

/* Emit one step/voice with the original conditional and gate behavior. */
static void br_seq_fire_timed(uint32_t p, uint32_t pos, uint32_t event_lane, uint32_t cycle,
                              uint32_t frozen_base_duration)
{
    uint32_t word = BR_PLAY_STEP(p, pos);
    uint32_t base_duration = frozen_base_duration ? frozen_base_duration : br_step_base_duration(p);
    if (br_is_kit(br_cached_sound[p].preset)) {
        uint32_t hits = BR_STEP_HIT(word), lane;
        for (lane = event_lane; lane == event_lane && lane < BR_DRUM_LANES; ++lane) {
            uint32_t condition, active, units, voice, velocity, pitch;
            uint32_t row = BR_SYNTH_PARTS + lane;
            if (!(hits & (1u << lane))) continue;
            condition = br_play_condition(p, pos, lane);
            active = br_seq_condition_for_cycle(row, condition,
                br_seq_prev_condition_valid[row],
                br_seq_prev_condition_result[row], cycle);
            if (condition != BR_STEP_COND_ALWAYS &&
                !br_seq_is_pre_condition(condition)) {
                br_seq_prev_condition_valid[row] = 1u;
                br_seq_prev_condition_result[row] = (uint8_t)active;
            }
            if (!active) continue;
            velocity = BR_PLAY_META(kit_velocity, pos, lane);
            if (!velocity) continue;
            units = BR_PLAY_META(kit_length, pos, lane);
            br_seq_lane_gate_frames[p][lane] = units ?
                br_seq_length_frames(base_duration, units) : 0u;
            voice = BR_PLAY_META(kit_note, pos, lane);
            pitch = br_play_pitch(p, pos, lane);
            {
                br_retrig_t *r = &br_retrig[lane];
                uint32_t setting = BR_PLAY_META(kit_retrig, pos, lane);
                /* An actual hit replaces any earlier repeat, even in this block. */
                memset(r, 0, sizeof *r);
                r->count = setting & 7u;
                if (r->count) {
                    uint32_t quarter = frozen_base_duration ?
                        base_duration * (2u << br_bound(BR_PLAY_FIELD(p, division), 2u)) : br_tempo_quarter_frames();
                    r->interval = (quarter << ((setting >> 3) & 7u)) / 32u;
                    if (!r->interval) r->interval = 1u;
                    r->remaining = r->interval;
                    r->gate = br_seq_lane_gate_frames[p][lane];
                    r->step = (uint8_t)pos; r->setting = (uint8_t)setting;
                    r->voice = (uint8_t)voice; r->velocity = (uint8_t)velocity;
                    r->pitch = (uint8_t)pitch;
                }
            }
            br_start_sequence_kit_voice(p, lane, voice, velocity, pitch);
        }
        br_seq_gate_frames[p] = 0u;
    } else {
        uint32_t active = !!(word & BR_STEP_ON), note = BR_STEP_NOTE(word);
        uint32_t condition = br_play_condition(p, pos, 0u);
        uint32_t velocity = BR_STEP_VEL(word), tie = !!(word & BR_STEP_TIE);
        if (active) {
            uint32_t metadata_velocity = BR_PLAY_META(synth_velocity, p, pos);
            if (metadata_velocity < 128u) {
                velocity = metadata_velocity;
                if (!velocity) active = 0u;
            } else if (!velocity) velocity = 100u;
        }
        if (active) {
            active = br_seq_condition_for_cycle(p, condition,
                br_seq_prev_condition_valid[p],
                br_seq_prev_condition_result[p], cycle);
            if (condition != BR_STEP_COND_ALWAYS &&
                !br_seq_is_pre_condition(condition)) {
                br_seq_prev_condition_valid[p] = 1u;
                br_seq_prev_condition_result[p] = (uint8_t)active;
            }
        }
        if (active) {
            uint32_t prev_tied = br_seq_note[p] != 255u && br_seq_tie[p];
            uint32_t slide = 0u; /* Plaits retriggers retain envelope continuity. */
            uint32_t sustain =
                prev_tied && br_seq_note[p] == note;
            if (br_seq_note[p] != 255u && !sustain && !slide)
                br_release_note(p, br_seq_note[p]);
            if (!sustain) br_start_note(p, note, velocity, slide);
            br_seq_note[p] = (uint8_t)note;
            br_seq_tie[p] = (uint8_t)tie;
            br_seq_deferred_off[p] = 0u;
            br_seq_gate_frames[p] = br_seq_length_frames(
                base_duration, br_play_length(p, pos, 0u));
        } else {
            /* An empty or failed conditional step does not truncate the
             * current note; it only breaks a tie chain. Its own deadline
             * remains responsible for the matching note-off. */
            br_seq_tie[p] = 0u;
        }
    }
}

static void br_seq_fire(uint32_t p, uint32_t pos, uint32_t lane, uint32_t cycle)
{ br_seq_fire_timed(p, pos, lane, cycle, 0u); }

static void br_micro_schedule_timed(uint32_t part, uint32_t step, uint32_t lane,
                                    uint32_t delay, uint32_t cycle, uint32_t clock_base)
{
    for (uint32_t i = 0u; i < BR_SEQ_EVENT_SLOTS; ++i) {
        br_micro_event_t *event = &br_micro_event[part][i];
        if (!event->active) {
            event->remaining = delay; event->cycle = cycle; event->step = (uint8_t)step;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
            event->clock_base_duration = clock_base;
#else
            (void)clock_base;
#endif
            event->lane = (uint8_t)lane; event->active = 1u; return;
        }
    }
}
static void br_micro_schedule(uint32_t part, uint32_t step, uint32_t lane, uint32_t delay, uint32_t cycle)
{ br_micro_schedule_timed(part, step, lane, delay, cycle, 0u); }
static void br_micro_due(uint32_t part)
{
    for (uint32_t i = 0u; i < BR_SEQ_EVENT_SLOTS; ++i) {
        br_micro_event_t *event = &br_micro_event[part][i];
        if (event->active && !event->remaining) {
            uint32_t step = event->step, lane = event->lane, cycle = event->cycle;
            event->active = 0u;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
            br_seq_fire_timed(part, step, lane, cycle, event->clock_base_duration);
#else
            br_seq_fire(part, step, lane, cycle);
#endif
        }
    }
}
/* Punch-in loop: while the effect is held the sequencer repeats a slice of the
 * bar, starting at the step that was playing. A shadow position keeps counting
 * as if playback had continued, so releasing resumes in sync. */
static uint8_t br_loop_on[BR_PARTS], br_loop_start[BR_PARTS], br_loop_i[BR_PARTS],
               br_loop_shadow[BR_PARTS];

#if defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
/* Bind and retire require a stopped/quiescent renderer except submission
 * to the publication mailbox. A retired slot must be released by the host
 * after every prior audio worker has joined. */
BR_AUDIO_API int br_pattern_irq_audio_bind(br_pattern_irq_publication *pub)
{
    if (!pub || (br_pattern_host_publication && br_pattern_host_publication != pub) ||
        br_playing || br_midi_paused || br_pattern_irq_gate ||
        br_pattern_irq_foreground_busy || br_pattern_host_retired != BR_PATTERN_IRQ_NO_SLOT ||
        pub->outstanding || pub->writer_slot != BR_PATTERN_IRQ_NO_SLOT ||
        pub->request_seq != pub->audio_seen || pub->ack_seq != pub->audio_seen ||
        pub->audio_retired != BR_PATTERN_IRQ_NO_SLOT) return 0;
    br_pattern_host_publication = pub;
    br_pattern_host_snapshot = 0;
    br_pattern_irq_gate = br_pattern_irq_return_request = 0u;
    br_pattern_irq_foreground_busy = br_pattern_irq_late_blocks = 0u;
    br_pattern_host_due = br_pattern_host_bars = br_pattern_host_error = 0u;
    return 1;
}
/* Claim/release are foreground-only, with audio IRQ masked for the check
 * and claim. Do not wrap the actual input processing in a long IRQ mask. */
/* Stopped recovery only: caller must exclude the audio IRQ, quiesce source
 * workers and all editors, and resolve/cancel the bank transaction first.
 * A paused MIDI position is not reset here: the caller must establish a
 * fully stopped transport and use a fresh Start for a new bar origin. */
BR_AUDIO_API int br_pattern_irq_audio_reset(br_pattern_irq_publication *pub)
{
    if (!pub || pub != br_pattern_host_publication || br_playing || br_midi_paused || br_pattern_irq_gate ||
        br_pattern_irq_return_request || br_pattern_irq_foreground_busy ||
        br_pattern_host_retired != BR_PATTERN_IRQ_NO_SLOT ||
        pub->writer_slot != BR_PATTERN_IRQ_NO_SLOT ||
        pub->request_seq != pub->audio_seen || pub->ack_seq != pub->audio_seen ||
        pub->audio_retired != BR_PATTERN_IRQ_NO_SLOT) return 0;
    br_pattern_irq_poll(pub);
    if (pub->outstanding) return 0;
    br_pattern_host_publication = pub;
    br_pattern_host_snapshot = 0;
    br_pattern_host_error = 0u;
    br_pattern_host_reset_clock();
    return 1;
}
BR_AUDIO_API uint32_t br_pattern_irq_edits_blocked(void)
{ return br_pattern_irq_gate; }
BR_AUDIO_API uint32_t br_pattern_irq_bar_count(void)
{ return br_pattern_host_bars; }
BR_AUDIO_API uint32_t br_pattern_irq_late_count(void)
{ return br_pattern_irq_late_blocks; }
BR_AUDIO_API uint32_t br_pattern_irq_error(void)
{ return br_pattern_host_error; }
BR_AUDIO_API int br_pattern_irq_foreground_claim(void)
{
    if (br_pattern_irq_gate || br_pattern_irq_foreground_busy) return 0;
    br_pattern_irq_foreground_busy = 1u; fm1_memory_publish_sync(); return 1;
}
BR_AUDIO_API void br_pattern_irq_foreground_release(void)
{ fm1_memory_publish_sync(); br_pattern_irq_foreground_busy = 0u; }
/* Called after writing every target field to globals, with competing writers
 * gated. Audio switches back to globals and acknowledges in a later block. */
BR_AUDIO_API int br_pattern_irq_globals_ready(void)
{
    if (!br_pattern_irq_gate || br_pattern_irq_return_request) return 0;
    fm1_memory_publish_sync(); br_pattern_irq_return_request = 1u; return 1;
}
static void br_pattern_host_reset_clock(void)
{
    br_pattern_host_due = 0u;
    br_pattern_irq_internal_quarter = br_tempo_quarter_frames();
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (br_clock_experiment.enabled &&
        !fm1_pattern_bar_external_start(&br_pattern_host_clock, br_clock_experiment.phase_q16))
        br_pattern_host_error = 1u;
#endif
}
static void br_pattern_host_clock_shift(uint64_t shift)
{
    if (br_pattern_host_publication && !fm1_pattern_bar_external_shift(&br_pattern_host_clock, shift))
        br_pattern_host_error = 1u;
}
static void br_pattern_host_begin_block(void)
{
    uint32_t p;
    unsigned old;
    const br_pattern_snapshot *next;
    if (!br_pattern_host_publication) return;
    if (br_pattern_irq_return_request) {
        fm1_memory_publish_sync();
        br_pattern_host_snapshot = 0;
        if (!br_pattern_irq_retire(br_pattern_host_publication, br_pattern_host_retired)) {
            br_pattern_host_error = 1u; return;
        }
        br_pattern_host_retired = BR_PATTERN_IRQ_NO_SLOT;
        br_pattern_irq_return_request = 0u;
        fm1_memory_publish_sync(); br_pattern_irq_gate = 0u;
    }
    if (br_pattern_host_error || !br_playing || !br_pattern_host_due) return;
    if (br_pattern_irq_foreground_busy) { ++br_pattern_irq_late_blocks; return; }
    if (br_pattern_host_retired != BR_PATTERN_IRQ_NO_SLOT) { br_pattern_host_error = 1u; return; }
    br_pattern_host_due = 0u; ++br_pattern_host_bars;
    next = br_pattern_irq_boundary(br_pattern_host_publication, &old);
    if (!next) { br_pattern_host_error = 1u; return; }
    if (old == BR_PATTERN_IRQ_NO_SLOT) return;
    br_pattern_irq_gate = 1u; fm1_memory_publish_sync();
    br_pattern_host_snapshot = next; br_pattern_host_retired = old;
    /* Restart all four track grids together. Old pending micro/retrigger
     * references must never be reinterpreted through the new snapshot.
     * Sounding voices and their gate deadlines remain in force; a matching
     * tied first note uses the ordinary sustain path. First-step negative
     * microtiming is clamped by the existing !br_seq_started rule. */
    memset(br_retrig, 0, sizeof br_retrig);
    for (p = 0; p < BR_PARTS; ++p) {
        memset(br_micro_event[p], 0, sizeof br_micro_event[p]);
        br_seq_frames[p] = br_seq_next[p] = br_seq_started[p] = 0u;
        br_loop_on[p] = 0u;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        if (br_clock_experiment.enabled)
            fm1_mcs_start(&br_clock_experiment.track[p], br_clock_experiment.phase_q16);
#endif
    }
    memset(br_seq_cycle, 0, sizeof br_seq_cycle);
    memset(br_seq_prev_condition_valid, 0, sizeof br_seq_prev_condition_valid);
    memset(br_seq_prev_condition_result, 0, sizeof br_seq_prev_condition_result);
}
/* Keep position continuous in musical beats when local tempo changes. The
 * legacy elapsed counter is in frames, so scale its fraction before using the
 * new quarter duration rather than skipping beats on a large tempo jump. */
static void br_pattern_irq_normalize_internal(void)
{
    uint32_t quarter;
    if (!br_pattern_host_publication || !br_playing) return;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (br_midi_clock_enabled || br_clock_experiment.enabled) return;
#endif
    quarter = br_tempo_quarter_frames();
    if (br_pattern_irq_internal_quarter && quarter != br_pattern_irq_internal_quarter) {
        uint64_t phase = (uint64_t)br_metro_frames * quarter;
        br_metro_frames = (uint32_t)(phase / br_pattern_irq_internal_quarter);
        if (br_metro_frames >= quarter) br_metro_frames = quarter - 1u;
    }
    br_pattern_irq_internal_quarter = quarter;
}
static void br_pattern_irq_check_external_gap(void)
{
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    int result;
    if (!br_pattern_host_publication || !br_playing || br_pattern_host_error ||
        !br_clock_experiment.enabled || !br_clock_experiment.period_q16) return;
    result = fm1_pattern_bar_external_gap(&br_pattern_host_clock,
        br_clock_experiment.phase_q16, br_clock_experiment.phase_end_q16);
    if (result < 0 || (result && br_pattern_host_due)) br_pattern_host_error = 1u;
    else if (result) br_pattern_host_due = 1u;
#endif
}
static void br_pattern_host_end_block(uint32_t n)
{
    int result;
    if (!br_pattern_host_publication || !br_playing || br_pattern_host_error) return;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (br_clock_experiment.enabled && br_clock_experiment.period_q16)
        result = fm1_pattern_bar_external(&br_pattern_host_clock,
            br_clock_experiment.phase_q16, br_clock_experiment.phase_end_q16);
    else if (br_midi_clock_enabled) return;
    else
#endif
        result = fm1_pattern_bar_internal(br_global_beat, br_metro_frames,
                                         br_pattern_irq_internal_quarter, n);
    if (result < 0) br_pattern_host_error = 1u;
    else if (result && br_pattern_host_due) br_pattern_host_error = 1u;
    else if (result) br_pattern_host_due = 1u;
}
#endif

static void br_seq_step(uint32_t p)
{
    uint32_t length = br_bound(BR_PLAY_FIELD(p, length), BR_STEPS), pos = br_seq_next[p];
    uint32_t duration, base_duration, next, unit, lanes, loop_next = 0xffffffffu;
    if (!length) length = 1u;
    if (pos >= length) pos = 0u;
    base_duration = br_step_base_duration(p);
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (br_clock_experiment.enabled) {
        uint64_t base_q16 = br_clock_experiment.period_q16 * 24u >>
            (1u + br_bound(BR_PLAY_FIELD(p, division), 2u));
        base_duration = (uint32_t)(base_q16 >> 16);
        if (!base_duration) base_duration = 1u;
    }
#endif
    if (br_punch_effect == 1u && br_playing) {
        uint32_t span = ((4u * br_tempo_quarter_frames()) >> (br_punch_loop_len & 3u)) / base_duration;
        if (span < 1u) span = 1u;
        if (span > length) span = length;
        if (!br_loop_on[p]) {          /* the step in flight is the loop's first */
            br_loop_on[p] = 1u;
            br_loop_start[p] = (uint8_t)(br_step_pos[p] % length);
            br_loop_i[p] = (uint8_t)(1u % span);
            br_loop_shadow[p] = (uint8_t)pos;
        }
        if (br_loop_i[p] >= span) br_loop_i[p] %= span;
        pos = (br_loop_start[p] + br_loop_i[p]) % length;
        br_loop_i[p] = (uint8_t)((br_loop_i[p] + 1u) % span);
        loop_next = (br_loop_start[p] + br_loop_i[p]) % length;
        br_loop_shadow[p] = (uint8_t)((br_loop_shadow[p] + 1u) % length);
    } else if (br_loop_on[p]) {
        br_loop_on[p] = 0u;
        pos = br_loop_shadow[p] % length;
    }
    duration = br_step_duration_from_base(p, pos, base_duration);
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (br_clock_experiment.enabled && br_clock_experiment.period_q16) {
        uint64_t phase = br_clock_experiment.phase_q16 +
            fm1_clock_div64((uint64_t)br_clock_experiment.block_offset << 32, br_clock_experiment.period_q16, 0);
        uint32_t planned = fm1_mcs_next(&br_clock_experiment.track[p], phase,
            br_clock_experiment.period_q16, br_bound(BR_PLAY_FIELD(p, division), 2u),
            br_bound(br_part[0].swing, BR_SWING_MAX), pos);
        if (!planned) { br_clock_experiment.invalid = 1u; br_playing = 0u; }
        else duration = planned;
    }
#endif
    unit = base_duration / (16u >> br_bound(BR_PLAY_FIELD(p, division), 2u));
    if (!unit) unit = 1u;
    next = loop_next != 0xffffffffu ? loop_next : pos + 1u < length ? pos + 1u : 0u;
    lanes = br_is_kit(br_cached_sound[p].preset) ? BR_DRUM_LANES : 1u;
    br_step_pos[p] = pos;
    br_seq_current_duration[p] = duration; br_seq_micro_unit[p] = unit;
    for (uint32_t lane = 0u; lane < lanes; ++lane) {
        uint32_t word = BR_PLAY_STEP(p, pos), next_word = BR_PLAY_STEP(p, next);
        uint32_t active = lanes == 1u ? (word & BR_STEP_ON) : ((BR_STEP_HIT(word) >> lane) & 1u);
        uint32_t next_active = lanes == 1u ? (next_word & BR_STEP_ON) : ((BR_STEP_HIT(next_word) >> lane) & 1u);
        uint32_t row = lanes == 1u ? p : BR_SYNTH_PARTS + lane;
        uint32_t cycle = br_seq_cycle[row];
        int32_t micro = br_play_micro(p, pos, lane);
        int32_t next_micro = br_play_micro(p, next, lane);
        if (active && (micro >= 0 || !br_seq_started[p])) {
            uint32_t delay = micro > 0 ? (uint32_t)micro * unit : 0u;
            if (delay >= duration) delay = duration - 1u;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
            br_micro_schedule_timed(p, pos, lane, delay, cycle,
                br_clock_experiment.enabled ? base_duration : 0u);
#else
            br_micro_schedule(p, pos, lane, delay, cycle);
#endif
        }
        if (next_active && next_micro < 0) {
            uint32_t early = (uint32_t)(-next_micro) * unit;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
            br_micro_schedule_timed(p, next, lane, early < duration ? duration - early : 0u,
                cycle + (next == 0u), br_clock_experiment.enabled ? base_duration : 0u);
#else
            br_micro_schedule(p, next, lane, early < duration ? duration - early : 0u,
                              cycle + (next == 0u));
#endif
        }
    }
    if (lanes == 1u && !(BR_PLAY_STEP(p, pos) & BR_STEP_ON)) br_seq_tie[p] = 0u;
    br_seq_started[p] = 1u;
    br_seq_next[p] = (uint8_t)next;
    if (!next) {
        if (p == BR_DRUM_PART) {
            for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
                ++br_seq_cycle[BR_SYNTH_PARTS + lane];
        } else ++br_seq_cycle[p];
    }
    br_seq_frames[p] = duration;
}

BR_AUDIO_API uint32_t br_engine_record_clock(uint32_t p)
{
    return p < BR_PARTS ? br_seq_clock[p] : 0u;
}

BR_AUDIO_API void br_engine_record_position(uint32_t part, uint32_t *step,
                                                   uint32_t *elapsed, uint32_t *duration,
                                                   uint32_t *unit)
{
    uint32_t before, after, tries = 0u;
    br_record_position_t *pos;
    if (part >= BR_PARTS) { *step = *elapsed = 0u; *duration = *unit = 1u; return; }
    if (br_playing && !br_transport_prev) {
        uint32_t base = br_step_base_duration(part);
        uint32_t division = br_bound(BR_PLAY_FIELD(part, division), 2u);
        *step = *elapsed = 0u;
        *duration = br_step_duration_from_base(part, 0u, base);
        *unit = base / (16u >> division);
        if (!*unit) *unit = 1u;
        return;
    }
    pos = &br_record_position[part];
    do {
        before = pos->revision; RING_PUBLISH();
        *step = pos->step; *elapsed = pos->elapsed; *duration = pos->duration; *unit = pos->unit;
        RING_PUBLISH(); after = pos->revision;
    } while ((before != after || (before & 1u)) && ++tries < 4u);
    if (!*duration) {
        uint32_t base = br_step_base_duration(part);
        uint32_t division = br_bound(BR_PLAY_FIELD(part, division), 2u);
        *step = br_step_pos[part]; *elapsed = 0u;
        *duration = br_step_duration_from_base(part, *step, base);
        *unit = base / (16u >> division);
    }
    if (!*unit) *unit = 1u;
    if (*elapsed > *duration) *elapsed = *duration;
}

static void br_seq_process(uint32_t p, uint32_t n)
{
    uint32_t remaining = n;
    if (!remaining) return;
    if (!br_playing && br_midi_paused) return;
    if (!br_playing) {
        br_loop_on[p] = 0u;
        if (br_seq_note[p] != 255u) br_seq_note_expire(p);
        br_seq_frames[p] = 0u;
        br_seq_gate_frames[p] = 0u;
        br_seq_started[p] = 0u;
        memset(br_micro_event[p], 0, sizeof br_micro_event[p]);
        if (p == BR_DRUM_PART) memset(br_retrig, 0, sizeof br_retrig);
        return;
    }
    /* Sequencing runs before source rendering. A very short gate that lands
     * inside this block must survive until this block has rendered once. */
    if (br_seq_deferred_off[p])
        br_seq_note_expire(p);
    while (remaining) {
        uint32_t span;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        br_clock_experiment.block_offset = n - remaining;
#endif
        if (!br_seq_frames[p]) br_seq_step(p);
        br_micro_due(p);
        if (p == BR_DRUM_PART) {
            for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane) {
                br_retrig_t *r = &br_retrig[lane];
                if (r->count &&
                    (BR_PLAY_META(kit_retrig, r->step, lane) != r->setting ||
                     !(BR_STEP_HIT(BR_PLAY_STEP(p, r->step)) & (1u << lane))))
                    memset(r, 0, sizeof *r);
                if (r->count && !r->remaining) {
                    r->pending = 1u;
                    --r->count;
                    r->remaining = r->interval;
                }
            }
        }
        /* A tied note may meet its deadline exactly on the next step edge.
         * The new step gets first chance to extend it; an empty/failed step
         * then expires it immediately at that same edge. */
        if (!br_seq_gate_frames[p] && br_seq_note[p] != 255u &&
            !br_seq_deferred_off[p])
            br_seq_note_expire(p);
        span = remaining < br_seq_frames[p] ? remaining : br_seq_frames[p];
        if (br_seq_gate_frames[p] && br_seq_gate_frames[p] < span)
            span = br_seq_gate_frames[p];
        for (uint32_t i = 0u; i < BR_SEQ_EVENT_SLOTS; ++i)
            if (br_micro_event[p][i].active && br_micro_event[p][i].remaining < span)
                span = br_micro_event[p][i].remaining;
        if (p == BR_DRUM_PART)
            for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
                if (br_retrig[lane].count && br_retrig[lane].remaining < span)
                    span = br_retrig[lane].remaining;
        if (!span) {
            if (br_seq_gate_frames[p] == 0u && br_seq_note[p] != 255u)
                br_seq_note_expire(p);
            else br_seq_step(p);
            continue;
        }
        br_seq_frames[p] -= span;
        for (uint32_t i = 0u; i < BR_SEQ_EVENT_SLOTS; ++i)
            if (br_micro_event[p][i].active) br_micro_event[p][i].remaining -= span;
        if (p == BR_DRUM_PART)
            for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
                if (br_retrig[lane].count) br_retrig[lane].remaining -= span;
        if (br_seq_gate_frames[p]) br_seq_gate_frames[p] -= span;
        remaining -= span;
        if (!br_seq_gate_frames[p] && br_seq_note[p] != 255u &&
            br_seq_frames[p] != 0u) br_seq_deferred_off[p] = 1u;
    }
    if (p == BR_DRUM_PART)
        for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            br_retrig_t *r = &br_retrig[lane];
            if (r->pending) {
                r->pending = 0u;
                br_seq_lane_gate_frames[p][lane] = r->gate;
                br_start_sequence_kit_voice(p, lane, r->voice, r->velocity, r->pitch);
            }
        }
    br_record_position[p].revision++; RING_PUBLISH();
    br_record_position[p].step = br_step_pos[p];
    br_record_position[p].duration = br_seq_current_duration[p];
    br_record_position[p].elapsed = br_seq_current_duration[p] - br_seq_frames[p];
    br_record_position[p].unit = br_seq_micro_unit[p];
    RING_PUBLISH(); br_record_position[p].revision++;
}

static void br_env_advance(br_env_t *e)
{
    if (e->stage == BR_ENV_ATTACK) {
        e->amp += e->attack_step;
        if (e->amp >= 32767u) { e->amp = 32767u; e->stage = BR_ENV_DECAY; }
    } else if (e->stage == BR_ENV_DECAY) {
        if (e->amp <= e->sustain) { e->amp = e->sustain; e->stage = BR_ENV_SUSTAIN; }
        else {
            e->amp = e->amp > e->decay_step ? e->amp - e->decay_step : e->sustain;
            if (e->amp <= e->sustain) { e->amp = e->sustain; e->stage = BR_ENV_SUSTAIN; }
        }
    } else if (e->stage == BR_ENV_RELEASE) {
        e->amp = e->amp > e->release_step ? e->amp - e->release_step : 0;
        if (!e->amp) e->stage = BR_ENV_OFF;
    }
}

static inline uint32_t br_env_advance_velocity_gain(br_env_t *e)
{
    if (e->velocity_gain_ramp_left) {
        if (e->velocity_gain_ramp_left == 1u)
            e->velocity_gain_q15 = e->velocity_gain_target_q15;
        else
            e->velocity_gain_q15 = (uint32_t)(
                (int32_t)e->velocity_gain_q15 + e->velocity_gain_step);
        --e->velocity_gain_ramp_left;
    }
    return e->velocity_gain_q15;
}

/* This path runs only while a pitched velocity change is slewing. Keeping it
 * out of the two source-mode branches avoids duplicating its code in XIP. */
static __attribute__((noinline)) int32_t br_env_render_velocity_ramp(
    br_env_t *e, int32_t *row, uint32_t count, uint32_t level,
    uint32_t calibration_q6)
{
    uint32_t i;
    int32_t osc_sum = 0;
#if BR_AUDIO_POST_GAIN_Q15_APPROX
    uint32_t level_q15 = level == 127u ? 32768u :
        br_scale_7bit_q15_exact(level);
    if (calibration_q6 != 64u)
        level_q15 = (level_q15 * calibration_q6 + 32u) >> 6;
#else
    uint32_t level_gain = level * 127u;
    if (calibration_q6 != 64u)
        level_gain = (level_gain * calibration_q6 + 32u) >> 6;
#endif
    for (i = 0; i < count; ++i) {
        int32_t sample = row[i];
        int32_t envelope_sample;
        uint32_t velocity_gain_q15;
#if BR_AUDIO_POST_GAIN_Q15_APPROX
        uint32_t ramp_gain_q15;
#else
        uint32_t ramp_gain;
#endif
        br_env_advance(e);
        velocity_gain_q15 = br_env_advance_velocity_gain(e);
#if BR_AUDIO_POST_GAIN_Q15_APPROX
        ramp_gain_q15 = br_filter_mul_q15(
            (int32_t)velocity_gain_q15, level_q15);
#endif
        envelope_sample = (sample * (int32_t)e->amp) >> 15;
#if BR_AUDIO_POST_GAIN_Q15_APPROX
        row[i] = ramp_gain_q15 == 32768u ? envelope_sample :
            br_filter_mul_q15(envelope_sample, ramp_gain_q15);
#else
        ramp_gain = (velocity_gain_q15 * level_gain + 16384u) >> 15;
        row[i] = br_div16129_exact(envelope_sample * (int32_t)ramp_gain);
#endif
        osc_sum += sample;
    }
    return osc_sum;
}

static void br_env_refresh(br_env_t *e, const br_sound_t *s, uint32_t keep_amp)
{
#if BR_AUDIO_ENV_CURVE_CONTROL_KEY
    uint32_t curve_key = br_env_curve_control_key(s);
    if (curve_key != e->level) {
        uint32_t attack = br_time_samples(s->attack, 3u);
        uint32_t decay = br_time_samples(s->decay, 8u);
        uint32_t release = br_time_samples(s->release, 8u);
        uint32_t sustain = br_q15(s->sustain);
        /* Live modulation still takes effect on this block. Stable
         * parameters do not need three duration calculations per block. */
        if (e->attack != attack) {
            e->attack = attack;
            e->attack_step = (32767u - (keep_amp ? e->amp : 0u) + attack - 1u) / attack;
        }
        if (e->decay != decay || e->sustain != sustain) {
            e->decay = decay;
            e->sustain = sustain;
            e->decay_step = (32767u - sustain + decay - 1u) / decay;
        }
        if (e->release != release || e->release_step_start != e->release_start) {
            e->release = release;
            e->release_step_start = e->release_start;
            e->release_step = (e->release_start + release - 1u) / release;
        }
        e->level = curve_key;
    } else if (e->release_step_start != e->release_start) {
        /* Release can be initiated without changing a live curve control. */
        e->release_step_start = e->release_start;
        e->release_step = (e->release_start + e->release - 1u) / e->release;
    }
#else
    uint32_t attack = br_time_samples(s->attack, 3u);
    uint32_t decay = br_time_samples(s->decay, 8u);
    uint32_t release = br_time_samples(s->release, 8u);
    uint32_t sustain = br_q15(s->sustain);
    /* Live modulation still takes effect on this block. Stable parameters do
     * not need three duration calculations on every source block. */
    if (e->attack != attack) {
        e->attack = attack;
        e->attack_step = (32767u - (keep_amp ? e->amp : 0u) + attack - 1u) / attack;
    }
    if (e->decay != decay || e->sustain != sustain) {
        e->decay = decay;
        e->sustain = sustain;
        e->decay_step = (32767u - sustain + decay - 1u) / decay;
    }
    if (e->release != release || e->release_step_start != e->release_start) {
        e->release = release;
        e->release_step_start = e->release_start;
        e->release_step = (e->release_start + release - 1u) / release;
    }
#endif
}

BR_AUDIO_API __attribute__((minsize, noinline)) void br_engine_init(void)
{
    uint32_t p, i;
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    br_audio_fixed8_foreground_invalidate();
#endif
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    PLAITS_STATE_ABI_GUARD();
#endif
    br_event_read = br_event_write = br_dropped_events = br_panic_parts = 0;
    memset(br_live_count, 0, sizeof br_live_count);
    br_deferred_start_reset();
    br_arp_reset();
    br_ready = 0;
    br_audio_source_fx_fusion_active = 0u;
#ifdef BR_SPLIT_AUDIO
    br_engine_pipeline_reset();
#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR && \
    defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    memset(br_audio_plaits_prev_append_profile, 0,
           sizeof br_audio_plaits_prev_append_profile);
    memset(br_audio_plaits_current_append_profile, 0,
           sizeof br_audio_plaits_current_append_profile);
    memset(br_audio_plaits_prev_cost_signature, 0,
           sizeof br_audio_plaits_prev_cost_signature);
    memset(br_audio_plaits_current_cost_signature, 0,
           sizeof br_audio_plaits_current_cost_signature);
    memset(br_audio_plaits_prev_cost_valid, 0,
           sizeof br_audio_plaits_prev_cost_valid);
    memset(br_audio_plaits_current_cost_valid, 0,
           sizeof br_audio_plaits_current_cost_valid);
#endif
#endif
    br_transport_prev = 0;
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    memset(&br_clock_experiment, 0, sizeof br_clock_experiment);
    br_midi_clock_active = br_clock_source_prev = 0u;
    ++br_midi_clock_generation;
    br_clock_generation_seen = br_midi_clock_generation;
#endif
    br_midi_paused = br_midi_transport_overflow = 0u;
    memset(br_midi_paused_velocity, 0, sizeof br_midi_paused_velocity);
    memset(br_retrig, 0, sizeof br_retrig);
    br_metro_frames = 0u;
    br_global_beat = 0u;
    br_transport_phase = 0u;
    br_playing = 0;
    for (p = 0; p < BR_PARTS; ++p) {
        br_mode[p] = 0;
        br_active_preset[p] = 255u;
        br_env[p].stage = BR_ENV_OFF;
        br_env[p].amp = br_env[p].release_start = 0;
        br_env[p].velocity = br_env[p].note = 0;
        br_env[p].velocity_gain_q15 = br_env[p].velocity_gain_target_q15 = 0u;
        br_env[p].velocity_gain_ramp_left = 0u;
        br_env[p].velocity_gain_step = 0;
        br_env[p].level = br_env[p].attack = br_env[p].decay = 0;
        br_env[p].release = br_env[p].sustain = 0;
        br_filter_init(&br_filter[p]);
        br_mod_init(&br_mod_state[p], 0x4D4F4400u + p);
#if BR_MOD_LFO_PHASE_DELTA_CACHE
        br_mod_phase_delta_cache_reset(p);
#endif
        br_active_raw_note[p] = br_active_dsp_note[p] = 255u;
        br_seq_frames[p] = br_seq_gate_frames[p] = 0;
        br_seq_note[p] = 255u; br_seq_tie[p] = 0; br_seq_next[p] = 0;
        br_seq_deferred_off[p] = 0u;
        br_seq_clock[p] = 0u;
        br_seq_started[p] = 0u;
        memset(br_micro_event[p], 0, sizeof br_micro_event[p]);
        memset(&br_record_position[p], 0, sizeof br_record_position[p]);
        br_seq_current_duration[p] = br_seq_micro_unit[p] = 0u;
        for (i = 0u; i < BR_DRUM_LANES; ++i)
            br_seq_lane_gate_frames[p][i] = 0u;
        br_step_pos[p] = 0;
        br_cached_revision[p] = 0xFFFFFFFFu;
        br_cached_mod_inactive[p] = 0u;
        br_effective_valid[p] = 0u;
        br_send_ready[p] = 0u;
        for (i = 0; i < BR_DRUM_LANES; ++i) {
            br_cached_lane[p][i] = 127u;
            br_cached_lane_gate[p][i] = 128u;
        }
        br_preset_sound(br_part[p].sound.preset, &br_cached_sound[p]);
        br_snapshot(p);
        br_select_mode(p);
        br_build_effective(p);
        br_quiet_part[p] = 1u;
    }
    for (i = 0u; i < BR_SEQ_TRACKS; ++i) {
        br_seq_cycle[i] = 0u;
        br_seq_rng[i] = 0x4D595DF4u ^ (i * 0x9E3779B9u);
        br_seq_prev_condition_valid[i] = 0u;
        br_seq_prev_condition_result[i] = 0u;
    }
    br_snapshot_source_compressor();
    br_ready = 1u;
}

/* Source work is isolated from track AMP, filter, Gate and quiet processing.
 * Events and controls are prepared on CPU0 before any source jobs run. */
static void br_engine_source_part(uint32_t p, int32_t *output,
                                  int32_t *key, uint32_t n, uint32_t lane_mask)
{
    uint32_t i;
#if defined(FM1_DEVICE)
    uint32_t diagnostic_slot = br_audio_kit_job_slot(lane_mask);
#endif
    int16_t samples[BR_BLOCK];
    br_sound_t *s = &br_effective_sound[p];
    br_env_t *e = &br_env[p];
    /* The Plaits kit C API clears its output/key buffers before rendering.
     * Avoid clearing the same output a second time on this internal path. */
    if (br_mode[p] != 4u)
        for (i = 0; i < n; ++i) output[i] = 0;
 if (br_mode[p] == 1u || br_mode[p] == 4u) {
        const br_kit_source_controls_t *controls = &br_audio_kit_source_controls[p];
#if defined(FM1_DEVICE)
        uint32_t synth_start = fm1_ticks();
#endif
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        if (br_mode[p] == 4u) {
            uint32_t offset;
#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR && defined(BR_SPLIT_AUDIO)
            const uint32_t slot = br_audio_kit_job_slot(lane_mask);
            uint8_t append_by_lane[BR_DRUM_LANES] = {0u, 0u, 0u, 0u};
            uint32_t append_units = 0u;
            for (offset = 0u; offset < n; offset += BR_AUDIO_NATIVE_RENDER_MAX) {
                uint32_t count = n - offset < BR_AUDIO_NATIVE_RENDER_MAX ?
                    n - offset : BR_AUDIO_NATIVE_RENDER_MAX;
                uint8_t segment_append[BR_DRUM_LANES];
                uint32_t lane;
                append_units += plaits_kit_render_keyed_mask_counted_by_lane(
                    br_plaits_kit_job_state(p, lane_mask), s, controls->level,
                    controls->gate_frames, lane_mask, output + offset,
                    key ? key + offset : 0, count, segment_append);
                for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
                    uint32_t total = (uint32_t)append_by_lane[lane] +
                                     segment_append[lane];
                    append_by_lane[lane] = (uint8_t)(total > 255u ? 255u : total);
                }
            }
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
            uint32_t first_lane = slot;
            uint32_t local_count = 1u;
#else
            uint32_t first_lane = slot ? 2u : 0u;
            uint32_t local_count = 2u;
#endif
            uint32_t local;
            uint8_t profile = br_audio_plaits_current_append_profile[p][slot];
            (void)append_units;
            for (local = 0u; local < local_count; ++local) {
                uint32_t shift = local * 2u;
                uint32_t prior = (profile >> shift) & 3u;
                uint32_t total = prior + append_by_lane[first_lane + local];
                if (total > 2u) total = 2u;
                profile = (uint8_t)((profile & ~(3u << shift)) |
                                    (total << shift));
            }
            br_audio_plaits_current_append_profile[p][slot] = profile;
#else
            for (offset = 0u; offset < n; offset += BR_AUDIO_NATIVE_RENDER_MAX) {
                uint32_t count = n - offset < BR_AUDIO_NATIVE_RENDER_MAX ?
                    n - offset : BR_AUDIO_NATIVE_RENDER_MAX;
                plaits_kit_render_keyed_mask(
                    br_plaits_kit_job_state(p, lane_mask), s, controls->level,
                    controls->gate_frames, lane_mask, output + offset,
                    key ? key + offset : 0, count);
            }
#endif
        }
        else
#endif
            if (s->preset == BR_KIT_808 || s->preset == BR_KIT_909)
                br_drum_render_keyed_mask_q13(br_drum_pair_state(p, lane_mask), s,
                    controls->level, controls->gate_frames, lane_mask, output, key, n);
            else
                br_drum_render_keyed_mask(br_drum_pair_state(p, lane_mask), s,
                    controls->level, controls->gate_frames, lane_mask, output, key, n);
#if defined(FM1_DEVICE)
        br_diagnostic_part_synth_ticks[p][diagnostic_slot] += fm1_ticks() - synth_start;
#endif
    }
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    else {
        uint32_t pitch = br_map_pitch_q7(p, e->note);
        if (pitch > 16383u) pitch = 16383u;
        if (e->stage != BR_ENV_OFF || !plaits_quiet(br_dsp[p].plaits)) {
#if defined(FM1_DEVICE)
            uint32_t synth_start = fm1_ticks();
#endif
            plaits_render(br_dsp[p].plaits, s->preset - BR_FIRST_PLAITS, pitch,
                (uint16_t)(br_scale_7bit_q15_exact(br_bound(s->timbre, 127u))),
                (uint16_t)(br_scale_7bit_q15_exact(br_bound(s->color, 127u))),
                (uint16_t)(br_scale_7bit_q15_exact(br_bound(s->plaits_harmonics, 127u))),
                (uint16_t)(br_scale_7bit_q15_exact(br_bound(s->plaits_aux, 127u))),
                samples, n);
#if defined(FM1_DEVICE)
        br_diagnostic_part_synth_ticks[p][diagnostic_slot] += fm1_ticks() - synth_start;
#endif
            if (e->stage != BR_ENV_OFF)
                for (i = 0; i < n; ++i) output[i] = samples[i];
        }
    }
#endif
}

#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR
static uint8_t br_audio_plaits_pack_units(const uint8_t lane_units[BR_DRUM_LANES],
                                         uint32_t slot)
{
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
    return (uint8_t)(lane_units[slot] & 3u);
#else
    const uint32_t first = slot ? 2u : 0u;
    return (uint8_t)((lane_units[first] & 3u) |
                     ((lane_units[first + 1u] & 3u) << 2));
#endif
}

static uint32_t br_audio_plaits_profile_multiplier(uint8_t previous,
                                                   uint8_t next,
                                                   uint32_t eligible)
{
    uint32_t multiplier = 1u;
    uint32_t local;
    if (!eligible) return 1u;
    for (local = 0u; local < (BR_AUDIO_FUSION_FOUR_WAY_KIT ? 1u : 2u); ++local) {
        uint32_t shift = local * 2u;
        uint32_t before = (previous >> shift) & 3u;
        uint32_t after = (next >> shift) & 3u;
        uint32_t ratio;
        if (before > 2u || after > 2u || after <= before) continue;
        /* A newly contributing lane has no matching prior timing sample. */
        if (!before) return 1u;
        ratio = (after + before - 1u) / before;
        if (ratio > multiplier) multiplier = ratio;
    }
    return multiplier;
}

static uint32_t br_audio_plaits_cost_for_profiles(uint32_t cost,
                                                  uint8_t previous,
                                                  uint8_t next,
                                                  uint32_t eligible)
{
    const uint32_t multiplier =
        br_audio_plaits_profile_multiplier(previous, next, eligible);
    if (multiplier <= 1u) return cost;
    return cost > UINT32_MAX / multiplier ? UINT32_MAX : cost * multiplier;
}
#endif

#ifdef BR_SPLIT_AUDIO
static int32_t br_engine_post_part_row(uint32_t p, int32_t *row,
                                       int32_t *key, uint32_t n);

#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
static void br_audio_failure_profile_stage_begin(br_audio_source_job_t *job,
                                                 uint32_t stage,
                                                 uint32_t started)
{
    job->failure_profile_stage_start_tick = started;
    job->failure_profile_stage_flags &= ~BR_AUDIO_FAILURE_STAGE_ACTIVE_MASK;
    job->failure_profile_stage_flags |= BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID |
        ((stage << BR_AUDIO_FAILURE_STAGE_ACTIVE_SHIFT) &
         BR_AUDIO_FAILURE_STAGE_ACTIVE_MASK);
}

static void br_audio_failure_profile_stage_end(br_audio_source_job_t *job,
                                               uint32_t stage,
                                               uint32_t done_flag,
                                               uint32_t ended)
{
    uint32_t active = (job->failure_profile_stage_flags &
                       BR_AUDIO_FAILURE_STAGE_ACTIVE_MASK) >>
                      BR_AUDIO_FAILURE_STAGE_ACTIVE_SHIFT;
    if (!(job->failure_profile_stage_flags &
          BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID) || active != stage) return;
    switch (stage) {
    case BR_AUDIO_FAILURE_STAGE_ID_SOURCE:
        job->failure_profile_source_ticks =
            ended - job->failure_profile_stage_start_tick;
        break;
    case BR_AUDIO_FAILURE_STAGE_ID_MERGE:
        job->failure_profile_merge_ticks =
            ended - job->failure_profile_stage_start_tick;
        break;
    case BR_AUDIO_FAILURE_STAGE_ID_POST:
        job->failure_profile_post_ticks =
            ended - job->failure_profile_stage_start_tick;
        break;
    case BR_AUDIO_FAILURE_STAGE_ID_FX:
        job->failure_profile_fx_ticks =
            ended - job->failure_profile_stage_start_tick;
        break;
    default:
        return;
    }
    job->failure_profile_stage_flags |= done_flag;
    job->failure_profile_stage_flags &=
        ~(BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID |
          BR_AUDIO_FAILURE_STAGE_ACTIVE_MASK);
}
#endif

uint32_t br_audio_worker_sources(void *opaque_job)
{
    br_audio_source_job_t *job = (br_audio_source_job_t *)opaque_job;
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG || BR_AUDIO_FUSION_EWMA_SCHEDULER
    uint32_t timing_core = 0u, timing_started = 0u;
#endif
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    uint32_t timing_finished = 0u;
#endif
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
    uint32_t stage_started = 0u;
#endif
    if (!job || !job->output || job->part >= BR_PARTS || job->frames > BR_BLOCK) return 0u;
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
    job->failure_profile_start_tick = 0u;
    job->failure_profile_flags = 0u;
#endif
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    job->failure_profile_stage_flags = 0u;
    job->failure_profile_stage_start_tick = 0u;
    job->failure_profile_source_ticks = 0u;
    job->failure_profile_merge_ticks = 0u;
    job->failure_profile_post_ticks = 0u;
    job->failure_profile_fx_ticks = 0u;
#endif
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG || BR_AUDIO_FUSION_EWMA_SCHEDULER
    if (br_audio_source_fx_fusion_active
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG
        && br_audio_job_timing_collect
#endif
        ) {
        timing_core = br_audio_job_timing_core();
        timing_started = br_audio_job_timing_now();
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
        job->failure_profile_start_tick = timing_started;
        job->failure_profile_flags |= BR_AUDIO_FAILURE_JOB_START_VALID;
        job->executor_core = timing_core;
#endif
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
        stage_started = timing_started;
#endif
    }
#endif
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    if (job->failure_profile_flags & BR_AUDIO_FAILURE_JOB_START_VALID)
        br_audio_failure_profile_stage_begin(
            job,
            job->kind == BR_AUDIO_JOB_KIT_FINALIZE ?
                BR_AUDIO_FAILURE_STAGE_ID_MERGE :
                BR_AUDIO_FAILURE_STAGE_ID_SOURCE,
            timing_started);
#endif
    if (job->kind == BR_AUDIO_JOB_TRACK_FX ||
        job->kind == BR_AUDIO_JOB_SHARED_REVERB) {
        /* Effects: the track's source job has completed, its row is final.
         * Shared reverb: reads only the previous block's sends. */
        if (job->kind == BR_AUDIO_JOB_TRACK_FX)
            br_audio_call_fx_fusion_process_track(job->part, job->output,
                                                  job->frames);
        else br_audio_call_fx_shared_reverb(job->frames);
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG
        br_audio_job_timing_job_end(job, timing_core, timing_started);
#elif BR_AUDIO_FUSION_EWMA_SCHEDULER
        if (br_audio_source_fx_fusion_active) {
            job->duration_ticks = br_audio_job_timing_now() - timing_started;
            job->executor_core = timing_core;
        }
#endif
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
        job->failure_profile_flags |= BR_AUDIO_FAILURE_JOB_CALLBACK_RETURNED;
#endif
        return 1u;
    }
    if (job->kind == BR_AUDIO_JOB_FUSED_TRACK ||
        job->kind == BR_AUDIO_JOB_TRACK_SOURCE) {
        br_engine_source_part(job->part, job->output, job->key, job->frames,
                              job->lane_mask ? job->lane_mask : BR_DRUM_ALL_MASK);
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        if (job->failure_profile_stage_flags &
            BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID) {
            timing_finished = br_audio_job_timing_now();
            br_audio_failure_profile_stage_end(
                job, BR_AUDIO_FAILURE_STAGE_ID_SOURCE,
                BR_AUDIO_FAILURE_STAGE_SOURCE_DONE, timing_finished);
            br_audio_failure_profile_stage_begin(
                job, BR_AUDIO_FAILURE_STAGE_ID_POST, timing_finished);
        }
#endif
        br_audio_fused_osc_block[job->part] = br_engine_post_part_row(
            job->part, job->output, br_comp_key[job->part], job->frames);
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        if (job->failure_profile_stage_flags &
            BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID) {
            timing_finished = br_audio_job_timing_now();
            br_audio_failure_profile_stage_end(
                job, BR_AUDIO_FAILURE_STAGE_ID_POST,
                BR_AUDIO_FAILURE_STAGE_POST_DONE, timing_finished);
            br_audio_failure_profile_stage_begin(
                job, BR_AUDIO_FAILURE_STAGE_ID_FX, timing_finished);
        }
#endif
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
        br_audio_job_timing_stage_end(job->part, timing_core, stage_started, 0u);
        stage_started = br_audio_job_timing_collect ?
            br_audio_job_timing_now() : 0u;
#endif
        if (job->kind == BR_AUDIO_JOB_FUSED_TRACK)
            br_audio_call_fx_fusion_process_track(job->part, job->output,
                                                  job->frames);
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        if (job->failure_profile_stage_flags &
            BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID) {
            timing_finished = br_audio_job_timing_now();
            br_audio_failure_profile_stage_end(
                job, BR_AUDIO_FAILURE_STAGE_ID_FX,
                BR_AUDIO_FAILURE_STAGE_FX_DONE, timing_finished);
        }
#endif
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
        br_audio_job_timing_stage_end(job->part, timing_core, stage_started, 1u);
#endif
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        br_audio_job_timing_job_end_at(job, timing_core, timing_started,
                                       timing_finished);
#else
        br_audio_job_timing_job_end(job, timing_core, timing_started);
#endif
#elif BR_AUDIO_FUSION_EWMA_SCHEDULER
        if (br_audio_source_fx_fusion_active) {
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
            job->duration_ticks = timing_finished - timing_started;
#else
            job->duration_ticks = br_audio_job_timing_now() - timing_started;
#endif
            job->executor_core = timing_core;
        }
#endif
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
        job->failure_profile_flags |= BR_AUDIO_FAILURE_JOB_CALLBACK_RETURNED;
#endif
        return 1u;
    }
    if (job->kind == BR_AUDIO_JOB_KIT_FINALIZE) {
        uint32_t i, lane;
        for (i = 0u; i < job->frames; ++i) {
            int32_t sample = 0, key_sample = 0;
            for (lane = 0u; lane < BR_AUDIO_KIT_JOB_LANES; ++lane) {
                int32_t *key = br_audio_kit_key_output(
                    job->part, lane, br_audio_kit_key_lane_mask(lane));
                sample += br_audio_kit_partial[0][lane][i];
                if (key) key_sample += key[i];
            }
            br_comp_key[job->part][i] = key_sample;
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
            if (br_mode[job->part] == 4u) {
                if (sample > 32767) sample = 32767;
                if (sample < -32768) sample = -32768;
            }
#endif
            job->output[i] = sample;
        }
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        if (job->failure_profile_stage_flags &
            BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID) {
            timing_finished = br_audio_job_timing_now();
            br_audio_failure_profile_stage_end(
                job, BR_AUDIO_FAILURE_STAGE_ID_MERGE,
                BR_AUDIO_FAILURE_STAGE_MERGE_DONE, timing_finished);
            br_audio_failure_profile_stage_begin(
                job, BR_AUDIO_FAILURE_STAGE_ID_POST, timing_finished);
        }
#endif
        br_audio_fused_osc_block[job->part] = br_engine_post_part_row(
            job->part, job->output, br_comp_key[job->part], job->frames);
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        if (job->failure_profile_stage_flags &
            BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID) {
            timing_finished = br_audio_job_timing_now();
            br_audio_failure_profile_stage_end(
                job, BR_AUDIO_FAILURE_STAGE_ID_POST,
                BR_AUDIO_FAILURE_STAGE_POST_DONE, timing_finished);
            br_audio_failure_profile_stage_begin(
                job, BR_AUDIO_FAILURE_STAGE_ID_FX, timing_finished);
        }
#endif
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
        br_audio_job_timing_stage_end(job->part, timing_core, stage_started, 0u);
        stage_started = br_audio_job_timing_collect ?
            br_audio_job_timing_now() : 0u;
#endif
        br_audio_call_fx_fusion_process_track(job->part, job->output,
                                              job->frames);
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        if (job->failure_profile_stage_flags &
            BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID) {
            timing_finished = br_audio_job_timing_now();
            br_audio_failure_profile_stage_end(
                job, BR_AUDIO_FAILURE_STAGE_ID_FX,
                BR_AUDIO_FAILURE_STAGE_FX_DONE, timing_finished);
        }
#endif
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
        br_audio_job_timing_stage_end(job->part, timing_core, stage_started, 1u);
#endif
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        br_audio_job_timing_job_end_at(job, timing_core, timing_started,
                                       timing_finished);
#else
        br_audio_job_timing_job_end(job, timing_core, timing_started);
#endif
#elif BR_AUDIO_FUSION_EWMA_SCHEDULER
        if (br_audio_source_fx_fusion_active) {
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
            job->duration_ticks = timing_finished - timing_started;
#else
            job->duration_ticks = br_audio_job_timing_now() - timing_started;
#endif
            job->executor_core = timing_core;
        }
#endif
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
        job->failure_profile_flags |= BR_AUDIO_FAILURE_JOB_CALLBACK_RETURNED;
#endif
        return 1u;
    }
    br_engine_source_part(job->part, job->output, job->key, job->frames,
                          job->lane_mask ? job->lane_mask : BR_DRUM_ALL_MASK);
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    if (job->failure_profile_stage_flags &
        BR_AUDIO_FAILURE_STAGE_ACTIVE_VALID) {
        timing_finished = br_audio_job_timing_now();
        br_audio_failure_profile_stage_end(
            job, BR_AUDIO_FAILURE_STAGE_ID_SOURCE,
            BR_AUDIO_FAILURE_STAGE_SOURCE_DONE, timing_finished);
    }
#endif
#if BR_AUDIO_FUSION_STAGE_TIMING_DIAG
    br_audio_job_timing_stage_end(job->part, timing_core, stage_started, 0u);
#endif
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
    br_audio_job_timing_job_end_at(job, timing_core, timing_started,
                                   timing_finished);
#else
    br_audio_job_timing_job_end(job, timing_core, timing_started);
#endif
#elif BR_AUDIO_FUSION_EWMA_SCHEDULER
    if (br_audio_source_fx_fusion_active) {
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        job->duration_ticks = timing_finished - timing_started;
#else
        job->duration_ticks = br_audio_job_timing_now() - timing_started;
#endif
        job->executor_core = timing_core;
    }
#endif
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
    job->failure_profile_flags |= BR_AUDIO_FAILURE_JOB_CALLBACK_RETURNED;
#endif
    return 1u;
}

#define BR_AUDIO_SOURCE_DRAIN_TICKS \
    BR_AUDIO_HALF_DEADLINE_TICKS

static uint32_t br_audio_source_now(void)
{
#if defined(FM1_DEVICE)
    return fm1_ticks();
#elif defined(BR_SOURCE_QUEUE_TEST)
    extern uint32_t br_audio_source_test_ticks(void);
    return br_audio_source_test_ticks();
#else
    return 0u;
#endif
}

/* Read-only queue telemetry for the private 0x87 failure snapshot. The
 * counters are aligned 32-bit words; callers use it after worker abort/hold.
 * `now` is sampled last so the saved deadline can be compared with it. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS || defined(BR_SOURCE_QUEUE_TEST)
void br_audio_source_diagnostic_snapshot(uint32_t out[4])
{
    if (!out) return;
    out[0] = br_audio_source_queue.count;
    out[1] = br_audio_source_queue.completed;
    out[2] = br_audio_source_queue.deadline_tick;
    out[3] = br_audio_source_now();
}
#endif

#if ((defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)) && \
     (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)) || \
    defined(BR_SOURCE_QUEUE_TEST)
/* Return the CPU0-sealed cost estimates in part-major A/B order. The command
 * caller masks the audio IRQ around this copy, preventing a mixed-batch read.
 * This getter owns no additional storage. */
void br_audio_source_cost_diagnostic_snapshot(uint32_t out[8])
{
    uint32_t p;
    if (!out) return;
    for (p = 0u; p < BR_PARTS; ++p) {
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
        if (br_mode[p] == 1u || br_mode[p] == 4u) {
            out[2u * p] = br_diagnostic_source_cost[p][0] +
                          br_diagnostic_source_cost[p][1];
            out[2u * p + 1u] = br_diagnostic_source_cost[p][2] +
                              br_diagnostic_source_cost[p][3];
        } else
#endif
        {
        out[2u * p] = br_diagnostic_source_cost[p][0];
        out[2u * p + 1u] = br_diagnostic_source_cost[p][1];
        }
    }
}
#endif

#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST) || \
    defined(BR_SOURCE_QUEUE_TEST)
/* Publish the completed source batch's measured per-job costs. Four-way kit
 * queue jobs are lane-specific, so keep each lane's timing in its matching
 * slot; the snapshot API combines lanes into pair totals for diagnostics. */
static void br_audio_publish_source_cost_diagnostics(void)
{
    uint32_t p;
    for (p = 0u; p < BR_PARTS; ++p) {
        uint32_t a = br_diagnostic_part_synth_ticks[p][0];
        uint32_t b = br_diagnostic_part_synth_ticks[p][1];
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
        if (br_mode[p] == 1u || br_mode[p] == 4u) {
            uint32_t lane;
            for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
                br_diagnostic_source_cost[p][lane] =
                    br_diagnostic_part_synth_ticks[p][lane];
        } else {
            br_diagnostic_source_cost[p][0] = a;
            br_diagnostic_source_cost[p][1] = b;
        }
#else
        br_diagnostic_source_cost[p][0] = a;
        br_diagnostic_source_cost[p][1] = b;
#endif
    }
}
#endif

static uint32_t br_audio_source_deadline_expired(void)
{
#if defined(FM1_DEVICE) || defined(BR_SOURCE_QUEUE_TEST)
    return (int32_t)(br_audio_source_now() - br_audio_source_queue.deadline_tick) >= 0;
#else
    return 0u;
#endif
}

/* With an asynchronous queue owner, a worker that finds no ready job can
 * return to its caller: the core still executing the claimed predecessor
 * will rescan after publishing completion, and the caller joins both cores
 * before finalization. This avoids polling the timer and queue lock while no
 * dependency can advance on this core. */
static uint32_t br_audio_source_queue_wait_policy(void)
{
#if BR_AUDIO_FUSION_NONBLOCKING_DRAIN
    if (br_audio_source_fx_fusion_active) return 0u;
#endif
    return 1u;
}

static uint32_t br_audio_source_remaining_ticks(void)
{
#if defined(FM1_DEVICE) || defined(BR_SOURCE_QUEUE_TEST)
    uint32_t now = br_audio_source_now();
    return (int32_t)(br_audio_source_queue.deadline_tick - now) > 0 ?
        br_audio_source_queue.deadline_tick - now : 1u;
#else
    return BR_AUDIO_SOURCE_DRAIN_TICKS;
#endif
}

/* The half cutoff may be absolute (pipeline mode) or relative (legacy mode).
 * Both worker joins must honor the budget left after CPU0 has run its batch. */
static uint32_t br_audio_source_join_timeout_ticks(void)
{
    return br_audio_source_remaining_ticks();
}

static uint32_t br_audio_source_queue_lock(void)
{
    while (!br_audio_source_deadline_expired())
        if (fm1_fault_owner_try_acquire(&br_audio_source_queue.lock)) return 1u;
    return 0u;
}

/* Successful callbacks must be retired even after the soft audio cutoff.
 * This lock never waits past the original hard cutoff and still attempts it
 * once on the first expired observation. A stopped timer is independently
 * bounded; failure leaves the claimed job unpublished and contained. */
#ifndef BR_AUDIO_RETIRE_LOCK_SPINS
#define BR_AUDIO_RETIRE_LOCK_SPINS 0x500000u
#endif
static uint32_t br_audio_source_retire_lock(uint32_t hard_deadline)
{
    uint32_t spins = 0u;
    for (;;) {
        uint32_t expired = (int32_t)(br_audio_source_now() - hard_deadline) >= 0;
#if defined(BR_PUBLICATION_TEST)
        { extern void br_audio_source_test_retire_probe(uint32_t expired);
          br_audio_source_test_retire_probe(expired); }
#endif
        if (fm1_fault_owner_try_acquire(&br_audio_source_queue.lock)) return 1u;
        if (expired || ++spins >= BR_AUDIO_RETIRE_LOCK_SPINS) return 0u;
    }
}

static void br_audio_source_queue_unlock(void)
{
#if defined(FM1_DEVICE)
    fm1_memory_publish_sync();
    br_audio_source_queue.lock = 0u;
    fm1_memory_publish_sync();
#else
    __sync_lock_release(&br_audio_source_queue.lock);
#endif
}

static void br_audio_source_publish(void)
{
#if defined(FM1_DEVICE)
    fm1_memory_publish_sync();
#else
    __sync_synchronize();
#endif
}

/* ID4 receives only this resident queue. Validate the published descriptor
 * graph before it runs FX or source work; a mailbox cannot select another
 * queue, callback, or frame. */
static uint32_t br_audio_job_dependencies_valid(
    const br_audio_source_job_t *job, uint32_t index)
{
    const uint32_t predecessors[4] = {
        job->predecessor, job->predecessor2,
        job->predecessor3, job->predecessor4
    };
    uint32_t i;
    for (i = 0u; i < 4u; ++i)
        if (predecessors[i] != UINT32_MAX && predecessors[i] >= index)
            return 0u;
    if (job->kind != BR_AUDIO_JOB_KIT_FINALIZE &&
        (job->predecessor2 != UINT32_MAX ||
         job->predecessor3 != UINT32_MAX ||
         job->predecessor4 != UINT32_MAX)) return 0u;
    return 1u;
}

static uint32_t br_audio_kit_finalize_graph_valid(
    const br_audio_source_job_t *job)
{
    const uint32_t predecessors[4] = {
        job->predecessor, job->predecessor2,
        job->predecessor3, job->predecessor4
    };
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
    const uint32_t expected = BR_DRUM_LANES;
#else
    const uint32_t expected = 2u;
#endif
    uint32_t i, lanes = 0u;
    for (i = 0u; i < expected; ++i) {
        const br_audio_source_job_t *source;
        uint32_t lane_mask;
        if (predecessors[i] == UINT32_MAX) return 0u;
        source = &br_audio_source_jobs[predecessors[i]];
        lane_mask = source->lane_mask & BR_DRUM_ALL_MASK;
        if (source->kind != BR_AUDIO_JOB_SOURCE ||
            source->part != job->part || source->frames != job->frames ||
            !lane_mask || (lanes & lane_mask)) return 0u;
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
        if (lane_mask != (1u << i)) return 0u;
#else
        if (lane_mask != (i == 0u ? BR_KIT_LANES_A : BR_KIT_LANES_B))
            return 0u;
#endif
        lanes |= lane_mask;
    }
    for (i = expected; i < BR_DRUM_LANES; ++i)
        if (predecessors[i] != UINT32_MAX) return 0u;
    return lanes == BR_DRUM_ALL_MASK;
}

static uint32_t br_audio_job_dependencies_done(
    const br_audio_source_job_t *job, uint32_t count)
{
    const uint32_t predecessors[4] = {
        job->predecessor, job->predecessor2,
        job->predecessor3, job->predecessor4
    };
    uint32_t i;
    for (i = 0u; i < 4u; ++i) {
        uint32_t predecessor = predecessors[i];
        if (predecessor != UINT32_MAX &&
            (predecessor >= count ||
             br_audio_source_jobs[predecessor].state != 2u)) return 0u;
    }
    return 1u;
}

uint32_t br_audio_worker_source_queue_context_valid(void *opaque_queue)
{
    uint32_t i, count;
    if (opaque_queue != &br_audio_source_queue) return 0u;
    count = br_audio_source_queue.count;
    /* CPU0 is allowed to start draining immediately after submission. The
     * lock, completion count, and job states are therefore live ownership
     * fields, not publication invariants; only validate the immutable graph. */
    if (!count || count > BR_AUDIO_MAX_SOURCE_JOBS) return 0u;
    for (i = 0; i < count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[i];
        if (job->part >= BR_PARTS || !job->frames ||
            job->frames > BR_BLOCK || !job->output ||
            !br_audio_job_dependencies_valid(job, i) ||
            (job->kind == BR_AUDIO_JOB_KIT_FINALIZE &&
             !br_audio_kit_finalize_graph_valid(job)))
            return 0u;
    }
#if BR_AUDIO_FUSION_FIXED_PLAN
    if (br_audio_source_queue.fixed_plan_active) {
        uint32_t seen[2] = {0u, 0u};
        uint32_t assigned[2] = {0u, 0u};
        uint32_t core;
        for (i = 0u; i < count; ++i) {
            const br_audio_source_job_t *job = &br_audio_source_jobs[i];
            uint32_t bit;
            if (job->fixed_plan_core >= 2u || job->fixed_plan_order >= 32u)
                return 0u;
            bit = 1u << job->fixed_plan_order;
            if (seen[job->fixed_plan_core] & bit) return 0u;
            seen[job->fixed_plan_core] |= bit;
            ++assigned[job->fixed_plan_core];
        }
        for (core = 0u; core < 2u; ++core) {
            uint32_t count_for_core = br_audio_source_queue.fixed_plan_count[core];
            uint32_t mask;
            if (count_for_core > 32u) return 0u;
            mask = count_for_core == 32u ? UINT32_MAX :
                ((1u << count_for_core) - 1u);
            if (assigned[core] != count_for_core || seen[core] != mask)
                return 0u;
        }
    }
#endif
    return 1u;
}

static uint32_t br_audio_source_queue_select_ready(uint32_t count,
                                                  uint32_t current_core,
                                                  uint32_t *deadline_hit)
{
    uint32_t i, selected = UINT32_MAX, selected_cost = 0u;
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
    uint32_t affinity_selected = UINT32_MAX, affinity_cost = 0u;
    uint32_t finalizer_selected = UINT32_MAX, finalizer_cost = 0u;
#endif
    if (deadline_hit) *deadline_hit = 0u;
    for (i = 0u; i < count; ++i) {
        br_audio_source_job_t *job = &br_audio_source_jobs[i];
        if (job->state != 0u ||
            !br_audio_job_dependencies_done(job, count))
            continue;
        if (br_audio_source_fx_fusion_active) {
#if !BR_AUDIO_FUSION_FAST_READY_SCAN
            if (br_audio_source_deadline_expired()) {
                if (deadline_hit) *deadline_hit = 1u;
                return UINT32_MAX;
            }
#endif
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
            if (job->kind == BR_AUDIO_JOB_KIT_FINALIZE) {
                if (finalizer_selected == UINT32_MAX ||
                    job->cost_hint > finalizer_cost) {
                    finalizer_selected = i;
                    finalizer_cost = job->cost_hint;
                }
            } else {
                if (selected == UINT32_MAX || job->cost_hint > selected_cost) {
                    selected = i;
                    selected_cost = job->cost_hint;
                }
                if (job->affinity_core == current_core && current_core < 2u &&
                    (affinity_selected == UINT32_MAX ||
                     job->cost_hint > affinity_cost)) {
                    affinity_selected = i;
                    affinity_cost = job->cost_hint;
                }
            }
#else
            if (selected == UINT32_MAX || job->cost_hint > selected_cost) {
                selected = i;
                selected_cost = job->cost_hint;
            }
#endif
        } else {
            if (br_audio_source_deadline_expired()) {
                if (deadline_hit) *deadline_hit = 1u;
                return UINT32_MAX;
            }
            selected = i;
            break;
        }
    }
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
    /* A ready kit finalizer is the end of the queue's longest dependency
     * chain, so schedule it before unrelated whole-track jobs. Otherwise
     * retain global LPT ordering unless a same-core job is within 25% of the
     * largest ready cost; this gives affinity without pinning an imbalanced
     * core to a much cheaper job. */
    if (finalizer_selected != UINT32_MAX) return finalizer_selected;
    if (affinity_selected != UINT32_MAX &&
        (selected == UINT32_MAX ||
         affinity_cost >= selected_cost - selected_cost / 4u))
        return affinity_selected;
#else
    (void)current_core;
#endif
    return selected;
}

#if defined(BR_SOURCE_QUEUE_TEST) && BR_AUDIO_FUSION_EWMA_SCHEDULER
uint32_t br_audio_source_test_select_ready(uint32_t current_core)
{
    uint32_t deadline_hit;
    return br_audio_source_queue_select_ready(br_audio_source_queue.count,
                                              current_core, &deadline_hit);
}
#endif

static uint32_t br_audio_fixed_plan_add_checked(uint32_t a, uint32_t b,
                                                uint32_t *sum)
{
    if (!sum || a > UINT32_MAX - b) return 0u;
    *sum = a + b;
    return 1u;
}

#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
static uint32_t br_audio_foreground_fixed8_key(
    uint32_t frames, uint32_t kit_part, const uint32_t role_desc[8])
{
    uint32_t key = 2166136261u, i;
    key = (key ^ frames) * 16777619u;
    key = (key ^ kit_part) * 16777619u;
    for (i = 0u; i < 8u; ++i)
        key = (key ^ role_desc[i]) * 16777619u;
    return key;
}

static void br_audio_foreground_fixed8_publish(
    uint32_t frames, uint32_t kit_part, uint32_t ready_queue_makespan,
    const uint32_t role_desc[8], const uint32_t independent_cost[7][2],
    const uint32_t finalizer_cost[2])
{
    br_audio_foreground_fixed8_dto_t *dto = &br_audio_foreground_fixed8_dto;
    uint32_t i, core;
    if (br_audio_foreground_fixed8_state != BR_AUDIO_FOREGROUND_FIXED8_FREE)
        return;
    dto->sequence = ++br_audio_foreground_fixed8_next_sequence;
    if (!dto->sequence) dto->sequence = ++br_audio_foreground_fixed8_next_sequence;
    dto->epoch = br_audio_foreground_fixed8_epoch;
    dto->frames = frames;
    dto->kit_part = kit_part;
    dto->ready_queue_makespan = ready_queue_makespan;
    for (i = 0u; i < 8u; ++i) dto->role_desc[i] = role_desc[i];
    dto->workclass_key = br_audio_foreground_fixed8_key(
        frames, kit_part, role_desc);
    for (i = 0u; i < 7u; ++i)
        for (core = 0u; core < 2u; ++core)
            dto->independent_cost[i][core] = independent_cost[i][core];
    for (core = 0u; core < 2u; ++core)
        dto->finalizer_cost[core] = finalizer_cost[core];
    RING_PUBLISH();
    if (dto->epoch != br_audio_foreground_fixed8_epoch) return;
    br_audio_foreground_fixed8_state = BR_AUDIO_FOREGROUND_FIXED8_INPUT;
    RING_PUBLISH();
}

static uint32_t br_audio_foreground_fixed8_score(
    const uint8_t core_for_job[8], const uint8_t order_for_job[8],
    const uint8_t count_for_core[2],
    const uint32_t independent_cost[7][2], const uint32_t finalizer_cost[2],
    uint32_t *makespan_out)
{
    uint32_t lane_load[2] = {0u, 0u}, track_load[2] = {0u, 0u};
    uint32_t lane_count[2] = {0u, 0u}, lane_last_order[2] = {0u, 0u};
    uint32_t seen[2] = {0u, 0u}, assigned[2] = {0u, 0u};
    uint32_t track_cost[3], track_core[3], track_order[3];
    uint32_t finalizer_core = UINT32_MAX, finalizer_order = UINT32_MAX, job;
    for (job = 0u; job < 8u; ++job) {
        const uint32_t core = core_for_job[job];
        const uint32_t order = order_for_job[job];
        uint32_t *load;
        uint32_t cost;
        if (core >= 2u || order >= 8u || (seen[core] & (1u << order)))
            return 0u;
        seen[core] |= 1u << order;
        ++assigned[core];
        if (job < 4u) {
            load = &lane_load[core];
            cost = independent_cost[job][core];
            ++lane_count[core];
            if (order > lane_last_order[core]) lane_last_order[core] = order;
        } else if (job < 7u) {
            load = &track_load[core];
            cost = independent_cost[job][core];
            track_cost[job - 4u] = cost;
            track_core[job - 4u] = core;
            track_order[job - 4u] = order;
        } else {
            finalizer_core = core;
            finalizer_order = order;
            continue;
        }
        if (!cost || !br_audio_fixed_plan_add_checked(*load, cost, load))
            return 0u;
    }
    if (finalizer_core >= 2u || assigned[0] != count_for_core[0] ||
        assigned[1] != count_for_core[1] ||
        !count_for_core[0] || !count_for_core[1] ||
        count_for_core[0] + count_for_core[1] != 8u ||
        seen[0] != ((1u << count_for_core[0]) - 1u) ||
        seen[1] != ((1u << count_for_core[1]) - 1u) ||
        !finalizer_cost[finalizer_core]) return 0u;
    {
        const uint32_t other = finalizer_core ^ 1u;
        uint32_t pre_tracks = 0u, post_tracks = 0u;
        uint32_t lane_ready = lane_load[0] > lane_load[1] ?
            lane_load[0] : lane_load[1];
        uint32_t owner_ready, final_start, final_end, owner_end;
        uint32_t other_end, span, i;
        if (lane_count[finalizer_core] &&
            finalizer_order <= lane_last_order[finalizer_core]) return 0u;
        for (i = 0u; i < 2u; ++i) {
            if (lane_count[i]) {
                uint32_t first_track = UINT32_MAX, j;
                for (j = 0u; j < 3u; ++j)
                    if (track_core[j] == i && track_order[j] < first_track)
                        first_track = track_order[j];
                if (first_track <= lane_last_order[i]) return 0u;
            }
        }
        for (i = 0u; i < 3u; ++i) {
            if (track_core[i] == finalizer_core) {
                uint32_t *bucket = track_order[i] < finalizer_order ?
                    &pre_tracks : &post_tracks;
                if (!br_audio_fixed_plan_add_checked(*bucket, track_cost[i],
                                                     bucket)) return 0u;
            }
        }
        if (!br_audio_fixed_plan_add_checked(lane_load[finalizer_core],
                pre_tracks, &owner_ready) ||
            !br_audio_fixed_plan_add_checked(lane_load[other],
                track_load[other], &other_end)) return 0u;
        final_start = lane_ready > owner_ready ? lane_ready : owner_ready;
        if (!br_audio_fixed_plan_add_checked(final_start,
                finalizer_cost[finalizer_core], &final_end) ||
            !br_audio_fixed_plan_add_checked(final_end, post_tracks,
                &owner_end)) return 0u;
        span = owner_end > other_end ? owner_end : other_end;
        if (!span) return 0u;
        *makespan_out = span;
    }
    return 1u;
}

static uint32_t br_audio_foreground_fixed8_take_candidate(
    uint32_t frames, uint32_t kit_part, uint32_t ready_queue_makespan,
    const uint32_t role_desc[8], const uint32_t independent_cost[7][2],
    const uint32_t finalizer_cost[2], uint8_t core_for_job[8],
    uint8_t order_for_job[8], uint8_t count_for_core[2],
    uint32_t *predicted_makespan)
{
    br_audio_foreground_fixed8_dto_t candidate;
    uint32_t i, span;
    if (br_audio_foreground_fixed8_state != BR_AUDIO_FOREGROUND_FIXED8_RESULT)
        return 0u;
    candidate = br_audio_foreground_fixed8_dto;
    RING_PUBLISH();
    br_audio_foreground_fixed8_state = BR_AUDIO_FOREGROUND_FIXED8_FREE;
    RING_PUBLISH();
    if (!candidate.sequence || candidate.epoch != br_audio_foreground_fixed8_epoch ||
        candidate.frames != frames || candidate.kit_part != kit_part ||
        candidate.workclass_key != br_audio_foreground_fixed8_key(
            frames, kit_part, role_desc)) return 0u;
    for (i = 0u; i < 8u; ++i)
        if (candidate.role_desc[i] != role_desc[i]) return 0u;
    if (!br_audio_foreground_fixed8_score(candidate.core_for_job,
            candidate.order_for_job, candidate.count_for_core,
            independent_cost, finalizer_cost, &span)) return 0u;
    if (ready_queue_makespan <= span ||
        ready_queue_makespan - span < BR_AUDIO_FUSION_FIXED8_PLAN_MIN_GAIN_TICKS)
        return 0u;
    for (i = 0u; i < 8u; ++i) {
        core_for_job[i] = candidate.core_for_job[i];
        order_for_job[i] = candidate.order_for_job[i];
    }
    count_for_core[0] = candidate.count_for_core[0];
    count_for_core[1] = candidate.count_for_core[1];
    *predicted_makespan = span;
    return 1u;
}

/* Called from the preemptible CPU0 main loop. Only the copied DTO is read; no
 * engine, job, UI, or worker-owned memory is touched. */
void br_audio_fixed8_foreground_service(void)
{
    br_audio_foreground_fixed8_dto_t snapshot;
    uint8_t core_for_job[8], order_for_job[8], count_for_core[2];
    uint32_t predicted_makespan = 0u, ok;
    if (br_audio_foreground_fixed8_state != BR_AUDIO_FOREGROUND_FIXED8_INPUT)
        return;
    br_audio_foreground_fixed8_state = BR_AUDIO_FOREGROUND_FIXED8_SOLVING;
    RING_PUBLISH();
    snapshot = br_audio_foreground_fixed8_dto;
    RING_PUBLISH();
    if (!snapshot.sequence || snapshot.epoch != br_audio_foreground_fixed8_epoch) {
        br_audio_foreground_fixed8_state = BR_AUDIO_FOREGROUND_FIXED8_FREE;
        RING_PUBLISH();
        return;
    }
#if BR_AUDIO_FIXED8_SOLVER_RAM
    {
        uint32_t (* volatile solve)(const uint32_t [7][2],
            const uint32_t [2], uint8_t [8], uint8_t [8], uint8_t [2],
            uint32_t *) = br_audio_fixed8_plan_solve;
        ok = solve(snapshot.independent_cost, snapshot.finalizer_cost,
            core_for_job, order_for_job, count_for_core, &predicted_makespan);
    }
#else
    ok = br_audio_fixed8_plan_solve(snapshot.independent_cost,
        snapshot.finalizer_cost, core_for_job, order_for_job, count_for_core,
        &predicted_makespan);
#endif
    if (!ok || !predicted_makespan ||
        count_for_core[0] + count_for_core[1] != 8u ||
        !count_for_core[0] || !count_for_core[1]) {
        br_audio_foreground_fixed8_state = BR_AUDIO_FOREGROUND_FIXED8_FREE;
        RING_PUBLISH();
        return;
    }
    for (uint32_t i = 0u; i < 8u; ++i) {
        br_audio_foreground_fixed8_dto.core_for_job[i] = core_for_job[i];
        br_audio_foreground_fixed8_dto.order_for_job[i] = order_for_job[i];
    }
    br_audio_foreground_fixed8_dto.count_for_core[0] = count_for_core[0];
    br_audio_foreground_fixed8_dto.count_for_core[1] = count_for_core[1];
    br_audio_foreground_fixed8_dto.solved_makespan = predicted_makespan;
    RING_PUBLISH();
    if (snapshot.epoch != br_audio_foreground_fixed8_epoch) {
        br_audio_foreground_fixed8_state = BR_AUDIO_FOREGROUND_FIXED8_FREE;
        RING_PUBLISH();
        return;
    }
    br_audio_foreground_fixed8_state = BR_AUDIO_FOREGROUND_FIXED8_RESULT;
    RING_PUBLISH();
}
#endif

#if BR_AUDIO_FUSION_FIXED_PLAN
/* This experiment only covers the exact 6-job graph: two independent kit
 * partitions, their merge, and three independent source/postprocess/FX jobs.
 * Every selected descriptor has one fixed owner, so workers never contend on
 * the shared queue lock. The state word remains the dependency publication. */
static uint32_t br_audio_fixed_plan_add(uint32_t a, uint32_t b)
{
    return a > UINT32_MAX - b ? UINT32_MAX : a + b;
}

static void br_audio_fixed_plan_disable(void)
{
    uint32_t i;
    br_audio_source_queue.fixed_plan_active = 0u;
    for (i = 0u; i < 2u; ++i) {
        br_audio_source_queue.fixed_plan_count[i] = 0u;
        br_audio_source_queue.fixed_plan_cursor[i] = 0u;
        br_audio_source_queue.fixed_plan_completed[i] = 0u;
    }
    for (i = 0u; i < BR_AUDIO_MAX_SOURCE_JOBS; ++i) {
        br_audio_source_jobs[i].fixed_plan_core = UINT32_MAX;
        br_audio_source_jobs[i].fixed_plan_order = UINT32_MAX;
    }
}

static uint32_t br_audio_fixed_plan_lpt_reference(
    uint32_t count, uint32_t kit_a, uint32_t kit_b, uint32_t finalizer)
{
    uint32_t independent[5], independent_count = 0u;
    uint32_t finish[BR_AUDIO_MAX_SOURCE_JOBS] = {0u};
    uint32_t load[2] = {0u, 0u};
    uint32_t i, kit_ready, best = UINT32_MAX;
    for (i = 0u; i < count; ++i) {
        uint32_t at;
        if (i == finalizer) continue;
        at = independent_count;
        while (at && br_audio_source_jobs[independent[at - 1u]].cost_hint <
                      br_audio_source_jobs[i].cost_hint) {
            independent[at] = independent[at - 1u];
            --at;
        }
        independent[at] = i;
        ++independent_count;
    }
    if (independent_count != 5u) return UINT32_MAX;
    for (i = 0u; i < independent_count; ++i) {
        uint32_t index = independent[i];
        uint32_t core = load[0] <= load[1] ? 0u : 1u;
        if (!br_audio_fixed_plan_add_checked(
                load[core], br_audio_source_jobs[index].cost_hint,
                &load[core]))
            return UINT32_MAX;
        finish[index] = load[core];
    }
    kit_ready = finish[kit_a] > finish[kit_b] ? finish[kit_a] : finish[kit_b];
    for (i = 0u; i < 2u; ++i) {
        uint32_t start = load[i] > kit_ready ? load[i] : kit_ready;
        uint32_t end;
        if (!br_audio_fixed_plan_add_checked(
                start, br_audio_source_jobs[finalizer].cost_hint, &end))
            return UINT32_MAX;
        uint32_t makespan = end > load[i ^ 1u] ? end : load[i ^ 1u];
        if (makespan < best) best = makespan;
    }
    return best;
}

#if BR_AUDIO_FUSION_FIXED_PLAN_PART0_CPU0 && \
    !BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT
/* Exact eight-job graph: four independent kit lanes, one merge, and three
 * independent fused tracks. Keep part 0 plus the finalizer on core 0 and
 * parts 1/2 on core 1, selecting the lowest-cost two-lane split. */
static uint32_t br_audio_fixed_plan_prepare_eight_part0(uint32_t count)
{
    uint32_t lane_jobs[BR_DRUM_LANES] = {
        UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX
    };
    uint32_t track_by_part[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t finalizer = UINT32_MAX, kit_part = UINT32_MAX;
    uint32_t frames = 0u, i, best_lane_mask = 0u;
    uint32_t best_makespan = UINT32_MAX;
    uint32_t lane_a, lane_b;

    if (count != 8u) return 0u;
    for (i = 0u; i < count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[i];
        if (job->part >= BR_PARTS || !job->frames ||
            job->frames > BR_BLOCK || !job->output ||
            !job->cost_hint || job->cost_hint == UINT32_MAX)
            return 0u;
        if (!frames) frames = job->frames;
        if (job->frames != frames) return 0u;
        if (job->kind == BR_AUDIO_JOB_SOURCE) {
            uint32_t lane;
            for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
                if (job->lane_mask == (1u << lane)) break;
            if (lane >= BR_DRUM_LANES || lane_jobs[lane] != UINT32_MAX ||
                job->predecessor != UINT32_MAX ||
                job->predecessor2 != UINT32_MAX ||
                job->predecessor3 != UINT32_MAX ||
                job->predecessor4 != UINT32_MAX)
                return 0u;
            if (kit_part == UINT32_MAX) kit_part = job->part;
            if (job->part != kit_part ||
                job->output != br_audio_kit_partial[0][lane] ||
                job->key != br_audio_kit_key_output(
                    job->part, lane, job->lane_mask))
                return 0u;
            lane_jobs[lane] = i;
        } else if (job->kind == BR_AUDIO_JOB_KIT_FINALIZE) {
            if (finalizer != UINT32_MAX) return 0u;
            finalizer = i;
        } else if (job->kind == BR_AUDIO_JOB_FUSED_TRACK) {
            if (job->part >= 3u || track_by_part[job->part] != UINT32_MAX ||
                job->lane_mask != BR_DRUM_ALL_MASK ||
                job->predecessor != UINT32_MAX ||
                job->predecessor2 != UINT32_MAX ||
                job->predecessor3 != UINT32_MAX ||
                job->predecessor4 != UINT32_MAX)
                return 0u;
            track_by_part[job->part] = i;
        } else {
            return 0u;
        }
    }
    if (kit_part < 3u || kit_part >= BR_PARTS || finalizer == UINT32_MAX ||
        br_audio_source_jobs[finalizer].part != kit_part ||
        br_audio_source_jobs[finalizer].frames != frames ||
        br_audio_source_jobs[finalizer].lane_mask != BR_DRUM_ALL_MASK ||
        !br_audio_job_dependencies_valid(&br_audio_source_jobs[finalizer],
                                         finalizer) ||
        !br_audio_kit_finalize_graph_valid(&br_audio_source_jobs[finalizer]))
        return 0u;
    for (i = 0u; i < BR_DRUM_LANES; ++i)
        if (lane_jobs[i] == UINT32_MAX) return 0u;
    for (i = 0u; i < 3u; ++i)
        if (track_by_part[i] == UINT32_MAX ||
            br_audio_source_jobs[track_by_part[i]].frames != frames)
            return 0u;

    for (lane_a = 0u; lane_a + 1u < BR_DRUM_LANES; ++lane_a) {
        for (lane_b = lane_a + 1u; lane_b < BR_DRUM_LANES; ++lane_b) {
            const uint32_t core0_mask = (1u << lane_a) | (1u << lane_b);
            uint32_t lane_load[2] = {0u, 0u};
            uint32_t lane, core0_before_finish, core1_finish;
            uint32_t final_start, final_end, makespan, valid = 1u;
            for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
                const uint32_t owner = (core0_mask & (1u << lane)) ? 0u : 1u;
                if (!br_audio_fixed_plan_add_checked(
                        lane_load[owner],
                        br_audio_source_jobs[lane_jobs[lane]].cost_hint,
                        &lane_load[owner])) {
                    valid = 0u;
                    break;
                }
            }
            if (!valid ||
                !br_audio_fixed_plan_add_checked(
                    lane_load[0u],
                    br_audio_source_jobs[track_by_part[0]].cost_hint,
                    &core0_before_finish) ||
                !br_audio_fixed_plan_add_checked(
                    lane_load[1u],
                    br_audio_source_jobs[track_by_part[1]].cost_hint,
                    &core1_finish) ||
                !br_audio_fixed_plan_add_checked(
                    core1_finish,
                    br_audio_source_jobs[track_by_part[2]].cost_hint,
                    &core1_finish))
                continue;
            final_start = core0_before_finish > lane_load[1u] ?
                core0_before_finish : lane_load[1u];
            if (!br_audio_fixed_plan_add_checked(
                    final_start,
                    br_audio_source_jobs[finalizer].cost_hint,
                    &final_end))
                continue;
            makespan = final_end > core1_finish ? final_end : core1_finish;
            if (makespan < best_makespan) {
                best_makespan = makespan;
                best_lane_mask = core0_mask;
            }
        }
    }
    if (!best_lane_mask || best_makespan == UINT32_MAX) return 0u;

    {
        uint32_t order[2] = {0u, 0u};
        uint32_t lane;
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            const uint32_t core = (best_lane_mask & (1u << lane)) ? 0u : 1u;
            br_audio_source_jobs[lane_jobs[lane]].fixed_plan_core = core;
            br_audio_source_jobs[lane_jobs[lane]].fixed_plan_order = order[core]++;
        }
        br_audio_source_jobs[track_by_part[0]].fixed_plan_core = 0u;
        br_audio_source_jobs[track_by_part[0]].fixed_plan_order = order[0]++;
        br_audio_source_jobs[track_by_part[1]].fixed_plan_core = 1u;
        br_audio_source_jobs[track_by_part[1]].fixed_plan_order = order[1]++;
        br_audio_source_jobs[track_by_part[2]].fixed_plan_core = 1u;
        br_audio_source_jobs[track_by_part[2]].fixed_plan_order = order[1]++;
        br_audio_source_jobs[finalizer].fixed_plan_core = 0u;
        br_audio_source_jobs[finalizer].fixed_plan_order = order[0]++;
        if (order[0] != 4u || order[1] != 4u) {
            br_audio_fixed_plan_disable();
            return 0u;
        }
        br_audio_source_queue.fixed_plan_count[0] = 4u;
        br_audio_source_queue.fixed_plan_count[1] = 4u;
        br_audio_source_queue.fixed_plan_cursor[0] = 0u;
        br_audio_source_queue.fixed_plan_cursor[1] = 0u;
        br_audio_source_queue.fixed_plan_completed[0] = 0u;
        br_audio_source_queue.fixed_plan_completed[1] = 0u;
        br_audio_source_queue.fixed_plan_active = 1u;
    }
    return 1u;
}
#endif

#if BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
static uint32_t br_audio_fusion_history_slot(
    const br_audio_source_job_t *job);
static uint32_t br_audio_fusion_job_signature(
    const br_audio_source_job_t *job);
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
static uint32_t br_audio_fusion_apply_robust_floor(
    const br_audio_source_job_t *job, uint32_t cost);
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
static uint32_t br_audio_fusion_guarded_history_cost(
    const br_audio_fusion_job_history_t *history, uint32_t core);
#endif
#endif
#endif

#if BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
/* The caller first validates the fixed eight-job graph: seven independent
 * lane/track jobs and one kit finalizer. Consequently only the finalizer can
 * have dependencies, and its validated predecessor set can be tested as one
 * bitmask instead of rebuilding/scanning four predecessor words for every
 * candidate job on every simulated dispatch. */
static uint32_t br_audio_fixed8_sim_select_ready(
    uint32_t count, uint32_t core, uint32_t done, uint32_t claimed,
    uint32_t finalizer, uint32_t finalizer_dependencies)
{
    uint32_t i, selected = UINT32_MAX, selected_cost = 0u;
    uint32_t affinity_selected = UINT32_MAX, affinity_cost = 0u;
    for (i = 0u; i < count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[i];
        if (claimed & (1u << i)) continue;
        if (i == finalizer) {
            if ((done & finalizer_dependencies) == finalizer_dependencies)
                return i;
            continue;
        }
        if (selected == UINT32_MAX || job->cost_hint > selected_cost) {
            selected = i;
            selected_cost = job->cost_hint;
        }
        if (job->affinity_core == core &&
            (affinity_selected == UINT32_MAX ||
             job->cost_hint > affinity_cost)) {
            affinity_selected = i;
            affinity_cost = job->cost_hint;
        }
    }
    if (affinity_selected != UINT32_MAX &&
        (selected == UINT32_MAX ||
         affinity_cost >= selected_cost - selected_cost / 4u))
        return affinity_selected;
    return selected;
}

static uint32_t br_audio_fixed8_ready_queue_estimate(
    uint32_t count, uint32_t finalizer, uint32_t *makespan_out)
{
    uint32_t now[2] = {0u, 0u}, finish[2] = {0u, 0u};
    uint32_t running[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t finalizer_dependencies = 0u;
    uint32_t claimed = 0u, done = 0u, completed = 0u, core;
    const br_audio_source_job_t *finalizer_job;
    const uint32_t predecessor[4] = {
        finalizer < count ? br_audio_source_jobs[finalizer].predecessor : UINT32_MAX,
        finalizer < count ? br_audio_source_jobs[finalizer].predecessor2 : UINT32_MAX,
        finalizer < count ? br_audio_source_jobs[finalizer].predecessor3 : UINT32_MAX,
        finalizer < count ? br_audio_source_jobs[finalizer].predecessor4 : UINT32_MAX
    };
    uint32_t i;
    if (count != 8u || !makespan_out || finalizer >= count) return 0u;
    finalizer_job = &br_audio_source_jobs[finalizer];
    if (finalizer_job->kind != BR_AUDIO_JOB_KIT_FINALIZE) return 0u;
    for (i = 0u; i < 4u; ++i) {
        if (predecessor[i] == UINT32_MAX) continue;
        if (predecessor[i] >= count) return 0u;
        finalizer_dependencies |= 1u << predecessor[i];
    }
    if (!finalizer_dependencies) return 0u;
    for (core = 0u; core < 2u; ++core) {
        const uint32_t selected = br_audio_fixed8_sim_select_ready(
            count, core, done, claimed, finalizer, finalizer_dependencies);
        if (selected != UINT32_MAX) {
            const uint32_t cost = br_audio_source_jobs[selected].cost_hint;
            if (!cost || cost == UINT32_MAX) return 0u;
            claimed |= 1u << selected;
            running[core] = selected;
            finish[core] = cost;
        }
    }
    while (completed < count) {
        uint32_t job, dispatch_core;
        if (running[0] == UINT32_MAX && running[1] == UINT32_MAX) return 0u;
        core = running[0] == UINT32_MAX ? 1u :
            running[1] == UINT32_MAX ? 0u :
            (finish[0] <= finish[1] ? 0u : 1u);
        job = running[core];
        done |= 1u << job;
        ++completed;
        now[core] = finish[core];
        running[core] = UINT32_MAX;
        for (dispatch_core = 0u; dispatch_core < 2u; ++dispatch_core) {
            uint32_t selected;
            if (running[dispatch_core] != UINT32_MAX) continue;
            selected = br_audio_fixed8_sim_select_ready(
                count, dispatch_core, done, claimed, finalizer,
                finalizer_dependencies);
            if (selected != UINT32_MAX) {
                const uint32_t cost = br_audio_source_jobs[selected].cost_hint;
                uint32_t start = now[dispatch_core];
                if (start < now[core]) start = now[core];
                if (!cost || cost == UINT32_MAX ||
                    UINT32_MAX - start < cost) return 0u;
                claimed |= 1u << selected;
                running[dispatch_core] = selected;
                now[dispatch_core] = start;
                finish[dispatch_core] = start + cost;
            }
        }
    }
    *makespan_out = now[0] > now[1] ? now[0] : now[1];
    return *makespan_out != 0u;
}

#endif

static uint32_t br_audio_fixed_plan_prepare_eight_general(uint32_t count)
{
    uint32_t lane_jobs[BR_DRUM_LANES] = {
        UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX
    };
    uint32_t track_jobs[3] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    uint32_t finalizer = UINT32_MAX, kit_part = UINT32_MAX;
    uint32_t frames = 0u, i;
    uint32_t independent_cost[7][2], finalizer_cost[2];
    uint8_t core_for_plan_job[8], order_for_plan_job[8], plan_count[2];
    uint32_t predicted_makespan;
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    uint32_t role_desc[8];
#endif
#if BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
    uint32_t ready_queue_makespan = 0u;
#endif

    if (count != 8u) return 0u;
    for (i = 0u; i < count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[i];
        if (job->part >= BR_PARTS || !job->frames ||
            job->frames > BR_BLOCK || !job->output ||
            !job->cost_hint || job->cost_hint == UINT32_MAX)
            return 0u;
        if (!frames) frames = job->frames;
        if (job->frames != frames) return 0u;
        if (job->kind == BR_AUDIO_JOB_SOURCE) {
            uint32_t lane;
            for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
                if (job->lane_mask == (1u << lane)) break;
            if (lane >= BR_DRUM_LANES || lane_jobs[lane] != UINT32_MAX ||
                job->predecessor != UINT32_MAX ||
                job->predecessor2 != UINT32_MAX ||
                job->predecessor3 != UINT32_MAX ||
                job->predecessor4 != UINT32_MAX)
                return 0u;
            if (kit_part == UINT32_MAX) kit_part = job->part;
            if (job->part != kit_part ||
                job->output != br_audio_kit_partial[0][lane] ||
                job->key != br_audio_kit_key_output(
                    job->part, lane, job->lane_mask))
                return 0u;
            lane_jobs[lane] = i;
        } else if (job->kind == BR_AUDIO_JOB_KIT_FINALIZE) {
            if (finalizer != UINT32_MAX) return 0u;
            finalizer = i;
        } else if (job->kind == BR_AUDIO_JOB_FUSED_TRACK) {
            if (job->part >= 3u || track_jobs[job->part] != UINT32_MAX ||
                job->lane_mask != BR_DRUM_ALL_MASK ||
                job->predecessor != UINT32_MAX ||
                job->predecessor2 != UINT32_MAX ||
                job->predecessor3 != UINT32_MAX ||
                job->predecessor4 != UINT32_MAX)
                return 0u;
            track_jobs[job->part] = i;
        } else {
            return 0u;
        }
    }
    if (kit_part < 3u || kit_part >= BR_PARTS || finalizer == UINT32_MAX ||
        br_audio_source_jobs[finalizer].part != kit_part ||
        br_audio_source_jobs[finalizer].frames != frames ||
        br_audio_source_jobs[finalizer].lane_mask != BR_DRUM_ALL_MASK ||
        !br_audio_job_dependencies_valid(&br_audio_source_jobs[finalizer],
                                         finalizer) ||
        !br_audio_kit_finalize_graph_valid(&br_audio_source_jobs[finalizer]))
        return 0u;
    for (i = 0u; i < BR_DRUM_LANES; ++i)
        if (lane_jobs[i] == UINT32_MAX) return 0u;
    for (i = 0u; i < 3u; ++i)
        if (track_jobs[i] == UINT32_MAX ||
            br_audio_source_jobs[track_jobs[i]].frames != frames)
            return 0u;
#if BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
    if (!br_audio_fixed8_ready_queue_estimate(count, finalizer,
                                               &ready_queue_makespan)) {
        return 0u;
    }
#endif

    for (i = 0u; i < 7u; ++i) {
        const br_audio_source_job_t *job = i < BR_DRUM_LANES ?
            &br_audio_source_jobs[lane_jobs[i]] :
            &br_audio_source_jobs[track_jobs[i - BR_DRUM_LANES]];
        uint32_t core;
#if BR_AUDIO_FUSION_EWMA_SCHEDULER && BR_AUDIO_FIXED8_HISTORY_KEY_HOIST
        const uint32_t slot = br_audio_fusion_history_slot(job);
        const uint32_t frame_class = job->frames >= BR_BLOCK ? 1u : 0u;
        uint32_t signature = 0u;
        uint32_t history_enabled = 0u;
#if BR_AUDIO_FUSION_ACTIVITY_EWMA
        if (job->activity_class && slot < BR_AUDIO_FUSION_HISTORY_SLOTS &&
            job->part < BR_PARTS) {
#else
        if (slot < BR_AUDIO_FUSION_HISTORY_SLOTS && job->part < BR_PARTS) {
#endif
            signature = br_audio_fusion_job_signature(job);
            history_enabled = 1u;
        }
#endif
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR && BR_AUDIO_FIXED8_HISTORY_KEY_HOIST
        const uint32_t robust_floor =
            br_audio_fusion_apply_robust_floor(job, 0u);
#endif
        for (core = 0u; core < 2u; ++core) {
            uint32_t cost = job->cost_hint;
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
            uint32_t guarded_history_hit = 0u;
#endif
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
            if (history_enabled) {
                const br_audio_fusion_job_history_t *history =
                    &br_audio_fusion_job_history[job->part][slot][frame_class];
                if (history->signature == signature &&
                    history->samples[core] && history->average_ticks[core]) {
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
                    uint32_t guarded =
                        br_audio_fusion_guarded_history_cost(history, core);
                    if (guarded) {
                        cost = guarded;
                        guarded_history_hit = 1u;
                    } else {
                        cost = history->average_ticks[core];
                    }
#else
                    cost = history->average_ticks[core];
#endif
                }
            }
#endif
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
            if (!guarded_history_hit && cost < robust_floor)
                cost = robust_floor;
#else
            if (cost < robust_floor) cost = robust_floor;
#endif
#endif
            independent_cost[i][core] = cost;
        }
    }
#if BR_AUDIO_FIXED8_HISTORY_KEY_HOIST && \
    (BR_AUDIO_FUSION_EWMA_SCHEDULER || BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR)
    {
        const br_audio_source_job_t *job = &br_audio_source_jobs[finalizer];
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
        uint32_t slot = br_audio_fusion_history_slot(job);
        uint32_t frame_class = frames >= BR_BLOCK ? 1u : 0u;
        uint32_t signature = 0u;
        uint32_t history_enabled = 0u;
#if BR_AUDIO_FUSION_ACTIVITY_EWMA
        if (job->activity_class && slot < BR_AUDIO_FUSION_HISTORY_SLOTS &&
            kit_part < BR_PARTS) {
#else
        if (slot < BR_AUDIO_FUSION_HISTORY_SLOTS && kit_part < BR_PARTS) {
#endif
            signature = br_audio_fusion_job_signature(job);
            history_enabled = 1u;
        }
#endif
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
        const uint32_t robust_floor =
            br_audio_fusion_apply_robust_floor(job, 0u);
#endif
        for (i = 0u; i < 2u; ++i) {
            uint32_t cost = job->cost_hint;
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
            uint32_t guarded_history_hit = 0u;
#endif
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
            if (history_enabled) {
                const br_audio_fusion_job_history_t *history =
                    &br_audio_fusion_job_history[kit_part][slot][frame_class];
                if (history->signature == signature &&
                    history->samples[i] && history->average_ticks[i]) {
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
                    uint32_t guarded =
                        br_audio_fusion_guarded_history_cost(history, i);
                    if (guarded) {
                        cost = guarded;
                        guarded_history_hit = 1u;
                    } else {
                        cost = history->average_ticks[i];
                    }
#else
                    cost = history->average_ticks[i];
#endif
                }
            }
#endif
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
            if (!guarded_history_hit && cost < robust_floor)
                cost = robust_floor;
#else
            if (cost < robust_floor) cost = robust_floor;
#endif
#endif
            finalizer_cost[i] = cost;
        }
    }
#else
    for (i = 0u; i < 2u; ++i) {
        uint32_t cost = br_audio_source_jobs[finalizer].cost_hint;
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
        uint32_t guarded_history_hit = 0u;
#endif
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
        uint32_t slot = br_audio_fusion_history_slot(
            &br_audio_source_jobs[finalizer]);
        uint32_t frame_class = frames >= BR_BLOCK ? 1u : 0u;
#if BR_AUDIO_FUSION_ACTIVITY_EWMA
        if (br_audio_source_jobs[finalizer].activity_class &&
            slot < BR_AUDIO_FUSION_HISTORY_SLOTS && kit_part < BR_PARTS) {
#else
        if (slot < BR_AUDIO_FUSION_HISTORY_SLOTS && kit_part < BR_PARTS) {
#endif
            const br_audio_fusion_job_history_t *history =
                &br_audio_fusion_job_history[kit_part][slot][frame_class];
            if (history->signature == br_audio_fusion_job_signature(
                    &br_audio_source_jobs[finalizer]) &&
                history->samples[i] && history->average_ticks[i]) {
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
                uint32_t guarded =
                    br_audio_fusion_guarded_history_cost(history, i);
                if (guarded) {
                    cost = guarded;
                    guarded_history_hit = 1u;
                } else {
                    cost = history->average_ticks[i];
                }
#else
                cost = history->average_ticks[i];
#endif
            }
        }
#endif
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
        if (!guarded_history_hit)
            cost = br_audio_fusion_apply_robust_floor(
                &br_audio_source_jobs[finalizer], cost);
#else
        cost = br_audio_fusion_apply_robust_floor(
            &br_audio_source_jobs[finalizer], cost);
#endif
#endif
        finalizer_cost[i] = cost;
    }
#endif
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    for (i = 0u; i < 8u; ++i) {
        const uint32_t job_index = i < BR_DRUM_LANES ? lane_jobs[i] :
            i < 7u ? track_jobs[i - BR_DRUM_LANES] : finalizer;
        const br_audio_source_job_t *job = &br_audio_source_jobs[job_index];
        role_desc[i] = job_index | (job->part << 8u) |
            (job->kind << 16u) | (job->lane_mask << 24u);
    }
#endif
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    if (!br_audio_foreground_fixed8_take_candidate(frames, kit_part,
            ready_queue_makespan, role_desc, independent_cost, finalizer_cost,
            core_for_plan_job, order_for_plan_job, plan_count,
            &predicted_makespan)) {
        br_audio_foreground_fixed8_publish(frames, kit_part,
            ready_queue_makespan, role_desc, independent_cost, finalizer_cost);
        return 0u;
    }
    br_audio_foreground_fixed8_publish(frames, kit_part,
        ready_queue_makespan, role_desc, independent_cost, finalizer_cost);
#else
#if BR_AUDIO_FIXED8_SOLVER_RAM
    uint32_t (* volatile solve)(const uint32_t [7][2],
            const uint32_t [2], uint8_t [8], uint8_t [8], uint8_t [2],
            uint32_t *) = br_audio_fixed8_plan_solve;
        if (!solve(independent_cost, finalizer_cost, core_for_plan_job,
                order_for_plan_job, plan_count, &predicted_makespan) ||
            !predicted_makespan || plan_count[0] + plan_count[1] != count ||
            !plan_count[0] || !plan_count[1]) {
#else
    if (!br_audio_fixed8_plan_solve(independent_cost, finalizer_cost,
            core_for_plan_job, order_for_plan_job, plan_count,
            &predicted_makespan) || !predicted_makespan ||
        plan_count[0] + plan_count[1] != count ||
        !plan_count[0] || !plan_count[1]) {
#endif
        return 0u;
    }
#if BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
    if (ready_queue_makespan <= predicted_makespan) {
        return 0u;
    }
    if (ready_queue_makespan - predicted_makespan <
            BR_AUDIO_FUSION_FIXED8_PLAN_MIN_GAIN_TICKS) {
        return 0u;
    }
#endif
#endif

    for (i = 0u; i < BR_DRUM_LANES; ++i) {
        br_audio_source_jobs[lane_jobs[i]].fixed_plan_core =
            core_for_plan_job[i];
        br_audio_source_jobs[lane_jobs[i]].fixed_plan_order =
            order_for_plan_job[i];
    }
    for (i = 0u; i < 3u; ++i) {
        const uint32_t plan_job = BR_DRUM_LANES + i;
        br_audio_source_jobs[track_jobs[i]].fixed_plan_core =
            core_for_plan_job[plan_job];
        br_audio_source_jobs[track_jobs[i]].fixed_plan_order =
            order_for_plan_job[plan_job];
    }
    br_audio_source_jobs[finalizer].fixed_plan_core = core_for_plan_job[7];
    br_audio_source_jobs[finalizer].fixed_plan_order = order_for_plan_job[7];
    br_audio_source_queue.fixed_plan_count[0] = plan_count[0];
    br_audio_source_queue.fixed_plan_count[1] = plan_count[1];
    br_audio_source_queue.fixed_plan_cursor[0] = 0u;
    br_audio_source_queue.fixed_plan_cursor[1] = 0u;
    br_audio_source_queue.fixed_plan_completed[0] = 0u;
    br_audio_source_queue.fixed_plan_completed[1] = 0u;
    br_audio_source_queue.fixed_plan_active = 1u;
    return 1u;
}
#endif

static uint32_t br_audio_fixed_plan_prepare(uint32_t count)
{
    uint32_t kit_a = UINT32_MAX, kit_b = UINT32_MAX;
    uint32_t finalizer = UINT32_MAX, tracks[3], track_count = 0u;
    uint32_t i, kit_part = UINT32_MAX, predicted_lpt, best = UINT32_MAX;
    uint32_t best_kit_core = UINT32_MAX, best_kit_track = UINT32_MAX;
#if !BR_AUDIO_FIXED_PLAN_SKIP_DUPLICATE_DISABLE
    br_audio_fixed_plan_disable();
#endif
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    if (count != 8u) br_audio_fixed8_foreground_invalidate();
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN_EIGHT_READY_QUEUE
    /* Preserve the claim-once ready queue unless the isolated fixed-eight
     * history-plan experiment is explicitly enabled. */
    if (count == 8u && !BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN) return 0u;
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN_GENERAL_EIGHT
    if (count == 8u) return br_audio_fixed_plan_prepare_eight_general(count);
#elif BR_AUDIO_FUSION_FIXED_PLAN_PART0_CPU0
    if (count == 8u) return br_audio_fixed_plan_prepare_eight_part0(count);
#endif
    if (count != 6u) return 0u;
    for (i = 0u; i < count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[i];
        if (job->part >= BR_PARTS || !job->frames ||
            job->frames > BR_BLOCK || !job->output ||
            !job->cost_hint || job->cost_hint == UINT32_MAX)
            return 0u;
        if (job->kind == BR_AUDIO_JOB_SOURCE &&
            job->lane_mask == BR_KIT_LANES_A) {
            if (kit_a != UINT32_MAX) return 0u;
            kit_a = i;
        } else if (job->kind == BR_AUDIO_JOB_SOURCE &&
                   job->lane_mask == BR_KIT_LANES_B) {
            if (kit_b != UINT32_MAX) return 0u;
            kit_b = i;
        } else if (job->kind == BR_AUDIO_JOB_KIT_FINALIZE) {
            if (finalizer != UINT32_MAX) return 0u;
            finalizer = i;
        } else if (job->kind == BR_AUDIO_JOB_FUSED_TRACK) {
            if (track_count >= 3u) return 0u;
            tracks[track_count++] = i;
        } else {
            return 0u;
        }
    }
    if (kit_a == UINT32_MAX || kit_b == UINT32_MAX ||
        finalizer == UINT32_MAX || track_count != 3u ||
        br_audio_source_jobs[kit_a].part != br_audio_source_jobs[kit_b].part ||
        br_audio_source_jobs[finalizer].part != br_audio_source_jobs[kit_a].part ||
        br_audio_source_jobs[finalizer].lane_mask != BR_DRUM_ALL_MASK ||
        br_audio_source_jobs[finalizer].predecessor != kit_a ||
        br_audio_source_jobs[finalizer].predecessor2 != kit_b ||
        br_audio_source_jobs[kit_a].predecessor != UINT32_MAX ||
        br_audio_source_jobs[kit_b].predecessor != UINT32_MAX ||
        br_audio_source_jobs[kit_a].predecessor2 != UINT32_MAX ||
        br_audio_source_jobs[kit_b].predecessor2 != UINT32_MAX)
        return 0u;
    kit_part = br_audio_source_jobs[kit_a].part;
    for (i = 0u; i < track_count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[tracks[i]];
        uint32_t prior;
        if (!job->frames || job->frames != br_audio_source_jobs[kit_a].frames ||
            job->lane_mask != BR_DRUM_ALL_MASK ||
            job->predecessor != UINT32_MAX ||
            job->predecessor2 != UINT32_MAX || job->part == kit_part)
            return 0u;
        for (prior = 0u; prior < i; ++prior)
            if (br_audio_source_jobs[tracks[prior]].part == job->part) return 0u;
    }
    if (!br_audio_source_jobs[kit_a].frames ||
        br_audio_source_jobs[kit_b].frames != br_audio_source_jobs[kit_a].frames ||
        br_audio_source_jobs[finalizer].frames != br_audio_source_jobs[kit_a].frames)
        return 0u;

    predicted_lpt = br_audio_fixed_plan_lpt_reference(
        count, kit_a, kit_b, finalizer);
    if (predicted_lpt == UINT32_MAX) return 0u;
    /* Try CPU1 as the kit owner first; equal predictions keep the measured
     * split convention where CPU0 carries the finalizer. */
    for (i = 0u; i < 6u; ++i) {
        uint32_t kit_core = 1u - i / 3u;
        uint32_t selected_track = i % 3u;
        uint32_t other[2], other_count = 0u, track, makespan;
        uint32_t kit_finish, kit_done, other_start, other_done;
        uint32_t other_tracks = 0u;
        uint32_t kit_track_cost = br_audio_source_jobs[tracks[selected_track]].cost_hint;
        if (!br_audio_fixed_plan_add_checked(
                br_audio_source_jobs[kit_a].cost_hint,
                br_audio_source_jobs[kit_b].cost_hint, &kit_finish))
            return 0u;
        for (track = 0u; track < 3u; ++track) {
            if (track == selected_track) continue;
            other[other_count++] = tracks[track];
            if (!br_audio_fixed_plan_add_checked(
                    other_tracks, br_audio_source_jobs[tracks[track]].cost_hint,
                    &other_tracks))
                return 0u;
        }
        if (other_count != 2u) return 0u;
        if (!br_audio_fixed_plan_add_checked(kit_finish, kit_track_cost,
                                             &kit_done))
            return 0u;
        other_start = other_tracks > kit_finish ? other_tracks : kit_finish;
        if (!br_audio_fixed_plan_add_checked(
                other_start, br_audio_source_jobs[finalizer].cost_hint,
                &other_done))
            return 0u;
        makespan = kit_done > other_done ? kit_done : other_done;
        if (makespan < best) {
            best = makespan;
            best_kit_core = kit_core;
            best_kit_track = selected_track;
        }
    }
    if (best_kit_core >= 2u || best == UINT32_MAX) return 0u;
    if (best >= predicted_lpt) {
#if BR_AUDIO_FUSION_FIXED_PLAN_FORCE_VALID_SIX
        /* Experimental probe: after exact graph and checked-cost validation,
         * keep the best fixed 3/3 candidate even when the dynamic LPT estimate
         * predicts a lower makespan. This changes only the startup-plan gate. */
#elif BR_AUDIO_FUSION_FIXED_PLAN_BALANCED_STARTUP
        /* Cold source/FX estimates can be noisy before the whole-job EWMA has
         * observed this exact part/effect signature. Permit the fixed 3/3
         * layout when its estimated makespan is still within 20% of LPT; a
         * clearly skewed or malformed plan keeps the ordinary dynamic queue. */
        if (predicted_lpt == UINT32_MAX ||
            best > br_audio_fixed_plan_add(predicted_lpt,
                                           predicted_lpt / 5u))
            return 0u;
#else
        return 0u;
#endif
    }

    {
        uint32_t other_core = best_kit_core ^ 1u;
        uint32_t other_order = 0u, i_track;
        br_audio_source_jobs[kit_a].fixed_plan_core = best_kit_core;
        br_audio_source_jobs[kit_a].fixed_plan_order = 0u;
        br_audio_source_jobs[kit_b].fixed_plan_core = best_kit_core;
        br_audio_source_jobs[kit_b].fixed_plan_order = 1u;
        br_audio_source_jobs[tracks[best_kit_track]].fixed_plan_core = best_kit_core;
        br_audio_source_jobs[tracks[best_kit_track]].fixed_plan_order = 2u;
        for (i_track = 0u; i_track < 3u; ++i_track) {
            if (i_track == best_kit_track) continue;
            br_audio_source_jobs[tracks[i_track]].fixed_plan_core = other_core;
            br_audio_source_jobs[tracks[i_track]].fixed_plan_order = other_order++;
        }
        br_audio_source_jobs[finalizer].fixed_plan_core = other_core;
        br_audio_source_jobs[finalizer].fixed_plan_order = other_order;
        br_audio_source_queue.fixed_plan_count[best_kit_core] = 3u;
        br_audio_source_queue.fixed_plan_count[other_core] = 3u;
        br_audio_source_queue.fixed_plan_cursor[0] = 0u;
        br_audio_source_queue.fixed_plan_cursor[1] = 0u;
        br_audio_source_queue.fixed_plan_completed[0] = 0u;
        br_audio_source_queue.fixed_plan_completed[1] = 0u;
        br_audio_source_queue.fixed_plan_active = 1u;
    }
    return 1u;
}

static uint32_t br_audio_fixed_plan_dependencies_done(
    const br_audio_source_job_t *job, uint32_t count)
{
    return br_audio_job_dependencies_done(job, count);
}

static uint32_t br_audio_source_fixed_plan_run(void)
{
    uint32_t core = br_audio_job_timing_core();
    if (core >= 2u || !br_audio_source_queue.fixed_plan_active) return 0u;
    for (;;) {
        uint32_t count, cursor, i, selected = UINT32_MAX;
        br_audio_source_job_t *job;
#if !BR_AUDIO_FUSION_FIXED_PLAN_FAST_CUTOFF
        if (br_audio_source_deadline_expired())
            return br_audio_source_publication_enabled ?
                BR_AUDIO_QUEUE_CUTOFF_DRAINED : BR_AUDIO_QUEUE_FAILED;
#endif
        count = br_audio_source_queue.count;
        cursor = br_audio_source_queue.fixed_plan_cursor[core];
        if (cursor >= br_audio_source_queue.fixed_plan_count[core]) return 1u;
        for (i = 0u; i < count; ++i)
            if (br_audio_source_jobs[i].fixed_plan_core == core &&
                br_audio_source_jobs[i].fixed_plan_order == cursor) {
                selected = i;
                break;
            }
        if (selected == UINT32_MAX) return 0u;
        job = &br_audio_source_jobs[selected];
        if (job->state == 2u) return 0u; /* Never repeat a completed job. */
        if (job->state != 0u) return 0u; /* State 1 has an unresolved owner. */
        if (!br_audio_fixed_plan_dependencies_done(job, count)) {
#if BR_AUDIO_FUSION_FIXED_PLAN_FAST_CUTOFF
            /* Keep dependency polling bounded after removing the loop-top
             * deadline read. Ready jobs retain the immediate preclaim gate. */
            if (br_audio_source_deadline_expired())
                return br_audio_source_publication_enabled ?
                    BR_AUDIO_QUEUE_CUTOFF_DRAINED : BR_AUDIO_QUEUE_FAILED;
#endif
            continue;
        }
        if (br_audio_source_deadline_expired())
            return br_audio_source_publication_enabled ?
                BR_AUDIO_QUEUE_CUTOFF_DRAINED : BR_AUDIO_QUEUE_FAILED;
        job->state = 1u;
        br_audio_source_publish();
#if defined(BR_SOURCE_QUEUE_TEST)
        br_audio_source_test_begin(selected);
#endif
        if (!br_audio_worker_sources(job)) {
#if defined(BR_SOURCE_QUEUE_TEST)
            br_audio_source_test_end(selected);
#endif
            return 0u;
        }
#if defined(BR_SOURCE_QUEUE_TEST)
        br_audio_source_test_end(selected);
#endif
        if (!br_audio_source_publication_enabled &&
            br_audio_source_deadline_expired()) return 0u;
        br_audio_source_publish();
        job->state = 2u;
        br_audio_source_publish();
        br_audio_source_queue.fixed_plan_completed[core] += 1u;
        br_audio_source_queue.fixed_plan_cursor[core] = cursor + 1u;
        br_audio_source_publish();
        if (br_audio_source_publication_enabled &&
            br_audio_source_deadline_expired())
            return BR_AUDIO_QUEUE_CUTOFF_DRAINED;
    }
}

/* Only the main core calls this after a confirmed join. Until then each core
 * publishes completion only to its own counter and the per-job state words. */
static uint32_t br_audio_fixed_plan_seal_completed(void)
{
    uint32_t completed, i;
    if (!br_audio_source_queue.fixed_plan_active) return 1u;
    if (br_audio_source_queue.fixed_plan_completed[0] !=
            br_audio_source_queue.fixed_plan_count[0] ||
        br_audio_source_queue.fixed_plan_completed[1] !=
            br_audio_source_queue.fixed_plan_count[1]) return 0u;
    completed = br_audio_source_queue.fixed_plan_completed[0] +
        br_audio_source_queue.fixed_plan_completed[1];
    if (completed != br_audio_source_queue.count) return 0u;
    for (i = 0u; i < completed; ++i)
        if (br_audio_source_jobs[i].state != 2u) return 0u;
    br_audio_source_queue.completed = completed;
    br_audio_source_publish();
    return 1u;
}
#endif

#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_LATE_COMPLETION_TEST)
static uint32_t br_audio_source_late_join_valid(
    const fm1_cpu1_worker_mailbox_t *mailbox, uint32_t sequence,
    uint32_t status, uint32_t result, uint32_t expected, uint32_t faults)
{
    return mailbox && sequence && status == FM1_CPU1_WORKER_OK &&
        result == expected && !faults && mailbox->cpu_id == 1u &&
        mailbox->request_seq == sequence && mailbox->done_seq == sequence &&
        mailbox->task_id == FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH &&
        mailbox->task_context == (uint32_t)(uintptr_t)&br_audio_source_queue &&
        mailbox->result == expected;
}
#endif

static uint32_t br_audio_late_index_ok(uint32_t index, uint32_t count)
{
    if (count == 8u) return index == 7u;
    return br_audio_graph_count_ok(count) && index < count &&
        (index == 7u || br_audio_source_jobs[index].kind == BR_AUDIO_JOB_TRACK_FX ||
         br_audio_source_jobs[index].kind == BR_AUDIO_JOB_SHARED_REVERB);
}

/* Validate immutable descriptors without reading another actor's live FX
 * state. Mutable states and counters are checked separately after join. */
static uint32_t br_audio_source_graph_layout(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline)
{
#if BR_AUDIO_FUSION_FOUR_WAY_KIT && BR_AUDIO_FUSION_ONLY_BUILD
    uint32_t i, split_mask = 0u, fx_mask = 0u, previous_fx_part = 0u, reverb_seen = 0u;
    if (!br_audio_graph_count_ok(count) || n != BR_BLOCK ||
        BR_BLOCK != FM1_AUDIO_HALF_FRAMES || !br_audio_source_fx_fusion_active ||
        br_pipeline_enabled ||
        br_audio_source_queue.fixed_plan_active > 1u ||
        br_audio_source_queue.count != count ||
        br_audio_source_queue.deadline_tick != deadline ||
        !br_audio_worker_source_queue_context_valid(&br_audio_source_queue))
        return 0u;
    for (i = 0u; i < count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[i];
        if (job->frames != n) return 0u;
        if (i < BR_DRUM_PART) {
            if (job->kind == BR_AUDIO_JOB_TRACK_SOURCE) split_mask |= 1u << i;
            if ((job->kind != BR_AUDIO_JOB_FUSED_TRACK &&
                 job->kind != BR_AUDIO_JOB_TRACK_SOURCE) || job->part != i ||
                job->output != bus[i] || job->key ||
                job->lane_mask != BR_DRUM_ALL_MASK ||
                job->predecessor != UINT32_MAX ||
                job->predecessor2 != UINT32_MAX ||
                job->predecessor3 != UINT32_MAX ||
                job->predecessor4 != UINT32_MAX)
                return 0u;
        } else if (i < 7u) {
            uint32_t lane = i - BR_DRUM_PART;
            uint32_t mask = 1u << lane;
            if (job->kind != BR_AUDIO_JOB_SOURCE || job->part != BR_DRUM_PART ||
                job->output != br_audio_kit_partial[0][lane] ||
                job->key != br_audio_kit_key_output(BR_DRUM_PART, lane, mask) ||
                job->lane_mask != mask || job->predecessor != UINT32_MAX ||
                job->predecessor2 != UINT32_MAX ||
                job->predecessor3 != UINT32_MAX ||
                job->predecessor4 != UINT32_MAX)
                return 0u;
        } else if (i == 7u) {
            if (job->kind != BR_AUDIO_JOB_KIT_FINALIZE ||
                job->part != BR_DRUM_PART || job->output != bus[BR_DRUM_PART] ||
                job->key || job->lane_mask != BR_DRUM_ALL_MASK ||
                job->predecessor != 3u || job->predecessor2 != 4u ||
                job->predecessor3 != 5u || job->predecessor4 != 6u)
                return 0u;
        } else if (job->kind == BR_AUDIO_JOB_SHARED_REVERB) {
            /* Optional, last, independent: the shared reverb of a split graph. */
            if (!split_mask || i != count - 1u || reverb_seen ||
                job->part != BR_DRUM_PART - 1u || job->output != bus[job->part] ||
                job->key || job->lane_mask != BR_DRUM_ALL_MASK ||
                job->predecessor != UINT32_MAX || job->predecessor2 != UINT32_MAX ||
                job->predecessor3 != UINT32_MAX || job->predecessor4 != UINT32_MAX)
                return 0u;
            reverb_seen = 1u;
        } else {
            /* Split tail: one effects job per split track, in track order,
             * depending only on that track's source job. */
            uint32_t p = job->part;
            if (job->kind != BR_AUDIO_JOB_TRACK_FX || p >= BR_DRUM_PART ||
                !((split_mask >> p) & 1u) || ((fx_mask >> p) & 1u) ||
                (fx_mask && p <= previous_fx_part) ||
                job->output != bus[p] || job->key ||
                job->lane_mask != BR_DRUM_ALL_MASK || job->predecessor != p ||
                job->predecessor2 != UINT32_MAX ||
                job->predecessor3 != UINT32_MAX ||
                job->predecessor4 != UINT32_MAX)
                return 0u;
            fx_mask |= 1u << p;
            previous_fx_part = p;
        }
    }
    return fx_mask == split_mask &&
        count == 8u + (uint32_t)__builtin_popcount(split_mask) + reverb_seen;
#else
    (void)bus; (void)n; (void)count; (void)deadline;
    return 0u;
#endif
}

static uint32_t br_audio_source_late_shape(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline, uint32_t all_complete)
{
    uint32_t i, claimed = 0u;
    if (!br_audio_source_graph_layout(bus, n, count, deadline) ||
        (br_audio_source_queue.fixed_plan_active && !all_complete) ||
        br_audio_source_queue.completed != count - (all_complete ? 0u : 1u))
        return 0u;
    /* Every job complete, or exactly one terminal job still claimed. */
    for (i = 0u; i < count; ++i) {
        uint32_t state = br_audio_source_jobs[i].state;
        if (state == 2u) continue;
        if (state != 1u || all_complete || claimed ||
            !br_audio_late_index_ok(i, count)) return 0u;
        claimed = 1u;
    }
    return all_complete || claimed;
}

/* Called with the queue unowned, after a confirmed source-worker join. */
static uint32_t br_audio_source_accept_complete_late(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline, const br_audio_source_late_t *late)
{
    if (!late || !br_audio_late_index_ok(late->index, count) ||
        br_audio_source_jobs[late->index].state != 1u ||
        br_audio_source_queue.lock ||
        (int32_t)(late->cutoff_tick - deadline) < 0 ||
        (int32_t)(late->cutoff_tick - late->returned_tick) < 0 ||
        !br_audio_source_late_shape(bus, n, count, deadline, 0u) ||
        !br_audio_call_fx_fusion_complete(bus, n)) return 0u;
    /* All other actors have joined and the lock is clear. Do not retry the
     * deadline-bound lock or execute the stateful callback a second time. */
    br_audio_source_publish();
    br_audio_source_jobs[late->index].state = 2u;
    br_audio_source_queue.completed = count;
    br_audio_source_publish();
    return 1u;
}

enum { BR_AUDIO_C1_PREFLIGHT_NONE = 0u, BR_AUDIO_C1_PREFLIGHT_CLAIMED = 1u,
       BR_AUDIO_C1_PREFLIGHT_COMPLETE = 2u, BR_AUDIO_C1_PREFLIGHT_TERMINAL = 3u,
       BR_AUDIO_C1_PREFLIGHT_INVALID = 4u };
/* Match the worker wait's independent finite guard if T4 stops. This caps
 * iterations; elapsed wall time still depends on core and interrupt timing. */
#ifndef BR_AUDIO_C1_PREFLIGHT_SPINS
#define BR_AUDIO_C1_PREFLIGHT_SPINS 0x500000u
#endif
/* The soft wait has ended. Retry only a busy lock until the same absolute
 * hard cutoff used by the worker wait. At the first expired observation,
 * allow one final lock try only if this worker has already completed. */
static uint32_t br_audio_source_c1_late_preflight(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline, uint32_t hard_deadline,
    const volatile uint32_t *done_seq, uint32_t sequence)
{
    uint32_t valid, spins = 0u;
    if (!done_seq || !sequence || hard_deadline - deadline != FM1_AUDIO_HALF_TICKS ||
        (int32_t)(br_audio_source_now() - deadline) < 0)
        return BR_AUDIO_C1_PREFLIGHT_INVALID;
    for (;;) {
        uint32_t now = br_audio_source_now();
        uint32_t expired = (int32_t)(now - hard_deadline) >= 0;
        if (expired) {
            br_audio_source_publish();
            if (*done_seq != sequence) return BR_AUDIO_C1_PREFLIGHT_TERMINAL;
        }
        if (fm1_fault_owner_try_acquire(&br_audio_source_queue.lock)) break;
        if (expired || ++spins >= BR_AUDIO_C1_PREFLIGHT_SPINS)
            return BR_AUDIO_C1_PREFLIGHT_TERMINAL;
    }
    /* Once acquired, a bad graph is an immediate refusal, never retried. */
    if (br_audio_source_late_shape(bus, n, count, deadline, 0u))
        valid = BR_AUDIO_C1_PREFLIGHT_CLAIMED;
    else if (br_audio_source_late_shape(bus, n, count, deadline, 1u) &&
             br_audio_call_fx_fusion_complete(bus, n) &&
             br_audio_source_jobs[7].executor_core <= 1u)
        valid = BR_AUDIO_C1_PREFLIGHT_COMPLETE;
    else valid = BR_AUDIO_C1_PREFLIGHT_INVALID;
    br_audio_source_queue_unlock();
    return valid;
}

#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_C1_LATE_COMPLETION_TEST)
static uint32_t br_audio_source_complete_watch_valid(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline, const fm1_cpu1_worker_mailbox_t *mailbox,
    uint32_t sequence, uint32_t status, uint32_t result, uint32_t faults)
{
#if BR_AUDIO_FUSION_FIXED_PLAN
    if (br_audio_source_queue.fixed_plan_active &&
        (result != BR_AUDIO_QUEUE_OK ||
         br_audio_source_queue.fixed_plan_completed[0] !=
             br_audio_source_queue.fixed_plan_count[0] ||
         br_audio_source_queue.fixed_plan_completed[1] !=
             br_audio_source_queue.fixed_plan_count[1] ||
         br_audio_source_queue.fixed_plan_cursor[0] !=
             br_audio_source_queue.fixed_plan_count[0] ||
         br_audio_source_queue.fixed_plan_cursor[1] !=
             br_audio_source_queue.fixed_plan_count[1])) return 0u;
#endif
    if ((result != BR_AUDIO_QUEUE_OK &&
         result != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
         result != BR_AUDIO_QUEUE_CUTOFF_DRAINED) ||
        !br_audio_source_late_join_valid(mailbox, sequence, status, result,
                                         result, faults) ||
        br_audio_source_queue.lock ||
        !br_audio_source_late_shape(bus, n, count, deadline, 1u) ||
        !br_audio_call_fx_fusion_complete(bus, n) ||
        br_audio_source_jobs[7].executor_core > 1u) return 0u;
#if defined(BR_C1_LATE_COMPLETION_TEST)
    { extern void br_audio_source_test_validation_boundary(void);
      br_audio_source_test_validation_boundary(); }
#endif
    return 1u;
}

/* Post-join validation for a complete or intentionally discarded partial
 * eight-job frame. This never reads any audio sample or advances DSP state. */
static uint32_t br_audio_source_joined_graph_valid(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline, const fm1_cpu1_worker_mailbox_t *mailbox,
    uint32_t sequence, uint32_t status, uint32_t result, uint32_t faults,
    uint32_t *completed_out)
{
    uint32_t i, completed = 0u, mask = 0u, per_core[2] = {0u, 0u};
    if (!completed_out || br_audio_source_publication_enabled != 1u ||
        (result != BR_AUDIO_QUEUE_OK &&
         result != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
         result != BR_AUDIO_QUEUE_CUTOFF_DRAINED) ||
        !br_audio_source_late_join_valid(mailbox, sequence, status, result,
                                        result, faults) ||
        br_audio_source_queue.lock ||
        !br_audio_source_graph_layout(bus, n, count, deadline)) return 0u;
    for (i = 0u; i < count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[i];
        if (job->state == 0u) {
            if (job->executor_core != UINT32_MAX || job->duration_ticks != 0u)
                return 0u;
        } else if (job->state == 2u) {
            uint32_t core = job->executor_core;
            if (core > 1u || !br_audio_job_dependencies_done(job, count))
                return 0u;
            ++completed;
            ++per_core[core];
            /* A track's effects ran in its fused job or its split effects job. */
            if (job->kind == BR_AUDIO_JOB_FUSED_TRACK ||
                job->kind == BR_AUDIO_JOB_TRACK_FX) mask |= 1u << job->part;
            else if (i == 7u) mask |= 1u << BR_DRUM_PART;
        } else return 0u; /* C1 or C0 still owns a claimed job */
#if BR_AUDIO_FUSION_FIXED_PLAN
        if (br_audio_source_queue.fixed_plan_active) {
            uint32_t core = job->fixed_plan_core;
            uint32_t cursor;
            if (core > 1u) return 0u;
            cursor = br_audio_source_queue.fixed_plan_cursor[core];
            if ((job->state == 2u &&
                 (job->executor_core != core || job->fixed_plan_order >= cursor)) ||
                (job->state == 0u && job->fixed_plan_order < cursor)) return 0u;
        }
#endif
    }
#if BR_AUDIO_FUSION_FIXED_PLAN
    if (br_audio_source_queue.fixed_plan_active) {
        for (i = 0u; i < 2u; ++i)
            if (br_audio_source_queue.fixed_plan_cursor[i] != per_core[i] ||
                br_audio_source_queue.fixed_plan_completed[i] != per_core[i] ||
                per_core[i] > br_audio_source_queue.fixed_plan_count[i]) return 0u;
        if (br_audio_source_queue.completed != (completed == count ? count : 0u))
            return 0u;
    } else
#endif
    if (br_audio_source_queue.completed != completed) return 0u;
    if (!br_audio_call_fx_fusion_matches_mask(bus, n, mask)) return 0u;
#if defined(BR_C1_LATE_COMPLETION_TEST)
    { extern void br_audio_source_test_validation_boundary(void);
      br_audio_source_test_validation_boundary(); }
#endif
    *completed_out = completed;
    return 1u;
}

static void br_audio_source_drop_partial(uint32_t count, uint32_t deadline,
                                         uint32_t sequence, uint32_t observed)
{
    uint32_t p;
    br_audio_source_fusion_cancel();
    /* Some post jobs did not run, so their old quiet flags are not evidence
     * that the newly consumed events left those parts quiet. */
    for (p = 0u; p < BR_PARTS; ++p) br_quiet_part[p] = 0u;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    br_audio_late_last_index = UINT32_MAX;
    br_audio_late_last_count = count;
    br_audio_late_last_deadline = deadline;
    br_audio_late_last_return = UINT32_MAX;
    br_audio_late_last_sequence = sequence;
    br_audio_late_last_cutoff = observed;
    br_audio_late_last_core = UINT32_MAX;
    ++br_audio_late_drop_count;
    ++br_audio_partial_drop_count;
#else
    (void)count; (void)deadline; (void)sequence; (void)observed;
#endif
    br_audio_late_render = 1u;
}

/* C0 has already returned from its successful finalizer. This check only
 * permits waiting for C1's matching ACK; publication and fusion are still
 * validated after that ACK, with the queue lock clear. */
static uint32_t br_audio_source_c0_late_ack_ready(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n, uint32_t count,
    uint32_t deadline, const fm1_cpu1_worker_mailbox_t *mailbox,
    uint32_t sequence, const br_audio_source_late_t *late)
{
    uint32_t now = br_audio_source_now();
    if (!mailbox || !sequence || mailbox->cpu_id != 1u ||
        mailbox->request_seq != sequence ||
        mailbox->task_id != FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH ||
        mailbox->task_context != (uint32_t)(uintptr_t)&br_audio_source_queue ||
        !late || !br_audio_late_index_ok(late->index, count) ||
        (int32_t)(late->returned_tick - (deadline - FM1_AUDIO_HALF_TICKS)) < 0 ||
        (int32_t)(late->cutoff_tick - deadline) < 0 ||
        (int32_t)(late->cutoff_tick - late->returned_tick) < 0 ||
        (int32_t)(now - late->cutoff_tick) < 0 ||
        br_audio_source_jobs[late->index].executor_core != 0u ||
        br_audio_source_jobs[late->index].state != 1u ||
        !br_audio_source_late_shape(bus, n, count, deadline, 0u) ||
        !br_audio_call_fx_fusion_complete(bus, n)) return 0u;
    return 1u;
}

#if BR_AUDIO_FUSION_FIXED_PLAN
/* This only permits a bounded mailbox wait. It never consumes a bus: after
 * the matching join the ordinary fixed-plan seal and complete-watch still
 * validate every job, cursor, result and fusion output. */
static uint32_t br_audio_source_fixed_published_ready(
    uint32_t n, uint32_t count, uint32_t deadline,
    const fm1_cpu1_worker_mailbox_t *mailbox, uint32_t sequence)
{
    uint32_t i, c1_count, c1_completed, c1_cursor;
    if (!mailbox || !sequence || mailbox->cpu_id != 1u ||
        mailbox->request_seq != sequence ||
        mailbox->task_id != FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH ||
        mailbox->task_context != (uint32_t)(uintptr_t)&br_audio_source_queue ||
        n != BR_BLOCK || BR_BLOCK != FM1_AUDIO_HALF_FRAMES || count != 8u ||
        !br_audio_source_fx_fusion_active || br_pipeline_enabled ||
        br_audio_source_queue.fixed_plan_active != 1u ||
        br_audio_source_queue.count != count ||
        br_audio_source_queue.deadline_tick != deadline ||
        br_audio_source_queue.lock) return 0u;
    br_audio_source_publish();
    if (br_audio_source_queue.fixed_plan_count[0] +
            br_audio_source_queue.fixed_plan_count[1] != count ||
        !br_audio_worker_source_queue_context_valid(&br_audio_source_queue) ||
        br_audio_source_queue.fixed_plan_completed[0] !=
            br_audio_source_queue.fixed_plan_count[0] ||
        br_audio_source_queue.fixed_plan_cursor[0] !=
            br_audio_source_queue.fixed_plan_count[0])
        return 0u;
    for (i = 0u; i < count; ++i)
        if (br_audio_source_jobs[i].state != 2u ||
            br_audio_source_jobs[i].frames != n ||
            br_audio_source_jobs[i].kind >= BR_AUDIO_JOB_KIND_COUNT) return 0u;
    /* C1 can publish its final count and cursor between these reads. Both
     * snapshots may independently be final or one behind; the joined seal
     * will require their exact final values before output is considered. */
    c1_count = br_audio_source_queue.fixed_plan_count[1];
    c1_completed = br_audio_source_queue.fixed_plan_completed[1];
#if defined(BR_C1_LATE_COMPLETION_TEST)
    { extern void br_audio_source_test_fixed_snapshot_boundary(void);
      br_audio_source_test_fixed_snapshot_boundary(); }
#endif
    c1_cursor = br_audio_source_queue.fixed_plan_cursor[1];
    if (c1_completed > c1_count || c1_cursor > c1_count ||
        c1_count - c1_completed > 1u || c1_count - c1_cursor > 1u)
        return 0u;
    br_audio_source_publish();
    return br_audio_source_queue.fixed_plan_active == 1u &&
        br_audio_source_queue.count == count && !br_audio_source_queue.lock;
}
#endif
#endif

static uint32_t br_audio_source_record_late(
    br_audio_source_late_t *late, uint32_t index, uint32_t count,
    uint32_t returned, uint32_t observed)
{
    if (!late || !br_audio_graph_count_ok(count) ||
        !br_audio_late_index_ok(index, count) ||
        (int32_t)(observed - br_audio_source_queue.deadline_tick) < 0)
        return BR_AUDIO_QUEUE_FAILED;
    late->index = index;
    late->returned_tick = returned;
    late->cutoff_tick = observed;
    return BR_AUDIO_QUEUE_COMPLETE_LATE;
}

/* Only submitted, full-half fused queues have a post-join complete-watch.
 * Returning success here is provisional: CPU0 must still acquire the exact
 * matching C1 completion and validate the full graph and fusion state before
 * it can consume the bus. No new work is claimed by this check. */
static uint32_t br_audio_source_completion_watch_eligible(
    const br_audio_source_late_t *late_out)
{
    return late_out && BR_AUDIO_C1_LATE_OWNER_KNOWN &&
        BR_AUDIO_FUSION_ONLY_BUILD && BR_AUDIO_FUSION_FOUR_WAY_KIT &&
        BR_BLOCK == FM1_AUDIO_HALF_FRAMES &&
        br_audio_source_fx_fusion_active && !br_pipeline_enabled &&
        !br_audio_source_queue.fixed_plan_active &&
        br_audio_graph_count_ok(br_audio_source_queue.count) &&
        br_audio_source_jobs[7].frames == BR_BLOCK;
}

/* The completion count advances under the queue lock only after a callback
 * returns success. Observe its publication before checking every state and
 * immutable descriptor. The later joined complete-watch is still required. */
static uint32_t br_audio_source_published_complete(
    const br_audio_source_late_t *late_out)
{
    uint32_t i, count = br_audio_source_queue.count;
    if (!br_audio_source_completion_watch_eligible(late_out) ||
        br_audio_source_queue.completed != count) return 0u;
    br_audio_source_publish();
    if (!br_audio_worker_source_queue_context_valid(&br_audio_source_queue))
        return 0u;
    for (i = 0u; i < count; ++i)
        if (br_audio_source_jobs[i].state != 2u ||
            br_audio_source_jobs[i].frames != BR_BLOCK ||
            br_audio_source_jobs[i].kind >= BR_AUDIO_JOB_KIND_LIMIT) return 0u;
    br_audio_source_publish();
    return br_audio_source_queue.count == count && br_audio_graph_count_ok(count) &&
        br_audio_source_queue.completed == count;
}

/* This status is used only at a deadline exit where this core has neither a
 * claimed callback nor a completion awaiting publication. It is provisional:
 * the owner must still join and validate the exact graph or late record. */
static uint32_t br_audio_source_cutoff_drained(
    const br_audio_source_late_t *late_out)
{
    if (br_audio_source_published_complete(late_out))
        return BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
    if (br_audio_source_completion_watch_eligible(late_out) &&
        br_audio_source_queue.completed < br_audio_source_queue.count)
        return BR_AUDIO_QUEUE_CUTOFF_DRAINED;
    /* It may have reached 8/8 while the first snapshot was being read. */
    if (br_audio_source_published_complete(late_out))
        return BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
    return BR_AUDIO_QUEUE_FAILED;
}

/* late_out is supplied only by the joined source-worker queue path. */
static uint32_t br_audio_source_queue_run_internal(
    uint32_t wait_for_work, br_audio_source_late_t *late_out)
{
#if BR_AUDIO_FUSION_FIXED_PLAN
    if (br_audio_source_queue.fixed_plan_active)
        return br_audio_source_fixed_plan_run();
#endif
    for (;;) {
        uint32_t count, selected, deadline_hit = 0u;
#if BR_AUDIO_FUSION_EWMA_SCHEDULER || BR_AUDIO_FUSION_FIXED_PLAN
        uint32_t current_core = br_audio_job_timing_core();
#else
        uint32_t current_core = 0u;
#endif
        if (br_audio_source_deadline_expired() || !br_audio_source_queue_lock())
            return br_audio_source_cutoff_drained(late_out);
        count = br_audio_source_queue.count;
        if (br_audio_source_deadline_expired()) {
            br_audio_source_queue_unlock();
            return br_audio_source_cutoff_drained(late_out);
        }
        if (br_audio_source_queue.completed >= count) {
            uint32_t exact = br_audio_source_queue.completed == count;
            br_audio_source_queue_unlock();
            return exact;
        }
        selected = br_audio_source_queue_select_ready(count, current_core,
                                                       &deadline_hit);
        if (deadline_hit) {
            br_audio_source_queue_unlock();
            return br_audio_source_cutoff_drained(late_out);
        }
#if BR_AUDIO_FUSION_FAST_READY_SCAN
        /* The graph is capped at BR_AUDIO_MAX_SOURCE_JOBS. One deadline read
         * after this bounded scan keeps the claim cutoff while avoiding a
         * timer MMIO read for every ready candidate under the queue lock. */
        if (br_audio_source_fx_fusion_active && selected != UINT32_MAX &&
            br_audio_source_deadline_expired()) {
            br_audio_source_queue_unlock();
            return br_audio_source_cutoff_drained(late_out);
        }
#endif
        if (selected != UINT32_MAX) br_audio_source_jobs[selected].state = 1u;
        br_audio_source_queue_unlock();
        if (selected != UINT32_MAX) {
#if defined(BR_SOURCE_QUEUE_TEST)
            br_audio_source_test_begin(selected);
#endif
            if (!br_audio_worker_sources(&br_audio_source_jobs[selected])) {
#if defined(BR_SOURCE_QUEUE_TEST)
                br_audio_source_test_end(selected);
#endif
                return 0u;
            }
#if defined(BR_SOURCE_QUEUE_TEST)
            br_audio_source_test_end(selected);
#endif
            if (br_audio_source_publication_enabled && late_out) {
                uint32_t published_last;
                uint32_t hard_deadline = br_audio_source_queue.deadline_tick +
                                         FM1_AUDIO_HALF_TICKS;
                if (!br_audio_source_retire_lock(hard_deadline)) return 0u;
                if (br_audio_source_queue.count != count ||
                    br_audio_source_queue.completed >= count ||
                    br_audio_source_jobs[selected].state != 1u) {
                    br_audio_source_queue_unlock();
                    return 0u;
                }
                br_audio_source_jobs[selected].state = 2u;
                ++br_audio_source_queue.completed;
                published_last = br_audio_source_queue.completed == count;
                br_audio_source_queue_unlock();
                if (br_audio_source_deadline_expired())
                    return published_last ? BR_AUDIO_QUEUE_PUBLISHED_COMPLETE :
                                            BR_AUDIO_QUEUE_CUTOFF_DRAINED;
                if (published_last) return BR_AUDIO_QUEUE_OK;
                continue;
            }
            /* This is the existing post-callback timer read, retained as the
             * return timestamp. Only the observed final C0 kit job is eligible
             * for the separate, joined validation path. */
#if defined(FM1_DEVICE) || defined(BR_SOURCE_QUEUE_TEST)
            uint32_t returned_tick = br_audio_source_now();
            if ((int32_t)(returned_tick - br_audio_source_queue.deadline_tick) >= 0) {
                return br_audio_source_record_late(late_out, selected, count,
                                                    returned_tick, returned_tick);
            }
            if (!br_audio_source_queue_lock())
                return br_audio_source_record_late(late_out, selected, count,
                                                    returned_tick, br_audio_source_now());
            uint32_t cutoff_tick = br_audio_source_now();
            if ((int32_t)(cutoff_tick - br_audio_source_queue.deadline_tick) >= 0) {
                br_audio_source_queue_unlock();
                return br_audio_source_record_late(late_out, selected, count,
                                                    returned_tick, cutoff_tick);
            }
#else
            if (!br_audio_source_queue_lock()) return 0u;
#endif
            br_audio_source_jobs[selected].state = 2u;
            ++br_audio_source_queue.completed;
            uint32_t published_last = br_audio_source_queue.completed == count;
            br_audio_source_queue_unlock();
            if (published_last && br_audio_source_completion_watch_eligible(late_out))
                return BR_AUDIO_QUEUE_PUBLISHED_COMPLETE;
            continue;
        }
        if (!wait_for_work) return 1u;
        if (br_audio_source_deadline_expired())
            return br_audio_source_cutoff_drained(late_out);
        /* A claimed predecessor may be running on the other core. Re-scan
         * until its completion is published or the shared T4 deadline ends. */
    }
}

static uint32_t br_audio_source_queue_run(uint32_t wait_for_work)
{
    return br_audio_source_queue_run_internal(wait_for_work, 0);
}

#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_C1_LATE_COMPLETION_TEST)
/* C1 writes this private record only on successful late callback completion;
 * the worker mailbox publishes its result/sequence after this function exits. */
static uint32_t br_audio_source_c1_late_publish(
    const fm1_cpu1_worker_mailbox_t *mailbox, uint32_t cpu,
    const br_audio_source_late_t *late)
{
    uint32_t sequence;
    if (!mailbox || cpu != 1u || mailbox->cpu_id != 1u || !late ||
        !br_audio_late_index_ok(late->index, br_audio_source_queue.count) ||
        br_audio_c1_late_record.valid ||
        mailbox->task_id != FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH ||
        mailbox->task_context != (uint32_t)(uintptr_t)&br_audio_source_queue ||
        !(sequence = mailbox->request_seq) || mailbox->done_seq == sequence)
        return BR_AUDIO_QUEUE_FAILED;
    br_audio_c1_late_record.index = late->index;
    br_audio_c1_late_record.returned_tick = late->returned_tick;
    br_audio_c1_late_record.cutoff_tick = late->cutoff_tick;
    br_audio_c1_late_record.deadline = br_audio_source_queue.deadline_tick;
    br_audio_c1_late_record.sequence = sequence;
    br_audio_source_publish();
    br_audio_c1_late_record.valid = BR_AUDIO_C1_LATE_VALID;
    br_audio_source_publish();
    return BR_AUDIO_QUEUE_COMPLETE_LATE;
}

/* Called only after the matching mailbox sequence has joined successfully. */
static uint32_t br_audio_source_c1_late_read(uint32_t sequence,
    uint32_t deadline, uint32_t joined_tick, br_audio_source_late_t *late)
{
    if (!late || br_audio_c1_late_record.valid != BR_AUDIO_C1_LATE_VALID ||
        br_audio_c1_late_record.sequence != sequence ||
        br_audio_c1_late_record.deadline != deadline) return 0u;
    late->index = br_audio_c1_late_record.index;
    late->returned_tick = br_audio_c1_late_record.returned_tick;
    late->cutoff_tick = br_audio_c1_late_record.cutoff_tick;
    /* The job belongs to this queue interval and cannot finish after the
     * observed join. The hard wait rejects an absent completion at its first
     * cutoff observation, but accepts one already visible on that boundary. */
    return br_audio_late_index_ok(late->index, br_audio_source_queue.count) &&
        (int32_t)(late->returned_tick - (deadline - FM1_AUDIO_HALF_TICKS)) >= 0 &&
        (int32_t)(late->cutoff_tick - deadline) >= 0 &&
        (int32_t)(late->cutoff_tick - late->returned_tick) >= 0 &&
        (int32_t)(joined_tick - late->cutoff_tick) >= 0;
}
#endif

/* Select and run the CPU0 source path for the current audio frame. Reverse
 * mode runs the complete source queue on CPU0 while the previous-frame FX job
 * occupies CPU1, then returns immediately so the legacy fallback cannot run
 * those stateful source jobs a second time. */
static uint32_t br_audio_source_run_direct_legacy(void)
{
    uint32_t i;
    for (i = 0; i < br_audio_source_queue.count; ++i) {
        uint32_t completed;
        if (br_audio_source_deadline_expired()) return 0u;
#if defined(BR_SOURCE_QUEUE_TEST)
        br_audio_source_test_begin(i);
#endif
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        if (br_audio_runtime_trace_chunk_active)
            fm1_runtime_trace_job_begin(i, 0u);
#endif
        completed = br_audio_worker_sources(&br_audio_source_jobs[i]);
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        if (br_audio_runtime_trace_chunk_active)
            fm1_runtime_trace_job_end(i, 0u);
#endif
#if defined(BR_SOURCE_QUEUE_TEST)
        br_audio_source_test_end(i);
#endif
        if (!completed || br_audio_source_deadline_expired()) return 0u;
    }
    return 1u;
}

static uint32_t br_audio_source_dispatch_local(uint32_t reverse_pipeline,
    uint32_t use_cpu1, uint32_t submitted)
{
    if (reverse_pipeline) return br_audio_source_queue_run(1u);
    if (use_cpu1 && submitted) return 1u;
    if (br_audio_source_legacy_mode && !br_audio_source_fx_fusion_active)
        return br_audio_source_run_direct_legacy();
    return br_audio_source_queue_run(br_audio_source_queue_wait_policy());
}

uint32_t br_audio_worker_source_batch(void *opaque_batch)
{
    if (opaque_batch == &br_audio_legacy_batches[1]) {
        br_audio_source_batch_t *batch = (br_audio_source_batch_t *)opaque_batch;
        uint32_t i;
        if (!br_audio_source_legacy_mode || !batch->count ||
            batch->count > BR_AUDIO_MAX_SOURCE_JOBS) return 0u;
        for (i = 0; i < batch->count; ++i) {
            uint32_t index = batch->job_index[i];
            if (br_audio_source_deadline_expired() ||
                index >= br_audio_source_queue.count ||
                index >= BR_AUDIO_MAX_SOURCE_JOBS) return 0u;
#if defined(BR_SOURCE_QUEUE_TEST)
            br_audio_source_test_begin(index);
#endif
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
            if (br_audio_runtime_trace_chunk_active)
                fm1_runtime_trace_job_begin(index, 1u);
#endif
            uint32_t completed = br_audio_worker_sources(&br_audio_source_jobs[index]);
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
            if (br_audio_runtime_trace_chunk_active)
                fm1_runtime_trace_job_end(index, 1u);
#endif
#if defined(BR_SOURCE_QUEUE_TEST)
            br_audio_source_test_end(index);
#endif
            if (!completed || br_audio_source_deadline_expired()) return 0u;
        }
        return 1u;
    }
    /* Only the resident queue context is valid; job descriptors and callback
     * addresses always come from this translation unit's static storage. */
    if (opaque_batch != &br_audio_source_queue ||
        !br_audio_source_queue.count ||
        br_audio_source_queue.count > BR_AUDIO_MAX_SOURCE_JOBS) return 0u;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO) && BR_AUDIO_C1_LATE_OWNER_KNOWN
    {
        br_audio_source_late_t late = {UINT32_MAX, 0u, 0u};
        uint32_t rc = br_audio_source_queue_run_internal(
            br_audio_source_queue_wait_policy(), &late);
        if (rc != BR_AUDIO_QUEUE_COMPLETE_LATE) return rc;
        return br_audio_source_c1_late_publish(&fm1_cpu1_worker_mailbox,
            fm1_fault_diag_read_cpu_id(), &late);
    }
#elif defined(BR_C1_LATE_COMPLETION_TEST) && BR_AUDIO_C1_LATE_OWNER_KNOWN
    {
        extern const fm1_cpu1_worker_mailbox_t *br_audio_source_test_c1_mailbox(void);
        br_audio_source_late_t late = {UINT32_MAX, 0u, 0u};
        uint32_t rc = br_audio_source_queue_run_internal(
            br_audio_source_queue_wait_policy(), &late);
        if (rc != BR_AUDIO_QUEUE_COMPLETE_LATE) return rc;
        return br_audio_source_c1_late_publish(br_audio_source_test_c1_mailbox(),
                                               br_audio_job_timing_core(), &late);
    }
#else
    return br_audio_source_queue_run(br_audio_source_queue_wait_policy());
#endif
}

static uint32_t br_audio_source_job_cost(uint32_t index)
{
    uint32_t cost;
#if defined(FM1_DEVICE)
    const br_audio_source_job_t *job = &br_audio_source_jobs[index];
    uint32_t slot = br_audio_kit_job_slot(job->lane_mask);
    cost = br_diagnostic_source_cost[job->part][slot];
#elif defined(BR_SOURCE_BATCH_TEST)
    const br_audio_source_job_t *job = &br_audio_source_jobs[index];
    uint32_t slot = br_audio_kit_job_slot(job->lane_mask);
    cost = br_audio_source_batch_test_cost[job->part][slot];
#elif defined(BR_SOURCE_QUEUE_TEST) && !defined(FM1_DEVICE) && \
    BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR
    const br_audio_source_job_t *job = &br_audio_source_jobs[index];
    uint32_t slot = br_audio_kit_job_slot(job->lane_mask);
    cost = br_audio_source_queue_test_cost[job->part][slot];
#else
    (void)index;
    return 1024u;
#endif
    if (cost < 1024u) cost = 1024u;
#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR
    {
        const br_audio_source_job_t *job = &br_audio_source_jobs[index];
        const uint32_t slot = br_audio_kit_job_slot(job->lane_mask);
        if (job->part < BR_PARTS && br_mode[job->part] == 4u) {
            uint8_t next_lane_units[BR_DRUM_LANES] = {0u, 0u, 0u, 0u};
            uint8_t next_profile = 0u, previous_profile = 0u;
            uint32_t nonproportional = 0u, signature = 0u, eligible = 0u;
#if (defined(FM1_DEVICE) || defined(BR_SOURCE_QUEUE_TEST)) && \
    defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
            const br_kit_source_controls_t *controls =
                &br_audio_kit_source_controls[job->part];
            previous_profile =
                br_audio_plaits_prev_append_profile[job->part][slot];
            /* The predictor models one bounded native call. A 64-frame
             * scheduler job is rendered as two <=32-frame calls, so keep
             * using the measured base cost until a multi-call predictor is
             * available rather than extrapolating from the first half. */
            if (job->frames <= BR_AUDIO_NATIVE_RENDER_MAX) {
                (void)plaits_kit_predict_append_units_ex(
                    br_plaits_kit_job_state(job->part, job->lane_mask),
                    &br_effective_sound[job->part], controls->gate_frames,
                    job->lane_mask, job->frames, &nonproportional, &signature,
                    next_lane_units);
            } else {
                nonproportional = 1u;
            }
            next_profile = br_audio_plaits_pack_units(next_lane_units, slot);
            eligible = !nonproportional &&
                br_audio_plaits_prev_cost_valid[job->part][slot] &&
                br_audio_plaits_prev_cost_signature[job->part][slot] == signature;
            br_audio_plaits_current_cost_signature[job->part][slot] = signature;
            br_audio_plaits_current_cost_valid[job->part][slot] =
                (uint8_t)(nonproportional == 0u);
#elif defined(BR_SOURCE_BATCH_TEST)
            previous_profile =
                br_audio_source_batch_test_prev_profile[job->part][slot];
            next_profile =
                br_audio_source_batch_test_next_profile[job->part][slot];
            eligible = br_audio_source_batch_test_signature_match[job->part][slot] &&
                !br_audio_source_batch_test_nonproportional[job->part][slot];
#endif
            cost = br_audio_plaits_cost_for_profiles(
                cost, previous_profile, next_profile, eligible);
        }
    }
#endif
    return cost;
}

#if BR_AUDIO_FUSION_EWMA_SCHEDULER
static uint32_t br_audio_fusion_history_slot(const br_audio_source_job_t *job)
{
    if (!job || job->kind >= BR_AUDIO_JOB_KIND_LIMIT) return UINT32_MAX;
    if (job->kind == BR_AUDIO_JOB_SOURCE) {
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
        uint32_t lane;
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
            if (job->lane_mask == (1u << lane)) return lane;
#endif
        if (job->lane_mask == BR_KIT_LANES_A) return 0u;
        if (job->lane_mask == BR_KIT_LANES_B) return 1u;
        return UINT32_MAX;
    }
    if (job->kind == BR_AUDIO_JOB_FUSED_TRACK) return 2u;
    /* Synth tracks never use the kit's lane slots 0 and 1. */
    if (job->kind == BR_AUDIO_JOB_TRACK_SOURCE) return 0u;
    if (job->kind == BR_AUDIO_JOB_TRACK_FX) return 1u;
    if (job->kind == BR_AUDIO_JOB_SHARED_REVERB) return 3u;
    if (job->kind == BR_AUDIO_JOB_KIT_FINALIZE)
        return BR_AUDIO_FUSION_HISTORY_SLOTS - 1u;
    return UINT32_MAX;
}

#if BR_AUDIO_FUSION_ACTIVITY_EWMA
/* Called on CPU0 after events and controls are applied but before publishing
 * the queue. This captures workload class without reading worker-owned state
 * after dispatch. The kit finalizer's reduction and FX work is present in both
 * quiet and active blocks, so it remains in the fixed-work history class. */
static uint32_t br_audio_fusion_job_activity_class(
    const br_audio_source_job_t *job)
{
    uint32_t p;
    if (!job || job->part >= BR_PARTS) return 1u;
    p = job->part;
    if (job->kind == BR_AUDIO_JOB_KIT_FINALIZE) return 1u;
#if BR_AUDIO_FUSION_KIT_LANE_ACTIVITY_CLASS
    /* A shared ADSR is not evidence that every source lane did work. Test
     * lane-specific source activity first, while leaving fused-track jobs on
     * their historical envelope/model classification below. */
    if (job->kind == BR_AUDIO_JOB_SOURCE && br_mode[p] == 1u) {
#if BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY
        if (br_effective_sound[p].preset == BR_KIT_BRAIDS) {
            uint32_t lane;
            const void *state = br_drum_pair_state(p, job->lane_mask);
            for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
                uint32_t engine_work = 0u;
                if (!(job->lane_mask & (1u << lane))) continue;
                (void)br_drum_lane_scheduler_state(
                    state, &br_effective_sound[p], lane, &engine_work, 0);
                if (engine_work) return 1u;
            }
            return 0u;
        }
#endif
        return br_drum_quiet_mask(br_drum_pair_state(p, job->lane_mask),
                                  job->lane_mask) ? 0u : 1u;
    }
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    if (job->kind == BR_AUDIO_JOB_SOURCE && br_mode[p] == 4u) {
        uint32_t lane;
        void *state = br_plaits_kit_job_state(p, job->lane_mask);
        if (plaits_kit_quiet_mask(state, job->lane_mask)) return 0u;
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            uint32_t engine_work = 0u;
            if (!(job->lane_mask & (1u << lane))) continue;
            (void)plaits_kit_lane_scheduler_state(
                state, &br_effective_sound[p], lane, &engine_work, 0);
            if (engine_work) return 1u;
        }
        /* Active bookkeeping with a voice-mask change renders only its short
         * output fade; tail_fade is likewise a small output-only path. */
        return 0u;
    }
#endif
#endif
    if (br_env[p].stage != BR_ENV_OFF) return 1u;
    if (job->kind == BR_AUDIO_JOB_SOURCE) {
        if (br_mode[p] == 1u)
            return br_drum_quiet_mask(br_drum_pair_state(p, job->lane_mask),
                                      job->lane_mask) ? 0u : 1u;
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        if (br_mode[p] == 4u)
            return plaits_kit_quiet_mask(
                br_plaits_kit_job_state(p, job->lane_mask),
                job->lane_mask) ? 0u : 1u;
#endif
    } else if (job->kind == BR_AUDIO_JOB_FUSED_TRACK ||
               job->kind == BR_AUDIO_JOB_TRACK_SOURCE) {
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        if (br_mode[p] == 3u)
            return plaits_quiet(br_dsp[p].plaits) ? 0u : 1u;
#endif
        /* Mode 0 does no oscillator render while its envelope is off. */
        if (br_mode[p] == 0u) return 0u;
    }
    /* Unknown or future work stays measured rather than being mislabeled
     * quiet and excluded from the active-cost estimator. */
    return 1u;
}

static uint32_t br_audio_fusion_activity_floor(uint32_t frames)
{
    uint32_t chunks = frames / BR_AUDIO_NATIVE_RENDER_MAX +
        ((frames % BR_AUDIO_NATIVE_RENDER_MAX) != 0u);
    if (!chunks) chunks = 1u;
    return chunks > UINT32_MAX / 1024u ? UINT32_MAX : chunks * 1024u;
}

#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
/* Conservative first-use floors scale with native render chunks rather than
 * this particular scene's preset or controls. These bounds cover the active
 * job classes observed in target236 and leave headroom for cost variation. */
static uint32_t br_audio_fusion_robust_plaits_source_chunk(uint32_t preset)
{
    uint32_t model;
    if (preset < BR_FIRST_PLAITS ||
        preset >= BR_FIRST_PLAITS + BR_PLAITS_MODEL_COUNT)
        return 4096u;
    model = preset - BR_FIRST_PLAITS;
    /* Distinguish broad engine families so the cold estimate does not erase
     * the large Modal/Harmonic/Particle cost differences after learning. */
    if (model == 20u) return 6400u; /* Modal */
    if (model == 12u) return 3500u; /* Harmonic */
    if (model == 18u) return 3800u; /* Particle */
    return 4096u;
}

static uint32_t br_audio_fusion_robust_activity_floor(
    const br_audio_source_job_t *job)
{
    uint32_t chunks, per_chunk = 1024u;
    if (!job || job->part >= BR_PARTS || !job->frames ||
        job->frames > BR_BLOCK) return UINT32_MAX;
    chunks = job->frames / BR_AUDIO_NATIVE_RENDER_MAX +
        ((job->frames % BR_AUDIO_NATIVE_RENDER_MAX) != 0u);
    if (!chunks) chunks = 1u;
    if (job->activity_class) {
        if (job->kind == BR_AUDIO_JOB_FUSED_TRACK ||
            job->kind == BR_AUDIO_JOB_TRACK_SOURCE) {
            /* Model-family prior plus a conservative allowance for active
             * FX stages. */
            uint32_t source_chunk = br_mode[job->part] == 3u ?
                br_audio_fusion_robust_plaits_source_chunk(
                    br_effective_sound[job->part].preset) : 4096u;
            const br_sound_t *sound = &br_effective_sound[job->part];
            const br_fx_config_t *fx = &br_effective_fx[job->part];
            uint32_t fx_stages = (sound->delay ? 1u : 0u) +
                (sound->reverb ? 1u : 0u) +
                (fx->mod_mix ? 1u : 0u);
            if (job->kind == BR_AUDIO_JOB_TRACK_SOURCE) fx_stages = 0u;
            per_chunk = source_chunk + 512u + fx_stages * 768u;
        } else if (job->kind == BR_AUDIO_JOB_TRACK_FX) {
            const br_sound_t *sound = &br_effective_sound[job->part];
            const br_fx_config_t *fx = &br_effective_fx[job->part];
            uint32_t fx_stages = (sound->delay ? 1u : 0u) +
                (sound->reverb ? 1u : 0u) + (fx->mod_mix ? 1u : 0u) +
                (sound->drive ? 1u : 0u);
            per_chunk = 512u + fx_stages * 768u;
        } else if (job->kind == BR_AUDIO_JOB_SOURCE) {
            per_chunk = br_mode[job->part] == 4u ? 3584u : 2816u;
#if BR_AUDIO_FUSION_BRAIDS_COLD_FLOOR
            if (br_mode[job->part] == 1u &&
                br_effective_sound[job->part].preset == BR_KIT_BRAIDS)
                per_chunk = 3584u;
#endif
        } else if (job->kind == BR_AUDIO_JOB_KIT_FINALIZE) {
            const br_sound_t *sound = &br_effective_sound[job->part];
            const br_fx_config_t *fx = &br_effective_fx[job->part];
            uint32_t fx_stages = (sound->delay ? 1u : 0u) +
                (sound->reverb ? 1u : 0u) +
                (fx->mod_mix ? 1u : 0u);
            per_chunk = 512u + fx_stages * 768u;
        }
    }
    return chunks > UINT32_MAX / per_chunk ? UINT32_MAX : chunks * per_chunk;
}

static uint32_t br_audio_fusion_apply_robust_floor(
    const br_audio_source_job_t *job, uint32_t cost)
{
    uint32_t floor = br_audio_fusion_robust_activity_floor(job);
    return cost < floor ? floor : cost;
}
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
/* After a matching signature has observations on a core, use its measured
 * high-water plus ceil(1/divisor) padding instead of retaining the cold prior.
 * The peak has no decay: controller/note variation within this coarse
 * signature remains represented until a signature reset. */
static uint32_t br_audio_fusion_guarded_history_cost(
    const br_audio_fusion_job_history_t *history, uint32_t core)
{
    uint32_t base, padding;
    if (!history || core >= 2u || !history->samples[core] ||
        !history->peak_ticks[core]) return 0u;
#if BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
    if (history->samples[core] <
        BR_AUDIO_FUSION_FIXED8_HISTORY_MIN_OBSERVATIONS) return 0u;
#endif
    base = history->peak_ticks[core];
    if (history->average_ticks[core] > base)
        base = history->average_ticks[core];
    padding = base / BR_AUDIO_FUSION_GUARDED_HISTORY_PAD_DIVISOR;
    if (base % BR_AUDIO_FUSION_GUARDED_HISTORY_PAD_DIVISOR) ++padding;
    if (UINT32_MAX - base < padding) return UINT32_MAX;
    return base + padding;
}
#endif
#endif
#endif

/* Source preset/mode and effect-engine choices change the work represented by
 * a job. Invalidate learned timing when those coarse cost drivers change;
 * note activity and continuously varying timbre controls remain in the same
 * model so the estimate can adapt to normal performance variation. */
static uint32_t br_audio_fusion_job_signature(
    const br_audio_source_job_t *job)
{
    const br_sound_t *sound;
    const br_fx_config_t *fx;
    uint32_t signature = 2166136261u;
    if (!job || job->part >= BR_PARTS) return 0u;
    sound = &br_effective_sound[job->part];
    fx = &br_effective_fx[job->part];
#define BR_FUSION_SIGNATURE_WORD(value) \
    do { signature ^= (uint32_t)(value); signature *= 16777619u; } while (0)
    BR_FUSION_SIGNATURE_WORD(br_mode[job->part]);
    BR_FUSION_SIGNATURE_WORD(sound->preset);
    BR_FUSION_SIGNATURE_WORD(job->lane_mask);
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
    BR_FUSION_SIGNATURE_WORD(BR_AUDIO_NATIVE_RENDER_MAX);
    BR_FUSION_SIGNATURE_WORD(BR_BLOCK);
#ifdef PLAITS_INTERNAL_SAMPLE_RATE
    BR_FUSION_SIGNATURE_WORD(PLAITS_INTERNAL_SAMPLE_RATE);
#endif
#endif
    BR_FUSION_SIGNATURE_WORD(fx->delay_engine);
    BR_FUSION_SIGNATURE_WORD(fx->reverb_engine);
    BR_FUSION_SIGNATURE_WORD(fx->mod_engine);
#if BR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE && \
    defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
    if ((job->kind == BR_AUDIO_JOB_FUSED_TRACK ||
         job->kind == BR_AUDIO_JOB_TRACK_SOURCE) && br_mode[job->part] == 3u) {
        const uint32_t model = sound->preset - BR_FIRST_PLAITS;
        const int32_t active = plaits_active_engine(br_dsp[job->part].plaits);
        BR_FUSION_SIGNATURE_WORD(active == (int32_t)model ? 0u : 1u);
    }
#endif
#if BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE
    if (job->kind == BR_AUDIO_JOB_SOURCE && br_mode[job->part] == 4u) {
        uint32_t lane;
        const void *state = br_plaits_kit_job_state(job->part,
                                                    job->lane_mask);
        /* Keep this key cheap and tied to the engine actually selected by
         * the pre-dispatch lane snapshot. Continuous controls remain in one
         * adaptive class; pending triggers and active overrides are handled
         * by the read-only lane state helper. */
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            uint32_t voice = 0u;
            if (!(job->lane_mask & (1u << lane))) continue;
            (void)plaits_kit_lane_scheduler_state(
                state, sound, lane, 0, &voice);
            BR_FUSION_SIGNATURE_WORD(voice);
        }
    }
#endif
#if BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY
    if (job->kind == BR_AUDIO_JOB_SOURCE && br_mode[job->part] == 1u &&
        sound->preset == BR_KIT_BRAIDS) {
        uint32_t lane;
        const void *state = br_drum_pair_state(job->part, job->lane_mask);
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            uint32_t engine_work = 0u, voice = 0u;
            if (!(job->lane_mask & (1u << lane))) continue;
            (void)br_drum_lane_scheduler_state(
                state, sound, lane, &engine_work, &voice);
            BR_FUSION_SIGNATURE_WORD(engine_work);
            if (engine_work) BR_FUSION_SIGNATURE_WORD(voice);
        }
    }
#endif
#if BR_AUDIO_FUSION_BRAIDS_COLD_RENDER_SIGNATURE
    if (job->kind == BR_AUDIO_JOB_SOURCE && br_mode[job->part] == 1u &&
        sound->preset == BR_KIT_BRAIDS) {
        uint32_t lane;
        const void *state = br_drum_pair_state(job->part, job->lane_mask);
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            uint32_t engine_work = 0u, cold = 0u;
            if (!(job->lane_mask & (1u << lane))) continue;
            (void)br_drum_lane_scheduler_state_ex(
                state, sound, lane, &engine_work, 0, &cold);
            if (engine_work) BR_FUSION_SIGNATURE_WORD(cold);
        }
    }
#endif
#if BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE
    if (job->kind == BR_AUDIO_JOB_FUSED_TRACK ||
        job->kind == BR_AUDIO_JOB_TRACK_FX ||
        job->kind == BR_AUDIO_JOB_KIT_FINALIZE) {
        BR_FUSION_SIGNATURE_WORD(sound->delay != 0u);
        BR_FUSION_SIGNATURE_WORD(sound->reverb != 0u);
        BR_FUSION_SIGNATURE_WORD(fx->mod_mix != 0u);
    }
#endif
#if BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE
    BR_FUSION_SIGNATURE_WORD(job->key != 0);
#endif
#undef BR_FUSION_SIGNATURE_WORD
    return signature ? signature : 1u;
}

static uint32_t br_audio_fusion_history_estimate(
    const br_audio_source_job_t *job,
#if BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE
    uint32_t snapshot_index,
#endif
    uint32_t *cost, uint32_t *affinity)
{
    uint32_t slot = br_audio_fusion_history_slot(job);
    uint32_t frame_class = job && job->frames >= BR_BLOCK ? 1u : 0u;
    uint32_t signature;
    const br_audio_fusion_job_history_t *history;
    uint32_t valid0, valid1;
#if !BR_AUDIO_FUSION_GUARDED_HISTORY
    uint32_t a, b;
#endif
    if (!cost || !affinity || !job || job->part >= BR_PARTS ||
        slot >= BR_AUDIO_FUSION_HISTORY_SLOTS)
        return 0u;
#if BR_AUDIO_FUSION_ACTIVITY_EWMA
    if (!job->activity_class) return 0u;
#endif
    history = &br_audio_fusion_job_history[job->part][slot][frame_class];
    valid0 = history->samples[0] != 0u;
    valid1 = history->samples[1] != 0u;
    if (!valid0 && !valid1) return 0u;
#if BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE
    if (snapshot_index < BR_AUDIO_MAX_SOURCE_JOBS &&
        br_audio_fusion_signature_snapshot_valid[snapshot_index])
        signature = br_audio_fusion_signature_snapshot[snapshot_index];
    else
        signature = br_audio_fusion_job_signature(job);
#else
    signature = br_audio_fusion_job_signature(job);
#endif
    if (history->signature != signature) return 0u;
#if !BR_AUDIO_FUSION_GUARDED_HISTORY
    a = history->average_ticks[0];
    b = history->average_ticks[1];
#endif
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
#if BR_AUDIO_FUSION_GUARDED_HISTORY
    {
        uint32_t pred0 = valid0 ?
            br_audio_fusion_guarded_history_cost(history, 0u) : 0u;
        uint32_t pred1 = valid1 ?
            br_audio_fusion_guarded_history_cost(history, 1u) : 0u;
        if (!pred0) pred0 = br_audio_fusion_robust_activity_floor(job);
        if (!pred1) pred1 = br_audio_fusion_robust_activity_floor(job);
        *cost = pred0 > pred1 ? pred0 : pred1;
    }
#else
    *cost = valid0 && valid1 ? (a > b ? a : b) : valid0 ? a : b;
#endif
#else
    if (valid0 && valid1)
        *cost = a < b ? a + (b - a) / 2u : b + (a - b) / 2u;
    else
        *cost = valid0 ? a : b;
#endif
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
#if !BR_AUDIO_FUSION_GUARDED_HISTORY
    *cost = br_audio_fusion_apply_robust_floor(job, *cost);
#endif
#endif
    if (*cost < 1u) *cost = 1u;
    *affinity = history->has_last_core ? history->last_core : UINT32_MAX;
    return 1u;
}

#define BR_POST_JOIN_HISTORY_AVAILABLE 1
/* Work counters for diagnostic 0x9b: every timed job's ticks and count, per track and core,
 * so the bench can measure the work a module adds independently of how jobs are scheduled. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
uint32_t br_audio_work_ticks[BR_PARTS][2], br_audio_work_jobs[BR_PARTS][2];
#endif
static void br_audio_fusion_history_commit(uint32_t count)
{
    uint32_t i;
    for (i = 0u; i < count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[i];
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
        if (job->part < BR_PARTS && job->executor_core < 2u && job->duration_ticks) {
            br_audio_work_ticks[job->part][job->executor_core] += job->duration_ticks;
            ++br_audio_work_jobs[job->part][job->executor_core];
        }
#endif
        uint32_t slot = br_audio_fusion_history_slot(job);
        uint32_t frame_class = job->frames >= BR_BLOCK ? 1u : 0u;
        uint32_t signature;
        uint32_t core = job->executor_core;
        br_audio_fusion_job_history_t *history;
        uint32_t old, delta, adjustment;
#if BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE || \
    BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY || \
    BR_AUDIO_FUSION_PLAITS_COLD_MODEL_SIGNATURE || \
    BR_AUDIO_FUSION_BRAIDS_COLD_RENDER_SIGNATURE || \
    BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE || \
    BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE
        if (!br_audio_fusion_signature_snapshot_valid[i]) continue;
        signature = br_audio_fusion_signature_snapshot[i];
#else
        signature = br_audio_fusion_job_signature(job);
#endif
#if BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
        uint32_t prior_samples;
#endif
        if (slot >= BR_AUDIO_FUSION_HISTORY_SLOTS ||
            job->part >= BR_PARTS || core >= 2u ||
            !job->duration_ticks) continue;
#if BR_AUDIO_FUSION_ACTIVITY_EWMA
        if (!job->activity_class) continue;
#endif
        history = &br_audio_fusion_job_history[job->part][slot][frame_class];
        if ((history->samples[0] || history->samples[1]) &&
            history->signature != signature)
            memset(history, 0, sizeof *history);
        history->signature = signature;
        old = history->average_ticks[core];
#if BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
        prior_samples = history->samples[core];
#endif
#if BR_AUDIO_FUSION_GUARDED_HISTORY || BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
        if (!history->samples[core] ||
            job->duration_ticks > history->peak_ticks[core])
            history->peak_ticks[core] = job->duration_ticks;
#endif
        if (!history->samples[core]) {
            history->average_ticks[core] = job->duration_ticks;
            history->samples[core] = 1u;
        } else if (job->duration_ticks >= old) {
            delta = job->duration_ticks - old;
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
            adjustment = delta / 2u;
#else
            adjustment = delta / 4u;
#endif
            if (!adjustment && delta) adjustment = 1u;
            history->average_ticks[core] = old + adjustment;
        } else {
            delta = old - job->duration_ticks;
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
            adjustment = delta / 16u;
#else
            adjustment = delta / 4u;
#endif
            if (!adjustment && delta) adjustment = 1u;
            history->average_ticks[core] = old - adjustment;
        }
#if BR_AUDIO_FUSION_FIXED8_HISTORY_PLAN
        if (prior_samples && history->samples[core] != UINT32_MAX)
            ++history->samples[core];
#endif
        history->last_core = core;
        history->has_last_core = 1u;
    }
}
#endif

#if BR_AUDIO_FUSION_MODAL_COLD_SOURCE_CHUNK_TICKS != 0u && \
    BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
/* This target241-only prior covers a measured cold Modal miss without raising
 * the persistent robust floor. Calls occur only after the EWMA lookup missed.
 */
static uint32_t br_audio_fusion_modal_cold_floor(
    const br_audio_source_job_t *job)
{
    uint32_t preset, chunks, fx_stages, per_chunk;
    const br_sound_t *sound;
    const br_fx_config_t *fx;
    if (!job || job->kind != BR_AUDIO_JOB_FUSED_TRACK ||
        job->part >= BR_PARTS || !job->frames || job->frames > BR_BLOCK ||
        !job->activity_class || br_mode[job->part] != 3u)
        return 0u;
    preset = br_effective_sound[job->part].preset;
    if (preset < BR_FIRST_PLAITS ||
        preset >= BR_FIRST_PLAITS + BR_PLAITS_MODEL_COUNT ||
        preset - BR_FIRST_PLAITS != 20u)
        return 0u;
    chunks = job->frames / BR_AUDIO_NATIVE_RENDER_MAX +
        ((job->frames % BR_AUDIO_NATIVE_RENDER_MAX) != 0u);
    if (!chunks) chunks = 1u;
    sound = &br_effective_sound[job->part];
    fx = &br_effective_fx[job->part];
    fx_stages = (sound->delay ? 1u : 0u) +
        (sound->reverb ? 1u : 0u) +
        (fx->mod_mix ? 1u : 0u);
    per_chunk = BR_AUDIO_FUSION_MODAL_COLD_SOURCE_CHUNK_TICKS + 512u +
        fx_stages * 768u;
    return chunks > UINT32_MAX / per_chunk ? UINT32_MAX :
        chunks * per_chunk;
}
#endif

static void br_audio_fusion_cost_hints(uint32_t count)
{
    uint32_t i;
    for (i = 0u; i < count; ++i) {
        br_audio_source_job_t *job = &br_audio_source_jobs[i];
#if BR_AUDIO_FUSION_LAZY_FX_COST_GETTER
        uint32_t fx_cost = 0u;
#else
        uint32_t fx_cost = br_audio_call_fx_fusion_cost(job->part);
#endif
#if BR_AUDIO_FUSION_ACTIVITY_EWMA
        job->activity_class = br_audio_fusion_job_activity_class(job);
#endif
#if BR_AUDIO_FUSION_KIT_VOICE_SIGNATURE || \
    BR_AUDIO_FUSION_BRAIDS_LANE_HISTORY || \
    BR_AUDIO_FUSION_STAGE_ENABLE_SIGNATURE || \
    BR_AUDIO_FUSION_KEY_OUTPUT_SIGNATURE
        br_audio_fusion_signature_snapshot_valid[i] = 0u;
        br_audio_fusion_signature_snapshot[i] =
            br_audio_fusion_job_signature(job);
        br_audio_fusion_signature_snapshot_valid[i] = 1u;
#endif
#if BR_AUDIO_FUSION_EWMA_SCHEDULER
        uint32_t whole_job_cost, affinity;
        job->affinity_core = UINT32_MAX;
#if BR_AUDIO_FUSION_ACTIVITY_EWMA
#if BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE
        if (job->activity_class &&
            br_audio_fusion_history_estimate(job, i, &whole_job_cost, &affinity)) {
#else
        if (job->activity_class &&
            br_audio_fusion_history_estimate(job, &whole_job_cost, &affinity)) {
#endif
#else
#if BR_AUDIO_FUSION_SIGNATURE_SNAPSHOT_REUSE
        if (br_audio_fusion_history_estimate(job, i, &whole_job_cost, &affinity)) {
#else
        if (br_audio_fusion_history_estimate(job, &whole_job_cost, &affinity)) {
#endif
#endif
            job->cost_hint = whole_job_cost;
            job->affinity_core = affinity;
            continue;
        }
#endif
        if (job->kind == BR_AUDIO_JOB_TRACK_SOURCE) {
            if (!br_audio_fixed_plan_add_checked(br_audio_source_job_cost(i), 512u,
                                                 &job->cost_hint))
                job->cost_hint = UINT32_MAX;
        } else if (job->kind == BR_AUDIO_JOB_SHARED_REVERB) {
            job->cost_hint = br_audio_call_fx_shared_reverb_cost() + 256u;
        } else if (job->kind == BR_AUDIO_JOB_TRACK_FX) {
#if BR_AUDIO_FUSION_LAZY_FX_COST_GETTER
            fx_cost = br_audio_call_fx_fusion_cost(job->part);
#endif
            if (!br_audio_fixed_plan_add_checked(fx_cost, 256u, &job->cost_hint))
                job->cost_hint = UINT32_MAX;
        } else if (job->kind == BR_AUDIO_JOB_FUSED_TRACK) {
#if BR_AUDIO_FUSION_LAZY_FX_COST_GETTER
            fx_cost = br_audio_call_fx_fusion_cost(job->part);
#endif
            uint32_t source_cost = br_audio_source_job_cost(i);
            if (!br_audio_fixed_plan_add_checked(source_cost, fx_cost,
                                                 &job->cost_hint) ||
                !br_audio_fixed_plan_add_checked(job->cost_hint, 512u,
                                                 &job->cost_hint))
                job->cost_hint = UINT32_MAX;
        } else if (job->kind == BR_AUDIO_JOB_KIT_FINALIZE) {
#if BR_AUDIO_FUSION_LAZY_FX_COST_GETTER
            fx_cost = br_audio_call_fx_fusion_cost(job->part);
#endif
            if (!br_audio_fixed_plan_add_checked(fx_cost, 1024u,
                                                 &job->cost_hint))
                job->cost_hint = UINT32_MAX;
        } else {
            job->cost_hint = br_audio_source_job_cost(i);
        }
#if BR_AUDIO_FUSION_ACTIVITY_EWMA
        if (job->activity_class) {
#if BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
            uint32_t floor = br_audio_fusion_robust_activity_floor(job);
#else
            uint32_t floor = br_audio_fusion_activity_floor(job->frames);
#endif
            if (job->cost_hint < floor) job->cost_hint = floor;
#if BR_AUDIO_FUSION_MODAL_COLD_SOURCE_CHUNK_TICKS != 0u && \
    BR_AUDIO_FUSION_ROBUST_COST_PREDICTOR
            {
                uint32_t modal_floor =
                    br_audio_fusion_modal_cold_floor(job);
                if (job->cost_hint < modal_floor)
                    job->cost_hint = modal_floor;
            }
#endif
        }
#endif
        if (!job->cost_hint) job->cost_hint = 1u;
    }
    /* Critical path first: a split track's source job carries its effects job's
     * cost as well, so the ready queue starts the longest chain early. The hint
     * only orders ready jobs; history learns from measured durations. */
    for (i = 8u; i < count; ++i) {
        const br_audio_source_job_t *fx = &br_audio_source_jobs[i];
        br_audio_source_job_t *src;
        if (fx->kind != BR_AUDIO_JOB_TRACK_FX || fx->predecessor >= count) continue;
        src = &br_audio_source_jobs[fx->predecessor];
        if (!br_audio_fixed_plan_add_checked(src->cost_hint, fx->cost_hint,
                                             &src->cost_hint))
            src->cost_hint = UINT32_MAX;
    }
}

static void br_audio_build_source_batches(uint32_t count)
{
    uint32_t ordered[BR_AUDIO_MAX_SOURCE_JOBS];
    uint32_t i, j, load[2] = {0u, 0u};
#if BR_AUDIO_LPT_COST_SNAPSHOT
    uint32_t cost_snapshot[BR_AUDIO_MAX_SOURCE_JOBS];
#endif
    br_audio_legacy_batches[0].count = 0u;
    br_audio_legacy_batches[1].count = 0u;
#if BR_AUDIO_LPT_COST_SNAPSHOT
    /* The per-job estimate is immutable for this build. Read and clamp it once
     * so insertion sort and load accounting reuse the same snapshot. */
    for (i = 0; i < count; ++i)
        cost_snapshot[i] = br_audio_source_job_cost(i);
#endif
    for (i = 0; i < count; ++i) {
        uint32_t at = i;
#if BR_AUDIO_LPT_COST_SNAPSHOT
        uint32_t cost = cost_snapshot[i];
        while (at && cost_snapshot[ordered[at - 1u]] < cost) {
            ordered[at] = ordered[at - 1u]; --at;
        }
#else
        uint32_t cost = br_audio_source_job_cost(i);
        while (at && br_audio_source_job_cost(ordered[at - 1u]) < cost) {
            ordered[at] = ordered[at - 1u]; --at;
        }
#endif
        ordered[at] = i;
    }
    for (i = 0; i < count; ++i) {
        uint32_t index = ordered[i], core = load[0] <= load[1] ? 0u : 1u;
        br_audio_source_batch_t *batch = &br_audio_legacy_batches[core];
        uint32_t cost;
        batch->job_index[batch->count++] = index;
#if BR_AUDIO_LPT_COST_SNAPSHOT
        cost = cost_snapshot[index];
#else
        cost = br_audio_source_job_cost(index);
#endif
        load[core] += cost;
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        if (br_audio_runtime_trace_chunk_active) {
            const br_audio_source_job_t *job = &br_audio_source_jobs[index];
            fm1_runtime_trace_job_metadata(index, job->part, job->lane_mask,
                                           core, cost);
        }
#endif
    }
    for (i = 0; i < 2u; ++i) {
        br_audio_source_batch_t *batch = &br_audio_legacy_batches[i];
        for (j = 1u; j < batch->count; ++j) {
            uint32_t index = batch->job_index[j], k = j;
            while (k && batch->job_index[k - 1u] > index) {
                batch->job_index[k] = batch->job_index[k - 1u]; --k;
            }
            batch->job_index[k] = index;
        }
    }
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
    if (br_audio_runtime_trace_chunk_active)
        fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_ASSIGN_END);
#endif
}

#if BR_AUDIO_RUNTIME_TRACE_ENABLED
static uint32_t br_audio_runtime_trace_base_cost(uint32_t index)
{
    const br_audio_source_job_t *job = &br_audio_source_jobs[index];
    uint32_t slot = br_audio_kit_job_slot(job->lane_mask);
    uint32_t cost = job->part < BR_PARTS ?
        br_diagnostic_source_cost[job->part][slot] : 0u;
    return cost < 1024u ? 1024u : cost;
}

static void br_audio_runtime_trace_direct_assignments(uint32_t count)
{
    uint32_t i;
    fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_ASSIGN_END);
    for (i = 0; i < count; ++i) {
        const br_audio_source_job_t *job = &br_audio_source_jobs[i];
        fm1_runtime_trace_job_metadata(i, job->part, job->lane_mask, 0u,
                                       br_audio_runtime_trace_base_cost(i));
    }
}
#endif

static uint32_t br_audio_run_source_batch_cpu0(const br_audio_source_batch_t *batch)
{
    uint32_t i;
    for (i = 0; i < batch->count; ++i) {
        uint32_t index = batch->job_index[i];
        if (br_audio_source_deadline_expired() ||
            index >= br_audio_source_queue.count) return 0u;
#if defined(BR_SOURCE_QUEUE_TEST)
        br_audio_source_test_begin(index);
#endif
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        if (br_audio_runtime_trace_chunk_active)
            fm1_runtime_trace_job_begin(index, 0u);
#endif
        uint32_t completed = br_audio_worker_sources(&br_audio_source_jobs[index]);
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        if (br_audio_runtime_trace_chunk_active)
            fm1_runtime_trace_job_end(index, 0u);
#endif
#if defined(BR_SOURCE_QUEUE_TEST)
        br_audio_source_test_end(index);
#endif
        if (!completed || br_audio_source_deadline_expired()) return 0u;
    }
    return 1u;
}
#endif

/* CPU0-only post-source phase. Keeping the original arithmetic and ordering
 * here makes the serial path an exact reference for a future joined worker. */
static int32_t br_engine_post_part_row(uint32_t p, int32_t *row,
                                       int32_t *key, uint32_t n)
{
    uint32_t i;
    int32_t osc_sum = 0, part_sum = 0;
    br_sound_t *s = &br_effective_sound[p];
    br_env_t *e = &br_env[p];
    if (br_mode[p] == 0u
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        || br_mode[p] == 3u
#endif
    ) {
        if (e->stage != BR_ENV_OFF) {
            uint32_t calibration_q6 = br_audio_preset_gain(s->preset);
            br_env_refresh(e, s, 1u);
            int32_t gain = (int32_t)e->velocity *
                (int32_t)(s->mute ? 0u : br_bound(s->level, 127u));
            uint32_t ramp_count = e->velocity_gain_ramp_left;
#if BR_AUDIO_POST_GAIN_Q15_APPROX
            uint32_t gain_q15 = gain == 16129 ? 32768u :
                ((uint32_t)gain * 32768u + 8064u) / 16129u;
            int32_t gain_mul = 1;
            if (calibration_q6 != 64u)
                gain_q15 = (gain_q15 * calibration_q6 + 32u) >> 6;
            /* A calibration above unity can push the gain past 1.0. Keep it at or under 1.0 in Q15 and apply
             * the rest as a power-of-two multiply, so the per-sample product stays on the fast 32-bit path. */
            while (gain_q15 > 32768u) { gain_q15 >>= 1; gain_mul <<= 1; }
#else
            if (calibration_q6 != 64u)
                gain = (int32_t)(((uint32_t)gain * calibration_q6 + 32u) >> 6);
#endif
#if BR_AUDIO_POST_GAIN_SUSTAIN_FOLD && BR_AUDIO_POST_GAIN_Q15_APPROX
            uint32_t sustain_fold = e->stage == BR_ENV_SUSTAIN;
            int32_t sustain_gain = sustain_fold ?
                br_filter_mul_q15((int32_t)e->amp, gain_q15) : 0;
#endif
            if (ramp_count > n) ramp_count = n;
            if (ramp_count) {
                uint32_t level = s->mute ? 0u : br_bound(s->level, 127u);
                osc_sum += br_env_render_velocity_ramp(e, row, ramp_count, level, calibration_q6);
            }
            for (i = ramp_count; i < n; ++i) {
                int32_t sample = row[i];
                int32_t envelope_sample;
                br_env_advance(e);
#if BR_AUDIO_POST_GAIN_SUSTAIN_FOLD && BR_AUDIO_POST_GAIN_Q15_APPROX
                if (sustain_fold)
                    row[i] = br_filter_mul_q15(sample, (uint32_t)sustain_gain) * gain_mul;
                else {
#endif
                envelope_sample = (sample * (int32_t)e->amp) >> 15;
#if BR_AUDIO_POST_GAIN_Q15_APPROX
                row[i] = (gain_q15 == 32768u ? envelope_sample :
                    br_filter_mul_q15(envelope_sample, gain_q15)) * gain_mul;
#else
                row[i] = br_div16129_exact(envelope_sample * gain);
#endif
#if BR_AUDIO_POST_GAIN_SUSTAIN_FOLD && BR_AUDIO_POST_GAIN_Q15_APPROX
                }
#endif
                osc_sum += sample;
            }
        }
    } if (br_mode[p] == 1u || br_mode[p] == 4u) {
        if (e->stage != BR_ENV_OFF) {
            br_env_refresh(e, s, 0u);
            for (i = 0; i < n; ++i) {
                br_env_advance(e);
                row[i] = br_filter_mul_q15(row[i], e->amp);
                key[i] = br_filter_mul_q15(key[i], e->amp);
            }
        } else for (i = 0; i < n; ++i) {
            row[i] = 0;
            key[i] = 0;
        }
    }
    if (!s->filter_resonance && !s->filter_keytrack) {
        br_filter[p].band = 0;
        for (i = 0; i < n; ++i)
            row[i] = br_filter_process(&br_filter[p], row[i],
                                       br_bound(s->filter, 127u));
    } else {
        for (i = 0; i < n; ++i)
            row[i] = br_filter_process_controls(&br_filter[p], row[i],
                br_bound(s->filter, 127u), br_bound(s->filter_resonance, 127u),
                s->filter_keytrack != 0u, e->note);
    }
    if ((br_mode[p] == 1u && br_drum_pair_quiet(p))
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        || (br_mode[p] == 4u && br_plaits_kit_pair_quiet(p))
#endif
    ) {
        e->stage = BR_ENV_OFF;
        e->amp = 0u;
    }
#if !BR_AUDIO_POST_SKIP_UNUSED_SUM
    for (i = 0; i < n; ++i) part_sum += row[i];
#endif
    if (n && br_mode[p] != 0u
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        && br_mode[p] != 3u
#endif
    ) {
#if BR_AUDIO_POST_SKIP_UNUSED_SUM
        /* Pitched modes return the pre-filter oscillator sum instead, so no
         * post-filter read/reduction is needed for those rows. */
        for (i = 0; i < n; ++i) part_sum += row[i];
#endif
        osc_sum = part_sum;
    }
    br_quiet_part[p] = (br_mode[p] == 0u ? e->stage == BR_ENV_OFF :
        br_mode[p] == 1u ? br_drum_pair_quiet(p) && e->stage == BR_ENV_OFF :
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        br_mode[p] == 4u ? br_plaits_kit_pair_quiet(p) && e->stage == BR_ENV_OFF :
#endif
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
        e->stage == BR_ENV_OFF && plaits_quiet(br_dsp[p].plaits)) &&
#else
        0) &&
#endif
        br_filter_quiet(&br_filter[p]);
#if BR_AUDIO_POST_POW2_MEAN && !FM1_NATIVE_OUTPUT_32K
    if (n == 128u) return br_filter_div_pow2(osc_sum, 7u);
    if (n == 64u) return br_filter_div_pow2(osc_sum, 6u);
    if (n == 32u) return br_filter_div_pow2(osc_sum, 5u);
#endif
    return n ? osc_sum / (int32_t)n : 0;
}

static int32_t br_engine_post_part(uint32_t p,
                                   int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{
    return br_engine_post_part_row(p, bus[p], br_comp_key[p], n);
}

#ifdef BR_SPLIT_AUDIO
/* Split a synth track when its effects took more than BR_AUDIO_SPLIT_ON_TICKS
 * in the last block, and fuse it again below BR_AUDIO_SPLIT_OFF_TICKS, so the
 * choice (and the cost history behind it) does not flap. */
/* In a split graph, run the shared reverb as its own job rather than inside
 * track 3's effects job. */
#ifndef BR_AUDIO_SPLIT_SHARED_REVERB
#define BR_AUDIO_SPLIT_SHARED_REVERB 1
#endif
#ifndef BR_AUDIO_SPLIT_ON_TICKS
#define BR_AUDIO_SPLIT_ON_TICKS 3000u
#endif
#ifndef BR_AUDIO_SPLIT_OFF_TICKS
#define BR_AUDIO_SPLIT_OFF_TICKS 2000u
#endif
static uint8_t br_audio_split_state[BR_SYNTH_PARTS];
/* Diagnostic 0x9b: halves built as a split graph, and split tracks summed over them. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
uint32_t br_audio_split_blocks, br_audio_split_tracks;
#endif
#if defined(BR_SOURCE_QUEUE_TEST)
uint32_t br_audio_split_test_force; /* bit p forces track p split in host tests */
#endif
static uint32_t br_audio_split_track(uint32_t p, uint32_t n)
{
#if BR_AUDIO_SPLIT_TRACK_FX
    uint32_t cost;
    if (p >= BR_SYNTH_PARTS || n != BR_BLOCK || !br_audio_source_fx_fusion_active ||
        br_pipeline_enabled) return 0u;
#if defined(BR_SOURCE_QUEUE_TEST)
    if ((br_audio_split_test_force >> p) & 1u) return 1u;
#endif
    cost = br_audio_call_fx_fusion_cost(p);
    if (br_audio_split_state[p]) {
        if (cost < BR_AUDIO_SPLIT_OFF_TICKS) br_audio_split_state[p] = 0u;
    } else if (cost > BR_AUDIO_SPLIT_ON_TICKS && cost != UINT32_MAX) {
        br_audio_split_state[p] = 1u;
    }
    return br_audio_split_state[p];
#else
    (void)p; (void)n;
    return 0u;
#endif
}

static uint32_t br_audio_build_fusion_jobs(
    int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{
    uint32_t p, i, count = 0u, split = 0u, kit_seen = 0u;
    for (p = 0u; p < BR_PARTS; ++p) {
        if (br_mode[p] == 1u || br_mode[p] == 4u) {
            uint32_t finish, lane_jobs[BR_AUDIO_KIT_JOB_LANES];
            uint32_t lane, lane_count = BR_AUDIO_KIT_JOB_LANES;
            if (br_mode[p] != 4u) {
                memset(br_audio_kit_partial[0], 0, sizeof br_audio_kit_partial[0]);
                if (!br_audio_kit_key_elision_active())
                    memset(br_audio_kit_key_partial[0], 0,
                           sizeof br_audio_kit_key_partial[0]);
            }
#if !BR_AUDIO_FUSION_FOUR_WAY_KIT
            lane_count = 2u;
#endif
            for (lane = 0u; lane < lane_count; ++lane) {
                uint32_t job = count++;
                uint32_t lane_mask;
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
                lane_mask = 1u << lane;
#else
                lane_mask = lane ? BR_KIT_LANES_B : BR_KIT_LANES_A;
#endif
                lane_jobs[lane] = job;
                br_audio_source_jobs[job].part = p;
                br_audio_source_jobs[job].frames = n;
                br_audio_source_jobs[job].output = br_audio_kit_partial[0][lane];
                br_audio_source_jobs[job].lane_mask = lane_mask;
                br_audio_source_jobs[job].key = br_audio_kit_key_output(
                    p, lane, lane_mask);
                br_audio_source_jobs[job].predecessor = UINT32_MAX;
                br_audio_source_jobs[job].predecessor2 = UINT32_MAX;
                br_audio_source_jobs[job].predecessor3 = UINT32_MAX;
                br_audio_source_jobs[job].predecessor4 = UINT32_MAX;
                br_audio_source_jobs[job].kind = BR_AUDIO_JOB_SOURCE;
            }

            finish = count++;
            br_audio_source_jobs[finish].part = p;
            br_audio_source_jobs[finish].frames = n;
            br_audio_source_jobs[finish].output = bus[p];
            br_audio_source_jobs[finish].lane_mask = BR_DRUM_ALL_MASK;
            br_audio_source_jobs[finish].key = 0;
            br_audio_source_jobs[finish].predecessor = lane_jobs[0];
            br_audio_source_jobs[finish].predecessor2 = lane_jobs[1];
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
            br_audio_source_jobs[finish].predecessor3 = lane_jobs[2];
            br_audio_source_jobs[finish].predecessor4 = lane_jobs[3];
#else
            br_audio_source_jobs[finish].predecessor3 = UINT32_MAX;
            br_audio_source_jobs[finish].predecessor4 = UINT32_MAX;
#endif
            br_audio_source_jobs[finish].kind = BR_AUDIO_JOB_KIT_FINALIZE;
            kit_seen = 1u;
        } else {
            uint32_t job = count++;
            br_audio_source_jobs[job].part = p;
            br_audio_source_jobs[job].frames = n;
            br_audio_source_jobs[job].output = bus[p];
            br_audio_source_jobs[job].lane_mask = BR_DRUM_ALL_MASK;
            br_audio_source_jobs[job].key = 0;
            br_audio_source_jobs[job].predecessor = UINT32_MAX;
            br_audio_source_jobs[job].predecessor2 = UINT32_MAX;
            br_audio_source_jobs[job].predecessor3 = UINT32_MAX;
            br_audio_source_jobs[job].predecessor4 = UINT32_MAX;
            br_audio_source_jobs[job].kind = BR_AUDIO_JOB_FUSED_TRACK;
            if (job == p && br_audio_split_track(p, n)) {
                br_audio_source_jobs[job].kind = BR_AUDIO_JOB_TRACK_SOURCE;
                split |= 1u << p;
            }
        }
    }
    /* Only the standard graph (three synth tracks at 0..2, the four-lane kit at
     * 3..7) gains an effects tail; anything else stays fused. */
    if (split && (!kit_seen || count != 8u)) {
        for (p = 0u; p < BR_SYNTH_PARTS; ++p)
            if ((split >> p) & 1u) br_audio_source_jobs[p].kind = BR_AUDIO_JOB_FUSED_TRACK;
        split = 0u;
    }
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    if (split) {
        ++br_audio_split_blocks;
        br_audio_split_tracks += (uint32_t)__builtin_popcount(split);
    }
#endif
    for (p = 0u; p < BR_SYNTH_PARTS; ++p) {
        uint32_t job;
        if (!((split >> p) & 1u)) continue;
        job = count++;
        br_audio_source_jobs[job].part = p;
        br_audio_source_jobs[job].frames = n;
        br_audio_source_jobs[job].output = bus[p];
        br_audio_source_jobs[job].lane_mask = BR_DRUM_ALL_MASK;
        br_audio_source_jobs[job].key = 0;
        br_audio_source_jobs[job].predecessor = p;
        br_audio_source_jobs[job].predecessor2 = UINT32_MAX;
        br_audio_source_jobs[job].predecessor3 = UINT32_MAX;
        br_audio_source_jobs[job].predecessor4 = UINT32_MAX;
        br_audio_source_jobs[job].kind = BR_AUDIO_JOB_TRACK_FX;
    }
#if BR_AUDIO_SPLIT_SHARED_REVERB
    if (split && br_audio_call_fx_shared_reverb_runs()) {
        uint32_t job = count++;
        br_audio_source_jobs[job].part = BR_DRUM_PART - 1u;    /* the reverb's host track */
        br_audio_source_jobs[job].frames = n;
        br_audio_source_jobs[job].output = bus[BR_DRUM_PART - 1u];
        br_audio_source_jobs[job].lane_mask = BR_DRUM_ALL_MASK;
        br_audio_source_jobs[job].key = 0;
        br_audio_source_jobs[job].predecessor = UINT32_MAX;
        br_audio_source_jobs[job].predecessor2 = UINT32_MAX;
        br_audio_source_jobs[job].predecessor3 = UINT32_MAX;
        br_audio_source_jobs[job].predecessor4 = UINT32_MAX;
        br_audio_source_jobs[job].kind = BR_AUDIO_JOB_SHARED_REVERB;
        br_audio_call_fx_shared_reverb_separate();
    }
#endif
    for (i = 0u; i < count; ++i) br_audio_source_jobs[i].state = 0u;
    return count;
}
#endif

/* Arpeggiator. The main loop publishes each synth track's held keys in
 * br_arp_keys (a seqlock: odd while it writes) and the settings in br_part.
 * This runs on CPU0 once per block, before the sequencer, and plays the
 * track's own voice. Like sequencer steps, arp notes are block-quantised.
 * While the transport runs, steps sit on the beat grid so recorded notes
 * line up; while stopped, the arp free-runs from the first key. */
typedef struct {
    uint8_t notes[BR_ARP_MAX_NOTES], count;    /* last consistent key snapshot */
    uint8_t sounding, step_note, repeats_left, last_count, was_playing; /* sounding: note + 1, 0 = none */
    uint32_t order_pos, gate_left, sub_left, sub_frames, free_left, step_count, rng;
} br_arp_state_t;
static br_arp_state_t br_arp_state[BR_SYNTH_PARTS];
static const uint8_t br_arp_divisor[BR_ARP_RATES] = {2u, 3u, 4u, 6u, 8u};

/* Tell the main loop about an arp note while the transport runs, with the
 * sequencer position at this block's start, so REC can write it. */
static void br_arp_record(uint32_t p, uint32_t note, uint32_t on)
{
    uint32_t w = br_arp_rec_write;
    volatile br_arp_rec_t *e;
    if (!br_playing || w - br_arp_rec_read >= BR_ARP_REC_SLOTS) return;
    e = &br_arp_rec[w % BR_ARP_REC_SLOTS];
    e->part = (uint8_t)p; e->note = (uint8_t)note; e->on = (uint8_t)on;
    e->clock = br_seq_clock[p];
    e->step = br_step_pos[p];
    e->duration = br_seq_current_duration[p];
    e->elapsed = br_seq_current_duration[p] - br_seq_frames[p];
    e->unit = br_seq_micro_unit[p];
    RING_PUBLISH();
    br_arp_rec_write = w + 1u;
}

static void br_arp_stop_note(uint32_t p, br_arp_state_t *a)
{
    if (!a->sounding) return;
    br_release_note(p, a->sounding - 1u);
    br_arp_record(p, a->sounding - 1u, 0u);
    a->sounding = 0u;
    a->gate_left = 0u;
}

/* Start note at this block's start. A gate shorter than the block still
 * sounds for one block, like a short sequencer gate. */
static void br_arp_play(uint32_t p, br_arp_state_t *a, uint32_t note,
                        uint32_t frames, uint32_t n)
{
    uint32_t gate = br_bound(br_part[p].arp_gate, 127u), length;
    br_arp_stop_note(p, a);
    br_start_note(p, note, 100u, 0u);
    br_arp_record(p, note, 1u);
    a->sounding = (uint8_t)(note + 1u);
    length = gate >= 127u ? frames : frames * (gate + 1u) / 128u;
    a->gate_left = length > n ? length - n : 1u;
}

/* The next note for the current mode, keys and octave range. */
static uint32_t br_arp_next_note(uint32_t p, br_arp_state_t *a, uint32_t octaves)
{
    uint8_t sorted[BR_ARP_MAX_NOTES];
    uint32_t mode = br_bound(br_part[p].arp_mode, BR_ARP_MODES - 1u);
    uint32_t count = a->count, length = count * octaves, i, j, index;
    uint32_t pos = a->order_pos++;
    for (i = 0u; i < count; ++i) sorted[i] = a->notes[i];
    if (mode != BR_ARP_AS_PLAYED)
        for (i = 1u; i < count; ++i)
            for (j = i; j && sorted[j - 1u] > sorted[j]; --j) {
                uint8_t t = sorted[j]; sorted[j] = sorted[j - 1u]; sorted[j - 1u] = t;
            }
    if (mode == BR_ARP_RANDOM) {
        a->rng = a->rng * 1664525u + 1013904223u;
        index = (a->rng >> 16) % length;
    } else if (mode == BR_ARP_DOWN) index = length - 1u - pos % length;
    else if (mode == BR_ARP_UP_DOWN && length > 2u) {
        uint32_t cycle = 2u * length - 2u, at = pos % cycle;
        index = at < length ? at : cycle - at;
    } else index = pos % length;
    i = sorted[index % count] + 12u * (index / count);
    return i > 127u ? 127u : i;
}

static void br_arp_step(uint32_t p, br_arp_state_t *a, uint32_t step_frames, uint32_t n)
{
    uint32_t octaves = br_part[p].arp_octaves, repeats = 1u;
    if (octaves < 1u) octaves = 1u;
    if (octaves > BR_ARP_MAX_OCTAVES) octaves = BR_ARP_MAX_OCTAVES;
    if (repeats < 1u) repeats = 1u;
    if (repeats > BR_ARP_MAX_REPEAT) repeats = BR_ARP_MAX_REPEAT;
    a->step_note = (uint8_t)br_arp_next_note(p, a, octaves);
    a->sub_frames = step_frames / repeats;
    if (!a->sub_frames) a->sub_frames = 1u;
    a->repeats_left = (uint8_t)(repeats - 1u);
    a->sub_left = a->sub_frames > n ? a->sub_frames - n : 1u;
    br_arp_play(p, a, a->step_note, a->sub_frames, n);
    ++a->step_count;
}

static void br_arp_process(uint32_t p, uint32_t n)
{
    br_arp_state_t *a = &br_arp_state[p];
    const volatile br_arp_keys_t *keys = &br_arp_keys[p];
    uint32_t seq, i, rate, step, swing, quarter;
    /* Arp off and nothing left to wind down: skip the key snapshot entirely. */
    if (!br_arp_on && !a->sounding && !a->count && !a->last_count && !a->repeats_left) {
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        br_clock_experiment.arp_start_armed[p] = 0u;
#endif
        return;
    }
    seq = keys->seq;
    if (!(seq & 1u)) {
        uint32_t count = keys->count;
        uint8_t copy[BR_ARP_MAX_NOTES];
        if (count > BR_ARP_MAX_NOTES) count = BR_ARP_MAX_NOTES;
        for (i = 0u; i < count; ++i) copy[i] = keys->notes[i];
        RING_PUBLISH();
        if (keys->seq == seq) {
            for (i = 0u; i < count; ++i) a->notes[i] = copy[i];
            a->count = (uint8_t)count;
        }
    }
    if (!br_arp_on || !a->count) {
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        br_clock_experiment.arp_ready[p] = 0u;
        br_clock_experiment.arp_start_armed[p] = 0u;
#endif
        br_arp_stop_note(p, a);
        a->repeats_left = 0u;
        a->last_count = 0u;
        a->order_pos = a->step_count = 0u;
        return;
    }
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    /* External-clock Stop/loss must not turn the ARP into a free-running
     * source while the transport is waiting for a clock or Continue. */
    if (br_midi_clock_enabled && !br_playing) {
        br_arp_stop_note(p, a);
        br_clock_experiment.arp_ready[p] = 0u;
        a->repeats_left = 0u;
        a->was_playing = 0u;
        a->last_count = a->count;
        return;
    }
#endif
    rate = br_bound(br_part[p].arp_rate, BR_ARP_RATES - 1u);
    quarter = br_tempo_quarter_frames();
    step = quarter / br_arp_divisor[rate];
    if (!step) step = 1u;
    /* Swing follows the project setting on straight rates only. */
    swing = br_arp_divisor[rate] % 3u ?
        br_bound(br_part[0].swing, BR_SWING_MAX) : 0u;
    /* Notes started in earlier blocks: gate, then any repeat. */
    if (a->sounding && a->gate_left) {
        if (a->gate_left <= n) br_arp_stop_note(p, a);
        else a->gate_left -= n;
    }
    if (a->repeats_left) {
        if (a->sub_left <= n) {
            --a->repeats_left;
            br_arp_play(p, a, a->step_note, a->sub_frames, n);
            a->sub_left += a->sub_frames;
        }
        a->sub_left -= a->sub_left > n ? n : a->sub_left;
    }
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (br_clock_experiment.enabled && br_playing && br_clock_experiment.period_q16) {
        fm1_mca_t *grid = &br_clock_experiment.arp[p];
        uint64_t start = br_clock_experiment.arp_start_armed[p] ?
            br_clock_experiment.arp_start_phase_q16 : br_clock_experiment.phase_q16;
        uint64_t end = br_clock_experiment.phase_end_q16;
        br_clock_experiment.arp_start_armed[p] = 0u;
        uint32_t d = br_arp_divisor[rate], length = 0u;
        uint32_t grid_swing = d % 3u ? swing : 0u;
        if (!br_clock_experiment.arp_ready[p] || grid->divisor != d || grid->swing != grid_swing) {
            if (!fm1_mca_start(grid, start, d, swing)) br_clock_experiment.invalid = 1u;
            br_clock_experiment.arp_ready[p] = 1u;
        }
        int hit = fm1_mca_advance(grid, end, br_clock_experiment.period_q16, &length);
        if (hit < 0) { br_clock_experiment.invalid = 1u; br_playing = 0u; }
        else if (hit) br_arp_step(p, a, length, n);
    } else
#endif
    if (br_playing) {
        /* Beat-grid position of this block within the 4/4 bar. */
        uint32_t bar = 4u * quarter, k, hit = 0u, length = step;
        uint32_t end = (br_global_beat & 3u) * quarter + br_metro_frames;
        uint32_t start = end >= n ? end - n : end + bar - n;
        if (start > end) end += bar;
        uint32_t d = br_arp_divisor[rate];
        /* Boundaries are exact on every beat, so triplets do not drift. */
        k = start * d / quarter;
        for (i = 0u; i < 2u && !hit; ++i, ++k) {
            uint32_t at = (k / d) * quarter + (k % d) * quarter / d +
                ((k & 1u) ? step * swing / 128u : 0u);
            if (at >= start && at < end) {
                hit = 1u;
                if (swing) {
                    uint32_t next = k + 1u;
                    uint32_t next_at = (next / d) * quarter + (next % d) * quarter / d +
                        ((next & 1u) ? step * swing / 128u : 0u);
                    length = next_at - at;
                }
            }
        }
        if (hit) br_arp_step(p, a, length, n);
    } else {
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        br_clock_experiment.arp_ready[p] = 0u;
#endif
        /* Free-running: the first key sounds at once. */
        if (!a->last_count || a->was_playing) a->free_left = 0u;
        if (a->free_left <= n) {
            uint32_t length = !swing ? step : (a->step_count & 1u) ?
                step * (128u - swing) / 128u : step * (128u + swing) / 128u;
            br_arp_step(p, a, length, n);
            a->free_left += length;
        }
        a->free_left -= a->free_left > n ? n : a->free_left;
    }
    a->was_playing = (uint8_t)br_playing;
    a->last_count = a->count;
}

static void br_arp_reset(void)
{
    uint32_t p;
    memset(br_arp_state, 0, sizeof br_arp_state);
    for (p = 0u; p < BR_SYNTH_PARTS; ++p) br_arp_state[p].rng = 0x2545F491u * (p + 1u);
}

/* Modulation capture/advance and scheduler-history commit prepare the NEXT
 * block; nothing in this block's FX or output reads them. They are recorded
 * here and run once, by CPU1 at the start of the master-FX stream (where it
 * would otherwise wait for CPU0's first mix slice) or by CPU0 afterwards. */
#ifndef BR_AUDIO_DEFER_POST_JOIN
#define BR_AUDIO_DEFER_POST_JOIN 1
#endif
#if BR_AUDIO_DEFER_POST_JOIN
static struct {
    int32_t (*bus)[BR_BLOCK];
    int32_t osc[BR_PARTS];
    uint32_t n, history_count;
    volatile uint32_t pending;
    uint32_t history;
} br_post_join;

static void br_engine_post_join_run(void)
{
    uint32_t p, i;
    if (!br_post_join.pending) return;
    br_post_join.pending = 0u;
#if defined(BR_POST_JOIN_HISTORY_AVAILABLE)
    if (br_post_join.history)
        br_audio_fusion_history_commit(br_post_join.history_count);
#endif
    if (br_post_join.n) {
        int32_t sum = 0, mix;
        for (i = 0; i < br_post_join.n; ++i)
            for (p = 0; p < BR_PARTS; ++p) sum += br_post_join.bus[p][i];
        mix = sum / (int32_t)br_post_join.n;
        for (p = 0; p < BR_PARTS; ++p) {
            br_mod_capture(&br_mod_state[p], br_post_join.osc[p], mix);
#if BR_MOD_LFO_PHASE_DELTA_CACHE
            br_mod_advance(&br_mod_state[p], &br_cached_mod[p], br_post_join.n, p);
#else
            br_mod_advance(&br_mod_state[p], &br_cached_mod[p], br_post_join.n);
#endif
        }
    }
}
#endif

BR_AUDIO_API void br_engine_post_join_finish(void)
{
#if BR_AUDIO_DEFER_POST_JOIN
    br_engine_post_join_run();
#endif
}

BR_AUDIO_API void br_engine_render(int32_t bus[BR_PARTS][BR_BLOCK], uint32_t n)
{
    uint32_t p, i;
    int32_t osc_block[BR_PARTS] = {0, 0, 0, 0};
#ifdef BR_SPLIT_AUDIO
    br_audio_source_late_t late_job = {UINT32_MAX, 0u, 0u};
    uint32_t late_accepted = 0u, late_deadline = 0u, late_sequence = 0u;
    uint32_t late_executor = 0u;
    uint32_t complete_watch = 0u;
    br_audio_late_render = 0u;
    br_audio_source_publication_enabled = 0u;
#endif
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG && defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    uint32_t failure_queue_origin_tick = 0u;
    uint32_t failure_queue_origin_estimated = 0u;
    uint32_t failure_queue_source_start_tick = 0u;
    uint32_t failure_queue_source_start_valid = 0u;
#endif
#if BR_AUDIO_RUNTIME_TRACE_ENABLED && defined(BR_SPLIT_AUDIO)
    uint32_t runtime_trace_chunk = 0u;
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    fm1_audio_cpu1_join_clear();
#endif
    br_audio_source_fx_fusion_active = 0u;
#ifdef BR_SPLIT_AUDIO
    uint32_t source_job_count = 0u;
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    uint32_t source_wall_start;
#endif
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
    for (p = 0; p < BR_PARTS; ++p) {
        for (i = 0u; i < BR_AUDIO_DIAGNOSTIC_SOURCE_SLOTS; ++i)
            br_diagnostic_part_synth_ticks[p][i] = 0u;
    }
#endif
    if (n > BR_BLOCK) n = BR_BLOCK;
    for (p = 0u; p < BR_PARTS; ++p) br_seq_clock[p] += n;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    if (fm1_cpu1_worker_fault) br_audio_source_worker_fault = 1u;
#endif
    for (p = 0; p < BR_PARTS; ++p)
        for (i = 0; i < BR_BLOCK; ++i) br_comp_key[p][i] = 0;
#if defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    if (br_audio_source_worker_fault) {
        for (p = 0; p < BR_PARTS; ++p)
            for (i = 0; i < n; ++i) bus[p][i] = 0;
        return;
    }
#endif
    if (!br_ready) {
        for (p = 0; p < BR_PARTS; ++p)
            for (i = 0; i < n; ++i) bus[p][i] = 0;
        return;
    }
#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR && \
    defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS && defined(BR_SPLIT_AUDIO)
    memset(br_audio_plaits_current_append_profile, 0,
           sizeof br_audio_plaits_current_append_profile);
    memset(br_audio_plaits_current_cost_signature, 0,
           sizeof br_audio_plaits_current_cost_signature);
    memset(br_audio_plaits_current_cost_valid, 0,
           sizeof br_audio_plaits_current_cost_valid);
#endif
    for (p = 0; p < BR_PARTS; ++p) {
        br_snapshot(p);
        br_select_mode(p);
        br_build_effective(p);
    }
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (!!br_midi_clock_enabled != br_clock_source_prev ||
        br_clock_generation_seen != br_midi_clock_generation) {
        /* A project recall or two toggles between blocks can return to the
         * same source value; generation still invalidates the old epoch. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
        br_clock_prev_f8_valid = 0u;
#endif
        if (br_clock_source_prev == !!br_midi_clock_enabled && br_midi_clock_enabled)
            br_clock_experiment.anchor.valid = 0u;
        if (br_playing || br_transport_prev) br_midi_apply_transport(0xFCu);
        br_midi_paused = 0u;
        br_clock_experiment.enabled = 0u;
        br_clock_experiment.invalid = 0u;
        br_clock_experiment.wait_transport = 0u;
        br_midi_clock_active = 0u;
        if (!br_midi_clock_enabled) br_clock_experiment.anchor.valid = 0u;
        br_clock_source_prev = !!br_midi_clock_enabled;
        br_clock_generation_seen = br_midi_clock_generation;
    }
#endif
    br_take_events();
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    br_clock_project();
    if (br_clock_experiment.enabled && br_clock_experiment.period_q16)
        br_clock_experiment.phase_end_q16 = br_clock_experiment.phase_q16 +
            fm1_clock_div64((uint64_t)n << 32, br_clock_experiment.period_q16, 0);
#endif
    if (!br_playing && br_transport_prev) {
        for (p = 0; p < BR_PARTS; ++p) br_stop_part(p);
        br_metro_frames = 0u;
        br_transport_phase = 0u;
        br_global_beat = 0u;
    } else if (br_playing && !br_transport_prev) {
        br_transport_reset_position();
    }
    br_transport_prev = br_playing;
#if defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE
    br_pattern_irq_normalize_internal();
    br_pattern_irq_check_external_gap();
    br_pattern_host_begin_block();
    if (br_playing) br_pattern_host_end_block(n);
#endif
    if (br_playing) {
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
        if (br_clock_experiment.enabled && br_clock_experiment.period_q16) {
            uint64_t rem, phase_end = br_clock_experiment.phase_end_q16;
            uint64_t beat = fm1_clock_div64(phase_end, 24u << 16, &rem);
            br_global_beat = (uint32_t)beat & 3u;
            br_metro_frames = (uint32_t)((rem * br_clock_experiment.period_q16) >> 32);
            br_transport_phase = ((uint32_t)rem * 128u) / (24u << 16);
        } else
#endif
        {
            uint32_t quarter = br_tempo_quarter_frames();
            if (br_metro_frames + n >= quarter) br_global_beat = (br_global_beat + 1u) & 3u;
            br_metro_frames = (br_metro_frames + n) % quarter;
            br_transport_phase = br_metro_frames * 128u / quarter;
        }
    } else if (!br_midi_paused) {
        br_transport_phase = 0u;
    }
    for (p = 0; p < BR_SYNTH_PARTS; ++p) br_arp_process(p, n);
    for (p = 0; p < BR_PARTS; ++p) br_seq_process(p, n);
#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
    if (br_clock_experiment.anchor.valid) br_clock_experiment.render_frame += n;
#endif
    for (p = 0; p < BR_PARTS; ++p) {
        br_build_effective(p);
        br_select_mode(p);
    }
    br_smooth_sends();
    br_snapshot_source_compressor();
    br_prepare_kit_source_controls();
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    source_wall_start = fm1_ticks();
#endif
#ifdef BR_SPLIT_AUDIO
    /* In reverse mode CPU1 starts the previous-frame FX batch before CPU0
     * renders the current source frame. The two frames occupy separate slots. */
    if (br_pipeline_enabled && br_pipeline_mode == 2u)
        br_engine_pipeline_process_pending();
#endif
#ifdef BR_SPLIT_AUDIO
    br_audio_source_fx_fusion_active =
        br_audio_source_fx_fusion && !br_pipeline_enabled && n != 0u;
    if (br_audio_source_fx_fusion_active &&
        !br_audio_call_fx_fusion_prepare(bus, n))
        br_audio_source_fx_fusion_active = 0u;
    if (br_audio_source_fx_fusion_active) {
        source_job_count = br_audio_build_fusion_jobs(bus, n);
        br_audio_fusion_cost_hints(source_job_count);
    } else {
    for (p = 0; p < BR_PARTS; ++p) {
        if (br_mode[p] == 1u || br_mode[p] == 4u) {
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
            uint32_t lane;
            if (br_mode[p] != 4u) {
                memset(br_audio_kit_partial[0], 0, sizeof br_audio_kit_partial[0]);
                if (!br_audio_kit_key_elision_active())
                    memset(br_audio_kit_key_partial[0], 0,
                           sizeof br_audio_kit_key_partial[0]);
            }
            for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
                uint32_t job = source_job_count++;
                br_audio_source_jobs[job].part = p;
                br_audio_source_jobs[job].frames = n;
                br_audio_source_jobs[job].output = br_audio_kit_partial[0][lane];
                br_audio_source_jobs[job].lane_mask = 1u << lane;
                br_audio_source_jobs[job].key = br_audio_kit_key_output(
                    p, lane, 1u << lane);
                br_audio_source_jobs[job].predecessor = UINT32_MAX;
                br_audio_source_jobs[job].predecessor2 = UINT32_MAX;
                br_audio_source_jobs[job].predecessor3 = UINT32_MAX;
                br_audio_source_jobs[job].predecessor4 = UINT32_MAX;
            }
#else
            uint32_t job_a, job_b;
            if (br_mode[p] != 4u) {
                memset(br_audio_kit_partial[0], 0, sizeof br_audio_kit_partial[0]);
                if (!br_audio_kit_key_elision_active())
                    memset(br_audio_kit_key_partial[0], 0,
                           sizeof br_audio_kit_key_partial[0]);
            }
            if (!br_audio_source_legacy_mode && br_mode[p] == 4u && n == BR_BLOCK) {
                job_a = source_job_count; source_job_count += 2u;
                job_b = source_job_count; source_job_count += 2u;
                br_audio_source_jobs[job_a].part = p;
                br_audio_source_jobs[job_a].frames = BR_BLOCK / 2u;
                br_audio_source_jobs[job_a].output = br_audio_kit_partial[0][0];
                br_audio_source_jobs[job_a].lane_mask = BR_KIT_LANES_A;
                br_audio_source_jobs[job_a].key = br_audio_kit_key_output(
                    p, 0u, BR_KIT_LANES_A);
                br_audio_source_jobs[job_a].predecessor = UINT32_MAX;
                br_audio_source_jobs[job_a].predecessor2 = UINT32_MAX;
                br_audio_source_jobs[job_a].predecessor3 = UINT32_MAX;
                br_audio_source_jobs[job_a].predecessor4 = UINT32_MAX;
                br_audio_source_jobs[job_a + 1u] = br_audio_source_jobs[job_a];
                br_audio_source_jobs[job_a + 1u].output += BR_BLOCK / 2u;
                if (br_audio_source_jobs[job_a + 1u].key)
                    br_audio_source_jobs[job_a + 1u].key += BR_BLOCK / 2u;
                br_audio_source_jobs[job_a + 1u].predecessor = job_a;
                br_audio_source_jobs[job_b].part = p;
                br_audio_source_jobs[job_b].frames = BR_BLOCK / 2u;
                br_audio_source_jobs[job_b].output = br_audio_kit_partial[0][1];
                br_audio_source_jobs[job_b].lane_mask = BR_KIT_LANES_B;
                br_audio_source_jobs[job_b].key = br_audio_kit_key_output(
                    p, 1u, BR_KIT_LANES_B);
                br_audio_source_jobs[job_b].predecessor = UINT32_MAX;
                br_audio_source_jobs[job_b].predecessor2 = UINT32_MAX;
                br_audio_source_jobs[job_b].predecessor3 = UINT32_MAX;
                br_audio_source_jobs[job_b].predecessor4 = UINT32_MAX;
                br_audio_source_jobs[job_b + 1u] = br_audio_source_jobs[job_b];
                br_audio_source_jobs[job_b + 1u].output += BR_BLOCK / 2u;
                if (br_audio_source_jobs[job_b + 1u].key)
                    br_audio_source_jobs[job_b + 1u].key += BR_BLOCK / 2u;
                br_audio_source_jobs[job_b + 1u].predecessor = job_b;
            } else {
                job_a = source_job_count++;
                job_b = source_job_count++;
                br_audio_source_jobs[job_a].part = p;
                br_audio_source_jobs[job_a].frames = n;
                br_audio_source_jobs[job_a].output = br_audio_kit_partial[0][0];
                br_audio_source_jobs[job_a].lane_mask = BR_KIT_LANES_A;
                br_audio_source_jobs[job_a].key = br_audio_kit_key_output(
                    p, 0u, BR_KIT_LANES_A);
                br_audio_source_jobs[job_a].predecessor = UINT32_MAX;
                br_audio_source_jobs[job_a].predecessor2 = UINT32_MAX;
                br_audio_source_jobs[job_a].predecessor3 = UINT32_MAX;
                br_audio_source_jobs[job_a].predecessor4 = UINT32_MAX;
                br_audio_source_jobs[job_b].part = p;
                br_audio_source_jobs[job_b].frames = n;
                br_audio_source_jobs[job_b].output = br_audio_kit_partial[0][1];
                br_audio_source_jobs[job_b].lane_mask = BR_KIT_LANES_B;
                br_audio_source_jobs[job_b].key = br_audio_kit_key_output(
                    p, 1u, BR_KIT_LANES_B);
                br_audio_source_jobs[job_b].predecessor = UINT32_MAX;
                br_audio_source_jobs[job_b].predecessor2 = UINT32_MAX;
                br_audio_source_jobs[job_b].predecessor3 = UINT32_MAX;
                br_audio_source_jobs[job_b].predecessor4 = UINT32_MAX;
            }
#endif
        } else {
            uint32_t job;
            if (!br_audio_source_legacy_mode && (br_mode[p] == 2u || br_mode[p] == 3u) &&
                n == BR_BLOCK) {
                job = source_job_count; source_job_count += 2u;
                br_audio_source_jobs[job].part = p;
                br_audio_source_jobs[job].frames = BR_BLOCK / 2u;
                br_audio_source_jobs[job].output = bus[p];
                br_audio_source_jobs[job].lane_mask = BR_DRUM_ALL_MASK;
                br_audio_source_jobs[job].key = 0;
                br_audio_source_jobs[job].predecessor = UINT32_MAX;
                br_audio_source_jobs[job].predecessor2 = UINT32_MAX;
                br_audio_source_jobs[job].predecessor3 = UINT32_MAX;
                br_audio_source_jobs[job].predecessor4 = UINT32_MAX;
                br_audio_source_jobs[job + 1u] = br_audio_source_jobs[job];
                br_audio_source_jobs[job + 1u].output += BR_BLOCK / 2u;
                br_audio_source_jobs[job + 1u].predecessor = job;
            } else {
                job = source_job_count++;
                br_audio_source_jobs[job].part = p;
                br_audio_source_jobs[job].frames = n;
                br_audio_source_jobs[job].output = bus[p];
                br_audio_source_jobs[job].lane_mask = BR_DRUM_ALL_MASK;
                br_audio_source_jobs[job].key = 0;
                br_audio_source_jobs[job].predecessor = UINT32_MAX;
                br_audio_source_jobs[job].predecessor2 = UINT32_MAX;
                br_audio_source_jobs[job].predecessor3 = UINT32_MAX;
                br_audio_source_jobs[job].predecessor4 = UINT32_MAX;
            }
        }
    }
    }
    br_audio_source_queue.count = source_job_count;
    br_audio_source_queue.completed = 0u;
    br_audio_source_queue.lock = 0u;
    br_audio_c1_late_record.valid = 0u;
#if BR_AUDIO_FUSION_FIXED_PLAN
    br_audio_fixed_plan_disable();
#endif
#if defined(FM1_DEVICE) || defined(BR_SOURCE_QUEUE_TEST)
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG && defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    if (br_pipeline_enabled && br_pipeline_source_deadline_valid) {
        br_audio_source_queue.deadline_tick = br_pipeline_source_deadline_tick;
        failure_queue_origin_tick = br_audio_source_queue.deadline_tick -
                                    BR_AUDIO_SOURCE_DRAIN_TICKS;
        failure_queue_origin_estimated = 1u;
    } else {
        failure_queue_origin_tick = br_audio_source_now();
        br_audio_source_queue.deadline_tick = failure_queue_origin_tick +
                                              BR_AUDIO_SOURCE_DRAIN_TICKS;
    }
#else
    br_audio_source_queue.deadline_tick =
        (br_pipeline_enabled && br_pipeline_source_deadline_valid) ?
        br_pipeline_source_deadline_tick :
        br_audio_source_now() + BR_AUDIO_SOURCE_DRAIN_TICKS;
#endif
#else
    br_audio_source_queue.deadline_tick = 0u;
#endif
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
    br_audio_failure_job_profile.flags = 0u;
#endif
    for (i = 0; i < source_job_count; ++i) {
        br_audio_source_jobs[i].state = 0u;
        br_audio_source_jobs[i].duration_ticks = 0u;
        br_audio_source_jobs[i].executor_core = UINT32_MAX;
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
        br_audio_source_jobs[i].failure_profile_start_tick = 0u;
        br_audio_source_jobs[i].failure_profile_flags = 0u;
#endif
#if BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
        br_audio_source_jobs[i].failure_profile_stage_flags = 0u;
        br_audio_source_jobs[i].failure_profile_stage_start_tick = 0u;
        br_audio_source_jobs[i].failure_profile_source_ticks = 0u;
        br_audio_source_jobs[i].failure_profile_merge_ticks = 0u;
        br_audio_source_jobs[i].failure_profile_post_ticks = 0u;
        br_audio_source_jobs[i].failure_profile_fx_ticks = 0u;
#endif
        if (!br_audio_source_fx_fusion_active) {
            br_audio_source_jobs[i].affinity_core = UINT32_MAX;
            br_audio_source_jobs[i].predecessor2 = UINT32_MAX;
            br_audio_source_jobs[i].kind = BR_AUDIO_JOB_SOURCE;
            br_audio_source_jobs[i].cost_hint = 0u;
        }
    }
#if BR_AUDIO_FUSION_FIXED_PLAN
    if (br_audio_source_fx_fusion_active)
        (void)br_audio_fixed_plan_prepare(source_job_count);
#endif
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG
    if (br_audio_source_fx_fusion_active)
        br_audio_job_timing_begin(n, source_job_count);
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG && defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    if (br_audio_job_timing_collect) {
        failure_queue_source_start_tick = br_audio_job_timing_queue_start;
        failure_queue_source_start_valid = 1u;
    }
#endif
#endif
    br_audio_source_publish();
#if defined(BR_SOURCE_QUEUE_TEST)
    if (br_audio_source_test_parallel &&
        br_audio_source_can_publish_late(n, source_job_count)) {
        br_audio_source_publication_enabled = 1u;
        br_audio_source_publish();
    }
    br_audio_source_test_reset(source_job_count);
#if BR_AUDIO_FUSION_FIXED_PLAN
    if (br_audio_source_queue.fixed_plan_active &&
        (!br_audio_source_test_parallel || source_job_count < 2u))
        br_audio_fixed_plan_disable();
#endif
    if (br_audio_source_fx_fusion_active && br_audio_source_test_parallel &&
        source_job_count > 1u) {
        uint32_t cpu0_ok, worker_ok;
        if (!br_audio_source_test_start(&br_audio_source_queue)) {
            br_audio_source_worker_fault = 1u;
            br_audio_source_fusion_cancel();
            for (p = 0; p < BR_PARTS; ++p)
                for (i = 0; i < n; ++i) bus[p][i] = 0;
            return;
        }
#if defined(BR_LATE_COMPLETION_TEST)
        late_deadline = br_audio_source_queue.deadline_tick;
        cpu0_ok = br_audio_source_queue_run_internal(
            br_audio_source_queue_wait_policy(), &late_job);
#else
        cpu0_ok = br_audio_source_queue_run(br_audio_source_queue_wait_policy());
#endif
#if defined(BR_C1_LATE_COMPLETION_TEST)
        { extern void br_audio_source_test_join_boundary(void);
          br_audio_source_test_join_boundary(); }
#endif
#if defined(BR_DIAGNOSTIC_TEST)
        worker_ok = br_audio_source_test_join_ticks(
            br_audio_source_join_timeout_ticks());
#else
        worker_ok = br_audio_source_test_join();
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN
        if (worker_ok) (void)br_audio_fixed_plan_seal_completed();
#endif
#if defined(BR_C1_LATE_COMPLETION_TEST)
        if (br_audio_source_publication_enabled) {
            extern const fm1_cpu1_worker_mailbox_t *br_audio_source_test_late_mailbox(void);
            extern uint32_t br_audio_source_test_late_sequence(void);
            const fm1_cpu1_worker_mailbox_t *m = br_audio_source_test_late_mailbox();
            uint32_t completed = 0u, now = br_audio_source_now();
            uint32_t result = m ? m->result : BR_AUDIO_QUEUE_FAILED;
            late_sequence = br_audio_source_test_late_sequence();
            if (worker_ok &&
                       (cpu0_ok == BR_AUDIO_QUEUE_OK ||
                        cpu0_ok == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                        cpu0_ok == BR_AUDIO_QUEUE_CUTOFF_DRAINED) &&
                       (!br_audio_source_queue.fixed_plan_active ||
                        (cpu0_ok != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
                         result != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
                         (cpu0_ok != BR_AUDIO_QUEUE_OK ||
                          br_audio_source_queue.fixed_plan_cursor[0] ==
                              br_audio_source_queue.fixed_plan_count[0]) &&
                         (result != BR_AUDIO_QUEUE_OK ||
                          br_audio_source_queue.fixed_plan_cursor[1] ==
                              br_audio_source_queue.fixed_plan_count[1]))) &&
                       ((cpu0_ok == BR_AUDIO_QUEUE_OK && result == BR_AUDIO_QUEUE_OK) ||
                        (int32_t)(now - late_deadline) >= 0) &&
                       br_audio_source_joined_graph_valid(
                           bus, n, source_job_count, late_deadline, m,
                           late_sequence, FM1_CPU1_WORKER_OK, result,
                           br_engine_pipeline_uncontained_fault_get() |
                               br_audio_source_worker_fault, &completed)) {
                if (completed < source_job_count) {
                    if (cpu0_ok != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
                        result != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
                        (cpu0_ok == BR_AUDIO_QUEUE_CUTOFF_DRAINED ||
                         result == BR_AUDIO_QUEUE_CUTOFF_DRAINED) &&
                        (int32_t)(now - late_deadline) >= 0) {
                        br_audio_source_drop_partial(source_job_count,
                            late_deadline, late_sequence, br_audio_source_now());
                        return;
                    }
                    cpu0_ok = BR_AUDIO_QUEUE_FAILED;
                } else {
                    cpu0_ok = BR_AUDIO_QUEUE_OK;
                    complete_watch = 1u;
                }
            } else cpu0_ok = BR_AUDIO_QUEUE_FAILED;
        }
#endif
#if defined(BR_LATE_COMPLETION_TEST)
        if (!br_audio_source_publication_enabled &&
            cpu0_ok == BR_AUDIO_QUEUE_COMPLETE_LATE && worker_ok) {
            extern const fm1_cpu1_worker_mailbox_t *br_audio_source_test_late_mailbox(void);
            extern uint32_t br_audio_source_test_late_sequence(void);
            const fm1_cpu1_worker_mailbox_t *m = br_audio_source_test_late_mailbox();
            uint32_t joined_result = m ? m->result : BR_AUDIO_QUEUE_FAILED;
            late_sequence = br_audio_source_test_late_sequence();
            if ((joined_result == BR_AUDIO_QUEUE_OK ||
                 joined_result == BR_AUDIO_QUEUE_CUTOFF_DRAINED) &&
                br_audio_source_late_join_valid(
                    m, late_sequence,
                    FM1_CPU1_WORKER_OK, joined_result, joined_result,
                    br_engine_pipeline_uncontained_fault_get() |
                        br_audio_source_worker_fault) &&
                (!BR_AUDIO_C1_LATE_OWNER_KNOWN ||
                 br_audio_source_jobs[7].executor_core == 0u) &&
                br_audio_source_accept_complete_late(
                    bus, n, source_job_count, late_deadline, &late_job)) {
                cpu0_ok = BR_AUDIO_QUEUE_OK;
                late_accepted = 1u;
            } else cpu0_ok = BR_AUDIO_QUEUE_FAILED;
        }
#endif
#if defined(BR_C1_LATE_COMPLETION_TEST)
        {
            extern const fm1_cpu1_worker_mailbox_t *br_audio_source_test_late_mailbox(void);
            const fm1_cpu1_worker_mailbox_t *m = br_audio_source_test_late_mailbox();
            if (!br_audio_source_publication_enabled &&
                (cpu0_ok == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                 cpu0_ok == BR_AUDIO_QUEUE_CUTOFF_DRAINED ||
                 (m && (m->result == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                        m->result == BR_AUDIO_QUEUE_CUTOFF_DRAINED))) &&
                (!BR_AUDIO_C1_LATE_OWNER_KNOWN || !BR_AUDIO_FUSION_ONLY_BUILD ||
                 !BR_AUDIO_FUSION_FOUR_WAY_KIT || n != BR_BLOCK ||
                 BR_BLOCK != FM1_AUDIO_HALF_FRAMES || source_job_count != 8u ||
                 (cpu0_ok == BR_AUDIO_QUEUE_CUTOFF_DRAINED && m &&
                  m->result == BR_AUDIO_QUEUE_CUTOFF_DRAINED) ||
                 br_audio_source_queue.fixed_plan_active)) {
                br_audio_source_worker_fault = 1u;
                br_audio_source_fusion_cancel();
                return;
            }
        }
#endif
        if ((cpu0_ok != BR_AUDIO_QUEUE_OK &&
             cpu0_ok != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
             cpu0_ok != BR_AUDIO_QUEUE_CUTOFF_DRAINED) || !worker_ok ||
            br_audio_source_queue.completed != br_audio_source_queue.count ||
            !br_audio_source_test_done(source_job_count)) {
            br_audio_source_worker_fault = 1u;
            br_audio_source_fusion_cancel();
            for (p = 0; p < BR_PARTS; ++p)
                for (i = 0; i < n; ++i) bus[p][i] = 0;
            return;
        }
#if defined(BR_C1_LATE_COMPLETION_TEST) && BR_AUDIO_C1_LATE_OWNER_KNOWN
        if (!br_audio_source_publication_enabled && !late_accepted && BR_AUDIO_FUSION_ONLY_BUILD &&
            BR_AUDIO_FUSION_FOUR_WAY_KIT && n == BR_BLOCK &&
            BR_BLOCK == FM1_AUDIO_HALF_FRAMES && source_job_count == 8u) {
            extern const fm1_cpu1_worker_mailbox_t *br_audio_source_test_late_mailbox(void);
            extern uint32_t br_audio_source_test_late_sequence(void);
            const fm1_cpu1_worker_mailbox_t *m = br_audio_source_test_late_mailbox();
            late_sequence = br_audio_source_test_late_sequence();
                if (!br_audio_source_complete_watch_valid(bus, n, source_job_count,
                    late_deadline, m, late_sequence,
                    FM1_CPU1_WORKER_OK, m ? m->result : 0u,
                    br_engine_pipeline_uncontained_fault_get() |
                        br_audio_source_worker_fault)) {
                br_audio_source_worker_fault = 1u;
                br_audio_source_fusion_cancel();
                return;
            }
            complete_watch = 1u;
            late_executor = br_audio_source_jobs[7].executor_core;
            cpu0_ok = BR_AUDIO_QUEUE_OK;
        }
#endif
    } else if (br_pipeline_enabled && br_pipeline_mode == 3u) {
        uint32_t cpu0_ok;
        br_engine_pipeline_process_hybrid_pending();
        cpu0_ok = br_audio_source_queue_run(br_audio_source_queue_wait_policy());
        if (!cpu0_ok) {
            br_audio_source_worker_fault = 1u;
            return; /* app_audio() joins ID4 before touching frame/output state. */
        }
    } else if (br_pipeline_enabled && br_pipeline_mode == 2u && br_audio_source_test_parallel) {
        if (!br_audio_source_dispatch_local(1u, 0u, 0u) ||
            br_audio_source_queue.completed != br_audio_source_queue.count ||
            !br_audio_source_test_done(source_job_count)) {
            br_audio_source_worker_fault = 1u;
            return;
        }
    } else if (br_pipeline_enabled && br_audio_source_test_parallel && source_job_count > 1u) {
        uint32_t worker_ok;
        if (!br_audio_source_test_start(&br_audio_source_queue)) {
            br_audio_source_worker_fault = 1u;
            return;
        }
        br_engine_pipeline_process_pending();
#if defined(BR_DIAGNOSTIC_TEST)
        worker_ok = br_audio_source_test_join_ticks(
            br_audio_source_join_timeout_ticks());
#else
        worker_ok = br_audio_source_test_join();
#endif
        if (!worker_ok || br_audio_source_queue.completed != br_audio_source_queue.count ||
            !br_audio_source_test_done(source_job_count)) {
            br_audio_source_worker_fault = 1u;
            return;
        }
    } else if (br_audio_source_legacy_mode && br_audio_source_test_parallel &&
        source_job_count > 1u) {
        uint32_t cpu0_ok, worker_ok;
        br_audio_build_source_batches(source_job_count);
        if (!br_audio_source_test_start(&br_audio_legacy_batches[1])) {
            for (p = 0; p < BR_PARTS; ++p)
                for (i = 0; i < n; ++i) bus[p][i] = 0;
            return;
        }
        cpu0_ok = br_audio_run_source_batch_cpu0(&br_audio_legacy_batches[0]);
        worker_ok = br_audio_source_test_join();
        if (!cpu0_ok || !worker_ok || !br_audio_source_test_done(source_job_count)) {
            for (p = 0; p < BR_PARTS; ++p)
                for (i = 0; i < n; ++i) bus[p][i] = 0;
            br_audio_source_fusion_cancel();
            return;
        }
    } else if (br_audio_source_test_parallel && source_job_count > 1u) {
        uint32_t cpu0_ok, worker_ok;
        if (!br_audio_source_test_start(&br_audio_source_queue)) {
            for (p = 0; p < BR_PARTS; ++p)
                for (i = 0; i < n; ++i) bus[p][i] = 0;
            return;
        }
        cpu0_ok = br_audio_source_queue_run(1u);
        worker_ok = br_audio_source_test_join();
        if (!cpu0_ok || !worker_ok ||
            br_audio_source_queue.completed != br_audio_source_queue.count) {
            for (p = 0; p < BR_PARTS; ++p)
                for (i = 0; i < n; ++i) bus[p][i] = 0;
            br_audio_source_fusion_cancel();
            return;
        }
        if (!br_audio_source_test_done(source_job_count)) {
            for (p = 0; p < BR_PARTS; ++p)
                for (i = 0; i < n; ++i) bus[p][i] = 0;
            br_audio_source_fusion_cancel();
            return;
        }
    } else if (!br_audio_source_queue_run(br_audio_source_queue_wait_policy())) {
        for (p = 0; p < BR_PARTS; ++p)
            for (i = 0; i < n; ++i) bus[p][i] = 0;
        br_audio_source_fusion_cancel();
        return;
    } else if (!br_audio_source_test_done(source_job_count)) {
        for (p = 0; p < BR_PARTS; ++p)
            for (i = 0; i < n; ++i) bus[p][i] = 0;
        br_audio_source_fusion_cancel();
        return;
    }
#elif defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)
    {
        uint32_t use_cpu1 = 0u, submitted = 0u, sequence = 0u;
        uint32_t worker_status = FM1_CPU1_WORKER_NOT_READY, worker_result = 0u;
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        uint32_t batches_built = 0u;
#endif
        uint32_t reverse_sources = br_pipeline_enabled && br_pipeline_mode == 2u;
        uint32_t hybrid_sources = br_pipeline_enabled && br_pipeline_mode == 3u;
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        /* This bounded trace covers legacy source assignment with pipeline
         * mode off. Reverse, hybrid, and queue-mode dispatch are unsupported. */
        runtime_trace_chunk = n == BR_BLOCK && br_audio_source_legacy_mode &&
                              !br_pipeline_enabled;
        if (runtime_trace_chunk) {
            fm1_runtime_trace_chunk_begin(source_job_count);
            br_audio_runtime_trace_chunk_active = 1u;
        }
#endif
        if (hybrid_sources) br_engine_pipeline_process_hybrid_pending();
        if (!reverse_sources && !hybrid_sources &&
            !br_audio_source_worker_fault && source_job_count > 1u &&
            fm1_cpu1_worker_available()) {
            if (br_pipeline_enabled) use_cpu1 = 1u;
            else if (br_audio_source_legacy_mode &&
                     !br_audio_source_fx_fusion_active) {
                br_audio_build_source_batches(source_job_count);
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
                batches_built = 1u;
#endif
                use_cpu1 = br_audio_legacy_batches[1].count != 0u;
            } else use_cpu1 = 1u;
        }
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        if (runtime_trace_chunk && !batches_built)
            br_audio_runtime_trace_direct_assignments(source_job_count);
#endif
        if (use_cpu1) {
            void *context = br_pipeline_enabled || br_audio_source_fx_fusion_active ?
                (void *)&br_audio_source_queue :
                br_audio_source_legacy_mode ?
                (void *)&br_audio_legacy_batches[1] : (void *)&br_audio_source_queue;
            if (context == &br_audio_source_queue &&
                br_audio_source_can_publish_late(n, source_job_count)) {
                br_audio_source_publication_enabled = 1u;
                br_audio_source_publish();
            }
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
            if (runtime_trace_chunk)
                fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_SUBMIT_BEGIN);
#endif
            worker_status = fm1_cpu1_worker_submit_audio_batch(
                context, &sequence);
            submitted = worker_status == FM1_CPU1_WORKER_OK;
            if (!submitted) br_audio_source_publication_enabled = 0u;
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
            if (runtime_trace_chunk)
                fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_SUBMIT_END);
#endif
        }
#if BR_AUDIO_FUSION_FIXED_PLAN
        /* The plan is valid only with two confirmed owners. A failed submit
         * never published a worker context, so CPU0 can safely use the legacy
         * dynamic queue after clearing the plan. */
        if (br_audio_source_queue.fixed_plan_active &&
            (!use_cpu1 || !submitted))
            br_audio_fixed_plan_disable();
#endif
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        if (runtime_trace_chunk && !use_cpu1) {
            /* No worker was eligible: preserve the complete marker schema with
             * explicit zero-duration submit and wait intervals. */
            fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_SUBMIT_BEGIN);
            fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_SUBMIT_END);
        }
        if (runtime_trace_chunk && (!use_cpu1 || !submitted))
            fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_CPU0_BATCH_BEGIN);
#endif
        if (!br_audio_source_dispatch_local(reverse_sources || hybrid_sources,
                use_cpu1, submitted)) {
            br_audio_source_worker_fault = 1u;
            if (hybrid_sources)
                return; /* Do not read current buses while C1 may own a job. */
        }
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
        if (runtime_trace_chunk && (!use_cpu1 || !submitted)) {
            fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_CPU0_BATCH_END);
            /* The failed submit path falls back to a complete local render. */
            br_audio_runtime_trace_chunk_active = 0u;
            if (!submitted) {
                /* There is no worker to wait for after local fallback. */
                fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_WAIT_BEGIN);
                fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_WAIT_END);
            }
        }
#endif
        if (use_cpu1 && submitted) {
            uint32_t cpu0_ok;
            uint32_t c1_extended = 0u;
#if BR_AUDIO_FUSION_FIXED_PLAN
            uint32_t fixed_complete = 1u;
#endif
            if (br_pipeline_enabled) {
                br_engine_pipeline_process_pending();
                cpu0_ok = 1u;
            } else if (br_audio_source_legacy_mode &&
                       !br_audio_source_fx_fusion_active) {
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
                if (runtime_trace_chunk)
                    fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_CPU0_BATCH_BEGIN);
#endif
                cpu0_ok = br_audio_run_source_batch_cpu0(&br_audio_legacy_batches[0]);
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
                if (runtime_trace_chunk)
                    fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_CPU0_BATCH_END);
#endif
            } else {
                late_deadline = br_audio_source_queue.deadline_tick;
                cpu0_ok = br_audio_source_queue_run_internal(
                    br_audio_source_queue_wait_policy(),
                    br_audio_source_fx_fusion_active && !br_pipeline_enabled ?
                        &late_job : 0);
            }
            uint32_t join_timeout = br_audio_source_join_timeout_ticks();
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
            if (runtime_trace_chunk)
                fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_WAIT_BEGIN);
#endif
            if (BR_AUDIO_C1_LATE_OWNER_KNOWN && BR_AUDIO_FUSION_ONLY_BUILD &&
                BR_AUDIO_FUSION_FOUR_WAY_KIT && n == BR_BLOCK &&
                BR_BLOCK == FM1_AUDIO_HALF_FRAMES &&
                (cpu0_ok == BR_AUDIO_QUEUE_OK ||
                 cpu0_ok == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                 cpu0_ok == BR_AUDIO_QUEUE_CUTOFF_DRAINED) &&
                br_audio_source_fx_fusion_active && !br_pipeline_enabled &&
                !br_audio_source_queue.fixed_plan_active && br_audio_graph_count_ok(source_job_count)) {
                /* A soft cutoff is not proof that C1 is hung. Observe without
                 * resetting until that cutoff, then extend only for the exact
                 * already-claimed finalizer after a deadline-bounded lock retry. */
                uint32_t hard_deadline = late_deadline + FM1_AUDIO_HALF_TICKS;
                uint32_t soft = fm1_cpu1_worker_poll_until(
                    sequence, late_deadline, &worker_result);
                if (soft == FM1_CPU1_WORKER_OK) {
                    /* The matching completion was acquired before cutoff;
                     * record the ordinary successful join without resetting. */
                    worker_status = fm1_cpu1_worker_wait(sequence, 1u,
                                                          &worker_result);
                } else if (fm1_cpu1_worker_fault ||
                           br_engine_pipeline_uncontained_fault_get() ||
                           br_audio_source_worker_fault || !sequence ||
                           fm1_cpu1_worker_mailbox.cpu_id != 1u ||
                           fm1_cpu1_worker_mailbox.request_seq != sequence ||
                           fm1_cpu1_worker_mailbox.task_id !=
                               FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH ||
                           fm1_cpu1_worker_mailbox.task_context !=
                               (uint32_t)(uintptr_t)&br_audio_source_queue) {
                    worker_status = FM1_CPU1_WORKER_TIMEOUT;
                } else if (br_audio_source_publication_enabled) {
                    /* The already-submitted worker may be retiring a claimed
                     * job. Do not hold its queue lock or sample mutable job
                     * state; the joined graph audit below is authoritative. */
                    worker_status = fm1_cpu1_worker_wait_until(
                        sequence, hard_deadline, &worker_result);
                } else {
                    c1_extended = br_audio_source_c1_late_preflight(
                        bus, n, source_job_count, late_deadline, hard_deadline,
                        &fm1_cpu1_worker_mailbox.done_seq, sequence);
                    if (c1_extended == BR_AUDIO_C1_PREFLIGHT_CLAIMED ||
                        c1_extended == BR_AUDIO_C1_PREFLIGHT_COMPLETE)
                        worker_status = fm1_cpu1_worker_wait_until(
                            sequence, hard_deadline, &worker_result);
                    else {
                        /* Terminal means the original budget/spin guard was
                         * exhausted. Record once without sampling C1 again;
                         * the existing common failure path holds/resets it. */
                        if (c1_extended == BR_AUDIO_C1_PREFLIGHT_TERMINAL)
                            fm1_cpu1_worker_note_terminal_timeout(sequence);
                        worker_status = FM1_CPU1_WORKER_TIMEOUT;
                    }
                }
            } else {
                if (BR_AUDIO_C1_LATE_OWNER_KNOWN && BR_AUDIO_FUSION_ONLY_BUILD &&
                    BR_AUDIO_FUSION_FOUR_WAY_KIT && n == BR_BLOCK &&
                    BR_BLOCK == FM1_AUDIO_HALF_FRAMES &&
                    cpu0_ok == BR_AUDIO_QUEUE_COMPLETE_LATE &&
                    br_audio_source_fx_fusion_active && !br_pipeline_enabled &&
                    !br_audio_source_queue.fixed_plan_active && br_audio_graph_count_ok(source_job_count)) {
                    if (fm1_cpu1_worker_fault ||
                        br_engine_pipeline_uncontained_fault_get() ||
                        br_audio_source_worker_fault ||
                        !br_audio_source_c0_late_ack_ready(
                            bus, n, source_job_count, late_deadline,
                            &fm1_cpu1_worker_mailbox, sequence, &late_job)) {
                        worker_status = FM1_CPU1_WORKER_TIMEOUT;
                    } else {
                        worker_status = fm1_cpu1_worker_wait_until(
                            sequence, late_deadline + FM1_AUDIO_HALF_TICKS,
                            &worker_result);
                    }
                }
#if BR_AUDIO_FUSION_FIXED_PLAN
                else if (BR_AUDIO_C1_LATE_OWNER_KNOWN && BR_AUDIO_FUSION_ONLY_BUILD &&
                    BR_AUDIO_FUSION_FOUR_WAY_KIT && n == BR_BLOCK &&
                    BR_BLOCK == FM1_AUDIO_HALF_FRAMES &&
                    (cpu0_ok == BR_AUDIO_QUEUE_OK ||
                     (br_audio_source_publication_enabled &&
                      cpu0_ok == BR_AUDIO_QUEUE_CUTOFF_DRAINED)) &&
                    br_audio_source_fx_fusion_active && !br_pipeline_enabled &&
                    br_audio_source_queue.fixed_plan_active && source_job_count == 8u) {
                    uint32_t soft = fm1_cpu1_worker_poll_until(
                        sequence, late_deadline, &worker_result);
                    if (soft == FM1_CPU1_WORKER_OK) {
                        worker_status = fm1_cpu1_worker_wait(
                            sequence, 1u, &worker_result);
                    } else if (fm1_cpu1_worker_fault ||
                               br_engine_pipeline_uncontained_fault_get() ||
                               br_audio_source_worker_fault || !sequence ||
                               fm1_cpu1_worker_mailbox.cpu_id != 1u ||
                               fm1_cpu1_worker_mailbox.request_seq != sequence ||
                               fm1_cpu1_worker_mailbox.task_id !=
                                   FM1_CPU1_WORKER_TASK_AUDIO_SOURCE_BATCH ||
                               fm1_cpu1_worker_mailbox.task_context !=
                                   (uint32_t)(uintptr_t)&br_audio_source_queue) {
                        worker_status = FM1_CPU1_WORKER_TIMEOUT;
                    } else if (!br_audio_source_publication_enabled &&
                               !br_audio_source_fixed_published_ready(
                                   n, source_job_count, late_deadline,
                                   &fm1_cpu1_worker_mailbox, sequence)) {
                        worker_status = FM1_CPU1_WORKER_TIMEOUT;
                    } else {
                        worker_status = fm1_cpu1_worker_wait_until(
                            sequence, late_deadline + FM1_AUDIO_HALF_TICKS,
                            &worker_result);
                    }
                }
#endif
                else {
                    worker_status = fm1_cpu1_worker_wait(sequence,
                        join_timeout, &worker_result);
                }
            }
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
            if (runtime_trace_chunk)
                fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_WAIT_END);
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
            if (worker_status == FM1_CPU1_WORKER_OK &&
                (worker_result == BR_AUDIO_QUEUE_OK ||
                 worker_result == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                 worker_result == BR_AUDIO_QUEUE_CUTOFF_DRAINED))
                fm1_audio_cpu1_join_mark(fm1_ticks());
#endif
#if BR_AUDIO_FUSION_FIXED_PLAN
            if (br_audio_source_queue.fixed_plan_active &&
                worker_status == FM1_CPU1_WORKER_OK)
                fixed_complete = br_audio_fixed_plan_seal_completed();
#endif
            if (br_audio_source_publication_enabled) {
                uint32_t completed = 0u;
                uint32_t now = br_audio_source_now();
                uint32_t faults = fm1_cpu1_worker_fault |
                    br_engine_pipeline_uncontained_fault_get() |
                    br_audio_source_worker_fault;
                if ((cpu0_ok != BR_AUDIO_QUEUE_OK &&
                     cpu0_ok != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
                     cpu0_ok != BR_AUDIO_QUEUE_CUTOFF_DRAINED) ||
                    (worker_result != BR_AUDIO_QUEUE_OK &&
                     worker_result != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
                     worker_result != BR_AUDIO_QUEUE_CUTOFF_DRAINED) ||
                    worker_status != FM1_CPU1_WORKER_OK ||
                    (br_audio_source_queue.fixed_plan_active &&
                     (cpu0_ok == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                      worker_result == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                      (cpu0_ok == BR_AUDIO_QUEUE_OK &&
                       br_audio_source_queue.fixed_plan_cursor[0] !=
                           br_audio_source_queue.fixed_plan_count[0]) ||
                      (worker_result == BR_AUDIO_QUEUE_OK &&
                       br_audio_source_queue.fixed_plan_cursor[1] !=
                           br_audio_source_queue.fixed_plan_count[1])))) {
                    cpu0_ok = BR_AUDIO_QUEUE_FAILED;
                } else if ((cpu0_ok != BR_AUDIO_QUEUE_OK ||
                            worker_result != BR_AUDIO_QUEUE_OK) &&
                           (int32_t)(now - late_deadline) < 0) {
                    /* Provisional completion is produced only on a soft
                     * cutoff; a contradictory clock/status is not success. */
                    cpu0_ok = BR_AUDIO_QUEUE_FAILED;
                } else if (br_audio_source_joined_graph_valid(
                               bus, n, source_job_count, late_deadline,
                               &fm1_cpu1_worker_mailbox, sequence, worker_status,
                               worker_result, faults, &completed)) {
                    if (completed != source_job_count) {
                        if (cpu0_ok != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
                            worker_result != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
                            (cpu0_ok == BR_AUDIO_QUEUE_CUTOFF_DRAINED ||
                             worker_result == BR_AUDIO_QUEUE_CUTOFF_DRAINED) &&
                            (int32_t)(now - late_deadline) >= 0) {
                            br_audio_source_drop_partial(source_job_count,
                                late_deadline, sequence, br_audio_source_now());
                            return;
                        }
                        cpu0_ok = BR_AUDIO_QUEUE_FAILED;
                    } else {
                        complete_watch = 1u;
                        late_sequence = sequence;
                        cpu0_ok = BR_AUDIO_QUEUE_OK;
                        worker_result = BR_AUDIO_QUEUE_OK;
                    }
                } else cpu0_ok = BR_AUDIO_QUEUE_FAILED;
            } else if (BR_AUDIO_C1_LATE_OWNER_KNOWN && BR_AUDIO_FUSION_ONLY_BUILD &&
                BR_AUDIO_FUSION_FOUR_WAY_KIT && n == BR_BLOCK &&
                BR_BLOCK == FM1_AUDIO_HALF_FRAMES &&
#if BR_AUDIO_FUSION_FIXED_PLAN
                fixed_complete &&
#endif
                c1_extended != BR_AUDIO_C1_PREFLIGHT_CLAIMED &&
                worker_status == FM1_CPU1_WORKER_OK &&
                (worker_result == BR_AUDIO_QUEUE_OK ||
                 worker_result == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                 worker_result == BR_AUDIO_QUEUE_CUTOFF_DRAINED) &&
                (cpu0_ok == BR_AUDIO_QUEUE_OK ||
                 cpu0_ok == BR_AUDIO_QUEUE_PUBLISHED_COMPLETE ||
                 cpu0_ok == BR_AUDIO_QUEUE_CUTOFF_DRAINED) &&
                !(cpu0_ok == BR_AUDIO_QUEUE_CUTOFF_DRAINED &&
                  worker_result == BR_AUDIO_QUEUE_CUTOFF_DRAINED) &&
                (!br_audio_source_queue.fixed_plan_active ||
                 (worker_result == BR_AUDIO_QUEUE_OK &&
                  cpu0_ok == BR_AUDIO_QUEUE_OK)) &&
                br_audio_source_fx_fusion_active && !br_pipeline_enabled &&
                br_audio_graph_count_ok(source_job_count)) {
                if (br_audio_source_complete_watch_valid(
                        bus, n, source_job_count, late_deadline,
                        &fm1_cpu1_worker_mailbox, sequence, worker_status,
                        worker_result, fm1_cpu1_worker_fault |
                            br_engine_pipeline_uncontained_fault_get() |
                            br_audio_source_worker_fault)) {
                    complete_watch = 1u;
                    late_sequence = sequence;
                    late_executor = br_audio_source_jobs[7].executor_core;
                    /* Provisional completion is never ordinary success until
                     * this matching join and full graph/fusion validation. */
                    cpu0_ok = BR_AUDIO_QUEUE_OK;
                    worker_result = BR_AUDIO_QUEUE_OK;
                } else cpu0_ok = BR_AUDIO_QUEUE_FAILED;
            }
            if (!br_audio_source_publication_enabled &&
                c1_extended == BR_AUDIO_C1_PREFLIGHT_CLAIMED) {
                FM1_CPU1_WORKER_BARRIER();
                if (cpu0_ok != BR_AUDIO_QUEUE_PUBLISHED_COMPLETE &&
                    br_audio_source_late_join_valid(
                        &fm1_cpu1_worker_mailbox, sequence, worker_status,
                        worker_result, BR_AUDIO_QUEUE_COMPLETE_LATE,
                        fm1_cpu1_worker_fault |
                            br_engine_pipeline_uncontained_fault_get() |
                            br_audio_source_worker_fault) &&
                    br_audio_source_c1_late_read(sequence, late_deadline,
                                                 br_audio_source_now(),
                                                 &late_job) &&
                    br_audio_source_jobs[7].executor_core == 1u &&
                    br_audio_source_accept_complete_late(
                        bus, n, source_job_count, late_deadline, &late_job)) {
                    cpu0_ok = BR_AUDIO_QUEUE_OK;
                    worker_result = BR_AUDIO_QUEUE_OK; /* validated completion */
                    late_sequence = sequence;
                    late_executor = 1u;
                    late_accepted = 1u;
                } else cpu0_ok = BR_AUDIO_QUEUE_FAILED;
            }
            if (cpu0_ok == BR_AUDIO_QUEUE_COMPLETE_LATE) {
                FM1_CPU1_WORKER_BARRIER();
                if ((worker_result == BR_AUDIO_QUEUE_OK ||
                     worker_result == BR_AUDIO_QUEUE_CUTOFF_DRAINED) &&
                    br_audio_source_late_join_valid(
                        &fm1_cpu1_worker_mailbox, sequence, worker_status,
                        worker_result, worker_result, fm1_cpu1_worker_fault |
                            br_engine_pipeline_uncontained_fault_get() |
                            br_audio_source_worker_fault) &&
                    (!BR_AUDIO_C1_LATE_OWNER_KNOWN ||
                     br_audio_source_jobs[7].executor_core == 0u) &&
                    br_audio_source_accept_complete_late(
                        bus, n, source_job_count, late_deadline, &late_job)) {
                    cpu0_ok = BR_AUDIO_QUEUE_OK;
                    worker_result = BR_AUDIO_QUEUE_OK;
                    late_sequence = sequence;
                    late_accepted = 1u;
                } else cpu0_ok = BR_AUDIO_QUEUE_FAILED;
            }
            if (
#if BR_AUDIO_FUSION_FIXED_PLAN
                !fixed_complete ||
#endif
                cpu0_ok != BR_AUDIO_QUEUE_OK || worker_status != FM1_CPU1_WORKER_OK ||
                worker_result != 1u ||
                ((!br_audio_source_legacy_mode || br_audio_source_fx_fusion_active) &&
                 br_audio_source_queue.completed != br_audio_source_queue.count)) {
                uint32_t abort_status;
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
                uint32_t reset_request_tick;
#endif
                uint32_t reset_confirmed;
                br_audio_source_worker_fault = 1u;
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
                reset_request_tick = br_audio_job_timing_now();
#endif
                abort_status = fm1_cpu1_worker_abort_hold_reset();
                reset_confirmed = br_pipeline_note_worker_reset(
                    abort_status == FM1_CPU1_WORKER_OK);
                if (reset_confirmed) {
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG
                    br_audio_failure_job_profile_capture_after_reset(
                        reset_confirmed, reset_request_tick,
                        failure_queue_origin_tick, n,
                        failure_queue_origin_estimated,
                        failure_queue_source_start_valid ?
                            failure_queue_source_start_tick :
                            failure_queue_origin_tick,
                        !failure_queue_source_start_valid);
#endif
                    br_audio_source_fusion_cancel();
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
                    if (runtime_trace_chunk)
                        br_audio_runtime_trace_chunk_active = 0u;
#endif
                    for (p = 0; p < BR_PARTS; ++p) {
                        for (i = 0; i < n; ++i) bus[p][i] = 0;
                        br_stop_part(p);
                    }
                    for (p = 0; p < BR_PARTS; ++p)
                        for (i = 0; i < BR_BLOCK; ++i) br_comp_key[p][i] = 0;
                } /* Otherwise CPU1 may still own its jobs, frame, and stack. */
                return;
            }
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
            if (runtime_trace_chunk)
                br_audio_runtime_trace_chunk_active = 0u;
#endif
        }
        if (br_audio_source_fx_fusion_active && br_audio_source_worker_fault) {
            if (!br_engine_pipeline_uncontained_fault_get())
                br_audio_source_fusion_cancel();
            return;
        }
    }
#else
    /* No resident worker: dependencies preserve the exact source-state order. */
#if BR_AUDIO_FUSION_FIXED_PLAN
    br_audio_fixed_plan_disable();
#endif
    if (!br_audio_source_queue_run(1u)) {
        for (p = 0; p < BR_PARTS; ++p)
            for (i = 0; i < n; ++i) bus[p][i] = 0;
        return;
    }
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    br_gap_stamp[0] = fm1_ticks();
#endif
#if BR_AUDIO_DEFER_POST_JOIN
    br_post_join.history = br_audio_source_fx_fusion_active &&
        br_audio_source_queue.completed == br_audio_source_queue.count;
    br_post_join.history_count = br_audio_source_queue.completed;
#elif BR_AUDIO_FUSION_EWMA_SCHEDULER
    if (br_audio_source_fx_fusion_active &&
        br_audio_source_queue.completed == br_audio_source_queue.count)
        br_audio_fusion_history_commit(br_audio_source_queue.completed);
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    br_gap_stamp[1] = fm1_ticks();
#endif
#if BR_AUDIO_FUSION_JOB_TIMING_DIAG
    if (br_audio_source_fx_fusion_active &&
        br_audio_source_queue.completed == br_audio_source_queue.count)
        br_audio_job_timing_seal(br_audio_source_queue.completed);
#endif
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
    if (runtime_trace_chunk)
        fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_MERGE_BEGIN);
#endif
    if (!br_audio_source_fx_fusion_active) for (p = 0; p < BR_PARTS; ++p) {
        if (br_mode[p] == 1u || br_mode[p] == 4u) {
            for (i = 0; i < n; ++i) {
                uint32_t lane;
                int32_t sample = 0, key_sample = 0;
                for (lane = 0u; lane < BR_AUDIO_KIT_JOB_LANES; ++lane) {
                    int32_t *key = br_audio_kit_key_output(
                        p, lane, br_audio_kit_key_lane_mask(lane));
                    sample += br_audio_kit_partial[0][lane][i];
                    if (key) key_sample += key[i];
                }
                br_comp_key[p][i] = key_sample;
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
                if (br_mode[p] == 4u) {
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                }
#endif
                bus[p][i] = sample;
            }
        }
    }
#if BR_AUDIO_RUNTIME_TRACE_ENABLED
    if (runtime_trace_chunk)
        fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_MERGE_END);
#endif
#else
    for (p = 0; p < BR_PARTS; ++p) {
        if (br_mode[p] == 1u || br_mode[p] == 4u) {
            if (br_mode[p] != 4u) {
                memset(br_audio_kit_partial[0], 0, sizeof br_audio_kit_partial[0]);
                if (!br_audio_kit_key_elision_active())
                    memset(br_audio_kit_key_partial[0], 0,
                           sizeof br_audio_kit_key_partial[0]);
            }
#if BR_AUDIO_FUSION_FOUR_WAY_KIT
            for (uint32_t lane = 0u; lane < BR_DRUM_LANES; ++lane)
                br_engine_source_part(p, br_audio_kit_partial[0][lane],
                    br_audio_kit_key_output(p, lane, 1u << lane), n,
                    1u << lane);
#else
            br_engine_source_part(p, br_audio_kit_partial[0][0],
                br_audio_kit_key_output(p, 0u, BR_KIT_LANES_A), n,
                BR_KIT_LANES_A);
            br_engine_source_part(p, br_audio_kit_partial[0][1],
                br_audio_kit_key_output(p, 1u, BR_KIT_LANES_B), n,
                BR_KIT_LANES_B);
#endif
            for (i = 0; i < n; ++i) {
                uint32_t lane;
                int32_t sample = 0, key_sample = 0;
                for (lane = 0u; lane < BR_AUDIO_KIT_JOB_LANES; ++lane) {
                    int32_t *key = br_audio_kit_key_output(
                        p, lane, br_audio_kit_key_lane_mask(lane));
                    sample += br_audio_kit_partial[0][lane][i];
                    if (key) key_sample += key[i];
                }
                br_comp_key[p][i] = key_sample;
#if defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS
                if (br_mode[p] == 4u) {
                    if (sample > 32767) sample = 32767;
                    if (sample < -32768) sample = -32768;
                }
#endif
                bus[p][i] = sample;
            }
        } else {
            br_engine_source_part(p, bus[p], 0, n, BR_DRUM_ALL_MASK);
        }
    }
#endif
#if defined(FM1_DEVICE)
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    br_diagnostic_synth_ticks += fm1_ticks() - source_wall_start;
#endif
    br_audio_publish_source_cost_diagnostics();
#endif
#if BR_AUDIO_PLAITS_APPEND_COST_PREDICTOR && \
    defined(BR_ENABLE_PLAITS) && BR_ENABLE_PLAITS && defined(BR_SPLIT_AUDIO)
    /* Reuse the completed frame's counts with its matching sealed timing on
     * the next callback. This point is reached only after every job joined. */
    for (p = 0; p < BR_PARTS; ++p) {
        if (br_mode[p] == 4u) {
            for (i = 0u; i < BR_AUDIO_KIT_JOB_LANES; ++i) {
                if (br_audio_plaits_current_cost_valid[p][i]) {
                    br_audio_plaits_prev_append_profile[p][i] =
                        br_audio_plaits_current_append_profile[p][i];
                    br_audio_plaits_prev_cost_signature[p][i] =
                        br_audio_plaits_current_cost_signature[p][i];
                    br_audio_plaits_prev_cost_valid[p][i] = 1u;
                } else {
                    br_audio_plaits_prev_append_profile[p][i] = 0u;
                    br_audio_plaits_prev_cost_signature[p][i] = 0u;
                    br_audio_plaits_prev_cost_valid[p][i] = 0u;
                }
            }
        } else {
            for (i = 0u; i < BR_AUDIO_KIT_JOB_LANES; ++i) {
                br_audio_plaits_prev_append_profile[p][i] = 0u;
                br_audio_plaits_prev_cost_signature[p][i] = 0u;
                br_audio_plaits_prev_cost_valid[p][i] = 0u;
            }
        }
    }
#endif
    /* The worker jobs have joined and the partial lane buffers are stable. */
    br_audio_kit_meter_capture(n, br_mode[BR_DRUM_PART]);
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    br_gap_stamp[2] = fm1_ticks();
#endif
#if BR_AUDIO_RUNTIME_TRACE_ENABLED && defined(BR_SPLIT_AUDIO)
    if (runtime_trace_chunk)
        fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_POST_BEGIN);
#endif
    if (br_audio_source_fx_fusion_active) {
        for (p = 0; p < BR_PARTS; ++p)
            osc_block[p] = br_audio_fused_osc_block[p];
    } else {
        for (p = 0; p < BR_PARTS; ++p)
            osc_block[p] = br_engine_post_part(p, bus, n);
    }
#if BR_AUDIO_RUNTIME_TRACE_ENABLED && defined(BR_SPLIT_AUDIO)
    if (runtime_trace_chunk)
        fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_POST_END);
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    br_gap_stamp[3] = fm1_ticks();
#endif
#if BR_AUDIO_DEFER_POST_JOIN
    br_post_join.bus = bus;
    br_post_join.n = n;
    for (p = 0; p < BR_PARTS; ++p) br_post_join.osc[p] = osc_block[p];
    br_post_join.pending = 1u;
#else
    if (n) {
        int32_t sum = 0, mix;
        for (i = 0; i < n; ++i)
            for (p = 0; p < BR_PARTS; ++p) sum += bus[p][i];
        mix = sum / (int32_t)n;
        for (p = 0; p < BR_PARTS; ++p) {
            br_mod_capture(&br_mod_state[p], osc_block[p], mix);
#if BR_MOD_LFO_PHASE_DELTA_CACHE
            br_mod_advance(&br_mod_state[p], &br_cached_mod[p], n, p);
#else
            br_mod_advance(&br_mod_state[p], &br_cached_mod[p], n);
#endif
        }
    }
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    br_gap_stamp[4] = fm1_ticks();
#endif
#ifdef BR_SPLIT_AUDIO
    if (!late_accepted && complete_watch) {
#if defined(BR_C1_LATE_COMPLETION_TEST)
        { extern void br_audio_source_test_tail_boundary(void);
          br_audio_source_test_tail_boundary(); }
#endif
        uint32_t after_tail = br_audio_source_now();
        if ((int32_t)(after_tail - late_deadline) >= 0) {
            late_job.index = br_audio_source_publication_enabled ? UINT32_MAX : 7u;
            late_job.returned_tick = UINT32_MAX; /* no callback return timestamp */
            late_job.cutoff_tick = after_tail;
            if (br_audio_source_publication_enabled) late_executor = UINT32_MAX;
            late_accepted = 1u;
        }
    }
    if (late_accepted) {
#if BR_AUDIO_DEFER_POST_JOIN
        br_engine_post_join_run();      /* no FX stage follows a drop */
#endif
        /* Source state and modulation advanced exactly once. Master FX and
         * the DMA output are intentionally left for the drop caller. */
        br_audio_source_fusion_cancel();
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
        br_audio_late_last_index = late_job.index;
        br_audio_late_last_count = source_job_count;
        br_audio_late_last_deadline = late_deadline;
        br_audio_late_last_return = late_job.returned_tick;
        br_audio_late_last_sequence = late_sequence;
        br_audio_late_last_core = late_executor;
        br_audio_late_last_cutoff = late_job.cutoff_tick;
        ++br_audio_late_drop_count;
#endif
        br_audio_late_render = 1u;
    }
#endif
}

#if defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT
/* Called on the audio owner, once per known DMA edge. The caller supplies
 * writable lead, including any pipeline block, and must invalidate on skips. */
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
BR_AUDIO_API void br_engine_clock_diag_snapshot(uint32_t out[23])
{
    uint32_t i;
    out[0] = 2u; out[1] = 1u;
    for (i = 0u; i < 5u; ++i) out[2u + i] = br_clock_fail_count[i];
    out[7] = br_clock_last_failure;
    out[8] = br_midi_clock_active;
    out[9] = br_clock_experiment.enabled;
    out[10] = br_midi_clock_bpm;
    out[11] = br_playing;
    out[12] = br_midi_clock_enabled;
    out[13] = br_clock_f8_processed; out[14] = br_clock_f8_rejected;
    out[15] = br_clock_f8_suspect; out[16] = br_clock_interval_count;
    out[17] = br_clock_interval_sum; out[18] = br_clock_interval_last;
    out[19] = br_clock_interval_min; out[20] = br_clock_interval_max;
    out[21] = (uint32_t)br_clock_experiment.follower.period_q16;
    out[22] = (uint32_t)(br_clock_experiment.follower.period_q16 >> 32u);
}
#endif
BR_AUDIO_API void br_engine_clock_dma_invalid(void)
{
    br_clock_experiment.anchor.valid = 0u;
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
    br_clock_prev_f8_valid = 0u;
#endif
    br_clock_fail(BR_CLOCK_FAIL_ANCHOR);
}
BR_AUDIO_API void br_engine_clock_dma_edge(uint32_t tick, uint32_t half, uint32_t lead_frames)
{
    if (!br_clock_experiment.anchor.valid) {
#if !defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS
        br_clock_prev_f8_valid = 0u;
#endif
        uint32_t seek_unsupported = br_clock_experiment.seek_unsupported;
        memset(&br_clock_experiment, 0, sizeof br_clock_experiment);
        br_clock_experiment.seek_unsupported = seek_unsupported;
        fm1_mc_init(&br_clock_experiment.follower, FS);
        br_clock_experiment.follower.period_q16 =
            fm1_clock_div64((uint64_t)br_tempo_quarter_frames() << 16, 24u, 0);
        br_clock_experiment.lead_frames = lead_frames;
        /* Keep room for receipt timestamps just before the first observed
         * edge. The mapper accepts at most two seconds of age; four seconds
         * of epoch pre-roll prevents valid events mapping below frame zero. */
        br_clock_experiment.render_frame = (uint64_t)FS * 4u + lead_frames;
        if (!fm1_mct_anchor_init(&br_clock_experiment.anchor, 24000000u, FS,
                FM1_AUDIO_HALF_FRAMES, 1000u, tick, (uint64_t)FS * 4u, (uint8_t)half))
            br_clock_fail(BR_CLOCK_FAIL_ANCHOR);
    } else if (lead_frames != br_clock_experiment.lead_frames ||
               !fm1_mct_edge(&br_clock_experiment.anchor, tick, (uint8_t)half)) {
        br_engine_clock_dma_invalid();
    }
}
/* Invoke in the source-setting transaction, with the same Timer4 epoch as
 * MIDI receipt. This invalidates older transport messages, not note events. */
BR_AUDIO_API void br_engine_clock_source_changed(uint32_t tick)
{
    (void)tick;
    ++br_midi_clock_generation;
    RING_PUBLISH();
}
/* Foreground producer; timestamp and status use the same ordered ring as
 * note/transport events. Calls while clock is off retain internal transport. */
BR_AUDIO_API void br_engine_clock_midi_event(uint8_t status, uint32_t tick, uint32_t generation)
{
    if (generation != br_midi_clock_generation) return;
    if (!br_midi_clock_enabled) {
        if (status == 0xFAu || status == 0xFBu || status == 0xFCu) br_enqueue(status);
        return;
    }
    if (status == 0xF2u || status == 0xF8u || status == 0xFAu || status == 0xFBu || status == 0xFCu)
        br_enqueue_timed(0x100u | status, tick);
}
#endif

BR_AUDIO_API void br_engine_midi_transport(uint8_t status)
{
    if (status == 0xFAu || status == 0xFBu || status == 0xFCu)
        br_enqueue(status); /* operation zero: ordered transport command */
}

BR_AUDIO_API void br_engine_note_on(uint32_t part, uint32_t note, uint32_t velocity)
{
    if (part < BR_PARTS && note < 128u && velocity)
        br_enqueue(br_pack_event(BR_EVENT_ON, part, note, velocity));
}

BR_AUDIO_API void br_engine_note_off(uint32_t part, uint32_t note)
{
    if (part < BR_PARTS && note < 128u)
        br_enqueue(br_pack_event(BR_EVENT_OFF, part, note, 0));
}

BR_AUDIO_API void br_engine_part_off(uint32_t part)
{
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    if (part < BR_PARTS) br_audio_fixed8_foreground_invalidate();
#endif
    if (part < BR_PARTS) br_enqueue(br_pack_event(BR_EVENT_STOP, part, 0, 0));
}

BR_AUDIO_API void br_engine_all_off(void)
{
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    br_audio_fixed8_foreground_invalidate();
#endif
    br_enqueue(br_pack_event(BR_EVENT_STOP, 3u, 127u, 127u));
}

BR_AUDIO_API int br_engine_quiet(void)
{
    uint32_t p;
    if (br_event_read != br_event_write || br_panic_parts) return 0;
    for (p = 0; p < BR_PARTS; ++p) if (!br_quiet_part[p]) return 0;
    return 1;
}

/* Sample the same signal and signed amount used by the modulation matrix.
 * This is a display-only read; it never advances the audio state. */
BR_AUDIO_API int32_t br_audio_patch_value(uint32_t part, uint32_t slot)
{
    br_mod_state_t state;
    br_route_config_t route;
    int32_t lfo[BR_LFOS];
    uint32_t i;
    if (part >= BR_PARTS || slot >= BR_ROUTES) return 0;
    route = br_cached_mod[part].route[slot];
    if (!route.enabled || !route.amount || !br_mod_source_active(route.source) ||
        !br_mod_target_supported(br_cached_sound[part].preset, route.target)) return 0;
    state = br_mod_state[part];
    for (i = 0; i < BR_LFOS; ++i) {
        uint32_t wave = br_cached_mod[part].lfo[i].wave;
        lfo[i] = br_mod_lfo(&state, i, wave < BR_WAVE_COUNT ? wave : BR_WAVE_SINE);
    }
    return br_mod_source(&state, route.source, (int32_t)br_env[part].amp, lfo) *
        br_mod_clamp_i7(route.amount) / 32767;
}

BR_AUDIO_API uint32_t br_audio_lfo_phase(uint32_t part, uint32_t lfo)
{
    const volatile br_mod_state_t *state;
    if (part >= BR_PARTS || lfo >= BR_LFOS) return 0u;
    state = &br_mod_state[part];
    return state->phase[lfo] >> 25;
}
