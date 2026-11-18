/* SPDX-License-Identifier: GPL-3.0-only */
#include "sound_record.h"
#include <string.h>

/* Field order is the v1 wire contract. Add future fields only in a new version. */
#define SOUND_FIELDS(X) \
 X(sound.preset,0,76) X(sound.timbre,0,127) X(sound.color,0,127) \
 X(sound.tune,-24,24) X(sound.attack,0,127) X(sound.decay,0,127) \
 X(sound.sustain,0,127) X(sound.release,0,127) X(sound.drive,0,127) \
 X(sound.delay,0,127) X(sound.reverb,0,127) X(sound.filter,0,127) \
 X(sound.filter_resonance,0,127) X(sound.filter_keytrack,0,127) \
 X(sound.filter_env_depth,-127,127) X(sound.timbre_mod,0,127) \
 X(sound.fm_mod,0,127) X(sound.morph_mod,0,127) X(sound.drum_snap,0,127) \
 X(sound.plaits_harmonics,0,127) X(sound.plaits_aux,0,127) \
 X(sound.gate,0,128) X(sound.drum_voice_mask,0,255) \
 X(mod.attack,0,127) X(mod.decay,0,127) X(mod.sustain,0,127) X(mod.release,0,127) \
 X(mod.amount,-127,127) X(mod.target,0,BR_TARGET_COUNT-1) X(mod.loop,0,1) X(mod.retrigger,0,1) \
 X(fx.drive_mode,0,BR_DRIVE_COUNT-1) X(fx.drive_gain,0,127) X(fx.drive_tone,0,127) \
 X(fx.delay_engine,0,BR_DELAY_COUNT-1) X(fx.delay_time,0,127) X(fx.delay_feedback,0,127) \
 X(fx.mod_engine,0,BR_MOD_COUNT-1) X(fx.mod_rate,0,127) X(fx.mod_depth,0,127) X(fx.mod_mix,0,127)
#define LFO_FIELDS(X,i) X(mod.lfo[i].wave,0,BR_WAVE_COUNT-1) X(mod.lfo[i].rate,0,127) \
 X(mod.lfo[i].depth,0,127) X(mod.lfo[i].target,0,BR_TARGET_COUNT-1)
#define ROUTE_FIELDS(X,i) X(mod.route[i].source,0,BR_SOURCE_COUNT-1) \
 X(mod.route[i].target,0,BR_TARGET_COUNT-1) X(mod.route[i].amount,-127,127) X(mod.route[i].enabled,0,1)
#define ALL_FIELDS(X) SOUND_FIELDS(X) LFO_FIELDS(X,0) LFO_FIELDS(X,1) \
 ROUTE_FIELDS(X,0) ROUTE_FIELDS(X,1) ROUTE_FIELDS(X,2) ROUTE_FIELDS(X,3) \
 ROUTE_FIELDS(X,4) ROUTE_FIELDS(X,5) ROUTE_FIELDS(X,6) ROUTE_FIELDS(X,7)
#define COUNT(f,lo,hi) +1
enum { SOUND_FIELD_COUNT = 0 ALL_FIELDS(COUNT) };
_Static_assert(SOUND_RECORD_BYTES == 12u + 4u*SOUND_FIELD_COUNT, "record size mismatch");
static void put32(uint8_t *p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c=0xffffffffu, i;
    while (n--) { c ^= *p++; for (i=0;i<8;i++) c=(c>>1)^((0u-(c&1u))&0xedb88320u); }
    return ~c;
}
static int valid(const sound_record_state *s) {
#define CHECK(f,lo,hi) if ((int64_t)s->f < (int64_t)(lo) || (int64_t)s->f > (int64_t)(hi)) return 0;
    ALL_FIELDS(CHECK)
#undef CHECK
    if ((s->sound.drum_voice_mask & 3u) == 3u ||
        ((s->sound.drum_voice_mask >> 2) & 3u) == 3u ||
        ((s->sound.drum_voice_mask >> 4) & 3u) == 3u ||
        ((s->sound.drum_voice_mask >> 6) & 3u) == 3u) return 0;
    return s->sound.preset == BR_KIT_BRAIDS || s->sound.preset == BR_KIT_PLAITS ||
           (s->sound.preset >= BR_FIRST_PLAITS && s->sound.preset < BR_FIRST_PLAITS + BR_PLAITS_MODEL_COUNT);
}
int sound_record_encode(uint8_t *out, const sound_record_state *s) {
    uint8_t tmp[SOUND_RECORD_BYTES]; uint32_t off=8;
    if (!out || !s || !valid(s)) return -1;
    tmp[0]='F'; tmp[1]='M'; tmp[2]='S'; tmp[3]='D'; put32(tmp+4,SOUND_RECORD_VERSION);
#define WRITE(f,lo,hi) put32(tmp+off,(uint32_t)s->f); off+=4;
    ALL_FIELDS(WRITE)
#undef WRITE
    put32(tmp+off,crc32(tmp,off)); memcpy(out,tmp,sizeof tmp); return 0;
}
int sound_record_decode(sound_record_state *out, const uint8_t *p, uint32_t size) {
    sound_record_state tmp; uint32_t off=8;
    if (!out || !p || size!=SOUND_RECORD_BYTES || memcmp(p,"FMSD",4) ||
        get32(p+4)!=SOUND_RECORD_VERSION || get32(p+size-4)!=crc32(p,size-4)) return -1;
    memset(&tmp,0,sizeof tmp);
/* Convert the two's-complement wire word through int64_t so malformed input
 * never relies on an implementation-defined unsigned-to-signed conversion. */
#define READ(f,lo,hi) do { uint32_t w=get32(p+off); int64_t v=w; off+=4; \
    if (w & 0x80000000u) v-=4294967296LL; \
    if (v<(int64_t)(lo) || v>(int64_t)(hi)) return -1; tmp.f=v; } while (0);
    ALL_FIELDS(READ)
#undef READ
    if (!valid(&tmp)) return -1;
    *out=tmp; return 0;
}
void sound_record_capture(sound_record_state *out, const volatile br_part_t *part) {
    if (!out || !part) return;
    memset(out,0,sizeof *out);
#define CAPTURE(f,lo,hi) out->f=part->f;
    ALL_FIELDS(CAPTURE)
#undef CAPTURE
}
int sound_record_apply(volatile br_part_t *part, uint32_t part_index, const sound_record_state *state) {
    if (!part || !state || !valid(state) || !br_preset_allowed_on_part(part_index, state->sound.preset)) return -1;
#define APPLY(f,lo,hi) part->f=state->f;
    ALL_FIELDS(APPLY)
#undef APPLY
    return 0;
}
