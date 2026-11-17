/* SPDX-License-Identifier: GPL-3.0-only */
/* Origin: Dinghy (zvenson), built on Felucca (Leo Kuroshita, Hügelton Instruments); modified for JamesOS.
 * GPL-3.0-only. See docs/legal/THIRD-PARTY-NOTICES.md. */
/* A fake FM-1 on a computer: the app (firmware/src/dinghy.c) runs unchanged on it. platform.c includes
 * this file instead of the hardware when DINGHY_HOST is set. A script presses keys over time; out come
 * the screen (PPM, at the times SHOT asks) and the sound (WAV, as the FM-1 sends it over USB).
 *
 *   mkdir -p build/host
 *   cc -O2 -Wall -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/dinghy firmware/src/dinghy.c
 *   build/host/dinghy build/host/dinghy        (-> build/host/dinghy.wav, build/host/dinghy-<ms>.ppm)
 *   build/host/dinghy OUT script.txt [FPS]    (your own script; with FPS: OUT-NNNNN.ppm, a frame every 1/FPS s:
 *                                             a film of your firmware, e.g. ffmpeg -r 30 -i OUT-%05d.ppm ...)
 *
 * Change SCRIPT below to try your own app. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/hal/fm1_cpu1_worker.h"

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS FM1_AUDIO_RATE_HZ
#define HALF_FRAMES FM1_AUDIO_HALF_FRAMES

static volatile uint32_t fm1_ms;
static int32_t host_battery_raw = 600; /* deterministic full-battery preview */
static int32_t fm1_battery_level(void) { return fm1_battery_bars_from_adc(host_battery_raw); }
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_pressed, host_released, host_note_pressed;
static int32_t host_encoder[7];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_pressed;
    host_pressed = 0;
    *released = host_released;
    host_released = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { uint32_t b = host_note_pressed; host_note_pressed = 0; return b; }
static uint32_t fm1_input_note_snapshot(uint32_t *held)
{
    uint32_t b = host_note_pressed;
    host_note_pressed = 0;
    if (held) *held = fm1_in.notes;
    return b;
}
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t n = e < 7u ? host_encoder[e] : 0;
    if (e < 7u) host_encoder[e] = 0;
    return n;
}
static uint8_t host_led_state[FM1_LED_COUNT];
static uint8_t host_led_level_state[FM1_LED_COUNT];
#ifdef LED_WRITE_TEST_COUNTERS
static uint32_t host_led_changes[FM1_LED_COUNT];
#endif
static void fm1_led_level(uint32_t id, fm1_led_level_t level)
{
    if (id >= sizeof host_led_state || level < FM1_LED_OFF || level > FM1_LED_BRIGHT)
        return;
#ifdef LED_WRITE_TEST_COUNTERS
    if (host_led_level_state[id] != (uint8_t)level) ++host_led_changes[id];
#endif
    host_led_level_state[id] = (uint8_t)level;
    host_led_state[id] = level == FM1_LED_BRIGHT;
}
static void fm1_led_key(uint32_t id, int on)
{
    fm1_led_level(id, on ? FM1_LED_BRIGHT : FM1_LED_OFF);
}

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
#ifdef UI_HOME_DOT_WATCH
static uint16_t host_dot_expected[8u * 240u];
static uint32_t host_dot_watch, host_dot_changes, host_dot_presence_watch, host_dot_missing;
static void host_dot_observe(void)
{
    if (host_dot_presence_watch) {
        uint32_t seen[2] = {0u, 0u};
        for (uint32_t y=200u; y<208u; ++y) for (uint32_t x=112u; x<128u; ++x)
            if (fb[y*240u+x] != 0u) seen[(x-112u)/8u]++;
        if (!seen[0] || !seen[1]) ++host_dot_missing;
    }
    if (host_dot_watch && memcmp(host_dot_expected, fb + 200u * 240u, sizeof host_dot_expected))
        ++host_dot_changes;
}
#endif
#ifdef UI_REFRESH_TEST_COUNTERS
static uint64_t ui_test_lcd_pixel_bytes;
static uint32_t ui_test_lcd_blits;
#endif
#ifdef UI_CUE_FLICKER_TEST
static uint32_t host_blits_cross_cue,host_cue_blits;
#endif
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    BR_UIT_LCD_BYTES((x < 240u && y < 240u) ? (x + w > 240u ? 240u - x : w) * (y + h > 240u ? 240u - y : h) * 2u : 0u);
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
#ifdef UI_HOME_DOT_WATCH
    host_dot_observe();
#endif
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    if (x + w > 240u || y + h > 240u) { fprintf(stderr, "out of bounds blit %u %u %u %u\n", x,y,w,h); abort(); }
    BR_UIT_LCD_BYTES(w * h * 2u);
#ifdef UI_CUE_FLICKER_TEST
    if (y < 216u && y + h > 216u) ++host_blits_cross_cue;
    if (y == 216u && h == 24u) ++host_cue_blits;
#endif
#ifdef UI_REFRESH_TEST_COUNTERS
    ui_test_lcd_pixel_bytes += (uint64_t)w * h * 2u;
    ++ui_test_lcd_blits;
#endif
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
#ifdef UI_HOME_DOT_WATCH
    host_dot_observe();
#endif
}
#define CV_MAX (240u * 40u)
#include "gfx.c"

#define HOST_SETTINGS_PAYLOAD_CAPACITY (4096u - 3u * sizeof(uint32_t))
static uint8_t host_store[HOST_SETTINGS_PAYLOAD_CAPACITY + 4u];
static uint32_t host_store_len;
static int settings_load(void *p, uint32_t n)
{
    if (host_store[0] != 1 || n > HOST_SETTINGS_PAYLOAD_CAPACITY || n != host_store_len) return -1;
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > HOST_SETTINGS_PAYLOAD_CAPACITY) return -1;
    host_store[0] = 1;
    host_store_len = n;
    memcpy(host_store + 4, p, n);
    printf("%6u ms  settings saved (%u B)\n", fm1_ms, n);
    return 0;
}

static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
/* Played-note timing: a fake 24 MHz timer (host_ticks: platform_poll keeps it at fm1_ms, a test may set
 * it finer), each MIDI message's arrival and each note key's last change, as the device records them. */
/* A message or key change a test puts in place directly, without a stamp, counts as arriving now. */
static uint32_t host_ticks, mq_time[64], midi_last_timestamp, host_note_tick[27], host_note_stamped;
static uint8_t mq_stamped[64];
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    midi_last_timestamp = mq_stamped[mq_r % 64u] ? mq_time[mq_r % 64u] : host_ticks;
    mq_stamped[mq_r % 64u] = 0u;
    mq_r++;
    return 1;
}
static uint32_t host_note_key_tick(uint32_t k)
{
    return k < 27u && !(((fm1_in.notes ^ host_note_stamped) >> k) & 1u) ? host_note_tick[k] : host_ticks;
}
#define BR_NOTE_NOW() host_ticks
#define BR_NOTE_MIDI_TICK() midi_last_timestamp
#define BR_NOTE_KEY_TICK(k) host_note_key_tick(k)
#ifdef MIDI_OUT_TEST_CAPTURE
static uint32_t host_midi_out_count, host_midi_out_last_status, host_midi_out_last_note;
#endif
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) {
#ifdef MIDI_OUT_TEST_CAPTURE
    ++host_midi_out_count; host_midi_out_last_status = st; host_midi_out_last_note = d1;
#endif
    printf("%6u ms  MIDI out %02X %u %u\n", fm1_ms, st, d1, d2);
}

static uint32_t app_audio(int32_t *out, uint32_t n);
static void app_main(void);

/* At t ms: keys MASK, btn ID (one-shot), buttons MASK (held-state), enc ID DELTA,
 * midi ST D1 D2, shot, or end. MASK accepts decimal or 0x-prefixed hex. */
enum { EV_KEYS, EV_BTN, EV_BUTTONS, EV_ENC, EV_MIDI, EV_SHOT };
typedef struct { uint32_t t; int kind; int32_t a, b, c; } host_ev_t;
static const host_ev_t SCRIPT0[] = {
    {200, EV_KEYS, 1u << 7, 0, 0},                               /* C4 */
    {700, EV_KEYS, 1u << 7 | 1u << 11 | 1u << 14, 0, 0},        /* + E4 G4: a chord */
    {1500, EV_KEYS, 0, 0, 0},
    {1700, EV_MIDI, 0x90, 81, 120},                             /* MIDI in: A5 */
    {2300, EV_MIDI, 0x80, 81, 0},
    {2500, EV_BTN, BTN_OCT_UP, 0, 0},
    {2700, EV_KEYS, 1u << 2 | 1u << 9, 0, 0},                   /* G4 + D5, an octave up */
    {3400, EV_KEYS, 0, 0, 0},
};
static const host_ev_t *script = SCRIPT0;
static uint32_t script_n = sizeof SCRIPT0 / sizeof SCRIPT0[0], end_ms = 6500, frame_fps;
static const uint32_t SHOT[] = {1000, 3000};
static uint32_t requested_shot, shot_pending;
static uint32_t host_tap_release;

static void read_script(const char *path)
{
    static host_ev_t ev[4096];
    char line[256], what[16];
    FILE *f = fopen(path, "r");
    uint32_t n = 0;
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f) && n < 4096u) {
        host_ev_t e = {0, 0, 0, 0, 0};
        unsigned a = 0, b = 0, c = 0;
        if (line[0] == '#' || sscanf(line, "%u %15s", &e.t, what) != 2) continue;
        if (!strcmp(what, "end")) { end_ms = e.t; continue; }
        if (!strcmp(what, "keys")) { e.kind = EV_KEYS; sscanf(line, "%*u %*s %i", &e.a); }
        else if (!strcmp(what, "btn")) { e.kind = EV_BTN; sscanf(line, "%*u %*s %d", &e.a); }
        else if (!strcmp(what, "buttons")) { e.kind = EV_BUTTONS; sscanf(line, "%*u %*s %i", &e.a); }
        else if (!strcmp(what, "enc")) { e.kind = EV_ENC; sscanf(line, "%*u %*s %u %d", &a, &e.b); e.a = (int32_t)a; }
        else if (!strcmp(what, "midi")) { e.kind = EV_MIDI; sscanf(line, "%*u %*s %i %u %u", &e.a, &b, &c); e.b = (int32_t)b; e.c = (int32_t)c; }
        else if (!strcmp(what, "shot")) { e.kind = EV_SHOT; }
        else continue;
        ev[n++] = e;
    }
    fclose(f);
    script = ev;
    script_n = n;
}

static const char *host_out = "build/host/dinghy";
static int16_t *wav;
static uint32_t wav_n;

static void write_ppm_as(const char *name)
{
    FILE *f;
    uint32_t i;
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}
static void write_ppm(uint32_t t)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%u.ppm", host_out, t);
    write_ppm_as(name);
    printf("%6u ms  screen -> %s\n", t, name);
}

static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
}

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t step, shot, owed, frame;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
    host_ticks = fm1_ms * 24000u;
    if (host_tap_release) {
        fm1_in.buttons &= ~host_tap_release;
        host_released |= host_tap_release;
        host_tap_release = 0;
    }
    if (shot_pending) { write_ppm(requested_shot); shot_pending = 0; }
    while (step < script_n && script[step].t <= fm1_ms) {
        const host_ev_t *e = &script[step];
        if (e->kind == EV_MIDI) {
            mq[mq_w % 64u][0] = (uint8_t)e->a; mq[mq_w % 64u][1] = (uint8_t)e->b; mq[mq_w % 64u][2] = (uint8_t)e->c;
            mq_time[mq_w % 64u] = host_ticks; mq_stamped[mq_w % 64u] = 1u;
            mq_w++;
        } else if (e->kind == EV_KEYS) {
            uint32_t k;
            for (k = 0; k < 27u; ++k)
                if ((((uint32_t)e->a ^ fm1_in.notes) >> k) & 1u) host_note_tick[k] = host_ticks;
            host_note_pressed |= (uint32_t)e->a & ~(fm1_in.notes);
            fm1_in.notes = host_note_stamped = (uint32_t)e->a;
        } else if (e->kind == EV_BTN && e->a >= 0 && e->a < 32) {
            uint32_t bit = 1u << (uint32_t)e->a;
            fm1_in.buttons |= bit;
            host_pressed |= bit;
            host_tap_release |= bit;
        }
        else if (e->kind == EV_BUTTONS) {
            uint32_t next = (uint32_t)e->a;
            host_pressed |= next & ~fm1_in.buttons;
            host_released |= fm1_in.buttons & ~next;
            fm1_in.buttons = next;
        } else if (e->kind == EV_ENC && e->a >= 0 && e->a < 7)
            host_encoder[e->a] += e->b;
        else if (e->kind == EV_SHOT) {
            requested_shot = e->t;
            shot_pending = 1;
        }
        step++;
    }
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        wav = realloc(wav, (wav_n + HALF_FRAMES) * 4u);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];                 /* (USB audio at the MASTER knob all the way up) */
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += HALF_FRAMES;
    }
    if (frame_fps) {                            /* a film: a frame every 1/fps s, as the screen is then */
        if ((uint64_t)frame * 1000u <= (uint64_t)fm1_ms * frame_fps) {
            char name[512];
            snprintf(name, sizeof name, "%s-%05u.ppm", host_out, frame++);
            write_ppm_as(name);
        }
    } else if (shot < sizeof SHOT / sizeof SHOT[0] && fm1_ms == SHOT[shot])
        write_ppm(SHOT[shot++]);
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

/* Match the device boot order: decode shared immutable resources before voices.
 * The host may create multiple app instances, so initialize at most once. */
static int host_init_resources(void)
{
#if FM1_PLAITS_PACK_WAVES && (!defined(BR_ENABLE_PLAITS) || BR_ENABLE_PLAITS)
    static int ready;
    if (!ready) {
        if (!fm1_plaits_waves_init(NULL)) return 0;
        ready = 1;
    }
#endif
    return 1;
}

int main(int argc, char **argv)
{
    if (!host_init_resources()) { fprintf(stderr, "Host waveform resource initialization failed.\n"); return 1; }
    if (argc > 1) host_out = argv[1];
    if (argc > 2) read_script(argv[2]);
    if (argc > 3) frame_fps = (uint32_t)atoi(argv[3]);
    app_main();
    return 0;
}
