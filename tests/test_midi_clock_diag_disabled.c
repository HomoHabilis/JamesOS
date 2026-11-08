#include <stdint.h>
#include <string.h>
#include "../firmware/src/instrument_fx_progress.h"
uint32_t br_fx_init_test_calls, br_app_test_inject_fault_before_mutation;
#define FM1_TICKS_PER_US 24u
static struct { uint32_t last_ticks; } fm1_audio_metrics;
static uint32_t fm1_ticks(void) { static uint32_t tick; return ++tick; }
static void fm1_audio_metrics_snapshot(uint32_t out[8]) { memset(out, 0, 32u); }
static void fm1_audio_metrics_reset(void) { }
static void fm1_uac_diagnostic_snapshot(uint32_t out[19]) { memset(out, 0, 76u); }
#define DINGHY_HOST 1
#define BR_SPLIT_AUDIO 1
#define BR_DIAGNOSTIC_TEST 1
#define main unused_host_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/src/instrument_audio_unit.c"
#include <assert.h>
#include <stdio.h>
int main(void) {
    uint8_t reply[128]; uint32_t n;
    br_app_init();
    assert(fm1_app_command(0xb3u,0,0u,reply,&n) && n==92u);
    assert(reply[0]==2u);
    for (uint32_t i=1u;i<n;++i) assert(reply[i]==0u);
    { uint8_t body[4]={'B','R',1u,1u};
      assert(!fm1_app_command(0xb4u,body,4u,reply,&n)); }
    puts("0xB3/0xB4 clock-disabled capability passed");
}
