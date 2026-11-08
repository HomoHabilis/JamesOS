/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_MODULES_H
#define BR_INSTRUMENT_MODULES_H
#include <stdint.h>

#define BR_LFOS 2u
/* The saved-module prefix is fixed at three rows through session v11. The
 * final row is now retired and remains reserved only for wire compatibility. */
#define BR_SAVED_LFOS 3u
#define BR_ROUTES 8u
enum { BR_WAVE_SINE, BR_WAVE_TRIANGLE, BR_WAVE_SAW, BR_WAVE_SQUARE, BR_WAVE_RANDOM, BR_WAVE_COUNT };
enum { BR_TARGET_OFF, BR_TARGET_PITCH, BR_TARGET_TIMBRE, BR_TARGET_COLOR, BR_TARGET_FILTER,
       BR_TARGET_LEVEL, BR_TARGET_DECAY, BR_TARGET_DRIVE, BR_TARGET_DELAY, BR_TARGET_REVERB,
       BR_TARGET_MOD, BR_TARGET_HARMONICS, BR_TARGET_OUTPUT, BR_TARGET_GATE,
       BR_TARGET_ATTACK, BR_TARGET_SUSTAIN, BR_TARGET_RELEASE, BR_TARGET_RESONANCE,
       BR_TARGET_LFO1_RATE, BR_TARGET_LFO2_RATE, BR_TARGET_COUNT };   /* LFO rates: the panel's LFO RATE inputs */
/* DRIVE and GATE retain their saved numeric IDs for project compatibility.
 * Neither is an active modulation destination. */
static inline uint32_t br_mod_target_active(uint32_t target)
{
    return target > BR_TARGET_OFF && target < BR_TARGET_COUNT &&
        target != BR_TARGET_DRIVE && target != BR_TARGET_GATE;
}
enum { BR_SOURCE_OFF = 0, BR_SOURCE_OSC = 1, BR_SOURCE_MIX = 2, BR_SOURCE_AMP = 3,
       BR_SOURCE_ENV = 4, BR_SOURCE_LFO1 = 5, BR_SOURCE_LFO2 = 6,
       BR_SOURCE_LFO3 = 7, /* retired, reserved for legacy projects */
       BR_SOURCE_VELOCITY = 8, BR_SOURCE_KEY = 9, BR_SOURCE_RANDOM = 10,
       /* The panel's GATE, LFO TRI and LFO SQU outputs. The triangle and square of each LFO are always
        * available, whatever wave the LFO itself is set to. */
       BR_SOURCE_GATE = 11, BR_SOURCE_LFO1_TRI = 12, BR_SOURCE_LFO1_SQU = 13,
       BR_SOURCE_LFO2_TRI = 14, BR_SOURCE_LFO2_SQU = 15,
       BR_SOURCE_COUNT = 16 };
static inline uint32_t br_mod_source_active(uint32_t source)
{
    return source != BR_SOURCE_OFF && source < BR_SOURCE_COUNT &&
        source != BR_SOURCE_LFO3;
}
enum { BR_SCALE_CHROMATIC, BR_SCALE_MAJOR, BR_SCALE_MINOR, BR_SCALE_DORIAN, BR_SCALE_MIXOLYDIAN,
       BR_SCALE_PENT_MAJOR, BR_SCALE_PENT_MINOR, BR_SCALE_BLUES, BR_SCALE_COUNT };
enum { BR_DRIVE_SOFT, BR_DRIVE_HARD, BR_DRIVE_FUZZ, BR_DRIVE_CRUSH, BR_DRIVE_COUNT };
enum { BR_DELAY_DIGITAL, BR_DELAY_TAPE, BR_DELAY_MULTITAP, BR_DELAY_GLITCH, BR_DELAY_COUNT };
enum { BR_REVERB_ROOM, BR_REVERB_HALL, BR_REVERB_SPRING, BR_REVERB_SPACE, BR_REVERB_ETHEREAL, BR_REVERB_COUNT };
enum { BR_MOD_CHORUS, BR_MOD_FLANGE, BR_MOD_PHASER, BR_MOD_VIBRATO, BR_MOD_COUNT };

typedef struct { uint32_t wave, rate, depth, target; } br_lfo_config_t;
typedef struct { uint32_t source, target; int32_t amount; uint32_t enabled; } br_route_config_t;
typedef struct {
    uint32_t attack, decay, sustain, release;
    int32_t amount;
    uint32_t target, loop, retrigger;
    uint32_t scale, root, quantize;
    int32_t transpose;
    br_lfo_config_t lfo[BR_LFOS];
    br_route_config_t route[BR_ROUTES];
} br_mod_config_t;
typedef struct {
    /* The drive stage. The saved session still calls these bytes fx[0..2] (once an older effect's engine,
     * tone and mix), so the wire layout is unchanged; in memory they are named for what they now mean. */
    uint32_t drive_mode, drive_gain, drive_tone;
    uint32_t delay_engine, delay_time, delay_feedback;
    uint32_t reverb_engine, reverb_tone; /* Legacy wire name; the control is now DECAY. */
    uint32_t mod_engine, mod_rate, mod_depth, mod_mix;
} br_fx_config_t;

/* A single revision publishes sound, lane, effects and modulation controls. */
static void br_module_defaults(br_mod_config_t *mod, br_fx_config_t *fx)
{
    uint32_t i;
    mod->attack = 0; mod->decay = 64; mod->sustain = 0; mod->release = 24;
    mod->amount = 0; mod->target = BR_TARGET_FILTER; mod->loop = 0; mod->retrigger = 1;
    mod->scale = BR_SCALE_CHROMATIC; mod->root = 0; mod->quantize = 0; mod->transpose = 0;
    for (i = 0; i < BR_LFOS; ++i) {
        mod->lfo[i].wave = BR_WAVE_SINE; mod->lfo[i].rate = 32u + 16u * i;
        mod->lfo[i].depth = 0; mod->lfo[i].target = BR_TARGET_OFF;
    }
    for (i = 0; i < BR_ROUTES; ++i) {
        mod->route[i].source = BR_SOURCE_OFF; mod->route[i].target = BR_TARGET_OFF;
        mod->route[i].amount = 0; mod->route[i].enabled = 0;
    }
    fx->drive_mode = BR_DRIVE_SOFT; fx->drive_gain = 64; fx->drive_tone = 127;
    fx->delay_engine = BR_DELAY_DIGITAL; fx->delay_time = 64; fx->delay_feedback = 57;
    fx->reverb_engine = BR_REVERB_ROOM; fx->reverb_tone = 64;
    fx->mod_engine = BR_MOD_CHORUS; fx->mod_rate = 40; fx->mod_depth = 48; fx->mod_mix = 0;
}
#endif
