/* Global filter smoothness and note-triggered envelope, the track filter envelope depth, the new
 * screens and the session bytes that carry them. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <math.h>

static void setup(void) { br_app_init(); fm1_ms = 1000u; memset(&br_gf, 0, sizeof br_gf); br_gf_kick = 0u; }
static void knob(uint32_t filter, int32_t depth, uint32_t smooth)
{
    br_master.revision++; br_master.fx.filter = filter; br_master.fx.env_depth = depth; br_master.fx.smooth = smooth;
    RING_PUBLISH(); br_master.revision++;
}
static uint32_t block(void) { br_master_snapshot_ram(); br_master_apply_lfo(); br_gf_apply(BR_BLOCK); return br_master_cached.filter; }

int main(void)
{
    /* Full-range movement takes about 250 ms, independent of legacy smooth. */
    setup(); knob(0u, 0, 0u); assert(block() == 0u);
    knob(127u, 0, 0u);
    uint32_t last = 0u, blocks = 0u;
    for (; blocks < 100u; ++blocks) {
        uint32_t v = block(); assert(v >= last && v <= 127u); last = v;
        if (v == 127u) break;
    }
    assert(blocks >= 40u && blocks <= 44u);
    for (int i = 0; i < 5; ++i) assert(block() == 127u);
    /* The same duration holds when the effects pipeline runs 32-frame blocks. */
    setup(); knob(0u, 0, 0u);
    br_master_snapshot_ram(); br_master_apply_lfo(); br_gf_apply(32u);
    knob(127u, 0, 0u);
    for (blocks = 0u; blocks < 400u; ++blocks) {
        br_master_snapshot_ram(); br_master_apply_lfo(); br_gf_apply(32u);
        if (br_master_cached.filter == 127u) break;
    }
    assert(blocks >= 340u && blocks <= 346u);
    knob(50u, 0, 100u); while (block() != 50u) {}
    knob(60u, 0, 100u);
    for (blocks = 0u; blocks < 100u && block() != 60u; ++blocks) {}
    assert(blocks >= 2u && blocks <= 5u);
    /* A new target during a glide starts from the current position. */
    knob(0u, 0, 0u); while (block() != 0u) {}
    knob(127u, 0, 0u); for (int i = 0; i < 10; ++i) (void)block();
    uint32_t here = br_master_cached.filter;
    knob(20u, 0, 0u); uint32_t next = block();
    assert(next <= here && here - next <= 4u);
    /* A snap reaches its destination on the next block even mid-glide. */
    knob(0u, 0, 0u); while (block() != 0u) {}
    knob(127u, 0, 0u); (void)block();
    br_master.revision++; br_master.fx.filter = 64u; ++br_master.fx.filter_snap_generation;
    RING_PUBLISH(); br_master.revision++;
    assert(block() == 64u);
#if !BR_AUDIO_FUSION_ONLY_BUILD
    { br_pipeline_controls_t captured;
      br_master.revision++; br_master.fx.filter = 127u; ++br_master.fx.filter_snap_generation;
      RING_PUBLISH(); br_master.revision++;
      br_fx_capture_pipeline_controls(&captured);
      br_master.fx.filter = 0u; br_fx_pipeline_controls = &captured;
      assert(block() == 127u); br_fx_pipeline_controls = 0; }
#endif
    /* The envelope: a note adds the depth, which then dies away; negative depth closes. */
    setup(); knob(64u, 60, 0u);
    assert(block() == 64u);
    br_gf_kick = 1u;
    uint32_t first = block();
    assert(first >= 120u && br_gf_kick == 0u);          /* 64 + 60 * full, clipped at 127 */
    uint32_t prev = first;
    for (blocks = 0u; blocks < 500u; ++blocks) { uint32_t v = block(); assert(v <= prev); prev = v; if (v == 64u) break; }
    assert(blocks > 60u && blocks < 300u && prev == 64u);
    knob(64u, -60, 0u); br_gf_kick = 1u;
    assert(block() <= 6u);                                /* closes toward the low-pass end */
    knob(64u, 127, 0u); br_gf_kick = 1u; assert(block() == 127u);
    knob(5u, -127, 0u); while (block() != 5u) {} br_gf_kick = 1u; assert(block() == 0u);     /* clamped */

    /* Every note, from any track or the kit, retriggers it. */
    setup(); knob(64u, 40, 0u);
    (void)block();
    br_load_preset(0u, BR_FIRST_PLAITS); br_engine_note_on(0u, 60u, 100u);
    { int32_t a[BR_BLOCK * 2u] = {0}; assert(app_audio(a, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
    assert(block() > 64u);
    for (int i = 0; i < 400; ++i) (void)block();
    assert(block() == 64u);
    br_engine_note_off(0u, 60u);
    br_engine_note_on(BR_DRUM_PART, 36u, 100u);
    { int32_t a[BR_BLOCK * 2u] = {0}; (void)app_audio(a, BR_BLOCK); }
    assert(block() > 64u);

    /* Session: the controls round-trip, and unused they leave the bytes as they were. */
    {
        br_session_t saved, other;
        setup(); assert(br_capture(&other));
        assert(other.b[br_v17_wire_at(BR_V17_ROW(M, env_depth), 0u, 0u)] == 0u && other.b[br_v17_wire_at(BR_V17_ROW(M, smooth), 0u, 0u)] == 0u);
        knob(80u, -45, 37u);
        assert(br_capture(&saved) && br_session_valid(&saved));
        assert((int8_t)saved.b[br_v17_wire_at(BR_V17_ROW(M, env_depth), 0u, 0u)] == -45 && saved.b[br_v17_wire_at(BR_V17_ROW(M, smooth), 0u, 0u)] == 37u);
        knob(64u, 0, 0u);
        assert(br_restore(&saved));
        assert(br_master.fx.env_depth == -45 && br_master.fx.smooth == 37u && br_master.fx.filter == 80u);
        saved.b[br_v17_wire_at(BR_V17_ROW(M, smooth), 0u, 0u)] = 240u;     /* out of range: clamped to 100 */
        assert(br_session_valid(&saved) && br_restore(&saved) && br_master.fx.smooth == 100u);
    }

    /* Screens: FILTER, RESONANCE, ENV DEPTH; the fourth control is unused. */
    setup();
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 1u;
    assert(br_screen_count() == 9u);   /* screen IDs 0-8; GLO shows six, then TEMPO / MIDI */
    assert(!strcmp(br_control(2u).label, "ENV DEPTH") && !br_control(3u).label[0]);
    br_control_turn(2u, -30); br_control_turn(3u, 55);
    assert(br_master.fx.env_depth == -30 && br_master.fx.smooth == 0u);
    br_ui.screen[BR_PAGE_MIX] = 6u;
    assert(!strcmp(br_header_title(), "GLOBAL / HEAT") && !strcmp(br_control(0u).label, "HEAT TYPE") && !strcmp(br_control(1u).label, "AMOUNT"));
    br_control_turn(0u, 1); br_control_turn(1u, 40);
    assert(br_master.fx.heat_type == BR_HEAT_SAT && br_master.fx.heat_amount == 40u);
    br_ui.redraw = 1u; br_draw();
    br_ui.screen[BR_PAGE_MIX] = 1u; br_ui.redraw = 1u; br_draw();

    /* Track filter pages: ENV DEPTH is the mod envelope's amount while it is aimed at FILTER; aimed
     * elsewhere the knob names the target ("TO PITCH"), is dimmed and changes nothing. */
    for (uint32_t part = 0u; part < 4u; ++part) {
        setup(); br_ui.part = part; br_ui.page = BR_PAGE_HOME;
        br_ui.screen[BR_PAGE_HOME] = 1u;
        /* ENV DEPTH is the filter's own depth from the mod envelope, whatever its TARGET. */
        br_part[part].mod.target = BR_TARGET_PITCH; br_part[part].mod.amount = 33;
        assert(!strcmp(br_control(2u).label, "ENV DEPTH") && br_control(2u).value == 0 && !br_control(2u).text);
        br_control_turn(2u, -40);
        assert(br_part[part].mod.target == BR_TARGET_PITCH && br_part[part].mod.amount == 33);
        assert(br_part[part].sound.filter_env_depth == -40 && br_control(2u).value == -40);
        br_part[part].mod.target = BR_TARGET_FILTER; assert(!br_control(2u).text && br_control(2u).value == -40);
        assert(br_control(2u).low == -127 && br_control(2u).high == 127);
        assert(!strcmp(br_control(3u).label, "KEY TRACK") && br_control(3u).high == 1);
        br_control_turn(3u, 1); assert(br_part[part].sound.filter_keytrack == 127u); br_control_turn(3u, -1);
        br_control_turn(2u, 200); assert(br_part[part].sound.filter_env_depth == 127 && br_part[part].mod.amount == 33);
        br_ui.redraw = 1u; br_draw();
    }
    /* Compressor SOURCE: OFF, ALL, TRACK 1-3 and KICK DRUM (drum sources 2 to 4 are no longer offered). */
    {
        static const char *const want[6] = {"OFF", "ALL", "TRACK 1", "TRACK 2", "TRACK 3", "KICK DRUM"};
        setup(); br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 4u;
        br_master.comp.source = BR_COMP_OFF;
        for (uint32_t i = 0u; i < 6u; ++i) {
            char text[32];
            br_control_t c = br_control(0u);
            br_control_text(0u, &c, text);
            assert(c.high == (int32_t)BR_COMP_DRUM_1 && !strcmp(text, want[i]) && br_control_widget(0u, &c) == BR_WIDGET_COMP_SOURCE);
            assert(br_control_widget_variant(0u, &c, BR_WIDGET_COMP_SOURCE) == i);
            br_ui.redraw = 1u; br_draw();
            br_control_turn(0u, 1);
        }
        assert(br_master.comp.source == BR_COMP_DRUM_1);                      /* stops at the kick */
        assert(br_comp_source_drum_lane(br_master.comp.source) == 0u && br_comp_source_part(br_master.comp.source) == BR_DRUM_PART);
    }
    printf("global filter: smoothing, note envelope, session, screens and track depth checks passed\n");
    return 0;
}
