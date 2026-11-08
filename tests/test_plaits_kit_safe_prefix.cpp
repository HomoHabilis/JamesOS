#include "../firmware/plaits/plaits_kit_c_api.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" {
void ref_plaits_kit_init(void*,uint32_t);
void ref_plaits_kit_note_on_voice_pitch_mask(void*,uint32_t,uint32_t,uint32_t,uint32_t,int32_t);
void ref_plaits_kit_note_off_mask(void*,uint32_t,uint32_t);
void plaits_kit_deadline_test_set_choke(void*,uint32_t,uint32_t);
void ref_plaits_kit_deadline_test_set_choke(void*,uint32_t,uint32_t);
uint32_t ref_plaits_kit_render_keyed_mask_counted_by_lane(void*,const br_sound_t*,const uint32_t*,const uint32_t*,uint32_t,int32_t*,int32_t*,uint32_t,uint8_t*);
void ref_plaits_kit_test_metadata(const void*,uint32_t*);
int plaits_kit_test_state_equal_except_nonrendered_phase(const void*,const void*,uint32_t*,uint32_t*,uint32_t*,uint32_t*);
void plaits_kit_test_metadata(const void*,uint32_t*);
}
struct alignas(8) Buffer { unsigned char b[PLAITS_KIT_STATE_BYTES]; };
static uint32_t rnd=947u;
static uint32_t next() {rnd=rnd*1664525u+1013904223u;return rnd;}
int main() {
 static const uint32_t lengths[]={0,1,7,32,63,64,127,128,255,256};
 static const uint32_t notes[]={36,38,42,49};
 unsigned sounding=0,active=0, fading=0, choked=0, key_nonzero=0, gate_cross=0, choke_cross=0; uint32_t voices=0;
 for (unsigned scenario=0;scenario<64;scenario++) {
  Buffer a={},b={},live={}; uint32_t seed=next();
  plaits_kit_init(live.b,seed); memcpy(a.b,live.b,sizeof live.b);memcpy(b.b,live.b,sizeof live.b);
  br_sound_t sound={};sound.level=127;sound.gate=128;sound.timbre=64;sound.color=64;sound.plaits_harmonics=64;
  uint32_t levels[4]={127,100,80,55},gates[4]={0,0,0,0};
  for(unsigned block=0;block<150;block++) {
   uint32_t random=next(),lane=(random>>4)&3u,mask=(random>>12)&15u;
   if(!mask)mask=1u<<lane;
   if((random&3u)==0) {
    uint32_t voice=(random>>17)%BR_DRUM_VOICE_COUNT,vel=1u+((random>>22)%127u);voices|=1u<<voice;int32_t pitch=(int32_t)((random>>9)%25u)-12;
    memcpy(live.b,a.b,sizeof live.b);plaits_kit_note_on_voice_pitch_mask(live.b,notes[lane],vel,1u<<lane,voice,pitch);memcpy(a.b,live.b,sizeof live.b);
    memcpy(live.b,b.b,sizeof live.b);ref_plaits_kit_note_on_voice_pitch_mask(live.b,notes[lane],vel,1u<<lane,voice,pitch);memcpy(b.b,live.b,sizeof live.b);
   }
   if((random&31u)==1u) {memcpy(live.b,a.b,sizeof live.b);plaits_kit_note_off_mask(live.b,notes[lane],1u<<lane);memcpy(a.b,live.b,sizeof live.b);memcpy(live.b,b.b,sizeof live.b);ref_plaits_kit_note_off_mask(live.b,notes[lane],1u<<lane);memcpy(b.b,live.b,sizeof live.b);}
   if((random&127u)==4u) {
    uint32_t remaining=(random>>17)%130u;
    memcpy(live.b,a.b,sizeof live.b);plaits_kit_deadline_test_set_choke(live.b,lane,remaining);memcpy(a.b,live.b,sizeof live.b);
    memcpy(live.b,b.b,sizeof live.b);ref_plaits_kit_deadline_test_set_choke(live.b,lane,remaining);memcpy(b.b,live.b,sizeof live.b);
   }
   if((random&7u)==2u) gates[lane]=((random>>16)&1u)?0u:1u+((random>>20)%400u);
   sound.drum_voice_mask=(random>>16)&255u;sound.drum_key_lane=(random>>9)&3u;
   sound.timbre=(random>>8)&127u;sound.color=(random>>15)&127u;sound.plaits_harmonics=(random>>22)&127u;
   unsigned n=lengths[(random>>24)%10u];int32_t x[256]={},y[256]={},kx[256]={},ky[256]={};uint8_t ux[4]={},uy[4]={};
   bool haskey=(random&0x80u)!=0;
   if((block%7u)==0 && n>=8u) {
    uint32_t state[82]={};memcpy(live.b,a.b,sizeof live.b);plaits_kit_test_metadata(live.b,state);
    for(unsigned l=0;l<4;l++) if((mask&(1u<<l)) && state[2+20*l+1]) {
      plaits_kit_deadline_test_set_choke(live.b,l,n/2);memcpy(a.b,live.b,sizeof live.b);
      memcpy(live.b,b.b,sizeof live.b);ref_plaits_kit_deadline_test_set_choke(live.b,l,n/2);memcpy(b.b,live.b,sizeof live.b);break;
    }
   }
   memcpy(live.b,a.b,sizeof live.b);
   { uint32_t before[82]={};plaits_kit_test_metadata(live.b,before);
     for(unsigned l=0;l<4;l++) if((mask & (1u<<l)) && before[2+20*l+1]) {
       uint32_t age=before[2+20*l+8], gate=before[2+20*l+14];
       if(gates[l] && (!gate || gates[l]<gate)) gate=gates[l];
       if(gate>age && gate-age<n && !before[2+20*l+15]) ++gate_cross;
       if(before[2+20*l+13] && before[2+20*l+12] && before[2+20*l+12]<n) ++choke_cross;
     }
   }
   memcpy(live.b,a.b,sizeof live.b);
   uint32_t ca=plaits_kit_render_keyed_mask_counted_by_lane(live.b,&sound,levels,gates,mask,x,haskey?kx:nullptr,n,ux);memcpy(a.b,live.b,sizeof live.b);
   memcpy(live.b,b.b,sizeof live.b);
   uint32_t cb=ref_plaits_kit_render_keyed_mask_counted_by_lane(live.b,&sound,levels,gates,mask,y,haskey?ky:nullptr,n,uy);memcpy(b.b,live.b,sizeof live.b);
   for(unsigned i=0;i<n;i++) {if(x[i]) ++sounding;if(kx[i])++key_nonzero;}
   uint32_t md[82]={}; plaits_kit_test_metadata(live.b,md);
   for(unsigned l=0;l<4;l++) {active+=md[2+20*l+1]!=0;fading+=md[2+20*l+15]!=0;choked+=md[2+20*l+13]!=0;}
   if(ca!=cb || memcmp(ux,uy,4) || memcmp(x,y,sizeof x) || memcmp(kx,ky,sizeof kx)) {
     std::fprintf(stderr,"PCM mismatch scenario=%u block=%u n=%u mask=%u\n",scenario,block,n,mask);return 1;
   }
   if(memcmp(a.b,b.b,sizeof a.b)) {
     size_t off=0;while(off<sizeof a.b && a.b[off]==b.b[off])++off;
     std::fprintf(stderr,"state mismatch scenario=%u block=%u offset=%zu\n",scenario,block,off);return 1;
   }
  }
 }
 std::printf("coverage: sounding=%u key=%u active=%u fade=%u choked=%u gate-cross=%u choke-cross=%u\n",sounding,key_nonzero,active,fading,choked,gate_cross,choke_cross);
 assert(sounding && key_nonzero && active && fading && choked && gate_cross && choke_cross);
 assert(voices == ((1u<<BR_DRUM_VOICE_COUNT)-1u));
 std::puts("Plaits kit fast-prefix reference: 9600 exact PCM/key/state steps passed");
}
