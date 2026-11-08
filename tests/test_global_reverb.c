/* One shared reverb with per-track sends, through the real app. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>

static void publish(uint32_t p) { RING_PUBLISH(); br_part[p].revision++; }

static void setup(void)
{
    br_app_init(); fm1_ms = 1000u;
    for (uint32_t p = 0u; p < BR_SYNTH_PARTS; ++p) {
        br_part[p].revision++;
        br_part[p].sound.reverb = 0u; br_part[p].sound.delay = 0u; br_part[p].sound.space = 64u;
        br_part[p].fx.reverb_engine = BR_REVERB_SPACE; br_part[p].fx.reverb_tone = 40u;
        publish(p);
    }
}
static void send(uint32_t p, uint32_t amount)
{
    br_part[p].revision++; br_part[p].sound.reverb = amount; publish(p);
}
static void reverb_settings(uint32_t p, uint32_t engine, uint32_t tone, uint32_t space)
{
    br_part[p].revision++;
    br_part[p].fx.reverb_engine = engine; br_part[p].fx.reverb_tone = tone; br_part[p].sound.space = space;
    publish(p);
}
static double block_rms(int32_t *keep)
{
    int32_t audio[BR_BLOCK * 2u] = {0};
    double sum = 0.0;
    assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
    for (uint32_t i = 0u; i < BR_BLOCK * 2u; ++i) sum += (double)audio[i] * audio[i];
    if (keep) memcpy(keep, audio, sizeof audio);
    return sqrt(sum / (BR_BLOCK * 2u));
}
/* Play a note on `part` for 15 blocks, release, and return the RMS of the tail
 * measured over blocks [skip, skip + count) after the release. */
static double tail_rms(uint32_t part, uint32_t skip, uint32_t count)
{
    double sum = 0.0;
    br_engine_note_on(part, 60u, 100u);
    for (uint32_t i = 0u; i < 15u; ++i) (void)block_rms(0);
    br_engine_note_off(part, 60u);
    for (uint32_t i = 0u; i < skip; ++i) (void)block_rms(0);
    for (uint32_t i = 0u; i < count; ++i) { double r = block_rms(0); sum += r * r; }
    return sqrt(sum / count);
}

int main(void)
{
    double dry, wet, two, other;
    /* Without a send there is nothing after the note; with one there is a tail. */
    setup();
    dry = tail_rms(0u, 60u, 40u);
    setup(); send(0u, 100u);
    wet = tail_rms(0u, 60u, 40u);
    assert(wet > 1.0 && dry < 0.01);

    /* Sends add: two tracks sending the same note level are louder than one. */
    setup(); send(0u, 100u); send(1u, 100u);
    br_engine_note_on(0u, 60u, 100u); br_engine_note_on(1u, 60u, 100u);
    for (uint32_t i = 0u; i < 15u; ++i) (void)block_rms(0);
    br_engine_note_off(0u, 60u); br_engine_note_off(1u, 60u);
    for (uint32_t i = 0u; i < 60u; ++i) (void)block_rms(0);
    { double sum = 0.0; for (uint32_t i = 0u; i < 40u; ++i) { double r = block_rms(0); sum += r * r; }
      two = sqrt(sum / 40.0); }
    assert(two > 1.15 * wet);

    /* The reverb's type, decay and size come from track 1 alone. */
    setup(); send(1u, 100u); reverb_settings(1u, BR_REVERB_HALL, 120u, 100u);
    other = tail_rms(1u, 60u, 40u);
    setup(); send(1u, 100u); reverb_settings(0u, BR_REVERB_HALL, 120u, 100u);
    wet = tail_rms(1u, 60u, 40u);
    assert(fabs(wet - other) > 0.02 * other);      /* track 1's settings are what is heard */
    setup(); send(1u, 100u); reverb_settings(0u, BR_REVERB_HALL, 120u, 100u);
    reverb_settings(1u, BR_REVERB_SPACE, 40u, 64u);        /* track 2's own values are ignored */
    { double again = tail_rms(1u, 60u, 40u); assert(fabs(again - wet) < 1e-6); }

    /* Size is shared too: track 1's value is heard, track 2's is not. */
    {
        double both_small, small_big, big;
        setup(); send(1u, 100u); reverb_settings(0u, BR_REVERB_HALL, 60u, 20u); reverb_settings(1u, BR_REVERB_HALL, 60u, 20u);
        both_small = tail_rms(1u, 30u, 30u);
        setup(); send(1u, 100u); reverb_settings(0u, BR_REVERB_HALL, 60u, 20u); reverb_settings(1u, BR_REVERB_HALL, 60u, 120u);
        small_big = tail_rms(1u, 30u, 30u);
        setup(); send(1u, 100u); reverb_settings(0u, BR_REVERB_HALL, 60u, 120u); reverb_settings(1u, BR_REVERB_HALL, 60u, 120u);
        big = tail_rms(1u, 30u, 30u);
        assert(fabs(both_small - small_big) < 1e-6 && fabs(big - both_small) > 0.02 * both_small);
    }

    /* The retired ids play exactly as their replacements: Spring as Room, Ethereal as Space. */
    {
        static const uint32_t pair[2][2] = {{BR_REVERB_SPRING, BR_REVERB_ROOM}, {BR_REVERB_ETHEREAL, BR_REVERB_SPACE}};
        for (uint32_t k = 0u; k < 2u; ++k) {
            double retired, kept;
            setup(); send(0u, 127u); reverb_settings(0u, pair[k][0], 100u, 64u); retired = tail_rms(0u, 20u, 60u);
            setup(); send(0u, 127u); reverb_settings(0u, pair[k][1], 100u, 64u); kept = tail_rms(0u, 20u, 60u);
            assert(retired > 0.2 && fabs(retired - kept) < 1e-9);
        }
    }

    /* Every type runs and gives a tail; no non-finite blow-ups, output bounded. */
    for (uint32_t engine = 0u; engine < BR_REVERB_COUNT; ++engine) {
        if (engine == BR_REVERB_SPRING || engine == BR_REVERB_ETHEREAL) continue;
        setup(); send(0u, 127u); reverb_settings(0u, engine, 100u, 64u);
        double level = tail_rms(0u, 20u, 60u);
        assert(level > 0.2 && level < 20000.0);
    }

    /* The tail dies away, the engine reports quiet, and a later phrase starts clean. */
    setup(); send(0u, 100u); reverb_settings(0u, BR_REVERB_ROOM, 10u, 40u);
    double first = tail_rms(0u, 5u, 20u);
    uint32_t blocks = 0u;
    while (br_fx_quiet() == 0 && blocks < 4000u) { (void)block_rms(0); ++blocks; }
    assert(first > 0.5 && br_fx_quiet() && blocks > 100u && blocks < 4000u);
    { double r = 0.0; for (uint32_t i = 0u; i < 20u; ++i) r += block_rms(0); assert(r == 0.0); }
    for (uint32_t i = 0u; i < BR_FX_ROOM_SAMPLES; ++i) assert(br_room_line[0][i] == 0);   /* lines emptied */
    assert(!br_gr_state.tail_storage);
    { double again = tail_rms(0u, 5u, 20u); assert(fabs(again - first) < 0.1 * first); }

    /* Turning a send to zero lets the tail ring out rather than cutting it. */
    setup(); send(0u, 100u);
    br_engine_note_on(0u, 60u, 100u);
    for (uint32_t i = 0u; i < 15u; ++i) (void)block_rms(0);
    br_engine_note_off(0u, 60u);
    for (uint32_t i = 0u; i < 20u; ++i) (void)block_rms(0);
    send(0u, 0u);
    { double sum = 0.0; for (uint32_t i = 0u; i < 20u; ++i) { double r = block_rms(0); sum += r * r; }
      assert(sqrt(sum / 20.0) > 0.5); }

    /* Sends on silent tracks cost nothing: the reverb stays idle until sound arrives. */
    setup(); send(0u, 100u); send(1u, 127u);
    for (uint32_t i = 0u; i < 40u; ++i) (void)block_rms(0);
    assert(br_gr_run == 0u);
    br_engine_note_on(0u, 60u, 100u);
    { uint32_t seen = 0u; for (uint32_t i = 0u; i < 6u; ++i) { (void)block_rms(0); seen |= br_gr_run; } assert(seen); }
    br_engine_note_off(0u, 60u);
    for (uint32_t i = 0u; i < 2500u && !(br_fx_quiet() && br_engine_quiet()); ++i) (void)block_rms(0);
    assert(br_fx_quiet() && br_engine_quiet());
    for (uint32_t i = 0u; i < 4u; ++i) (void)block_rms(0);
    assert(br_gr_run == 0u);

    /* At full send every type returns about as loud as the dry note (within 4 dB). */
    {
        double dry_level;
        setup(); br_engine_note_on(0u, 60u, 100u);
        { double sum = 0.0; for (uint32_t i = 0u; i < 100u; ++i) { double r = block_rms(0); if (i >= 40u) sum += r * r; } dry_level = sqrt(sum / 60.0); }
        for (uint32_t engine = 0u; engine < BR_REVERB_COUNT; ++engine) {
            if (engine == BR_REVERB_SPRING || engine == BR_REVERB_ETHEREAL) continue;
            setup(); send(0u, 127u); reverb_settings(0u, engine, 64u, 64u);
            br_engine_note_on(0u, 60u, 100u);
            double sum = 0.0;
            for (uint32_t i = 0u; i < 100u; ++i) {
                (void)block_rms(0);
                if (i >= 40u) for (uint32_t k = 0u; k < BR_BLOCK; ++k)
                    sum += (double)br_gr_wet[k][0] * br_gr_wet[k][0] + (double)br_gr_wet[k][1] * br_gr_wet[k][1];
            }
            double ret = sqrt(sum / (60.0 * BR_BLOCK * 2.0));
            assert(ret > 0.63 * dry_level && ret < 1.6 * dry_level);
        }
    }

    /* The keyboard plays on the FX and GLO pages. */
    setup();
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB; br_ui.part = 0u;
    br_key_down(5u, 1);
    assert(br_ui.held_note[5]);
    { double level = 0.0; for (uint32_t i = 0u; i < 30u; ++i) { double r = block_rms(0); if (r > level) level = r; } assert(level > 50.0); }
    br_key_up(5u); assert(!br_ui.held_note[5]);
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u;
    br_key_down(5u, 1); assert(br_ui.held_note[5]);        /* the GLO pages play the selected track too */
    br_key_up(5u);

    /* A click leaves a dense wash, not a few separate echoes: the 4 ms envelope of the
     * return changes smoothly (it was 0.5 to 0.8 log10 units per bin without input diffusion). */
    for (uint32_t engine = 0u; engine < BR_REVERB_COUNT; ++engine) {
        if (engine == BR_REVERB_SPRING || engine == BR_REVERB_ETHEREAL) continue;
        static double env[200];
        setup(); send(0u, 127u); reverb_settings(0u, engine, 64u, 64u);
        br_load_preset(0u, BR_FIRST_PLAITS + 23u);
        br_part[0].revision++; br_part[0].sound.decay = 0u; br_part[0].sound.sustain = 0u; br_part[0].sound.release = 0u; publish(0u);
        uint32_t onset = 0u, count = 0u; double sum = 0.0; uint32_t bin = 0u;
        for (uint32_t b = 0u; b < 60u; ++b) {
            if (b == 4u) br_engine_note_on(0u, 60u, 127u);
            if (b == 5u) br_engine_note_off(0u, 60u);
            (void)block_rms(0);
            for (uint32_t i = 0u; i < BR_BLOCK; ++i) {
                double w = br_gr_wet[i][0];
                if (!onset && fabs(w) > 3.0) onset = 1u, count = 0u, sum = 0.0, bin = 0u;
                if (!onset) continue;
                sum += w * w;
                if (++count == 176u) { if (bin < 100u) env[bin++] = sqrt(sum / 176.0); count = 0u; sum = 0.0; }
            }
        }
        assert(bin >= 60u);
        double rough = 0.0;
        for (uint32_t k = 1u; k < 60u; ++k) rough += fabs(log10(env[k] + 1e-3) - log10(env[k - 1u] + 1e-3));
        assert(rough / 59.0 < 0.25);
    }

    /* SPACE's modulation is slow: both LFOs under 0.6 Hz. */
    assert(BR_SP_LFO_A_INC * 11025.0 / 4294967296.0 < 0.6 && BR_SP_LFO_B_INC * 11025.0 / 4294967296.0 < 0.6);

    /* SPACE stays bounded at its longest decay and extreme sizes, through loud notes and
     * switches to another type and back. */
    {
        double peak = 0.0;
        setup(); send(0u, 127u); send(1u, 127u);
        for (uint32_t round = 0u; round < 6u; ++round) {
            uint32_t sizes[3] = {0u, 127u, 64u};
            reverb_settings(0u, round % 2u ? BR_REVERB_SPACE : BR_REVERB_SPACE, 127u, sizes[round % 3u]);
            if (round == 3u) reverb_settings(0u, BR_REVERB_HALL, 127u, 64u);
            if (round == 4u) reverb_settings(0u, BR_REVERB_SPACE, 127u, 100u);
            br_engine_note_on(0u, 48u, 127u); br_engine_note_on(1u, 55u, 127u);
            for (uint32_t i = 0u; i < 200u; ++i) {
                (void)block_rms(0);
                for (uint32_t k = 0u; k < BR_BLOCK; ++k) { double a = fabs((double)br_gr_wet[k][0]); if (a > peak) peak = a; }
            }
            br_engine_note_off(0u, 48u); br_engine_note_off(1u, 55u);
            for (uint32_t i = 0u; i < 100u; ++i) (void)block_rms(0);
        }
        /* Both tracks at full send and decay with calibrated (louder) engines: a sanity bound against overflow. */
        assert(peak > 100.0 && peak < 220000.0);
    }

    /* After a boot or project load (restore, FX init, warm controls) the reverb is heard at once. */
    for (uint32_t engine = 0u; engine < BR_REVERB_COUNT; ++engine) {
        if (engine == BR_REVERB_SPRING || engine == BR_REVERB_ETHEREAL) continue;
        br_session_t saved;
        setup(); send(0u, 127u); send(1u, 90u); reverb_settings(0u, engine, 40u, 70u);
        assert(br_capture(&saved));
        setup();
        assert(br_restore(&saved));
        br_fx_init(); br_engine_init();
        if (!br_playing && br_engine_quiet() && br_fx_quiet()) br_fx_warm_controls_quiescent();
        br_engine_note_on(0u, 60u, 100u);
        double sum = 0.0; uint32_t seen = 0u;
        for (uint32_t i = 0u; i < 80u; ++i) {
            (void)block_rms(0);
            if (i >= 30u) for (uint32_t k = 0u; k < BR_BLOCK; ++k)
                sum += (double)br_gr_wet[k][0] * br_gr_wet[k][0] + (double)br_gr_wet[k][1] * br_gr_wet[k][1];
            seen |= br_gr_run;
        }
        assert(seen && br_gr_controls.reverb_return_q8 && sqrt(sum / (50.0 * BR_BLOCK * 2.0)) > 100.0);
    }

#if defined(BR_REVERB_AUDIO_ONLY) && BR_REVERB_AUDIO_ONLY
    puts("PASS: shared reverb audio checks (screen checks not run)");
    return 0;
#endif
    /* The mixer and the FX send bars are different screens: switching between them repaints. */
    setup();
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u; br_ui.redraw = 1u; br_draw();
    uint32_t mixer_layout = br_draw_layout_old;
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
    assert(br_draw_layout_old != mixer_layout);
    br_ui.page = BR_PAGE_MIX; br_ui.redraw = 1u; br_draw();
    assert(br_draw_layout_old == mixer_layout);

    /* Entering FX sends from the mixer must match entry from another page;
     * the current FX sends screen has its own cue in the bottom rows. */
    {
        uint32_t white_mixer = 0u, white_drive = 0u;
        uint16_t fx_from_mixer[13u * 240u];
        br_ui.part = 0u; br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u; br_ui.redraw = 1u; br_draw();
        for (uint32_t y = 227u; y < 240u; ++y) for (uint32_t x = 0u; x < 240u; ++x) white_mixer += fb[y * 240u + x] == 0xffffu;
        br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
        memcpy(fx_from_mixer, &fb[227u * 240u], sizeof fx_from_mixer);
        br_ui.page = BR_PAGE_MIX; br_ui.redraw = 1u; br_draw();
        br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_DRIVE; br_ui.redraw = 1u; br_draw();
        for (uint32_t y = 227u; y < 240u; ++y) for (uint32_t x = 0u; x < 240u; ++x) white_drive += fb[y * 240u + x] == 0xffffu;
        assert(white_mixer > 40u && white_drive == 0u);
        br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
        assert(!memcmp(fx_from_mixer, &fb[227u * 240u], sizeof fx_from_mixer));
    }

    /* The REVERB screen has a goldenrod GLOBAL badge in its header. */
    {
        uint32_t gold_tag = 0u, gold_track = 0u;
        uint16_t gold = BR_GLOBAL_ACC, gold_swapped = (uint16_t)(gold >> 8 | gold << 8);
        br_ui.part = 1u; br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB; br_ui.redraw = 1u; br_draw();
        assert(br_ui_accent == BR_GLOBAL_ACC);
        for (uint32_t y = 46u; y < 62u; ++y) for (uint32_t x = 80u; x < 160u; ++x)
            gold_tag += fb[y * 240u + x] == gold || fb[y * 240u + x] == gold_swapped;
        assert(gold_tag > 200u);
        br_ui.screen[BR_PAGE_FX] = BR_FX_DELAY; br_ui.redraw = 1u; br_draw();
        assert(br_ui_accent == br_track_color(1u) && br_ui_accent != BR_GLOBAL_ACC);
        for (uint32_t y = 46u; y < 62u; ++y) for (uint32_t x = 80u; x < 160u; ++x)
            gold_track += fb[y * 240u + x] == gold || fb[y * 240u + x] == gold_swapped;
        assert(gold_track == 0u);
        /* The track's own effect screens carry a TRACK tag in the track's colour. */
        for (uint32_t screen = BR_FX_DRIVE; screen <= BR_FX_MOD; ++screen) {
            uint32_t tag = 0u; uint16_t c = br_track_color(1u), cs = (uint16_t)(c >> 8 | c << 8);
            br_ui.screen[BR_PAGE_FX] = screen; br_ui.redraw = 1u; br_draw();
            for (uint32_t y = 46u; y < 62u; ++y) for (uint32_t x = 80u; x < 160u; ++x)
                tag += fb[y * 240u + x] == c || fb[y * 240u + x] == cs;
            assert(tag > 200u);
        }
        br_ui.screen[BR_PAGE_FX] = BR_FX_SENDS; br_ui.redraw = 1u; br_draw();
    }

    /* Send levels walk to a new value a step per block, so a quick knob turn is a ramp, not a click. */
    {
        int32_t a[BR_BLOCK * 2u];
        uint32_t last[3], now[3], reached[3] = {0u, 0u, 0u};
        setup(); br_engine_note_on(0u, 60u, 100u);
        for (int blk = 0; blk < 6; ++blk) { memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
        br_part[0].revision++; br_part[0].sound.delay = 100u; br_part[0].sound.reverb = 100u; br_part[0].fx.mod_mix = 90u;
        publish(0);
        last[0] = br_effective_sound[0].delay; last[1] = br_effective_sound[0].reverb; last[2] = br_effective_fx[0].mod_mix;
        assert(!last[0] && !last[1] && !last[2]);
        for (int blk = 0; blk < 140; ++blk) {
            memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
            now[0] = br_effective_sound[0].delay; now[1] = br_effective_sound[0].reverb; now[2] = br_effective_fx[0].mod_mix;
            assert(now[0] - last[0] <= 1u && now[1] - last[1] <= 2u && now[2] - last[2] <= 1u);   /* one step at most */
            for (int k = 0; k < 3; ++k) { if (now[k] == (k == 2 ? 90u : 100u) && !reached[k]) reached[k] = (uint32_t)blk + 1u; last[k] = now[k]; }
        }
        assert(reached[0] > 90u && reached[1] > 40u && reached[2] > 80u);                             /* a ramp, not a jump */
        assert(last[0] == 100u && last[1] == 100u && last[2] == 90u);                                 /* and it arrives */
        br_part[0].revision++; br_part[0].sound.delay = 0u; publish(0);                              /* down again, to exactly zero */
        for (int blk = 0; blk < 140; ++blk) { memset(a, 0, sizeof a); assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
        assert(br_effective_sound[0].delay == 0u);
    }

    /* The REVERB screen edits one shared setting from any track; only SEND is per track. */
    setup();
    br_ui.page = BR_PAGE_FX; br_ui.screen[BR_PAGE_FX] = BR_FX_REVERB; br_ui.part = 2u;
    {
        uint32_t revisions[BR_PARTS];
        for (uint32_t i = 0u; i < BR_PARTS; ++i) revisions[i] = br_part[i].revision;
        br_control_turn(1u, 7); br_control_turn(2u, -5);
        for (uint32_t i = 0u; i < BR_PARTS; ++i) {
            assert(br_part[i].sound.space == 71u && br_part[i].fx.reverb_tone == 35u);
            assert(br_part[i].revision != revisions[i] && !(br_part[i].revision & 1u));
        }
        br_control_turn(0u, 1);
        for (uint32_t i = 0u; i < BR_PARTS; ++i)
            assert(br_part[i].fx.reverb_engine == br_part[0].fx.reverb_engine);
        assert(br_part[0].fx.reverb_engine != BR_REVERB_ETHEREAL);
        br_control_turn(3u, 40);
        assert(br_part[2].sound.reverb == 40u && !br_part[0].sound.reverb && !br_part[1].sound.reverb);
        br_ui.part = 0u;                      /* every track shows the same shared values */
        assert(br_control(1u).value == 71 && br_control(2u).value == 35);
        br_ui.part = 2u;
        assert(br_control(1u).value == 71 && !strcmp(br_control(3u).label, "SEND"));
        br_ui.redraw = 1u; br_draw();
    }

    printf("shared reverb: send, sum, track-1 settings, every type, tail and quiet checks passed\n");
    return 0;
}
