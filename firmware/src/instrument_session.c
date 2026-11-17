/* SPDX-License-Identifier: GPL-3.0-only */
/* Session format v18 (docs/SESSION-V18.md): the field table in
 * instrument_session_v17.h drives capture, restore and validation. Each table
 * row is bound to its runtime home below (br_v17_bind: base object, width,
 * offset); capture and restore walk the table and the bindings together, so
 * no intermediate struct is built. Older records (v16 and before, magic
 * 0x42524431) are rejected, never misread; tools/session_v16_to_v17 converts
 * them on a host. */
#include <stddef.h>
#define BR_V17_NAMES 0
#define BR_V17_RUNTIME 1
#include "instrument_session_v17.h"

/* A saved session: v17 or v18 wire bytes, both 3584 bytes. */
typedef struct { uint8_t b[BR_V17_TOTAL_LEN]; } br_session_t;
_Static_assert(sizeof(br_session_t) == 3584u, "v17 session length");
_Static_assert(sizeof(br_session_t) <= 4084u, "session fits settings sector");

extern volatile uint8_t br_midi_channels[7];
extern volatile uint8_t br_midi_clock_enabled;
static void br_monitor_set(uint32_t on);
static uint32_t br_restore_preset(uint32_t part, uint32_t preset);
static uint32_t br_monitor_on(void);
#if defined(__ELF__) && !defined(DINGHY_HOST) && !defined(DINGHY_WEB)
static br_session_t br_pending_save __attribute__((section(".pool"), aligned(4)));
static br_session_t br_undo __attribute__((section(".pool"), aligned(4)));
static br_session_t br_loaded __attribute__((section(".pool"), aligned(4)));
#else
static br_session_t br_pending_save __attribute__((aligned(4))), br_undo __attribute__((aligned(4))),
    br_loaded __attribute__((aligned(4)));
#endif
static uint8_t br_save_pending, br_undo_valid;
/* br_legacy_migration_pending stays 0: no legacy record is ever adopted (kept
 * for the SAVE-protection status bits). br_invalid_session_pending: the settings
 * sector holds bytes that are not a v17 session; ordinary SAVE must not erase
 * them. */
static uint8_t br_legacy_migration_pending, br_invalid_session_pending;

static uint32_t br_session_legacy_migration_pending(void)
{ return br_legacy_migration_pending != 0u; }

static uint32_t br_session_save_blocked(void)
{ return br_legacy_migration_pending != 0u || br_invalid_session_pending != 0u; }

/* Project scratch is independent of protected settings flash. */
static uint32_t br_pattern_session_scratch_blocked(void)
{
#if defined(FM1_PATTERN_DEVICE_CANDIDATE) && FM1_PATTERN_DEVICE_CANDIDATE && defined(FM1_PATTERN_V16_ONLY) && FM1_PATTERN_V16_ONLY
    return br_legacy_migration_pending != 0u;
#else
    return br_session_save_blocked();
#endif
}

/* ---- Row bindings: one per table row, in table order. ---- */
enum { BR_VB_NONE, BR_VB_PART, BR_VB_PEND, BR_VB_SEQDEF, BR_VB_RECQ, BR_VB_MASTER, BR_VB_KIT,
       BR_VB_PUNCH, BR_VB_MIDI, BR_VB_BPM, BR_VB_SWING, BR_VB_UIPART, BR_VB_OCTAVE, BR_VB_ARPON,
       BR_VB_CLOCK, BR_VB_LEVELER, BR_VB_FADER_LINK, BR_VB_SPECIAL };
enum { BR_VW_U8, BR_VW_S8, BR_VW_U16, BR_VW_WORD };        /* width of the runtime home */
enum { BR_VS_POLY, BR_VS_MONITOR, BR_VS_REVERB_TYPE, BR_VS_PRESET, BR_VS_VOICE, BR_VS_LANE_MUTE };
typedef struct { uint8_t base, type; uint16_t off; } br_v17_bind_t; /* type: width | element stride << 2 */
#define BR_VBIND(b, w, o, stride) { (uint8_t)(b), (uint8_t)((w) | (stride) << 2), (uint16_t)(o) }
#define BR_VPART(m) BR_VBIND(BR_VB_PART, BR_VW_WORD, offsetof(br_part_t, m), 0)
#define BR_VMASTER(m) BR_VBIND(BR_VB_MASTER, BR_VW_WORD, offsetof(br_master_t, m), 0)
#define BR_VKIT(m, stride) BR_VBIND(BR_VB_KIT, BR_VW_WORD, offsetof(br_part_t, m), stride)
#define BR_VPEND(m) BR_VBIND(BR_VB_PEND, BR_VW_U8, offsetof(br_part_pending_t, m), 0)
#define BR_VPUNCH(m) BR_VBIND(BR_VB_PUNCH, BR_VW_U8, offsetof(br_punch_cfg_t, m), 0)
#define BR_VSPECIAL(id) BR_VBIND(BR_VB_SPECIAL, 0, id, 0)
#define BR_VNONE BR_VBIND(BR_VB_NONE, 0, 0, 0)
typedef __typeof__(br_ui.seq_default[0][0]) br_seq_default_t;
#define BR_VSEQDEF(lane, m, w) BR_VBIND(BR_VB_SEQDEF, w, \
    (BR_DRUM_PART * ((lane) ? 1u : 0u)) * sizeof br_ui.seq_default[0] + (lane) * sizeof(br_seq_default_t) + \
    offsetof(br_seq_default_t, m), (lane) ? sizeof(br_seq_default_t) : 0u)
static const br_v17_bind_t br_v17_binds[BR_V17_ROW_COUNT] = {
    /* GLOBAL */
    BR_VBIND(BR_VB_BPM, BR_VW_WORD, 0, 0), BR_VBIND(BR_VB_SWING, BR_VW_WORD, 0, 0),
    BR_VBIND(BR_VB_UIPART, BR_VW_WORD, 0, 0), BR_VBIND(BR_VB_OCTAVE, BR_VW_S8, 0, 0),
    BR_VBIND(BR_VB_ARPON, BR_VW_WORD, 0, 0), BR_VSPECIAL(BR_VS_POLY), BR_VSPECIAL(BR_VS_MONITOR),
    BR_VBIND(BR_VB_CLOCK, BR_VW_U8, 0, 0),
    BR_VBIND(BR_VB_MIDI, BR_VW_U8, 0, 0), BR_VBIND(BR_VB_MIDI, BR_VW_U8, 1, 0), BR_VBIND(BR_VB_MIDI, BR_VW_U8, 2, 0),
    BR_VBIND(BR_VB_MIDI, BR_VW_U8, 3, 0), BR_VBIND(BR_VB_MIDI, BR_VW_U8, 4, 0), BR_VBIND(BR_VB_MIDI, BR_VW_U8, 5, 0),
    BR_VBIND(BR_VB_MIDI, BR_VW_U8, 6, 0), BR_VBIND(BR_VB_FADER_LINK, BR_VW_U8, 0, 0),
    BR_VMASTER(global_lfo.wave), BR_VMASTER(global_lfo.rate),
    BR_VMASTER(global_lfo.depth), BR_VMASTER(global_lfo.target),
    /* MASTER */
    BR_VMASTER(fx.filter), BR_VMASTER(fx.resonance), BR_VMASTER(fx.env_depth), BR_VMASTER(fx.smooth),
    BR_VMASTER(fx.heat_type), BR_VMASTER(fx.heat_amount),
    BR_VMASTER(comp.source), BR_VMASTER(comp.threshold), BR_VMASTER(comp.ratio), BR_VMASTER(comp.makeup),
    BR_VMASTER(comp.attack), BR_VMASTER(comp.release), BR_VMASTER(comp.hold), BR_VMASTER(comp.mix),
    BR_VBIND(BR_VB_LEVELER, BR_VW_U8, 0, 0), BR_VSPECIAL(BR_VS_REVERB_TYPE),
    BR_VMASTER(reverb_size), BR_VMASTER(reverb_decay),
    BR_VBIND(BR_VB_MASTER, BR_VW_U8, offsetof(br_master_t, reverb_extra) + 0u, 0),
    BR_VBIND(BR_VB_MASTER, BR_VW_U8, offsetof(br_master_t, reverb_extra) + 1u, 0),
    BR_VBIND(BR_VB_MASTER, BR_VW_U8, offsetof(br_master_t, reverb_extra) + 2u, 0),
    BR_VBIND(BR_VB_MASTER, BR_VW_U8, offsetof(br_master_t, reverb_extra) + 3u, 0),
    BR_VBIND(BR_VB_MASTER, BR_VW_U8, offsetof(br_master_t, reverb_extra) + 4u, 0),
    BR_VBIND(BR_VB_MASTER, BR_VW_U8, offsetof(br_master_t, reverb_manual), 0),
    BR_VBIND(BR_VB_MASTER, BR_VW_WORD, offsetof(br_master_t, global_route[0].source), sizeof(br_route_config_t)),
    BR_VBIND(BR_VB_MASTER, BR_VW_WORD, offsetof(br_master_t, global_route[0].target), sizeof(br_route_config_t)),
    BR_VBIND(BR_VB_MASTER, BR_VW_WORD, offsetof(br_master_t, global_route[0].amount), sizeof(br_route_config_t)),
    BR_VBIND(BR_VB_MASTER, BR_VW_WORD, offsetof(br_master_t, global_route[0].enabled), sizeof(br_route_config_t)),
    /* TRACKS (one block per track) */
    BR_VSPECIAL(BR_VS_PRESET), BR_VPART(sound.timbre), BR_VPART(sound.color), BR_VPART(sound.plaits_harmonics),
    BR_VPART(sound.plaits_aux), BR_VPART(sound.tune), BR_VPART(sound.level), BR_VPART(sound.pan),
    BR_VPART(sound.mute), BR_VPART(sound.attack), BR_VPART(sound.decay), BR_VPART(sound.sustain),
    BR_VPART(sound.release), BR_VPART(sound.filter), BR_VPART(sound.filter_resonance),
    BR_VPART(sound.filter_keytrack), BR_VPART(sound.filter_env_depth), BR_VPART(sound.timbre_mod),
    BR_VPART(sound.fm_mod), BR_VPART(sound.morph_mod),
    BR_VPART(mod.attack), BR_VPART(mod.decay), BR_VPART(mod.sustain), BR_VPART(mod.release),
    BR_VPART(mod.amount), BR_VPART(mod.target), BR_VPART(mod.loop), BR_VPART(mod.retrigger),
    BR_VPART(mod.lfo[0].wave), BR_VPART(mod.lfo[0].rate), BR_VPART(mod.lfo[0].depth), BR_VPART(mod.lfo[0].target),
    BR_VPEND(lfo_sync[0]), BR_VPEND(lfo_division[0]), BR_VPEND(lfo_key_reset[0]), BR_VPEND(lfo_fade_in[0]),
    BR_VPART(mod.lfo[1].wave), BR_VPART(mod.lfo[1].rate), BR_VPART(mod.lfo[1].depth), BR_VPART(mod.lfo[1].target),
    BR_VPEND(lfo_sync[1]), BR_VPEND(lfo_division[1]), BR_VPEND(lfo_key_reset[1]), BR_VPEND(lfo_fade_in[1]),
    BR_VBIND(BR_VB_PART, BR_VW_WORD, offsetof(br_part_t, mod.route[0].source), sizeof(br_route_config_t)),
    BR_VBIND(BR_VB_PART, BR_VW_WORD, offsetof(br_part_t, mod.route[0].target), sizeof(br_route_config_t)),
    BR_VBIND(BR_VB_PART, BR_VW_WORD, offsetof(br_part_t, mod.route[0].amount), sizeof(br_route_config_t)),
    BR_VBIND(BR_VB_PART, BR_VW_WORD, offsetof(br_part_t, mod.route[0].enabled), sizeof(br_route_config_t)),
    BR_VPART(sound.drive), BR_VPART(fx.drive_mode), BR_VPART(fx.drive_gain), BR_VPART(fx.drive_tone),
    BR_VPART(sound.delay), BR_VPART(fx.delay_engine), BR_VPART(fx.delay_time), BR_VPART(fx.delay_feedback),
    BR_VPEND(delay_width), BR_VPEND(delay_tone), BR_VPEND(delay_wow_rate),
    BR_VPART(sound.reverb), BR_VPART(fx.mod_mix), BR_VPART(fx.mod_engine), BR_VPART(fx.mod_rate),
    BR_VPART(fx.mod_depth), BR_VPEND(mod_feedback),
    BR_VPART(length), BR_VPART(division), BR_VBIND(BR_VB_RECQ, BR_VW_U8, 0, 0),
    BR_VPART(arp_mode), BR_VPART(arp_rate), BR_VPART(arp_octaves), BR_VPART(arp_gate),
    BR_VPART(arp_latch), BR_VPART(arp_swing), BR_VPART(arp_repeat),
    BR_VSEQDEF(0, note, BR_VW_U8), BR_VSEQDEF(0, length, BR_VW_U16), BR_VSEQDEF(0, velocity, BR_VW_U8),
    BR_VSEQDEF(0, condition, BR_VW_U8), BR_VPART(sound.voice_mode), BR_VPART(sound.harmonics_mod), BR_VPEND(delay_short), BR_VPEND(drive_bias),
    /* KIT (lanes 0..3 of the kit track) */
    BR_VSPECIAL(BR_VS_VOICE), BR_VKIT(lane_level[0], sizeof(uint32_t)), BR_VSPECIAL(BR_VS_LANE_MUTE),
    BR_VKIT(sound.gate, 0), BR_VKIT(sound.drum_snap, 0),
    BR_VPUNCH(loop_len), BR_VPUNCH(echo_time), BR_VPUNCH(filter_shape), BR_VPUNCH(trem_rate),
    BR_VPUNCH(echo_fb), BR_VPUNCH(filter_amount), BR_VPUNCH(trem_depth),
    BR_VSEQDEF(1, note, BR_VW_U8), BR_VSEQDEF(1, length, BR_VW_U16), BR_VSEQDEF(1, velocity, BR_VW_U8),
    BR_VSEQDEF(1, condition, BR_VW_U8), BR_VNONE,
};
_Static_assert(sizeof br_v17_binds / sizeof br_v17_binds[0] == BR_V17_ROW_COUNT, "one binding per table row");

/* The TEMPO screen's POLY switch exists where instrument_ui.c's BR_POLY_UI is 1. */
#if defined(BR_POLY_SCHEDULER_EXPERIMENT) && BR_POLY_SCHEDULER_EXPERIMENT && \
    ((defined(FM1_DEVICE) && defined(FM1_PATTERN_TARGET_EXPERIMENT) && FM1_PATTERN_TARGET_EXPERIMENT) || \
     (defined(DINGHY_HOST) && defined(BR_SPLIT_AUDIO)))
#define BR_POLY_OFF_HOME br_poly_user_off
#else
static uint8_t br_poly_saved_off;   /* no polyphony in this build: kept so the setting survives */
#define BR_POLY_OFF_HOME br_poly_saved_off
#endif
/* SMK-37 Pro "fader link" (smk37_fader_logic.h): 1 = fader 1 sets T1 and T2, fader 2 is
 * ignored in the track-level bank. Saved in GLOBAL; the FM-1 saves and restores it and never reads it. */
static uint8_t br_smk_fader_link;
static const uint8_t br_reverb_type_id[3] = {BR_REVERB_ROOM, BR_REVERB_HALL, BR_REVERB_SPACE};

static volatile uint8_t *br_v17_home(const br_v17_bind_t *b, uint32_t rep, uint32_t k)
{
    volatile uint8_t *p;
    uint32_t stride = 0u;
    switch (b->base) {
    case BR_VB_PART: p = (volatile uint8_t *)&br_part[0]; stride = sizeof(br_part_t); break;
    case BR_VB_PEND: p = (volatile uint8_t *)&br_part_pending[0]; stride = sizeof(br_part_pending_t); break;
    case BR_VB_SEQDEF: p = (volatile uint8_t *)&br_ui.seq_default[0][0]; stride = sizeof br_ui.seq_default[0]; break;
    case BR_VB_RECQ: p = (volatile uint8_t *)&br_seq_record_quantize[0]; stride = 1u; break;
    case BR_VB_MASTER: p = (volatile uint8_t *)&br_master; break;
    case BR_VB_KIT: p = (volatile uint8_t *)&br_part[BR_DRUM_PART]; break;
    case BR_VB_PUNCH: p = (volatile uint8_t *)&br_punch_cfg; break;
    case BR_VB_MIDI: p = br_midi_channels; break;
    case BR_VB_BPM: p = (volatile uint8_t *)&br_bpm; break;
    case BR_VB_SWING: p = (volatile uint8_t *)&br_swing; break;
    case BR_VB_UIPART: p = (volatile uint8_t *)&br_ui.part; break;
    case BR_VB_OCTAVE: p = (volatile uint8_t *)&br_ui.octave; break;
    case BR_VB_ARPON: p = (volatile uint8_t *)&br_arp_on; break;
    case BR_VB_CLOCK: p = &br_midi_clock_enabled; break;
    case BR_VB_FADER_LINK: p = &br_smk_fader_link; break;
    default: p = &br_leveler_on; break;                    /* BR_VB_LEVELER */
    }
    return p + rep * stride + b->off + k * (uint32_t)(b->type >> 2);
}

/* The runtime value of row `row`, element k, track rep. */
static int32_t br_v17_get(uint32_t row, uint32_t rep, uint32_t k)
{
    const br_v17_bind_t *b = &br_v17_binds[row];
    volatile uint8_t *p;
    if (b->base == BR_VB_SPECIAL) {
        switch (b->off) {
        case BR_VS_POLY: return !BR_POLY_OFF_HOME;
        case BR_VS_MONITOR: return (int32_t)br_monitor_on();
        case BR_VS_REVERB_TYPE:
            return br_master.reverb_engine == BR_REVERB_HALL ? 1 :
                br_master.reverb_engine == BR_REVERB_SPACE || br_master.reverb_engine == BR_REVERB_ETHEREAL ? 2 : 0;
        case BR_VS_PRESET: return (int32_t)br_part[rep].sound.preset;
        case BR_VS_VOICE: return (int32_t)br_drum_lane_choice(br_part[BR_DRUM_PART].sound.drum_voice_mask, k);
        default: return (int32_t)((br_kit_mute_mask >> k) & 1u);  /* BR_VS_LANE_MUTE */
        }
    }
    p = br_v17_home(b, rep, k);
    switch (b->type & 3u) {
    case BR_VW_U8: return *p;
    case BR_VW_S8: return *(volatile int8_t *)&p[0];
    case BR_VW_U16: return *(volatile uint16_t *)&p[0];
    default: return *(volatile int32_t *)&p[0];
    }
}

static void br_v17_set(uint32_t row, uint32_t rep, uint32_t k, int32_t v)
{
    const br_v17_bind_t *b = &br_v17_binds[row];
    volatile uint8_t *p;
    if (b->base == BR_VB_SPECIAL) {
        switch (b->off) {
        case BR_VS_POLY: BR_POLY_OFF_HOME = (uint8_t)!v; break;
        case BR_VS_MONITOR: br_monitor_set((uint32_t)v); break;
        case BR_VS_REVERB_TYPE: br_master.reverb_engine = br_reverb_type_id[v]; break;
        case BR_VS_PRESET: {
            /* The track takes the preset's whole sound first; every saved field
             * that follows overwrites its part of it. */
            br_sound_t sound;
            br_preset_sound(br_restore_preset(rep, (uint32_t)v), &sound);
            br_part[rep].sound = sound;
            break;
        }
        case BR_VS_VOICE:
            br_part[BR_DRUM_PART].sound.drum_voice_mask =
                br_drum_set_lane_choice(br_part[BR_DRUM_PART].sound.drum_voice_mask, k, (uint32_t)v);
            break;
        default:
            br_kit_mute_mask = (br_kit_mute_mask & ~(1u << k)) | ((uint32_t)v << k);
            break;
        }
        return;
    }
    p = br_v17_home(b, rep, k);
    switch (b->type & 3u) {
    case BR_VW_U8: case BR_VW_S8: *p = (uint8_t)v; break;
    case BR_VW_U16: *(volatile uint16_t *)&p[0] = (uint16_t)v; break;
    default: *(volatile int32_t *)&p[0] = v; break;
    }
}

/* Persisted records keep the complete 77-ID catalog across target builds.
 * Without Plaits DSP, map its melodic models to CSAW and its kit to BRAIDS KIT. */
static uint32_t br_restore_preset(uint32_t part, uint32_t preset)
{
    if (part == BR_DRUM_PART && (preset == BR_KIT_808 || preset == BR_KIT_909))
        return BR_ENABLE_PLAITS ? BR_KIT_PLAITS : BR_KIT_BRAIDS;
#if !BR_ENABLE_PLAITS
    if (preset >= BR_FIRST_PLAITS && preset < BR_FIRST_PLAITS + BR_PLAITS_MODEL_COUNT) return 0u;
    if (preset == BR_KIT_PLAITS) return BR_KIT_BRAIDS;
#endif
    return preset;
}

/* The on-disk catalog decides, not the models compiled into this image. */
static int br_session_preset_role_valid(uint32_t part, uint32_t preset)
{
    if (part < BR_SYNTH_PARTS) return br_is_plaits(preset);
    return (preset >= BR_KIT_BRAIDS && preset <= BR_KIT_909) || preset == BR_KIT_PLAITS;
}

/* ---- Structure ---- */
typedef struct { const uint8_t *p[BR_V17_SEC_PATTERN + 1u]; uint16_t len[BR_V17_SEC_PATTERN + 1u]; } br_v17_found_t;

static uint32_t br_v17_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

/* Locate the known sections of a v17 record without changing anything.
 * Structural errors reject the record (BR_V17_ERR_*); the codec rules apply:
 * unknown sections and trailing bytes are skipped. */
static int br_v17_scan(const uint8_t *in, uint32_t len, br_v17_found_t *f)
{
    uint32_t header_len, count, total, at, i;
    memset(f, 0, sizeof *f);
    if (!in || len < BR_V17_HEADER_LEN) return BR_V17_ERR_SHORT;
    if ((br_v17_rd16(in) | br_v17_rd16(in + 2) << 16) != BR_V17_MAGIC) return BR_V17_ERR_MAGIC;
    if (br_v17_rd16(in + 4) != BR_V17_VERSION && br_v17_rd16(in + 4) != BR_V19_VERSION && br_v17_rd16(in + 4) != BR_V18_VERSION && br_v17_rd16(in + 4) != BR_V17_LEGACY_VERSION) return BR_V17_ERR_VERSION;
    header_len = in[6]; count = in[7]; total = br_v17_rd16(in + 8);
    if (header_len < BR_V17_HEADER_LEN) return BR_V17_ERR_HEADER;
    if (total > len || total < header_len) return BR_V17_ERR_TOTAL;
    for (at = header_len, i = 0u; i < count; ++i) {
        uint32_t id, slen;
        if (total - at < BR_V17_SECTION_HEADER_LEN) return BR_V17_ERR_SECTION;
        id = in[at]; slen = br_v17_rd16(in + at + 2);
        at += BR_V17_SECTION_HEADER_LEN;
        if (total - at < slen) return BR_V17_ERR_SECTION;
        if (id >= BR_V17_SEC_GLOBAL && id <= BR_V17_SEC_PATTERN) {
            if (f->p[id]) return BR_V17_ERR_DUPLICATE;
            if (id == BR_V17_SEC_TRACKS && slen % BR_V17_TRACKS) return BR_V17_ERR_TRACKS;
            f->p[id] = in + at; f->len[id] = (uint16_t)slen;
        }
        at += slen;
    }
    return BR_V17_OK;
}

/* Byte offset of a row's first element inside one block, and its wire size. */
static uint32_t br_v17_row_at(uint32_t sec, uint32_t row, uint32_t *size)
{
    uint32_t i, at = 0u;
    for (i = br_v17_sections[sec].first_row; i < row; ++i)
        at += br_v17_rows[i].count * br_v17_range_size(&br_v17_ranges[br_v17_rows[i].range]);
    *size = br_v17_range_size(&br_v17_ranges[br_v17_rows[row].range]);
    return at;
}
#define BR_V17_TRACK_ROW(m) (BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT + (m))
/* Table row numbers by name, e.g. BR_V17_ROW(T, seq_length), BR_V17_ROW(M, filter). */
#define BR_V17_ROW_G(n) (BR_V17_GROW_##n)
#define BR_V17_ROW_M(n) (BR_V17_GLOBAL_ROW_COUNT + BR_V17_MROW_##n)
#define BR_V17_ROW_T(n) BR_V17_TRACK_ROW(BR_V17_TROW_##n)
#define BR_V17_ROW_K(n) (BR_V17_GLOBAL_ROW_COUNT + BR_V17_MASTER_ROW_COUNT + BR_V17_TRACK_ROW_COUNT + BR_V17_KROW_##n)
#define BR_V17_ROW(sec, n) BR_V17_ROW_##sec(n)
/* Where a field sits in a record this firmware writes (tests poke records). */
static uint32_t br_v17_wire_at(uint32_t row, uint32_t rep, uint32_t k)
{
    uint32_t sec = 0u, at = BR_V17_HEADER_LEN + BR_V17_SECTION_HEADER_LEN, size;
    while (sec + 1u < BR_V17_FIELD_SECTIONS && row >= br_v17_sections[sec + 1u].first_row) {
        at += (uint32_t)br_v17_sections[sec].len * br_v17_sections[sec].repeat + BR_V17_SECTION_HEADER_LEN;
        ++sec;
    }
    at += rep * br_v17_sections[sec].len + br_v17_row_at(sec, row, &size);
    return at + k * size;
}

/* A record is valid when its structure is and each track's preset (when the
 * record holds it) suits the track's role. Field values are never a reason to
 * reject: out-of-range values are clamped on restore. */
static int br_session_bytes_valid(const uint8_t *in, uint32_t len)
{
    br_v17_found_t f;
    uint32_t t, stride;
    if (br_v17_scan(in, len, &f) != BR_V17_OK) return 0;
    stride = f.len[BR_V17_SEC_TRACKS] / BR_V17_TRACKS;
    for (t = 0u; f.p[BR_V17_SEC_TRACKS] && stride && t < BR_V17_TRACKS; ++t)
        if (!br_session_preset_role_valid(t, f.p[BR_V17_SEC_TRACKS][t * stride])) return 0;
    return 1;
}
static int br_session_valid(const br_session_t *s)
{ return s && br_session_bytes_valid(s->b, sizeof s->b); }

/* ---- Capture ---- */
static uint32_t br_v17_clamps;    /* values clamped by the last capture or restore */

static void br_v17_put(uint8_t *at, uint32_t v, uint32_t n) { at[0] = (uint8_t)v; if (n == 2u) at[1] = (uint8_t)(v >> 8); }

static uint8_t *br_v17_section_header(uint8_t *at, uint32_t id, uint32_t len)
{
    at[0] = (uint8_t)id; at[1] = BR_V17_SECTION_VERSION; br_v17_put(at + 2, len, 2u);
    return at + BR_V17_SECTION_HEADER_LEN;
}

static uint32_t br_session_timed_length(uint16_t length, int8_t offset)
{ return (uint32_t)(length & 0x7ffu) | (((uint32_t)(uint8_t)offset & 31u) << 11); }
static int8_t br_session_micro(uint32_t packed)
{ uint32_t bits = (packed >> 11) & 31u; return (int8_t)(bits >= 16u ? (int32_t)bits - 32 : (int32_t)bits); }

/* The live pattern in the PATTERN section's wire order. */
static void br_v17_capture_pattern(uint8_t *o)
{
    uint32_t p, i, l;
    for (p = 0u; p < BR_SYNTH_PARTS; ++p)
        for (i = 0u; i < BR_STEPS; ++i, o += 2) {
            uint32_t step = br_part[p].step[i];
            o[0] = step & BR_STEP_ON ? (uint8_t)(BR_STEP_NOTE(step) + 1u) : 0u;
            o[1] = (uint8_t)(BR_STEP_VEL(step) | (step & BR_STEP_TIE ? 128u : 0u));
        }
    for (i = 0u; i < BR_STEPS; ++i, o += 2) {
        uint32_t step = br_part[BR_DRUM_PART].step[i];
        o[0] = (uint8_t)BR_STEP_HIT(step); o[1] = (uint8_t)BR_STEP_ACC(step);
    }
    for (p = 0u; p < BR_SYNTH_PARTS; ++p)
        for (i = 0u; i < BR_STEPS; ++i, o += 2)
            br_v17_put(o, br_session_timed_length(br_seq_synth_length[p][i], br_seq_synth_micro[p][i]), 2u);
    memcpy(o, (const void *)br_seq_synth_condition, 192u); o += 192;
    memcpy(o, (const void *)br_seq_synth_note, 192u); o += 192;
    memcpy(o, (const void *)br_seq_synth_velocity, 192u); o += 192;
    for (i = 0u; i < BR_STEPS; ++i)
        for (l = 0u; l < BR_DRUM_LANES; ++l, o += 2)
            br_v17_put(o, br_session_timed_length(br_seq_kit_length[i][l], br_seq_kit_micro[i][l]), 2u);
    memcpy(o, (const void *)br_seq_kit_condition, 256u); o += 256;
    memcpy(o, (const void *)br_seq_kit_velocity, 256u); o += 256;
    memcpy(o, (const void *)br_seq_kit_pitch, 256u); o += 256;
    memcpy(o, (const void *)br_seq_kit_retrig, 256u);
}
_Static_assert(sizeof br_seq_synth_condition == 192u && sizeof br_seq_kit_condition == 256u &&
               sizeof br_seq_kit_retrig == 256u, "pattern arrays match the PATTERN section");

/* Bring the PATTERN section into range (the same rules as the codec). */
static void br_v17_pattern_bytes_fix(uint8_t *p)
{
    uint32_t b, at = 0u;
    for (b = 0u; b < BR_V17_PATTERN_BLOCK_COUNT; ++b) {
        const br_v17_pattern_block_t *k = &br_v17_pattern_blocks[b];
        uint32_t end = at + k->bytes;
        for (; at < end; at += k->elem) {
            uint32_t v = k->elem == 2u ? br_v17_rd16(p + at) : p[at], w = br_v17_pattern_fix(k->rule, v);
            if (w != v) { ++br_v17_clamps; br_v17_put(p + at, w, k->elem); }
        }
    }
}

/* Write the running state as a v17 record. Out-of-range runtime values are
 * clamped (and counted in br_v17_clamps); it always succeeds. */
static int br_capture(br_session_t *s)
{
    uint8_t *o;
    uint32_t sec, rep, i, k;
    if (!s) return 0;
    br_v17_clamps = 0u;
    o = s->b;
    br_v17_put(o, BR_V17_MAGIC & 0xffffu, 2u); br_v17_put(o + 2, BR_V17_MAGIC >> 16, 2u);
    br_v17_put(o + 4, BR_V17_VERSION, 2u); o[6] = BR_V17_HEADER_LEN; o[7] = 5u;
    br_v17_put(o + 8, BR_V17_TOTAL_LEN, 2u); br_v17_put(o + 10, 0u, 2u);
    o += BR_V17_HEADER_LEN;
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        o = br_v17_section_header(o, d->id, (uint32_t)d->len * d->repeat);
        for (rep = 0u; rep < d->repeat; ++rep)
            for (i = d->first_row; i < (uint32_t)d->first_row + d->rows; ++i) {
                const br_v17_range_t *r = &br_v17_ranges[br_v17_rows[i].range];
                uint32_t n = br_v17_range_size(r);
                for (k = 0u; k < br_v17_rows[i].count; ++k, o += n)
                    br_v17_put(o, (uint32_t)(br_v17_rows[i].range == BR_V17_R_SPARE ? 0 :
                        br_v17_clamp(br_v17_get(i, rep, k), r, &br_v17_clamps)), n);
            }
    }
    /* v18 stores the two panel patches per track in the unused upper bits of
     * the eight existing route-enable bytes. Each patch takes 19 bits. */
    for (uint32_t part = 0u; part < BR_PARTS; ++part) {
        uint32_t bit = 0u;
        for (uint32_t slot = 0u; slot < 2u; ++slot) {
            const volatile br_route_config_t *patch = &br_part[part].mod.route[BR_ROUTES + slot];
            uint32_t packed = (patch->source & 31u) | ((patch->target & 31u) << 5u) |
                (((uint32_t)patch->amount & 255u) << 10u) | ((patch->enabled & 1u) << 18u);
            for (uint32_t n = 0u; n < 19u; ++n, ++bit) {
                uint32_t pos = br_v17_wire_at(BR_V17_ROW(T, route_enabled), part, bit / 7u);
                s->b[pos] |= (uint8_t)(((packed >> n) & 1u) << (1u + bit % 7u));
            }
        }
    }
    for (uint32_t part = 0u; part < BR_SYNTH_PARTS; ++part) {
        uint32_t at = br_v17_wire_at(BR_V17_ROW(K, spare_kit), 0u, part * 2u);
        uint32_t amount = br_part[part].sound.portamento, mode = br_part[part].sound.glide_mode, amp = br_part[part].sound.amp_mode, gate = br_part[part].sound.lpg_gate;
        s->b[at] = (uint8_t)(amount > 100u ? 100u : amount);
        s->b[at + 1u] = (uint8_t)((mode > 2u ? 2u : mode) | ((amp ? 1u : 0u) << 2u) | ((gate > 31u ? 31u : gate) << 3u));
    }
    o = br_v17_section_header(o, BR_V17_SEC_PATTERN, BR_V17_PATTERN_BYTES);
    br_v17_capture_pattern(o);
    br_v17_pattern_bytes_fix(o);
    return 1;
}

/* ---- Restore ---- */
static uint32_t br_v17_pattern_element(const uint8_t *in, uint32_t len, uint32_t block_at, uint32_t b, uint32_t e)
{
    const br_v17_pattern_block_t *k = &br_v17_pattern_blocks[b];
    uint32_t at = block_at + e * k->elem, v, w;
    if (!in || at + k->elem > len) v = k->elem == 2u ? (uint32_t)k->def * 257u : k->def;
    else v = k->elem == 2u ? br_v17_rd16(in + at) : in[at];
    w = br_v17_pattern_fix(k->rule, v);
    if (w != v) ++br_v17_clamps;
    return w;
}

static void br_v17_restore_pattern(const uint8_t *in, uint32_t len)
{
    uint32_t p, i, l, b, at = 0u, off[BR_V17_PATTERN_BLOCK_COUNT];
    /* Element `e` of block `b`: from the record when it holds it, else the
     * block's default; always brought into range. */
#define BR_V17_PAT(bk, e) br_v17_pattern_element(in, len, off[bk], bk, e)
    for (b = 0u; b < BR_V17_PATTERN_BLOCK_COUNT; ++b) { off[b] = at; at += br_v17_pattern_blocks[b].bytes; }
    for (p = 0u; p < BR_SYNTH_PARTS; ++p)
        for (i = 0u; i < BR_STEPS; ++i) {
            uint32_t w = BR_V17_PAT(0u, p * BR_STEPS + i), a = w & 255u, v = w >> 8;
            br_part[p].step[i] = a ? BR_NOTE_STEP(a - 1u, v & 127u) | (v & 128u ? BR_STEP_TIE : 0u) : 0u;
            w = BR_V17_PAT(2u, p * BR_STEPS + i);
            br_seq_synth_length[p][i] = (uint16_t)(w & 0x7ffu); br_seq_synth_micro[p][i] = br_session_micro(w);
            br_seq_synth_condition[p][i] = (uint8_t)BR_V17_PAT(3u, p * BR_STEPS + i);
            br_seq_synth_note[p][i] = (uint8_t)BR_V17_PAT(4u, p * BR_STEPS + i);
            br_seq_synth_velocity[p][i] = (uint8_t)BR_V17_PAT(5u, p * BR_STEPS + i);
        }
    for (i = 0u; i < BR_STEPS; ++i) {
        uint32_t w = BR_V17_PAT(1u, i);
        br_part[BR_DRUM_PART].step[i] = (w & 255u) | (w >> 8) << 8;
        for (l = 0u; l < BR_DRUM_LANES; ++l) {
            uint32_t e = i * BR_DRUM_LANES + l;
            w = BR_V17_PAT(6u, e);
            br_seq_kit_length[i][l] = (uint16_t)(w & 0x7ffu); br_seq_kit_micro[i][l] = br_session_micro(w);
            br_seq_kit_condition[i][l] = (uint8_t)BR_V17_PAT(7u, e);
            br_seq_kit_velocity[i][l] = (uint8_t)BR_V17_PAT(8u, e);
            br_seq_kit_pitch[i][l] = (uint8_t)BR_V17_PAT(9u, e);
            br_seq_kit_retrig[i][l] = (uint8_t)BR_V17_PAT(10u, e);
            br_seq_kit_note[i][l] = 255u;   /* retired per-step voice override */
        }
    }
#undef BR_V17_PAT
}

/* Restore a record. A record that fails validation changes nothing. Fields
 * the record does not hold take their table defaults; values out of range
 * are clamped (counted in br_v17_clamps). */
static int br_restore_bytes(const uint8_t *in, uint32_t len)
{
    br_v17_found_t f;
    uint32_t sec, rep, i, k, p, old_kit = 0u;
    if (!br_session_bytes_valid(in, len) || br_v17_scan(in, len, &f) != BR_V17_OK) return 0;
    if (f.p[BR_V17_SEC_TRACKS] && f.len[BR_V17_SEC_TRACKS] / BR_V17_TRACKS)
        old_kit = f.p[BR_V17_SEC_TRACKS][BR_DRUM_PART * (f.len[BR_V17_SEC_TRACKS] / BR_V17_TRACKS)];
    br_v17_clamps = 0u;
    br_midi_before_session_restore();
    br_solo_mask = 0u; br_kit_solo_mask = 0u;
    for (p = 0u; p < BR_PARTS; ++p) br_part[p].revision++;
    br_master.revision++;
    for (sec = 0u; sec < BR_V17_FIELD_SECTIONS; ++sec) {
        const br_v17_section_t *d = &br_v17_sections[sec];
        const uint8_t *src = f.p[d->id];
        uint32_t stride = src ? f.len[d->id] / d->repeat : 0u;
        for (rep = 0u; rep < d->repeat; ++rep) {
            uint32_t at = 0u, slot = 0u;
            for (i = d->first_row; i < (uint32_t)d->first_row + d->rows; ++i) {
                const br_v17_range_t *r = &br_v17_ranges[br_v17_rows[i].range];
                uint32_t n = br_v17_range_size(r);
                for (k = 0u; k < br_v17_rows[i].count; ++k, ++slot, at += n) {
                    int32_t v = br_v17_rows[i].def;
                    if (br_v17_rows[i].range == BR_V17_R_SPARE) continue;
                    if (sec == 2u && rep == BR_V17_KIT_TRACK) {
                        uint32_t o;
                        for (o = 0u; o < BR_V17_KIT_DEFAULT_COUNT; ++o)
                            if (br_v17_kit_defaults[o].slot == slot) v = br_v17_kit_defaults[o].value;
                    }
                    if (at + n <= stride) {
                        const uint8_t *q = src + rep * stride + at;
                        v = (int32_t)(n == 2u ? br_v17_rd16(q) : q[0]);
                        if (r->min < 0) v = n == 2u ? (int16_t)(uint16_t)v : (int8_t)(uint8_t)v;
                    }
                    if (br_v17_rd16(in + 4) >= BR_V18_VERSION && i == BR_V17_ROW(T, route_enabled)) v &= 1;
                    if (br_v17_rd16(in + 4) < BR_V19_VERSION &&
                        (i == BR_V17_ROW(T, timbre_mod) || i == BR_V17_ROW(T, pitch_mod) || i == BR_V17_ROW(T, morph_mod))) {
                        if (at + n > stride) v = 127;
                        v = (int32_t)br_v19_from_old_mod((uint32_t)br_v17_clamp(v, r, &br_v17_clamps));
                    } else if (br_v17_rd16(in + 4) < BR_V19_VERSION && i == BR_V17_ROW(T, harmonics_mod) && v) {
                        v = (int32_t)(1u + br_v19_from_old_mod((uint32_t)br_v17_clamp(v, r, &br_v17_clamps) - 1u));
                    }
                    br_v17_set(i, rep, k, br_v17_clamp(v, r, &br_v17_clamps));
                }
            }
        }
    }
    for (p = 0u; p < BR_SYNTH_PARTS; ++p) {
        uint32_t amount = 0u, mode = 0u, amp = 0u, gate = 0u, size;
        uint32_t at = br_v17_row_at(3u, BR_V17_ROW(K, spare_kit), &size) + p * 2u;
        if (br_v17_rd16(in + 4) >= BR_V19_VERSION && f.p[BR_V17_SEC_KIT] && at + 1u < f.len[BR_V17_SEC_KIT]) {
            amount = f.p[BR_V17_SEC_KIT][at];
            mode = f.p[BR_V17_SEC_KIT][at + 1u] & 3u;
            amp = (f.p[BR_V17_SEC_KIT][at + 1u] >> 2u) & 1u;
            if (br_v17_rd16(in + 4) >= BR_V17_VERSION) gate = f.p[BR_V17_SEC_KIT][at + 1u] >> 3u;
            else if (f.p[BR_V17_SEC_KIT][at + 1u] & 0xf8u) ++br_v17_clamps;
            if (amount > 100u || mode > 2u) ++br_v17_clamps;
        }
        br_part[p].sound.portamento = amount > 100u ? 100u : amount;
        br_part[p].sound.glide_mode = mode > 2u ? 2u : mode;
        br_part[p].sound.amp_mode = amp;
        br_part[p].sound.lpg_gate = gate;
    }
    for (p = 0u; p < BR_PARTS; ++p) {
        for (i = 0u; i < 2u; ++i) {
            volatile br_route_config_t *patch = &br_part[p].mod.route[BR_ROUTES + i];
            if (br_v17_rd16(in + 4) == BR_V17_LEGACY_VERSION) {
                patch->source = br_master.global_route[i].source;
                patch->target = br_master.global_route[i].target;
                patch->amount = br_master.global_route[i].amount;
                patch->enabled = br_master.global_route[i].enabled;
            } else {
                uint32_t packed = 0u, size, at = br_v17_row_at(2u, BR_V17_ROW(T, route_enabled), &size);
                uint32_t stride = f.len[BR_V17_SEC_TRACKS] / BR_V17_TRACKS;
                for (uint32_t n = 0u; n < 19u; ++n) {
                    uint32_t bit = i * 19u + n, byte = at + bit / 7u;
                    if (f.p[BR_V17_SEC_TRACKS] && byte < stride)
                        packed |= ((uint32_t)(f.p[BR_V17_SEC_TRACKS][p * stride + byte] >> (1u + bit % 7u)) & 1u) << n;
                }
                patch->source = br_v17_clamp((int32_t)(packed & 31u), &br_v17_ranges[BR_V17_R_SOURCE], &br_v17_clamps);
                patch->target = br_v17_clamp((int32_t)((packed >> 5u) & 31u), &br_v17_ranges[BR_V17_R_TARGET], &br_v17_clamps);
                patch->amount = br_v17_clamp((int8_t)((packed >> 10u) & 255u), &br_v17_ranges[BR_V17_R_S7], &br_v17_clamps);
                patch->enabled = (packed >> 18u) & 1u;
            }
            if (!br_mod_source_active(patch->source)) patch->source = BR_SOURCE_OFF;
        }
    }
    if (old_kit == BR_KIT_808 || old_kit == BR_KIT_909) {
#if BR_ENABLE_PLAITS
        br_kit_bank_apply(&br_kit_bank[old_kit == BR_KIT_808 ? 6u : 9u], &br_part[BR_DRUM_PART].sound);
        br_message(old_kit == BR_KIT_808 ? "80S KIT NOW SYNTH" : "90S KIT NOW SYNTH");
#else
        br_message("KIT NOW BRAIDS");
#endif
        br_ui.message_until += 2000u;
    }
    /* Retired route source (old LFO 3) never reaches the engine. */
    for (p = 0u; p < BR_PARTS; ++p)
        for (i = 0u; i < BR_ROUTES; ++i)
            if (!br_mod_source_active(br_part[p].mod.route[i].source)) br_part[p].mod.route[i].source = BR_SOURCE_OFF;
    br_v17_restore_pattern(f.p[BR_V17_SEC_PATTERN], f.len[BR_V17_SEC_PATTERN]);
    RING_PUBLISH();
    for (p = 0u; p < BR_PARTS; ++p) br_part[p].revision++;
    br_master.revision++;
    if (br_ui.cursor >= br_part[br_ui.part].length) br_ui.cursor = br_part[br_ui.part].length - 1u;
    br_ui.step_bank = br_ui.cursor / BR_BANK_STEPS;
    br_ui.redraw = 1;
    return 1;
}
static int br_restore(const br_session_t *s)
{ return s && br_restore_bytes(s->b, sizeof s->b); }

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
    br_legacy_migration_pending = 0u;
    br_invalid_session_pending = 0u;
#if !defined(FM1_PATTERN_V16_ONLY) || !FM1_PATTERN_V16_ONLY
    br_step_metadata_clear();
#endif
    memset(&br_loaded, 0, sizeof br_loaded);
    if (!settings_load(&br_loaded, sizeof br_loaded)) {
        /* Only a v17 record is read; anything else is kept and protected. */
        if (!br_restore(&br_loaded)) br_invalid_session_pending = 1u;
        return;
    }
    if (br_session_settings_header_nonempty()) br_invalid_session_pending = 1u;
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
/* Binding is host-only. Do not borrow the async save, loaded or undo
 * buffers: they may have independent lifetimes despite stopped transport. */
int br_pattern_session_bind(void *scratch,size_t n)
{ br_pattern_session_scratch=0;if(!scratch||n<sizeof(br_session_t)||((uintptr_t)scratch%_Alignof(br_session_t))||(br_pattern_session_overlap(scratch,n,&br_pending_save,sizeof br_pending_save)&&!br_pattern_exact_loan(scratch,n,2u,&br_pending_save))||(br_pattern_session_overlap(scratch,n,&br_loaded,sizeof br_loaded)&&!br_pattern_exact_loan(scratch,n,1u,&br_loaded))||(br_pattern_session_overlap(scratch,n,&br_undo,sizeof br_undo)&&!br_pattern_exact_loan(scratch,n,4u,&br_undo)))return 0;br_pattern_session_scratch=scratch;return 1; }
#include "pattern_snapshot.h"
/* A TRACKS field of track t from a scanned record (its default when absent). */
static uint32_t br_pattern_track_field(const br_v17_found_t *f,uint32_t t,uint32_t row)
{
 uint32_t size,at=br_v17_row_at(2u,row,&size),stride=f->len[BR_V17_SEC_TRACKS]/BR_V17_TRACKS;
 const br_v17_range_t *r=&br_v17_ranges[br_v17_rows[row].range];
 int32_t v=br_v17_rows[row].def;
 if(f->p[BR_V17_SEC_TRACKS]&&at+size<=stride)v=(int32_t)f->p[BR_V17_SEC_TRACKS][t*stride+at];
 return (uint32_t)br_v17_clamp(v,r,0);
}
/* Decode the v17 pattern fields without restoring the engine or touching MIDI,
 * transport, voices or control state. Scratch and output must be disjoint. */
int br_pattern_session_snapshot(const uint8_t *in,size_t n,br_pattern_snapshot *out)
{
 br_session_t *s=br_pattern_session_scratch;br_v17_found_t f;uint32_t p,i,l,b,at=0u,off[BR_V17_PATTERN_BLOCK_COUNT],saved=br_v17_clamps;const uint8_t *pat;uint32_t plen;
 if(!s||!in||!out||n!=sizeof *s||br_pattern_session_overlap(in,n,s,sizeof *s)||br_pattern_session_overlap(out,sizeof *out,s,sizeof *s)||br_pattern_session_overlap(in,n,out,sizeof *out))return 0;
 memcpy(s,in,sizeof *s);if(!br_session_valid(s)||br_v17_scan(s->b,sizeof s->b,&f)!=BR_V17_OK)return 0;memset(out,0,sizeof *out);
 pat=f.p[BR_V17_SEC_PATTERN];plen=f.len[BR_V17_SEC_PATTERN];
 for(b=0u;b<BR_V17_PATTERN_BLOCK_COUNT;++b){off[b]=at;at+=br_v17_pattern_blocks[b].bytes;}
#define BR_PS_PAT(bk,e) br_v17_pattern_element(pat,plen,off[bk],bk,e)
 for(p=0;p<BR_PARTS;p++){out->length[p]=br_pattern_track_field(&f,p,BR_V17_TRACK_ROW(BR_V17_TROW_seq_length));out->division[p]=br_pattern_track_field(&f,p,BR_V17_TRACK_ROW(BR_V17_TROW_seq_speed));}
 for(p=0;p<BR_SYNTH_PARTS;p++)for(i=0;i<BR_STEPS;i++){uint32_t w=BR_PS_PAT(0u,p*BR_STEPS+i),a=w&255u,v=w>>8;out->step[p][i]=a?(BR_NOTE_STEP(a-1u,v&127u)|(v&128u?BR_STEP_TIE:0u)):0u;w=BR_PS_PAT(2u,p*BR_STEPS+i);out->synth_length[p][i]=(uint16_t)(w&0x7ffu);out->synth_micro[p][i]=br_session_micro(w);out->synth_condition[p][i]=(uint8_t)BR_PS_PAT(3u,p*BR_STEPS+i);out->synth_note[p][i]=(uint8_t)BR_PS_PAT(4u,p*BR_STEPS+i);out->synth_velocity[p][i]=(uint8_t)BR_PS_PAT(5u,p*BR_STEPS+i);}
 for(i=0;i<BR_STEPS;i++){out->step[BR_DRUM_PART][i]=BR_PS_PAT(1u,i);for(l=0;l<BR_DRUM_LANES;l++){uint32_t e=i*BR_DRUM_LANES+l,w=BR_PS_PAT(6u,e);out->kit_length[i][l]=(uint16_t)(w&0x7ffu);out->kit_micro[i][l]=br_session_micro(w);out->kit_condition[i][l]=(uint8_t)BR_PS_PAT(7u,e);out->kit_velocity[i][l]=(uint8_t)BR_PS_PAT(8u,e);out->kit_note[i][l]=255u;out->kit_pitch[i][l]=(uint8_t)BR_PS_PAT(9u,e);out->kit_retrig[i][l]=(uint8_t)BR_PS_PAT(10u,e);}}
#undef BR_PS_PAT
 br_v17_clamps=saved;
 return 1;
}
int br_pattern_session_capture(uint8_t *out,size_t n)
{ if(!br_pattern_session_scratch||!out||n!=sizeof(br_session_t)||br_pattern_session_overlap(out,n,br_pattern_session_scratch,sizeof(br_session_t))||!br_capture(br_pattern_session_scratch))return 0;memcpy(out,br_pattern_session_scratch,sizeof(br_session_t));return 1; }
int br_pattern_session_validate(const uint8_t *in,size_t n,void *context)
{ (void)context;if(!br_pattern_session_scratch||!in||n!=sizeof(br_session_t)||br_pattern_session_overlap(in,n,br_pattern_session_scratch,sizeof(br_session_t)))return 0;memcpy(br_pattern_session_scratch,in,sizeof(br_session_t));return br_session_valid(br_pattern_session_scratch); }
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
