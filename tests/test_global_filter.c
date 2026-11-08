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
static uint32_t block(void) { br_gf_apply(); return br_master_cached.filter; }

int main(void)
{
    /* Smoothness 0 follows the knob exactly, a step per block. */
    setup(); knob(10u, 0, 0u); assert(block() == 10u);
    knob(100u, 0, 0u); assert(block() == 100u);

    /* Smoothness 100: a full-range move takes about a second (172 blocks), monotonic, then holds. */
    knob(0u, 0, 100u); while (block() != 0u) {}
    knob(127u, 0, 100u);
    uint32_t last = 0u, blocks = 0u;
    for (; blocks < 400u; ++blocks) {
        uint32_t v = block();
        assert(v >= last && v <= 127u);
        last = v;
        if (v == 127u) break;
    }
    assert(blocks >= 150u && blocks <= 190u);
    for (int i = 0; i < 20; ++i) assert(block() == 127u);

    /* The time scales with the size of the move: a 10-step move is quick. */
    knob(50u, 0, 100u); while (block() != 50u) {}
    knob(60u, 0, 100u);
    for (blocks = 0u; blocks < 100u && block() != 60u; ++blocks) {}
    assert(blocks >= 8u && blocks <= 18u);

    /* A lower smoothness is proportionally quicker. */
    knob(0u, 0, 50u); while (block() != 0u) {}
    knob(127u, 0, 50u);
    for (blocks = 0u; blocks < 300u && block() != 127u; ++blocks) {}
    assert(blocks >= 75u && blocks <= 95u);

    /* A new target in mid-move restarts from where the filter is, without a jump. */
    knob(0u, 0, 100u); while (block() != 0u) {}
    knob(127u, 0, 100u); for (int i = 0; i < 40; ++i) (void)block();
    uint32_t here = br_master_cached.filter;
    knob(20u, 0, 100u);
    uint32_t next = block();
    assert(next <= here && here - next <= 3u);

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
    knob(5u, -127, 0u); br_gf_kick = 1u; assert(block() == 0u);     /* clamped */

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
        assert(other.legacy_gate[0] == 128u && other.legacy_gate[1] == 128u && other.legacy_gate[2] == 128u && other.legacy_gate[3] == 128u);
        knob(80u, -45, 37u);
        assert(br_capture(&saved) && br_session_v16_valid(&saved));
        assert(saved.legacy_gate[0] == 128u - 45u && saved.legacy_gate[1] == 128u + 37u);
        knob(64u, 0, 0u);
        assert(br_restore(&saved));
        assert(br_master.fx.env_depth == -45 && br_master.fx.smooth == 37u && br_master.fx.filter == 80u);
        saved.legacy_gate[1] = 240u; assert(!br_session_v16_valid(&saved));
        saved.legacy_gate[1] = 128u; saved.legacy_gate[2] = 127u; assert(!br_session_v16_valid(&saved));
        saved.legacy_gate[2] = 128u; saved.legacy_gate[0] = 0u; assert(!br_session_v16_valid(&saved));
    }

    /* Screens: GLO screen 2 is FILTER, RESONANCE, ENV DEPTH, SMOOTH; HEAT has its own; both edit the master. */
    setup();
    br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 1u;
    assert(br_screen_count() == 7u);
    assert(!strcmp(br_control(2u).label, "ENV DEPTH") && !strcmp(br_control(3u).label, "SMOOTH"));
    br_control_turn(2u, -30); br_control_turn(3u, 55);
    assert(br_master.fx.env_depth == -30 && br_master.fx.smooth == 55u);
    br_control_turn(3u, 100); assert(br_master.fx.smooth == 100u);
    br_ui.screen[BR_PAGE_MIX] = 6u;
    assert(!strcmp(br_header_title(), "GLOBAL / HEAT") && !strcmp(br_control(0u).label, "HEAT TYPE") && !strcmp(br_control(1u).label, "AMOUNT"));
    br_control_turn(0u, 1); br_control_turn(1u, 40);
    assert(br_master.fx.heat_type == BR_HEAT_SAT && br_master.fx.heat_amount == 40u);
    br_ui.redraw = 1u; br_draw();
    br_ui.screen[BR_PAGE_MIX] = 1u; br_ui.redraw = 1u; br_draw();

    /* Track filter pages: ENV DEPTH is the mod envelope's amount, aimed at FILTER. */
    for (uint32_t part = 0u; part < 4u; ++part) {
        setup(); br_ui.part = part; br_ui.page = BR_PAGE_HOME;
        br_ui.screen[BR_PAGE_HOME] = 1u;
        br_part[part].mod.target = BR_TARGET_PITCH; br_part[part].mod.amount = 33;
        assert(!strcmp(br_control(2u).label, "ENV DEPTH") && br_control(2u).value == 0);   /* not aimed at the filter */
        br_control_turn(2u, -40);
        assert(br_part[part].mod.target == BR_TARGET_FILTER && br_part[part].mod.amount == -40);
        assert(br_control(2u).value == -40 && br_control(2u).low == -127 && br_control(2u).high == 127);
        assert(!strcmp(br_control(3u).label, "KEY TRACK") && br_control(3u).high == 1);
        br_control_turn(3u, 1); assert(br_part[part].sound.filter_keytrack == 1u); br_control_turn(3u, -1);
        br_control_turn(2u, 200); assert(br_part[part].mod.amount == 127);
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
