#include <stdint.h>
#include <string.h>
#include "../firmware/src/instrument_fx_progress.h"
uint32_t br_fx_init_test_calls;
uint32_t br_app_test_inject_fault_before_mutation;
#define FM1_TICKS_PER_US 24u
static struct { uint32_t last_ticks; } fm1_audio_metrics;
static uint32_t fm1_ticks(void) { static uint32_t tick; return ++tick; }
static void fm1_audio_metrics_snapshot(uint32_t out[8]) { memset(out, 0, 32u); }
static void fm1_audio_metrics_reset(void) { }
static void fm1_uac_diagnostic_snapshot(uint32_t out[19]) { memset(out, 0, 76u); out[0] = 1u; }
#define DINGHY_HOST 1
#define BR_SPLIT_AUDIO 1
#define BR_DIAGNOSTIC_TEST 1
#define main unused_host_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/src/instrument_audio_unit.c"
#include <assert.h>
#include <stdio.h>
#if BR_AUDIO_FAILURE_JOB_PROFILE_DIAG || BR_AUDIO_FAILURE_JOB_STAGE_PROFILE_DIAG
#error "Release protocol test requires both failure-job profiles disabled"
#endif

static uint32_t word(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int main(void)
{
    uint8_t reply[128], body[4] = {'B', 'R', 1u, 0u};
    uint32_t n;
    uint8_t page = 0u;
    br_app_init();
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_app_command(0x60u, 0, 0u, reply, &n) && n == 32u);
    assert(fm1_app_command(0x61u, body, 4u, reply, &n) && n == 1u);
#else
    assert(!fm1_app_command(0x60u, 0, 0u, reply, &n));
    assert(!fm1_app_command(0x61u, body, 4u, reply, &n));
#endif
    br_playing = 1u;
#if FM1_DEVELOPMENT_DIAGNOSTICS
    br_audio_late_drop_count = 4u;
    br_audio_partial_drop_count = 2u;
    br_audio_late_last_index = 7u;
    br_audio_late_last_count = 8u;
    br_audio_late_last_deadline = 1000u;
    br_audio_late_last_return = 999u;
    br_audio_late_last_sequence = 42u;
    br_audio_late_last_cutoff = 1001u;
    br_audio_late_last_core = 1u;
    assert(fm1_app_command(0xb2u, 0, 0u, reply, &n) && n == 68u);
    assert(word(reply) == 3u && word(reply + 4u) == 4u);
    assert(word(reply + 8u) == 7u && word(reply + 12u) == 8u);
    assert(word(reply + 16u) == 1000u && word(reply + 20u) == 999u);
    assert(word(reply + 24u) == 42u && word(reply + 44u) == 1001u);
    assert(word(reply + 48u) == 1u);
    assert(word(reply + 60u) == 0u && word(reply + 64u) == 2u);
    assert(br_playing == 1u && br_audio_late_drop_count == 4u);
#else
    assert(!fm1_app_command(0xb2u, 0, 0u, reply, &n));
#endif
    assert(!fm1_app_command(0x92u, &page, 1u, reply, &n));
    assert(br_playing == 1u);
    assert(!fm1_app_command(0xb2u, body, 1u, reply, &n));
    br_engine_pipeline_test_uncontained_fault_latch();
    assert(br_engine_pipeline_uncontained_fault_get());
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(fm1_app_command(0xb2u, 0, 0u, reply, &n) && n == 68u);
#else
    assert(!fm1_app_command(0xb2u, 0, 0u, reply, &n));
#endif
    assert(!fm1_app_command(0x61u, body, 4u, reply, &n));
#if FM1_DEVELOPMENT_DIAGNOSTICS
    assert(br_audio_late_drop_count == 4u && br_playing == 1u);
    br_audio_late_drop_reset();
    assert(br_audio_late_drop_count == 0u && br_audio_partial_drop_count == 0u);
#endif
    puts("PASS: diagnostic command selection and fault gate; disabled 0x92 refuses");
    return 0;
}
