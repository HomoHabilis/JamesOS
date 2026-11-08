/* Host-only experimental integration through the actual sequencer render. */
#define DINGHY_HOST 1
#define BR_PATTERN_TEST_IRQ_TRACK 1
#define DINGHY_DEVICE_SAVE_TEST 1
#define BR_PATTERN_SONG_EXPERIMENT 1
#define BR_PATTERN_OWNER_HOST 1
#define BR_PATTERN_BANK_EXPERIMENT 1
#define BR_PATTERN_SNAPSHOT_MONOLITHIC_TEST 1
#define FM1_PATTERN_TICK_CLAIM_PROTOTYPE 1
#define BR_MIDI_CLOCK_EXPERIMENT 1
#define FM1_PATTERN_AUDIO_IRQ_PROTOTYPE 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

static br_pattern_snapshot slots[3];
static br_pattern_irq_publication pub;
static void render(void)
{
    int32_t out[BR_BLOCK * 2u] = {0};
    assert(app_audio(out, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    assert(!br_pattern_host_error);
}
static void fill(br_pattern_snapshot *s, unsigned note)
{
    memset(s, 0, sizeof *s);
    for (unsigned p = 0; p < BR_PARTS; ++p) {
        s->length[p] = p + 1u; s->division[p] = 1u;
        if (p < BR_SYNTH_PARTS) {
            s->step[p][0] = BR_NOTE_STEP(note + p, 100u);
            s->synth_velocity[p][0] = 255u;
            s->synth_length[p][0] = 16u;
        }
    }
    s->step[BR_DRUM_PART][0] = 1u;
    s->kit_velocity[0][0] = 100u;
    s->kit_length[0][0] = 16u;
    s->kit_pitch[0][0] = 60u;
}
static void apply_globals(const br_pattern_snapshot *s)
{
    for (unsigned p = 0; p < BR_PARTS; ++p) {
        br_part[p].length = s->length[p]; br_part[p].division = s->division[p];
        for (unsigned i = 0; i < BR_STEPS; ++i) {
            br_part[p].step[i] = s->step[p][i];
            if (p < BR_SYNTH_PARTS) {
                br_seq_synth_length[p][i] = s->synth_length[p][i];
                br_seq_synth_velocity[p][i] = s->synth_velocity[p][i];
                br_seq_synth_condition[p][i] = s->synth_condition[p][i];
                br_seq_synth_micro[p][i] = s->synth_micro[p][i];
                br_seq_synth_note[p][i] = s->synth_note[p][i];
            }
        }
    }
    for (unsigned i = 0; i < BR_STEPS; ++i) for (unsigned l = 0; l < BR_DRUM_LANES; ++l) {
        br_seq_kit_length[i][l] = s->kit_length[i][l];
        br_seq_kit_velocity[i][l] = s->kit_velocity[i][l];
        br_seq_kit_condition[i][l] = s->kit_condition[i][l];
        br_seq_kit_micro[i][l] = s->kit_micro[i][l];
        br_seq_kit_note[i][l] = s->kit_note[i][l];
        br_seq_kit_pitch[i][l] = s->kit_pitch[i][l];
        br_seq_kit_retrig[i][l] = s->kit_retrig[i][l];
    }
}
static void finish_handoff(void)
{
    assert(br_pattern_irq_edits_blocked());
    apply_globals(br_pattern_host_snapshot);
    assert(br_pattern_irq_globals_ready());
    render();
    assert(!br_pattern_irq_edits_blocked() && !br_pattern_host_snapshot);
}
static void setup(void)
{
    br_pattern_host_publication = NULL; br_pattern_host_snapshot = NULL;
    br_pattern_host_retired = BR_PATTERN_IRQ_NO_SLOT;
    br_app_init();
    for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p) br_load_preset(p, BR_FIRST_PLAITS);
    br_load_preset(BR_DRUM_PART, BR_KIT_PLAITS);
    fill(&slots[0], 50u); fill(&slots[1], 60u); fill(&slots[2], 70u);
    assert(br_pattern_irq_init(&pub, &slots[0], &slots[1], &slots[2]));
    apply_globals(&slots[0]);
    assert(br_pattern_irq_audio_bind(&pub));
}
static void submit(unsigned slot)
{
    unsigned claimed = br_pattern_irq_claim(&pub);
    assert(claimed == slot); assert(br_pattern_irq_submit(&pub, claimed));
}
static void force_internal_bar(void)
{
    uint32_t q = br_tempo_quarter_frames();
    assert(q > BR_BLOCK);
    br_global_beat = 3u; br_metro_frames = q - BR_BLOCK / 2u;
    render(); if(!br_pattern_host_due) fprintf(stderr,"due missing: playing=%u beat=%u metro=%u err=%u queued=%u bar=%u\n",br_playing,br_global_beat,br_metro_frames,br_pattern_host_error,br_ps_state.queued,br_pattern_host_bars); assert(br_pattern_host_due);
    render(); assert(!br_pattern_host_due);
}

#undef br_ps_select
#undef br_ps_copy_pattern
#undef br_ps_paste_pattern
#undef br_ps_set_entry
#undef br_ps_set_length
#undef br_ps_enable_song
#define BR_SPLIT_AUDIO 1
static int inject_queue_interrupts, capture_interrupts, decode_interrupts;
#define br_pattern_snapshot_capture capture_actual
#include "../firmware/src/pattern_snapshot.c"
#undef br_pattern_snapshot_capture
int br_pattern_snapshot_capture(br_pattern_snapshot *s)
{
    if (inject_queue_interrupts) {
        assert(br_pattern_irq_foreground_busy && br_test_irq_depth == 0u);
        ++capture_interrupts; render();
        assert(!br_pattern_irq_edits_blocked());
    }
    return capture_actual(s);
}
#include "../firmware/src/pattern_storage_codec.c"
#define br_pattern_bank_prepare prepare_actual
#include "../firmware/src/pattern_bank.c"
#undef br_pattern_bank_prepare
int br_pattern_bank_prepare(br_pattern_bank *b, unsigned target,
    const br_pattern_snapshot *live, br_pattern_snapshot *work, size_t capacity)
{
    if (inject_queue_interrupts) {
        assert(br_pattern_irq_foreground_busy && br_test_irq_depth == 0u);
        ++decode_interrupts; render();
        assert(!br_pattern_irq_edits_blocked());
    }
    return prepare_actual(b,target,live,work,capacity);
}
#include "../firmware/src/pattern_owner.c"
#include "../firmware/src/pattern_song.c"
static uint8_t owner_arena[20000], owner_codec[9000];
static br_pattern_snapshot owner_live,owner_work,owner_clip;
static int owner_quiet(void *ctx) { (void)ctx; return !br_playing && br_diagnostic_restore_in_progress; }
static void ui_tick(void) { ++fm1_ms; br_app_tick(); }
static void note_down(unsigned k) { fm1_in.notes|=1u<<k; host_note_pressed|=1u<<k; ui_tick(); }
static void note_up(unsigned k) { fm1_in.notes&=~(1u<<k); ui_tick(); }
static void button_down(unsigned b) { fm1_in.buttons|=1u<<b; host_pressed|=1u<<b; ui_tick(); }
static void button_up(unsigned b) { fm1_in.buttons&=~(1u<<b); host_released|=1u<<b; ui_tick(); }
static void ui_select(unsigned key) { br_ui.page=BR_PAGE_PS; br_ui.screen[BR_PAGE_PS]=0; note_down(key); note_up(key); br_ui.page=BR_PAGE_SEQ; }
int main(void) {
    br_app_init(); fm1_ms=2000;
    br_load_preset(0,BR_FIRST_PLAITS);
    br_ui.part=0; br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0;
    br_part[0].length=16; br_part[0].division=1;
    br_part[0].step[0]=BR_NOTE_STEP(50,100); br_seq_synth_velocity[0][0]=255;
    br_seq_synth_micro[0][0]=-4; br_seq_synth_micro[0][1]=-3;
    assert(br_quiescent_transaction_begin());
    assert(br_ps_host_init(owner_arena,sizeof owner_arena,&owner_live,&owner_work,&owner_clip,owner_codec,sizeof owner_codec,owner_quiet,0));
    assert(br_ps_runtime_bind(&pub,&slots[0],&slots[1],&slots[2]));
    br_quiescent_transaction_end();
    /* Even an abnormal audio-owned gate must block input before edge reads. */
    fm1_in.notes|=1u; host_note_pressed|=1u; br_pattern_irq_gate=1u;
    ui_tick(); assert((host_note_pressed&1u) && !br_ps_tick_claimed);
    br_pattern_irq_gate=0u;fm1_in.notes&=~1u;host_note_pressed&=~1u;
    br_playing=1; render(); assert(br_seq_note[0]==50);
    inject_queue_interrupts=1;
    ui_select(3); assert(br_ps_state.queued==1);
    inject_queue_interrupts=0;
    assert(capture_interrupts == 1 && decode_interrupts == 1);
    ui_select(5); assert(br_ps_state.active==0 && br_ps_state.queued==1);
    /* Actual UI tap edits outgoing pattern after target preparation. */
    note_down(2); note_up(2); assert(br_step_active(0,1,0,0));
    /* An old-pattern delayed micro-event may not leak across the switch. */
    br_micro_schedule(1u, 0u, 0u, BR_BLOCK * 10u, 0u);
    force_internal_bar(); assert(br_pattern_irq_edits_blocked());
    for (unsigned i=0; i<BR_SEQ_EVENT_SLOTS; ++i) assert(!br_micro_event[1][i].active);
    /* The target's negative first-step offset clamps to its new start. */
    assert(br_seq_note[0]==50);
    ui_tick(); assert(!br_step_active(0,1,0,0)); assert(br_seq_synth_micro[0][1]==-3);
    render(); ui_tick(); assert(br_ps_state.active==1 && !br_pattern_irq_edits_blocked());
    /* Change active pattern through UI and return to the first pattern. */
    note_down(4); note_up(4); assert(br_step_active(0,2,0,0));
    br_seq_synth_micro[0][1]=5;
    ui_select(1); assert(br_ps_state.queued==0);
    force_internal_bar(); ui_tick(); render(); ui_tick();
    assert(br_ps_state.active==0 && br_step_active(0,1,0,0) && !br_step_active(0,2,0,0));
    assert(br_seq_synth_micro[0][1]==-3);
    assert(br_seq_note[0]==50);
    /* Fault after audio accepts the switch: retain queued UI input and finish
     * gate/ack without overwriting outgoing pattern data. */
    ui_select(3); assert(br_ps_state.queued==1);
    force_internal_bar(); assert(br_pattern_irq_edits_blocked());
    br_pattern_host_error=1;
    fm1_in.notes|=1u<<2; host_note_pressed|=1u<<2; ui_tick();
    assert(!br_playing && br_pattern_irq_edits_blocked());
    assert(host_note_pressed & (1u<<2));
    { int32_t out[BR_BLOCK*2u]={0}; assert(app_audio(out,BR_BLOCK)==FM1_AUDIO_RENDER_OK); }
    assert(!br_pattern_irq_edits_blocked());
    ui_tick(); fm1_in.notes&=~(1u<<2); ui_tick();
    assert(br_ps_state.active==1 && br_step_active(0,1,0,0));
    assert(br_quiescent_transaction_begin());
    assert(br_ps_runtime_reset_stopped());
    assert(br_ps_select(0)); assert(br_step_active(0,1,0,0));
    br_quiescent_transaction_end();
    /* Exercise note and pattern clipboard gestures on the real UI. */
    br_ui.page=BR_PAGE_SEQ; br_ui.screen[BR_PAGE_SEQ]=0;
    note_down(0); button_down(BTN_REC); button_up(BTN_REC); note_up(0);
    note_down(6); button_down(BTN_PLAY); button_up(BTN_PLAY); note_up(6);
    assert(br_part[0].step[3]==br_part[0].step[0]);
    br_ui.page=BR_PAGE_PS; br_ui.screen[BR_PAGE_PS]=0;
    { static const unsigned black[] = {1,3,5,8,10,13,15,17,20,22,25};
      unsigned saved_used=br_ps_state.used, saved_active=br_ps_state.active;
      br_ps_state.used=3u; br_ps_state.active=0u;
      for(unsigned phase=0;phase<2;phase++) {
        fm1_ms=2000+250*phase; br_leds();
        for(unsigned k=0;k<27;k++) {
          int index=-1; for(unsigned j=0;j<11;j++) if(black[j]==k)index=(int)j;
          fm1_led_level_t want=index<0 ? FM1_LED_OFF : index==0 ?
            (phase ? FM1_LED_BRIGHT : FM1_LED_MID) : index==1 ? FM1_LED_BRIGHT : FM1_LED_DIM;
          assert(host_led_level_state[KEY_LED(k)]==want);
        }
      }
      br_ps_state.used=saved_used; br_ps_state.active=saved_active;
    }
    note_down(3); button_down(BTN_REC); button_up(BTN_REC); note_up(3);
    note_down(5); button_down(BTN_PLAY); button_up(BTN_PLAY); note_up(5);
    assert(br_ps_state.used & (1u<<2));
    assert(br_quiescent_transaction_begin());
    assert(!br_pattern_snapshot_capture(&owner_live));
    assert(br_pattern_bank_copy(&bank,1,&owner_live,&slots[1])==BR_BANK_OK);
    assert(br_pattern_bank_copy(&bank,2,&owner_live,&slots[2])==BR_BANK_OK);
    assert(!memcmp(&slots[1],&slots[2],sizeof slots[1]));
    br_quiescent_transaction_end();
    /* Stop before the boundary cancels an unseen manual switch. Continue
     * resumes the same active pattern; it must not resurrect the request. */
    br_ui.page=BR_PAGE_SEQ; br_playing=1; render(); ui_tick();
    ui_select(3); assert(br_ps_state.queued==1);
    mq[mq_w%64u][0]=0xfcu; mq[mq_w%64u][1]=mq[mq_w%64u][2]=0; ++mq_w; ui_tick();
    render(); ui_tick();
    assert(!br_playing && br_ps_state.active==0 && br_ps_state.queued==FM1_PATTERN_NONE);
    assert(!br_pattern_irq_edits_blocked());
    mq[mq_w%64u][0]=0xfbu; mq[mq_w%64u][1]=mq[mq_w%64u][2]=0; ++mq_w; ui_tick();
    render(); ui_tick(); assert(br_playing && br_ps_state.active==0);
    force_internal_bar(); ui_tick(); render(); ui_tick();
    assert(br_ps_state.active==0 && br_ps_state.queued==FM1_PATTERN_NONE);
    /* The selected target retains its own microtiming after a switch. */
    ui_select(3); assert(br_ps_state.queued==1);
    force_internal_bar(); ui_tick(); render(); ui_tick();
    assert(br_ps_state.active==1 && br_seq_synth_micro[0][1]==5);
    /* Force the exact reserve limit while retaining a valid bank. A UI
     * selection must fail without changing live content, bank or mailbox. */
    { size_t cap=bank.capacity; br_pattern_snapshot before; br_pattern_bank before_bank;
      static uint8_t before_arena[sizeof owner_arena];
      /* The selected stored target becomes live, so its encoded bytes are
       * safely reclaimable at commit. Test one byte below that true bound. */
      size_t base=bank.size-bank.length[bank.active]-bank.length[0];
      assert(base+4334u<cap); bank.capacity=base+4334u;
      before_bank=bank; memcpy(before_arena,owner_arena,sizeof owner_arena);
      assert(!br_pattern_snapshot_capture(&before));
      ui_select(1);
      assert(br_ps_state.active==1 && br_ps_state.queued==FM1_PATTERN_NONE);
      assert(!br_pattern_irq_edits_blocked() && !pub.outstanding && !bank.reserved);
      assert(!br_pattern_snapshot_capture(&owner_live));
      assert(!memcmp(&before,&owner_live,sizeof before));
      assert(!memcmp(&before_bank,&bank,sizeof bank));
      assert(!memcmp(before_arena,owner_arena,sizeof owner_arena)); bank.capacity=cap;
    }
    /* Stop received while a consumed switch owns the gate waits for the
     * handoff, then stops on the newly active pattern without a torn edit. */
    ui_select(1); assert(br_ps_state.queued==0);
    force_internal_bar(); assert(br_pattern_irq_edits_blocked());
    mq[mq_w%64u][0]=0xfcu; mq[mq_w%64u][1]=mq[mq_w%64u][2]=0; ++mq_w;
    ui_tick(); render(); ui_tick(); render(); ui_tick();
    assert(!br_playing && !br_pattern_irq_edits_blocked());
    assert(br_ps_state.active==0 && br_ps_state.queued==FM1_PATTERN_NONE);
    assert(br_step_active(0,1,0,0) && br_seq_synth_micro[0][1]==-3);
    /* Idle input ticks do not acquire a handoff claim. A selection acquires
     * one lazily, and an interrupt at the boundary must defer until release. */
    br_playing=1; render(); ui_tick();
    assert(!br_ps_tick_claimed && !br_pattern_irq_foreground_busy);
    assert(br_pattern_irq_foreground_claim()); br_ps_tick_claimed=1u;
    assert(br_ps_ui_select(1u));
    assert(br_ps_tick_claimed && br_pattern_irq_foreground_busy && br_ps_state.queued==1u);
    {uint32_t q=br_tempo_quarter_frames(), late=br_pattern_irq_late_count();
     br_global_beat=3u;br_metro_frames=q-BR_BLOCK/2u;
     render();assert(br_pattern_host_due);
     render();assert(br_pattern_host_due && !br_pattern_irq_edits_blocked());
     assert(br_pattern_irq_late_count()>late);}
    br_ps_tick_release(); render(); assert(br_pattern_irq_edits_blocked());
    ui_tick(); render(); ui_tick();
    assert(br_ps_state.active==1u && br_ps_state.queued==FM1_PATTERN_NONE && !br_pattern_irq_edits_blocked());
    assert(!br_ps_tick_claimed && !br_pattern_irq_foreground_busy);
    /* Key release precedes a simultaneous MIDI Stop in the input loop: the
     * switch is queued, then the ordinary stopped cancellation handles it. */
    br_ui.page=BR_PAGE_PS; note_down(1);
    mq[mq_w%64u][0]=0xfcu; mq[mq_w%64u][1]=mq[mq_w%64u][2]=0; ++mq_w;
    note_up(1); assert(br_ps_state.queued==0u);
    render(); ui_tick(); render(); ui_tick();
    assert(!br_playing && br_ps_state.active==1u && br_ps_state.queued==FM1_PATTERN_NONE);
    mq[mq_w%64u][0]=0xfbu; mq[mq_w%64u][1]=mq[mq_w%64u][2]=0; ++mq_w;
    ui_tick(); render(); ui_tick(); assert(br_playing && br_ps_state.active==1u);
    /* An interrupted handler must not replay a stale selection after recovery. */
    assert(br_ps_defer_selection(0u));
    br_diagnostic_restore_in_progress=1u; ui_tick();
    assert(!br_ps_selection_pending); br_diagnostic_restore_in_progress=0u;
    ui_tick(); assert(br_ps_state.active==1u && br_ps_state.queued==FM1_PATTERN_NONE);
    assert(br_ps_defer_selection(0u)); br_playing=0u; br_ps_service_selection();
    assert(br_ps_state.active==1u && br_ps_state.queued==FM1_PATTERN_NONE && !br_ps_selection_pending);
    puts("manual UI, owner and audio pattern integration passed"); return 0;
}
