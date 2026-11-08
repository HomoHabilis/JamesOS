/* Real-app MIDI routing/recording regressions. No hardware. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <stdio.h>
#include <string.h>
static int failed;
#define CHECK(c, name) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", name); ++failed; } } while (0)
static void setup(void)
{
    br_app_init();
    memset(&br_midi_routes, 0, sizeof br_midi_routes);
    br_ui_recording_state_reset();
    for (unsigned i=0;i<7;++i) br_midi_channels[i]=(uint8_t)(i+1);
    br_recording=1; br_playing=0; br_ui.cursor=0;
}
int main(void)
{
    setup();
    br_midi(0x90,60,100); br_midi(0x90,60,100);
    CHECK(br_record_hold[0][60].active, "synth retrigger is held");
    br_midi(0x80,60,0);
    CHECK(br_record_hold[0][60].active, "synth stale off preserves newest hit");
    br_midi(0x80,60,0);
    CHECK(!br_record_hold[0][60].active, "synth final off closes recording");
    setup();
    br_midi(0x93,36,100); br_midi(0x93,52,100);
    CHECK(br_record_hold[3][br_drum_lane_note(0)].active, "kit retrigger is held");
    br_midi(0x83,36,0);
    CHECK(br_record_hold[3][br_drum_lane_note(0)].active, "kit stale pitch off preserves newest hit");
    br_midi(0x83,52,0);
    CHECK(!br_record_hold[3][br_drum_lane_note(0)].active, "kit final pitch off closes recording");
    setup();
    br_midi(0x90,60,100);
    br_midi_channel_set(0,2); br_midi_channel_set(0,1);
    br_midi(0x90,60,100);
    br_midi(0x80,60,0);
    CHECK(br_record_hold[0][60].active, "reassigned stale off preserves new hit");
    br_midi(0x80,60,0);
    CHECK(!br_record_hold[0][60].active, "reassigned final off closes recording");
    setup();
    br_midi_channel_set(4,4); /* D1 and D2 share channel 4 */
    br_midi(0x93,33,100);
    CHECK(br_record_hold[3][br_drum_lane_note(0)].active && br_record_hold[3][br_drum_lane_note(1)].active, "duplicate channel records both lanes");
    br_midi(0x83,33,0);
    CHECK(!br_record_hold[3][br_drum_lane_note(0)].active && !br_record_hold[3][br_drum_lane_note(1)].active, "duplicate channel releases both lane records");
    setup();
    { br_session_t saved; CHECK(br_capture(&saved), "capture before recall");
      br_midi(0x90,60,100); CHECK(br_restore(&saved), "restore while MIDI held");
      br_midi(0x90,60,100); br_midi(0x80,60,0);
      CHECK(br_record_hold[0][60].active, "pre-recall off preserves new hit");
      br_midi(0x80,60,0);
      CHECK(!br_record_hold[0][60].active, "post-recall final off closes record"); }
    setup();
    { uint16_t physical;
      br_midi(0x90,60,100);
      physical=br_record_note_begin(0,60,100,0,0,0);
      br_midi(0x80,60,0);
      CHECK(br_record_hold[0][60].active && br_record_hold[0][60].generation == physical, "MIDI off preserves replacement physical recording");
      br_record_note_finish(0,60,physical);
      CHECK(!br_record_hold[0][60].active, "physical off finishes own recording"); }
    setup();
    { uint16_t physical=br_record_note_begin(0,60,100,0,0,0);
      br_midi(0x90,60,100);
      br_record_note_finish(0,60,physical);
      CHECK(br_record_hold[0][60].active && br_record_hold[0][60].midi_source, "old physical off preserves replacement MIDI recording");
      br_midi(0x80,60,0);
      CHECK(!br_record_hold[0][60].active, "MIDI off finishes own recording"); }
    /* Full ledger must recover after either channel panic, with no per-note offs. */
    for (unsigned ccidx=0;ccidx<2;++ccidx) {
      uint8_t cc = ccidx ? 120u : 123u;
      int32_t audio[BR_BLOCK*2];
      setup();
      for (unsigned cycle=0;cycle<4;++cycle) {
        for (unsigned note=0;note<64;++note) {
          br_midi(0x90,(uint8_t)note,100);
          if ((note & 7u)==7u) CHECK(app_audio(audio,BR_BLOCK)==FM1_AUDIO_RENDER_OK, "render during fill");
        }
        unsigned used=0;
        for (unsigned i=0;i<BR_MIDI_ROUTE_NOTES;++i) used += br_midi_routes.note[i].channel != 0;
        CHECK(used==64u, "64 input notes fill route ledger");
        br_midi(0xb0,cc,0);
        CHECK(app_audio(audio,BR_BLOCK)==FM1_AUDIO_RENDER_OK, "render after channel panic");
        used=0;
        for (unsigned i=0;i<BR_MIDI_ROUTE_NOTES;++i) used += br_midi_routes.note[i].channel != 0;
        CHECK(used==0u, "channel panic clears note ownership");
        br_midi(0x90,100,100);
        CHECK(br_record_hold[0][100].active, "new note after panic is routed and recorded");
        CHECK(app_audio(audio,BR_BLOCK)==FM1_AUDIO_RENDER_OK, "render new note");
        CHECK(br_env[0].note == 100u && br_env[0].stage != BR_ENV_OFF, "new note after panic reaches synth engine");
        br_midi(0x80,100,0);
        CHECK(!br_record_hold[0][100].active, "new note off closes recording");
        CHECK(app_audio(audio,BR_BLOCK)==FM1_AUDIO_RENDER_OK, "render new note off");
      }
    }
    if (!failed) puts("MIDI actual-app regressions passed");
    return failed ? 1 : 0;
}
