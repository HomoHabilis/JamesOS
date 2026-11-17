/* SPDX-License-Identifier: GPL-3.0-only */
/* Session v17 wiring on the real app (host; never touches hardware):
 *  - HOME ENV DEPTH and KEY TRACK amount in the engine;
 *  - every saved setting set to a non-default value, through the UI's own knob
 *    paths where one exists (discovered by turning every knob of every screen
 *    on every track), the rest directly; capture, clear, restore, compare;
 *  - 10,000 random in-range states round-trip;
 *  - garbage, truncated, old-format and wrong-role records are rejected and
 *    leave the running state untouched;
 *  - fixture files given on the command line restore and capture back
 *    byte-identically. */
#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(c) do { if (!(c)) { if (failures < 40) printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

/* ---- table walking ---- */
typedef struct { uint16_t row, rep, k, slot; } field_t;
static field_t fields[1024];
static uint32_t nfields;
static void index_fields(void)
{
    uint32_t sec, rep, i, k;
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        for (rep = 0u; rep < d->repeat; ++rep) {
            uint32_t slot = 0u;
            for (i = d->first_row; i < (uint32_t)d->first_row + d->rows; ++i)
                for (k = 0u; k < br_v17_rows[i].count; ++k, ++slot)
                    if (br_v17_rows[i].range != BR_V17_R_SPARE)
                        fields[nfields++] = (field_t){(uint16_t)i, (uint16_t)rep, (uint16_t)k, (uint16_t)slot};
        }
    }
}
static const br_v17_range_t *range_of(const field_t *f) { return &br_v17_ranges[br_v17_rows[f->row].range]; }
static int32_t def_of(const field_t *f)
{
    uint32_t o; int32_t v = br_v17_rows[f->row].def;
    if (f->row >= BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT &&
        f->row < BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT + BR_V17_TRACK_ROW_COUNT && f->rep == BR_V17_KIT_TRACK)
        for (o = 0u; o < BR_V17_KIT_DEFAULT_COUNT; ++o) if (br_v17_kit_defaults[o].slot == f->slot) v = br_v17_kit_defaults[o].value;
    return v;
}
static int is_preset(const field_t *f) { return f->row == BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT; }
static int32_t value_of(const field_t *f) { return br_v17_get(f->row, f->rep, f->k); }
static void values(int32_t *v) { uint32_t i; for (i = 0u; i < nfields; ++i) v[i] = value_of(&fields[i]); }
#define TN_S(n, r, d) #n,
#define TN_A(n, c, r, d) #n,
static const char *const row_names[BR_V17_ROW_COUNT] = {
    BR_V17_GLOBAL_ROWS(TN_S, TN_A) BR_V17_MASTER_ROWS(TN_S, TN_A) BR_V17_TRACK_ROWS(TN_S, TN_A) BR_V17_KIT_ROWS(TN_S, TN_A)
};
static const char *name_of(const field_t *f)
{
    static char b[80];
    uint32_t t = f->row >= BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT &&
        f->row < BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT + BR_V17_TRACK_ROW_COUNT;
    if (br_v17_rows[f->row].count > 1u) snprintf(b, sizeof b, "%s%s%u%s[%u]", t ? "track" : "", "", t ? f->rep + 1u : 0u, row_names[f->row], f->k);
    else snprintf(b, sizeof b, "%s%u%s%s", t ? "track" : "", t ? f->rep + 1u : 0u, t ? "." : "", row_names[f->row]);
    return b;
}

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static int32_t rnd_in(int32_t lo, int32_t hi) { return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1)); }
static int32_t random_value(const field_t *f)
{
    const br_v17_range_t *r = range_of(f);
    if (is_preset(f)) return f->rep < BR_SYNTH_PARTS ? rnd_in(BR_FIRST_PLAITS, BR_FIRST_PLAITS + 23) :
        (int32_t)((const uint8_t[]){BR_KIT_BRAIDS, BR_KIT_PLAITS})[rnd() & 1u];
    if (f->row == BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT + BR_V17_TROW_route_source) {
        int32_t v = rnd_in(r->min, r->max);
        return v == BR_SOURCE_LFO3 ? BR_SOURCE_OFF : v;   /* retired; restore maps it to OFF */
    }
    return rnd_in(r->min, r->max);
}

/* ---- engine ---- */
static uint32_t filter_with(uint32_t base, int32_t depth, uint32_t env, uint32_t target, int32_t amount)
{
    br_mod_state_t st; br_mod_config_t m; br_fx_config_t fx, fx_out; br_sound_t s, out; int32_t pitch = 0;
    memset(&st, 0, sizeof st);
    br_module_defaults(&m, &fx);
    br_preset_sound(BR_FIRST_PLAITS, &s);
    s.filter = base; s.filter_env_depth = depth;
    m.target = target; m.amount = amount;
    st.env = env;
    br_mod_effective(&st, &m, &s, &fx, 0, &out, &fx_out, &pitch);
    return out.filter;
}
static void engine_checks(void)
{
    br_mod_config_t m; br_fx_config_t fx; br_sound_t s;
    CHECK(filter_with(30u, 0, 32767u, BR_TARGET_PITCH, 0) == 30u);
    CHECK(filter_with(30u, 127, 32767u, BR_TARGET_PITCH, 0) == 127u);      /* crosses open and clamps */
    CHECK(filter_with(30u, -127, 32767u, BR_TARGET_PITCH, 0) == 0u);
    CHECK(filter_with(30u, 32, 16384u, BR_TARGET_PITCH, 0) > 30u);
    CHECK(filter_with(30u, 32, 0u, BR_TARGET_PITCH, 0) == 30u);           /* follows the envelope */
    CHECK(filter_with(64u, 127, 32767u, BR_TARGET_PITCH, 0) == 127u);     /* open sweeps into HP */

    CHECK(filter_with(100u, 127, 32767u, BR_TARGET_PITCH, 0) == 127u);    /* high-pass stays high-pass */
    CHECK(filter_with(20u, 20, 32767u, BR_TARGET_FILTER, 20) > filter_with(20u, 20, 32767u, BR_TARGET_PITCH, 20)); /* adds */
    br_module_defaults(&m, &fx); br_preset_sound(BR_FIRST_PLAITS, &s);
    CHECK(!br_mod_has_active_target(&m, &s));
    s.filter_env_depth = -5; CHECK(br_mod_has_active_target(&m, &s));
    CHECK(br_filter_keytrack(30u, 60u, 127u) == 30u);
    CHECK(br_filter_keytrack(30u, 72u, 127u) > br_filter_keytrack(30u, 72u, 64u));
    CHECK(br_filter_keytrack(30u, 72u, 64u) > 30u);
    CHECK(br_filter_keytrack(30u, 72u, 1u) <= br_filter_keytrack(30u, 72u, 64u));
}

/* ---- every setting, through the UI where it has a knob ---- */
static void tick(void) { ++fm1_ms; br_app_tick(); }
static void tap(uint32_t button)
{
    fm1_in.buttons |= 1u << button; host_pressed |= 1u << button; tick();
    fm1_in.buttons &= ~(1u << button); host_released |= 1u << button; tick();
}
static uint8_t reached[1024];
static int32_t va[1024], vb[1024];
static br_session_t sa, sb, s0;

static void sweep_knobs(void)
{
    static const uint32_t pages[] = {BR_PAGE_HOME, BR_PAGE_ENV, BR_PAGE_FX, BR_PAGE_MIX, BR_PAGE_SEQ,
                                     BR_PAGE_PATTERN, BR_PAGE_LFO, BR_PAGE_ROUTE, BR_PAGE_PUNCH};
    uint32_t p, g, sc, k, i, n, lane;
    for (p = 0u; p < BR_PARTS; ++p)
        for (lane = 0u; lane < (p == BR_DRUM_PART ? BR_DRUM_LANES : 1u); ++lane)
        for (g = 0u; g < sizeof pages / sizeof pages[0]; ++g) {
            br_ui.part = p; br_ui.page = pages[g];
            n = br_screen_count();
            if (pages[g] == BR_PAGE_ROUTE && p == BR_DRUM_PART) n = 4u;   /* screen 3: the kit's GATE page */
            for (sc = 0u; sc < n; ++sc)
                for (k = 0u; k < 4u; ++k) {
                    int32_t d;
                    br_ui.part = p; br_ui.page = pages[g]; br_ui.screen[pages[g]] = sc; br_ui.lane = lane;
                    br_ui.step_press_active = 0u; br_ui.step_edit_active = 0u;
                    for (d = 0; d < 2; ++d) {
                        uint32_t moved = 0u;
                        values(va);
                        br_control_turn(k, d ? -2 : 3);
                        values(vb);
                        for (i = 0u; i < nfields; ++i)
                            if (va[i] != vb[i] && vb[i] != def_of(&fields[i])) { reached[i] = 1u; moved = 1u; }
                        if (moved) break;
                    }
                }
        }
    br_ui.page = BR_PAGE_HOME; br_ui.part = 0u;
}

static void every_setting(void)
{
    uint32_t i, direct = 0u, by_ui = 0u, p;
    br_app_init(); fm1_ms = 3000u;
    /* Sounds come from the browser; it rewrites the whole sound, so presets go first. */
    for (p = 0u; p < BR_PARTS; ++p) br_v17_set(BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT, p, 0u,
                                                p < BR_SYNTH_PARTS ? BR_FIRST_PLAITS + 3 + (int32_t)p : BR_KIT_PLAITS);
    for (i = 0u; i < nfields; ++i) if (is_preset(&fields[i])) reached[i] = 2u;
    /* Buttons: the mixer's OCT- mutes the selected track, OCT+ raises the octave on HOME,
     * ARP held 0.8 s turns the arpeggiator on. */
    for (p = 0u; p < BR_PARTS; ++p) {
        br_ui.page = BR_PAGE_MIX; br_ui.screen[BR_PAGE_MIX] = 0u; br_ui.part = p; tap(BTN_OCT_DOWN);
        CHECK(br_part[p].sound.mute == 1u);
    }
    br_ui.page = BR_PAGE_HOME; br_ui.part = 0u; tap(BTN_OCT_UP);
    fm1_in.buttons |= 1u << BTN_ARP; host_pressed |= 1u << BTN_ARP;
    for (i = 0u; i < 900u; ++i) tick();
    fm1_in.buttons &= ~(1u << BTN_ARP); host_released |= 1u << BTN_ARP; tick();
    CHECK(br_arp_on == 1u);
    sweep_knobs();
    for (i = 0u; i < nfields; ++i) {
        const field_t *f = &fields[i];
        int32_t v = value_of(f);
        if (reached[i] == 1u) { ++by_ui; continue; }
        if (v != def_of(f) && reached[i] != 2u) { reached[i] = 1u; ++by_ui; continue; }   /* a button above */
        if (reached[i] == 2u) continue;
        {   /* no knob path: set directly, to a value other than the default */
            const br_v17_range_t *r = range_of(f);
            int32_t want = def_of(f) == r->max ? r->min : def_of(f) + 1;
            if (f->row == BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT - 1u) {}
            br_v17_set(f->row, f->rep, f->k, want);
            ++direct;
        }
    }
    /* Anything still at its default (a knob that wrapped back) is set directly too. */
    for (i = 0u; i < nfields; ++i)
        if (value_of(&fields[i]) == def_of(&fields[i])) {
            const br_v17_range_t *r = range_of(&fields[i]);
            br_v17_set(fields[i].row, fields[i].rep, fields[i].k, def_of(&fields[i]) == r->max ? r->min : def_of(&fields[i]) + 1);
        }
    values(va);
    for (i = 0u; i < nfields; ++i) CHECK(va[i] != def_of(&fields[i]) || is_preset(&fields[i]));
    br_master.reverb_manual = 0x35u;                          /* synced pre-delay, mod, width */
    br_smk_fader_link = 1u;                                   /* SMK-37 Pro fader link */
    br_master.reverb_extra[BR_REVERB_X_PREDELAY] = 4u;
    br_part[BR_DRUM_PART].sound.gate = 55u; br_arp_on = 1u;
    br_part[2].sound.filter_env_depth = -77;
    br_ui.seq_default[1][0].note = 41u; br_ui.seq_default[BR_DRUM_PART][2].velocity = 33u;
    /* Some steps, so the pattern section carries data. */
    br_part[0].step[3] = BR_NOTE_STEP(64u, 90u) | BR_STEP_TIE; br_part[BR_DRUM_PART].step[5] = 0x0305u;
    br_seq_synth_micro[0][3] = -5; br_seq_kit_retrig[5][1] = 0x2bu; br_seq_kit_pitch[5][0] = 50u;
    values(va);
    CHECK(br_capture(&sa));
    CHECK(br_v17_clamps == 0u);
    br_app_init();
    values(vb);
    { uint32_t differ = 0u; for (i = 0u; i < nfields; ++i) differ += va[i] != vb[i]; CHECK(differ > nfields * 9u / 10u); }
    CHECK(br_restore(&sa));
    values(vb);
    for (i = 0u; i < nfields; ++i)
        if (va[i] != vb[i]) { printf("  differs after restore: %s %d -> %d\n", name_of(&fields[i]), va[i], vb[i]); ++failures; }
    CHECK(br_part[BR_DRUM_PART].sound.gate == 55u && br_arp_on == 1u && br_part[2].sound.filter_env_depth == -77);
    CHECK(br_master.reverb_manual == 0x35u && br_master.reverb_extra[BR_REVERB_X_PREDELAY] == 4u);
    CHECK(br_smk_fader_link == 1u);
    CHECK(br_ui.seq_default[1][0].note == 41u && br_ui.seq_default[BR_DRUM_PART][2].velocity == 33u);
    CHECK(br_part[0].step[3] == (BR_NOTE_STEP(64u, 90u) | BR_STEP_TIE) && br_part[BR_DRUM_PART].step[5] == 0x0305u);
    CHECK(br_seq_synth_micro[0][3] == -5 && br_seq_kit_retrig[5][1] == 0x2bu && br_seq_kit_pitch[5][0] == 50u);
    CHECK(br_capture(&sb) && !memcmp(&sa, &sb, sizeof sa));
    printf("every setting: %u saved settings, %u reached through a knob or button, %u presets via the browser path, %u set directly (no UI path):\n",
           nfields, by_ui, BR_PARTS, direct);
    for (i = 0u; i < nfields; ++i)
        if (!reached[i] && (fields[i].rep == 0u || fields[i].row < BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT ||
                            fields[i].row >= BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT + BR_V17_TRACK_ROW_COUNT))
            printf("    direct: %s\n", name_of(&fields[i]));
}

/* ---- random states ---- */
static void fuzz(uint32_t rounds)
{
    uint32_t r, i;
    br_app_init();
    for (r = 0u; r < rounds; ++r) {
        for (i = 0u; i < nfields; ++i) br_v17_set(fields[i].row, fields[i].rep, fields[i].k, random_value(&fields[i]));
        for (i = 0u; i < BR_STEPS; ++i) {
            uint32_t p = rnd() % BR_PARTS;
            br_part[p].step[i] = p == BR_DRUM_PART ? (rnd() & 0x0f0fu) :
                rnd() & 1u ? BR_NOTE_STEP(rnd() & 127u, rnd() & 127u) | (rnd() & 1u ? BR_STEP_TIE : 0u) : 0u;
            br_seq_synth_length[rnd() % 3u][i] = (uint16_t)(rnd() % 1025u);
            br_seq_synth_micro[rnd() % 3u][i] = (int8_t)rnd_in(-16, 15);
            br_seq_kit_retrig[i][rnd() & 3u] = (uint8_t)(rnd() & 63u);
            br_seq_kit_pitch[i][rnd() & 3u] = rnd() & 1u ? 255u : (uint8_t)rnd_in(36, 84);
            br_seq_synth_note[rnd() % 3u][i] = rnd() & 1u ? 255u : (uint8_t)(rnd() & 127u);
            br_seq_kit_condition[i][rnd() & 3u] = (uint8_t)(rnd() % 145u);
        }
        values(va);
        if (!br_capture(&sa) || br_v17_clamps) { CHECK(0); break; }
        if (!br_restore(&sa)) { CHECK(0); break; }
        values(vb);
        if (memcmp(va, vb, nfields * sizeof va[0])) {
            for (i = 0u; i < nfields; ++i) if (va[i] != vb[i]) printf("  fuzz: %s %d -> %d\n", name_of(&fields[i]), va[i], vb[i]);
            CHECK(!"fuzz values"); break;
        }
        if (!br_capture(&sb) || memcmp(&sa, &sb, sizeof sa)) { CHECK(!"fuzz bytes"); break; }
    }
    printf("fuzz: %u random in-range states round-trip\n", r);
}

/* ---- rejected records change nothing ---- */
static void reject(const uint8_t *b, uint32_t len, const char *what)
{
    if (br_restore_bytes(b, len)) { printf("FAIL accepted: %s\n", what); ++failures; CHECK(br_restore(&s0)); return; }
    CHECK(br_capture(&sb));
    if (memcmp(&s0, &sb, sizeof s0)) { printf("FAIL state changed by rejected record: %s\n", what); ++failures; }
}
static void garbage(const char *v16_path)
{
    static uint8_t b[sizeof(br_session_t) + 64];
    uint32_t i, r, track_hdr;
    br_app_init();
    for (i = 0u; i < nfields; ++i) br_v17_set(fields[i].row, fields[i].rep, fields[i].k, random_value(&fields[i]));
    CHECK(br_capture(&s0));
    for (r = 0u; r < 2000u; ++r) { for (i = 0u; i < sizeof b; ++i) b[i] = (uint8_t)rnd(); reject(b, sizeof(br_session_t), "random bytes"); }
    memcpy(b, &s0, sizeof s0);
    for (r = 0u; r < 64u; ++r) reject(b, BR_V17_TOTAL_LEN - 1u - rnd() % (BR_V17_TOTAL_LEN - 1u), "truncated");
    b[0] ^= 1u; reject(b, sizeof s0, "magic"); b[0] ^= 1u;
    b[4] = 16u; reject(b, sizeof s0, "version 16"); b[4] = BR_V17_VERSION;
    b[6] = 11u; reject(b, sizeof s0, "header length"); b[6] = 12u;
    track_hdr = BR_V17_HEADER_LEN + 2u * BR_V17_SECTION_HEADER_LEN + BR_V17_GLOBAL_BYTES + BR_V17_MASTER_BYTES;
    b[track_hdr + 2] ^= 1u; reject(b, sizeof s0, "section length"); b[track_hdr + 2] ^= 1u;
    b[track_hdr] = BR_V17_SEC_GLOBAL; reject(b, sizeof s0, "duplicate section"); b[track_hdr] = BR_V17_SEC_TRACKS;
    b[track_hdr + 4] = BR_KIT_808; reject(b, sizeof s0, "kit preset on a synth track");
    b[track_hdr + 4 + 3u * BR_V17_TRACK_BYTES] = BR_FIRST_PLAITS; reject(b, sizeof s0, "synth preset on the kit track");
    if (v16_path) {
        FILE *f = fopen(v16_path, "rb");
        if (f) { memset(b, 0, sizeof b); i = (uint32_t)fread(b, 1, sizeof b, f); fclose(f); reject(b, sizeof(br_session_t), "a v16 session"); (void)i; }
    }
    CHECK(br_restore(&s0));
    printf("rejects: random, truncated, magic, version, header, section, duplicate, preset roles%s leave the state untouched\n",
           v16_path ? ", v16" : "");
}

static void fixture(const char *path)
{
    static br_session_t in, out, again;
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) { printf("FAIL %s: cannot open\n", path); ++failures; return; }
    memset(&in, 0, sizeof in);
    n = fread(&in, 1, sizeof in, f); fclose(f);
    if (n != sizeof in || !br_restore(&in) || !br_capture(&out)) { printf("FAIL %s: restore/capture\n", path); ++failures; return; }
    if (br_v17_rd16(in.b + 4u) < BR_V17_VERSION) {
        if (br_v17_rd16(in.b + 4u) == BR_V17_LEGACY_VERSION)
            for (uint32_t p=0; p<BR_PARTS; ++p) for (uint32_t slot=0; slot<2u; ++slot)
                CHECK(!memcmp((const void *)&br_part[p].mod.route[BR_ROUTES+slot],
                              (const void *)&br_master.global_route[slot], sizeof(br_route_config_t)));
        CHECK(br_v17_rd16(out.b + 4u)==BR_V17_VERSION);
        CHECK(br_restore(&out) && br_capture(&again) && !memcmp(&out,&again,sizeof out));
        printf("ok %s: migrated legacy session and stable current capture\n",path);
    } else {
        if (memcmp(&in, &out, sizeof in)) { printf("FAIL %s: capture differs\n", path); ++failures; return; }
        printf("ok %s: restores and captures byte-identically\n", path);
    }
}

int main(int argc, char **argv)
{
    int i;
    const char *v16 = getenv("SESSION_WIRE_V16");
    index_fields();
    br_app_init();
    {
        static br_session_t current;
        for (uint32_t p=0; p<BR_SYNTH_PARTS; ++p) {
            br_part[p].sound.portamento=p*50u; br_part[p].sound.glide_mode=p; br_part[p].sound.amp_mode=p&1u; br_part[p].sound.lpg_gate=p*15u;
        }
        CHECK(br_capture(&current));
        for (uint32_t p=0; p<BR_SYNTH_PARTS; ++p)
            br_part[p].sound.portamento=br_part[p].sound.glide_mode=br_part[p].sound.amp_mode=br_part[p].sound.lpg_gate=0u;
        CHECK(br_restore(&current));
        for (uint32_t p=0; p<BR_SYNTH_PARTS; ++p)
            CHECK(br_part[p].sound.portamento==p*50u && br_part[p].sound.glide_mode==p && br_part[p].sound.amp_mode==(p&1u) && br_part[p].sound.lpg_gate==p*15u);
        for (uint32_t p=0; p<BR_SYNTH_PARTS; ++p) br_part[p].sound.lpg_gate=0u;
        br_part[0].sound.timbre_mod=33u;
        CHECK(br_capture(&current)); current.b[4]=BR_V19_VERSION;
        br_part[0].sound.lpg_gate=31u; br_part[0].sound.timbre_mod=0u; br_part[1].sound.portamento=0u;
        CHECK(br_restore(&current));
        CHECK(br_part[0].sound.lpg_gate==0u && br_part[0].sound.timbre_mod==33u && br_part[1].sound.portamento==50u);
        br_app_init();
    }
    {
        static br_session_t old;
        br_part[0].sound.timbre_mod = 127u; br_part[0].sound.fm_mod = 96u;
        br_part[0].sound.morph_mod = 0u; br_part[0].sound.harmonics_mod = 128u;
        br_part[0].mod.route[BR_ROUTES] = (br_route_config_t){8u, 2u, -52, 1u};
        CHECK(br_capture(&old)); old.b[4] = BR_V18_VERSION;
        CHECK(br_restore(&old));
        CHECK(br_part[0].sound.timbre_mod == 64u && br_part[0].sound.fm_mod == 33u &&
              br_part[0].sound.morph_mod == 0u && br_part[0].sound.harmonics_mod == 65u);
        CHECK(br_part[0].mod.route[BR_ROUTES].source == 8u &&
              br_part[0].mod.route[BR_ROUTES].amount == -52 && br_part[0].mod.route[BR_ROUTES].enabled);
        br_app_init();
    }
    engine_checks();
    every_setting();
    fuzz(10000u);
    garbage(v16);
    br_app_init();
    for (i = 1; i < argc; ++i) fixture(argv[i]);
    if (!failures) puts("session wire: all checks passed");
    return failures != 0;
}
