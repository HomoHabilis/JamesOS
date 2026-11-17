/* SPDX-License-Identifier: GPL-3.0-only */
/* SMK-37 controls in JamesOS's foreground, where edits are serialized with audio. */
#include "smk37_punch_pads.h"
#include "smk37_fader_logic.h"

static void br_smk_outer_keys(void)
{
    uint16_t held;
    uint16_t pressed = smk_outer_snapshot(&held);
    uint32_t part = br_ui.part;
    int kit = br_is_kit(br_part[part].sound.preset);
    for (uint32_t i = 0; i < 10u; ++i) {
        uint32_t slot = 27u + i;
        uint32_t bit = 1u << i;
        uint32_t physical = i < 5u ? i : i + 27u;
        uint32_t note = kit ? br_drum_lane_note(physical % BR_DRUM_LANES) :
            (uint32_t)(48 + (int32_t)physical + 12 * br_ui.octave);
        br_note_played(smk_key_tick(physical), BR_NOTE_NOW());   /* played-note timing (instrument_ui.c) */
        if (br_ui.held_note[slot] && (!(held & bit) || (pressed & bit)))
            br_key_up(slot);
        if (!(pressed & bit)) { br_note_played_end(); continue; }
        if (br_seq_grid_page() && !kit && !br_step_press_pending()) {
            br_ui.note[part] = note;
            br_ui.seq_default[part][0].note = (uint8_t)note;
            br_ui.redraw = 1;
        }
        br_play_key(slot, (held & bit) != 0u, part, note, fm1_in.key_vel[physical]);
        br_note_played_end();
    }
}

/* The printed SCALE button is the logical LFO button. */
#define SMK_LOGICAL_SCALE BTN_LFO
/* fm1_input_edges (smk37_input.h): a SCALE (LFO) press while NOTE REPEAT is down is held back; the
 * chord (smk37_fader_logic.h) delivers it late (on the next call) if let go early, or drops it. */
static uint8_t smk_scale_held_back, smk_scale_late;
static void smk_scale_hold_back(uint32_t *pressed, uint32_t held)
{
    if ((*pressed & (1u << SMK_LOGICAL_SCALE)) && (held & (1u << SMK_BTN_REPEAT))) {
        *pressed &= ~(1u << SMK_LOGICAL_SCALE); smk_scale_held_back = 1u;
    }
    if (smk_scale_late) { *pressed |= 1u << SMK_LOGICAL_SCALE; smk_scale_late = 0u; }
}

static void br_smk_faders(uint32_t buttons)
{
    static smk_fader_state_t state;
    uint8_t values[4];
    for (uint32_t i = 0; i < 4u; ++i) values[i] = fm1_in.fader[i];
    br_smk_fader_peek_tick();
    if (!state.ready) {
        if (fm1_in.adc_ok < 960u) return;
        smk_fader_reset_pickup(&state, values);
        state.link = br_smk_fader_link;
        state.ready = 1u;
    }
    br_smk_faders_step(&state, values, buttons & (1u << SMK_BTN_FADER_BANK));
    fm1_led[SMK_BUTTON_PHYSICAL[SMK_BTN_FADER_BANK]] = state.bank;
}

static void br_smk_drum_pads(uint16_t pressed, uint16_t held, const uint8_t velocity[SMK_NPAD])
{
    static uint16_t playing_pads, generation[BR_DRUM_LANES];
    static uint8_t sent[BR_DRUM_LANES];
    for (uint32_t lane = 0; lane < BR_DRUM_LANES; ++lane) {
        uint32_t mask = 0x1111u << lane, note = br_drum_lane_note(lane);
        uint16_t before = playing_pads;
        uint16_t hits = pressed & mask;
        uint32_t last = 0u, last_valid = 0u;   /* the lane's latest pad change: when its note ends */
        for (uint32_t pad = lane; pad < SMK_NPAD; pad += BR_DRUM_LANES) {
            uint32_t tick = smk_pad_tick_get(pad);
            if (!last_valid || (int32_t)(tick - last) > 0) { last = tick; last_valid = 1u; }
            if (!(hits & (1u << pad))) continue;
            br_note_played(tick, BR_NOTE_NOW());
            br_engine_note_on(BR_DRUM_PART, note, velocity[pad]);
            generation[lane] = br_record_note_begin(BR_DRUM_PART, note, velocity[pad], 1u, lane, 0u);
            br_note_played_end();
            if (br_midi_channels[BR_SYNTH_PARTS + lane]) {
                midi_send(0x90u | BR_DRUM_PART, note, velocity[pad]);
                sent[lane] = 1u;
            }
        }
        playing_pads = (uint16_t)((playing_pads | hits) & held);
        if (((before | hits) & mask) && !(playing_pads & mask)) {
            br_note_played(last, BR_NOTE_NOW());
            br_engine_note_off(BR_DRUM_PART, note);
            if (generation[lane]) br_record_note_finish(BR_DRUM_PART, note, generation[lane]);
            br_note_played_end();
            if (sent[lane]) midi_send(0x80u | BR_DRUM_PART, note, 0u);
            sent[lane] = 0u;
            generation[lane] = 0u;
        }
    }
}

/* SMK wheel travel is much narrower than the ADC range. Learn the pitch
 * centre after ADC startup, with a dead band and independent end stops. */
static void br_smk_expression(void)
{
    static int32_t center, low, high;
    static int32_t mod_low = 474, mod_high = 629;
    static uint32_t next, previous = BR_PARTS;
    static int32_t bend_sent = 8192, mod_sent, pedal_sent;
    if (fm1_in.frames < 300u || (int32_t)(fm1_ms - next) < 0) return;
    next = fm1_ms + 8u;
    int32_t pedal = fm1_in.pedal < 640u ? 127 : 0;
    br_smk_pedal_set(pedal != 0);
    int32_t pitch = fm1_in.wheel[0], mod = fm1_in.wheel[1];
    if (!pitch || !mod) return;
    if (!center) {
        center = pitch;
        low = pitch - 80;
        high = pitch + 80;
    }
    if (pitch < low) low = pitch;
    if (pitch > high) high = pitch;
    if (mod < mod_low) mod_low = mod;
    if (mod > mod_high) mod_high = mod;
    int32_t delta = pitch - center;
    int32_t bend = delta > 12 ? (delta - 12) * 8191 / (high - center - 12) :
        delta < -12 ? (delta + 12) * 8192 / (center - low - 12) : 0;
    int32_t amount = br_clip((mod_high - mod - 12) * 127 / (mod_high - mod_low - 12), 0, 127);
    uint32_t p = br_ui.part;
    if (p != previous && previous < BR_SYNTH_PARTS) {
        br_smk_bend_q7[previous] = 0;
        br_smk_mod_wheel[previous] = 0;
        if (br_midi_channels[previous]) {
            midi_send(0xE0u | previous, 0u, 64u);
            midi_send(0xB0u | previous, 1u, 0u);
            midi_send(0xB0u | previous, 64u, 0u);
        }
    }
    if (p < BR_SYNTH_PARTS) {
        br_smk_bend_q7[p] = bend / 32; /* +/- two semitones, Q7 */
        br_smk_mod_wheel[p] = (uint8_t)amount;
        bend += 8192;
        if (br_midi_channels[p]) {
            if (bend != bend_sent || p != previous) midi_send(0xE0u | p, bend & 127, bend >> 7);
            if (amount != mod_sent || p != previous) midi_send(0xB0u | p, 1u, amount);
            if (pedal != pedal_sent || p != previous) midi_send(0xB0u | p, 64u, pedal);
        }
        bend_sent = bend;
        mod_sent = amount;
        pedal_sent = pedal;
    }
    previous = p;
}

static void br_smk_controls(uint32_t buttons, uint32_t released, uint32_t *keys, uint32_t *edges)
{
    static uint32_t seq_key_held;
    static uint16_t editor_pads;
    static smk_punch_pads_t punch_pads;
    static uint8_t previous_mode;
    static const uint8_t pattern_keys[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
    uint8_t velocity[SMK_NPAD];
    uint16_t held;
    uint16_t pressed = smk_pad_snapshot(&held, velocity);
    static smk_chord_t chord;
    uint32_t down = fm1_in.buttons;
    /* NOTE REPEAT's LED shows the RETRIGGER page (written as FADER BANK's is; the hardware LED is unverified). */
    fm1_led[SMK_BUTTON_PHYSICAL[SMK_BTN_REPEAT]] = !es.mode && br_retrig_page();
    if (!br_retrig_page()) br_smk_retrig.opened = 0u;   /* left by a page button or a track change */
    uint32_t repeat = br_smk_link_chord(&chord, buttons & (1u << SMK_BTN_REPEAT), &smk_scale_held_back,
                                        &smk_scale_late, (down >> SMK_BTN_REPEAT) & 1u, (down >> SMK_LOGICAL_SCALE) & 1u);
    if (released & (1u << SMK_BTN_REPEAT)) {
        if (br_punch_key(&br_punch_ks, BR_PUNCH_SHIFT_KEY, 0u)) br_punch_changed();
    }
    if (es.mode) {
        br_smk_seq_keys(&seq_key_held, 0u, 0u, 0u, fm1_in.key_vel + SMK_KEY_F3);
        br_smk_drum_pads(0, held, velocity);
        smk_punch_pads_update(&punch_pads, 0, held, 0);
        br_punch_ks.active = 0;
        RING_PUBLISH(); br_punch_effect = 0;
        return;
    }
    if (repeat) {
        /* Drum track: a tap toggles RETRIGGER (smk_repeat_decide). Synth tracks: the punch shift key. */
        uint32_t act = br_smk_repeat_press(&br_smk_retrig, br_step_press_pending() || br_ui.step_edit_active);
        if (act == SMK_REPEAT_PUNCH && br_punch_page() &&
            br_punch_key(&br_punch_ks, BR_PUNCH_SHIFT_KEY, 1u)) br_punch_changed();
    }
    if (!br_step_press_pending() && !br_ui.step_edit_active) {
        int32_t direction = !!(buttons & (1u << SMK_BTN_RIGHT)) -
                            !!(buttons & (1u << SMK_BTN_LEFT));
        if (direction) br_smk_track_turn(direction);
    }
    br_smk_faders(buttons);
    br_smk_outer_keys();
    br_smk_expression();
    uint32_t mode = br_seq_grid_page() || br_retrig_page();
#ifdef BR_PATTERN_SONG_EXPERIMENT
    if (br_ui.page == BR_PAGE_PS) mode = 2u;
#endif
    if (mode != previous_mode) editor_pads = 0;
    previous_mode = (uint8_t)mode;
    br_smk_drum_pads(0, held, velocity);
    uint8_t punch = smk_punch_pads_update(&punch_pads, pressed, held,
                                           !mode && br_punch_page());
    uint8_t effect = punch;
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT
    if (punch == BR_PUNCH_ECHO && br_fx_poly_loan_active()) {
        if (br_punch_ks.active != punch) br_message("ECHO UNAVAILABLE IN POLY");
        effect = BR_PUNCH_NONE;
    }
#endif
    if (br_punch_ks.active != punch || br_punch_effect != effect) {
        if (punch && effect) {
            static const char *const name[] = {"", "PUNCH LOOP", "PUNCH ECHO", "PUNCH FILTER", "PUNCH TREM"};
            br_message(name[punch]);
        }
        br_punch_ks.active = punch;
        RING_PUBLISH();
        br_punch_loop_len = br_punch_cfg.loop_len & 3u;
        br_punch_effect = effect;
        br_ui.redraw = 1u;
    }
    if (!mode) {
        br_smk_seq_keys(&seq_key_held, 0u, 0u, 0u, fm1_in.key_vel + SMK_KEY_F3);
        return;
    }

    if (mode == 1u) {
        uint32_t p = br_ui.part;
        int kit = br_is_kit(br_part[p].sound.preset);
        br_smk_seq_keys(&seq_key_held, *keys, *edges, br_seq_grid_page() && !kit,
                        fm1_in.key_vel + SMK_KEY_F3);
        if (!br_step_press_pending() && !br_retrig_editing()) {
            for (uint32_t k = 0; k < 27u; ++k) {
                if (!(*edges & (1u << k))) continue;
                if (kit) {
                    if (br_black_key(k)) br_key_down(k, 0);
                } else {
                    uint32_t note = (uint32_t)(53 + (int32_t)k + 12 * br_ui.octave);
                    br_ui.note[p] = note;
                    br_ui.seq_default[p][0].note = (uint8_t)note;
                    br_ui.redraw = 1;
                }
            }
        }
        *keys = *edges = 0;
    }
    if (mode != 1u) br_smk_seq_keys(&seq_key_held, 0u, 0u, 0u, fm1_in.key_vel + SMK_KEY_F3);
    editor_pads = (uint16_t)((editor_pads | pressed) & held);
    uint32_t count = mode == 2u ? 11u : SMK_NPAD;
    for (uint32_t pad = 0; pad < count; ++pad) {
        uint32_t bit = 1u << pad;
        uint32_t key = 1u << (mode == 2u ? pattern_keys[pad] : SMK_WHITE_NOTE[pad]);
        if (editor_pads & bit) *keys |= key;
        if (pressed & bit) *edges |= key;
    }
}

static void br_smk_pad_lights(void)
{
    static uint32_t next;
    if ((int32_t)(fm1_ms - next) < 0) return;
    next = fm1_ms + 25u;
    int grid = br_seq_grid_page() || br_retrig_page();
    uint32_t part = br_ui.part;
    int kit = br_is_kit(br_part[part].sound.preset);
    for (uint32_t pad = 0; pad < SMK_NPAD; ++pad) {
        uint32_t step = br_ui.step_bank * BR_BANK_STEPS + pad;
#ifdef BR_PATTERN_SONG_EXPERIMENT
        if (br_ui.page == BR_PAGE_PS) {
            if (pad == br_ps_state.active) padled_set(pad, 4u, 20u, 11u);
            else if (pad < FM1_PATTERN_COUNT && (br_ps_state.used & (1u << pad))) padled_set(pad, 3u, 7u, 16u);
            else padled_set(pad, 0u, 0u, pad < FM1_PATTERN_COUNT ? 2u : 0u);
            continue;
        }
#endif
        if (grid) {
            int valid = step < br_part[part].length;
            int on = valid && br_step_active(part, step, br_ui.lane, (uint32_t)kit);
            int cursor = valid && (step == br_ui.cursor || (br_playing && step == br_step_pos[part]));
            if (cursor) padled_set(pad, 24u, 12u, 2u);
            else if (on) padled_set(pad, kit ? 19u : 1u, kit ? 3u : 18u, kit ? 13u : 12u);
            else padled_set(pad, 0u, valid ? 2u : 0u, valid ? 3u : 0u);
        } else if (!es.mode && br_punch_page()) {
            static const uint8_t colors[4][3] = {{20, 3, 2}, {19, 11, 1}, {2, 14, 21}, {12, 4, 18}};
            const uint8_t *rgb = colors[pad % BR_DRUM_LANES];
            uint32_t dim = (smk_pad_down & (1u << pad)) ? 1u : 4u;
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT
            if (pad % BR_DRUM_LANES == 1u && br_fx_poly_loan_active()) dim = 16u;
#endif
            padled_set(pad, rgb[0] / dim, rgb[1] / dim, rgb[2] / dim);
        } else padled_set(pad, 0u, 0u, 0u);
    }
    padled_flush();
}
