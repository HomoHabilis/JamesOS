/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BR_INSTRUMENT_BANK_H
#define BR_INSTRUMENT_BANK_H
#include <stdint.h>

/* SEL sound bank: 64 starting-point sounds for the synth tracks. A bank entry
 * writes only existing sound and modulation fields, so the session format is
 * unchanged. Values are the spec's 0-127 numbers (assets/presets/bank.json). */
#define BR_BANK_COUNT 64u
#define BR_BANK_SIZE 8u              /* sounds per bank A..H */

typedef struct {
    char name[11];
    uint8_t model;                   /* Plaits model, 0..23 */
    uint8_t h, t, m;                 /* harmonics, timbre, morph */
    uint8_t f, q;                    /* low-pass cutoff (127 = open), resonance */
    uint8_t a, d, s, r;              /* amplitude envelope */
    uint8_t dly, rev;                /* sends */
    uint8_t lfo_target, lfo_rate, lfo_depth;   /* LFO 1; target 0 = none */
    uint8_t env_target;              /* modulation envelope target; 0 = none */
    int8_t env_amount;
    uint8_t env_decay;
    uint8_t aux;                     /* Plaits main/aux output mix */
} br_bank_t;

static const br_bank_t br_bank[BR_BANK_COUNT] = {
#include "instrument_bank_data.inc"
};

static const char *const br_bank_name[BR_BANK_COUNT / BR_BANK_SIZE] = {
    "BASS", "KEYS", "CHORDS", "PADS", "LEADS", "PLUCKS", "PERC/FX", "TEXTURES"
};

/* The fields a bank entry rewrites. Taking and restoring this much leaves the
 * track's level, tune, pan, mute, drive, gate and routing alone. */
typedef struct {
    uint32_t preset, harmonics, aux, timbre, color, filter, resonance, keytrack;
    uint32_t attack, decay, sustain, release, delay, reverb;
    uint32_t timbre_mod, fm_mod, morph_mod;
    uint32_t env_attack, env_decay, env_sustain, env_release, env_target;
    int32_t env_amount;
    br_lfo_config_t lfo[BR_LFOS];
} br_bank_state_t;

static inline void br_bank_take(br_bank_state_t *s, const volatile br_sound_t *k,
                                const volatile br_mod_config_t *m)
{
    uint32_t i;
    s->preset = k->preset; s->harmonics = k->plaits_harmonics; s->aux = k->plaits_aux;
    s->timbre = k->timbre; s->color = k->color; s->filter = k->filter;
    s->resonance = k->filter_resonance; s->keytrack = k->filter_keytrack;
    s->attack = k->attack; s->decay = k->decay; s->sustain = k->sustain;
    s->release = k->release; s->delay = k->delay; s->reverb = k->reverb;
    s->timbre_mod = k->timbre_mod; s->fm_mod = k->fm_mod; s->morph_mod = k->morph_mod;
    s->env_attack = m->attack; s->env_decay = m->decay; s->env_sustain = m->sustain;
    s->env_release = m->release; s->env_target = m->target; s->env_amount = m->amount;
    for (i = 0u; i < BR_LFOS; ++i) {
        s->lfo[i].wave = m->lfo[i].wave; s->lfo[i].rate = m->lfo[i].rate;
        s->lfo[i].depth = m->lfo[i].depth; s->lfo[i].target = m->lfo[i].target;
    }
}

static inline void br_bank_put(const br_bank_state_t *s, volatile br_sound_t *k,
                               volatile br_mod_config_t *m)
{
    uint32_t i;
    k->preset = s->preset; k->plaits_harmonics = s->harmonics; k->plaits_aux = s->aux;
    k->timbre = s->timbre; k->color = s->color; k->filter = s->filter;
    k->filter_resonance = s->resonance; k->filter_keytrack = s->keytrack;
    k->attack = s->attack; k->decay = s->decay; k->sustain = s->sustain;
    k->release = s->release; k->delay = s->delay; k->reverb = s->reverb;
    k->timbre_mod = s->timbre_mod; k->fm_mod = s->fm_mod; k->morph_mod = s->morph_mod;
    m->attack = s->env_attack; m->decay = s->env_decay; m->sustain = s->env_sustain;
    m->release = s->env_release; m->target = s->env_target; m->amount = s->env_amount;
    for (i = 0u; i < BR_LFOS; ++i) {
        m->lfo[i].wave = s->lfo[i].wave; m->lfo[i].rate = s->lfo[i].rate;
        m->lfo[i].depth = s->lfo[i].depth; m->lfo[i].target = s->lfo[i].target;
    }
}

/* The state a bank entry stands for. The spec's cutoff runs 0..127 with 127
 * open; the track's filter control is bipolar (0..63 low-pass, 64 open). */
static inline void br_bank_state(const br_bank_t *b, br_bank_state_t *s)
{
    uint32_t i;
    s->preset = BR_FIRST_PLAITS + b->model; s->harmonics = b->h; s->aux = b->aux;
    s->timbre = b->t; s->color = b->m;
    s->filter = b->f >= 127u ? 64u : (uint32_t)(b->f >> 1);
    s->resonance = b->q; s->keytrack = 0u;
    s->attack = b->a; s->decay = b->d; s->sustain = b->s; s->release = b->r;
    s->delay = b->dly; s->reverb = b->rev;
    s->timbre_mod = s->fm_mod = s->morph_mod = 64u;
    s->env_attack = 0u; s->env_decay = b->env_decay; s->env_sustain = 0u; s->env_release = 24u;
    s->env_target = b->env_target ? b->env_target : BR_TARGET_FILTER;
    s->env_amount = b->env_target ? b->env_amount : 0;
    for (i = 0u; i < BR_LFOS; ++i) {
        s->lfo[i].wave = BR_WAVE_SINE; s->lfo[i].rate = 32u + 16u * i;
        s->lfo[i].depth = 0u; s->lfo[i].target = BR_TARGET_OFF;
    }
    if (b->lfo_target) {
        s->lfo[0].target = b->lfo_target; s->lfo[0].rate = b->lfo_rate;
        s->lfo[0].depth = b->lfo_depth;
    }
}

/* Kit patches use existing saved controls; no new preset IDs or session bytes.
 * They leave the pattern, lane assignments, mix, routing and effects untouched. */
#define BR_KIT_BANK_COUNT (BR_ENABLE_PLAITS ? 12u : 9u)
typedef struct {
    char name[11];
    uint8_t preset, timbre, color, harmonics, decay, snap;
    int8_t tune;
} br_kit_patch_t;
static const br_kit_patch_t br_kit_bank[BR_KIT_BANK_COUNT] = {
    {"BRAIDS",     BR_KIT_BRAIDS, 64, 64, 64, 64, 64, 0},
    {"BRAIDS LOW", BR_KIT_BRAIDS, 32, 38, 64, 98, 36,-5},
    {"BRAIDS AIR", BR_KIT_BRAIDS,100,108, 64, 42,110, 5},
#if BR_ENABLE_PLAITS
    {"PLAITS",     BR_KIT_PLAITS, 64, 64, 64, 64, 64, 0},
    {"PLAITS LOW", BR_KIT_PLAITS, 34, 38, 35, 95, 40,-5},
    {"PLAITS AIR", BR_KIT_PLAITS,104,105,102, 42,105, 5},
#endif
    {"80S KIT",    BR_KIT_808,    64, 64, 64,127, 64, 0},
    {"80S DEEP",   BR_KIT_808,    32, 64, 64,105, 38,-5},
    {"80S BRIGHT", BR_KIT_808,   112, 64, 64, 72,112, 4},
    {"90S KIT",    BR_KIT_909,    64, 64, 64,127, 64, 0},
    {"90S DEEP",   BR_KIT_909,    32, 64, 64,100, 38,-5},
    {"90S BRIGHT", BR_KIT_909,   112, 64, 64, 72,112, 4},
};
typedef struct { uint32_t preset, timbre, color, harmonics, decay, snap; int32_t tune; } br_kit_bank_state_t;
static inline void br_kit_bank_take(br_kit_bank_state_t *s, const volatile br_sound_t *k)
{
    s->preset=k->preset; s->timbre=k->timbre; s->color=k->color;
    s->harmonics=k->plaits_harmonics; s->decay=k->decay; s->snap=k->drum_snap; s->tune=k->tune;
}
static inline void br_kit_bank_put(const br_kit_bank_state_t *s, volatile br_sound_t *k)
{
    k->preset=s->preset; k->timbre=s->timbre; k->color=s->color;
    k->plaits_harmonics=s->harmonics; k->decay=s->decay; k->drum_snap=s->snap; k->tune=s->tune;
}
static inline void br_kit_bank_apply(const br_kit_patch_t *b, volatile br_sound_t *k)
{
    br_kit_bank_state_t s = {b->preset,b->timbre,b->color,b->harmonics,b->decay,b->snap,b->tune};
    br_kit_bank_put(&s,k);
}
static inline const char *br_sel_entry_name(uint32_t part, uint32_t entry)
{ return part == BR_DRUM_PART ? br_kit_bank[entry].name : br_bank[entry].name; }

/* Browser state for one visit to the SEL page. `base` is what the track holds
 * when it is left without loading: the sound it had on entry, or the last
 * sound loaded. `undo` is the sound before the last load. `loaded` says the
 * shown sound has been kept (SEL pressed on it). */
typedef struct {
    uint8_t active;                  /* a visit is open: `base` is valid */
    uint8_t part;
    uint8_t cursor[BR_PARTS];               /* highlighted entry per synth track */
    uint8_t top[BR_PARTS];                  /* first of the five listed entries */
    uint8_t shown;                   /* the highlighted entry is on the track */
    uint8_t loaded;                  /* the shown entry was confirmed */
    uint8_t undo_ready;
    uint32_t knob_ms;                /* when knob 1 last moved the cursor (lights its chip) */
    br_bank_state_t base, undo;
    br_kit_bank_state_t kit_base, kit_undo;
} br_sel_t;

#define BR_SEL_ROWS 5u
/* The list scrolls only when the cursor walks off its top or bottom row. */
static inline uint32_t br_sel_top(uint32_t top, uint32_t cursor)
{
    if (cursor < top) top = cursor;
    else if (cursor >= top + BR_SEL_ROWS) top = cursor - (BR_SEL_ROWS - 1u);
    return top > BR_BANK_COUNT - BR_SEL_ROWS ? BR_BANK_COUNT - BR_SEL_ROWS : top;
}

static inline uint32_t br_sel_wrap(uint32_t cursor, int32_t turn)
{
    int32_t at = ((int32_t)cursor + turn % (int32_t)BR_BANK_COUNT) % (int32_t)BR_BANK_COUNT;
    return (uint32_t)(at < 0 ? at + (int32_t)BR_BANK_COUNT : at);
}

/* Put the highlighted entry on the track so the keys, and a running sequence,
 * play it. Opens the visit on first use. */
static inline void br_sel_show(br_sel_t *s, uint32_t part, volatile br_sound_t *k,
                               volatile br_mod_config_t *m)
{
    br_bank_state_t entry;
    if (!s->active) { if (part == BR_DRUM_PART) br_kit_bank_take(&s->kit_base, k); else br_bank_take(&s->base, k, m); s->active = 1u; s->part = (uint8_t)part; s->undo_ready = 0u; }
    if (part == BR_DRUM_PART) br_kit_bank_apply(&br_kit_bank[s->cursor[part]], k);
    else { br_bank_state(&br_bank[s->cursor[part]], &entry); br_bank_put(&entry, k, m); }
    s->shown = 1u; s->loaded = 0u;
}

/* Put back what the visit found (or last loaded) and close it. */
static inline void br_sel_leave(br_sel_t *s, volatile br_sound_t *k, volatile br_mod_config_t *m)
{
    if (s->active && s->shown) { if (s->part == BR_DRUM_PART) br_kit_bank_put(&s->kit_base, k); else br_bank_put(&s->base, k, m); }
    s->active = s->shown = s->loaded = s->undo_ready = 0u;
}

/* Keep the shown entry: the old base becomes the undo step. */
static inline uint32_t br_sel_load(br_sel_t *s, const volatile br_sound_t *k,
                                   const volatile br_mod_config_t *m)
{
    if (!s->active || !s->shown) return 0u;
    if (s->part == BR_DRUM_PART) { s->kit_undo = s->kit_base; br_kit_bank_take(&s->kit_base, k); }
    else { s->undo = s->base; br_bank_take(&s->base, k, m); }
    s->undo_ready = 1u; s->loaded = 1u;
    return 1u;
}

/* Go back to the sound before the last load. */
static inline uint32_t br_sel_undo(br_sel_t *s, volatile br_sound_t *k, volatile br_mod_config_t *m)
{
    if (!s->active || !s->undo_ready) return 0u;
    if (s->part == BR_DRUM_PART) { br_kit_bank_put(&s->kit_undo, k); s->kit_base = s->kit_undo; }
    else { br_bank_put(&s->undo, k, m); s->base = s->undo; }
    s->undo_ready = 0u; s->shown = 0u; s->loaded = 0u;
    return 1u;
}
#endif
