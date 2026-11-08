#include <stdint.h>
#include <string.h>
#include "../firmware/src/instrument_fx_progress.h"
uint32_t br_fx_init_test_calls, br_app_test_inject_fault_before_mutation;
#define FM1_TICKS_PER_US 24u
static struct { uint32_t last_ticks; } fm1_audio_metrics;
static uint32_t fm1_ticks(void) { static uint32_t tick; return ++tick; }
static void fm1_audio_metrics_snapshot(uint32_t out[8]) { memset(out, 0, 32u); }
static void fm1_audio_metrics_reset(void) { }
static void fm1_uac_diagnostic_snapshot(uint32_t out[19]) { memset(out, 0, 76u); out[0] = 1u; }
#ifndef TEST_SPLIT
static void br_engine_source_mode_set(uint32_t x) { (void)x; }
static uint32_t br_engine_source_mode_get(void) { return 0; }
static void br_audio_source_cost_diagnostic_snapshot(uint32_t out[8]) { memset(out, 0, 32); }
#endif
#define BR_MIDI_CLOCK_EXPERIMENT 1
#define DINGHY_HOST 1
#ifdef TEST_SPLIT
#define BR_SPLIT_AUDIO 1
#endif
#define BR_DIAGNOSTIC_TEST 1
#define main unused_host_main
#include "../firmware/src/dinghy.c"
#undef main
#ifdef TEST_SPLIT
#include "../firmware/src/instrument_audio_unit.c"
#endif
#include <assert.h>
#include <stdio.h>
static uint32_t word(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static int send(uint8_t value, uint32_t *accepted)
{
    uint8_t body[4]={'B','R',1u,value}, reply[128]; uint32_t n=0;
    if (!fm1_app_command(0xb4u,body,4u,reply,&n)) return 0;
    assert(n==8u); *accepted=word(reply); assert(word(reply+4u)==br_midi_clock_enabled); return 1;
}
int main(void)
{
    uint8_t reply[128], body[4]={'B','R',1u,1u}; uint32_t n, accepted, gen;
    br_app_init();
    for (uint32_t i=0;i<BR_PARTS;++i) br_quiet_part[i]=1u;
    assert(br_engine_quiet() && br_fx_quiet());
    assert(!fm1_app_command(0xb4u,0,4u,reply,&n));
    assert(!fm1_app_command(0xb4u,body,3u,reply,&n));
    body[0]='X'; assert(!fm1_app_command(0xb4u,body,4u,reply,&n)); body[0]='B';
    body[2]=2; assert(!fm1_app_command(0xb4u,body,4u,reply,&n)); body[2]=1;
    body[3]=2; assert(!fm1_app_command(0xb4u,body,4u,reply,&n));
    assert(!br_midi_clock_enabled); gen=br_midi_clock_generation;
    br_playing=1; assert(send(1,&accepted) && !accepted && !br_midi_clock_enabled && gen==br_midi_clock_generation); br_playing=0;
    br_diagnostic_armed=1; assert(send(1,&accepted) && !accepted); br_diagnostic_armed=0;
    br_diagnostic_muted=1; assert(send(1,&accepted) && !accepted); br_diagnostic_muted=0;
    br_diagnostic_abort=1; assert(send(1,&accepted) && !accepted); br_diagnostic_abort=0;
    fm1_in.notes=1; assert(send(1,&accepted) && !accepted); fm1_in.notes=0;
    fm1_in.buttons=1; assert(send(1,&accepted) && !accepted); fm1_in.buttons=0;
    br_ui.held_note[0]=61; assert(send(1,&accepted) && !accepted); br_ui.held_note[0]=0;
    br_midi_routes.note[0].channel=1; assert(send(1,&accepted) && !accepted); br_midi_routes.note[0].channel=0;
    br_arp_keys[0].count=1; assert(send(1,&accepted) && !accepted); br_arp_keys[0].count=0;
    br_quiet_part[0]=0; assert(send(1,&accepted) && !accepted); br_quiet_part[0]=1;
    assert(send(1,&accepted) && accepted && br_midi_clock_enabled==1u && br_midi_clock_generation==gen+1u);
    assert(send(1,&accepted) && accepted && br_midi_clock_generation==gen+1u);
    assert(send(0,&accepted) && accepted && !br_midi_clock_enabled && br_midi_clock_generation==gen+2u);
#ifdef TEST_SPLIT
    br_app_test_inject_fault_before_mutation=1u;
    assert(!send(1,&accepted));
    assert(!br_midi_clock_enabled && br_midi_clock_generation==gen+2u);
#endif
    puts("0xB4 guarded RAM-only MIDI clock control passed"); return 0;
}
