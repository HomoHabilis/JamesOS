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
static void read_diag(uint32_t out[23])
{
    uint8_t reply[128]; uint32_t n;
    assert(fm1_app_command(0xb3u, 0, 0u, reply, &n) && n == 92u);
    for (uint32_t i=0;i<23;++i) out[i]=word(reply+i*4u);
}
int main(void)
{
    uint32_t d[23], n; uint8_t reply[128], bad=0;
    br_app_init(); br_midi_clock_enabled=1u;
    read_diag(d); assert(d[0]==2u && d[1]==1u && d[12]==1u);
    for (uint32_t i=2;i<8;++i) assert(d[i]==0u);
    assert(!fm1_app_command(0xb3u, &bad, 1u, reply, &n));
    br_engine_clock_dma_edge(1000000u, 0u, 256u);
    assert(br_clock_event(0xf2u,1000000u));
    read_diag(d); assert(d[6]==1u && d[7]==BR_CLOCK_FAIL_SPP);
    br_engine_clock_dma_invalid();
    read_diag(d); assert(d[4]==1u && d[7]==BR_CLOCK_FAIL_ANCHOR);
    br_engine_clock_dma_edge(2000000u,0u,256u);
    br_engine_clock_dma_edge(2001000u,0u,256u); /* repeated half => invalid */
    read_diag(d); assert(d[4]==2u);
    br_engine_clock_dma_edge(3000000u,0u,256u);
    assert(br_clock_event(0xf8u,3000000u));
    assert(br_clock_event(0xf8u,3000000u)); /* duplicate timestamp */
    read_diag(d); assert(d[2]==1u && d[7]==BR_CLOCK_FAIL_INVALID);
    assert(d[13]==2u && d[14]==1u && d[15]==0u && d[16]==1u && d[17]==0u);
    br_clock_experiment.anchor.valid=0u;
    br_engine_clock_dma_edge(4000000u,0u,256u);
    assert(br_clock_event(0xf8u,4000000u));
    assert(!br_clock_event(0xf8u,4250340u)); /* future event must not count twice */
    read_diag(d); assert(d[13]==3u && d[16]==1u);
    br_clock_experiment.render_frame+=512u;
    assert(br_clock_event(0xf8u,4250340u));
    read_diag(d);
    assert(d[13]==4u && d[14]==1u && d[15]==0u && d[16]==2u);
    assert(d[17]==460u && d[18]==460u && d[19]==0u && d[20]==460u);
    assert(d[21]==(460u<<16) && d[22]==0u);
    assert(br_clock_event(0xf8u,4304761u)); /* 100-frame early pulse: explicit REJECTED */
    read_diag(d);
    assert(d[13]==5u && d[14]==2u && d[2]==1u);
    assert(d[16]==3u && d[17]==560u && d[18]==100u);
    br_clock_experiment.follower.have_tick=1u;
    br_clock_experiment.follower.last_tick=0u;
    br_clock_experiment.follower.now=0u;
    br_clock_experiment.follower.period_q16=(uint64_t)1000u<<16;
    br_clock_experiment.render_frame=FS;
    br_clock_project();
    read_diag(d); assert(d[3]==1u && d[7]==BR_CLOCK_FAIL_LOST);
    br_midi_transport_overflow=1u; br_take_events();
    read_diag(d); assert(d[5]==1u && d[7]==BR_CLOCK_FAIL_OVERFLOW);
    br_midi_clock_active=1u; br_clock_experiment.enabled=1u; br_midi_clock_bpm=123u; br_playing=1u;
    read_diag(d); assert(d[8]==1u && d[9]==1u && d[10]==123u && d[11]==1u && d[12]==1u);
    read_diag(d); assert(d[2]==1u && d[3]==1u && d[4]==2u && d[5]==1u && d[6]==1u);
#ifdef TEST_SPLIT
    br_engine_pipeline_test_uncontained_fault_latch();
    read_diag(d); assert(d[4]==2u);
#endif
    puts("0xB3 clock diagnostic read-only and failure paths passed");
    return 0;
}
