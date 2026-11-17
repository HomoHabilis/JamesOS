/* SPDX-License-Identifier: GPL-3.0-only */
/* Host tool: convert a v16 session (3,484 B, magic 0x42524431) to v17 using the
 * shared table in firmware/src/instrument_session_v17.h. Never touches hardware.
 *
 *   session_v16_to_v17 IN.v16 OUT.v17   convert; prints notes about v16 values with no v17 home
 *   session_v16_to_v17 --dump IN.v17     decode and print every field as name=value
 *
 * The v16 byte map is the one in instrument_session.c (br_session_t, 1b21ff4). */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "../firmware/src/instrument_session_v17.h"

#define V16_SIZE 3484u
#define PART(p) (8u + 58u * (p))
#define MOD(p) (240u + 165u * (p))

static uint8_t v16[V16_SIZE + 1u];
static int notes;
static void note(const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    printf("  note: "); vprintf(msg, ap); printf("\n"); ++notes;
    va_end(ap);
}
#define GSLOT(m) (offsetof(br_v17_global_t, m) / sizeof(int16_t))
#define MSLOT(m) (offsetof(br_v17_master_t, m) / sizeof(int16_t))

static void convert(br_session_v17_t *s)
{
    uint32_t p, i, lane, word = 0u;
    const uint8_t *b = v16;
    br_v17_defaults(s);
    /* GLOBAL */
    s->global.bpm = (int16_t)(b[4] + (b[967] & 0x80u ? 256 : 0));
    s->global.swing = b[PART(0) + 16];
    s->global.selected_track = b[5];
    s->global.octave = (int8_t)b[6];
    for (i = 0u; i < 15u; ++i) word |= (uint32_t)(b[2568 + i] >> 6) << (i * 2u);
    for (i = 0u; i < 7u; ++i) {
        uint32_t at = 15u + i / 2u;
        int16_t ch = (b[2568 + at] & (1u << (6u + i % 2u))) ? 0 : (int16_t)(((word >> (i * 4u)) & 15u) + 1u);
        s->global.v[GSLOT(midi_ch_t1) + i] = ch;
    }
    s->global.midi_clock_in = (int16_t)((word >> 28) & 1u);
    /* MASTER */
    s->master.filter = b[900];
    s->master.heat_type = b[901];
    s->master.heat_amount = b[902];
    s->master.resonance = b[905] == 0x52u && b[907] == 0xa7u ? b[906] : 0;
    s->master.env_depth = (int16_t)(b[960] - 128);
    s->master.smooth = (int16_t)(b[961] - 128);
    for (i = 0u; i < 8u; ++i) s->master.v[MSLOT(comp_source) + i] = b[952 + i];
    {   /* reverb engine id -> UI ordinal ROOM/HALL/SPACE; SPRING->ROOM, ETHEREAL->SPACE as br_unpack_module */
        static const int16_t ord[5] = { 0, 1, 0, 2, 2 };
        uint32_t e = b[MOD(0) + 25 + 6];
        s->master.reverb_type = e < 5u ? ord[e] : 0;
        s->master.reverb_size = b[PART(0) + 12];
        s->master.reverb_decay = b[MOD(0) + 25 + 7];
    }
    /* TRACKS */
    for (p = 0u; p < 4u; ++p) {
        const uint8_t *pt = b + PART(p), *m = b + MOD(p), *mc = b + 2824 + 5u * p;
        br_v17_track_t *t = &s->track[p];
        t->preset = pt[0]; t->timbre = pt[1]; t->morph = pt[2]; t->tune = (int8_t)pt[3]; t->level = pt[4];
        t->amp_attack = pt[5]; t->amp_decay = pt[6]; t->amp_sustain = pt[7]; t->amp_release = pt[8];
        t->drive_send = pt[9]; t->delay_send = pt[10]; t->reverb_send = pt[11];
        t->mute = pt[13] & 1u; t->filter = pt[13] >> 1;
        t->seq_length = pt[14]; t->seq_speed = pt[15];
        t->harmonics = b[908 + 2u * p]; t->osc_mix = b[909 + 2u * p]; t->pan = b[916 + p];
        t->resonance = mc[0]; t->keytrack = mc[1] ? 127 : 0;
        /* v16 64 = attenuverter neutral; v17 127 = +100% (all modulation passes). Other values copied. */
        t->timbre_mod = mc[2] == 64u ? 127 : mc[2];
        t->pitch_mod = mc[3] == 64u ? 127 : mc[3];
        t->morph_mod = mc[4] == 64u ? 127 : mc[4];
        t->menv_attack = m[1]; t->menv_decay = m[2]; t->menv_sustain = m[3]; t->menv_release = m[4];
        t->menv_amount = (int8_t)m[5]; t->menv_target = m[6]; t->menv_loop = m[7]; t->menv_retrigger = m[8];
        t->lfo1_wave = m[13]; t->lfo1_rate = m[14]; t->lfo1_depth = m[15]; t->lfo1_target = m[16];
        t->lfo2_wave = m[17]; t->lfo2_rate = m[18]; t->lfo2_depth = m[19]; t->lfo2_target = m[20];
        t->drive_mode = m[25]; t->drive_gain = m[26]; t->drive_tone = m[27];
        t->delay_engine = m[28]; t->delay_time = m[29]; t->delay_feedback = m[30];
        t->mod_engine = m[33]; t->mod_rate = m[34]; t->mod_depth = m[35]; t->mod_send = m[36];
        for (i = 0u; i < 8u; ++i) {
            t->route_source[i] = m[37 + 4 * i]; t->route_target[i] = m[38 + 4 * i];
            t->route_amount[i] = (int8_t)m[39 + 4 * i]; t->route_enabled[i] = m[40 + 4 * i];
        }
        t->rec_quantize = (b[965 + p / 2u] >> (4u * (p & 1u))) & 15u;
        if (p < 3u && pt[18] == 0x41u) {
            t->arp_mode = pt[19]; t->arp_rate = pt[20]; t->arp_octaves = (int16_t)(pt[21] + 1);
            t->arp_gate = pt[22]; t->arp_latch = pt[23]; t->arp_follow_swing = pt[24];
            t->arp_repeat = (int16_t)(pt[25] + 1);
        }
        /* values with no v17 home: report when they carry information */
        if (p && pt[16] != b[PART(0) + 16]) note("track %u swing %u differs from track 1 (global swing kept)", p + 1u, pt[16]);
        if (p && pt[12] != b[PART(0) + 12]) note("track %u reverb size copy %u dropped (global from track 1)", p + 1u, pt[12]);
        if (p && m[31] != b[MOD(0) + 31]) note("track %u reverb type copy %u dropped (global from track 1)", p + 1u, m[31]);
        if (p && m[32] != b[MOD(0) + 32]) note("track %u reverb decay copy %u dropped (global from track 1)", p + 1u, m[32]);
        if (p != 3u && m[0] != m[0 + MOD(3) - MOD(p)]) note("track %u drum snap copy %u dropped", p + 1u, m[0]);
        if (m[9] || m[10] || m[11] || m[12]) note("track %u scale/root/quantize/transpose non-zero (dropped), first %u", p + 1u, m[9]);
        if (m[21] || m[22] || m[23] || m[24]) note("track %u retired LFO3 row non-zero (dropped) %u", p + 1u, m[21]);
    }
    /* KIT */
    for (lane = 0u; lane < 4u; ++lane) {
        uint8_t ll = b[PART(3) + 18 + lane];
        s->kit.voice_choice[lane] = (b[964] >> (2u * lane)) & 3u;
        s->kit.lane_level[lane] = ll & 0x7fu;
        s->kit.lane_mute[lane] = ll >> 7;
    }
    s->kit.drum_snap = b[MOD(3) + 0];
    {
        const uint8_t *pc = b + PART(3) + 22;
        if (pc[0] == 0x50u && pc[3] < 0x80u) {
            uint32_t v = (uint32_t)pc[2] | (uint32_t)pc[3] << 8;
            s->kit.punch_loop_len = pc[1] & 3u; s->kit.punch_echo_time = (pc[1] >> 2) & 3u;
            s->kit.punch_filter_shape = (pc[1] >> 4) & 3u; s->kit.punch_trem_rate = (pc[1] >> 6) & 3u;
            s->kit.punch_echo_feedback = v & 31u; s->kit.punch_filter_amount = (v >> 5) & 31u;
            s->kit.punch_trem_depth = (v >> 10) & 31u;
        }
    }
    /* PATTERN */
    for (p = 0u; p < 4u; ++p)
        for (i = 0u; i < 64u; ++i) {
            const uint8_t *src = i < 16u ? b + PART(p) + 26 + 2 * i : b + MOD(p) + 69 + 2 * (i - 16u);
            uint8_t *dst = s->pattern + (p < 3u ? BR_V17_PAT_step_synth + 128u * p : BR_V17_PAT_step_kit) + 2u * i;
            dst[0] = src[0]; dst[1] = src[1];
        }
    memcpy(s->pattern + BR_V17_PAT_synth_len_micro, b + 968, 384);
    memcpy(s->pattern + BR_V17_PAT_synth_condition, b + 1352, 192);
    memcpy(s->pattern + BR_V17_PAT_synth_note, b + 2844, 192);
    memcpy(s->pattern + BR_V17_PAT_synth_velocity, b + 3036, 192);
    memcpy(s->pattern + BR_V17_PAT_kit_len_micro, b + 1544, 512);
    memcpy(s->pattern + BR_V17_PAT_kit_condition, b + 2056, 256);
    memcpy(s->pattern + BR_V17_PAT_kit_velocity, b + 2312, 256);
    memcpy(s->pattern + BR_V17_PAT_kit_pitch, b + 3228, 256);
    for (i = 0u; i < 256u; ++i) s->pattern[BR_V17_PAT_kit_retrig + i] = b[2568 + i] & 0x3fu; /* MIDI bits moved to GLOBAL */
    /* other v16 bytes with no home */
    for (p = 0u; p < 4u; ++p) {
        if (b[PART(p) + 17] != 80u) note("track %u part.gate %u dropped (never read)", p + 1u, b[PART(p) + 17]);
        for (lane = 0u; lane < 8u; ++lane)
            if (b[920 + 8u * p + lane] != 128u) note("track %u lane_gate %u dropped (audio forces 128)", p + 1u, b[920 + 8u * p + lane]);
    }
    if (b[903]) note("master_fx[3] %u dropped", b[903]);
    if (b[904]) note("select_target %u dropped (restore forces MASTER_FILTER)", b[904]);
}

static void dump(const br_session_v17_t *s)
{
    uint32_t sec, rep, i, k, b;
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        for (rep = 0u; rep < d->repeat; ++rep) {
            const int16_t *v = br_v17_section_values((br_session_v17_t *)(uintptr_t)s, sec, rep);
            uint32_t slot = 0u;
            for (i = 0u; i < d->rows; ++i) {
                const br_v17_row_t *row = &br_v17_rows[d->first_row + i];
                for (k = 0u; k < row->count; ++k, ++slot) {
                    if (row->range == BR_V17_R_SPARE) continue;
                    if (d->repeat > 1u) printf("track%u.", (unsigned)(rep + 1u));
                    else printf("%s.", sec == 0u ? "global" : sec == 1u ? "master" : "kit");
                    if (row->count > 1u) printf("%s[%u]=%d\n", br_v17_row_names[d->first_row + i], (unsigned)k, v[slot]);
                    else printf("%s=%d\n", br_v17_row_names[d->first_row + i], v[slot]);
                }
            }
        }
    }
    for (b = 0u, k = 0u; b < BR_V17_PATTERN_BLOCK_COUNT; ++b) {
        uint32_t sum = 0u;
        for (i = 0u; i < br_v17_pattern_blocks[b].bytes; ++i) sum = sum * 31u + s->pattern[k + i];
        printf("pattern.%s.hash=%08x\n", br_v17_pattern_names[b], (unsigned)sum);
        k += br_v17_pattern_blocks[b].bytes;
    }
}

int main(int argc, char **argv)
{
    static br_session_v17_t s;
    static uint8_t out[BR_V17_TOTAL_LEN + 16u];
    FILE *f;
    size_t n;
    uint32_t clamps = 0u;
    if (argc == 3 && !strcmp(argv[1], "--dump")) {
        int rc;
        if (!(f = fopen(argv[2], "rb"))) { perror(argv[2]); return 2; }
        n = fread(out, 1, sizeof out, f); fclose(f);
        rc = br_v17_decode(out, (uint32_t)n, &s, &clamps);
        if (rc) { fprintf(stderr, "%s: not a v17 session (error %d)\n", argv[2], rc); return 1; }
        printf("clamps=%u\n", (unsigned)clamps);
        dump(&s);
        return 0;
    }
    if (argc != 3) { fprintf(stderr, "usage: %s IN.v16 OUT.v17 | --dump IN.v17\n", argv[0]); return 2; }
    if (!(f = fopen(argv[1], "rb"))) { perror(argv[1]); return 2; }
    n = fread(v16, 1, sizeof v16, f); fclose(f);
    if (n != V16_SIZE || v16[0] != 0x31u || v16[1] != 0x44u || v16[2] != 0x52u || v16[3] != 0x42u || v16[7] != 16u) {
        fprintf(stderr, "%s: not a v16 session (size %u, version %u)\n", argv[1], (unsigned)n, n > 7u ? v16[7] : 0u);
        return 1;
    }
    printf("converting %s\n", argv[1]);
    convert(&s);
    if (br_v17_encode(&s, out, sizeof out, &clamps) != BR_V17_TOTAL_LEN) { fprintf(stderr, "encode failed\n"); return 1; }
    /* This legacy conversion tool promises v17 output. The v18 encoder
     * writes no packed patch bits here because convert() leaves them zero. */
    br_v17_wr(out + 4, BR_V17_LEGACY_VERSION, 2u);
    if (clamps) { fprintf(stderr, "%s: %u values out of v17 range (clamped); refusing\n", argv[1], (unsigned)clamps); return 1; }
    if (!(f = fopen(argv[2], "wb")) || fwrite(out, 1, BR_V17_TOTAL_LEN, f) != BR_V17_TOTAL_LEN) { perror(argv[2]); return 2; }
    fclose(f);
    printf("  wrote %s (%u B, %d notes)\n", argv[2], (unsigned)BR_V17_TOTAL_LEN, notes);
    return 0;
}
