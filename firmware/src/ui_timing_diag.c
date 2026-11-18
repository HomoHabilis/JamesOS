/* SPDX-License-Identifier: GPL-3.0-only */
/* UI timing diagnostic (BR_UI_TIMING_DIAG=1 builds only; dinghy.c includes this file only then): counters
 * for the main loop, drawing, the LCD link, frames, played notes and input-to-screen, plus remote panel
 * input. Commands (fm1_app_command, any diagnostics level; tools/ui_timing.py):
 *
 *   0xB5 read   (empty body): 4 + 4 * BR_UIT_WORDS bytes; word 0 = 'U','T',1,BR_UIT_WORDS, then the
 *               words of ui_timing_diag.h in order (word 1 + BR_UIT_PASSES ...). Read-only. (29 words
 *               since the region counters; a reader takes the count from byte 3.)
 *   0xB6 reset  (body 'U','T',1): zeroes the counters; reply 1 byte = 1. Touches nothing else
 *               (not the 0x60/0x61 audio counters).
 *   0xB7 input  (body 'U','T',1,kind,index,v0,v1): reply 3 bytes: status, SAVE mode (es.mode), injector
 *               states (slot 0 | slot 1 << 4). kind 1 = button tap: index 0..13 (BTN_*), hold v0 | v1 << 8
 *               ms (0..5000) after the press registers. kind 3 = note key: index 0..26 (the key scan's
 *               note keys, white and black), hold as kind 1. kind 2 = encoder turn: index 0..6 (ENC_*),
 *               v0 = signed detents (-32..32, not 0), v1 = 0. Two buttons or keys may be held at once (a
 *               chord): a second is accepted while the first is in its hold.
 *               status 1 accepted; 2 refused: the SAVE page is open and the button is not HOME (keys:
 *               any); 3 refused: busy (both slots in use, the first not yet in its hold, or the same
 *               button); 4 refused: SAVE while REC is held (ours or a finger's) or pending, or REC while a
 *               SAVE of ours is in flight (REC + SAVE is a quick save); 5 refused: SAVE with a hold;
 *               6 refused: bad kind, index or value.
 *   0xB8 where  (empty body): 36 bytes: 'U','W',1, page, screen of that page, track (part), SAVE mode
 *               (0 = closed), playing, recording, injector states (slot 0 | slot 1 << 4; each 0 idle,
 *               1 pressing, 2 holding, 3 releasing), last injector outcome (as 0xB7's status; 7 = the press did not register
 *               in 100 ms, 8 = the hold was cut short because the SAVE page opened), buttons held
 *               (2 bytes LE), a frame going out, then the header title (22 bytes, NUL-padded).
 *
 * Definitions. A pass: from one br_app_tick call to the next (platform_poll, the input, the drawing).
 * Drawing: the time inside br_draw_pass_with (both the page and SAVE-screen drawers); compute = that
 * time less the LCD time that fell inside it. LCD time: lcd.c blocked on the link (lcd_sync waiting for
 * the running DMA, lcd_cmd, starting a lcd_data transfer); a transfer that runs while the loop works
 * costs nothing here. A frame: from the first br_draw_pass_with call after the last frame completed to
 * the call that returns 0 (nothing left to send); a frame running at a reset is not counted. An input
 * event: a pass that takes a button press or release edge (after debounce) or finds an encoder detent
 * waiting; input-to-screen: from the first such pass not yet covered to the end of the first frame that
 * starts at or after it (that frame is drawn from state that includes the event).
 *
 * Remote input goes in where the panel's does. On the device a button is held closed at the scan's
 * debouncer (hal/fm1_input.h fm1_input_diag_held), so it is pressed and released through the same
 * debounce, edges and held state (fm1_in.buttons) as a finger; an encoder turn adds detents where the
 * quadrature decoder puts them (fm1_in.enc_steps). On the host, tests/host.c's button and encoder state.
 * SAVE-page safety, three layers, for both slots: (1) 0xB7 refuses every button but HOME (and every key)
 * while es.mode != 0, SAVE with a hold, SAVE while REC is held (ours or a finger's) or pending, and REC
 * while a SAVE of ours is in flight; (2) while a button of ours is in flight (and 250 ms
 * after) br_uit_input strips its press and release edges (all but HOME) from the pass's input whenever
 * the SAVE page is open, and SAVE's press whenever REC is held or pending, before es_input sees them;
 * (3) a hold in progress is let go at once if the SAVE page opens. Encoder turns are always allowed. */
#if BR_PRODUCT_SMK37 && defined(FM1_DEVICE)
#error "BR_UI_TIMING_DIAG: FM-1 only"
#endif
#define BR_UIT_MS_TICKS 24000u
#if defined(DINGHY_EMULATOR_SAVE) || defined(FM1_DEVICE) || defined(DINGHY_DEVICE_SAVE_TEST)
#define BR_UIT_SAVE_OPEN() (es.mode != 0)
#define BR_UIT_REC_PENDING() (es.rec_pending != 0)
#define BR_UIT_SAVE_MODE() ((uint8_t)es.mode)
#else
#define BR_UIT_SAVE_OPEN() 0
#define BR_UIT_REC_PENDING() 0
#define BR_UIT_SAVE_MODE() 0u
#endif
#if defined(FM1_DEVICE)
#define BR_UIT_PRESS(bit) (fm1_input_diag_held |= (bit))
#define BR_UIT_LET_GO(bit) (fm1_input_diag_held &= ~(bit))
#define BR_UIT_NOTE_PRESS(bit) (fm1_input_diag_notes |= (bit))
#define BR_UIT_NOTE_LET_GO(bit) (fm1_input_diag_notes &= ~(bit))
#define BR_UIT_TURN(e, n) fm1_input_diag_turn((e), (n))
#define BR_UIT_ENC_WAITING() fm1_input_diag_steps_waiting()
#else
#define BR_UIT_PRESS(bit) (fm1_in.buttons |= (bit), host_pressed |= (bit))
#define BR_UIT_LET_GO(bit) (fm1_in.buttons &= ~(bit), host_released |= (bit))
#define BR_UIT_NOTE_PRESS(bit) (host_note_pressed |= (bit), fm1_in.notes |= (bit))
#define BR_UIT_NOTE_LET_GO(bit) (fm1_in.notes &= ~(bit))
#define BR_UIT_TURN(e, n) (host_encoder[(e)] += (n))
static uint32_t br_uit_host_steps_waiting(void)
{
    for (uint32_t e = 0u; e < 7u; ++e) if (host_encoder[e]) return 1u;
    return 0u;
}
#define BR_UIT_ENC_WAITING() br_uit_host_steps_waiting()
#endif
enum { BR_UIT_IDLE, BR_UIT_PRESSING, BR_UIT_HOLDING, BR_UIT_RELEASING };

static void br_uit_max(uint32_t word, uint32_t v) { if (v > br_uit.c[word]) br_uit.c[word] = v; }

static void br_uit_reset(void)
{
    memset(br_uit.c, 0, sizeof br_uit.c);
    br_uit.reset_ms = fm1_ms;
    br_uit.frame_valid = 0u;                 /* a frame already going out is not counted */
    br_uit.has_pending = br_uit.has_armed = 0u;
}

/* Buttons of ours whose edges the SAVE guard may strip (HOME is always allowed through). */
static uint32_t br_uit_guard(void)
{
    uint32_t bits = 0u;
    for (uint32_t i = 0u; i < 2u; ++i)
        if (!br_uit.inj_note[i]) bits |= 1u << br_uit.inj_btn[i];
    if (!br_uit.inj_state[0] && !br_uit.inj_state[1] && (int32_t)(br_uit.guard_until - fm1_ms) <= 0) return 0u;
    return bits & ~(1u << BTN_HOME);
}

static void br_uit_let_go(uint32_t i, uint32_t status)
{
    uint32_t bit = 1u << br_uit.inj_btn[i];
    if (br_uit.inj_note[i]) BR_UIT_NOTE_LET_GO(bit); else BR_UIT_LET_GO(bit);
    br_uit.inj_state[i] = BR_UIT_RELEASING; br_uit.inj_t[i] = fm1_ms;
    if (status) br_uit.last_status = (uint8_t)status;
}

static void br_uit_service(void)
{
    for (uint32_t i = 0u; i < 2u; ++i) {
        uint32_t bit = 1u << br_uit.inj_btn[i], down = ((br_uit.inj_note[i] ? fm1_in.notes : fm1_in.buttons) & bit) != 0u;
        switch (br_uit.inj_state[i]) {
        case BR_UIT_PRESSING:
            if (down) { br_uit.inj_state[i] = BR_UIT_HOLDING; br_uit.inj_t[i] = fm1_ms; }
            else if (fm1_ms - br_uit.inj_t[i] > 100u) br_uit_let_go(i, 7u);
            break;
        case BR_UIT_HOLDING:
            if (BR_UIT_SAVE_OPEN() && (br_uit.inj_note[i] || br_uit.inj_btn[i] != BTN_HOME)) br_uit_let_go(i, 8u);
            else if (fm1_ms != br_uit.inj_t[i] && fm1_ms - br_uit.inj_t[i] >= br_uit.inj_hold[i]) br_uit_let_go(i, 0u);
            break;
        case BR_UIT_RELEASING:
            if (!down || fm1_ms - br_uit.inj_t[i] > 100u) {
                br_uit.inj_state[i] = BR_UIT_IDLE; br_uit.guard_until = fm1_ms + 250u;
            }
            break;
        default:
            break;
        }
    }
}

static void br_uit_loop_pass(void)
{
    uint32_t now = BR_NOTE_NOW();
    if (br_uit.have_last) {
        uint32_t dt = now - br_uit.last_pass;
        ++br_uit.c[BR_UIT_PASSES];
        br_uit_max(BR_UIT_PASS_WORST, dt);
        if (dt > 5u * BR_UIT_MS_TICKS) ++br_uit.c[BR_UIT_PASS_OVER5];
        if (dt > 10u * BR_UIT_MS_TICKS) ++br_uit.c[BR_UIT_PASS_OVER10];
    }
    br_uit.last_pass = now; br_uit.have_last = 1u;
    br_uit_service();
}

static void br_uit_input(uint32_t *buttons, uint32_t *released)
{
    uint32_t guard = br_uit_guard();
    if (guard && BR_UIT_SAVE_OPEN()) { *buttons &= ~guard; *released &= ~guard; }
    if ((guard & (1u << BTN_SAVE)) && (BR_UIT_REC_PENDING() || (fm1_in.buttons & (1u << BTN_REC))))
        *buttons &= ~(1u << BTN_SAVE);
    if ((*buttons | *released) || BR_UIT_ENC_WAITING()) {
        ++br_uit.c[BR_UIT_INPUT_PASSES];
        if (!br_uit.has_pending) { br_uit.input_pending = BR_NOTE_NOW(); br_uit.has_pending = 1u; }
    }
}

/* Drawing regions (instrument_ui.c br_open): a region runs from its go-ahead to the next question or the
 * pass's end. Its place: the questions asked so far in this pass (the drawing order of that screen). */
static void br_uit_region_open(void)
{
    br_uit.region_t0 = BR_DRAW_NOW(); br_uit.region_open = 1u; br_uit.region_open_ord = br_uit.region_ord;
}
static void br_uit_region_close(void)
{
    uint32_t dt;
    if (!br_uit.region_open) return;
    br_uit.region_open = 0u;
    dt = BR_DRAW_NOW() - br_uit.region_t0;
    if (dt > br_uit.c[BR_UIT_REGION_WORST]) {
        br_uit.c[BR_UIT_REGION_WORST] = dt;
        br_uit.c[BR_UIT_REGION_WHERE] = (uint32_t)br_ui.page << 24 | (uint32_t)(br_ui.screen[br_ui.page] & 255u) << 16 |
            (uint32_t)(BR_UIT_SAVE_MODE() & 15u) << 12 | (br_uit.region_open_ord & 4095u);
    }
}

static void br_uit_draw_begin(void)
{
    br_uit.region_ord = 0u; br_uit.pass_timed = 0u;
    br_uit.draw_t0 = BR_NOTE_NOW();
    br_uit.draw_lcd0 = br_uit.c[BR_UIT_LCD_TICKS];
    br_uit.draw_bytes0 = br_uit.c[BR_UIT_BYTES];
    if (!br_uit.in_frame) {
        br_uit.in_frame = br_uit.frame_valid = 1u;
        br_uit.frame_start = br_uit.draw_t0;
        br_uit.frame_bytes0 = br_uit.draw_bytes0;
        if (br_uit.has_pending) {
            br_uit.input_armed = br_uit.input_pending; br_uit.has_armed = 1u; br_uit.has_pending = 0u;
        }
    }
}

static void br_uit_draw_end(uint32_t more)
{
    uint32_t now = BR_NOTE_NOW(), dt = now - br_uit.draw_t0;
    uint32_t lcd = br_uit.c[BR_UIT_LCD_TICKS] - br_uit.draw_lcd0;
    ++br_uit.c[BR_UIT_DRAW_PASSES];
    br_uit_max(BR_UIT_DRAW_WORST, dt);
    if (br_uit.pass_timed) ++br_uit.c[BR_UIT_PASS_TIMED];
    br_uit.c[BR_UIT_DRAW_TICKS] += dt;
    br_uit.c[BR_UIT_LCD_IN_DRAW] += lcd;
    br_uit.c[BR_UIT_DRAW_COMPUTE] += dt > lcd ? dt - lcd : 0u;
    if (br_uit.c[BR_UIT_BYTES] != br_uit.draw_bytes0) ++br_uit.c[BR_UIT_DRAW_SENDING];
    if (more) return;
    br_uit.in_frame = 0u;
    if (br_uit.frame_valid) {
        uint32_t span = now - br_uit.frame_start;
        ++br_uit.c[BR_UIT_FRAMES];
        br_uit.c[BR_UIT_FRAME_TOTAL] += span;
        br_uit_max(BR_UIT_FRAME_WORST, span);
        if (br_uit.c[BR_UIT_BYTES] != br_uit.frame_bytes0) ++br_uit.c[BR_UIT_FRAMES_SENDING];
    }
    if (br_uit.has_armed) {
        uint32_t lag = now - br_uit.input_armed;
        ++br_uit.c[BR_UIT_INPUTS];
        br_uit.c[BR_UIT_INPUT_TOTAL] += lag;
        br_uit_max(BR_UIT_INPUT_WORST, lag);
        br_uit.has_armed = 0u;
    }
}

static void br_uit_note(uint32_t arrival, uint32_t now)
{
    uint32_t lag = now - arrival;
    if ((int32_t)lag < 0 || lag > 200u * BR_UIT_MS_TICKS) { ++br_uit.c[BR_UIT_NOTE_STALE]; return; }
    ++br_uit.c[BR_UIT_NOTES];
    br_uit.c[BR_UIT_NOTE_TOTAL] += lag;
    br_uit_max(BR_UIT_NOTE_WORST, lag);
    if (lag > 6u * BR_UIT_MS_TICKS) ++br_uit.c[BR_UIT_NOTE_OVER6];
}

static void br_uit_word(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t br_uit_inject(const uint8_t *body)
{
    uint32_t kind = body[3], index = body[4], value = (uint32_t)body[5] | (uint32_t)body[6] << 8, i, other;
    if (kind == 1u || kind == 3u) {
        if (index >= (kind == 1u ? 14u : 27u) || value > 5000u) return 6u;
        /* a free slot; a second press only while the first is in its hold, and not the same one */
        i = br_uit.inj_state[0] ? 1u : 0u; other = 1u - i;
        if (br_uit.inj_state[i] || (br_uit.inj_state[other] &&
            (br_uit.inj_state[other] != BR_UIT_HOLDING || (br_uit.inj_btn[other] == index && br_uit.inj_note[other] == (kind == 3u)))))
            return 3u;
        if (BR_UIT_SAVE_OPEN() && (kind == 3u || index != BTN_HOME)) return 2u;
        if (kind == 1u && index == BTN_SAVE && value) return 5u;
        if (kind == 1u && index == BTN_SAVE && (BR_UIT_REC_PENDING() || (fm1_in.buttons & (1u << BTN_REC)) ||
            (br_uit.inj_state[other] && !br_uit.inj_note[other] && br_uit.inj_btn[other] == BTN_REC))) return 4u;
        if (kind == 1u && index == BTN_REC && br_uit.inj_state[other] && !br_uit.inj_note[other] && br_uit.inj_btn[other] == BTN_SAVE)
            return 4u;
        br_uit.inj_btn[i] = (uint8_t)index; br_uit.inj_note[i] = kind == 3u; br_uit.inj_hold[i] = (uint16_t)value;
        br_uit.inj_state[i] = BR_UIT_PRESSING; br_uit.inj_t[i] = fm1_ms;
        if (kind == 3u) BR_UIT_NOTE_PRESS(1u << index); else BR_UIT_PRESS(1u << index);
        return 1u;
    }
    if (kind == 2u) {
        int32_t detents = (int8_t)body[5];
        if (index >= 7u || body[6] || !detents || detents > 32 || detents < -32) return 6u;
        BR_UIT_TURN(index, detents);
        return 1u;
    }
    return 6u;
}

/* -1: not one of ours; 0: refused without a reply (malformed); 1: reply in reply[0..*reply_n). */
static int br_uit_command(uint32_t cmd, const uint8_t *body, uint32_t n, uint8_t reply[128], uint32_t *reply_n)
{
    uint32_t i;
    int ours = n == 3u && body && body[0] == 'U' && body[1] == 'T' && body[2] == 1u;
    if (cmd == 0xB5u) {
        if (n) return 0;
        reply[0] = 'U'; reply[1] = 'T'; reply[2] = 1u; reply[3] = BR_UIT_WORDS;
        br_uit.c[BR_UIT_ELAPSED_MS] = fm1_ms - br_uit.reset_ms;
        for (i = 0u; i < BR_UIT_WORDS; ++i) br_uit_word(reply + 4u + 4u * i, br_uit.c[i]);
        *reply_n = 4u + 4u * BR_UIT_WORDS;
        return 1;
    }
    if (cmd == 0xB6u) {
        if (!ours) return 0;
        br_uit_reset();
        reply[0] = 1u; *reply_n = 1u;
        return 1;
    }
    if (cmd == 0xB7u) {
        if (n != 7u || !body || body[0] != 'U' || body[1] != 'T' || body[2] != 1u) return 0;
        reply[0] = (uint8_t)br_uit_inject(body);
        if (body[3] != 2u) br_uit.last_status = reply[0];
        reply[1] = BR_UIT_SAVE_MODE(); reply[2] = (uint8_t)(br_uit.inj_state[0] | br_uit.inj_state[1] << 4);
        *reply_n = 3u;
        return 1;
    }
    if (cmd == 0xB8u) {
        const char *title;
        if (n) return 0;
        memset(reply, 0, 36u);
        reply[0] = 'U'; reply[1] = 'W'; reply[2] = 1u;
        reply[3] = (uint8_t)br_ui.page; reply[4] = (uint8_t)br_ui.screen[br_ui.page];
        reply[5] = (uint8_t)br_ui.part; reply[6] = BR_UIT_SAVE_MODE();
        reply[7] = (uint8_t)(br_playing != 0u); reply[8] = (uint8_t)(br_recording != 0u);
        reply[9] = (uint8_t)(br_uit.inj_state[0] | br_uit.inj_state[1] << 4); reply[10] = br_uit.last_status;
        reply[11] = (uint8_t)fm1_in.buttons; reply[12] = (uint8_t)(fm1_in.buttons >> 8);
        reply[13] = br_draw_more;
        title = br_header_title();
        for (i = 0u; i < 21u && title && title[i]; ++i) reply[14u + i] = (uint8_t)title[i];
        *reply_n = 36u;
        return 1;
    }
    return -1;
}
