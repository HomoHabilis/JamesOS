/* Render each track effect through the real app and write the stereo output, so an
 * optimised effect can be compared with the previous build sample for sample, and listened to.
 * Usage: test_fx_render OUTDIR.  One file per case: OUTDIR/<case>.raw, int32 stereo at 44.1 kHz. */
#define DINGHY_HOST 1
#define main host_unused_main
#ifdef FX_RENDER_DINGHY
#include FX_RENDER_DINGHY
#else
#include "../firmware/src/dinghy.c"
#endif
#undef main
#include <assert.h>

static void publish(uint32_t p) { RING_PUBLISH(); br_part[p].revision++; }

typedef struct { const char *name; uint32_t kind, engine, a, b, send, on_at, off_at, pan; } fx_case_t;
enum { K_DRY, K_DRIVE, K_DELAY, K_MOD, K_REVERB, K_HEAT, K_COMP };
static const fx_case_t cases[] = {
    {"dry", K_DRY, 0, 0, 0, 0},
    {"drive-soft", K_DRIVE, BR_DRIVE_SOFT, 64, 64, 127},
    {"drive-fuzz", K_DRIVE, BR_DRIVE_FUZZ, 90, 40, 127},
    {"drive-hard", K_DRIVE, BR_DRIVE_HARD, 100, 100, 90},
    {"delay-digital", K_DELAY, BR_DELAY_DIGITAL, 40, 70, 110},
    {"delay-digital-long", K_DELAY, BR_DELAY_DIGITAL, 110, 80, 110},
    {"delay-tape", K_DELAY, BR_DELAY_TAPE, 40, 90, 110},
    {"delay-tape-long", K_DELAY, BR_DELAY_TAPE, 110, 100, 110},
    {"delay-multitap", K_DELAY, BR_DELAY_MULTITAP, 50, 80, 110},
    {"delay-multitap-long", K_DELAY, BR_DELAY_MULTITAP, 100, 110, 110},
    {"delay-glitch", K_DELAY, BR_DELAY_GLITCH, 30, 90, 110},
    {"delay-glitch-long", K_DELAY, BR_DELAY_GLITCH, 100, 100, 110},
    {"mod-chorus", K_MOD, BR_MOD_CHORUS, 64, 80, 100},
    {"mod-flange", K_MOD, BR_MOD_FLANGE, 40, 90, 100},
    {"mod-phaser", K_MOD, BR_MOD_PHASER, 60, 90, 100},
    {"mod-vibrato", K_MOD, BR_MOD_VIBRATO, 70, 60, 127},
    {"reverb-room", K_REVERB, BR_REVERB_ROOM, 80, 64, 100},
    {"reverb-hall", K_REVERB, BR_REVERB_HALL, 100, 90, 100},
    {"reverb-space", K_REVERB, BR_REVERB_SPACE, 90, 100, 100},
    /* Switched on part way, and off and on again: the idle stages must hand over cleanly. */
    {"toggle-delay-tape", K_DELAY, BR_DELAY_TAPE, 40, 90, 110, 93, 0},
    {"toggle-delay-multitap", K_DELAY, BR_DELAY_MULTITAP, 50, 80, 110, 61, 0},
    {"toggle-delay-off-on", K_DELAY, BR_DELAY_DIGITAL, 40, 70, 110, 1, 130},
    {"toggle-mod-chorus", K_MOD, BR_MOD_CHORUS, 64, 80, 100, 77, 0},
    {"toggle-mod-phaser", K_MOD, BR_MOD_PHASER, 60, 90, 100, 77, 0},
    {"toggle-mod-off-on", K_MOD, BR_MOD_FLANGE, 40, 90, 100, 1, 130},
    /* Off-centre pan, both sides and hard right. */
    {"pan-left-delay", K_DELAY, BR_DELAY_DIGITAL, 40, 70, 110, 0, 0, 20},
    {"pan-right-chorus", K_MOD, BR_MOD_CHORUS, 64, 80, 100, 0, 0, 100},
    {"pan-hard-drive", K_DRIVE, BR_DRIVE_SOFT, 64, 64, 127, 0, 0, 127},
    /* The master section: HEAT types (engine = type, a = amount, b = filter), and the compressor. */
    {"heat-bit", K_HEAT, BR_HEAT_BIT, 100, 64, 0},
    {"heat-bit-light", K_HEAT, BR_HEAT_BIT, 20, 40, 0},
    {"heat-sat", K_HEAT, BR_HEAT_SAT, 100, 90, 0},
    {"heat-wow", K_HEAT, BR_HEAT_WOW, 90, 64, 0},
    {"compressor", K_COMP, 0, 10, 5, 0},
};

static void set_send(const fx_case_t *k, uint32_t on)
{
    br_part[0].revision++;
    if (k->kind == K_DRIVE) br_part[0].sound.drive = on ? k->send : 0u;
    if (k->kind == K_DELAY) br_part[0].sound.delay = on ? k->send : 0u;
    if (k->kind == K_MOD) br_part[0].fx.mod_mix = on ? k->send : 0u;
    if (k->kind == K_REVERB) br_part[0].sound.reverb = on ? k->send : 0u;
    publish(0u);
}

static void configure(const fx_case_t *k)
{
    br_app_init(); fm1_ms = 1000u;
    for (uint32_t p = 0u; p < BR_PARTS; ++p) {
        br_part[p].revision++;
        br_part[p].sound.drive = br_part[p].sound.delay = br_part[p].sound.reverb = 0u;
        br_part[p].fx.mod_mix = 0u;
        publish(p);
    }
    br_part[0].revision++;
    switch (k->kind) {
    case K_DRIVE: br_part[0].fx.drive_mode = k->engine; br_part[0].fx.drive_gain = k->a;
                  br_part[0].fx.drive_tone = k->b; br_part[0].sound.drive = k->send; break;
    case K_DELAY: br_part[0].fx.delay_engine = k->engine; br_part[0].fx.delay_time = k->a;
                  br_part[0].fx.delay_feedback = k->b; br_part[0].sound.delay = k->send; break;
    case K_MOD:   br_part[0].fx.mod_engine = k->engine; br_part[0].fx.mod_rate = k->a;
                  br_part[0].fx.mod_depth = k->b; br_part[0].fx.mod_mix = k->send; break;
    case K_REVERB: br_part[0].fx.reverb_engine = k->engine; br_part[0].fx.reverb_tone = k->a;
                  br_part[0].sound.space = k->b; br_part[0].sound.reverb = k->send; break;
    case K_HEAT:  br_master.revision++;
                  br_master.fx.heat_type = k->engine; br_master.fx.heat_amount = k->a; br_master.fx.filter = k->b;
                  RING_PUBLISH(); br_master.revision++; break;
    case K_COMP:  br_master.revision++;
                  br_master.comp.source = BR_COMP_ALL; br_master.comp.threshold = k->a; br_master.comp.ratio = k->b;
                  br_master.comp.makeup = 6u; br_master.comp.mix = 127u;
                  RING_PUBLISH(); br_master.revision++; break;
    }
    if (k->pan) br_part[0].sound.pan = k->pan;
    publish(0u);
    if (k->on_at) {   /* another track keeps the stage running, as when it carries another send */
        br_part[0].revision++; br_part[0].sound.drive = 1u; publish(0u);
        set_send(k, 0u);
    }
}

int main(int argc, char **argv)
{
    static int32_t audio[BR_BLOCK * 2u];
    static const uint8_t notes[] = {48, 55, 60, 63, 67, 72};
    char path[512];
    assert(argc == 2);
    for (uint32_t c = 0u; c < sizeof cases / sizeof cases[0]; ++c) {
        FILE *f;
        configure(&cases[c]);
        snprintf(path, sizeof path, "%s/%s.raw", argv[1], cases[c].name);
        f = fopen(path, "wb"); assert(f);
        /* Six short notes, then a long tail: about 6 s. */
        for (uint32_t b = 0u; b < 1040u; ++b) {
            uint32_t slot = b / 40u, phase = b % 40u;
            if (slot < 6u && phase == 0u) br_engine_note_on(0u, notes[slot], 100u);
            if (slot < 6u && phase == 18u) br_engine_note_off(0u, notes[slot]);
            if (cases[c].on_at && b == cases[c].on_at) set_send(&cases[c], 1u);
            if (cases[c].off_at && b == cases[c].off_at) set_send(&cases[c], 0u);
            if (cases[c].off_at && b == cases[c].off_at + 50u) set_send(&cases[c], 1u);
            memset(audio, 0, sizeof audio);
            assert(app_audio(audio, BR_BLOCK) == FM1_AUDIO_RENDER_OK);
            fwrite(audio, sizeof audio, 1u, f);
            fm1_ms += 6u;
        }
        fclose(f);
        printf("%s\n", cases[c].name);
    }
    return 0;
}
