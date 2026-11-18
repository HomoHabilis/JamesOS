/* SPDX-License-Identifier: GPL-3.0-only */
/* Partial-repaint differential harness for the HD UI.
 *
 * Usage: normally driven by tools/hd_verify.py, which builds this file the same way tools/render_screens.py builds
 * tools/ui_emulator.c (the real app, unity-included, with the in-memory device project store so the SAVE menus
 * run), feeds it a long scripted session on stdin and turns its output into a report and cropped images.
 * Every millisecond the app ticks exactly as on the device (audio blocks, then br_app_tick, which repaints
 * selectively); fb is the emulated LCD, i.e. what the panel shows after all the partial pushes so far. After
 * every tick that drew, the harness fork()s: the child holds the identical app state and time, clears the LCD
 * (once to black, once to a magenta sentinel), forces a complete redraw (layout cache invalidated + redraw flag,
 * the same path a page change takes; es_draw for the SAVE screens) and hands both screens back through shared
 * memory. Pixels where the accumulated LCD differs from the full redraw are reported ('M' lines); pixels that
 * the full redraw itself leaves to whatever was on the LCD (black and magenta references differ) are reported
 * separately as 'U'. After a report the mismatching pixels are resynchronised to the reference (unless
 * HDV_NO_RESYNC is set), so each finding is attributed to the action that caused it.
 *
 * stdin commands (one per line):  L text | b BTN | h BTN 0/1 | e ENC DELTA | n KEY 0/1 | t MS | p PART |
 * v PAGE SCREEN | c (compare now) | B N (N forced full redraws, for cost measurement) | Q (quit).
 * SMK-37 Pro builds also: P 1 MS / P 0 (fader peek on for MS ms / off) | I (replay the text intro); with
 * BR_PATTERN_SONG_EXPERIMENT: S USED QUEUED (pattern slot state).
 * Environment: HDV_OUT=dir (dump raw frames of mismatches there), HDV_NO_RESYNC=1, HDV_NOCMP=1 (no forks: just
 * print a hash of the LCD after every draw, for build-to-build comparison), HDV_DUMP=i,j (dump LCD at those
 * draw indices). */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
static int hdv_quiet_printf(const char *format, ...) { (void)format; return 0; }
#ifdef HDV_COUNTERS
/* Hooks used only by instrumented copies of the sources made by tools/hd_verify.py. */
static unsigned long long hdv_blend_calls, hdv_blend_px, hdv_blend_oob, hdv_get_oob, hdv_rect_px, hdv_pset_px, hdv_fill_px;
static unsigned long long hdv_bad_write, hdv_canvas, hdv_last_canvas, hdv_stale_chain, hdv_stale_px, hdv_chain_total;
static void hdv_stale_event(int x0, int y0, int x1, int y1, unsigned color, void *caller)
{
    ++hdv_stale_chain;          /* bw_qline joined a segment drawn into an earlier canvas (strip or frame) */
    fprintf(stdout, "J %d,%d->%d,%d c=%04x caller=%p\n", x0, y0, x1, y1, color, caller);
}
#endif
#define DINGHY_HOST 1
#define main host_unused_main
#define printf hdv_quiet_printf
#include "../firmware/src/dinghy.c"
#undef printf
#undef main

static uint32_t hdv_owed;
static uint16_t *hdv_ref;                         /* shared: [0] black-cleared reference, [1] magenta-cleared */
static uint16_t hdv_prev[240 * 240];              /* LCD at the previous comparison */
static char hdv_label[200] = "boot";
static uint32_t hdv_draws, hdv_compares, hdv_mismatch_events, hdv_nocmp, hdv_resync = 1;
static unsigned long long hdv_mismatch_px, hdv_unowned_px;
static const char *hdv_out;
static long hdv_dump[16]; static uint32_t hdv_ndump;

static uint32_t hdv_layout(void)
{
    return br_draw_layout_old;
}

static void hdv_full_redraw(void)
{
    if (es.mode) es_draw();
    else { br_draw_layout_old = 0xffffffffu; br_ui.redraw = 1u; br_draw(); }
}

static void hdv_write(const char *tag, uint32_t n, const uint16_t *px)
{
    char path[512]; FILE *f;
    if (!hdv_out) return;
    snprintf(path, sizeof path, "%s/%06u-%s.565", hdv_out, n, tag);
    if ((f = fopen(path, "wb"))) { fwrite(px, 2, 240 * 240, f); fclose(f); }
}

static uint32_t hdv_hash(const uint16_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void hdv_state(FILE *o)
{
    fprintf(o, "t=%u page=%u screen=%u part=%u layout=%u es=%u playing=%u |%s|", fm1_ms, br_ui.page,
            br_ui.screen[br_ui.page], br_ui.part, hdv_layout(), (unsigned)es.mode, br_playing, hdv_label);
}

static void hdv_compare(void)
{
    uint32_t i, x0 = 240, y0 = 240, x1 = 0, y1 = 0, n = 0, u = 0, ux0 = 240, uy0 = 240, ux1 = 0, uy1 = 0;
    ++hdv_compares;
    for (int pass = 0; pass < 2; ++pass) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); exit(2); }
        if (!pid) {
            uint16_t fill = pass ? 0xF81Fu : 0x0000u;
            for (i = 0; i < 240u * 240u; ++i) fb[i] = fill;
            hdv_full_redraw();
            memcpy(hdv_ref + pass * 240 * 240, fb, sizeof fb);
            _exit(0);
        }
        int st; waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st)) { fprintf(stdout, "E child failed status=%d ", st); hdv_state(stdout); putchar('\n'); exit(3); }
    }
    for (i = 0; i < 240u * 240u; ++i) {
        uint32_t x = i % 240u, y = i / 240u;
        if (hdv_ref[i] != hdv_ref[240 * 240 + i]) {
            if (fb[i] != hdv_ref[i] && fb[i] != hdv_ref[240 * 240 + i]) { /* only report when unowned AND not black */ }
            ++u; if (x < ux0) ux0 = x; if (x > ux1) ux1 = x; if (y < uy0) uy0 = y; if (y > uy1) uy1 = y;
            continue;
        }
        if (fb[i] != hdv_ref[i]) {
            ++n; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
        }
    }
    if (n) {
        ++hdv_mismatch_events; hdv_mismatch_px += n;
        printf("M %u n=%u box=%u,%u,%u,%u ", hdv_compares, n, x0, y0, x1, y1); hdv_state(stdout); putchar('\n');
        hdv_write("prev", hdv_compares, hdv_prev); hdv_write("act", hdv_compares, fb); hdv_write("ref", hdv_compares, hdv_ref);
        if (hdv_resync)
            for (i = 0; i < 240u * 240u; ++i)
                if (hdv_ref[i] == hdv_ref[240 * 240 + i]) fb[i] = hdv_ref[i];
    }
    if (u) {
        hdv_unowned_px += u;
        printf("U %u n=%u box=%u,%u,%u,%u ", hdv_compares, u, ux0, uy0, ux1, uy1); hdv_state(stdout); putchar('\n');
        if (!n) { hdv_write("act", hdv_compares, fb); hdv_write("ref", hdv_compares, hdv_ref); }
        hdv_write("ref2", hdv_compares, hdv_ref + 240 * 240);
    }
    memcpy(hdv_prev, fb, sizeof fb);
}

static void hdv_after_draw(void)
{
    ++hdv_draws;
    for (uint32_t k = 0; k < hdv_ndump; ++k)
        if (hdv_dump[k] == (long)hdv_draws) hdv_write("dump", hdv_draws, fb);
    if (hdv_nocmp) { printf("H %u %08x ", hdv_draws, hdv_hash(fb, 240 * 240)); hdv_state(stdout); putchar('\n'); }
    else hdv_compare();
}

static void hdv_advance(uint32_t ms)
{
    static int32_t block[2u * HALF_FRAMES];
    while (ms--) {
        ++fm1_ms;
        hdv_owed += FS;
        while (hdv_owed >= 1000u * HALF_FRAMES) {
            hdv_owed -= 1000u * HALF_FRAMES;
            memset(block, 0, sizeof block);
            app_audio(block, HALF_FRAMES);
        }
#if BR_PRODUCT_SMK37
        br_smk_fader_peek_tick();                     /* the device panel scan ends a peek (smk37_panel.c) */
#endif
        br_app_tick();
        if (br_ui.last_draw == fm1_ms && !br_intro_until) hdv_after_draw();
    }
}

/* 'L prof ...' lines call this marker, so a profiler can split its output (callgrind --dump-before=hdv_mark). */
static __attribute__((noinline)) void hdv_mark(void) { __asm__ volatile("" ::: "memory"); }
static __attribute__((noinline)) void hdv_bench_draws(uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) hdv_full_redraw();
}
/* N forced full redraws of the current state: counters (instrumented builds) and host time per redraw. In a forked
 * child so the session state is untouched, unless HDV_BENCH_NOFORK is set (profiling). */
static void hdv_bench(uint32_t n)
{
    int nofork = getenv("HDV_BENCH_NOFORK") != 0;
    pid_t pid = nofork ? 0 : fork();
    if (!pid) {
        struct timespec a, b;
#ifdef HDV_COUNTERS
        hdv_blend_calls = hdv_blend_px = hdv_blend_oob = hdv_get_oob = hdv_rect_px = hdv_pset_px = hdv_fill_px = 0;
        ui_test_lcd_pixel_bytes = 0; ui_test_lcd_blits = 0;
#endif
        clock_gettime(CLOCK_MONOTONIC, &a);
        hdv_bench_draws(n);
        clock_gettime(CLOCK_MONOTONIC, &b);
        double us = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / 1e3 / n;
#ifdef HDV_COUNTERS
        printf("B per_redraw blend_calls=%llu blend_px=%llu blend_oob=%llu get_oob=%llu rect_px=%llu pset_px=%llu "
               "fill_px=%llu lcd_px=%llu blits=%u host_us=%.1f bad=%llu ",
               hdv_blend_calls / n, hdv_blend_px / n, hdv_blend_oob / n, hdv_get_oob / n, hdv_rect_px / n, hdv_pset_px / n,
               hdv_fill_px / n, (unsigned long long)(ui_test_lcd_pixel_bytes / 2 / n), ui_test_lcd_blits / n, us, hdv_bad_write);
#else
        printf("B per_redraw host_us=%.1f ", us);
#endif
        hdv_state(stdout); putchar('\n'); fflush(stdout);
        if (!nofork) _exit(0);
        return;
    }
    int st; waitpid(pid, &st, 0);
}

int main(void)
{
    char line[256];
    hdv_ref = mmap(0, 2 * 240 * 240 * 2, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (hdv_ref == MAP_FAILED) return 2;
    hdv_out = getenv("HDV_OUT");
    if (hdv_out) mkdir(hdv_out, 0755);
    hdv_nocmp = getenv("HDV_NOCMP") != 0;
    if (getenv("HDV_NO_RESYNC")) hdv_resync = 0;
    if (getenv("HDV_DUMP")) {
        char *s = getenv("HDV_DUMP");
        while (*s && hdv_ndump < 16) { hdv_dump[hdv_ndump++] = strtol(s, &s, 10); if (*s == ',') ++s; }
    }
    setvbuf(stdout, 0, _IOLBF, 0);
#ifdef DINGHY_DEVICE_SAVE_TEST
    memset(host_project_flash, 0xff, sizeof host_project_flash);
#endif
    if (!host_init_resources()) return 1;
    br_app_init();
    fm1_ms = 2000; br_intro_until = 0; br_ui.message_until = 0; br_ui.redraw = 1; br_draw(); br_leds();
    br_ui.last_draw = fm1_ms;
    hdv_after_draw();
    while (fgets(line, sizeof line, stdin)) {
        int a = 0, b = 0; char op = 0;
        line[strcspn(line, "\n")] = 0;
        if (line[0] == 'L' && line[1] == ' ') {
            snprintf(hdv_label, sizeof hdv_label, "%s", line + 2);
            if (!strncmp(hdv_label, "prof", 4)) {
                hdv_mark();
#ifdef HDV_COUNTERS
                /* cumulative drawing work at each mark (tools/hd_verify.py 'steadycost' takes differences) */
                printf("P %u blend=%llu rect=%llu pset=%llu lcd_bytes=%llu blits=%u draws=%u |%s|\n", fm1_ms, hdv_blend_px,
                       hdv_rect_px, hdv_pset_px, (unsigned long long)ui_test_lcd_pixel_bytes, ui_test_lcd_blits, hdv_draws, hdv_label);
#endif
            }
            continue;
        }
        if (sscanf(line, " %c %d %d", &op, &a, &b) < 1) continue;
        if (op == 'Q') break;
        if (op == 't') hdv_advance((uint32_t)(a > 0 ? a : 0));
        else if (op == 'e' && a >= 0 && a < 7) { host_encoder[a] += b; hdv_advance(20); }
        else if (op == 'b' && a >= 0 && a < 14) {
            uint32_t bit = 1u << a;
            fm1_in.buttons |= bit; host_pressed |= bit; hdv_advance(40);
            fm1_in.buttons &= ~bit; host_released |= bit; hdv_advance(40);
        } else if (op == 'h' && a >= 0 && a < 14) {
            uint32_t bit = 1u << a;
            if (b && !(fm1_in.buttons & bit)) { fm1_in.buttons |= bit; host_pressed |= bit; }
            else if (!b && (fm1_in.buttons & bit)) { fm1_in.buttons &= ~bit; host_released |= bit; }
            hdv_advance(30);
        } else if (op == 'n' && a >= 0 && a < 27) {
            uint32_t bit = 1u << a;
            if (b) { host_note_pressed |= bit & ~fm1_in.notes; fm1_in.notes |= bit; }
            else fm1_in.notes &= ~bit;
            hdv_advance(30);
        } else if (op == 'p' && a >= 0 && a < BR_PARTS) { br_ui.part = (uint32_t)a; br_ui.redraw = 1; hdv_advance(2); }
        else if (op == 'v' && a >= 0 && a < BR_PAGE_COUNT) {
            br_ui.page = (uint32_t)a; br_ui.screen[a] = (uint32_t)br_clip(b, 0, (int32_t)br_screen_count() - 1);
            br_ui.redraw = 1; hdv_advance(2);
        }
#if BR_PRODUCT_SMK37
        else if (op == 'P') {                         /* SMK-37 fader peek on for b ms (1) or off (0), as a fader move */
            if (a) { br_smk_fader_peek.active = 1u; br_smk_fader_peek.until = fm1_ms + (uint32_t)(b > 0 ? b : 1500);
                     br_smk_fader_peek.page = br_ui.page; br_smk_fader_peek.screen = br_ui.screen[br_ui.page];
                     br_smk_fader_peek.part = br_ui.part; }
            else br_smk_fader_peek_cancel();
            hdv_advance(2);
        }
        else if (op == 'I') {                         /* the SMK text intro, as at boot (then the first full screen) */
            br_intro_until = fm1_ms + BR_INTRO_MS; br_draw_layout_old = 0xffffffffu; br_ui.redraw = 1; hdv_advance(2);
        }
#endif
#ifdef BR_PATTERN_SONG_EXPERIMENT
        else if (op == 'S') {                         /* pattern slots: used mask, queued slot (>= count: none) */
            br_ps_state.used = (uint16_t)a; br_ps_state.queued = (uint8_t)b; br_ui.redraw = 1; hdv_advance(2);
        }
#endif
        else if (op == 'c') { if (!hdv_nocmp) hdv_compare(); }
        else if (op == 'x') { fb[a % (240 * 240)] ^= 0x1234u; hdv_compare(); }   /* self-test: corrupt one LCD pixel */
        else if (op == 'B') hdv_bench((uint32_t)(a > 0 ? a : 1));
    }
    printf("S draws=%u compares=%u mismatch_events=%u mismatch_px=%llu unowned_px=%llu end_ms=%u\n", hdv_draws, hdv_compares,
           hdv_mismatch_events, hdv_mismatch_px, hdv_unowned_px, fm1_ms);
#ifdef HDV_COUNTERS
    printf("C blend_oob_total=%llu get_oob_total=%llu bad_write=%llu chain_joins=%llu stale_chain=%llu stale_chain_px=%llu\n", hdv_blend_oob,
           hdv_get_oob, hdv_bad_write, hdv_chain_total, hdv_stale_chain, hdv_stale_px);
#endif
    return 0;
}
