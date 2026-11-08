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

static void check_status(uint8_t expected)
{
    uint8_t reply[128], body = 0;
    uint32_t n;
    assert(fm1_app_command(0xA0u, 0, 0u, reply, &n));
    assert(n == 4u && reply[0] == 'B' && reply[1] == 'R' &&
           reply[2] == 1u && reply[3] == expected);
    assert(!fm1_app_command(0xA0u, &body, 1u, reply, &n));
}

static void upload(const br_session_t *session)
{
    uint8_t reply[128], body[34], commit[4] = {'B', 'R', 1u, 0u};
    const uint8_t *data = (const uint8_t *)session;
    uint32_t offset, n;
    for (offset = 0; offset < sizeof(*session); offset += 32u) {
        uint32_t count = sizeof(*session) - offset;
        if (count > 32u) count = 32u;
        body[0] = (uint8_t)offset; body[1] = (uint8_t)(offset >> 8);
        memcpy(body + 2u, data + offset, count);
        assert(fm1_app_command(0x64u, body, count + 2u, reply, &n));
    }
    assert(fm1_app_command(0x65u, commit, sizeof commit, reply, &n));
    assert(n == 1u && reply[0] == 1u);
}

int main(void)
{
    br_session_t session;
    br_app_init();
#if BR_FX_GLOBAL_REVERB
    /* The audio planner must still receive the measured reverb-job cost. */
#if FM1_DEVELOPMENT_DIAGNOSTICS
    br_gr_stats[2] = 1234u;
#else
    br_gr_cost_ticks = 1234u;
#endif
    assert(br_fx_shared_reverb_cost() == 1234u);
    br_fx_init();
    assert(br_fx_shared_reverb_cost() == 0u);
#endif
    {
        uint8_t reply[128], workload[4] = {0xf4u, 1u, 0u, 0u};
        uint32_t n;
        int accepted = fm1_app_command(0x66u, workload, sizeof workload, reply, &n);
#if FM1_DEVELOPMENT_DIAGNOSTICS
        assert(accepted && n == 1u && br_diagnostic_armed);
        br_diagnostic_armed = 0u;
        assert(fm1_app_command(0x69u, 0, 0u, reply, &n) && n == 16u);
        assert(fm1_app_command(0x8cu, 0, 0u, reply, &n) && n == 76u);
        assert(fm1_app_command(0x8au, 0, 0u, reply, &n) && n == 76u);
        assert(fm1_app_command(0x8bu, 0, 0u, reply, &n) && n == 36u);
        assert(fm1_app_command(0xb3u, 0, 0u, reply, &n) && n == 92u);
#else
        assert(!accepted && !br_diagnostic_armed);
        assert(!fm1_app_command(0x69u, 0, 0u, reply, &n));
        assert(!fm1_app_command(0x8cu, 0, 0u, reply, &n));
        assert(!fm1_app_command(0x8au, 0, 0u, reply, &n));
        assert(!fm1_app_command(0x8bu, 0, 0u, reply, &n));
        assert(!fm1_app_command(0xb3u, 0, 0u, reply, &n));
#endif
        assert(fm1_app_command(0x62u, 0, 0u, reply, &n) && n == 24u);
        assert(fm1_app_command(0x68u, 0, 0u, reply, &n) && n == 12u);
        { uint8_t read_first[] = {0, 0, 32};
          assert(fm1_app_command(0x63u, read_first, sizeof read_first, reply, &n) && n == 32u); }
    }
    assert(br_capture(&session));
    check_status(0u);
    br_legacy_migration_pending = 1u;
    br_legacy_raw_length = 968u; br_legacy_raw_version = 10u;
    check_status(1u);
    assert(br_legacy_migration_pending && br_legacy_raw_length == 968u);
    upload(&session);
    check_status(0u); /* Existing converted-session migration path. */
    /* Exercise the real loader's truncated-record detection. */
    assert(!settings_save(&session, sizeof session - 1u));
    br_load_session();
    check_status(2u);
    upload(&session);
    check_status(2u); /* Routine upload must not release invalid-record lock. */
    assert(br_session_save_blocked());
    br_legacy_migration_pending = 1u;
    check_status(3u);
    puts("PASS: session protection diagnostic and upload locks");
    return 0;
}
