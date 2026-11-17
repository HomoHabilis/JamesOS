/* Host-only sound record tests. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sound_record.h"

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c=~0u, i; while(n--) { c^=*p++; for(i=0;i<8;i++) c=(c>>1)^((0u-(c&1u))&0xedb88320u); } return ~c;
}
static void put32(uint8_t *p,uint32_t x) { p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24; }
static void seal(uint8_t *p) { put32(p+SOUND_RECORD_BYTES-4,crc32(p,SOUND_RECORD_BYTES-4)); }
int main(void) {
    br_part_t src={0}, dest={0}, before; sound_record_state a,b,old;
    uint8_t record[SOUND_RECORD_BYTES], again[SOUND_RECORD_BYTES]; unsigned i;
    src.sound.preset=BR_FIRST_PLAITS+5; src.sound.tune=-24;
    src.sound.filter_env_depth=-127; src.sound.gate=128;
    src.sound.level=15; src.sound.pan=12; src.sound.mute=1;
    src.mod.amount=-127; src.mod.loop=1; src.mod.retrigger=1;
    src.mod.lfo[1].wave=BR_WAVE_RANDOM; src.mod.lfo[1].target=BR_TARGET_LFO2_RATE;
    src.mod.route[7].source=BR_SOURCE_VELOCITY; src.mod.route[7].target=BR_TARGET_TIMBRE;
    src.mod.route[7].amount=127; src.mod.route[7].enabled=1;
    src.fx.delay_engine=BR_DELAY_GLITCH; src.fx.mod_mix=127;
    sound_record_capture(&a,&src); assert(sound_record_encode(record,&a)==0);
    assert(sound_record_decode(&b,record,sizeof record)==0);
    assert(sound_record_encode(again,&b)==0 && !memcmp(record,again,sizeof record));
    dest.sound.level=110; dest.sound.pan=60; dest.sound.mute=1;
    dest.step[0]=0x1234; dest.lane_level[0]=77; dest.revision=4;
    assert(sound_record_apply(&dest,0,&b)==0);
    assert(dest.sound.tune==-24 && dest.sound.filter_env_depth==-127);
    assert(dest.mod.route[7].amount==127 && dest.mod.lfo[1].wave==BR_WAVE_RANDOM);
    assert(dest.fx.delay_engine==BR_DELAY_GLITCH && dest.fx.mod_mix==127);
    assert(dest.sound.level==110 && dest.sound.pan==60 && dest.sound.mute==1);
    assert(dest.step[0]==0x1234 && dest.lane_level[0]==77 && dest.revision==4);
    before=dest; assert(sound_record_apply(&dest,BR_DRUM_PART,&b)==-1); assert(!memcmp(&before,&dest,sizeof dest));
    src.sound.preset=BR_KIT_BRAIDS; src.sound.drum_voice_mask=0x98; src.sound.drum_snap=117;
    sound_record_capture(&a,&src); assert(!sound_record_encode(record,&a));
    assert(!sound_record_decode(&b,record,sizeof record)); assert(!sound_record_apply(&dest,BR_DRUM_PART,&b));
    assert(dest.sound.drum_voice_mask==0x98 && dest.sound.drum_snap==117);
    old=b;
    for (i=0;i<SOUND_RECORD_BYTES;i++) { memcpy(again,record,sizeof record); again[i]^=1; assert(sound_record_decode(&b,again,sizeof again)==-1); assert(!memcmp(&b,&old,sizeof b)); }
    assert(sound_record_decode(&b,record,sizeof record-1)==-1);
    memcpy(again,record,sizeof record); put32(again+8,51); seal(again); assert(sound_record_decode(&b,again,sizeof again)==-1);
    memcpy(again,record,sizeof record); put32(again+12,128); seal(again); assert(sound_record_decode(&b,again,sizeof again)==-1);
    memcpy(again,record,sizeof record); put32(again+8+79*4,128); seal(again); assert(sound_record_decode(&b,again,sizeof again)==-1);
    memcpy(again,record,sizeof record); put32(again+8+22*4,3); seal(again); assert(sound_record_decode(&b,again,sizeof again)==-1);
    memcpy(again,record,sizeof record); put32(again+4,2); seal(again); assert(sound_record_decode(&b,again,sizeof again)==-1);
    memset(again,0x5a,sizeof again); a.mod.amount=128; assert(sound_record_encode(again,&a)==-1);
    for (i=0;i<sizeof again;i++) assert(again[i]==0x5a);
    puts("sound record tests passed"); return 0;
}
