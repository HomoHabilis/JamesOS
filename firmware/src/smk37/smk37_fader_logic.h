/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SMK37_FADER_LOGIC_H
#define SMK37_FADER_LOGIC_H
#include <stdint.h>
#include "smk37_panel_logic.h"
typedef struct {
    uint8_t previous[4], picked[4], last_applied[4];
    uint8_t bank, ready, link;
} smk_fader_state_t;
static inline void smk_fader_reset_pickup(smk_fader_state_t *s, const uint8_t values[4])
{
    for (uint32_t i = 0; i < 4u; ++i) { s->previous[i] = values[i]; s->picked[i] = 0u; }
}
/* Returns 1 on movement, 2 when the movement has picked up the target. */
static inline uint32_t smk_fader_move(smk_fader_state_t *s, uint32_t i,
                                      uint32_t value, uint32_t target)
{
    uint32_t old = s->previous[i];
    s->previous[i] = (uint8_t)value;
    if (s->picked[i] && target != s->last_applied[i]) s->picked[i] = 0u;
    if (old == value) return 0u;
    if (!s->picked[i] && !((old <= target && value >= target) ||
                           (old >= target && value <= target))) return 1u;
    s->picked[i] = 1u; s->last_applied[i] = (uint8_t)value;
    return 2u;
}

/* NOTE REPEAT + SCALE held together for 0.8 s toggles the fader link. Whichever button goes down
 * first acts at once, as before; the second one's press is held back (SCALE's in fm1_input_edges,
 * via smk37_panel.c smk_scale_hold_back) while both are down. Let go of either before 0.8 s: the held-back
 * press is delivered then. At 0.8 s: the link toggles, the held-back press is dropped and a NOTE
 * REPEAT that went first has its punch overlay closed again; nothing more until both are up. */
enum { SMK_CHORD_IDLE, SMK_CHORD_PENDING, SMK_CHORD_DONE };
enum { SMK_CHORD_TOGGLE = 1u, SMK_CHORD_REPEAT_PRESS = 2u, SMK_CHORD_SCALE_PRESS = 4u,
       SMK_CHORD_REPEAT_UNDO = 8u };
#define SMK_CHORD_MS 800u
typedef struct { uint32_t start; uint8_t state, scale, repeat, undo; } smk_chord_t;
/* repeat_pressed: a NOTE REPEAT press edge now; scale_held_back: the input layer held a SCALE press
 * back (NOTE REPEAT was down). Returns SMK_CHORD_*: REPEAT_PRESS = act on a NOTE REPEAT press now,
 * SCALE_PRESS = deliver the held-back SCALE press now. */
static inline uint32_t smk_chord_step(smk_chord_t *c, uint32_t repeat_pressed, uint32_t scale_held_back,
                                      uint32_t repeat_held, uint32_t scale_held, uint32_t now)
{
    uint32_t out = 0u;
    if (c->state == SMK_CHORD_IDLE) {
        if (scale_held_back || (repeat_pressed && scale_held)) {
            c->state = SMK_CHORD_PENDING; c->start = now;
            c->scale = scale_held_back != 0u; c->repeat = repeat_pressed != 0u;
            c->undo = scale_held_back && !repeat_pressed;          /* NOTE REPEAT went first */
        } else if (repeat_pressed) out = SMK_CHORD_REPEAT_PRESS;
    }
    if (c->state == SMK_CHORD_PENDING) {
        if (!repeat_held || !scale_held) {
            if (c->scale) out |= SMK_CHORD_SCALE_PRESS;
            if (c->repeat && repeat_held) out |= SMK_CHORD_REPEAT_PRESS;
            c->state = SMK_CHORD_IDLE;
        } else if (now - c->start >= SMK_CHORD_MS) {
            out |= SMK_CHORD_TOGGLE | (c->undo ? SMK_CHORD_REPEAT_UNDO : 0u);
            c->state = SMK_CHORD_DONE;
        }
    } else if (c->state == SMK_CHORD_DONE && !repeat_held && !scale_held) c->state = SMK_CHORD_IDLE;
    return out;
}

/* ---- The app side (include after the UI: needs br_part, br_message, the punch keys). ---- */
#if defined(BR_PUNCH_SHIFT_KEY) && BR_PRODUCT_SMK37
static void br_smk_level_set(uint32_t part, uint32_t value)
{
    br_part[part].revision++;
    br_part[part].sound.level = value;
    RING_PUBLISH();
    br_part[part].revision++;
    br_ui.redraw = 1u;
}

/* A kit lane's level, as KIT LEVEL sets it. */
static void br_smk_lane_set(uint32_t lane, uint32_t value)
{
    br_part[BR_DRUM_PART].revision++;
    br_part[BR_DRUM_PART].lane_level[lane] = (uint8_t)value;
    RING_PUBLISH();
    br_part[BR_DRUM_PART].revision++;
    br_ui.redraw = 1u;
}

/* "Fader link": on, T2 takes T1's level and
 * kit lane 2 takes lane 1's now; fader 1 then sets both in either bank. */
static void br_smk_fader_link_toggle(void)
{
    br_smk_fader_link ^= 1u;
    if (br_smk_fader_link && br_part[1].sound.level != br_part[0].sound.level)
        br_smk_level_set(1u, br_part[0].sound.level);
    if (br_smk_fader_link && br_part[BR_DRUM_PART].lane_level[1] != br_part[BR_DRUM_PART].lane_level[0])
        br_smk_lane_set(1u, br_part[BR_DRUM_PART].lane_level[0]);
    br_message(br_smk_fader_link ? "FADER LINK ON" : "FADER LINK OFF");
    br_ui.redraw = 1u;
}

/* smk_retrig_t / br_smk_retrig (the page the drum track's RETRIGGER page was opened from by NOTE REPEAT)
 * live in instrument_ui.c: br_retrig_page() reads them. */

static void br_smk_retrig_return(smk_retrig_t *r)
{
    if (r->opened && br_retrig_page()) {
        br_ui.page = r->page; br_ui.screen[r->page] = r->screen;
        br_ui.edit_key = 0xffffffffu; br_ui.redraw = 1u;
    }
    r->opened = 0u;
}

/* A NOTE REPEAT press. Returns SMK_REPEAT_PUNCH when the caller is to run the punch shift. */
static uint32_t br_smk_repeat_press(smk_retrig_t *r, uint32_t blocked)
{
    uint32_t shown = br_retrig_page();
    uint32_t act = smk_repeat_decide(br_ui.part == BR_DRUM_PART, shown, r->opened && shown, blocked);
    if (act == SMK_REPEAT_OPEN_RETRIG) {
        r->page = (uint8_t)br_ui.page; r->screen = (uint8_t)br_ui.screen[br_ui.page]; r->opened = 1u;
        br_ui.page = BR_PAGE_PATTERN; br_ui.screen[BR_PAGE_PATTERN] = 0u;
        br_ui.edit_key = 0xffffffffu; br_ui.redraw = 1u;
    } else if (act == SMK_REPEAT_RETURN) br_smk_retrig_return(r);
    return act;
}

/* One panel tick of the chord. Returns 1 when a NOTE REPEAT press is to act now. */
static uint32_t br_smk_link_chord(smk_chord_t *c, uint32_t repeat_pressed, uint8_t *scale_held_back,
                                  uint8_t *scale_late, uint32_t repeat_held, uint32_t scale_held)
{
    uint32_t act = smk_chord_step(c, repeat_pressed, *scale_held_back, repeat_held, scale_held, fm1_ms);
    *scale_held_back = 0u;
    if (act & SMK_CHORD_SCALE_PRESS) *scale_late = 1u;
    if (act & SMK_CHORD_TOGGLE) {
        if (act & SMK_CHORD_REPEAT_UNDO) {
            if (br_punch_key(&br_punch_ks, BR_PUNCH_SHIFT_KEY, 0u)) br_punch_changed();
            br_smk_retrig_return(&br_smk_retrig);       /* a RETRIGGER page it opened closes again too */
        }
        br_smk_fader_link_toggle();
    }
    return (act & SMK_CHORD_REPEAT_PRESS) != 0u;
}

/* Bank 0: the track levels T1 T2 T3 DK; bank 1: the kit's four lane levels. With the link on,
 * fader 1 sets the first two of its bank (T1 and T2, or kit lanes 1 and 2) and fader 2 is ignored
 * entirely (no value, no peek, no pickup). */
static void br_smk_faders_step(smk_fader_state_t *s, const uint8_t values[4], uint32_t bank_press)
{
    if (bank_press && br_smk_fader_bank_select(s->bank ^ 1u)) {
        s->bank ^= 1u;
        smk_fader_reset_pickup(s, values);
    }
    if (s->link != br_smk_fader_link) {         /* switched (chord or LOAD): fader 2 picks up anew */
        s->link = br_smk_fader_link;
        s->previous[1] = values[1]; s->picked[1] = 0u;
    }
    for (uint32_t i = 0; i < 4u; ++i) {
        if (i == 1u && s->link) continue;               /* fader 2: ignored in both banks while linked */
        uint32_t target = s->bank ? br_part[BR_DRUM_PART].lane_level[i] : br_part[i].sound.level;
        uint32_t moved = smk_fader_move(s, i, values[i], target);
        if (!moved) continue;
        br_smk_fader_moved(s->bank);
        if (moved != 2u) continue;
        if (s->bank) {
            br_smk_lane_set(i, values[i]);
            if (!i && s->link) br_smk_lane_set(1u, values[i]);
        } else {
            br_smk_level_set(i, values[i]);
            if (!i && s->link) br_smk_level_set(1u, values[i]);
        }
    }
}
#endif
#endif
