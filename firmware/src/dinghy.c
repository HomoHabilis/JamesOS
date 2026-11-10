/* SPDX-License-Identifier: GPL-3.0-only */
/* BRAIDS instrument on Dinghy's platform. The device plumbing stays in platform.c. */
#define APP_NAME "JAMESOS"
#include "platform.c"
#include "instrument.h"
#include "instrument_drums.h"
#ifdef BR_PATTERN_SONG_EXPERIMENT
#include "pattern_song.h"
extern fm1_pattern_song br_ps_state;
extern int br_ps_select(uint32_t), br_ps_copy_pattern(uint32_t), br_ps_paste_pattern(uint32_t);
#if defined(BR_PATTERN_OWNER_HOST) && BR_PATTERN_OWNER_HOST
extern int br_ps_host_can_reset(void), br_ps_host_reset(void);
static uint32_t br_ps_external_restore_completed;
#endif
#endif
#if defined(BR_SOURCE_QUEUE_TEST)
extern volatile uint32_t br_audio_source_worker_fault;
#endif
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif

/* Foreground edits and project restores are transactions with respect to the
 * audio IRQ. A reset-ownership fault may be latched by that IRQ, so check the
 * sticky flag while interrupts are masked before touching shared DSP state. */
static volatile uint32_t br_diagnostic_restore_in_progress;
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
/* Build-only target experiment. Clipboard is compressed into this bounded
 * arena; its staging snapshot is also the sole audio publication slot. This
 * does not enable or implement multi-pattern flash persistence. */
#include "pattern_owner.h"
static br_pattern_snapshot br_ps_target_live, br_ps_target_stage;
uint8_t br_ps_target_arena[7756] __attribute__((aligned(4)));
static uint8_t br_ps_target_encoded[4335] __attribute__((aligned(4)));
/* Reserved separately so a future load can validate an entire compressed
 * replacement without modifying the current bank. Until the target project
 * adapter exists, no code reads from or writes projects through this buffer. */
static uint8_t br_ps_target_load_arena[3584] __attribute__((section(".pool"), aligned(4)));
static br_pattern_irq_publication br_ps_target_pub __attribute__((section(".pool")));
static int br_ps_target_quiet(void *context)
{ (void)context; return !br_playing && br_diagnostic_restore_in_progress; }
#endif

#if defined(BR_PATTERN_TEST_IRQ_TRACK)
static uint32_t br_test_irq_depth;
#endif
static uint32_t br_foreground_irq_save(void)
{
#if defined(BR_PATTERN_TEST_IRQ_TRACK)
    return br_test_irq_depth++;
#elif defined(FM1_DEVICE)
    return fm1_cpu1_worker_irq_save();
#else
    return 0u;
#endif
}

static void br_foreground_irq_restore(uint32_t state)
{
#if defined(BR_PATTERN_TEST_IRQ_TRACK)
    br_test_irq_depth = state;
#elif defined(FM1_DEVICE)
    fm1_cpu1_worker_irq_restore(state);
#else
    (void)state;
#endif
}

/* Long foreground transactions cannot keep audio interrupts masked. Claim
 * the transaction while masked, then let the ISR emit silence until it ends. */
static uint32_t br_quiescent_transaction_begin(void)
{
    uint32_t irq_state = br_foreground_irq_save();
    if (br_engine_pipeline_uncontained_fault_get() || in_audio ||
        br_diagnostic_restore_in_progress) {
        br_foreground_irq_restore(irq_state);
        return 0u;
    }
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    br_audio_fixed8_foreground_invalidate();
#endif
    br_diagnostic_restore_in_progress = 1u;
    RING_PUBLISH();
    br_foreground_irq_restore(irq_state);
    return 1u;
}

static void br_quiescent_transaction_end(void)
{
    uint32_t irq_state = br_foreground_irq_save();
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
    br_audio_fixed8_foreground_invalidate();
#endif
    RING_PUBLISH();
    br_diagnostic_restore_in_progress = 0u;
    br_foreground_irq_restore(irq_state);
}

static uint32_t br_foreground_mutation_begin(uint32_t *irq_state)
{
    if (!irq_state) return 0u;
#if defined(BR_DIAGNOSTIC_TEST) && defined(BR_SPLIT_AUDIO)
    extern uint32_t br_app_test_inject_fault_before_mutation;
    extern void br_engine_pipeline_test_uncontained_fault_latch(void);
    if (br_app_test_inject_fault_before_mutation) {
        br_app_test_inject_fault_before_mutation = 0u;
        br_engine_pipeline_test_uncontained_fault_latch();
    }
#endif
    *irq_state = br_foreground_irq_save();
    if (br_engine_pipeline_uncontained_fault_get() ||
        br_diagnostic_restore_in_progress) {
        br_foreground_irq_restore(*irq_state);
        return 0u;
    }
    return 1u;
}

static void br_foreground_mutation_end(uint32_t irq_state)
{
    RING_PUBLISH();
    br_foreground_irq_restore(irq_state);
}

#define BR_FOREGROUND_MUTATE(statement) do { \
    uint32_t br_mutation_irq_state; \
    if (!br_foreground_mutation_begin(&br_mutation_irq_state)) return; \
    statement; \
    br_foreground_mutation_end(br_mutation_irq_state); \
    if (br_engine_pipeline_uncontained_fault_get()) return; \
} while (0)

enum { BR_PAGE_HOME, BR_PAGE_ENV, BR_PAGE_FX, BR_PAGE_MIX, BR_PAGE_SEQ, BR_PAGE_PATTERN,
       BR_PAGE_SCALE, BR_PAGE_LFO, BR_PAGE_ROUTE, BR_PAGE_PUNCH,
#ifdef BR_PATTERN_SONG_EXPERIMENT
       BR_PAGE_PS,
#endif
       BR_PAGE_COUNT };
#include "instrument_punch.h"
#include "instrument_bank.h"
static uint32_t br_recording, br_hold_on;
/* Punch-in FX: the key state, saved settings, the effect now running, and the
 * page to return to when the shift key is released. */
static br_punch_keys_t br_punch_ks;
static br_punch_cfg_t br_punch_cfg = BR_PUNCH_CFG_DEFAULTS;
static br_punch_dsp_t br_punch_dsp;
static uint8_t br_punch_echo_part;
static uint32_t br_punch_return_page, br_punch_return_screen;
static struct {
    uint32_t page, part, cursor, lane, pattern, redraw, last_draw, message_until, message_start;
    uint32_t screen[BR_PAGE_COUNT];
    uint32_t step_bank, gate_bank, seq_pressed, seq_turned, seq_was_active;
    uint32_t step_press_key, step_press_at, step_press_part, step_press_step;
    uint32_t step_press_lane, step_press_kit, step_press_active, step_edit_active;
    uint32_t retrig_key, retrig_step, retrig_lane, retrig_part; /* key+1, zero when released */
    uint32_t note[BR_PARTS];
    struct { uint16_t length; uint8_t note, velocity, condition; } seq_default[BR_PARTS][BR_DRUM_LANES];
    uint32_t previous_keys, held_note[27], held_part[27], edit_time, edit_key;
    uint32_t glo_pending, glo_used, glo_snap, glo_start;   /* glo_snap: this GLO press began on the global filter page */
    /* Keys routed to the arpeggiator, and notes sustained by HOLD. */
    uint8_t held_arp[27];
    uint8_t fx_snap_pending, fx_snap_used, fx_snap_screen, fx_snap_part;
    uint8_t midi_oct_pending; /* OCT- awaits release, note or navigation to allow OCT- + GLO */
    uint32_t sustain_note[27], sustain_part[27];
    uint16_t sustain_gen[27];
    /* HOME and ARP act on release; held 0.8 s they toggle HOLD and ARP. */
    uint32_t mode_pending, mode_fired, mode_start[2];
#ifdef BR_PATTERN_SONG_EXPERIMENT
    uint32_t ps_held_key, ps_held_used;
#endif
    int8_t octave;
    char message[25];
} br_ui;
#if defined(FM1_DEVICE)
/* Called only by the elected fatal owner after both core snapshots are ready.
 * Keep these as one-time reads of the live context, not inferred saved GPRs. */
void fm1_fault_diag_context_snapshot(uint32_t out[2])
{
    uint32_t audio = (uint32_t)in_audio;
    uint32_t part = br_ui.part;
    out[0] = audio;
    out[1] = part;
}
#endif
static void br_text_copy(char *out, const char *text, uint32_t cap)
{
    uint32_t i = 0;
    if (!cap) return;
    while (text[i] && i + 1u < cap) { out[i] = text[i]; i++; }
    out[i] = 0;
}
static void br_text_append(char *out, const char *text, uint32_t cap)
{
    uint32_t n = 0;
    while (n < cap && out[n]) n++;
    if (n < cap) br_text_copy(out + n, text, cap - n);
}
static void br_number(char *out, int32_t value)
{
    char rev[12]; uint32_t n = 0, i = 0, v = value < 0 ? (uint32_t)(-value) : (uint32_t)value;
    do { rev[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    if (value < 0) out[i++] = '-';
    while (n) out[i++] = rev[--n];
    out[i] = 0;
}
static void br_message(const char *text)
{
    uint32_t i;
    for (i = 0; text[i] && i + 1u < sizeof br_ui.message; i++) br_ui.message[i] = text[i];
    br_ui.message[i] = 0;
    br_ui.message_start = fm1_ms;
    br_ui.message_until = fm1_ms + 640u + 60000u / (br_bpm ? br_bpm : 120u);
    br_ui.redraw = 1;
}
#if defined(BR_PATTERN_SONG_EXPERIMENT) && defined(BR_PATTERN_OWNER_HOST)
#if defined(FM1_DEVICE) && (!defined(FM1_PATTERN_TARGET_EXPERIMENT) || !FM1_PATTERN_TARGET_EXPERIMENT)
#error "Host pattern owner must not be enabled on device"
#endif
enum { BR_PS_SELECT=1, BR_PS_COPY, BR_PS_PASTE };
static struct { uint32_t op, a, b, c; } br_ps_request;
static int br_ps_enqueue(uint32_t op, uint32_t a, uint32_t b, uint32_t c)
{
    if (br_ps_request.op) return 0;
    br_ps_request.a=a; br_ps_request.b=b; br_ps_request.c=c; br_ps_request.op=op;
    return 1;
}
/* Called outside foreground mutation: large copies and compression must not
 * run with interrupts masked. This host owner has no running transport path. */
static void br_ps_service(void)
{
    uint32_t op=br_ps_request.op, a=br_ps_request.a;
    int ok=0;
    if (!op) return;
    br_ps_request.op=0u;

    if (br_playing) { br_message("STOP FOR PATTERNS"); return; }
    if (!br_quiescent_transaction_begin()) { br_message("PATTERN BUSY"); return; }
    switch (op) {
    case BR_PS_SELECT: ok=br_ps_select(a); break;
    case BR_PS_COPY: ok=br_ps_copy_pattern(a); break;
    case BR_PS_PASTE: ok=br_ps_paste_pattern(a); break;
    }
    br_quiescent_transaction_end();
    if (!ok) br_message("PATTERN FAILED");
    else if (op==BR_PS_COPY) br_message("PATTERN COPIED");
    else if (op==BR_PS_PASTE) br_message("PATTERN PASTED");
    br_ui.redraw=1u;
}
#if defined(FM1_PATTERN_TICK_CLAIM_PROTOTYPE) && FM1_PATTERN_TICK_CLAIM_PROTOTYPE
extern int br_ps_runtime_queue(uint32_t);
static int br_ps_ui_select(uint32_t target)
{
    if (!br_playing) return br_ps_enqueue(BR_PS_SELECT,target,0u,0u);
    if (target==br_ps_state.active && br_ps_state.queued==FM1_PATTERN_NONE) return 1;
    if (!br_ps_runtime_queue(target)) return 0;
    br_message("PATTERN QUEUED"); return 1;
}
/* Key handlers run inside the general foreground IRQ mask. Record only the
 * intent there; snapshot capture and decoding run after the mask is released. */
static uint32_t br_ps_selection_pending, br_ps_selection_playing;
static int br_ps_defer_selection(uint32_t target)
{
    if (br_ps_selection_pending) return 0;
    br_ps_selection_playing = !!br_playing;
    br_ps_selection_pending = target + 1u;
    return 1;
}
static void br_ps_service_selection(void)
{
    uint32_t target = br_ps_selection_pending;
    if (!target) return;
    br_ps_selection_pending = 0u;
    if (!!br_playing != br_ps_selection_playing ||
        !br_ps_ui_select(target - 1u)) br_message("PATTERN FAILED");
}
#define br_ps_select(a) br_ps_defer_selection(a)
#define BR_PS_SERVICE_SELECTION() br_ps_service_selection()
#else
#define BR_PS_SERVICE_SELECTION() ((void)0)
#define br_ps_select(a) br_ps_enqueue(BR_PS_SELECT,(a),0u,0u)
#endif
#define br_ps_copy_pattern(a) br_ps_enqueue(BR_PS_COPY,(a),0u,0u)
#define br_ps_paste_pattern(a) br_ps_enqueue(BR_PS_PASTE,(a),0u,0u)
#endif
/* Each track's signal for the block being rendered (after its level, before the effects): the mixer
 * meters read it. */
static int32_t bus[BR_PARTS][BR_BLOCK];
#ifndef BR_SPLIT_AUDIO
#include "instrument_presets.c"
#include "instrument_audio.c"
#include "instrument_fx.c"
#endif
static void br_midi_before_session_restore(void);
#include "instrument_session.c"
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT && \
    defined(BR_MIDI_CLOCK_EXPERIMENT) && BR_MIDI_CLOCK_EXPERIMENT && \
    defined(FM1_PATTERN_TICK_CLAIM_PROTOTYPE) && FM1_PATTERN_TICK_CLAIM_PROTOTYPE && \
    defined(FM1_PATTERN_AUDIO_IRQ_PROTOTYPE) && FM1_PATTERN_AUDIO_IRQ_PROTOTYPE && defined(BR_SPLIT_AUDIO)
#define BR_UI_CLOCK_SUPPORT 1
static uint32_t br_ps_tick_claimed;
static void br_ui_clock_service(void);
#define BR_UI_CLOCK_SERVICE() br_ui_clock_service()
#else
#define BR_UI_CLOCK_SERVICE() ((void)0)
#endif
#include "instrument_ui.c"
#if defined(BR_UI_CLOCK_SUPPORT)
#include "pattern_ui_clock_service.h"
#endif
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
static uint32_t br_project_diag_down,br_project_diag_up;
static int32_t br_project_diag_knob1,br_project_diag_knob2;
#endif
#if defined(FM1_PATTERN_PANEL_TEST) && FM1_PATTERN_PANEL_TEST
static uint32_t br_pattern_diag_key;
#endif
#ifdef DINGHY_EMULATOR_SAVE
#include "../../tools/ui_save_emulator.c"
#elif defined(DINGHY_DEVICE_SAVE_TEST)
#include "../../tests/project_device_host.c"
#include "instrument_project_device.c"
#include "instrument_project_device_ui.c"
#elif defined(FM1_DEVICE)
#include "instrument_project_device.c"
#include "instrument_project_device_ui.c"
#endif

#if defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
/* Link-only workspace integration. No flash callbacks are installed and no
 * project UI invokes these functions. The owned token distinguishes this
 * transaction from diagnostic use of the same audio-silence flag. */
#include "pattern_target_storage.h"
#include "pattern_session_loan.h"
#include "pattern_loan_token.h"
#include "pattern_park_io.h"
static br_pattern_park_io br_ps_target_park_io;
static unsigned br_ps_target_workspace_lent, br_ps_target_session_lent, br_ps_target_settings_lent;
static size_t br_ps_target_scratch_capacity;
static uint32_t br_ps_target_reserved_sectors;
static fm1_project_work br_ps_target_project_work;
#if defined(FM1_DEVICE)
static fm1_project_adapter br_ps_target_project_adapter __attribute__((section(".pool"), aligned(4)));
#else
static fm1_project_adapter br_ps_target_project_adapter;
#endif
static br_pattern_loan_token br_ps_target_project_token;
/* Project transactions retain exclusive worker and scratch ownership while
 * interrupt-side input accumulates for the foreground to process afterward. */
static uint32_t br_ps_project_watch_token;
static uint32_t br_ps_project_post_release_fault;
static int br_ps_project_watch_begin(uint32_t token);
static int br_ps_project_watch_ok_locked(void);
static int br_ps_project_watch_ok(void);
/* Set only after the submission fence and drained, verified CPU1 park. */
static volatile uint32_t br_ps_target_worker_certified;
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
static uint32_t br_ps_provision_watch_token,br_ps_provision_watch_rx,br_ps_provision_watch_midi;
static uint32_t br_ps_provision_watch_config,br_ps_provision_watch_resets,br_ps_provision_watch_panel;
static int br_ps_target_provision_watch_ok(void);
static int br_ps_target_provision_watch_ok_locked(void);
#endif
static int br_ps_target_fence(void *ctx) { (void)ctx; return fm1_cpu1_worker_project_fence_acquire(); }
static int br_ps_target_unfence(void *ctx) { (void)ctx; return fm1_cpu1_worker_project_fence_release(); }
static int br_ps_target_park(void *ctx)
{ (void)ctx; return fm1_cpu1_worker_park(FM1_CPU1_WORKER_TIMEOUT_TICKS)==FM1_CPU1_WORKER_OK ? 0 : -1; }
static int br_ps_target_parked(void *ctx)
{
    (void)ctx;
    fm1_memory_publish_sync();
    return !br_playing && br_diagnostic_restore_in_progress && br_ps_target_project_token.active &&
        fm1_cpu1_worker_project_fence_held() && !fm1_cpu1_worker_fault &&
        !fm1_cpu1_worker_uncontained_fault_get() && fm1_cpu1_worker_mailbox.cpu_id==1u &&
        fm1_cpu1_worker_mailbox.state==FM1_CPU1_WORKER_PARKED &&
        fm1_cpu1_worker_mailbox.park_request && fm1_cpu1_worker_mailbox.park_ack &&
        fm1_cpu1_worker_mailbox.request_seq==fm1_cpu1_worker_mailbox.done_seq;
}
/* Flash RAM routines cannot dereference XIP while flash is in command mode.
 * Accept only linked data/BSS, pool, or the two explicit CPU0 stack ranges. */
#ifndef BR_PATTERN_RAM_RANGE
static int br_ps_target_ram_range(const void *p,uint32_t n)
{
    extern uint32_t _data_start[], _bss_end[], _pool_start[], _pool_end[];
    extern char _ustack_lo[], _ustack_top[], _sstack_lo[], _sstack_top[];
    const uintptr_t lo[]={ (uintptr_t)_data_start, (uintptr_t)_pool_start, (uintptr_t)_ustack_lo, (uintptr_t)_sstack_lo };
    const uintptr_t hi[]={ (uintptr_t)_bss_end, (uintptr_t)_pool_end, (uintptr_t)_ustack_top, (uintptr_t)_sstack_top };
    uintptr_t v=(uintptr_t)p; unsigned i;
    if(!p || !n) return 0;
    for(i=0;i<4;i++) if(v>=lo[i] && v<hi[i] && n<=hi[i]-v) return 1;
    return 0;
}
#define BR_PATTERN_RAM_RANGE(p,n) br_ps_target_ram_range((p),(n))
#endif
/* These raw operations deliberately never invoke generic st_read or the
 * legacy project callbacks, which each park/resume independently. */
static int br_ps_target_read_raw(void *ctx,uint32_t off,void *dst,uint32_t n)
{
    uint32_t irq; int rc; (void)ctx;
    if(!flash_ok || !FL_IN(off,n,BR_PROJECT_BASE,BR_PROJECT_END) || !n || n>256u || !BR_PATTERN_RAM_RANGE(dst,n)) return -1;
    irq=irq_save();
    if(!br_ps_target_parked(0) || !br_ps_project_watch_ok_locked()) { irq_restore(irq); return -1; }
    rc=FL_FAR(fl_read_ram)(off,(uint8_t *)dst,n); irq_restore(irq); return rc;
}
/* First write to each sector reserves it only after an independent full
 * erased-sector scan. Subsequent writes must stay in that reserved sector.
 * This does not authorize erase or make the legacy/PFS routing safe. */
static int br_ps_target_reserve_sector(uint32_t off,uint32_t n)
{
    uint8_t chunk[64]; uint32_t sector,index,pos,i,bit;
    if(!n || !FL_IN(off,n,BR_PROJECT_BASE,BR_PROJECT_END)) return -1;
    sector=off & ~(PS_SECTOR-1u);
    if(n>PS_SECTOR-(off-sector)) return -1;
    index=(sector-BR_PROJECT_BASE)/PS_SECTOR;
    if(index>=PS_TOTAL_SLOTS*2u) return -1;
    bit=1u<<index; if(br_ps_target_reserved_sectors&bit) return 0;
    for(pos=0;pos<PS_SECTOR;pos+=sizeof chunk) {
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
        if(!br_ps_target_provision_watch_ok())return -1;
#endif
        if(!br_ps_target_parked(0) || br_ps_target_read_raw(0,sector+pos,chunk,sizeof chunk)) return -1;
        for(i=0;i<sizeof chunk;i++) if(chunk[i]!=0xffu) return -1;
    }
    br_ps_target_reserved_sectors|=bit; return 0;
}
static int br_ps_target_program_raw(void *ctx,uint32_t off,const void *src,uint32_t n)
{
    uint32_t irq; int rc; (void)ctx;
#if !defined(DINGHY_HOST) && (!defined(FM1_PATTERN_DEVICE_CANDIDATE) || !FM1_PATTERN_DEVICE_CANDIDATE)
    /* Device writes are enabled only in the explicit candidate build. */
    (void)off; (void)src; (void)n; return -1;
#endif
    if(!flash_ok || !FL_IN(off,n,BR_PROJECT_BASE,BR_PROJECT_END) || !n ||
       n>256u || (off&255u)+n>256u || !BR_PATTERN_RAM_RANGE(src,n) || br_ps_target_reserve_sector(off,n)) return -1;
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
    if(!br_ps_target_provision_watch_ok())return -1;
#endif
    irq=irq_save();
    if(!br_ps_target_parked(0) || !br_ps_project_watch_ok_locked()
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
       || !br_ps_target_provision_watch_ok_locked()
#endif
       ) { irq_restore(irq); return -1; }
    rc=fl_write(off,(const uint8_t *)src,n); irq_restore(irq); return rc;
}
static int br_ps_target_erase_raw(void *ctx,uint32_t off)
{
    uint32_t irq,took; int rc; (void)ctx;
#if !defined(DINGHY_HOST) && (!defined(FM1_PATTERN_DEVICE_CANDIDATE) || !FM1_PATTERN_DEVICE_CANDIDATE)
    /* Device writes are enabled only in the explicit candidate build. */
    (void)off; return -1;
#endif
    if(!flash_ok || !FL_IN(off,PS_SECTOR,BR_PROJECT_BASE,BR_PROJECT_END)) return -1;
    irq=irq_save();
    if(!br_ps_target_parked(0) || !br_ps_project_watch_ok_locked()) { irq_restore(irq); return -1; }
    rc=fl_erase4k(off,&took); irq_restore(irq); return rc;
}
#if defined(FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY) && FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY
static int br_ps_target_fixed_may_erase(uint32_t off);
#endif
static int br_ps_target_may_erase(void *ctx,uint32_t off) {
    (void)ctx;
#if defined(FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY) && FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY
    return br_ps_target_fixed_may_erase(off);
#else
    (void)off; return 0;
#endif
}
static int br_ps_target_revoke(void *ctx)
{
    uint32_t irq; (void)ctx;
    /* If ownership was lost, only a verified held reset permits reuse of the
     * loaned buffers. Do not revoke them on an uncontained CPU1 failure. */
    if((br_ps_target_session_lent || br_ps_target_workspace_lent || br_ps_target_settings_lent) &&
       !br_ps_target_parked(0) && !fm1_cpu1_worker_reset_held()) return -1;
#if defined(FM1_PATTERN_NAMESPACE_TARGET_EXPERIMENT) && FM1_PATTERN_NAMESPACE_TARGET_EXPERIMENT
    /* A partially written entry stays invalid on flash, but its in-memory
     * capability must not survive release of this transaction's scratch. */
    if(br_ps_target_journal_handle()->writing) return -1;
    br_ps_target_journal_close(br_ps_target_journal_handle());
#endif
    /* Activity cannot race the final proof and loan revocation. Keep the
     * watch armed through worker resume and the final token release. */
    irq=irq_save();
    if(!br_ps_project_watch_ok_locked()) { irq_restore(irq); return -1; }
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
    br_ps_provision_watch_token=0;
#endif
    (void)br_pattern_session_bind(0,0);
    if(br_ps_target_settings_lent) { br_ps_settings_scratch_release(); br_ps_target_settings_lent=0; }
    if(br_ps_target_session_lent) { br_pattern_session_loan_release(); br_ps_target_session_lent=0; }
    if(br_ps_target_workspace_lent) { br_ps_host_workspace_release(); br_ps_target_workspace_lent=0; }
    br_ps_target_worker_certified=0; br_ps_target_scratch_capacity=0;
    irq_restore(irq); return 0;
}
static int br_ps_target_resume(void *ctx)
{
    (void)ctx;
    if(fm1_cpu1_worker_project_authorize_resume()) return -1;
    return fm1_cpu1_worker_project_resume(FM1_CPU1_WORKER_TIMEOUT_TICKS)==FM1_CPU1_WORKER_OK ? 0 : -1;
}
static void br_ps_target_hold(void *ctx) { (void)ctx; (void)fm1_cpu1_worker_abort_hold_reset(); }
static int br_ps_target_contained(void *ctx) { (void)ctx; return fm1_cpu1_worker_reset_held()==1u; }
static const br_pattern_park_ops br_ps_target_park_ops = {
    0, br_ps_target_fence, br_ps_target_unfence, br_ps_target_park, br_ps_target_parked,
    br_ps_target_read_raw, br_ps_target_program_raw, br_ps_target_erase_raw,
    br_ps_target_may_erase, br_ps_target_revoke, br_ps_target_resume, br_ps_target_hold, br_ps_target_contained
};
/* These checks apply to every project transaction, not only provisioning.
 * Pending control traffic is never consumed by the guard. */
static int br_ps_project_input_idle(void)
{
    /* Held/queued controls are handled after the synchronous transaction.
     * An already-requested boot/update takes priority before acquiring it. */
    return !recovery_active && !fm1_cpu1_early_reboot_pending &&
        !usb.uboot_req && !usb.ota_req && !br_playing && !br_recording &&
        !br_hold_on && !br_save_pending && !br_pattern_session_scratch_blocked();
}
static int br_ps_project_watch_begin(uint32_t token)
{
    uint32_t irq=irq_save(); int ok=0;
    if(!br_ps_project_watch_token && br_pattern_loan_matches(&br_ps_target_project_token,token) && br_ps_project_input_idle()) {
        br_ps_project_watch_token=token;ok=1;
    }
    irq_restore(irq);return ok;
}
static int br_ps_project_watch_ok_locked(void)
{
    /* Interrupts enqueue controls and update USB state. Foreground consumes
     * them only after the project loan and worker fence have been released. */
    return br_ps_project_watch_token &&
        br_pattern_loan_matches(&br_ps_target_project_token,br_ps_project_watch_token) &&
        !recovery_active && !fm1_cpu1_early_reboot_pending &&
        !br_playing && !br_recording && !br_hold_on && !br_save_pending &&
        !br_pattern_session_scratch_blocked();
}
static int br_ps_project_watch_ok(void)
{ uint32_t irq=irq_save();int ok=br_ps_project_watch_ok_locked();irq_restore(irq);return ok; }
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
/* This watch is opt-in for the separately gated provisioning protocol. It
 * never consumes events. USB packets remain in the existing bounded ring /
 * endpoint backpressure; panel accumulators remain untouched. */
static int br_ps_provision_input_idle(void)
{
    uint32_t i;
    if(fm1_in.pattern_activity==0xffffffffu || recovery_active || fm1_cpu1_early_reboot_pending ||
       usb.uboot_req || usb.ota_req || usb.sx_on ||
#if FELUCCA_OTA
       sx_ready || sx_collect || sx_busy || so_r!=so_w ||
#endif
       br_playing || br_recording || br_hold_on || br_save_pending || br_session_save_blocked() ||
       br_midi_clock_enabled || br_midi_clock_active || !usb.up || !usb.config || usb.detached || usb.suspended || usb.rx_pend || mi_r!=mi_w || fm1_in.notes || fm1_in.buttons ||
       fm1_in.pressed || fm1_in.released || fm1_in.notes_pressed)return 0;
    for(i=0;i<FM1_NENC;i++)if(fm1_in.enc_steps[i] || fm1_in.enc_sub[i])return 0;
    for(i=0;i<27u;i++)if(br_ui.held_note[i])return 0;
    for(i=0;i<BR_MIDI_ROUTE_NOTES;i++)if(br_midi_routes.note[i].channel)return 0;
    return 1;
}
int br_ps_target_provision_watch_begin(uint32_t token)
{
    uint32_t irq=irq_save();int ok=0;
    if(!br_ps_provision_watch_token && br_pattern_loan_matches(&br_ps_target_project_token,token) &&
       br_ps_target_worker_certified && br_ps_target_parked(0) && br_ps_provision_input_idle()) {
        br_ps_provision_watch_rx=*(volatile uint32_t *)&usb.rx_pkts;
        br_ps_provision_watch_midi=mi_w;br_ps_provision_watch_config=usb.config;
        br_ps_provision_watch_resets=usb.resets;br_ps_provision_watch_panel=fm1_in.pattern_activity;
        br_ps_provision_watch_token=token;ok=1;
    }
    irq_restore(irq);return ok;
}
static int br_ps_target_provision_watch_ok_locked(void)
{
    if(!br_ps_provision_watch_token)return 1;
    return br_pattern_loan_matches(&br_ps_target_project_token,br_ps_provision_watch_token) &&
       br_ps_provision_input_idle() && mi_w==br_ps_provision_watch_midi &&
       usb.config==br_ps_provision_watch_config && usb.resets==br_ps_provision_watch_resets &&
       fm1_in.pattern_activity==br_ps_provision_watch_panel &&
       *(volatile uint32_t *)&usb.rx_pkts==br_ps_provision_watch_rx;
}
static int br_ps_target_provision_watch_ok(void)
{
    uint32_t irq=irq_save();int ok=br_ps_target_provision_watch_ok_locked();irq_restore(irq);return ok;
}
int br_ps_target_provision_watch_end(uint32_t token)
{
    uint32_t irq=irq_save();int ok=0;
    if(token && token==br_ps_provision_watch_token) { ok=br_ps_target_provision_watch_ok_locked();br_ps_provision_watch_token=0; }
    irq_restore(irq);return ok;
}
#endif
static int br_ps_target_store_read(void *ctx,uint32_t off,void *out,uint32_t n)
{
    br_ps_target_store_handle *h=ctx;
    if(!out || !n || !br_ps_project_watch_ok())return -1;
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
    if(!br_ps_target_provision_watch_ok())return -1;
#endif
    if(!h || !br_pattern_loan_matches(&br_ps_target_project_token,h->token) || !br_ps_target_worker_certified) return -1;
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
    { uint8_t *p=out; uint32_t k;
      while(n) { if(!br_ps_target_provision_watch_ok())return -1;k=n>256u?256u:n;
          if(br_pattern_park_read(&br_ps_target_park_io,off,p,k))return -1;off+=k;p+=k;n-=k; }
      return 0; }
#else
    return br_pattern_park_read(&br_ps_target_park_io,off,out,n);
#endif
}
static int br_ps_target_store_program(void *ctx,uint32_t off,const void *in,uint32_t n)
{
    br_ps_target_store_handle *h=ctx;
    if(!in || !n || !br_ps_project_watch_ok())return -1;
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
    if(!br_ps_target_provision_watch_ok())return -1;
#endif
    if(!h || !br_pattern_loan_matches(&br_ps_target_project_token,h->token) || !br_ps_target_worker_certified) return -1;
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
    { const uint8_t *p=in; uint32_t k;
      while(n) { if(!br_ps_target_provision_watch_ok())return -1;k=256u-(off&255u);if(k>n)k=n;
          if(br_pattern_park_program(&br_ps_target_park_io,off,p,k))return -1;off+=k;p+=k;n-=k; }
      return 0; }
#else
    return br_pattern_park_program(&br_ps_target_park_io,off,in,n);
#endif
}
int br_ps_target_project_store(uint32_t token,br_ps_target_store_handle *handle)
{
    if(!handle || !br_pattern_loan_matches(&br_ps_target_project_token,token) || !br_ps_target_worker_certified) return 0;
    handle->token=token; handle->store.ctx=handle;
    handle->store.read=br_ps_target_store_read; handle->store.program=br_ps_target_store_program;
    handle->store.base=BR_PROJECT_BASE; return 1;
}
static int br_ps_target_project_exclusive(void *ctx)
{ (void)ctx; return br_ps_target_worker_certified==1u && br_ps_target_project_token.active &&
    br_diagnostic_restore_in_progress && !br_playing && br_ps_target_parked(0) && br_ps_project_watch_ok(); }
#if defined(FM1_PATTERN_NAMESPACE_TARGET_EXPERIMENT) && FM1_PATTERN_NAMESPACE_TARGET_EXPERIMENT
/* Private raw writer is reachable only during an armed cursor-gate call.
 * Generic PFS writes still require a whole erased-sector reservation. */
static br_ps_target_journal br_ps_target_journal_instance;
br_ps_target_journal *br_ps_target_journal_handle(void) { return &br_ps_target_journal_instance; }
static int br_ps_target_journal_exclusive(void *ctx)
{
    br_ps_target_journal *j=ctx;
    if(j!=&br_ps_target_journal_instance || !br_ps_target_workspace_lent ||
       j->sector!=br_ps_target_project_adapter.sector || !br_pattern_loan_matches(&br_ps_target_project_token,j->token) ||
       !br_ps_target_project_exclusive(0) || br_pattern_park_faulted(&br_ps_target_park_io) ||
       !pns_namespace_unchanged(j->ns,&j->pinned,&j->pinned_store))return 0;
    return 1;
}
static int br_ps_target_journal_raw(void *ctx,uint32_t off,const void *src,uint32_t n)
{
    br_ps_target_journal *j=ctx; const uint8_t *p=src; uint32_t k,irq; int rc;
#if !defined(DINGHY_HOST)
    (void)j; (void)p; (void)k; (void)irq; (void)rc; (void)off; (void)n; return -1;
#endif
    if(j!=&br_ps_target_journal_instance || !j->writing)return -1;
    if(!br_ps_target_journal_exclusive(j) || !flash_ok ||
       !FL_IN(off,n,BR_PROJECT_BASE,BR_PROJECT_END) || !n || !BR_PATTERN_RAM_RANGE(src,n)) {
        (void)br_pattern_park_abort(&br_ps_target_park_io);return -1;
    }
    while(n) {
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
        if(!br_ps_target_provision_watch_ok()) { (void)br_pattern_park_abort(&br_ps_target_park_io);return -1; }
#endif
        k=256u-(off&255u);if(k>n)k=n;
        irq=irq_save();
        if(!br_ps_target_journal_exclusive(j) || !br_ps_project_watch_ok_locked()
#if defined(FM1_PATTERN_PROVISION_EXPERIMENT) && FM1_PATTERN_PROVISION_EXPERIMENT
           || !br_ps_target_provision_watch_ok_locked()
#endif
           ) { irq_restore(irq); (void)br_pattern_park_abort(&br_ps_target_park_io); return -1; }
        rc=fl_write(off,p,k);irq_restore(irq);
        if(rc) { (void)br_pattern_park_abort(&br_ps_target_park_io); return -1; }
        off+=k;p+=k;n-=k;
    }
    return 0;
}
static int br_ps_target_journal_overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y ? y-x<an : x-y<bn);
}
int br_ps_target_journal_init(br_ps_target_journal *j,uint32_t token,const pns_namespace *n,void *sector)
{
    br_ps_target_store_handle *h;
    if(j!=&br_ps_target_journal_instance || j->writing || j->gate.phase || !n || !n->store || !sector ||
       !br_ps_target_workspace_lent || sector!=br_ps_target_project_adapter.sector || !BR_PATTERN_RAM_RANGE(j,sizeof *j) ||
       !BR_PATTERN_RAM_RANGE(sector,PFS_SECTOR) ||
       br_ps_target_journal_overlap(j,sizeof *j,sector,PFS_SECTOR) ||
       br_ps_target_journal_overlap(j,sizeof *j,n,sizeof *n) ||
       br_ps_target_journal_overlap(j,sizeof *j,n->store,sizeof *n->store) ||
       br_ps_target_journal_overlap(sector,PFS_SECTOR,n,sizeof *n) ||
       br_ps_target_journal_overlap(sector,PFS_SECTOR,n->store,sizeof *n->store) ||
       !br_pattern_loan_matches(&br_ps_target_project_token,token) ||
       !br_ps_target_project_exclusive(0) || n->ctx!=j || n->journal_program!=br_ps_target_journal_program ||
       n->store->read!=br_ps_target_store_read || n->store->program!=br_ps_target_store_program ||
       n->store->base!=BR_PROJECT_BASE) return 0;
    h=n->store->ctx;if(!h || h->token!=token || &h->store!=n->store) return 0;
    memset(j,0,sizeof *j);j->token=token;j->ns=n;j->pinned=*n;j->pinned_store=*n->store;j->sector=sector;return 1;
}
int br_ps_target_journal_program(void *ctx,uint32_t off,const void *data,uint32_t size)
{
    br_ps_target_journal *j=ctx; uint8_t event[48]; int rc;
    if(j!=&br_ps_target_journal_instance || !j->ns || !data || !br_ps_target_journal_exclusive(j) || j->writing)return -1;
    if(size==44u) {
        memcpy(event,data,44);event[44]=0x50;event[45]=0x4e;event[46]=0x43;event[47]=0x32;
        if(pns_journal_gate_arm(&j->gate,j->ns,j,br_ps_target_journal_exclusive,
                                br_ps_target_journal_raw,event,j->sector)!=PNS_OK)return -1;
    }
    j->writing=1;rc=pns_journal_gate_program(&j->gate,off,data,size);j->writing=0;
    if(rc || size==4u)pns_journal_gate_disarm(&j->gate);
    return rc;
}
void br_ps_target_journal_close(br_ps_target_journal *j)
{ if(j==&br_ps_target_journal_instance && !j->writing) { pns_journal_gate_disarm(&j->gate);j->token=0;j->ns=0;j->sector=0; } }
#endif
static int br_ps_target_export(void *ctx, br_pattern_bank *b, br_pattern_snapshot *s, fm1_pattern_song *song)
{ (void)ctx; return br_ps_host_export(b,s,song); }
static int br_ps_target_can_adopt(void *ctx,const br_pattern_bank *b,const br_pattern_snapshot *s,const fm1_pattern_song *song)
{ (void)ctx; return br_ps_host_can_adopt_copy(b,s,song); }
static int br_ps_target_adopt(void *ctx,const br_pattern_bank *b,const br_pattern_snapshot *s,const fm1_pattern_song *song)
{ (void)ctx; return br_ps_host_adopt_copy(b,s,song); }
static int br_ps_target_restore_pattern(void *ctx,const br_pattern_snapshot *s)
{ (void)ctx; return br_pattern_snapshot_apply(s)==0; }
static const fm1_project_adapter_ops br_ps_target_project_ops = {
    0, br_ps_target_project_exclusive, br_pattern_session_capture, br_pattern_session_validate,
    br_pattern_session_restore, br_ps_target_export, br_ps_target_can_adopt,
    br_ps_target_adopt, br_ps_target_restore_pattern
};
int br_ps_target_project_mode(uint32_t token, int loading)
{
    if(!br_pattern_loan_matches(&br_ps_target_project_token,token) || !br_ps_target_project_exclusive(0) ||
       !br_ps_target_session_lent || (loading!=0 && loading!=1)) return 0;
    br_ps_target_project_adapter.live = loading ? br_ps_target_project_work.pattern : &br_ps_target_live;
    return 1;
}
int br_ps_target_project_begin(int loading, fm1_project_adapter **adapter, const fm1_project_adapter_ops **ops, uint32_t *token_out)
{
    br_pattern_snapshot *previous, *stage; uint8_t *scratch; size_t scratch_capacity, bytes;
    void *loaded, *pending, *undo, *session_scratch; size_t session_scratch_size;
    uint32_t token; int park_result;
    if(!adapter || !ops || !token_out || (loading!=0 && loading!=1 && loading!=2) || br_ps_target_project_token.active ||
       !flash_ok || br_playing || !br_engine_quiet() || !br_fx_quiet() || !br_quiescent_transaction_begin()) return 0;
    token=br_pattern_loan_claim(&br_ps_target_project_token);
    if(!token) { br_quiescent_transaction_end(); return 0; }
    br_ps_project_quarantine=1u;
    if(!br_ps_project_watch_begin(token)) goto fail_unparked;
    park_result=br_pattern_park_begin(&br_ps_target_park_io,&br_ps_target_park_ops,BR_PROJECT_BASE,BR_PROJECT_END);
    if(park_result==BR_PARK_BUSY) goto fail_unparked;
    if(park_result!=BR_PARK_OK) return 0; /* fail closed: keep transaction/fence */
    br_ps_target_worker_certified=1; br_ps_target_reserved_sectors=0;
    if(!br_ps_project_watch_ok()) { (void)br_pattern_park_abort(&br_ps_target_park_io); return 0; }
    if(!br_ps_host_workspace_acquire(&previous,&stage,&scratch,&scratch_capacity)) goto fail_parked;
    br_ps_target_workspace_lent=1;
    br_ps_target_scratch_capacity=scratch_capacity;
    if(scratch_capacity<PFS_SECTOR) goto fail_parked;
    br_ps_target_project_adapter.work=&br_ps_target_project_work;
    br_ps_target_project_adapter.sector=scratch;
    br_ps_target_project_adapter.arena=br_ps_target_load_arena;
    br_ps_target_project_adapter.arena_capacity=sizeof br_ps_target_load_arena;
    if(loading==2) {
        if(!br_ps_project_watch_ok()) { (void)br_pattern_park_abort(&br_ps_target_park_io); return 0; }
        *adapter=&br_ps_target_project_adapter; *ops=&br_ps_target_project_ops; *token_out=token; return 1;
    }
    if(!br_pattern_session_loan_acquire(3u,br_ps_target_project_exclusive,0,&loaded,&pending,&undo,&bytes)) goto fail_parked;
    br_ps_target_session_lent=1;
    if(!br_ps_settings_scratch_acquire(br_ps_target_project_exclusive,0,&session_scratch,&session_scratch_size)) goto fail_parked;
    br_ps_target_settings_lent=1;
    if(bytes!=FM1_PROJECT_SESSION || session_scratch_size<bytes || !br_pattern_session_bind(session_scratch,bytes)) goto fail_parked;
    br_ps_target_project_work.arena=br_ps_target_load_arena;
    br_ps_target_project_work.arena_capacity=sizeof br_ps_target_load_arena;
    br_ps_target_project_work.pattern=stage;
    br_ps_target_project_work.session=pending;
    br_ps_target_project_work.compressed=0; br_ps_target_project_work.compressed_capacity=0;
    br_ps_target_project_adapter.work=&br_ps_target_project_work;
    br_ps_target_project_adapter.sector=scratch;
    br_ps_target_project_adapter.arena=br_ps_target_load_arena;
    br_ps_target_project_adapter.arena_capacity=sizeof br_ps_target_load_arena;
    br_ps_target_project_adapter.session=loaded; br_ps_target_project_adapter.previous=pending;
    br_ps_target_project_adapter.previous_live=previous;
    br_ps_target_project_adapter.live=loading?stage:previous;
    if(!br_ps_project_watch_ok()) { (void)br_pattern_park_abort(&br_ps_target_park_io); return 0; }
    *adapter=&br_ps_target_project_adapter; *ops=&br_ps_target_project_ops; *token_out=token; return 1;
fail_parked:
    if(!br_ps_project_watch_ok()) { (void)br_pattern_park_abort(&br_ps_target_park_io); return 0; }
    if(br_pattern_park_end(&br_ps_target_park_io)!=BR_PARK_OK) return 0;
fail_unparked:
    br_ps_project_watch_token=0; br_ps_project_quarantine=0u;
    (void)br_pattern_loan_release(&br_ps_target_project_token,token);
    br_quiescent_transaction_end(); return 0;
}
br_pattern_park_io *br_ps_target_project_park(uint32_t token)
{
    if(!br_pattern_loan_matches(&br_ps_target_project_token,token) || !br_ps_target_project_exclusive(0)) return 0;
    return &br_ps_target_park_io;
}
size_t br_ps_target_project_scratch_capacity(uint32_t token)
{
    if(!br_pattern_loan_matches(&br_ps_target_project_token,token) || !br_ps_target_project_exclusive(0) || !br_ps_target_workspace_lent) return 0;
    return br_ps_target_scratch_capacity;
}
int br_ps_target_project_faulted(void)
{
    return br_ps_project_post_release_fault || br_pattern_park_faulted(&br_ps_target_park_io) || fm1_cpu1_worker_uncontained_fault_get();
}
int br_ps_target_project_end(uint32_t token)
{
    uint32_t irq;
    if(!br_pattern_loan_matches(&br_ps_target_project_token,token)) return 0;
    if(!br_ps_project_watch_ok()) { (void)br_pattern_park_abort(&br_ps_target_park_io); return 0; }
    if(br_pattern_park_end(&br_ps_target_park_io)!=BR_PARK_OK) return 0;
    /* The park is released; atomically check for input during resume before
     * clearing the token and editor quarantine. A late observed event is a
     * fatal partial transaction, not an unchanged-state result. */
    irq=irq_save();
    if(!br_ps_project_watch_ok_locked()) {
        br_ps_project_post_release_fault=1; irq_restore(irq);
        (void)fm1_cpu1_worker_abort_hold_reset(); return 0;
    }
    br_ps_project_watch_token=0;
    (void)br_pattern_loan_release(&br_ps_target_project_token,token);
    br_ps_project_quarantine=0u;
    br_quiescent_transaction_end(); irq_restore(irq); return 1;
}
#if defined(FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY) && FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY
/* Sizing/host-test seam only. The included runtime header deliberately rejects
 * device compilation in the working source. No target flash authority exists. */
#include "pattern_fixed_runtime.h"
static int br_ps_runtime_port_begin(void *ctx,int mode,fm1_project_adapter **adapter,
    const fm1_project_adapter_ops **ops,uint32_t *token,br_pattern_park_io **park,size_t *capacity)
{
    uint32_t t=0; fm1_project_adapter *a=0; const fm1_project_adapter_ops *o=0;
    br_pattern_park_io *p=0; size_t cap=0; (void)ctx;
    if(!adapter || !ops || !token || !park || !capacity) return 0;
    if(!br_ps_target_project_begin(mode,&a,&o,&t)) return br_ps_target_project_faulted()?-1:0;
    p=br_ps_target_project_park(t); cap=br_ps_target_project_scratch_capacity(t);
    if(!a || !o || !p || !cap) return br_ps_target_project_end(t)?0:-1;
    *adapter=a;*ops=o;*park=p;*capacity=cap;*token=t;return 1;
}
static int br_ps_runtime_port_mode(void *ctx,uint32_t token,int loading)
{ (void)ctx; return br_ps_target_project_mode(token,loading); }
static int br_ps_runtime_port_end(void *ctx,uint32_t token)
{ (void)ctx; return br_ps_target_project_end(token); }
static int br_ps_runtime_port_fence(void *ctx,uint32_t token)
{ (void)ctx; return br_pattern_loan_matches(&br_ps_target_project_token,token) && br_ps_target_project_exclusive(0); }
static int br_ps_runtime_port_fault(void *ctx)
{ (void)ctx; return br_ps_target_project_faulted(); }
/* Link-only sizing assumes the approved non-adversarial fixed partition.
 * AUTO validates both sectors under the fence and bypasses these callbacks;
 * physical writes remain disabled pending target qualification. */
static int br_ps_runtime_port_pair(void *ctx,uint32_t token,uint32_t slot,int write,unsigned *initializing)
{ (void)ctx;(void)token;(void)slot;(void)write;(void)initializing;return 0; }
static int br_ps_runtime_port_cert(void *ctx,uint32_t token,uint32_t slot,unsigned initializing,uint32_t address)
{ (void)ctx;(void)token;(void)slot;(void)initializing;(void)address;return 0; }
static int br_ps_runtime_port_activate(void *ctx,uint32_t token,uint32_t slot)
{ (void)ctx;(void)token;(void)slot;return 0; }
static int br_ps_runtime_port_commit(void *ctx,uint32_t token,int slot)
{
    uint32_t irq; (void)ctx;
    if(slot < -1 || slot >= 12 || !br_ps_runtime_port_fence(0,token)) return 0;
    irq=irq_save();
    if(!br_pattern_loan_matches(&br_ps_target_project_token,token) || !br_ps_target_parked(0) || !br_ps_project_watch_ok_locked()) { irq_restore(irq); return 0; }
    es.current=slot; irq_restore(irq); return 1;
}
#if defined(FM1_DEVICE)
static fm1_fixed_runtime br_ps_target_fixed_runtime __attribute__((section(".pool"), aligned(4)));
#else
static fm1_fixed_runtime br_ps_target_fixed_runtime;
#endif
static const fm1_fixed_runtime_port br_ps_target_fixed_port={
    0,br_ps_runtime_port_begin,br_ps_runtime_port_mode,br_ps_runtime_port_end,
    br_ps_runtime_port_fence,br_ps_runtime_port_fault,br_ps_runtime_port_pair,
    br_ps_runtime_port_cert,br_ps_runtime_port_activate,br_ps_runtime_port_commit,
    {0x46,0x4d,0x31,0x2d,0x50,0x41,0x54,0x54,0x45,0x52,0x4e,0x2d,0x30,0x30,0x30,0x31},12u,1u
};
static int br_ps_target_fixed_may_erase(uint32_t off)
{
    fm1_fixed_runtime *r=&br_ps_target_fixed_runtime;
    if(!r->active || !br_ps_runtime_port_fence(0,r->token)) return 0;
    return pps_owner_may_erase(&r->owner[0],off)==1 || pps_owner_may_erase(&r->owner[1],off)==1;
}
int br_ps_target_fixed_runtime_bind(void)
{ return fm1_fixed_runtime_bind(&br_ps_target_fixed_runtime,&br_ps_target_fixed_port); }
int br_pp_target_ready(void)
{ return br_ps_target_fixed_runtime.port && !br_ps_target_fixed_runtime.bridge.fault; }
int br_pp_target_scan(unsigned slot,char name[13])
{ return fm1_fixed_runtime_scan(&br_ps_target_fixed_runtime,slot,name); }
int br_pp_target_save(unsigned slot,const char *name)
{ return fm1_fixed_bridge_save(&br_ps_target_fixed_runtime.bridge,slot,name); }
int br_pp_target_load(unsigned slot)
{ return fm1_fixed_bridge_load(&br_ps_target_fixed_runtime.bridge,slot); }
int br_pp_target_recover(int *slot)
{
    int rc;
    if(!slot || br_ps_external_restore_completed) return PFS_INVALID;
    rc=fm1_fixed_bridge_recover(&br_ps_target_fixed_runtime.bridge);
    if(rc==PFS_OK) *slot=es.current;
    return rc;
}
int br_pp_target_init_project(void)
{ return fm1_fixed_runtime_new(&br_ps_target_fixed_runtime,(const uint8_t *)&es.factory,sizeof es.factory); }
#endif
#endif

#ifndef BR_AUDIO_SOURCE_FX_FUSION_DEFAULT
#define BR_AUDIO_SOURCE_FX_FUSION_DEFAULT 1u
#endif

static uint32_t br_control_turn_restores_session(uint32_t knob)
{
    (void)knob;
    return 0u;   /* the ARP page no longer hosts UNDO */
}

#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
static volatile uint32_t br_diagnostic_muted;
#include "instrument_diagnostic.c"
#else
#define br_half_timing_mark_failed(unconfirmed) ((void)(unconfirmed))
#define br_half_timing_mark_late_drop() ((void)0)
#endif

#if !BR_AUDIO_FUSION_ONLY_BUILD && defined(FM1_DEVICE)
static br_pipeline_frame_t br_audio_pipeline_frames[2]
    __attribute__((section(".pool"), aligned(4)));
#elif !BR_AUDIO_FUSION_ONLY_BUILD
static br_pipeline_frame_t br_audio_pipeline_frames[2];
#endif
#if !BR_AUDIO_FUSION_ONLY_BUILD
static uint32_t br_audio_pipeline_write_slot, br_audio_pipeline_valid;
static uint32_t br_audio_pipeline_generation;
#endif

static void br_audio_commit_block(int32_t *out, uint32_t frames)
{
#if defined(FM1_DEVICE) && FM1_DEVELOPMENT_DIAGNOSTICS
    br_tail_stamp[BR_TAIL_COMMIT_START] = fm1_ticks();
#endif
#if FM1_RUNTIME_CHUNK_PROFILE
    fm1_runtime_trace_mark(FM1_RUNTIME_TRACE_MARK_COMMIT_BEGIN);
#endif
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
    br_half_timing_commit_block(out, frames);
#else
    platform_audio_block_done(out, frames);
#endif
#if defined(FM1_DEVICE) && FM1_DEVELOPMENT_DIAGNOSTICS
    br_tail_stamp[BR_TAIL_COMMIT_END] = fm1_ticks();
#endif
#if FM1_RUNTIME_CHUNK_PROFILE
    fm1_runtime_trace_commit(fm1_ticks());
#endif
}

static uint32_t app_audio(int32_t *out, uint32_t n)
{
    uint32_t render_result = FM1_AUDIO_RENDER_OK;
    if (!br_ready) return FM1_AUDIO_RENDER_OK; /* precleared silence */
    if (br_engine_pipeline_uncontained_fault_get())
        return FM1_AUDIO_RENDER_UNCONTAINED;
    if (br_diagnostic_restore_in_progress) return FM1_AUDIO_RENDER_OK;
#if defined(FM1_DEVICE)
    extern volatile uint32_t fm1_cpu1_worker_fault;
#if FM1_DEVELOPMENT_DIAGNOSTICS
    fm1_audio_cpu1_join_clear();
#endif
#endif
    uint32_t off;
#if !BR_AUDIO_FUSION_ONLY_BUILD
    uint32_t pipeline_mode = br_engine_pipeline_mode_get();
    uint32_t pipeline_generation = br_engine_pipeline_generation_get();
    if (pipeline_generation != br_audio_pipeline_generation) {
        br_audio_pipeline_generation = pipeline_generation;
        br_audio_pipeline_valid = 0u;
        br_audio_pipeline_write_slot = 0u;
    }
    if (!pipeline_mode || (n % BR_BLOCK) != 0u) br_audio_pipeline_valid = 0u;
#endif
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
#if FM1_DEVELOPMENT_DIAGNOSTICS
    uint32_t engine_ticks = 0, fx_ticks = 0, start;
#endif
    br_diagnostic_audio_guard();
#if defined(FM1_DEVICE)
    if (fm1_cpu1_worker_fault) {
        br_diagnostic_muted = 1u;
        br_diagnostic_disarm();
        br_diagnostic_abort = 3u;
    }
#endif
    if (br_diagnostic_muted
#if defined(FM1_DEVICE)
        || fm1_cpu1_worker_fault
#endif
       ) {
        if (br_engine_pipeline_uncontained_fault_get())
            return FM1_AUDIO_RENDER_UNCONTAINED;
        memset(out, 0, n * 2u * sizeof *out);
        return FM1_AUDIO_RENDER_OK;
    }
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
    /* The interval begins after ownership, restore, diagnostic guard, and
     * mute checks. Per-block platform scaling/UAC spans are subtracted below. */
    br_half_timing_begin();
#endif
#if defined(FM1_DEVICE) && FM1_DEVELOPMENT_DIAGNOSTICS
    br_diagnostic_synth_ticks = 0;
#endif
#endif
    for (off = 0; off < n; off += BR_BLOCK) {
        uint32_t size = n - off < BR_BLOCK ? n - off : BR_BLOCK;
#if !BR_AUDIO_FUSION_ONLY_BUILD
        uint32_t use_pipeline = pipeline_mode && size == BR_BLOCK && (n % BR_BLOCK) == 0u;
#endif
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
        uint32_t chunk_failed = 0u;
#endif
        int32_t (*render_bus)[BR_BLOCK] = bus;
#if BR_AUDIO_FUSION_ONLY_BUILD
        br_engine_pipeline_begin(0, 0, 0u);
#else
        br_pipeline_frame_t *current_frame = 0;
        br_pipeline_frame_t *previous_frame = 0;
        if (use_pipeline) {
            current_frame = &br_audio_pipeline_frames[br_audio_pipeline_write_slot];
            if (br_audio_pipeline_valid)
                previous_frame = &br_audio_pipeline_frames[br_audio_pipeline_write_slot ^ 1u];
            render_bus = current_frame->bus;
            memset(out + 2u * off, 0, size * 2u * sizeof *out);
            br_engine_pipeline_begin(previous_frame, out + 2u * off, size);
        } else {
            br_engine_pipeline_begin(0, 0, 0u);
        }
#endif
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
#if FM1_DEVELOPMENT_DIAGNOSTICS
        start = fm1_ticks();
#endif
        br_half_timing_attempt_chunk();
#endif
        br_engine_render(render_bus, size);
#if defined(BR_SPLIT_AUDIO)
        if (br_engine_late_drop_get()) {
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
#if FM1_DEVELOPMENT_DIAGNOSTICS
            engine_ticks += fm1_ticks() - start;
#endif
            br_half_timing_mark_late_drop();
#endif
#if defined(FM1_DEVICE) && FM1_DEVELOPMENT_DIAGNOSTICS
            fm1_audio_cpu1_join_clear();
#endif
            render_result = FM1_AUDIO_RENDER_DROP_LATE;
            goto br_audio_timing_finish;
        }
#endif
#if defined(BR_SOURCE_QUEUE_TEST)
        /* A failed worker batch ends this host-simulated DMA half at the same
         * first failed chunk as the target wrapper; do not launch later chunks. */
        if (br_audio_source_worker_fault) {
            uint32_t uncontained = br_engine_pipeline_uncontained_fault_get();
#if !BR_AUDIO_FUSION_ONLY_BUILD
            if (!uncontained && use_pipeline && pipeline_mode == 3u) {
                uint32_t finish_status = br_engine_pipeline_finish();
                if (finish_status == 2u) uncontained = 1u;
            }
#endif
            br_half_timing_mark_failed(uncontained);
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
            br_diagnostic_muted = 1u;
            br_diagnostic_disarm();
            br_diagnostic_abort = 3u;
#endif
            render_result = uncontained ? FM1_AUDIO_RENDER_UNCONTAINED :
                                          FM1_AUDIO_RENDER_CONTAINED_FAULT;
            goto br_audio_timing_finish;
        }
#endif
#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_SOURCE_QUEUE_TEST)
        if (br_audio_source_worker_fault) {
            uint32_t uncontained = br_engine_pipeline_uncontained_fault_get();
            br_half_timing_mark_failed(uncontained);
            if (uncontained) {
                br_diagnostic_muted = 1u;
                br_diagnostic_disarm();
                br_diagnostic_abort = 3u;
                goto br_audio_timing_finish; /* never touch a possibly owned buffer */
            }
            uint32_t finish_status = 1u;
#if !BR_AUDIO_FUSION_ONLY_BUILD
            if (use_pipeline && (pipeline_mode == 2u || pipeline_mode == 3u))
                finish_status = br_engine_pipeline_finish(); /* join or verified hold-reset before reuse */
#endif
            if (finish_status == 2u) {
                br_half_timing_mark_failed(1u);
#if !BR_AUDIO_FUSION_ONLY_BUILD
                /* In hybrid mode C1 may still own current_frame->bus. Do not
                 * write even its metadata until reset is confirmed. */
                if (current_frame && pipeline_mode != 3u) current_frame->valid = 0u;
#endif
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
                br_diagnostic_muted = 1u;
                br_diagnostic_disarm();
                br_diagnostic_abort = 3u;
#endif
                goto br_audio_timing_finish; /* do not touch an owned buffer */
            }
#if defined(FM1_DEVICE) && FM1_DEVELOPMENT_DIAGNOSTICS
            fm1_audio_cpu1_join_clear();
#endif
            br_diagnostic_muted = 1u;
            br_diagnostic_disarm();
            br_diagnostic_abort = 3u;
            render_result = FM1_AUDIO_RENDER_CONTAINED_FAULT;
            goto br_audio_timing_finish;
        }
#endif
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
#if FM1_DEVELOPMENT_DIAGNOSTICS
        engine_ticks += fm1_ticks() - start;
        start = fm1_ticks();
#endif
#endif
#if defined(FM1_DEVICE)
#if FM1_DEVELOPMENT_DIAGNOSTICS
        br_tail_stamp[BR_TAIL_FX_START] = start;
#endif
        br_fx_output_gain_q12 = master_q12;
        br_fx_output_scale_request = 1u;
        br_fx_output_prescaled = 0u;
#endif
#if !BR_AUDIO_FUSION_ONLY_BUILD
        if (use_pipeline) {
            uint32_t finish_status = br_engine_pipeline_finish();
            if (finish_status == 2u) {
                br_half_timing_mark_failed(1u);
                /* Mode 3's fixed task can still own the current source bus. */
                if (pipeline_mode != 3u) current_frame->valid = 0u;
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
                br_diagnostic_muted = 1u;
                br_diagnostic_disarm();
                br_diagnostic_abort = 3u;
#endif
                goto br_audio_timing_finish; /* no reuse while CPU1 ownership is unknown */
            }
            if (!br_engine_pipeline_capture(current_frame, render_bus, size)) {
                br_audio_pipeline_valid = 0u;
                memset(out + 2u * off, 0, size * 2u * sizeof *out);
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
                chunk_failed = 1u;
#endif
                br_half_timing_mark_failed(0u);
            } else {
                br_audio_pipeline_valid = 1u;
                br_audio_pipeline_write_slot ^= 1u;
            }
        } else {
            br_fx_process(bus, out + 2u * off, size);
        }
#else
        br_fx_process(bus, out + 2u * off, size);
#endif
#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_SOURCE_QUEUE_TEST)
        if (br_audio_source_worker_fault) {
            uint32_t uncontained = br_engine_pipeline_uncontained_fault_get();
            br_half_timing_mark_failed(uncontained);
            if (uncontained) {
                br_diagnostic_muted = 1u;
                br_diagnostic_disarm();
                br_diagnostic_abort = 3u;
                goto br_audio_timing_finish; /* the worker may still own this buffer */
            }
#if defined(FM1_DEVICE) && FM1_DEVELOPMENT_DIAGNOSTICS
            fm1_audio_cpu1_join_clear();
#endif
            br_diagnostic_muted = 1u;
            br_diagnostic_disarm();
            br_diagnostic_abort = 3u;
            render_result = FM1_AUDIO_RENDER_CONTAINED_FAULT;
            goto br_audio_timing_finish;
        }
#endif
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
#if FM1_DEVELOPMENT_DIAGNOSTICS
        fx_ticks += fm1_ticks() - start;
#endif
#endif
#if defined(FM1_DEVICE) && FM1_DEVELOPMENT_DIAGNOSTICS
        br_tail_stamp[BR_TAIL_FX_END] = fm1_ticks();
#endif
        /* Punch-in filter and trem work on the finished mix; with no effect held
         * this only notes the state change and costs a few instructions. */
        {
            uint32_t bpm = br_bpm < BR_BPM_MIN ? BR_BPM_MIN : br_bpm > BR_BPM_MAX ? BR_BPM_MAX : br_bpm;
            uint32_t effect = br_punch_effect;
            if (effect == BR_PUNCH_ECHO && br_punch_dsp.last != BR_PUNCH_ECHO) {
                /* Borrow the buffer of the last track with no delay send. */
                uint32_t p = BR_PARTS;
                while (p-- > 1u && br_part[p].sound.delay) {}
                br_punch_echo_part = (uint8_t)p;
                br_punch_dsp.line = br_fx_echo_line(p, &br_punch_dsp.line_len);
            }
            br_punch_process(&br_punch_dsp, &br_punch_cfg, effect,
                             out + 2u * off, size, FS * 60u / bpm);
        }
#if (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO)) || defined(BR_SOURCE_QUEUE_TEST)
        if (!br_audio_source_worker_fault)
#endif
            br_engine_post_join_finish();   /* no-op if CPU1 already ran it */
        br_audio_commit_block(out + 2u * off, size);
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
        if (!chunk_failed) br_half_timing_complete_chunk();
#endif
    }
#if defined(BR_SPLIT_AUDIO) || !BR_AUDIO_FUSION_ONLY_BUILD || defined(BR_SOURCE_QUEUE_TEST) || \
    (defined(FM1_DEVICE) && defined(BR_CPU1_AUDIO))
br_audio_timing_finish:
#endif
    ;
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
#if defined(FM1_DEVICE) && FM1_DEVELOPMENT_DIAGNOSTICS
    br_diagnostic_detail[0] = br_diagnostic_synth_ticks;
    br_diagnostic_detail[2] = engine_ticks - br_diagnostic_synth_ticks;
    if (br_diagnostic_detail[0] > br_diagnostic_detail[1])
        br_diagnostic_detail[1] = br_diagnostic_detail[0];
    if (br_diagnostic_detail[2] > br_diagnostic_detail[3])
        br_diagnostic_detail[3] = br_diagnostic_detail[2];
#endif
#if FM1_DEVELOPMENT_DIAGNOSTICS
    br_diagnostic_profile[0] = engine_ticks;
    br_diagnostic_profile[1] = fx_ticks;
    if (engine_ticks > br_diagnostic_profile[2]) br_diagnostic_profile[2] = engine_ticks;
    if (fx_ticks > br_diagnostic_profile[3]) br_diagnostic_profile[3] = fx_ticks;
#endif
    br_half_timing_finish();
#endif
    return render_result;
}

static void br_app_init(void)
{
    static const uint8_t startup[BR_PARTS] = {
        BR_FIRST_PLAITS, BR_FIRST_PLAITS, BR_FIRST_PLAITS, BR_KIT_BRAIDS
    };
    uint32_t p, i;
    /* Startup can follow a failed CPU1 park/reset attempt. Do not initialize
     * session, FX, or engine state unless ownership is known and the audio
     * ISR is excluded from the initialization window. */
    if (!br_quiescent_transaction_begin()) return;
    for (i = 0u; i < 7u; ++i) br_midi_output_release_destination(i);
    memset(br_midi_out_held, 0, sizeof br_midi_out_held);
    memset(br_midi_out_sustain, 0, sizeof br_midi_out_sustain);
    br_ui_recording_state_reset();
    memset(&br_ui, 0, sizeof br_ui);
    br_encoder_reset();
    br_ui.edit_key = 0xFFFFFFFFu;
    br_ui.step_press_key = 0xFFFFFFFFu;
    br_bpm = 120; br_playing = br_recording = br_hold_on = br_arp_on = 0; br_solo_mask = 0u; br_kit_mute_mask = br_kit_solo_mask = 0u;
    memset(&br_punch_ks, 0, sizeof br_punch_ks); br_punch_effect = 0u;
    memset(&br_sel, 0, sizeof br_sel);
    { static const br_punch_cfg_t d = BR_PUNCH_CFG_DEFAULTS; br_punch_cfg = d; }
    memset(br_arp_keys, 0, sizeof br_arp_keys);
    br_master.revision = 0;
    br_master.fx = (br_master_fx_t){64, 0, 0, 0, 0, BR_HEAT_BIT, 0};
    br_master.select_target = BR_SELECT_MASTER_FILTER;
    br_master.comp = (br_compressor_config_t)BR_COMP_DEFAULTS;
    br_save_pending = br_undo_valid = 0;
    for (p = 0; p < BR_PARTS; p++) {
        br_ui.note[p] = 60u;
        br_sound_t sound;
        br_preset_sound(startup[p], &sound);
        br_part[p].sound = sound;
        br_mod_config_t mod;
        br_fx_config_t fx;
        br_module_defaults(&mod, &fx);
        br_part[p].mod = mod; br_part[p].fx = fx;
        br_part[p].revision = 0;
        br_part[p].length = BR_BANK_STEPS; br_part[p].division = 1;
        br_part[p].swing = 0; br_part[p].gate = 80;
        br_arp_part_defaults(&br_part[p]);
        for (i = 0; i < BR_STEPS; i++) br_part[p].step[i] = 0;
        for (i = 0; i < BR_DRUM_LANES; i++) {
            br_ui.seq_default[p][i].note = BR_STEP_PITCH_DEFAULT;
            br_ui.seq_default[p][i].length = p == BR_DRUM_PART ? 0u : BR_STEP_LENGTH_DEFAULT;
            br_ui.seq_default[p][i].velocity = 100u;
            br_ui.seq_default[p][i].condition = BR_STEP_COND_ALWAYS;
            br_part[p].lane_level[i] = 100;
            br_part[p].lane_gate[i] = 128;
        }
        br_step_pos[p] = 0;
    }
#if (defined(FM1_DEVICE) || defined(DINGHY_DEVICE_SAVE_TEST)) && !defined(DINGHY_EMULATOR_SAVE)
    memset(&es,0,sizeof es);es.current=-1;
    memset(es_directory,0,sizeof es_directory);
    br_step_metadata_clear();
    (void)br_capture(&es.factory);
#endif
    br_load_session();
#if (defined(FM1_DEVICE) || defined(DINGHY_DEVICE_SAVE_TEST)) && !defined(DINGHY_EMULATOR_SAVE)
#if (!defined(BR_PATTERN_PROJECT_HOST) || !BR_PATTERN_PROJECT_HOST) && (!defined(BR_PATTERN_PROJECT_STREAM_HOST) || !BR_PATTERN_PROJECT_STREAM_HOST) && (!defined(FM1_PATTERN_PROJECT_TARGET_EXPERIMENT) || !FM1_PATTERN_PROJECT_TARGET_EXPERIMENT)
    if(br_project_recover(&es.current)==PS_OK && es.current>=0)es_scan_slot(es.current);
#endif
#endif
    if (br_engine_pipeline_uncontained_fault_get()) {
        br_quiescent_transaction_end();
        return;
    }
    br_fx_init();
    br_engine_init();
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT
    /* Reserve staging in this link-only build, including when no loader is
     * wired yet, so the RAM audit includes its actual footprint. */
    *(volatile uint8_t *)&br_ps_target_load_arena[0] = 0u;
    br_ps_target_project_adapter.current_slot=-1;
#if defined(FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY) && FM1_PATTERN_RUNTIME_TARGET_LINK_ONLY
    (void)br_ps_target_fixed_runtime_bind();
#endif
    if (!br_ps_host_init(br_ps_target_arena, sizeof br_ps_target_arena,
            &br_ps_target_live, &br_ps_target_stage, 0, br_ps_target_encoded,
            sizeof br_ps_target_encoded, br_ps_target_quiet, 0) ||
        !br_ps_runtime_bind(&br_ps_target_pub, &br_ps_target_stage, 0, 0))
        br_message("PATTERN INIT FAILED");
#endif
#ifdef BR_SPLIT_AUDIO
    br_engine_source_fx_fusion_set(BR_AUDIO_SOURCE_FX_FUSION_DEFAULT);
#endif
    /* Effective controls are rebuilt by br_engine_init(); warm only while
     * startup still owns the quiescent transaction and audio is stopped. */
    if (!br_playing && br_engine_quiet() && br_fx_quiet())
        br_fx_warm_controls_quiescent();
#if defined(FM1_DEVICE) || defined(BR_DIAGNOSTIC_TEST)
    br_half_timing_reset();
#endif
    lcd_fill(0, 0, 240, 240, C_BLACK);
#if defined(FM1_DEVICE)
    br_intro_until = fm1_ms + BR_INTRO_MS;        /* two seconds of the name before the instrument appears */
#endif
    br_ui.redraw = 1;
    br_draw();
    br_quiescent_transaction_end();
}

#if defined(FM1_PATTERN_TICK_CLAIM_PROTOTYPE) && FM1_PATTERN_TICK_CLAIM_PROTOTYPE
/* The runtime bridge supplies these; a failed claim does not consume input.
 * Hardware bitsets can still coalesce repeated edges while processing waits. */
extern int br_pattern_irq_foreground_claim(void);
extern void br_pattern_irq_foreground_release(void);
extern uint32_t br_pattern_irq_late_count(void);
extern int br_ps_runtime_service(void);
extern int br_ps_runtime_reset_stopped(void);
extern uint32_t br_pattern_irq_edits_blocked(void);
static uint32_t br_ps_tick_start, br_ps_tick_late_start;
#if !defined(BR_UI_CLOCK_SUPPORT)
static uint32_t br_ps_tick_claimed;
#endif
static uint32_t br_ps_tick_max_ms, br_ps_tick_late_blocks, br_ps_reset_pending, br_ps_last_playing;
static void br_ps_tick_release(void)
{
    if (br_ps_tick_claimed) {
        uint32_t elapsed=fm1_ms-br_ps_tick_start;
        uint32_t late=br_pattern_irq_late_count()-br_ps_tick_late_start;
        if (elapsed>br_ps_tick_max_ms) br_ps_tick_max_ms=elapsed;
        br_ps_tick_late_blocks+=late;
        br_pattern_irq_foreground_release(); br_ps_tick_claimed=0u;
    }
}
/* Called with the tick claim held, after panel and MIDI transport input. */
static void br_ps_tick_transport(void)
{
    if (!br_playing && br_ps_last_playing && br_ps_state.queued<FM1_PATTERN_COUNT) br_ps_reset_pending=1u;
    br_ps_last_playing=br_playing;
}
static int br_ps_tick_service(void)
{
    if (br_engine_pipeline_uncontained_fault_get() || br_diagnostic_restore_in_progress) return 0;
    if (br_ps_runtime_service() < 0) br_ps_reset_pending=1u;


    if (br_ps_reset_pending) {
        int reset=0;
        if (!br_playing && br_quiescent_transaction_begin()) {
            uint32_t irq_state=br_foreground_irq_save();
            reset=br_ps_runtime_reset_stopped();
            br_foreground_irq_restore(irq_state);
            br_quiescent_transaction_end();
        }
        if (reset) { br_ps_reset_pending=0u; br_message("PATTERN STOPPED"); }
        else br_message("PATTERN SWITCH ERROR");
        return 1;
    }
    return 0;
}
#else
#define br_ps_tick_release() ((void)0)
#define br_ps_tick_service() ((void)0)
#define br_ps_tick_transport() ((void)0)
#endif
static void br_app_tick_inner(void)
{
    if (br_engine_pipeline_uncontained_fault_get() ||
        br_diagnostic_restore_in_progress) return;
#if defined(FM1_DEVICE)
    if (midi_in_overflow) {
        uint32_t irq_state;
        if (!br_foreground_mutation_begin(&irq_state)) return;
        mi_r = mi_w; midi_in_overflow = 0u;
        br_midi_before_session_restore();
        br_foreground_mutation_end(irq_state);
    }
#endif
    uint32_t released, keys, handled_down = 0u;
    uint32_t buttons = fm1_input_edges(&released);
#if defined(FM1_DEVICE) && (!defined(FM1_DEVELOPMENT_DIAGNOSTICS) || FM1_DEVELOPMENT_DIAGNOSTICS)
    buttons|=br_project_diag_down;released|=br_project_diag_up;
    br_project_diag_down=br_project_diag_up=0;
#endif
    uint32_t edges = fm1_input_note_snapshot(&keys);
#if defined(FM1_PATTERN_PANEL_TEST) && FM1_PATTERN_PANEL_TEST
    /* Test input joins the ordinary key scan; physical input cancels it. */
    if (br_pattern_diag_key && (keys || fm1_in.buttons || br_ui.page != BR_PAGE_PS || es.mode
#if defined(FM1_DEVICE)
        || !usb.up || usb.detached || usb.suspended
#endif
        )) {
        br_ui.ps_held_used = 1u;
        br_pattern_diag_key = 0u;
    }
    keys |= br_pattern_diag_key;
#endif
    uint32_t down = (keys & ~br_ui.previous_keys) | edges;
    uint32_t up = br_ui.previous_keys & ~keys, k;
    uint32_t navigation_buttons = (1u << BTN_HOME) | (1u << BTN_ENV) | (1u << BTN_FX) |
        (1u << BTN_GLO) | (1u << BTN_ARP) | (1u << BTN_SEL) | (1u << BTN_LFO) |
        (1u << BTN_EDIT) | (1u << BTN_OCT_DOWN) | (1u << BTN_OCT_UP);
    uint32_t locked_at_start = br_step_press_pending() || br_ui.step_edit_active;
    uint32_t seq_grid;
    int32_t turn, select_turn;
    uint32_t save_active = 0u;
    uint8_t status, d1, d2;
    /* A new press edge on a previously held key proves it was released and
     * pressed again between snapshots. Release the old voice before handling
     * the new press, including when that press was already released too. */
    for (k = 0u; k < 27u; ++k) {
        uint32_t bit = 1u << k;
        if ((edges & br_ui.previous_keys & bit) && br_ui.held_note[k]) {
            BR_FOREGROUND_MUTATE(br_key_up(k));
            up &= ~bit;
        }
    }
    br_ui.previous_keys = keys;
    if (br_ui.retrig_key && !br_retrig_editing()) { br_ui.retrig_key = 0u; br_ui.redraw = 1u; }
#if defined(DINGHY_EMULATOR_SAVE) || defined(FM1_DEVICE) || defined(DINGHY_DEVICE_SAVE_TEST)
    save_active = es.mode != 0;
#endif
#ifdef BR_PATTERN_SONG_EXPERIMENT
    /* Consume both edges of OCT- + SEQ before the ordinary deferred gestures. */
    if (!save_active && !locked_at_start &&
        (((buttons & (1u << BTN_SEQ)) && ((fm1_in.buttons & (1u << BTN_OCT_DOWN)) || br_ui.midi_oct_pending || br_ui.fx_snap_pending)) ||
         ((buttons & (1u << BTN_OCT_DOWN)) && (fm1_in.buttons & (1u << BTN_SEQ))))) {
        buttons &= ~((1u << BTN_SEQ) | (1u << BTN_OCT_DOWN));
        br_ui.midi_oct_pending = br_ui.fx_snap_pending = br_ui.fx_snap_used = 0u;
        br_ui.seq_pressed = 0u; br_ui.page = BR_PAGE_PS;
        br_ui.ps_held_key = br_ui.ps_held_used = 0u; br_message("HOLD KEY: REC/PLAY"); br_ui.redraw = 1u;
    }
    if (!save_active && br_ui.page == BR_PAGE_PS && br_ui.ps_held_key &&
        (keys & (1u << (br_ui.ps_held_key - 1u))) && (buttons & ((1u << BTN_REC) | (1u << BTN_PLAY)))) {
        uint32_t pattern = br_key_place(br_ui.ps_held_key - 1u);
        int ok = (buttons & (1u << BTN_REC)) ? br_ps_copy_pattern(pattern) : br_ps_paste_pattern(pattern);
        br_ui.ps_held_used = 1u;
        br_message(ok ? ((buttons & (1u << BTN_REC)) ? "PATTERN COPIED" : "PATTERN PASTED") : "PATTERN FAILED");
        buttons &= ~((1u << BTN_REC) | (1u << BTN_PLAY));
    }

#endif
    /* Resolve the chord before either deferred GLO or OCT- can act. OCT-
     * is otherwise committed on release, or before a note/navigation. */
    if (!save_active && !locked_at_start &&
        (((buttons & (1u << BTN_GLO)) &&
          ((fm1_in.buttons & (1u << BTN_OCT_DOWN)) || br_ui.midi_oct_pending || br_ui.fx_snap_pending)) ||
         ((buttons & (1u << BTN_OCT_DOWN)) &&
          ((fm1_in.buttons & (1u << BTN_GLO)) || br_ui.glo_pending)))) {
        buttons &= ~((1u << BTN_GLO) | (1u << BTN_OCT_DOWN));
        br_ui.midi_oct_pending = 0u; br_ui.fx_snap_pending = br_ui.fx_snap_used = 0u;
        br_ui.glo_pending = br_ui.glo_used = br_ui.glo_snap = 0u;
        BR_FOREGROUND_MUTATE(br_open_midi_settings());
    } else if (!save_active && !locked_at_start && br_ui.page != BR_PAGE_FX &&
               (buttons & (1u << BTN_OCT_DOWN))) {
        br_ui.midi_oct_pending = 1u; buttons &= ~(1u << BTN_OCT_DOWN);
    }
    if (br_ui.midi_oct_pending && !save_active && !locked_at_start &&
        ((released & (1u << BTN_OCT_DOWN)) || down ||
         (buttons & (navigation_buttons & ~((1u << BTN_GLO) | (1u << BTN_OCT_DOWN)))))) {
        br_ui.midi_oct_pending = 0u;
        BR_FOREGROUND_MUTATE(br_buttons(1u << BTN_OCT_DOWN));
    }
    if (save_active || locked_at_start) br_ui.midi_oct_pending = 0u;
    if (save_active || !br_fx_snap_context()) br_ui.fx_snap_pending = br_ui.fx_snap_used = 0u;
    if (!save_active && !locked_at_start && br_ui.page == BR_PAGE_FX && (buttons & (1u << BTN_OCT_DOWN))) {
        br_ui.fx_snap_pending = 1u; br_ui.fx_snap_used = 0u;
        br_ui.fx_snap_screen = br_ui.screen[BR_PAGE_FX]; br_ui.fx_snap_part = br_ui.part;
        buttons &= ~(1u << BTN_OCT_DOWN);
    }
    /* GLO is a tap on release unless a SELECT gesture consumes the hold. */
    select_turn = fm1_enc_take(ENC_SELECT);
    if (buttons & (1u << BTN_GLO)) {
        if (!locked_at_start) {
            br_ui.glo_pending = 1u; br_ui.glo_used = 0u; br_ui.glo_start = fm1_ms;
            /* Pressed while already on the global filter page: turning the knob now snaps the filter to
             * its 0 %, middle and 100 % values. A hold that arrives here by turning does not. */
            br_ui.glo_snap = br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 1u;
        }
        buttons &= ~(1u << BTN_GLO);
    }
    if (br_ui.glo_pending && select_turn && !save_active && !locked_at_start && br_ui.page != BR_PAGE_SEQ) {
        br_ui.glo_used = 1u;
        br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 1u;
        br_ui.edit_key = 0xffffffffu; br_ui.redraw = 1u;
    }
    if (released & (1u << BTN_GLO)) {
        if (br_ui.glo_pending && !br_ui.glo_used && !locked_at_start) buttons |= 1u << BTN_GLO;
        br_ui.glo_pending = br_ui.glo_used = br_ui.glo_snap = 0u;
    }
    /* HOME and ARP navigate on release, so holding one for 0.8 seconds can
     * toggle HOLD or the arpeggiator without leaving the current page. */
    for (k = 0u; k < 2u; ++k) {
        const uint32_t bit = 1u << (k ? BTN_ARP : BTN_HOME);
        if (buttons & bit) {
            if (!locked_at_start) {
                br_ui.mode_pending |= bit; br_ui.mode_fired &= ~bit;
                br_ui.mode_start[k] = fm1_ms;
            }
            buttons &= ~bit;
        }
        if ((br_ui.mode_pending & bit) && !(br_ui.mode_fired & bit) &&
            (fm1_in.buttons & bit) && fm1_ms - br_ui.mode_start[k] >= 800u) {
            br_ui.mode_fired |= bit;
            BR_FOREGROUND_MUTATE(k ? br_arp_toggle() : br_hold_toggle());
        }
        if (released & bit) {
            if ((br_ui.mode_pending & bit) && !(br_ui.mode_fired & bit) && !locked_at_start)
                buttons |= bit;
            br_ui.mode_pending &= ~bit; br_ui.mode_fired &= ~bit;
        }
    }
    /* Hold gestures are invisible until you try them, so name the active one while its button is held.
     * The cue appears after a short delay so plain taps do not flash it. */
    {
        static const char *shown;
        const char *cue = 0;
        for (k = 0u; k < 2u; ++k) {
            const uint32_t bit = 1u << (k ? BTN_ARP : BTN_HOME);
            if ((br_ui.mode_pending & bit) && !(br_ui.mode_fired & bit) && fm1_ms - br_ui.mode_start[k] >= 400u)
                cue = k ? (br_ui.part == BR_DRUM_PART ? "RETRIGGER" : br_arp_on ? "HOLD 0.8S: ARP OFF" : "HOLD 0.8S: ARP ON") :
                          (br_hold_on ? "HOLD 0.8S: HOLD OFF" : "HOLD 0.8S: HOLD ON");
        }
        if (br_ui.glo_pending && !br_ui.glo_used && fm1_ms - br_ui.glo_start >= 400u)
            cue = br_ui.glo_snap ? "TURN: SNAP TO 0 50 100" : "TURN SELECT: FILTER";
        if (cue) {
            if (cue != shown) { br_message(cue); shown = cue; }
            br_ui.message_until = fm1_ms + 700u;
        } else if (shown) {
            /* Release only our own cue; a message raised meanwhile (HOLD ON) stays. */
            uint32_t same = 1u, i;
            for (i = 0u; shown[i] || br_ui.message[i]; ++i) if (shown[i] != br_ui.message[i]) { same = 0u; break; }
            if (same) { br_ui.message_until = fm1_ms; br_ui.redraw = 1u; }
            shown = 0;
        }
    }
    br_sel_housekeeping(buttons);
    /* This precedes project input: on device it otherwise defers REC until release. */
    if (buttons && locked_at_start && br_seq_grid_page() && br_step_press_pending() &&
        (keys & (1u << br_ui.step_press_key))) {
        /* Held step consumes REC/PLAY instead of changing transport. */
        if (buttons & (1u << BTN_REC)) BR_FOREGROUND_MUTATE(br_step_clipboard_action(0u));
        else if (buttons & (1u << BTN_PLAY)) BR_FOREGROUND_MUTATE(br_step_clipboard_action(1u));
        buttons &= ~((1u << BTN_REC) | (1u << BTN_PLAY));
    }
#if defined(DINGHY_EMULATOR_SAVE) || defined(FM1_DEVICE) || defined(DINGHY_DEVICE_SAVE_TEST)
    if (es_input(&buttons, &released, keys)) {
        if (buttons & (1u << BTN_PLAY)) BR_FOREGROUND_MUTATE(br_buttons(1u << BTN_PLAY));
        while (midi_get(&status, &d1, &d2)) BR_FOREGROUND_MUTATE(br_midi(status, d1, d2));
        br_ps_tick_transport();
        br_ps_tick_release();
        br_ps_tick_service();
#if (defined(FM1_DEVICE) || defined(DINGHY_DEVICE_SAVE_TEST)) && !defined(DINGHY_EMULATOR_SAVE)
        static uint32_t cue_was_active;
        uint32_t cue_active=(int32_t)(br_ui.message_until-fm1_ms)>0;
        es_scan_tick();
        if(cue_active!=cue_was_active){br_ui.redraw=1;cue_was_active=cue_active;}
        if (br_ui.redraw || (!cue_active && fm1_ms-br_ui.last_draw>=250u)) {
            es_draw();br_ui.redraw=0;br_ui.last_draw=fm1_ms;
        } else if (cue_active && fm1_ms-br_ui.last_draw>=32u) {
            br_draw_cue_bar(es.mode==1?2u:es.mode==3?10u:es.mode==4?9u:es.mode==5?5u:es.mode>=4?4u:3u,0u);br_ui.last_draw=fm1_ms;
        }
#endif
        return;
    }
#endif
    if (buttons) {
        if (locked_at_start && br_seq_grid_page()) {
            if (buttons & (1u << BTN_OCT_DOWN)) BR_FOREGROUND_MUTATE(br_step_micro_shift(-1));
            if (buttons & (1u << BTN_OCT_UP)) BR_FOREGROUND_MUTATE(br_step_micro_shift(1));
        }
        if (locked_at_start) buttons &= ~navigation_buttons;
        /* Preserve the physical button order around SAVE, but run its session
         * capture with audio muted instead of keeping IRQs masked for the copy. */
        {
            uint32_t before_save = buttons & ((1u << BTN_SAVE) - 1u);
            uint32_t save = buttons & (1u << BTN_SAVE);
            uint32_t after_save = buttons & ~((1u << (BTN_SAVE + 1u)) - 1u);
            if (before_save) BR_FOREGROUND_MUTATE(br_buttons(before_save));
            if (save) {
                br_save_button_action();
                if (br_engine_pipeline_uncontained_fault_get()) return;
            }
            if (after_save) BR_FOREGROUND_MUTATE(br_buttons(after_save));
        }
    }
    /* Release notes held across a transition to a non-playable page. */
    if (!br_page_plays_keys() && br_ui.page != BR_PAGE_PUNCH) {
        for (k = 0; k < 27u; ++k)
            if (br_ui.held_note[k]) BR_FOREGROUND_MUTATE(br_key_up(k));
    }
    seq_grid = br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ];
    br_encoder_sync();
    turn = select_turn;
    if (!locked_at_start && turn != 0) {
        if (br_retrig_page()) {
            if (!br_retrig_editing()) { br_ui.lane = (uint32_t)br_clip((int32_t)br_ui.lane + turn, 0, BR_DRUM_LANES - 1); br_ui.redraw = 1u; }
        } else if (seq_grid && br_is_kit(br_part[br_ui.part].sound.preset)) {
            BR_FOREGROUND_MUTATE(br_gate_select_lane((uint32_t)br_clip((int32_t)br_ui.lane + turn, 0, BR_DRUM_LANES - 1)));
        } else if (br_sel_browse()) {
            BR_FOREGROUND_MUTATE(br_sel_move(br_clip(turn, -4, 4)));
        } else {
            int32_t adjusted;
            if (br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 1u && br_ui.glo_pending && br_ui.glo_snap) {
                br_ui.glo_used = 1u; adjusted = br_filter_snap_delta(turn);
            } else adjusted = br_encoder_accel_step(&br_encoder_speed[4], turn, fm1_ms);
            BR_FOREGROUND_MUTATE(br_select_turn(adjusted));
        }
    }
    turn = fm1_enc_take(ENC_ALGORITHM);
    if (!locked_at_start && turn != 0) {
        BR_FOREGROUND_MUTATE(br_algorithm_encoder_turn(turn));
    }
    turn = fm1_enc_take(ENC_PRESETS);
    if (!locked_at_start && turn != 0) {
        BR_FOREGROUND_MUTATE(br_preset_encoder_turn(turn));
    }

    /* Establish the step target before controls are sampled. Kit lane keys
     * sort ahead of white trigger keys; tonal black keys set insertion pitch
     * before a simultaneous white-key tap uses it. */
    if (seq_grid && locked_at_start) {
        /* While a step is pending/editing, additional keyboard presses must
         * not audition or record a second target behind the held-step lock. */
        handled_down |= down & ((1u << 27) - 1u);
    } else if (seq_grid) {
        for (uint32_t pass = 0u; pass < 2u; ++pass) {
            for (k = 0; k < 27u; ++k) {
                uint32_t bit = 1u << k;
                if (!(down & bit) || (uint32_t)br_black_key(k) != (1u - pass)) continue;
                BR_FOREGROUND_MUTATE(br_key_down(k, (keys & bit) != 0u));
                handled_down |= bit;
            }
        }
    }
    if (br_step_press_pending() && br_ui.step_press_active &&
        (fm1_in.buttons & (1u << BTN_SEQ))) {
        if (!br_ui.step_edit_active) BR_FOREGROUND_MUTATE(br_step_edit_begin());
        br_fill_active = 1u;
    } else br_fill_active = 0u;
    if (br_step_press_pending() || br_ui.step_edit_active)
        BR_FOREGROUND_MUTATE(br_step_press_update(keys));
    if ((released & (1u << BTN_SEQ)) && br_ui.seq_pressed)
        BR_FOREGROUND_MUTATE(br_seq_release());

    if (br_retrig_page()) {
        for (uint32_t pass = 0u; pass < 2u; ++pass) for (k = 0u; k < 27u; ++k) {
            uint32_t bit = 1u << k;
            if ((down & bit) && (uint32_t)br_black_key(k) == (1u - pass)) {
                BR_FOREGROUND_MUTATE(br_key_down(k, (keys & bit) != 0u)); handled_down |= bit;
            }
        }
    }
    if (!br_fx_snap_context()) br_ui.fx_snap_pending = br_ui.fx_snap_used = 0u;
    br_encoder_sync(); /* Track/preset edits above can change the knob target. */
    for (k = 0; k < 4u; k++) if ((turn = fm1_enc_take(ENC_KNOB1 + k)) != 0) {
        if (br_step_press_pending()) {
            BR_FOREGROUND_MUTATE(br_step_edit_begin());
        }
        int32_t adjusted;
        if (!k && br_ui.page == BR_PAGE_MIX && br_ui.screen[BR_PAGE_MIX] == 1u && br_ui.glo_pending && br_ui.glo_snap) {
            br_ui.glo_used = 1u; adjusted = br_filter_snap_delta(turn);
        } else if (br_fx_snap_knob(k) && br_ui.fx_snap_pending) {
            br_ui.fx_snap_used = 1u;
            adjusted = br_snap_delta(br_control(k).value, turn);
        } else adjusted = !k && (fm1_in.buttons & (1u << BTN_SEQ)) ? turn : br_control_input(k, turn);
        if (!br_ui.step_edit_active && !k && (fm1_in.buttons & (1u << BTN_SEQ)))
            BR_FOREGROUND_MUTATE(br_length_turn(adjusted));
        else if (br_control_turn_restores_session(k)) {
            if (!br_quiescent_transaction_begin()) return;
            br_control_turn(k, adjusted);
            br_quiescent_transaction_end();
            if (br_engine_pipeline_uncontained_fault_get()) return;
        }
        else BR_FOREGROUND_MUTATE(br_control_turn(k, adjusted));
    }
    /* A note played before release commits an ordinary octave gesture first. */
    if (br_ui.fx_snap_pending && !br_ui.fx_snap_used && br_fx_snap_context() && !locked_at_start &&
        (down & ~(1u << BR_PUNCH_SHIFT_KEY))) {
        BR_FOREGROUND_MUTATE(br_buttons(1u << BTN_OCT_DOWN));
        br_ui.fx_snap_used = 1u;
    }
    if (released & (1u << BTN_OCT_DOWN)) {
        if (br_ui.fx_snap_pending && !br_ui.fx_snap_used && br_fx_snap_context() && !locked_at_start)
            BR_FOREGROUND_MUTATE(br_buttons(1u << BTN_OCT_DOWN));
        br_ui.fx_snap_pending = br_ui.fx_snap_used = 0u;
    }
    /* A simultaneous lane/step press edits the newly selected lane, regardless
     * of where those physical keys appear in the keyboard scan. */
    uint32_t drum_grid = (br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ]) && br_is_kit(br_part[br_ui.part].sound.preset);
    if (drum_grid) for (k = 0; k < 27u; k++)
        if (((down >> k) & 1u) && !(handled_down & (1u << k)) && br_black_key(k))
            BR_FOREGROUND_MUTATE(br_key_down(k, (keys >> k) & 1u));
    for (k = 0; k < 27u; k++) {
        if (((down >> k) & 1u) && !(handled_down & (1u << k)) && (!drum_grid || !br_black_key(k)))
        {
            BR_FOREGROUND_MUTATE(br_key_down(k, (keys >> k) & 1u));
#if defined(BR_PATTERN_SONG_EXPERIMENT) && defined(BR_PATTERN_OWNER_HOST)
            BR_PS_SERVICE_SELECTION();
#endif
        }
        if ((up >> k) & 1u) {
            BR_FOREGROUND_MUTATE(br_key_up(k));
#if defined(BR_PATTERN_SONG_EXPERIMENT) && defined(BR_PATTERN_OWNER_HOST)
            BR_PS_SERVICE_SELECTION();
#endif
        }
    }
    while (midi_get(&status, &d1, &d2))
        BR_FOREGROUND_MUTATE(br_midi(status, d1, d2));
    BR_FOREGROUND_MUTATE(br_arp_record_drain());
    br_ps_tick_transport();
    br_ps_tick_release();
    br_ps_tick_service();
    br_save_poll();
    br_sel_housekeeping(0u);
#if defined(BR_PATTERN_SONG_EXPERIMENT) && defined(BR_PATTERN_OWNER_HOST)
    br_ps_service();
#endif
    {   /* LED levels change at human speed: recompute them at most once a millisecond. */
        static uint32_t leds_ms = 0xffffffffu;
        if (fm1_ms != leds_ms) { leds_ms = fm1_ms; br_leds(); }
    }
    if (br_ui.redraw || fm1_ms - br_ui.last_draw >= 32u) {
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
        /* A screen draw can hold the loop for milliseconds; hand the audio scheduler
         * any plan it is waiting on first. */
        br_audio_fixed8_foreground_service();
#endif
        br_draw(); br_ui.last_draw = fm1_ms;
    } else if (br_mon.on && fm1_ms - br_mon.at >= 100u) {
        br_monitor_overlay();
    }
}

static void br_app_tick(void)
{
    if (br_engine_pipeline_uncontained_fault_get() || br_diagnostic_restore_in_progress) {
#if defined(BR_PATTERN_SONG_EXPERIMENT) && defined(BR_PATTERN_OWNER_HOST) && \
    defined(FM1_PATTERN_TICK_CLAIM_PROTOTYPE) && FM1_PATTERN_TICK_CLAIM_PROTOTYPE
        br_ps_selection_pending=0u;
#endif
#if defined(FM1_PATTERN_PANEL_TEST) && FM1_PATTERN_PANEL_TEST
        if (br_pattern_diag_key) { br_ui.ps_held_used=1u; br_pattern_diag_key=0u; }
#endif
        return;
    }
#if defined(FM1_PATTERN_TICK_CLAIM_PROTOTYPE) && FM1_PATTERN_TICK_CLAIM_PROTOTYPE
    /* Retire completed pattern swaps before processing more input. */
    br_ps_tick_service();
    uint32_t irq_state=br_foreground_irq_save();
    int claimed=br_pattern_irq_foreground_claim();
    br_foreground_irq_restore(irq_state);
    if (!claimed) {
        if (!br_ps_tick_service()) br_message("PATTERN SWITCH BUSY");
        return;
    }
    br_ps_tick_claimed=1u; br_ps_tick_start=fm1_ms;
    br_ps_tick_late_start=br_pattern_irq_late_count();
#endif
    br_app_tick_inner();
    /* Also releases on every early return from the input path. */
    br_ps_tick_release();
    br_ps_tick_service();
}

#if defined(FM1_DEVICE) && defined(FM1_PATTERN_PROJECT_TARGET_EXPERIMENT) && FM1_PATTERN_PROJECT_TARGET_EXPERIMENT
#include "pattern_boot_recovery.h"
#endif
static void app_main(void)
{
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_PROJECT_TARGET_EXPERIMENT) && FM1_PATTERN_PROJECT_TARGET_EXPERIMENT
    static br_pattern_boot_recovery project_recovery;
    static uint32_t project_lock_reported;
#endif
    for (;;) {
        if (!br_ready) br_app_init();
        platform_poll();
#if defined(FM1_DEVICE) && defined(FM1_PATTERN_PROJECT_TARGET_EXPERIMENT) && FM1_PATTERN_PROJECT_TARGET_EXPERIMENT
        /* Startup init already owns quiescence; recover only after it ends
         * and a proven backend policy explicitly reports readiness. */
        if(br_ready && !project_recovery.done && !br_ps_external_restore_completed) {
            int ready=br_pp_target_ready();
            if(br_pattern_boot_should_try(&project_recovery,ready,fm1_ms)) {
                int rc=br_project_target_recover();
                int status=br_pattern_boot_result(&project_recovery,fm1_ms,rc,br_ps_target_project_faulted());
                if(status==-1) { es_project_boot_state=2u; br_message("PROJECT RECOVERY NEEDED"); }
                else if(status==-2) { es_project_boot_state=2u; br_message("PROJECT RECOVERY FAILED"); }
            } else if(!ready && !project_lock_reported) {
                project_lock_reported=1; br_message("PROJECT STORE LOCKED");
            }
        }
#endif
        if (br_ready) br_app_tick();
#if BR_AUDIO_FOREGROUND_FIXED8_PLAN
        if (br_ready) br_audio_fixed8_foreground_service();
#endif
    }
}
