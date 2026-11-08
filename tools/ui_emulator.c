/* Local browser bridge for the real application and host display. No MIDI I/O. */
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
/* Host diagnostics must never share the framed display protocol on stdout. */
static int bridge_host_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vfprintf(stderr, format, args);
    va_end(args);
    return result;
}
#define DINGHY_HOST 1
#ifndef DINGHY_DEVICE_SAVE_TEST
#define DINGHY_EMULATOR_SAVE 1
#endif
#define main host_unused_main
#define printf bridge_host_printf
#include "../firmware/src/dinghy.c"
#undef printf
#undef main

static uint32_t bridge_owed, bridge_saved;
static void advance(uint32_t ms)
{
    static int32_t block[2u * HALF_FRAMES];
    while (ms--) {
        ++fm1_ms;
        bridge_owed += FS;
        while (bridge_owed >= 1000u * HALF_FRAMES) {
            bridge_owed -= 1000u * HALF_FRAMES;
            memset(block, 0, sizeof block);
            app_audio(block, HALF_FRAMES);
        }
        br_app_tick();
    }
}

static void quoted(const char *s)
{
    putchar('"');
    while (*s) {
        if (*s == '"' || *s == '\\') putchar('\\');
        if ((unsigned char)*s >= 32) putchar(*s);
        ++s;
    }
    putchar('"');
}

static void frame(void)
{
    printf("{\"part\":%u,\"page\":%u,\"screen\":%u,\"screens\":%u,\"preset\":%u,\"playing\":%u,\"recording\":%u,\"controls\":[",
           br_ui.part, br_ui.page, br_ui.screen[br_ui.page], br_screen_count(),
           br_part[br_ui.part].sound.preset, br_playing, br_recording);
    for (uint32_t k = 0; k < 4; ++k) {
        br_control_t c = br_control(k);
        char value[24]; br_control_text(k, &c, value);
        if (k) putchar(',');
        printf("{\"label\":"); quoted(c.label);
        printf(",\"text\":"); quoted(value);
        printf("}");
    }
    printf("],\"buttonMask\":%u,\"noteMask\":%u,\"sessionSaved\":%u,\"saveScreen\":%u,\"saveSlot\":%u,\"ledLevels\":[",
           fm1_in.buttons, fm1_in.notes, bridge_saved, es.mode != 0, (unsigned)(es.slot+1));
    for (uint32_t i = 0; i < 41; ++i) {
        if (i) putchar(',');
        printf("%u", host_led_level_state[i]);
    }
    printf("],\"messageActive\":%u",(unsigned)(br_ui.message[0] && (int32_t)(br_ui.message_until-fm1_ms)>0));
#ifdef DINGHY_DEVICE_SAVE_TEST
    printf(",\"saveCurrent\":%d,\"saveStage\":%d,\"saveCursor\":%d,\"saveName\":",es.current,es.mode,es.cursor);quoted(es.name);
    printf(",\"saveError\":");quoted(es.error);
#endif
    printf("}\n");
    static uint8_t rgb[240u * 240u * 3u];
    for (uint32_t i = 0; i < 240u * 240u; ++i) {
        uint16_t c = fb[i];
        rgb[i*3] = (uint8_t)(((c >> 11) & 31u) * 255u / 31u);
        rgb[i*3+1] = (uint8_t)(((c >> 5) & 63u) * 255u / 63u);
        rgb[i*3+2] = (uint8_t)((c & 31u) * 255u / 31u);
    }
    fwrite(rgb, 1, sizeof rgb, stdout); fflush(stdout);
}

int main(int argc, char **argv)
{
#ifdef DINGHY_DEVICE_SAVE_TEST
    memset(host_project_flash,0xff,sizeof host_project_flash);
#endif
    if (!host_init_resources()) { fprintf(stderr, "Host waveform resource initialization failed.\n"); return 1; }
    br_app_init();
#ifndef DINGHY_DEVICE_SAVE_TEST
    (void)mkdir(ES_PATH, 0700);
    if (!br_capture(&es.factory)) return 4;
#endif
    if (argc > 1) {
        br_session_t session;
        FILE *f = fopen(argv[1], "rb");
        if (f) {
            if (fread(&session, 1, sizeof session, f) != sizeof session ||
                !br_restore(&session)) return 3;
#ifndef DINGHY_DEVICE_SAVE_TEST
            es_restore_identity(&session);
#endif
            fclose(f);
        }
    }
    fm1_ms = 2000; br_ui.message_until = 0; br_ui.redraw = 1; br_draw(); br_leds();
    char line[128], op;
    int a, b, c;
    while (fgets(line, sizeof line, stdin)) {
        a = b = c = 0;
        if (sscanf(line, " %c %d %d %d", &op, &a, &b, &c) < 1) continue;
        if (op == 't') advance((uint32_t)br_clip(a, 0, 250));
        else if (op == 'e' && a >= 0 && a < 7) {
            host_encoder[a] += br_clip(b, -16, 16); advance(2);
        } else if (op == 'b' && a >= 0 && a < 14) {
            uint32_t bit = 1u << a;
            if (!(fm1_in.buttons & bit)) {
                fm1_in.buttons |= bit; host_pressed |= bit; advance(2);
                fm1_in.buttons &= ~bit; host_released |= bit; advance(2);
            } else advance(2);
        } else if (op == 'h' && a >= 0 && a < 14) {
            uint32_t bit = 1u << a;
            if (b && !(fm1_in.buttons & bit)) {
                fm1_in.buttons |= bit;
                host_pressed |= bit;
            } else if (!b && (fm1_in.buttons & bit)) {
                fm1_in.buttons &= ~bit;
                host_released |= bit;
            }
            advance(2);
        } else if (op == 'n' && a >= 0 && a < 27) {
            uint32_t bit = 1u << a;
            if (b) { host_note_pressed |= bit & ~fm1_in.notes; fm1_in.notes |= bit; }
            else fm1_in.notes &= ~bit;
            advance(2);
        } else if (op == 'p' && a >= 0 && a < BR_PARTS) {
            br_ui.part = (uint32_t)a; br_ui.redraw = 1; advance(2);
        } else if (op == 'v' && a >= 0 && a < BR_PAGE_COUNT) {
            br_ui.page = (uint32_t)a;
            br_ui.screen[a] = (uint32_t)br_clip(b, 0, (int32_t)br_screen_count()-1);
            br_ui.redraw = 1; advance(2);
        } else if (op == 's' && argc > 2) {
            br_session_t session;
            bridge_saved = 0;
            if (br_capture(&session)) {
                char tmp[1024];
                int n = snprintf(tmp, sizeof tmp, "%s.tmp", argv[2]);
                if (n > 0 && (size_t)n < sizeof tmp) {
                    FILE *f = fopen(tmp, "wb");
                    if (f) {
                        int ok = fwrite(&session, 1, sizeof session, f) == sizeof session;
                        if (fclose(f)) ok = 0;
                        if (ok && !rename(tmp, argv[2])) {
#ifdef DINGHY_DEVICE_SAVE_TEST
                            bridge_saved=1;
#else
                            bridge_saved = es_save_identity(&session);
#endif
                        }
                        else remove(tmp);
                    }
                }
            }
        }
#ifdef DINGHY_DEVICE_SAVE_TEST
        else if(op=='z') br_app_init();
        else if(op=='f') host_project_fail=a;
#endif
        if(es.mode)es_draw();else br_draw(); frame();
    }
    return 0;
}
