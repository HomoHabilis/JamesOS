/* SPDX-License-Identifier: GPL-3.0-only */
/* Host tests for the table-driven session v17 codec. Never touches hardware.
 *   test_session_v17          run all checks
 *   test_session_v17 --doc    print docs/SESSION-V17.md generated from the table */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "instrument_session_v17.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { ++failures; if (failures < 40) { \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } } while (0)

static uint32_t rng_state;
static uint32_t rnd(void) { uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }
static int32_t rnd_in(int32_t lo, int32_t hi) { return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1)); }

/* ---- table walking helpers ---- */
typedef struct { uint32_t sec, rep, row, elem, slot, wire; } field_ref_t; /* wire = absolute offset */
#define MAX_FIELDS 1024
static field_ref_t fields[MAX_FIELDS];
static uint32_t field_count, setting_count;
static uint32_t section_payload_at[6]; /* absolute payload offset by section id */

static void index_fields(void)
{
    uint32_t sec, rep, i, k, at = BR_V17_HEADER_LEN;
    field_count = setting_count = 0u;
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        at += BR_V17_SECTION_HEADER_LEN;
        section_payload_at[d->id] = at;
        for (rep = 0u; rep < d->repeat; ++rep) {
            uint32_t slot = 0u;
            for (i = 0u; i < d->rows; ++i) {
                const br_v17_row_t *row = &br_v17_rows[d->first_row + i];
                uint32_t n = br_v17_range_size(&br_v17_ranges[row->range]);
                for (k = 0u; k < row->count; ++k, ++slot, at += n) {
                    field_ref_t f = { sec, rep, d->first_row + i, k, slot, at };
                    fields[field_count++] = f;
                    if (row->range != BR_V17_R_SPARE) ++setting_count;
                }
            }
        }
    }
    at += BR_V17_SECTION_HEADER_LEN;
    section_payload_at[BR_V17_SEC_PATTERN] = at;
}

static int16_t *slot_ptr(br_session_v17_t *s, const field_ref_t *f) { return br_v17_section_values(s, f->sec, f->rep) + f->slot; }
static const br_v17_range_t *range_of(const field_ref_t *f) { return &br_v17_ranges[br_v17_rows[f->row].range]; }
static uint32_t size_of(const field_ref_t *f) { return br_v17_range_size(range_of(f)); }
static int is_spare(const field_ref_t *f) { return br_v17_rows[f->row].range == BR_V17_R_SPARE; }

static void random_pattern(uint8_t *p)
{
    uint32_t b, at = 0u;
    for (b = 0u; b < BR_V17_PATTERN_BLOCK_COUNT; ++b) {
        const br_v17_pattern_block_t *k = &br_v17_pattern_blocks[b];
        uint32_t end = at + k->bytes;
        for (; at < end; at += k->elem) {
            uint32_t w;
            switch (k->rule) {
            case BR_V17_PR_SYNTH_STEP: w = (uint32_t)rnd_in(0, 128) | (rnd() & 255u) << 8; break;
            case BR_V17_PR_KIT_STEP: w = rnd() & 0x0f0fu; break;
            case BR_V17_PR_LEN_MICRO: w = (uint32_t)rnd_in(0, 1024) | (rnd() & 31u) << 11; break;
            case BR_V17_PR_COND: w = (uint32_t)rnd_in(0, 144); break;
            case BR_V17_PR_NOTE_OR_NONE: w = rnd() & 1u ? 255u : (uint32_t)rnd_in(0, 127); break;
            case BR_V17_PR_VELOCITY: w = (uint32_t)rnd_in(0, 127); break;
            case BR_V17_PR_KIT_PITCH: w = rnd() & 1u ? 255u : (uint32_t)rnd_in(36, 84); break;
            default: w = rnd() & 63u; break;
            }
            br_v17_wr(p + at, w, k->elem);
        }
    }
}

static void random_session(br_session_v17_t *s)
{
    uint32_t i;
    memset(s, 0, sizeof *s);
    for (i = 0u; i < field_count; ++i) {
        const br_v17_range_t *r = range_of(&fields[i]);
        *slot_ptr(s, &fields[i]) = (int16_t)rnd_in(r->min, r->max);
    }
    for (uint32_t t=0; t<BR_V17_TRACKS; ++t) for (uint32_t k=0; k<2u; ++k) {
        s->patch[t][k][0]=(int16_t)rnd_in(br_v17_ranges[BR_V17_R_SOURCE].min,br_v17_ranges[BR_V17_R_SOURCE].max);
        s->patch[t][k][1]=(int16_t)rnd_in(br_v17_ranges[BR_V17_R_TARGET].min,br_v17_ranges[BR_V17_R_TARGET].max);
        s->patch[t][k][2]=(int16_t)rnd_in(-127,127);
        s->patch[t][k][3]=(int16_t)rnd_in(0,1);
    }
    random_pattern(s->pattern);
}

static int same(const br_session_v17_t *a, const br_session_v17_t *b) { return !memcmp(a, b, sizeof *a); }

static br_session_v17_t sa, sb, sc;
static uint8_t buf[8192], buf2[8192];

static int roundtrip(const br_session_v17_t *s, br_session_v17_t *back, uint32_t *enc_clamps, uint32_t *dec_clamps)
{
    uint32_t n;
    *enc_clamps = *dec_clamps = 0u;
    n = br_v17_encode(s, buf, sizeof buf, enc_clamps);
    if (n != BR_V17_TOTAL_LEN) return -100;
    return br_v17_decode(buf, n, back, dec_clamps);
}

/* Rebuild an encoded record with each section cut (keep < 0: whole), extended
 * by `extra` bytes, or omitted; optionally insert an unknown section and add
 * bytes after the last section. Returns the new length. */
typedef struct { int keep[6]; int extra[6]; int omit[6]; int unknown_len; int trailing; int unknown_first; } shape_t;
static uint32_t reshape(const uint8_t *src, uint8_t *dst, const shape_t *sh)
{
    uint32_t at = BR_V17_HEADER_LEN, out = BR_V17_HEADER_LEN, count = 0u, i;
    memcpy(dst, src, BR_V17_HEADER_LEN);
    for (i = 0u; i < 5u; ++i) {
        uint32_t id = src[at], len = br_v17_rd(src + at + 2, 2u), rep = id == BR_V17_SEC_TRACKS ? 4u : 1u;
        uint32_t stride = len / rep, nstride, r;
        const uint8_t *p = src + at + 4;
        at += 4u + len;
        if (sh->unknown_len >= 0 && (int)i == sh->unknown_first) {
            uint32_t k;
            dst[out] = 0x77u; dst[out + 1] = 3u; br_v17_wr(dst + out + 2, (uint32_t)sh->unknown_len, 2u);
            for (k = 0u; k < (uint32_t)sh->unknown_len; ++k) dst[out + 4 + k] = (uint8_t)rnd();
            out += 4u + (uint32_t)sh->unknown_len; ++count;
        }
        if (sh->omit[id]) continue;
        nstride = sh->keep[id] >= 0 ? (uint32_t)sh->keep[id] : stride + (uint32_t)sh->extra[id];
        dst[out] = (uint8_t)id; dst[out + 1] = 1u; br_v17_wr(dst + out + 2, nstride * rep, 2u);
        out += 4u;
        for (r = 0u; r < rep; ++r) {
            uint32_t k;
            for (k = 0u; k < nstride; ++k) dst[out + k] = k < stride ? p[r * stride + k] : (uint8_t)(rnd() | 0x80u);
            out += nstride;
        }
        ++count;
    }
    for (i = 0u; i < (uint32_t)sh->trailing; ++i) dst[out++] = (uint8_t)rnd();
    dst[7] = (uint8_t)count;
    br_v17_wr(dst + 8, out, 2u);
    return out;
}
static void shape_init(shape_t *sh) { int i; memset(sh, 0, sizeof *sh); for (i = 0; i < 6; ++i) sh->keep[i] = -1; sh->unknown_len = -1; }

/* ---- documentation ---- */
static const char *type_name(const br_v17_range_t *r) { return br_v17_range_size(r) == 2u ? (r->min < 0 ? "i16" : "u16") : (r->min < 0 ? "i8" : "u8"); }

/* Notes printed under each field section's table (firmware behaviour, not codec rules). */
static const char *const section_notes[BR_V17_FIELD_SECTIONS] = {
    "`smk_fader_link` (SMK-37 Pro \"fader link\", NOTE REPEAT + SCALE held 0.8 s): 1 = in the track-level fader\n"
    "bank fader 1 sets T1 and T2 and fader 2 is ignored. The FM-1 saves and restores it and never acts on it.\n\n",
    "Reverb \"derive\": the reverb derives each optional control (pre-delay, tone, mod depth, low cut, width) from\n"
    "TYPE/SIZE/DECAY unless its bit in `reverb_manual` is set (bit 0 predelay, 1 tone, 2 mod_depth, 3 low_cut,\n"
    "4 width). Bit 5 selects tempo-synced pre-delay; its stored value selects a note division. A clear bit means derive\n"
    "whatever the stored 0..127 value is; the value is kept so switching the control\n"
    "to manual later starts from it. Default: all derive. `leveler_on` is saved and restored but not yet acted on by\n"
    "the engine (the leveler is behind the compile switch BR_MASTER_LEVELER).\n\n",
    "Saved and restored but not yet acted on by the engine: `lfo*_sync`, `lfo*_division`, `lfo*_key_reset`,\n"
    "`lfo*_fade_in`, `delay_width`, `delay_tone`, `delay_wow_rate`, `mod_feedback` (runtime home `br_part_pending`).\n"
    "`keytrack` is an amount (127 = 1 octave per octave); the HOME KEY TRACK knob switches 0/127.\n"
    "`filter_env_depth` is HOME ENV DEPTH: the mod envelope to the cutoff, besides that envelope's own TARGET/DEPTH.\n\n",
    "",
};

static void print_doc(FILE *o)
{
    uint32_t sec, i, k, b, at, spares[4] = {0}, rows = 0u;
    index_fields();
    for (i = 0u; i < field_count; ++i) if (is_spare(&fields[i])) spares[fields[i].sec]++;
    for (i = 0u; i < BR_V17_ROW_COUNT; ++i) rows += br_v17_rows[i].range != BR_V17_R_SPARE;
    fprintf(o, "# Session format v20 byte map\n\n");
    fprintf(o, "Generated from `firmware/src/instrument_session_v17.h` by `python3 tests/run_session_v17.py --write-doc`;\n");
    fprintf(o, "the host test fails if this file and the table differ. Little-endian. Do not edit by hand.\n\n");
    fprintf(o, "Total **%u B** (limit %u B, %u B headroom). %u settings (%u table rows without spares), %u spare bytes.\n\n",
            (unsigned)BR_V17_TOTAL_LEN, (unsigned)BR_V17_SIZE_LIMIT, (unsigned)(BR_V17_SIZE_LIMIT - BR_V17_TOTAL_LEN),
            (unsigned)setting_count, (unsigned)rows, (unsigned)(spares[0] + spares[1] + spares[2] + spares[3]));
    fprintf(o, "Version 18 packs two track patches into the upper bits of each track's route-enable bytes; see\n"
               "`SESSION-V18-TRACK-PATCHES.md` for the bit layout and v17 migration. The table below shows the\n"
               "low-bit legacy fields; it does not describe the packed upper bits.\n\n");
    fprintf(o, "Version 19 changes MOD DEPTHS to a neutral-centred 0–200%% multiplier and uses the first six KIT\n"
               "spare bytes for PORTAMENTO and voice modes (two bytes per synth track). The first byte is 0–100;\n"
               "the second holds GLIDE MODE in bits 0–1 and AMP MODE in bit 2. Older records default these\n"
               "controls to zero; positive v17/v18 modulation multipliers are converted, while inverted values\n"
               "cannot be represented by the new law and become zero modulation.\n\n");
    fprintf(o, "Version 20 stores each synth track's LPG gate length in bits 3–7 of its voice-mode byte.\n"
               "Zero is OFF; 1–31 select timed gates. Versions 17–19 migrate with gate length OFF.\n"
               "Older firmware rejects v20, preventing silent loss of the new setting.\n\n");
    fprintf(o, "Rules: magic/version/lengths are structural (record rejected). A reader skips unknown sections, bytes after the\n"
               "last section and the tail of a section longer than it knows; fields beyond a shorter section take their\n"
               "defaults; TRACKS is four equal track blocks (stride = len / 4). Out-of-range values are clamped and counted.\n"
               "Spare bytes are written 0 and ignored on read. No checksum: the project store header and the FMP1 envelope\n"
               "already carry CRCs over these bytes.\n\n");
    fprintf(o, "Firmware: instrument_session.c binds every table row to its runtime home (br_v17_binds) and walks the\n"
               "table for capture, restore and validation; a record that fails validation (structure, or a preset that does\n"
               "not suit its track) changes nothing. Projects carry it inside the FMP1 envelope version 6 (24-byte header;\n"
               "bytes 20..21 hold the session length; the 80-byte song arrangement follows the session). Envelope version 5\n"
               "(no arrangement) loads with an empty arrangement and saves back as 6. Version 4 and older (session v16,\n"
               "magic 0x42524431) are never read: a working project in the old format boots from defaults with the store usable (\"OLD PROJECT NOT\n"
               "LOADED\"), a named slot lists as OLD FORMAT, refuses LOAD and is replaced by SAVE after the usual confirm.\n"
               "Host conversion of old captures: tools/session_v16_to_v17.py.\n\n");
    fprintf(o, "## Header (0..11)\n\n| Off | Size | Field | Value |\n|---|---|---|---|\n");
    fprintf(o, "| 0 | 4 | magic | \"JSES\" (0x%08X) |\n| 4 | 2 | version | %u |\n| 6 | 1 | header_len | %u |\n",
            (unsigned)BR_V17_MAGIC, (unsigned)BR_V17_VERSION, (unsigned)BR_V17_HEADER_LEN);
    fprintf(o, "| 7 | 1 | section_count | 5 |\n| 8 | 2 | total_len | %u |\n| 10 | 2 | flags | 0 |\n\n", (unsigned)BR_V17_TOTAL_LEN);
    fprintf(o, "## Sections\n\n| Id | Name | Header at | Payload at | Payload len | Spare bytes |\n|---|---|---|---|---|---|\n");
    at = BR_V17_HEADER_LEN;
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        uint32_t len = (uint32_t)d->len * d->repeat;
        fprintf(o, "| %u | %s | %u | %u | %u%s | %u%s |\n", d->id, br_v17_section_names[sec], (unsigned)at, (unsigned)(at + 4u),
                (unsigned)len, d->repeat > 1u ? " (4 x 112)" : "", (unsigned)spares[sec], d->repeat > 1u ? " (4 per track)" : "");
        at += 4u + len;
    }
    fprintf(o, "| %u | PATTERN | %u | %u | %u | 0 |\n\n", (unsigned)BR_V17_SEC_PATTERN, (unsigned)at, (unsigned)(at + 4u),
            (unsigned)BR_V17_PATTERN_BYTES);
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        uint32_t off = 0u;
        fprintf(o, "## Section %u %s\n\n", d->id, br_v17_section_names[sec]);
        if (d->repeat > 1u)
            fprintf(o, "Track t block at absolute %u + %u*t. Defaults are synth-track values; kit track (t = 3) overrides: ",
                    (unsigned)section_payload_at[d->id], (unsigned)d->len);
        if (d->repeat > 1u) {
            for (k = 0u; k < BR_V17_KIT_DEFAULT_COUNT; ++k) {
                uint32_t slot = 0u, r;
                for (r = 0u; r < d->rows; ++r) {
                    const br_v17_row_t *row = &br_v17_rows[d->first_row + r];
                    if (br_v17_kit_defaults[k].slot >= slot && br_v17_kit_defaults[k].slot < slot + row->count) break;
                    slot += row->count;
                }
                fprintf(o, "%s%s = %d", k ? ", " : "", br_v17_row_names[d->first_row + r], br_v17_kit_defaults[k].value);
            }
            fprintf(o, ".\n\n");
        }
        fprintf(o, "| +Off | Abs | Size | Type | Field | Range | Min | Max | Default |\n|---|---|---|---|---|---|---|---|---|\n");
        for (i = 0u; i < d->rows; ++i) {
            const br_v17_row_t *row = &br_v17_rows[d->first_row + i];
            const br_v17_range_t *r = &br_v17_ranges[row->range];
            uint32_t n = br_v17_range_size(r);
            char name[64];
            if (row->count > 1u) snprintf(name, sizeof name, "%s[%u]", br_v17_row_names[d->first_row + i], (unsigned)row->count);
            else snprintf(name, sizeof name, "%s", br_v17_row_names[d->first_row + i]);
            fprintf(o, "| %u | %u | %u | %s | %s | %s | %d | %d | %d |\n", (unsigned)off,
                    (unsigned)(section_payload_at[d->id] + off), (unsigned)(n * row->count), type_name(r), name,
                    br_v17_range_names[row->range], r->min, r->max, row->def);
            off += n * row->count;
        }
        fprintf(o, "\n%s", section_notes[sec]);
    }
    fprintf(o, "## Section %u PATTERN\n\nThe live pattern. Out-of-range elements are replaced (clamped) and counted.\n\n",
            (unsigned)BR_V17_SEC_PATTERN);
    fprintf(o, "| +Off | Abs | Size | Block | Element | Default byte |\n|---|---|---|---|---|---|\n");
    for (b = 0u, at = 0u; b < BR_V17_PATTERN_BLOCK_COUNT; ++b) {
        fprintf(o, "| %u | %u | %u | %s | %s | %u |\n", (unsigned)at, (unsigned)(section_payload_at[BR_V17_SEC_PATTERN] + at),
                (unsigned)br_v17_pattern_blocks[b].bytes, br_v17_pattern_names[b],
                br_v17_pattern_blocks[b].elem == 2u ? "u16" : "u8", (unsigned)br_v17_pattern_blocks[b].def);
        at += br_v17_pattern_blocks[b].bytes;
    }
    fprintf(o, "\nElements: step_synth [3][64] lo = note+1 (0 off, max 128), hi = velocity | tie<<7; step_kit [64] lo = hit\n"
               "mask, hi = accent mask (bits 0-3); *_len_micro bits 0-10 length 0..1024, bits 11-15 micro -16..15;\n"
               "synth [3][64], kit [64][4]; conditions 0..144; synth note/velocity 0..127 or 255 none; kit velocity 0..127;\n"
               "kit_pitch 36..84 or 255 (C4); kit_retrig bits 0-2 count, 3-5 spacing, 6-7 zero.\n");
}

/* ---- tests ---- */
static void test_layout(void)
{
    static uint8_t owner[BR_V17_TOTAL_LEN];
    uint32_t i, k, at = 0u, sum = 0u, sec;
    index_fields();
    memset(owner, 0, sizeof owner);
    for (i = 0u; i < BR_V17_HEADER_LEN; ++i) owner[at++]++;
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        uint32_t bytes = 0u, r;
        for (r = 0u; r < d->rows; ++r) bytes += br_v17_rows[d->first_row + r].count * br_v17_range_size(&br_v17_ranges[br_v17_rows[d->first_row + r].range]);
        CHECK(bytes == d->len, "section %u rows cover %u of %u bytes", d->id, (unsigned)bytes, (unsigned)d->len);
        for (k = 0u; k < 4u; ++k) owner[section_payload_at[d->id] - 4u + k]++;
        sum += 4u + (uint32_t)d->len * d->repeat;
    }
    for (k = 0u; k < 4u; ++k) owner[section_payload_at[BR_V17_SEC_PATTERN] - 4u + k]++;
    for (i = 0u; i < field_count; ++i)
        for (k = 0u; k < size_of(&fields[i]); ++k) owner[fields[i].wire + k]++;
    for (i = 0u; i < BR_V17_PATTERN_BYTES; ++i) owner[section_payload_at[BR_V17_SEC_PATTERN] + i]++;
    sum += BR_V17_HEADER_LEN + 4u + BR_V17_PATTERN_BYTES;
    CHECK(sum == BR_V17_TOTAL_LEN, "table total %u != constant %u", (unsigned)sum, (unsigned)BR_V17_TOTAL_LEN);
    CHECK(BR_V17_TOTAL_LEN <= BR_V17_SIZE_LIMIT, "over the size limit");
    for (i = 0u; i < BR_V17_TOTAL_LEN; ++i) CHECK(owner[i] == 1u, "byte %u owned %u times", (unsigned)i, owner[i]);
    for (i = 0u; i < BR_V17_ROW_COUNT; ++i) {
        const br_v17_range_t *r = &br_v17_ranges[br_v17_rows[i].range];
        CHECK(br_v17_rows[i].def >= r->min && br_v17_rows[i].def <= r->max, "row %s default out of range", br_v17_row_names[i]);
        CHECK(br_v17_rows[i].count >= 1u, "row %s count", br_v17_row_names[i]);
        for (k = 0u; k < i; ++k)
            if (br_v17_rows[k].range != BR_V17_R_SPARE && /* names unique within a section */
                !strcmp(br_v17_row_names[k], br_v17_row_names[i])) {
                uint32_t s1 = 0u, s2 = 0u;
                for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
                    if (k >= br_v17_sections[sec].first_row && k < br_v17_sections[sec].first_row + br_v17_sections[sec].rows) s1 = sec;
                    if (i >= br_v17_sections[sec].first_row && i < br_v17_sections[sec].first_row + br_v17_sections[sec].rows) s2 = sec;
                }
                CHECK(s1 != s2, "duplicate name %s", br_v17_row_names[i]);
            }
    }
    for (i = 0u; i < BR_V17_KIT_DEFAULT_COUNT; ++i) CHECK(br_v17_kit_defaults[i].slot < BR_V17_TRACK_SLOTS, "kit default slot");
    {   /* a 2-byte field must have a 2-byte range and vice versa: proven by the coverage above; check ranges fit */
        for (i = 0u; i < BR_V17_RANGE_COUNT; ++i)
            CHECK(br_v17_ranges[i].min <= br_v17_ranges[i].max, "range %s", br_v17_range_names[i]);
    }
}

static void test_defaults(void)
{
    uint32_t ec, dc, i;
    int rc;
    br_v17_defaults(&sa);
    rc = roundtrip(&sa, &sb, &ec, &dc);
    CHECK(rc == 0 && ec == 0u && dc == 0u && same(&sa, &sb), "defaults round trip rc=%d clamps %u/%u", rc, (unsigned)ec, (unsigned)dc);
    for (i = 0u; i < field_count; ++i) if (is_spare(&fields[i])) CHECK(buf[fields[i].wire] == 0u, "spare not zero");
    CHECK(sa.global.bpm == 120 && sa.global.poly_on == 1 && sa.global.arp_on == 0 && sa.master.leveler_on == 0, "global defaults");
    CHECK(sa.master.reverb_predelay == 32 && sa.master.reverb_width == 127, "reverb defaults");
    CHECK(sa.track[0].preset == 52 && sa.track[3].preset == 48 && sa.track[3].seq_def_length == 0 &&
          sa.track[2].seq_def_length == 16, "per-track defaults");
    CHECK(sa.track[1].timbre_mod == 64 && sa.track[1].delay_wow_rate == 64 && sa.kit.kit_gate == 128, "new field defaults");
    CHECK(sa.pattern[BR_V17_PAT_synth_note] == 255u && sa.pattern[BR_V17_PAT_kit_velocity] == 100u, "pattern defaults");
    /* empty input decodes to the same defaults */
    rc = br_v17_decode(buf, 0u, &sc, NULL);
    CHECK(rc == BR_V17_ERR_SHORT && same(&sa, &sc), "short buffer leaves defaults");
}

static uint32_t random_roundtrips(uint32_t n)
{
    uint32_t seed, ec, dc, bad = 0u, clamps = 0u;
    for (seed = 1u; seed <= n; ++seed) {
        int rc;
        rng_state = seed * 2654435761u | 1u;
        random_session(&sa);
        rc = roundtrip(&sa, &sb, &ec, &dc);
        clamps += ec + dc;
        if (rc != 0 || ec || dc || !same(&sa, &sb)) ++bad;
    }
    CHECK(bad == 0u, "%u of %u random round trips differ", (unsigned)bad, (unsigned)n);
    CHECK(clamps == 0u, "%u clamps on in-range round trips", (unsigned)clamps);
    return clamps;
}

static void test_min_max(void)
{
    uint32_t i, ec, dc, pass;
    for (pass = 0u; pass < 2u; ++pass) {
        int rc;
        br_v17_defaults(&sa);
        for (i = 0u; i < field_count; ++i) *slot_ptr(&sa, &fields[i]) = pass ? range_of(&fields[i])->max : range_of(&fields[i])->min;
        rc = roundtrip(&sa, &sb, &ec, &dc);
        CHECK(rc == 0 && !ec && !dc && same(&sa, &sb), "all-%s round trip", pass ? "max" : "min");
    }
}

static uint32_t test_out_of_range(void)
{
    uint32_t i, ec, dc, checked = 0u;
    for (i = 0u; i < field_count; ++i) {
        const field_ref_t *f = &fields[i];
        const br_v17_range_t *r = range_of(f);
        uint32_t n = size_of(f), side;
        if (is_spare(f)) {
            /* encode: a non-zero spare member is written 0 and counted; decode ignores spare bytes */
            br_v17_defaults(&sa); *slot_ptr(&sa, f) = 5;
            ec = 0u; br_v17_encode(&sa, buf, sizeof buf, &ec);
            CHECK(ec == 1u && buf[f->wire] == 0u, "spare encode");
            if (f->sec == 3u && f->elem < 6u) continue; /* v19 voice settings occupy these bytes */
            buf[f->wire] = 0xa5u; dc = 0u;
            CHECK(br_v17_decode(buf, BR_V17_TOTAL_LEN, &sb, &dc) == 0 && dc == 0u && *slot_ptr(&sb, f) == 0, "spare ignored on read");
            continue;
        }
        for (side = 0u; side < 2u; ++side) {
            int32_t bad = side ? r->max + 1 : r->min - 1, expect = side ? r->max : r->min;
            int32_t lo = r->min < 0 ? (n == 2u ? -32768 : -128) : 0, hi = r->min < 0 ? (n == 2u ? 32767 : 127) : (n == 2u ? 65535 : 255);
            /* encode side */
            br_v17_defaults(&sa); *slot_ptr(&sa, f) = (int16_t)bad;
            ec = dc = 0u;
            CHECK(br_v17_encode(&sa, buf, sizeof buf, &ec) == BR_V17_TOTAL_LEN && ec == 1u, "encode clamp count %s", br_v17_row_names[f->row]);
            CHECK(br_v17_decode(buf, BR_V17_TOTAL_LEN, &sb, &dc) == 0 && dc == 0u && *slot_ptr(&sb, f) == expect,
                  "encode clamp value %s", br_v17_row_names[f->row]);
            /* decode side, when the wire can hold the bad value */
            if (bad < lo || bad > hi) continue;
            /* Upper bits carry v18 track patches, not out-of-range enable values. */
            if (f->row == BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT + BR_V17_TROW_route_enabled) continue;
            br_v17_defaults(&sa);
            br_v17_encode(&sa, buf, sizeof buf, NULL);
            br_v17_wr(buf + f->wire, (uint32_t)bad & (n == 2u ? 0xffffu : 0xffu), n);
            dc = 0u;
            CHECK(br_v17_decode(buf, BR_V17_TOTAL_LEN, &sb, &dc) == 0 && dc == 1u && *slot_ptr(&sb, f) == expect,
                  "decode clamp %s side %u (dc %u value %d)", br_v17_row_names[f->row], (unsigned)side, (unsigned)dc, *slot_ptr(&sb, f));
            ++checked;
        }
    }
    {   /* pattern: one bad element per rule is clamped and counted */
        static const uint32_t at[] = { BR_V17_PAT_step_synth, BR_V17_PAT_step_kit, BR_V17_PAT_synth_len_micro, BR_V17_PAT_synth_condition,
                                       BR_V17_PAT_synth_note, BR_V17_PAT_kit_velocity, BR_V17_PAT_kit_pitch, BR_V17_PAT_kit_retrig };
        static const uint8_t val[] = { 129u, 0x10u, 0xffu, 145u, 128u, 128u, 35u, 0x40u };
        uint32_t k;
        for (k = 0u; k < sizeof at / sizeof at[0]; ++k) {
            br_v17_defaults(&sa);
            br_v17_encode(&sa, buf, sizeof buf, NULL);
            buf[section_payload_at[BR_V17_SEC_PATTERN] + at[k]] = val[k];
            if (k == 2u) buf[section_payload_at[BR_V17_SEC_PATTERN] + at[k] + 1] = 0x07u; /* length 2047 */
            dc = 0u;
            CHECK(br_v17_decode(buf, BR_V17_TOTAL_LEN, &sb, &dc) == 0 && dc == 1u, "pattern clamp %u (dc %u)", (unsigned)k, (unsigned)dc);
            ++checked;
        }
    }
    return checked;
}

static uint32_t test_truncation(void)
{
    uint32_t cases = 0u, sec;
    shape_t sh;
    rng_state = 777u;
    random_session(&sa);
    br_v17_encode(&sa, buf, sizeof buf, NULL);
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        int keep;
        for (keep = 0; keep <= d->len; ++keep) {
            uint32_t n, dc = 0u, i;
            int ok = 1;
            shape_init(&sh); sh.keep[d->id] = keep;
            n = reshape(buf, buf2, &sh);
            if (br_v17_decode(buf2, n, &sb, &dc) != 0 || dc) { CHECK(0, "truncated %u/%d rejected", d->id, keep); continue; }
            br_v17_defaults(&sc);
            for (i = 0u; i < field_count; ++i) {
                const field_ref_t *f = &fields[i];
                int16_t want;
                if (f->sec != sec) want = *slot_ptr(&sa, f);
                else {
                    uint32_t rel = f->wire - section_payload_at[d->id] - f->rep * d->len;
                    want = rel + size_of(f) <= (uint32_t)keep ? *slot_ptr(&sa, f) : *slot_ptr(&sc, f);
                }
                if (is_spare(f)) want = 0;
                if (*slot_ptr(&sb, f) != want) ok = 0;
            }
            CHECK(ok && !memcmp(sa.pattern, sb.pattern, sizeof sa.pattern), "truncated section %u keep %d", d->id, keep);
            ++cases;
        }
    }
    {   /* pattern cut at even lengths, and each section omitted */
        int keep;
        for (keep = 0; keep <= BR_V17_PATTERN_BYTES; keep += 94) {
            uint32_t n, dc = 0u;
            shape_init(&sh); sh.keep[BR_V17_SEC_PATTERN] = keep;
            n = reshape(buf, buf2, &sh);
            br_v17_defaults(&sc);
            memcpy(sc.pattern, sa.pattern, (size_t)keep);
            CHECK(br_v17_decode(buf2, n, &sb, &dc) == 0 && !memcmp(sb.pattern, sc.pattern, sizeof sc.pattern),
                  "pattern cut %d", keep);
            ++cases;
        }
        for (sec = 1u; sec <= 5u; ++sec) {
            uint32_t n, dc = 0u;
            shape_init(&sh); sh.omit[sec] = 1;
            n = reshape(buf, buf2, &sh);
            CHECK(br_v17_decode(buf2, n, &sb, &dc) == 0 && dc == 0u, "omit %u", (unsigned)sec);
            br_v17_defaults(&sc);
            if (sec == BR_V17_SEC_PATTERN) CHECK(!memcmp(sb.pattern, sc.pattern, sizeof sc.pattern), "omitted pattern = defaults");
            if (sec == BR_V17_SEC_TRACKS) CHECK(!memcmp(sb.track, sc.track, sizeof sc.track), "omitted tracks = defaults");
            if (sec == BR_V17_SEC_GLOBAL) CHECK(!memcmp(&sb.global, &sc.global, sizeof sc.global), "omitted global = defaults");
            ++cases;
        }
    }
    return cases;
}

static uint32_t test_unknown(void)
{
    uint32_t cases = 0u, pos;
    shape_t sh;
    rng_state = 4242u;
    random_session(&sa);
    br_v17_encode(&sa, buf, sizeof buf, NULL);
    for (pos = 0u; pos < 5u; ++pos) {
        uint32_t n, dc = 0u, id;
        shape_init(&sh);
        sh.unknown_first = (int)pos; sh.unknown_len = (int)(17u + pos * 100u); sh.trailing = (int)(pos * 7u);
        for (id = 1u; id <= 5u; ++id) sh.extra[id] = (int)(pos + id);
        n = reshape(buf, buf2, &sh);
        CHECK(br_v17_decode(buf2, n, &sb, &dc) == 0 && dc == 0u && same(&sa, &sb), "unknown section/tail case %u", (unsigned)pos);
        /* carrier padding after total_len is ignored too */
        memset(buf2 + n, 0xee, 64);
        CHECK(br_v17_decode(buf2, n + 64u, &sb, &dc) == 0 && same(&sa, &sb), "padding after total_len");
        ++cases;
    }
    return cases;
}

static uint32_t test_structural(void)
{
    uint32_t n = BR_V17_TOTAL_LEN, cases = 0u;
    shape_t sh;
#define REJECT(mod, err) do { br_v17_defaults(&sa); br_v17_encode(&sa, buf, sizeof buf, NULL); mod; \
    CHECK(br_v17_decode(buf, n, &sb, NULL) == (err), "expected %s", #err); ++cases; n = BR_V17_TOTAL_LEN; } while (0)
    REJECT(buf[0] ^= 1u, BR_V17_ERR_MAGIC);
    REJECT((buf[0] = 0x31, buf[1] = 0x44, buf[2] = 0x52, buf[3] = 0x42), BR_V17_ERR_MAGIC); /* v16 magic */
    REJECT(buf[4] = 16u, BR_V17_ERR_VERSION);
    REJECT(buf[5] = 1u, BR_V17_ERR_VERSION);
    REJECT(buf[6] = 11u, BR_V17_ERR_HEADER);
    REJECT(br_v17_wr(buf + 8, BR_V17_TOTAL_LEN + 1u, 2u), BR_V17_ERR_TOTAL);
    REJECT(br_v17_wr(buf + 8, 11u, 2u), BR_V17_ERR_TOTAL);
    REJECT(n = BR_V17_TOTAL_LEN - 1u, BR_V17_ERR_TOTAL);
    REJECT(n = 11u, BR_V17_ERR_SHORT);
    REJECT(buf[7] = 6u, BR_V17_ERR_SECTION);                                       /* a sixth header past the end */
    REJECT(br_v17_wr(buf + BR_V17_HEADER_LEN + 2, 0xffffu, 2u), BR_V17_ERR_SECTION); /* section longer than record */
    REJECT(buf[section_payload_at[BR_V17_SEC_MASTER] - 4u] = BR_V17_SEC_GLOBAL, BR_V17_ERR_DUPLICATE);
    REJECT(br_v17_wr(buf + section_payload_at[BR_V17_SEC_TRACKS] - 2u, 447u, 2u), BR_V17_ERR_TRACKS);
    shape_init(&sh); sh.keep[BR_V17_SEC_TRACKS] = 0;
    br_v17_defaults(&sa); br_v17_encode(&sa, buf, sizeof buf, NULL);
    n = reshape(buf, buf2, &sh);
    br_v17_wr(buf2 + section_payload_at[BR_V17_SEC_TRACKS] - 2u, 0u, 2u);
    {   /* TRACKS len 6: not a multiple of 4 -> rejected; build it by hand */
        static uint8_t t[64];
        memcpy(t, buf2, BR_V17_HEADER_LEN);
        t[7] = 1u; t[12] = BR_V17_SEC_TRACKS; t[13] = 1u; br_v17_wr(t + 14, 6u, 2u);
        br_v17_wr(t + 8, 22u, 2u);
        CHECK(br_v17_decode(t, 22u, &sb, NULL) == BR_V17_ERR_TRACKS, "tracks len %% 4");
        ++cases;
    }
    n = BR_V17_TOTAL_LEN;
    return cases;
}

static void check_in_range(const br_session_v17_t *s)
{
    uint32_t i;
    for (i = 0u; i < field_count; ++i) {
        int16_t v = *slot_ptr((br_session_v17_t *)(uintptr_t)s, &fields[i]);
        if (v < range_of(&fields[i])->min || v > range_of(&fields[i])->max) { CHECK(0, "decoded value out of range"); return; }
    }
}

static uint32_t fuzz_ok;
static void fuzz(uint32_t iterations)
{
    uint32_t it;
    rng_state = 0x1234567u;
    br_v17_defaults(&sa);
    for (it = 0u; it < iterations; ++it) {
        uint32_t len, i, ec = 0u, dc = 0u;
        uint8_t *m;
        int rc;
        if (it % 4u == 0u) random_session(&sa);
        br_v17_encode(&sa, buf, sizeof buf, NULL);
        if (it & 1u) {             /* bit flips of a valid record, sometimes truncated or padded */
            uint32_t flips = 1u + rnd() % 8u;
            len = BR_V17_TOTAL_LEN;
            if (rnd() % 4u == 0u) len = rnd() % (BR_V17_TOTAL_LEN + 64u);
            for (i = BR_V17_TOTAL_LEN; i < len; ++i) buf[i] = (uint8_t)rnd();
            for (i = 0u; i < flips; ++i) {
                uint32_t bit = rnd() % ((rnd() & 1u ? 64u : BR_V17_TOTAL_LEN) * 8u);
                buf[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
            }
        } else {                   /* random bytes behind a mostly valid header */
            len = rnd() % 4200u;
            for (i = 0u; i < len; ++i) buf[i] = (uint8_t)rnd();
            if (len >= 12u && rnd() % 4u) {
                br_v17_wr(buf, 0x534Au, 2u); br_v17_wr(buf + 2, 0x5345u, 2u); br_v17_wr(buf + 4, 17u, 2u);
                if (rnd() & 1u) buf[6] = 12u;
                if (rnd() & 1u) br_v17_wr(buf + 8, len - rnd() % 16u, 2u);
                buf[7] = (uint8_t)(rnd() % 12u);
                for (i = 12u; i + 4u <= len && rnd() % 3u; i += 4u + br_v17_rd(buf + i + 2, 2u)) {
                    buf[i] = (uint8_t)(rnd() % 8u);
                    br_v17_wr(buf + i + 2, rnd() % 600u, 2u);
                }
            }
        }
        m = (uint8_t *)malloc(len ? len : 1u);   /* exact size: ASan sees any overread */
        memcpy(m, buf, len);
        rc = br_v17_decode(m, len, &sb, &dc);
        free(m);
        if (rc == 0) {
            ++fuzz_ok;
            check_in_range(&sb);
            ec = dc = 0u;
            if (br_v17_encode(&sb, buf2, sizeof buf2, &ec) != BR_V17_TOTAL_LEN ||
                br_v17_decode(buf2, BR_V17_TOTAL_LEN, &sc, &dc) != 0 || ec || dc || !same(&sb, &sc))
                CHECK(0, "fuzz: accepted record does not re-encode cleanly (it %u)", (unsigned)it);
        }
    }
}

int main(int argc, char **argv)
{
    uint32_t oor, trunc, unk, structural, iterations = 100000u;
    if (argc > 1 && !strcmp(argv[1], "--doc")) { print_doc(stdout); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--fuzz")) iterations = (uint32_t)strtoul(argv[2], NULL, 10);
    test_layout();
    test_defaults();
    /* Both historic track IDs and the new global IDs retain their exact wire value. */
    for (uint32_t id = 0u; id <= 75u; ++id) {
        uint32_t enc, dec;
        br_v17_defaults(&sa); sa.global.global_lfo_target = (int16_t)id;
        CHECK(roundtrip(&sa, &sb, &enc, &dec) == 0 && !enc && !dec && sb.global.global_lfo_target == (int16_t)id,
              "global LFO target %u round trip", (unsigned)id);
    }
    random_roundtrips(10000u);
    test_min_max();
    /* Independent panel patches survive the standalone codec, including signed extremes. */
    br_v17_defaults(&sa);
    for (uint32_t t=0; t<BR_V17_TRACKS; ++t) for (uint32_t slot=0; slot<2u; ++slot) {
        sa.patch[t][slot][0]=(int16_t)(8u+t); sa.patch[t][slot][1]=(int16_t)(2u+slot);
        sa.patch[t][slot][2]=(int16_t)(t==0 ? -127 : (t==3 ? 127 : 3*(int)t)); sa.patch[t][slot][3]=1;
    }
    { uint32_t ec,dc; CHECK(roundtrip(&sa,&sb,&ec,&dc)==0 && !ec && !dc && same(&sa,&sb), "v18 per-track patch roundtrip"); }
    br_v17_defaults(&sa);
    for (uint32_t p=0; p<3u; ++p) {
        sa.voice_extra[p][0]=(uint8_t)(p*50u); sa.voice_extra[p][1]=(uint8_t)p; sa.voice_extra[p][2]=(uint8_t)(p&1u); sa.voice_extra[p][3]=(uint8_t)(p*15u);
    }
    { uint32_t ec,dc; CHECK(roundtrip(&sa,&sb,&ec,&dc)==0 && !ec && !dc && same(&sa,&sb), "v20 voice settings roundtrip"); }
    br_v17_defaults(&sa); sa.track[0].timbre_mod=33; sa.voice_extra[0][0]=42; sa.voice_extra[0][1]=2; sa.voice_extra[0][2]=1;
    br_v17_encode(&sa,buf,sizeof buf,NULL); buf[4]=BR_V19_VERSION;
    CHECK(br_v17_decode(buf,BR_V17_TOTAL_LEN,&sb,NULL)==0 && sb.track[0].timbre_mod==33 && sb.voice_extra[0][0]==42 && sb.voice_extra[0][1]==2 && sb.voice_extra[0][2]==1 && sb.voice_extra[0][3]==0, "v19 voice and modulation migrate with gate off");
    br_v17_defaults(&sa);
    sa.track[0].timbre_mod=127; sa.track[0].pitch_mod=96; sa.track[0].morph_mod=0; sa.track[0].harmonics_mod=128;
    sa.patch[0][0][0]=8; sa.patch[0][0][1]=2; sa.patch[0][0][2]=-52; sa.patch[0][0][3]=1;
    br_v17_encode(&sa,buf,sizeof buf,NULL); buf[4]=BR_V18_VERSION;
    CHECK(br_v17_decode(buf,BR_V17_TOTAL_LEN,&sb,NULL)==0, "v18 decode");
    CHECK(sb.track[0].timbre_mod==64 && sb.track[0].pitch_mod==33 && sb.track[0].morph_mod==0 && sb.track[0].harmonics_mod==65,
          "v18 modulation law migration");
    CHECK(sb.patch[0][0][0]==8 && sb.patch[0][0][1]==2 && sb.patch[0][0][2]==-52 && sb.patch[0][0][3]==1,
          "v18 per-track patch preserved");
    br_v17_defaults(&sa); sa.master.global_route_source[0]=8; sa.master.global_route_target[0]=2;
    sa.master.global_route_amount[0]=-52; sa.master.global_route_enabled[0]=1;
    br_v17_encode(&sa,buf,sizeof buf,NULL); buf[4]=BR_V17_LEGACY_VERSION;
    CHECK(br_v17_decode(buf,BR_V17_TOTAL_LEN,&sb,NULL)==0, "legacy decode");
    for (uint32_t t=0; t<BR_V17_TRACKS; ++t)
        CHECK(sb.patch[t][0][0]==8 && sb.patch[t][0][1]==2 && sb.patch[t][0][2]==-52 && sb.patch[t][0][3]==1, "legacy global route migration track %u",t);
    oor = test_out_of_range();
    trunc = test_truncation();
    unk = test_unknown();
    structural = test_structural();
    fuzz(iterations);
    printf("layout: total %u B, %u settings, %u table rows, %u ranges\n", (unsigned)BR_V17_TOTAL_LEN,
           (unsigned)setting_count, (unsigned)BR_V17_ROW_COUNT, (unsigned)BR_V17_RANGE_COUNT);
    printf("round trips: defaults + 10000 random seeds + all-min + all-max, 0 clamps\n");
    printf("clamp checks %u, truncation cases %u, unknown/tail cases %u, structural rejects %u\n",
           (unsigned)oor, (unsigned)trunc, (unsigned)unk, (unsigned)structural);
    printf("fuzz: %u buffers, %u accepted and re-encoded cleanly\n", (unsigned)iterations, (unsigned)fuzz_ok);
    if (failures) { printf("session v17: %d FAILURES\n", failures); return 1; }
    printf("session v17: all checks passed\n");
    return 0;
}
