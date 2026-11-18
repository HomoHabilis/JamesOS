/* SPDX-License-Identifier: GPL-3.0-only */
/* Screen reader adapter (BR_CAP_READER, firmware/product.h): tells a screen reader program on the
 * computer what the screen shows, over USB-MIDI SysEx, in the community protocol version 1
 * ("F0 7D 53 52", docs/SCREEN-READER.md). Written for JamesOS from the protocol's description;
 * it shares no code with other implementations.
 *
 * Included once by dinghy.c, after the UI and the SAVE menu, so it reads their own strings:
 *   br_reader_frame(p, n)  a received SysEx (the bytes between F0 and F7); 1 = it was ours
 *                          (platform.c, before the updater looks at the frame)
 *   br_reader_tick()       once per UI pass (br_app_tick); one flag test while nobody listens
 *
 * What is said, always from the strings the screen draws:
 *   a new screen   its title, then the selected item (list row, mixer strip, sequencer step,
 *                  SAVE menu choice), the four knobs "LABEL VALUE", then the OCT-/+ hint
 *   the selection  the item (a list row with its place, "4 OF 128"; a step with its knobs)
 *   a knob         "LABEL VALUE" of the knob being turned
 *   a message      the transient message line, at once
 * Pages and knobs are said once they have been still for 200 / 120 ms, and at least every
 * 500 ms while they keep changing. Sends are at least 50 ms apart and never wait: with no
 * room in the USB SysEx queue the announcement stays pending and is tried again. */
#if BR_CAP_READER
#define RD_TEXT_MAX 170u                /* a SAY frame is at most 177 bytes: 59 USB-MIDI packets */
#define RD_SILENT_MS 5000u              /* no HELLO for this long: stop */
enum { RD_SCREEN = 1u, RD_FOCUS = 2u, RD_KNOB = 4u };
enum { RD_HELLO = 1u, RD_BYE = 2u, RD_WHERE = 3u, RD_SAY = 16u };
enum { RD_NOW = 1u };
#if defined(DINGHY_EMULATOR_SAVE) || defined(FM1_DEVICE) || defined(DINGHY_DEVICE_SAVE_TEST)
#define RD_SAVE_MENU 1                  /* this build has the SAVE menu (es) */
#else
#define RD_SAVE_MENU 0
#endif

static struct {
    uint32_t heard, due, first, sent, said_msg;   /* fm1_ms of the last HELLO, pending send, ... */
    uint32_t ctx, focus, knob, msg;               /* what was last seen (knob: its edit time) */
    uint8_t on, pend, knob_k;
} br_reader
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
__attribute__((section(".pool"), aligned(4), used))   /* zeroed spare pool (as br_sel): .data/.bss */
#endif                                          /* stay as they were; kept whole (used): base + offset */
;

/* ---- the wire ---- */
#if !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static int rd_wire(const uint8_t *p, uint32_t n)       /* never waits: no room, not sent */
{
    if (!usb.config || usb.suspended || SXQ - (so_w - so_r) < (n + 2u) / 3u + 1u)
        return -1;                                      /* (+1: a cut-off frame's F7 may go first) */
    return ota_wire_send(p, n);
}
#else
/* Host and web builds: the frames sent, for tests (rd_host_full simulates a full queue). */
static uint8_t rd_host_out[4096];
static uint32_t rd_host_len, rd_host_full;
static int rd_wire(const uint8_t *p, uint32_t n)
{
    if (rd_host_full || rd_host_len + n > sizeof rd_host_out) return -1;
    memcpy(rd_host_out + rd_host_len, p, n);
    rd_host_len += n;
    return 0;
}
#endif

/* ---- text ---- */
typedef struct { uint8_t b[6u + RD_TEXT_MAX + 2u]; uint32_t n; const char *gap; } rd_msg_t;  /* header, text, (0,) F7 */

static void rd_begin(rd_msg_t *m) { m->n = 6u; m->gap = 0; }
static void rd_put(rd_msg_t *m, const char *s)          /* printable 7-bit ASCII only, cut at the limit */
{
    if (*s && m->gap) { const char *g = m->gap; m->gap = 0; rd_put(m, g); }
    while (*s && m->n < 6u + RD_TEXT_MAX) {
        uint8_t c = (uint8_t)*s++;
        m->b[m->n++] = c >= 0x20u && c < 0x7Fu ? c : ' ';
    }
}
static void rd_sep(rd_msg_t *m, const char *s)          /* a separator, written only if more text follows; */
{                                                       /* one already waiting stays (". " before ", ") */
    if (m->n > 6u && !m->gap) m->gap = s;
}
static void rd_trim(rd_msg_t *m)                         /* drop trailing spaces (the padded SAVE name) */
{
    while (m->n > 6u && m->b[m->n - 1u] == ' ') --m->n;
}
static void rd_num(rd_msg_t *m, int32_t v)
{
    char t[12];
    br_number(t, v); rd_put(m, t);
}
static uint32_t rd_hash(const rd_msg_t *m)
{
    uint32_t i, h = 2166136261u;
    for (i = 6u; i < m->n; ++i) h = (h ^ m->b[i]) * 16777619u;
    return h;
}

#if RD_SAVE_MENU
static const char *rd_save_title(void)                  /* es_draw's titles */
{
    return es.mode==1?"SAVE":es.mode==2?"SAVE FIRST?":es.mode==3?"CHOOSE SLOT":es.mode==4?"SAVE AS":
           es.mode==5?"OVERWRITE":"CONFIRM";
}
#endif

static uint32_t rd_mixer(void)                          /* MIXER and KIT LEVEL: strips in dB */
{
    return (br_ui.page == BR_PAGE_MIX && !br_ui.screen[BR_PAGE_MIX]) || br_gate_page();
}
static uint32_t rd_seq(void)
{
    return br_seq_step_edit() || (br_ui.page == BR_PAGE_SEQ && !br_ui.screen[BR_PAGE_SEQ]);
}

/* The knob's label and value, as its card draws them ("" when the slot is empty). */
static void rd_knob(rd_msg_t *m, uint32_t k)
{
    char text[32];
    br_control_t c = br_control(k);
    if (!c.label[0]) return;
    if (rd_mixer()) br_level_db_text(text, (uint32_t)c.value);
    else br_control_text(k, &c, text);
    rd_put(m, c.label); rd_put(m, " "); rd_put(m, text);
}
static void rd_knobs(rd_msg_t *m, uint32_t skip)        /* all four but skip (the one already said) */
{
    uint32_t k;
    for (k = 0u; k < 4u; ++k) if (k != skip) { rd_sep(m, ", "); rd_knob(m, k); }
    m->gap = 0;
}

/* The selected item: list row, mixer strip, step, SAVE menu choice. Returns 1 when the four knobs
 * belong with it (a sequencer step), 2 + k for mixer strip k (said already). */
static uint32_t rd_focus(rd_msg_t *m)
{
    char text[24];
#if RD_SAVE_MENU
    if (es.mode) {
        static const char *const actions[4] = {"QUICKSAVE", "SAVE AS", "LOAD", "INIT"};
        static const char *const choices[3] = {"SAVE", "DISCARD", "CANCEL"};
        es_record r;
        if (es.mode == 1) rd_put(m, actions[es.action & 3]);
        else if (es.mode == 2) rd_put(m, choices[es.choice % 3]);
        else {
            es_slot_string(text, es.slot, " ", sizeof text);
            rd_put(m, text);                            /* "02 ", then the row or the name */
            if (es.mode == 3) {
                rd_put(m, es_latest(es.slot, &r, 0) ? r.name : "EMPTY");
                rd_put(m, ", "); rd_num(m, es.slot + 1); rd_put(m, " OF "); rd_num(m, ES_SLOTS);
            } else if (es.mode != 5) rd_put(m, es.name);
        }
        rd_trim(m);
        return 0u;
    }
#endif
    if (br_sel_browse()) {
        uint32_t p = br_ui.part, cur = br_sel.cursor[p];
        br_sel_row_text(text, cur);
        rd_put(m, text); rd_put(m, ", "); rd_num(m, (int32_t)cur + 1); rd_put(m, " OF ");
        rd_num(m, (int32_t)(p == BR_DRUM_PART ? BR_KIT_BANK_COUNT : BR_BANK_COUNT));
    } else if (rd_mixer()) {                            /* the cue bar's strip and dB */
        uint32_t k = br_gate_page() ? br_ui.lane & 3u : br_ui.part;
        rd_knob(m, k);
        return 2u + k;
    } else if (rd_seq()) {
        uint32_t edit = br_seq_step_edit();
        rd_put(m, "STEP "); rd_num(m, (int32_t)(edit ? br_ui.step_press_step : br_ui.cursor) + 1);
        if (edit ? br_ui.step_press_kit : br_is_kit(br_part[br_ui.part].sound.preset)) {
            uint32_t p = edit ? br_ui.step_press_part : br_ui.part;
            rd_put(m, " "); rd_put(m, br_drum_voice_label(br_part[p].sound.drum_voice_mask,
                                                          edit ? br_ui.step_press_lane : br_ui.lane));
        }
        return 1u;
    }
    return 0u;
}

static void rd_screen(rd_msg_t *m)
{
#if RD_SAVE_MENU
    if (es.mode) { rd_put(m, rd_save_title()); rd_sep(m, ". "); rd_focus(m); return; }
#endif
    rd_put(m, br_header_title());
    rd_sep(m, ". ");
#if BR_PRODUCT_SMK37
    if (br_arp_cue_page()) { rd_put(m, "SWITCH TO T1 T2 T3"); return; }
#endif
    uint32_t f = rd_focus(m);
    rd_sep(m, ". ");
    if (!br_sel_browse()) rd_knobs(m, f >= 2u ? f - 2u : 4u);
    rd_sep(m, ". ");
    if (br_seq_pages()) { rd_put(m, "OCT-/+: STEP BANK"); rd_sep(m, ". "); }   /* (the drawn cue alternates) */
    rd_put(m, br_oct_hint());
}

/* ---- sending ---- */
static int rd_send(rd_msg_t *m, uint32_t cmd, uint32_t flags)   /* text is already in m->b[6..] */
{
    m->b[0] = 0xF0; m->b[1] = 0x7D; m->b[2] = 0x53; m->b[3] = 0x52;
    m->b[4] = (uint8_t)cmd; m->b[5] = (uint8_t)flags;
    m->b[m->n++] = 0xF7;
    if (rd_wire(m->b, m->n)) return 0;
    br_reader.sent = fm1_ms;
    return 1;
}
static void rd_hello(void)
{
    rd_msg_t m;
    rd_begin(&m);
#ifdef FELUCCA_ID
    rd_put(&m, APP_NAME " " APP_VER " " FELUCCA_ID);   /* e.g. "JAMESOS 1.0b FM-1_9262263" */
#else
    rd_put(&m, APP_NAME " HOST");
#endif
    m.b[m.n++] = 0;                                     /* then the words file link: none */
    rd_send(&m, RD_HELLO, 1u);
}
static void rd_raise(uint32_t kind, uint32_t delay)
{
    if (!br_reader.pend) br_reader.first = fm1_ms;
    br_reader.pend |= (uint8_t)kind;
    br_reader.due = fm1_ms + delay;
    if (br_reader.due - br_reader.first > 500u) br_reader.due = br_reader.first + 500u;
}

static int br_reader_frame(const uint8_t *p, uint32_t n)
{
    if (n < 4u || p[0] != 0x7D || p[1] != 0x53 || p[2] != 0x52) return 0;
    if (p[3] == RD_HELLO) {
        if (!br_reader.on) {                                   /* a new listener: the whole screen */
            br_reader.on = 1u; br_reader.pend = 0u; br_reader.msg = br_ui.message_start; br_reader.ctx = ~0u;
            br_reader.said_msg = fm1_ms - 1000u;
        }
        br_reader.heard = fm1_ms;
        rd_hello();
    } else if (p[3] == RD_BYE) br_reader.on = 0u;
    else if (p[3] == RD_WHERE && br_reader.on) { br_reader.ctx = ~0u; br_reader.said_msg = fm1_ms - 1000u; }
    return 1;
}

static void br_reader_tick(void)
{
    rd_msg_t m;
    uint32_t key, k;
    if (!br_reader.on) return;
    if (fm1_ms - br_reader.heard > RD_SILENT_MS) { br_reader.on = 0u; br_reader.pend = 0u; return; }
    if (fm1_ms - br_reader.sent < 50u) return;
    /* the message line: said at once, cutting off what is being said */
    if (br_ui.message_start != br_reader.msg && br_ui.message[0]) {
        rd_begin(&m); rd_put(&m, br_ui.message);
        if (rd_send(&m, RD_SAY, RD_NOW)) { br_reader.msg = br_ui.message_start; br_reader.said_msg = fm1_ms; }
        return;
    }
    /* the screen */
    rd_begin(&m);
    rd_put(&m, br_header_title());
    key = rd_hash(&m) ^ (br_ui.page << 24) ^ (br_ui.screen[br_ui.page] << 16) ^ (br_ui.part << 8) ^
#if RD_SAVE_MENU
          ((uint32_t)es.mode << 28) ^
#endif
          (br_seq_step_edit() << 5);
    if (key != br_reader.ctx) {
        uint32_t first = br_reader.ctx == ~0u;
        br_reader.ctx = key; br_reader.knob = br_ui.edit_time; br_reader.focus = ~0u;
        rd_raise(RD_SCREEN, first ? 0u : 200u);
    }
    /* the selection */
    rd_begin(&m); rd_focus(&m);
    key = rd_hash(&m);
    if (br_reader.focus != ~0u && key != br_reader.focus) rd_raise(RD_FOCUS, 120u);
    br_reader.focus = key;
    /* the knob being turned (lists and the SAVE menu say it as the selection) */
    if (br_ui.edit_time != br_reader.knob) {                   /* a knob edit (instrument_ui.c stamps each) */
        br_reader.knob = br_ui.edit_time;
        k = br_active_knob();
        if (k < 4u && !br_sel_browse()
#if RD_SAVE_MENU
            && !es.mode
#endif
           ) {
            br_reader.pend &= (uint8_t)~RD_FOCUS;              /* a mixer strip: the knob says it */
            br_reader.knob_k = (uint8_t)k;
            rd_raise(RD_KNOB, 120u);
        }
    }
    if (!br_reader.pend || (int32_t)(fm1_ms - br_reader.due) < 0) return;
    rd_begin(&m);
    if (br_reader.pend & RD_SCREEN) rd_screen(&m);
    else {
        if ((br_reader.pend & RD_FOCUS) && rd_focus(&m) == 1u) { rd_sep(&m, ". "); rd_knobs(&m, 4u); }
        else if (br_reader.pend & RD_KNOB) { rd_sep(&m, ". "); rd_knob(&m, br_reader.knob_k); }
    }
    /* nothing to say (an empty slot); right after a message, queue behind it instead of cutting it off */
    if (m.n == 6u || rd_send(&m, RD_SAY, fm1_ms - br_reader.said_msg < 1000u ? 0u : RD_NOW)) br_reader.pend = 0u;
}
#endif /* BR_CAP_READER */
