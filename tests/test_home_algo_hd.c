/* SPDX-License-Identifier: GPL-3.0-only */
/* HD home page model chip in the top bar (built with -DBR_UI_HD=1). On SOUND screen 0 a Plaits track's
 * header leads its title with the model chip: the 20 px chip at x 40..59 (after the track badge), a 6 px
 * gap, then the model name, which ends by x 190 and leaves everything right of it (metronome, BPM,
 * battery) exactly as a non-Plaits header draws it. The band above the controls (y 46..65) shows no
 * icon or name. Screen 1 (titled with the same sound name) has the same header. Non-Plaits sounds (a Braids
 * preset, the kit track) keep the title at x 40 with no chip. Model, screen and sound changes redraw the header to match a fresh draw.
 * Writes a contact sheet of every header (and two full screens) to <crops>/../home-algo-sheet.ppm. */
#define DINGHY_HOST 1
#define UI_REFRESH_TEST_COUNTERS 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "hd_look.h"
#include "host_intro.h"
#include <stdlib.h>

#define BAND_Y 46u
#define HEAD_H 24u
#define MODELS BR_PLAITS_MODEL_COUNT
static uint16_t head[MODELS][240u * HEAD_H], band_ref[240u * 20u];
static uint16_t ref_head[240u * HEAD_H], kit_head[240u * HEAD_H], full_plaits[240u * 240u], full_braids[240u * 240u];

/* A fresh draw of track part with preset on HOME screen; copies the header and checks the band. */
static void draw(uint32_t part, uint32_t preset, uint32_t screen, uint32_t fresh, uint16_t *header)
{
    br_ui.part = part; br_ui.page = BR_PAGE_HOME; br_ui.screen[BR_PAGE_HOME] = screen;
    br_part[part].sound.preset = preset;
    if (fresh) br_ui.redraw = 1u;
    br_draw();
    if (header) memcpy(header, fb, 240u * HEAD_H * sizeof *header);
}
/* The header's background is a dithered dark gradient: ink is what differs clearly in brightness from
 * the row's background as at x 38 (between the badge and the title). */
static uint16_t bgrow[HEAD_H];
static void take_bg(const uint16_t *h) { for (uint32_t y = 0; y < HEAD_H; ++y) bgrow[y] = h[y * 240u + 38u]; }
static int ink(uint16_t c, uint32_t y) { return abs((int32_t)hd_luma(c) - (int32_t)hd_luma(bgrow[y])) > 96; }
static uint32_t col_ink(const uint16_t *h, int32_t x)
{
    uint32_t n = 0u;
    for (int32_t y = 0; y < (int32_t)HEAD_H; ++y) n += ink(h[y * 240 + x], (uint32_t)y);
    return n;
}
static uint32_t diff(const uint16_t *a, const uint16_t *b, int32_t x0, int32_t x1)
{
    uint32_t n = 0u;
    for (int32_t y = 0; y < (int32_t)HEAD_H; ++y) for (int32_t x = x0; x <= x1; ++x) n += a[y * 240 + x] != b[y * 240 + x];
    return n;
}

int main(int argc, char **argv)
{
    static uint16_t now[240u * HEAD_H], fresh[240u * HEAD_H];
    uint32_t widest = 0u, narrowest = 0u;
    int32_t rmax = -1, rmin = 1000;
    if (argc > 1) hd_crop_dir = argv[1];
    br_app_init(); host_after_intro(); fm1_ms = 5000u;
    /* References: a Braids synth preset on track 1 (no chip) and the kit track. */
    draw(0u, 0u, 0u, 1u, ref_head);
    memcpy(band_ref, fb + BAND_Y * 240u, sizeof band_ref);
    memcpy(full_braids, fb, sizeof full_braids);
    take_bg(ref_head);
    HD_CHECK(col_ink(ref_head, 40) || col_ink(ref_head, 41) || col_ink(ref_head, 42),
             "Braids preset: title starts at x 40");
    draw(0u, 0u, 1u, 1u, now);
    HD_CHECK(!diff(now, ref_head, 0, 239), "Braids preset: screen 0 and screen 1 headers are identical (no chip)");
    draw(BR_DRUM_PART, br_part[BR_DRUM_PART].sound.preset, 0u, 1u, kit_head);
    draw(BR_DRUM_PART, br_part[BR_DRUM_PART].sound.preset, 1u, 1u, now);
    take_bg(kit_head);
    HD_CHECK(!diff(now, kit_head, 0, 239), "kit track: screen 0 and screen 1 headers are identical (no chip)");
    HD_CHECK(col_ink(kit_head, 40) || col_ink(kit_head, 41) || col_ink(kit_head, 42),
             "kit track: title starts at x 40");
    take_bg(ref_head);
    for (uint32_t m = 0u; m < MODELS; ++m) {
        uint32_t preset = BR_FIRST_PLAITS + m, chip = 0u, gap = 0u, clear = 0u;
        const char *name = br_sound_name(preset);
        int32_t left = 240, right = -1;
        draw(0u, preset, 0u, 1u, head[m]);
        if (m == 13u) memcpy(full_plaits, fb, sizeof full_plaits);
        {   /* The band above the controls: exactly as for the Braids preset (no icon, no name). */
            uint32_t n = 0u;
            for (uint32_t i = 0; i < 240u * 20u; ++i) n += fb[BAND_Y * 240u + i] != band_ref[i];
            HD_CHECK(!n, "model %u '%s': band above the controls differs from a non-Plaits one (%u px)", m, name, n);
        }
        for (int32_t y = 2; y < 22; ++y) for (int32_t x = 40; x < 60; ++x) chip += ink(head[m][y * 240 + x], (uint32_t)y);
        for (int32_t x = 60; x < 64; ++x) gap += col_ink(head[m], x);
        for (int32_t x = 191; x < 196; ++x) clear += col_ink(head[m], x);
        for (int32_t x = 60; x < 196; ++x) if (col_ink(head[m], x)) { if (x < left) left = x; right = x; }
        HD_CHECK(chip >= 380u, "model %u '%s': chip fills x 40..59, y 2..21 (%u of 400 px)", m, name, chip);
        HD_CHECK(!col_ink(head[m], 39), "model %u: x 39 clear between the badge and the chip", m);
        HD_CHECK(!gap, "model %u '%s': gap x 60..63 after the chip (title at 66; a W's ink starts at 65) (%u px ink)", m, name, gap);
        HD_CHECK(left >= 64 && left <= 68 && right <= 190 && right - left >= 30,
                 "model %u '%s': title ink x %d..%d (want from 64..68, end by 190)", m, name, left, right);
        HD_CHECK(!clear && !diff(head[m], ref_head, 191, 239),
                 "model %u '%s': x 191..239 (metronome, BPM, battery) as on a non-Plaits header", m, name);
        HD_CHECK(!diff(head[m], ref_head, 0, 39), "model %u '%s': track badge unchanged", m, name);
        draw(0u, preset, 1u, 1u, now);       /* screen 1 (FILTER) carries the same title: the same header, chip included */
        HD_CHECK(!diff(now, head[m], 0, 239), "model %u: screen 1's header equals screen 0's (chip and title)", m);
        if (right > rmax) { rmax = right; widest = m; }
        if (right < rmin) { rmin = right; narrowest = m; }
        if (m) HD_CHECK(diff(head[m], head[m - 1u], 0, 239), "model %u header differs from model %u", m, m - 1u);
        printf("model %2u %-14s title ink x %d..%d (%d px to spare)\n", m, name, left, right, 190 - right);
    }
    HD_CHECK(diff(head[0], head[8], 40, 59), "models 0 and 8 share a symbol but not a chip colour");
    /* Redraw on change, without forcing: each header equals a fresh draw. */
    draw(0u, BR_FIRST_PLAITS + widest, 0u, 1u, 0);
    draw(0u, BR_FIRST_PLAITS + narrowest, 0u, 0u, now);
    HD_CHECK(!diff(now, head[narrowest], 0, 239), "widest -> narrowest model: header equals a fresh draw");
    draw(0u, BR_FIRST_PLAITS + narrowest, 1u, 0u, now);
    draw(0u, BR_FIRST_PLAITS + narrowest, 1u, 1u, fresh);
    HD_CHECK(!diff(now, fresh, 0, 239), "screen 0 -> 1: the chip goes (header equals a fresh draw)");
    draw(0u, BR_FIRST_PLAITS + narrowest, 0u, 0u, now);
    HD_CHECK(!diff(now, head[narrowest], 0, 239), "screen 1 -> 0: the chip returns");
    draw(0u, 0u, 0u, 0u, now);
    HD_CHECK(!diff(now, ref_head, 0, 239), "Plaits -> Braids preset: header equals a fresh draw");
    {   /* Contact sheet: every model's header, the two references, then two full screens, at 2x. */
        static uint16_t sheet[480u * 1680u];
        uint32_t rows = MODELS + 2u;
        for (uint32_t r = 0; r < rows; ++r) {
            const uint16_t *h = r < MODELS ? head[r] : r == MODELS ? ref_head : kit_head;
            for (uint32_t y = 0; y < HEAD_H; ++y) for (uint32_t x = 0; x < 240u; ++x)
                sheet[(r * (HEAD_H + 2u) + y) * 240u + x] = h[y * 240u + x];
        }
        hd_write_crop("build/home-algo-headers.ppm", sheet, 240, 0, 0, 0, 240, (int32_t)(rows * (HEAD_H + 2u)), 2);
        hd_write_crop("build/home-algo-plaits.ppm", full_plaits, 240, 0, 0, 0, 240, 240, 2);
        hd_write_crop("build/home-algo-braids.ppm", full_braids, 240, 0, 0, 0, 240, 240, 2);
    }
    return hd_finish("HD home page: Plaits model chip in the top bar, band above the controls clear");
}
