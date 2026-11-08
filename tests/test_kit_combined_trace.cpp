#include "../firmware/plaits/plaits_kit_c_api.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" {
void plaits_kit_deadline_test_set_choke(void*,uint32_t,uint32_t);
void plaits_kit_test_metadata(const void*,uint32_t*);
}
struct alignas(8) Buffer { unsigned char b[PLAITS_KIT_STATE_BYTES]; };
static uint32_t rnd=947u; static uint32_t next(){rnd=rnd*1664525u+1013904223u;return rnd;}
int main(){
 static const uint32_t lengths[]={0,1,7,32,63,64,127,128,255,256},notes[]={36,38,42,49};
 for(unsigned scenario=0;scenario<64;scenario++){
  Buffer state={};plaits_kit_init(state.b,next()); br_sound_t sound={};
  sound.level=127;sound.gate=128;sound.timbre=64;sound.color=64;sound.plaits_harmonics=64;
  uint32_t levels[4]={127,100,80,55},gates[4]={};
  for(unsigned block=0;block<150;block++){
   uint32_t r=next(),lane=(r>>4)&3u,mask=(r>>12)&15u;if(!mask)mask=1u<<lane;
   if((r&3u)==0) plaits_kit_note_on_voice_pitch_mask(state.b,notes[lane],1u+((r>>22)%127u),1u<<lane,(r>>17)%BR_DRUM_VOICE_COUNT,(int32_t)((r>>9)%25u)-12);
   if((r&31u)==1u) plaits_kit_note_off_mask(state.b,notes[lane],1u<<lane);
   if((r&127u)==4u) plaits_kit_deadline_test_set_choke(state.b,lane,(r>>17)%130u);
   if((r&7u)==2u) gates[lane]=((r>>16)&1u)?0u:1u+((r>>20)%400u);
   sound.drum_voice_mask=(r>>16)&255u;sound.drum_key_lane=(r>>9)&3u;
   sound.timbre=(r>>8)&127u;sound.color=(r>>15)&127u;sound.plaits_harmonics=(r>>22)&127u;
   uint32_t n=lengths[(r>>24)%10u]; int32_t out[256]={}, key[256]={};uint8_t counts[4]={};uint32_t md[82]={};
   uint32_t result=plaits_kit_render_keyed_mask_counted_by_lane(state.b,&sound,levels,gates,mask,out,(r&0x80u)?key:nullptr,n,counts);
   plaits_kit_test_metadata(state.b,md);
   if(fwrite(&result,sizeof(result),1,stdout)!=1 || fwrite(counts,sizeof(counts),1,stdout)!=1 ||
      fwrite(out,sizeof(out),1,stdout)!=1 || fwrite(key,sizeof(key),1,stdout)!=1 || fwrite(md,sizeof(md),1,stdout)!=1) return 1;
  }
 }
 return 0;
}
