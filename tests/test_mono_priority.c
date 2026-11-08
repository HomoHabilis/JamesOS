#define DINGHY_HOST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
static void audio(void) { int32_t out[BR_BLOCK*2u] = {0}; assert(app_audio(out, BR_BLOCK) == FM1_AUDIO_RENDER_OK); }
static void on(uint32_t p, uint32_t n, uint32_t v) { br_engine_note_on(p,n,v); audio(); }
static void off(uint32_t p, uint32_t n) { br_engine_note_off(p,n); audio(); }
int main(void) {
 br_app_init(); fm1_ms=2000u;
 on(0,60,40); assert(br_env[0].note==60);
 on(0,64,90); assert(br_env[0].note==64);
 off(0,64); assert(br_env[0].note==60 && br_env[0].velocity==40 && br_env[0].stage != BR_ENV_RELEASE);
 off(0,60); assert(br_env[0].stage==BR_ENV_RELEASE || br_env[0].stage==BR_ENV_OFF);
 on(0,60,40); on(0,64,90); on(0,67,110); off(0,64);
 assert(br_env[0].note==67); off(0,67); assert(br_env[0].note==60);
 on(0,64,90); br_start_note(0,72,100,0); off(0,64); assert(br_env[0].note==60);
 br_env[0].stage=BR_ENV_OFF; on(0,67,110); br_env[0].stage=BR_ENV_OFF; off(0,67); assert(br_env[0].note==60);
 on(1,55,80); on(0,62,70); off(0,62); assert(br_env[0].note==60 && br_env[1].note==55);
 on(0,60,100); off(0,60); assert(br_env[0].note==60 && br_env[0].stage!=BR_ENV_RELEASE);
 off(0,60); assert(br_env[0].stage==BR_ENV_RELEASE || br_env[0].stage==BR_ENV_OFF);
 br_engine_part_off(1); audio(); assert(br_live_count[1]==0);
 br_midi(0x90,48,33); audio(); br_midi(0x90,52,99); audio(); br_midi(0x80,52,0); audio();
 assert(br_env[0].note==48 && br_env[0].velocity==33);
 br_midi(0x80,48,0); audio();
 br_ui.page=BR_PAGE_HOME; br_ui.part=0; br_arp_on=0; br_hold_on=0;
 br_key_down(0,1); audio(); {uint32_t n=br_env[0].note; br_key_down(2,1); audio(); br_key_up(2); audio(); assert(br_env[0].note==n); br_key_up(0); audio();}
 puts("PASS: mono last-note priority, duplicates, reset, MIDI and keyboard"); return 0;
}
