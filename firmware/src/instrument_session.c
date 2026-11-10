/* SPDX-License-Identifier: GPL-3.0-only */
/* Versions 10–13 remain readable; new sessions use v16. The v13 timing
 * offsets retain their length-field bits. V14 reuses the retired per-step
 * drum voice override bytes for retriggers; lane voice choices stay intact. */
#include <stddef.h>
typedef struct {
    uint8_t preset, timbre, color;
    int8_t tune;
    uint8_t level, attack, decay, sustain, release, drive, delay, reverb, space, mute;
    uint8_t length, division, swing, gate, lane_level[8], step[BR_BANK_STEPS][2];
} br_saved_part_t;
typedef struct {
    uint8_t snap, env[4];
    int8_t amount;
    uint8_t target, loop, retrigger, scale, root, quantize;
    int8_t transpose;
    /* Keep all three serialized rows stable. Runtime v12 uses only rows 0-1. */
    uint8_t lfo[BR_SAVED_LFOS][4], fx[12], route[BR_ROUTES][4];
    uint8_t extra_step[BR_STEPS - BR_BANK_STEPS][2];
} br_saved_module_t;
typedef struct {
    uint32_t magic;
    uint8_t bpm, selected;
    int8_t octave;
    uint8_t version;
    br_saved_part_t part[BR_PARTS];
    br_saved_module_t module[BR_PARTS];
} br_session_v3_t;
typedef struct {
    br_session_v3_t v3;
    uint8_t master_fx[4];
    uint8_t select_target;
} br_session_v4_t;
typedef struct {
    br_session_v3_t v3;
    uint8_t master_fx[4];
    uint8_t select_target;
    uint8_t reserved_v4[3];
    uint8_t plaits[BR_PARTS][2];
} br_session_v5_t;
typedef struct {
    br_session_v3_t v3;
    uint8_t master_fx[4];
    uint8_t select_target;
    uint8_t reserved_v4[3];
    uint8_t plaits[BR_PARTS][2];
    uint8_t pan[BR_PARTS];
} br_session_v6_t;
typedef struct {
    br_session_v3_t v3;
    uint8_t master_fx[4];
    uint8_t select_target;
    uint8_t reserved_v4[3];
    uint8_t plaits[BR_PARTS][2];
    uint8_t pan[BR_PARTS];
    uint8_t lane_gate[BR_PARTS][8];
    uint8_t compressor[8];
    uint8_t legacy_gate[BR_PARTS];
} br_session_v9_t;
typedef struct {
    br_session_v3_t v3;
    uint8_t master_fx[4];
    uint8_t select_target;
    uint8_t reserved_v4[3];
    uint8_t plaits[BR_PARTS][2];
    uint8_t pan[BR_PARTS];
    /* The v9 storage shape is preserved for prefix compatibility. Only the
     * first four entries of the kit track are logical in v10; unused entries
     * are canonicalized and validated below. */
    uint8_t lane_gate[BR_PARTS][8];
    uint8_t compressor[8];
    uint8_t legacy_gate[BR_PARTS];
    uint8_t kit_choice_mask;
    uint8_t reserved_v10[3];
} br_session_v10_t;
typedef struct {
    union {
        br_session_v10_t v10;
        struct {
            br_session_v3_t v3;
            uint8_t master_fx[4];
            uint8_t select_target;
            uint8_t reserved_v4[3];
            uint8_t plaits[BR_PARTS][2];
            uint8_t pan[BR_PARTS];
            uint8_t lane_gate[BR_PARTS][8];
            uint8_t compressor[8];
            uint8_t legacy_gate[BR_PARTS];
            uint8_t kit_choice_mask;
            uint8_t reserved_v10[3];
        };
    };
    uint16_t synth_length[BR_SYNTH_PARTS][BR_STEPS];
    uint8_t synth_condition[BR_SYNTH_PARTS][BR_STEPS];
    uint16_t kit_length[BR_STEPS][BR_DRUM_LANES];
    uint8_t kit_condition[BR_STEPS][BR_DRUM_LANES];
    uint8_t kit_velocity[BR_STEPS][BR_DRUM_LANES];
    /* v14 replaces hidden per-step voice overrides with retrigger settings. */
    union {
        uint8_t kit_note[BR_STEPS][BR_DRUM_LANES];
        uint8_t kit_retrig[BR_STEPS][BR_DRUM_LANES];
    };
    uint8_t model_controls[BR_PARTS][5];
    uint8_t synth_note[BR_SYNTH_PARTS][BR_STEPS];
    uint8_t synth_velocity[BR_SYNTH_PARTS][BR_STEPS];
    uint8_t kit_pitch[BR_STEPS][BR_DRUM_LANES];
} br_session_t;
typedef struct {
    uint32_t magic;
    uint8_t bpm, selected;
    int8_t octave;
    uint8_t version;
    br_saved_part_t part[BR_PARTS];
} br_legacy_session_t;
_Static_assert(sizeof(br_legacy_session_t) == 240u, "legacy settings layout");
_Static_assert(offsetof(br_saved_module_t, fx) == 25u,
               "three saved LFO rows keep the version 3 module offsets");
_Static_assert(offsetof(br_saved_module_t, route) == 37u,
               "version 3 route offset remains fixed");
_Static_assert(sizeof(br_saved_module_t) == 165u,
               "version 3 through v11 module wire layout remains fixed");
_Static_assert(sizeof(br_session_v3_t) == 900u, "version 3 settings layout");
_Static_assert(offsetof(br_session_v6_t, master_fx) == 900u, "version 4 preserves version 3 prefix");
_Static_assert(sizeof(br_session_v4_t) == 908u, "version 4 settings layout");
_Static_assert(offsetof(br_session_v5_t, plaits) == 908u, "version 5 preserves version 4 prefix");
_Static_assert(sizeof(br_session_v5_t) == 916u, "version 5 settings layout");
_Static_assert(offsetof(br_session_v6_t, plaits) == 908u, "version 6 preserves version 5 prefix");
_Static_assert(offsetof(br_session_v6_t, pan) == 916u, "version 6 pan offset");
_Static_assert(sizeof(br_session_v6_t) == 920u, "version 6 settings layout");
_Static_assert(offsetof(br_session_v9_t, master_fx) == 900u, "version 7 preserves version 3 prefix");
_Static_assert(offsetof(br_session_v9_t, plaits) == 908u, "version 7 preserves version 5 prefix");
_Static_assert(offsetof(br_session_v9_t, pan) == 916u, "version 7 preserves version 6 prefix");
_Static_assert(offsetof(br_session_v9_t, lane_gate) == 920u, "version 7 lane-gate offset");
_Static_assert(offsetof(br_session_v9_t, compressor) == 952u, "version 8 compressor offset");
_Static_assert(offsetof(br_session_v9_t, legacy_gate) == 960u, "version 9 legacy gate offset");
_Static_assert(sizeof(br_session_v9_t) == 964u, "version 9 settings layout");
_Static_assert(offsetof(br_session_v10_t, master_fx) == 900u, "version 10 preserves version 9 prefix");
_Static_assert(offsetof(br_session_v10_t, plaits) == 908u, "version 10 preserves version 5 prefix");
_Static_assert(offsetof(br_session_v10_t, pan) == 916u, "version 10 preserves version 6 prefix");
_Static_assert(offsetof(br_session_v10_t, lane_gate) == 920u, "version 10 preserves version 7 prefix");
_Static_assert(offsetof(br_session_v10_t, compressor) == 952u, "version 10 preserves version 8 prefix");
_Static_assert(offsetof(br_session_v10_t, legacy_gate) == 960u, "version 10 preserves version 9 prefix");
_Static_assert(offsetof(br_session_v10_t, kit_choice_mask) == 964u, "version 10 choice-mask offset");
_Static_assert(sizeof(br_session_v10_t) == 968u, "version 10 settings layout");
_Static_assert(offsetof(br_session_t, v10) == 0u, "version 11 preserves version 10 prefix");
_Static_assert(offsetof(br_session_t, synth_length) == 968u, "version 11 trigger metadata offset");
_Static_assert(sizeof(br_session_t) == 3484u, "versions 11–16 session length");
#define BR_SESSION_V13_SIZE sizeof(br_session_t)
_Static_assert(sizeof(br_session_t) <= 4084u, "session fits settings sector");
#define BR_SESSION_MAGIC 0x42524431u
static int br_session_v10_valid(const br_session_v10_t *s);
static int br_session_v11_valid(const br_session_t *s);
static int br_session_v12_valid(const br_session_t *s);
static int br_session_v13_valid(const br_session_t *s);
static int br_session_v14_valid(const br_session_t *s);
static int br_session_v15_valid(const br_session_t *s);
static int br_session_v16_valid(const br_session_t *s);
extern volatile uint8_t br_midi_channels[7];
extern volatile uint8_t br_midi_clock_enabled, br_midi_clock_out;
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static br_session_t br_pending_save __attribute__((section(".pool"), aligned(4)));
static br_session_t br_undo __attribute__((section(".pool"), aligned(4)));
static br_session_t br_loaded __attribute__((section(".pool"), aligned(4)));
#else
static br_session_t br_pending_save, br_undo, br_loaded;
#endif
static uint8_t br_save_pending, br_undo_valid;
static uint8_t br_legacy_migration_pending, br_invalid_session_pending;
static uint32_t br_legacy_raw_length, br_legacy_raw_version;
#if !defined(FM1_PATTERN_V16_ONLY) || !FM1_PATTERN_V16_ONLY
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static uint8_t br_legacy_raw_session[sizeof(br_session_v10_t)]
    __attribute__((section(".pool"), aligned(4)));
#else
static uint8_t br_legacy_raw_session[sizeof(br_session_v10_t)] __attribute__((aligned(4)));
#endif
#endif

static uint32_t br_session_legacy_migration_pending(void)
{ return br_legacy_migration_pending != 0u; }

static uint32_t br_session_save_blocked(void)
{ return br_legacy_migration_pending != 0u || br_invalid_session_pending != 0u; }

/* Project scratch is independent of protected settings flash. A legacy
 * migration still owns br_loaded, so it must retain exclusive access. */
static uint32_t br_pattern_session_scratch_blocked(void)
{
#if defined(FM1_PATTERN_DEVICE_CANDIDATE) && FM1_PATTERN_DEVICE_CANDIDATE && defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    return br_legacy_migration_pending != 0u;
#else
    return br_session_save_blocked();
#endif
}

#if !defined(FM1_PATTERN_V16_ONLY) || !FM1_PATTERN_V16_ONLY
static uint32_t br_session_legacy_raw_info(uint32_t *length, uint32_t *version)
{
    if (!length || !version || !br_legacy_migration_pending) return 0u;
    *length = br_legacy_raw_length;
    *version = br_legacy_raw_version;
    return 1u;
}

static uint32_t br_session_legacy_raw_chunk(uint32_t offset, uint32_t count,
                                             uint8_t *out, uint32_t capacity)
{
    const uint8_t *raw = br_legacy_raw_length == BR_SESSION_V13_SIZE &&
        br_legacy_raw_version == 11u ? (const uint8_t *)&br_loaded : br_legacy_raw_session;
    if (!br_legacy_migration_pending || !out || !count || count > 32u ||
        count > capacity || offset > br_legacy_raw_length ||
        count > br_legacy_raw_length - offset) return 0u;
    memcpy(out, raw + offset, count);
    return count;
}

#endif

/* Only the explicit converted-session upload path may release the flash
 * overwrite lock. Generic restore is also used by undo and must not do so. */
static void br_session_explicit_v10_upload_accepted(const br_session_v10_t *s)
{
    if (!br_session_v10_valid(s)) return;
    br_legacy_migration_pending = 0u;
    br_invalid_session_pending = 0u;
    br_legacy_raw_length = br_legacy_raw_version = 0u;
}

/* The current upload path validates the new full-size session before it can
 * release the preserved-record SAVE lock. */
static void br_session_explicit_v12_upload_accepted(const br_session_t *s)
{
    if (!br_session_v12_valid(s) && !br_session_v13_valid(s) && !br_session_v14_valid(s) && !br_session_v15_valid(s) && !br_session_v16_valid(s)) return;
    br_legacy_migration_pending = 0u;
    br_invalid_session_pending = 0u;
    br_legacy_raw_length = br_legacy_raw_version = 0u;
}

/* legacy_gate[] was the old per-track Home gate length. Version 13 reuses two of the bytes for the global
 * filter: [0] is the envelope depth and [1] the smoothness (below), so the layout is unchanged. The bytes were constant (128). In v13 the first two carry the global filter's
 * envelope depth (128 + depth) and smoothness (128 + smoothness); unused, they stay 128, so
 * a session that does not use the new controls is byte-identical to before. */
static int br_session_legacy_gate_ok(uint32_t value, uint32_t p)
{
    return p == 0u ? value >= 1u : p == 1u ? value >= 128u && value <= 228u : value == BR_GATE_OPEN;
}

static void br_pack_master(br_session_v10_t *s)
{
    uint32_t attempt, before, after;
    int stable = 0;
    uint32_t filter = 64u, distortion = 0u, saturation = 0u, wow = 0u, resonance = 0u;
    uint32_t heat_type = BR_HEAT_BIT, heat_amount = 0u;
    uint32_t target = BR_SELECT_MASTER_FILTER;
    int32_t env_depth = 0;
    uint32_t smooth = 0u;
    uint32_t comp[8] = {BR_COMP_OFF, 18u, 3u, 0u, 12u, 40u, 0u, 127u};
    /* Master controls are normally written by the main loop, like session
     * capture. Still honor the revision protocol and keep this read bounded. */
    for (attempt = 0; attempt < 4u; ++attempt) {
        before = br_master.revision;
        if (before & 1u) continue;
        RING_PUBLISH();
        filter = br_master.fx.filter; distortion = br_master.fx.distortion;
        saturation = br_master.fx.saturation; wow = br_master.fx.wow;
        resonance = br_master.fx.resonance;
        heat_type = br_master.fx.heat_type; heat_amount = br_master.fx.heat_amount;
        env_depth = br_master.fx.env_depth; smooth = br_master.fx.smooth;
        target = br_master.select_target;
        comp[0] = br_master.comp.source; comp[1] = br_master.comp.threshold;
        comp[2] = br_master.comp.ratio; comp[3] = br_master.comp.makeup;
        comp[4] = br_master.comp.attack; comp[5] = br_master.comp.release;
        comp[6] = br_master.comp.hold; comp[7] = br_master.comp.mix;
        RING_PUBLISH();
        after = br_master.revision;
        if (before == after && !(after & 1u)) { stable = 1; break; }
    }
    if (!stable) {
        filter = 64u; distortion = saturation = wow = resonance = 0u;
        heat_type = BR_HEAT_BIT; heat_amount = 0u; env_depth = 0; smooth = 0u;
        target = BR_SELECT_MASTER_FILTER;
        comp[0] = BR_COMP_OFF; comp[1] = 18u; comp[2] = 3u; comp[3] = 0u;
        comp[4] = 12u; comp[5] = 40u; comp[6] = 0u; comp[7] = 127u;
    }
    s->master_fx[0] = (uint8_t)br_clip(filter, 0, 127);
    if (s->v3.version >= 12u) {
        s->master_fx[1] = (uint8_t)br_clip(heat_type, 0, BR_HEAT_COUNT - 1u);
        s->master_fx[2] = (uint8_t)br_clip(heat_amount, 0, 127);
        s->master_fx[3] = 0u;
    } else {
        s->master_fx[1] = (uint8_t)br_clip(distortion, 0, 127);
        s->master_fx[2] = (uint8_t)br_clip(saturation, 0, 127);
        s->master_fx[3] = (uint8_t)br_clip(wow, 0, 127);
    }
    /* Preserve the v11 resonance marker in its existing padding bytes. */
    s->reserved_v4[0] = s->reserved_v4[1] = s->reserved_v4[2] = 0u;
    if (s->v3.version >= 11u && resonance) {
        s->reserved_v4[0] = 0x52u;
        s->reserved_v4[1] = (uint8_t)br_clip(resonance, 0, 127);
        s->reserved_v4[2] = 0xa7u;
    }
    if (s->v3.version >= 13u) {
        s->legacy_gate[0] = (uint8_t)(128 + br_clip(env_depth, -127, 127));
        s->legacy_gate[1] = (uint8_t)(128 + br_clip((int32_t)smooth, 0, 100));
    }
    s->select_target = (uint8_t)br_clip(target, 0, BR_SELECT_COUNT - 1);
    s->compressor[0] = (uint8_t)br_clip(comp[0], 0, BR_COMP_SOURCE_COUNT - 1);
    s->compressor[1] = (uint8_t)br_clip(comp[1], 0, 48);
    s->compressor[2] = (uint8_t)br_clip(comp[2], 0, 7);
    s->compressor[3] = (uint8_t)br_clip(comp[3], 0, 24);
    s->compressor[4] = (uint8_t)br_clip(comp[4], 0, 127);
    s->compressor[5] = (uint8_t)br_clip(comp[5], 0, 127);
    s->compressor[6] = (uint8_t)br_clip(comp[6], 0, 127);
    s->compressor[7] = (uint8_t)br_clip(comp[7], 0, 127);
}

static void br_unpack_master(const br_session_v10_t *s, uint32_t version)
{
    br_compressor_config_t comp = BR_COMP_DEFAULTS;
    br_master.revision++;
    if (version >= 4u) {
        /* Restore follows prefix validation, which bounds these saved bytes. */
        br_master.fx.filter = s->master_fx[0];
        if (version >= 12u) {
            br_master.fx.distortion = br_master.fx.saturation = br_master.fx.wow = 0u;
            br_master.fx.heat_type = s->master_fx[1];
            br_master.fx.heat_amount = s->master_fx[2];
        } else {
            br_master.fx.distortion = br_master.fx.saturation = br_master.fx.wow = 0u;
            if (s->master_fx[2] >= s->master_fx[3]) {
                br_master.fx.heat_type = BR_HEAT_SAT;
                br_master.fx.heat_amount = s->master_fx[2];
            } else {
                br_master.fx.heat_type = BR_HEAT_WOW;
                br_master.fx.heat_amount = s->master_fx[3];
            }
        }
        br_master.fx.env_depth = version >= 13u ? (int32_t)s->legacy_gate[0] - 128 : 0;
        br_master.fx.smooth = version >= 13u ? (uint32_t)s->legacy_gate[1] - 128u : 0u;
        br_master.fx.resonance = version >= 11u && s->reserved_v4[0] == 0x52u &&
            s->reserved_v4[2] == 0xa7u ? (uint32_t)s->reserved_v4[1] : 0u;
        br_master.select_target = BR_SELECT_MASTER_FILTER;
        if (version >= 8u) {
            comp.source = s->compressor[0];
            comp.threshold = s->compressor[1];
            comp.ratio = s->compressor[2];
            comp.makeup = s->compressor[3];
            comp.attack = s->compressor[4];
            comp.release = s->compressor[5];
            comp.hold = s->compressor[6];
            comp.mix = s->compressor[7];
        }
    } else {
        br_master.fx.filter = 64u;
        br_master.fx.distortion = br_master.fx.saturation = br_master.fx.wow = 0u;
        br_master.fx.resonance = 0u;
        br_master.fx.heat_type = BR_HEAT_BIT;
        br_master.fx.heat_amount = 0u;
        br_master.select_target = BR_SELECT_MASTER_FILTER;
    }
    br_master.comp = comp;
    RING_PUBLISH();
    br_master.revision++;
}

static void br_pack_module(br_saved_module_t *s, uint32_t p)
{
    const volatile br_mod_config_t *m = &br_part[p].mod;
    const volatile br_fx_config_t *f = &br_part[p].fx;
    uint32_t i;
    s->snap = (uint8_t)br_part[p].sound.drum_snap;
    s->env[0] = (uint8_t)m->attack; s->env[1] = (uint8_t)m->decay;
    s->env[2] = (uint8_t)m->sustain; s->env[3] = (uint8_t)m->release;
    s->amount = (int8_t)m->amount; s->target = (uint8_t)m->target;
    s->loop = (uint8_t)m->loop; s->retrigger = (uint8_t)m->retrigger;
    s->scale = (uint8_t)m->scale; s->root = (uint8_t)m->root;
    s->quantize = (uint8_t)m->quantize; s->transpose = (int8_t)m->transpose;
    for (i = 0; i < BR_LFOS; ++i) {
        s->lfo[i][0] = (uint8_t)m->lfo[i].wave; s->lfo[i][1] = (uint8_t)m->lfo[i].rate;
        s->lfo[i][2] = (uint8_t)m->lfo[i].depth; s->lfo[i][3] = (uint8_t)m->lfo[i].target;
    }
    s->lfo[BR_SAVED_LFOS - 1u][0] = BR_WAVE_SINE;
    s->lfo[BR_SAVED_LFOS - 1u][1] = 0u;
    s->lfo[BR_SAVED_LFOS - 1u][2] = 0u;
    s->lfo[BR_SAVED_LFOS - 1u][3] = BR_TARGET_OFF;
    s->fx[0] = (uint8_t)f->drive_mode; s->fx[1] = (uint8_t)f->drive_gain; s->fx[2] = (uint8_t)f->drive_tone;
    s->fx[3] = (uint8_t)f->delay_engine; s->fx[4] = (uint8_t)f->delay_time; s->fx[5] = (uint8_t)f->delay_feedback;
    s->fx[6] = (uint8_t)f->reverb_engine; s->fx[7] = (uint8_t)f->reverb_tone;
    s->fx[8] = (uint8_t)f->mod_engine; s->fx[9] = (uint8_t)f->mod_rate;
    s->fx[10] = (uint8_t)f->mod_depth; s->fx[11] = (uint8_t)f->mod_mix;
    for (i = 0; i < BR_ROUTES; ++i) {
        s->route[i][0] = (uint8_t)(br_mod_source_active(m->route[i].source) ?
            m->route[i].source : BR_SOURCE_OFF);
        s->route[i][1] = (uint8_t)m->route[i].target;
        s->route[i][2] = (uint8_t)(int8_t)m->route[i].amount; s->route[i][3] = (uint8_t)m->route[i].enabled;
    }
    for (i = BR_BANK_STEPS; i < BR_STEPS; ++i) {
        uint32_t step = br_part[p].step[i];
        uint8_t *out = s->extra_step[i - BR_BANK_STEPS];
        out[0] = br_is_kit(br_part[p].sound.preset) ? (uint8_t)BR_STEP_HIT(step) :
                 step & BR_STEP_ON ? (uint8_t)(BR_STEP_NOTE(step) + 1u) : 0;
        out[1] = br_is_kit(br_part[p].sound.preset) ? (uint8_t)BR_STEP_ACC(step) :
                 (uint8_t)(BR_STEP_VEL(step) | (step & BR_STEP_TIE ? 128u : 0u));
    }
}

static void br_unpack_module(uint32_t p, const br_saved_module_t *s, uint32_t version)
{
    br_mod_config_t m;
    br_fx_config_t f;
    uint32_t i;
    m.attack = s->env[0]; m.decay = s->env[1];
    m.sustain = s->env[2]; m.release = s->env[3];
    m.amount = s->amount; m.target = s->target;
    m.loop = s->loop; m.retrigger = s->retrigger;
    m.scale = s->scale; m.root = s->root;
    m.quantize = s->quantize; m.transpose = s->transpose;
    for (i = 0; i < BR_LFOS; ++i) {
        m.lfo[i].wave = s->lfo[i][0];
        m.lfo[i].rate = s->lfo[i][1];
        m.lfo[i].depth = s->lfo[i][2];
        m.lfo[i].target = s->lfo[i][3];
    }
    f.drive_mode = s->fx[0];
    f.drive_gain = s->fx[1]; f.drive_tone = s->fx[2];
    f.delay_engine = s->fx[3];
    f.delay_time = s->fx[4]; f.delay_feedback = s->fx[5];
    /* Retired engines remain accepted in older saved sessions, then migrate
     * to the nearest available space before entering the live FX path. */
    f.reverb_engine = s->fx[6] == BR_REVERB_SPRING ? BR_REVERB_ROOM :
        s->fx[6] == BR_REVERB_ETHEREAL ? BR_REVERB_SPACE : s->fx[6];
    f.reverb_tone = s->fx[7];
    f.mod_engine = s->fx[8];
    f.mod_rate = s->fx[9]; f.mod_depth = s->fx[10];
    f.mod_mix = s->fx[11];
    for (i = 0; i < BR_ROUTES; ++i) {
        m.route[i].source = version < 12u && s->route[i][0] == BR_SOURCE_LFO3 ?
            BR_SOURCE_OFF : s->route[i][0];
        m.route[i].target = s->route[i][1];
        m.route[i].amount = (int8_t)s->route[i][2];
        m.route[i].enabled = s->route[i][3];
    }
    br_part[p].mod = m; br_part[p].fx = f;
    br_part[p].sound.drum_snap = s->snap;
    for (i = BR_BANK_STEPS; i < BR_STEPS; ++i) {
        uint32_t a = s->extra_step[i - BR_BANK_STEPS][0], b = s->extra_step[i - BR_BANK_STEPS][1];
        br_part[p].step[i] = br_is_kit(br_part[p].sound.preset) ? a | b << 8 :
            a ? BR_NOTE_STEP(a - 1u, b & 127u) | (b & 128u ? BR_STEP_TIE : 0u) : 0u;
    }
}

/* Synth tracks never use the eight saved lane-level bytes, so they carry the
 * arpeggiator settings: a marker byte, then mode, rate, octaves-1, gate,
 * latch, swing and repeat-1. Default settings are saved as zeroes, which is
 * also what older projects contain, so the session layout and version are
 * unchanged. Firmware before 1528 rejects a project with non-default arp. */
#define BR_ARP_SAVE_MARK 0x41u
static void br_arp_pack(uint32_t p, uint8_t out[8])
{
    br_part_t def;
    const volatile br_part_t *a = &br_part[p];
    br_arp_part_defaults(&def);
    if (a->arp_mode == def.arp_mode && a->arp_rate == def.arp_rate &&
        a->arp_octaves == def.arp_octaves && a->arp_gate == def.arp_gate &&
        a->arp_latch == def.arp_latch && a->arp_swing == def.arp_swing &&
        a->arp_repeat == def.arp_repeat) return;
    out[0] = BR_ARP_SAVE_MARK;
    out[1] = (uint8_t)br_clip((int32_t)a->arp_mode, 0, BR_ARP_MODES - 1);
    out[2] = (uint8_t)br_clip((int32_t)a->arp_rate, 0, BR_ARP_RATES - 1);
    out[3] = (uint8_t)(br_clip((int32_t)a->arp_octaves, 1, BR_ARP_MAX_OCTAVES) - 1);
    out[4] = (uint8_t)br_clip((int32_t)a->arp_gate, 1, 127);
    out[5] = a->arp_latch != 0u;
    out[6] = a->arp_swing != 0u;
    out[7] = (uint8_t)(br_clip((int32_t)a->arp_repeat, 1, BR_ARP_MAX_REPEAT) - 1);
}

static uint32_t br_arp_packed_valid(const uint8_t in[8])
{
    uint32_t i, zero = 1u;
    for (i = 0u; i < 8u; ++i) zero &= !in[i];
    return zero || (in[0] == BR_ARP_SAVE_MARK && in[1] < BR_ARP_MODES &&
        in[2] < BR_ARP_RATES && in[3] < BR_ARP_MAX_OCTAVES && in[4] >= 1u &&
        in[4] <= 127u && in[5] <= 1u && in[6] <= 1u && in[7] < BR_ARP_MAX_REPEAT);
}

static void br_arp_unpack(uint32_t p, const uint8_t in[8], uint32_t version)
{
    br_part_t *a = &br_part[p];
    br_arp_part_defaults(a);
    if (version < 13u || in[0] != BR_ARP_SAVE_MARK || !br_arp_packed_valid(in)) return;
    a->arp_mode = in[1]; a->arp_rate = in[2]; a->arp_octaves = in[3] + 1u;
    a->arp_gate = in[4]; a->arp_latch = in[5]; a->arp_swing = in[6];
    a->arp_repeat = in[7] + 1u;
}

static void br_pack_part(br_saved_part_t *s, uint32_t p)
{
    const volatile br_sound_t *v = &br_part[p].sound;
    uint32_t i;
#define BR_PACK(field) s->field = (uint8_t)v->field
    BR_PACK(preset); BR_PACK(timbre); BR_PACK(color); s->tune = (int8_t)v->tune;
    BR_PACK(level); BR_PACK(attack); BR_PACK(decay); BR_PACK(sustain); BR_PACK(release);
    BR_PACK(drive); BR_PACK(delay); BR_PACK(reverb); BR_PACK(space);
    /* Version 2 uses the seven spare bits beside mute for the bipolar filter. */
    s->mute = (uint8_t)(((v->filter > 127u ? 127u : v->filter) << 1) | (v->mute != 0));
#undef BR_PACK
    s->length = (uint8_t)br_part[p].length; s->division = (uint8_t)br_part[p].division;
    s->swing = (uint8_t)br_part[p].swing; s->gate = (uint8_t)br_part[p].gate;
    /* The v10 wire shape keeps the old eight-byte prefix, but only the kit
     * track has four live lane levels. Canonical zeroes prevent stale legacy
     * lane data from leaking into newly saved projects. */
    if (p == BR_DRUM_PART)
        for (i = 0; i < BR_DRUM_LANES; i++)
            s->lane_level[i] = (uint8_t)(br_clip(br_part[p].lane_level[i], 0, 127) |
                ((br_kit_mute_mask & (1u << i)) ? 0x80u : 0u));
    else if (p < BR_SYNTH_PARTS) br_arp_pack(p, s->lane_level);
    if (p == BR_DRUM_PART) br_punch_cfg_pack(&br_punch_cfg, s->lane_level + BR_DRUM_LANES);
    for (i = 0; i < BR_BANK_STEPS; i++) {
        uint32_t step = br_part[p].step[i];
        if (br_is_kit(v->preset)) {
            s->step[i][0] = (uint8_t)BR_STEP_HIT(step);
            s->step[i][1] = (uint8_t)BR_STEP_ACC(step);
        } else {
            s->step[i][0] = step & BR_STEP_ON ? (uint8_t)(BR_STEP_NOTE(step) + 1u) : 0;
            s->step[i][1] = (uint8_t)(BR_STEP_VEL(step) | (step & BR_STEP_TIE ? 128u : 0u));
        }
    }
}

/* Persisted records retain the complete 77-ID catalog across target builds.
 * Without Plaits DSP, map its melodic models to CSAW and its kit to BRAIDS KIT. */
static uint32_t br_restore_preset(uint32_t part, uint32_t preset, uint32_t version)
{
    /* The Braids models and the reserved id were removed from pitched tracks in
     * v12. Migrate those old model IDs to the first Plaits model while retaining the saved sound
     * and sequencing controls that follow the preset field. */
    if (version < 12u && part < BR_SYNTH_PARTS &&
        (preset < BR_FIRST_KIT || preset == BR_PRESET_RESERVED))
        preset = BR_FIRST_PLAITS;
    if (preset >= BR_FIRST_PLAITS && preset < BR_FIRST_PLAITS + BR_PLAITS_MODEL_COUNT) {
#if !BR_ENABLE_PLAITS
        return 0u; /* CSAW is the deterministic melodic fallback in no-Plaits images. */
#endif
    }
    if (preset == BR_KIT_PLAITS) {
#if !BR_ENABLE_PLAITS
        return BR_KIT_BRAIDS;
#endif
    }
    return preset;
}

/* Validate against the on-disk catalog, not the models compiled into this
 * particular image. A no-Plaits image must still accept and safely remap a
 * valid v10 project authored by the full catalog. */
static int br_session_preset_role_valid(uint32_t part, uint32_t preset, uint32_t version)
{
    if (part < BR_SYNTH_PARTS && version < 12u)
        return preset < BR_FIRST_KIT ||
            (preset >= BR_PRESET_RESERVED && preset <= BR_KIT_PLAITS - 1u);
    if (part < BR_SYNTH_PARTS)
        return br_is_plaits(preset);
    if (part == BR_DRUM_PART)
        return (preset >= BR_KIT_BRAIDS && preset <= BR_KIT_909) ||
            preset == BR_KIT_PLAITS;
    return 0;
}

static void br_unpack_part(uint32_t p, const br_saved_part_t *s, uint32_t version)
{
    br_sound_t sound;
    uint32_t i, preset = br_restore_preset(p, s->preset, version);
    br_preset_sound(preset, &sound);
#define BR_UNPACK(field) sound.field = s->field
    BR_UNPACK(timbre); BR_UNPACK(color); sound.tune = s->tune;
    BR_UNPACK(level); BR_UNPACK(attack); BR_UNPACK(decay); BR_UNPACK(sustain); BR_UNPACK(release);
    BR_UNPACK(drive); BR_UNPACK(delay); BR_UNPACK(reverb); BR_UNPACK(space);
#undef BR_UNPACK
    sound.mute = version >= 2u ? s->mute & 1u : s->mute != 0;
    sound.filter = version >= 2u ? s->mute >> 1 : 64u;
    if (version < 3u && br_is_kit(sound.preset)) { sound.drum_snap = sound.attack; sound.attack = 0; }
    br_part[p].sound = sound;
    br_part[p].length = s->length;
    br_part[p].division = s->division;
    br_part[p].swing = s->swing;
    br_part[p].gate = s->gate;
    for (i = 0; i < BR_DRUM_LANES; i++)
        br_part[p].lane_level[i] = p == BR_DRUM_PART ?
            s->lane_level[i] & 0x7fu : 0u;
    if (p < BR_SYNTH_PARTS) br_arp_unpack(p, s->lane_level, version);
    if (p == BR_DRUM_PART) {
        if (version >= 13u) br_punch_cfg_unpack(&br_punch_cfg, s->lane_level + BR_DRUM_LANES);
        else { static const br_punch_cfg_t d = BR_PUNCH_CFG_DEFAULTS; br_punch_cfg = d; }
    }
    for (i = 0; i < BR_STEPS; i++) {
        uint32_t a = i < BR_BANK_STEPS ? s->step[i][0] : 0u;
        uint32_t b = i < BR_BANK_STEPS ? s->step[i][1] : 0u;
        br_part[p].step[i] = br_is_kit(sound.preset) ? a | b << 8 :
            a ? BR_NOTE_STEP(a - 1u, b & 127u) | (b & 128u ? BR_STEP_TIE : 0u) : 0u;
    }
}

static int br_session_kit_pattern_valid(const br_session_v10_t *s)
{
    uint32_t i, p;
    const br_saved_part_t *kit = &s->v3.part[BR_DRUM_PART];
    const br_saved_module_t *module = &s->v3.module[BR_DRUM_PART];
    for (i = 0u; i < BR_BANK_STEPS; ++i)
        if ((kit->step[i][0] & ~BR_DRUM_ALL_MASK) ||
            (kit->step[i][1] & ~BR_DRUM_ALL_MASK)) return 0;
    for (i = 0u; i < BR_STEPS - BR_BANK_STEPS; ++i)
        if ((module->extra_step[i][0] & ~BR_DRUM_ALL_MASK) ||
            (module->extra_step[i][1] & ~BR_DRUM_ALL_MASK)) return 0;
    /* Melodic steps store note+1 (0 for off) and velocity/tie. */
    for (p = 0u; p < BR_SYNTH_PARTS; ++p) {
        for (i = 0u; i < BR_BANK_STEPS; ++i)
            if (s->v3.part[p].step[i][0] > 128u) return 0;
        for (i = 0u; i < BR_STEPS - BR_BANK_STEPS; ++i)
            if (s->v3.module[p].extra_step[i][0] > 128u) return 0;
    }
    return 1;
}

static int br_session_module_valid(const br_saved_module_t *m, uint32_t version)
{
    uint32_t i;
    if (m->snap > 127u || m->env[0] > 127u || m->env[1] > 127u ||
        m->env[2] > 127u || m->env[3] > 127u || m->amount < -127 ||
        m->target >= BR_TARGET_COUNT || m->loop > 1u || m->retrigger > 1u ||
        m->scale >= BR_SCALE_COUNT || m->root >= 12u || m->quantize > 1u ||
        m->transpose < -24 || m->transpose > 24 ||
        m->fx[0] >= BR_DRIVE_COUNT || m->fx[1] > 127u || m->fx[2] > 127u ||
        m->fx[3] >= BR_DELAY_COUNT || m->fx[4] > 127u || m->fx[5] > 127u ||
        m->fx[6] >= BR_REVERB_COUNT || m->fx[7] > 127u ||
        m->fx[8] >= BR_MOD_COUNT || m->fx[9] > 127u || m->fx[10] > 127u ||
        m->fx[11] > 127u) return 0;
    for (i = 0u; i < BR_SAVED_LFOS; ++i)
        if (m->lfo[i][0] >= BR_WAVE_COUNT || m->lfo[i][1] > 127u ||
            m->lfo[i][2] > 127u || m->lfo[i][3] >= BR_TARGET_COUNT) return 0;
    if (version >= 12u &&
        (m->lfo[2][0] != BR_WAVE_SINE || m->lfo[2][1] || m->lfo[2][2] ||
         m->lfo[2][3] != BR_TARGET_OFF)) return 0;
    for (i = 0u; i < BR_ROUTES; ++i)
        if (m->route[i][0] >= BR_SOURCE_COUNT || m->route[i][1] >= BR_TARGET_COUNT ||
            (version >= 12u && m->route[i][0] == BR_SOURCE_LFO3) ||
            (int8_t)m->route[i][2] < -127 || m->route[i][3] > 1u) return 0;
    return 1;
}

static int br_session_prefix_valid(const br_session_v10_t *s, uint32_t version)
{
    uint32_t p, lane, i;
    uint32_t kit_choice_mask = version >= 12u ? BR_DRUM_CHOICE_MASK : BR_DRUM_HIT_MASK;
    uint32_t bpm;
    if (!s) return 0;
    bpm = s->v3.bpm + (version >= 13u && (s->reserved_v10[2] & 0x80u) ? 256u : 0u);
    if (s->v3.magic != BR_SESSION_MAGIC || s->v3.version != version ||
        bpm < 40u || bpm > (version >= 13u ? BR_BPM_STORED_MAX : 240u) ||
        s->v3.selected >= BR_PARTS || s->v3.octave < -2 || s->v3.octave > 2 ||
        (s->kit_choice_mask & ~kit_choice_mask) ||
        ((s->reserved_v4[0] || s->reserved_v4[1] || s->reserved_v4[2]) &&
         (version < 11u || s->reserved_v4[0] != 0x52u ||
          s->reserved_v4[1] > 127u || s->reserved_v4[2] != 0xa7u)) ||
        (version < 13u && (s->reserved_v10[0] || s->reserved_v10[1] || s->reserved_v10[2])) ||
        (version >= 13u && ((s->reserved_v10[2] & 0x7fu) != 0x51u ||
         (s->reserved_v10[0] & 15u) >= BR_TIME_QUANT_COUNT ||
         (s->reserved_v10[0] >> 4) >= BR_TIME_QUANT_COUNT ||
         (s->reserved_v10[1] & 15u) >= BR_TIME_QUANT_COUNT ||
         (s->reserved_v10[1] >> 4) >= BR_TIME_QUANT_COUNT)) ||
        s->select_target >= BR_SELECT_COUNT ||
        s->compressor[0] >= BR_COMP_SOURCE_COUNT || s->compressor[1] > 48u ||
        s->compressor[2] > 7u || s->compressor[3] > 24u ||
        s->compressor[4] > 127u || s->compressor[5] > 127u ||
        s->compressor[6] > 127u || s->compressor[7] > 127u ||
        s->master_fx[0] > 127u || s->master_fx[1] > 127u ||
        s->master_fx[2] > 127u || s->master_fx[3] > 127u ||
        !br_session_kit_pattern_valid(s)) return 0;
    if (version >= 12u &&
        (s->master_fx[1] >= BR_HEAT_COUNT || s->master_fx[2] > 127u ||
         s->master_fx[3] != 0u)) return 0;
    if (version >= 12u)
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
            if (((s->kit_choice_mask >> (lane * 2u)) & 3u) == 3u) return 0;
    for (p = 0u; p < BR_PARTS; ++p) {
        const br_saved_part_t *part = &s->v3.part[p];
        if (!br_session_preset_role_valid(p, part->preset, version)) return 0;
        if (part->timbre > 127u || part->color > 127u || part->tune < -24 ||
            part->tune > 24 || part->level > 127u || part->attack > 127u ||
            part->decay > 127u || part->sustain > 127u || part->release > 127u ||
            part->drive > 127u || part->delay > 127u || part->reverb > 127u ||
            part->space > 127u || part->length < 1u || part->length > BR_STEPS ||
            part->division > 2u || part->swing > BR_SWING_MAX || part->gate < 1u ||
            part->gate > 127u || !br_session_module_valid(&s->v3.module[p], version) ||
            s->plaits[p][0] > 127u || s->plaits[p][1] > 127u ||
            s->pan[p] > 127u ||
            (version >= 13u ? !br_session_legacy_gate_ok(s->legacy_gate[p], p) : s->legacy_gate[p] > BR_GATE_OPEN)) return 0;
        for (lane = 0u; lane < 8u; ++lane) {
            if (p == BR_DRUM_PART && lane < BR_DRUM_LANES) {
                if ((version < 15u && part->lane_level[lane] > 127u) ||
                    s->lane_gate[p][lane] < 1u || s->lane_gate[p][lane] > 128u)
                    return 0;
            } else if ((p >= BR_SYNTH_PARTS && !(p == BR_DRUM_PART && lane >= BR_DRUM_LANES) &&
                        part->lane_level[lane] != 0u) || s->lane_gate[p][lane] != 128u) {
                return 0;
            }
        }
        if (p < BR_SYNTH_PARTS && !br_arp_packed_valid(part->lane_level)) return 0;
        if (p == BR_DRUM_PART && (!br_punch_cfg_packed_valid(part->lane_level + BR_DRUM_LANES) ||
            (version < 13u && part->lane_level[BR_DRUM_LANES]))) return 0;
    }
    return 1;
}

static int br_session_v10_valid(const br_session_v10_t *s)
{
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    (void)s; return 0;
#else
    return br_session_prefix_valid(s, 10u);
#endif
}

static int br_session_full_valid(const br_session_t *s, uint32_t version)
{
    uint32_t p, step, lane;
    uint32_t voice_limit = version >= 12u ? BR_DRUM_VOICE_COUNT : 8u;
    if (!s || !br_session_prefix_valid(&s->v10, version)) return 0;
    for (p = 0u; p < BR_SYNTH_PARTS; ++p) {
        for (step = 0u; step < BR_STEPS; ++step) {
            if ((version >= 13u ? (s->synth_length[p][step] & 0x7ffu) : s->synth_length[p][step]) > BR_STEP_LENGTH_MAX ||
                s->synth_condition[p][step] >= BR_STEP_COND_COUNT ||
                (s->synth_note[p][step] != 255u && s->synth_note[p][step] > 127u) ||
                (s->synth_velocity[p][step] != 255u &&
                 s->synth_velocity[p][step] > 127u)) return 0;
        }
    }
    for (step = 0u; step < BR_STEPS; ++step) {
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            uint32_t note = s->kit_note[step][lane];
            if ((version >= 13u ? (s->kit_length[step][lane] & 0x7ffu) : s->kit_length[step][lane]) > BR_STEP_LENGTH_MAX ||
                s->kit_condition[step][lane] >= BR_STEP_COND_COUNT ||
                s->kit_velocity[step][lane] > 127u ||
                (version < 14u && note != 255u &&
                 (note >= voice_limit || br_drum_voice_lane(note) != lane)) ||
                (s->kit_pitch[step][lane] != 255u &&
                 (s->kit_pitch[step][lane] < 36u || s->kit_pitch[step][lane] > 84u)) ||
                (version >= 14u && (version < 16u || step * BR_DRUM_LANES + lane >= 19u) && (s->kit_retrig[step][lane] & 0xc0u)))
                return 0;
        }
    }
    for (p = 0u; p < BR_PARTS; ++p) {
        if (s->model_controls[p][0] > 127u || s->model_controls[p][1] > 1u ||
            s->model_controls[p][2] > 127u || s->model_controls[p][3] > 127u ||
            s->model_controls[p][4] > 127u ||
            (version >= 13u ? !br_session_legacy_gate_ok(s->legacy_gate[p], p) : s->legacy_gate[p] != BR_GATE_OPEN))
            return 0;
    }
    return 1;
}

static int br_session_v11_valid(const br_session_t *s)
{
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    (void)s; return 0;
#else
    return br_session_full_valid(s, 11u);
#endif
}

static int br_session_v12_valid(const br_session_t *s)
{
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    (void)s; return 0;
#else
    return br_session_full_valid(s, 12u);
#endif
}
static int br_session_v13_valid(const br_session_t *s)
{
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    (void)s; return 0;
#else
    return br_session_full_valid(s, 13u);
#endif
}
static int br_session_v14_valid(const br_session_t *s)
{
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    (void)s; return 0;
#else
    return br_session_full_valid(s, 14u);
#endif
}
static int br_session_v15_valid(const br_session_t *s)
{
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    (void)s; return 0;
#else
    return br_session_full_valid(s, 15u);
#endif
}
/* v16 uses formerly required-zero retrigger high bits; low six bits remain
 * unchanged. First 15 entries store little-endian 2-bit groups: channels
 * (1..16 encoded as 0..15) in bits 0..27, receive-clock in bit 28, and
 * bit 29 reserved zero. The next seven high bits encode MIDI OFF for the
 * seven destinations; their channel nibble must be zero. Other bits stay zero.
 * Earlier v16 firmware rejects sessions with OFF, rather than misreading them. */
static uint32_t br_session_midi_word(const br_session_t *s)
{
    uint32_t i, word = 0u;
    for (i = 0u; i < 15u; ++i)
        word |= ((uint32_t)s->kit_retrig[i / BR_DRUM_LANES][i % BR_DRUM_LANES] >> 6u) << (i * 2u);
    return word;
}
static int br_session_v16_valid(const br_session_t *s)
{
    uint32_t i, word;
    if (!br_session_full_valid(s, 16u)) return 0;
    word = br_session_midi_word(s);
    /* Bit 28 follows an external clock and bit 29 sends the clock: never both. */
    if ((word >> 28u) == 3u || (s->kit_retrig[4][2] & 0x80u)) return 0;
    for (i = 0u; i < 7u; ++i) {
        uint32_t at = 15u + i / 2u;
        if ((s->kit_retrig[at / BR_DRUM_LANES][at % BR_DRUM_LANES] & (1u << (6u + i % 2u))) &&
            ((word >> (i * 4u)) & 15u)) return 0;
    }
    return 1;
}
static void br_session_midi_defaults(void)
{
    uint32_t i;
    for (i = 0u; i < 7u; ++i) br_midi_channels[i] = (uint8_t)(i + 1u);
    br_midi_clock_enabled = 0u;
    br_midi_clock_out = 0u;
}
static void br_session_midi_restore(const br_session_t *s, uint32_t version)
{
    uint32_t i, word;
    if (version < 16u) { br_session_midi_defaults(); return; }
    word = br_session_midi_word(s);
    for (i = 0u; i < 7u; ++i) {
        uint32_t at = 15u + i / 2u;
        br_midi_channels[i] = (s->kit_retrig[at / BR_DRUM_LANES][at % BR_DRUM_LANES] &
                               (1u << (6u + i % 2u))) ? 0u : (uint8_t)(((word >> (i * 4u)) & 15u) + 1u);
    }
    br_midi_clock_enabled = (uint8_t)((word >> 28u) & 1u);
    br_midi_clock_out = (uint8_t)((word >> 29u) & 1u);
}
static uint16_t br_session_timed_length(uint16_t length, int8_t offset)
{ return (uint16_t)((length & 0x7ffu) | (((uint16_t)offset & 31u) << 11)); }
static int8_t br_session_micro(uint16_t packed)
{ uint32_t bits = packed >> 11; return (int8_t)(bits >= 16u ? (int32_t)bits - 32 : (int32_t)bits); }

static uint32_t br_migrate_kit_choices(uint32_t old_choices)
{
    uint32_t lane, choices = 0u;
    for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
        if (old_choices & (1u << lane)) choices |= 1u << (lane * 2u);
    return choices;
}

static int br_capture(br_session_t *s)
{
    uint32_t p, lane, step;
    if (!s) return 0;
    memset(s, 0, sizeof *s);
    s->v3.magic = BR_SESSION_MAGIC; s->v3.version = 16u;
    s->v3.bpm = (uint8_t)br_clip((int32_t)br_bpm, BR_BPM_MIN, BR_BPM_MAX);
    s->v3.selected = br_ui.part; s->v3.octave = br_ui.octave;
    for (p = 0; p < BR_PARTS; p++) {
        br_pack_part(&s->v3.part[p], p);
        br_pack_module(&s->v3.module[p], p);
        s->plaits[p][0] = (uint8_t)br_clip(br_part[p].sound.plaits_harmonics, 0, 127);
        s->plaits[p][1] = (uint8_t)br_clip(br_part[p].sound.plaits_aux, 0, 127);
        s->pan[p] = (uint8_t)br_clip(br_part[p].sound.pan, 0, 127);
        s->legacy_gate[p] = BR_GATE_OPEN;
        s->model_controls[p][0] = (uint8_t)br_clip(br_part[p].sound.filter_resonance, 0, 127);
        s->model_controls[p][1] = (uint8_t)(br_part[p].sound.filter_keytrack != 0u);
        s->model_controls[p][2] = (uint8_t)br_clip(br_part[p].sound.timbre_mod, 0, 127);
        s->model_controls[p][3] = (uint8_t)br_clip(br_part[p].sound.fm_mod, 0, 127);
        s->model_controls[p][4] = (uint8_t)br_clip(br_part[p].sound.morph_mod, 0, 127);
        for (lane = 0; lane < 8u; ++lane)
            s->lane_gate[p][lane] = (uint8_t)(p == BR_DRUM_PART && lane < BR_DRUM_LANES ?
                br_clip(br_part[p].lane_gate[lane], 1, 128) : 128u);
    }
    if (br_part[BR_DRUM_PART].sound.drum_voice_mask & ~BR_DRUM_CHOICE_MASK) return 0;
    s->kit_choice_mask = (uint8_t)br_part[BR_DRUM_PART].sound.drum_voice_mask;
    br_pack_master(&s->v10);
    s->reserved_v10[0] = (uint8_t)(br_seq_record_quantize[0] | (br_seq_record_quantize[1] << 4));
    s->reserved_v10[1] = (uint8_t)(br_seq_record_quantize[2] | (br_seq_record_quantize[3] << 4));
    /* The old extended marker is accepted on load; new saves need one byte. */
    s->reserved_v10[2] = 0x51u;
    for (p = 0u; p < BR_SYNTH_PARTS; ++p) {
        for (step = 0u; step < BR_STEPS; ++step) {
            s->synth_length[p][step] = br_session_timed_length(br_seq_synth_length[p][step], br_seq_synth_micro[p][step]);
            s->synth_condition[p][step] = br_seq_synth_condition[p][step];
            s->synth_note[p][step] = br_seq_synth_note[p][step];
            s->synth_velocity[p][step] = br_seq_synth_velocity[p][step];
        }
    }
    for (step = 0u; step < BR_STEPS; ++step) {
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            s->kit_length[step][lane] = br_session_timed_length(br_seq_kit_length[step][lane], br_seq_kit_micro[step][lane]);
            s->kit_condition[step][lane] = br_seq_kit_condition[step][lane];
            s->kit_velocity[step][lane] = br_seq_kit_velocity[step][lane];
            s->kit_pitch[step][lane] = br_seq_kit_pitch[step][lane];
            if (br_seq_kit_retrig[step][lane] & 0xc0u) return 0;
            s->kit_retrig[step][lane] = br_seq_kit_retrig[step][lane];
        }
    }
    {
        uint32_t word = 0u;
        if (br_midi_clock_enabled > 1u || br_midi_clock_out > 1u ||
            (br_midi_clock_enabled && br_midi_clock_out)) return 0;
        for (p = 0u; p < 7u; ++p) {
            uint32_t channel = br_midi_channels[p];
            if (channel > 16u) return 0;
            if (channel) word |= (channel - 1u) << (p * 4u);
            else {
                uint32_t at = 15u + p / 2u;
                s->kit_retrig[at / BR_DRUM_LANES][at % BR_DRUM_LANES] |= (uint8_t)(1u << (6u + p % 2u));
            }
        }
        word |= (uint32_t)br_midi_clock_enabled << 28u | (uint32_t)br_midi_clock_out << 29u;
        for (p = 0u; p < 15u; ++p)
            s->kit_retrig[p / BR_DRUM_LANES][p % BR_DRUM_LANES] |= (uint8_t)(((word >> (p * 2u)) & 3u) << 6u);
    }
    return br_session_v16_valid(s);
}

static void br_restore_prefix(const br_session_v10_t *s, uint32_t version)
{
    uint32_t p, lane, kit_mute_mask = 0u;
    /* Solo is transient; older sessions have no per-lane mutes. */
    if (version >= 15u)
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
            if (s->v3.part[BR_DRUM_PART].lane_level[lane] & 0x80u)
                kit_mute_mask |= 1u << lane;
    /* All direct unpack assignments below are bounded by prefix validation. */
    br_bpm = s->v3.bpm + (version >= 13u && (s->reserved_v10[2] & 0x80u) ? 256u : 0u);
    if (br_bpm < BR_BPM_MIN) br_bpm = BR_BPM_MIN;
    if (br_bpm > BR_BPM_MAX) br_bpm = BR_BPM_MAX;
    br_ui.part = s->v3.selected;
    br_ui.octave = s->v3.octave;
    for (p = 0; p < BR_PARTS; p++) {
        br_part[p].revision++;
        if (p == BR_DRUM_PART) {
            br_kit_mute_mask = kit_mute_mask;
            br_kit_solo_mask = 0u;
        }
        br_unpack_part(p, &s->v3.part[p], version);
        /* The old legacy gate byte remains part of v10 compatibility data, but
         * v11 retires that duration cutoff. The canonical runtime value keeps
         * older preset/session readers harmless. */
        br_part[p].sound.gate = BR_GATE_OPEN;
        br_part[p].sound.plaits_harmonics = s->plaits[p][0];
        br_part[p].sound.plaits_aux = s->plaits[p][1];
        br_part[p].sound.pan = s->pan[p];
        br_part[p].sound.drum_voice_mask = p == BR_DRUM_PART ?
            (version >= 12u ? s->kit_choice_mask : br_migrate_kit_choices(s->kit_choice_mask)) : 0u;
        for (lane = 0; lane < BR_DRUM_LANES; ++lane) {
            br_part[p].lane_level[lane] = p == BR_DRUM_PART ?
                (uint32_t)(s->v3.part[p].lane_level[lane] & 0x7fu) : 0u;
            br_part[p].lane_gate[lane] = p == BR_DRUM_PART ?
                (uint32_t)s->lane_gate[p][lane] : 128u;
        }
        br_unpack_module(p, &s->v3.module[p], version);
        RING_PUBLISH(); br_part[p].revision++;
    }
    br_unpack_master(s, version);
    if (br_ui.cursor >= br_part[br_ui.part].length) br_ui.cursor = br_part[br_ui.part].length - 1u;
    br_ui.step_bank = br_ui.cursor / BR_BANK_STEPS;
    br_ui.redraw = 1;
}

static void br_step_metadata_defaults(const br_session_v10_t *s)
{
    uint32_t step, lane;
    memset((void *)br_seq_synth_micro, 0, sizeof br_seq_synth_micro);
    memset((void *)br_seq_kit_micro, 0, sizeof br_seq_kit_micro);
    memset((void *)br_seq_kit_retrig, 0, sizeof br_seq_kit_retrig);
    memset((void *)br_seq_record_quantize, 0, sizeof br_seq_record_quantize);
    memset((void *)br_seq_synth_length, 0, sizeof br_seq_synth_length);
    memset((void *)br_seq_synth_condition, 0, sizeof br_seq_synth_condition);
    memset((void *)br_seq_synth_note, 255, sizeof br_seq_synth_note);
    memset((void *)br_seq_synth_velocity, 255, sizeof br_seq_synth_velocity);
    memset((void *)br_seq_kit_length, 0, sizeof br_seq_kit_length);
    memset((void *)br_seq_kit_condition, 0, sizeof br_seq_kit_condition);
    memset((void *)br_seq_kit_note, 255, sizeof br_seq_kit_note);
    memset((void *)br_seq_kit_pitch, 255, sizeof br_seq_kit_pitch);
    for (step = 0u; step < BR_STEPS; ++step) {
        uint32_t hits = step < BR_BANK_STEPS ?
            s->v3.part[BR_DRUM_PART].step[step][0] :
            s->v3.module[BR_DRUM_PART].extra_step[step - BR_BANK_STEPS][0];
        uint32_t accents = step < BR_BANK_STEPS ?
            s->v3.part[BR_DRUM_PART].step[step][1] :
            s->v3.module[BR_DRUM_PART].extra_step[step - BR_BANK_STEPS][1];
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
            br_seq_kit_velocity[step][lane] =
                (hits & accents & (1u << lane)) ? 127u : 100u;
    }
}

static void br_step_metadata_clear(void)
{
    uint32_t step, lane;
    memset((void *)br_seq_synth_micro, 0, sizeof br_seq_synth_micro);
    memset((void *)br_seq_kit_micro, 0, sizeof br_seq_kit_micro);
    memset((void *)br_seq_kit_retrig, 0, sizeof br_seq_kit_retrig);
    memset((void *)br_seq_record_quantize, 0, sizeof br_seq_record_quantize);
    memset((void *)br_seq_synth_length, 0, sizeof br_seq_synth_length);
    memset((void *)br_seq_synth_condition, 0, sizeof br_seq_synth_condition);
    memset((void *)br_seq_synth_note, 255, sizeof br_seq_synth_note);
    memset((void *)br_seq_synth_velocity, 255, sizeof br_seq_synth_velocity);
    memset((void *)br_seq_kit_length, 0, sizeof br_seq_kit_length);
    memset((void *)br_seq_kit_condition, 0, sizeof br_seq_kit_condition);
    memset((void *)br_seq_kit_note, 255, sizeof br_seq_kit_note);
    memset((void *)br_seq_kit_pitch, 255, sizeof br_seq_kit_pitch);
    for (step = 0u; step < BR_STEPS; ++step)
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane)
            br_seq_kit_velocity[step][lane] = 100u;
}

static int br_restore_v10(const br_session_v10_t *s)
{
    if (!br_session_v10_valid(s)) return 0;
    br_midi_before_session_restore();
    br_restore_prefix(s, 10u);
    br_session_midi_defaults();
    br_step_metadata_defaults(s);
    return 1;
}

static int br_restore(const br_session_t *s)
{
    uint32_t p, step, lane;
    uint32_t version;
    if (!s) return 0;
    version = s->v3.version;
    if ((version == 11u && !br_session_v11_valid(s)) ||
        (version == 12u && !br_session_v12_valid(s)) ||
        (version == 13u && !br_session_v13_valid(s)) ||
        (version == 14u && !br_session_v14_valid(s)) ||
        (version == 15u && !br_session_v15_valid(s)) ||
        (version == 16u && !br_session_v16_valid(s)) ||
        (version != 11u && version != 12u && version != 13u && version != 14u && version != 15u && version != 16u)) return 0;
    br_midi_before_session_restore();
    br_solo_mask = 0u;
    br_restore_prefix(&s->v10, version);
    br_session_midi_restore(s, version);
    for (p = 0u; p < BR_PARTS; ++p)
        br_seq_record_quantize[p] = version >= 13u ?
            (uint8_t)((s->reserved_v10[p / 2u] >> ((p & 1u) * 4u)) & 15u) : BR_TIME_QUANT_STEP;
    for (p = 0u; p < BR_PARTS; ++p) {
        br_part[p].revision++;
        br_part[p].sound.filter_resonance = s->model_controls[p][0];
        br_part[p].sound.filter_keytrack = s->model_controls[p][1];
        br_part[p].sound.timbre_mod = s->model_controls[p][2];
        br_part[p].sound.fm_mod = s->model_controls[p][3];
        br_part[p].sound.morph_mod = s->model_controls[p][4];
        RING_PUBLISH(); br_part[p].revision++;
    }
    for (p = 0u; p < BR_SYNTH_PARTS; ++p) {
        for (step = 0u; step < BR_STEPS; ++step) {
            br_seq_synth_length[p][step] = version >= 13u ? s->synth_length[p][step] & 0x7ffu : s->synth_length[p][step];
            br_seq_synth_micro[p][step] = version >= 13u ? br_session_micro(s->synth_length[p][step]) : 0;
            br_seq_synth_condition[p][step] = s->synth_condition[p][step];
            br_seq_synth_note[p][step] = s->synth_note[p][step];
            br_seq_synth_velocity[p][step] = s->synth_velocity[p][step];
        }
    }
    for (step = 0u; step < BR_STEPS; ++step) {
        for (lane = 0u; lane < BR_DRUM_LANES; ++lane) {
            br_seq_kit_length[step][lane] = version >= 13u ? s->kit_length[step][lane] & 0x7ffu : s->kit_length[step][lane];
            br_seq_kit_micro[step][lane] = version >= 13u ? br_session_micro(s->kit_length[step][lane]) : 0;
            br_seq_kit_condition[step][lane] = s->kit_condition[step][lane];
            br_seq_kit_velocity[step][lane] = s->kit_velocity[step][lane];
            br_seq_kit_note[step][lane] = version < 14u ? s->kit_note[step][lane] : 255u;
            br_seq_kit_pitch[step][lane] = s->kit_pitch[step][lane];
            br_seq_kit_retrig[step][lane] = version >= 14u ? (s->kit_retrig[step][lane] & 63u) : 0u;
        }
    }
    return 1;
}

#if !defined(FM1_PATTERN_V16_ONLY) || !FM1_PATTERN_V16_ONLY
static uint32_t br_session_legacy_length_version(uint32_t length)
{
    switch (length) {
    case 968u: return 10u;
    case 240u: return 2u; /* versions 1 and 2 share this length */
    case 900u: return 3u;
    case 908u: return 4u;
    case 916u: return 5u;
    case 920u: return 6u;
    case 952u: return 7u;
    case 960u: return 8u;
    case 964u: return 9u;
    default: return 0u;
    }
}

static void br_load_legacy_length(uint32_t length)
{
    uint32_t version, magic;
    if (br_legacy_migration_pending || br_invalid_session_pending) return;
    if (settings_load(br_legacy_raw_session, length)) return;
    memcpy(&magic, br_legacy_raw_session, sizeof magic);
    version = br_legacy_raw_session[7];
    if (!br_session_legacy_length_version(length) || magic != BR_SESSION_MAGIC ||
        version < 1u || version > 10u ||
        (version == 1u || version == 2u ? length != 240u :
         br_session_legacy_length_version(length) != version)) {
        br_invalid_session_pending = 1u;
        return;
    }
    if (version == 10u) {
        if (!br_restore_v10((const br_session_v10_t *)br_legacy_raw_session)) {
            br_invalid_session_pending = 1u;
            return;
        }
    }
    br_legacy_raw_length = length;
    br_legacy_raw_version = version;
    br_legacy_migration_pending = 1u;
}

#endif

/* settings_load() intentionally has a simple 0/nonzero contract. If no known
 * record parsed, inspect only the settings header to distinguish a truly
 * erased sector from a nonempty/corrupt or unknown record. Fail closed when
 * flash status cannot be read; an ordinary SAVE must not erase uncertain data. */
static uint32_t br_session_settings_header_nonempty(void)
{
#if defined(DINGHY_HOST)
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    uint32_t i;
    for (i=0u;i<sizeof host_store;i++) if (host_store[i]!=0u) return 1u;
    return 0u;
#else
    return host_store[0] != 0u;
#endif
#elif defined(FM1_DEVICE)
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    uint32_t words[16], off, i;
    if (br_engine_pipeline_uncontained_fault_get() || !flash_ok) return 1u;
    for (off=0u;off<0x1000u;off+=sizeof words) {
        if (st_read(SETTINGS_AT+off,words,sizeof words)) return 1u;
        for (i=0u;i<16u;i++) if (words[i]!=0xFFFFFFFFu) return 1u;
    }
    return 0u;
#else
    uint32_t header[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    if (br_engine_pipeline_uncontained_fault_get() || !flash_ok ||
        st_read(SETTINGS_AT, header, sizeof header)) return 1u;
    /* A blank settings record has an erased 12-byte header. Inspect all three
     * words so a torn/nonempty header with an erased magic word still blocks
     * ordinary SAVE. Payload bytes behind a completely erased header are not
     * addressable as a valid record and are outside this header-only check. */
    return header[0] != 0xFFFFFFFFu || header[1] != 0xFFFFFFFFu ||
        header[2] != 0xFFFFFFFFu;
#endif
#else
    return 0u;
#endif
}

static void br_load_session(void)
{
    uint32_t version;
    br_legacy_migration_pending = 0u;
    br_invalid_session_pending = 0u;
    br_legacy_raw_length = br_legacy_raw_version = 0u;
#if !defined(FM1_PATTERN_V16_ONLY) || !FM1_PATTERN_V16_ONLY
    memset(br_legacy_raw_session, 0, sizeof br_legacy_raw_session);
#endif
#if !defined(FM1_PATTERN_V16_ONLY) || !FM1_PATTERN_V16_ONLY
    br_step_metadata_clear();
#endif
    memset(&br_loaded, 0, sizeof br_loaded);
    {
    uint32_t loaded_length = 0u;
    if (!settings_load(&br_loaded, sizeof br_loaded)) loaded_length = sizeof br_loaded;
    if (loaded_length) {
        version = br_loaded.v3.version;
        if (version == 12u || version == 13u || version == 14u || version == 15u || version == 16u) {
            if (!(version == 16u ? br_session_v16_valid(&br_loaded) :
                  version == 15u ? br_session_v15_valid(&br_loaded) :
                  version == 14u ? br_session_v14_valid(&br_loaded) :
                  version == 13u ? br_session_v13_valid(&br_loaded) : br_session_v12_valid(&br_loaded)) || !br_restore(&br_loaded))
                br_invalid_session_pending = 1u;
        } else if (version == 11u) {
            if (!br_session_v11_valid(&br_loaded) || !br_restore(&br_loaded)) {
                br_invalid_session_pending = 1u;
            } else {
                /* The input bytes remain in br_loaded for exact legacy export
                 * until the explicitly converted v12 upload is accepted. */
                br_legacy_raw_length = BR_SESSION_V13_SIZE;
                br_legacy_raw_version = 11u;
                br_legacy_migration_pending = 1u;
            }
        } else {
            br_invalid_session_pending = 1u;
        }
        return;
    }
    }
#if !defined(FM1_PATTERN_V16_ONLY) || !FM1_PATTERN_V16_ONLY
    br_load_legacy_length(968u);
    if (br_legacy_migration_pending || br_invalid_session_pending) return;
    br_load_legacy_length(964u);
    br_load_legacy_length(960u);
    br_load_legacy_length(952u);
    br_load_legacy_length(920u);
    br_load_legacy_length(916u);
    br_load_legacy_length(908u);
    br_load_legacy_length(900u);
    br_load_legacy_length(240u);
#endif
    if (!br_legacy_migration_pending && !br_invalid_session_pending &&
        br_session_settings_header_nonempty())
        br_invalid_session_pending = 1u;
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    /* A rejected record must leave the current live take intact. Blank
     * settings still require the usual fresh-session metadata defaults. */
    if (!br_session_save_blocked()) br_step_metadata_clear();
#endif
}
static void br_remember(void) { br_undo_valid = (uint8_t)br_capture(&br_undo); }

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int br_save_capture_copy(void)
{
    /* br_loaded is no longer needed once migration is accepted. SAVE is
     * blocked while legacy bytes must be preserved, so it can stage a
     * validated capture without a 3.5 KiB automatic session object. */
    if (br_session_save_blocked() || !br_capture(&br_loaded)) return 0;
    br_pending_save = br_loaded;
    return 1;
}

static void br_save_poll(void)
{
    if (br_save_pending && br_session_save_blocked()) {
        br_save_pending = 0u;
        br_message("CONVERT FIRST");
        return;
    }
    if (br_save_pending && !br_playing && br_engine_quiet() && br_fx_quiet()) {
        if (!settings_save(&br_pending_save, sizeof br_pending_save)) br_message("SESSION SAVED");
        else br_message("SAVE FAILED");
        br_save_pending = 0;
    }
}

#if defined(BR_PATTERN_SESSION_LOAN_EXPERIMENT) && BR_PATTERN_SESSION_LOAN_EXPERIMENT
static unsigned br_pattern_loan_mask;
static int br_pattern_exact_loan(const void *p,size_t n,unsigned bit,const br_session_t *owner)
{ return (br_pattern_loan_mask&bit) && p==owner && n==sizeof *owner; }
#else
static int br_pattern_exact_loan(const void *p,size_t n,unsigned bit,const br_session_t *owner)
{ (void)p;(void)n;(void)bit;(void)owner;return 0; }
#endif
#if defined(BR_PATTERN_BANK_EXPERIMENT) && BR_PATTERN_BANK_EXPERIMENT && (!defined(FM1_DEVICE) || (defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT))
/* Host-only bridge to the actual legacy-session capture/validation path.
 * The caller must quiesce audio and foreground edits for the transaction. */
static br_session_t *br_pattern_session_scratch;
static int br_pattern_session_overlap(const void *a,size_t an,const void *b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an&&bn&&(x<=y?y-x<an:x-y<bn); }
#if defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
#define BR_PATTERN_LEGACY_OVERLAP(s,n) 0
#else
#define BR_PATTERN_LEGACY_OVERLAP(s,n) br_pattern_session_overlap((s),(n),br_legacy_raw_session,sizeof br_legacy_raw_session)
#endif
/* Binding is host-only. Do not borrow the async save, loaded, undo or legacy
 * buffers: they may have independent lifetimes despite stopped transport. */
int br_pattern_session_bind(void *scratch,size_t n)
{ br_pattern_session_scratch=0;if(!scratch||n<sizeof(br_session_t)||((uintptr_t)scratch%_Alignof(br_session_t))||(br_pattern_session_overlap(scratch,n,&br_pending_save,sizeof br_pending_save)&&!br_pattern_exact_loan(scratch,n,2u,&br_pending_save))||(br_pattern_session_overlap(scratch,n,&br_loaded,sizeof br_loaded)&&!br_pattern_exact_loan(scratch,n,1u,&br_loaded))||(br_pattern_session_overlap(scratch,n,&br_undo,sizeof br_undo)&&!br_pattern_exact_loan(scratch,n,4u,&br_undo))||BR_PATTERN_LEGACY_OVERLAP(scratch,n))return 0;br_pattern_session_scratch=scratch;return 1; }
#include "pattern_snapshot.h"
/* Decode the v16 pattern fields without restoring the engine or touching MIDI,
 * transport, voices or control state. Scratch and output must be disjoint. */
int br_pattern_session_snapshot(const uint8_t *in,size_t n,br_pattern_snapshot *out)
{
 br_session_t *s=br_pattern_session_scratch;uint32_t p,i,l;
 if(!s||!in||!out||n!=sizeof *s||br_pattern_session_overlap(in,n,s,sizeof *s)||br_pattern_session_overlap(out,sizeof *out,s,sizeof *s)||br_pattern_session_overlap(in,n,out,sizeof *out))return 0;
 memcpy(s,in,sizeof *s);if(!br_session_v16_valid(s))return 0;memset(out,0,sizeof *out);
 for(p=0;p<BR_PARTS;p++){out->length[p]=s->v3.part[p].length;out->division[p]=s->v3.part[p].division;for(i=0;i<BR_STEPS;i++){const uint8_t *v=i<BR_BANK_STEPS?s->v3.part[p].step[i]:s->v3.module[p].extra_step[i-BR_BANK_STEPS];uint32_t a=v[0],b=v[1];out->step[p][i]=p==BR_DRUM_PART?a|(b<<8):a?(BR_NOTE_STEP(a-1u,b&127u)|(b&128u?BR_STEP_TIE:0u)):0u;}}
 for(p=0;p<BR_SYNTH_PARTS;p++)for(i=0;i<BR_STEPS;i++){out->synth_length[p][i]=s->synth_length[p][i]&0x7ffu;out->synth_micro[p][i]=br_session_micro(s->synth_length[p][i]);out->synth_condition[p][i]=s->synth_condition[p][i];out->synth_note[p][i]=s->synth_note[p][i];out->synth_velocity[p][i]=s->synth_velocity[p][i];}
 for(i=0;i<BR_STEPS;i++)for(l=0;l<BR_DRUM_LANES;l++){out->kit_length[i][l]=s->kit_length[i][l]&0x7ffu;out->kit_micro[i][l]=br_session_micro(s->kit_length[i][l]);out->kit_condition[i][l]=s->kit_condition[i][l];out->kit_velocity[i][l]=s->kit_velocity[i][l];out->kit_note[i][l]=255u;out->kit_pitch[i][l]=s->kit_pitch[i][l];out->kit_retrig[i][l]=s->kit_retrig[i][l]&63u;}
 return 1;
}
int br_pattern_session_capture(uint8_t *out,size_t n)
{ if(!br_pattern_session_scratch||!out||n!=sizeof(br_session_t)||br_pattern_session_overlap(out,n,br_pattern_session_scratch,sizeof(br_session_t))||!br_capture(br_pattern_session_scratch))return 0;memcpy(out,br_pattern_session_scratch,sizeof(br_session_t));return 1; }
int br_pattern_session_validate(const uint8_t *in,size_t n,void *context)
{ (void)context;if(!br_pattern_session_scratch||!in||n!=sizeof(br_session_t)||br_pattern_session_overlap(in,n,br_pattern_session_scratch,sizeof(br_session_t)))return 0;memcpy(br_pattern_session_scratch,in,sizeof(br_session_t));return br_session_v16_valid(br_pattern_session_scratch); }
int br_pattern_session_restore(const uint8_t *in,size_t n)
{ if(!br_pattern_session_scratch||!in||n!=sizeof(br_session_t)||br_pattern_session_overlap(in,n,br_pattern_session_scratch,sizeof(br_session_t)))return 0;memcpy(br_pattern_session_scratch,in,sizeof(br_session_t));return br_restore(br_pattern_session_scratch); }
#endif
#if defined(BR_PATTERN_SESSION_LOAN_EXPERIMENT) && BR_PATTERN_SESSION_LOAN_EXPERIMENT
#include "pattern_session_loan.h"
int br_pattern_session_loan_acquire(unsigned mask,int (*exclusive)(void *),void *context,void **loaded,void **pending,void **undo,size_t *bytes)
{
 if(!mask||(mask&~7u)||!exclusive||!exclusive(context)||!loaded||!pending||!undo||!bytes||br_pattern_loan_mask||br_save_pending||br_pattern_session_scratch_blocked()||((mask&FM1_SESSION_LOAN_UNDO)&&br_undo_valid))return 0;
 br_pattern_loan_mask=mask;*loaded=(mask&FM1_SESSION_LOAN_LOADED)?&br_loaded:0;*pending=(mask&FM1_SESSION_LOAN_PENDING)?&br_pending_save:0;*undo=(mask&FM1_SESSION_LOAN_UNDO)?&br_undo:0;*bytes=sizeof(br_session_t);return 1;
}
void br_pattern_session_loan_release(void){
#if defined(BR_PATTERN_BANK_EXPERIMENT) && BR_PATTERN_BANK_EXPERIMENT && (!defined(FM1_DEVICE) || (defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT))
 if(br_pattern_session_scratch&&((br_pattern_loan_mask&1u&&br_pattern_session_scratch==&br_loaded)||(br_pattern_loan_mask&2u&&br_pattern_session_scratch==&br_pending_save)||(br_pattern_loan_mask&4u&&br_pattern_session_scratch==&br_undo)))br_pattern_session_scratch=0;
#endif
 br_pattern_loan_mask=0;
}
#endif
