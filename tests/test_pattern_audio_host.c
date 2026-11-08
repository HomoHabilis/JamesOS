/* Host-only experimental integration through the actual sequencer render. */
#define DINGHY_HOST 1
#define BR_MIDI_CLOCK_EXPERIMENT 1
#define FM1_PATTERN_AUDIO_IRQ_PROTOTYPE 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>

static br_pattern_snapshot slots[3], staged;
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
    assert(br_pattern_irq_init(&pub, &staged, NULL, NULL));
    apply_globals(&slots[0]);
    assert(br_pattern_irq_audio_bind(&pub));
}
static void submit(unsigned slot)
{
    unsigned claimed = br_pattern_irq_claim(&pub);
    assert(claimed == 0u); staged = slots[slot];
    assert(br_pattern_irq_submit(&pub, claimed));
}
static void force_internal_bar(void)
{
    uint32_t q = br_tempo_quarter_frames();
    assert(q > BR_BLOCK);
    br_global_beat = 3u; br_metro_frames = q - BR_BLOCK / 2u;
    render(); assert(br_pattern_host_due);
    render(); assert(!br_pattern_host_due);
}
int main(void)
{
    setup();
    /* A latched error cannot be reset while running, paused, or while an
     * unconsumed request still owns a slot. Recovery preserves globals. */
    br_pattern_host_error = 1u;
    br_playing = 1u; assert(!br_pattern_irq_audio_reset(&pub)); br_playing = 0u;
    br_midi_paused = 1u; assert(!br_pattern_irq_audio_reset(&pub)); br_midi_paused = 0u;
    submit(1); assert(!br_pattern_irq_audio_reset(&pub));
    assert(br_pattern_irq_cancel_unseen(&pub));
    assert(br_pattern_irq_audio_reset(&pub));
    assert(!br_pattern_irq_error() && BR_STEP_NOTE(br_part[0].step[0]) == 50u);
    /* First note of the target has a negative offset. Since the target was
     * not active before the switch, it must play at the transition. */
    slots[1].synth_micro[0][0] = -4;
    br_bpm = 60u; br_playing = 1u; render();
    /* Large tempo changes retain fractional beat phase in both directions. */
    { uint32_t q = br_tempo_quarter_frames();
      br_global_beat = 1u; br_metro_frames = q / 2u;
      br_bpm = 240u; render(); q = br_tempo_quarter_frames();
      assert(br_global_beat == 1u && br_metro_frames >= q / 2u && br_metro_frames <= q / 2u + BR_BLOCK + 1u);
      br_metro_frames = q / 2u; br_bpm = 60u; render(); q = br_tempo_quarter_frames();
      assert(br_global_beat == 1u && br_metro_frames >= q / 2u && br_metro_frames <= q / 2u + BR_BLOCK + 1u); }
    for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p) assert(br_seq_note[p] == 50u + p);
    assert(br_seq_lane_gate_frames[BR_DRUM_PART][0]);
    /* Pending old-pattern micro events and retriggers must not be interpreted
     * against the new pattern. Keep both in flight over the boundary. */
    br_micro_schedule(1u, 0u, 0u, BR_BLOCK * 10u, 0u);
    br_seq_kit_retrig[0][0] = 2u;
    br_retrig[0].setting = 2u; br_retrig[0].step = 0u;
    br_retrig[0].count = 1u; br_retrig[0].remaining = BR_BLOCK * 10u;
    submit(1); force_internal_bar();
    for (unsigned i = 0; i < BR_SEQ_EVENT_SLOTS; ++i) assert(!br_micro_event[1][i].active);
    assert(!br_retrig[0].count);
    assert(br_pattern_host_snapshot == &staged && br_pattern_host_bars == 1u);
    assert(!br_pattern_irq_audio_reset(&pub));
    for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p) assert(br_seq_note[p] == 60u + p);
    assert(br_seq_lane_gate_frames[BR_DRUM_PART][0]);
    finish_handoff();
    /* A second queued change verifies recycling and differing lengths. */
    submit(0);
    assert(br_pattern_irq_foreground_claim());
    { uint32_t q = br_tempo_quarter_frames();
      br_global_beat = 3u; br_metro_frames = q - BR_BLOCK / 2u;
      render(); render();
      assert(br_pattern_host_snapshot == NULL && br_pattern_host_due);
      assert(br_pattern_irq_late_count() == 1u);
      br_pattern_irq_foreground_release(); render(); }
    assert(br_pattern_host_snapshot == &staged && br_pattern_host_bars == 2u);
    for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p) assert(br_seq_note[p] == 50u + p);
    br_playing = 0u; /* A stop during the gate must still allow acknowledgement. */
    finish_handoff();

    /* A tied note of the same pitch is sustained across a pattern switch;
     * the new step's different velocity must not retrigger the envelope. */
    setup();
    br_part[0].step[0] |= BR_STEP_TIE;
    slots[1].step[0][0] = BR_NOTE_STEP(50u, 30u);
    br_playing = 1u; render();
    assert(br_seq_note[0] == 50u && br_seq_tie[0] && br_env[0].velocity == 100u);
    submit(1); force_internal_bar();
    assert(br_seq_note[0] == 50u && br_env[0].velocity == 100u);
    finish_handoff();

    /* External clock's bar is measured in pulses from Start, independent of
     * per-track lengths; the switch occurs in the first following block. */
    setup();
    memset(&br_clock_experiment, 0, sizeof br_clock_experiment);
    br_clock_experiment.enabled = 1u;
    br_clock_experiment.period_q16 = (uint64_t)1000u << 16;
    br_engine_midi_transport(0xFAu); render();
    submit(1);
    /* Stop/Continue preserves the bar deadline and queued pattern. */
    { uint64_t deadline = br_pattern_host_clock.next_q16;
      br_midi_apply_transport(0xFCu); assert(!br_playing);
      render(); assert(br_pattern_host_clock.next_q16 == deadline);
      br_midi_apply_transport(0xFBu); assert(br_playing);
      assert(br_pattern_host_clock.next_q16 == deadline); }
    br_clock_experiment.phase_q16 = FM1_PATTERN_BAR_PULSES_Q16 - 1u;
    render(); assert(br_pattern_host_due && br_pattern_host_snapshot == NULL);
    br_clock_experiment.phase_q16 = FM1_PATTERN_BAR_PULSES_Q16 + 100u;
    render();
    assert(br_pattern_host_snapshot == &staged && br_pattern_host_bars == 1u);
    for (unsigned p = 0; p < BR_SYNTH_PARTS; ++p) assert(br_seq_note[p] == 60u + p);
    finish_handoff();
    /* Start before the first pulse must wait for acquisition, then anchor the
     * bar clock and honor a request made after playback actually starts. */
    setup();
    { uint64_t frame = 0; uint32_t half = 0, next_tick = 100000u;
      unsigned queued = 0, switched = 0, saw_wait = 0;
      br_midi_clock_enabled = 1u;
      br_engine_clock_dma_edge(0u, 0u, BR_BLOCK);
      br_engine_clock_midi_event(0xfau, 0u, br_midi_clock_generation);
      for (unsigned b = 0; b < 1800u; ++b) {
          uint32_t now = (uint32_t)(frame * 24000000u / FS);
          while (next_tick <= now) {
              br_engine_clock_midi_event(0xf8u, next_tick, br_midi_clock_generation);
              next_tick += 500000u;
          }
          render();
          if (!br_playing && !br_clock_experiment.enabled) saw_wait = 1u;
          if (br_playing && !queued) { submit(1); queued = 1u; }
          if (br_pattern_host_snapshot == &staged) { switched = 1u; break; }
          frame += BR_BLOCK; half ^= 1u;
          uint32_t observed = (uint32_t)(frame * 24000000u / FS);
          observed = half ? observed + 100u : observed - 100u;
          br_engine_clock_dma_edge(observed, half, BR_BLOCK);
      }
      assert(saw_wait && queued && switched && br_pattern_host_bars == 1u);
      finish_handoff();
    }
    /* Exercise the follower, timestamp mapping, F8, Start and clock loss
     * through the same bridge used by the real application. */
    setup();
    { uint64_t frame = 0; uint32_t half = 0, next_tick = 0; unsigned switched = 0;
      br_midi_clock_enabled = 1u;
      br_engine_clock_dma_edge(0u, 0u, BR_BLOCK);
      submit(1);
      for (unsigned b = 0; b < 1800u; ++b) {
          uint32_t now = (uint32_t)(frame * 24000000u / FS);
          while (next_tick <= now) {
              br_engine_clock_midi_event(0xf8u, next_tick, br_midi_clock_generation);
              next_tick += b < 200u ? 500000u : 600000u;
          }
          if (b == 20u) br_engine_clock_midi_event(0xfau, now, br_midi_clock_generation);
          render(); frame += BR_BLOCK; half ^= 1u;
          uint32_t observed = (uint32_t)(frame * 24000000u / FS);
          observed = half ? observed + 100u : observed - 100u;
          br_engine_clock_dma_edge(observed, half, BR_BLOCK);
          if (br_pattern_host_snapshot == &staged) { switched = 1u; break; }
      }
      assert(switched && br_pattern_host_bars == 1u);
      finish_handoff();
      for (unsigned b = 0; b < 40u; ++b) {
          render(); frame += BR_BLOCK; half ^= 1u;
          uint32_t observed = (uint32_t)(frame * 24000000u / FS);
          observed = half ? observed + 100u : observed - 100u;
          br_engine_clock_dma_edge(observed, half, BR_BLOCK);
      }
      assert(!br_playing && br_clock_experiment.invalid);
      submit(0);
      next_tick = (uint32_t)(frame * 24000000u / FS) + 100000u;
      for (unsigned b = 0; b < 300u; ++b) {
          uint32_t now = (uint32_t)(frame * 24000000u / FS);
          while (next_tick <= now) {
              br_engine_clock_midi_event(0xf8u, next_tick, br_midi_clock_generation);
              next_tick += 500000u;
          }
          render(); frame += BR_BLOCK; half ^= 1u;
          uint32_t observed = (uint32_t)(frame * 24000000u / FS);
          observed = half ? observed + 100u : observed - 100u;
          br_engine_clock_dma_edge(observed, half, BR_BLOCK);
      }
      assert(!br_playing && br_midi_clock_active);
      br_engine_clock_midi_event(0xfbu, (uint32_t)(frame * 24000000u / FS), br_midi_clock_generation);
      switched = 0;
      for (unsigned b = 0; b < 1600u; ++b) {
          uint32_t now = (uint32_t)(frame * 24000000u / FS);
          while (next_tick <= now) {
              br_engine_clock_midi_event(0xf8u, next_tick, br_midi_clock_generation);
              next_tick += 500000u;
          }
          render(); frame += BR_BLOCK; half ^= 1u;
          uint32_t observed = (uint32_t)(frame * 24000000u / FS);
          observed = half ? observed + 100u : observed - 100u;
          br_engine_clock_dma_edge(observed, half, BR_BLOCK);
          if (br_pattern_host_snapshot == &staged) { switched = 1u; break; }
      }
      assert(switched && br_pattern_host_bars == 2u);
      finish_handoff();
    }
    puts("host pattern audio integration passed");
    return 0;
}
